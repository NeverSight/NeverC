/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

 BluetoothLEAudioStreaming.h

Abstract:

    This module defines the types needed for audio drivers to indicate support for Bluetooth LE Audio Streaming.

--*/

#pragma once

//
// {5C52FDB5-722A-4AB7-A342-70163B7E9B5C}
// ACX circuit component ID definition for LE Audio render endpoints
//
DEFINE_GUID(GUID_BLUETOOTH_LEAUDIO_RENDER_COMPONENT_ID, 0x5c52fdb5, 0x722a, 0x4ab7, 0xa3, 0x42, 0x70, 0x16, 0x3b, 0x7e, 0x9b, 0x5c);

//
// {1DFF2EE3-AE89-441C-BDE3-24F885C55DF8}
// ACX circuit component ID definition for LE audio capture endpoints
//
DEFINE_GUID(GUID_BLUETOOTH_LEAUDIO_CAPTURE_COMPONENT_ID, 0x1dff2ee3, 0xae89, 0x441c, 0xbd, 0xe3, 0x24, 0xf8, 0x85, 0xc5, 0x5d, 0xf8);

//
// GUID_BLUETOOTH_LEAUDIO_SUPPORT_INTERFACE
// Published by the audio driver to indicate that it is configured for Bluetooth LE Audio streaming.
//
// {BA02FA1B-0FD0-4A0F-A748-4FAE2E2D2F67}
DEFINE_GUID(GUID_BLUETOOTH_LEAUDIO_SUPPORT_INTERFACE, 0xba02fa1b, 0x0fd0, 0x4a0f, 0xa7, 0x48, 0x4f, 0xae, 0x2e, 0x2d, 0x2f, 0x67);

#define GUID_BLUETOOTH_LEAUDIO_SUPPORT_INTERFACE_DEFINED

#if defined(DEFINE_DEVPROPKEY)
//
// DEVPKEY_BluetoothLEAudioBidirectionalMultichannelStreamingCapabilities
//
// An optional property for the device interface, GUID_BLUETOOTH_LEAUDIO_SUPPORT_INTERFACE. An audio driver should publish
// this property only if it supports bidirectional multichannel audio streaming (e.g., stereo render with mono capture).
//
DEFINE_DEVPROPKEY(DEVPKEY_BluetoothLEAudioBidirectionalMultichannelStreamingCapabilities,
    0xd27ba3a4, 0x1bfe, 0x4374, 0x88, 0x7d, 0xe8, 0xb3, 0xa6, 0xac, 0xe, 0xe9, 2); // DEVPROP_TYPE_BINARY (BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_CAPABILITY[])
#endif

//
// Bluetooth Audio Codec ID
//
typedef struct _BTH_LE_AUDIO_CODEC_ID
{
    // Coding format
    //
    // See section 2.11 "Coding_Format and Codec_ID (HCI)" of Bluetooth Assigned Numbers (https://www.bluetooth.com/specifications/assigned-numbers).
    //
    UINT8 CodingFormat;

    // Company identifier
    //
    // See section 7 "Company Identifiers" of Bluetooth Assigned Numbers (https://www.bluetooth.com/specifications/assigned-numbers).
    //
    UINT16 CompanyId;

    // Vendor-specific Codec ID
    //
    UINT16 VendorCodecId;
} BTH_LE_AUDIO_CODEC_ID;

typedef UINT8 BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_CHANNEL_COUNT;

// BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY
// 
// List of render/capture sampling frequencies for BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_CAPABILITY (see below).
//
typedef enum _BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY : UINT8 // Bit flags
{
    // 16 kHz
    BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY_16000HZ = 0x1,

    // 24 kHz
    BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY_24000HZ = 0x2,

    // 32 kHz
    BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY_32000HZ = 0x4,

    // 48 kHz
    BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY_48000HZ = 0x8,

    // A dummy value for indicating a sampling frequency is "not applicable" in the respective context.
    BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY_NONE = 0,

    // All valid sampling frequencies combined.
    BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY_ALL =
        BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY_16000HZ |
        BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY_24000HZ |
        BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY_32000HZ |
        BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY_48000HZ,
} BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY;

DEFINE_ENUM_FLAG_OPERATORS(BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY);

#define BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY_BIT_LENGTH \
    (sizeof(BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY) * 8)

// BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_CAPABILITY
//
// Specifies the list of bidirectional multichannel audio formats supported by the driver.
//
typedef struct _BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_CAPABILITY
{
    BTH_LE_AUDIO_CODEC_ID CodecId;
    BOOL IsCodecPresent;
    BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_CHANNEL_COUNT RenderChannelCount;
    BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_CHANNEL_COUNT CaptureChannelCount;
    BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY RenderSamplingFrequencies;
    BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY CaptureSamplingFrequenciesList[
        BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_SAMPLING_FREQUENCY_BIT_LENGTH
    ];
} BTH_LE_AUDIO_BIDIRECTIONAL_MULTICHANNEL_STREAMING_CAPABILITY;
