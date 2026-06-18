#!/usr/bin/env python3
"""ESP32 LED 一键安装器 — 支持 10+ 平台，自动检测环境"""

import sys
import os
import shutil
import subprocess
import json
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent
GLOBAL_BIN = Path.home() / ".local" / "share" / "esp32-led"
SKILL_SRC = PROJECT_DIR / "skill" / "SKILL.md"          # hooks/plugin 平台
SKILL_MCP_SRC = PROJECT_DIR / "skill" / "SKILL-mcp.md"  # MCP-only 平台
TRANSPORT_DIR = PROJECT_DIR / "transport"

# Platforms that have auto-trigger (hooks or plugin) — use SKILL.md
AUTO_TRIGGER_PLATFORMS = {"claude", "codex", "opencode", "mimocode"}

# ── 平台定义 ───────────────────────────────────────────
PLATFORMS = {
    "claude": {
        "name": "Claude Code",
        "has_hooks": True, "has_skill": True, "has_plugin": False, "has_mcp": True,
        "global_mcp": Path.home() / ".claude.json",
        "project_mcp": PROJECT_DIR / ".mcp.json",
    },
    "codex": {
        "name": "Codex CLI",
        "has_hooks": True, "has_skill": True, "has_plugin": False, "has_mcp": True,
        "global_mcp": Path.home() / ".codex" / "config.toml",
        "project_mcp": PROJECT_DIR / ".codex" / "config.toml",
    },
    "cursor": {
        "name": "Cursor",
        "has_mcp": True,
        "global_mcp": Path.home() / ".cursor" / "mcp.json",
        "project_mcp": PROJECT_DIR / ".cursor" / "mcp.json",
    },
    "windsurf": {
        "name": "Windsurf",
        "has_mcp": True,
        "global_mcp": Path.home() / ".windsurf" / "mcp.json",
        "project_mcp": PROJECT_DIR / ".windsurf" / "mcp.json",
    },
    "trae": {
        "name": "Trae",
        "has_mcp": True,
        "global_mcp": Path.home() / ".trae" / "mcp.json",
        "project_mcp": PROJECT_DIR / ".trae" / "mcp.json",
    },
    "traecn": {
        "name": "TraeCN",
        "has_mcp": True,
        "global_mcp": Path.home() / ".trae-cn" / "mcp.json",
        "project_mcp": PROJECT_DIR / ".trae-cn" / "mcp.json",
    },
    "opencode": {
        "name": "OpenCode",
        "has_plugin": True, "has_mcp": True,
        "global_plugin": Path.home() / ".config" / "opencode" / "plugins",
        "project_plugin": PROJECT_DIR / ".opencode" / "plugins",
        "global_mcp": Path.home() / ".config" / "opencode" / "opencode.json",
        "project_mcp": PROJECT_DIR / "opencode.json",
    },
    "mimocode": {
        "name": "MiMoCode",
        "has_plugin": True, "has_mcp": True,
        "global_plugin": Path.home() / ".config" / "mimocode" / "plugins",
        "project_plugin": PROJECT_DIR / ".mimocode" / "plugins",
        "global_mcp": Path.home() / ".config" / "mimocode" / "mimocode.json",
        "project_mcp": PROJECT_DIR / ".mimocode" / "mimocode.json",
    },
    "openclaw": {
        "name": "OpenClaw",
        "has_mcp": True,
        "global_mcp": Path.home() / ".openclaw" / "mcp.json",
        "project_mcp": PROJECT_DIR / ".openclaw" / "mcp.json",
    },
    "hermes": {
        "name": "Hermes Agent",
        "has_mcp": True,
        "global_mcp": Path.home() / ".hermes" / "mcp.json",
        "project_mcp": PROJECT_DIR / ".hermes" / "mcp.json",
    },
}

MCP_STDIO_CONFIG = {
    "command": sys.executable,
    "args": [str(GLOBAL_BIN / "transport" / "server.py")]
}


# ── 环境检测 ───────────────────────────────────────────
def check_python():
    v = sys.version_info
    return v.major == 3 and v.minor >= 9

BIND_FILE = Path.home() / ".local" / "share" / "esp32-led" / ".esp32_device_id"

def load_binding():
    if BIND_FILE.exists():
        return BIND_FILE.read_text().strip()
    return ""

def save_binding(dev_id):
    BIND_FILE.parent.mkdir(parents=True, exist_ok=True)
    BIND_FILE.write_text(dev_id)

def clear_binding():
    if BIND_FILE.exists():
        BIND_FILE.unlink()

def scan_devices():
    try:
        sys.path.insert(0, str(PROJECT_DIR))
        from transport.transport_wifi import scan_for_devices
        print("    (扫描中...)", end="", flush=True)
        result = scan_for_devices(timeout_per_ip=0.15, total_timeout=60.0)
        print(f" 完成, 发现 {len(result)} 台")
        return result
    except Exception as e:
        print(f" 扫描出错: {e}")
        return []

def check_deps():
    try:
        __import__("bleak"); __import__("mcp")
        return True
    except ImportError:
        return False

def env_report():
    print("环境检测:")
    print(f"  Python:  {sys.version}")
    print(f"  路径:    {sys.executable}")
    print(f"  版本:    {'OK' if check_python() else 'NEED >=3.9'}")
    print(f"  依赖:    {'OK' if check_deps() else '需安装 bleak + mcp'}")
    print("")

# ── 文件比较工具 ─────────────────────────────────────────
def _files_identical(a: Path, b: Path) -> bool:
    """Return True if both files exist and have identical content."""
    try:
        if not a.exists() or not b.exists():
            return False
        return a.read_bytes() == b.read_bytes()
    except Exception:
        return False

# ── MCP JSON 操作 ──────────────────────────────────────
def load_json(path):
    if path.exists():
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    return {}

def save_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
    print(f"  OK {path}")

def install_mcp(platform_key, scope="global"):
    p = PLATFORMS[platform_key]
    if not p.get("has_mcp"): return
    tgt = p["project_mcp"] if scope == "project" else p["global_mcp"]
    if scope == "project":
        expected_args = [str(PROJECT_DIR / "transport" / "server.py")]
    else:
        expected_args = [str(GLOBAL_BIN / "transport" / "server.py")]
    expected = {"command": sys.executable, "args": expected_args}
    cfg = load_json(tgt)
    if "mcpServers" not in cfg: cfg["mcpServers"] = {}
    existing = cfg["mcpServers"].get("esp32-led")
    if existing and existing.get("command") == expected["command"] and existing.get("args") == expected["args"]:
        print(f"  MCP 已注册(无变化): {tgt}")
        return
    cfg["mcpServers"]["esp32-led"] = expected
    save_json(tgt, cfg)

def uninstall_mcp(platform_key, scope="global"):
    p = PLATFORMS[platform_key]
    if not p.get("has_mcp"): return
    tgt = p["project_mcp"] if scope == "project" else p["global_mcp"]
    cfg = load_json(tgt)
    if "mcpServers" in cfg and "esp32-led" in cfg["mcpServers"]:
        del cfg["mcpServers"]["esp32-led"]
        if not cfg["mcpServers"]: del cfg["mcpServers"]
        save_json(tgt, cfg)
    else:
        print(f"  MCP 未找到: {tgt}")

# ── 插件/技能安装 ─────────────────────────────────────
def install_plugin(platform_key, scope="global"):
    p = PLATFORMS[platform_key]
    if not p.get("has_plugin"): return
    src = PROJECT_DIR / "plugin" / "esp32-led.js"
    if not src.exists(): return
    pdir = p["project_plugin"] if scope == "project" else p["global_plugin"]
    dst = pdir / "esp32-led.js"
    if _files_identical(src, dst):
        print(f"  Plugin 已安装(无变化): {dst}")
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    if platform_key != "opencode":
        content = dst.read_text(encoding="utf-8")
        content = content.replace('"opencode"', f'"{platform_key}"')
        dst.write_text(content, encoding="utf-8")
    print(f"  OK Plugin: {dst}")

def uninstall_plugin(platform_key, scope="global"):
    p = PLATFORMS[platform_key]
    if not p.get("has_plugin"): return
    pdir = p["project_plugin"] if scope == "project" else p["global_plugin"]
    dst = pdir / "esp32-led.js"
    if dst.exists(): dst.unlink(); print(f"  REMOVED: {dst}")

def _purge_sendpy_hooks(cfg_path):
    """Remove all hooks whose nested command contains 'send.py'."""
    cfg = load_json(cfg_path)
    hooks = cfg.get("hooks", {})
    removed = 0
    for event in list(hooks.keys()):
        cleaned = []
        for entry in hooks[event]:
            inner = entry.get("hooks", [])
            has_sendpy = any("send.py" in h.get("command", "") for h in inner)
            if has_sendpy:
                removed += 1
            else:
                cleaned.append(entry)
        if cleaned:
            hooks[event] = cleaned
        else:
            del hooks[event]
    if removed:
        save_json(cfg_path, cfg)
    return removed


def _hooks_already_installed(cfg_path, template, send_py):
    """Return True if all template hooks with correct commands exist in config."""
    existing = load_json(cfg_path)
    hooks = existing.get("hooks", {})
    if not hooks:
        return False
    for event, entries in template.get("hooks", {}).items():
        if event not in hooks:
            return False
        for entry in entries:
            found = False
            for existing_entry in hooks[event]:
                inner = existing_entry.get("hooks", [])
                for h in inner:
                    if h.get("command") == f'python "{send_py}"':
                        found = True
                        break
                if found:
                    break
            if not found:
                return False
    return True


def install_hooks(platform_key, scope="global"):
    if platform_key == "claude":
        tmpl = PROJECT_DIR / "hooks" / "claude_hooks.json"
        if not tmpl.exists(): return
        template = json.loads(tmpl.read_text(encoding="utf-8"))
        if scope == "project":
            cfg_path = PROJECT_DIR / ".claude" / "settings.local.json"
            send_py = str(PROJECT_DIR / "transport" / "send.py")
        else:
            cfg_path = Path.home() / ".claude" / "settings.json"
            send_py = str(GLOBAL_BIN / "transport" / "send.py")
        removed = _purge_sendpy_hooks(cfg_path)
        if removed:
            print(f"  已清理 {removed} 个旧 hooks")
        if removed == 0 and _hooks_already_installed(cfg_path, template, send_py):
            print(f"  Hooks 已安装(无变化): {cfg_path}")
            return
        existing = load_json(cfg_path)
        hooks = existing.get("hooks", {})
        for event, entries in template.get("hooks", {}).items():
            for entry in entries:
                for h in entry.get("hooks", []):
                    h["command"] = f'python "{send_py}"'
            if event in hooks:
                hooks[event].extend(entries)
            else:
                hooks[event] = list(entries)
        existing["hooks"] = hooks
        save_json(cfg_path, existing)
    elif platform_key == "codex":
        tmpl = PROJECT_DIR / "hooks" / "codex_hooks.json"
        if not tmpl.exists(): return
        template = json.loads(tmpl.read_text(encoding="utf-8"))
        if scope == "project":
            cfg_path = PROJECT_DIR / ".codex" / "hooks.json"
            send_py = str(PROJECT_DIR / "transport" / "send.py")
        else:
            cfg_path = Path.home() / ".codex" / "hooks.json"
            send_py = str(GLOBAL_BIN / "transport" / "send.py")
        removed = _purge_sendpy_hooks(cfg_path)
        if removed:
            print(f"  已清理 {removed} 个旧 hooks")
        if removed == 0 and _hooks_already_installed(cfg_path, template, send_py):
            print(f"  Hooks 已安装(无变化): {cfg_path}")
            return
        existing = load_json(cfg_path)
        hooks = existing.get("hooks", {})
        for event, entries in template.get("hooks", {}).items():
            for entry in entries:
                for h in entry.get("hooks", []):
                    h["command"] = f'python "{send_py}"'
            if event in hooks:
                hooks[event].extend(entries)
            else:
                hooks[event] = list(entries)
        existing["hooks"] = hooks
        save_json(cfg_path, existing)

def install_skill(scope="global", project_dir=None, platform_key=None):
    """Install skill file — auto-trigger platforms get SKILL.md, MCP-only get SKILL-mcp.md."""
    is_auto = platform_key in AUTO_TRIGGER_PLATFORMS if platform_key else False
    src = SKILL_SRC if is_auto else SKILL_MCP_SRC
    if not src.exists():
        return

    if scope == "global":
        targets = [
            Path.home() / ".claude" / "skills" / "esp32-led",
            Path.home() / ".agents" / "skills" / "esp32-led",
        ]
    elif project_dir:
        proj = Path(project_dir)
        targets = [
            proj / ".claude" / "skills" / "esp32-led",
            proj / ".cursor" / "rules",
            proj / ".windsurf" / "rules",
            proj,
        ]
    else:
        return

    for d in targets:
        dst = d / "SKILL.md" if d.name != "rules" else d / "esp32-led-skill.md"
        if _files_identical(src, dst):
            print(f"  Skill 已安装(无变化): {dst}")
            continue
        d.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        print(f"  OK Skill: {dst}")

def install_global_bin():
    dst_dir = GLOBAL_BIN / "transport"
    # Check if all files are already identical — skip if so
    if TRANSPORT_DIR.is_dir():
        all_same = True
        for src_file in TRANSPORT_DIR.iterdir():
            if src_file.is_file():
                if not _files_identical(src_file, dst_dir / src_file.name):
                    all_same = False
                    break
        if all_same and (dst_dir / "server.py").exists():
            print(f"  Transport 已安装(无变化): {dst_dir}")
            return
    if dst_dir.exists():
        shutil.rmtree(dst_dir, ignore_errors=True)
    shutil.copytree(TRANSPORT_DIR, dst_dir)
    (dst_dir / "server.py").chmod(0o755)
    print(f"  OK transport: {dst_dir}")

# ── 安装 / 卸载 ───────────────────────────────────────
def do_install(platform_key, scope="global", project_dir=None):
    p = PLATFORMS[platform_key]
    print(f"\n安装 {p['name']} ({scope}):")
    if scope == "global":
        install_global_bin()
    # Skill is always installed — variant depends on platform type
    install_skill(scope, project_dir, platform_key)
    if p.get("has_hooks"): install_hooks(platform_key, scope)
    if p.get("has_plugin"): install_plugin(platform_key, scope)
    if p.get("has_mcp"): install_mcp(platform_key, scope)

def uninstall_skill(scope="global", project_dir=None, platform_key=None):
    """Remove skill files. Destination names match install_skill (always SKILL.md)."""
    if scope == "global":
        targets = [
            Path.home() / ".claude" / "skills" / "esp32-led",
            Path.home() / ".agents" / "skills" / "esp32-led",
        ]
    elif project_dir:
        proj = Path(project_dir)
        targets = [
            proj / ".claude" / "skills" / "esp32-led",
            proj / ".cursor" / "rules",
            proj / ".windsurf" / "rules",
            proj,
        ]
    else:
        return

    for d in targets:
        dst = d / "SKILL.md" if d.name != "rules" else d / "esp32-led-skill.md"
        if dst.exists():
            dst.unlink()
            print(f"  REMOVED Skill: {dst}")


def uninstall_hooks(platform_key, scope="global"):
    """Remove hooks installed for claude or codex."""
    if platform_key == "claude":
        if scope == "project":
            cfg_path = PROJECT_DIR / ".claude" / "settings.local.json"
        else:
            cfg_path = Path.home() / ".claude" / "settings.json"
        removed = _purge_sendpy_hooks(cfg_path)
        if removed:
            print(f"  REMOVED {removed} hooks: {cfg_path}")
        else:
            print(f"  Hooks 未找到: {cfg_path}")
    elif platform_key == "codex":
        if scope == "project":
            cfg_path = PROJECT_DIR / ".codex" / "hooks.json"
        else:
            cfg_path = Path.home() / ".codex" / "hooks.json"
        removed = _purge_sendpy_hooks(cfg_path)
        if removed:
            print(f"  REMOVED {removed} hooks: {cfg_path}")
        else:
            print(f"  Hooks 未找到: {cfg_path}")


def do_uninstall(platform_key, scope="global", project_dir=None):
    p = PLATFORMS[platform_key]
    print(f"\n卸载 {p['name']} ({scope}):")
    if p.get("has_mcp"): uninstall_mcp(platform_key, scope)
    if p.get("has_hooks"): uninstall_hooks(platform_key, scope)
    if p.get("has_plugin"): uninstall_plugin(platform_key, scope)
    uninstall_skill(scope, project_dir, platform_key)

# ── 导出 mcp.json ─────────────────────────────────────
def export_mcp_json(path=None):
    if not path:
        path = PROJECT_DIR / "mcp.json"
    save_json(Path(path), {"mcpServers": {"esp32-led": dict(MCP_STDIO_CONFIG)}})
    print(f"\nmcp.json 已导出到: {path}")
    print("将此文件放到对应平台的配置目录即可。")

USAGE = """用法:
  python install.py install [--target <平台>] [--scope global|project]
  python install.py uninstall [--target <平台>] [--scope global|project]
  python install.py export [路径]
  python install.py scan
  python install.py bind <device_id>
  python install.py unbind
  python install.py status

平台: claude, codex, cursor, windsurf, trae, traecn, opencode, mimocode, openclaw, hermes
默认: --target all --scope global"""

def _parse_args(args):
    opts = {"target": "all", "scope": "global", "path": None, "dev_id": None, "project_dir": None}
    i = 0
    while i < len(args):
        if args[i] == "--target" and i+1 < len(args):
            opts["target"] = args[i+1]; i += 2
        elif args[i] == "--scope" and i+1 < len(args):
            opts["scope"] = args[i+1]; i += 2
        elif args[i] == "--project-dir" and i+1 < len(args):
            opts["project_dir"] = args[i+1]; i += 2
        elif args[i] == "--output" and i+1 < len(args):
            opts["path"] = args[i+1]; i += 2
        else:
            if opts["dev_id"] is None and not args[i].startswith("-"):
                opts["dev_id"] = args[i]
            elif opts["path"] is None and not args[i].startswith("-"):
                opts["path"] = args[i]
            i += 1
    return opts

def show_status():
    env_report()
    bound = load_binding()
    print(f"设备绑定: {bound}" if bound else "设备绑定: 未设置")
    print("")
    for key, p in PLATFORMS.items():
        installed = False
        if p.get("has_mcp"):
            tgt = p["global_mcp"]
            if tgt.exists():
                try:
                    cfg = json.loads(tgt.read_text(encoding="utf-8"))
                    if "esp32-led" in cfg.get("mcpServers", {}): installed = True
                except: pass
        if p.get("has_plugin"):
            pdir = p.get("global_plugin")
            if pdir and (pdir / "esp32-led.js").exists(): installed = True
        if p.get("has_hooks") and key == "claude":
            hooks = Path.home() / ".claude" / "settings.json"
            if hooks.exists() and "send.py" in hooks.read_text(encoding="utf-8", errors="ignore"): installed = True
        if p.get("has_hooks") and key == "codex":
            hooks = Path.home() / ".codex" / "hooks.json"
            if hooks.exists() and "send.py" in hooks.read_text(encoding="utf-8", errors="ignore"): installed = True
        print(f"  [{'已安装' if installed else '未安装'}] {key}: {p['name']}")

def main():
    if len(sys.argv) < 2:
        print(USAGE)
        return

    cmd = sys.argv[1]
    opts = _parse_args(sys.argv[2:])

    if cmd == "status":
        show_status()
    elif cmd == "export":
        export_mcp_json(opts["path"])
    elif cmd == "scan":
        print("正在扫描局域网设备...")
        devices = scan_devices()
        if devices:
            bound = load_binding()
            for dev_id, ip in devices:
                marker = " [已绑定]" if bound == dev_id else ""
                print(f"  {dev_id}  ({ip}){marker}")
        else:
            print("未发现设备")
    elif cmd == "bind":
        dev_id = opts["dev_id"]
        if not dev_id:
            print("用法: python install.py bind <device_id>")
            print("先用 'python install.py scan' 扫描设备获取 device_id")
            return
        save_binding(dev_id)
        print(f"已绑定: {dev_id}")
    elif cmd == "unbind":
        clear_binding()
        print("绑定已清除")
    elif cmd == "install":
        env_report()
        platforms = list(PLATFORMS) if opts["target"] == "all" else [opts["target"]]
        for p in platforms:
            if p in PLATFORMS:
                do_install(p, opts["scope"], opts["project_dir"])
        print("\n完成！重启 AI 工具生效。")
    elif cmd == "uninstall":
        platforms = list(PLATFORMS) if opts["target"] == "all" else [opts["target"]]
        for p in platforms:
            if p in PLATFORMS:
                do_uninstall(p, opts["scope"], opts["project_dir"])
        print("\n完成！重启 AI 工具生效。")
    else:
        print(f"未知命令: {cmd}")
        print(USAGE)

if __name__ == "__main__":
    main()
