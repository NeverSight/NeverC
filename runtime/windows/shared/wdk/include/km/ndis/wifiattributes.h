// Copyright (C) Microsoft Corporation. All rights reserved.
#pragma once

#include <windot11.h>

#define NDIS_MINIPORT_ADAPTER_802_11_ATTRIBUTES_REVISION_1     1
#define NDIS_MINIPORT_ADAPTER_802_11_ATTRIBUTES_REVISION_2     2
#define NDIS_MINIPORT_ADAPTER_802_11_ATTRIBUTES_REVISION_3     3

typedef  struct _NDIS_MINIPORT_ADAPTER_NATIVE_802_11_ATTRIBUTES
{
    NDIS_OBJECT_HEADER Header;

    ULONG                    OpModeCapability;
    ULONG                    NumOfTXBuffers;
    ULONG                    NumOfRXBuffers;
    BOOLEAN                  MultiDomainCapabilityImplemented;
    ULONG                    NumSupportedPhys;
#ifdef __midl
    [size_is(NumSupportedPhys)]
#endif
    PDOT11_PHY_ATTRIBUTES    SupportedPhyAttributes;

    // Attributes specific to the operation modes
    PDOT11_EXTSTA_ATTRIBUTES ExtSTAAttributes;


#if (NDIS_SUPPORT_NDIS620)
    // virtual wifi specific attributes
    PDOT11_VWIFI_ATTRIBUTES VWiFiAttributes;
    // Ext AP specific attributes
    PDOT11_EXTAP_ATTRIBUTES ExtAPAttributes;
#endif // (NDIS_SUPPORT_NDIS620)

#if (NDIS_SUPPORT_NDIS630)
    PDOT11_WFD_ATTRIBUTES   WFDAttributes;

#endif // (NDIS_SUPPORT_NDIS630)

}NDIS_MINIPORT_ADAPTER_NATIVE_802_11_ATTRIBUTES,
  *PNDIS_MINIPORT_ADAPTER_NATIVE_802_11_ATTRIBUTES;

#define NDIS_SIZEOF_MINIPORT_ADAPTER_NATIVE_802_11_ATTRIBUTES_REVISION_1     \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_MINIPORT_ADAPTER_NATIVE_802_11_ATTRIBUTES, ExtSTAAttributes)

#if (NDIS_SUPPORT_NDIS620)

#define NDIS_SIZEOF_MINIPORT_ADAPTER_NATIVE_802_11_ATTRIBUTES_REVISION_2     \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_MINIPORT_ADAPTER_NATIVE_802_11_ATTRIBUTES, ExtAPAttributes)

#endif // (NDIS_SUPPORT_NDIS620)

#if (NDIS_SUPPORT_NDIS630)

#define NDIS_SIZEOF_MINIPORT_ADAPTER_NATIVE_802_11_ATTRIBUTES_REVISION_3     \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_MINIPORT_ADAPTER_NATIVE_802_11_ATTRIBUTES, WFDAttributes)

#endif // (NDIS_SUPPORT_NDIS630)
