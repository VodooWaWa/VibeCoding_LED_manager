"""BLE GATT transport for ESP32-C3 AI Status LED.

Service UUID: 0000ff00-0000-1000-8000-00805f9b34fb
Write Char:   0000ff01-0000-1000-8000-00805f9b34fb
Notify Char:  0000ff02-0000-1000-8000-00805f9b34fb

Pairing-first design (v3.2): the OS must pair with the ESP32 device first.
The Python client connects directly to the bonded address — no scanning.
"""

import asyncio
from pathlib import Path
from typing import Optional, Tuple

SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
WRITE_CHAR_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"
NOTIFY_CHAR_UUID = "0000ff02-0000-1000-8000-00805f9b34fb"
DEVICE_NAME_PREFIX = "3DAi_LED_"

# Persisted BLE address — set via send.py --bind-ble <MAC>
_BIND_FILE = Path.home() / ".local" / "share" / "3dai-led" / ".3dai_ble_device_id"

VALID_STATES = {
    "thinking", "coding", "busy", "waiting",
    "success", "error", "alarm", "off",
}


def _is_ble_available() -> bool:
    """Return True if the bleak library can be imported."""
    try:
        __import__("bleak")
        return True
    except ImportError:
        return False


# ---------------------------------------------------------------------------
# Persisted address
# ---------------------------------------------------------------------------


def load_ble_address() -> Optional[str]:
    """Return the persisted BLE MAC address, or None."""
    try:
        if _BIND_FILE.exists():
            addr = _BIND_FILE.read_text().strip()
            if addr:
                return addr
    except Exception:
        pass
    return None


def save_ble_address(address: str) -> None:
    """Persist a BLE MAC address."""
    try:
        _BIND_FILE.parent.mkdir(parents=True, exist_ok=True)
        _BIND_FILE.write_text(address)
    except Exception:
        pass


def clear_ble_address() -> None:
    """Remove persisted BLE address."""
    try:
        _BIND_FILE.unlink(missing_ok=True)
    except Exception:
        pass


# ---------------------------------------------------------------------------
# Core send logic — direct connect (no scanning)
# ---------------------------------------------------------------------------


# Cache-dir shared with send.py for WiFi binding
_CACHE_DIR = Path.home() / ".local" / "share" / "3dai-led"
_WIFI_DEVICE_ID_FILE = _CACHE_DIR / ".3dai_device_id"


def _get_wifi_device_suffix() -> str:
    """Extract hex suffix from WiFi binding (3dai-led-7604F1A8 -> 7604F1A8)."""
    try:
        if _WIFI_DEVICE_ID_FILE.exists():
            did = _WIFI_DEVICE_ID_FILE.read_text().strip()
            # format: "3dai-led-XXXXXXXX"
            parts = did.rsplit("-", 1)
            if len(parts) == 2 and len(parts[1]) == 8:
                return parts[1].upper()
    except Exception:
        pass
    return ""


async def _auto_discover_ble_address(timeout: float = 1.5) -> str | None:
    """Find ESP32 BLE MAC from OS paired-device list. No BLE connect, no scan.

    1. Windows: registry paired MACs → match by WiFi binding suffix.
    2. Linux: BlueZ cache info files → match by device name.
    3. Mac: BleakScanner fallback (CoreBluetooth cache, fast).
    4. Cross-platform fallback: BleakScanner active scan.

    Once MAC is cached, BleakClient connects for state sends.
    BleakClient needs BLE advertising (firmware keeps it on persistently).
    """
    addr = _os_paired_mac()
    if addr:
        save_ble_address(addr)
        return addr
    return await _fallback_ble_scan(timeout)


def _os_paired_mac() -> str | None:
    """Cross-platform: get paired BLE MAC from OS, no scan."""
    import sys
    if sys.platform == "win32":
        return _win32_paired_mac()
    elif sys.platform == "darwin":
        return _macos_paired_mac()
    else:
        return _linux_paired_mac()


# ---- Windows: registry ----

def _win32_paired_mac() -> str | None:
    """Read paired BLE MACs from registry. Match by WiFi suffix.

    Suffix comes from base_mac[2..5] (8 hex chars). BLE MAC may differ
    at byte 5 (base + 1 or 2 on ESP32). Match bytes 2-4 (6 chars) fuzzy.
    """
    suffix = _get_wifi_device_suffix()
    try:
        import winreg
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                             r"SYSTEM\CurrentControlSet\Services\BTHPORT\Parameters\Devices")
        i = 0
        while True:
            try:
                name = winreg.EnumKey(key, i); i += 1
                if len(name) != 12:
                    continue
                flat = name.upper()
                mac = ":".join(name[j:j+2].upper() for j in range(0, 12, 2))
                if suffix:
                    # Suffix bytes 2-5 of base MAC → flat positions 4-11
                    if flat[4:12] == suffix:
                        winreg.CloseKey(key); return mac
                    # Fuzzy: bytes 2-4 (6 chars) = flat[4:10]
                    if flat[4:10] == suffix[:6]:
                        winreg.CloseKey(key); return mac
                else:
                    winreg.CloseKey(key); return mac  # no binding, first paired
            except OSError:
                break
        winreg.CloseKey(key)
    except Exception:
        pass
    return None


# ---- Linux: BlueZ cache ----

def _linux_paired_mac() -> str | None:
    """Read paired BLE device names from BlueZ cache.
    /var/lib/bluetooth/<adapter>/<mac_with_colons>/info contains Name=..."""
    import os
    suffix = _get_wifi_device_suffix()
    bt_root = "/var/lib/bluetooth"
    if not os.path.isdir(bt_root):
        return None
    try:
        for adapter in os.listdir(bt_root):
            adapter_dir = os.path.join(bt_root, adapter)
            if not os.path.isdir(adapter_dir):
                continue
            for entry in os.listdir(adapter_dir):
                info = os.path.join(adapter_dir, entry, "info")
                if not os.path.isfile(info):
                    continue
                try:
                    with open(info) as f:
                        txt = f.read()
                    # entry = "44:B1:76:04:F1:AA", strip colons → format
                    flat = entry.replace(":", "").upper()
                    if len(flat) != 12:
                        continue
                    mac = ":".join(flat[j:j+2] for j in range(0, 12, 2))
                    if f"Name={DEVICE_NAME_PREFIX}" in txt:
                        if suffix and f"Name={DEVICE_NAME_PREFIX}{suffix}" not in txt:
                            continue
                        return mac
                except Exception:
                    continue
    except Exception:
        pass
    return None


# ---- macOS: uses BleakScanner fallback (CoreBluetooth cache is fast) ----

def _macos_paired_mac() -> str | None:
    """macOS: skip fragile system_profiler parsing. Use BleakScanner fallback
    which leverages CoreBluetooth cache — fast and reliable."""
    return None


# ---- Cross-platform scan fallback ----

async def _fallback_ble_scan(timeout: float) -> str | None:
    """Active BLE scan. Needs device advertising (discoverable)."""
    from bleak import BleakScanner
    suffix = _get_wifi_device_suffix()
    try:
        devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
        best = None
        for addr, (device, adv) in devices.items():
            name = device.name or adv.local_name or ""
            if not name.startswith(DEVICE_NAME_PREFIX):
                continue
            if suffix:
                if name[-8:].upper().startswith(suffix[:6]):
                    best = addr; break
                continue
            best = addr; break
        if best:
            save_ble_address(best)
            return best
    except Exception:
        pass
    return None


async def _send_ble_async(
    state: str, timeout: float = 5.0
) -> Tuple[bool, str]:
    """Connect to the bonded BLE device and write the state command.

    Uses persisted MAC address first. If none, auto-discovers by scanning
    for nearby devices with name prefix 3DAi_LED_. Caches result.
    """
    bare = state.split()[0] if state else state
    if bare not in VALID_STATES:
        return False, f"invalid state: {state!r}"

    address = load_ble_address()
    if not address:
        address = await _auto_discover_ble_address(timeout=min(timeout, 1.5))
    if not address:
        return False, "no BLE device found (pair in OS Bluetooth settings first)"

    if not _is_ble_available():
        return False, "BLE support not available (pip install bleak)"

    from bleak import BleakClient

    data = (state + "\n").encode("utf-8")

    try:
        async with BleakClient(
            address,
            services=[SERVICE_UUID],
            timeout=timeout,
        ) as client:
            await client.write_gatt_char(WRITE_CHAR_UUID, data, response=True)
            return True, f"BLE -> {state}"
    except Exception as exc:
        return False, f"BLE error: {exc}"


# ---------------------------------------------------------------------------
# Synchronous wrapper
# ---------------------------------------------------------------------------


async def send_state_ble_async(state: str, timeout: float = 5.0) -> Tuple[bool, str]:
    """Async BLE send. Use from within a running asyncio event loop (MCP)."""
    bare = state.split()[0] if state else state
    if bare not in VALID_STATES:
        return False, f"invalid state: {state!r}"
    return await _send_ble_async(state, timeout)


def send_state_ble(state: str, timeout: float = 5.0,
                   filter_device: str = "") -> Tuple[bool, str]:
    """Synchronous wrapper for BLE send.  *filter_device* is deprecated
    (kept for backwards compatibility) — the persisted MAC address is used.
    """
    bare = state.split()[0] if state else state
    if bare not in VALID_STATES:
        return False, f"invalid state: {state!r}"

    try:
        return asyncio.run(_send_ble_async(state, timeout))
    except RuntimeError:
        import concurrent.futures
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(
                lambda s=state, t=timeout:
                    asyncio.run(_send_ble_async(s, t))
            )
            return future.result(timeout=timeout + 2)


# ---------------------------------------------------------------------------
# Type stub for static analysis — the real import is deferred
# ---------------------------------------------------------------------------

BLE_DEVICE_TYPE = object  # bleak.BLEDevice at runtime
