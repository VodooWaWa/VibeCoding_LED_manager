# Vibe Coding LED Manager

[中文](README.md)

One-click installer for AI status LED indicator. Single-file executable, supports 10 AI platforms.

> ## Ai3D趣造 Hardware
>
> Purchase: [Taobao](https://shop106055843.taobao.com/category.htm?spm=pc_detail.30350276.shop_block.dshopinfo.3e907dd6X0ddB7) | [Pinduoduo](https://mobile.yangkeduo.com/mall_page.html?ps=kSxgffPoX9)
>
> For questions, read the [User Manual](用户使用说明.pdf)
>
> ## Latest Firmware: v3.5.1 (check WebUI)
>
> **v3.5.1 Changelog:**
> - Attempted fix for macOS BLE device discovery failure (unverified)
> - BLE advertising packet restructured: device name now sent in advertising data, no longer relies on scan response
>
> **v3.5 Changelog:**
> - LED direction flip toggle
> - WiFi AP provisioning and fallback, fixed channel 6
> - BLE advertising switch now functional (was dead)
> - Static IP now applies correctly (was ignored)
> - HTTP response buffer — fixed chunked EAGAIN truncation
>
> Firmware cannot OTA upgrade. Contact support for remote assistance (Windows + asklink).

## Download

Get the latest from [Releases](https://github.com/VodooWaWa/VibeCoding_LED_manager/releases):

| OS | Arch | File |
|------|------|--------|
| Windows | x64 | `VibeCoding_LED_manager_win_x64.exe` |
| Windows | ARM64 | `VibeCoding_LED_manager_win_arm64.exe` |
| macOS | x64 (Intel) | `VibeCoding_LED_manager_mac_x64.dmg` |
| macOS | ARM64 (Apple Silicon) | `VibeCoding_LED_manager_mac_arm64.dmg` |
| Linux | x64 | `VibeCoding_LED_manager_linux_x64.AppImage` |

> macOS: If "can't be opened" or "damaged" appears:
>   1. System Settings → Privacy & Security → Open Anyway
>   2. If still blocked: `sudo xattr -rd com.apple.quarantine /Applications/VibeCoding.app`
>   3. Apple Silicon (M series) note: unsigned ARM64 builds are blocked by Gatekeeper — this is expected
> Linux: `chmod +x` the AppImage first.

## Features

- **One-click install**: Pick platform → Install. Auto-detects Python/bleak/mcp.
- **One-click uninstall**: Clean removal, no leftovers.
- **Device management**: LAN scan, WiFi/BLE bind/unbind, clear BLE bonds.
- **10 platforms**: Claude Code / Codex CLI / OpenCode / MiMoCode / Cursor / Windsurf / Trae / TraeCN / OpenClaw / Reasonix
- **Global / Project scope**: Global = all projects. Project = per-repo config.
- **Environment check**: Python 3.12+, bleak, mcp auto-detection.
- **MCP+Skill export**: Bundle config files for manual platform setup.
- **Bilingual UI**: Chinese / English.

## Quick Start

1. Download the single-file executable
2. Double-click to run
3. Verify Python/bleak/mcp deps
4. Select platform → Global or Project → Install
5. Scan devices → Bind → Done

## Scope

| Mode | Path | Use case |
|------|------|----------|
| Global | `~/.local/share/3dai-led/` | Single AI tool, all projects |
| Project | `.claude/` `.codex/` `.trae/` etc. | Team, config travels with repo |

Both modes coexist.

## Requirements

- Windows 10/11 / macOS / Linux
- Python 3.12+
- WiFi 2.4GHz LAN

## Security

- LAN trust model, no app-layer auth
- BLE Just Works + Secure Connections + Bonding
- Uninstall removes config only, transport + cache untouched

## Build

```bash
npm install
npx electron-builder --win portable
```

GitHub Actions builds all platforms (x64 + arm64) on manual trigger.

## License

MIT
