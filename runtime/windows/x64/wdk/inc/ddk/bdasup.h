//==========================================================================;
//
//  THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
//  KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
//  PURPOSE.
//
//  Copyright (c) Microsoft Corporation. All rights reserved.
//
//==========================================================================;

#if (NTDDI_VERSION >= NTDDI_WINXP)

#if !defined(_BDATYPES_)
#error BDATYPES.H must be included before BDATOPGY.H
#endif // !defined(_BDATYPES_)

#if !defined(_BDATOPGY_)
#define _BDATOPGY_

#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)


//---------------------------------------------------------------------------
// Common typedefs
//---------------------------------------------------------------------------

#define STDMETHODCALLTYPE       __stdcall

typedef GUID * PGUID;

//===========================================================================
//
//  BDA KS Topology Structures
//
//===========================================================================

typedef struct _KSM_PIN_PAIR
{
    KSMETHOD    Method;
    ULONG       InputPinId;
    ULONG       OutputPinId;
    ULONG       Reserved;
} KSM_PIN_PAIR, * PKSM_PIN_PAIR;

typedef struct _KSM_PIN
{
    KSMETHOD    Method;
    union
    {
        ULONG       PinId;
        ULONG       PinType;
    };
    ULONG       Reserved;
} KSM_PIN, * PKSM_PIN;

typedef ULONG   BDA_TOPOLOGY_JOINT, * PBDA_TOPOLOGY_JOINT;

typedef struct _BDA_PIN_PAIRING
{
    ULONG                   ulInputPin;
    ULONG                   ulOutputPin;
    ULONG                   ulcMaxInputsPerOutput;
    ULONG                   ulcMinInputsPerOutput;
    ULONG                   ulcMaxOutputsPerInput;
    ULONG                   ulcMinOutputsPerInput;
    ULONG                   ulcTopologyJoints;
    const ULONG *           pTopologyJoints;

} BDA_PIN_PAIRING, * PBDA_PIN_PAIRING;


// BDA  Topology Template Structures
//
typedef struct _BDA_FILTER_TEMPLATE
{
    const KSFILTER_DESCRIPTOR *     pFilterDescriptor;
    ULONG                           ulcPinPairs;
    const BDA_PIN_PAIRING *         pPinPairs;

} BDA_FILTER_TEMPLATE,  *PBDA_FILTER_TEMPLATE;


//===========================================================================
//
//  BDA Utility Functions
//
//===========================================================================


/*
**  BdaCreateFilterFactory()
**
**  Creates a Filter Factory according to pFilterDescriptor.  Keeps a
**  reference to pBdaFilterTemplate so that Pin Factories can be dynamically
**  created on a Filter created from this Filter Factory.
**
** Arguments:
**
**
** Returns:
**
**
**
** Side Effects:  none
*/

__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaCreateFilterFactory(
    __in PKSDEVICE                       pKSDevice,
    __in const KSFILTER_DESCRIPTOR *     pFilterDescriptor,
    __in const BDA_FILTER_TEMPLATE *     pBdaFilterTemplate
    );


/*
**  BdaCreateFilterFactoryEx()
**
**  Creates a Filter Factory according to pFilterDescriptor.  Keeps a
**  reference to pBdaFilterTemplate so that Pin Factories can be dynamically
**  created on a Filter created from this Filter Factory.
**
** Arguments:
**
**
** Returns:
**
**
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaCreateFilterFactoryEx(
    __in PKSDEVICE                       pKSDevice,
    __in const KSFILTER_DESCRIPTOR *     pFilterDescriptor,
    __in const BDA_FILTER_TEMPLATE *     pBdaFilterTemplate,
    __out_opt PKSFILTERFACTORY *              ppKSFilterFactory
    );


/*
**  BdaInitFilter()
**
**  Initializes a BDA filter context for this KS Filter instance.  Creates
**  a linkage to the BDA Filter Template associated with the factory from
**  which this KS Filter instance was created.
**
** Arguments:
**
**
** Returns:
**
**
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaInitFilter(
    __in PKSFILTER                       pKSFilter,
    __in const BDA_FILTER_TEMPLATE *     pBdaFilterTemplate
    );


/*
**  BdaUninitFilter()
**
**  Unitializes and frees resources from the BDA filter context associated
**  with this KS filter instance.
**
** Arguments:
**
**
** Returns:
**
**
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaUninitFilter(
    __in PKSFILTER                       pKSFilter
    );


/*
**  BdaFilterFactoryUpdateCacheData()
**
**  Updates the pin data cache for the given filter factory.
**  The function will update the cached information for all pin factories
**  exposed by the given filter factory.  
**  
**  If the option filter descriptor is given, the function will update
**  the pin data cache for all pins listed in the given filter descriptor
**  instead of those in the filter factory.
**
**  Drivers will call this to update the pin data cache for all
**  pins that may be exposed by the filter factory.  The driver will
**  provide a filter descriptor listing pins that are not initially exposed
**  by the filter factory (this is usually the same as the template filter
**  descriptor).
**
** Arguments:
**
**
** Returns:
**
**
**
** Side Effects:  none
*/

__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaFilterFactoryUpdateCacheData(
    __in PKSFILTERFACTORY             pFilterFactory,
    __in const KSFILTER_DESCRIPTOR *  pFilterDescriptor OPTIONAL
    );


/*
**  BdaCreatePin()
**
**      Utility function creates a new pin in the given filter instance.
**
**
**  Arguments:
**
**
**  Returns:
**
**
** Side Effects:  none
*/

__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaCreatePin(
    __in PKSFILTER                   pKSFilter,
    __in ULONG                       ulPinType,
    __out_opt PULONG                      pulPinId
    );


/*
**  BdaDeletePin()
**
**      Utility function deletes a pin from the given filter instance.
**
**
**  Arguments:
**
**
**  Returns:
**
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaDeletePin(
    __in PKSFILTER                   pKSFilter,
    __out_opt PULONG                      pulPinId
    );


/*
**  BdaCreateTopology()
**
**      Utility function creates the topology between two pins.
**
**
**  Arguments:
**
**
**  Returns:
**
**      NULL            If no valid pin pairing exists with the
**                      given input and output pins.
**
** Side Effects:  none
*/

__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaCreateTopology(
    __in PKSFILTER                   pKSFilter,
    __in ULONG                       InputPinId,
    __in ULONG                       OutputPinId
    );



//===========================================================================
//
//  BDA Property and Method Functions
//
//===========================================================================


/*
** BdaPropertyNodeTypes ()
**
**    Returns a list of ULONGs.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaPropertyNodeTypes(
    __in PIRP         pIrp,
    __in PKSPROPERTY  pKSProperty,
    __out_bcount(OutputBufferLenFromIrp(Irp)) ULONG *     pulProperty
    );


/*
** BdaPropertyPinTypes ()
**
**    Returns a list of GUIDS.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaPropertyPinTypes(
    __in PIRP         pIrp,
    __in PKSPROPERTY  pKSProperty,
    __out_bcount(OutputBufferLenFromIrp(Irp)) ULONG *     pulProperty
    );


/*
** BdaPropertyTemplateConnections ()
**
**    Returns a list of KSTOPOLOGY_CONNECTIONS.  The list of connections
**    describs how pin types and node types are connected in the template
**    topology
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaPropertyTemplateConnections(
    __in PIRP                     pIrp,
    __in PKSPROPERTY              pKSProperty,
    __out_opt PKSTOPOLOGY_CONNECTION  pConnectionProperty
    );


/*
** BdaPropertyNodeProperties ()
**
**    Returns a list of GUIDs.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaPropertyNodeProperties(
    __in PIRP         pIrp,
    __in PKSP_NODE    pKSProperty,
    __out_opt GUID *      pguidProperty
    );


/*
** BdaPropertyNodeMethods ()
**
**    Returns a list of GUIDs.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaPropertyNodeMethods(
    __in PIRP         pIrp,
    __in PKSP_NODE    pKSProperty,
    __out_opt GUID *      pguidProperty
    );


/*
** BdaPropertyNodeEvents ()
**
**    Returns a list of GUIDs.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaPropertyNodeEvents(
    __in PIRP         pIrp,
    __in PKSP_NODE    pKSProperty,
    __out_opt GUID *      pguidProperty
    );


/*
** BdaPropertyNodeDescriptors ()
**
**    Returns a list of BDA Node Descriptors.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaPropertyNodeDescriptors(
    __in PIRP                     pIrp,
    __in PKSPROPERTY              pKSProperty,
   __out_opt BDANODE_DESCRIPTOR *    pNodeDescriptorProperty
    );


/*
** BdaPropertyGetControllingPinId ()
**
**    Gets the ID of the pin on which to submit node properties, methods
**    and events.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaPropertyGetControllingPinId(
    __in PIRP                     Irp,
    __in PKSP_BDA_NODE_PIN        Property,
   __out_opt PULONG                  pulControllingPinId
    );


/*
** BdaStartChanges ()
**
**    Starts a new set of BDA topology changes.  All changes to BDA topology
**    that have not been committed are ignored.  Changes after this will be
**    in effect only after BdaCommitChanges.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaStartChanges(
    __in PIRP         pIrp
    );


/*
** BdaCheckChanges ()
**
**    Checks the changes to BDA topology that have occured since the
**    last BdaStartChanges.  Returns the result that would have occurred if
**    CommitChanges had been called.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaCheckChanges(
    __in PIRP         pIrp
    );


/*
** BdaCommitChanges ()
**
**    Commits the changes to BDA topology that have occured since the
**    last BdaStartChanges.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaCommitChanges(
    __in PIRP         pIrp
    );


/*
** BdaGetChangeState ()
**
**    Returns the current change state of the BDA topology.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaGetChangeState(
    __in PIRP             pIrp,
    __out_opt PBDA_CHANGE_STATE   pChangeState
    );


/*
** BdaMethodCreatePin ()
**
**    Creates a new pin factory for the given pin type.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaMethodCreatePin(
    __in PIRP         pIrp,
    __in PKSMETHOD    pKSMethod,
   __out_opt PULONG      pulPinFactoryID
    );


/*
** BdaMethodDeletePin ()
**
**    Deletes the given pin factory
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaMethodDeletePin(
    __in PIRP         pIrp,
    __in PKSMETHOD    pKSMethod,
    PVOID  pvIgnored
    );


/*
** BdaMethodCreateTopology ()
**
**    Creates the topology between the two given pin factories.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaMethodCreateTopology(
    __in PIRP         pIrp,
    __in PKSMETHOD    pKSMethod,
    OPTIONAL PVOID  pvIgnored
    );


/*
** BdaPropertyGetPinControl ()
**
**    Returns a the BDA ID or BDA Template Type of the Pin.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaPropertyGetPinControl(
    __in PIRP         Irp,
    __in PKSPROPERTY  Property,
   __out_opt ULONG *     pulProperty
    );


/*
** BdaValidateNodeProperty ()
**
**    Validates that the node property belongs to the current pin.
**
** Arguments:
**
**
** Returns:
**
** Side Effects:  none
*/
__checkReturn
__drv_requiresIRQL(PASSIVE_LEVEL)
STDMETHODIMP_(NTSTATUS)
BdaValidateNodeProperty(
    __in PIRP         pIrp,
    __in PKSPROPERTY  pProperty
    );


#if defined(__cplusplus)
}
#endif // defined(__cplusplus)

#endif // !defined(_BDATOPGY_)

#endif // (NTDDI_VERSION >= NTDDI_WINXP)


