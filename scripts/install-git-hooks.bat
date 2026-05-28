@echo off
REM Install local git hooks for this repo on Windows.
REM
REM Usage:
REM     scripts\install-git-hooks.bat
REM
REM Installs a pre-commit hook that checks Options.td.h ordering whenever
REM the file is staged. Skip with `git commit --no-verify`.

setlocal enabledelayedexpansion

for /f "delims=" %%i in ('git rev-parse --show-toplevel 2^>nul') do set REPO_ROOT=%%i
if "%REPO_ROOT%"=="" (
    echo ERROR: not inside a git repository.
    exit /b 1
)

set HOOK_PATH=%REPO_ROOT%\.git\hooks\pre-commit

REM Git on Windows runs hooks via bash (Git Bash / MSYS), so the hook itself
REM is still a shell script.  We just write the same content as the Unix
REM installer.
> "%HOOK_PATH%" (
    echo #!/usr/bin/env bash
    echo set -e
    echo.
    echo REPO_ROOT="$(git rev-parse --show-toplevel^)"
    echo TARGET="neverc/include/neverc/Invoke/Options.td.h"
    echo.
    echo if git diff --cached --name-only ^| grep -qx "$TARGET"; then
    echo     echo "[pre-commit] Checking $TARGET is sorted..."
    echo     if ! python "$REPO_ROOT/scripts/sort-options-td.py" --check; then
    echo         echo ""
    echo         echo "[pre-commit] Options.td.h is not sorted. Run:"
    echo         echo "    python scripts/sort-options-td.py --write"
    echo         echo "and 'git add' the result, or use 'git commit --no-verify' to skip."
    echo         exit 1
    echo     fi
    echo fi
)

echo Installed pre-commit hook at %HOOK_PATH%
endlocal
