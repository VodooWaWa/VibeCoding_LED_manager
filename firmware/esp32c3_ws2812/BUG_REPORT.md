# 固件严重缺陷报告

**文件:** `main/ws2812_scheme_b.c` (2960 行)
**芯片:** ESP32-C3 (单核 RISC-V, FreeRTOS)
**日期:** 2026-06-25

---

## 一、HTTP 全局响应缓冲区竞态条件 [严重]

**位置:** lines 1305-1328 — `resp_init()` / `resp_append()` / `resp_flush()` + 全局 `g_resp_buf`

### 问题

三个全局变量 `g_resp_buf`、`g_resp_len`、`g_resp_cap` 作为单例被所有 HTTP handler 共享。ESP-IDF 的 `httpd` 默认创建多个工作线程处理并发请求，没有任何互斥锁保护。

### 触发

```
请求 A: GET /        → resp_append("<!DOCTYPE html>...") → 正在拼接主页
请求 B: GET /dash    → resp_init()                       → g_resp_len = 0，截断 A
请求 A:              → resp_flush()                      → 发送 B 的片段到 A 的客户端
```

浏览器打开 WebUI 时 JS 每 2s 轮询 `/status`，同时用户可能点击 `/dash` 或 `/help`。两个 GET 并发打到服务器时触发，页面显示错乱或浏览器报 JSON 解析错误。

### WebUI 中的真实并发

| 页面 | 轮询频率 | 端点 |
|------|---------|------|
| `/` 主页 | 每 2s | `/status` |
| `/dash` 看板 | 每 2s | `/status` + `/ledmap` |
| `/help` 页面 | 按需 | `/set` |

用户快速切换页面或同时打开多个标签页时冲突必现。

---

## 二、WiFi 扫描后 BLE GATT 服务永久损坏 [严重]

**位置:** lines 1810-1923 — `handle_scan()`（扫描处理 → BLE 恢复代码）

### 问题

`handle_scan()` 临时禁用了 BLE 并在扫描后恢复，但恢复代码不完整——缺了 GATT 重注册步骤。

### ble_runtime_enable() vs handle_scan() 恢复代码

| 步骤 | `ble_runtime_enable()` (正确) | `handle_scan()` 恢复代码 (有缺陷) |
|------|:---:|:---:|
| `esp_bt_controller_enable()` | 有 | 有 |
| `esp_bluedroid_enable()` | 有 | 有 |
| `esp_ble_gap_set_device_name()` | 有 | **无** |
| `esp_ble_gatts_register_callback()` | 有 | **无** |
| `esp_ble_gap_register_callback()` | 有 | **无** |
| `esp_ble_gatts_app_register()` | 有 | **无** |
| `ble_start_advertising()` | 有 | 仅 `ble_discoverable` 时才调用 |

Bluedroid 的 disable/enable 周期会清除所有之前的 callback 注册。恢复时缺了这些步骤，BLE 无线层面虽已启用且可能广播，但 GATT write 到达时没有任何 handler 处理——BLE 命令**全部静默丢弃**。设备看似正常（扫描可见），实则 BLE 功能已死。

### 触发

WebUI 主页点击"扫描"按钮 → `GET /scan` → `handle_scan()` 被调用，BLE 被关闭再恢复，此后 BLE 永久失效，直至设备重启。

### 问题五的放大效应

WiFi 扫描的 JSON 分配如果失败（见问题五），AP 数组 `aps` 被释放后返回 500 错误，但 BLE **已经被禁用**且同样走这条残缺的恢复路径——即任何形式的 `handle_scan()` 执行都会毁掉 BLE。

---

## 三、JSON 响应缓冲区溢出风险 [高]

### 3a. `handle_status()` — line 1339

**缓冲区:** `static char json[768]`

**格式串:**
```c
"{\"state\":\"%s\",\"brightness\":%d,\"ssid\":\"%s\",\"ip\":\"%s\","
"\"ble_enabled\":%s,\"ble_discoverable\":%s,\"idle_timeout\":%lu,"
"\"multi_led\":%s,\"ble_bond_count\":%d,\"led_states\":[%s],\"states\":[%s]}"
```

**最坏情况字节估算:**

| 字段 | 最大字节 |
|------|---------|
| 格式串固定 JSON 骨架 | ~130 |
| `state_names[]` | 8 |
| `brightness` (255) | 3 |
| `esc_ssid` (每字符 `"`/`\` 需 `\` 前缀翻倍) | ~132 |
| `ip` (255.255.255.255) | 15 |
| 布尔值 ×3 | ~15 |
| `idle_timeout` (uint32 文本) | 10 |
| `bond_count` | 10 |
| `led_states_json` | 128 |
| `states_json` | 256 |
| **合计** | **~707** |

当前极端情况 707 < 768，**侥幸安全**。但只余 61 字节余量，一次字段扩展、SSID 增长或状态名变长即溢出。

### 3b. `handle_ledmap()` — line 1751（风险更高）

**缓冲区:** `static char json[768]`

**拼接逻辑:**
```c
int off = 0;
off += snprintf(json + off, sizeof(json) - off, "{\"l\":[");
for (int n = 0; n < LED_COUNT; n++) {
    off += snprintf(json + off, sizeof(json) - off, ...);   // 每 LED 最大 ~80 字节
}
off += snprintf(json + off, sizeof(json) - off, "],\"multi\":%s}", ...);
```

每 LED 条目含 `project_names[32]` + `platform_names[16]` + `state_names[]` + JSON 骨架 ≈ 80 字节。8 个 LED × 80 = 640 + 头尾 30 = **670 字节**。接近上限。

**核心隐患:** `snprintf` 截断时返回值为"本应写入的字节数"而非实际写入数，`off` 在截断后继续膨胀。后续 `sizeof(json) - off` 下溢为巨大正数时，`snprintf` 可越界写入栈内存（`static` 在 `.bss`/`.data` 段）。

---

## 四、TCP 逐字节读取 [中]

**位置:** lines 517-586

```c
while (1) {
    char c;
    int ret = recv(client_sock, &c, 1, 0);   // ← 一次一个字节
    if (ret <= 0) break;
    // ...
}
```

### 问题 1: 系统调用风暴

一条 `"thinking myproject platform:claude\n"` (约 40 字节) = 40 次 `recv()` → 40 次 lwIP 协议栈穿越 → 40 次上下文切换。ESP32-C3 160MHz 单核上这是巨大的不必要开销。

### 问题 2: 错误语义不分

`socket` 设置了 `SO_RCVTIMEO = 10s`，`recv()` 超时返回 `-1` 且 `errno = EAGAIN`，但当前代码统一 `break`，将**超时误判为断连**。同一条 `if (ret <= 0)` 路径也吃掉了 `EINTR`（信号中断），这些都是可恢复的临时状态。

### 问题 3: 无心跳检测

客户端静默断开但未发送 TCP FIN（如 WiFi 信号丢失），`recv` 会挂整整 10 秒才返回超时，连接资源在此期间被占用。

---

## 五、WiFi 扫描 JSON 内存分配失败 [中]

**位置:** line 1899

```c
char *json = malloc(ap_count * 96 + 8);
```

### 问题

ESP32-C3 可用堆约 300KB。`ap_count` 来自 `esp_wifi_scan_get_ap_num()`，在密集环境（商圈/写字楼）可达 50-200。

| `ap_count` | 分配大小 | 叠加 AP 记录数组后的瞬态分配 | 风险 |
|-----------|---------|---------------------------|------|
| 20 | 1,928 字节 | ~5.5 KB | 安全 |
| 50 | 4,808 字节 | ~13.8 KB | 安全 |
| 100 | 9,608 字节 | ~27.6 KB | 碎片后可能失败 |
| 200 | 19,208 字节 | ~55 KB | 高概率失败 |
| 500 | 48,008 字节 | ~138 KB | 几乎必定失败 |

### 失败后果

```c
if (!json) {
    free(aps);         // 正确释放，无泄漏
    ESP_LOGE(TAG, "Scan JSON malloc failed");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "malloc failed");
    return ESP_OK;
}
```

- AP 记录数组被释放 → 无内存泄漏
- 但 BLE **此前已被禁用**（lines 1827-1833），且残缺的恢复代码（见问题二）导致 BLE 进入**半死状态**
- 用户看到 "malloc failed" 错误，此后 BLE 命令全部失效，需重启设备

---

## 严重度排序

| # | 问题 | 严重度 | 触发条件 |
|---|------|:---:|------|
| 一 | HTTP 全局缓冲区竞态 | **严重** | 并发请求，必现 |
| 二 | BLE GATT 服务永久损坏 | **严重** | 任一次 WiFi 扫描，必现 |
| 三 | JSON 缓冲区溢出风险 | **高** | 字段增长/SSID 长时触发 |
| 四 | TCP 逐字节读取 | **中** | 每条连接都存在 |
| 五 | JSON 分配 OOM | **中** | AP 密集环境 + 堆碎片化 |
