#include "neverc/Plugin/Host/ObjectPluginBridge.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"
#include <limits>
#include <string>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus objectStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

ObjectPluginBridge *bridge(void *Context, NevercTaskHandle Task,
                           NevercStatus &Status) {
  if (!Context) {
    Status = objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return nullptr;
  }
  auto *Bridge = static_cast<ObjectPluginBridge *>(Context);
  if (!sameHandle(Bridge->taskHandle(), Task)) {
    Status = objectStatus(NEVERC_STATUS_WRONG_SCOPE);
    return nullptr;
  }
  Status = neverc_status_ok();
  return Bridge;
}

template <typename T> bool validRecord(const T *Value) {
  return Value && Value->Header.StructSize >= sizeof(T) &&
         Value->Header.Major == NEVERC_OBJECT_API_MAJOR &&
         Value->Header.Minor <= NEVERC_OBJECT_API_MINOR;
}

bool validString(NevercStringView Value) {
  return (Value.Length == 0 || Value.Data != nullptr) &&
         Value.Length <= std::numeric_limits<size_t>::max();
}

bool validBytes(NevercByteView Value) {
  return (Value.Length == 0 || Value.Data != nullptr) &&
         Value.Length <= std::numeric_limits<size_t>::max();
}

std::string copyString(NevercStringView Value) {
  return std::string(Value.Data, static_cast<size_t>(Value.Length));
}

PluginObjectExtension copyExtension(NevercObjectFormatID Owner,
                                    uint32_t Version,
                                    NevercByteView Bytes) {
  PluginObjectExtension Result;
  Result.Owner = Owner;
  Result.Version = Version;
  if (Bytes.Length != 0)
    Result.Bytes.assign(Bytes.Data, Bytes.Data + Bytes.Length);
  return Result;
}

Error statusError(NevercStatus Status, StringRef Operation) {
  return createStringError(
      inconvertibleErrorCode(),
      Operation + " failed with NeverC status " + Twine(Status.Code));
}

template <typename T>
NevercStatus writeHandle(Expected<T> Handle, T *Output) {
  if (!Output) {
    if (!Handle)
      consumeError(Handle.takeError());
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  *Output = {};
  if (!Handle) {
    consumeError(Handle.takeError());
    return objectStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Output = *Handle;
  return neverc_status_ok();
}

template <typename Storage>
bool moveBefore(Storage &Values, uint64_t ID, uint64_t PositionID) {
  auto From = llvm::find_if(
      Values, [ID](const auto &Value) { return Value.ID == ID; });
  if (From == Values.end())
    return false;
  if (PositionID == 0) {
    Values.splice(Values.end(), Values, From);
    return true;
  }
  auto Position = llvm::find_if(
      Values, [PositionID](const auto &Value) {
        return Value.ID == PositionID;
      });
  if (Position == Values.end())
    return false;
  if (From != Position)
    Values.splice(Position, Values, From);
  return true;
}

template <typename Storage>
bool eraseByID(Storage &Values, uint64_t ID) {
  auto It = llvm::find_if(
      Values, [ID](const auto &Value) { return Value.ID == ID; });
  if (It == Values.end())
    return false;
  Values.erase(It);
  return true;
}

NevercStatus NEVERC_CALL CreateSection(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation,
    const NevercObjectSectionDescriptor *Descriptor,
    NevercObjectSectionHandle *OutSection) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor) || !validString(Descriptor->Name) ||
      !validBytes(Descriptor->Data) ||
      !validBytes(Descriptor->Extension))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeHandle(Bridge->createSection(Mutation, *Descriptor),
                     OutSection);
}

NevercStatus NEVERC_CALL ReplaceSection(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation, NevercObjectSectionHandle Section,
    const NevercObjectSectionDescriptor *Descriptor) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor) || !validString(Descriptor->Name) ||
      !validBytes(Descriptor->Data) ||
      !validBytes(Descriptor->Extension))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->replaceSection(Mutation, Section, *Descriptor);
}

NevercStatus NEVERC_CALL MoveSectionBefore(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation, NevercObjectSectionHandle Section,
    NevercObjectSectionHandle Position) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->moveSectionBefore(Mutation, Section, Position)
                : Status;
}

NevercStatus NEVERC_CALL EraseSection(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation,
    NevercObjectSectionHandle Section) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->eraseSection(Mutation, Section) : Status;
}

NevercStatus NEVERC_CALL CreateSymbol(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation,
    const NevercObjectSymbolDescriptor *Descriptor,
    NevercObjectSymbolHandle *OutSymbol) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor) || !validString(Descriptor->Name) ||
      !validBytes(Descriptor->Extension))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeHandle(Bridge->createSymbol(Mutation, *Descriptor),
                     OutSymbol);
}

NevercStatus NEVERC_CALL ReplaceSymbol(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation, NevercObjectSymbolHandle Symbol,
    const NevercObjectSymbolDescriptor *Descriptor) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor) || !validString(Descriptor->Name) ||
      !validBytes(Descriptor->Extension))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->replaceSymbol(Mutation, Symbol, *Descriptor);
}

NevercStatus NEVERC_CALL MoveSymbolBefore(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation, NevercObjectSymbolHandle Symbol,
    NevercObjectSymbolHandle Position) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->moveSymbolBefore(Mutation, Symbol, Position)
                : Status;
}

NevercStatus NEVERC_CALL EraseSymbol(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation, NevercObjectSymbolHandle Symbol) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->eraseSymbol(Mutation, Symbol) : Status;
}

NevercStatus NEVERC_CALL CreateRelocation(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation,
    const NevercObjectRelocationDescriptor *Descriptor,
    NevercObjectRelocationHandle *OutRelocation) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor) ||
      !validBytes(Descriptor->Extension))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeHandle(Bridge->createRelocation(Mutation, *Descriptor),
                     OutRelocation);
}

NevercStatus NEVERC_CALL ReplaceRelocation(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation,
    NevercObjectRelocationHandle Relocation,
    const NevercObjectRelocationDescriptor *Descriptor) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor) ||
      !validBytes(Descriptor->Extension))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->replaceRelocation(Mutation, Relocation, *Descriptor);
}

NevercStatus NEVERC_CALL MoveRelocationBefore(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation,
    NevercObjectRelocationHandle Relocation,
    NevercObjectRelocationHandle Position) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->moveRelocationBefore(Mutation, Relocation,
                                               Position)
                : Status;
}

NevercStatus NEVERC_CALL EraseRelocation(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation,
    NevercObjectRelocationHandle Relocation) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->eraseRelocation(Mutation, Relocation) : Status;
}

NevercStatus NEVERC_CALL CreateComdat(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation,
    const NevercObjectComdatDescriptor *Descriptor,
    NevercObjectComdatHandle *OutComdat) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor) || !validString(Descriptor->Name) ||
      !validBytes(Descriptor->Extension))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeHandle(Bridge->createComdat(Mutation, *Descriptor),
                     OutComdat);
}

NevercStatus NEVERC_CALL ReplaceComdat(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation, NevercObjectComdatHandle Comdat,
    const NevercObjectComdatDescriptor *Descriptor) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor) || !validString(Descriptor->Name) ||
      !validBytes(Descriptor->Extension))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->replaceComdat(Mutation, Comdat, *Descriptor);
}

NevercStatus NEVERC_CALL MoveComdatBefore(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation, NevercObjectComdatHandle Comdat,
    NevercObjectComdatHandle Position) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->moveComdatBefore(Mutation, Comdat, Position)
                : Status;
}

NevercStatus NEVERC_CALL EraseComdat(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation, NevercObjectComdatHandle Comdat) {
  NevercStatus Status;
  ObjectPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->eraseComdat(Mutation, Comdat) : Status;
}

} // namespace

Expected<NevercObjectSectionHandle>
ObjectPluginBridge::createSection(
    NevercObjectMutationHandle Mutation,
    const NevercObjectSectionDescriptor &Descriptor) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return statusError(Status, "CreateSection");
  PluginObjectSection Section;
  Section.ID = Working->allocateEntityID();
  if (Section.ID == 0)
    return createStringError(inconvertibleErrorCode(),
                             "ObjectGraph entity IDs are exhausted");
  Section.Name = copyString(Descriptor.Name);
  Section.Kind = Descriptor.Kind;
  Section.Flags = Descriptor.Flags;
  Section.Alignment = Descriptor.Alignment;
  if (Descriptor.Data.Length != 0)
    Section.Data.assign(Descriptor.Data.Data,
                        Descriptor.Data.Data + Descriptor.Data.Length);
  Section.ZeroFillSize = Descriptor.ZeroFillSize;
  if (!neverc_handle_is_null(Descriptor.Comdat)) {
    PluginObjectComdat *Comdat = nullptr;
    Status = resolveComdat(Descriptor.Comdat, &Comdat);
    if (Status.Code != NEVERC_STATUS_OK)
      return statusError(Status, "CreateSection COMDAT");
    Section.ComdatID = Comdat->ID;
  }
  Section.Extension =
      copyExtension(Descriptor.ExtensionOwner,
                    Descriptor.ExtensionVersion, Descriptor.Extension);
  Working->sections().push_back(std::move(Section));
  return wrapSection(Working->sections().back());
}

NevercStatus ObjectPluginBridge::replaceSection(
    NevercObjectMutationHandle Mutation,
    NevercObjectSectionHandle SectionHandle,
    const NevercObjectSectionDescriptor &Descriptor) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectSection *Section = nullptr;
  Status = resolveSection(SectionHandle, &Section);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectSection Replacement;
  Replacement.ID = Section->ID;
  Replacement.Name = copyString(Descriptor.Name);
  Replacement.Kind = Descriptor.Kind;
  Replacement.Flags = Descriptor.Flags;
  Replacement.Alignment = Descriptor.Alignment;
  if (Descriptor.Data.Length != 0)
    Replacement.Data.assign(
        Descriptor.Data.Data,
        Descriptor.Data.Data + Descriptor.Data.Length);
  Replacement.ZeroFillSize = Descriptor.ZeroFillSize;
  if (!neverc_handle_is_null(Descriptor.Comdat)) {
    PluginObjectComdat *Comdat = nullptr;
    Status = resolveComdat(Descriptor.Comdat, &Comdat);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Replacement.ComdatID = Comdat->ID;
  }
  Replacement.Extension =
      copyExtension(Descriptor.ExtensionOwner,
                    Descriptor.ExtensionVersion, Descriptor.Extension);
  *Section = std::move(Replacement);
  return neverc_status_ok();
}

NevercStatus ObjectPluginBridge::moveSectionBefore(
    NevercObjectMutationHandle Mutation,
    NevercObjectSectionHandle SectionHandle,
    NevercObjectSectionHandle PositionHandle) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectSection *Section = nullptr;
  Status = resolveSection(SectionHandle, &Section);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  uint64_t PositionID = 0;
  if (!neverc_handle_is_null(PositionHandle)) {
    PluginObjectSection *Position = nullptr;
    Status = resolveSection(PositionHandle, &Position);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    PositionID = Position->ID;
  }
  return moveBefore(Working->sections(), Section->ID, PositionID)
             ? neverc_status_ok()
             : objectStatus(NEVERC_STATUS_NOT_FOUND);
}

NevercStatus ObjectPluginBridge::eraseSection(
    NevercObjectMutationHandle Mutation,
    NevercObjectSectionHandle SectionHandle) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectSection *Section = nullptr;
  Status = resolveSection(SectionHandle, &Section);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return eraseByID(Working->sections(), Section->ID)
             ? neverc_status_ok()
             : objectStatus(NEVERC_STATUS_NOT_FOUND);
}

Expected<NevercObjectSymbolHandle>
ObjectPluginBridge::createSymbol(
    NevercObjectMutationHandle Mutation,
    const NevercObjectSymbolDescriptor &Descriptor) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return statusError(Status, "CreateSymbol");
  PluginObjectSymbol Symbol;
  Symbol.ID = Working->allocateEntityID();
  if (Symbol.ID == 0)
    return createStringError(inconvertibleErrorCode(),
                             "ObjectGraph entity IDs are exhausted");
  Symbol.Name = copyString(Descriptor.Name);
  Symbol.Binding = Descriptor.Binding;
  Symbol.Visibility = Descriptor.Visibility;
  Symbol.Type = Descriptor.Type;
  Symbol.Definition = Descriptor.Definition;
  if (!neverc_handle_is_null(Descriptor.Section)) {
    PluginObjectSection *Section = nullptr;
    Status = resolveSection(Descriptor.Section, &Section);
    if (Status.Code != NEVERC_STATUS_OK)
      return statusError(Status, "CreateSymbol section");
    Symbol.SectionID = Section->ID;
  }
  Symbol.Value = Descriptor.Value;
  Symbol.Size = Descriptor.Size;
  Symbol.Alignment = Descriptor.Alignment;
  if (!neverc_handle_is_null(Descriptor.Comdat)) {
    PluginObjectComdat *Comdat = nullptr;
    Status = resolveComdat(Descriptor.Comdat, &Comdat);
    if (Status.Code != NEVERC_STATUS_OK)
      return statusError(Status, "CreateSymbol COMDAT");
    Symbol.ComdatID = Comdat->ID;
  }
  Symbol.Flags = Descriptor.Flags;
  Symbol.Extension =
      copyExtension(Descriptor.ExtensionOwner,
                    Descriptor.ExtensionVersion, Descriptor.Extension);
  Working->symbols().push_back(std::move(Symbol));
  return wrapSymbol(Working->symbols().back());
}

NevercStatus ObjectPluginBridge::replaceSymbol(
    NevercObjectMutationHandle Mutation,
    NevercObjectSymbolHandle SymbolHandle,
    const NevercObjectSymbolDescriptor &Descriptor) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectSymbol *Symbol = nullptr;
  Status = resolveSymbol(SymbolHandle, &Symbol);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectSymbol Replacement;
  Replacement.ID = Symbol->ID;
  Replacement.Name = copyString(Descriptor.Name);
  Replacement.Binding = Descriptor.Binding;
  Replacement.Visibility = Descriptor.Visibility;
  Replacement.Type = Descriptor.Type;
  Replacement.Definition = Descriptor.Definition;
  if (!neverc_handle_is_null(Descriptor.Section)) {
    PluginObjectSection *Section = nullptr;
    Status = resolveSection(Descriptor.Section, &Section);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Replacement.SectionID = Section->ID;
  }
  Replacement.Value = Descriptor.Value;
  Replacement.Size = Descriptor.Size;
  Replacement.Alignment = Descriptor.Alignment;
  if (!neverc_handle_is_null(Descriptor.Comdat)) {
    PluginObjectComdat *Comdat = nullptr;
    Status = resolveComdat(Descriptor.Comdat, &Comdat);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Replacement.ComdatID = Comdat->ID;
  }
  Replacement.Flags = Descriptor.Flags;
  Replacement.Extension =
      copyExtension(Descriptor.ExtensionOwner,
                    Descriptor.ExtensionVersion, Descriptor.Extension);
  *Symbol = std::move(Replacement);
  return neverc_status_ok();
}

NevercStatus ObjectPluginBridge::moveSymbolBefore(
    NevercObjectMutationHandle Mutation,
    NevercObjectSymbolHandle SymbolHandle,
    NevercObjectSymbolHandle PositionHandle) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectSymbol *Symbol = nullptr;
  Status = resolveSymbol(SymbolHandle, &Symbol);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  uint64_t PositionID = 0;
  if (!neverc_handle_is_null(PositionHandle)) {
    PluginObjectSymbol *Position = nullptr;
    Status = resolveSymbol(PositionHandle, &Position);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    PositionID = Position->ID;
  }
  return moveBefore(Working->symbols(), Symbol->ID, PositionID)
             ? neverc_status_ok()
             : objectStatus(NEVERC_STATUS_NOT_FOUND);
}

NevercStatus ObjectPluginBridge::eraseSymbol(
    NevercObjectMutationHandle Mutation,
    NevercObjectSymbolHandle SymbolHandle) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectSymbol *Symbol = nullptr;
  Status = resolveSymbol(SymbolHandle, &Symbol);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return eraseByID(Working->symbols(), Symbol->ID)
             ? neverc_status_ok()
             : objectStatus(NEVERC_STATUS_NOT_FOUND);
}

Expected<NevercObjectRelocationHandle>
ObjectPluginBridge::createRelocation(
    NevercObjectMutationHandle Mutation,
    const NevercObjectRelocationDescriptor &Descriptor) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return statusError(Status, "CreateRelocation");
  PluginObjectRelocation Relocation;
  Relocation.ID = Working->allocateEntityID();
  if (Relocation.ID == 0)
    return createStringError(inconvertibleErrorCode(),
                             "ObjectGraph entity IDs are exhausted");
  PluginObjectSection *Section = nullptr;
  Status = resolveSection(Descriptor.Section, &Section);
  if (Status.Code != NEVERC_STATUS_OK)
    return statusError(Status, "CreateRelocation section");
  Relocation.SectionID = Section->ID;
  Relocation.Offset = Descriptor.Offset;
  Relocation.Kind = Descriptor.Kind;
  Relocation.TargetKind = Descriptor.TargetKind;
  Relocation.Width = Descriptor.Width;
  Relocation.IsPCRelative =
      Descriptor.IsPCRelative == NEVERC_TRUE;
  Relocation.IsSigned = Descriptor.IsSigned == NEVERC_TRUE;
  Relocation.Addend = Descriptor.Addend;
  if (!neverc_handle_is_null(Descriptor.TargetSymbol)) {
    PluginObjectSymbol *Target = nullptr;
    Status = resolveSymbol(Descriptor.TargetSymbol, &Target);
    if (Status.Code != NEVERC_STATUS_OK)
      return statusError(Status, "CreateRelocation symbol");
    Relocation.TargetSymbolID = Target->ID;
  }
  if (!neverc_handle_is_null(Descriptor.TargetSection)) {
    PluginObjectSection *Target = nullptr;
    Status = resolveSection(Descriptor.TargetSection, &Target);
    if (Status.Code != NEVERC_STATUS_OK)
      return statusError(Status, "CreateRelocation target section");
    Relocation.TargetSectionID = Target->ID;
  }
  Relocation.TargetValue = Descriptor.TargetValue;
  Relocation.TargetExtensionKind = Descriptor.TargetExtensionKind;
  Relocation.Extension =
      copyExtension(Descriptor.ExtensionOwner,
                    Descriptor.ExtensionVersion, Descriptor.Extension);
  Working->relocations().push_back(std::move(Relocation));
  return wrapRelocation(Working->relocations().back());
}

NevercStatus ObjectPluginBridge::replaceRelocation(
    NevercObjectMutationHandle Mutation,
    NevercObjectRelocationHandle RelocationHandle,
    const NevercObjectRelocationDescriptor &Descriptor) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectRelocation *Relocation = nullptr;
  Status = resolveRelocation(RelocationHandle, &Relocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectRelocation Replacement;
  Replacement.ID = Relocation->ID;
  PluginObjectSection *Section = nullptr;
  Status = resolveSection(Descriptor.Section, &Section);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Replacement.SectionID = Section->ID;
  Replacement.Offset = Descriptor.Offset;
  Replacement.Kind = Descriptor.Kind;
  Replacement.TargetKind = Descriptor.TargetKind;
  Replacement.Width = Descriptor.Width;
  Replacement.IsPCRelative =
      Descriptor.IsPCRelative == NEVERC_TRUE;
  Replacement.IsSigned = Descriptor.IsSigned == NEVERC_TRUE;
  Replacement.Addend = Descriptor.Addend;
  if (!neverc_handle_is_null(Descriptor.TargetSymbol)) {
    PluginObjectSymbol *Target = nullptr;
    Status = resolveSymbol(Descriptor.TargetSymbol, &Target);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Replacement.TargetSymbolID = Target->ID;
  }
  if (!neverc_handle_is_null(Descriptor.TargetSection)) {
    PluginObjectSection *Target = nullptr;
    Status = resolveSection(Descriptor.TargetSection, &Target);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Replacement.TargetSectionID = Target->ID;
  }
  Replacement.TargetValue = Descriptor.TargetValue;
  Replacement.TargetExtensionKind = Descriptor.TargetExtensionKind;
  Replacement.Extension =
      copyExtension(Descriptor.ExtensionOwner,
                    Descriptor.ExtensionVersion, Descriptor.Extension);
  *Relocation = std::move(Replacement);
  return neverc_status_ok();
}

NevercStatus ObjectPluginBridge::moveRelocationBefore(
    NevercObjectMutationHandle Mutation,
    NevercObjectRelocationHandle RelocationHandle,
    NevercObjectRelocationHandle PositionHandle) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectRelocation *Relocation = nullptr;
  Status = resolveRelocation(RelocationHandle, &Relocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  uint64_t PositionID = 0;
  if (!neverc_handle_is_null(PositionHandle)) {
    PluginObjectRelocation *Position = nullptr;
    Status = resolveRelocation(PositionHandle, &Position);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    PositionID = Position->ID;
  }
  return moveBefore(Working->relocations(), Relocation->ID, PositionID)
             ? neverc_status_ok()
             : objectStatus(NEVERC_STATUS_NOT_FOUND);
}

NevercStatus ObjectPluginBridge::eraseRelocation(
    NevercObjectMutationHandle Mutation,
    NevercObjectRelocationHandle RelocationHandle) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectRelocation *Relocation = nullptr;
  Status = resolveRelocation(RelocationHandle, &Relocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return eraseByID(Working->relocations(), Relocation->ID)
             ? neverc_status_ok()
             : objectStatus(NEVERC_STATUS_NOT_FOUND);
}

Expected<NevercObjectComdatHandle>
ObjectPluginBridge::createComdat(
    NevercObjectMutationHandle Mutation,
    const NevercObjectComdatDescriptor &Descriptor) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return statusError(Status, "CreateComdat");
  PluginObjectComdat Comdat;
  Comdat.ID = Working->allocateEntityID();
  if (Comdat.ID == 0)
    return createStringError(inconvertibleErrorCode(),
                             "ObjectGraph entity IDs are exhausted");
  Comdat.Name = copyString(Descriptor.Name);
  Comdat.Selection = Descriptor.Selection;
  if (!neverc_handle_is_null(Descriptor.AssociatedComdat)) {
    PluginObjectComdat *Associated = nullptr;
    Status = resolveComdat(Descriptor.AssociatedComdat, &Associated);
    if (Status.Code != NEVERC_STATUS_OK)
      return statusError(Status, "CreateComdat parent");
    Comdat.AssociatedComdatID = Associated->ID;
  }
  Comdat.Extension =
      copyExtension(Descriptor.ExtensionOwner,
                    Descriptor.ExtensionVersion, Descriptor.Extension);
  Working->comdats().push_back(std::move(Comdat));
  return wrapComdat(Working->comdats().back());
}

NevercStatus ObjectPluginBridge::replaceComdat(
    NevercObjectMutationHandle Mutation,
    NevercObjectComdatHandle ComdatHandle,
    const NevercObjectComdatDescriptor &Descriptor) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectComdat *Comdat = nullptr;
  Status = resolveComdat(ComdatHandle, &Comdat);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectComdat Replacement;
  Replacement.ID = Comdat->ID;
  Replacement.Name = copyString(Descriptor.Name);
  Replacement.Selection = Descriptor.Selection;
  if (!neverc_handle_is_null(Descriptor.AssociatedComdat)) {
    PluginObjectComdat *Associated = nullptr;
    Status = resolveComdat(Descriptor.AssociatedComdat, &Associated);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Replacement.AssociatedComdatID = Associated->ID;
  }
  Replacement.Extension =
      copyExtension(Descriptor.ExtensionOwner,
                    Descriptor.ExtensionVersion, Descriptor.Extension);
  *Comdat = std::move(Replacement);
  return neverc_status_ok();
}

NevercStatus ObjectPluginBridge::moveComdatBefore(
    NevercObjectMutationHandle Mutation,
    NevercObjectComdatHandle ComdatHandle,
    NevercObjectComdatHandle PositionHandle) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectComdat *Comdat = nullptr;
  Status = resolveComdat(ComdatHandle, &Comdat);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  uint64_t PositionID = 0;
  if (!neverc_handle_is_null(PositionHandle)) {
    PluginObjectComdat *Position = nullptr;
    Status = resolveComdat(PositionHandle, &Position);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    PositionID = Position->ID;
  }
  return moveBefore(Working->comdats(), Comdat->ID, PositionID)
             ? neverc_status_ok()
             : objectStatus(NEVERC_STATUS_NOT_FOUND);
}

NevercStatus ObjectPluginBridge::eraseComdat(
    NevercObjectMutationHandle Mutation,
    NevercObjectComdatHandle ComdatHandle) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectComdat *Comdat = nullptr;
  Status = resolveComdat(ComdatHandle, &Comdat);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return eraseByID(Working->comdats(), Comdat->ID)
             ? neverc_status_ok()
             : objectStatus(NEVERC_STATUS_NOT_FOUND);
}

void initializeObjectBuilderAPI(NevercObjectAPI &API,
                                ObjectPluginBridge &Bridge) {
  API.Context = &Bridge;
  API.CreateSection = CreateSection;
  API.ReplaceSection = ReplaceSection;
  API.MoveSectionBefore = MoveSectionBefore;
  API.EraseSection = EraseSection;
  API.CreateSymbol = CreateSymbol;
  API.ReplaceSymbol = ReplaceSymbol;
  API.MoveSymbolBefore = MoveSymbolBefore;
  API.EraseSymbol = EraseSymbol;
  API.CreateRelocation = CreateRelocation;
  API.ReplaceRelocation = ReplaceRelocation;
  API.MoveRelocationBefore = MoveRelocationBefore;
  API.EraseRelocation = EraseRelocation;
  API.CreateComdat = CreateComdat;
  API.ReplaceComdat = ReplaceComdat;
  API.MoveComdatBefore = MoveComdatBefore;
  API.EraseComdat = EraseComdat;
}

} // namespace neverc::plugin
