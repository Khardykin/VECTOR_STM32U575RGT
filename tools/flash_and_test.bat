@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul
set "PYTHONIOENCODING=utf-8"

rem ===================================================================
rem  Запускать ИЗ ПАПКИ tools (или двойным кликом - путь подставляется).
rem  Прошивка звука по Modbus + тест воспроизведения по окончании.
rem
rem  tools> flash_and_test.bat [PORT] [SLAVE] [IMAGE] [CHUNK] [/I] [/T] [/Q]
rem    PORT   COM4
rem    SLAVE  100
rem    IMAGE  sounds.bin        (в этой же папке)
rem    CHUNK  255
rem    /I     интерактив: гейт block_sound + кнопка, с прослушиванием
rem    /T     только тест, запись пропустить
rem    /Q     без вопросов и без pause (автоматизация)
rem ===================================================================

cd /d "%~dp0"

set "PORT=COM4"
set "SLAVE=100"
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

if not exist "logs" mkdir "logs" >nul 2>&1
set "TS=run"
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_Hmmmss" 2^>nul`) do set "TS=%%i"
set "LOG=logs\flash_test_%TS%.log"

call :say "=================================================================="
call :say " FLASH + TEST   cwd=%CD%"
call :say " port=%PORT%  slave=%SLAVE%  chunk=%CHUNK%"
call :say " image=%IMAGE%  testonly=%TESTONLY%  interactive=%INTERACTIVE%"
call :say " log=%LOG%"
call :say "=================================================================="

rem --- 0. зависимости и файлы ----------------------------------------
py -c "import serial" >nul 2>&1
if errorlevel 1 (
  call :say "ОШИБКА: нет pyserial.  py -m pip install pyserial"
  goto fail
)

set "MISSING="
for %%f in (pack_sounds.py test_flash_write_final.py test_sound_test.py who_is_beeping.py) do (
  if not exist "%%f" set "MISSING=!MISSING! %%f"
)
if defined MISSING (
  call :say "ОШИБКА: нет скриптов в %CD%:!MISSING!"
  goto fail
)

rem --- 1. порт свободен ----------------------------------------------
call :say "STEP 1/5  проверка порта %PORT%"
py -c "import serial; s=serial.Serial('%PORT%',115200,timeout=0.3); s.close()" >nul 2>>"%LOG%"
if errorlevel 1 (
  call :say "ОШИБКА: порт %PORT% занят или отсутствует."
  call :say "        Закрой терминал CubeIDE / PuTTY / второй py-процесс."
  goto fail
)
call :say "          порт свободен"

rem --- 2. образ валиден ----------------------------------------------
call :say "STEP 2/5  проверка образа %IMAGE%"
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
  call :say "STEP 3/5  запись пропущена (/T)"
  goto bg
)

rem --- 3. запись во flash --------------------------------------------
call :say "STEP 3/5  запись во flash"
call :say "          erase ~70 s + write ~10 min при 5 КБ/с"
call :say "          ВНИМАНИЕ: сессия отладки закрыта, breakpoint в"
call :say "          load_image() / плеере СНЯТ - останов ядра рвёт Modbus."
if "%QUIET%"=="0" (
  choice /C YN /M "          Продолжить запись"
  if errorlevel 2 goto cancel
)
py test_flash_write_final.py %PORT% --slave %SLAVE% --file "%IMAGE%" --chunk %CHUNK%
if errorlevel 1 (
  call :say "ОШИБКА: запись не завершена. Тест НЕ запускаю."
  goto fail
)
call :say "          запись завершена, жду 2 с на reload образа"
timeout /t 2 /nobreak >nul

rem --- 4. фоновые статусы --------------------------------------------
:bg
call :say "STEP 4/5  фоновые статусы (кто пищит, когда тест освободит канал)"
py who_is_beeping.py %PORT% --slave %SLAVE%

rem --- 5. тест воспроизведения ---------------------------------------
call :say "STEP 5/5  тест воспроизведения"
set "EXTRA="
if "%INTERACTIVE%"=="1" set "EXTRA=--interactive"
py test_sound_test.py %PORT% --slave %SLAVE% %EXTRA%
if errorlevel 1 (
  call :say "РЕЗУЛЬТАТ: есть FAIL - смотри строки выше"
  goto fail
)

call :say "=================================================================="
call :say " ГОТОВО: образ записан, тест PASS"
call :say " log=%LOG%"
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