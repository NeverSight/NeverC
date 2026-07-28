/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

 SoundWireClass.h

Abstract:

    This module defines the types, constants, functions and control codes that are
    used by device drivers and APOs to interact with a SoundWire Class drivers.

Environment:

    Kernel-mode.

--*/

#ifndef _SOUNDWIRECLASS_H_
#define _SOUNDWIRECLASS_H_

// Audio Module definition for communicating the posture
// {01746DF6-11BE-4D41-A0B1-B4907DC3746F}
static const GUID SDCA_POSTURE_MODULE = 
{ 0x1746df6, 0x11be, 0x4d41, { 0xa0, 0xb1, 0xb4, 0x90, 0x7d, 0xc3, 0x74, 0x6f } };

#define SDCA_POSTURE_NAME L"Audio Posture"
#define SDCA_POSTURE_VERSION_MAJOR  0x1
#define SDCA_POSTURE_VERSION_MINOR  0X0
#define SDCA_POSTURE_INSTANCEID     0X0

typedef enum _SDCA_POSTURE
{
    SdcaPostureOrientationNotRotated = 0,
    SdcaPostureOrientationRotated90DegreesCounterClockwise,
    SdcaPostureOrientationRotated180DegreesCounterClockwise,
    SdcaPostureOrientationRotated270DegreesCounterClockwise,
    SdcaPostureLidClosed,
    SdcaPostureCount
} SDCA_POSTURE, *PSDCA_POSTURE;

typedef struct _SDCA_NOTIFICATION_POSTURE
{
    SDCA_POSTURE          Posture;
} SDCA_NOTIFICATION_POSTURE, *PSDCA_NOTIFICATION_POSTURE;

#endif // _SOUNDWIRECLASS_H_
