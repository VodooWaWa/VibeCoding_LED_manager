#!/usr/bin/env bash
# Vibe Coding LED Manager — Linux/macOS TUI
# All logic delegates to install.py / send.py
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RED="\033[0;31m" GREEN="\033[0;32m" YELLOW="\033[0;33m" CYAN="\033[0;36m" NC="\033[0m" BOLD="\033[1m"

detect_python() {
    PYTHON=""
    command -v python3 &>/dev/null && PYTHON=python3
    command -v python &>/dev/null && [ -z "$PYTHON" ] && PYTHON=python
    echo "$PYTHON"
}

check_env() {
    local PY
    PY=$(detect_python)
    clear 2>/dev/null || true
    echo ""
    echo -e "  ${BOLD}=== Vibe Coding LED Manager ===${NC}"
    echo ""
    echo -e "  ${BOLD}运行环境${NC}"
    if [ -z "$PY" ]; then
        echo -e "    Python       ${RED}未安装${NC}"
    else
        echo -e "    Python       ${GREEN}$($PY --version 2>&1)${NC}"
        $PY -c "import bleak" 2>/dev/null && echo -e "    bleak        ${GREEN}OK${NC}" || echo -e "    bleak        ${RED}需安装${NC}"
        $PY -c "import mcp" 2>/dev/null && echo -e "    mcp          ${GREEN}OK${NC}" || echo -e "    mcp          ${RED}需安装${NC}"
    fi
    echo ""
    # Device binding
    local BIND_FILE="$HOME/.local/share/3dai-led/.3dai_device_id"
    if [ -f "$BIND_FILE" ]; then
        echo -e "  ${BOLD}设备绑定${NC}"
        echo -e "    ${GREEN}$(cat "$BIND_FILE")${NC}"
    else
        echo -e "  ${BOLD}设备绑定${NC}  未绑定"
    fi
    echo ""
}

show_platforms() {
    echo -e "  ${BOLD}平台列表${NC}"
    cd "$SCRIPT_DIR"
    $(detect_python) install.py status 2>/dev/null | while IFS= read -r line; do
        case "$line" in
            *"[已安装]"*) echo -e "    ${GREEN}$line${NC}" ;;
            *"[未安装]"*) echo "    $line" ;;
        esac
    done
    echo ""
}

# ── Main Menu ──────────────────────────────────────
main_menu() {
    while true; do
        check_env
        show_platforms
        echo "  [1] 安装平台        [4] 设备扫描"
        echo "  [2] 卸载平台        [5] 绑定/解绑设备"
        echo "  [3] 安装依赖        [6] 导出 mcp.json"
        echo ""
        echo "  [7] 导出 SKILL.md   [0] 退出"
        echo ""
        read -p "  输入序号: " OP
        case "$OP" in
            1) install_menu ;;
            2) uninstall_menu ;;
            3) install_deps; press_enter ;;
            4) scan_devices; press_enter ;;
            5) device_menu ;;
            6) export_mcp; press_enter ;;
            7) export_skill; press_enter ;;
            0) echo ""; exit 0 ;;
        esac
    done
}

# ── Install ────────────────────────────────────────
install_menu() {
    local TARGET SCOPE PROJ_DIR
    pick_platform; TARGET=$RET
    pick_scope; SCOPE=$RET
    if [ "$SCOPE" = "project" ]; then pick_project_dir; PROJ_DIR=$RET; fi
    echo ""
    echo -e "  ${CYAN}即将安装: $TARGET / $SCOPE${NC}"
    read -p "  确认? 回车继续, N 取消: " OK
    [ "${OK,,}" = "n" ] && return
    echo ""; cd "$SCRIPT_DIR"
    local ARGS="install --target $TARGET --scope $SCOPE"
    [ -n "$PROJ_DIR" ] && ARGS="$ARGS --project-dir \"$PROJ_DIR\""
    $(detect_python) install.py $ARGS 2>&1 | tail -20
    echo ""; echo -e "  ${GREEN}完成！${NC}重启 AI 工具生效。"
    press_enter
}

uninstall_menu() {
    local TARGET SCOPE PROJ_DIR
    pick_platform; TARGET=$RET
    pick_scope; SCOPE=$RET
    if [ "$SCOPE" = "project" ]; then pick_project_dir; PROJ_DIR=$RET; fi
    echo ""
    echo -e "  ${CYAN}即将卸载: $TARGET / $SCOPE${NC}"
    read -p "  确认? 回车继续, N 取消: " OK
    [ "${OK,,}" = "n" ] && return
    echo ""; cd "$SCRIPT_DIR"
    local ARGS="uninstall --target $TARGET --scope $SCOPE"
    [ -n "$PROJ_DIR" ] && ARGS="$ARGS --project-dir \"$PROJ_DIR\""
    $(detect_python) install.py $ARGS 2>&1 | tail -20
    echo ""; echo -e "  ${GREEN}完成！${NC}"
    press_enter
}

# ── Device ─────────────────────────────────────────
scan_devices() {
    echo ""
    echo "  正在扫描局域网设备..."
    cd "$SCRIPT_DIR"
    $(detect_python) install.py scan 2>&1 | tail -20
}

device_menu() {
    echo ""; echo "  设备管理"
    echo "  [1] 扫描设备"
    echo "  [2] 绑定设备"
    echo "  [3] 解除绑定"
    echo "  [0] 返回"
    read -p "  输入序号: " OP
    case "$OP" in
        1) scan_devices; press_enter ;;
        2) read -p "  输入设备 ID (如 3dai-led-7604F1A8): " DID
           cd "$SCRIPT_DIR"; $(detect_python) install.py bind "$DID" 2>&1 | tail -5
           press_enter ;;
        3) cd "$SCRIPT_DIR"; $(detect_python) install.py unbind 2>&1
           echo -e "  ${GREEN}绑定已解除${NC}"; press_enter ;;
    esac
}

# ── Dependencies ───────────────────────────────────
install_deps() {
    echo ""; echo "  正在安装依赖..."
    local PY; PY=$(detect_python)
    [ -z "$PY" ] && { echo -e "  ${RED}Python 未安装${NC}"; return; }
    $PY -m pip install bleak mcp 2>&1 | tail -5
    echo -e "  ${GREEN}完成${NC}"
}

# ── Export ─────────────────────────────────────────
export_mcp() {
    echo ""; cd "$SCRIPT_DIR"
    $(detect_python) install.py export 2>&1 | tail -5
    echo ""
    echo "  复制 mcp.json 到对应平台配置目录:"
    echo "    Cursor:    ~/.cursor/mcp.json"
    echo "    Windsurf:  ~/.windsurf/mcp.json"
    echo "    Trae:      ~/.trae/mcp.json"
    echo "    其它:      查阅平台 MCP 文档"
}

export_skill() {
    echo ""
    read -p "  导出到目录 (默认当前): " DIR
    DIR="${DIR:-.}"
    local SRC="$SCRIPT_DIR/skill/SKILL.md"
    [ -f "$SRC" ] && cp "$SRC" "$DIR/SKILL.md" && echo -e "  ${GREEN}已导出: $DIR/SKILL.md${NC}" || echo -e "  ${RED}SKILL.md 不存在${NC}"
}

# ── Helpers ────────────────────────────────────────
press_enter() { echo ""; read -p "  按回车返回..."; }

pick_platform() {
    echo ""; echo "  选择平台:"
    echo "    [1] Claude Code     [6] OpenCode"
    echo "    [2] Codex CLI       [7] MiMoCode"
    echo "    [3] Cursor          [8] OpenClaw"
    echo "    [5] Trae / TraeCN   [0] 返回"
    read -p "  输入序号: " T
    case "$T" in
        1) RET=claude ;; 2) RET=codex ;; 3) RET=cursor ;;
        4) RET=windsurf ;; 5) RET=trae ;; 6) RET=opencode ;;
        0) RET="" ;;
        *) echo "  无效"; pick_platform ;;
    esac
}

pick_scope() {
    echo ""; echo "  安装范围: [G] 全局  [P] 项目"
    read -p "  输入 (默认 G): " S
    case "${S,,}" in p|project) RET="project" ;; *) RET="global" ;; esac
}

pick_project_dir() {
    echo ""; echo "  选择项目目录:"
    read -e -p "  输入路径: " DIR
    [ -z "$DIR" ] && { echo "  已取消"; main_menu; }
    [ ! -d "$DIR" ] && { echo "  目录不存在"; pick_project_dir; }
    RET="$DIR"
}

# ── Entry ──────────────────────────────────────────
main_menu
