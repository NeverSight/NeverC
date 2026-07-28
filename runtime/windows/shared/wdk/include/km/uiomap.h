// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#include <winapifamily.h>

#include <assert.h>
#include <string.h>

#ifndef UIOMAP_EXPORT
#define UIOMAP_EXPORT EXTERN_C DECLSPEC_IMPORT
#endif // UIOMAP_EXPORT

#pragma region Desktop Family or OneCore Family
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM)

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding

DECLARE_HANDLE(UIOMAP_DOMAIN_HANDLE);
DECLARE_HANDLE(UIOMAP_DEVICE_HANDLE);
DECLARE_HANDLE(UIOMAP_REGION_HANDLE);

typedef
HRESULT
(*UIOMAP_PFN_DOMAIN_CREATE)(
    _Out_ UIOMAP_DOMAIN_HANDLE * DomainHandle
);

typedef
void
(*UIOMAP_PFN_DOMAIN_DESTROY)(
    _In_ UIOMAP_DOMAIN_HANDLE DomainHandle
);

typedef
void
(WINAPI * UIOMAP_PFN_NOTIFY_DEVICE_REMOVED)(
    void * DeviceRemovedContext
);

typedef struct _UIOMAP_DOMAIN_DEVICE_ATTACH
{

    HANDLE
        DeviceHandle;

    UIOMAP_PFN_NOTIFY_DEVICE_REMOVED
        NotifyDeviceRemoved;

    void *
        NotifyDeviceRemovedContext;

} UIOMAP_DOMAIN_DEVICE_ATTACH;

static
inline
void
UIOMAP_DOMAIN_DEVICE_ATTACH_INIT(
    _Out_ UIOMAP_DOMAIN_DEVICE_ATTACH * Attach,
    _In_ HANDLE DeviceHandle
)
{
    static_assert(sizeof(*Attach) ==
        sizeof(DeviceHandle) + sizeof(UIOMAP_PFN_NOTIFY_DEVICE_REMOVED) + sizeof(void *));
    memset(Attach, 0, sizeof(*Attach));
    Attach->DeviceHandle = DeviceHandle;
}

typedef
HRESULT
(*UIOMAP_PFN_DOMAIN_DEVICE_ATTACH)(
    _In_ UIOMAP_DOMAIN_HANDLE DomainHandle,
    _In_ UIOMAP_DOMAIN_DEVICE_ATTACH const * Attach,
    _Out_ UIOMAP_DEVICE_HANDLE * DeviceHandle
);

typedef
void
(*UIOMAP_PFN_DOMAIN_DEVICE_DETACH)(
    _In_ UIOMAP_DEVICE_HANDLE DeviceHandle
);

typedef enum UIOMAP_REGION_MAP_FLAGS {
    UIOMAP_REGION_MAP_READ = (1<<0),
    UIOMAP_REGION_MAP_WRITE = (1<<1),
} UIOMAP_REGION_MAP_FLAGS;

typedef struct _UIOMAP_DOMAIN_REGION_MAP
{

    void *
        Base;

    SIZE_T
        Size;

    ULONG
        Flags;

    ULONG
        Reserved;

} UIOMAP_DOMAIN_REGION_MAP;

static
inline
void
UIOMAP_DOMAIN_REGION_MAP_INIT(
    _Out_ UIOMAP_DOMAIN_REGION_MAP * Map,
    _In_ void * Base,
    _In_ SIZE_T Size,
    _In_ ULONG Flags
)
{
    static_assert(sizeof(*Map) == sizeof(void *) + sizeof(SIZE_T) + sizeof(ULONG) * 2);
    memset(Map, 0, sizeof(*Map));
    Map->Base = Base;
    Map->Size = Size;
    Map->Flags = Flags;
}

typedef
HRESULT
(*UIOMAP_PFN_DOMAIN_REGION_MAP)(
    _In_ UIOMAP_DOMAIN_HANDLE DomainHandle,
    _Inout_ UIOMAP_DOMAIN_REGION_MAP * Map,
    _Out_ UIOMAP_REGION_HANDLE * RegionHandle
);

typedef
void
(*UIOMAP_PFN_DOMAIN_REGION_UNMAP)(
    _In_ UIOMAP_REGION_HANDLE RegionHandle
);

typedef struct _UIOMAP_VERSION
{

    UINT16
        Major;

    UINT16
        Minor;

    UINT16
        Patch;

} UIOMAP_VERSION;

typedef enum UIOMAP_FEATURE_FLAGS
{
    LOGICAL_IDENTITY_MAPPING = (1<<0),
} UIOMAP_FEATURE_FLAGS;


typedef struct _UIOMAP_API
{

    UIOMAP_PFN_DOMAIN_CREATE
        DomainCreate;

    UIOMAP_PFN_DOMAIN_DESTROY
        DomainDestroy;

    UIOMAP_PFN_DOMAIN_DEVICE_ATTACH
        DomainDeviceAttach;

    UIOMAP_PFN_DOMAIN_DEVICE_DETACH
        DomainDeviceDetach;

    UIOMAP_PFN_DOMAIN_REGION_MAP
        DomainRegionMap;

    UIOMAP_PFN_DOMAIN_REGION_UNMAP
        DomainRegionUnmap;

} UIOMAP_API;

typedef struct _UIOMAP_INTERFACE
{

    UINT16
        Size;

    UIOMAP_VERSION
        Version;

    UINT64
        FeatureFlags;

    union {
        HMODULE
            Module;

        UINT64
            Aligned;
    };

    UIOMAP_API
        Api;

} UIOMAP_INTERFACE;

typedef
HRESULT
(WINAPI * UIOMAP_PFN_LOAD_INTERFACE)(
    _Inout_ UIOMAP_INTERFACE * Interface
);

static
inline
HRESULT
UiomapLoadLibrary(
    _Out_ UIOMAP_INTERFACE * Interface
    )
{
    HRESULT hr = E_NOINTERFACE;

    HMODULE module = LoadLibraryA("uiomapapi.dll");
    if (module == NULL)
    {
        return hr;
    }

    UIOMAP_PFN_LOAD_INTERFACE loadInterface =
        (UIOMAP_PFN_LOAD_INTERFACE)GetProcAddress(module, "UiomapLoadInterface");

    UIOMAP_INTERFACE descriptor = {
        .Size = sizeof(UIOMAP_INTERFACE),
        .Version = { .Major = 1, },
    };

    if (loadInterface == NULL)
    {
        goto error;
    }

    hr = loadInterface(&descriptor);
    if (S_OK != hr)
    {
        goto error;
    }

    RtlCopyMemory(Interface, &descriptor, sizeof(descriptor));
    Interface->Module = module;

    return S_OK;

error:
    FreeLibrary(module);

    return hr;
}

static
inline
void
UiomapUnloadLibrary(
    _In_ UIOMAP_INTERFACE * Interface
)
{
    FreeLibrary(Interface->Module);

    RtlZeroMemory(Interface, sizeof(*Interface));
}

#pragma warning(pop)

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM)
#pragma endregion

