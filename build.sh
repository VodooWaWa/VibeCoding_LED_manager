#!/bin/bash
set -e
cd "$(dirname "$0")"
echo "Vibe Coding LED Manager - Build"
echo ""
if [ ! -d "node_modules" ]; then npm install; fi
echo "Building portable EXE..."
CSC_IDENTITY_AUTO_DISCOVERY=false npx electron-builder --win portable
echo ""
echo "Done: dist/Vibe-Coding-LED-Manager-1.0.0-win-x64.exe"
