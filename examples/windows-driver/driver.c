#include <ntddk.h>

#define DEVICE_NAME    L"\\Device\\ExampleDriver"
#define SYMLINK_NAME   L"\\DosDevices\\ExampleDriver"
#define dprintf(...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__)

static UNICODE_STRING DeviceName;
static UNICODE_STRING SymlinkName;
static PDEVICE_OBJECT DeviceObject;

static NTSTATUS DispatchCreate(PDEVICE_OBJECT DevObj, PIRP Irp) {
  UNREFERENCED_PARAMETER(DevObj);
  Irp->IoStatus.Status = STATUS_SUCCESS;
  Irp->IoStatus.Information = 0;
  IoCompleteRequest(Irp, IO_NO_INCREMENT);
  return STATUS_SUCCESS;
}

static NTSTATUS DispatchClose(PDEVICE_OBJECT DevObj, PIRP Irp) {
  UNREFERENCED_PARAMETER(DevObj);
  Irp->IoStatus.Status = STATUS_SUCCESS;
  Irp->IoStatus.Information = 0;
  IoCompleteRequest(Irp, IO_NO_INCREMENT);
  return STATUS_SUCCESS;
}

static NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT DevObj, PIRP Irp) {
  UNREFERENCED_PARAMETER(DevObj);

  PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
  NTSTATUS Status = STATUS_SUCCESS;

  switch (Stack->Parameters.DeviceIoControl.IoControlCode) {
  default:
    Status = STATUS_INVALID_DEVICE_REQUEST;
    break;
  }

  Irp->IoStatus.Status = Status;
  Irp->IoStatus.Information = 0;
  IoCompleteRequest(Irp, IO_NO_INCREMENT);
  return Status;
}

static VOID DriverUnload(PDRIVER_OBJECT DriverObj) {
  UNREFERENCED_PARAMETER(DriverObj);
  IoDeleteSymbolicLink(&SymlinkName);
  IoDeleteDevice(DeviceObject);
  dprintf("[ExampleDriver] Unloaded\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObj, PUNICODE_STRING RegistryPath) {
  UNREFERENCED_PARAMETER(RegistryPath);

  RtlInitUnicodeString(&DeviceName, DEVICE_NAME);
  RtlInitUnicodeString(&SymlinkName, SYMLINK_NAME);

  NTSTATUS Status =
      IoCreateDevice(DriverObj, 0, &DeviceName, FILE_DEVICE_UNKNOWN,
                     FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
  if (!NT_SUCCESS(Status))
    return Status;

  Status = IoCreateSymbolicLink(&SymlinkName, &DeviceName);
  if (!NT_SUCCESS(Status)) {
    IoDeleteDevice(DeviceObject);
    return Status;
  }

  DriverObj->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
  DriverObj->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
  DriverObj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
  DriverObj->DriverUnload = DriverUnload;

  dprintf("[ExampleDriver] Loaded\n");
  return STATUS_SUCCESS;
}
