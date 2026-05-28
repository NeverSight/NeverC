/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    ws2san.h

Abstract:

    This module contains the Microsoft-specific extensions to the Windows
    Sockets SPI for WinSock Direct (SAN) support.

Revision History:

--*/

#ifndef _WS2SAN_H_
#define _WS2SAN_H_

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Option for getting maximum RDMA transfer size supported by provider
 */
#define SO_MAX_RDMA_SIZE                 0x700D

/*
 * Option for getting minimum RDMA transfer size feasible (performance-wise)
 * for the provider
 */
#define SO_RDMA_THRESHOLD_SIZE           0x700E

/*
 * The upcall table. This structure is passed by value to the service
 * provider's WSPStartup() entrypoint.
 */

typedef struct _WSPUPCALLTABLEEX {

    LPWPUCLOSEEVENT               lpWPUCloseEvent;
    LPWPUCLOSESOCKETHANDLE        lpWPUCloseSocketHandle;
    LPWPUCREATEEVENT              lpWPUCreateEvent;
    LPWPUCREATESOCKETHANDLE       lpWPUCreateSocketHandle;
    LPWPUFDISSET                  lpWPUFDIsSet;
    LPWPUGETPROVIDERPATH          lpWPUGetProviderPath;
    LPWPUMODIFYIFSHANDLE          lpWPUModifyIFSHandle;
    LPWPUPOSTMESSAGE              lpWPUPostMessage;
    LPWPUQUERYBLOCKINGCALLBACK    lpWPUQueryBlockingCallback;
    LPWPUQUERYSOCKETHANDLECONTEXT lpWPUQuerySocketHandleContext;
    LPWPUQUEUEAPC                 lpWPUQueueApc;
    LPWPURESETEVENT               lpWPUResetEvent;
    LPWPUSETEVENT                 lpWPUSetEvent;
    LPWPUOPENCURRENTTHREAD        lpWPUOpenCurrentThread;
    LPWPUCLOSETHREAD              lpWPUCloseThread;
    LPWPUCOMPLETEOVERLAPPEDREQUEST lpWPUCompleteOverlappedRequest;

} WSPUPCALLTABLEEX, FAR * LPWSPUPCALLTABLEEX;

/*
 *  An extended WSABUF, that includes a registration handle
 */

typedef struct _WSABUFEX {
    u_long      len;     /* the length of the buffer */
    __field_bcount(len) char FAR *  buf;     /* the pointer to the buffer */
    HANDLE  handle; /*  The handle returned by WSPRegisterMemory */
} WSABUFEX, FAR * LPWSABUFEX;


/*
 *  WinSock 2 SPI socket function prototypes
 */

__checkReturn
int
WSPAPI
WSPStartupEx(
    __in WORD wVersionRequested,
    __out LPWSPDATA lpWSPData,
    __in LPWSAPROTOCOL_INFOW lpProtocolInfo,
    __in LPWSPUPCALLTABLEEX lpUpcallTable,
    __out LPWSPPROC_TABLE lpProcTable
    );

typedef
__checkReturn
int
(WSPAPI * LPWSPSTARTUPEX)(
    __in WORD wVersionRequested,
    __out LPWSPDATA lpWSPData,
    __in LPWSAPROTOCOL_INFOW lpProtocolInfo,
    __in LPWSPUPCALLTABLEEX lpUpcallTable,
    __out LPWSPPROC_TABLE lpProcTable
    );

#define WSAID_REGISTERMEMORY \
        {0xC0B422F5,0xF58C,0x11d1,{0xAD,0x6C,0x00,0xC0,0x4F,0xA3,0x4A,0x2D}}

#define WSAID_DEREGISTERMEMORY \
        {0xC0B422F6,0xF58C,0x11d1,{0xAD,0x6C,0x00,0xC0,0x4F,0xA3,0x4A,0x2D}}

#define WSAID_REGISTERRDMAMEMORY \
        {0xC0B422F7,0xF58C,0x11d1,{0xAD,0x6C,0x00,0xC0,0x4F,0xA3,0x4A,0x2D}}

#define WSAID_DEREGISTERRDMAMEMORY \
        {0xC0B422F8,0xF58C,0x11d1,{0xAD,0x6C,0x00,0xC0,0x4F,0xA3,0x4A,0x2D}}

#define WSAID_RDMAWRITE \
        {0xC0B422F9,0xF58C,0x11d1,{0xAD,0x6C,0x00,0xC0,0x4F,0xA3,0x4A,0x2D}}

#define WSAID_RDMAREAD \
        {0xC0B422FA,0xF58C,0x11d1,{0xAD,0x6C,0x00,0xC0,0x4F,0xA3,0x4A,0x2D}}

#if(_WIN32_WINNT >= 0x0501)
#define WSAID_MEMORYREGISTRATIONCACHECALLBACK \
        {0xE5DA4AF8,0xD824,0x48CD,{0xA7,0x99,0x63,0x37,0xA9,0x8E,0xD2,0xAF}}
#endif //(_WIN32_WINNT >= 0x0501)

#define MEM_READ        1
#define MEM_WRITE       2
#define MEM_READWRITE   3


__checkReturn
HANDLE WSPAPI
WSPRegisterMemory(
    __in SOCKET s,
    __in_bcount(dwBufferLength) PVOID lpBuffer,
    __in DWORD dwBufferLength,
    __in DWORD dwFlags,
    __out LPINT lpErrno
    );

int WSPAPI
WSPDeregisterMemory(
    __in SOCKET s,
    __in HANDLE handle,
    __out LPINT lpErrno
    );

__checkReturn
int WSPAPI
WSPRegisterRdmaMemory(
    __in SOCKET s,
    __in_bcount(dwBufferLength) PVOID lpBuffer,
    __in DWORD dwBufferLength,
    __in DWORD dwFlags,
    __out_bcount(*lpdwDescriptorLength) LPVOID lpRdmaBufferDescriptor,
    __inout LPDWORD lpdwDescriptorLength,
    __out LPINT lpErrno
    );

int WSPAPI
WSPDeregisterRdmaMemory(
    __in SOCKET s,
    __in_bcount(dwDescriptorLength) LPVOID lpRdmaBufferDescriptor,
    __in DWORD dwDescriptorLength,
    __out LPINT lpErrno
    );

int WSPAPI
WSPRdmaWrite(
    __in SOCKET s,
    __in_ecount(dwBufferCount) LPWSABUFEX lpBuffers,
    __in DWORD dwBufferCount,
    __in_bcount(dwTargetDescriptorLength) LPVOID lpTargetBufferDescriptor,
    __in DWORD dwTargetDescriptorLength,
    __in DWORD dwTargetBufferOffset,
    __out LPDWORD lpdwNumberOfBytesWritten,
    __in DWORD dwFlags,
    __in_opt LPWSAOVERLAPPED lpOverlapped,
    __in_opt LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
    __in LPWSATHREADID lpThreadId,
    __out LPINT lpErrno
    );

int WSPAPI
WSPRdmaRead(
    __in SOCKET s,
    __in_ecount(dwBufferCount) LPWSABUFEX lpBuffers,
    __in DWORD dwBufferCount,
    __in_bcount(dwTargetDescriptorLength) LPVOID lpTargetBufferDescriptor,
    __in DWORD dwTargetDescriptorLength,
    __in DWORD dwTargetBufferOffset,
    __out LPDWORD lpdwNumberOfBytesRead,
    __in DWORD dwFlags,
    __in_opt LPWSAOVERLAPPED lpOverlapped,
    __in_opt LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
    __in LPWSATHREADID lpThreadId,
    __out LPINT lpErrno
    );

#if(_WIN32_WINNT >= 0x0501)
__checkReturn
int WSPAPI
WSPMemoryRegistrationCacheCallback(
    __in_bcount(Size) LPVOID lpvAddress,                             
    __in SIZE_T Size,                               
    __out LPINT lpErrno                
    );
#endif //(_WIN32_WINNT >= 0x0501)

/*
 * "QueryInterface" versions of the above APIs.
 */

typedef
__checkReturn
HANDLE
(WSPAPI * LPFN_WSPREGISTERMEMORY)(
    __in SOCKET s,
    __in_bcount(dwBufferLength) PVOID lpBuffer,
    __in DWORD dwBufferLength,
    __in DWORD dwFlags,
    __out LPINT lpErrno
    );

typedef
int
(WSPAPI * LPFN_WSPDEREGISTERMEMORY)(
    __in SOCKET s,
    __in HANDLE handle,
    __out LPINT lpErrno
    );

typedef
__checkReturn
BOOL
(WSPAPI * LPFN_WSPREGISTERRDMAMEMORY)(
    __in SOCKET s,
    __in_bcount(dwBufferLength) PVOID lpBuffer,
    __in DWORD dwBufferLength,
    __in DWORD dwFlags,
    __out_bcount(*lpdwDescriptorLength) LPVOID lpRdmaBufferDescriptor,
    __inout LPDWORD lpdwDescriptorLength,
    __out LPINT lpErrno
    );

typedef
int
(WSPAPI * LPFN_WSPDEREGISTERRDMAMEMORY)(
    __in SOCKET s,
    __in_bcount(dwDescriptorLength) LPVOID lpRdmaBufferDescriptor,
    __in DWORD dwDescriptorLength,
    __out LPINT lpErrno
    );

typedef
int
(WSPAPI * LPFN_WSPRDMAWRITE)(
    __in SOCKET s,
    __in_ecount(dwBufferCount) LPWSABUFEX lpBuffers,
    __in DWORD dwBufferCount,
    __in_bcount(dwTargetDescriptorLength) LPVOID lpTargetBufferDescriptor,
    __in DWORD dwTargetDescriptorLength,
    __in DWORD dwTargetBufferOffset,
    __out LPDWORD lpdwNumberOfBytesWritten,
    __in DWORD dwFlags,
    __in_opt LPWSAOVERLAPPED lpOverlapped,
    __in_opt LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
    __in LPWSATHREADID lpThreadId,
    __out LPINT lpErrno
    );

typedef
int
(WSPAPI * LPFN_WSPRDMAREAD)(
    __in SOCKET s,
    __in_ecount(dwBufferCount) LPWSABUFEX lpBuffers,
    __in DWORD dwBufferCount,
    __in_bcount(dwTargetDescriptorLength) LPVOID lpTargetBufferDescriptor,
    __in DWORD dwTargetDescriptorLength,
    __in DWORD dwTargetBufferOffset,
    __out LPDWORD lpdwNumberOfBytesRead,
    __in DWORD dwFlags,
    __in_opt LPWSAOVERLAPPED lpOverlapped,
    __in_opt LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
    __in LPWSATHREADID lpThreadId,
    __out LPINT lpErrno
    );

#if(_WIN32_WINNT >= 0x0501)
typedef
__checkReturn
int 
(WSPAPI * LPFN_WSPMEMORYREGISTRATIONCACHECALLBACK)(
    __in_bcount(Size) LPVOID lpvAddress,                             
    __in SIZE_T Size,                               
    __out LPINT lpErrno                
    );
#endif //(_WIN32_WINNT >= 0x0501)

#ifdef __cplusplus
}
#endif

#endif // _WS2SAN_H_

