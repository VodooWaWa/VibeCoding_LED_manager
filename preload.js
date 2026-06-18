const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('electronAPI', {
  checkEnv: () => ipcRenderer.invoke('check-env'),
  installDeps: () => ipcRenderer.invoke('install-deps'),
  getStatus: () => ipcRenderer.invoke('get-status'),
  getProjectStatus: (projectDir) => ipcRenderer.invoke('get-project-status', projectDir),
  installPlatform: (key, scope, projectDir) =>
    ipcRenderer.invoke('install-platform', key, scope, projectDir),
  uninstallPlatform: (key, scope, projectDir) =>
    ipcRenderer.invoke('uninstall-platform', key, scope, projectDir),
  scanDevices: () => ipcRenderer.invoke('scan-devices'),
  bindDevice: (deviceId) => ipcRenderer.invoke('bind-device', deviceId),
  unbindDevice: () => ipcRenderer.invoke('unbind-device'),
  exportMcp: (outputDir) => ipcRenderer.invoke('export-mcp', outputDir),
  exportSkill: (outputDir) => ipcRenderer.invoke('export-skill', outputDir),
  selectDir: () => ipcRenderer.invoke('select-dir'),
  selectSaveDir: () => ipcRenderer.invoke('select-save-dir'),
});
