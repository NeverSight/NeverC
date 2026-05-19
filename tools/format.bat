@echo off
setlocal EnableDelayedExpansion
REM Format all .cpp / .h under this repo using .clang-format files.
REM Excludes: .git\, build-neverc\
REM
REM Usage:
REM   tools\format.bat              Format in place
REM   tools\format.bat --check      Fail if any file would change (CI)

set "REPO_ROOT=%~dp0.."
cd /d "%REPO_ROOT%"

where clang-format >nul 2>&1
if errorlevel 1 (
    echo error: clang-format not found in PATH
    exit /b 1
)

set "CHECK=0"
set "FMT_ARGS=-i"
if "%~1"=="--check" (
    set "CHECK=1"
    set "FMT_ARGS=--dry-run --Werror"
)
if "%~1"=="-n" (
    set "CHECK=1"
    set "FMT_ARGS=--dry-run --Werror"
)
if "%~1"=="--help" (
    echo Format all .cpp / .h under this repo using .clang-format files.
    echo Usage: %~nx0 [--check^|-n]
    exit /b 0
)
if "%~1"=="-h" (
    echo Format all .cpp / .h under this repo using .clang-format files.
    echo Usage: %~nx0 [--check^|-n]
    exit /b 0
)

set "FILE_COUNT=0"
set "FAIL_COUNT=0"

for /r %%f in (*.cpp *.h) do (
    set "FPATH=%%f"
    echo !FPATH! | findstr /i /c:".git\" /c:"build-neverc\" >nul 2>&1
    if errorlevel 1 (
        clang-format %FMT_ARGS% "%%f"
        if errorlevel 1 set /a FAIL_COUNT+=1
        set /a FILE_COUNT+=1
    )
)

if %CHECK% equ 1 (
    if %FAIL_COUNT% gtr 0 (
        echo %FAIL_COUNT% file(s) need formatting.
        exit /b 1
    )
    echo All %FILE_COUNT% files are correctly formatted.
) else (
    echo Formatted %FILE_COUNT% files.
)
