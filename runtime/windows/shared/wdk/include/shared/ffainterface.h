/*++

Copyright (c) Microsoft Corporation

Module Name:

    ffainterface.h

Abstract:

    This file contains the interfaces required for FF-A support.

Author:

    Yinghan Yang (yinghany) 18-Sep-2024

Environment:

    Kernel Mode

Revision History:

--*/


#pragma once

#pragma warning( push )
#pragma warning( disable : 4115 ) /* nonstandard extension used : named type definition in parens */
#pragma warning( disable : 4201 ) /* nonstandard extension used : nameless struct/union */
#pragma warning( disable : 4214 ) /* nonstandard extension used : bit field types other then int */

#define FFA_NOTIFICATION_COUNT 64
#define FFA_MSG_SEND_DIRECT_REQ2_PARAMETERS_VERSION_V1 0x1

typedef struct _FFA_SEND_DIRECT_REQ2_BUFFER {
    union {
        struct {
            ULONGLONG Arg0;
            ULONGLONG Arg1;
            ULONGLONG Arg2;
            ULONGLONG Arg3;
            ULONGLONG Arg4;
            ULONGLONG Arg5;
            ULONGLONG Arg6;
            ULONGLONG Arg7;
            ULONGLONG Arg8;
            ULONGLONG Arg9;
            ULONGLONG Arg10;
            ULONGLONG Arg11;
            ULONGLONG Arg12;
            ULONGLONG Arg13;
            ULONGLONG Arg14;
            ULONGLONG Arg15;
            ULONGLONG Arg16;
            ULONGLONG Arg17;
        };

        UCHAR Buffer[ANYSIZE_ARRAY];
    };
} FFA_SEND_DIRECT_REQ2_BUFFER, *PFFA_SEND_DIRECT_REQ2_BUFFER;

#define FFA_SEND_DIRECT_REQ2_BUFFER_SIZE sizeof(FFA_SEND_DIRECT_REQ2_BUFFER)

typedef struct _FFA_PARAMETERS FFA_PARAMETERS, *PFFA_PARAMETERS;

typedef struct _FFA_DIRECT_REQ2_PARAMETER_FLAGS {
    struct {
        ULONG DriverAsyncHandling: 1;
        ULONG Reserved: 31;
    };

    ULONG AsULONG;
} FFA_DIRECT_REQ2_PARAMETER_FLAGS, *PFFA_DIRECT_REQ2_PARAMETER_FLAGS;

typedef struct _FFA_DIRECT_REQ2_ASYNC_PARAMETERS {

    //
    // Input Parameters
    //

    FFA_DIRECT_REQ2_PARAMETER_FLAGS Flags;

    //
    // Output Parameters
    //

    ULONGLONG DelayHintNs;
    ULONG TargetId;
    NTSTATUS Status;
} FFA_DIRECT_REQ2_ASYNC_PARAMETERS, *PFFA_DIRECT_REQ2_ASYNC_PARAMETERS;

typedef struct _FFA_RUN_TARGET_INPUT_PARAMETERS {
    ULONG TargetId;
} FFA_RUN_TARGET_INPUT_PARAMETERS, *PFFA_RUN_TARGET_INPUT_PARAMETERS;

typedef struct _FFA_RUN_TARGET_OUTPUT_PARAMETERS {
    ULONG FunctionIdReturn;
    ULONGLONG DelayHintNs;
    ULONG TargetId;
    FFA_SEND_DIRECT_REQ2_BUFFER OutputBuffer;
} FFA_RUN_TARGET_OUTPUT_PARAMETERS, *PFFA_RUN_TARGET_OUTPUT_PARAMETERS;

typedef struct _FFA_MSG_SEND_DIRECT_REQ2_PARAMETERS {
    USHORT Version;
    union {
        struct {
            ULONG64 ReceiverId : 16;
            ULONG64 Reserved : 48;
        };

        ULONG64 AsULONG64;
    };

    GUID ServiceUuid;
    FFA_DIRECT_REQ2_ASYNC_PARAMETERS AsyncParameters;
    FFA_SEND_DIRECT_REQ2_BUFFER InputBuffer;
    FFA_SEND_DIRECT_REQ2_BUFFER OutputBuffer;
} FFA_MSG_SEND_DIRECT_REQ2_PARAMETERS, *PFFA_MSG_SEND_DIRECT_REQ2_PARAMETERS;

typedef
NTSTATUS 
(*PFFA_NOTIFY_CALLBACK) (
    _In_ PVOID Context,
    _In_ LPGUID ServiceGuid,
    _In_ ULONG NotifyCode
    );

typedef struct _FFA_NOTIFICATION_REGISTRATION_PARAMETERS {
    LPGUID ServiceUuid;
    ULONG NotifyCode;
    PVOID NotifyContext;
    PFFA_NOTIFY_CALLBACK NotifyCallback;
} FFA_NOTIFICATION_REGISTRATION_PARAMETERS, *PFFA_NOTIFICATION_REGISTRATION_PARAMETERS;

typedef PVOID _FFA_NOTIFICATION_REGISTRATION_TOKEN, FFA_NOTIFICATION_REGISTRATION_TOKEN, *PFFA_NOTIFICATION_REGISTRATION_TOKEN;

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTSTATUS
FFA_REGISTER_NOTIFICATION (
    _In_ PFFA_NOTIFICATION_REGISTRATION_PARAMETERS RegistrationParameters,
    _Out_ PFFA_NOTIFICATION_REGISTRATION_TOKEN Token
    );

typedef FFA_REGISTER_NOTIFICATION *PFFA_REGISTER_NOTIFICATION;

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS 
FFA_UNREGISTER_NOTIFICATION (
    _In_ FFA_NOTIFICATION_REGISTRATION_TOKEN Token
    );

typedef FFA_UNREGISTER_NOTIFICATION *PFFA_UNREGISTER_NOTIFICATION;

typedef
_Function_class_(FFA_MSG_SEND_DIRECT_REQ2)
NTSTATUS
FFA_MSG_SEND_DIRECT_REQ2 (
    _In_ PFFA_MSG_SEND_DIRECT_REQ2_PARAMETERS Parameters
    );

typedef FFA_MSG_SEND_DIRECT_REQ2 *PFFA_MSG_SEND_DIRECT_REQ2;

typedef
_Function_class_(FFA_RUN_TARGET)
NTSTATUS
FFA_RUN_TARGET (
    _In_ PFFA_RUN_TARGET_INPUT_PARAMETERS InputParameters,
    _Out_ PFFA_RUN_TARGET_OUTPUT_PARAMETERS OutputParameters
    );

typedef FFA_RUN_TARGET *PFFA_RUN_TARGET;

typedef struct _FFA_INTERFACE_V1 {
    PFFA_MSG_SEND_DIRECT_REQ2 SendDirectReq2;
    PFFA_RUN_TARGET RunTarget;
    PFFA_REGISTER_NOTIFICATION RegisterNotification;
    PFFA_UNREGISTER_NOTIFICATION UnregisterNotification;
} FFA_INTERFACE_V1, *PFFA_INTERFACE_V1;

typedef struct _FFA_INTERFACE_V1 FFA_INTERFACE, *PFFA_INTERFACE;

#define FFA_INTERFACE_VERSION_1 0x1

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTKERNELAPI
PFFA_INTERFACE
ExGetFfaInterface (
    _In_ ULONG Version
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTKERNELAPI
VOID
ExFreeFfaInterface (
    _In_ PFFA_INTERFACE Interface
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
PFFA_INTERFACE
(*EX_GET_FFA_INTERFACE) (
    _In_ ULONG Version
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
(*EX_FREE_FFA_INTERFACE) (
    _In_ PFFA_INTERFACE Interface
    );

#pragma warning( pop )

