@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul
set "PYTHONIOENCODING=utf-8"

rem ===================================================================
rem  Запускать ИЗ ПАПКИ tools (или двойным кликом - путь подставляется).
rem  Прошивка звука по Modbus + тест воспроизведения по окончании.
rem
rem  Без аргументов: интерактивный выбор COM-порта из списка доступных.
rem  С аргументами:  flash_and_test.bat COM4 [SLAVE] [IMAGE] [CHUNK] [/I] [/T] [/Q]
rem    SLAVE  0
rem    IMAGE  sounds.bin        (в этой же папке)
rem    CHUNK  255
rem    /I     интерактив: гейт block_sound + кнопка, с прослушиванием
rem    /T     только тест, запись пропустить
rem    /Q     без вопросов и без pause (автоматизация)
rem ===================================================================

cd /d "%~dp0"

set "PORT="
set "SLAVE=0"
set "IMAGE=sounds.bin"
set "CHUNK=255"
set "INTERACTIVE=0"
set "TESTONLY=0"
set "QUIET=0"
set "POS=0"

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="/I" ( set "INTERACTIVE=1" & shift & goto parse )
if /I "%~1"=="/T" ( set "TESTONLY=1"    & shift & goto parse )
if /I "%~1"=="/Q" ( set "QUIET=1"       & shift & goto parse )
set /a POS+=1
if %POS%==1 set "PORT=%~1"
if %POS%==2 set "SLAVE=%~1"
if %POS%==3 set "IMAGE=%~1"
if %POS%==4 set "CHUNK=%~1"
shift
goto parse
:parsed

rem --- Интерактивный выбор порта, если не задан --------------------------
if not defined PORT (
  echo.
  echo   Доступные COM-порты:
  for /f "tokens=*" %%p in ('powershell -NoProfile -Command "[System.IO.Ports.SerialPort]::getportnames()" 2^>nul') do (
    echo     %%p
  )
  echo.
  set /p "PORT=  Введите номер порта (например COM4): "
  if not defined PORT (
    echo   Порт не указан. Выход.
    pause
    exit /b 2
  )
)

if not exist "logs" mkdir "logs" >nul 2>&1
set "TS=run"
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss" 2^>nul`) do set "TS=%%i"
set "LOG=logs\flash_test_%TS%.log"

call :say "=================================================================="
call :say " ПРОШИВКА ЗВУКА + ТЕСТ   cwd=%CD%"
call :say " порт=%PORT%  адрес=%SLAVE%  чанк=%CHUNK%"
call :say " образ=%IMAGE%  только_тест=%TESTONLY%  интерактив=%INTERACTIVE%"
call :say " лог=%LOG%"
call :say "=================================================================="

rem --- 0. зависимости и файлы ----------------------------------------
py -c "import serial" >nul 2>&1
if errorlevel 1 (
  call :say "ОШИБКА: нет pyserial. Установи: py -m pip install pyserial"
  goto fail
)

set "MISSING="
for %%f in (pack_sounds.py flash_sound.py test_sound_test.py) do (
  if not exist "%%f" set "MISSING=!MISSING! %%f"
)
if defined MISSING (
  call :say "ОШИБКА: нет скриптов в %CD%:!MISSING!"
  goto fail
)

rem --- 1. порт свободен ----------------------------------------------
call :say "ШАГ 1/4  проверка порта %PORT%"
py -c "import serial; s=serial.Serial('%PORT%',115200,timeout=0.3); s.close()" >nul 2>>"%LOG%"
if errorlevel 1 (
  call :say "ОШИБКА: порт %PORT% занят или отсутствует."
  call :say "        Закрой терминал CubeIDE / PuTTY / второй py-процесс."
  goto fail
)
call :say "          порт свободен"

rem --- 2. образ валиден ----------------------------------------------
call :say "ШАГ 2/4  проверка образа %IMAGE%"
if not exist "%IMAGE%" (
  call :say "ОШИБКА: образа нет: %CD%\%IMAGE%"
  call :say "        Собери: py pack_sounds.py *.wav --out %IMAGE%"
  goto fail
)
py pack_sounds.py "%IMAGE%" --check
if errorlevel 1 (
  call :say "ОШИБКА: образ битый (magic / version / CRC таблицы)."
  goto fail
)

if "%TESTONLY%"=="1" (
  call :say "ШАГ 3/4  запись пропущена (/T)"
  goto run_test
)

rem --- 3. запись во flash --------------------------------------------
call :say "ШАГ 3/4  запись во внешнюю flash"
call :say "          стирание ~70 с + запись ~10 мин при 5 КБ/с"
call :say "          ВНИМАНИЕ: сессия отладки закрыта, breakpoint в"
call :say "          load_image() / плеере СНЯТ - останов ядра рвёт Modbus."
if "%QUIET%"=="0" (
  choice /C YN /M "          Продолжить запись"
  if errorlevel 2 goto cancel
)
py flash_sound.py %PORT% --slave %SLAVE% --file "%IMAGE%" --chunk %CHUNK%
if errorlevel 1 (
  call :say "ОШИБКА: запись не завершена. Тест НЕ запускаю."
  goto fail
)
call :say "          запись завершена, жду 2 с на reload образа"
timeout /t 2 /nobreak >nul

:run_test
rem --- 4. тест воспроизведения ---------------------------------------
call :say "ШАГ 4/4  тест воспроизведения"
set "EXTRA=--all"
if "%INTERACTIVE%"=="1" set "EXTRA=--all --interactive"
py test_sound_test.py %PORT% --slave %SLAVE% %EXTRA%
if errorlevel 1 (
  call :say "РЕЗУЛЬТАТ: есть FAIL - смотри строки выше"
  goto fail
)

call :say "=================================================================="
call :say " ГОТОВО: образ записан, тест PASS"
call :say " лог=%LOG%"
call :say "=================================================================="
if "%QUIET%"=="0" pause
endlocal & exit /b 0

:cancel
call :say "Отменено пользователем."
if "%QUIET%"=="0" pause
endlocal & exit /b 2

:fail
call :say "=================================================================="
call :say " ПРЕРВАНО. Подробный лог: %LOG%"
call :say "=================================================================="
if "%QUIET%"=="0" pause
endlocal & exit /b 1

:say
echo %~1
>>"%LOG%" echo [%TIME%] %~1
exit /b 0