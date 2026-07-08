#!/usr/bin/env python3
"""3DAi LED 一键安装器 — 支持 10+ 平台，自动检测环境"""

import sys
import os
import re
import shutil
import subprocess
import json
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent
GLOBAL_BIN = Path.home() / ".local" / "share" / "3dai-led"
SKILL_SRC = PROJECT_DIR / "skill" / "SKILL.md"          # hooks/plugin 平台
SKILL_MCP_SRC = PROJECT_DIR / "skill" / "SKILL-mcp.md"  # MCP-only 平台
TRANSPORT_DIR = PROJECT_DIR / "transport"

# Platforms that have auto-trigger (hooks or plugin) — use SKILL.md
AUTO_TRIGGER_PLATFORMS = {"claude", "codex", "opencode", "mimocode", "windsurf", "reasonix"}

# ── 平台定义 ───────────────────────────────────────────
PLATFORMS = {
    "claude": {
        "name": "Claude Code", "dir": "claude",
        "has_hooks": True, "has_skill": True, "has_plugin": False, "has_mcp": True,
        "global_mcp": Path.home() / ".claude.json",
        "project_mcp": PROJECT_DIR / ".mcp.json",
    },
    "codex": {
        "name": "Codex CLI", "dir": "codex",
        "has_hooks": True, "has_skill": True, "has_plugin": False, "has_mcp": True,
        "global_mcp": Path.home() / ".codex" / "config.toml",
        "project_mcp": PROJECT_DIR / ".codex" / "config.toml",
        "config_format": "toml",
    },
    "cursor": {
        "name": "Cursor", "dir": "cursor",
        "has_mcp": True,
        "global_mcp": Path.home() / ".cursor" / "mcp.json",
        "project_mcp": PROJECT_DIR / ".cursor" / "mcp.json",
    },
    "windsurf": {
        "name": "Windsurf", "dir": "windsurf",
        "has_hooks": True, "has_mcp": True,
        "global_mcp": Path.home() / ".codeium" / "windsurf" / "mcp_config.json",
        # no project-level MCP - docs only mention global path
        # hooks: ~/.codeium/windsurf/hooks.json (user) / .windsurf/hooks.json (workspace)
    },
    "trae": {
        "name": "Trae", "dir": "trae",
        "has_mcp": True,
        "global_mcp": Path.home() / ".trae" / "mcp.json",
        "project_mcp": PROJECT_DIR / ".trae" / "mcp.json",
    },
    "traecn": {
        "name": "TraeCN", "dir": "trae-cn",
        "has_mcp": True,
        "global_mcp": Path.home() / ".trae-cn" / "mcp.json",
        "project_mcp": PROJECT_DIR / ".trae-cn" / "mcp.json",
    },
    "opencode": {
        "name": "OpenCode", "dir": "opencode",
        "has_plugin": True, "has_mcp": True,
        "global_plugin": Path.home() / ".opencode" / "plugins",
        "project_plugin": PROJECT_DIR / ".opencode" / "plugins",
        "global_mcp": Path.home() / ".opencode.json",
        "project_mcp": PROJECT_DIR / ".opencode.json",
        "config_format": "mimocode",
    },
    "mimocode": {
        "name": "MiMoCode", "dir": "mimocode",
        "has_plugin": True, "has_mcp": True,
        "global_plugin": Path.home() / ".local" / "share" / "mimocode" / "plugins",
        "project_plugin": PROJECT_DIR / ".mimocode" / "plugins",
        "global_mcp": Path.home() / ".config" / "mimocode" / "mimocode.json",
        "project_mcp": PROJECT_DIR / ".mimocode" / "mimocode.json",
        "config_format": "mimocode",
    },
    "openclaw": {
        "name": "OpenClaw", "dir": "openclaw",
        "has_mcp": True,
        "global_mcp": Path.home() / ".openclaw" / "openclaw.json",
        "project_mcp": PROJECT_DIR / ".openclaw" / "openclaw.json",
    },
    "reasonix": {
        "name": "Reasonix", "dir": "reasonix",
        "has_hooks": True, "has_skill": True, "has_plugin": False, "has_mcp": True,
        "project_mcp": PROJECT_DIR / ".mcp.json",
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

BIND_FILE = Path.home() / ".local" / "share" / "3dai-led" / ".3dai_device_id"

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
        __import__("bleak"); __import__("serial"); __import__("mcp")
        return True
    except ImportError:
        return False

def env_report():
    print("环境检测:")
    print(f"  Python:  {sys.version}")
    print(f"  路径:    {sys.executable}")
    print(f"  版本:    {'OK' if check_python() else 'NEED >=3.9'}")
    print(f"  依赖:    {'OK' if check_deps() else '需安装 bleak + pyserial + mcp'}")
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

# ── MCP 配置读写 (JSON / TOML) ─────────────────────────────
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

def _load_config(path, fmt="json"):
    """Load config, dispatch by format."""
    if not path.exists():
        return {}
    if fmt == "mimocode":
        cfg = load_json(path)
        # MiMoCode uses "mcp" key, normalize to "mcpServers" for internal use
        mcp = cfg.get("mcp", {})
        return {"mcpServers": mcp, "_rest": {k: v for k, v in cfg.items() if k != "mcp"}}
    if fmt == "toml":
        try:
            import tomllib  # Python 3.11+
        except ImportError:
            import tomli as tomllib  # fallback
        with open(path, "rb") as f:
            return tomllib.load(f)
    return load_json(path)

def _ensure_toml_feature(path, section, key, value):
    """Ensure a TOML [section] key = value line exists."""
    path.parent.mkdir(parents=True, exist_ok=True)
    section_header = f"[{section}]"
    entry = f"{key} = {value}"
    if path.exists():
        lines = path.read_text(encoding="utf-8").splitlines()
        if entry in lines:
            return  # already set
        # Insert after section header, or append
        try:
            idx = lines.index(section_header)
            lines.insert(idx + 1, entry)
        except ValueError:
            lines.append(section_header)
            lines.append(entry)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    else:
        path.write_text(f"{section_header}\n{entry}\n", encoding="utf-8")

def _save_config(path, data, fmt="json"):
    """Save MCP config with format dispatch."""
    if fmt == "mimocode":
        path.parent.mkdir(parents=True, exist_ok=True)
        # MiMoCode uses "mcp" key with type+command array format
        cfg = load_json(path) if path.exists() else {}
        # Preserve non-mcp keys from existing config
        rest = {k: v for k, v in cfg.items() if k != "mcp"}
        mcp = {}
        for name, srv in data.get("mcpServers", {}).items():
            mcp[name] = {"type": "local", "command": [srv["command"]] + srv.get("args", [])}
        rest["$schema"] = "https://opencode.ai/config.json"
        if mcp:
            rest["mcp"] = mcp
        save_json(path, rest)
        return
    if fmt == "toml":
        path.parent.mkdir(parents=True, exist_ok=True)
        # Preserve non-mcpServers sections from existing file
        existing = []
        if path.exists():
            existing = path.read_text(encoding="utf-8").splitlines()
        preserved = [l for l in existing if not l.startswith("[mcpServers.") and not l.startswith("command =") and not l.startswith("args =")]
        lines = preserved
        mcp = data.get("mcpServers", {})
        for name, srv in mcp.items():
            lines.append(f'[mcpServers."{name}"]')
            # TOML literal strings (single quotes) - no backslash escaping needed
            lines.append("command = '" + srv["command"] + "'")
            args_str = "', '".join(srv.get("args", []))
            lines.append("args = ['" + args_str + "']")
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print(f"  OK {path} (toml)")
        return
    save_json(path, data)

def _reasonix_config_dir():
    """Return Reasonix global config directory (platform-aware)."""
    if sys.platform == "win32":
        return Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming")) / "reasonix"
    return Path.home() / ".reasonix"


def _ensure_reasonix_toml_plugin(config_path, name, command, args):
    """Add a [[plugins]] entry to Reasonix config.toml if not present."""
    config_path.parent.mkdir(parents=True, exist_ok=True)
    if config_path.exists():
        content = config_path.read_text(encoding="utf-8")
        if f"name = '{name}'" in content or f'name = "{name}"' in content:
            return  # already configured
        # Also check with variable spacing (e.g. Reasonix-generated: name    = "3dai-led")
        if re.search(rf"name\s*=\s*['\"].*\b{re.escape(name)}\b.*['\"]", content):
            return
        # Append to existing config
        text = content.rstrip("\n") + "\n\n"
    else:
        text = ""
    args_str = "', '".join(args)
    # Use TOML literal strings (single quotes) — no backslash escaping needed
    text += f'[[plugins]]\nname = \'{name}\'\ncommand = \'{command}\'\nargs = [\'{args_str}\']\n'
    config_path.write_text(text, encoding="utf-8")
    print(f"  OK [[plugins]]: {config_path}")


def _remove_reasonix_toml_plugin(config_path, name):
    """Remove a [[plugins]] entry from Reasonix config.toml."""
    if not config_path.exists():
        return False
    lines = config_path.read_text(encoding="utf-8").splitlines()
    new_lines = []
    skip = False
    removed = False
    for line in lines:
        if line.strip() == "[[plugins]]":
            skip = False  # previous plugin block ended
            # Peek ahead to see if this block is ours
            # We'll handle below
        # Handle variable spacing: name=name / name = 'name' / name    = "name"
        stripped = line.strip()
        if re.match(rf"name\s*=\s*['\"].*\b{re.escape(name)}\b.*['\"]", stripped):
            skip = True
            removed = True
            # Also skip the [[plugins]] header before this
            if new_lines and new_lines[-1].strip() == "[[plugins]]":
                new_lines.pop()
            continue
        if skip:
            # Skip lines until next [[plugins]] or end
            if line.strip().startswith("[["):
                skip = False
                new_lines.append(line)
            continue
        new_lines.append(line)
    if removed:
        config_path.write_text("\n".join(new_lines).strip() + "\n", encoding="utf-8")
    return removed


def _mcp_target(p, scope):
    if scope == "project":
        return p.get("project_mcp")
    return p.get("global_mcp")

def install_mcp(platform_key, scope="global"):
    p = PLATFORMS[platform_key]
    if not p.get("has_mcp"): return

    # ── Reasonix: project → .mcp.json, global → [[plugins]] in config.toml ──
    if platform_key == "reasonix":
        if scope == "project":
            tgt = PROJECT_DIR / ".mcp.json"
            expected_args = [str(PROJECT_DIR / "transport" / "server.py")]
            expected = {"command": sys.executable, "args": expected_args}
            cfg = load_json(tgt)
            if "mcpServers" not in cfg: cfg["mcpServers"] = {}
            existing = cfg["mcpServers"].get("3dai-led")
            if existing and existing.get("command") == expected["command"] and existing.get("args") == expected["args"]:
                print(f"  MCP 已注册(无变化): {tgt}")
                return
            cfg["mcpServers"]["3dai-led"] = expected
            save_json(tgt, cfg)
        else:
            tgt = _reasonix_config_dir() / "config.toml"
            expected_args = [str(GLOBAL_BIN / "transport" / "server.py")]
            _ensure_reasonix_toml_plugin(tgt, "3dai-led", sys.executable, expected_args)
        return

    tgt = _mcp_target(p, scope)
    if not tgt: return  # no MCP target for this scope (e.g. Windsurf project)
    fmt = p.get("config_format", "json")
    if scope == "project":
        expected_args = [str(PROJECT_DIR / "transport" / "server.py")]
    else:
        expected_args = [str(GLOBAL_BIN / "transport" / "server.py")]
    expected = {"command": sys.executable, "args": expected_args}
    cfg = _load_config(tgt, fmt)
    if "mcpServers" not in cfg: cfg["mcpServers"] = {}
    existing = cfg["mcpServers"].get("3dai-led")
    if existing and existing.get("command") == expected["command"] and existing.get("args") == expected["args"]:
        print(f"  MCP 已注册(无变化): {tgt}")
        return
    cfg["mcpServers"]["3dai-led"] = expected
    _save_config(tgt, cfg, fmt)
    # MiMoCode: also write to ~/.mimocode.json (OpenCode-compatible path)
    if platform_key == "mimocode" and scope == "global":
        alt = Path.home() / ".mimocode.json"
        _save_config(alt, cfg, fmt)

def uninstall_mcp(platform_key, scope="global"):
    p = PLATFORMS[platform_key]
    if not p.get("has_mcp"): return

    # ── Reasonix ──
    if platform_key == "reasonix":
        if scope == "project":
            tgt = PROJECT_DIR / ".mcp.json"
            cfg = load_json(tgt)
            if "mcpServers" in cfg and "3dai-led" in cfg["mcpServers"]:
                del cfg["mcpServers"]["3dai-led"]
                if not cfg["mcpServers"]: del cfg["mcpServers"]
                save_json(tgt, cfg)
            else:
                print(f"  MCP 未找到: {tgt}")
        else:
            tgt = _reasonix_config_dir() / "config.toml"
            if _remove_reasonix_toml_plugin(tgt, "3dai-led"):
                print(f"  REMOVED [[plugins]]: {tgt}")
            else:
                print(f"  MCP 未找到: {tgt}")
        return

    tgt = _mcp_target(p, scope)
    if not tgt: return  # no MCP target for this scope
    fmt = p.get("config_format", "json")
    cfg = _load_config(tgt, fmt)
    if "mcpServers" in cfg and "3dai-led" in cfg["mcpServers"]:
        del cfg["mcpServers"]["3dai-led"]
        if not cfg["mcpServers"]: del cfg["mcpServers"]
        _save_config(tgt, cfg, fmt)
    else:
        print(f"  MCP 未找到: {tgt}")

# ── 插件/技能安装 ─────────────────────────────────────
def install_plugin(platform_key, scope="global"):
    p = PLATFORMS[platform_key]
    if not p.get("has_plugin"): return
    src = PROJECT_DIR / "plugin" / "3dai-led.js"
    if not src.exists(): return
    pdir = p["project_plugin"] if scope == "project" else p["global_plugin"]
    dst = pdir / "3dai-led.js"
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
    dst = pdir / "3dai-led.js"
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
        # Enable hooks in config.toml (Codex requires [features] hooks = true)
        if scope == "project":
            toml_path = PROJECT_DIR / ".codex" / "config.toml"
        else:
            toml_path = Path.home() / ".codex" / "config.toml"
        _ensure_toml_feature(toml_path, "features", "hooks", "true")
    elif platform_key == "windsurf":
        # Windsurf format: flat [{command, show_output}], state in command
        tmpl = PROJECT_DIR / "hooks" / "windsurf_hooks.json"
        if not tmpl.exists(): return
        template = json.loads(tmpl.read_text(encoding="utf-8"))
        if scope == "project":
            cfg_path = PROJECT_DIR / ".windsurf" / "hooks.json"
            send_py = str(PROJECT_DIR / "transport" / "send.py")
        else:
            cfg_path = Path.home() / ".codeium" / "windsurf" / "hooks.json"
            send_py = str(GLOBAL_BIN / "transport" / "send.py")
        existing = load_json(cfg_path)
        hooks = existing.get("hooks", {})
        for event, entries in template.get("hooks", {}).items():
            for entry in entries:
                entry["command"] = entry["command"].replace(
                    "python \"<PROJECT_DIR>/transport/send.py\"",
                    f'python "{send_py}"'
                )
            hooks[event] = entries  # Windsurf overwrites, not merges
        existing["hooks"] = hooks
        save_json(cfg_path, existing)
    elif platform_key == "reasonix":
        tmpl = PROJECT_DIR / "hooks" / "reasonix_hooks.json"
        if not tmpl.exists(): return
        template = json.loads(tmpl.read_text(encoding="utf-8"))
        if scope == "project":
            cfg_path = PROJECT_DIR / ".reasonix" / "settings.json"
            send_py = str(PROJECT_DIR / "transport" / "send.py")
        else:
            cfg_path = Path.home() / ".reasonix" / "settings.json"
            send_py = str(GLOBAL_BIN / "transport" / "send.py")
        import copy
        hooks = copy.deepcopy(template)
        for event, entries in hooks.get("hooks", {}).items():
            for entry in entries:
                entry["command"] = entry["command"].replace(
                    "\"<PROJECT_DIR>/transport/send.py\"",
                    f'"{send_py}"'
                )
        existing = load_json(cfg_path)
        existing["hooks"] = hooks.get("hooks", {})
        save_json(cfg_path, existing)
        print(f"  OK {cfg_path}")

def install_skill(scope="global", project_dir=None, platform_key=None):
    """Install skill file — auto-trigger platforms get SKILL.md, MCP-only get SKILL-mcp.md."""
    is_auto = platform_key in AUTO_TRIGGER_PLATFORMS if platform_key else False
    src = SKILL_SRC if is_auto else SKILL_MCP_SRC
    if not src.exists():
        return

    if scope == "global":
        targets = [
            Path.home() / ".claude" / "skills" / "3dai-led",
            Path.home() / ".agents" / "skills" / "3dai-led",
        ]
        if platform_key == "trae":
            targets.append(Path.home() / ".trae" / "builtin" / "global" / "skills" / "3dai-led")
        elif platform_key == "traecn":
            targets.append(Path.home() / ".trae-cn" / "builtin" / "global" / "skills" / "3dai-led")
        elif platform_key == "mimocode":
            targets.append(Path.home() / ".local" / "share" / "mimocode" / "skills" / "3dai-led")
        elif platform_key == "reasonix":
            targets.append(Path.home() / ".reasonix" / "skills" / "3dai-led")
    elif project_dir and platform_key:
        proj = Path(project_dir)
        p = PLATFORMS[platform_key]
        platform_dir = p.get("dir", platform_key)
        targets = [
            proj / f".{platform_dir}" / "skills" / "3dai-led",
        ]
        if platform_key in ("trae", "traecn"):
            targets.append(proj / f".{platform_dir}" / "builtin" / "trae" / "default" / "skills" / "3dai-led")
        elif platform_key == "reasonix":
            targets.append(proj / ".reasonix" / "skills" / "3dai-led")
    else:
        return

    for d in targets:
        dst = d / "SKILL.md"
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
    """Remove skill files. Destination names match install_skill."""
    if scope == "global":
        targets = [
            Path.home() / ".claude" / "skills" / "3dai-led",
            Path.home() / ".agents" / "skills" / "3dai-led",
        ]
        if platform_key == "trae":
            targets.append(Path.home() / ".trae" / "builtin" / "global" / "skills" / "3dai-led")
        elif platform_key == "traecn":
            targets.append(Path.home() / ".trae-cn" / "builtin" / "global" / "skills" / "3dai-led")
        elif platform_key == "mimocode":
            targets.append(Path.home() / ".local" / "share" / "mimocode" / "skills" / "3dai-led")
        elif platform_key == "reasonix":
            targets.append(Path.home() / ".reasonix" / "skills" / "3dai-led")
    elif project_dir and platform_key:
        proj = Path(project_dir)
        p = PLATFORMS[platform_key]
        platform_dir = p.get("dir", platform_key)
        targets = [
            proj / f".{platform_dir}" / "skills" / "3dai-led",
        ]
        if platform_key in ("trae", "traecn"):
            targets.append(proj / f".{platform_dir}" / "builtin" / "trae" / "default" / "skills" / "3dai-led")
        elif platform_key == "reasonix":
            targets.append(proj / ".reasonix" / "skills" / "3dai-led")
    else:
        return

    for d in targets:
        dst = d / "SKILL.md"
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
    elif platform_key == "windsurf":
        # Windsurf: hooks are standalone file, just remove it
        if scope == "project":
            cfg_path = PROJECT_DIR / ".windsurf" / "hooks.json"
        else:
            cfg_path = Path.home() / ".codeium" / "windsurf" / "hooks.json"
        if cfg_path.exists():
            cfg_path.unlink()
            print(f"  REMOVED hooks: {cfg_path}")
        else:
            print(f"  Hooks 未找到: {cfg_path}")
    elif platform_key == "reasonix":
        # Reasonix uses flat hooks format — remove entries containing send.py
        if scope == "project":
            cfg_path = PROJECT_DIR / ".reasonix" / "settings.json"
        else:
            cfg_path = Path.home() / ".reasonix" / "settings.json"
        if not cfg_path.exists():
            print(f"  Hooks 未找到: {cfg_path}")
            return
        cfg = load_json(cfg_path)
        hooks = cfg.get("hooks", {})
        removed = 0
        for event in list(hooks.keys()):
            cleaned = [e for e in hooks[event] if "send.py" not in e.get("command", "")]
            removed += len(hooks[event]) - len(cleaned)
            if cleaned:
                hooks[event] = cleaned
            else:
                del hooks[event]
        if removed:
            cfg["hooks"] = hooks
            save_json(cfg_path, cfg)
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
    save_json(Path(path), {"mcpServers": {"3dai-led": dict(MCP_STDIO_CONFIG)}})
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

平台: claude, codex, cursor, windsurf, trae, traecn, opencode, mimocode, openclaw, reasonix
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
            tgt = p.get("global_mcp")
            if tgt and tgt.exists():
                try:
                    cfg = json.loads(tgt.read_text(encoding="utf-8"))
                    if "3dai-led" in cfg.get("mcpServers", {}): installed = True
                except: pass
        if p.get("has_plugin"):
            pdir = p.get("global_plugin")
            if pdir and (pdir / "3dai-led.js").exists(): installed = True
        if p.get("has_hooks") and key == "claude":
            hooks = Path.home() / ".claude" / "settings.json"
            if hooks.exists() and "send.py" in hooks.read_text(encoding="utf-8", errors="ignore"): installed = True
        if p.get("has_hooks") and key == "codex":
            hooks = Path.home() / ".codex" / "hooks.json"
            if hooks.exists() and "send.py" in hooks.read_text(encoding="utf-8", errors="ignore"): installed = True
        if p.get("has_hooks") and key == "windsurf":
            hooks = Path.home() / ".codeium" / "windsurf" / "hooks.json"
            if hooks.exists() and "send.py" in hooks.read_text(encoding="utf-8", errors="ignore"): installed = True
        if p.get("has_hooks") and key == "reasonix":
            hooks = Path.home() / ".reasonix" / "settings.json"
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
