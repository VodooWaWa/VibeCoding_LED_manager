# AI Status LED — 安装指南（Agent 可执行版本）

本文档供 AI 编码工具（Claude Code / Codex CLI / Cursor）的 Agent 读取并**自动执行**。

---

## Phase 0: 环境准备

```bash
# 安装 Python 依赖
pip install bleak mcp
```

`bleak` 用于 BLE 通信，`mcp` 用于 MCP 协议服务端。

---

## Phase 1: 发现设备

### 1.1 通过网络扫描发现 ESP32

```bash
python transport/send.py --transport auto off
```

该命令会自动执行：
1. 检查 `ESP32_HOST` 环境变量
2. 读取 `.esp32_ip_cache` 缓存的 IP
3. mDNS 解析 `esp32-led.local`
4. 全子网扫描（256 个 IP，每 IP 0.1 秒）

**结果处理：**
- **成功 →** 记录 IP 地址，跳过 Phase 2 配网步骤
- **失败 →** 进入 Phase 2 配网流程

### 1.2 通过 WebUI 获取设备状态

```bash
curl -s http://<DEVICE_IP>/status
```

返回 JSON 包含 `state`, `ip`, `ssid`, `ble_enabled`, `ble_discoverable`, `brightness`, `idle_timeout`。

**Agent 应告知用户：**
```
发现设备！IP: <DEVICE_IP>
- WebUI: http://<DEVICE_IP>/
- WiFi: <SSID>
- BLE: <已启用/未启用>
- 亮度: <brightness>%
```

---

## Phase 2: 配网引导（仅当 Phase 1 找不到设备时）

### 2.1 设备处于 AP 模式

设备未配置 WiFi 时，会自动进入 AP 模式，热点名为 `ESP32_LED_XXXXXXXX`。

**告知用户：**
```
未发现设备。请按以下步骤配网：

1. 用手机或电脑连接 WiFi 热点：ESP32_LED_XXXXXXXX
2. 浏览器打开 http://192.168.4.1/
3. 在 WebUI 中点击"修改"按钮
4. 输入你的 WiFi 名称和密码，点击"保存并重启"
5. 设备重启后会自动连接你的 WiFi
6. 完成后告诉我，我会重新扫描设备
```

### 2.2 用户确认配网完成后

重新执行 Phase 1 发现设备。

---

## Phase 3: BLE 配对连接（可选）

发现设备后，**询问用户**：
```
是否启用蓝牙连接？启用后即使 WiFi 断开也能通过蓝牙控制 LED。

蓝牙发现模式 120 秒后自动关闭，配对后下次连接更快。
```

### 3.1 如果用户同意

```bash
# 1. 开启 BLE 发现（120 秒广播）
curl -s "http://<DEVICE_IP>/bledisc?en=1"

# 2. 扫描 BLE 设备（模糊匹配 ESP32_LED_ 前缀）
python -c "
import asyncio
from bleak import BleakScanner

async def scan():
    devices = await BleakScanner.discover(timeout=5.0, return_adv=True)
    for d, adv in devices.values():
        name = d.name or ''
        if 'ESP32_LED' in name:
            print(f'Found: {name} [{d.address}]')
            return d.address
    return None

addr = asyncio.run(scan())
if addr:
    print(f'BLE_DEVICE={addr}')
"
```

### 3.2 自动配对连接

```bash
# 用 BLE 发送状态（首次连接自动 Just Works 配对）
python transport/send.py --transport ble thinking
```

**如果配对成功，告知用户**：
```
蓝牙已配对！地址: <BLE_ADDR>
以后连接会自动使用存储的密钥加密。
如需清除配对，访问 WebUI → 蓝牙设置 → 清除配对。
```

**如果配对失败（超时 / BLE 设备未找到 / 连接被拒），告知用户**：
```
BLE 自动配对失败。请尝试手动配对：

1. 确保设备 WebUI 中已开启蓝牙发现：
   http://<DEVICE_IP>/bledisc?en=1
2. 在你的电脑上打开蓝牙设置，搜索蓝牙设备
3. 找到 "ESP32_LED_XXXXXXXX" 设备并连接/配对
4. 配对完成后告诉我，我会重新尝试 BLE 连接

如果多次失败，WiFi 传输仍然可用，不影响 LED 控制。
```

---

## Phase 4: 配置设备参数（可选）

### 4.1 设置闲置超时

```bash
# 默认 120 秒，0 = 常亮不待机
curl -s "http://<DEVICE_IP>/idle_timeout?t=120"
```

### 4.2 设置亮度

```bash
# 亮度 1-255
curl -s "http://<DEVICE_IP>/brightness?b=128"
```

### 4.3 设置语言

```bash
# zh 或 en
curl -s "http://<DEVICE_IP>/lang?l=zh"
```

---

## Phase 5: 安装

### 架构说明（Agent 必读）

```
Skill (SKILL.md)  →  告诉 Agent "什么时候" 调用什么状态    ← 必装
MCP Server        →  提供 send_led_state 等工具             ← 必装
Hooks             →  自动触发，Agent 无需手动调用             ← 可选增强
```

**Skill + MCP Server 是必装的。** Hooks 是可选的自动化增强。

### 5.1 询问用户：平台

**首先确定用户使用的 AI 编码工具和操作系统，不同平台的安装路径不同。**

**Agent 询问用户：**

```
AI Status LED 安装设置

1. 你使用的 AI 编码工具是？
   🅰  Claude code （支持自动 hooks）
   🅱  Codex （支持自动 hooks）
   🅲  Cursor / 其他 （仅手动模式,或者查询你的工具是否支持hooks触发，让AI阅读代码进行改写支持）

（A/B 可以自动安装 hooks，C 只能手动模式 + MCP）
```

#### 平台路径速查表

**Claude Code：**

| 项目 | Linux / macOS | Windows |
|------|--------------|---------|
| 全局配置目录 | `~/.claude/` | `%USERPROFILE%\.claude\` |
| 全局 hooks | `~/.claude/settings.json` | `%USERPROFILE%\.claude\settings.json` |
| 全局 skills | `~/.claude/skills/<name>/` | `%USERPROFILE%\.claude\skills\<name>\` |
| 全局 MCP | `~/.claude.json` | `%USERPROFILE%\.claude.json` |
| 项目配置目录 | 项目根 `.claude/` | 项目根 `.claude\` |
| 项目 hooks | `.claude/settings.json`（可提交）或 `.claude/settings.local.json`（gitignored） | 同左 |
| 项目 skills | `.claude/skills/<name>/` | `.claude\skills\<name>\` |
| 项目 MCP | `.mcp.json`（项目根） | `.mcp.json` |

**Codex CLI：**

| 项目 | Linux / macOS | Windows |
|------|--------------|---------|
| 全局配置目录 | `~/.codex/` | `%USERPROFILE%\.codex\` |
| 全局 hooks | `~/.codex/hooks.json` | `%USERPROFILE%\.codex\hooks.json` |
| 全局 skills | `~/.agents/skills/<name>/` | `%USERPROFILE%\.agents\skills\<name>\` |
| 全局 MCP | `~/.codex/config.toml` | `%USERPROFILE%\.codex\config.toml` |
| 项目 hooks | `.codex/hooks.json` | `.codex\hooks.json` |
| 项目 skills | `.agents/skills/<name>/` | `.agents\skills\<name>\` |
| 项目 MCP | `.codex/config.toml` | `.codex\config.toml` |

> **Codex MCP 是 TOML 格式，不是 JSON。** `install.py` 会自动生成正确的 TOML 配置。
>
> **Claude MCP 全局是 `~/.claude.json`（JSON），项目级是 `.mcp.json`（项目根）。**

### 5.2 如果是 Claude Code 或 Codex CLI（自动模式可用）

**继续询问作用域：**

```
2. 安装范围：
   G) 全局 — 所有项目都可用
   P) 仅当前项目
```

#### 5.2.1 执行自动安装命令

```bash
# Claude Code — 自动模式 + 全局
python skill/install.py --target claude --scope global

# Claude Code — 自动模式 + 仅当前项目
python skill/install.py --target claude --scope project

# Codex — 自动模式 + 全局
python skill/install.py --target codex --scope global

# Codex — 自动模式 + 仅当前项目
python skill/install.py --target codex --scope project
```

**install.py 做了什么：**

| 步骤 | Claude + 全局 | Claude + 项目 | Codex + 全局 | Codex + 项目 |
|------|--------------|--------------|-------------|-------------|
| Skill | `~/.claude/skills/esp32-led/` | `.claude/skills/esp32-led/` | `~/.agents/skills/esp32-led/` | `.agents/skills/esp32-led/` |
| Hooks | `~/.claude/settings.json` | `.claude/settings.local.json` | `~/.codex/hooks.json` | `.codex/hooks.json` |
| MCP | `~/.claude.json`（JSON） | `.mcp.json`（JSON） | `~/.codex/config.toml`（TOML） | `.codex/config.toml`（TOML） |

> Win 路径将 `~/` 替换为 `%USERPROFILE%\`，`/` 替换为 `\`。

### 5.3 如果是 Cursor / 其他工具（仅手动模式）

**不支持自动 hooks，需要用户手动完成以下步骤。**

#### 5.3.1 安装 Skill

**Agent 告知用户：**

```
Hooks 自动触发在你的平台不可用。你需要手动遵守 SKILL.md 中的强制调用规则。

Skill 文件位于: <PROJECT_DIR>/skill/SKILL.md
请按你使用的工具文档，将 SKILL.md 注册为 skill/rule。
```

#### 5.3.2 安装 MCP Server

**Agent 告知用户：**

```
MCP Server 位于: <PROJECT_DIR>/transport/server.py

在你的 AI 工具 MCP 配置中添加：
{
  "mcpServers": {
    "esp32-led": {
      "command": "python",
      "args": ["<PROJECT_DIR>/transport/server.py"]
    }
  }
}
```

#### 5.3.3 Agent 强制调用规则

**在手动模式下（无 hooks），Agent 必须手动调用 MCP 工具。以下规则适用于所有平台的手动模式。**

| 触发条件 | 必须调用 |
|---------|---------|
| 收到用户消息，开始处理 | `send_led_state(state="thinking")` |
| 执行 Edit / Write / 写入文件 | `send_led_state(state="coding")` |
| 执行 Bash / 终端命令 | `send_led_state(state="busy")` |
| 等待用户确认或输入 | `send_led_state(state="waiting")` |
| 用户做出选择或回复后 | `send_led_state(state="thinking")` |
| 任务成功完成 | `send_led_state(state="success")` |
| 命令执行失败 | `send_led_state(state="error")` |
| 遇到 API 错误或异常 | `send_led_state(state="alarm")` |
| 会话空闲或任务结束 | `send_led_state(state="off")` |

优先使用 MCP 工具，不可用时回退 CLI：
```bash
python "<PROJECT_DIR>/transport/send.py" <state>
```

### 5.4 确认安装结果

```bash
# 验证 send.py 可用
python transport/send.py off
```

**Agent 按平台告知用户安装位置（Agent 需根据当前 OS 选择对应路径）：**

**Claude Code + 自动 + 全局：**
```
安装完成！

Linux/macOS:
  Hooks: ~/.claude/settings.json
  MCP:   ~/.claude.json
  Skill: ~/.claude/skills/esp32-led/

Windows:
  Hooks: %USERPROFILE%\.claude\settings.json
  MCP:   %USERPROFILE%\.claude.json
  Skill: %USERPROFILE%\.claude\skills\esp32-led\
```

**Claude Code + 自动 + 项目：**
```
安装完成！

  Hooks: .claude/settings.local.json
  MCP:   .mcp.json
  Skill: .claude/skills/esp32-led/
```

**Codex + 自动 + 全局：**
```
安装完成！

Linux/macOS:
  Hooks: ~/.codex/hooks.json
  MCP:   ~/.codex/config.toml (TOML)
  Skill: ~/.agents/skills/esp32-led/

Windows:
  Hooks: %USERPROFILE%\.codex\hooks.json
  MCP:   %USERPROFILE%\.codex\config.toml (TOML)
  Skill: %USERPROFILE%\.agents\skills\esp32-led\
```

**Codex + 自动 + 项目：**
```
安装完成！

  Hooks: .codex/hooks.json
  MCP:   .codex/config.toml (TOML)
  Skill: .agents/skills/esp32-led/
```

**手动模式（Cursor / 其他）：**
```
安装完成！（手动模式）

Skill 来源:  skill/SKILL.md — 请手动导入到你的工具
MCP Server:  transport/server.py — 请手动配置到你的工具 MCP 设置
验证命令:    python transport/send.py off
```

**所有平台通用的后续使用方式：**
```
使用方式：
- 自动: hooks 触发 LED 变色（无需操作）
- 手动: python transport/send.py <state>
- MCP:   send_led_state(state="thinking")
- WebUI: http://<DEVICE_IP>/
```

---

## Phase 6: 卸载

```bash
python skill/install.py --uninstall
# 或指定目标
python skill/install.py --uninstall --target claude
python skill/install.py --uninstall --target codex
```

---

## API 速查表

| 端点 | 方法 | 参数 | 说明 |
|------|------|------|------|
| `/status` | GET | - | JSON 状态 |
| `/set?s=<state>` | GET | s=thinking/coding/... | 设置 LED 状态 |
| `/brightness?b=128` | GET | b=1-255 | 亮度 |
| `/ble?en=1\|0` | GET | en=1/0 | BLE 开关 |
| `/bledisc?en=1\|0` | GET | en=1/0 | BLE 发现模式 |
| `/ble_unbond` | GET | - | 清除 BLE 配对 |
| `/idle_timeout?t=120` | GET | t=0-3600 | 闲置超时（秒） |
| `/wifi?ssid=x&pwd=y` | GET | ssid,pwd | 配网（会重启） |
| `/lang?l=zh\|en` | GET | l=zh/en | 语言 |
| `/scan` | GET | - | WiFi 扫描结果 |

## MCP 工具

| 工具 | 参数 | 说明 |
|------|------|------|
| `send_led_state` | state, transport? | 发送状态 |
| `get_led_status` | - | 查询状态 |
| `list_led_states` | - | 列出 8 种状态 |

## send.py CLI

```bash
python transport/send.py <state>                    # auto 传输
python transport/send.py --transport wifi <state>   # 仅 WiFi
python transport/send.py --transport ble <state>    # 仅 BLE
python transport/send.py --host 192.168.1.100 <state>  # 指定 IP
```
