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

Import libraries live under `runtime/windows/<arch>/msvc/sdk/lib/` (`um`, `ucrt`).

VBS enclave targets use the same layout for both supported architectures:

| Path | Role |
|------|------|
| `windows/<arch>/msvc/crt/lib/enclave/` | Enclave `libcmt.lib` and `libvcruntime.lib` |
| `windows/<arch>/msvc/sdk/lib/ucrt_enclave/ucrt.lib` | Enclave UCRT import library |
| `windows/<arch>/msvc/sdk/lib/um/vertdll.lib` | Vertdll enclave import library |
| `windows/<arch>/msvc/sdk/lib/um/bcrypt.lib` | Cryptography import library required by the enclave link contract |

The SDK import libraries above come from Microsoft's architecture-specific
`Microsoft.Windows.SDK.CPP` 10.0.26100.1 NuGet packages, matching the bundled
user-mode SDK baseline.

Any explicit `-vctoolsdir` or `-winsysroot` selection keeps its normal
precedence. Without those overrides, every `/ENCLAVE` link on macOS, Linux, or
Windows searches for Windows libraries only in the matching bundled target
runtime; it does not auto-detect or fall back to a Visual Studio toolset or
Windows SDK installed on the host.

When bundled resolution is active, only explicit `/ENCLAVE` combined with global
`/NODEFAULTLIB` switches from the ordinary bundled CRT/UCRT directories to the
enclave CRT/UCRT directories. In that mode, the driver validates the bundled
`libcmt.lib`, `libvcruntime.lib`, `ucrt.lib`, `vertdll.lib`, and `bcrypt.lib`
before linking; callers still select those libraries explicitly. `/ENCLAVE`
alone does not enable or select the enclave CRT/UCRT and continues to use the
bundled ordinary Windows runtime search paths.

The bundle makes compilation and COFF linking host-independent; it does not
make the Windows packaging and execution tools portable. VEIID processing,
SignTool signing, and actual enclave loading remain Windows-only.

Kernel drivers use WDK (`runtime/windows/shared/wdk`, `runtime/windows/x64/wdk/lib`)
with `-fms-kernel`, not this tree.

NeverC injects these paths for Windows user-mode targets regardless of the
build host (`MSVC.cpp`).
