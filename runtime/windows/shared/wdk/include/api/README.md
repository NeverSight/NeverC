# WDK `api/` headers (kernel driver subset)

Trimmed for **kernel-mode drivers** (`-fms-kernel`, see `examples/windows-driver/`).

## What is kept (~30 headers)

| Layer | Headers |
|-------|---------|
| **Minimal core** | Transitive `#include` closure from `ddk/ntddk.h` (types, SAL, ETW stubs, pack macros, …) |
| **Extended common** | Often included directly: `devioctl.h`, `devpkey.h`, `initguid.h`, `basetyps.h`, `ifdef.h`, `winioctl.h`, and `ipifcons.h` (via `winioctl.h`) |

## What was removed

User-mode Win32 (COMMCTRL, GDI+, SETUPAPI, …), D3D, DirectMusic, HID miniport-only stacks, and other headers not reachable from the sets above.

NeverC adds `include/ddk` then `include/api` on the include path (`neverc/lib/Invoke/ToolChains/MSVC.cpp`). Libc headers (`string.h`, `excpt.h`, …) come from the compiler, not this tree.
