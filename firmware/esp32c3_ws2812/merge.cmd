@echo off
setlocal EnableDelayedExpansion
:: Merge firmware into single binary for distribution
set IDF_PATH=C:\esp\v6.0.1\esp-idf
set "PATH=C:\Espressif\tools\python\v6.0.1\venv\Scripts;C:\esp\v6.0.1\esp-idf\tools;%PATH%"
cd /d "%~dp0"
python -m esptool --chip esp32c3 merge-bin --flash-mode dio --flash-size 4MB --flash-freq 80m -o release\ws2812_ai_status_led_full.bin 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\Vibe_Coding_LED_state.bin
if !errorlevel! neq 0 (
    echo MERGE FAILED
    pause
    exit /b 1
)
echo Merged to: release\ws2812_ai_status_led_full.bin
pause
