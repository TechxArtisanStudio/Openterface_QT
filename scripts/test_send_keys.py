#!/usr/bin/env python3
"""
test_send_keys.py — TCP Server Send Command Test
=================================================
Tests the AHK-style Send command against the Openterface TCP server,
covering special characters, backtick escapes, and modifier key combinations.

Usage:
    python test_send_keys.py
    python test_send_keys.py --host 127.0.0.1 --port 4896
    python test_send_keys.py --host 192.168.1.10 --delay 1.5
    python test_send_keys.py --case plain_text modifier_keys

Command reference for the Send command:
    ^c          → Ctrl+C
    !t          → Alt+T
    +a          → Shift+A  (same as 'A')
    #d          → Win+D
    ^+c         → Ctrl+Shift+C
    {Enter}     → Enter key
    {Tab}       → Tab key
    `!          → literal '!'
    `#          → literal '#'
    `+          → literal '+'
    `^          → literal '^'
    ``          → literal backtick
    `n          → newline (Enter)
    `t          → Tab
"""

import argparse
import json
import socket
import sys
import time

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 12345
RECV_TIMEOUT = 8.0   # seconds to wait for server response
INTER_TEST_DELAY = 1.0  # seconds between test cases (let target process each)


# ── helpers ──────────────────────────────────────────────────────────────────

def send_raw(host: str, port: int, payload: str, timeout: float = RECV_TIMEOUT) -> dict | None:
    """Send a raw TCP command and return the parsed JSON response (or None on error)."""
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            sock.sendall(payload.encode("utf-8"))

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
                for byte in chunk:
                    c = chr(byte)
                    if c == "{":
                        brace_depth += 1
                        started = True
                    elif c == "}":
                        brace_depth -= 1
                chunks.append(chunk)
                if started and brace_depth == 0:
                    break

        raw = b"".join(chunks).decode("utf-8", errors="replace").strip()
        return json.loads(raw)
    except (ConnectionRefusedError, OSError) as exc:
        print(f"  [CONNECTION ERROR] {exc}")
        return None
    except json.JSONDecodeError as exc:
        print(f"  [JSON PARSE ERROR] {exc} — raw: {raw!r}")
        return None


def send_script(host: str, port: int, script: str, timeout: float = RECV_TIMEOUT) -> dict | None:
    """Send a script command (e.g. 'Send \"hello\"') and return response."""
    return send_raw(host, port, script, timeout)


def poll_status(host: str, port: int, retries: int = 10, interval: float = 0.3) -> str:
    """Poll checkstatus until finish/fail or retries exhausted. Returns final status string."""
    for _ in range(retries):
        resp = send_raw(host, port, "checkstatus")
        if resp:
            status = resp.get("status", "")
            if status in ("finish", "fail"):
                return status
        time.sleep(interval)
    return "timeout"


def run_case(host: str, port: int, label: str, script: str, delay: float) -> bool:
    """Execute one Send test case, report result, return True on success."""
    print(f"\n{'─' * 60}")
    print(f"  TEST : {label}")
    print(f"  CMD  : {script!r}")

    resp = send_script(host, port, script)
    if resp is None:
        print("  ✗  No response (server unreachable?)")
        return False

    resp_status = resp.get("status", "?")
    print(f"  RESP : status={resp_status!r}  message={resp.get('message','')!r}")

    if resp_status in ("finish",):
        print("  ✓  Command acknowledged as finished immediately")
        time.sleep(delay)
        return True

    if resp_status == "running":
        time.sleep(delay)
        final = poll_status(host, port)
        ok = final == "finish"
        icon = "✓" if ok else "✗"
        print(f"  {icon}  Final status after polling: {final!r}")
        return ok

    # Some builds reply with "success" wrapping (script enqueued)
    if resp_status in ("success", "accepted"):
        print("  ✓  Script accepted, waiting…")
        time.sleep(delay)
        return True

    print(f"  ✗  Unexpected status: {resp_status!r}")
    return False


# ── test cases ────────────────────────────────────────────────────────────────

def build_test_cases() -> list[dict]:
    """
    Each case:
        id      – short identifier used with --case filter
        label   – human-readable description
        script  – raw TCP payload (AHK-style Send command)
    """
    return [
        # ── plain text ──────────────────────────────────────────────────
        {
            "id": "plain_lower",
            "label": "Plain lowercase letters 'hello'",
            "script": 'Send "hello"',
        },
        {
            "id": "plain_upper",
            "label": "Uppercase letters 'WORLD' (auto Shift)",
            "script": 'Send "WORLD"',
        },
        {
            "id": "plain_text",
            "label": "Mixed case with space 'Hello World'",
            "script": 'Send "Hello World"',
        },
        # ── shift-symbol characters (Phase 1 + 2) ───────────────────────
        {
            "id": "shift_symbols",
            "label": "Shift-symbols: @ $ % & * ( )",
            "script": 'Send "@$%&*()"',
        },
        # ── backtick escape sequences (Phase 4) ──────────────────────────
        {
            "id": "backtick_exclam",
            "label": "Backtick escape: `! → literal '!'",
            "script": 'Send "`!"',
        },
        {
            "id": "backtick_hash",
            "label": "Backtick escape: `# → literal '#'",
            "script": 'Send "`#"',
        },
        {
            "id": "backtick_plus",
            "label": "Backtick escape: `+ → literal '+'",
            "script": 'Send "`+"',
        },
        {
            "id": "backtick_caret",
            "label": "Backtick escape: `^ → literal '^'",
            "script": 'Send "`^"',
        },
        {
            "id": "backtick_backtick",
            "label": "Backtick escape: `` → literal backtick",
            "script": 'Send "``"',
        },
        {
            "id": "backtick_newline",
            "label": "Backtick escape: `n → Enter",
            "script": 'Send "`n"',
        },
        {
            "id": "backtick_tab",
            "label": "Backtick escape: `t → Tab",
            "script": 'Send "`t"',
        },
        # ── original failing input (issue report) ───────────────────────
        {
            "id": "original_issue",
            "label": "Original reported string: @#$%^&*()! (using backtick escapes for AHK modifiers)",
            "script": r'Send "@`#$%`^&*()`!"',
        },
        # ── modifier key combinations (Phase 3) ────────────────────────
        {
            "id": "modifier_ctrl_c",
            "label": "Modifier: ^c → Ctrl+C",
            "script": 'Send "^c"',
        },
        {
            "id": "modifier_alt_tab",
            "label": "Modifier: !{Tab} → Alt+Tab",
            "script": 'Send "!{Tab}"',
        },
        {
            "id": "modifier_ctrl_shift",
            "label": "Modifier: ^+End → Ctrl+Shift+End",
            "script": 'Send "^+{End}"',
        },
        {
            "id": "modifier_win_d",
            "label": "Modifier: #d → Win+D (show desktop)",
            "script": 'Send "#d"',
        },
        # ── brace key names ─────────────────────────────────────────────
        {
            "id": "brace_enter",
            "label": "Brace key: {Enter}",
            "script": 'Send "{Enter}"',
        },
        {
            "id": "brace_tab",
            "label": "Brace key: {Tab}",
            "script": 'Send "{Tab}"',
        },
        {
            "id": "brace_esc",
            "label": "Brace key: {Escape}",
            "script": 'Send "{Escape}"',
        },
        {
            "id": "brace_f5",
            "label": "Brace key: {F5} (refresh)",
            "script": 'Send "{F5}"',
        },
        # ── mixed complex sequences ─────────────────────────────────────
        {
            "id": "complex_email",
            "label": "Complex: user@example.com",
            "script": r'Send "user@example.com"',
        },
        {
            "id": "complex_password",
            "label": "Complex: P@ssw0rd!",
            "script": r'Send "P@ssw0rd`!"',
        },
        {
            "id": "sleep_between",
            "label": "Sleep 200ms between keys",
            "script": 'Sleep 200',
        },
    ]


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Test Openterface TCP server Send command with special characters."
    )
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"Server host (default: {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"Server port (default: {DEFAULT_PORT})")
    parser.add_argument("--delay", type=float, default=INTER_TEST_DELAY,
                        help=f"Seconds to wait between test cases (default: {INTER_TEST_DELAY})")
    parser.add_argument("case", nargs="*",
                        help="Run only specific test case IDs (omit to run all)")
    args = parser.parse_args()

    all_cases = build_test_cases()

    # Filter by requested IDs if given
    if args.case:
        selected = [c for c in all_cases if c["id"] in args.case]
        unknown = set(args.case) - {c["id"] for c in all_cases}
        if unknown:
            print(f"Unknown test IDs: {', '.join(sorted(unknown))}")
            print(f"Available IDs: {', '.join(c['id'] for c in all_cases)}")
            sys.exit(1)
    else:
        selected = all_cases

    print(f"Openterface Send-Key Test Suite")
    print(f"Target: {args.host}:{args.port}")
    print(f"Cases : {len(selected)}/{len(all_cases)}")

    # Quick connectivity check
    probe = send_raw(args.host, args.port, "checkstatus", timeout=3.0)
    if probe is None:
        print(f"\n[FATAL] Cannot connect to {args.host}:{args.port}. Is the app running?")
        sys.exit(1)
    print(f"Server reachable — initial status: {probe.get('status','?')!r}")

    passed = 0
    failed = 0
    for case in selected:
        ok = run_case(args.host, args.port, case["label"], case["script"], args.delay)
        if ok:
            passed += 1
        else:
            failed += 1

    print(f"\n{'═' * 60}")
    print(f"  Results: {passed} passed, {failed} failed  (total {passed + failed})")
    print(f"{'═' * 60}\n")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
