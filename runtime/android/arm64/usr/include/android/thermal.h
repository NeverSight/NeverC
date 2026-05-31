/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @addtogroup Thermal
 * @{
 */

/**
 * @file thermal.h
 */

#ifndef _ANDROID_THERMAL_H
#define _ANDROID_THERMAL_H

#include <sys/cdefs.h>

/******************************************************************
 *
 * IMPORTANT NOTICE:
 *
 *   This file is part of Android's set of stable system headers
 *   exposed by the Android NDK (Native Development Kit).
 *
 *   Third-party source AND binary code relies on the definitions
 *   here to be FROZEN ON ALL UPCOMING PLATFORM RELEASES.
 *
 *   - DO NOT MODIFY ENUMS (EXCEPT IF YOU ADD NEW 32-BIT VALUES)
 *   - DO NOT MODIFY CONSTANTS OR FUNCTIONAL MACROS
 *   - DO NOT CHANGE THE SIGNATURE OF FUNCTIONS IN ANY WAY
 *   - DO NOT CHANGE THE LAYOUT OR SIZE OF STRUCTURES
 */

/*
 * Structures and functions to access thermal status and register/unregister
 * thermal status listener in native code.
 */

#include <stdint.h>
#include <sys/types.h>

#if !defined(__INTRODUCED_IN)
#define __INTRODUCED_IN(__api_level) /* nothing */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Thermal status used in function {@link AThermal_getCurrentThermalStatus} and
 * {@link AThermal_StatusCallback}.
 */
enum AThermalStatus {
    /** Error in thermal status. */
    ATHERMAL_STATUS_ERROR = -1,
    /** Not under throttling. */
    ATHERMAL_STATUS_NONE = 0,
    /** Light throttling where UX is not impacted. */
    ATHERMAL_STATUS_LIGHT = 1,
    /** Moderate throttling where UX is not largely impacted. */
    ATHERMAL_STATUS_MODERATE = 2,
    /** Severe throttling where UX is largely impacted. */
    ATHERMAL_STATUS_SEVERE = 3,
    /** Platform has done everything to reduce power. */
    ATHERMAL_STATUS_CRITICAL = 4,
    /**
     * Key components in platform are shutting down due to thermal condition.
     * Device functionalities will be limited.
     */
    ATHERMAL_STATUS_EMERGENCY = 5,
    /** Need shutdown immediately. */
    ATHERMAL_STATUS_SHUTDOWN = 6,
};

/**
 * An opaque type representing a handle to a thermal manager.
 * An instance of thermal manager must be acquired prior to
 * using thermal status APIs and must be released after use.
 *
 * <p>To use:<ul>
 *    <li>Create a new thermal manager instance by calling the
 *        {@link AThermal_acquireManager} function.</li>
 *    <li>Get current thermal status with
 *        {@link AThermal_getCurrentThermalStatus}.</li>
 *    <li>Register a thermal status listener with
 *        {@link AThermal_registerThermalStatusListener}.</li>
 *    <li>Unregister a thermal status listener with
 *        {@link AThermal_unregisterThermalStatusListener}.</li>
 *    <li>Release the thermal manager instance with
 *        {@link AThermal_releaseManager}.</li></ul></p>
 *
 */
typedef struct AThermalManager AThermalManager;

/**
 * Prototype of the function that is called when thermal status changes.
 * It's passed the updated thermal status as parameter, as well as the
 * pointer provided by the client that registered a callback.
 */
typedef void (*AThermal_StatusCallback)(void *data, AThermalStatus status);

/**
  * Acquire an instance of the thermal manager. This must be freed using
  * {@link AThermal_releaseManager}.
  *
  * Available since API level 30.
  *
  * @return manager instance on success, nullptr on failure.
  */
AThermalManager* AThermal_acquireManager() __INTRODUCED_IN(30);

/**
 * Release the thermal manager pointer acquired via
 * {@link AThermal_acquireManager}.
 *
 * Available since API level 30.
 *
 * @param manager The manager to be released.
 */
void AThermal_releaseManager(AThermalManager *manager) __INTRODUCED_IN(30);

/**
  * Gets the current thermal status.
  *
  * Available since API level 30.
  *
  * @param manager The manager instance to use to query the thermal status.
  * Acquired via {@link AThermal_acquireManager}.
  *
  * @return current thermal status, ATHERMAL_STATUS_ERROR on failure.
  */
AThermalStatus AThermal_getCurrentThermalStatus(AThermalManager *manager) __INTRODUCED_IN(30);

/**
 * Register the thermal status listener for thermal status change.
 *
 * Available since API level 30.
 *
 * @param manager The manager instance to use to register.
 * Acquired via {@link AThermal_acquireManager}.
 * @param callback The callback function to be called when thermal status updated.
 * @param data The data pointer to be passed when callback is called.
 *
 * @return 0 on success
 *         EINVAL if the listener and data pointer were previously added and not removed.
 *         EPERM if the required permission is not held.
 *         EPIPE if communication with the system service has failed.
 */
int AThermal_registerThermalStatusListener(AThermalManager *manager,
        AThermal_StatusCallback callback, void *data) __INTRODUCED_IN(30);

/**
 * Unregister the thermal status listener previously resgistered.
 *
 * Available since API level 30.
 *
 * @param manager The manager instance to use to unregister.
 * Acquired via {@link AThermal_acquireManager}.
 * @param callback The callback function to be called when thermal status updated.
 * @param data The data pointer to be passed when callback is called.
 *
 * @return 0 on success
 *         EINVAL if the listener and data pointer were not previously added.
 *         EPERM if the required permission is not held.
 *         EPIPE if communication with the system service has failed.
 */
int AThermal_unregisterThermalStatusListener(AThermalManager *manager,
        AThermal_StatusCallback callback, void *data) __INTRODUCED_IN(30);

/**
 * Provides an estimate of how much thermal headroom the device currently has before
 * hitting severe throttling.
 *
 * Note that this only attempts to track the headroom of slow-moving sensors, such as
 * the skin temperature sensor. This means that there is no benefit to calling this function
 * more frequently than about once per second, and attempted to call significantly
 * more frequently may result in the function returning {@code NaN}.
 *
 * In addition, in order to be able to provide an accurate forecast, the system does
 * not attempt to forecast until it has multiple temperature samples from which to
 * extrapolate. This should only take a few seconds from the time of the first call,
 * but during this time, no forecasting will occur, and the current headroom will be
 * returned regardless of the value of {@code forecastSeconds}.
 *
 * The value returned is a non-negative float that represents how much of the thermal envelope
 * is in use (or is forecasted to be in use). A value of 1.0 indicates that the device is
 * (or will be) throttled at {@link #ATHERMAL_STATUS_SEVERE}. Such throttling can affect the
 * CPU, GPU, and other subsystems. Values may exceed 1.0, but there is no implied mapping
 * to specific thermal levels beyond that point. This means that values greater than 1.0
 * may correspond to {@link #ATHERMAL_STATUS_SEVERE}, but may also represent heavier throttling.
 *
 * A value of 0.0 corresponds to a fixed distance from 1.0, but does not correspond to any
 * particular thermal status or temperature. Values on (0.0, 1.0] may be expected to scale
 * linearly with temperature, though temperature changes over time are typically not linear.
 * Negative values will be clamped to 0.0 before returning.
 *
 * Available since API level 31.
 *
 * @param manager The manager instance to use.
 *                Acquired via {@link AThermal_acquireManager}.
 * @param forecastSeconds how many seconds into the future to forecast. Given that device
 *                        conditions may change at any time, forecasts from further in the
 *                        future will likely be less accurate than forecasts in the near future.
 * @return a value greater than equal to 0.0, where 1.0 indicates the SEVERE throttling threshold,
 *         as described above. Returns NaN if the device does not support this functionality or
 *         if this function is called significantly faster than once per second.
  */
float AThermal_getThermalHeadroom(AThermalManager *manager,
        int forecastSeconds) __INTRODUCED_IN(31);

#ifdef __cplusplus
}
#endif

#endif // _ANDROID_THERMAL_H

/** @} */
