# Vibe Coding AI 状态指示灯

ESP32-C3 WS2812 8 灯珠 RGB 状态指示器。AI 编码工具通过 WiFi TCP / BLE GATT 实时同步工作状态到物理 LED 灯带。

支持 9 个 AI 平台：**Claude Code / Codex CLI / MiMoCode / OpenCode / Cursor / Windsurf / Trae / TraeCN / OpenClaw**。

---

## 项目结构

```
coding light/
├── firmware/esp32c3_ws2812/        ESP32 固件 (ESP-IDF v5.5.3)
│   ├── main/ws2812_scheme_b.c      主程序 (2832 行, v3.4)
│   ├── main/ws2812_chase_test.c    跑马灯测试固件
│   ├── main/CMakeLists.txt         组件构建配置
│   ├── CMakeLists.txt              顶层项目
│   ├── build.cmd                   增量编译
│   ├── flash.cmd                   编译 + 烧录 COM16
│   ├── merge.cmd                   生成 0x0 合并固件
│   ├── build_flash.cmd              编译 + 烧录 + 合并 一键
│   └── rebuild.cmd                 编译 + 烧录 + 合并 一键
├── transport/                      Python 传输层
│   ├── send.py                     Hooks 桥接 + CLI 入口
│   ├── server.py                   MCP 服务端 (13 工具)
│   ├── transport_wifi.py           WiFi TCP 传输 + 设备发现
│   └── transport_ble.py            BLE GATT 传输 + OS 配对 MAC 自动发现
├── hooks/                          Hooks 模板
│   ├── claude_hooks.json           Claude Code hooks (14 事件, --platform claude)
│   ├── windsurf_hooks.json          Windsurf hooks (12 事件, --platform windsurf)
│   └── codex_hooks.json            Codex CLI hooks (10 事件, --platform codex)
├── plugin/                         JS 插件
│   └── 3dai-led.js                OpenCode / MiMoCode 插件
├── skill/                          AI Skill 配置
│   ├── SKILL.md                    Hooks/Plugin 平台 Skill
│   └── SKILL-mcp.md                MCP 平台 Skill
├── mcp.json                        MCP 服务注册配置
├── install.py / install.sh         安装启动器
├── electron-manager/               Electron 桌面安装管理器
│   ├── main.js / preload.js        主进程 + 上下文桥接
│   ├── renderer/                   前端 UI
│   └── dist/linux-unpacked/        Linux 发布包 (嵌入资源)
├── VibeCoding_LED_manager/         独立发布仓库
├── .github/workflows/              CI/CD (push 自动构建三平台)
├── release/                        发布目录
│   ├── win/                        Windows 发布包
│   ├── unix/                       Linux/macOS 发布包
│   └── firmware/                   合并固件
└── docs/                           项目文档
```

---

## 硬件

| 项目 | 说明 |
|------|------|
| 芯片 | ESP32-C3 SuperMini (RISC-V 160MHz, QFN32) |
| Flash | 4MB (XMC) |
| LED | WS2812 5050 RGB 8 灯珠, GPIO8, RMT 驱动 |
| 时序 | 10MHz RMT, GRB 颜色格式 |
| 烧录 | USB-C 直连, 内置 USB-UART (CH340 兼容) |
| 接线 | GPIO8 → 100Ω → 灯板 DI, 5V → 灯板 4.7VDC, GND → GND |

---

## 系统架构

```
┌──────────────────────────────────────────────────────────────┐
│                         AI 工具层                             │
│  Claude Code         Codex CLI        MiMoCode     OpenCode  │
│  settings.json       hooks.json       JS 插件      JS 插件   │
│  --platform claude   --platform codex --p opencode --p mimo   │
└───────┬─────────────────┬────────────────┬───────────┬───────┘
        │                 │                │           │
        └────────┬────────┘                └─────┬─────┘
                 │                               │
           send.py stdin                   send.py <state>
           (JSON payload)                  (CLI args)
                 │                               │
        ┌────────┴──────┐                        │
        ▼               ▼                        ▼
  ┌──────────┐   ┌──────────────┐          ┌──────────┐
  │ WiFi TCP │   │  BLE GATT    │          │ WiFi TCP │
  │ :8080    │   │ 128-bit UUID │          │ :8080    │
  └────┬─────┘   └──────┬───────┘          └────┬─────┘
       │                │                       │
       └───────┬────────┘                       │
               ▼                                ▼
  ┌──────────────────────────────────────────────────────────┐
  │                  ESP32-C3 固件 (v3.4)                     │
  │                                                           │
  │  TCP :8080          │ HTTP :80 (WebUI + REST API)         │
  │  BLE GATT Server    │ mDNS: 3dai-led-XXXXXXXX.local      │
  │  WiFi STA/AP 双模   │ NVS 凭证持久化                       │
  │  8 种 LED 动画      │ per-LED 独立状态 + 项目分配          │
  │  多项目平台隔离      │ 64 位微秒级空闲超时                  │
  └──────────────────────────────────────────────────────────┘
```

---

## 通信协议

### WiFi TCP (端口 8080, 首选)

```
客户端连接 :8080
→ 接收 greeting: "3dai-led 3dai-led-7604F1A8 ready\n"
→ 发送命令:    "thinking myproject platform:claude\n"
→ 接收响应:    "ok 0:thinking\n"
```

| 命令 | 响应 | 说明 |
|------|------|------|
| `ping` | `pong` | 连接探测 |
| `status` | `0:state 1:state ...` | 查询所有 LED 状态 |
| `state` | `ok N:state` | 设置 LED 0 状态 (单项目模式) |
| `state project` | `ok N:state` | 按项目名自动分配 LED |
| `state project platform:name` | `ok N:state` | 多平台隔离分配 |

- 超时 10s, 3 次重试 (0.15s, 0.30s 递增)
- greeting 校验 `"3dai-led" in greeting` 防误连
- 每连接单行发送, 支持多条命令

### BLE GATT (回退传输)

```
Service UUID:    0000ff00-0000-1000-8000-00805f9b34fb
Write Characteristic: 0000ff01-0000-1000-8000-00805f9b34fb
Notify Characteristic: 0000ff02-0000-1000-8000-00805f9b34fb
Device Name Prefix: 3DAi_LED_
```

- 配对: Just Works (ESP_IO_CAP_NONE), Secure Connections + Bonding
- GATT 写权限不要求加密 (ESP_GATT_PERM_WRITE, 无 _ENCRYPTED)
- BLE 广播持久开启, `ble_enabled` 运行时开关
- `ble_discoverable` 120s 自动关 (仅影响扫描列表名称可见, 广播不停止)

### HTTP REST API (端口 80)

| 端点 | 方法 | 参数 | 说明 |
|------|------|------|------|
| `/` | GET | `?lang=zh\|en` | WebUI 控制面板 |
| `/dash` | GET | — | 项目看板 (8 LED 网格) |
| `/help` | GET | — | 状态说明 + 测试预览 |
| `/status` | GET | — | JSON 设备完整状态 |
| `/set` | GET | `?s=<state>&project=<n>&platform=<n>&led=<n>&test=1` | 设置 LED 状态 |
| `/brightness` | GET | `?b=<1-255>` | 亮度调节 |
| `/mode` | GET | `?m=0\|1` | 单/多项目模式切换 |
| `/ledmap` | GET | — | JSON: 每 LED 状态+项目+平台 |
| `/ble` | GET | `?en=0\|1` | 运行时开关 BLE 子系统 |
| `/bledisc` | GET | `?en=0\|1` | BLE 发现模式 (120s 自动关) |
| `/ble/clearbonds` | GET | — | 清除所有 BLE 配对 |
| `/idle_timeout` | GET | `?t=<0-3600>` | 闲置超时秒数 (0=常亮) |
| `/wifi` | GET | `?ssid=x&pwd=y` | WiFi 配网 (保存后重启) |
| `/static_ip` | GET | `?ip=x&gw=y&mask=z&en=1\|0` | 固定 IP 配置 |
| `/scan` | GET | — | 附近 WiFi 扫描 (JSON) |
| `/lang` | GET | `?l=zh\|en` | WebUI 语言切换 |

---

## LED 状态与动画

| 状态 | 英文名 | 灯光效果 | 技术实现 | 典型触发 |
|------|--------|---------|---------|---------|
| 思考中 | `thinking` | 高速彩虹旋转 | HSV→RGB, 360° hue 每 LED +45° 偏移, 360ms 周期 | 收到用户消息 |
| 编码中 | `coding` | 青→紫液态呼吸 | HSV 170°+n×5° hue spread, sin 呼吸 3s 周期 | 编辑/写入文件 |
| 执行中 | `busy` | 黄色双向扫描 (单灯) 或 per-LED 脉冲 (多灯) | 单灯: 3-LED dark-bright-dark 梯度扫描, 1s 周期. 多灯: 80ms attack+80ms decay+640ms off, 交错偏移 | 运行终端命令 |
| 等待中 | `waiting` | 红色呼吸 | sin 呼吸 3s 周期, brightness 10%~55% | 等待用户确认 |
| 完成 | `success` | 绿色呼吸 | sin 呼吸 3s 周期, 10%~55% | 任务成功 |
| 出错 | `error` | 红→橙三连快闪 | 3 组 50ms burst, 750ms 周期, R=255 G=0.12×R | 命令执行失败 |
| 告警 | `alarm` | 红蓝交替翻转 | 180ms 间隔翻转 R=255 ↔ B=220 | API 异常 |
| 待机 | `off` | 全灭 | 全部 LED 熄灭, 释放项目槽位 | 会话结束 |

- 帧率: ~66 FPS (15ms 循环)
- 亮度: 0-255 全局 scaling (brightness/256)
- 物理 LED 序号: 反向 (LED[0]=最右, 软件自动映射)
- 单项目模式: LED[0] 驱动全灯带 full-strip 动画
- 多项目模式: 每 LED 独立 per-LED 动画

---

## 多项目 + 平台隔离

多项目模式下 8 颗 LED 独立分配, 每颗对应一个 `(platform, project)` 组合:

```
复合键: "platform:project"
分配顺序: LED 1→5→3→7→2→4→6→8 (奇偶交错优先级)

例:
  Claude Code → coding light     → LED 1 (key: claude:coding light)
  Codex CLI   → coding light     → LED 5 (key: codex:coding light)
  OpenCode    → my-project       → LED 3 (key: opencode:my-project)
```

- **复合键**: `platform:project` 作为 NVS 分配键, 同项目不同平台不冲突
- **CLI 格式**: `state project_name platform:platform_name` (project 在 platform 之前)
- **Hooks 自动**: `claude_hooks.json` 传 `--platform claude`, `codex_hooks.json` 传 `--platform codex`
- **插件传参**: OpenCode `--platform opencode`, MiMoCode `--platform mimocode`
- **MCP 传参**: `send_led_state(project="x", platform="cursor")`
- **项目看板** `/dash`: 8 格表格 (# | Platform | Project | Status)
- **LED 释放**: 显式 `off` 或空闲超时 (默认 120s) 自动释放槽位

---

## BLE 自动发现

三平台自动从 OS 配对列表获取 MAC 地址, **无需手动 `--bind-ble`**:

| 平台 | 方法 | 延迟 | 匹配策略 |
|------|------|------|---------|
| Windows | 注册表 `HKLM\...\BTHPORT\Devices` 读已配对 MAC | 0ms | 后缀模糊匹配 (bytes 2-4, 兼容 BLE MAC 偏移) |
| Linux | BlueZ 缓存 `/var/lib/bluetooth/*/info` 读设备名 | 0ms | `Name=3DAi_LED_` 前缀 + 后缀匹配 |
| macOS | BleakScanner 回退 (CoreBluetooth 缓存) | <1s | 名称前缀过滤 |

```
流程:
  1. 读 OS 配对列表 → 取出 MAC (0ms)
  2. 缓存到 ~/.local/share/3dai-led/.esp32_ble_device_id
  3. BleakClient 连上 MAC → 写 GATT 发状态

后续: 直接从缓存文件读 MAC, 0ms 取地址
```

`send.py --bind-ble <MAC>` 仍可用作手动回退, 覆盖自动发现。

---

## WiFi 设备发现与 IP 缓存

### 三级查询路径

```
1. $ESP32_HOST 环境变量           → 瞬间 (用户手动指定)
2. .3dai_ip_cache 缓存 + TCP 探测 → <150ms (0.15s timeout)
3. 子网后台扫描 (独立子进程)        → 不阻塞当前请求
```

### 快速路径 (每次 hook 调用)

1. 读 `ESP32_HOST` 环境变量 (0ms)
2. 读缓存文件 `~/.local/share/3dai-led/.3dai_ip_cache`
3. TCP 0.15s 探测验证:
   - **有设备绑定**: 校验 greeting 中的 device ID 匹配
   - **无设备绑定**: 任意 3dai-led 设备应答即用

### 后台子网扫描

缓存失效时 `subprocess.Popen` 启动独立 Python 子进程:

```
扫描范围: 当前子网 .1–.254 (自动检测)
超时: 总 45s, 每 IP 0.15s
结果: 写入 .3dai_ip_cache → 删 .3dai_scan_lock
失败: 写入 .3dai_last_fail
```

- 子进程独立于 hooks, send.py 退出后继续运行
- 锁文件防并发 (30s 内不重复启动)
- 结果在**下一次 hook 调用**生效

### 设备绑定

```bash
python transport/send.py --bind 3dai-led-7604F1A8    # WiFi
python transport/send.py --bind-ble AA:BB:CC:DD:EE:FF  # BLE (手动回退)
python transport/send.py --unbind                       # 解除 WiFi
python transport/send.py --unbind-ble                   # 解除 BLE
python transport/send.py --list                         # 扫描局域网
```

---

## AI 工具集成

### 平台对比

| 特性 | Claude Code | Codex CLI | MiMoCode | OpenCode |
|------|------------|-----------|----------|----------|
| 机制 | settings.json hooks | hooks.json | JS 插件 | JS 插件 |
| 调用方式 | send.py stdin JSON | send.py stdin JSON | send.py CLI args | send.py CLI args |
| platform 传参 | hooks --platform claude | hooks --platform codex | spawn --platform opencode | spawn --platform mimocode |
| 事件数 | 14 | 10 | 9 | 9 |

| MCP 配置 | settings.json mcpServers | config.toml | claude.json (共享) | claude.json (共享) |

> **Codex 注意**：需要在 `~/.codex/config.toml` 中加 `[features] hooks = true` 启用 hooks。安装器已自动写入。

### send.py 双入口

```
main()
  ├─ stdin 有数据 (Claude / Codex hooks)
  │   → JSON 解析 → _get_hook_event() 提取事件名
  │   → _get_hook_cwd() 提取项目名
  │   → _infer_state() 事件→状态映射
  │   → _should_send_hook_state() 跨进程去重
  │   → _parse_platform_from_argv() 取 --platform
  │   → _send_state(platform=platform_arg)
  │
  └─ stdin 无数据 (MiMoCode / OpenCode / CLI)
      → argparse 解析 CLI args
      → _send_state(state, project=args.project, platform=args.platform)
```

### Hooks 事件映射 (Claude Code / Codex CLI)

| 事件 | 状态 | 特殊逻辑 |
|------|------|---------|
| SessionStart | off | — |
| UserPromptSubmit | thinking | — |
| PreToolUse (Bash) | busy | — |
| PreToolUse (Edit/Write/MultiEdit) | coding | — |
| PostToolUse | thinking | 含 permission_decision 处理 |
| PermissionRequest | waiting | — |
| PostToolUseFailure | error | — |
| Stop | success | bg_tasks → busy |
| StopFailure | alarm | — |
| SubagentStart | thinking | 子代理名拼入 project |
| SubagentStop | off | — |
| PreCompact | busy | — |
| PostCompact | thinking | — |
| SessionEnd | off | — |
| Notification | waiting/off/success | 按 notification_type 分发 |

### JS 插件事件映射 (MiMoCode / OpenCode)

| 事件 | 状态 |
|------|------|
| message.updated | thinking |
| session.updated | thinking |
| permission.asked | waiting |
| permission.replied | thinking |
| session.created | thinking |
| session.idle | off |
| session.error | error |
| session.compacted | busy |

### 子代理隔离

Claude Code 子代理自动分配独立 LED, 按 `项目名:代理类型` 命名:

| 代理类型 | 拼接后的 project 名 |
|---------|-------------------|
| Explore | `myproject:explore` |
| Plan | `myproject:plan` |
| general-purpose | `myproject:general-purpose` |

`send.py` 检测 `SubagentStart` payload 中 `agent_type` 字段自动拼接。平台前缀确保同项目 Claude/Codex 子代理不冲突: `claude:myproject:explore` vs `codex:myproject:explore`。

---

## MCP 服务

### 配置

```json
{
  "mcpServers": {
    "3dai-led": {
      "command": "python",
      "args": ["C:\\Users\\...\\transport\\server.py"]
    }
  }
}
```

### 工具列表

**状态控制:**
| 工具 | 参数 | 说明 |
|------|------|------|
| `send_led_state` | state, transport?, project?, platform? | 发送 LED 状态 |
| `get_led_status` | — | 当前状态 (离线返回本地缓存) |
| `get_led_map` | — | 8 LED 项目+平台分配表 |
| `list_led_states` | — | 8 种状态及中文说明 |

**设备配置 (HTTP API):**
| 工具 | 参数 | 说明 |
|------|------|------|
| `get_device_info` | — | 完整设备状态 JSON |
| `set_led_brightness` | level: 1-255 | 亮度 |
| `configure_device_ble` | enabled: bool | 运行时开关 BLE (停止/恢复广播) |
| `set_ble_discoverable` | enabled: bool | BLE 扫描可见 (120s 自动关) |
| `clear_ble_bonds` | — | 清除 BLE 配对 |
| `set_idle_timeout` | seconds: 0-3600 | 空闲超时 |
| `set_device_language` | language: zh/en | WebUI 语言 |
| `scan_wifi` | — | 设备端 WiFi 扫描 |
| `set_static_ip` | ip, gateway, netmask, enabled | 固定 IP 配置 |

### BLE 传输 (MCP 内)

```python
# server.py: async 直接 await, 避免嵌套事件循环问题
async def _try_ble(state):
    return await send_state_ble_async(state, timeout=5.0)
```

---

## 固件技术细节

### 编译环境

- ESP-IDF v5.5.3
- RISC-V 工具链: riscv32-esp-elf-gcc 14.2.0
- CMake + Ninja
- Windows 终端 (非 MSys/MinGW)

### 编译烧录

```cmd
cd firmware\esp32c3_ws2812
build.cmd          # 增量编译
flash.cmd          # 编译 + 烧录 COM16
rebuild.cmd        # 编译 + 烧录 + 合并固件到 release/
merge.cmd          # 仅合并 (不编译)
```

### Flash 布局

| 偏移 | 内容 |
|------|------|
| 0x0 | bootloader |
| 0x8000 | partition_table |
| 0x10000 | vibecoding_status_led (固件) |

合并命令: `python -m esptool --chip esp32c3 merge_bin --flash_mode dio --flash_size 4MB --flash_freq 80m -o release/3dai_led_full.bin 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/ws2812_chase_test.bin`

### BLE 子系统

```
ble_enabled (运行时开关):
  ON  → ble_runtime_enable():  controller_enable → bluedroid_enable → start_advertising
  OFF → ble_runtime_disable(): stop_advertising → bluedroid_disable → controller_disable

ble_discoverable (扫描可见, 120s 自动关):
  ON  → 扫描列表中可见设备名
  OFF → 扫描列表不可见, BLE 广播继续 (仍可直接连接)
  ble_disc_timer_cb 120s 只设标志+NVS, 不停广播

配对: Just Works + Secure Connections + Bonding
GATT: 128-bit UUID, 无加密要求, MTU 500
```

### WiFi 子系统

```
STA+AP 双模:
  有 NVS 凭据 → STA 连接 (20s 超时)
  连接失败   → AP 回退 (WIFI_MODE_APSTA)
               SSID: 3DAi_LED_XXXXXXXX
               IP:   192.168.4.1
  STA 成功   → WIFI_MODE_STA + mDNS (3dai-led-XXXXXXXX.local)

重试: 最多 5 次, 耗尽后清除 NVS 凭据并重启
静态 IP: NVS 持久化, 重启生效
```

### LED 分配算法

```c
// 奇偶交错优先级: LED 1→5→3→7→2→4→6→8
static const uint8_t alloc_order[8] = {0, 4, 2, 6, 1, 3, 5, 7};

// 复合键: "platform:project"
static char alloc_keys[LED_COUNT][48];
static char project_names[LED_COUNT][32];
static char platform_names[LED_COUNT][16];
```

### 状态管理

```
每 LED 独立状态:
  led_states[8]      当前渲染状态
  target_states[8]   已确认目标状态
  pending_states[8]  等待 debounce 的状态
  pending_since_us[8] debounce 计时器

Debounce: 50ms (STATE_DEBOUNCE_US)
空闲超时: per-LED 微秒级, 可配置 0-3600s (默认 120s)

测试模式:
  test_led_states[]  独立测试数组, 不污染真实状态
  test_mode_active   跳过空闲超时, JS 定时器控制
```

### 组件依赖 (CMakeLists.txt)

```
led_strip  spi_flash  nvs_flash  esp_http_server
esp_wifi   esp_event  lwip       esp_netif
mdns       bt
```

---

## 跨进程状态去重

`~/.local/share/3dai-led/.3dai_last_state`:

- 相同状态跳过 (不重复发送 TCP/BLE)
- `coding` 状态 500ms 防抖 (thinking→coding 快速切换时忽略 coding)
- 随机抖动 0-10ms 减少多进程竞态窗口

---

## 日志

`~/.local/share/3dai-led/send_debug.log`:

```
2025-01-01T12:00:00 | thinking | OK | event=UserPromptSubmit | project=mydir | transport=auto used=wifi (192.168.1.15)
2025-01-01T12:00:05 | thinking | DEDUP | event=UserPromptSubmit | project=mydir
```

- 超 1MB 自动轮转 `.1` → `.5`, 最多 6 文件
- 所有 I/O 操作静默容错

---

## 安装

### 启动器

| 平台 | 命令 |
|------|------|
| Windows | `install.bat` (GBK 编码, 中文菜单) |
| macOS/Linux | `bash install.sh` (UTF-8) |

### 命令行

```bash
# 指定平台
python install.py --target claude --scope global
python install.py --target codex --scope project
python install.py --target mimocode --scope project
python install.py --target opencode --scope project

# 全部平台
python install.py --target all --scope project

# 卸载
python install.py --uninstall --target claude --scope global
```

- `--scope global`: 安装到 `~/.local/share/3dai-led/transport/`, 所有项目共享
- `--scope project`: 安装到当前项目 `.claude/` `.codex/` 等目录
- `sys.executable` 绝对路径, 无 `python` PATH 依赖
- MCP 注册为单层扁平结构

---

## 发布目标同步

每次修改源文件后同步:

| 源 | 目标 |
|---|------|
| `transport/*.py` | `release/win/transport/` `release/unix/transport/` `VibeCoding_LED_manager/transport/` `electron-manager/dist/linux-unpacked/resources/transport/` `~/.local/share/3dai-led/transport/` |
| `hooks/*.json` | `release/win/hooks/` `release/unix/hooks/` `VibeCoding_LED_manager/hooks/` `electron-manager/dist/linux-unpacked/resources/hooks/` |
| `skill/*.md` | `release/win/skill/` `release/unix/skill/` `electron-manager/dist/linux-unpacked/resources/skill/` |
| `mcp.json` | `release/win/mcp.json` `release/unix/mcp.json` `electron-manager/dist/linux-unpacked/resources/mcp.json` |

---

## CI/CD

Push 到 GitHub 自动构建三平台单文件安装器:

| 平台 | 产物 |
|------|------|
| Windows | `Vibe-Coding-LED-Manager-1.0.0-win-x64.exe` |
| macOS | `Vibe-Coding-LED-Manager-1.0.0-mac.dmg` |
| Linux | `Vibe-Coding-LED-Manager-1.0.0-linux.AppImage` |

独立仓库 `VibeCoding_LED_manager/` → `VodooWaWa/VibeCoding_LED_manager` → `.github/workflows/build.yml`。

---

## 安全

- 局域网信任模型, 无应用层认证
- BLE Just Works 配对 + Secure Connections + Bonding
- BLE GATT 写不要求加密 (局域网内操作)
- HTTP WiFi 密码 `type="password"` + URL 编解码
- ESP32 AP 模式开放无密码 (WIFI_AUTH_OPEN)

---

## 许可证

MIT
