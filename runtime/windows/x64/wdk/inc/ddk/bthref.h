/*++

Copyright (c) 2005      Microsoft Corporation

Module Name:

        BTHREF.H

Abstract:

   Public structures used for object reference counting

Environment:

    Kernel & user mode

Revision History:

  --*/

#ifndef __BTHREF_H__
#define __BTHREF_H__

//
// Added in Vista
//
#if (NTDDI_VERSION >= NTDDI_VISTA)

typedef struct _REF_OBJ *PREF_OBJ;

__drv_sameIRQL
typedef void (*PFNDESTROY)(__in PREF_OBJ);

typedef struct _REF_OBJ_DEBUG_INFO *PREF_OBJ_DEBUG_INFO;

typedef struct _REF_OBJ {
    ULONG Count;
    PFNDESTROY DestroyFunction;
    PREF_OBJ_DEBUG_INFO DebugInfo;
#if DBG
    ULONG Flags;
#endif
} REF_OBJ, *PREF_OBJ;

#endif // >= VISTA

#endif // __BTHREF_H__


