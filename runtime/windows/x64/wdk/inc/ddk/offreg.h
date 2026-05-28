/*++

Copyright (c) Microsoft Corporation

Module Name:

    offreg.h

Abstract:

    This module contains the header file for the
    offreg utility.

--*/

#pragma once

#ifndef __OFFREG_H__
#define __OFFREG_H__

#ifdef __cplusplus
extern "C"
{
#endif


#if defined(OFFREG_DLL)
#define ORAPI _declspec(dllexport) __stdcall
#else 
#define ORAPI _declspec(dllimport) __stdcall
#endif

typedef PVOID   ORHKEY;
typedef ORHKEY  *PORHKEY;

VOID 
ORAPI
ORGetVersion(
    __out  PDWORD pdwMajorVersion,
    __out  PDWORD pdwMinorVersion
    );

DWORD
ORAPI
OROpenHive (
    __in  PCWSTR    lpHivePath,
    __out PORHKEY   phkResult
    );

DWORD
ORAPI
ORCreateHive (
    __out PORHKEY   phkResult
    );

DWORD
ORAPI
ORCloseHive (
    __in ORHKEY     Handle
    );

DWORD
ORAPI
ORSaveHive (
    __in ORHKEY     Handle,
    __in PCWSTR     lpHivePath,
    __in DWORD      dwOsMajorVersion,
    __in DWORD      dwOsMinorVersion
    );

DWORD
ORAPI
OROpenKey (
    __in ORHKEY     Handle,
    __in_opt PCWSTR lpSubKeyName,
    __out PORHKEY   phkResult
    );

DWORD
ORAPI
ORCloseKey (
    __in ORHKEY     Handle
    );

DWORD
ORAPI
ORCreateKey (
    __in ORHKEY     Handle,
    __in PCWSTR     lpSubKey,
    __in_opt PWSTR  lpClass,
    __in_opt DWORD  dwOptions,
    __in_opt PSECURITY_DESCRIPTOR pSecurityDescriptor,
    __out PORHKEY   phkResult,
    __out_opt PDWORD pdwDisposition
    );

DWORD
ORAPI
ORDeleteKey (
    __in ORHKEY     Handle,
    __in_opt PCWSTR lpSubKey
    );

DWORD
ORAPI
ORQueryInfoKey (
    __in ORHKEY     Handle,
    __out_ecount_opt(*lpcClass) PWSTR lpClass,
    __inout_opt PDWORD lpcClass,
    __out_opt PDWORD lpcSubKeys,
    __out_opt PDWORD lpcMaxSubKeyLen,
    __out_opt PDWORD lpcMaxClassLen,
    __out_opt PDWORD lpcValues,
    __out_opt PDWORD lpcMaxValueNameLen,
    __out_opt PDWORD lpcMaxValueLen,
    __out_opt PDWORD lpcbSecurityDescriptor,
    __out_opt PFILETIME lpftLastWriteTime
    );

DWORD
ORAPI
OREnumKey (
    __in ORHKEY     Handle,
    __in DWORD      dwIndex,
    __out_ecount(*lpcName) PWSTR lpName,
    __inout PDWORD  lpcName,
    __out_ecount_opt(*lpcClass) PWSTR lpClass,
    __inout_opt PDWORD lpcClass,
    __out_opt PFILETIME lpftLastWriteTime
    );

DWORD
ORAPI
ORGetKeySecurity (
    __in ORHKEY     Handle,
    __in  SECURITY_INFORMATION SecurityInformation,
    __out_opt PSECURITY_DESCRIPTOR pSecurityDescriptor,
    __inout PDWORD  lpcbSecurityDescriptor
    );

DWORD  
ORAPI
ORSetKeySecurity (
    __in ORHKEY     Handle,
    __in  SECURITY_INFORMATION SecurityInformation,
    __in PSECURITY_DESCRIPTOR pSecurityDescriptor
    );

DWORD
ORAPI
ORGetVirtualFlags (
    __in ORHKEY  Handle,
    __out PDWORD pdwFlags
    );

DWORD
ORAPI
ORSetVirtualFlags (
    __in ORHKEY  Handle,
    __in DWORD   dwFlags
    );

DWORD
ORAPI
ORDeleteValue (
    __in ORHKEY     Handle,
    __in_opt PCWSTR lpValueName
    );

DWORD
ORAPI
ORGetValue (
    __in ORHKEY     Handle,
    __in_opt PCWSTR lpSubKey,
    __in_opt PCWSTR lpValue,
    __out_opt PDWORD pdwType,
    __out_bcount_opt(*pcbData) PVOID pvData,
    __inout_opt PDWORD pcbData
    );

DWORD
ORAPI
ORSetValue (
    __in ORHKEY     Handle,
    __in_opt PCWSTR lpValueName,
    __in DWORD      dwType,
    __in_bcount_opt(cbData) const BYTE * lpData,
    __in DWORD      cbData
    );

DWORD
ORAPI
OREnumValue (
    __in ORHKEY     Handle,
    __in DWORD      dwIndex,
    __out_ecount(*lpcValueName) PWSTR lpValueName,
    __inout         PDWORD lpcValueName,
    __out_opt PDWORD lpType,
    __out_bcount_opt(*lpcbData) PBYTE lpData,
    __inout_opt PDWORD lpcbData
    );

VOID
ORShutdown (
    VOID
    );


#ifdef __cplusplus
}
#endif

#endif //__OFFREG_H__

