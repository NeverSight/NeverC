/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef ANDROID_NATIVE_PERFORMANCE_HINT_H
#define ANDROID_NATIVE_PERFORMANCE_HINT_H

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

#include <android/api-level.h>
#include <stdint.h>

__BEGIN_DECLS

struct APerformanceHintManager;
struct APerformanceHintSession;

/**
 * An opaque type representing a handle to a performance hint manager.
 * It must be released after use.
 *
 * <p>To use:<ul>
 *    <li>Obtain the performance hint manager instance by calling
 *        {@link APerformanceHint_getManager} function.</li>
 *    <li>Create an {@link APerformanceHintSession} with
 *        {@link APerformanceHint_createSession}.</li>
 *    <li>Get the preferred update rate in nanoseconds with
 *        {@link APerformanceHint_getPreferredUpdateRateNanos}.</li>
 */
typedef struct APerformanceHintManager APerformanceHintManager;

/**
 * An opaque type representing a handle to a performance hint session.
 * A session can only be acquired from a {@link APerformanceHintManager}
 * with {@link APerformanceHint_getPreferredUpdateRateNanos}. It must be
 * freed with {@link APerformanceHint_closeSession} after use.
 *
 * A Session represents a group of threads with an inter-related workload such that hints for
 * their performance should be considered as a unit. The threads in a given session should be
 * long-life and not created or destroyed dynamically.
 *
 * <p>Each session is expected to have a periodic workload with a target duration for each
 * cycle. The cycle duration is likely greater than the target work duration to allow other
 * parts of the pipeline to run within the available budget. For example, a renderer thread may
 * work at 60hz in order to produce frames at the display's frame but have a target work
 * duration of only 6ms.</p>
 *
 * <p>After each cycle of work, the client is expected to use
 * {@link APerformanceHint_reportActualWorkDuration} to report the actual time taken to
 * complete.</p>
 *
 * <p>To use:<ul>
 *    <li>Update a sessions target duration for each cycle of work
 *        with  {@link APerformanceHint_updateTargetWorkDuration}.</li>
 *    <li>Report the actual duration for the last cycle of work with
 *        {@link APerformanceHint_reportActualWorkDuration}.</li>
 *    <li>Release the session instance with
 *        {@link APerformanceHint_closeSession}.</li></ul></p>
 */
typedef struct APerformanceHintSession APerformanceHintSession;

/**
  * Acquire an instance of the performance hint manager.
  *
  * @return manager instance on success, nullptr on failure.
  */
APerformanceHintManager* APerformanceHint_getManager() __INTRODUCED_IN(__ANDROID_API_T__);

/**
 * Creates a session for the given set of threads and sets their initial target work
 * duration.
 * @param manager The performance hint manager instance.
 * @param threadIds The list of threads to be associated with this session. They must be part of
 *     this app's thread group.
 * @param size the size of threadIds.
 * @param initialTargetWorkDurationNanos The desired duration in nanoseconds for the new session.
 *     This must be positive.
 * @return manager instance on success, nullptr on failure.
 */
APerformanceHintSession* APerformanceHint_createSession(
        APerformanceHintManager* manager,
        const int32_t* threadIds, size_t size,
        int64_t initialTargetWorkDurationNanos) __INTRODUCED_IN(__ANDROID_API_T__);

/**
 * Get preferred update rate information for this device.
 *
 * @param manager The performance hint manager instance.
 * @return the preferred update rate supported by device software.
 */
int64_t APerformanceHint_getPreferredUpdateRateNanos(
        APerformanceHintManager* manager) __INTRODUCED_IN(__ANDROID_API_T__);

/**
 * Updates this session's target duration for each cycle of work.
 *
 * @param session The performance hint session instance to update.
 * @param targetDurationNanos the new desired duration in nanoseconds. This must be positive.
 * @return 0 on success
 *         EINVAL if targetDurationNanos is not positive.
 *         EPIPE if communication with the system service has failed.
 */
int APerformanceHint_updateTargetWorkDuration(
        APerformanceHintSession* session,
        int64_t targetDurationNanos) __INTRODUCED_IN(__ANDROID_API_T__);

/**
 * Reports the actual duration for the last cycle of work.
 *
 * <p>The system will attempt to adjust the core placement of the threads within the thread
 * group and/or the frequency of the core on which they are run to bring the actual duration
 * close to the target duration.</p>
 *
 * @param session The performance hint session instance to update.
 * @param actualDurationNanos how long the thread group took to complete its last task in
 *     nanoseconds. This must be positive.
 * @return 0 on success
 *         EINVAL if actualDurationNanos is not positive.
 *         EPIPE if communication with the system service has failed.
 */
int APerformanceHint_reportActualWorkDuration(
        APerformanceHintSession* session,
        int64_t actualDurationNanos) __INTRODUCED_IN(__ANDROID_API_T__);

/**
 * Release the performance hint manager pointer acquired via
 * {@link APerformanceHint_createSession}.
 *
 * @param session The performance hint session instance to release.
 */
void APerformanceHint_closeSession(
        APerformanceHintSession* session) __INTRODUCED_IN(__ANDROID_API_T__);

__END_DECLS

#endif // ANDROID_NATIVE_PERFORMANCE_HINT_H
