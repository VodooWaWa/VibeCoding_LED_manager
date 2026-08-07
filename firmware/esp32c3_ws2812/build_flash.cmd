@echo off
setlocal EnableDelayedExpansion
echo ==============================================
echo  3DAi LED Firmware Build ^| Flash ^| Merge
echo ==============================================
echo.

echo [1/4] ESP-IDF environment...
set IDF_PATH=D:\Program Files\esp\v6.0.1\esp-idf
set IDF_PYTHON_ENV_PATH=C:\Users\Vodoo\.espressif\python_env\idf6.0_py3.12_env\venv
set ESP_IDF_VERSION=6.0.1
set ESP_ROM_ELF_DIR=C:\Espressif\tools\esp-rom-elfs\20241011
set "PATH=C:\Users\Vodoo\.espressif\python_env\idf6.0_py3.12_env\venv\Scripts;D:\Program Files\esp\tools\tools\cmake\4.0.3\bin;D:\Program Files\esp\tools\tools\ninja\1.12.1;D:\Program Files\esp\tools\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin;D:\Program Files\esp\v6.0.1\esp-idf\tools;%PATH%"
cd /d "%~dp0"

echo [2/4] Clean build...
if exist build rmdir /s /q build
echo [3/4] Set target + Build...
C:\Users\Vodoo\.espressif\python_env\idf6.0_py3.12_env\venv\Scripts\python.exe "D:\Program Files\esp\v6.0.1\esp-idf\tools\idf.py" set-target esp32c3
C:\Users\Vodoo\.espressif\python_env\idf6.0_py3.12_env\venv\Scripts\python.exe "D:\Program Files\esp\v6.0.1\esp-idf\tools\idf.py" build
if !errorlevel! neq 0 (
    echo BUILD FAILED
    pause
    exit /b 1
)
echo   Build OK

echo [4/5] Flashing to COM16...
C:\Users\Vodoo\.espressif\python_env\idf6.0_py3.12_env\venv\Scripts\python.exe "D:\Program Files\esp\v6.0.1\esp-idf\tools\idf.py" -p COM16 flash
if !errorlevel! neq 0 (
    echo FLASH FAILED
    pause
    exit /b 1
)
echo   Flash OK

echo [5/5] Merging 0x0 full firmware...
set RELEASE_DIR=..\..\release\firmware
if not exist "%RELEASE_DIR%" mkdir "%RELEASE_DIR%"
set MERGE_OUT=%RELEASE_DIR%\3dai_led_full.bin
python -m esptool --chip esp32c3 merge-bin --flash-mode dio --flash-size 4MB --flash-freq 80m -o "%MERGE_OUT%" 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\Vibe_Coding_LED_state.bin
if !errorlevel! neq 0 (
    echo MERGE FAILED
    pause
    exit /b 1
)

echo ==============================================
echo  DONE: %MERGE_OUT%
echo ==============================================
pause
