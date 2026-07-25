#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginSource.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Literal)                                                  \
  { (Literal), (uint64_t)(sizeof(Literal) - 1) }

static const uint8_t ThinArchive[] =
    "!<thin>\n"
    "member.nobj/    "
    "0           "
    "0     "
    "0     "
    "100644  "
    "8         "
    "`\n";
static const uint8_t MemberObject[] = "NOBJ-vfs";

static NevercStatus failed(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static int path_ends_with(NevercStringView Path, const char *Suffix) {
  size_t SuffixLength = strlen(Suffix);
  size_t Start;
  size_t Index;
  if (Path.Length < SuffixLength)
    return 0;
  Start = (size_t)Path.Length - SuffixLength;
  for (Index = 0; Index != SuffixLength; ++Index) {
    /* The host hands back paths in its own separator style. */
    char PathCharacter = Path.Data[Start + Index];
    if (PathCharacter == '\\')
      PathCharacter = '/';
    if (PathCharacter != Suffix[Index])
      return 0;
  }
  return 1;
}

static const uint8_t *content_for_path(NevercStringView Path,
                                       uint64_t *Length) {
  if (path_ends_with(Path, "/virtual/libanswers.a")) {
    *Length = sizeof(ThinArchive) - 1;
    return ThinArchive;
  }
  if (path_ends_with(Path, "/virtual/member.nobj")) {
    *Length = sizeof(MemberObject) - 1;
    return MemberObject;
  }
  return NULL;
}

static void fill_status(NevercVFSStatus *Status, uint64_t Size,
                        uint64_t FileID) {
  memset(Status, 0, sizeof(*Status));
  Status->Header.StructSize = sizeof(*Status);
  Status->Header.Major = NEVERC_IO_API_MAJOR;
  Status->Header.Minor = NEVERC_IO_API_MINOR;
  Status->Type = NEVERC_VFS_FILE_REGULAR;
  Status->Permissions = UINT32_C(0444);
  Status->Size = Size;
  Status->ModificationTime = 1;
  Status->UniqueID.Device = UINT64_C(0x4e435641);
  Status->UniqueID.File = FileID;
  Status->Local = NEVERC_FALSE;
}

static NevercStatus NEVERC_CALL
matches_path(NevercTaskHandle Task, NevercStringView Path, void *UserData,
             NevercBool *OutMatches) {
  uint64_t Length = 0;
  (void)Task;
  (void)UserData;
  if (OutMatches == NULL)
    return failed(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMatches =
      content_for_path(Path, &Length) ? NEVERC_TRUE : NEVERC_FALSE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
provider_status(NevercTaskHandle Task, NevercStringView Path, void *UserData,
                NevercVFSStatusResult *OutResult) {
  uint64_t Length = 0;
  const uint8_t *Content;
  (void)Task;
  (void)UserData;
  if (OutResult == NULL)
    return failed(NEVERC_STATUS_INVALID_ARGUMENT);
  Content = content_for_path(Path, &Length);
  if (Content == NULL) {
    OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
    return neverc_status_ok();
  }
  OutResult->Disposition = NEVERC_VFS_RESULT_HANDLED;
  fill_status(&OutResult->Status, Length,
              Content == ThinArchive ? UINT64_C(1) : UINT64_C(2));
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
provider_open(NevercTaskHandle Task, NevercStringView Path, void *UserData,
              NevercVFSOpenReadResult *OutResult) {
  uint64_t Length = 0;
  const uint8_t *Content;
  (void)Task;
  (void)UserData;
  if (OutResult == NULL)
    return failed(NEVERC_STATUS_INVALID_ARGUMENT);
  Content = content_for_path(Path, &Length);
  if (Content == NULL) {
    OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
    return neverc_status_ok();
  }
  OutResult->Disposition = NEVERC_VFS_RESULT_HANDLED;
  fill_status(&OutResult->Status, Length,
              Content == ThinArchive ? UINT64_C(1) : UINT64_C(2));
  OutResult->Content.Header.StructSize = sizeof(OutResult->Content);
  OutResult->Content.Header.Major = NEVERC_IO_API_MAJOR;
  OutResult->Content.Header.Minor = NEVERC_IO_API_MINOR;
  OutResult->Content.Data = Content;
  OutResult->Content.Length = Length;
  OutResult->Content.NullTerminated = NEVERC_FALSE;
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
    return failed(NEVERC_STATUS_ABI_MISMATCH);

  memset(&Provider, 0, sizeof(Provider));
  Provider.Header.StructSize = sizeof(Provider);
  Provider.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Provider.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Provider.ProviderID =
      (NevercStringView)STRING_VIEW("org.neverc.test.virtual-archive");
  Provider.MatchesPath = matches_path;
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Cacheable = NEVERC_TRUE;
  Provider.Status = provider_status;
  Provider.OpenRead = provider_open;
  return ((const NevercIOAPI *)Table)
      ->RegisterVFSProvider(((const NevercIOAPI *)Table)->Context,
                            RegistrarContext, &Provider);
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
    return failed(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Descriptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Descriptor.PluginID =
      (NevercStringView)STRING_VIEW("org.neverc.test.virtual-archive");
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW("NeverC Virtual Archive Test Plugin");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_THREAD_SAFE;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.Register = register_plugin;
  BytesToWrite =
      Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
