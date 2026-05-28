#include <ntddk.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define DEVICE_NAME    L"\\Device\\CetDriver"
#define SYMLINK_NAME   L"\\DosDevices\\CetDriver"
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

typedef ULONG (*ComputeFn)(ULONG x);

static ULONG __attribute__((noinline)) RotateLeft13(ULONG val) {
  return (val << 13) | (val >> 19);
}

static ULONG __attribute__((noinline)) XorFold(ULONG val) {
  return val ^ (val >> 16);
}

static ULONG DispatchCompute(ComputeFn fn, ULONG input) {
  return fn(input);
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
  dprintf("[CetDriver] Unloaded\n");
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

  ULONG r1 = DispatchCompute(RotateLeft13, 0xDEADBEEF);
  ULONG r2 = DispatchCompute(XorFold, 0xCAFEBABE);

  char buf[32];
  const char *src = "CET Shadow Stack";
  strncpy(buf, src, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  size_t len = strlen(buf);
  for (size_t i = 0; i < len; ++i)
    buf[i] = (char)toupper((unsigned char)buf[i]);

  dprintf("[CetDriver] Loaded  rotl=0x%08X xfold=0x%08X crt=\"%s\" len=%u\n",
          r1, r2, buf, (unsigned)len);

  return STATUS_SUCCESS;
}
