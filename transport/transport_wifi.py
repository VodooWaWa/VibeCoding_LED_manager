"""WiFi TCP transport module for sending state commands to ESP32-C3."""

import socket
from typing import Tuple

VALID_STATES = {
    "thinking", "coding", "busy", "waiting",
    "success", "error", "alarm", "off",
}


DEVICE_IDENTIFIER = "esp32-led"
LEGACY_GREETING = "ESP32-C3 AI status LED"


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
        return False, f"not esp32-led device: {greeting!r}"
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
_DEVICE_ID_RE = _re.compile(r'esp32-led-[0-9A-Fa-f]{8}')


def _extract_device_id(greeting: str) -> str:
    """Extract device ID like 'esp32-led-7604F1A8' from a greeting."""
    m = _DEVICE_ID_RE.search(greeting)
    return m.group(0) if m else greeting


def probe_device(host: str, port: int = 8080, timeout: float = 0.15) -> Tuple[bool, str]:
    """Quickly probe a host to check if it's an ESP32 LED device.

    Returns (is_device, device_id_or_error).
    If is_device is True, the second element is the clean device ID
    (e.g. "esp32-led-7604F1A8"). Otherwise it's an error message.
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
    return False, f"not esp32-led: {greeting!r}"


def scan_for_devices(subnet: str = None, port: int = 8080,
                     timeout_per_ip: float = 0.07, total_timeout: float = 15.0
                     ) -> list[Tuple[str, str]]:
    """Scan the subnet for all ESP32 LED devices.

    Returns a list of (device_id, ip) tuples, e.g.
    [("esp32-led-7604F1A8", "192.168.1.15"), ("esp32-led-A3B2C1D0", "192.168.1.200")].
    """
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

    # Scan low-to-high (most home routers assign from .1 upwards)
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

    def __init__(self, host: str = "esp32-led.local", port: int = 8080):
        self.host = host
        self.port = port

    def send(self, state: str, timeout: float = 0.5) -> bool:
        """Send *state* to the configured host/port.

        Returns True on success, False otherwise.
        """
        ok, _msg = send_state_wifi(self.host, self.port, state, timeout)
        return ok
