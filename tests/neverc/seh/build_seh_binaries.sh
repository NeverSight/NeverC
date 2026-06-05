#!/usr/bin/env bash
#
# Build the Windows SEH validation binaries (C-only subset) for Windows ARM64
# and x64 using a locally built neverc, then drop the executables under
# local_docs/windows_seh_tests/bin/{arm64,x64}/ for manual testing on a Windows
# box.
#
# These exercise the Windows structured-exception-handling / setjmp-longjmp
# stack-unwinding path that the .p2align/.xdata FunctionLength fix targets:
#   - NESTED_COLLIDED (nestcol.c)      : nested-exception + collided-unwind
#   - XCPT4 (xcpt4u/ex/pg.c, C-only)   : comprehensive local-frame SEH
#   - XFRAME (xframe_eh_exe/dll.c)      : cross-module EH (EXE loads DLL)
#
# XCPT4's C++ throw/catch sub-tests (Test82/Test90, xcpt4cxx.cpp) are skipped:
# neverc is a C-only compiler. See xcpt4ex.c's XCPT4_HAVE_CXX_EH guard.
#
# runtimeobject.lib is requested by the bundled CRT's /DEFAULTLIB directives but
# is absent from the bundled SDK; the link is unaffected (the symbols resolve
# elsewhere) and the resulting PE is valid. On a real Windows SDK it is present.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
NEVERC="${NEVERC:-$ROOT/build-neverc/bin/neverc}"
SEH="$ROOT/tests/neverc/seh"
OUT="$ROOT/local_docs/windows_seh_tests/bin"

OPT="${OPT:--O2}"
COMMON="-fno-lto $OPT -w"

build_arch() {
  local triple="$1" arch="$2"
  local d="$OUT/$arch"
  mkdir -p "$d"
  echo "=== $arch ($triple) ==="

  "$NEVERC" --target="$triple" $COMMON "$SEH/nestcol.c" -o "$d/nestcol.exe" \
    2>/dev/null && echo "  nestcol.exe        ok" || echo "  nestcol.exe        FAIL"

  "$NEVERC" --target="$triple" $COMMON \
    "$SEH/xcpt4u.c" "$SEH/xcpt4ex.c" "$SEH/xcpt4pg.c" -o "$d/xcpt4.exe" \
    2>/dev/null && echo "  xcpt4.exe          ok" || echo "  xcpt4.exe          FAIL"

  "$NEVERC" --target="$triple" $COMMON -shared \
    "$SEH/xframe_eh_dll.c" -o "$d/xframe_eh_dll.dll" \
    2>/dev/null && echo "  xframe_eh_dll.dll  ok" || echo "  xframe_eh_dll.dll  FAIL"

  "$NEVERC" --target="$triple" $COMMON \
    "$SEH/xframe_eh_exe.c" -o "$d/xframe_eh_exe.exe" \
    2>/dev/null && echo "  xframe_eh_exe.exe  ok" || echo "  xframe_eh_exe.exe  FAIL"
}

build_arch "aarch64-pc-windows-msvc" "arm64"
build_arch "x86_64-pc-windows-msvc"  "x64"

echo
echo "Output under: $OUT"
