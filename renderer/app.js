// ── State ──────────────────────────────────────────
const state = {
  platforms: [],
  devices: [],
  exportedPath: null,
  deviceId: '',
  projectDir: '',
  scope: 'global',
  busy: false,
  env: null,
};

const TYPE_LABELS = {
  hooks: 'Hooks',
  plugin: 'Plugin',
  skill: 'Skill',
  mcp: 'MCP',
};

// ── I18N ───────────────────────────────────────────
const I18N = {
  zh: {
    shopBtn: '设备购买',
    shopBtnTitle: '设备购买',
    shopTitle: 'Ai3D趣造',
    shopTaobao: '淘宝',
    shopPinduoduo: '拼多多',
    closeBtn: '关闭',
    envTitle: '运行环境',
    depsTitle: '依赖库',
    installDepsBtn: '安装依赖',
    deviceBindingTitle: '设备绑定',
    scanDeviceBtn: '扫描设备',
    unbindDeviceBtn: '解除绑定',
    installSettingsTitle: '安装设置',
    scopeLabel: '安装范围:',
    globalScope: '全局',
    projectScope: '项目',
    projectDirLabel: '项目目录:',
    browsePlaceholder: '点击浏览选择项目目录...',
    browseBtn: '浏览',
    platformListTitle: '平台列表',
    thPlatform: '平台',
    thType: '类型',
    thStatus: '状态',
    thActions: '操作',
    otherPlatformsTitle: '其他平台',
    hintText: '不支持自动安装的工具，导出配置文件后参考平台文档手动配置。',
    exportPkgBtn: '导出完整包',
    ready: '就绪',
    checking: '检测中...',
    needInstall: '需安装',
    notInstalled: '未安装',
    installed: '已安装',
    unbound: '未绑定',
    bound: '已绑定',
    visit: '访问',
    bind: '绑定',
    uninstall: '卸载',
    install: '安装',
    checkingEnvStatus: '检测环境中...',
    envCheckFailed: '环境检测失败: ',
    installDepsStatus: '安装依赖中... pip install bleak mcp',
    depsInstalled: '依赖安装完成',
    depsInstallFailed: '依赖安装失败: ',
    loadingStatus: '加载平台状态...',
    loadFailed: '加载失败: ',
    selectProjectFirst: '请先选择项目目录',
    confirmInstall: '确认安装 {0} ({1})？',
    projectLevel: '项目级',
    globalLevel: '全局',
    installingStatus: '安装 {0} ({1}) 中...',
    installComplete: '{0} 安装完成',
    installError: '{0} 安装出错',
    confirmUninstall: '确认卸载 {0}？',
    uninstallingStatus: '卸载 {0} 中...',
    uninstalled: '{0} 已卸载',
    uninstallError: '{0} 卸载出错',
    scanningStatus: '扫描设备中...',
    scanningLabel: '扫描中...',
    noDeviceFound: '未发现设备',
    scanError: '扫描出错: ',
    bindingStatus: '绑定 {0}...',
    boundTo: '已绑定: {0}',
    bindFailed: '绑定失败: ',
    unboundSuccess: '绑定已解除',
    exportedTo: '已导出: {0}',
    exportFailed: '导出失败: ',
    exportingPkg: '导出包...',
    scopeGlobal: '全局',
    scopeProject: '项目',
    scopeProjectWithDir: '项目 ({0})',
  },
  en: {
    shopBtn: 'Buy Device',
    shopBtnTitle: 'Buy Device',
    shopTitle: 'Ai3D趣造',
    shopTaobao: 'Taobao',
    shopPinduoduo: 'Pinduoduo',
    closeBtn: 'Close',
    envTitle: 'Environment',
    depsTitle: 'Dependencies',
    installDepsBtn: 'Install Deps',
    deviceBindingTitle: 'Device Binding',
    scanDeviceBtn: 'Scan',
    unbindDeviceBtn: 'Unbind',
    installSettingsTitle: 'Install Settings',
    scopeLabel: 'Scope:',
    globalScope: 'Global',
    projectScope: 'Project',
    projectDirLabel: 'Project Dir:',
    browsePlaceholder: 'Click to select project directory...',
    browseBtn: 'Browse',
    platformListTitle: 'Platforms',
    thPlatform: 'Platform',
    thType: 'Type',
    thStatus: 'Status',
    thActions: 'Actions',
    otherPlatformsTitle: 'Other Platforms',
    hintText: 'For tools without auto-install, export config files and follow platform docs to set up manually.',
    exportPkgBtn: 'Export Package',
    ready: 'Ready',
    checking: 'Checking...',
    needInstall: 'Required',
    notInstalled: 'Not Installed',
    installed: 'Installed',
    unbound: 'Not Bound',
    bound: 'Bound',
    visit: 'Visit',
    bind: 'Bind',
    uninstall: 'Uninstall',
    install: 'Install',
    checkingEnvStatus: 'Checking environment...',
    envCheckFailed: 'Env check failed: ',
    installDepsStatus: 'Installing dependencies...',
    depsInstalled: 'Dependencies installed',
    depsInstallFailed: 'Dependency install failed: ',
    loadingStatus: 'Loading platform status...',
    loadFailed: 'Load failed: ',
    selectProjectFirst: 'Please select a project directory first',
    confirmInstall: 'Confirm install {0} ({1})?',
    projectLevel: 'Project-level',
    globalLevel: 'Global',
    installingStatus: 'Installing {0} ({1})...',
    installComplete: '{0} installed',
    installError: '{0} install error',
    confirmUninstall: 'Confirm uninstall {0}?',
    uninstallingStatus: 'Uninstalling {0}...',
    uninstalled: '{0} uninstalled',
    uninstallError: '{0} uninstall error',
    scanningStatus: 'Scanning devices...',
    scanningLabel: 'Scanning...',
    noDeviceFound: 'No devices found',
    scanError: 'Scan error: ',
    bindingStatus: 'Binding {0}...',
    boundTo: 'Bound: {0}',
    bindFailed: 'Bind failed: ',
    unboundSuccess: 'Unbound',
    exportedTo: 'Exported: {0}',
    exportFailed: 'Export failed: ',
    exportingMcp: 'Exporting mcp.json...',
    exportingSkill: 'Exporting SKILL.md...',
    scopeGlobal: 'Global',
    scopeProject: 'Project',
    scopeProjectWithDir: 'Project ({0})',
  },
};

let lang = localStorage.getItem('ui-lang') || 'zh';

function t(key, ...args) {
  var dict = I18N[lang] || I18N.zh;
  var text = (dict && dict[key]) || (I18N.zh[key]) || key;
  for (var i = 0; i < args.length; i++) {
    text = text.replace(new RegExp('\\{' + i + '\\}', 'g'), String(args[i]));
  }
  return text;
}

function updateUITexts() {
  document.querySelectorAll('[data-i18n]').forEach(function(el) {
    el.textContent = t(el.dataset.i18n);
  });
  document.querySelectorAll('[data-i18n-title]').forEach(function(el) {
    el.title = t(el.dataset.i18nTitle);
  });
  document.querySelectorAll('[data-i18n-placeholder]').forEach(function(el) {
    el.placeholder = t(el.dataset.i18nPlaceholder);
  });
}

function updateLangButton() {
  document.getElementById('btn-lang').textContent = lang === 'zh' ? 'EN' : '中';
}

function switchLang() {
  lang = lang === 'zh' ? 'en' : 'zh';
  localStorage.setItem('ui-lang', lang);
  updateLangButton();
  updateUITexts();
  renderEnv();
  renderDevice();
  renderPlatforms();
  if (state.devices && state.devices.length) renderScanResults(state.devices);
  if (state.exportedPath) dom.exportResult.textContent = t('exportedTo', state.exportedPath);
  setStatus(t('ready'));
}

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
  btnExportPkg: $('#btn-export-pkg'),
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
  if (!busy) { renderDevice(); renderPlatforms(); }
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
  setStatus(t('checkingEnvStatus'));
  dom.envPython.textContent = t('checking');
  dom.envPython.className = 'badge checking';
  dom.envBleak.textContent = t('checking');
  dom.envBleak.className = 'badge checking';
  dom.envMcp.textContent = t('checking');
  dom.envMcp.className = 'badge checking';

  try {
    const env = await window.electronAPI.checkEnv();
    state.env = env;
    renderEnv();
    setStatus(t('ready'));
  } catch (e) {
    setStatus(t('envCheckFailed') + e.message);
  }
}

function setBadge(el, ok, okText, failText) {
  el.textContent = ok ? okText : failText;
  el.className = 'badge ' + (ok ? 'ok' : 'fail');
}

function renderEnv() {
  if (!state.env) return;
  var env = state.env;
  setBadge(dom.envPython, env.python, env.pythonVer || 'OK', t('notInstalled'));
  setBadge(dom.envBleak, env.bleak, 'OK', t('needInstall'));
  setBadge(dom.envMcp, env.mcp, 'OK', t('needInstall'));
  checkDepsButton();
}

// ── Install Dependencies ──────────────────────────
async function installDeps() {
  setBusy(true);
  setStatus(t('installDepsStatus'));
  try {
    const r = await window.electronAPI.installDeps();
    if (r.success) {
      toast(t('depsInstalled'));
    } else {
      toast(t('depsInstallFailed') + r.output);
    }
    await checkEnv();
  } finally {
    setBusy(false);
  }
}

// ── Platform Status ───────────────────────────────
async function loadStatus() {
  setStatus(t('loadingStatus'));
  try {
    if (state.scope === 'project' && state.projectDir) {
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
    setStatus(t('loadFailed') + e.message);
  }
  setStatus(t('ready'));
}

function renderPlatforms() {
  const tbody = dom.platformTbody;
  tbody.innerHTML = '';
  const isProjectNoDir = state.scope === 'project' && !state.projectDir;
  const scopeLabel = state.scope === 'project'
    ? (state.projectDir ? t('scopeProjectWithDir', state.projectDir.split(/[/\\]/).pop()) : t('scopeProject'))
    : t('scopeGlobal');
  dom.scopeLabel.textContent = scopeLabel;

  for (const p of state.platforms) {
    const types = [];
    if (p.has_hooks) types.push('hooks');
    if (p.has_plugin) types.push('plugin');
    types.push('skill');
    if (state.scope === 'project' ? p.has_project_mcp : p.has_mcp) types.push('mcp');
    const badgesHtml = types.map(t =>
      `<span class="type-badge ${t}">${TYPE_LABELS[t] || t}</span>`
    ).join('');

    let statusHtml;
    if (isProjectNoDir) {
      statusHtml = '<span class="platform-status dash">—</span>';
    } else {
      statusHtml = p.installed
        ? '<span class="platform-status installed">' + t('installed') + '</span>'
        : '<span class="platform-status uninstalled">' + t('notInstalled') + '</span>';
    }

    let actionsHtml;
    if (isProjectNoDir) {
      actionsHtml = '<span class="platform-status dash">—</span>';
    } else if (p.installed) {
      actionsHtml = `<button class="btn danger small" data-action="uninstall" data-key="${esc(p.key)}">${t('uninstall')}</button>`;
    } else {
      actionsHtml = `<button class="btn small" data-action="install" data-key="${esc(p.key)}">${t('install')}</button>`;
    }

    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${esc(p.name)}</td>
      <td><div class="type-badges">${badgesHtml}</div></td>
      <td>${statusHtml}</td>
      <td class="platform-actions">${actionsHtml}</td>`;
    tbody.appendChild(tr);
  }

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
    dom.deviceId.textContent = t('unbound');
    dom.deviceId.classList.add('dim');
    dom.btnUnbind.disabled = true;
  }
}

// ── Install / Uninstall ───────────────────────────
async function installPlatform(key) {
  if (state.scope === 'project' && !state.projectDir) {
    toast(t('selectProjectFirst'));
    return;
  }
  if (!confirm(t('confirmInstall', key, state.scope === 'project' ? t('projectLevel') : t('globalLevel')))) return;

  setBusy(true);
  setStatus(t('installingStatus', key, t(state.scope === 'global' ? 'globalLevel' : 'projectLevel')));
  try {
    const r = await window.electronAPI.installPlatform(key, state.scope, state.projectDir);
    if (r.success) {
      toast(t('installComplete', key));
    } else {
      toast(t('installError', key));
    }
    await loadStatus();
  } finally {
    setBusy(false);
  }
}

async function uninstallPlatform(key) {
  if (!confirm(t('confirmUninstall', key))) return;

  setBusy(true);
  setStatus(t('uninstallingStatus', key));
  try {
    const r = await window.electronAPI.uninstallPlatform(key, state.scope, state.projectDir);
    if (r.success) {
      toast(t('uninstalled', key));
    } else {
      toast(t('uninstallError', key));
    }
    await loadStatus();
  } finally {
    setBusy(false);
  }
}

// ── Device Scan / Bind ────────────────────────────
async function scanDevices() {
  setBusy(true);
  setStatus(t('scanningStatus'));
  dom.scanResults.innerHTML = '<div class="hint" style="padding:8px">' + t('scanningLabel') + '</div>';
  try {
    const r = await window.electronAPI.scanDevices();
    if (!r || !r.devices) { dom.scanResults.innerHTML = '<div class="hint" style="padding:8px">' + t('scanError') + '</div>'; return; }
    state.devices = r.devices || [];
    if (state.devices.length === 0) {
      dom.scanResults.innerHTML = '<div class="hint" style="padding:8px">' + t('noDeviceFound') + '</div>';
    } else {
      renderScanResults(state.devices);
    }
  } catch (e) {
    dom.scanResults.innerHTML = '<div class="hint" style="padding:8px">' + t('scanError') + esc(e.message) + '</div>';
  } finally {
    setBusy(false);
    setStatus(t('ready'));
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
        <div class="ip">${esc(d.ip)}${d.bound ? ' [' + t('bound') + ']' : ''}</div>
      </div>
    `;
    const btnRow = document.createElement('div');
    btnRow.className = 'btn-row';
    const visitBtn = document.createElement('button');
    visitBtn.className = 'btn small';
    visitBtn.textContent = t('visit');
    visitBtn.addEventListener('click', (e) => { e.stopPropagation(); window.electronAPI.openExternal('http://' + d.ip); });
    btnRow.appendChild(visitBtn);
    if (!d.bound) {
      const bindBtn = document.createElement('button');
      bindBtn.className = 'btn small';
      bindBtn.textContent = t('bind');
      bindBtn.addEventListener('click', (e) => { e.stopPropagation(); bindDevice(d.id); });
      btnRow.appendChild(bindBtn);
    }
    div.appendChild(btnRow);
    dom.scanResults.appendChild(div);
  }
}

async function bindDevice(deviceId) {
  setBusy(true);
  setStatus(t('bindingStatus', deviceId));
  try {
    const r = await window.electronAPI.bindDevice(deviceId);
    if (r.success) {
      state.deviceId = deviceId;
      renderDevice();
      if (state.devices && state.devices.length) {
        state.devices = state.devices.map(d => d.id === deviceId ? { ...d, bound: true } : d);
        renderScanResults(state.devices);
      }
      toast(t('boundTo', deviceId));
    } else {
      toast(t('bindFailed') + r.output);
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
    toast(t('unboundSuccess'));
    dom.scanResults.innerHTML = '';
  } finally {
    setBusy(false);
  }
}

// ── Export ────────────────────────────────────────
async function exportPkg() {
  const dir = await window.electronAPI.selectSaveDir();
  if (!dir) return;
  setBusy(true);
  setStatus(t('exportingPkg'));
  try {
    const r = await window.electronAPI.exportPkg(dir);
    if (r.success) {
      state.exportedPath = r.path;
      dom.exportResult.textContent = t('exportedTo', r.path);
    } else {
      state.exportedPath = null;
      dom.exportResult.textContent = t('exportFailed') + (r.error || r.output || '');
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
    loadStatus();
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
  updateUITexts();
  updateLangButton();

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
  dom.btnExportPkg.addEventListener('click', exportPkg);

  // Language toggle
  document.getElementById('btn-lang').addEventListener('click', switchLang);

  // Load data
  checkEnv();
  loadStatus();
}

// Shop button
document.addEventListener('DOMContentLoaded', function() {
  const btnShop = document.getElementById('btn-shop');
  if (btnShop) {
    btnShop.addEventListener('click', function() {
      document.getElementById('shop-modal').classList.add('show');
    });
  }
  document.getElementById('shop-logo').src = 'ai3D趣造LOGO.jpg';
});

function openExternal(url) { window.electronAPI.openExternal(url); }

init();
