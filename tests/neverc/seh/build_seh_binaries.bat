@echo off
setlocal EnableExtensions
rem ===========================================================================
rem Build the Windows SEH validation binaries (C-only subset) for Windows ARM64
rem and x64 using a locally built neverc, then drop the executables under
rem local_docs\windows_seh_tests\bin\{arm64,x64}\ for manual testing.
rem
rem Windows equivalent of build_seh_binaries.sh. NOT part of the gtest suite
rem (that is tests\neverc\SEHTests.cpp); this just emits the deliverable PEs.
rem
rem   - NESTED_COLLIDED (nestcol.c)     : nested-exception + collided-unwind
rem   - XCPT4 (xcpt4u/ex/pg.c, C-only)  : comprehensive local-frame SEH
rem   - XFRAME (xframe_eh_exe/dll.c)     : cross-module EH (EXE loads DLL)
rem
rem XCPT4's C++ throw/catch sub-tests (Test82/Test90, xcpt4cxx.cpp) are skipped:
rem neverc is a C-only compiler (see xcpt4ex.c's XCPT4_HAVE_CXX_EH guard).
rem
rem Override the compiler with:  set NEVERC=C:\path\to\neverc.exe
rem Override optimization with:  set OPT=-O0
rem ===========================================================================

rem Repo root = three levels up from this script (tests\neverc\seh).
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
set "SEH=%~dp0"
set "OUT=%ROOT%\local_docs\windows_seh_tests\bin"
if not defined NEVERC set "NEVERC=%ROOT%\build-neverc\bin\neverc.exe"
if not defined OPT set "OPT=-O2"
set "COMMON=-fno-lto %OPT% -w"

if not exist "%NEVERC%" (
  echo ERROR: neverc not found at "%NEVERC%"
  echo Set NEVERC to your neverc.exe, e.g.  set NEVERC=C:\src\NeverC\build\bin\neverc.exe
  exit /b 1
)

call :build_arch aarch64-pc-windows-msvc arm64
call :build_arch x86_64-pc-windows-msvc  x64

echo.
echo Output under: %OUT%
endlocal
exit /b 0

:build_arch
set "TRIPLE=%~1"
set "ARCH=%~2"
set "D=%OUT%\%ARCH%"
if not exist "%D%" mkdir "%D%"
echo === %ARCH% (%TRIPLE%) ===

"%NEVERC%" --target=%TRIPLE% %COMMON% "%SEH%nestcol.c" -o "%D%\nestcol.exe" 2>nul
if errorlevel 1 (echo   nestcol.exe        FAIL) else (echo   nestcol.exe        ok)

"%NEVERC%" --target=%TRIPLE% %COMMON% "%SEH%xcpt4u.c" "%SEH%xcpt4ex.c" "%SEH%xcpt4pg.c" -o "%D%\xcpt4.exe" 2>nul
if errorlevel 1 (echo   xcpt4.exe          FAIL) else (echo   xcpt4.exe          ok)

"%NEVERC%" --target=%TRIPLE% %COMMON% -shared "%SEH%xframe_eh_dll.c" -o "%D%\xframe_eh_dll.dll" 2>nul
if errorlevel 1 (echo   xframe_eh_dll.dll  FAIL) else (echo   xframe_eh_dll.dll  ok)

"%NEVERC%" --target=%TRIPLE% %COMMON% "%SEH%xframe_eh_exe.c" -o "%D%\xframe_eh_exe.exe" 2>nul
if errorlevel 1 (echo   xframe_eh_exe.exe  FAIL) else (echo   xframe_eh_exe.exe  ok)

goto :eof
