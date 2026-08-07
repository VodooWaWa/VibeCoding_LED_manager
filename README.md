# ✨ Vibe Coding LED 管理器 + ESP32-C3 固件 ✨

[English](README_EN.md)

## 🎯 这是什么神仙东西？

AI 编码状态指示灯，**一键安装管理器 + 开源固件**二合一！单文件运行，**10 个 AI 平台**自动配置，让全世界都知道你在卷代码！🤖💡

> 一边写代码，一边灯灯变色，仪式感拉满 ✨

---

## 🛒 硬件在哪买？

> ## Ai3D趣造
>
> 硬件购买：[淘宝](https://shop106055843.taobao.com/category.htm?spm=pc_detail.30350276.shop_block.dshopinfo.3e907dd6X0ddB7) | [拼多多](https://mobile.yangkeduo.com/mall_page.html?ps=kSxgffPoX9)
>
> 使用疑问？先翻翻[用户使用说明](用户使用说明.pdf)～

---

## 🚀 固件最新版 v3.5.1（WebUI 可查）

**v3.5.1 更新日志：**
- 🔧 尝试修复苹果 macOS 蓝牙 BLE 发现失败（未验证，穷！没 Mac 测 😭）
- 📡 优化 BLE 广播包：设备名随广播直发，不再依赖扫描响应

**v3.5 更新日志：**
- 🔄 反向灯序开关
- 📶 优化 WiFi-AP 配网 + 回退逻辑，固定信道 6 信号更稳
- 🩹 修复 BLE 发现 BUG（广播开关原来形同虚设）
- 🩹 修复静态 IP 设置失效 BUG
- ⚡ 优化页面发送机制，解决 chunked EAGAIN 截断

> ⚠️ 设备不支持 OTA！要升级固件可以找客服远程协助（Windows + 连连控）。

---

## 📂 固件源码开源啦！

ESP32-C3 固件源码已开源，想自己编译烧录的宝子们看这里：

- **源码位置**：[`firmware/esp32c3_ws2812/`](firmware/esp32c3_ws2812/)
- **最新版本**：v3.5.1
- **环境要求**：ESP-IDF v6.0.1
- **编译烧录**：进 `firmware/esp32c3_ws2812/` 跑 `flash.cmd`（Windows）

固件都能干啥？8 颗 WS2812 多状态动画、WebUI 配置、TCP:8080 控制、BLE GATT 控制、WiFi AP 配网、多项目平台 LED 隔离！

> 源码仅供学习参考和自行烧录，商用请联系授权～

---

## 📥 下载

从 [Releases](https://github.com/VodooWaWa/VibeCoding_LED_manager/releases) 拿走对应系统的：

| 系统 | 架构 | 文件名 |
|------|------|--------|
| Windows | x64 | `VibeCoding_LED_manager_win_x64.exe` |
| Windows | ARM64 | `VibeCoding_LED_manager_win_arm64.exe` |
| macOS | x64 (Intel) | `VibeCoding_LED_manager_mac_x64.dmg` |
| macOS | ARM64 (Apple Silicon) | `VibeCoding_LED_manager_mac_arm64.dmg` |
| Linux | x64 | `VibeCoding_LED_manager_linux_x64.AppImage` |

> 🍎 macOS 提示"已损坏"？别慌：
> 1. 系统设置 → 隐私与安全性 → 仍要打开
> 2. 还不行？终端跑：`sudo xattr -rd com.apple.quarantine /Applications/VibeCoding.app`
> 3. M 芯片的宝子注意：未签名 ARM64 应用被 Gatekeeper 拦是正常的
> 🐧 Linux AppImage 记得先 `chmod +x` 哦

---

## 🖼️ 长这样！

### 管理器 UI
![WebUI](/Priview-IMG/ManagerUI.png "管理器UI界面")

### 设备 WebUI
![WebUI](/Priview-IMG/WebUI-1.png "WebUI主界面")
![WebUI](/Priview-IMG/WebUI-2.png "WebUI灯光测试")
![WebUI](/Priview-IMG/WebUI-3.png "WebUI项目看板")

---

## 💡 功能亮点

- 🚀 **一键安装**：选平台 → 点安装，环境检测、依赖、配置全自动
- 🧹 **一键卸载**：说走就走，零残留
- 📡 **设备管理**：局域网扫描、WiFi/BLE 绑定解绑、清除蓝牙配对
- 🤝 **10 平台通吃**：Claude Code / Codex CLI / OpenCode / MiMoCode / Cursor / Windsurf / Trae / TraeCN / OpenClaw / Reasonix
- 🌍 **全局 / 项目级**：全局所有项目通用，项目级跟着目录走
- ✅ **环境检测**：自动检测 Python、bleak、mcp 依赖
- 📦 **MCP+Skill 导出**：打包完整配置，列表外平台也能手动接入
- 🌐 **中英文切换**：中文 / English 随意切

---

## 📖 使用教程（超简单）

1. 📥 下载对应系统的单文件
2. 🖱️ 双击运行
3. ✅ 检查环境（Python 3.12+、bleak、mcp）
4. 🎯 选平台 → 全局或项目级 → 点「安装」
5. 🔍 扫描设备 → 绑定 → 完事！

---

## 📍 安装级别

| 模式 | 路径 | 适用 |
|------|------|------|
| 全局 | `~/.local/share/3dai-led/` | 单 AI 工具，所有项目通用 |
| 项目级 | `.codex/` `.claude/` `.trae/` 等 | 团队协作，配置跟着项目 |

两种模式还能共存哦～

---

## 🔧 环境要求

- Windows 10/11 / macOS / Linux
- Python 3.12+
- 局域网 WiFi (2.4G)

---

## 🔒 安全说明

- 局域网信任模型，无应用层认证
- BLE Just Works + Secure Connections + Bonding
- 卸载只删配置，不碰传输层和缓存

---

## 🏗️ 想自己构建？

```bash
npm install
npx electron-builder --win portable
```

GitHub Actions 自动构建全平台 x64+arm64，躺平就行～

---

## 📜 许可证

MIT，随便造！

#AI编码 #状态灯 #vibecoding #ESP32 #WS2812 #ClaudeCode #Codex #程序员桌面 #电子diy
