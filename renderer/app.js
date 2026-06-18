// ── State ──────────────────────────────────────────
const state = {
  platforms: [],
  deviceId: '',
  projectDir: '',
  scope: 'global',
  busy: false,
};

// Platform types for badge display
const PLATFORM_TYPES = {
  claude:    ['hooks', 'skill', 'mcp'],
  codex:     ['hooks', 'skill', 'mcp'],
  cursor:    ['skill', 'mcp'],
  windsurf:  ['skill', 'mcp'],
  trae:      ['skill', 'mcp'],
  traecn:    ['skill', 'mcp'],
  opencode:  ['plugin', 'skill', 'mcp'],
  mimocode:  ['plugin', 'skill', 'mcp'],
  openclaw:  ['skill', 'mcp'],
  hermes:    ['skill', 'mcp'],
};

const TYPE_LABELS = {
  hooks: 'Hooks',
  plugin: 'Plugin',
  skill: 'Skill',
  mcp: 'MCP',
};

// ── DOM refs ──────────────────────────────────────
const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

const dom = {
  statusBar: $('#status-bar'),
  // Env
  envPython: $('#env-python'),
  envBleak: $('#env-bleak'),
  envMcp: $('#env-mcp'),
  btnInstallDeps: $('#btn-install-deps'),
  // Device
  deviceId: $('#device-id'),
  btnScan: $('#btn-scan'),
  btnUnbind: $('#btn-unbind'),
  scanResults: $('#scan-results'),
  // Settings
  scopeRadios: $$('input[name="scope"]'),
  projectDirGroup: $('#project-dir-group'),
  projectDirInput: $('#project-dir'),
  btnBrowse: $('#btn-browse'),
  // Platform table
  platformTbody: $('#platform-tbody'),
  scopeLabel: $('#scope-label'),
  // Export
  btnExportMcp: $('#btn-export-mcp'),
  btnExportSkill: $('#btn-export-skill'),
  exportResult: $('#export-result'),
};

// ── Helpers ───────────────────────────────────────
function setStatus(msg) { dom.statusBar.textContent = msg; }
function toast(msg) {
  let el = $('.toast');
  if (!el) {
    el = document.createElement('div');
    el.className = 'toast';
    document.body.appendChild(el);
  }
  el.textContent = msg;
  el.classList.add('show');
  setTimeout(() => el.classList.remove('show'), 2500);
}

function setBusy(busy) {
  state.busy = busy;
  const btns = $$('button');
  btns.forEach((b) => {
    if (b.id === 'btn-install-deps' && !busy) checkDepsButton();
    else b.disabled = busy;
  });
}

function checkDepsButton() {
  const pyOk = dom.envPython.classList.contains('ok');
  dom.btnInstallDeps.disabled = !(pyOk && (
    dom.envBleak.classList.contains('fail') ||
    dom.envMcp.classList.contains('fail')
  ));
}

// ── Environment ───────────────────────────────────
async function checkEnv() {
  setStatus('检测环境中...');
  dom.envPython.textContent = '检测中...';
  dom.envPython.className = 'badge checking';
  dom.envBleak.textContent = '检测中...';
  dom.envBleak.className = 'badge checking';
  dom.envMcp.textContent = '检测中...';
  dom.envMcp.className = 'badge checking';

  try {
    const env = await window.electronAPI.checkEnv();
    setBadge(dom.envPython, env.python, env.pythonVer || 'OK', '未安装');
    setBadge(dom.envBleak, env.bleak, 'OK', '需安装');
    setBadge(dom.envMcp, env.mcp, 'OK', '需安装');
    checkDepsButton();
    setStatus('就绪');
  } catch (e) {
    setStatus('环境检测失败: ' + e.message);
  }
}

function setBadge(el, ok, okText, failText) {
  el.textContent = ok ? okText : failText;
  el.className = 'badge ' + (ok ? 'ok' : 'fail');
}

// ── Install Dependencies ──────────────────────────
async function installDeps() {
  setBusy(true);
  setStatus('安装依赖中... pip install bleak mcp');
  try {
    const r = await window.electronAPI.installDeps();
    if (r.success) {
      toast('依赖安装完成');
    } else {
      toast('依赖安装失败: ' + r.output);
    }
    await checkEnv();
  } finally {
    setBusy(false);
  }
}

// ── Platform Status ───────────────────────────────
async function loadStatus() {
  setStatus('加载平台状态...');
  try {
    if (state.scope === 'project' && state.projectDir) {
      // Check project-level config files in the selected directory
      const r = await window.electronAPI.getProjectStatus(state.projectDir);
      state.platforms = r.platforms;
    } else {
      const r = await window.electronAPI.getStatus();
      state.platforms = r.platforms;
      state.deviceId = r.deviceId;
      renderDevice();
    }
    renderPlatforms();
  } catch (e) {
    setStatus('加载失败: ' + e.message);
  }
  setStatus('就绪');
}

function renderPlatforms() {
  const tbody = dom.platformTbody;
  tbody.innerHTML = '';
  const isProjectNoDir = state.scope === 'project' && !state.projectDir;
  const scopeLabel = state.scope === 'project'
    ? (state.projectDir ? `项目 (${state.projectDir.split(/[/\\]/).pop()})` : '项目')
    : '全局';
  dom.scopeLabel.textContent = scopeLabel;

  for (const p of state.platforms) {
    // Type badges
    const types = PLATFORM_TYPES[p.key] || [];
    const badgesHtml = types.map(t =>
      `<span class="type-badge ${t}">${TYPE_LABELS[t] || t}</span>`
    ).join('');

    // Status: show "-" when project mode but no dir selected
    let statusHtml;
    if (isProjectNoDir) {
      statusHtml = '<span class="platform-status dash">—</span>';
    } else {
      statusHtml = p.installed
        ? '<span class="platform-status installed">已安装</span>'
        : '<span class="platform-status uninstalled">未安装</span>';
    }

    // Actions: show "-" when project mode but no dir selected
    let actionsHtml;
    if (isProjectNoDir) {
      actionsHtml = '<span class="platform-status dash">—</span>';
    } else if (p.installed) {
      actionsHtml = `<button class="btn danger small" data-action="uninstall" data-key="${esc(p.key)}">卸载</button>`;
    } else {
      actionsHtml = `<button class="btn small" data-action="install" data-key="${esc(p.key)}">安装</button>`;
    }

    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${esc(p.name)}</td>
      <td><div class="type-badges">${badgesHtml}</div></td>
      <td>${statusHtml}</td>
      <td class="platform-actions">${actionsHtml}</td>`;
    tbody.appendChild(tr);
  }

  // Attach handlers (only when actions are buttons, not "-")
  tbody.querySelectorAll('button[data-action="install"]').forEach((btn) => {
    btn.addEventListener('click', () => installPlatform(btn.dataset.key));
  });
  tbody.querySelectorAll('button[data-action="uninstall"]').forEach((btn) => {
    btn.addEventListener('click', () => uninstallPlatform(btn.dataset.key));
  });
}

function renderDevice() {
  if (state.deviceId) {
    dom.deviceId.textContent = state.deviceId;
    dom.deviceId.classList.remove('dim');
    dom.btnUnbind.disabled = false;
  } else {
    dom.deviceId.textContent = '未绑定';
    dom.deviceId.classList.add('dim');
    dom.btnUnbind.disabled = true;
  }
}

// ── Install / Uninstall ───────────────────────────
async function installPlatform(key) {
  if (state.scope === 'project' && !state.projectDir) {
    toast('请先选择项目目录');
    return;
  }
  if (!confirm(`确认安装 ${key} (${state.scope === 'project' ? '项目级' : '全局'})？`)) return;

  setBusy(true);
  setStatus(`安装 ${key} (${state.scope}) 中...`);
  try {
    const r = await window.electronAPI.installPlatform(key, state.scope, state.projectDir);
    if (r.success) {
      toast(`${key} 安装完成`);
    } else {
      toast(`${key} 安装出错`);
    }
    await loadStatus();
  } finally {
    setBusy(false);
  }
}

async function uninstallPlatform(key) {
  if (!confirm(`确认卸载 ${key}？`)) return;

  setBusy(true);
  setStatus(`卸载 ${key} 中...`);
  try {
    const r = await window.electronAPI.uninstallPlatform(key, state.scope, state.projectDir);
    if (r.success) {
      toast(`${key} 已卸载`);
    } else {
      toast(`${key} 卸载出错`);
    }
    await loadStatus();
  } finally {
    setBusy(false);
  }
}

// ── Device Scan / Bind ────────────────────────────
async function scanDevices() {
  setBusy(true);
  setStatus('扫描设备中...');
  dom.scanResults.innerHTML = '<div class="hint" style="padding:8px">扫描中...</div>';
  try {
    const r = await window.electronAPI.scanDevices();
    if (r.devices.length === 0) {
      dom.scanResults.innerHTML = '<div class="hint" style="padding:8px">未发现设备</div>';
    } else {
      renderScanResults(r.devices);
    }
  } catch (e) {
    dom.scanResults.innerHTML = `<div class="hint" style="padding:8px">扫描出错: ${esc(e.message)}</div>`;
  } finally {
    setBusy(false);
    setStatus('就绪');
  }
}

function renderScanResults(devices) {
  dom.scanResults.innerHTML = '';
  for (const d of devices) {
    const div = document.createElement('div');
    div.className = 'scan-device' + (d.bound ? ' bound' : '');
    div.innerHTML = `
      <div>
        <div>${esc(d.id)}</div>
        <div class="ip">${esc(d.ip)}${d.bound ? ' [已绑定]' : ''}</div>
      </div>
    `;
    if (!d.bound) {
      const btn = document.createElement('button');
      btn.className = 'btn small';
      btn.textContent = '绑定';
      btn.addEventListener('click', (e) => { e.stopPropagation(); bindDevice(d.id); });
      div.appendChild(btn);
    }
    dom.scanResults.appendChild(div);
  }
}

async function bindDevice(deviceId) {
  setBusy(true);
  setStatus(`绑定 ${deviceId}...`);
  try {
    const r = await window.electronAPI.bindDevice(deviceId);
    if (r.success) {
      state.deviceId = deviceId;
      renderDevice();
      toast(`已绑定: ${deviceId}`);
      await scanDevices(); // refresh scan results
    } else {
      toast('绑定失败: ' + r.output);
    }
  } finally {
    setBusy(false);
  }
}

async function unbindDevice() {
  setBusy(true);
  try {
    await window.electronAPI.unbindDevice();
    state.deviceId = '';
    renderDevice();
    toast('绑定已解除');
    dom.scanResults.innerHTML = '';
  } finally {
    setBusy(false);
  }
}

// ── Export ────────────────────────────────────────
async function exportMcp() {
  const dir = await window.electronAPI.selectSaveDir();
  if (!dir) return;
  setBusy(true);
  setStatus('导出 mcp.json...');
  try {
    const r = await window.electronAPI.exportMcp(dir);
    if (r.success) {
      dom.exportResult.textContent = `已导出: ${r.path}`;
      toast('mcp.json 已导出');
    } else {
      dom.exportResult.textContent = '导出失败: ' + r.output;
    }
  } finally {
    setBusy(false);
  }
}

async function exportSkill() {
  const dir = await window.electronAPI.selectSaveDir();
  if (!dir) return;
  setBusy(true);
  setStatus('导出 SKILL.md...');
  try {
    const r = await window.electronAPI.exportSkill(dir);
    if (r.success) {
      dom.exportResult.textContent = `已导出: ${r.path}`;
      toast('SKILL.md 已导出');
    } else {
      dom.exportResult.textContent = '导出失败: ' + r.error;
    }
  } finally {
    setBusy(false);
  }
}

// ── Project Directory ─────────────────────────────
async function selectProjectDir() {
  const dir = await window.electronAPI.selectDir();
  if (dir) {
    state.projectDir = dir;
    dom.projectDirInput.value = dir;
    loadStatus();  // Re-scan status for the selected project
  }
}

// ── Scope switch ──────────────────────────────────
function onScopeChange(e) {
  state.scope = e.target.value;
  dom.projectDirGroup.style.display = state.scope === 'project' ? 'flex' : 'none';
  if (state.scope === 'global') {
    state.projectDir = '';
    dom.projectDirInput.value = '';
  }
  loadStatus();
}

// ── Utilities ─────────────────────────────────────
function esc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }

// ── Init ──────────────────────────────────────────
function init() {
  // Scope radio
  dom.scopeRadios.forEach((r) => r.addEventListener('change', onScopeChange));

  // Env
  dom.btnInstallDeps.addEventListener('click', installDeps);

  // Device
  dom.btnScan.addEventListener('click', scanDevices);
  dom.btnUnbind.addEventListener('click', unbindDevice);

  // Browse
  dom.btnBrowse.addEventListener('click', selectProjectDir);

  // Export
  dom.btnExportMcp.addEventListener('click', exportMcp);
  dom.btnExportSkill.addEventListener('click', exportSkill);

  // Load data
  checkEnv();
  loadStatus();
}

init();
