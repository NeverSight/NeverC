@echo off
REM Local development helper: add or remove build-neverc\bin on PATH so the
REM in-tree neverc binary is available in the current shell. Not for production
REM installs.
REM
REM Usage (cmd):
REM   tools\neverc-env.bat                Add to PATH (current session)
REM   tools\neverc-env.bat --remove       Remove from PATH (current session)
REM   tools\neverc-env.bat --global       Persist to user PATH via setx
REM   tools\neverc-env.bat --global -r    Remove from user PATH via setx

set "REPO_ROOT=%~dp0.."
set "NEVERC_BIN=%REPO_ROOT%\build-neverc\bin"

if "%~1"=="-h"       goto :show_help
if "%~1"=="--help"   goto :show_help
if "%~1"=="--global" goto :parse_global
if "%~1"=="-r"       goto :do_remove
if "%~1"=="--remove" goto :do_remove
if "%~1"==""         goto :do_add

echo error: unknown option: %~1 >&2
echo        try: %~nx0 --help >&2
exit /b 1

:parse_global
if "%~2"=="-r"       goto :do_global_remove
if "%~2"=="--remove" goto :do_global_remove
if "%~2"==""         goto :do_global_add
echo error: unknown option after --global: %~2 >&2
exit /b 1

:show_help
echo Usage: %~nx0 [OPTION]
echo.
echo Local development helper for the in-tree neverc binary.
echo.
echo   (none)             Add build-neverc\bin to PATH (current session)
echo   -r, --remove       Remove build-neverc\bin from PATH (current session)
echo   --global           Persist to user PATH via setx (no admin required)
echo   --global -r        Remove from user PATH via setx
echo   -h, --help         Show this help
exit /b 0

:do_add
if not exist "%NEVERC_BIN%\neverc.exe" (
    echo warning: neverc.exe not found at %NEVERC_BIN%\neverc.exe >&2
    echo          build first, e.g. cmake --build build-neverc --target neverc >&2
)
echo %PATH% | findstr /i /c:"%NEVERC_BIN%" >nul 2>&1
if errorlevel 1 (
    set "PATH=%NEVERC_BIN%;%PATH%"
    echo added %NEVERC_BIN% to PATH (current session)
) else (
    echo note: %NEVERC_BIN% is already on PATH
)
exit /b 0

:do_remove
setlocal EnableDelayedExpansion
set "_new_path="
set "_found=0"
set "_remaining=%PATH%"

:remove_loop
if "!_remaining!"=="" goto :remove_done

for /f "tokens=1* delims=;" %%a in ("!_remaining!") do (
    set "_entry=%%a"
    set "_remaining=%%b"
)

if /i "!_entry!"=="%NEVERC_BIN%" (
    set "_found=1"
) else (
    if "!_new_path!"=="" (
        set "_new_path=!_entry!"
    ) else (
        set "_new_path=!_new_path!;!_entry!"
    )
)
goto :remove_loop

:remove_done
if !_found! equ 1 (
    endlocal & set "PATH=%_new_path%"
    echo removed %NEVERC_BIN% from PATH (current session)
) else (
    endlocal
    echo note: %NEVERC_BIN% is not on PATH >&2
)
exit /b 0

:do_global_add
setlocal EnableDelayedExpansion
set "_user_path="
for /f "tokens=2*" %%a in ('reg query "HKCU\Environment" /v PATH 2^>nul ^| findstr /i PATH') do set "_user_path=%%b"

echo !_user_path! | findstr /i /c:"%NEVERC_BIN%" >nul 2>&1
if not errorlevel 1 (
    echo note: %NEVERC_BIN% is already in user PATH
    endlocal
    exit /b 0
)

if "!_user_path!"=="" (
    setx PATH "%NEVERC_BIN%" >nul
) else (
    setx PATH "%NEVERC_BIN%;!_user_path!" >nul
)
endlocal
set "PATH=%NEVERC_BIN%;%PATH%"
echo persisted %NEVERC_BIN% to user PATH (also applied to current session)
echo restart cmd to take effect in new windows
exit /b 0

:do_global_remove
setlocal EnableDelayedExpansion
set "_user_path="
for /f "tokens=2*" %%a in ('reg query "HKCU\Environment" /v PATH 2^>nul ^| findstr /i PATH') do set "_user_path=%%b"

echo !_user_path! | findstr /i /c:"%NEVERC_BIN%" >nul 2>&1
if errorlevel 1 (
    echo note: %NEVERC_BIN% is not in user PATH >&2
    endlocal
    exit /b 0
)

set "_new_path="
set "_remaining=!_user_path!"

:global_remove_loop
if "!_remaining!"=="" goto :global_remove_done

for /f "tokens=1* delims=;" %%a in ("!_remaining!") do (
    set "_entry=%%a"
    set "_remaining=%%b"
)

if /i "!_entry!"=="%NEVERC_BIN%" goto :global_remove_loop

if "!_new_path!"=="" (
    set "_new_path=!_entry!"
) else (
    set "_new_path=!_new_path!;!_entry!"
)
goto :global_remove_loop

:global_remove_done
if "!_new_path!"=="" (
    reg delete "HKCU\Environment" /v PATH /f >nul 2>&1
) else (
    setx PATH "!_new_path!" >nul
)
endlocal
echo removed %NEVERC_BIN% from user PATH
echo restart cmd to take effect in new windows
exit /b 0
