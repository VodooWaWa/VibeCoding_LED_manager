# ESP32-C3 AI 状态灯固件 (v3.5.1)

基于 ESP-IDF v6.0.1 的 WS2812 智能 LED 状态指示灯固件。

## 功能

- **8 颗 WS2812 LED** 多状态动画（thinking/coding/busy/waiting/success/error/alarm/off）
- **多项目平台隔离**：不同 `platform:project` 自动分配独立 LED
- **WebUI 配置**：WiFi 配网、亮度、语言、静态 IP、BLE 开关、空闲超时
- **TCP 控制**：端口 8080，文本命令 `state project platform:name`
- **BLE GATT**：写 `state project platform:name` 控制状态
- **WiFi AP 模式**：未配网时自动开启热点 `3DAi_LED_xxxx`

## 目录结构

```
esp32c3_ws2812/
├── main/
│   ├── ws2812_scheme_b.c    # 主固件源码（单文件）
│   ├── ws2812_chase_test.c  # LED 跑马灯测试
│   ├── CMakeLists.txt
│   └── idf_component.yml
├── webui.html               # WebUI 页面源码
├── sdkconfig.defaults       # 默认配置
├── flash.cmd                # 一键编译烧录 (COM16)
├── merge.cmd                # 合并 bootloader+partition+app 完整固件
└── release/                 # 编译产物（.bin 固件）
```

## 编译烧录

环境要求：ESP-IDF v6.0.1

```bash
# Windows
flash.cmd          # 编译 + 烧录到 COM16

# 其他平台
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

## 控制协议

### TCP (端口 8080)

```
连接后设备发送: 3dai-led ESP32C3_LED_xxxx ready
发送命令:       state project_name platform:name
响应:           ok <led_idx>:<state>
其他:           ping -> pong, status -> 各 LED 状态
```

### BLE GATT

- Service: `0000FF00-0000-1000-8000-00805F9B34FB`
- Write:   `0000FF01-0000-1000-8000-00805F9B34FB`
- Notify:  `0000FF02-0000-1000-8000-00805F9B34FB`

### HTTP (WebUI)

- `GET /status`  设备状态 JSON
- `GET /set?s=state&project=&platform=` 设置状态

## 版本历史

### v3.5.1
- 尝试修复 macOS BLE 发现失败（未验证）
- BLE 广播包重组：设备名随广播发送，不再依赖扫描响应
- 新增 USB Serial 调试命令通道（后续版本移除）

### v3.5
- LED 方向翻转开关
- WiFi AP 配网与回退（固定信道 6）
- BLE 广播开关（原为死代码）
- 静态 IP 生效（原被忽略）
- 修复 HTTP 响应分块 EAGAIN 截断

## 固件不能 OTA 升级

需要重新编译烧录。合并完整固件（bootloader + partition + app）：

```bash
merge.cmd  # 产物: ws2812_ai_status_led_full.bin
```
