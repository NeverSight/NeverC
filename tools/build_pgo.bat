@echo off
setlocal EnableDelayedExpansion
REM
REM PGO (Profile-Guided Optimisation) two-phase build for NeverC (Windows).
REM
REM Usage:
REM   tools\build_pgo.bat                 &  full pipeline: generate - train - use - benchmark
REM   tools\build_pgo.bat generate        &  Phase 1: build instrumented neverc
REM   tools\build_pgo.bat train           &  Phase 1b: run workloads to collect profiles
REM   tools\build_pgo.bat use             &  Phase 2: build optimised neverc from .profdata
REM   tools\build_pgo.bat benchmark       &  A/B compare baseline vs PGO build
REM   tools\build_pgo.bat clean           &  remove PGO build dirs and profiles
REM
REM Requires: cmake, ninja, llvm-profdata (from LLVM/Clang install)

set "REPO_ROOT=%~dp0.."
set "CACHE=%REPO_ROOT%\neverc\cmake\caches\NeverC.cmake"

set "BUILD_PGO_GEN=%REPO_ROOT%\build-pgo-gen"
set "BUILD_PGO_USE=%REPO_ROOT%\build-pgo-use"
set "BUILD_BASELINE=%REPO_ROOT%\build-neverc"
set "PROFILE_DIR=%REPO_ROOT%\pgo-profiles"
set "PROFDATA=%PROFILE_DIR%\merged.profdata"

set "SQLITE_DIR=%REPO_ROOT%\local_docs\sqlite"
set "REDIS_DIR=%REPO_ROOT%\local_docs\redis"
set "ZLIB_DIR=%REPO_ROOT%\local_docs\zlib"
set "CURL_DIR=%REPO_ROOT%\local_docs\curl"

if not defined JOBS set "JOBS=%NUMBER_OF_PROCESSORS%"

REM Locate llvm-profdata
set "LLVM_PROFDATA="
where llvm-profdata >nul 2>&1 && (
    for /f "delims=" %%i in ('where llvm-profdata') do set "LLVM_PROFDATA=%%i"
)
if not defined LLVM_PROFDATA if defined LLVM_ROOT (
    if exist "%LLVM_ROOT%\bin\llvm-profdata.exe" set "LLVM_PROFDATA=%LLVM_ROOT%\bin\llvm-profdata.exe"
)
if not defined LLVM_PROFDATA (
    echo ERROR: llvm-profdata not found. Add LLVM bin directory to PATH or set LLVM_ROOT.
    exit /b 1
)

set "CMD=%~1"
if "%CMD%"=="" set "CMD=all"

if "%CMD%"=="generate"  goto :phase_generate
if "%CMD%"=="train"     goto :phase_train
if "%CMD%"=="use"       goto :phase_use
if "%CMD%"=="benchmark" goto :phase_benchmark
if "%CMD%"=="clean"     goto :phase_clean
if "%CMD%"=="all"       goto :phase_all

echo Usage: %~nx0 {all^|generate^|train^|use^|benchmark^|clean}
exit /b 1

:phase_all
call :phase_generate || exit /b 1
call :phase_train    || exit /b 1
call :phase_use      || exit /b 1
call :phase_benchmark
goto :eof

REM ════════════════════════════════════════════════════════════════
:phase_generate
echo ==============================================================
echo   Phase 1: Building instrumented neverc (PGO generate)
echo ==============================================================

cmake -S "%REPO_ROOT%\llvm" -B "%BUILD_PGO_GEN%" -G Ninja ^
    -C "%CACHE%" ^
    -DNEVERC_ENABLE_LTO=OFF ^
    -DNEVERC_ENABLE_MIMALLOC=OFF ^
    -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -fprofile-instr-generate -DNEVERC_PGO_TRAINING -ffunction-sections -fdata-sections" ^
    -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG -fprofile-instr-generate -DNEVERC_PGO_TRAINING -ffunction-sections -fdata-sections" ^
    -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate"
if errorlevel 1 exit /b 1

cmake --build "%BUILD_PGO_GEN%" --target neverc --parallel %JOBS%
if errorlevel 1 exit /b 1

echo [OK] Instrumented neverc: %BUILD_PGO_GEN%\bin\neverc.exe
goto :eof

REM ════════════════════════════════════════════════════════════════
:phase_train
echo ==============================================================
echo   Phase 1b: Training -- collecting profiles
echo ==============================================================

set "NC=%BUILD_PGO_GEN%\bin\neverc.exe"
if not exist "%NC%" (
    echo ERROR: instrumented neverc not found at %NC%
    echo        Run '%~nx0 generate' first.
    exit /b 1
)

if exist "%PROFILE_DIR%" rd /s /q "%PROFILE_DIR%"
mkdir "%PROFILE_DIR%"

set "TRAIN_COUNT=0"

REM SQLite
if exist "%SQLITE_DIR%\sqlite3.c" (
    echo   - SQLite ^(all modes^)...
    call :train_one "%SQLITE_DIR%\sqlite3.c" sqlite preproc
    call :train_one "%SQLITE_DIR%\sqlite3.c" sqlite sema
    call :train_one "%SQLITE_DIR%\sqlite3.c" sqlite irgen
    call :train_one "%SQLITE_DIR%\sqlite3.c" sqlite O0
    call :train_one "%SQLITE_DIR%\sqlite3.c" sqlite O2
    call :train_one "%SQLITE_DIR%\sqlite3.c" sqlite O3
    if exist "%SQLITE_DIR%\shell.c" (
        call :train_one "%SQLITE_DIR%\shell.c" sqlite O2
    )
)

REM Redis
if exist "%REDIS_DIR%\src" (
    echo   - Redis ^(O2^)...
    for %%f in ("%REDIS_DIR%\src\*.c") do (
        call :train_one "%%f" redis O2
    )
)

REM zlib
if exist "%ZLIB_DIR%" (
    echo   - zlib...
    for %%f in ("%ZLIB_DIR%\*.c") do (
        call :train_one "%%f" zlib O2
    )
)

REM curl
if exist "%CURL_DIR%\lib" (
    echo   - curl lib ^(O2^)...
    for %%f in ("%CURL_DIR%\lib\*.c") do (
        set "LLVM_PROFILE_FILE=%PROFILE_DIR%\curl_%%~nf.profraw"
        "%NC%" -c -O2 -w -fno-neverc-types -I"%CURL_DIR%\include" -I"%CURL_DIR%\lib" -DBUILDING_LIBCURL -o NUL "%%f" 2>nul
        set /a TRAIN_COUNT+=1
    )
)

echo   Collected %TRAIN_COUNT% profraw files.

set "PROFRAW_COUNT=0"
for %%f in ("%PROFILE_DIR%\*.profraw") do set /a PROFRAW_COUNT+=1
if %PROFRAW_COUNT% equ 0 (
    echo ERROR: no .profraw files generated. Check workload paths.
    exit /b 1
)

echo   Merging %PROFRAW_COUNT% profiles...
where bash >nul 2>&1 && (
    bash "%REPO_ROOT%\tools\merge_pgo_profiles.sh" ^
        --llvm-profdata "%LLVM_PROFDATA%" ^
        --profile-dir "%PROFILE_DIR%" ^
        --output "%PROFDATA%"
) || (
    "%LLVM_PROFDATA%" merge -output="%PROFDATA%" "%PROFILE_DIR%\*.profraw"
)
if errorlevel 1 exit /b 1

echo [OK] Profile data: %PROFDATA%
goto :eof

:train_one
REM %1 = source file, %2 = label, %3 = mode
set "SRC=%~1"
set "LABEL=%~2"
set "MODE=%~3"
set "TAG=%LABEL%_%~n1_%MODE%"
set "LLVM_PROFILE_FILE=%PROFILE_DIR%\%TAG%.profraw"

if "%MODE%"=="preproc" "%NC%" -E  -w -fno-neverc-types -o NUL "%SRC%" 2>nul
if "%MODE%"=="sema"    "%NC%" -fsyntax-only -w -fno-neverc-types "%SRC%" 2>nul
if "%MODE%"=="irgen"   "%NC%" -emit-llvm -S -w -fno-neverc-types -o NUL "%SRC%" 2>nul
if "%MODE%"=="O0"      "%NC%" -c -O0 -w -fno-neverc-types -o NUL "%SRC%" 2>nul
if "%MODE%"=="O2"      "%NC%" -c -O2 -w -fno-neverc-types -o NUL "%SRC%" 2>nul
if "%MODE%"=="O3"      "%NC%" -c -O3 -w -fno-neverc-types -o NUL "%SRC%" 2>nul

set /a TRAIN_COUNT+=1
goto :eof

REM ════════════════════════════════════════════════════════════════
:phase_use
echo ==============================================================
echo   Phase 2: Building optimised neverc (PGO use)
echo ==============================================================

if not exist "%PROFDATA%" (
    echo ERROR: %PROFDATA% not found. Run '%~nx0 train' first.
    exit /b 1
)

cmake -S "%REPO_ROOT%\llvm" -B "%BUILD_PGO_USE%" -G Ninja ^
    -C "%CACHE%" ^
    -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -fprofile-instr-use=%PROFDATA% -ffunction-sections -fdata-sections" ^
    -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG -fprofile-instr-use=%PROFDATA% -ffunction-sections -fdata-sections" ^
    -DCMAKE_EXE_LINKER_FLAGS=""
if errorlevel 1 exit /b 1

cmake --build "%BUILD_PGO_USE%" --target neverc --parallel %JOBS%
if errorlevel 1 exit /b 1

echo [OK] PGO-optimised neverc: %BUILD_PGO_USE%\bin\neverc.exe

if exist "%BUILD_BASELINE%\bin\neverc.exe" (
    for %%A in ("%BUILD_BASELINE%\bin\neverc.exe") do set "BASELINE_SIZE=%%~zA"
    for %%A in ("%BUILD_PGO_USE%\bin\neverc.exe") do set "PGO_SIZE=%%~zA"
    echo   Binary size: baseline=!BASELINE_SIZE! bytes  PGO=!PGO_SIZE! bytes
)
goto :eof

REM ════════════════════════════════════════════════════════════════
:phase_benchmark
echo ==============================================================
echo   Benchmark: Baseline vs PGO
echo ==============================================================

if not exist "%SQLITE_DIR%\sqlite3.c" (
    echo   SKIP: SQLite workload not found at %SQLITE_DIR%\sqlite3.c
    goto :eof
)

set "NC_BASE=%BUILD_BASELINE%\bin\neverc.exe"
set "NC_PGO=%BUILD_PGO_USE%\bin\neverc.exe"
set "RUNS=5"

for %%B in ("Baseline:%NC_BASE%" "PGO:%NC_PGO%") do (
    for /f "tokens=1,2 delims=:" %%L in (%%B) do (
        set "LABEL=%%L"
        set "BIN=%%M"
    )
    if not exist "!BIN!" (
        echo   SKIP !LABEL!: !BIN! not found
    ) else (
        echo.
        echo   -- !LABEL! --
        set "TIMES="
        for /l %%i in (1,1,%RUNS%) do (
            set "T_START=!time!"
            "!BIN!" -c -O3 -w -fno-neverc-types -o NUL "%SQLITE_DIR%\sqlite3.c" 2>nul
            set "T_END=!time!"
            REM Rough timing via %time% (HH:MM:SS.CC)
            call :time_diff "!T_START!" "!T_END!" ELAPSED_MS
            set "TIMES=!TIMES! !ELAPSED_MS!ms"
        )
        echo     SQLite -O3: !TIMES!
    )
)
echo.
goto :eof

:time_diff
REM Approximate millisecond diff between two %time% values
set "T1=%~1"
set "T2=%~2"
for /f "tokens=1-4 delims=:." %%a in ("%T1%") do (
    set /a "S1=(((%%a*60)+%%b)*60+%%c)*100+%%d"
)
for /f "tokens=1-4 delims=:." %%a in ("%T2%") do (
    set /a "S2=(((%%a*60)+%%b)*60+%%c)*100+%%d"
)
set /a "DIFF_CS=S2-S1"
if !DIFF_CS! lss 0 set /a "DIFF_CS+=8640000"
set /a "DIFF_MS=DIFF_CS*10"
set "%~3=!DIFF_MS!"
goto :eof

REM ════════════════════════════════════════════════════════════════
:phase_clean
echo Cleaning PGO build artifacts...
if exist "%BUILD_PGO_GEN%" rd /s /q "%BUILD_PGO_GEN%"
if exist "%BUILD_PGO_USE%" rd /s /q "%BUILD_PGO_USE%"
if exist "%PROFILE_DIR%"   rd /s /q "%PROFILE_DIR%"
echo Done.
goto :eof
