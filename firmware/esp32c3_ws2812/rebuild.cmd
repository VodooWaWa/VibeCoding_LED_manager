@echo off
setlocal EnableDelayedExpansion
echo ========================================
echo  WS2812 AI Status LED - Build ^& Flash
echo ========================================
echo.
echo [1/3] Setting up ESP-IDF environment...
set IDF_PATH=C:\esp\v6.0.1\esp-idf
set "PATH=C:\Espressif\tools\python\v6.0.1\venv\Scripts;C:\esp\v6.0.1\esp-idf\tools;%PATH%"
if !errorlevel! neq 0 (
    echo ERROR: Failed to set up ESP-IDF
    pause
    exit /b 1
)
echo.
echo [2/3] Building firmware (incremental)...
cd /d "%~dp0"
C:\Espressif\tools\python\v6.0.1\venv\Scripts\python.exe C:\esp\v6.0.1\esp-idf\tools\idf.py build
if !errorlevel! neq 0 (
    echo ERROR: Build failed
    pause
    exit /b 1
)
echo.
echo [3/3] Flashing to COM16...
C:\Espressif\tools\python\v6.0.1\venv\Scripts\python.exe C:\esp\v6.0.1\esp-idf\tools\idf.py -p COM16 flash
if !errorlevel! neq 0 (
    echo ERROR: Flash failed
    pause
    exit /b 1
)
echo.
echo Merging firmware...
python -m esptool --chip esp32c3 merge-bin --flash-mode dio --flash-size 4MB --flash-freq 80m -o release\ws2812_ai_status_led_full.bin 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\ws2812_chase_test.bin
if !errorlevel! neq 0 (
    echo ERROR: Merge failed
    pause
    exit /b 1
)
echo.
echo ========================================
echo  DONE! Merged binary: release\ws2812_ai_status_led_full.bin
echo ========================================
pause
