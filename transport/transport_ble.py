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
DEVICE_NAME_PREFIX = "ESP32_LED_"

# Persisted BLE address — set via send.py --bind-ble <MAC>
_BIND_FILE = Path.home() / ".local" / "share" / "esp32-led" / ".esp32_ble_device_id"

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


async def _send_ble_async(
    state: str, timeout: float = 5.0
) -> Tuple[bool, str]:
    """Connect to the bonded BLE device and write the state command.

    Uses the persisted MAC address — no scanning.  The OS must have
    previously paired / bonded with the ESP32 device.
    """
    bare = state.split()[0] if state else state
    if bare not in VALID_STATES:
        return False, f"invalid state: {state!r}"

    address = load_ble_address()
    if not address:
        return False, "no BLE device bound (use send.py --bind-ble <MAC>)"

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
