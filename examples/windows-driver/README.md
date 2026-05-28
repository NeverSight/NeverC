# Windows Kernel Driver Example

A minimal WDM kernel driver built with NeverC. Cross-compiles from macOS / Linux.

NeverC is an all-in-one compiler — a single invocation handles preprocessing,
compilation, optimization (auto-LTO), and linking via the built-in linker.

## Build

From the repo:

```bash
cd examples/windows-driver
make
```

From a standalone NeverC release:

```bash
make NEVERC=/path/to/neverc
```

The output is `ExampleDriver.sys` (~3 KB, auto-LTO optimized).

## Manual build (without Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver.sys driver.c
```

## What it does

- Creates a device object at `\Device\ExampleDriver`
- Creates a symbolic link at `\DosDevices\ExampleDriver`
- Handles `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Prints load/unload messages via `DbgPrint`

## Loading (on a Windows test machine)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Enable test signing or use a code signing certificate for production.
