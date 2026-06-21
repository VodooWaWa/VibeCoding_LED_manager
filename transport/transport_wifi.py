"""WiFi TCP transport module for sending state commands to ESP32-C3."""

import socket
from typing import Tuple

VALID_STATES = {
    "thinking", "coding", "busy", "waiting",
    "success", "error", "alarm", "off",
}


DEVICE_IDENTIFIER = "3dai-led"
LEGACY_GREETING = "3DAi status LED"


def _try_send(host, port, state, timeout):
    """Single TCP send attempt. Accepts 'state', 'LED:state', or 'state project_name'."""
    cmd = state
    # Extract bare state for validation (first word before space or colon)
    bare = state.split()[0] if state else state
    if ':' in bare:
        bare = bare.split(':', 1)[-1]
    if bare not in VALID_STATES:
        return False, f"invalid state: {state!r}"
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
    except (socket.timeout, ConnectionRefusedError, OSError):
        return False, "connection failed"
    try:
        sock.settimeout(timeout)
        greeting = sock.recv(1024).decode(errors="replace").strip()
        sock.sendall((cmd + "\n").encode())
        response = sock.recv(1024).decode(errors="replace").strip()
    except (socket.timeout, OSError):
        return False, "send/receive failed"
    finally:
        sock.close()
    is_device = (DEVICE_IDENTIFIER in greeting or LEGACY_GREETING in greeting)
    if not is_device:
        return False, f"not 3dai-led device: {greeting!r}"
    if response.startswith("ok"):
        return True, response
    return False, f"unexpected response: {response!r}"


def send_state_wifi(
    host: str,
    port: int,
    state: str,
    timeout: float = 0.5,
) -> Tuple[bool, str]:
    """Send a state command, retrying up to 3 times on failure."""
    import time
    last_msg = ""
    for attempt in range(3):
        ok, msg = _try_send(host, port, state, timeout)
        if ok:
            return ok, msg
        last_msg = msg
        if attempt < 2:
            time.sleep(0.15 * (attempt + 1))
    return False, last_msg


import re as _re
_DEVICE_ID_RE = _re.compile(r'3dai-led-[0-9A-Fa-f]{8}')


def _extract_device_id(greeting: str) -> str:
    """Extract device ID like '3dai-led-7604F1A8' from a greeting."""
    m = _DEVICE_ID_RE.search(greeting)
    return m.group(0) if m else greeting


def probe_device(host: str, port: int = 8080, timeout: float = 0.15) -> Tuple[bool, str]:
    """Quickly probe a host to check if it's an ESP32 LED device.

    Returns (is_device, device_id_or_error).
    If is_device is True, the second element is the clean device ID
    (e.g. "3dai-led-7604F1A8"). Otherwise it's an error message.
    """
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
    except (socket.timeout, ConnectionRefusedError, OSError):
        return False, "connection failed"
    try:
        sock.settimeout(timeout)
        greeting = sock.recv(1024).decode(errors="replace").strip()
    except socket.timeout:
        return False, "no greeting"
    finally:
        sock.close()
    if DEVICE_IDENTIFIER in greeting or LEGACY_GREETING in greeting:
        return True, _extract_device_id(greeting)
    return False, f"not 3dai-led: {greeting!r}"


def scan_for_devices(subnet: str = None, port: int = 8080,
                     timeout_per_ip: float = 0.15, total_timeout: float = 45.0
                     ) -> list[Tuple[str, str]]:
    """Discover all 3dai-led devices. mDNS first (<1s), subnet scan fallback.

    Returns a list of (device_id, ip) tuples, e.g.
    [("3dai-led-7604F1A8", "192.168.1.15"), ("3dai-led-A3B2C1D0", "192.168.1.200")].
    """

    # 1. mDNS — resolve 3dai-led-*.local on Mac/Linux (instant)
    found = _scan_mdns(port, timeout=total_timeout)
    if found:
        return found

    # 2. Subnet scan fallback (Windows / mDNS not available)
    return _scan_subnet(subnet, port, timeout_per_ip, total_timeout)


def _scan_mdns(port: int = 8080, timeout: float = 5.0) -> list[Tuple[str, str]]:
    """Resolve all 3dai-led-XXXXXXXX.local devices via mDNS. Fast on Mac/Linux."""
    import time as _time
    found = []

    # Try resolving the bound device first (instant)
    try:
        from pathlib import Path
        bid = Path.home() / ".local" / "share" / "3dai-led" / ".3dai_device_id"
        if bid.exists():
            suffix = bid.read_text().strip().rsplit("-", 1)[-1]
            hostname = f"3dai-led-{suffix}.local"
            try:
                info = socket.getaddrinfo(hostname, port, socket.AF_INET, socket.SOCK_STREAM)
                ip = info[0][4][0]
                is_dev, greeting = probe_device(ip, port, timeout=0.15)
                if is_dev:
                    found.append((greeting, ip))
            except Exception:
                pass
    except Exception:
        pass

    # Search for other devices by trying common suffixes
    # Firmware uses last 4 bytes of MAC = 8 hex chars = ~65536 possibilities.
    # Instead of brute force, try a quick probe of .local names if zeroconf available.
    try:
        import subprocess, shutil
        # macOS: dns-sd, Linux: avahi-browse
        if shutil.which("dns-sd"):
            out = subprocess.check_output(
                ["dns-sd", "-B", "_http._tcp", "local"], timeout=min(timeout, 3.0), text=True
            )
            import re
            for m in re.finditer(r"3dai-led-([0-9A-Fa-f]{8})\b", out):
                host = f"3dai-led-{m.group(1)}.local"
                try:
                    info = socket.getaddrinfo(host, port, socket.AF_INET, socket.SOCK_STREAM)
                    ip = info[0][4][0]
                    is_dev, greeting = probe_device(ip, port, timeout=0.15)
                    if is_dev and greeting not in [f[0] for f in found]:
                        found.append((greeting, ip))
                except Exception:
                    continue
        elif shutil.which("avahi-browse"):
            out = subprocess.check_output(
                ["avahi-browse", "-tp", "_http._tcp"], timeout=min(timeout, 3.0), text=True
            )
            for m in re.finditer(r"3dai-led-([0-9A-Fa-f]{8})\b", out):
                host = f"3dai-led-{m.group(1)}.local"
                try:
                    info = socket.getaddrinfo(host, port, socket.AF_INET, socket.SOCK_STREAM)
                    ip = info[0][4][0]
                    is_dev, greeting = probe_device(ip, port, timeout=0.15)
                    if is_dev and greeting not in [f[0] for f in found]:
                        found.append((greeting, ip))
                except Exception:
                    continue
    except Exception:
        pass

    return found


def _scan_subnet(subnet: str, port: int, timeout_per_ip: float,
                 total_timeout: float) -> list[Tuple[str, str]]:
    """Fallback: TCP probe every IP in the subnet."""
    import time as _time
    if subnet is None:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            subnet = s.getsockname()[0].rsplit(".", 1)[0] + "."
            s.close()
        except Exception:
            subnet = "192.168.1."

    deadline = _time.monotonic() + total_timeout
    found: list[Tuple[str, str]] = []

    for start, end in [(1, 255)]:
        for i in range(start, end):
            if _time.monotonic() > deadline:
                return found
            ip = subnet + str(i)
            try:
                is_dev, greeting = probe_device(ip, port, timeout=min(timeout_per_ip, max(0.03, deadline - _time.monotonic())))
                if is_dev:
                    found.append((greeting, ip))
            except Exception:
                continue
    return found


class WifiTransport:
    """High-level wrapper around send_state_wifi()."""

    def __init__(self, host: str = "3dai-led.local", port: int = 8080):
        self.host = host
        self.port = port

    def send(self, state: str, timeout: float = 0.5) -> bool:
        """Send *state* to the configured host/port.

        Returns True on success, False otherwise.
        """
        ok, _msg = send_state_wifi(self.host, self.port, state, timeout)
        return ok
