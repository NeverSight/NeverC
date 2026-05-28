//***************************************************************************
//
//  Copyright (c) Microsoft Corporation.  All rights reserved.
//
//  utillib.h
//
//  Purpose: gather up utillib headers into one catch-all
//
//***************************************************************************

#if _MSC_VER > 1000
#pragma once
#endif

#ifndef UTILLIB_HEADERFILE_IS_INCLUDED
#define UTILLIB_HEADERFILE_IS_INCLUDED

#include <winapifamily.h>

#pragma region Desktop Family
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)

#include <provexce.h>

#include <genlex.h>
#include <objpath.h>
#include <opathlex.h>

#include <chptrarr.h>
#include <chstrarr.h>
#include <chstring.h>
#include <polarity.h>
#include <wbemtime.h>

#ifndef _DBG_ASSERT
#ifdef DBG
#define _DBG_ASSERT(X)                                                         \
  {                                                                            \
    if (!(X)) {                                                                \
      DebugBreak();                                                            \
    }                                                                          \
  }
#else
#define _DBG_ASSERT(X)
#endif
#endif

// MACRO for tracing the safe string return failure  -- currently empty
#ifndef DoTraceHRFailureEmpty
#define DoTraceHRFailureEmpty(hr1, szTraceInfo, hr2) ;
#endif

#endif /* WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP) */
#pragma endregion

#endif
