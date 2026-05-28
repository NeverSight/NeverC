/*++

Copyright (c) 2004  Microsoft Corporation

Module Name:

    aux_klib.h

Abstract:

    Kernel mode shim to access system functionality that is not properly exposed
    to applications in currently shipping operating systems.

--*/

#ifndef _AUX_KLIB_H
#define _AUX_KLIB_H

#ifndef PIMAGE_EXPORT_DIRECTORY
#include <ntimage.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


#if (NTDDI_VERSION >= NTDDI_WIN2K)

#define AUX_KLIB_MODULE_PATH_LEN 256

typedef struct _AUX_MODULE_BASIC_INFO {
    PVOID ImageBase;
} AUX_MODULE_BASIC_INFO, *PAUX_MODULE_BASIC_INFO;

typedef struct _AUX_MODULE_EXTENDED_INFO {
    AUX_MODULE_BASIC_INFO BasicInfo;
    ULONG ImageSize;
    USHORT FileNameOffset;
    UCHAR FullPathName [AUX_KLIB_MODULE_PATH_LEN];
} AUX_MODULE_EXTENDED_INFO, *PAUX_MODULE_EXTENDED_INFO;

typedef struct _KBUGCHECK_DATA {
    ULONG     BugCheckDataSize;
    ULONG     BugCheckCode;
    ULONG_PTR Parameter1;
    ULONG_PTR Parameter2;
    ULONG_PTR Parameter3;
    ULONG_PTR Parameter4;
} KBUGCHECK_DATA, *PKBUGCHECK_DATA;

#endif // (NTDDI_VERSION >= NTDDI_WIN2K)


#if (NTDDI_VERSION >= NTDDI_WIN2K)

NTSTATUS
__stdcall
AuxKlibInitialize (
    VOID
    );

#endif // (NTDDI_VERSION >= NTDDI_WIN2K)


#if (NTDDI_VERSION >= NTDDI_WIN2K)

NTSTATUS
__stdcall
AuxKlibQueryModuleInformation (
    IN OUT PULONG BufferSize,
    IN ULONG ElementSize,
    OUT PVOID QueryInfo OPTIONAL
    );

#endif // (NTDDI_VERSION >= NTDDI_WIN2K)


#if (NTDDI_VERSION >= NTDDI_WIN2K)

NTSTATUS
AuxKlibGetBugCheckData(
    OUT PKBUGCHECK_DATA BugCheckData
    );

#endif // (NTDDI_VERSION >= NTDDI_WIN2K)


#if (NTDDI_VERSION >= NTDDI_WIN2K)

PIMAGE_EXPORT_DIRECTORY
AuxKlibGetImageExportDirectory(
    IN PVOID ImageBase
    );

#endif // (NTDDI_VERSION >= NTDDI_WIN2K)


#ifdef __cplusplus
}
#endif

#endif // _AUX_KLIB_H

