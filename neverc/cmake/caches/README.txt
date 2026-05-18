CMake Caches
============

This directory contains CMake cache scripts that pre-populate the CMakeCache
in a build directory with commonly used settings.

Usage:

  cmake -G <build system> -C <path to cache file> [options] <path to llvm>

Options on the command line override options in the cache file.

NeverC
------

The NeverC cache configures the unified NeverC compiler + embedded linker
(single `neverc` executable; lld lives inside under `neverc/lib/Linker`):

  cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake

Then build with:
  cmake --build build-neverc --target neverc
Or: cd build-neverc && ninja neverc

The driver rejects C++ and non-C languages; linking is dispatched to the
embedded NeverC Linker automatically based on the inputs (`.o`/`.obj`/`.a`/
`.lib`/`.so`/`.dylib`/`.dll`).  The linker flavor (ELF / Mach-O / COFF)
is selected by the driver based on the target triple — there are no
standalone linker entry points or symlinks.

NeverC still supports cross-compiling C to Apple Darwin targets (including
iOS-family triples) and to Android (NDK-style aarch64-linux-android* triples);
you supply -isysroot / NDK paths as usual.

The cache enables AArch64 and X86 codegen only (no other targets).
llvm-objcopy is not built; use the system object-copy tool when needed.
