#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginSource.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef NEVERC_TEST_VFS_PLUGIN_ID
#define NEVERC_TEST_VFS_PLUGIN_ID "org.neverc.test.vfs"
#endif

#define STRING_VIEW(Literal)                                                  \
  { (Literal), (uint64_t)(sizeof(Literal) - 1) }

static const NevercIOAPI *IO;
static unsigned OpenCount;

static int path_ends_with(NevercStringView Path, const char *Suffix) {
  size_t SuffixLength = strlen(Suffix);
  size_t Start;
  size_t Index;
  if (Path.Length < SuffixLength)
    return 0;
  Start = (size_t)Path.Length - SuffixLength;
  for (Index = 0; Index != SuffixLength; ++Index) {
    char PathCharacter = Path.Data[Start + Index];
    char SuffixCharacter = Suffix[Index];
    if (PathCharacter == '\\')
      PathCharacter = '/';
    if (PathCharacter != SuffixCharacter)
      return 0;
  }
  return 1;
}

static NevercStatus failed(NevercStatusCode Code, uint64_t Detail) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  Status.Detail = Detail;
  return Status;
}

static void fill_status(NevercVFSStatus *Status, uint64_t Size,
                        NevercVFSFileType Type) {
  memset(Status, 0, sizeof(*Status));
  Status->Header.StructSize = sizeof(*Status);
  Status->Header.Major = NEVERC_IO_API_MAJOR;
  Status->Header.Minor = NEVERC_IO_API_MINOR;
  Status->Type = Type;
  Status->Permissions = UINT32_C(0444);
  Status->Size = Size;
  Status->ModificationTime = 17;
  Status->UniqueID.Device = UINT64_C(0x564653);
  Status->UniqueID.File = Type == NEVERC_VFS_FILE_DIRECTORY ? 1 : 2;
  Status->Local = NEVERC_FALSE;
}

static NevercStatus NEVERC_CALL
matches_path(NevercTaskHandle Task, NevercStringView Path, void *UserData,
             NevercBool *OutMatches) {
  (void)Task;
  (void)UserData;
  if (OutMatches == NULL)
    return failed(NEVERC_STATUS_INVALID_ARGUMENT, 0);
  *OutMatches = path_ends_with(Path, "plugin") ||
                        path_ends_with(Path, "plugin/virtual.h")
                    ? NEVERC_TRUE
                    : NEVERC_FALSE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
provider_status(NevercTaskHandle Task, NevercStringView Path, void *UserData,
                NevercVFSStatusResult *OutResult) {
  (void)UserData;
  if (OutResult == NULL)
    return failed(NEVERC_STATUS_INVALID_ARGUMENT, 0);

#if defined(NEVERC_TEST_VFS_ERROR_FALLBACK)
  (void)Task;
  (void)Path;
  return failed(NEVERC_STATUS_PLUGIN_FAILURE,
                NEVERC_IO_ERROR_PERMISSION_DENIED);
#elif defined(NEVERC_TEST_VFS_RECURSIVE)
  {
    NevercVFSStatus Nested;
    memset(&Nested, 0, sizeof(Nested));
    Nested.Header.StructSize = sizeof(Nested);
    return IO->Stat(IO->Context, Task, Path, &Nested);
  }
#elif defined(NEVERC_TEST_VFS_HALF_RESULT)
  (void)Task;
  (void)Path;
  OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
  OutResult->Status.Header.StructSize = sizeof(OutResult->Status);
  return neverc_status_ok();
#elif defined(NEVERC_TEST_VFS_INVALID_DISPOSITION)
  (void)Task;
  (void)Path;
  OutResult->Disposition = UINT32_C(99);
  return neverc_status_ok();
#else
  (void)Task;
  if (path_ends_with(Path, "plugin")) {
    OutResult->Disposition = NEVERC_VFS_RESULT_HANDLED;
    fill_status(&OutResult->Status, 0, NEVERC_VFS_FILE_DIRECTORY);
    return neverc_status_ok();
  }
  if (path_ends_with(Path, "plugin/virtual.h")) {
    OutResult->Disposition = NEVERC_VFS_RESULT_HANDLED;
    fill_status(&OutResult->Status, 9, NEVERC_VFS_FILE_REGULAR);
    return neverc_status_ok();
  }
  OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
  return neverc_status_ok();
#endif
}

static NevercStatus NEVERC_CALL
provider_open(NevercTaskHandle Task, NevercStringView Path, void *UserData,
              NevercVFSOpenReadResult *OutResult) {
  static const uint8_t First[] = "int one;\n";
  static const uint8_t Second[] = "int two;\n";
  const uint8_t *Content;
  (void)Task;
  (void)UserData;
  if (OutResult == NULL)
    return failed(NEVERC_STATUS_INVALID_ARGUMENT, 0);
  if (!path_ends_with(Path, "plugin/virtual.h")) {
    OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
    return neverc_status_ok();
  }
  Content = OpenCount++ == 0 ? First : Second;
  OutResult->Disposition = NEVERC_VFS_RESULT_HANDLED;
#if defined(NEVERC_TEST_VFS_BAD_SIZE)
  fill_status(&OutResult->Status, 10, NEVERC_VFS_FILE_REGULAR);
#else
  fill_status(&OutResult->Status, 9, NEVERC_VFS_FILE_REGULAR);
#endif
  OutResult->Content.Header.StructSize = sizeof(OutResult->Content);
  OutResult->Content.Header.Major = NEVERC_IO_API_MAJOR;
  OutResult->Content.Header.Minor = NEVERC_IO_API_MINOR;
  OutResult->Content.Data = Content;
  OutResult->Content.Length = 9;
  OutResult->Content.NullTerminated = NEVERC_TRUE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
provider_directory(NevercTaskHandle Task, NevercStringView Path,
                   void *UserData, NevercVFSDirectoryResult *OutResult) {
  static NevercVFSDirectoryEntry Entry;
  (void)Task;
  (void)UserData;
  if (OutResult == NULL)
    return failed(NEVERC_STATUS_INVALID_ARGUMENT, 0);
  if (!path_ends_with(Path, "plugin")) {
    OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
    return neverc_status_ok();
  }
  memset(&Entry, 0, sizeof(Entry));
  Entry.Header.StructSize = sizeof(Entry);
  Entry.Header.Major = NEVERC_IO_API_MAJOR;
  Entry.Header.Minor = NEVERC_IO_API_MINOR;
  Entry.Path =
      (NevercStringView)STRING_VIEW("/plugin/virtual.h");
  Entry.Type = NEVERC_VFS_FILE_REGULAR;
  OutResult->Disposition = NEVERC_VFS_RESULT_HANDLED;
  OutResult->Entries = &Entry;
  OutResult->EntryCount = 1;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
provider_canonicalize(NevercTaskHandle Task, NevercStringView Path,
                      void *UserData,
                      NevercVFSCanonicalPathResult *OutResult) {
  (void)Task;
  (void)UserData;
  if (OutResult == NULL)
    return failed(NEVERC_STATUS_INVALID_ARGUMENT, 0);
  if (Path.Length != 0 &&
      (Path.Data[Path.Length - 1] == '/' ||
       Path.Data[Path.Length - 1] == '\\')) {
    NevercStringView WithoutTrailing = Path;
    --WithoutTrailing.Length;
    if (!path_ends_with(WithoutTrailing, "plugin")) {
      OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
      return neverc_status_ok();
    }
    OutResult->Disposition = NEVERC_VFS_RESULT_HANDLED;
    OutResult->Path = WithoutTrailing;
    return neverc_status_ok();
  }
  OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core,
                const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  NevercVFSProviderDescriptor Provider;
  NevercStatus Status;
  (void)Registrar;
  (void)ProcessState;

  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_IO_HIGH,
                          NEVERC_INTERFACE_IO_LOW},
      NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR, &Table, &Minor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Table == NULL || StructSize < sizeof(NevercIOAPI))
    return failed(NEVERC_STATUS_ABI_MISMATCH, 0);
  IO = (const NevercIOAPI *)Table;

  memset(&Provider, 0, sizeof(Provider));
  Provider.Header.StructSize = sizeof(Provider);
  Provider.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Provider.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Provider.ProviderID =
      (NevercStringView)STRING_VIEW(NEVERC_TEST_VFS_PLUGIN_ID);
#if defined(NEVERC_TEST_VFS_ERROR_FALLBACK)
  Provider.RoutePrefix = (NevercStringView)STRING_VIEW("/work");
#else
  Provider.MatchesPath = matches_path;
#endif
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Cacheable = NEVERC_TRUE;
  Provider.Status = provider_status;
  Provider.OpenRead = provider_open;
  Provider.ReadDirectory = provider_directory;
  Provider.Canonicalize = provider_canonicalize;
  return IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Provider);
}

static NevercStatus NEVERC_CALL
destroy_plugin(const NevercCoreAPI *Core, void *ProcessState) {
  (void)Core;
  (void)ProcessState;
  IO = NULL;
  OpenCount = 0;
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  size_t BytesToWrite;
  (void)Bootstrap;
  if (OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return failed(NEVERC_STATUS_INVALID_ARGUMENT, 0);

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Descriptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Descriptor.PluginID =
      (NevercStringView)STRING_VIEW(NEVERC_TEST_VFS_PLUGIN_ID);
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW("NeverC VFS Test Plugin");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.Register = register_plugin;
  Descriptor.Destroy = destroy_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity
                                               : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
