/**************************************************************************\
*
* Copyright (c) 1998-2001, Microsoft Corp.  All Rights Reserved.
*
* Module Name:
*
*   Gdiplus.h
*
* Abstract:
*
*   GDI+ public header file
*
\**************************************************************************/

#ifndef _GDIPLUS_H
#define _GDIPLUS_H
#include <winapifamily.h>

#pragma region Desktop Family
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)

struct IDirectDrawSurface7;

typedef signed short INT16;
typedef unsigned short UINT16;

// Define the Current GDIPlus Version
#ifndef GDIPVER
#define GDIPVER 0x0100
#endif

#include <pshpack8.h> // set structure packing to 8

namespace Gdiplus {
namespace DllExports {
#include "gdiplusmem.h"
};

#include "gdiplusbase.h"

#include "gdiplusenums.h"
#include "gdiplustypes.h"
#include "gdiplusinit.h"
#include "gdipluspixelformats.h"
#include "gdipluscolor.h"
#include "gdiplusmetaheader.h"
#include "gdiplusimaging.h"
#include "gdipluscolormatrix.h"
#if (GDIPVER >= 0x0110)
#include "gdipluseffects.h"
#endif
#include "gdiplusgpstubs.h"
#include "gdiplusheaders.h"

namespace DllExports {
#include "gdiplusflat.h"
};

#include "gdiplusimageattributes.h"
#include "gdiplusmatrix.h"
#include "gdiplusbrush.h"
#include "gdipluspen.h"
#include "gdiplusstringformat.h"
#include "gdipluspath.h"
#include "gdipluslinecaps.h"
#include "gdiplusgraphics.h"
#include "gdiplusmetafile.h"
#include "gdipluscachedbitmap.h"
#include "gdiplusregion.h"
#include "gdiplusfontcollection.h"
#include "gdiplusfontfamily.h"
#include "gdiplusfont.h"
#include "gdiplusbitmap.h"
#include "gdiplusimagecodec.h"

}; // namespace Gdiplus

#include <poppack.h> // pop structure packing back to previous state

#endif /* WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP) */
#pragma endregion

#endif // !_GDIPLUS_HPP
