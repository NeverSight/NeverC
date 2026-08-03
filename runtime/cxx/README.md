# NeverC C++ runtime (`runtime/cxx`)

NeverC-only C++ ABI v1 support library. **Not** compatible with system
libstdc++ / libc++ / libc++abi.

| File | Role |
|------|------|
| `operator_new_delete.cpp` | `operator new` / `delete` (+ placement); `_Znwm`/`_ZdlPv`/`_Znam`/`_ZdaPv` |
| `operator_new_nothrow.cpp` | nothrow new |
| `pure_virtual.cpp` | `__cxa_pure_virtual` / deleted virtual |
| `rtti.cpp` | `std::type_info`, `__neverc_dynamic_cast` |
| `cxa_exception.cpp` | throw/catch, `__cxa_begin_catch`/`end_catch`, `__neverc_personality_v0` |
| `cxa_guard.cpp` | static init guards |
| `cxa_atexit.cpp` | `__cxa_atexit` / finalize |
| `coroutine.cpp` | coroutine frame alloc helpers |

Linked automatically when compiling with `-std=c++20` (once driver wiring is
finalized). Streams are intentionally absent.
