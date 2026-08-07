@echo off
echo ========================================
echo  WS2812 AI Status LED - One-Click Flash
echo ========================================
echo.

set PORT=COM16
if not "%1"=="" set PORT=%1

echo Flashing to %PORT%...
python -m esptool --chip esp32c3 -p %PORT% -b 460800 --before default_reset --after hard_reset write_flash 0x0 ws2812_ai_status_led_full.bin

if %errorlevel% equ 0 (
    echo.
    echo Flash SUCCESS! Device rebooting...
    echo WebUI: http://192.168.4.1/ (AP mode)
    echo or check your router for IP
) else (
    echo.
    echo Flash FAILED. Try:
    echo   flash.bat COM8   (change port)
    echo   Hold BOOT button while flashing
)
pause
