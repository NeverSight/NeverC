# MSVC CRT import libraries (`x64`, C only)

NeverC does **not** bundle the Microsoft C++ standard library.

## Kept (typical C program)

| Library | When |
|---------|------|
| `libcmt.lib` | `/MT` — static C runtime (default static link) |
| `msvcrt.lib` | `/MD` — C runtime via DLL |
| `libvcruntime.lib` / `vcruntime.lib` | Exception handling / startup helpers |
| `legacy_stdio_definitions.lib`, `oldnames.lib` | POSIX/legacy CRT symbols |
| `delayimp.lib` | Delay-load helper (if used) |

Plus `.obj` startup objects (`chkstk.obj`, `setargv.obj`, …) used when linking `libcmt`.

## Removed (C++)

`libcpmt`, `msvcprt`, `concrt`, `comsupp`, `vccorlib`, `vcamp`, OpenMP (`libomp`, `vcomp`), managed (`msvcmrt`), STLCLR, …

Use `-fms-compatibility` / `-MD` / `-MT` as documented for NeverC; drivers use `-nostdlib` and WDK instead.
