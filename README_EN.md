# ✨ Vibe Coding LED Manager + ESP32-C3 Firmware ✨

[中文](README.md)

## 🎯 What's this?

Your AI coding status LED — **one-click installer + open-source firmware** in one repo! Single-file executable, **10 AI platforms** auto-configured. Let the world know you're grinding code. 🤖💡

> Code + lights + vibes. The desk setup you didn't know you needed. ✨

---

## 🛒 Where to get hardware?

> ## Ai3D趣造 Hardware
>
> Purchase: [Taobao](https://shop106055843.taobao.com/category.htm?spm=pc_detail.30350276.shop_block.dshopinfo.3e907dd6X0ddB7) | [Pinduoduo](https://mobile.yangkeduo.com/mall_page.html?ps=kSxgffPoX9)
>
> Questions? Check the [User Manual](用户使用说明.pdf) first~

---

## 🚀 Latest Firmware v3.5.1 (check WebUI)

**v3.5.1 Changelog:**
- 🔧 Attempted fix for macOS BLE device discovery failure (unverified — no Mac to test 😭)
- 📡 BLE advertising restructured: device name sent in advertising data, no longer relies on scan response

**v3.5 Changelog:**
- 🔄 LED direction flip toggle
- 📶 WiFi AP provisioning + fallback, fixed channel 6
- 🩹 BLE advertising switch now functional (was dead)
- 🩹 Static IP now applies correctly (was ignored)
- ⚡ HTTP response buffer — fixed chunked EAGAIN truncation

> ⚠️ No OTA! Firmware upgrades need support's remote assistance (Windows + asklink).

---

## 📂 Firmware Source — Now Open!

ESP32-C3 firmware source is open — build & flash it yourself:

- **Source**: [`firmware/esp32c3_ws2812/`](firmware/esp32c3_ws2812/)
- **Latest**: v3.5.1
- **Requires**: ESP-IDF v6.0.1
- **Build & flash**: run `flash.cmd` inside `firmware/esp32c3_ws2812/` (Windows)

Features: 8× WS2812 multi-state animations, WebUI config, TCP:8080 control, BLE GATT control, WiFi AP provisioning, per-project LED allocation.

> For learning & self-flashing. Contact for commercial licensing.

---

## 📥 Download

Grab yours from [Releases](https://github.com/VodooWaWa/VibeCoding_LED_manager/releases):

| OS | Arch | File |
|------|------|--------|
| Windows | x64 | `VibeCoding_LED_manager_win_x64.exe` |
| Windows | ARM64 | `VibeCoding_LED_manager_win_arm64.exe` |
| macOS | x64 (Intel) | `VibeCoding_LED_manager_mac_x64.dmg` |
| macOS | ARM64 (Apple Silicon) | `VibeCoding_LED_manager_mac_arm64.dmg` |
| Linux | x64 | `VibeCoding_LED_manager_linux_x64.AppImage` |

> 🍎 macOS says "can't be opened" or "damaged"? Don't panic:
> 1. System Settings → Privacy & Security → Open Anyway
> 2. Still blocked? `sudo xattr -rd com.apple.quarantine /Applications/VibeCoding.app`
> 3. M-series note: unsigned ARM64 builds blocked by Gatekeeper — expected
> 🐧 Linux: `chmod +x` the AppImage first.

---

## 🖼️ Sneak Peek

### Manager UI
![WebUI](/Priview-IMG/ManagerUI.png "Manager UI")

### Device WebUI
![WebUI](/Priview-IMG/WebUI-1.png "WebUI main")
![WebUI](/Priview-IMG/WebUI-2.png "WebUI light test")
![WebUI](/Priview-IMG/WebUI-3.png "WebUI project board")

---

## 💡 Features

- 🚀 **One-click install**: Pick platform → Install. Auto env check, deps, config.
- 🧹 **One-click uninstall**: Clean removal, zero leftovers.
- 📡 **Device management**: LAN scan, WiFi/BLE bind/unbind, clear BLE bonds.
- 🤝 **10 platforms**: Claude Code / Codex CLI / OpenCode / MiMoCode / Cursor / Windsurf / Trae / TraeCN / OpenClaw / Reasonix
- 🌍 **Global / Project scope**: Global = all projects. Project = per-repo config.
- ✅ **Environment check**: Python 3.12+, bleak, mcp auto-detection.
- 📦 **MCP+Skill export**: Bundle config files for manual platform setup.
- 🌐 **Bilingual UI**: Chinese / English.

---

## 📖 Quick Start

1. 📥 Download the single-file executable
2. 🖱️ Double-click to run
3. ✅ Verify Python 3.12+ / bleak / mcp deps
4. 🎯 Select platform → Global or Project → Install
5. 🔍 Scan devices → Bind → Done!

---

## 📍 Scope

| Mode | Path | Use case |
|------|------|----------|
| Global | `~/.local/share/3dai-led/` | Single AI tool, all projects |
| Project | `.claude/` `.codex/` `.trae/` etc. | Team, config travels with repo |

Both modes coexist.

---

## 🔧 Requirements

- Windows 10/11 / macOS / Linux
- Python 3.12+
- WiFi 2.4GHz LAN

---

## 🔒 Security

- LAN trust model, no app-layer auth
- BLE Just Works + Secure Connections + Bonding
- Uninstall removes config only, transport + cache untouched

---

## 🏗️ Build it yourself?

```bash
npm install
npx electron-builder --win portable
```

GitHub Actions builds all platforms (x64 + arm64) on manual trigger. Just sit back~

---

## 📜 License

MIT — go wild!

#AIcoding #statusLED #vibecoding #ESP32 #WS2812 #ClaudeCode #Codex #desksetup #electronicsDIY
