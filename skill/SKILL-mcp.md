---
name: 3dai-led
description: Vibe Coding AI status LED indicator — WiFi/BLE control of WS2812 LED strip (MCP mode)
---

# Vibe Coding AI 状态指示灯（MCP 模式 · v3.4）

AI 编码工具通过 WiFi / BLE 控制 ESP32 WS2812 灯带，8 种 RGB 动画实时反映工作状态。

> **你运行在纯 MCP 模式。没有 hooks / 插件自动触发，你必须手动调用每一个 `send_led_state`。**
> **v3.3：调用时带上 `platform` 参数标识来源平台（如 cursor/windsurf/trae）。多项目模式下同项目不同平台隔离 LED。**

---

## 强制调用规则（最高优先级）

**以下每条规则必须遵守。每次触发条件发生时，立即调用 `send_led_state`。**

| 触发条件 | 调用 | 时机 |
|---------|------|------|
| 收到用户消息、开始处理请求 | `send_led_state(state="thinking", platform="xxx")` | 第一动作，在任何思考/回答之前 |
| 执行 `Edit` / `Write` / 任何写入文件的操作 | `send_led_state(state="coding", platform="xxx")` | 写入前 |
| 执行 `Bash` / 终端命令 | `send_led_state(state="busy", platform="xxx")` | 命令执行前 |
| 需要用户确认权限 / 等待用户输入 | `send_led_state(state="waiting", platform="xxx")` | 等待前 |
| 用户做出选择或回复后 | `send_led_state(state="thinking", platform="xxx")` | 收到回复后立即 |
| 任务成功完成 | `send_led_state(state="success", platform="xxx")` | 完成时 |
| 命令执行失败或出错 | `send_led_state(state="error", platform="xxx")` | 失败后立即 |
| 遇到 API 错误、异常、崩溃 | `send_led_state(state="alarm", platform="xxx")` | 异常后立即 |
| 会话空闲 / 任务结束 | `send_led_state(state="off", platform="xxx")` | 最终动作 |

> **替换 `xxx` 为当前平台名**，如 cursor/windsurf/trae 等。同一项目在不同平台开发时，每个平台独立 LED，互不干扰。

---

## MCP 工具

### send_led_state（核心工具）

```
参数:
  state (必填): "thinking" | "coding" | "busy" | "waiting" | "success" | "error" | "alarm" | "off"
  transport (可选): "auto" | "wifi" | "ble"
  project (可选): 项目名, 多项目模式下自动分配 LED (v3.1+)
  platform (推荐): 平台标识 (v3.3+)。同项目不同平台分配不同 LED。
                  常用值: cursor/windsurf/trae/traecn/openclaw
```

### 查询类工具

| 工具 | 说明 |
|------|------|
| `get_device_info` | 查询设备完整状态 (state, brightness, IP, SSID, BLE, idle timeout) |
| `get_led_status` | 查询当前 LED 状态和传输方式 |
| `get_led_map` | 查询 8 灯珠项目分配表 |
| `list_led_states` | 列出 8 种状态及中文说明 |

### 设备控制工具

| 工具 | 参数 | 说明 |
|------|------|------|
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

| 方式 | 速度 | 说明 |
|------|------|------|
| WiFi TCP | <10ms | 首选。IP 缓存在 `~/.local/share/3dai-led/.esp32_ip_cache` |
| BLE | 0.5-2s | 自动回退。首次从 OS 配对列表取 MAC（0ms），缓存后用。需 BLE 广播开启 |

## 设备发现 (WiFi)

快速路径（非阻塞，<0.15s）：
1. `$ESP32_HOST` 环境变量
2. `~/.local/share/3dai-led/.esp32_ip_cache` 全局缓存

后台发现（缓存失效时触发，不阻塞 AI）：
1. 子网扫描（254 IP × 0.1s）

## 设备发现 (BLE)

自动，无需手动操作：
1. Windows: 注册表读取已配对 MAC（0ms）
2. Linux: BlueZ 缓存读取（0ms）
3. Mac: BleakScanner 扫描回退（<1s）
4. 找到后缓存到 `~/.local/share/3dai-led/.esp32_ble_device_id`

---

## CLI 工具

```bash
# 发送状态（必须带 platform 标识来源平台）
python transport/send.py thinking --platform cursor
python transport/send.py coding --project myproject --platform windsurf

# 设备管理
python transport/send.py --list                    # 扫描局域网设备
python transport/send.py --bind 3dai-led-7604F1A8  # 绑定 WiFi 设备
python transport/send.py --unbind                   # 解除绑定
```
