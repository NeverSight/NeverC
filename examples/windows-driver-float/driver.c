#include <ntddk.h>

#define DEVICE_NAME    L"\\Device\\FloatDriver"
#define SYMLINK_NAME   L"\\DosDevices\\FloatDriver"
#define dprintf(...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__)

static UNICODE_STRING DeviceName;
static UNICODE_STRING SymlinkName;
static PDEVICE_OBJECT DeviceObject;

// Compute area of a circle. Uses x87/SSE floating point — the kernel will
// NOT preserve these registers across context switches by default, so the
// caller must bracket this with KeSaveExtendedProcessorState /
// KeRestoreExtendedProcessorState (see ComputeAreaSafe below).
static double __attribute__((noinline)) ComputeArea(double radius) {
  const double pi = 3.14159265358979323846;
  return pi * radius * radius;
}

// Wrapper that safely saves and restores the extended processor state
// (XMM/YMM, AVX-512 if XSTATE_MASK_AVX512 is requested) before touching FP.
//
// XSTATE_MASK_LEGACY_FLOATING_POINT (bit 0) — x87
// XSTATE_MASK_LEGACY_SSE            (bit 1) — XMM0–15
// XSTATE_MASK_GSSE / _AVX           (bit 2) — YMM0–15 upper halves
// (combine via bitwise OR; XSTATE_MASK_LEGACY covers x87 + SSE)
static NTSTATUS ComputeAreaSafe(double radius, double *out) {
  XSTATE_SAVE save;
  NTSTATUS status = KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &save);
  if (!NT_SUCCESS(status))
    return status;

  *out = ComputeArea(radius);

  KeRestoreExtendedProcessorState(&save);
  return STATUS_SUCCESS;
}

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
  NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
  (void)Stack;
  Irp->IoStatus.Status = Status;
  Irp->IoStatus.Information = 0;
  IoCompleteRequest(Irp, IO_NO_INCREMENT);
  return Status;
}

static VOID DriverUnload(PDRIVER_OBJECT DriverObj) {
  UNREFERENCED_PARAMETER(DriverObj);
  IoDeleteSymbolicLink(&SymlinkName);
  IoDeleteDevice(DeviceObject);
  dprintf("[FloatDriver] Unloaded\n");
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

  double area_r1 = 0.0;
  double area_r5 = 0.0;
  Status = ComputeAreaSafe(1.0, &area_r1);
  if (NT_SUCCESS(Status))
    Status = ComputeAreaSafe(5.0, &area_r5);

  // %f is not supported by DbgPrint; use integer representation of the
  // double (memcpy double bits into UINT64) to demonstrate the value.
  UINT64 bits_r1, bits_r5;
  RtlCopyMemory(&bits_r1, &area_r1, sizeof(bits_r1));
  RtlCopyMemory(&bits_r5, &area_r5, sizeof(bits_r5));
  dprintf("[FloatDriver] Loaded  area(1.0)=0x%016llX area(5.0)=0x%016llX\n",
          (unsigned long long)bits_r1, (unsigned long long)bits_r5);

  return STATUS_SUCCESS;
}
