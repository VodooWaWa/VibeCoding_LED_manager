# WS2812 AI 状态指示灯

ESP32-C3 SuperMini + 8 灯珠 WS2812 5050 RGB 灯带，实时显示 AI 编码工具工作状态。

## 硬件清单

| 物料 | 型号/规格 | 数量 |
|------|----------|------|
| 主控 | ESP32-C3 SuperMini | 1 |
| 灯带 | WS2812 5050 RGB 8 灯珠 | 1 |
| 电阻 | 100Ω（数据线防反射） | 1 |
| 电容 | 100µF/16V 电解（可选） | 1 |

## 接线

```
ESP32-C3 SuperMini          WS2812 灯板 (8 珠)
┌──────────────┐         ┌──────────────────┐
│         VIN  │────────▶│  4.7VDC (任一)   │
│         GND  │────────▶│  GND (任一)      │
│      GPIO 8  │──[100Ω]─│  DI (任一)       │
└──────────────┘         └──────────────────┘
```

> 灯板 VCC/GND 之间建议并个 100µF 电解电容，防瞬间电流拉低 ESP32。

## 烧录

### Windows 一键刷入

```batch
flash.bat
```

或指定串口号：

```batch
flash.bat COM16
```

### 手动刷入

```bash
esptool.py --chip esp32c3 -p COM16 -b 460800 --before default_reset --after hard_reset write_flash 0x0 ws2812_ai_status_led_full.bin
```

### 首次使用

1. 刷入固件后，设备自动进入 AP 模式
2. 用手机或电脑连接 WiFi 热点：`ESP32_LED_XXXXXXXX`
3. 浏览器访问 `http://192.168.4.1/`
4. 在 WebUI 中点击"修改"按钮，输入你的 WiFi 名称和密码
5. 点击"保存并重启"，设备会自动连接你的 WiFi
6. 连接成功后，浏览器访问 `http://esp32-led.local/` 或路由器后台查 IP

## 8 种状态动画

| 状态 | 动画效果 | 触发场景 |
|------|---------|---------|
| thinking | 全色谱彩虹旋转，1 秒/周 | AI 思考推理 |
| coding | 青→紫液态渐变呼吸，3 秒/周期 | AI 生成代码 |
| busy | 黄色双向 Ping-Pong 扫描（明暗渐变） | 执行工具命令 |
| waiting | 红色呼吸灯，3 秒/周期 | 等待人类决策 |
| success | 绿色呼吸灯，3 秒/周期 | 任务成功 |
| error | 红→橙三连快闪，750ms | 执行出错 |
| alarm | 红蓝全灯带翻转，180ms | 安全警告 |
| off | 全灭 | 待机 |

## Web 配置页面

| 路由 | 功能 |
|------|------|
| `/` | 主页：WiFi 信息、亮度滑块、当前状态、帮助链接、语言切换 |
| `/help` | 状态说明 + 每行预览按钮（点击播 3 秒） |
| `/status` | API: `{"state":"thinking","brightness":128,"ssid":"...","ip":"..."}` |
| `/set?s=thinking` | 切换状态 |
| `/brightness?b=128` | 亮度 0-255 |
| `/scan` | WiFi 扫描（自动禁用 BLE 避免冲突） |
| `/wifi?ssid=x&pwd=y` | 保存 WiFi（自动重启） |

## 通信协议

### TCP（端口 8080）

```
thinking\n   → LED 切换
ping\n       → pong\n
status\n     → state thinking\n
```

Claude Code hooks / Codex hooks 通过 `transport/send.py` 发送 TCP 指令。

### MCP 服务器

支持 MCP 协议的 AI 工具（Cursor、Copilot 等）直接调用 `send_led_state` 工具。

## 文件说明

| 文件 | 说明 |
|------|------|
| `ws2812_ai_status_led_full.bin` | 完整刷机包（0x0 起始） |
| `ws2812_scheme_b.c` | 固件源码 |
| `CMakeLists.txt` | ESP-IDF 项目配置 |
| `CMakeLists_main.txt` | main 组件配置 |
| `idf_component.yml` | led_strip 依赖 |

## 依赖

- **ESP-IDF** ≥ 5.5
- **led_strip** 组件（`idf.py add-dependency led_strip`）
- **ESP32-C3** 目标

## 配套 Python 工具

项目根目录 `transport/` 下：

| 文件 | 说明 |
|------|------|
| `send.py` | CLI 发送状态到 ESP32 |
| `server.py` | MCP 服务器 |
| `hook_adapter.py` | Claude Code / Codex hook 适配器 |
| `transport_wifi.py` | WiFi TCP 通信 |
| `transport_ble.py` | BLE 通信 |

安装：

```bash
python skill/install.py --target claude   # Claude Code
python skill/install.py --target codex    # Codex CLI
```

## 已知问题与修复

### WiFi 扫描在 AP 模式下断开连接

**问题**：BLE 和 WiFi 共享射频资源，同时工作会导致 AP 连接断开。

**修复**：扫描前自动禁用 BLE，扫描后恢复。

## 版本历史

### v1.1.0 (2026-06-12)
- 修复 WiFi 扫描在 AP 模式下断开连接的问题
- 扫描前自动禁用 BLE，扫描后恢复
- 更新编译脚本，支持自动生成合并固件
- 更新 COM 端口为 COM16

### v1.0.0 (2026-06-08)
- 初始版本
- 8 种 LED 动画
- WiFi STA/AP 双模
- BLE GATT 服务器
- WebUI 配网
