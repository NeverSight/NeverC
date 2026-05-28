#ifndef _npivwmi_h_
#define _npivwmi_h_

// MSFC_FibrePortNPIVMethods - MSFC_FibrePortNPIVMethods
#define MSFC_FibrePortNPIVMethodsGuid \
    { 0x8d49ef4c,0xc172,0x45d8, { 0xa3,0x03,0xc5,0xfb,0xc5,0x60,0x1f,0x37 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_FibrePortNPIVMethods_GUID, \
            0x8d49ef4c,0xc172,0x45d8,0xa3,0x03,0xc5,0xfb,0xc5,0x60,0x1f,0x37);
#endif

//
// Method id definitions for MSFC_FibrePortNPIVMethods
#define CreateVirtualPort     1
typedef struct _CreateVirtualPort_IN
{
    // 
    UCHAR WWPN[8];
    #define CreateVirtualPort_IN_WWPN_SIZE sizeof(UCHAR[8])
    #define CreateVirtualPort_IN_WWPN_ID 2

    // 
    UCHAR WWNN[8];
    #define CreateVirtualPort_IN_WWNN_SIZE sizeof(UCHAR[8])
    #define CreateVirtualPort_IN_WWNN_ID 3

    // 
    UCHAR Tag[16];
    #define CreateVirtualPort_IN_Tag_SIZE sizeof(UCHAR[16])
    #define CreateVirtualPort_IN_Tag_ID 4

    // 
    USHORT VirtualName[64];
    #define CreateVirtualPort_IN_VirtualName_SIZE sizeof(USHORT[64])
    #define CreateVirtualPort_IN_VirtualName_ID 5

} CreateVirtualPort_IN, *PCreateVirtualPort_IN;

#define CreateVirtualPort_IN_SIZE (FIELD_OFFSET(CreateVirtualPort_IN, VirtualName) + CreateVirtualPort_IN_VirtualName_SIZE)

typedef struct _CreateVirtualPort_OUT
{
    // 
    ULONG Status;
    #define CreateVirtualPort_OUT_Status_SIZE sizeof(ULONG)
    #define CreateVirtualPort_OUT_Status_ID 1

} CreateVirtualPort_OUT, *PCreateVirtualPort_OUT;

#define CreateVirtualPort_OUT_SIZE (FIELD_OFFSET(CreateVirtualPort_OUT, Status) + CreateVirtualPort_OUT_Status_SIZE)

#define RemoveVirtualPort     2
typedef struct _RemoveVirtualPort_IN
{
    // 
    UCHAR WWPN[8];
    #define RemoveVirtualPort_IN_WWPN_SIZE sizeof(UCHAR[8])
    #define RemoveVirtualPort_IN_WWPN_ID 2

} RemoveVirtualPort_IN, *PRemoveVirtualPort_IN;

#define RemoveVirtualPort_IN_SIZE (FIELD_OFFSET(RemoveVirtualPort_IN, WWPN) + RemoveVirtualPort_IN_WWPN_SIZE)

typedef struct _RemoveVirtualPort_OUT
{
    // 
    ULONG Status;
    #define RemoveVirtualPort_OUT_Status_SIZE sizeof(ULONG)
    #define RemoveVirtualPort_OUT_Status_ID 1

} RemoveVirtualPort_OUT, *PRemoveVirtualPort_OUT;

#define RemoveVirtualPort_OUT_SIZE (FIELD_OFFSET(RemoveVirtualPort_OUT, Status) + RemoveVirtualPort_OUT_Status_SIZE)


// MSFC_VirtualFibrePortAttributes - MSFC_VirtualFibrePortAttributes
#define MSFC_VirtualFibrePortAttributesGuid \
    { 0x3574bc0b,0x6f5f,0x4baf, { 0xaf,0x67,0xc4,0x8c,0xc7,0x9f,0xe9,0x57 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_VirtualFibrePortAttributes_GUID, \
            0x3574bc0b,0x6f5f,0x4baf,0xaf,0x67,0xc4,0x8c,0xc7,0x9f,0xe9,0x57);
#endif


typedef struct _MSFC_VirtualFibrePortAttributes
{
    // 
    ULONG Status;
    #define MSFC_VirtualFibrePortAttributes_Status_SIZE sizeof(ULONG)
    #define MSFC_VirtualFibrePortAttributes_Status_ID 1

    // 
    ULONG FCId;
    #define MSFC_VirtualFibrePortAttributes_FCId_SIZE sizeof(ULONG)
    #define MSFC_VirtualFibrePortAttributes_FCId_ID 2

    // 
    USHORT VirtualName[64];
    #define MSFC_VirtualFibrePortAttributes_VirtualName_SIZE sizeof(USHORT[64])
    #define MSFC_VirtualFibrePortAttributes_VirtualName_ID 3

    // 
    UCHAR Tag[16];
    #define MSFC_VirtualFibrePortAttributes_Tag_SIZE sizeof(UCHAR[16])
    #define MSFC_VirtualFibrePortAttributes_Tag_ID 4

    // 
    UCHAR WWPN[8];
    #define MSFC_VirtualFibrePortAttributes_WWPN_SIZE sizeof(UCHAR[8])
    #define MSFC_VirtualFibrePortAttributes_WWPN_ID 5

    // 
    UCHAR WWNN[8];
    #define MSFC_VirtualFibrePortAttributes_WWNN_SIZE sizeof(UCHAR[8])
    #define MSFC_VirtualFibrePortAttributes_WWNN_ID 6

    // 
    UCHAR FabricWWN[8];
    #define MSFC_VirtualFibrePortAttributes_FabricWWN_SIZE sizeof(UCHAR[8])
    #define MSFC_VirtualFibrePortAttributes_FabricWWN_ID 7

} MSFC_VirtualFibrePortAttributes, *PMSFC_VirtualFibrePortAttributes;

#define MSFC_VirtualFibrePortAttributes_SIZE (FIELD_OFFSET(MSFC_VirtualFibrePortAttributes, FabricWWN) + MSFC_VirtualFibrePortAttributes_FabricWWN_SIZE)

// MSFC_FibrePortNPIVAttributes - MSFC_FibrePortNPIVAttributes
#define MSFC_FibrePortNPIVAttributesGuid \
    { 0x66902796,0x54f4,0x4e24, { 0xad,0x91,0x0a,0xb3,0xb1,0xad,0x65,0xaf } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_FibrePortNPIVAttributes_GUID, \
            0x66902796,0x54f4,0x4e24,0xad,0x91,0x0a,0xb3,0xb1,0xad,0x65,0xaf);
#endif


typedef struct _MSFC_FibrePortNPIVAttributes
{
    // 
    UCHAR WWPN[8];
    #define MSFC_FibrePortNPIVAttributes_WWPN_SIZE sizeof(UCHAR[8])
    #define MSFC_FibrePortNPIVAttributes_WWPN_ID 1

    // 
    UCHAR WWNN[8];
    #define MSFC_FibrePortNPIVAttributes_WWNN_SIZE sizeof(UCHAR[8])
    #define MSFC_FibrePortNPIVAttributes_WWNN_ID 2

    // 
    ULONG NumberVirtualPorts;
    #define MSFC_FibrePortNPIVAttributes_NumberVirtualPorts_SIZE sizeof(ULONG)
    #define MSFC_FibrePortNPIVAttributes_NumberVirtualPorts_ID 3

    // 
    MSFC_VirtualFibrePortAttributes VirtualPorts[1];
    #define MSFC_FibrePortNPIVAttributes_VirtualPorts_ID 4

} MSFC_FibrePortNPIVAttributes, *PMSFC_FibrePortNPIVAttributes;

// MSFC_NPIVLUNMappingInformation - MSFC_NPIVLUNMappingInformation
#define MSFC_NPIVLUNMappingInformationGuid \
    { 0x21763208,0xfeb5,0x49bc, { 0xa4,0xe6,0xf5,0x6f,0xdd,0x8c,0xcb,0xd4 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(MSFC_NPIVLUNMappingInformation_GUID, \
            0x21763208,0xfeb5,0x49bc,0xa4,0xe6,0xf5,0x6f,0xdd,0x8c,0xcb,0xd4);
#endif


typedef struct _MSFC_NPIVLUNMappingInformation
{
    // 
    UCHAR WWPNVirtualPort[8];
    #define MSFC_NPIVLUNMappingInformation_WWPNVirtualPort_SIZE sizeof(UCHAR[8])
    #define MSFC_NPIVLUNMappingInformation_WWPNVirtualPort_ID 1

    // 
    UCHAR WWPNPhysicalPort[8];
    #define MSFC_NPIVLUNMappingInformation_WWPNPhysicalPort_SIZE sizeof(UCHAR[8])
    #define MSFC_NPIVLUNMappingInformation_WWPNPhysicalPort_ID 2

    // 
    UCHAR OSBus;
    #define MSFC_NPIVLUNMappingInformation_OSBus_SIZE sizeof(UCHAR)
    #define MSFC_NPIVLUNMappingInformation_OSBus_ID 3

    // 
    UCHAR OSTarget;
    #define MSFC_NPIVLUNMappingInformation_OSTarget_SIZE sizeof(UCHAR)
    #define MSFC_NPIVLUNMappingInformation_OSTarget_ID 4

    // 
    UCHAR OSLUN;
    #define MSFC_NPIVLUNMappingInformation_OSLUN_SIZE sizeof(UCHAR)
    #define MSFC_NPIVLUNMappingInformation_OSLUN_ID 5

} MSFC_NPIVLUNMappingInformation, *PMSFC_NPIVLUNMappingInformation;

#define MSFC_NPIVLUNMappingInformation_SIZE (FIELD_OFFSET(MSFC_NPIVLUNMappingInformation, OSLUN) + MSFC_NPIVLUNMappingInformation_OSLUN_SIZE)

#endif

