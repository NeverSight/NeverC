# Bundled MSVC / Windows SDK (shared headers)

| `crt/include` | **Full MSVC tree kept** (C++ STL: `iostream`, `vector`, `libcpmt` headers, …) |
| `sdk/include/{ucrt,shared,um}` | Trimmed to the **transitive closure** of common desktop entry points (`windows.h`, CRT, `winsock2.h`, …) |

## Layout

| Path | Role |
|------|------|
| `crt/include` | VC runtime + **MSVC C++ standard library** (not pruned) |
| `sdk/include/ucrt` | Universal C runtime headers (pruned) |
| `sdk/include/shared` | SDK headers shared by um/km (pruned) |
| `sdk/include/um` | User-mode Windows API (pruned) |

Import libraries live under `runtime/windows/x64/msvc/sdk/lib/` (`um`, `ucrt`).

Kernel drivers use WDK (`runtime/windows/shared/wdk`, `runtime/windows/x64/wdk/lib`)
with `-fms-kernel`, not this tree.

NeverC injects these paths for Windows user-mode targets (`MSVC.cpp`).
