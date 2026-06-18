$ErrorActionPreference = "Stop"
Set-Location "$PSScriptRoot"

Write-Host "Vibe Coding LED Manager - Build" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path "node_modules")) {
    Write-Host "Installing dependencies..."
    npm install
}
Write-Host "Building portable EXE..."
$env:CSC_IDENTITY_AUTO_DISCOVERY = "false"
npx electron-builder --win portable 2>&1 | Select-Object -Last 5
Write-Host ""
Write-Host "Done: dist/Vibe-Coding-LED-Manager-1.0.0-win-x64.exe" -ForegroundColor Green
