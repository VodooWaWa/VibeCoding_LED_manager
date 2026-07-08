const { app, BrowserWindow, ipcMain, dialog, Menu } = require('electron');
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

// ── Paths ──────────────────────────────────────────────
// In development: PROJECT_DIR = electron-manager directory itself
// In packaged build: PROJECT_DIR = process.resourcesPath (extraResources root)
const isPackaged = app.isPackaged;
const PROJECT_DIR = isPackaged
  ? process.resourcesPath
  : __dirname;
const INSTALL_PY = path.join(PROJECT_DIR, 'install.py');
const SKILL_MCP_SRC = path.join(PROJECT_DIR, 'skill', 'SKILL-mcp.md');
const SKILL_SRC = path.join(PROJECT_DIR, 'skill', 'SKILL.md');

// Project-level config paths for each platform (mirrors install.py PLATFORMS)
const PLATFORM_DEFS = {
  claude:    { name: 'Claude Code',    has_hooks: true,  has_mcp: true,  project_mcp: '.mcp.json',                   hooks_file: '.claude/settings.local.json' },
  codex:     { name: 'Codex CLI',      has_hooks: true,  has_mcp: true,  project_mcp: '.codex/config.toml',           hooks_file: '.codex/hooks.json' },
  cursor:    { name: 'Cursor',                          has_mcp: true,  project_mcp: '.cursor/mcp.json' },
  windsurf:  { name: 'Windsurf',       has_hooks: true,  has_mcp: true,  project_mcp: null,                           hooks_file: '.windsurf/hooks.json' },
  trae:      { name: 'Trae',                            has_mcp: true,  project_mcp: '.trae/mcp.json' },
  traecn:    { name: 'TraeCN',                          has_mcp: true,  project_mcp: '.trae-cn/mcp.json' },
  opencode:  { name: 'OpenCode',                        has_mcp: true,  project_mcp: '.opencode.json',               plugin_file: '.opencode/plugins/3dai-led.js' },
  mimocode:  { name: 'MiMoCode',                        has_mcp: true,  project_mcp: '.mimocode/mimocode.json',       plugin_file: '.mimocode/plugins/3dai-led.js' },
  openclaw:  { name: 'OpenClaw',                        has_mcp: true,  project_mcp: '.openclaw/openclaw.json' },
};

// ── Verify resource directory ─────────────────────────────
function verifyResources() {
  if (!fs.existsSync(INSTALL_PY)) {
    const msg = isPackaged
      ? `Resource files not found.\n\nExpected install.py at:\n${INSTALL_PY}`
      : `Dev mode: install.py not found at:\n${INSTALL_PY}\n\nRun from the project root that contains these files.`;
    dialog.showErrorBox('Missing Resources', msg);
    app.quit();
    return false;
  }
  return true;
}

// ── Python detection ────────────────────────────────────
function getPython() {
  return process.platform === 'win32' ? 'python' : 'python3';
}

function runPython(args, opts = {}) {
  return new Promise((resolve) => {
    const python = getPython();
    const child = spawn(python, args, {
      cwd: PROJECT_DIR,
      timeout: opts.timeout || 120000,
      env: { ...process.env, PYTHONIOENCODING: 'utf-8' },
    });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', (d) => { stdout += d.toString(); });
    child.stderr.on('data', (d) => { stderr += d.toString(); });
    child.on('close', (code) => {
      resolve({ success: code === 0, code, stdout, stderr: stderr || '' });
    });
    child.on('error', (err) => {
      resolve({ success: false, code: -1, stdout: '', stderr: err.message });
    });
  });
}

// ── IPC Handlers ────────────────────────────────────────

ipcMain.handle('check-env', async () => {
  const result = { python: false, pythonVer: '', bleak: false, mcp: false };
  const py = await runPython(['--version']);
  if (py.code === 0) {
    result.python = true;
    result.pythonVer = py.stdout.trim();
  }
  const bleak = await runPython(['-c', 'import bleak']);
  result.bleak = bleak.code === 0;
  const mcp = await runPython(['-c', 'import mcp']);
  result.mcp = mcp.code === 0;
  return result;
});

ipcMain.handle('install-deps', async () => {
  const r = await runPython(['-m', 'pip', 'install', 'bleak', 'mcp'], { timeout: 120000 });
  return { success: r.success, output: r.stdout };
});

ipcMain.handle('get-status', async () => {
  const r = await runPython([INSTALL_PY, 'status']);
  const platforms = [];
  const lines = r.stdout.split('\n');
  for (const line of lines) {
    const m = line.match(/^\s*\[(已安装|未安装)\]\s+(\S+):\s+(.+)/);
    if (m) {
      const def = PLATFORM_DEFS[m[2]] || {};
      platforms.push({ key: m[2], name: m[3], installed: m[1] === '已安装',
        has_mcp: def.has_mcp !== false, has_project_mcp: !!(def.has_mcp && def.project_mcp),
        has_hooks: !!def.has_hooks, has_plugin: !!def.plugin_file });
    }
  }
  const bindLine = lines.find((l) => l.startsWith('设备绑定:'));
  let deviceId = '';
  if (bindLine) {
    const m = bindLine.match(/设备绑定:\s*(.+)/);
    if (m && m[1] !== '未设置') deviceId = m[1].trim();
  }
  return { platforms, deviceId, raw: r.stdout };
});

ipcMain.handle('install-platform', async (_e, platform, scope, projectDir) => {
  const args = [INSTALL_PY, 'install', '--target', platform, '--scope', scope];
  if (scope === 'project' && projectDir) args.push('--project-dir', projectDir);
  const r = await runPython(args, { timeout: 30000 });
  return { success: r.success, output: r.stdout };
});

ipcMain.handle('uninstall-platform', async (_e, platform, scope, projectDir) => {
  const args = [INSTALL_PY, 'uninstall', '--target', platform, '--scope', scope];
  if (scope === 'project' && projectDir) args.push('--project-dir', projectDir);
  const r = await runPython(args, { timeout: 30000 });
  return { success: r.success, output: r.stdout };
});

ipcMain.handle('scan-devices', async () => {
  const r = await runPython([INSTALL_PY, 'scan']);
  const devices = [];
  const lines = r.stdout.split('\n');
  for (const line of lines) {
    const m = line.match(/^\s*(3dai-led-\S+)\s+\(([^)]+)\)\s*(?:\[已绑定\])?\s*$/);
    if (m) devices.push({ id: m[1], ip: m[2], bound: line.includes('已绑定') });
  }
  return { devices };
});

ipcMain.handle('bind-device', async (_e, deviceId) => {
  const r = await runPython([INSTALL_PY, 'bind', deviceId]);
  return { success: r.success, output: r.stdout };
});

ipcMain.handle('unbind-device', async () => {
  const r = await runPython([INSTALL_PY, 'unbind']);
  return { success: r.success, output: r.stdout };
});

ipcMain.handle('export-pkg', async (_e, outputPath) => {
  const outDir = outputPath || path.join(PROJECT_DIR, '3dai-export');
  if (!fs.existsSync(outDir)) fs.mkdirSync(outDir, { recursive: true });

  // 1. MCP config
  const mcpCfg = {
    mcpServers: {
      "3dai-led": {
        command: "python",
        args: [path.join(".", "transport", "server.py")]
      }
    }
  };
  fs.writeFileSync(path.join(outDir, 'mcp.json'), JSON.stringify(mcpCfg, null, 2), 'utf-8');

  // 2. SKILL.md (MCP version)
  const skillSrc = path.join(PROJECT_DIR, 'skill', 'SKILL-mcp.md');
  if (fs.existsSync(skillSrc)) {
    fs.copyFileSync(skillSrc, path.join(outDir, 'SKILL.md'));
  }

  // 3. Transport files
  const transportDir = path.join(outDir, 'transport');
  if (!fs.existsSync(transportDir)) fs.mkdirSync(transportDir, { recursive: true });
  const srcTransport = path.join(PROJECT_DIR, 'transport');
  ['send.py', 'server.py', 'transport_ble.py', 'transport_wifi.py', '__init__.py'].forEach(f => {
    const s = path.join(srcTransport, f);
    if (fs.existsSync(s)) fs.copyFileSync(s, path.join(transportDir, f));
  });

  // 4. README
  const readme = [
    '# 3DAi LED Status Indicator / 3DAi AI 状态指示灯',
    '',
    'Vibe Coding Agent tools control WS2812 LED strip via WiFi/BLE. 8 RGB animations reflect real-time work state.',
    'Vibe Coding Agent 工具通过 WiFi/BLE 控制 WS2812 灯带，8 种 RGB 动画实时反映工作状态。',
    '',
    '## setup / 安装',
    '1. install deps: `pip install bleak mcp`',
    '2. MCP: copy `mcp.json` to your platform MCP config / 复制 `mcp.json` 到你的平台 MCP 配置',
    '3. SKILL.md: copy to your platform skill/rules directory / 复制到平台 skill/rules 目录',
    '4. transport/: keep as-is next to mcp.json (or edit `args` path in mcp.json) / 保持与 mcp.json 同级',
    '5. restart AI tool / 重启 AI 工具',
    '',
    '## MCP tools / MCP 工具',
    '| tool | description / 说明 |',
    '|------|------|',
    '| `send_led_state` | send LED state (thinking/coding/busy/waiting/success/error/alarm/off) |',
    '| `get_device_info` | query device status (IP, SSID, BLE, brightness) / 设备完整状态 |',
    '| `get_led_status` | current LED state + transport / 当前状态 |',
    '| `get_led_map` | 8-LED project allocation table / 8 灯珠项目分配 |',
    '| `set_led_brightness` | brightness 1-255 / 亮度 |',
    '| `configure_device_ble` | BLE on/off runtime switch / 蓝牙运行时开关 |',
    '| `set_ble_discoverable` | BLE scan visible (120s auto-off) / 蓝牙发现模式 |',
    '| `clear_ble_bonds` | clear all BLE pairings / 清除配对 |',
    '| `set_idle_timeout` | auto-standby seconds (0=always on) / 闲置超时 |',
    '| `set_device_language` | WebUI language zh/en / 语言 |',
    '| `scan_wifi` | nearby WiFi scan (JSON) / WiFi 扫描 |',
    '| `set_static_ip` | static IP config (reboot applies) / 固定 IP |',
    '| `list_led_states` | 8 states with Chinese descriptions / 状态列表 |',
    '',
    '## state calling rules / 状态调用规则 (SKILL.md)',
    '| trigger / 触发 | call / 调用 |',
    '|------|------|',
    '| receive user message / 收到消息 | `send_led_state(state="thinking")` |',
    '| before Edit/Write / 写文件前 | `send_led_state(state="coding")` |',
    '| before Bash/terminal / 执行命令前 | `send_led_state(state="busy")` |',
    '| wait for user input / 等待输入 | `send_led_state(state="waiting")` |',
    '| task succeeded / 任务完成 | `send_led_state(state="success")` |',
    '| command failed / 命令失败 | `send_led_state(state="error")` |',
    '| API error/exception / 异常 | `send_led_state(state="alarm")` |',
    '| session idle/ended / 空闲结束 | `send_led_state(state="off")` |',
    ''
  ].join('\n');
  fs.writeFileSync(path.join(outDir, 'README.md'), readme, 'utf-8');

  return { success: true, path: outDir };
});

ipcMain.handle('get-project-status', async (_e, projectDir) => {
  const platforms = [];
  if (!projectDir || !fs.existsSync(projectDir)) return { platforms };
  for (const [key, def] of Object.entries(PLATFORM_DEFS)) {
    let installed = false;
    const mcpConfig = def.project_mcp;
    if (def.has_mcp && mcpConfig) {
      const mcpPath = path.join(projectDir, mcpConfig);
      if (fs.existsSync(mcpPath)) {
        try {
          const raw = fs.readFileSync(mcpPath, 'utf-8');
          if (mcpPath.endsWith('.toml')) {
            installed = raw.includes('[mcpServers.') && raw.includes('3dai-led');
          } else {
            const cfg = JSON.parse(raw);
            installed = !!(cfg.mcpServers && cfg.mcpServers['3dai-led']);
          }
        } catch (_) {}
      }
    }
    if (def.plugin_file) {
      const pluginPath = path.join(projectDir, def.plugin_file);
      if (fs.existsSync(pluginPath)) installed = true;
    }
    if (def.has_hooks && def.hooks_file) {
      const hooksPath = path.join(projectDir, def.hooks_file);
      if (fs.existsSync(hooksPath)) {
        try { const cfg = JSON.parse(fs.readFileSync(hooksPath, 'utf-8')); installed = !!cfg.hooks; } catch (_) {}
      }
    }
    platforms.push({ key, name: def.name, installed,
      has_mcp: def.has_mcp !== false, has_project_mcp: !!(def.has_mcp && def.project_mcp),
      has_hooks: !!def.has_hooks, has_plugin: !!def.plugin_file });
  }
  return { platforms };
});

ipcMain.handle('select-dir', async () => {
  const { dialog } = require('electron');
  const result = await dialog.showOpenDialog({ properties: ['openDirectory'] });
  return result.canceled ? null : result.filePaths[0];
});

ipcMain.handle('select-save-dir', async () => {
  const { dialog } = require('electron');
  const result = await dialog.showOpenDialog({ properties: ['openDirectory', 'createDirectory'] });
  return result.canceled ? null : result.filePaths[0];
});

ipcMain.handle('show-item-in-folder', async (_e, filePath) => {
  const { shell } = require('electron');
  shell.showItemInFolder(filePath);
});

ipcMain.handle('open-external', async (_e, url) => {
  const { shell } = require('electron');
  await shell.openExternal(url);
});

// ── Window ───────────────────────────────────────────────

function createWindow() {
  Menu.setApplicationMenu(null);
  const win = new BrowserWindow({
    width: 1050,
    height: 920,
    title: 'Vibe Coding LED Manager - Ai3D趣造',
    icon: path.join(__dirname, 'icon.ico'),
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  win.loadFile(path.join(__dirname, 'renderer', 'index.html'));
}

app.whenReady().then(() => {
  if (verifyResources()) createWindow();
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});
