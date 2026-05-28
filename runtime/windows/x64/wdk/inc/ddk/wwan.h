/*++
Copyright (c) 2007  Microsoft Corporation

Module Name:
    wwan.h

Abstract:
    Header file for WWAN structures

Revision History:
    DATE			DESCRIPTION
    ------------		-----------
    23-FEB-2007		V0.40 Driver Model Compliant
    23-MAY-2007		V0.50 Driver Model Compliant
    01-AUG-2007		V0.60 Driver Model Compliant
    02-APR-2008		V1.00 Driver Model Compliant

--*/

#ifndef __WWAN_DECL__
#define __WWAN_DECL__

#define WWAN_ERROR_UNSUPPORTED_FIRMWARE		0xf0f0f000
#define WWAN_ERROR_COM_PORT_CONFLICT		0xf0f0f001
#define WWAN_ERROR_RESOURCE_CONFLICT_OTHER 	0xf0f0ffff

typedef ULONG WWAN_STATUS;

#define WWAN_STATUS_SUCCESS			STATUS_SUCCESS
#define WWAN_STATUS_BUSY			0xC0040002
#define WWAN_STATUS_FAILURE			0xC0040003
#define WWAN_STATUS_SIM_NOT_INSERTED		0xC0040004
#define WWAN_STATUS_BAD_SIM			0xC0040005
#define WWAN_STATUS_PIN_REQUIRED		0xC0040006
#define WWAN_STATUS_PIN_DISABLED		0x40040007
#define WWAN_STATUS_NOT_REGISTERED			0x40040008
#define WWAN_STATUS_PROVIDERS_NOT_FOUND		0x40040009
#define WWAN_STATUS_NO_DEVICE_SUPPORT           0xC004000a
#define WWAN_STATUS_PROVIDER_NOT_VISIBLE	0x4004000b
#define WWAN_STATUS_DATA_CLASS_NOT_AVAILABLE    0x4004000c
#define WWAN_STATUS_PACKET_SVC_DETACHED		0xC004000d
#define WWAN_STATUS_MAX_ACTIVATED_CONTEXTS	0xC004000e
#define WWAN_STATUS_NOT_INITIALIZED		    0xC004000f
#define WWAN_STATUS_VOICE_CALL_IN_PROGRESS	0x40040010
#define WWAN_STATUS_CONTEXT_NOT_ACTIVATED	0xC0040011
#define WWAN_STATUS_SERVICE_NOT_ACTIVATED	0xC0040012
#define WWAN_STATUS_INVALID_ACCESS_STRING	0xC0040013
#define WWAN_STATUS_INVALID_USER_NAME_PWD	0xC0040014
#define WWAN_STATUS_RADIO_POWER_OFF		0xC0040015 
#define WWAN_STATUS_INVALID_PARAMETERS		0xC0040016
#define WWAN_STATUS_READ_FAILURE		0xC0040017
#define WWAN_STATUS_WRITE_FAILURE		0xC0040018

//SMS specific error codes
#define WWAN_STATUS_SMS_OPERATION_NOT_ALLOWED	0xC0040100
#define WWAN_STATUS_SMS_MEMORY_FAILURE		0xC0040101
#define WWAN_STATUS_SMS_INVALID_MEMORY_INDEX	0xC0040102
#define WWAN_STATUS_SMS_UNKNOWN_SMSC_ADDRESS	0xC0040103
#define WWAN_STATUS_SMS_NETWORK_TIMEOUT		0xC0040104
#define WWAN_STATUS_SMS_MEMORY_FULL		0xC0040105
#define WWAN_STATUS_SMS_UNKNOWN_ERROR		0xC0040106
#define WWAN_STATUS_SMS_FILTER_NOT_SUPPORTED	0xC0040107
#define WWAN_STATUS_SMS_MORE_DATA		0x40040108
#define WWAN_STATUS_SMS_LANG_NOT_SUPPORTED 		0xC0040109
#define WWAN_STATUS_SMS_ENCODING_NOT_SUPPORTED 		0xC004010A
#define WWAN_STATUS_SMS_FORMAT_NOT_SUPPORTED		0xC004010B

typedef enum _WWAN_STRUCT_TYPE {
    WwanStructTN = 0,
    WwanStructContext,
    WwanStructProvider,
    WwanStructSmsPdu,
    WwanStructReserved0,
    WwanStructReserved1,
    WwanStructReserved2,
    WwanStructSmsCdma,
    WwanStructReserved3,
    WwanStructMax
} WWAN_STRUCT_TYPE, *PWWAN_STRUCT_TYPE;

typedef struct _WWAN_LIST_HEADER {
    WWAN_STRUCT_TYPE	ElementType;
    ULONG		ElementCount;
} WWAN_LIST_HEADER, *PWWAN_LIST_HEADER;

#define WWAN_MANUFACTURER_LEN			32
#define WWAN_MODEL_LEN				32
#define WWAN_FIRMWARE_LEN			32
#define WWAN_DEVICEID_LEN			18
#define WWAN_CUSTOM_DATA_CLASS_LEN	        12
#define WWAN_CUSTOM_BAND_CLASS_LEN	        20
#define WWAN_PIN_LEN				12
#define WWAN_PROVIDERID_LEN			7
#define WWAN_PROVIDERNAME_LEN			21
#define WWAN_ROAMTEXT_LEN			64
#define WWAN_ACCESSSTRING_LEN			101
#define WWAN_USERNAME_LEN			256
#define WWAN_PASSWORD_LEN			256
#define WWAN_SUBSCRIBERID_LEN			16
#define WWAN_SIMICCID_LEN			21
#define WWAN_TN_LEN				16
#define WWAN_SCA_MAX_LEN			14
#define WWAN_SMS_ADDRESS_MAX_LEN		16
#define WWAN_SC_TIME_STAMP_MAX_LEN		20
#define WWAN_SMS_CDMA_ADDR_MAX_LEN		50
#define WWAN_SMS_CDMA_TIMESTAMP_MAX_LEN         20
#define WWAN_SMS_MSG_PDU_LEN			183
#define WWAN_SMS_RAW_PDU_LEN			(WWAN_SMS_MSG_PDU_LEN - 12)
#define WWAN_SMS_PDU_HEX_BUF_LEN		(WWAN_SMS_MSG_PDU_LEN * 2)
#define WWAN_CDMA_SHORT_MSG_SIZE_UNKNOWN	0
#define WWAN_CDMA_SHORT_MSG_SIZE_MAX		160
#define WWAN_SMS_CDMA_MAX_BUF_LEN		160
#define WWAN_SMS_CDMA_MAX_MSG_LEN		WWAN_CDMA_SHORT_MSG_SIZE_MAX

#define WWAN_CDMA_DEFAULT_PROVIDER_ID	(0)

typedef enum _WWAN_ASYNC_GETSET_TYPE {
    WwanAsyncGetDeviceCaps = 0,
    WwanAsyncGetReadyInfo,
    WwanAsyncGetRadioState,
    WwanAsyncSetRadioState,
    WwanAsyncGetPin,
    WwanAsyncSetPin,
    WwanAsyncGetPinList,
    WwanAsyncGetHomeProvider,
    WwanAsyncGetPreferredProviders,
    WwanAsyncSetPreferredProviders,
    WwanAsyncGetVisibleProviders,
    WwanAsyncGetRegisterState,
    WwanAsyncSetRegisterState,
    WwanAsyncGetPacketService,
    WwanAsyncSetPacketService,
    WwanAsyncGetSignalState,
    WwanAsyncSetSignalState,
    WwanAsyncGetConnect,
    WwanAsyncSetConnect,
    WwanAsyncGetProvisionedContexts,
    WwanAsyncSetProvisionedContext,
    WwanAsyncSetServiceActivation,
    WwanAsyncGetSmsConfiguration,
    WwanAsyncSetSmsConfiguration,
    WwanAsyncSmsRead,
    WwanAsyncSmsSend,
    WwanAsyncSmsDelete,
    WwanAsyncSmsStatus,
    WwanAsyncSetVendorSpecific,
    WWAN_ASYNC_GETSET_TYPE_MAX
} WWAN_ASYNC_GETSET_TYPE, *PWWAN_ASYNC_GETSET_TYPE;

#ifndef WWAN_MAJOR_VERSION
#define WWAN_MAJOR_VERSION	1
#endif

#ifndef WWAN_MINOR_VERSION
#define WWAN_MINOR_VERSION	0
#endif

#ifndef WWAN_CURRENT_VERSION
#define WWAN_CURRENT_VERSION	\
    ((WWAN_MAJOR_VERSION << 16) | WWAN_MINOR_VERSION)
#endif

typedef ULONG WWAN_VERSION;         /* A value specifies the version. */
                                    /* bit[16:31]: major version      */
                                    /* bit[0:15]:  minor version      */

#define WWAN_DRIVER_CAPS_NONE		0x00000000

typedef struct _WWAN_DRIVER_CAPS {
    ULONG ulMajorVersion;
    ULONG ulMinorVersion;
    ULONG ulDriverCaps;
} WWAN_DRIVER_CAPS, *PWWAN_DRIVER_CAPS;

typedef enum _WWAN_DEVICE_TYPE {
    WwanDeviceTypeUnknown = 0,
    WwanDeviceTypeEmbedded,
    WwanDeviceTypeRemovable,
    WwanDeviceTypeRemote,
    WwanDeviceTypeMax
} WWAN_DEVICE_TYPE, *PWWAN_DEVICE_TYPE;

typedef enum _WWAN_CELLULAR_CLASS {
    WwanCellularClassUnknown = 0,
    WwanCellularClassGsm,
    WwanCellularClassCdma,
    WwanCellularClassMax
} WWAN_CELLULAR_CLASS, *PWWAN_CELLULAR_CLASS;

typedef enum _WWAN_VOICE_CLASS {
    WwanVoiceClassUnknown = 0,
    WwanVoiceClassNoVoice,
    WwanVoiceClassSeparateVoiceData,
    WwanVoiceClassSimultaneousVoiceData,
    WwanVoiceClassMax
} WWAN_VOICE_CLASS, *PWWAN_VOICE_CLASS;

typedef enum _WWAN_SIM_CLASS {
    WwanSimClassUnknown = 0,
    WwanSimClassSimLogical,
    WwanSimClassSimRemovable,
    WwanSimClassSimRemote,
    WwanSimClassMax
} WWAN_SIM_CLASS, *PWWAN_SIM_CLASS;

#define WWAN_DATA_CLASS_NONE		0x00000000
#define WWAN_DATA_CLASS_GPRS		0x00000001
#define WWAN_DATA_CLASS_EDGE		0x00000002 /* EGPRS */
#define WWAN_DATA_CLASS_UMTS		0x00000004
#define WWAN_DATA_CLASS_HSDPA		0x00000008
#define WWAN_DATA_CLASS_HSUPA		0x00000010
#define WWAN_DATA_CLASS_LTE		0x00000020
#define WWAN_DATA_CLASS_1XRTT		0x00010000
#define WWAN_DATA_CLASS_1XEVDO		0x00020000
#define WWAN_DATA_CLASS_1XEVDO_REVA	0x00040000
#define WWAN_DATA_CLASS_1XEVDV		0x00080000
#define WWAN_DATA_CLASS_3XRTT		0x00100000
#define WWAN_DATA_CLASS_1XEVDO_REVB	0x00200000 /* for future use */
#define WWAN_DATA_CLASS_UMB		0x00400000
#define WWAN_DATA_CLASS_CUSTOM	        0x80000000

#define WWAN_BAND_CLASS_UNKNOWN   	0x00000000
#define WWAN_BAND_CLASS_0		0x00000001
#define WWAN_BAND_CLASS_I		0x00000002
#define WWAN_BAND_CLASS_II		0x00000004
#define WWAN_BAND_CLASS_III		0x00000008
#define WWAN_BAND_CLASS_IV		0x00000010
#define WWAN_BAND_CLASS_V		0x00000020
#define WWAN_BAND_CLASS_VI		0x00000040
#define WWAN_BAND_CLASS_VII		0x00000080
#define WWAN_BAND_CLASS_VIII		0x00000100
#define WWAN_BAND_CLASS_IX		0x00000200
#define WWAN_BAND_CLASS_X		0x00000400
#define WWAN_BAND_CLASS_XI		0x00000800
#define WWAN_BAND_CLASS_XII		0x00001000
#define WWAN_BAND_CLASS_XIII		0x00002000
#define WWAN_BAND_CLASS_XIV		0x00004000
#define WWAN_BAND_CLASS_XV		0x00008000
#define WWAN_BAND_CLASS_XVI		0x00010000
#define WWAN_BAND_CLASS_XVII		0x00020000
#define WWAN_BAND_CLASS_CUSTOM	        0x80000000

#define WWAN_CTRL_CAPS_NONE		0x00000000
#define WWAN_CTRL_CAPS_REG_MANUAL	0x00000001
#define WWAN_CTRL_CAPS_HW_RADIO_SWITCH	0x00000002
#define WWAN_CTRL_CAPS_CDMA_MOBILE_IP	0x00000004
#define WWAN_CTRL_CAPS_CDMA_SIMPLE_IP	0x00000008
#define WWAN_CTRL_CAPS_PROTECT_UNIQUEID 0x00000010

#define WWAN_SMS_CAPS_NONE		0x00000000
#define WWAN_SMS_CAPS_PDU_RECEIVE	0x00000001
#define WWAN_SMS_CAPS_PDU_SEND		0x00000002
#define WWAN_SMS_CAPS_TEXT_RECEIVE	0x00000004
#define WWAN_SMS_CAPS_TEXT_SEND		0x00000008

typedef struct _WWAN_DEVICE_CAPS {
    WWAN_DEVICE_TYPE    WwanDeviceType;
    WWAN_CELLULAR_CLASS WwanCellularClass;
    WWAN_VOICE_CLASS    WwanVoiceClass;
    WWAN_SIM_CLASS      WwanSimClass;
    ULONG               WwanDataClass;
    WCHAR               CustomDataClass[WWAN_CUSTOM_DATA_CLASS_LEN];
    ULONG		WwanGsmBandClass;
    ULONG		WwanCdmaBandClass;
    WCHAR               CustomBandClass[WWAN_CUSTOM_BAND_CLASS_LEN];
    ULONG               WwanSmsCaps;
    ULONG               WwanControlCaps;
    WCHAR               DeviceId [WWAN_DEVICEID_LEN];
    WCHAR               Manufacturer [WWAN_MANUFACTURER_LEN];
    WCHAR               Model [WWAN_MODEL_LEN];
    WCHAR               FirmwareInfo [WWAN_FIRMWARE_LEN];
    ULONG               MaxActivatedContexts;
} WWAN_DEVICE_CAPS, *PWWAN_DEVICE_CAPS;

typedef enum _WWAN_READY_STATE {
    WwanReadyStateOff = 0,              /* stack is off                    */
    WwanReadyStateInitialized,          /* ready to power up and register  */
    WwanReadyStateSimNotInserted,       /* SIM not inserted                */
    WwanReadyStateBadSim,               /* SIM is invalid                  */
    WwanReadyStateFailure,              /* Device failure                  */
    WwanReadyStateNotActivated,         /* Device not activated (CDMA)     */
    WwanReadyStateDeviceLocked		/* Device is locked		   */
} WWAN_READY_STATE, *PWWAN_READY_STATE;

typedef enum _WWAN_EMERGENCY_MODE
{
    WwanEmergencyModeOff = 0,
    WwanEmergencyModeOn,
    WwanEmergencyModeMax
}WWAN_EMERGENCY_MODE, *PWWAN_EMERGENCY_MODE;

typedef struct _WWAN_READY_INFO {
    WWAN_READY_STATE	ReadyState;
    WWAN_EMERGENCY_MODE EmergencyMode;
    WCHAR		SubscriberId [WWAN_SUBSCRIBERID_LEN];
    WCHAR		SimIccId [WWAN_SIMICCID_LEN];
    BYTE		CdmaShortMsgSize;
    WWAN_LIST_HEADER	TNListHeader;
} WWAN_READY_INFO, *PWWAN_READY_INFO;

typedef struct _WWAN_SERVICE_ACTIVATION {
    ULONG uVendorSpecificBufferSize;
} WWAN_SERVICE_ACTIVATION, *PWWAN_SERVICE_ACTIVATION;

typedef enum _WWAN_RADIO {
    WwanRadioOff = 0,
    WwanRadioOn
} WWAN_RADIO, *PWWAN_RADIO;

typedef struct _WWAN_RADIO_STATE {
    WWAN_RADIO	HwRadioState;
    WWAN_RADIO	SwRadioState;
} WWAN_RADIO_STATE, *PWWAN_RADIO_STATE;

typedef enum _WWAN_PIN_TYPE {
    WwanPinTypeNone = 0,
    WwanPinTypeCustom,
    WwanPinTypePin1,
    WwanPinTypePin2,
    WwanPinTypeDeviceSimPin,
    WwanPinTypeDeviceFirstSimPin,
    WwanPinTypeNetworkPin,
    WwanPinTypeNetworkSubsetPin,
    WwanPinTypeSvcProviderPin,
    WwanPinTypeCorporatePin,
    WwanPinTypeSubsidyLock,
    WwanPinTypePuk1,
    WwanPinTypePuk2,
    WwanPinTypeDeviceFirstSimPuk,
    WwanPinTypeNetworkPuk,
    WwanPinTypeNetworkSubsetPuk,
    WwanPinTypeSvcProviderPuk,
    WwanPinTypeCorporatePuk,
    WwanPinTypeMax
} WWAN_PIN_TYPE, *PWWAN_PIN_TYPE;

typedef enum _WWAN_PIN_STATE {
    WwanPinStateNone = 0,
    WwanPinStateEnter,
    WwanPinStateMax
} WWAN_PIN_STATE, *PWWAN_PIN_STATE;

#define WWAN_ATTEMPTS_REMAINING_UNKNOWN		~0

typedef struct _WWAN_PIN_INFO {
    WWAN_PIN_TYPE	PinType;
    WWAN_PIN_STATE	PinState;
    ULONG		AttemptsRemaining;
} WWAN_PIN_INFO, *PWWAN_PIN_INFO;

typedef enum _WWAN_PIN_OPERATION {
    WwanPinOperationEnter = 0,
    WwanPinOperationEnable,
    WwanPinOperationDisable,
    WwanPinOperationChange,
    WwanPinOperationMax
} WWAN_PIN_OPERATION, *PWWAN_PIN_OPERATION;

typedef struct _WWAN_PIN_ACTION {
    WWAN_PIN_TYPE	PinType;
    WWAN_PIN_OPERATION	PinOperation;
    WCHAR		Pin [WWAN_PIN_LEN];
    WCHAR		NewPin [WWAN_PIN_LEN];
} WWAN_PIN_ACTION, *PWWAN_PIN_ACTION;

typedef enum _WWAN_PIN_FORMAT {
    WwanPinFormatUnknown = 0,
    WwanPinFormatNumeric,
    WwanPinFormatAlphaNumeric,
    WwanPinFormatMax
} WWAN_PIN_FORMAT, *PWWAN_PIN_FORMAT;

typedef enum _WWAN_PIN_MODE {
    WwanPinModeNotSupported = 0,
    WwanPinModeEnabled,
    WwanPinModeDisabled,
    WwanPinModeMax
} WWAN_PIN_MODE, *PWWAN_PIN_MODE;

#define WWAN_PIN_LENGTH_UNKNOWN			~0

typedef struct _WWAN_PIN_DESC {
    WWAN_PIN_MODE	PinMode;
    WWAN_PIN_FORMAT	PinFormat;
    ULONG		PinLengthMin;
    ULONG		PinLengthMax;
} WWAN_PIN_DESC, *PWWAN_PIN_DESC;

typedef struct _WWAN_PIN_LIST {
    WWAN_PIN_DESC	WwanPinDescPin1;
    WWAN_PIN_DESC	WwanPinDescPin2;
    WWAN_PIN_DESC	WwanPinDescDeviceSimPin;
    WWAN_PIN_DESC	WwanPinDescDeviceFirstSimPin;
    WWAN_PIN_DESC	WwanPinDescNetworkPin;
    WWAN_PIN_DESC	WwanPinDescNetworkSubsetPin;
    WWAN_PIN_DESC	WwanPinDescSvcProviderPin;
    WWAN_PIN_DESC	WwanPinDescCorporatePin;
    WWAN_PIN_DESC	WwanPinDescSubsidyLock;
    WWAN_PIN_DESC	WwanPinDescCustom;
} WWAN_PIN_LIST, *PWWAN_PIN_LIST;

#define WWAN_PROVIDER_STATE_UNKNOWN 		0x00000000
#define WWAN_PROVIDER_STATE_HOME 		0x00000001
#define WWAN_PROVIDER_STATE_FORBIDDEN 		0x00000002
#define WWAN_PROVIDER_STATE_PREFERRED 		0x00000004
#define WWAN_PROVIDER_STATE_VISIBLE 		0x00000008
#define WWAN_PROVIDER_STATE_REGISTERED		0x00000010

typedef struct _WWAN_PROVIDER {
    WCHAR	ProviderId [WWAN_PROVIDERID_LEN];       
    ULONG	ProviderState;
    WCHAR	ProviderName [WWAN_PROVIDERNAME_LEN];
    ULONG	WwanDataClass;
} WWAN_PROVIDER, *PWWAN_PROVIDER;

typedef enum _WWAN_REGISTER_ACTION {
    WwanRegisterActionAutomatic = 0,
    WwanRegisterActionManual,
    WwanRegisterActionMax
} WWAN_REGISTER_ACTION, *PWWAN_REGISTER_ACTION;

typedef struct _WWAN_SET_REGISTER_STATE {
    WCHAR			ProviderId [WWAN_PROVIDERID_LEN];       
    WWAN_REGISTER_ACTION	RegisterAction;
    ULONG			WwanDataClass;
} WWAN_SET_REGISTER_STATE, *PWWAN_SET_REGISTER_STATE;

typedef enum _WWAN_REGISTER_STATE {
    WwanRegisterStateUnknown = 0,
    WwanRegisterStateDeregistered,
    WwanRegisterStateSearching,
    WwanRegisterStateHome,
    WwanRegisterStateRoaming,
    WwanRegisterStatePartner,
    WwanRegisterStateDenied,
    WwanRegisterStateMax
} WWAN_REGISTER_STATE, *PWWAN_REGISTER_STATE;

typedef enum _WWAN_REGISTER_MODE {
    WwanRegisterModeUnknown = 0,
    WwanRegisterModeAutomatic,
    WwanRegisterModeManual,
    WwanRegisterModeMax
} WWAN_REGISTER_MODE, *PWWAN_REGISTER_MODE;

typedef struct _WWAN_REGISTRATION_STATE {
    ULONG               	uNwError;
    WWAN_REGISTER_STATE		RegisterState;
    WWAN_REGISTER_MODE		RegisterMode;
    WCHAR			ProviderId [WWAN_PROVIDERID_LEN];
    WCHAR			ProviderName [WWAN_PROVIDERNAME_LEN];
    WCHAR			RoamingText [WWAN_ROAMTEXT_LEN];
} WWAN_REGISTRATION_STATE, *PWWAN_REGISTRATION_STATE;

typedef enum _WWAN_PACKET_SERVICE_ACTION {
    WwanPacketServiceActionAttach = 0,
    WwanPacketServiceActionDetach
} WWAN_PACKET_SERVICE_ACTION, *PWWAN_PACKET_SERVICE_ACTION;

typedef enum _WWAN_PACKET_SERVICE_STATE {
    WwanPacketServiceStateUnknown = 0,
    WwanPacketServiceStateAttaching,
    WwanPacketServiceStateAttached,
    WwanPacketServiceStateDetaching,
    WwanPacketServiceStateDetached
} WWAN_PACKET_SERVICE_STATE, *PWWAN_PACKET_SERVICE_STATE;

typedef struct _WWAN_PACKET_SERVICE {
    ULONG			uNwError;
    WWAN_PACKET_SERVICE_STATE	PacketServiceState;
    ULONG			AvailableDataClass;
    ULONG			CurrentDataClass;
} WWAN_PACKET_SERVICE, *PWWAN_PACKET_SERVICE;

#define WWAN_RSSI_UNKNOWN				99
#define WWAN_ERROR_RATE_UNKNOWN				99

typedef struct _WWAN_SIGNAL_STATE {
    ULONG	Rssi;
    ULONG	ErrorRate;
    ULONG	RssiInterval;
    ULONG	RssiThreshold;
} WWAN_SIGNAL_STATE, *PWWAN_SIGNAL_STATE;

#define WWAN_RSSI_DEFAULT				0xffffffff
#define WWAN_RSSI_DISABLE				0

typedef struct _WWAN_SET_SIGNAL_INDICATION {
    ULONG	RssiInterval;
    ULONG	RssiThreshold;
} WWAN_SET_SIGNAL_INDICATION, *PWWAN_SET_SIGNAL_INDICATION;

typedef enum _WWAN_ACTIVATION_COMMAND {
    WwanActivationCommandDeactivate = 0,
    WwanActivationCommandActivate,
    WwanActivationCommandMax
} WWAN_ACTIVATION_COMMAND, *PWWAN_ACTIVATION_COMMAND;

typedef enum _WWAN_COMPRESSION {
    WwanCompressionNone = 0,
    WwanCompressionEnable,
    WwanCompressionMax
} WWAN_COMPRESSION, *PWWAN_COMPRESSION;

typedef enum _WWAN_AUTH_PROTOCOL {
    WwanAuthProtocolNone = 0,
    WwanAuthProtocolPap,
    WwanAuthProtocolChap,
    WwanAuthProtocolMsChapV2,
    WwanAuthProtocolMax
} WWAN_AUTH_PROTOCOL, *PWWAN_AUTH_PROTOCOL;

typedef struct _WWAN_SET_CONTEXT_STATE {
    ULONG			ConnectionId;
    WWAN_ACTIVATION_COMMAND	ActivationCommand;
    WCHAR			AccessString [WWAN_ACCESSSTRING_LEN];
    WCHAR			UserName [WWAN_USERNAME_LEN];
    WCHAR			Password [WWAN_PASSWORD_LEN];
    WWAN_COMPRESSION		Compression;
    WWAN_AUTH_PROTOCOL		AuthType;
} WWAN_SET_CONTEXT_STATE, *PWWAN_SET_CONTEXT_STATE;

typedef enum _WWAN_ACTIVATION_STATE {
    WwanActivationStateUnknown = 0,
    WwanActivationStateActivated,
    WwanActivationStateActivating,
    WwanActivationStateDeactivated,
    WwanActivationStateDeactivating,
    WwanActivationStateMax
} WWAN_ACTIVATION_STATE, *PWWAN_ACTIVATION_STATE;

typedef enum _WWAN_VOICE_CALL_STATE
{
    WwanVoiceCallStateNone = 0,
    WwanVoiceCallStateInProgress,
    WwanVoiceCallStateHangUp,
    WwanVoiceCallStateMaximum
} WWAN_VOICE_CALL_STATE, *PWWAN_VOICE_CALL_STATE;

typedef struct _WWAN_CONTEXT_STATE {
    ULONG			uNwError;
    ULONG			ConnectionId;
    WWAN_ACTIVATION_STATE	ActivationState;
    WWAN_VOICE_CALL_STATE	VoiceCallState;
} WWAN_CONTEXT_STATE, *PWWAN_CONTEXT_STATE;

typedef enum _WWAN_CONTEXT_TYPE {
    WwanContextTypeNone = 0,
    WwanContextTypeInternet,
    WwanContextTypeVpn,
    WwanContextTypeVoice,
    WwanContextTypeVideoShare,
    WwanContextTypeCustom,
    WwanContextTypeMax
} WWAN_CONTEXT_TYPE, *PWWAN_CONTEXT_TYPE;

#define WWAN_CONTEXT_ID_APPEND				0xffffffff

typedef struct _WWAN_CONTEXT {
    ULONG		ContextId;
    WWAN_CONTEXT_TYPE	ContextType;
    WCHAR		AccessString [WWAN_ACCESSSTRING_LEN];
    WCHAR		UserName [WWAN_USERNAME_LEN];            
    WCHAR		Password [WWAN_PASSWORD_LEN];
    WWAN_COMPRESSION	Compression;
    WWAN_AUTH_PROTOCOL	AuthType;
} WWAN_CONTEXT, *PWWAN_CONTEXT;

typedef struct _WWAN_SET_CONTEXT
{
    ULONG				ContextId;
    WWAN_CONTEXT_TYPE		ContextType;
    WCHAR                     AccessString[WWAN_ACCESSSTRING_LEN];
    WCHAR                     UserName[WWAN_USERNAME_LEN];            
    WCHAR                     Password[WWAN_PASSWORD_LEN];
    WWAN_COMPRESSION		Compression;
    WWAN_AUTH_PROTOCOL		AuthType;
    WCHAR   			ProviderId[WWAN_PROVIDERID_LEN];
} WWAN_SET_CONTEXT, *PWWAN_SET_CONTEXT;

typedef enum _WWAN_SMS_FORMAT {
    WwanSmsFormatPdu = 0,
    WwanSmsFormatReserved0,
    WwanSmsFormatReserved1,
    WwanSmsFormatReserved2,
    WwanSmsFormatCdma,
    WwanSmsFormatMax
} WWAN_SMS_FORMAT, *PWWAN_SMS_FORMAT;

typedef struct _WWAN_SET_SMS_CONFIGURATION {
    CHAR		ScAddress [WWAN_SMS_ADDRESS_MAX_LEN];
    WWAN_SMS_FORMAT	SmsFormat;
} WWAN_SET_SMS_CONFIGURATION, *PWWAN_SET_SMS_CONFIGURATION;

typedef struct _WWAN_SMS_CONFIGURATION {
    CHAR		ScAddress [WWAN_SMS_ADDRESS_MAX_LEN];
    WWAN_SMS_FORMAT	SmsFormat;
    ULONG		ulMaxMessageIndex;
} WWAN_SMS_CONFIGURATION, *PWWAN_SMS_CONFIGURATION;

typedef enum _WWAN_SMS_FLAG {
    WwanSmsFlagAll = 0,
    WwanSmsFlagIndex,
    WwanSmsFlagNew,
    WwanSmsFlagOld,
    WwanSmsFlagSent,
    WwanSmsFlagDraft,
    WwanSmsFlagMax
} WWAN_SMS_FLAG, *PWWAN_SMS_FLAG;

#define WWAN_MESSAGE_INDEX_NONE		0

typedef struct _WWAN_SMS_FILTER {
    WWAN_SMS_FLAG	Flag;
    ULONG		MessageIndex;
} WWAN_SMS_FILTER, *PWWAN_SMS_FILTER;

typedef struct _WWAN_SMS_READ {
    WWAN_SMS_FORMAT	SmsFormat;
    WWAN_SMS_FILTER	ReadFilter; 
} WWAN_SMS_READ, *PWWAN_SMS_READ;

typedef enum _WWAN_MSG_STATUS {
    WwanMsgStatusNew = 0,
    WwanMsgStatusOld,
    WwanMsgStatusDraft,
    WwanMsgStatusSent,
    WwanMsgStatusMax
} WWAN_MSG_STATUS, *PWWAN_MSG_STATUS;

typedef enum _WWAN_SMS_CDMA_LANG {
    WwanSmsCdmaLangUnknown = 0,
    WwanSmsCdmaLangEnglish,
    WwanSmsCdmaLangFrench,
    WwanSmsCdmaLangSpanish,
    WwanSmsCdmaLangJapanese,
    WwanSmsCdmaLangKorean,
    WwanSmsCdmaLangChinese,
    WwanSmsCdmaLangHebrew,
    WwanSmsCdmaLangMax
} WWAN_SMS_CDMA_LANG, *PWWAN_SMS_CDMA_LANG;

typedef enum _WWAN_SMS_CDMA_ENCODING {
    WwanSmsCdmaEncodingOctet = 0,
    WwanSmsCdmaEncodingEpm,
    WwanSmsCdmaEncoding7BitAscii,
    WwanSmsCdmaEncodingIa5,
    WwanSmsCdmaEncodingUnicode,
    WwanSmsCdmaEncodingShiftJis,
    WwanSmsCdmaEncodingKorean,
    WwanSmsCdmaEncodingLatinHebrew,
    WwanSmsCdmaEncodingLatin,
    WwanSmsCdmaEncodingGsm7Bit,
    WwanSmsCdmaEncodingMax
} WWAN_SMS_CDMA_ENCODING, *PWWAN_SMS_CDMA_ENCODING;

typedef struct _WWAN_SMS_CDMA_RECORD {
    ULONG			MessageIndex;
    WWAN_MSG_STATUS		MsgStatus;
    CHAR                        Address[WWAN_SMS_CDMA_ADDR_MAX_LEN];
    CHAR                        ScTimeStamp[WWAN_SMS_CDMA_TIMESTAMP_MAX_LEN];
    WWAN_SMS_CDMA_ENCODING 	EncodingId;
    WWAN_SMS_CDMA_LANG     	LanguageId;
    USHORT			SizeInBytes;
    BYTE			SizeInCharacters;
    BYTE                    	EncodedMsg [WWAN_SMS_CDMA_MAX_BUF_LEN];
} WWAN_SMS_CDMA_RECORD, *PWWAN_SMS_CDMA_RECORD;

typedef struct _WWAN_SMS_PDU_RECORD {
    ULONG		MessageIndex;
    WWAN_MSG_STATUS	MsgStatus;
    BYTE		Size;
    CHAR		PduData [WWAN_SMS_PDU_HEX_BUF_LEN];
} WWAN_SMS_PDU_RECORD, *PWWAN_SMS_PDU_RECORD;

typedef struct _WWAN_SMS_SEND_CDMA {
    WWAN_SMS_CDMA_ENCODING	EncodingId;
    WWAN_SMS_CDMA_LANG		LanguageId;
    CHAR			Address [WWAN_SMS_CDMA_ADDR_MAX_LEN];
    USHORT			SizeInBytes;
    BYTE			SizeInCharacters;
    BYTE			EncodedMsg [WWAN_SMS_CDMA_MAX_BUF_LEN];
} WWAN_SMS_SEND_CDMA, *PWWAN_SMS_SEND_CDMA;

typedef struct _WWAN_SMS_SEND_PDU {
    BYTE	Size;
    CHAR	PduData [WWAN_SMS_PDU_HEX_BUF_LEN];
} WWAN_SMS_SEND_PDU, *PWWAN_SMS_SEND_PDU;

typedef struct _WWAN_SMS_SEND {
    WWAN_SMS_FORMAT SmsFormat;

    union {
	WWAN_SMS_SEND_PDU  Pdu;
	WWAN_SMS_SEND_CDMA Cdma;
    } u;
} WWAN_SMS_SEND, *PWWAN_SMS_SEND;

#define WWAN_SMS_FLAG_NONE			0x00000000
#define WWAN_SMS_FLAG_MESSAGE_STORE_FULL 	0x00000001
#define WWAN_SMS_FLAG_NEW_MESSAGE		0x00000002

typedef struct _WWAN_SMS_STATUS {
    ULONG	uFlag;
    ULONG	MessageIndex;
} WWAN_SMS_STATUS, *PWWAN_SMS_STATUS;

typedef struct _WWAN_SERVICE_ACTIVATION_STATUS {
    ULONG uNwError;
    ULONG uVendorSpecificBufferSize;
} WWAN_SERVICE_ACTIVATION_STATUS,*PWWAN_SERVICE_ACTIVATION_STATUS;

typedef struct _WWAN_VENDOR_SPECIFIC {
    ULONG   uVendorSpecificBufferSize;
} WWAN_VENDOR_SPECIFIC, *PWWAN_VENDOR_SPECIFIC;

#endif

