#include "neverc/Plugin/NevercPluginAPI.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Literal)                                                   \
  (NevercStringView) { (Literal), (uint64_t)(sizeof(Literal) - 1) }

static const uint8_t VirtualHeader[] =
    "#ifndef NEVERC_EXAMPLE_VIRTUAL_H\n"
    "#define NEVERC_EXAMPLE_VIRTUAL_H\n"
    "#define NEVERC_VIRTUAL_ANSWER 42\n"
    "#endif\n";

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static int path_ends_with(NevercStringView Path, const char *Suffix) {
  const size_t SuffixLength = strlen(Suffix);
  size_t Start;
  size_t Index;
  if (!Path.Data || Path.Length < SuffixLength)
    return 0;
  Start = (size_t)Path.Length - SuffixLength;
  for (Index = 0; Index != SuffixLength; ++Index) {
    char Character = Path.Data[Start + Index];
    if (Character == '\\')
      Character = '/';
    if (Character != Suffix[Index])
      return 0;
  }
  return 1;
}

static void fill_status(NevercVFSStatus *Status, NevercVFSFileType Type,
                        uint64_t Size) {
  memset(Status, 0, sizeof(*Status));
  Status->Header = (NevercABITableHeader){
      sizeof(*Status), NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR, 0};
  Status->Type = Type;
  Status->Permissions = UINT32_C(0444);
  Status->Size = Size;
  Status->UniqueID.Device = UINT64_C(0x4e43564653);
  Status->UniqueID.File =
      Type == NEVERC_VFS_FILE_DIRECTORY ? UINT64_C(1) : UINT64_C(2);
  Status->Local = NEVERC_FALSE;
}

static NevercStatus NEVERC_CALL
matches_path(NevercTaskHandle Task, NevercStringView Path, void *UserData,
             NevercBool *OutMatches) {
  (void)Task;
  (void)UserData;
  if (!OutMatches)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMatches =
      path_ends_with(Path, "neverc-example") ||
              path_ends_with(Path, "neverc-example/virtual.h")
          ? NEVERC_TRUE
          : NEVERC_FALSE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
status_path(NevercTaskHandle Task, NevercStringView Path, void *UserData,
            NevercVFSStatusResult *OutResult) {
  (void)Task;
  (void)UserData;
  if (!OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  if (path_ends_with(Path, "neverc-example")) {
    OutResult->Disposition = NEVERC_VFS_RESULT_HANDLED;
    fill_status(&OutResult->Status, NEVERC_VFS_FILE_DIRECTORY, 0);
  } else if (path_ends_with(Path, "neverc-example/virtual.h")) {
    OutResult->Disposition = NEVERC_VFS_RESULT_HANDLED;
    fill_status(&OutResult->Status, NEVERC_VFS_FILE_REGULAR,
                sizeof(VirtualHeader) - 1);
  } else {
    OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
open_path(NevercTaskHandle Task, NevercStringView Path, void *UserData,
          NevercVFSOpenReadResult *OutResult) {
  (void)Task;
  (void)UserData;
  if (!OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  if (!path_ends_with(Path, "neverc-example/virtual.h")) {
    OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
    return neverc_status_ok();
  }

  OutResult->Disposition = NEVERC_VFS_RESULT_HANDLED;
  fill_status(&OutResult->Status, NEVERC_VFS_FILE_REGULAR,
              sizeof(VirtualHeader) - 1);
  OutResult->Content.Header = (NevercABITableHeader){
      sizeof(OutResult->Content), NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR, 0};
  OutResult->Content.Data = VirtualHeader;
  OutResult->Content.Length = sizeof(VirtualHeader) - 1;
  OutResult->Content.NullTerminated = NEVERC_TRUE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  const void *Table = NULL;
  const NevercIOAPI *IO;
  NevercVFSProviderDescriptor Provider;
  NevercStatus Status;
  uint16_t ActualMinor = 0;
  uint64_t StructSize = 0;
  (void)Registrar;
  (void)ProcessState;

  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_IO_HIGH, NEVERC_INTERFACE_IO_LOW},
      NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR, &Table, &ActualMinor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table || StructSize < sizeof(NevercIOAPI))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  IO = (const NevercIOAPI *)Table;

  memset(&Provider, 0, sizeof(Provider));
  Provider.Header = (NevercABITableHeader){
      sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.ProviderID = STRING_VIEW("org.neverc.example.virtual-header");
  Provider.MatchesPath = matches_path;
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Cacheable = NEVERC_TRUE;
  Provider.Status = status_path;
  Provider.OpenRead = open_path;
  return IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Provider);
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  size_t BytesToWrite;
  if (!Bootstrap || !OutPlugin ||
      OutPlugin->Header.StructSize < (uint32_t)sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.example.virtual-header");
  Descriptor.DisplayName = STRING_VIEW("NeverC virtual header example");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_THREAD_SAFE;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.Register = register_plugin;

  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  OutPlugin->Header.StructSize = sizeof(Descriptor);
  return neverc_status_ok();
}
