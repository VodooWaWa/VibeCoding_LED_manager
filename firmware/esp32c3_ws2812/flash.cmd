@echo off
setlocal EnableDelayedExpansion
:: Build and flash to COM16
set IDF_PATH=C:\esp\v6.0.1\esp-idf
set "PATH=C:\Espressif\tools\python\v6.0.1\venv\Scripts;C:\esp\v6.0.1\esp-idf\tools;%PATH%"
cd /d "%~dp0"
C:\Espressif\tools\python\v6.0.1\venv\Scripts\python.exe C:\esp\v6.0.1\esp-idf\tools\idf.py build
if !errorlevel! neq 0 (
    echo BUILD FAILED
    pause
    exit /b 1
)
C:\Espressif\tools\python\v6.0.1\venv\Scripts\python.exe C:\esp\v6.0.1\esp-idf\tools\idf.py -p COM16 flash
if !errorlevel! neq 0 (
    echo FLASH FAILED
    pause
    exit /b 1
)
echo BUILD ^& FLASH SUCCESS
pause
