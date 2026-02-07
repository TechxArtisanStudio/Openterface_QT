#!/usr/bin/env python3
"""sysinfo_inspector.py

Lightweight Linux system inspector focused on:
 - distribution (from /etc/os-release or lsb_release)
 - kernel (uname, /proc/version)
 - loaded kernel modules (lsmod or /proc/modules)
 - PCI devices + drivers (lspci -k, optional)
 - USB device tree (lsusb -t or /sys/bus/usb/devices)

Outputs human-readable text and JSON (--json).
Designed to run without root; will gracefully fall back when tools/files are missing.

Usage:
    python3 scripts/sysinfo_inspector.py [--json] [--output FILE]
    python3 scripts/sysinfo_inspector.py --usb --modules

Dependencies (optional, improves output):
 - lsusb (usb tree alternative)
 - lspci (pci driver info)

"""
from __future__ import annotations
import argparse
import json
import os
import platform
import subprocess
import shutil
import sys
import re
from typing import Dict, List, Optional, Tuple


def run_cmd(cmd: List[str]) -> Tuple[int, str, str]:
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, check=False)
        return p.returncode, p.stdout.strip(), p.stderr.strip()
    except FileNotFoundError:
        return 127, "", f"{cmd[0]}: not found"


def get_distro_info() -> Dict[str, str]:
    data: Dict[str, str] = {}
    # primary: /etc/os-release
    try:
        with open("/etc/os-release", "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                v = v.strip('"')
                data[k] = v
    except Exception:
        pass

    # fallback: lsb_release
    if not data and shutil.which("lsb_release"):
        rc, out, err = run_cmd(["lsb_release", "-a"])
        if rc == 0 and out:
            for line in out.splitlines():
                if ":" in line:
                    k, v = [s.strip() for s in line.split(":", 1)]
                    data[k] = v
    # normalize
    pretty = data.get("PRETTY_NAME") or data.get("Description") or data.get("DISTRIB_DESCRIPTION")
    return {
        "pretty": pretty or "Unknown",
        "raw": data,
    }


def get_kernel_info() -> Dict[str, str]:
    uname = platform.uname()
    bits = platform.architecture()[0] or ""
    machine = uname.machine or platform.machine() or ""
    kernel = {
        "system": uname.system,
        "node": uname.node,
        "release": uname.release,
        "version": uname.version,
        "machine": machine,
        "processor": uname.processor,
        "architecture": f"{machine} ({bits})",
        "bits": bits,
    }
    # /proc/version if present
    try:
        with open("/proc/version", "r", encoding="utf-8") as f:
            kernel["proc_version"] = f.read().strip()
    except Exception:
        kernel["proc_version"] = ""
    return kernel


def parse_lsmod_output(text: str) -> List[Dict[str, object]]:
    lines = [l for l in text.splitlines() if l.strip()]
    if not lines:
        return []
    # header expected: Module Size Used by
    entries: List[Dict[str, object]] = []
    for line in lines[1:]:
        parts = line.split()
        if len(parts) < 3:
            continue
        name = parts[0]
        size = parts[1]
        used_by = parts[2:]
        entries.append({"module": name, "size": size, "used_by": used_by})
    return entries


def get_loaded_modules() -> Dict[str, object]:
    # prefer lsmod
    if shutil.which("lsmod"):
        rc, out, err = run_cmd(["lsmod"])
        if rc == 0 and out:
            return {"source": "lsmod", "modules": parse_lsmod_output(out)}
    # fallback: /proc/modules
    try:
        with open("/proc/modules", "r", encoding="utf-8") as f:
            lines = f.read().splitlines()
            modules = []
            for line in lines:
                parts = line.split()
                if len(parts) >= 6:
                    modules.append({
                        "module": parts[0],
                        "size": parts[1],
                        "instances": parts[2],
                        "deps": parts[3].split(",") if parts[3] != "-" else [],
                        "state": parts[4],
                        "offset": parts[5],
                    })
            return {"source": "/proc/modules", "modules": modules}
    except Exception:
        return {"source": "none", "modules": []}


def parse_lspci_k(output: str) -> List[Dict[str, object]]:
    # lspci -k produces blocks separated by blank lines; first line is device
    items: List[Dict[str, object]] = []
    cur: Optional[Dict[str, object]] = None
    for line in output.splitlines():
        if not line.strip():
            if cur:
                items.append(cur)
                cur = None
            continue
        if not line.startswith("\t") and not line.startswith(" "):
            # new device
            if cur:
                items.append(cur)
            cur = {"device": line.strip(), "drivers": []}
        else:
            # indented lines
            if cur is None:
                continue
            text = line.strip()
            if text.startswith("Kernel driver in use:"):
                cur.setdefault("kernel_driver_in_use", text.split(":", 1)[1].strip())
            elif text.startswith("Kernel modules:"):
                cur.setdefault("kernel_modules", [s.strip() for s in text.split(":", 1)[1].split(",")])
            else:
                cur.setdefault("extra", []).append(text)
    if cur:
        items.append(cur)
    return items


def get_pci_info() -> Dict[str, object]:
    if not shutil.which("lspci"):
        return {"available": False, "reason": "lspci not found"}
    rc, out, err = run_cmd(["lspci", "-k"])
    if rc != 0:
        return {"available": False, "reason": err or out or f"lspci rc={rc}"}
    return {"available": True, "devices": parse_lspci_k(out)}


# ---------------- USB tree via sysfs -----------------

def read_sysfs_attr(path: str, attr: str) -> Optional[str]:
    p = os.path.join(path, attr)
    try:
        with open(p, "r", encoding="utf-8") as f:
            return f.read().strip()
    except Exception:
        return None


def _parse_lsusb(output: str) -> Dict[Tuple[int, int], Dict[str, str]]:
    """Parse plain `lsusb` output and return mapping (bus,dev) -> {idVendor,idProduct,desc}.

    Example line:
      Bus 001 Device 002: ID 8087:8000 Intel Corp.
    """
    import re

    m = re.compile(r"Bus\s+(\d+)\s+Device\s+(\d+):\s+ID\s+([0-9a-fA-F]{4}):([0-9a-fA-F]{4})\s*(.*)")
    out: Dict[Tuple[int, int], Dict[str, str]] = {}
    for line in output.splitlines():
        line = line.strip()
        mm = m.match(line)
        if not mm:
            continue
        bus = int(mm.group(1))
        dev = int(mm.group(2))
        vid = mm.group(3).lower()
        pid = mm.group(4).lower()
        desc = mm.group(5).strip()
        out[(bus, dev)] = {"idVendor": vid, "idProduct": pid, "desc": desc}
    return out


def collect_usb_from_sysfs(lsusb_map: Optional[Dict[Tuple[int, int], Dict[str, str]]] = None) -> List[Dict[str, object]]:
    base = "/sys/bus/usb/devices"
    if not os.path.isdir(base):
        return []
    devices: Dict[str, Dict[str, object]] = {}
    for name in os.listdir(base):
        # skip entries like usb1, usb2 (they are hubs' interface files) but include them
        path = os.path.join(base, name)
        if not os.path.isdir(path):
            continue
        # only consider names that look like device nodes (contain digits and '-')
        # but include others defensively
        dev: Dict[str, object] = {"name": name, "path": path}
        for attr in (
            "idVendor",
            "idProduct",
            "manufacturer",
            "product",
            "serial",
            "busnum",
            "devnum",
            "devpath",
        ):
            v = read_sysfs_attr(path, attr)
            if v:
                dev[attr] = v
        # try reading PRODUCT from uevent (format often: PRODUCT=vvvv/pppp/..)
        u = read_sysfs_attr(path, "uevent")
        if u and "PRODUCT=" in u:
            try:
                prod = [l for l in u.splitlines() if l.startswith("PRODUCT=")][0].split("=", 1)[1]
                parts = prod.split("/")
                if len(parts) >= 2:
                    dev.setdefault("idVendor", parts[0].lower())
                    dev.setdefault("idProduct", parts[1].lower())
            except Exception:
                pass

        # driver
        drv = None
        drv_link = os.path.join(path, "driver")
        if os.path.islink(drv_link):
            try:
                drv = os.path.basename(os.readlink(drv_link))
            except Exception:
                drv = None
        if drv:
            dev["driver"] = drv

        # enrich from lsusb mapping when busnum/devnum provided
        if lsusb_map and "busnum" in dev and "devnum" in dev:
            try:
                b = int(str(dev.get("busnum")))
                d = int(str(dev.get("devnum")))
                entry = lsusb_map.get((b, d))
                if entry:
                    dev.setdefault("idVendor", entry.get("idVendor"))
                    dev.setdefault("idProduct", entry.get("idProduct"))
                    dev.setdefault("lsusb_desc", entry.get("desc"))
            except Exception:
                pass

        devices[name] = dev

    # build parent-child by prefix heuristic: parent is the longest other name which is a prefix
    tree_nodes: Dict[str, Dict[str, object]] = {}
    for name, dev in devices.items():
        tree_nodes[name] = {**dev, "children": []}
    for name in sorted(tree_nodes.keys(), key=lambda s: len(s)):
        # find parent
        parent = None
        for cand in tree_nodes.keys():
            if cand == name:
                continue
            if name.startswith(cand + "-") or name.startswith(cand + "."):
                if parent is None or len(cand) > len(parent):
                    parent = cand
        if parent:
            tree_nodes[parent]["children"].append(tree_nodes[name])
    # roots: those not a child of any other
    roots = [n for n in tree_nodes.values() if not any(n is c for p in tree_nodes.values() for c in p.get("children", []))]
    # fallback: sort roots by name
    roots = sorted(roots, key=lambda r: r.get("name", ""))
    return roots


def parse_lsusb_tree(text: str, lsusb_map: Optional[Dict[Tuple[int, int], Dict[str, str]]] = None) -> List[Dict[str, object]]:
    """Parse `lsusb -t` into a list preserving order, infer parent/child by indentation
    depth (count of '|' characters), propagate `bus` from parent to children, then
    enrich entries using `lsusb_map` (when bus+dev are available).
    """
    import re
    rows: List[Tuple[str, Dict[str, object], int]] = []
    bus_re = re.compile(r"Bus\s+(\d+)")
    dev_re = re.compile(r"Dev\s+(\d+)")

    for raw in text.splitlines():
        line = raw.rstrip()
        if not line.strip() or ":" not in line:
            continue
        # depth heuristic: count '|' characters in the left-hand side
        try:
            lhs, rhs = line.split(":", 1)
            depth = lhs.count("|")
            path = lhs.strip()
            parts = [p.strip() for p in rhs.split(",") if p.strip()]
            info: Dict[str, object] = {"path": path}
            b_match = bus_re.search(rhs)
            d_match = dev_re.search(rhs)
            if b_match:
                info["bus"] = int(b_match.group(1))
            if d_match:
                info["dev"] = int(d_match.group(1))
            for p in parts:
                if p.startswith("Class="):
                    info.setdefault("class", p.split("=", 1)[1])
                elif p.startswith("Driver="):
                    info.setdefault("driver", p.split("=", 1)[1])
                else:
                    if p.endswith("M") or p.endswith("G"):
                        info.setdefault("speed", p)
            rows.append((path, info, depth))
        except Exception:
            continue

    # propagate bus from nearest ancestor (previous row with smaller depth)
    for idx, (path, info, depth) in enumerate(rows):
        if "bus" in info:
            continue
        # look backwards for first row with depth < current depth
        if depth == 0:
            continue
        for j in range(idx - 1, -1, -1):
            _, pinfo, pdepth = rows[j]
            if pdepth < depth and "bus" in pinfo:
                info["bus"] = pinfo["bus"]
                break

    # enrich using lsusb_map where possible
    for path, info, _ in rows:
        if lsusb_map and "bus" in info and "dev" in info:
            try:
                key = (int(info["bus"]), int(info["dev"]))
                m = lsusb_map.get(key)
                if m:
                    info.setdefault("idVendor", m.get("idVendor"))
                    info.setdefault("idProduct", m.get("idProduct"))
                    info.setdefault("lsusb_desc", m.get("desc"))
            except Exception:
                pass

    # return items in original order (drop depth)
    return [info for (_p, info, _d) in rows]


def get_usb_tree(force_sysfs: bool = False) -> Dict[str, object]:
    # try to gather an lsusb map first (best source for VID:PID)
    lsusb_map: Optional[Dict[Tuple[int, int], Dict[str, str]]] = None
    if shutil.which("lsusb"):
        rc, out, err = run_cmd(["lsusb"])
        if rc == 0 and out:
            lsusb_map = _parse_lsusb(out)

    # if user forces sysfs we skip using `lsusb -t` as the primary tree source
    if not force_sysfs and shutil.which("lsusb"):
        rc, out, err = run_cmd(["lsusb", "-t"])
        if rc == 0 and out:
            parsed = parse_lsusb_tree(out, lsusb_map=lsusb_map)
            if parsed:
                return {"source": "lsusb -t (+lsusb)", "tree": parsed}

    # fallback (or forced): use sysfs and enrich entries using lsusb_map when possible
    roots = collect_usb_from_sysfs(lsusb_map=lsusb_map)
    src = "sysfs"
    if lsusb_map:
        src += " (+lsusb)"
    return {"source": src, "tree": roots}


def format_usb_node(n: Dict[str, object]) -> str:
    # Support both sysfs-style nodes (have 'name') and lsusb -t nodes (have 'path')
    name = n.get("name") or n.get("path") or "<unknown>"
    vendor = n.get("idVendor") or n.get("vendor")
    product = n.get("idProduct") or n.get("product_id")
    lsdesc = n.get("lsusb_desc")
    prodname = n.get("product") or lsdesc or ""
    driver = n.get("driver")
    busnum = n.get("busnum") or n.get("bus")
    devnum = n.get("devnum") or n.get("dev")
    parts: List[str] = [str(name)]
    if vendor and product:
        parts.append(f"[{vendor}:{product}]")
    elif vendor and not product:
        parts.append(f"[vid={vendor}]")
    elif product and not vendor:
        parts.append(f"[pid={product}]")
    if prodname:
        parts.append(f"— {prodname}")
    if driver:
        parts.append(f"(driver={driver})")
    if busnum and devnum:
        parts.append(f"bus={busnum} dev={devnum}")
    return "  ".join(parts)


# ---------------- Serial test helpers -----------------

def _read_sysfs_id_from_tty(tty_name: str) -> Optional[Tuple[str, str, str]]:
    """Given tty device name (e.g. ttyUSB0, ttyACM0) return (devpath, idVendor, idProduct)
    by walking /sys/class/tty/<tty>/device upwards until idVendor/idProduct are found.
    """
    base = f"/sys/class/tty/{tty_name}/device"
    if not os.path.exists(base):
        return None
    p = os.path.realpath(base)
    # walk up to a few levels
    for _ in range(6):
        vid = read_sysfs_attr(p, "idVendor")
        pid = read_sysfs_attr(p, "idProduct")
        if vid and pid:
            return (p, vid.lower(), pid.lower())
        parent = os.path.dirname(p)
        if parent == p:
            break
        p = parent
    return None


def find_ttys_by_vidpid(vidpid_list: List[Tuple[str, str]]) -> List[Dict[str, str]]:
    """Scan /dev for ttyUSB* and ttyACM* devices and return those matching vid:pid.
    Returns list of dicts: {tty, devpath, vid, pid}
    """
    found: List[Dict[str, str]] = []
    candidates = []
    for d in os.listdir("/dev"):
        if d.startswith("ttyUSB") or d.startswith("ttyACM"):
            candidates.append(d)
    for tty in sorted(candidates):
        info = _read_sysfs_id_from_tty(tty)
        if not info:
            continue
        devpath, vid, pid = info
        for want_vid, want_pid in vidpid_list:
            if vid == want_vid.lower() and pid == want_pid.lower():
                found.append({"tty": f"/dev/{tty}", "devpath": devpath, "vid": vid, "pid": pid})
                break
    return found


def _checksum_sum(b: bytes) -> int:
    return sum(b) & 0xFF


def _checksum_xor(b: bytes) -> int:
    x = 0
    for c in b:
        x ^= c
    return x & 0xFF


def _open_write_read_tty(fd_path: str, baud: int, write_bytes: bytes, timeout: float) -> bytes:
    """Write then read from tty. Prefer pyserial if available, otherwise use termios+select.
    Returns the bytes read (may be empty).
    """
    try:
        import serial as _pyserial  # type: ignore
    except Exception:
        _pyserial = None

    if _pyserial:
        try:
            with _pyserial.Serial(fd_path, baudrate=baud, timeout=timeout) as s:
                s.reset_input_buffer()
                s.reset_output_buffer()
                s.write(write_bytes)
                s.flush()
                data = s.read(256)
                return data or b""
        except Exception:
            # fall through to termios fallback
            pass

    # termios fallback
    import termios
    import fcntl
    import select

    fd = os.open(fd_path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attrs = termios.tcgetattr(fd)
        # set raw, 8N1
        attrs[2] = attrs[2] | termios.CLOCAL | termios.CREAD
        attrs[3] = 0
        # if possible set baud (guard for platforms where cfsetispeed is not available)
        baud_map = {
            9600: termios.B9600,
            19200: termios.B19200,
            38400: termios.B38400,
            57600: termios.B57600,
            115200: termios.B115200,
        }
        if baud in baud_map:
            try:
                termios.cfsetispeed(attrs, baud_map[baud])
                termios.cfsetospeed(attrs, baud_map[baud])
            except Exception:
                # some Python builds/platforms lack cfsetispeed bindings — continue without raising
                pass
        termios.tcsetattr(fd, termios.TCSANOW, attrs)

        # write
        os.write(fd, write_bytes)
        # wait for response up to timeout
        r, _, _ = select.select([fd], [], [], timeout)
        out = b""
        if r:
            try:
                out = os.read(fd, 4096)
            except Exception:
                out = b""
        return out or b""
    finally:
        try:
            os.close(fd)
        except Exception:
            pass


def _determine_baud_sequence(vid: Optional[str], pid: Optional[str], args) -> List[int]:
    """Return an ordered list of baud rates to try for a device.

    Priority:
      - If the user supplied --serial-baud, honor that single value.
      - If VID:PID is 1a86:fe0c -> try [115200].
      - If VID:PID is 1a86:7523 -> try [9600, 115200].
      - Otherwise fall back to [115200].
    """
    # explicit override from user
    if getattr(args, "serial_baud", None) is not None:
        return [int(args.serial_baud)]
    v = (str(vid or "").lower(), str(pid or "").lower())
    if v == ("1a86", "fe0c"):
        return [115200]
    if v == ("1a86", "7523"):
        return [9600, 115200]
    # sensible default for unknown devices
    return [115200]


# Serial summary printer (concise) 
def _print_serial_summary(report: Dict[str, object]) -> None:
    st = report.get("serial_test")
    if not st:
        print("No serial-test data available.")
        return
    if st.get("note"):
        print(" ", st.get("note"))
    for dev in st.get("devices", []):
        status = "OK" if dev.get("ok") else "FAIL"
        print(f"{dev.get('tty')}  {dev.get('vid') or '?:?'}:{dev.get('pid') or '?:?'}  => {status}")
        # show only first successful response or last attempt
        shown = False
        for a in dev.get("attempts", []):
            if a.get("success"):
                b = a.get("baud")
                prefix = f"[baud={b}] " if b else ""
                print(f"  {prefix}response: {a.get('resp_hex')}")
                shown = True
                break
        if not shown:
            # show last attempt error or indicate no response
            if dev.get("attempts"):
                last = dev.get("attempts")[-1]
                lb = last.get("baud")
                lprefix = f"[baud={lb}] " if lb else ""
                if last.get("error"):
                    print(f"  {lprefix}error: {last.get('error')}")
                elif last.get("resp_hex"):
                    print(f"  {lprefix}resp: {last.get('resp_hex')}")
                else:
                    print(f"  {lprefix}no response")
            else:
                print("  no attempts")


def format_usb_tree(nodes: List[Dict[str, object]], indent: str = "") -> str:
    lines: List[str] = []
    for n in nodes:
        lines.append(indent + format_usb_node(n))
        # some nodes (sysfs) have 'children'
        children = n.get("children") or []
        if children:
            lines.append(format_usb_tree(children, indent + "  "))
    return "\n".join(lines)


def pretty_print_usb_tree(nodes: List[Dict[str, object]], indent: str = "") -> None:
    s = format_usb_tree(nodes, indent=indent)
    if s:
        print(s)


def build_report(include_modules: bool = True, include_pci: bool = True, include_usb: bool = True, usb_force_sysfs: bool = False) -> Dict[str, object]:
    report: Dict[str, object] = {}
    report["distro"] = get_distro_info()
    report["kernel"] = get_kernel_info()
    if include_modules:
        report["modules"] = get_loaded_modules()
    if include_pci:
        report["pci"] = get_pci_info()
    if include_usb:
        report["usb"] = get_usb_tree(force_sysfs=usb_force_sysfs)
    return report


def render_human_report(report: Dict[str, object], args) -> str:
    """Return the human-readable report as a single string (used for printing and for file output).
    This mirrors the previous print-only code paths so writing to a file is straightforward.
    """
    lines: List[str] = []

    # Distribution
    d = report.get("distro", {})
    lines.append("=== Distribution ===")
    lines.append(d.get("pretty") or "Unknown")
    raw = d.get("raw") or {}
    if raw:
        lines.append("")
        for k in ("NAME", "VERSION", "ID", "VERSION_ID", "PRETTY_NAME"):
            if k in raw:
                lines.append(f"{k}: {raw[k]}")

    # Kernel
    k = report.get("kernel", {})
    lines.append("")
    lines.append("=== Kernel ===")
    lines.append(f"{k.get('release')} — {k.get('version')}")
    lines.append(f"machine: {k.get('machine')}, processor: {k.get('processor')}")
    if k.get('architecture'):
        lines.append(f"architecture: {k.get('architecture')}")
    if k.get('proc_version'):
        pv = k.get('proc_version')
        lines.append(f"kernel build: {pv.split(')')[-1].strip()[:80]}...")

    # USB (optional)
    if getattr(args, 'usb', True):
        lines.append("")
        lines.append("=== USB device tree ===")
        usb = report.get("usb", {})
        src = usb.get("source") or ""
        lines.append(f"source: {src}")
        tree = usb.get("tree") or []
        if src.startswith("lsusb -t"):
            for item in tree:
                if item.get("idVendor") and item.get("idProduct"):
                    v = item.get("idVendor")
                    p = item.get("idProduct")
                    desc = item.get("lsusb_desc") or item.get("class") or ""
                    lines.append(f" - {item.get('path')}  [{v}:{p}]  — {desc}")
                else:
                    lines.append(" - " + json.dumps(item, ensure_ascii=False))
        else:
            if not tree:
                lines.append("  (no usb devices found or insufficient permissions)")
            else:
                lines.extend((format_usb_tree(tree) or "").splitlines())

    # Drivers summary
    lines.append("")
    lines.append("=== Drivers ===")
    usb_nodes = report.get("usb", {}).get("tree") or []
    found_drivers: Dict[str, List[Dict[str, object]]] = {}
    def _collect_drivers(nodes):
        for n in nodes:
            drv = n.get("driver")
            if drv:
                name = str(drv).split("/")[0]
                found_drivers.setdefault(name, []).append(n)
            for c in n.get("children", []):
                _collect_drivers([c])
    _collect_drivers(usb_nodes)

    # get loaded module info for drivers we found (best-effort)
    module_map = {}
    try:
        lm = report.get('modules') or get_loaded_modules()
        for m in lm.get("modules", []):
            module_map[m.get("module")] = m
    except Exception:
        module_map = {}

    if not found_drivers:
        lines.append("  (no drivers detected for USB devices)")
    else:
        for drv, nodes in sorted(found_drivers.items()):
            mod = module_map.get(drv)
            if mod:
                used = mod.get("used_by") or mod.get("instances") or ""
                lines.append(f" - {drv} (loaded) — used_by={used}")
            else:
                lines.append(f" - {drv}")
            for n in nodes:
                vid = n.get("idVendor")
                pid = n.get("idProduct")
                path = n.get("name") or n.get("path")
                prod = n.get("product") or n.get("lsusb_desc") or ""
                if vid and pid:
                    lines.append(f"     • {path} [{vid}:{pid}] — {prod}")
                else:
                    lines.append(f"     • {path} — {prod}")

    # Loaded modules (optional)
    if getattr(args, 'modules', True):
        mods = report.get('modules') or {}
        lines.append("")
        lines.append("=== Loaded kernel modules (top) ===")
        src = mods.get('source')
        if src:
            lines.append(f"source: {src}")
        module_list = mods.get('modules', [])
        limit = getattr(args, 'limit_modules', 0) or 0
        if limit > 0:
            module_list = module_list[:limit]
        for m in module_list:
            name = m.get('module')
            used = m.get('used_by') or m.get('instances') or ''
            lines.append(f" - {name} — used_by={used}")

    # PCI (optional)
    if getattr(args, 'pci', True):
        pci = report.get('pci') or {}
        if pci:
            lines.append("")
            lines.append("=== PCI devices ===")
            for e in pci.get('devices', [])[:50]:
                lines.append(f" - {e.get('businfo')}  {e.get('vendor')}:{e.get('device')} — {e.get('desc')}")

    # Serial test summary (if present)
    st = report.get('serial_test')
    if st:
        lines.append("")
        lines.append("=== Serial port test ===")
        if st.get('note'):
            lines.append("  " + st.get('note'))
        for dev in st.get('devices', []):
            status = "OK" if dev.get('ok') else "FAIL"
            lines.append(f" - {dev.get('tty')}  {dev.get('vid') or '?:?'}:{dev.get('pid') or '?:?'}  => {status}")
            for a in dev.get('attempts', []):
                meth = a.get('method')
                b = a.get('baud')
                prefix = f"[baud={b}] " if b else ""
                if a.get('success'):
                    lines.append(f"     • {prefix}{meth}: response={a.get('resp_hex')}")
                else:
                    if a.get('resp_hex'):
                        lines.append(f"     • {prefix}{meth}: no success, resp={a.get('resp_hex')}")
                    elif a.get('error'):
                        lines.append(f"     • {prefix}{meth}: error={a.get('error')}")
                    else:
                        lines.append(f"     • {prefix}{meth}: no response")
            for adv in dev.get('advice', []):
                lines.append(f"     Advice: {adv}")

    return "\n".join(lines)


def execute_actions(args) -> None:
    """Perform the report building, serial test (if requested), and print/save results.
    Extracted so the interactive menu can call it repeatedly.
    """
    # (debug prints removed)
    report = build_report(include_modules=args.modules, include_pci=args.pci, include_usb=args.usb, usb_force_sysfs=args.usb_sysfs)

    # If caller requested interactive serial-only (menu option 2), run a minimal probe
    # and print ONLY the serial summary — do not show other inspect output.
    if getattr(args, "serial_only", False) and getattr(args, "serial_test", False):
        # helper: interactive chooser when multiple ttys detected
        def _choose_tty_interactively(ttys: List[Dict[str, str]]) -> Optional[Dict[str, str]]:
            if not ttys:
                return None
            if len(ttys) == 1:
                return ttys[0]
            if not sys.stdin.isatty():
                # non-interactive: default to first
                return ttys[0]
            print("Multiple matching serial devices found — choose one to test:")
            for i, t in enumerate(ttys, start=1):
                vid = t.get("vid") or "?:?"
                pid = t.get("pid") or "?:?"
                prod = t.get("product") or t.get("lsusb_desc") or ""
                print(f"  {i}) {t.get('tty')}  [{vid}:{pid}]  {prod}")
            choice = input("Enter number (or Enter to cancel): ").strip()
            if not choice:
                return None
            try:
                idx = int(choice) - 1
                if 0 <= idx < len(ttys):
                    return ttys[idx]
            except Exception:
                return None
            return None

        # build VID:PID list
        if args.serial_vidpid:
            vidpid_list = []
            for p in args.serial_vidpid.split(","):
                if ":" in p:
                    a, b = p.split(":", 1)
                    vidpid_list.append((a.strip().lower(), b.strip().lower()))
        else:
            vidpid_list = [("1a86", "fe0c"), ("1a86", "7523")]

        # locate candidates
        if args.serial_tty:
            # user supplied a path -> still show matching candidates if any, but prefer provided tty
            candidates = find_ttys_by_vidpid(vidpid_list)
            provided = {"tty": args.serial_tty}
            # if there are candidates and running interactively, let user choose among them (or keep provided)
            if candidates and sys.stdin.isatty():
                chosen = _choose_tty_interactively(candidates)
                if chosen is None:
                    # user cancelled selection -> offer to test provided tty
                    ttys = [provided]
                else:
                    ttys = [chosen]
            else:
                ttys = [provided]
        else:
            candidates = find_ttys_by_vidpid(vidpid_list)
            if not candidates:
                ttys = []
            else:
                # if multiple candidates, ask user to pick one
                if len(candidates) > 1 and sys.stdin.isatty():
                    chosen = _choose_tty_interactively(candidates)
                    ttys = [chosen] if chosen else []
                else:
                    ttys = candidates

        # perform probe (reuse existing helpers)
        results = {"targets": vidpid_list, "devices": []}
        if not ttys:
            results["note"] = "no matching /dev/ttyUSB* or /dev/ttyACM* found or selection cancelled"
            _print_serial_summary({"serial_test": results})
            return

        probe_payload = bytes([0x57, 0xAB, 0x00, 0x01, 0x00])
        for dev in ttys:
            tty = dev.get("tty")
            vid = dev.get("vid")
            pid = dev.get("pid")
            entry: Dict[str, object] = {"tty": tty, "vid": vid, "pid": pid, "attempts": []}

            # select baud sequence according to VID:PID (or honor explicit --serial-baud)
            baud_seq = _determine_baud_sequence(vid, pid, args)
            ok = False
            for baud in baud_seq:
                # try checksum = sum
                cs = _checksum_sum(probe_payload)
                msg = probe_payload + bytes([cs])
                try:
                    resp = _open_write_read_tty(tty, baud, msg, args.serial_timeout)
                    ok = bool(resp)
                    entry["attempts"].append({"baud": baud, "method": "sum", "msg": msg.hex(), "resp_hex": resp.hex() if resp else "", "success": ok})
                except Exception as e:
                    entry["attempts"].append({"baud": baud, "method": "sum", "error": str(e), "success": False})
                    resp = b""
                    ok = False

                if ok:
                    break

                # if no response, try XOR checksum as a fallback
                cs2 = _checksum_xor(probe_payload)
                msg2 = probe_payload + bytes([cs2])
                try:
                    resp2 = _open_write_read_tty(tty, baud, msg2, args.serial_timeout)
                    ok2 = bool(resp2)
                    entry["attempts"].append({"baud": baud, "method": "xor", "msg": msg2.hex(), "resp_hex": resp2.hex() if resp2 else "", "success": ok2})
                    ok = ok2 or ok
                except Exception as e:
                    entry["attempts"].append({"baud": baud, "method": "xor", "error": str(e), "success": False})

                if ok:
                    break

            entry["ok"] = ok
            if not ok:
                entry.setdefault("advice", []).append("If there is no response, check that the correct driver is installed (e.g. CH34x/CDC) and verify /dev permissions.")
            results["devices"].append(entry)

        # print concise serial-only summary and return
        _print_serial_summary({"serial_test": results})
        return

    # SERIAL test (optional)
    if getattr(args, "serial_test", False):
        # prepare VID:PID list
        if args.serial_vidpid:
            vidpid_list = []
            for p in args.serial_vidpid.split(","):
                if ":" in p:
                    a, b = p.split(":", 1)
                    vidpid_list.append((a.strip().lower(), b.strip().lower()))
        else:
            vidpid_list = [("1a86", "fe0c"), ("1a86", "7523")]

        results: Dict[str, object] = {"targets": vidpid_list, "devices": []}

        # if user forced a tty, test only that
        ttys_to_try: List[Dict[str, str]] = []
        if args.serial_tty:
            # try to resolve VID/PID for the provided tty (best-effort)
            ttyname = os.path.basename(args.serial_tty)
            info = _read_sysfs_id_from_tty(ttyname)
            if info:
                _, vid, pid = info
            else:
                vid = pid = ""
            ttys_to_try = [{"tty": args.serial_tty, "devpath": info[0] if info else "", "vid": vid, "pid": pid}]
        else:
            found = find_ttys_by_vidpid(vidpid_list)
            ttys_to_try = found

        # if nothing found, still attempt to list probable ttys for user
        if not ttys_to_try:
            results["note"] = "no matching /dev/ttyUSB* or /dev/ttyACM* found for requested VID:PID"
            report["serial_test"] = results
        else:
            probe_payload = bytes([0x57, 0xAB, 0x00, 0x01, 0x00])
            for dev in ttys_to_try:
                tty = dev.get("tty")
                entry: Dict[str, object] = {"tty": tty, "vid": dev.get("vid"), "pid": dev.get("pid"), "attempts": []}
                # try checksum = sum
                cs = _checksum_sum(probe_payload)
                msg = probe_payload + bytes([cs])
                try:
                    resp = _open_write_read_tty(tty, args.serial_baud, msg, args.serial_timeout)
                    ok = bool(resp)
                    entry["attempts"].append({"method": "sum", "msg": msg.hex(), "resp_hex": resp.hex() if resp else "", "success": ok})
                except Exception as e:
                    entry["attempts"].append({"method": "sum", "error": str(e), "success": False})
                    resp = b""
                    ok = False

                # if no response, try XOR checksum as a fallback
                if not ok:
                    cs2 = _checksum_xor(probe_payload)
                    msg2 = probe_payload + bytes([cs2])
                    try:
                        resp2 = _open_write_read_tty(tty, args.serial_baud, msg2, args.serial_timeout)
                        ok2 = bool(resp2)
                        entry["attempts"].append({"method": "xor", "msg": msg2.hex(), "resp_hex": resp2.hex() if resp2 else "", "success": ok2})
                        ok = ok2 or ok
                    except Exception as e:
                        entry["attempts"].append({"method": "xor", "error": str(e), "success": False})

                entry["ok"] = ok
                if not ok:
                    entry.setdefault("advice", []).append("If there is no response, check that the correct driver is installed (e.g. CH34x/CDC) and verify /dev permissions.")
                results["devices"].append(entry)
            report["serial_test"] = results


    if args.json:
        out = json.dumps(report, indent=2, ensure_ascii=False)
        # if user requested USB tree saved as human-readable, do that now (even when --json)
        if args.save_usb:
            try:
                usb_tree = report.get("usb", {}).get("tree") or []
                txt = format_usb_tree(usb_tree) if usb_tree else "(no usb devices)"
                with open(args.save_usb, "w", encoding="utf-8") as sf:
                    sf.write(txt + "\n")
                print(f"Wrote USB tree to {args.save_usb}")
            except Exception as e:
                print(f"Failed to write USB tree to {args.save_usb}: {e}")
        if args.output:
            with open(args.output, "w", encoding="utf-8") as f:
                f.write(out + "\n")
            print(f"Wrote JSON report to {args.output}")
        else:
            print(out)
        return

    # Interactive-inspect (menu option 1): show Kernel + USB tree + Drivers summary only
    # NOTE: when serial_test is also requested (report-full), do NOT short-circuit
    if getattr(args, "interactive_inspect", False) and not getattr(args, "serial_test", False):
        d = report["distro"]
        print("=== Distribution ===")
        print(d.get("pretty") or "Unknown")
        if d.get("raw"):
            print()
            for k in ("NAME", "VERSION", "ID", "VERSION_ID", "PRETTY_NAME"):
                if k in d["raw"]:
                    print(f"{k}: {d['raw'][k]}")

        print("\n=== Kernel ===")
        k = report["kernel"]
        print(f"{k.get('release')} — {k.get('version')}")
        print(f"machine: {k.get('machine')}, processor: {k.get('processor')}")
        # explicit architecture (machine + bitness)
        if k.get("architecture"):
            print(f"architecture: {k.get('architecture')}")

        # show kernel build info (short)
        if k.get('proc_version'):
            pv = k.get('proc_version')
            print(f"kernel build: {pv.split(')')[-1].strip()[:80]}...")

        # USB tree
        if args.usb:
            print("\n=== USB device tree ===")
            usb = report.get("usb", {})
            src = usb.get("source") or ""
            print(f"source: {src}")
            tree = usb.get("tree") or []
            if src.startswith("lsusb -t"):
                for item in tree:
                    if item.get("idVendor") and item.get("idProduct"):
                        v = item.get("idVendor")
                        p = item.get("idProduct")
                        desc = item.get("lsusb_desc") or item.get("class") or ""
                        print(f" - {item.get('path')}  [{v}:{p}]  — {desc}")
                    else:
                        print(" - ", json.dumps(item, ensure_ascii=False))
            else:
                if not tree:
                    print("  (no usb devices found or insufficient permissions)")
                else:
                    pretty_print_usb_tree(tree)

        # Drivers summary (only drivers referenced by USB devices)
        print("\n=== Drivers ===")
        usb_nodes = report.get("usb", {}).get("tree") or []
        found_drivers = {}
        def _collect_drivers(nodes):
            for n in nodes:
                drv = n.get("driver")
                if drv:
                    name = str(drv).split("/")[0]
                    found_drivers.setdefault(name, []).append(n)
                for c in n.get("children", []):
                    _collect_drivers([c])
        _collect_drivers(usb_nodes)

        # get loaded module info for drivers we found (best-effort)
        module_map = {}
        try:
            lm = get_loaded_modules()
            for m in lm.get("modules", []):
                module_map[m.get("module")] = m
        except Exception:
            module_map = {}

        if not found_drivers:
            print("  (no drivers detected for USB devices)")
        else:
            for drv, nodes in sorted(found_drivers.items()):
                mod = module_map.get(drv)
                if mod:
                    used = mod.get("used_by") or mod.get("instances") or ""
                    print(f" - {drv} (loaded) — used_by={used}")
                else:
                    print(f" - {drv}")
                for n in nodes:
                    vid = n.get("idVendor")
                    pid = n.get("idProduct")
                    path = n.get("name") or n.get("path")
                    prod = n.get("product") or n.get("lsusb_desc") or ""
                    if vid and pid:
                        print(f"     • {path} [{vid}:{pid}] — {prod}")
                    else:
                        print(f"     • {path} — {prod}")

        # If a serial test was requested, show its human-readable summary here too
        st = report.get("serial_test")
        if st:
            print("\n=== Serial port test ===")
            if st.get("note"):
                print("  ", st.get("note"))
            for dev in st.get("devices", []):
                status = "OK" if dev.get("ok") else "FAIL"
                print(f" - {dev.get('tty')}  {dev.get('vid') or '?:?'}:{dev.get('pid') or '?:?'}  => {status}")
                for a in dev.get("attempts", []):
                    meth = a.get("method")
                    if a.get("success"):
                        print(f"     • {meth}: response={a.get('resp_hex')}")
                    else:
                        if a.get("resp_hex"):
                            print(f"     • {meth}: no success, resp={a.get('resp_hex')}")
                        elif a.get("error"):
                            print(f"     • {meth}: error={a.get('error')}")
                        else:
                            print(f"     • {meth}: no response")
                for adv in dev.get("advice", []):
                    print(f"     Advice: {adv}")
        return

    # Human readable (full/default)
    s = render_human_report(report, args)

    # Always print the human report to stdout for interactive users
    print(s)

    # If the caller requested a human-readable output file, resolve the path
    # into the script directory (unless absolute) and write the file once.
    if getattr(args, 'output', None) and not getattr(args, 'json', False):
        out_candidate = str(args.output).strip()
        # reject accidental numeric/menu input like "0" or "0.json"
        if not out_candidate or re.fullmatch(r"\d+(?:\.json)?", out_candidate):
            out_candidate = 'opf_report.txt'
        # place relative names inside the script directory so reports are colocated
        out_name = out_candidate if os.path.isabs(out_candidate) else os.path.join(os.path.dirname(os.path.abspath(__file__)), out_candidate)
        try:
            with open(out_name, 'w', encoding='utf-8') as f:
                f.write(s + "\n")
            print(f"Wrote report to {out_name}")
        except Exception as e:
            print(f"Failed to write report to {out_name}: {e}")

    # --- PCI output ---
    if args.pci:
        print("\n=== PCI devices & drivers ===")
        pci = report.get("pci", {})
        if not pci.get("available"):
            print(f"  (pci info unavailable: {pci.get('reason')})")
        else:
            devs = pci.get("devices", [])
            for d in devs:
                line = d.get("device")
                kd = d.get("kernel_driver_in_use")
                km = d.get("kernel_modules")
                extra = []
                if kd:
                    extra.append(f"driver={kd}")
                if km:
                    extra.append(f"modules={','.join(km)}")
                if extra:
                    line += "  (" + ", ".join(extra) + ")"
                print(" - " + line)

    # --- USB output ---
    if args.usb:
        print("\n=== USB device tree ===")
        usb = report.get("usb", {})
        src = usb.get("source") or ""
        print(f"source: {src}")
        tree = usb.get("tree") or []
        if src.startswith("lsusb -t"):
            for item in tree:
                if item.get("idVendor") and item.get("idProduct"):
                    v = item.get("idVendor")
                    p = item.get("idProduct")
                    desc = item.get("lsusb_desc") or item.get("class") or ""
                    print(f" - {item.get('path')}  [{v}:{p}]  — {desc}")
                else:
                    print(" - ", json.dumps(item, ensure_ascii=False))
        else:
            if not tree:
                print("  (no usb devices found or insufficient permissions)")
            else:
                pretty_print_usb_tree(tree)
        # save human-readable USB tree if requested
        if args.save_usb:
            try:
                txt = format_usb_tree(tree) if tree else "(no usb devices)"
                with open(args.save_usb, "w", encoding="utf-8") as sf:
                    sf.write(txt + "\n")
                print(f"Wrote USB tree to {args.save_usb}")
            except Exception as e:
                print(f"Failed to write USB tree to {args.save_usb}: {e}")

        # Human summary for serial-test (if requested)
        st = report.get("serial_test")
        if st:
            print("\n=== Serial port test ===")
            if st.get("note"):
                print("  ", st.get("note"))
            for dev in st.get("devices", []):
                status = "OK" if dev.get("ok") else "FAIL"
                print(f" - {dev.get('tty')}  {dev.get('vid') or '?:?'}:{dev.get('pid') or '?:?'}  => {status}")
                for a in dev.get("attempts", []):
                    meth = a.get("method")
                    if a.get("success"):
                        print(f"     • {meth}: response={a.get('resp_hex')}")
                    else:
                        if a.get("resp_hex"):
                            print(f"     • {meth}: no success, resp={a.get('resp_hex')}")
                        elif a.get("error"):
                            print(f"     • {meth}: error={a.get('error')}")
                        else:
                            print(f"     • {meth}: no response")
                for adv in dev.get("advice", []):
                    print(f"     Advice: {adv}")

# (moved into execute_actions)

def main() -> None:
    ap = argparse.ArgumentParser(description="Inspect Linux distro, kernel, drivers and USB tree")
    ap.add_argument("--json", action="store_true", help="output JSON")
    ap.add_argument("--output", "-o", help="write output to FILE")
    ap.add_argument("--no-modules", dest="modules", action="store_false", help="skip loaded modules")
    ap.add_argument("--no-pci", dest="pci", action="store_false", help="skip pci info")
    ap.add_argument("--no-usb", dest="usb", action="store_false", help="skip usb tree")
    ap.add_argument("-a", "--all", dest="all", action="store_true", help="show everything (no truncation); enabled automatically when script run with no args")
    ap.add_argument("--usb-sysfs", dest="usb_sysfs", action="store_true", help="force reading USB tree from sysfs (instead of lsusb -t)")
    ap.add_argument("--save-usb", dest="save_usb", metavar="FILE", help="save USB tree (human-readable) to FILE")
    ap.add_argument("--serial-test", dest="serial_test", action="store_true", help="perform USB-serial test (search VID:PID, open tty, send probe and expect response)")
    ap.add_argument("--serial-vidpid", dest="serial_vidpid", metavar="VID:PID", help="comma-separated VID:PID list to look for (default: 1a86:fe0c,1a86:7523)")
    ap.add_argument("--serial-tty", dest="serial_tty", metavar="TTY", help="force a specific tty (e.g. /dev/ttyACM0) for the serial test")
    ap.add_argument("--serial-baud", dest="serial_baud", type=int, default=None, help="baud rate to use for serial probe (optional). If omitted: 1a86:fe0c => 115200; 1a86:7523 => try 9600 then 115200. Explicit value overrides automatic selection.")
    ap.add_argument("--serial-timeout", dest="serial_timeout", type=float, default=1.5, help="seconds to wait for serial response")
    ap.add_argument("--report-full", dest="report_full", action="store_true",
                    help="generate combined report: option 1 (inspect) + serial-test; use --json/--output to save machine-readable report")
    ap.add_argument("--limit-modules", type=int, default=0, help="how many modules to show in pretty output (0 = show all)")
    args = ap.parse_args()

    # location of this script (used as the default directory for reports)
    script_dir = os.path.dirname(os.path.abspath(__file__))

    # sanitize args.output early to avoid accidental numeric filenames (menu
    # choices like "0"). Default files are placed in the script directory.
    if getattr(args, 'output', None):
        _cand = str(args.output).strip()
        if re.fullmatch(r"\d+(?:\.json)?", _cand):
            args.output = os.path.join(script_dir, 'opf_report.json' if getattr(args, 'json', False) else 'opf_report.txt')

    # --report-full: non-interactive shortcut that requests both inspect (opt 1)
    # and serial-test (opt 2). Honors --json/--output. When no --output is given,
    # default to a human-readable file name inside the script directory.
    if getattr(args, 'report_full', False):
        args.serial_test = True
        args.interactive_inspect = True
        # choose sensible default output when user didn't provide one
        if not getattr(args, 'output', None):
            args.output = os.path.join(script_dir, 'opf_report.json' if getattr(args, 'json', False) else 'opf_report.txt')


    # Interactive mode: present a persistent menu (0 exits). In non-interactive
    # environments (CI/scripts) fall back to the old "show all" behavior.
    if len(sys.argv) == 1 and sys.stdin.isatty():
        while True:
            print("\nPlease choose an action (0 to exit):")
            print("  1) Inspect Linux distro, kernel, drivers, and USB tree")
            print("  2) Test serial port (auto-detect 1a86:fe0c or 1a86:7523)")
            print("  3) Generate full report (inspect + serial-test) — saved to 'opf_report.txt' in the script directory by default")
            print("  0) Exit")
            choice = input("Enter 0, 1, 2 or 3 and press Enter: ").strip()
            if choice == "0":
                print("Exiting.")
                return
            if choice == "1":
                # interactive inspect: show kernel + USB tree + drivers summary only
                args_all_copy = argparse.Namespace(**vars(args))
                args_all_copy.interactive_inspect = True
                args_all_copy.modules = False
                args_all_copy.pci = False
                args_all_copy.usb = True
                args_all_copy.limit_modules = 0
                execute_actions(args_all_copy)
            elif choice == "2":
                args_serial_copy = argparse.Namespace(**vars(args))
                args_serial_copy.serial_test = True
                # interactive serial-only: do NOT show inspect output, only serial summary
                args_serial_copy.serial_only = True
                args_serial_copy.modules = False
                args_serial_copy.pci = False
                args_serial_copy.usb = False

                # discover candidates and show them before prompting
                if args_serial_copy.serial_vidpid:
                    vidpid_list = []
                    for p in args_serial_copy.serial_vidpid.split(","):
                        if ":" in p:
                            a, b = p.split(":", 1)
                            vidpid_list.append((a.strip().lower(), b.strip().lower()))
                else:
                    vidpid_list = [("1a86", "fe0c"), ("1a86", "7523")]

                candidates = find_ttys_by_vidpid(vidpid_list)
                if candidates:
                    print("Matching serial devices found:")
                    for i, c in enumerate(candidates, start=1):
                        print(f"   {i}) {c.get('tty')}  {c.get('vid') or '?:?'}:{c.get('pid') or '?:?'}  — {c.get('lsusb_desc') or c.get('product') or ''}")
                    print("   0) Do not run serial test now")
                    chose = input("Choose a device number to test or press Enter to run all candidates: ").strip()
                    if chose and chose != "0":
                        try:
                            idx = int(chose) - 1
                            candidates = [candidates[idx]]
                        except Exception:
                            print("Invalid selection, running all candidates.")

                # attach candidates and run serial-only flow
                args_serial_copy._candidates = candidates
                execute_actions(args_serial_copy)

            elif choice == "3":
                # interactive: generate combined report (inspect + serial-test)
                args_report_copy = argparse.Namespace(**vars(args))
                args_report_copy.interactive_inspect = True
                args_report_copy.serial_test = True
                args_report_copy.modules = False
                args_report_copy.pci = False
                args_report_copy.usb = True
                args_report_copy.limit_modules = 0

                # Immediately save to the default filename inside the script directory.
                # Respect an explicit --output or --json passed when launching the script.
                if getattr(args, 'output', None):
                    fname = args.output
                    use_json = bool(getattr(args, 'json', False))
                else:
                    use_json = bool(getattr(args, 'json', False))
                    fname = os.path.join(script_dir, 'opf_report.json' if use_json else 'opf_report.txt')

                args_report_copy.output = fname
                args_report_copy.json = use_json
                execute_actions(args_report_copy)
                print(f"Wrote combined report to {fname}")
            else:
                print("Invalid selection — try again.")
            # pause before re-displaying menu
            try:
                input("Press Enter to continue...")
            except Exception:
                pass

    else:
        # non-interactive: preserve previous behavior (show everything)
        if len(sys.argv) == 1:
            args.all = True
            args.modules = True
            args.pci = True
            args.usb = True
            args.limit_modules = 0

    # if explicit --all used, don't truncate modules
    if getattr(args, "all", False):
        args.limit_modules = 0

    # Non-interactive / CLI-invoked: execute once and exit. Interactive menu handles repeated runs.
    if not (len(sys.argv) == 1 and sys.stdin.isatty()):
        execute_actions(args)
        return

    report = build_report(include_modules=args.modules, include_pci=args.pci, include_usb=args.usb, usb_force_sysfs=args.usb_sysfs)

    # SERIAL test (optional)
    if getattr(args, "serial_test", False):
        # prepare VID:PID list
        if args.serial_vidpid:
            vidpid_list = []
            for p in args.serial_vidpid.split(","):
                if ":" in p:
                    a, b = p.split(":", 1)
                    vidpid_list.append((a.strip().lower(), b.strip().lower()))
        else:
            vidpid_list = [("1a86", "fe0c"), ("1a86", "7523")]

        results: Dict[str, object] = {"targets": vidpid_list, "devices": []}

        # if user forced a tty, test only that
        ttys_to_try: List[Dict[str, str]] = []
        if args.serial_tty:
            # try to resolve VID/PID for the provided tty (best-effort)
            ttyname = os.path.basename(args.serial_tty)
            info = _read_sysfs_id_from_tty(ttyname)
            if info:
                _, vid, pid = info
            else:
                vid = pid = ""
            ttys_to_try = [{"tty": args.serial_tty, "devpath": info[0] if info else "", "vid": vid, "pid": pid}]
        else:
            found = find_ttys_by_vidpid(vidpid_list)
            ttys_to_try = found

        # if nothing found, still attempt to list probable ttys for user
        if not ttys_to_try:
            results["note"] = "no matching /dev/ttyUSB* or /dev/ttyACM* found for requested VID:PID"
            report["serial_test"] = results
        else:
            probe_payload = bytes([0x57, 0xAB, 0x00, 0x01, 0x00])
            for dev in ttys_to_try:
                tty = dev.get("tty")
                entry: Dict[str, object] = {"tty": tty, "vid": dev.get("vid"), "pid": dev.get("pid"), "attempts": []}
                # try checksum = sum
                cs = _checksum_sum(probe_payload)
                msg = probe_payload + bytes([cs])
                try:
                    resp = _open_write_read_tty(tty, args.serial_baud, msg, args.serial_timeout)
                    ok = bool(resp)
                    entry["attempts"].append({"method": "sum", "msg": msg.hex(), "resp_hex": resp.hex() if resp else "", "success": ok})
                except Exception as e:
                    entry["attempts"].append({"method": "sum", "error": str(e), "success": False})
                    resp = b""
                    ok = False

                # if no response, try XOR checksum as a fallback
                if not ok:
                    cs2 = _checksum_xor(probe_payload)
                    msg2 = probe_payload + bytes([cs2])
                    try:
                        resp2 = _open_write_read_tty(tty, args.serial_baud, msg2, args.serial_timeout)
                        ok2 = bool(resp2)
                        entry["attempts"].append({"method": "xor", "msg": msg2.hex(), "resp_hex": resp2.hex() if resp2 else "", "success": ok2})
                        ok = ok2 or ok
                    except Exception as e:
                        entry["attempts"].append({"method": "xor", "error": str(e), "success": False})

                entry["ok"] = ok
                if not ok:
                    entry.setdefault("advice", []).append("If there is no response, check that the correct driver is installed (e.g. CH34x/CDC) and verify /dev permissions.")
                results["devices"].append(entry)
            report["serial_test"] = results


    if args.json:
        out = json.dumps(report, indent=2, ensure_ascii=False)
        # if user requested USB tree saved as human-readable, do that now (even when --json)
        if args.save_usb:
            try:
                usb_tree = report.get("usb", {}).get("tree") or []
                txt = format_usb_tree(usb_tree) if usb_tree else "(no usb devices)"
                with open(args.save_usb, "w", encoding="utf-8") as sf:
                    sf.write(txt + "\n")
                print(f"Wrote USB tree to {args.save_usb}")
            except Exception as e:
                print(f"Failed to write USB tree to {args.save_usb}: {e}")

        # extra safeguard: ensure output filename is sensible
        def _safe_output_name(name: Optional[str], want_json: bool) -> str:
            if not name:
                return os.path.join(script_dir, 'opf_report.json' if want_json else 'opf_report.txt')
            n = str(name).strip()
            if re.fullmatch(r"\d+(?:\.json)?", n):
                return os.path.join(script_dir, 'opf_report.json' if want_json else 'opf_report.txt')
            if want_json and not n.lower().endswith('.json'):
                return n + '.json'
            return n

        if args.output:
            out_name = _safe_output_name(args.output, want_json=True)
            try:
                with open(out_name, "w", encoding="utf-8") as f:
                    f.write(out + "\n")
                print(f"Wrote JSON report to {out_name}")
            except Exception as e:
                print(f"Failed to write JSON report to {out_name}: {e}")
        else:
            print(out)
        return

    # Human-readable output is handled inside execute_actions(args) when called from the interactive menu.
    # In non-interactive mode execute_actions(args) has already been executed.
    pass


if __name__ == "__main__":
    main()
