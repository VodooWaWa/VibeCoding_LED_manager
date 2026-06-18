"""Hook bridge: send AI state to ESP32-C3 LED indicator.

Called by Claude Code / Codex CLI hooks via stdin JSON.
Reads hook event payload, maps it to an LED state, sends via WiFi (primary)
or BLE (fallback).
"""

import argparse
import json
import os
import sys
from pathlib import Path

_project_root = Path(__file__).resolve().parent.parent
if str(_project_root) not in sys.path:
    sys.path.insert(0, str(_project_root))

from transport.transport_wifi import send_state_wifi, VALID_STATES, probe_device, scan_for_devices
from transport.transport_ble import send_state_ble, load_ble_address, save_ble_address, clear_ble_address

# ============================================================
# Device discovery
# ============================================================

_cache_dir = Path.home() / ".local" / "share" / "esp32-led"
_cache_file = _cache_dir / ".esp32_ip_cache"
_last_fail_file = _cache_dir / ".esp32_last_fail"
_state_file = _cache_dir / ".esp32_last_state"
_device_id_file = _cache_dir / ".esp32_device_id"
_BUSY_DEBOUNCE_MS = 500
_log_file = _cache_dir / "send_debug.log"
_LOG_MAX_SIZE = 1 * 1024 * 1024
_LOG_MAX_FILES = 5


def _log_rotate():
    try:
        if _log_file.exists() and _log_file.stat().st_size > _LOG_MAX_SIZE:
            oldest = Path(str(_log_file) + ".%d" % _LOG_MAX_FILES)
            oldest.unlink(missing_ok=True)
            for i in range(_LOG_MAX_FILES - 1, 0, -1):
                old = Path(str(_log_file) + ".%d" % i)
                new = Path(str(_log_file) + ".%d" % (i + 1))
                if old.exists():
                    old.rename(new)
            backup = Path(str(_log_file) + ".1")
            _log_file.rename(backup)
    except Exception:
        pass


def _log_write(msg: str):
    _log_rotate()
    import datetime
    try:
        _cache_dir.mkdir(parents=True, exist_ok=True)
        with open(_log_file, "a", encoding="utf-8") as f:
            f.write(f"{datetime.datetime.now().isoformat()} | {msg}\n")
    except Exception:
        pass


def load_bound_device() -> str:
    """Return the bound device ID (e.g. 'esp32-led-7604F1A8'), or ''."""
    try:
        if _device_id_file.exists():
            return _device_id_file.read_text().strip()
    except Exception:
        pass
    return ""


def bind_device(device_id: str) -> None:
    """Persist a device binding."""
    try:
        _cache_dir.mkdir(parents=True, exist_ok=True)
        _device_id_file.write_text(device_id)
    except Exception:
        pass


# Project→LED mapping removed — firmware auto-allocates LEDs via alloc_led_for_project()


def _save_cache(ip: str):
    try:
        _cache_file.write_text(ip)
    except Exception:
        pass


def discover_host() -> str:
    env_host = os.environ.get("ESP32_HOST")
    if env_host:
        return env_host
    bound = load_bound_device()
    if _cache_file.exists():
        try:
            cached = _cache_file.read_text().strip()
            if cached:
                # With binding: verify this IP belongs to the bound device
                if bound:
                    is_dev, greeting = probe_device(cached, 8080, timeout=0.15)
                    if is_dev and bound == greeting:
                        return cached
                else:
                    # No binding: accept any ESP32 LED device
                    ok, _ = send_state_wifi(cached, 8080, "off", timeout=0.15)
                    if ok:
                        return cached
        except Exception:
            pass
    return ""


def discover_host_background():
    """Spawn an independent background *process* that scans the full subnet
    for the ESP32 LED device.  The process survives the parent send.py exit
    and writes ``.esp32_ip_cache`` when found, so the **next** hook
    invocation picks up the cached address instantly.

    We use a subprocess instead of a daemon thread because Python kills
    daemon threads when the main thread exits — the scan would never
    complete and the cache would stay empty forever.

    A lock file (``.esp32_scan_lock``) prevents concurrent scans when
    hooks fire rapidly before the cache is populated.
    """
    import subprocess

    # Lock file prevents duplicate concurrent scans
    lock_file = _cache_dir / ".esp32_scan_lock"
    try:
        if lock_file.exists():
            age = __import__("time").time() - lock_file.stat().st_mtime
            if age < 30:  # still running (or recently finished)
                return
            # Stale lock (30+ s old) — overwrite
    except Exception:
        pass

    try:
        _cache_dir.mkdir(parents=True, exist_ok=True)
        lock_file.write_text(str(os.getpid()))
    except Exception:
        return

    project_root = str(_project_root)
    cache_dir_str = str(_cache_dir)
    cache_file_str = str(_cache_file)
    last_fail_str = str(_last_fail_file)
    lock_file_str = str(lock_file)
    device_id_file_str = str(_device_id_file)
    bound = load_bound_device()

    # Inline script — self-contained so the subprocess has zero import
    # dependencies on the project (only stdlib + transport_wifi).
    scan_code = f'''
import socket, sys, time
from pathlib import Path
sys.path.insert(0, {project_root!r})
from transport.transport_wifi import probe_device

cache_dir = Path({cache_dir_str!r})
cache_file = Path({cache_file_str!r})
last_fail = Path({last_fail_str!r})
lock_file = Path({lock_file_str!r})
device_id_file = Path({device_id_file_str!r})

bound = ""
try:
    if device_id_file.exists():
        bound = device_id_file.read_text().strip()
except Exception:
    pass

# Determine local subnet
try:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("8.8.8.8", 80))
    subnet = s.getsockname()[0].rsplit(".", 1)[0] + "."
    s.close()
except Exception:
    subnet = "192.168.1."

deadline = time.monotonic() + 15.0
found = False

for i in range(1, 255):
    if time.monotonic() > deadline:
        break
    ip = subnet + str(i)
    try:
        is_dev, greeting = probe_device(ip, 8080, timeout=0.1)
        if is_dev:
            if bound and greeting != bound:
                continue  # skip devices that don't match binding
            cache_dir.mkdir(parents=True, exist_ok=True)
            cache_file.write_text(ip)
            found = True
            break
    except Exception:
        continue

if not found:
    try:
        cache_dir.mkdir(parents=True, exist_ok=True)
        last_fail.write_text(str(time.monotonic()))
    except Exception:
        pass

# Remove lock so future hooks can spawn a new scan if needed
try:
    lock_file.unlink(missing_ok=True)
except Exception:
    pass
'''
    try:
        kwargs = {}
        if sys.platform == "win32":
            kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
        subprocess.Popen(
            [sys.executable, "-c", scan_code],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            **kwargs,
        )
    except Exception:
        # Clean up lock on spawn failure
        try:
            lock_file.unlink(missing_ok=True)
        except Exception:
            pass


# ============================================================
# Hook event mapping (stdin JSON mode)
# ============================================================

CLAUDE_EVENT_TO_STATE = {
    "SessionStart": "off", "Setup": "busy", "UserPromptSubmit": "thinking",
    "UserPromptExpansion": "thinking", "PreToolUse": "busy",
    "PostToolUse": "thinking", "PostToolUseFailure": "error",
    "PostToolBatch": "thinking", "PermissionDenied": "alarm",
    "PermissionRequest": "waiting", "SubagentStart": "thinking",
    "SubagentStop": "off", "TaskCreated": "busy", "TaskCompleted": "thinking",
    "PreCompact": "busy", "PostCompact": "thinking", "Stop": "success",
    "StopFailure": "alarm", "TeammateIdle": "off", "SessionEnd": "off",
    "Elicitation": "waiting", "ElicitationResult": "thinking",
}

CLAUDE_NOTIFICATION_TO_STATE = {
    "permission_prompt": "waiting", "elicitation_dialog": "waiting",
    "idle_prompt": "off", "auth_success": "success",
    "elicitation_complete": "thinking", "elicitation_response": "thinking",
}

CLAUDE_PERMISSION_DECISION_TO_STATE = {"allowed": "thinking", "denied": "error"}

CODEX_EVENT_TO_STATE = {
    "SessionStart": "off", "UserPromptSubmit": "thinking",
    "PreToolUse": "busy", "PermissionRequest": "waiting",
    "PostToolUse": "thinking", "Stop": "success",
    "SubagentStart": "thinking", "SubagentStop": "off",
    "PreCompact": "busy", "PostCompact": "thinking",
}


def _infer_state(payload: dict) -> str | None:
    """Map hook payload (Claude Code or Codex) to LED state."""
    event = str(payload.get("hook_event_name", ""))

    if event == "Notification":
        return CLAUDE_NOTIFICATION_TO_STATE.get(
            str(payload.get("notification_type", "")))

    if event == "PreToolUse":
        tool_name = str(payload.get("tool_name", ""))
        if tool_name in ("Edit", "Write", "MultiEdit"):
            return "coding"
        return "busy"

    if event == "PostToolUse":
        decision = str(payload.get("permission_decision", ""))
        if decision in CLAUDE_PERMISSION_DECISION_TO_STATE:
            return CLAUDE_PERMISSION_DECISION_TO_STATE[decision]
        return CLAUDE_EVENT_TO_STATE.get(event)

    if event == "Stop":
        if payload.get("background_tasks") or payload.get("session_crons"):
            return "busy"
        return "success"

    state = CLAUDE_EVENT_TO_STATE.get(event)
    if state is None:
        state = CODEX_EVENT_TO_STATE.get(event)
    return state


# ============================================================
# Cross-process state dedup (for Claude Code / Codex CLI hooks)
# ============================================================

def _read_state_file():
    try:
        if _state_file.exists():
            data = json.loads(_state_file.read_text())
            return data.get("state"), data.get("ts", 0)
    except Exception:
        pass
    return None, 0


def _write_state_file(state: str):
    import time
    try:
        _state_file.parent.mkdir(parents=True, exist_ok=True)
        _state_file.write_text(json.dumps({"state": state, "ts": time.monotonic()}))
    except Exception:
        pass


def _should_send_hook_state(new_state: str) -> bool:
    import time, random
    # Small random jitter to reduce cross-process race windows
    time.sleep(random.uniform(0, 0.01))
    last_state, last_ts = _read_state_file()
    now = time.monotonic()

    if new_state == last_state:
        return False
    if new_state == "busy" and last_state == "thinking":
        if now - last_ts < _BUSY_DEBOUNCE_MS / 1000.0:
            return False
    _write_state_file(new_state)
    return True


# ============================================================
# State sending
# ============================================================

def _send_state(state: str, transport: str = "auto", host: str = None,
                port: int = 8080, timeout: float = 0.5,
                project: str = None, platform: str = None) -> str | None:
    """Send state to ESP32. Returns transport_used string or None.

    Fast-fail design: WiFi attempts first (cached IP, 0.5s timeout).
    BLE is only attempted when there's an explicit BLE binding or
    ESP32_BLE_FALLBACK env var is set.  Otherwise BLE scan overhead
    (import bleak, device discovery) would block the hook for >2s.
    """
    cmd = state
    if project:
        cmd = f"{state} {project}"
        if platform:
            cmd = f"{cmd} platform:{platform}"
    if transport in ("wifi", "auto"):
        h = host or discover_host()
        if h:
            ok, _ = send_state_wifi(h, port, cmd, timeout)

            if ok:
                return f"wifi ({h})"
            # Cached IP stale - start background re-scan
            discover_host_background()
        else:
            # No known IP — background scan for next time, don't block
            discover_host_background()

    # BLE fallback: only when a BLE address is bound (OS-paired device).
    # Direct connect — no scanning, no filter_device needed.
    if transport in ("ble", "auto") and not os.environ.get("ESP32_NO_BLE"):
        if load_ble_address():
            ok, _ = send_state_ble(cmd, timeout=1.0)
            if ok:
                return "ble"

    return None


# ============================================================
# Main entry point
# ============================================================

def _has_stdin_data() -> bool:
    """Check if stdin has piped data ready (cross-platform, non-blocking)."""
    try:
        import msvcrt
        return not sys.stdin.isatty()
    except ImportError:
        import select
        return select.select([sys.stdin], [], [], 0)[0] != []


def main() -> int:
    # --- Mode 1: Stdin JSON (hooks: Claude Code, Codex CLI) ---
    if _has_stdin_data():
        try:
            raw = sys.stdin.read()
            if raw.strip():
                try:
                    payload = json.loads(raw)
                except json.JSONDecodeError:
                    # JSON parse failed — try regex fallback to extract event name
                    import re
                    match = re.search(r'"hook_event_name"\s*:\s*"([^"]+)"', raw)
                    if not match:
                        match = re.search(r'"event"\s*:\s*"([^"]+)"', raw)
                    if match:
                        payload = {"hook_event_name": match.group(1)}
                    else:
                        raise
                state = _infer_state(payload)
                if state and state in VALID_STATES:
                    # Extract project name from cwd (last path component)
                    cwd = str(payload.get("cwd", ""))
                    project = cwd.rstrip("/\\").rsplit("/", 1)[-1].rsplit("\\", 1)[-1] if cwd else None
                    # Subagent: append agent type so each subagent gets its own LED
                    event = str(payload.get("hook_event_name", ""))
                    if event in ("SubagentStart", "SubagentStop"):
                        agent_type = str(payload.get("agent_type", "")).lower()
                        if agent_type:
                            project = f"{project or 'subagent'}:{agent_type}"
                        elif not project:
                            project = None  # no identifying info → share main agent
                    # Auto-detect platform from hook payload
                    platform = None
                    if "hook_event" in payload:
                        # Codex CLI: top-level hook_event object with event_type
                        platform = "codex"
                    elif "hook_event_name" in payload:
                        # Claude Code: hook_event_name string (always claude)
                        platform = "claude"
                    elif "event" in payload:
                        # Legacy: bare "event" field → Codex
                        platform = "codex"
                    if not _should_send_hook_state(state):
                        _log_write(f"{state} | DEDUP | event={payload.get('hook_event_name','?')} | project={project}")
                        return 0
                    host = discover_host()
                    if not host:
                        discover_host_background()
                    transport_used = None
                    if host:
                        transport_used = _send_state(state, transport="wifi",
                                                     host=host, project=project, platform=platform)
                    if not transport_used:
                        transport_used = _send_state(state, transport="ble",
                                                     project=project, platform=platform)
                    _log_write(f"{state} | {'OK' if transport_used else 'FAIL'} | event={payload.get('hook_event_name','?')} | project={project} | transport=auto used={transport_used or 'none'}")
                    return 0
        except Exception as e:
            _log_write(f"ERROR | {e}")
            return 0

    # --- OpenCode / MiMoCode plugins ---
    # These platforms call send.py with CLI args (not stdin JSON).
    # Their JS plugins run:  python send.py <state>
    parser = argparse.ArgumentParser(
        description="Send AI state to ESP32-C3 LED indicator"
    )
    parser.add_argument("state", nargs="?", choices=list(VALID_STATES),
                        help="State: thinking/coding/busy/waiting/success/error/alarm/off")
    parser.add_argument("--project", default=None,
                        help="Project name for multi-LED auto-allocation (v3.1+)")
    parser.add_argument("--platform", default=None,
                        help="Platform identifier (claude/codex/opencode/mimocode, v3.2+)")
    parser.add_argument("--list", action="store_true",
                        help="Scan and list all ESP32 LED devices on the network")
    parser.add_argument("--bind", metavar="DEVICE_ID",
                        help="Bind to a specific device (e.g. --bind esp32-led-7604F1A8)")
    parser.add_argument("--unbind", action="store_true",
                        help="Remove device binding, accept any device")
    parser.add_argument("--bind-ble", metavar="MAC_ADDRESS",
                        help="Bind to a BLE device by MAC address (e.g. --bind-ble AA:BB:CC:DD:EE:FF)")
    parser.add_argument("--unbind-ble", action="store_true",
                        help="Remove BLE device binding")
    args = parser.parse_args()

    # --- Device management ---
    if args.list:
        print("Scanning for ESP32 LED devices...")
        devices = scan_for_devices(timeout_per_ip=0.05, total_timeout=10.0)
        if devices:
            bound = load_bound_device()
            print(f"Found {len(devices)} device(s):\n")
            for greeting, ip in devices:
                marker = " <-- bound" if bound == greeting else ""
                print(f"  {greeting}  {ip}{marker}")
            if not bound:
                print(f"\n  Bind one: python transport/send.py --bind <device_id>")
        else:
            print("No ESP32 LED devices found on this network.")
        return 0

    if args.unbind:
        try:
            _device_id_file.unlink(missing_ok=True)
            print("Device binding removed.")
        except Exception as e:
            print(f"Error: {e}", file=sys.stderr)
            return 1
        return 0

    if args.unbind_ble:
        clear_ble_address()
        print("BLE device binding removed.")
        return 0

    if args.bind_ble:
        save_ble_address(args.bind_ble)
        print(f"Bound to BLE device {args.bind_ble}")
        return 0

    if args.bind:
        print(f"Looking for {args.bind}...")
        # Try cached IP first (instant)
        ip = discover_host()
        if ip:
            from transport.transport_wifi import probe_device as _probe
            is_dev, greeting = _probe(ip, 8080, timeout=0.15)
            if is_dev and greeting == args.bind:
                bind_device(args.bind)
                _save_cache(ip)
                print(f"Bound to {args.bind} ({ip})")
                return 0
        # Full scan
        devices = scan_for_devices(timeout_per_ip=0.05, total_timeout=10.0)
        found = [(g, found_ip) for g, found_ip in devices if g == args.bind]
        if not found:
            print(f"Device {args.bind} not found on the network.")
            if devices:
                print(f"Available devices:")
                for g, found_ip in devices:
                    print(f"  {g}  {found_ip}")
            return 1
        _, ip = found[0]
        bind_device(args.bind)
        _save_cache(ip)
        print(f"Bound to {args.bind} ({ip})")
        return 0

    if not args.state:
        return 0

    transport_used = _send_state(args.state, project=args.project, platform=args.platform)

    _log_write(f"{args.state} | {'OK' if transport_used else 'FAIL'} | transport=auto used={transport_used or 'none'}")

    return 0 if transport_used else 1


if __name__ == "__main__":
    sys.exit(main())
