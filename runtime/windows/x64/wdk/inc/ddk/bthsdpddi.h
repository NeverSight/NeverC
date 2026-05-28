#ifndef  __BTHSDPDDI_H__
#define  __BTHSDPDDI_H__

#if (NTDDI_VERSION >= NTDDI_VISTA)


#ifdef __cplusplus
extern "C" {
#endif

#define BTHDDI_SDP_PARSE_INTERFACE_VERSION_FOR_QI       (0x0100)
#define BTHDDI_SDP_NODE_INTERFACE_VERSION_FOR_QI        (0x0100)

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_TREE_ROOT_NODE (*PCREATENODETREEROOT)(__in ULONG tag);

__drv_sameIRQL
typedef NTSTATUS (*PFREETREE)(__in __drv_freesMem(Mem) PSDP_TREE_ROOT_NODE Tree);

__checkReturn
__drv_sameIRQL
typedef NTSTATUS (*PAPPENDNODETOCONTAINERNODE)(__in PSDP_NODE Container,
                                               __in __drv_aliasesMem PSDP_NODE Node);


__checkReturn
__drv_sameIRQL
typedef NTSTATUS (*PADDATTRIBUTETOTREEE)(__in PSDP_TREE_ROOT_NODE Root,
                                         __in USHORT AttribId,
                                         __in __drv_aliasesMem PSDP_NODE AttribValueNode,
                                         __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODENIL)(__in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEBOOLEAN)(__in UCHAR bVal, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEUINT8)(__in UCHAR ucVal, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEUINT16)(__in USHORT usVal, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEUINT32)(__in ULONG ulVal, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEUINT64)(__in ULONGLONG ullVal, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEUINT128)(__in PSDP_ULARGE_INTEGER_16 puli16Val, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEINT8)(__in CHAR cVal, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEINT16)(__in SHORT sVal, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEINT32)(__in LONG lVal, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEINT64)(__in LONGLONG llVal, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEINT128)(__in PSDP_LARGE_INTEGER_16 pul16Val, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEUUID16)(__in USHORT usVal, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEUUID32)(__in ULONG ulVal, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEUUID128)(__in const GUID * pUuidVal, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODESTRING)(__in_bcount(stringLength) PCHAR string, __in ULONG stringLength, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEURL)(__in_bcount(urlLength) PCHAR url, __in ULONG urlLength, __in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODEALTERNATIVE)(__in ULONG tag);

__checkReturn
__drv_sameIRQL
__drv_when(return!=0, __drv_allocatesMem(Mem))
typedef PSDP_NODE (*PCREATENODESEQUENCE)(__in ULONG tag);


//
// GUID_BTHDDI_SDP_NODE_INTERFACE
//
typedef struct _BTHDDI_SDP_NODE_INTERFACE {
    INTERFACE           Interface;

    PCREATENODETREEROOT SdpCreateNodeTree;
    PFREETREE           SdpFreeTree;

    PCREATENODENIL      SdpCreateNodeNil;

    PCREATENODEBOOLEAN  SdpCreateNodeBoolean;

    PCREATENODEUINT8    SdpCreateNodeUint8;
    PCREATENODEUINT16   SdpCreateNodeUint16;
    PCREATENODEUINT32   SdpCreateNodeUint32;
    PCREATENODEUINT64   SdpCreateNodeUint64;
    PCREATENODEUINT128  SdpCreateNodeUint128;

    PCREATENODEINT8     SdpCreateNodeInt8;
    PCREATENODEINT16    SdpCreateNodeInt16;
    PCREATENODEINT32    SdpCreateNodeInt32;
    PCREATENODEINT64    SdpCreateNodeInt64;
    PCREATENODEINT128   SdpCreateNodeInt128;

    PCREATENODEUUID16   SdpCreateNodeUuid16;
    PCREATENODEUUID32   SdpCreateNodeUuid32;
    PCREATENODEUUID128  SdpCreateNodeUuid128;

    PCREATENODESTRING   SdpCreateNodeString;

    PCREATENODEURL      SdpCreateNodeUrl;

    PCREATENODEALTERNATIVE SdpCreateNodeAlternative;
    PCREATENODESEQUENCE SdpCreateNodeSequence;

    PADDATTRIBUTETOTREEE SdpAddAttributeToTree;
    PAPPENDNODETOCONTAINERNODE SdpAppendNodeToContainerNode;

} BTHDDI_SDP_NODE_INTERFACE, *PBTHDDI_SDP_NODE_INTERFACE;



__drv_sameIRQL
typedef void (*PBYTESWAPUUID128)(__in GUID *pUuidFrom, __out GUID *pUuiidTo);

__drv_sameIRQL
typedef void (*PBYTESWAPUINT128)(__in PSDP_ULARGE_INTEGER_16 pInUint128,
                                 __out PSDP_ULARGE_INTEGER_16 pOutUint128);

__drv_sameIRQL
typedef ULONGLONG (*PBYTESWAPUINT64)(__in ULONGLONG uint64);

__drv_sameIRQL
typedef void (*PRETRIEVEUUID128)(__in PUCHAR Stream, __out GUID *uuid128);

__drv_sameIRQL
typedef void (*PRETRIEVEUINT128)(__in PUCHAR Stream,
                                 __out PSDP_ULARGE_INTEGER_16 pUint128);

__drv_sameIRQL
typedef void (*PRETRIEVEUINT64)(__in PUCHAR Stream, __out PULONGLONG pUint16);

__checkReturn
__drv_sameIRQL
typedef NTSTATUS (*PVALIDATESTREAM)(__in_bcount(Size) PUCHAR Stream,
                                    __in ULONG Size,
                                    __out PULONG_PTR ErrorByte);

__checkReturn
__drv_sameIRQL
typedef NTSTATUS (*PFINDATTRIBUTEINTREE)(__in PSDP_TREE_ROOT_NODE Tree,
                                         __in USHORT AttribId,
                                         __deref_out PSDP_NODE *AttribValue);

__checkReturn
__drv_sameIRQL
typedef NTSTATUS (*PCONVERTTREETOSTREAM)(__in PSDP_TREE_ROOT_NODE Root,
                                         __post __drv_when(return==0, __drv_allocatesMem(Mem)) PUCHAR *Stream, 
                                         __out PULONG Size, 
                                         __in ULONG tag);

__checkReturn
__drv_sameIRQL
typedef NTSTATUS (*PCONVERTSTREAMTOTREE)(__in_bcount(Size) PUCHAR Stream,
                                         __in ULONG Size,
                                         __out PSDP_TREE_ROOT_NODE *Node,
                                         __in ULONG tag);
__drv_sameIRQL
typedef VOID (*PGETNEXTELEMENT)(__in_bcount(StreamSize) PUCHAR Stream,
                                __in ULONG StreamSize,
                                __in PUCHAR CurrentElement,
                                __deref_out_bcount(*NextElementSize) PUCHAR* NextElement,
                                __out PULONG NextElementSize);

typedef VOID (*pReservedFunction)();


#ifndef __BTHSDPDDIP_H__  

typedef struct _BTHDDI_SDP_PARSE_INTERFACE {
    INTERFACE               Interface;

    PVALIDATESTREAM         SdpValidateStream;

    PCONVERTSTREAMTOTREE    SdpConvertStreamToTree;
    PCONVERTTREETOSTREAM    SdpConvertTreeToStream;
    PFREETREE               SdpFreeTree;
    PBYTESWAPUUID128        SdpByteSwapUuid128;
    PBYTESWAPUINT128        SdpByteSwapUint128;
    PBYTESWAPUINT64         SdpByteSwapUint64;
    PRETRIEVEUUID128        SdpRetrieveUuid128;
    PRETRIEVEUINT128        SdpRetrieveUint128;
    PRETRIEVEUINT64         SdpRetrieveUint64;
    PFINDATTRIBUTEINTREE    SdpFindAttributeInTree;
    PGETNEXTELEMENT         SdpGetNextElement;
    pReservedFunction       Reserved1;
    pReservedFunction       Reserved2;
    pReservedFunction       Reserved3;
    pReservedFunction       Reserved4;
} BTHDDI_SDP_PARSE_INTERFACE, *PBTHDDI_SDP_PARSE_INTERFACE;



#endif


#ifdef __cplusplus
}
#endif 


#endif // (NTDDI_VERSION >= NTDDI_VISTA)

#endif //  __BTHSDPDDI_H__

