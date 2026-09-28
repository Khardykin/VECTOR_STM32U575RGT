@echo off
chcp 65001 >nul 2>&1
echo ============================================
echo  Запись sounds.bin во внешнюю flash
echo  (MX25R6435F через external loader)
echo ============================================
echo.

:: --- Настройки (поменяйте под себя, если нужно) ---
set "PROG=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
set "LOADER=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\ExternalLoader\ExtLoader_MX25R64.stldr"
set "IMAGE=tools\sounds.bin"
set "ADDR=0x00000000"

:: --- Проверки ---
if not exist "%PROG%" (
    echo [ОШИБКА] CubeProgrammer не найден:
    echo   %PROG%
    echo Проверьте путь установки.
    goto :fail
)
if not exist "%LOADER%" (
    echo [ОШИБКА] External loader не найден:
    echo   %LOADER%
    echo Скопируйте ExtLoader_MX25R64.stldr в ExternalLoader.
    goto :fail
)
if not exist "%IMAGE%" (
    echo [ОШИБКА] Файл образа не найден:
    echo   %IMAGE%
    echo Сначала соберите образ:
    echo   python tools\pack_sounds.py tools\b_click.wav tools\c_voice_gas.wav tools\d_myvoice.wav --rate 16000 --out tools\sounds.bin
    goto :fail
)

echo Файл образа : %IMAGE%
echo Адрес записи: %ADDR%
echo External LDR: %LOADER%
echo.

:: --- Запись ---
"%PROG%" -c port=SWD reset=HWrst ^
         -el "%LOADER%" ^
         -d "%IMAGE%" %ADDR% -v

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ============================================
    echo  ГОТОВО. Образ записан и верифицирован.
    echo ============================================
) else (
    echo.
    echo ============================================
    echo  ОШИБКА записи (код %ERRORLEVEL%).
    echo  Проверьте: ST-LINK подключен, плата запитана,
    echo  CubeIDE не держит сессию отладки.
    echo ============================================
    goto :fail
)
pause
exit /b 0

:fail
echo.
echo Нажмите любую клавишу для выхода...
pause >nul
exit /b 1