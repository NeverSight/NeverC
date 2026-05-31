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

// This file provides preprocessor configuration for libicuuc headers when used in NDK.

#pragma once

// Include API-level tagging facility provided by NDK.
#include <android/log.h>
#ifdef __ANDROID__
#include <android/versioning.h>
#endif

#define U_DISABLE_RENAMING 1
#define U_HIDE_DRAFT_API 1
#define U_HIDE_DEPRECATED_API 1
#define U_SHOW_CPLUSPLUS_API 0
#define U_HIDE_INTERNAL_API 1
#define U_HIDE_OBSOLETE_UTF_OLD_H 1

// Set this flag to allow header-only C++ usages when using a C++ compiler
#ifdef __cplusplus
#   ifndef LIBICU_U_SHOW_CPLUSPLUS_API
#       define LIBICU_U_SHOW_CPLUSPLUS_API 1
#   endif
#else
#   undef LIBICU_U_SHOW_CPLUSPLUS_API
#   define LIBICU_U_SHOW_CPLUSPLUS_API 0
#endif
