# Windows Kernel Driver Example

A minimal WDM kernel driver built with NeverC. Cross-compiles from macOS / Linux.

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

The output is `ExampleDriver.sys`.

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
