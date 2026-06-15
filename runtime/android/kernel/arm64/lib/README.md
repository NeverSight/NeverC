# `arm64/lib` — (vestigial) optional compiler-rt builtins

You almost certainly do **not** need anything here.

`neverc -fandroid-kernel-driver-mode` is self-contained: it lowers wide
(`__int128`) integer div/rem inline (`-expand-div-rem-bits=64`), disables
outline atomics, and otherwise emits only code the kernel already provides
(`memcpy`/`memset`/... resolve to the kernel's own exports). The demos build
with **zero** compiler-rt symbols, so no `libclang_rt.builtins-aarch64.a` is
ever pulled in.

This directory is kept only as a manual escape hatch for exotic modules that
deliberately use constructs the kernel can't satisfy (e.g. software floating
point — which has no place in a kernel module anyway). If you ever truly need
one, drop `libclang_rt.builtins-aarch64.a` here and link it explicitly with
`-L<this dir> -lclang_rt.builtins-aarch64`.
