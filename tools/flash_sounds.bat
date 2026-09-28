@echo off
chcp 65001 >nul 2>&1
echo.
echo ============================================
echo   VECTOR: запись sounds.bin во внешнюю flash
echo ============================================
echo.

set "PROG=STM32_Programmer_CLI.exe"
set "LOADER=ExtLoader_MX25R64.stldr"
set "IMAGE=tools\sounds.bin"
set "ADDR=0x00000000"

if not exist "%IMAGE%" (
    if exist "tools\sounds.img" (
        copy /Y "tools\sounds.img" "%IMAGE%" >nul
    )
)

if not exist "%IMAGE%" (
    echo [ОШИБКА] Файл образа не найден: %IMAGE%
    goto :end
)

echo Файл  : %IMAGE%
echo Адрес : %ADDR%
echo.

"%PROG%" -c port=SWD reset=HWrst -el "%LOADER%" -d "%IMAGE%" %ADDR% -v
set "RC=%ERRORLEVEL%"

echo.
echo ============================================
if %RC% EQU 0 (
    echo   УСПЕХ: образ записан и верифицирован.
) else (
    echo   ОШИБКА записи (код %RC%).
)
echo ============================================

:end
echo.
echo Нажмите любую клавишу для выхода...
pause >nul