const { app, BrowserWindow, ipcMain, dialog, Menu } = require('electron');
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

// ── Paths ──────────────────────────────────────────────
// In development: PROJECT_DIR = electron-ci/.. (project root)
// In packaged build: PROJECT_DIR = process.resourcesPath (extraResources root)
const isPackaged = app.isPackaged;
const PROJECT_DIR = isPackaged
  ? process.resourcesPath
  : path.resolve(__dirname, '..');
const INSTALL_PY = path.join(PROJECT_DIR, 'install.py');
const SKILL_MCP_SRC = path.join(PROJECT_DIR, 'skill', 'SKILL-mcp.md');
const SKILL_SRC = path.join(PROJECT_DIR, 'skill', 'SKILL.md');

// Project-level config paths for each platform (mirrors install.py PLATFORMS)
const PLATFORM_DEFS = {
  claude:    { name: 'Claude Code',    has_hooks: true,  project_mcp: '.mcp.json',              hooks_file: '.claude/settings.local.json' },
  codex:     { name: 'Codex CLI',      has_hooks: true,  project_mcp: '.codex/config.toml',      hooks_file: '.codex/hooks.json' },
  cursor:    { name: 'Cursor',         project_mcp: '.cursor/mcp.json' },
  windsurf:  { name: 'Windsurf',       project_mcp: '.windsurf/mcp.json' },
  trae:      { name: 'Trae',           project_mcp: '.trae/mcp.json' },
  traecn:    { name: 'TraeCN',         project_mcp: '.trae-cn/mcp.json' },
  opencode:  { name: 'OpenCode',       project_mcp: 'opencode.json',          plugin_file: '.opencode/plugins/esp32-led.js' },
  mimocode:  { name: 'MiMoCode',       project_mcp: '.mimocode/mimocode.json', plugin_file: '.mimocode/plugins/esp32-led.js' },
  openclaw:  { name: 'OpenClaw',       project_mcp: '.openclaw/mcp.json' },
  hermes:    { name: 'Hermes Agent',   project_mcp: '.hermes/mcp.json' },
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
      resolve({ code, stdout, stderr });
    });
    child.on('error', (err) => {
      resolve({ code: -1, stdout, stderr: err.message });
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
  return { ok: r.code === 0, stdout: r.stdout, stderr: r.stderr };
});

ipcMain.handle('get-status', async () => {
  const r = await runPython(['install.py', 'status']);
  return r.stdout;
});

ipcMain.handle('install-platform', async (_e, platform, scope) => {
  const args = ['install.py', 'install', '--target', platform, '--scope', scope];
  const r = await runPython(args, { timeout: 30000 });
  return { ok: r.code === 0, stdout: r.stdout, stderr: r.stderr };
});

ipcMain.handle('uninstall-platform', async (_e, platform, scope) => {
  const args = ['install.py', 'uninstall', '--target', platform, '--scope', scope];
  const r = await runPython(args, { timeout: 30000 });
  return { ok: r.code === 0, stdout: r.stdout, stderr: r.stderr };
});

ipcMain.handle('scan-devices', async () => {
  const r = await runPython(['install.py', 'scan']);
  return r.stdout;
});

ipcMain.handle('bind-device', async (_e, deviceId) => {
  const r = await runPython(['install.py', 'bind', deviceId]);
  return { ok: r.code === 0, stdout: r.stdout, stderr: r.stderr };
});

ipcMain.handle('unbind-device', async () => {
  const r = await runPython(['install.py', 'unbind']);
  return { ok: r.code === 0, stdout: r.stdout, stderr: r.stderr };
});

ipcMain.handle('export-mcp', async (_e, outputPath) => {
  const args = outputPath ? ['install.py', 'export', outputPath] : ['install.py', 'export'];
  const r = await runPython(args);
  return { ok: r.code === 0, stdout: r.stdout, stderr: r.stderr };
});

ipcMain.handle('show-item-in-folder', async (_e, filePath) => {
  const { shell } = require('electron');
  shell.showItemInFolder(filePath);
});

// ── Window ───────────────────────────────────────────────

function createWindow() {
  Menu.setApplicationMenu(null);
  const win = new BrowserWindow({
    width: 960,
    height: 680,
    title: 'Vibe Coding LED Manager',
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
