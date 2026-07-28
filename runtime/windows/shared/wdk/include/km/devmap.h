// Copyright (C) Microsoft Corporation. All rights reserved.

#include <winapifamily.h>

#pragma region Desktop Family
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)

#if (NTDDI_VERSION >= NTDDI_WIN11_GA)
// {27F01187-5335-440C-A5E1-907E4FB9563F}
DEFINE_GUID(GUID_DEVINTERFACE_DEVMAP,
0x27f01187, 0x5335, 0x440c, 0xa5, 0xe1, 0x90, 0x7e, 0x4f, 0xb9, 0x56, 0x3f);

EXTERN_C DECLSPEC_SELECTANY const UNICODE_STRING SDDL_DEVOBJ_UMHO_ALL =
    RTL_CONSTANT_STRING(L"D:P(A;;GA;;;HO)");

#endif

#endif /* WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP) */
#pragma endregion

