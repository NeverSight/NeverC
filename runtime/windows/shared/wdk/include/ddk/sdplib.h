#ifndef __SDPLIB_H__
#define __SDPLIB_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "sdpnode.h"

#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif

#ifdef _NTDDK_
#define SDPLIB_KERNEL
#endif

#ifdef SDPLIB_KERNEL
__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_TREE_ROOT_NODE
SdpCreateNodeTree(
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeNil(
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeUInt128(
    __in PSDP_ULARGE_INTEGER_16 puli16Val,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeUInt64(
    __in ULONGLONG ullVal,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeUInt32(
    __in ULONG ulVal,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeUInt16(
    __in USHORT usVal,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeUInt8(
    __in UCHAR ucVal,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeInt128(
    __in PSDP_LARGE_INTEGER_16 uil16Val,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeInt64(
    __in LONGLONG llVal,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeInt32(
    __in LONG lVal,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeInt16(
    __in SHORT sVal,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeInt8(
    __in CHAR cVal,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeUUID128(
    __in const GUID *uuid,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeUUID32(
    __in ULONG uuidVal4,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeUUID16(
    __in USHORT uuidVal2,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeBoolean(
    __in SDP_BOOLEAN  bVal,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeSequence(
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeAlternative(
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeUrl(
    __in_bcount(UrlLength) PCHAR url,
    __in_bound ULONG urlLength,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
NTSTATUS
SdpAddAttributeToTree(
    __in PSDP_TREE_ROOT_NODE Tree,
    __in USHORT AttribId,
    __in __drv_when(return==0, __drv_aliasesMem) PSDP_NODE AttribValue,
    __in ULONG tag
    );

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
PSDP_NODE
SdpCreateNodeString(
    __in_bcount(StringLength) PCHAR string, 
    __in_bound ULONG stringLength, 
    __in ULONG tag
    );

#else //SDPLIB_KERNEL

__checkReturn
PSDP_TREE_ROOT_NODE
SdpCreateNodeTree();

__checkReturn
PSDP_NODE
SdpCreateNodeNil();

__checkReturn
PSDP_NODE
SdpCreateNodeUInt128(
    __in PSDP_ULARGE_INTEGER_16 puli16Val
    );

__checkReturn
PSDP_NODE
SdpCreateNodeUInt64(
    __in ULONGLONG ullVal
    );

__checkReturn
PSDP_NODE
SdpCreateNodeUInt32(
    __in ULONG ulVal
    );

__checkReturn
PSDP_NODE
SdpCreateNodeUInt16(
    __in USHORT usVal
    );

__checkReturn
PSDP_NODE
SdpCreateNodeUInt8(
    __in UCHAR ucVal
    );

__checkReturn
PSDP_NODE
SdpCreateNodeInt128(
    __in PSDP_LARGE_INTEGER_16 uil16Val
    );

__checkReturn
PSDP_NODE
SdpCreateNodeInt64(
    __in LONGLONG llVal
    );

__checkReturn
PSDP_NODE
SdpCreateNodeInt32(
    __in LONG lVal
    );

__checkReturn
PSDP_NODE
SdpCreateNodeInt16(
    __in SHORT sVal
    );

__checkReturn
PSDP_NODE
SdpCreateNodeInt8(
    __in CHAR cVal
    );

__checkReturn
PSDP_NODE
SdpCreateNodeUUID128(
    __in const GUID *uuid
    );

__checkReturn
PSDP_NODE
SdpCreateNodeUUID32(
    __in ULONG uuidVal4
    );

__checkReturn
PSDP_NODE
SdpCreateNodeUUID16(
    __in USHORT uuidVal2
    );

__checkReturn
PSDP_NODE
SdpCreateNodeBoolean(
    __in SDP_BOOLEAN  bVal
    );

__checkReturn
PSDP_NODE
SdpCreateNodeSequence();

__checkReturn
PSDP_NODE
SdpCreateNodeAlternative();

__checkReturn
PSDP_NODE
SdpCreateNodeUrl(
    __in_bcount(UrlLength) PCHAR url,
    __in_bound ULONG urlLength
    );

__checkReturn
NTSTATUS
SdpAddAttributeToTree(
    __in PSDP_TREE_ROOT_NODE Tree,
    __in USHORT AttribId,
    __in PSDP_NODE AttribValue
    );

__checkReturn
PSDP_NODE
SdpCreateNodeString(
    __in_bcount(StringLength) PCHAR string, 
    __in_bound ULONG stringLength
    );

#endif //SDPLIB_KERNEL

__drv_sameIRQL
__success(TRUE)
NTSTATUS
SdpFreeTree(
    __in __drv_freesMem(Mem) PSDP_TREE_ROOT_NODE Tree
    );
    
#define   SdpCreateNodeUUID SdpCreateNodeUUID128

__checkReturn
__drv_sameIRQL
NTSTATUS
SdpAppendNodeToContainerNode(
    __in PSDP_NODE Parent,
    __in __drv_aliasesMem PSDP_NODE Node
    );

__checkReturn
__drv_sameIRQL
NTSTATUS
SdpFindAttributeInTree(
    __in PSDP_TREE_ROOT_NODE Tree,
    __in USHORT AttribId,
    __out PSDP_NODE *Attribute
    );

#ifdef __cplusplus
};
#endif


#endif


