# WDK import libraries (`x64`, minimal kernel)

## Core (typical WDM driver)

| Library | Role |
|---------|------|
| `ntoskrnl.lib` | NT kernel exports |
| `hal.lib` | Hardware abstraction layer |
| `wdm.lib` | WDM helper imports |
| `BufferOverflowK.lib` | Kernel `/GS` stack cookie support |

Example: `examples/windows-driver/Makefile` uses `-lntoskrnl -lhal`.

## File-system minifilter (`fltkernel.h`)

There is no separate `fltkernel.lib`. Minifilter drivers include `fltkernel.h`
(from `include/ddk/`) and link **Filter Manager**:

| Library | Role |
|---------|------|
| `fltMgr.lib` | `FltRegisterFilter`, `FltStartFiltering`, … |
| `fltLib.lib` | Additional Filter Manager helpers |

Example link flags: `-lfltMgr` (and `-lfltLib` if needed).

Other stacks (NDIS, storport, USB class, …) need extra `.lib` files from a full WDK drop.

NeverC adds this directory via `--libpath=` when `-fms-kernel` is set
(`neverc/lib/Invoke/ToolChains/MSVC.cpp`).
