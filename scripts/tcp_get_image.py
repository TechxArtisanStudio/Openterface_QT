#!/usr/bin/env python3
"""
Openterface TCP Client - Image Retrieval Script
================================================
Communicates with the Openterface QT TCP server to retrieve images.

TCP Commands and when to use them:
  gettargetscreen - [RECOMMENDED] Grab the current live camera frame directly.
                    Works immediately, no prior action needed.
  lastimage       - Retrieve the last image that was SAVED TO DISK by the app.
                    Requires a prior FullScreenCapture (or UI button) to have run.
                    Returns an error if no image was saved yet.
  checkstatus     - Poll execution status of the last script command.
  screenshot      - [COMPOSITE] Send FullScreenCapture script → wait for finish
                    → retrieve saved image via lastimage.  Fully automatic.

Response JSON schema:
  {
    "type":      "image" | "screen" | "status" | "error",
    "status":    "success" | "error" | "warning" | "pending",
    "timestamp": "<ISO-8601>",
    "data": {
      "content":  "<base64-encoded image bytes>",
      "format":   "raw" | "jpeg",
      "encoding": "base64",        # screen only
      "size":     <int>,
      "width":    <int>,           # screen only
      "height":   <int>            # screen only
    },
    "message": "<str>"             # error responses
  }

Usage examples:
  python tcp_get_image.py                            # default: gettargetscreen
  python tcp_get_image.py --cmd gettargetscreen      # current live frame (recommended)
  python tcp_get_image.py --cmd screenshot           # capture + save + retrieve automatically
  python tcp_get_image.py --cmd lastimage            # last previously saved image
  python tcp_get_image.py --host 192.168.1.10 --port 4896 --cmd gettargetscreen --output frame.jpg
  python tcp_get_image.py --loop --interval 1.0      # continuous capture every 1 second
"""

import argparse
import base64
import json
import os
import socket
import sys
import time
from datetime import datetime


# ──────────────────────────────────────────────────────────────────────────────
# Core client
# ──────────────────────────────────────────────────────────────────────────────

def send_command(host: str, port: int, command: str, timeout: float = 10.0) -> dict:
    """
    Open a TCP connection, send *command*, read the full JSON response, and
    return it as a dict.

    The server sends one JSON object per command then keeps the connection
    alive; we read until the JSON is complete (balanced braces) or the socket
    is closed.
    """
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(command.encode("utf-8"))

        chunks = []
        brace_depth = 0
        started = False

        while True:
            try:
                chunk = sock.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            chunks.append(chunk)
            # Track JSON brace depth so we stop as soon as the root object ends
            for byte in chunk:
                ch = chr(byte)
                if ch == "{":
                    brace_depth += 1
                    started = True
                elif ch == "}":
                    brace_depth -= 1
            if started and brace_depth == 0:
                break

    raw = b"".join(chunks).decode("utf-8", errors="replace").strip()
    if not raw:
        raise ValueError("Server returned an empty response.")
    return json.loads(raw)


# ──────────────────────────────────────────────────────────────────────────────
# Response parsing
# ──────────────────────────────────────────────────────────────────────────────

def extract_image_bytes(response: dict) -> bytes:
    """
    Decode the base64 image payload from a server response.
    Works for both 'image' (lastimage) and 'screen' (gettargetscreen) types.
    """
    resp_type = response.get("type", "")
    status    = response.get("status", "")

    if status != "success":
        msg = response.get("message", "Unknown error")
        raise RuntimeError(f"Server error [{resp_type}]: {msg}")

    data = response.get("data")
    if not data:
        raise ValueError("Response contains no 'data' field.")

    content = data.get("content")
    if not content:
        raise ValueError("Response 'data' contains no 'content' field.")

    image_bytes = base64.b64decode(content)
    return image_bytes


def choose_extension(response: dict, command: str) -> str:
    """Determine a sensible file extension from the response metadata."""
    data = response.get("data", {})
    fmt  = data.get("format", "")

    if fmt == "jpeg":
        return ".jpg"
    if fmt == "raw":
        # The 'lastimage' path typically points to a saved JPEG/PNG; default jpg
        return ".jpg"
    # Fallback based on command
    return ".jpg" if command == "gettargetscreen" else ".bin"


# ──────────────────────────────────────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────────────────────────────────────

def timestamped_filename(ext: str) -> str:
    ts = datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:21]
    return f"openterface_{ts}{ext}"


def save_image(image_bytes: bytes, path: str) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(image_bytes)
    print(f"  Saved → {os.path.abspath(path)}  ({len(image_bytes):,} bytes)")


def print_status(response: dict) -> None:
    data    = response.get("data", {})
    state   = data.get("state", "unknown")
    message = data.get("message", "")
    ts      = response.get("timestamp", "")
    print(f"  Status : {state}")
    print(f"  Message: {message}")
    print(f"  Time   : {ts}")


# ──────────────────────────────────────────────────────────────────────────────
# Single capture
# ──────────────────────────────────────────────────────────────────────────────

def capture_once(host: str, port: int, command: str, output_path: str | None,
                 timeout: float, verbose: bool) -> bool:
    """
    Send one command and handle the response.
    Returns True on success, False on failure.
    """
    print(f"[{datetime.now().strftime('%H:%M:%S')}] Sending '{command}' → {host}:{port}")

    try:
        response = send_command(host, port, command, timeout)
    except (ConnectionRefusedError, OSError) as e:
        print(f"  Connection failed: {e}", file=sys.stderr)
        return False
    except json.JSONDecodeError as e:
        print(f"  Invalid JSON response: {e}", file=sys.stderr)
        return False

    if verbose:
        # Print response without the (potentially huge) content field
        debug_resp = json.loads(json.dumps(response))
        if "data" in debug_resp and "content" in debug_resp["data"]:
            size = len(debug_resp["data"]["content"])
            debug_resp["data"]["content"] = f"<base64 {size} chars>"
        print("  Response metadata:", json.dumps(debug_resp, indent=4))

    resp_type = response.get("type", "")

    # ── status command ──────────────────────────────────────────────────────
    if command == "checkstatus" or resp_type == "status":
        print_status(response)
        return True

    # ── error from server ───────────────────────────────────────────────────
    if resp_type == "error" or response.get("status") == "error":
        print(f"  Server error: {response.get('message', 'unknown')}", file=sys.stderr)
        return False

    # ── image / screen ──────────────────────────────────────────────────────
    try:
        image_bytes = extract_image_bytes(response)
    except (RuntimeError, ValueError) as e:
        print(f"  {e}", file=sys.stderr)
        return False

    # Width × height info (screen command only)
    data = response.get("data", {})
    w, h = data.get("width"), data.get("height")
    if w and h:
        print(f"  Resolution: {w} × {h}")

    # Determine save path
    if output_path:
        path = output_path
    else:
        ext  = choose_extension(response, command)
        path = timestamped_filename(ext)

    save_image(image_bytes, path)
    return True


# ──────────────────────────────────────────────────────────────────────────────
# CLI
# ──────────────────────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Retrieve images from Openterface QT via TCP.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--host",     default="127.0.0.1",       help="Server host (default: 127.0.0.1)")
    parser.add_argument("--port",     default=4896, type=int,    help="Server port (default: 4896)")
    parser.add_argument(
        "--cmd",
        default="gettargetscreen",
        choices=["lastimage", "gettargetscreen", "checkstatus"],
        help="TCP command to send (default: gettargetscreen)",
    )
    parser.add_argument("--output",   default=None,              help="Output file path (auto-named if omitted)")
    parser.add_argument("--timeout",  default=10.0, type=float,  help="Socket timeout in seconds (default: 10)")
    parser.add_argument("--verbose",  action="store_true",       help="Print response metadata")
    parser.add_argument("--loop",     action="store_true",       help="Capture continuously")
    parser.add_argument("--interval", default=1.0, type=float,   help="Seconds between captures in loop mode (default: 1.0)")
    parser.add_argument("--count",    default=0,  type=int,      help="Stop after N captures in loop mode (0 = unlimited)")
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    if args.loop:
        print(f"Continuous capture mode — interval={args.interval}s  count={'∞' if args.count == 0 else args.count}")
        n = 0
        try:
            while True:
                capture_once(args.host, args.port, args.cmd, args.output,
                             args.timeout, args.verbose)
                n += 1
                if args.count and n >= args.count:
                    break
                time.sleep(args.interval)
        except KeyboardInterrupt:
            print("\nStopped by user.")
    else:
        ok = capture_once(args.host, args.port, args.cmd, args.output,
                          args.timeout, args.verbose)
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
