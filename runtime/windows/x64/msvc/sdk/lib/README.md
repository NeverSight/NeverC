# MSVC / Windows SDK import libraries (`x64`)

## `ucrt/` (keep all)

| Library | Notes |
|---------|--------|
| `ucrt.lib` / `libucrt.lib` | Universal CRT (release / static) |

Linked by default with `-fms-compatibility` (`--defaultlib=ucrt` or `libucrt`).

## `um/` (common desktop subset)

Roughly three dozen import libraries for typical Win32 apps, for example:

`kernel32`, `user32`, `gdi32`, `advapi32`, `shell32`, `ole32`, `oleaut32`,
`uuid`, `comctl32`, `comdlg32`, `ws2_32`, `wsock32`, `wininet`, `iphlpapi`,
`crypt32`, `secur32`, `bcrypt`, `shlwapi`, `version`, `winmm`, `imm32`,
`uxtheme`, `dwmapi`, `winspool`, `psapi`, `ntdll`, `userenv`, `powrprof`,
`netapi32`, `mpr`, `gdiplus`, `opengl32`, `setupapi`, `wldap32`,
`BufferOverflow`, `BufferOverflowU`.

Specialized stacks (DirectX, WMI, debug engines, cluster, etc.) were removed.
Restore from git history if needed.

CRT startup is under `runtime/windows/x64/msvc/crt/lib/`:

| Library | Role |
|---------|------|
| `libcmt.lib` / `libcmtd` | Static **C** runtime (`/MT`) |
| `msvcrt.lib` | DLL **C** runtime (`/MD`) |
| `vcruntime.lib` / `libvcruntime.lib` | EH / helpers (linked with C) |
| `legacy_stdio_definitions.lib`, `oldnames.lib` | CRT compatibility |

**No C++ STL:** `libcpmt`, `msvcprt`, `concrt`, etc. were removed (NeverC is C-only).
