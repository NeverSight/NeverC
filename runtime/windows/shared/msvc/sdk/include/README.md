# Windows SDK headers (`sdk/include`, C only)

Trimmed for **NeverC pure C** user-mode Windows targets.

| Directory | Files (approx.) | Contents |
|-----------|-----------------|----------|
| **ucrt/** | ~39 | C standard library (`stdio.h`, `stdlib.h`, `string.h`, …) |
| **shared/** | ~49 | Types/macros shared by Windows headers (`winapifamily.h`, …) |
| **um/** | ~115 | Win32 C API reached from `windows.h`, `winsock2.h`, console APIs |

## Not included

- MSVC C++ STL (`iostream`, …) — see `crt/include` policy (C-only, no STL)
- **DirectX / DXGI:** `d3d9`–`d3d12`, `dxgi`, `d3dcompiler`, … under `shared/` and `um/` (~80 headers) with matching `.lib` in `x64/msvc/sdk/lib/um/`
- Other COM/UI extras not in the default C closure unless restored from git
- Kernel/WDK headers — use `runtime/windows/shared/wdk`

## Seeds used for pruning

`stdio.h`, `stdlib.h`, `string.h`, `windows.h`, `winsock2.h`, `ws2tcpip.h`, **`shellapi.h`**, **`setupapi.h`**, and their `#include` closure.

Link with `-lsetupapi` / use shell APIs as needed (`SetupAPI.Lib` is in `sdk/lib/um/`).
