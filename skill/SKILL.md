# Vibe Coding AI 状态指示灯 (v3.4)

AI 编码工具通过 WiFi / BLE 控制 ESP32 WS2812 灯带，8 种 RGB 动画实时反映工作状态。

> **你运行在 hooks / plugin 模式。LED 状态由 hooks 或插件自动触发，你无需手动调用 `send_led_state`。**
> **你使用 MCP 工具进行设备管理和调试，不要手动发送日常状态更新。**
> **v3.3 新增：多项目平台隔离。hooks 自动携带 `platform:claude`（或 codex/opencode/mimocode），同项目不同平台分配不同 LED。**

---

## 自动触发规则（无需手动操作）

Hooks 或插件已在以下事件自动设置 LED 状态——**你不需要做任何事**：

| 触发事件 | → LED 状态 | 说明 |
|---------|-----------|------|
| 收到用户消息 | thinking | 自动 |
| 编辑/写入文件 | coding | 自动 |
| 执行终端命令 | busy | 自动 |
| 等待用户确认 | waiting | 自动 |
| 任务完成 | success | 自动 |
| 命令失败 | error | 自动 |
| 异常/崩溃 | alarm | 自动 |
| 会话空闲 | off | 自动 |

---

## MCP 工具（设备管理和调试）

你可以使用以下 MCP 工具来管理设备——这些不会与 hooks 自动触发冲突。

### 查询类

| 工具 | 说明 |
|------|------|
| `get_device_info` | 查询设备完整状态 (state, brightness, IP, SSID, BLE, idle timeout) |
| `get_led_status` | 查询当前 LED 状态和传输方式 |
| `get_led_map` | 查询 8 灯珠项目分配表 |
| `list_led_states` | 列出 8 种状态及中文说明 |

### 控制类

| 工具 | 参数 | 说明 |
|------|------|------|
| `send_led_state` | state, transport?, project?, platform? | 手动设置状态（调试/测试，日常勿用）。platform 可选值: claude/codex/opencode/mimocode |
| `set_led_brightness` | level: 1-255 | 设置亮度 (默认 128) |
| `configure_device_ble` | enabled: true/false | 运行时开关 BLE 蓝牙子系统（关=完全停，开=恢复广播） |
| `set_ble_discoverable` | enabled: true/false | BLE 扫描可见 (120s 自动关闭，广播仍继续) |
| `clear_ble_bonds` | — | 清除所有已配对的 BLE 设备 |
| `set_idle_timeout` | seconds: 0-3600 | 闲置超时自动待机 (0=常亮, 默认 120s) |
| `set_device_language` | language: "zh"/"en" | WebUI 语言 |
| `scan_wifi` | — | 通过设备扫描附近 WiFi |
| `set_static_ip` | ip, gateway, netmask, enabled | 配置固定 IP (需重启) |
| `set_led_direction` | reversed: true/false | LED 灯序方向翻转 (true=反转, false=正常) |

---

## 状态表

| 状态 | 灯光效果 |
|------|---------|
| thinking | 高速彩虹旋转 |
| coding | 青→紫液态呼吸 |
| busy | 黄色双向扫描 |
| waiting | 红色呼吸 |
| success | 绿色呼吸 |
| error | 红→橙三连快闪 |
| alarm | 红蓝全灯带翻转 |
| off | 全灭待机 |

---

## 传输方式

| 方式 | 速度 | 何时使用 |
|------|------|---------|
| WiFi TCP | <10ms | 首选，自动 |
| BLE | 0.5-2s | WiFi 不可用时自动回退。Windows/Linux 自动从 OS 配对列表取 MAC，首次自动发现后缓存 |

---

## Hooks 事件（安装后自动触发，无需手动调用）

不同工具使用不同的 hooks 机制：

| 工具 | 机制 | 配置位置 |
|------|------|---------|
| MiMoCode | JS 插件 | `.mimocode/plugins/3dai-led.js` |
| OpenCode | JS 插件 | `.opencode/plugins/3dai-led.js` |
| Claude Code | settings.json hooks | `.claude/settings.json` |
| Codex CLI | hooks.json | `.codex/hooks.json` |

### Claude Code / Codex CLI 事件映射

| 事件 | → 状态 |
|------|--------|
| SessionStart | off |
| UserPromptSubmit | thinking |
| PreToolUse (Bash) | busy |
| PreToolUse (Edit\|Write) | coding |
| PostToolUse | thinking |
| PermissionRequest | waiting |
| PostToolUseFailure | error |
| Stop | success |
| StopFailure | alarm |
| SubagentStart | thinking |
| SubagentStop | off |
| PreCompact | busy |
| PostCompact | thinking |
| SessionEnd | off |

---

## HTTP API 端点（设备 WebUI）

| 端点 | 参数 | 说明 |
|------|------|------|
| `/` | `?lang=zh\|en` | WebUI 控制面板 |
| `/status` | — | JSON 设备状态 |
| `/set` | `?s=<state>&project=<name>&platform=<name>` | 设置 LED 状态 (v3.3 新增 platform) |
| `/brightness` | `?b=<1-255>` | 设置亮度 |
| `/ble/clearbonds` | — | 清除 BLE 配对 |
| `/idle_timeout` | `?t=<seconds>` | 闲置超时 |

---

## CLI 工具

```bash
python transport/send.py thinking --project myproject              # 手动发送（调试用）
python transport/send.py busy --project myproj --platform claude   # 指定平台 (v3.3)
python transport/send.py --list                                    # 扫描设备
python transport/send.py --bind 3dai-led-7604F1A8                # 绑定设备
```
