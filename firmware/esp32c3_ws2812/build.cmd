@echo off
setlocal EnableDelayedExpansion
echo ==============================================
echo  3DAi LED Firmware Build ^| Flash ^| Merge
echo ==============================================
echo.

echo [1/3] ESP-IDF environment...
set IDF_PATH=C:\esp\v6.0.1\esp-idf
set "PATH=C:\Espressif\tools\python\v6.0.1\venv\Scripts;C:\esp\v6.0.1\esp-idf\tools;%PATH%"
cd /d "%~dp0"

echo [2/3] Building...
C:\Espressif\tools\python\v6.0.1\venv\Scripts\python.exe C:\esp\v6.0.1\esp-idf\tools\idf.py build
if !errorlevel! neq 0 (
    echo BUILD FAILED
    pause
    exit /b 1
)
echo   Build OK

echo [3/3] Merging 0x0 full firmware...
set RELEASE_DIR=..\..\release\firmware
if not exist "%RELEASE_DIR%" mkdir "%RELEASE_DIR%"
set MERGE_OUT=%RELEASE_DIR%\3dai_led_full_test.bin
python -m esptool --chip esp32c3 merge_bin --flash_mode dio --flash_size 4MB --flash_freq 80m -o "%MERGE_OUT%" 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\Vibe_Coding_LED_state.bin
if !errorlevel! neq 0 (
    echo MERGE FAILED
    pause
    exit /b 1
)

echo ==============================================
echo  DONE: %MERGE_OUT%
echo ==============================================
pause
