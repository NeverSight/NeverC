#include "LinkInputReaderInternal.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Object/TapiUniversal.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cstring>

using namespace llvm;
using namespace llvm::object;

namespace neverc::plugin {
namespace {

Error sharedError(const LinkInputBlob &Blob, const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "shared library '" + Blob.LogicalURI + "': " +
                               Message);
}

std::string fileName(StringRef URI) {
  StringRef Name = sys::path::filename(URI);
  return (Name.empty() ? URI : Name).str();
}

void addUnique(std::vector<std::string> &Values, StringRef Value) {
  if (!Value.empty() && !llvm::is_contained(Values, Value))
    Values.push_back(Value.str());
}

void addSymbol(std::vector<std::string> &Symbols, StringRef Name) {
  if (Name.empty() ||
      llvm::any_of(Symbols,
                   [&](const std::string &Value) { return Value == Name; }))
    return;
  Symbols.push_back(Name.str());
}

Error readSymbolRange(SymbolicFile::basic_symbol_iterator_range Range,
                      PluginLinkSharedLibrary &Library) {
  for (BasicSymbolRef Symbol : Range) {
    std::string NameStorage;
    raw_string_ostream NameStream(NameStorage);
    if (Error E = Symbol.printName(NameStream))
      return E;
    NameStream.flush();
    StringRef Name(NameStorage);
    auto Flags = Symbol.getFlags();
    if (!Flags)
      return Flags.takeError();
    if ((*Flags & SymbolRef::SF_Undefined) != 0)
      addSymbol(Library.Imports, Name);
    else if ((*Flags & (SymbolRef::SF_Global | SymbolRef::SF_Exported)) != 0)
      addSymbol(Library.Exports, Name);
  }
  return Error::success();
}

template <class ELFT>
Error readELFMetadata(const ELFObjectFile<ELFT> &Object,
                      PluginLinkSharedLibrary &Library) {
  const ELFFile<ELFT> &File = Object.getELFFile();
  auto Entries = File.dynamicEntries();
  if (!Entries)
    return Entries.takeError();

  uint64_t StringAddress = 0;
  uint64_t StringSize = 0;
  for (const typename ELFT::Dyn &Entry : *Entries) {
    if (Entry.getTag() == ELF::DT_STRTAB)
      StringAddress = Entry.getPtr();
    else if (Entry.getTag() == ELF::DT_STRSZ)
      StringSize = Entry.getVal();
  }
  if (StringAddress == 0)
    return Error::success();

  auto StringTable = File.toMappedAddr(StringAddress);
  if (!StringTable)
    return StringTable.takeError();
  const char *Strings = reinterpret_cast<const char *>(*StringTable);
  for (const typename ELFT::Dyn &Entry : *Entries) {
    if (Entry.getTag() != ELF::DT_NEEDED &&
        Entry.getTag() != ELF::DT_SONAME)
      continue;
    const uint64_t Offset = Entry.getVal();
    if (Offset >= StringSize)
      return createStringError(errc::invalid_argument,
                               "ELF dynamic string offset is out of range");
    const size_t Length =
        strnlen(Strings + Offset, static_cast<size_t>(StringSize - Offset));
    if (Length == StringSize - Offset)
      return createStringError(errc::invalid_argument,
                               "ELF dynamic string is not terminated");
    StringRef Value(Strings + Offset, Length);
    if (Entry.getTag() == ELF::DT_SONAME)
      Library.InstallName = Value.str();
    else
      addUnique(Library.NeededLibraries, Value);
  }
  return Error::success();
}

Error readELF(ELFObjectFileBase &Object, PluginLinkSharedLibrary &Library) {
  if (Error E = readSymbolRange(Object.getDynamicSymbolIterators(), Library))
    return E;
  if (auto *Value = dyn_cast<ELF32LEObjectFile>(&Object))
    return readELFMetadata(*Value, Library);
  if (auto *Value = dyn_cast<ELF32BEObjectFile>(&Object))
    return readELFMetadata(*Value, Library);
  if (auto *Value = dyn_cast<ELF64LEObjectFile>(&Object))
    return readELFMetadata(*Value, Library);
  if (auto *Value = dyn_cast<ELF64BEObjectFile>(&Object))
    return readELFMetadata(*Value, Library);
  return createStringError(errc::invalid_argument,
                           "unsupported ELF shared-library encoding");
}

Expected<StringRef> loadCommandString(const MachOObjectFile::LoadCommandInfo &LC,
                                      uint32_t Offset) {
  if (Offset >= LC.C.cmdsize)
    return createStringError(errc::invalid_argument,
                             "Mach-O dylib string offset is out of range");
  const char *Start = LC.Ptr + Offset;
  const size_t Capacity = LC.C.cmdsize - Offset;
  const size_t Length = strnlen(Start, Capacity);
  if (Length == Capacity)
    return createStringError(errc::invalid_argument,
                             "Mach-O dylib string is not terminated");
  return StringRef(Start, Length);
}

Error readMachO(MachOObjectFile &Object,
                PluginLinkSharedLibrary &Library) {
  if (Error E = readSymbolRange(Object.symbols(), Library))
    return E;
  for (const MachOObjectFile::LoadCommandInfo &LC : Object.load_commands()) {
    switch (LC.C.cmd) {
    case MachO::LC_ID_DYLIB:
    case MachO::LC_LOAD_DYLIB:
    case MachO::LC_LOAD_WEAK_DYLIB:
    case MachO::LC_LAZY_LOAD_DYLIB:
    case MachO::LC_REEXPORT_DYLIB:
    case MachO::LC_LOAD_UPWARD_DYLIB: {
      MachO::dylib_command Command = Object.getDylibIDLoadCommand(LC);
      auto Name = loadCommandString(LC, Command.dylib.name);
      if (!Name)
        return Name.takeError();
      if (LC.C.cmd == MachO::LC_ID_DYLIB)
        Library.InstallName = Name->str();
      else
        addUnique(Library.NeededLibraries, *Name);
      break;
    }
    default:
      break;
    }
  }
  return Error::success();
}

Error readCOFF(COFFObjectFile &Object, PluginLinkSharedLibrary &Library) {
  for (const ImportDirectoryEntryRef &Directory :
       Object.import_directories()) {
    StringRef DLL;
    if (Error E = Directory.getName(DLL))
      return E;
    addUnique(Library.NeededLibraries, DLL);
    for (const ImportedSymbolRef &Imported : Directory.imported_symbols()) {
      bool IsOrdinal = false;
      if (Error E = Imported.isOrdinal(IsOrdinal))
        return E;
      if (IsOrdinal)
        continue;
      StringRef Name;
      if (Error E = Imported.getSymbolName(Name))
        return E;
      addSymbol(Library.Imports, Name);
    }
  }
  for (const ExportDirectoryEntryRef &Export : Object.export_directories()) {
    StringRef DLL;
    if (Error E = Export.getDllName(DLL))
      return E;
    if (!DLL.empty())
      Library.InstallName = DLL.str();
    StringRef Name;
    if (Error E = Export.getSymbolName(Name))
      return E;
    addSymbol(Library.Exports, Name);
  }
  return Error::success();
}

Error readTBD(MemoryBufferRef Buffer, PluginLinkSharedLibrary &Library) {
  auto Universal = TapiUniversal::create(Buffer);
  if (!Universal)
    return Universal.takeError();
  const MachO::InterfaceFile &Interface = (*Universal)->getInterfaceFile();
  Library.InstallName = Interface.getInstallName().str();
  for (const MachO::Symbol *Symbol : Interface.exports())
    addSymbol(Library.Exports, Symbol->getName());
  for (const MachO::Symbol *Symbol : Interface.reexports())
    addSymbol(Library.Exports, Symbol->getName());
  for (const MachO::Symbol *Symbol : Interface.undefineds())
    addSymbol(Library.Imports, Symbol->getName());
  for (const MachO::InterfaceFileRef &Reexport :
       Interface.reexportedLibraries())
    addUnique(Library.NeededLibraries, Reexport.getInstallName());
  return Error::success();
}

Error readUniversalMachO(MachOUniversalBinary &Universal,
                         const OwnedTargetKey &Target,
                         PluginLinkSharedLibrary &Library) {
  const NevercTargetKey Key = Target.view();
  Triple TargetTriple(
      StringRef(Key.RawTriple.Data,
                static_cast<size_t>(Key.RawTriple.Length)));
  for (const MachOUniversalBinary::ObjectForArch &Object :
       Universal.objects()) {
    if (Object.getTriple().getArch() != TargetTriple.getArch())
      continue;
    auto Slice = Object.getAsObjectFile();
    if (!Slice)
      return Slice.takeError();
    return readMachO(**Slice, Library);
  }
  return createStringError(
      errc::invalid_argument,
      "Mach-O universal library has no slice for target architecture");
}

} // namespace

Error readSharedLibraryInput(LinkInputSetImpl &Set, LinkInputBlob &Blob,
                             PluginLinkInput &Input) {
  PluginLinkSharedLibrary Library;
  Library.InputID = Input.ID;
  Library.Name = fileName(Blob.LogicalURI);
  Library.ContentDigest = Blob.Digest;
  Library.Origin.InputID = Input.ID;

  const file_magic Magic = identify_magic(Blob.Buffer->getBuffer());
  Error ParseError = [&]() -> Error {
    if (Magic == file_magic::tapi_file) {
      Input.ReaderRoute = "llvm-tapi";
      return readTBD(Blob.Buffer->getMemBufferRef(), Library);
    }
    auto Parsed = createBinary(Blob.Buffer->getMemBufferRef());
    if (!Parsed)
      return joinErrors(sharedError(Blob, "cannot parse input"),
                        Parsed.takeError());
    Binary *BinaryValue = Parsed->get();
    if (auto *ELF = dyn_cast<ELFObjectFileBase>(BinaryValue)) {
      Input.ReaderRoute = "llvm-elf-shared";
      return readELF(*ELF, Library);
    }
    if (auto *MachO = dyn_cast<MachOObjectFile>(BinaryValue)) {
      Input.ReaderRoute = "llvm-macho-dylib";
      return readMachO(*MachO, Library);
    }
    if (auto *Universal = dyn_cast<MachOUniversalBinary>(BinaryValue)) {
      Input.ReaderRoute = "llvm-macho-universal-dylib";
      return readUniversalMachO(*Universal, Set.Target, Library);
    }
    if (auto *COFF = dyn_cast<COFFObjectFile>(BinaryValue)) {
      Input.ReaderRoute = "llvm-coff-image";
      return readCOFF(*COFF, Library);
    }
    return sharedError(Blob, "unsupported shared-library container");
  }();
  if (ParseError)
    return joinErrors(sharedError(Blob, "invalid metadata"),
                      std::move(ParseError));

  if (Library.InstallName.empty())
    Library.InstallName = Library.Name;
  for (const std::string &Name : Library.Exports) {
    PluginLinkSymbol Symbol;
    Symbol.Name = Name;
    Symbol.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
    Symbol.Definition = NEVERC_LINK_SYMBOL_SHARED;
    Symbol.IsExported = true;
    Symbol.Origin.InputID = Input.ID;
    Set.Graph->addSymbol(std::move(Symbol));
  }
  for (const std::string &Name : Library.Imports) {
    PluginLinkSymbol Symbol;
    Symbol.Name = Name;
    Symbol.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
    Symbol.Definition = NEVERC_LINK_SYMBOL_UNDEFINED;
    Symbol.IsImported = true;
    Symbol.Origin.InputID = Input.ID;
    Set.Graph->addSymbol(std::move(Symbol));
  }
  PluginLinkSharedLibrary &Stored =
      Set.Graph->addSharedLibrary(std::move(Library));
  Input.SharedLibraryID = Stored.ID;
  return Error::success();
}

} // namespace neverc::plugin
