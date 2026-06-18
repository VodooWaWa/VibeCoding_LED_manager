# Vibe Coding LED Manager

AI 编程状态指示灯 — 一键安装管理器。支持 10 个 AI 编码工具，涵盖 Windows / macOS / Linux 三平台。

## 下载

到 [Releases](https://github.com/VodooWaWa/VibeCoding_LED_manager/releases) 下载对应平台的安装器：

| 平台 | 文件 |
|------|------|
| Windows x64 | `VibeCoding_LED_manager_win_x64.exe` |
| Windows ARM64 | `VibeCoding_LED_manager_win_arm64.exe` |
| macOS x64 (Intel) | `VibeCoding_LED_manager_mac_x64.dmg` |
| macOS ARM64 (Apple Silicon) | `VibeCoding_LED_manager_mac_arm64.dmg` |
| Linux x64 | `VibeCoding_LED_manager_linux_x64.AppImage` |

> 单文件运行，无需安装。首次使用建议给可执行权限（macOS/Linux）。

## 功能

- **10 平台一键安装**：Claude Code / Codex CLI / Cursor / Windsurf / Trae / OpenCode / MiMoCode / OpenClaw / Hermes
- **设备扫描绑定**：自动发现局域网内的 ESP32 LED 设备
- **配置管理**：全局安装 / 项目级安装 / 卸载 / MCP 配置导出
- **环境检测**：自动检查 Python、bleak、mcp 依赖

## 使用

1. 下载对应平台的单文件
2. 双击运行
3. 左侧检测环境 → 扫描设备 → 绑定
4. 右侧选择 AI 工具 → 一键安装

## 硬件

需要搭配 ESP32-C3 + WS2812 8 灯珠硬件使用。详见 [项目主页](https://github.com/VodooWaWa/VibeCoding_LED_manager)。

## 构建

本仓库通过 GitHub Actions 自动构建。推送代码到 master 分支自动触发三平台打包并发布到 Releases。

```bash
npm install
npm run build:win    # Windows 便携版
npm run build:mac    # macOS DMG
npm run build:linux  # Linux AppImage
```
