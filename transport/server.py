"""MCP server for ESP32-C3 AI Status LED.

Exposes tools for controlling an ESP32-C3 LED indicator via WiFi TCP
and/or BLE GATT, following the Model Context Protocol.
"""

import sys
from pathlib import Path

# Ensure parent directory is in sys.path so 'transport' imports work
# when server.py is run from a different working directory (MCP stdio mode)
_PARENT = Path(__file__).resolve().parent.parent
if str(_PARENT) not in sys.path:
    sys.path.insert(0, str(_PARENT))

import asyncio
import socket
from typing import Any, Dict, Optional, Tuple

from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent

from transport.transport_wifi import send_state_wifi, VALID_STATES

from transport.transport_ble import send_state_ble_async, load_ble_address
HAS_BLE = True  # always try — send_state_ble returns fast error if no address bound

DEFAULT_WIFI_HOST = "auto"
DEFAULT_WIFI_PORT = 8080
DEFAULT_WIFI_TIMEOUT = 0.5
DEFAULT_BLE_TIMEOUT = 5.0

_last_state: str = "off"
_last_transport: str = "none"
_discovered_host: str = ""


# ============================================================
# HTTP API wrapper for device control endpoints
# ============================================================

def _api_call(endpoint: str) -> Tuple[bool, str]:
    """Call the ESP32 Web API endpoint."""
    import urllib.request
    host = _get_host()
    if not host:
        return False, "device not found"
    try:
        url = f"http://{host}{endpoint}"
        with urllib.request.urlopen(url, timeout=3) as resp:
            body = resp.read().decode(errors="replace")
            return True, body
    except Exception as e:
        return False, str(e)

# ---------------------------------------------------------------------------
# Core transport logic
# ---------------------------------------------------------------------------


def _get_host() -> str:
    """Lazy host discovery with caching — same logic as send.py."""
    global _discovered_host
    if _discovered_host:
        ok, _ = send_state_wifi(_discovered_host, DEFAULT_WIFI_PORT, "off", timeout=0.15)
        if ok:
            return _discovered_host
    _discovered_host = ""
    from transport.send import discover_host, discover_host_background
    host = discover_host()
    if host:
        _discovered_host = host
    else:
        discover_host_background()
    return host


def _try_wifi(state: str) -> Tuple[bool, str]:
    """Attempt to send *state* via WiFi TCP (non-blocking, fast)."""
    return send_state_wifi(
        _get_host(), DEFAULT_WIFI_PORT, state, DEFAULT_WIFI_TIMEOUT
    )


async def _try_ble(state: str) -> Tuple[bool, str]:
    """Attempt to send *state* via BLE (direct connect, no scanning)."""
    if not load_ble_address():
        return False, "no BLE device bound"
    return await send_state_ble_async(state, DEFAULT_BLE_TIMEOUT)


async def send_state(state: str, transport: str = "auto", project: str = None,
               platform: str = None) -> Tuple[bool, str]:
    """Send a state command using the specified transport.

    Args:
        state: One of the 8 valid LED states.
        transport: "auto" (try WiFi first, fall back to BLE),
                   "wifi" (WiFi only), or "ble" (BLE only).
        project: Optional project name for multi-LED mode (v3.1+).
        platform: Optional platform identifier (v3.2+).

    Returns:
        (success: bool, message: str)
    """
    global _last_state, _last_transport

    # Validate bare state (strip project name if embedded)
    bare = state.split()[0] if state else state
    if bare not in VALID_STATES:
        return False, f"invalid state: {state!r}"

    # Construct "state project_name platform:name" format
    cmd = state
    if project:
        cmd = f"{state} {project}"
        if platform:
            cmd = f"{cmd} platform:{platform}"

    transport = transport.lower()
    if transport not in ("auto", "wifi", "ble"):
        return False, f"unknown transport: {transport!r}"

    if transport == "wifi":
        ok, msg = _try_wifi(cmd)
        if ok:
            _last_state = state
            _last_transport = "wifi"
        return ok, msg

    if transport == "ble":
        ok, msg = await _try_ble(cmd)
        if ok:
            _last_state = state
            _last_transport = "ble"
        return ok, msg

    # transport == "auto": WiFi first, fall back to BLE
    ok, msg = _try_wifi(cmd)
    if ok:
        _last_state = state
        _last_transport = "wifi"
        return ok, msg

    # WiFi failed — try BLE
    ok_ble, msg_ble = await _try_ble(cmd)
    if ok_ble:
        _last_state = state
        _last_transport = "ble"
        return ok_ble, msg_ble

    return False, f"all transports failed: wifi={msg}, ble={msg_ble if HAS_BLE else 'not available'}"


# ---------------------------------------------------------------------------
# MCP server definition
# ---------------------------------------------------------------------------

LED_STATE_DESCRIPTIONS: Dict[str, str] = {
    "thinking": "思考中 — 高速彩虹旋转灯带",
    "coding":   "编码中 — 青→紫液态渐变呼吸",
    "busy":     "执行中 — 黄色双向扫描",
    "waiting":  "等待输入 — 红色呼吸灯",
    "success":  "完成 — 绿色呼吸灯",
    "error":    "出错 — 红→橙三连快闪",
    "alarm":    "告警 — 红蓝全灯带翻转",
    "off":      "待机 — 全部熄灭",
}

server = Server("3dai-led-mcp")


@server.list_tools()
async def list_tools() -> list[Tool]:
    return [
        Tool(
            name="send_led_state",
            description=(
                "Send a state to the AI status LED strip (WS2812, 8 LEDs). "
                "Valid states: thinking, coding, busy, waiting, success, error, alarm, off. "
                "Call this when: starting work (thinking), editing files (coding), "
                "running commands (busy), waiting for user input (waiting), "
                "task done (success), command failed (error), API error (alarm), idle (off). "
                "Transport: auto (WiFi first, BLE fallback), wifi, or ble."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "state": {"type": "string", "description": "LED state", "enum": sorted(VALID_STATES)},
                    "transport": {"type": "string", "description": "Transport (default: auto)", "enum": ["auto", "wifi", "ble"]},
                    "project": {"type": "string", "description": "Project name for multi-LED auto-allocation (optional, v3.1+)"},
                    "platform": {"type": "string", "description": "Platform identifier: claude, codex, opencode, mimocode (optional, v3.2+)"},
                },
                "required": ["state"],
            },
        ),
        Tool(
            name="get_led_status",
            description="Query the current LED state and device status (state, brightness, IP, BLE status).",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="list_led_states",
            description="List all 8 valid LED states with Chinese descriptions.",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="set_led_brightness",
            description="Set the LED strip brightness (1-255). Default is 128 (50%).",
            inputSchema={
                "type": "object",
                "properties": {"level": {"type": "integer", "description": "Brightness 1-255", "minimum": 1, "maximum": 255}},
                "required": ["level"],
            },
        ),
        Tool(
            name="configure_device_ble",
            description="Enable or disable the BLE (Bluetooth) service on the ESP32 device.",
            inputSchema={
                "type": "object",
                "properties": {"enabled": {"type": "boolean", "description": "True to enable BLE, False to disable"}},
                "required": ["enabled"],
            },
        ),
        Tool(
            name="set_ble_discoverable",
            description="Enable or disable BLE discoverable mode (auto-disables after 120s).",
            inputSchema={
                "type": "object",
                "properties": {"enabled": {"type": "boolean", "description": "True to enable discoverable, False to disable"}},
                "required": ["enabled"],
            },
        ),
        Tool(
            name="set_idle_timeout",
            description="Set the auto-standby timeout in seconds (0 = never auto-off). Default is 120s.",
            inputSchema={
                "type": "object",
                "properties": {"seconds": {"type": "integer", "description": "Timeout in seconds, 0-3600", "minimum": 0, "maximum": 3600}},
                "required": ["seconds"],
            },
        ),
        Tool(
            name="set_device_language",
            description="Set the device WebUI language: zh (Chinese) or en (English).",
            inputSchema={
                "type": "object",
                "properties": {"language": {"type": "string", "description": "zh or en", "enum": ["zh", "en"]}},
                "required": ["language"],
            },
        ),
        Tool(
            name="scan_wifi",
            description="Scan nearby WiFi networks via the ESP32 device. Returns SSID and RSSI list.",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="get_device_info",
            description="Get full device info from the ESP32 WebUI (state, brightness, IP, SSID, BLE status, idle timeout).",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="get_led_map",
            description="Get the per-LED state dashboard — which LED corresponds to which project, and current animation status of each.",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="clear_ble_bonds",
            description="Clear all bonded/paried BLE devices on the ESP32. After clearing, re-pair from OS Bluetooth settings.",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="set_static_ip",
            description="Configure a static IP for the ESP32 device (requires reboot). Set enabled=false to go back to DHCP.",
            inputSchema={
                "type": "object",
                "properties": {
                    "ip": {"type": "string", "description": "Static IP address, e.g. 192.168.1.15"},
                    "gateway": {"type": "string", "description": "Gateway IP, e.g. 192.168.1.1"},
                    "netmask": {"type": "string", "description": "Netmask, e.g. 255.255.255.0"},
                    "enabled": {"type": "boolean", "description": "True to enable static IP, false for DHCP"},
                },
                "required": ["ip", "gateway", "netmask", "enabled"],
            },
        ),
        Tool(
            name="set_led_direction",
            description="Flip LED left-right order. True = reversed (LED #1 = rightmost), False = normal (LED #1 = leftmost).",
            inputSchema={
                "type": "object",
                "properties": {"reversed": {"type": "boolean", "description": "True for reversed LED order, false for normal"}},
                "required": ["reversed"],
            },
        ),
    ]


@server.call_tool()
async def call_tool(name: str, arguments: dict[str, Any]) -> list[TextContent]:
    if name == "send_led_state":
        state: str = arguments["state"]
        transport: str = arguments.get("transport", "auto")
        project: str = arguments.get("project")
        platform: str = arguments.get("platform")
        ok, msg = await send_state(state, transport, project, platform)
        return [TextContent(type="text", text=msg)]

    if name == "get_led_status":
        ok, msg = _api_call("/status")
        if ok:
            return [TextContent(type="text", text=msg)]
        return [TextContent(type="text", text=(
            f"Current state: {_last_state}\n"
            f"Last transport: {_last_transport}\n"
            f"BLE available: {HAS_BLE}\n"
            f"(device offline, showing local cache)"
        ))]

    if name == "list_led_states":
        lines = []
        for s in sorted(VALID_STATES):
            lines.append(f"  {s:10s} — {LED_STATE_DESCRIPTIONS.get(s, '')}")
        return [TextContent(type="text", text="LED states:\n" + "\n".join(lines))]

    if name == "set_led_brightness":
        level = int(arguments["level"])
        ok, msg = _api_call(f"/brightness?b={level}")
        return [TextContent(type="text", text=f"Brightness set to {level}" if ok else f"Failed: {msg}")]

    if name == "configure_device_ble":
        en = 1 if arguments["enabled"] else 0
        ok, msg = _api_call(f"/ble?en={en}")
        return [TextContent(type="text", text=f"BLE {'enabled' if en else 'disabled'}" if ok else f"Failed: {msg}")]

    if name == "set_ble_discoverable":
        en = 1 if arguments["enabled"] else 0
        ok, msg = _api_call(f"/bledisc?en={en}")
        return [TextContent(type="text", text=f"BLE discoverable {'enabled (120s)' if en else 'disabled'}" if ok else f"Failed: {msg}")]

    if name == "set_idle_timeout":
        sec = int(arguments["seconds"])
        ok, msg = _api_call(f"/idle_timeout?t={sec}")
        return [TextContent(type="text", text=f"Idle timeout set to {sec}s" if ok else f"Failed: {msg}")]

    if name == "set_device_language":
        lang = arguments["language"]
        ok, msg = _api_call(f"/lang?l={lang}")
        return [TextContent(type="text", text=f"Language set to {lang}" if ok else f"Failed: {msg}")]

    if name == "scan_wifi":
        ok, msg = _api_call("/scan")
        return [TextContent(type="text", text=msg if ok else f"Failed: {msg}")]

    if name == "get_device_info":
        ok, msg = _api_call("/status")
        return [TextContent(type="text", text=msg if ok else f"Failed: {msg}")]

    if name == "get_led_map":
        ok, msg = _api_call("/ledmap")
        if ok:
            return [TextContent(type="text", text=msg)]
        return [TextContent(type="text", text="Device offline, LED map unavailable")]

    if name == "clear_ble_bonds":
        ok, msg = _api_call("/ble/clearbonds")
        return [TextContent(type="text", text=f"BLE bonds cleared: {msg}" if ok else f"Failed: {msg}")]

    if name == "set_static_ip":
        ip = arguments["ip"]
        gw = arguments["gateway"]
        mask = arguments["netmask"]
        en = 1 if arguments["enabled"] else 0
        ok, msg = _api_call(f"/static_ip?ip={ip}&gw={gw}&mask={mask}&en={en}")
        return [TextContent(type="text", text=f"Static IP configured, reboot device to apply" if ok else f"Failed: {msg}")]

    if name == "set_led_direction":
        rev = 1 if arguments["reversed"] else 0
        ok, msg = _api_call(f"/led_reverse?r={rev}")
        return [TextContent(type="text", text=f"LED direction {'reversed' if rev else 'normal'}" if ok else f"Failed: {msg}")]

    return [TextContent(type="text", text=f"Unknown tool: {name}")]


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


async def main() -> None:
    """Run the MCP server via stdio transport."""
    async with stdio_server() as (read_stream, write_stream):
        await server.run(read_stream, write_stream, server.create_initialization_options())


def run() -> None:
    """Synchronous entry point (e.g. for console_scripts)."""
    asyncio.run(main())


if __name__ == "__main__":
    asyncio.run(main())
