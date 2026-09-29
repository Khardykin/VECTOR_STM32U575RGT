@echo off
chcp 65001 >nul
echo.
echo ============================================
echo   VECTOR: запись sounds.bin во внешнюю flash
echo ============================================
echo.

set "PROG=%STM32_PRG_PATH%\STM32_Programmer_CLI.exe"
set "LOADER=%STM32_PRG_PATH%\ExternalLoader\ExtLoader_MX25R64.stldr"
set "IMAGE=sounds.bin"
set "ADDR=0x00000000"

if not exist "%PROG%" (
    echo [ОШИБКА] STM32_Programmer_CLI.exe не найден:
    echo   %PROG%
    echo Проверьте переменную STM32_PRG_PATH.
    goto :end
)
if not exist "%LOADER%" (
    echo [ОШИБКА] External loader не найден:
    echo   %LOADER%
    goto :end
)
if not exist "%IMAGE%" (
    echo [ОШИБКА] Файл не найден: %IMAGE%
    echo Положите sounds.bin рядом с этим bat-файлом.
    goto :end
)

echo Файл   : %IMAGE%
echo Адрес  : %ADDR%
echo.
echo --- Запись... ---
echo.

"%PROG%" -c port=SWD reset=HWrst -el "%LOADER%" -d "%IMAGE%" %ADDR% -v

echo.
echo ============================================
echo   ЗАПИСЬ ЗАВЕРШЕНА
echo ============================================
echo.

:end
pause