# Vibe Coding LED Manager

[English](README_EN.md)

AI 编程状态指示灯 — 一键安装管理器。单文件运行，支持 10 个 AI 平台自动配置。

> ## Ai3D趣造
>
> 硬件购买：[淘宝](https://shop106055843.taobao.com/category.htm?spm=pc_detail.30350276.shop_block.dshopinfo.3e907dd6X0ddB7) | [拼多多](https://mobile.yangkeduo.com/mall_page.html?ps=kSxgffPoX9)
>
> 如果您有使用方面的任何疑问，请阅读[用户使用说明](用户使用说明.pdf)
>
## 当前设备固件最新版本号：v3.5.2(可以访问设备WebUI查看)

**v3.5.1更新日志：**
- 尝试修复苹果 macOS 系统蓝牙 BLE 发现失败的问题（已验证，没修好。。我TM放弃了！开发穷，没钱买苹果。。）
- 优化 BLE 广播数据包结构，设备名称随广播直接发送，不再依赖扫描响应

**v3.5更新日志：****
- 更新了反向灯序开关功能
- 优化了wifi-AP配网和配网回退逻辑，固定信道6保证信号稳定。
- 修复BLE发现BUG，广播开关形同虚设
- 修复静态IP设置BUG，早期版本存在静态IP设置失效问题。
- 优化页面发送机制，解决 chunked 非阻塞偶发 EAGAIN 截断导致页面无法加载的问题

因为设备无法直接进行OTA，如果需要升级最新版固件，可以联系客服远程协助进行操作。（Windows环境+连连控远程控制软件）

## 下载

从 [Releases](https://github.com/VodooWaWa/VibeCoding_LED_manager/releases) 下载对应系统版本：

| 系统 | 架构 | 文件名 |
|------|------|--------|
| Windows | x64 | `VibeCoding_LED_manager_win_x64.exe` |
| Windows | ARM64 | `VibeCoding_LED_manager_win_arm64.exe` |
| macOS | x64 (Intel) | `VibeCoding_LED_manager_mac_x64.dmg` |
| macOS | ARM64 (Apple Silicon) | `VibeCoding_LED_manager_mac_arm64.dmg` |
| Linux | x64 | `VibeCoding_LED_manager_linux_x64.AppImage` |

> macOS 首次打开若提示"无法验证开发者"或"已损坏，无法打开"：
>   1. 系统设置 → 隐私与安全性 → 仍要打开
>   2. 若仍有提示，终端执行：`sudo xattr -rd com.apple.quarantine /Applications/VibeCoding.app`
>   3. Apple Silicon (M 芯片) 用户注意：未签名的 ARM64 应用会被 Gatekeeper 拦截，属正常现象
> Linux AppImage 需先 `chmod +x`。

## 安装管理器UI预览
![WebUI](/Priview-IMG/ManagerUI.png "管理器UI界面")

## 设备UI预览
![WebUI](/Priview-IMG/WebUI-1.png "WebUI主界面")
![WebUI](/Priview-IMG/WebUI-2.png "WebUI灯光测试")
![WebUI](/Priview-IMG/WebUI-3.png "WebUI项目看板")

## 功能

- **一键安装**：选平台 → 点安装，自动完成环境检测、依赖安装、配置写入
- **一键卸载**：随时移除，不留残留
- **设备管理**：扫描局域网设备、WiFi/BLE 绑定解绑、清除蓝牙配对
- **平台支持**：Claude Code / Codex CLI / OpenCode / MiMoCode / Cursor / Windsurf / Trae / TraeCN / OpenClaw / Reasonix
- **全局 / 项目级**：全局所有项目通用，项目级跟着目录走
- **环境检测**：自动检测 Python、bleak、mcp 依赖
- **MCP+Skill 导出**：打包为完整配置文件包，用于列表外平台手动接入
- **中英文切换**：界面支持中文/English

## 使用方法

1. 下载对应系统的单文件
2. 双击运行
3. 检查环境检测（Python 3.12+、bleak、mcp）
4. 选择目标 AI 平台 → 全局或项目级 → 点"安装"
5. 扫描设备 → 绑定 → 完成

## 安装级别

| 模式 | 路径 | 适用 |
|------|------|------|
| 全局 | `~/.local/share/3dai-led/` | 单 AI 工具，所有项目通用 |
| 项目级 | `.codex/` `.claude/` `.trae/` 等 | 团队协作，配置跟着项目 |

两种模式可共存。

## 环境要求

- Windows 10/11 / macOS / Linux
- Python 3.12+
- 局域网 WiFi (2.4G)

## 安全

- 局域网信任模型，无应用层认证
- BLE Just Works + Secure Connections + Bonding
- 卸载只删配置，不碰传输层和缓存

## 构建

```bash
npm install
npx electron-builder --win portable
```

GitHub Actions 自动构建全平台 x64+arm64。

## 许可证

MIT
