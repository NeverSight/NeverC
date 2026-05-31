// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*   Copyright (C) 2000-2011, International Business Machines
*   Corporation and others.  All Rights Reserved.
*******************************************************************************
*
*   file name:  uversion.h
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   Created by: Vladimir Weinstein
*
*  Gets included by utypes.h and Windows .rc files
*/

/**
 * @addtogroup icu4c ICU4C
 * @{
 * \file
 * \brief C API: API for accessing ICU version numbers. 
 */
/*===========================================================================*/
/* Main ICU version information                                              */
/*===========================================================================*/

#ifndef UVERSION_H
#define UVERSION_H

#include "unicode/umachine.h"

/* Actual version info lives in uvernum.h */
#include "unicode/uvernum.h"

/** Maximum length of the copyright string.
 *  \xrefitem stable "Stable" "Stable List" ICU 2.4
 */
#define U_COPYRIGHT_STRING_LENGTH  128

/** An ICU version consists of up to 4 numbers from 0..255.
 *  \xrefitem stable "Stable" "Stable List" ICU 2.4
 */
#define U_MAX_VERSION_LENGTH 4

/** In a string, ICU version fields are delimited by dots.
 *  \xrefitem stable "Stable" "Stable List" ICU 2.4
 */
#define U_VERSION_DELIMITER '.'

/** The maximum length of an ICU version string.
 *  \xrefitem stable "Stable" "Stable List" ICU 2.4
 */
#define U_MAX_VERSION_STRING_LENGTH 20

/** The binary form of a version on ICU APIs is an array of 4 uint8_t.
 *  To compare two versions, use memcmp(v1,v2,sizeof(UVersionInfo)).
 *  \xrefitem stable "Stable" "Stable List" ICU 2.4
 */
typedef uint8_t UVersionInfo[U_MAX_VERSION_LENGTH];

/*===========================================================================*/
/* C++ namespace if supported. Versioned unless versioning is disabled.      */
/*===========================================================================*/

/* Define C++ namespace symbols. */
#ifdef __cplusplus

/**
 * \def U_NAMESPACE_BEGIN
 * This is used to begin a declaration of a public ICU C++ API within
 * versioned-ICU-namespace block.
 *
 * \xrefitem stable "Stable" "Stable List" ICU 2.4
 */

/**
 * \def U_NAMESPACE_END
 * This is used to end a declaration of a public ICU C++ API.
 * It ends the versioned-ICU-namespace block begun by U_NAMESPACE_BEGIN.
 *
 * \xrefitem stable "Stable" "Stable List" ICU 2.4
 */

/**
 * \def U_NAMESPACE_USE
 * This is used to specify that the rest of the code uses the
 * public ICU C++ API namespace.
 * \xrefitem stable "Stable" "Stable List" ICU 2.4
 */

/**
 * \def U_NAMESPACE_QUALIFIER
 * This is used to qualify that a function or class is part of
 * the public ICU C++ API namespace.
 *
 * This macro is unnecessary since ICU 49 requires namespace support.
 * You can just use "icu::" instead.
 * \xrefitem stable "Stable" "Stable List" ICU 2.4
 */

#   if U_DISABLE_RENAMING
#       define U_ICU_NAMESPACE icu
        namespace U_ICU_NAMESPACE { }
#   else
#       define U_ICU_NAMESPACE U_ICU_ENTRY_POINT_RENAME(icu)
        namespace U_ICU_NAMESPACE { }
        namespace icu = U_ICU_NAMESPACE;
#   endif

#   define U_NAMESPACE_BEGIN namespace U_ICU_NAMESPACE {
#   define U_NAMESPACE_END }
#   define U_NAMESPACE_USE using namespace U_ICU_NAMESPACE;
#   define U_NAMESPACE_QUALIFIER U_ICU_NAMESPACE::

#   ifndef U_USING_ICU_NAMESPACE
#       if defined(U_COMBINED_IMPLEMENTATION) || defined(U_COMMON_IMPLEMENTATION) || \
                defined(U_I18N_IMPLEMENTATION) || defined(U_IO_IMPLEMENTATION) || \
                defined(U_LAYOUTEX_IMPLEMENTATION) || defined(U_TOOLUTIL_IMPLEMENTATION)
#           define U_USING_ICU_NAMESPACE 0
#       else
#           define U_USING_ICU_NAMESPACE 0
#       endif
#   endif
#   if U_USING_ICU_NAMESPACE
        U_NAMESPACE_USE
#   endif
#endif /* __cplusplus */

/*===========================================================================*/
/* General version helper functions. Definitions in putil.c                  */
/*===========================================================================*/






/**
 * Write a string with dotted-decimal version information according
 * to the input UVersionInfo.
 * Definition of this function lives in putil.c
 *
 * @param versionArray The version information to be written as a string.
 * @param versionString A string buffer that will be filled in with
 *                      a string corresponding to the numeric version
 *                      information in versionArray.
 *                      The buffer size must be at least U_MAX_VERSION_STRING_LENGTH.
 * \xrefitem stable "Stable" "Stable List" ICU 2.4
 */
U_CAPI void U_EXPORT2
u_versionToString(const UVersionInfo versionArray, char *versionString) __INTRODUCED_IN(31);



/**
 * Gets the ICU release version.  The version array stores the version information
 * for ICU.  For example, release "1.3.31.2" is then represented as 0x01031F02.
 * Definition of this function lives in putil.c
 *
 * @param versionArray the version # information, the result will be filled in
 * \xrefitem stable "Stable" "Stable List" ICU 2.0
 */
U_CAPI void U_EXPORT2
u_getVersion(UVersionInfo versionArray) __INTRODUCED_IN(31);


#endif

/** @} */ // addtogroup
