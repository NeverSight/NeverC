#pragma once

#include "AndroidKernelReleaseWriterPolicy.h"
#include "Link/AndroidKernelModuleFinalizer.h"
#include "Link/AndroidKernelProfileContractVerifier.h"
#include "Link/AndroidKernelReleaseIdentitySeal.h"
#include "Link/AndroidKernelReleaseInputVerifier.h"
#include "Link/AndroidKernelReleasePipeline.h"
#include "Link/BuiltinObjectMergeAdapter.h"
#include "Link/LinkGraph.h"
#include "Link/ObjectGraphImporter.h"
#include "Link/ObjectMergeProvider.h"
#include "Object/BuiltinELFTableCanonicalizer.h"
#include "Object/BuiltinLLVMObjectWriter.h"
#include "neverc/Foundation/AndroidKernelModuleReleaseNames.h"
#include "neverc/Foundation/AndroidKernelModuleSectionPolicy.h"
#include "neverc/Foundation/AndroidKernelModuleSymbolPolicy.h"
#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "neverc/Linker/Core/Driver/Dispatcher.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinObjectExtension.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/LinkExecutionHooksBridge.h"
#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/Host/NativeELFSectionFacts.h"
#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/ObjectWriterProvider.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

static_assert(
    !std::is_default_constructible_v<AndroidKernelReleaseInputContract>,
    "an audited Android release input contract must not be forgeable");
static_assert(!std::is_aggregate_v<AndroidKernelReleaseInputContract>,
              "Android release input contract fields must remain private");
static_assert(
    !std::is_default_constructible_v<AndroidKernelReleaseBoundOutputContract>,
    "a bound native-output contract must not be forgeable");
static_assert(!std::is_aggregate_v<AndroidKernelReleaseBoundOutputContract>,
              "bound native-output contract fields must remain private");
static_assert(
    !std::is_copy_constructible_v<AndroidKernelReleaseBoundOutputContract>,
    "a bound native-output token must preserve object identity");
static_assert(
    !std::is_default_constructible_v<
        AndroidKernelReleaseNativeOutputBindingAuthority>,
    "only the direct built-in adapter may mint a bound native output");
static_assert(!std::is_copy_constructible_v<
                  AndroidKernelReleaseNativeOutputBindingAuthority>,
              "native-output binding authority must not escape by copying");

constexpr NevercTargetID TestTargetID{UINT64_C(0x4e43504d45524745),
                                      UINT64_C(1)};
constexpr NevercObjectFormatID TestFormatID{UINT64_C(0x4e43504d45524746),
                                            UINT64_C(1)};
constexpr NevercInterfaceID TestProductID{UINT64_C(0x4e43504d45524750),
                                          UINT64_C(1)};

std::string errorText(Error Value) {
  return toString(std::move(Value)).str().str();
}

Expected<OwnedTargetKey>
makeTargetKey(NevercTargetID TargetID = TestTargetID,
              NevercObjectFormatID FormatID = TestFormatID) {
  return TargetKeyBuilder()
      .setTargetID(TargetID)
      .setTriple("x86_64-neverc-none", "x86_64", "neverc", "none", "")
      .setCPU("generic", "generic")
      .setFeatures({})
      .setABI({UINT64_C(0x4e43504142495401), UINT64_C(1)})
      .setCallingConvention({UINT64_C(0x4e43504343495401), UINT64_C(1)})
      .setObjectFormat(FormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest("0123456789abcdef0123456789abcdef"
                       "0123456789abcdef0123456789abcdef")
      .build();
}

Expected<OwnedTargetKey> makeAndroidTestTargetKey() {
  return TargetKeyBuilder()
      .setTargetID(TestTargetID)
      .setTriple("aarch64-linux-android", "aarch64", "unknown", "linux",
                 "android")
      .setCPU("generic", "generic")
      .setFeatures({})
      .setABI({UINT64_C(0x4e43504142495401), UINT64_C(1)})
      .setCallingConvention({UINT64_C(0x4e43504343495401), UINT64_C(1)})
      .setObjectFormat(TestFormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_KERNEL, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest("0123456789abcdef0123456789abcdef"
                       "0123456789abcdef0123456789abcdef")
      .build();
}

std::unique_ptr<PluginObjectGraph> makeObject(unsigned SectionCount) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return nullptr;
  }
  auto Graph = std::make_unique<PluginObjectGraph>(std::move(*Target));
  for (unsigned I = 0; I != SectionCount; ++I) {
    PluginObjectSection Section;
    Section.ID = Graph->allocateEntityID();
    Section.Name = ".input." + std::to_string(I);
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    Section.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
    Section.Alignment = 1;
    Section.Data = {static_cast<uint8_t>(I)};
    Graph->sections().push_back(std::move(Section));
  }
  return Graph;
}

std::unique_ptr<PluginObjectGraph> makeAndroidObject(unsigned SectionCount) {
  auto Target = makeAndroidTestTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return nullptr;
  }
  auto Graph = std::make_unique<PluginObjectGraph>(std::move(*Target));
  for (unsigned I = 0; I != SectionCount; ++I) {
    PluginObjectSection Section;
    Section.ID = Graph->allocateEntityID();
    Section.Name = ".input." + std::to_string(I);
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    Section.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
    Section.Alignment = 1;
    Section.Data = {static_cast<uint8_t>(I)};
    Graph->sections().push_back(std::move(Section));
  }
  return Graph;
}

struct AndroidKernelContractEntities {
  uint64_t SectionID;
  uint64_t SymbolID;
};

AndroidKernelContractEntities
addAndroidKernelProfileContract(PluginObjectGraph &Graph) {
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".neverc.android.kernel.profile";
  Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED;
  Section.Alignment = 8;
  // Native contract v1 for profile 612 with normalized KCFI and dynamic SCS.
  Section.Data = {UINT8_C(0x02), UINT8_C(0x02), UINT8_C(0x01), UINT8_C(0x00),
                  UINT8_C(0x64), UINT8_C(0x02), UINT8_C(0x00), UINT8_C(0x00)};
  const uint64_t SectionID = Section.ID;
  Graph.sections().push_back(std::move(Section));

  PluginObjectSymbol Symbol;
  Symbol.ID = Graph.allocateEntityID();
  Symbol.Name = "__neverc_android_kernel_profile_contract";
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Symbol.SectionID = SectionID;
  Symbol.Size = 8;
  Symbol.Alignment = 8;
  const uint64_t SymbolID = Symbol.ID;
  Graph.symbols().push_back(std::move(Symbol));
  return {SectionID, SymbolID};
}

void initializeBuiltinTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();
  });
}

const BuiltinTargetRoute *findAndroidAArch64ObjectRoute() {
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64 && Parsed.isAndroid())
      return &Route;
  }
  return nullptr;
}

Expected<OwnedTargetKey> makeBuiltinTargetKey(const BuiltinTargetRoute &Route) {
  Triple Parsed(Triple::normalize(Route.CanonicalTriple));
  return TargetKeyBuilder()
      .setTargetID(Route.TargetID)
      .setTriple(Route.CanonicalTriple.str(), Parsed.getArchName().str(),
                 Parsed.getVendorName().str(), Parsed.getOSName().str(),
                 Parsed.getEnvironmentName().str())
      .setCPU(Route.DefaultCPU.str(), Route.DefaultCPU.str())
      .setFeatures({})
      .setABI(Route.ABIID)
      .setCallingConvention({UINT64_C(0x4e434f424a4d4343), Route.TargetID.Low})
      .setObjectFormat(Route.ObjectFormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest("0123456789abcdef0123456789abcdef"
                       "0123456789abcdef0123456789abcdef")
      .build();
}

Expected<std::vector<uint8_t>>
assembleBuiltinObject(const BuiltinTargetRoute &Route, StringRef Assembly) {
  auto Target = lookupBuiltinLLVMTarget(Route);
  if (!Target)
    return Target.takeError();
  SmallVector<char, 0> Bytes;
  raw_svector_ostream Output(Bytes);
  BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple = Triple(Triple::normalize(Route.CanonicalTriple));
  Request.CPU = Route.DefaultCPU;
  Request.Input = MemoryBufferRef(Assembly, "<plugin-object-merge-test>");
  Request.Output = &Output;
  if (Error E = runBuiltinLLVMAsmParser(Request))
    return std::move(E);
  return std::vector<uint8_t>(Bytes.begin(), Bytes.end());
}

template <typename Mutator>
Error patchELF64Symbol(std::vector<uint8_t> &Bytes, StringRef SymbolName,
                       Mutator &&Apply) {
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    if (Section.sh_type != ELF::SHT_SYMTAB)
      continue;
    auto Symbols = Parsed->symbols(&Section);
    auto Strings = Parsed->getStringTableForSymtab(Section);
    if (!Symbols)
      return Symbols.takeError();
    if (!Strings)
      return Strings.takeError();
    for (const object::ELF64LE::Sym &Symbol : *Symbols) {
      auto Name = Symbol.getName(*Strings);
      if (!Name)
        return Name.takeError();
      if (*Name != SymbolName)
        continue;
      object::ELF64LE::Sym Replacement = Symbol;
      Apply(Replacement);
      const auto *SymbolBytes = reinterpret_cast<const uint8_t *>(&Symbol);
      if (SymbolBytes < Bytes.data())
        return createStringError(inconvertibleErrorCode(),
                                 "test symbol precedes ELF image");
      const size_t Offset = static_cast<size_t>(SymbolBytes - Bytes.data());
      if (Offset > Bytes.size() || sizeof(Replacement) > Bytes.size() - Offset)
        return createStringError(inconvertibleErrorCode(),
                                 "test symbol exceeds ELF image");
      std::memcpy(Bytes.data() + Offset, &Replacement, sizeof(Replacement));
      return Error::success();
    }
  }
  return createStringError(inconvertibleErrorCode(),
                           "test ELF symbol was not found");
}

Error patchELF64SymbolSectionIndex(std::vector<uint8_t> &Bytes,
                                   StringRef SymbolName,
                                   uint16_t SectionIndex) {
  return patchELF64Symbol(Bytes, SymbolName,
                          [SectionIndex](object::ELF64LE::Sym &Symbol) {
                            Symbol.st_shndx = SectionIndex;
                          });
}

Expected<uint16_t> findELF64SectionIndex(ArrayRef<uint8_t> Bytes,
                                         StringRef SectionName) {
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();
  for (size_t Index = 0; Index != Sections->size(); ++Index) {
    auto Name = Parsed->getSectionName((*Sections)[Index]);
    if (!Name)
      return Name.takeError();
    if (*Name != SectionName)
      continue;
    if (Index > std::numeric_limits<uint16_t>::max())
      return createStringError(inconvertibleErrorCode(),
                               "test ELF section index exceeds ELF64 st_shndx");
    return static_cast<uint16_t>(Index);
  }
  return createStringError(inconvertibleErrorCode(),
                           "test ELF section was not found");
}

Expected<std::pair<uint64_t, uint64_t>>
findELF64SectionFileRange(ArrayRef<uint8_t> Bytes, StringRef SectionName) {
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    auto Name = Parsed->getSectionName(Section);
    if (!Name)
      return Name.takeError();
    if (*Name != SectionName)
      continue;
    if (Section.sh_type == ELF::SHT_NOBITS || Section.sh_size == 0 ||
        Section.sh_offset > Bytes.size() ||
        Section.sh_size > Bytes.size() - Section.sh_offset)
      return createStringError(inconvertibleErrorCode(),
                               "test ELF section has no file payload");
    return std::make_pair(static_cast<uint64_t>(Section.sh_offset),
                          static_cast<uint64_t>(Section.sh_size));
  }
  return createStringError(inconvertibleErrorCode(),
                           "test ELF section was not found");
}

Error replaceELF64SymbolNameBytes(std::vector<uint8_t> &Bytes,
                                  StringRef Original, StringRef Replacement) {
  if (Original.size() != Replacement.size() || Original.empty())
    return createStringError(inconvertibleErrorCode(),
                             "test symbol names have different widths");
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    if (Section.sh_type != ELF::SHT_SYMTAB)
      continue;
    auto Symbols = Parsed->symbols(&Section);
    auto Strings = Parsed->getStringTableForSymtab(Section);
    if (!Symbols)
      return Symbols.takeError();
    if (!Strings)
      return Strings.takeError();
    for (const object::ELF64LE::Sym &Symbol : *Symbols) {
      auto Name = Symbol.getName(*Strings);
      if (!Name)
        return Name.takeError();
      if (*Name != Original)
        continue;
      const auto *NameBytes = reinterpret_cast<const uint8_t *>(Name->data());
      if (NameBytes < Bytes.data())
        return createStringError(inconvertibleErrorCode(),
                                 "test symbol name precedes ELF image");
      const size_t Offset = static_cast<size_t>(NameBytes - Bytes.data());
      if (Offset > Bytes.size() || Name->size() > Bytes.size() - Offset)
        return createStringError(inconvertibleErrorCode(),
                                 "test symbol name exceeds ELF image");
      std::memcpy(Bytes.data() + Offset, Replacement.data(),
                  Replacement.size());
      return Error::success();
    }
  }
  return createStringError(inconvertibleErrorCode(),
                           "test ELF symbol-name string was not found");
}

Error swapELF64SymbolNameBytes(std::vector<uint8_t> &Bytes, StringRef First,
                               StringRef Second) {
  if (First.size() != Second.size() || First.empty() || First == Second)
    return createStringError(inconvertibleErrorCode(),
                             "test symbol names cannot be exchanged");
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();

  std::optional<size_t> FirstOffset;
  std::optional<size_t> SecondOffset;
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    if (Section.sh_type != ELF::SHT_SYMTAB)
      continue;
    auto Symbols = Parsed->symbols(&Section);
    auto Strings = Parsed->getStringTableForSymtab(Section);
    if (!Symbols)
      return Symbols.takeError();
    if (!Strings)
      return Strings.takeError();
    for (const object::ELF64LE::Sym &Symbol : *Symbols) {
      auto Name = Symbol.getName(*Strings);
      if (!Name)
        return Name.takeError();
      if (*Name != First && *Name != Second)
        continue;
      const auto *NameBytes = reinterpret_cast<const uint8_t *>(Name->data());
      if (NameBytes < Bytes.data())
        return createStringError(inconvertibleErrorCode(),
                                 "test symbol name precedes ELF image");
      const size_t Offset = static_cast<size_t>(NameBytes - Bytes.data());
      if (Offset > Bytes.size() || Name->size() > Bytes.size() - Offset)
        return createStringError(inconvertibleErrorCode(),
                                 "test symbol name exceeds ELF image");
      std::optional<size_t> &Destination =
          *Name == First ? FirstOffset : SecondOffset;
      if (Destination && *Destination != Offset)
        return createStringError(inconvertibleErrorCode(),
                                 "test ELF has duplicate symbol-name strings");
      Destination = Offset;
    }
  }
  if (!FirstOffset || !SecondOffset)
    return createStringError(inconvertibleErrorCode(),
                             "test ELF symbol-name string was not found");
  std::memcpy(Bytes.data() + *FirstOffset, Second.data(), Second.size());
  std::memcpy(Bytes.data() + *SecondOffset, First.data(), First.size());
  return Error::success();
}

Error replaceELF64SectionNameBytes(std::vector<uint8_t> &Bytes,
                                   StringRef Original, StringRef Replacement,
                                   unsigned Occurrence = 0) {
  if (Original.size() != Replacement.size() || Original.empty())
    return createStringError(inconvertibleErrorCode(),
                             "test section names have different widths");
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();
  unsigned Seen = 0;
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    auto Name = Parsed->getSectionName(Section);
    if (!Name)
      return Name.takeError();
    if (*Name != Original || Seen++ != Occurrence)
      continue;
    const auto *NameBytes = reinterpret_cast<const uint8_t *>(Name->data());
    if (NameBytes < Bytes.data())
      return createStringError(inconvertibleErrorCode(),
                               "test section name precedes ELF image");
    const size_t Offset = static_cast<size_t>(NameBytes - Bytes.data());
    if (Offset > Bytes.size() || Name->size() > Bytes.size() - Offset)
      return createStringError(inconvertibleErrorCode(),
                               "test section name exceeds ELF image");
    std::memcpy(Bytes.data() + Offset, Replacement.data(), Replacement.size());
    return Error::success();
  }
  return createStringError(inconvertibleErrorCode(),
                           "test ELF section-name string was not found");
}

template <typename Mutator>
Error patchELF64SectionHeader(std::vector<uint8_t> &Bytes,
                              StringRef SectionName, unsigned Occurrence,
                              Mutator &&Apply) {
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();
  unsigned Seen = 0;
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    auto Name = Parsed->getSectionName(Section);
    if (!Name)
      return Name.takeError();
    if (*Name != SectionName || Seen++ != Occurrence)
      continue;
    object::ELF64LE::Shdr Replacement = Section;
    Apply(Replacement);
    const auto *SectionBytes = reinterpret_cast<const uint8_t *>(&Section);
    if (SectionBytes < Bytes.data())
      return createStringError(inconvertibleErrorCode(),
                               "test section precedes ELF image");
    const size_t Offset = static_cast<size_t>(SectionBytes - Bytes.data());
    if (Offset > Bytes.size() || sizeof(Replacement) > Bytes.size() - Offset)
      return createStringError(inconvertibleErrorCode(),
                               "test section exceeds ELF image");
    std::memcpy(Bytes.data() + Offset, &Replacement, sizeof(Replacement));
    return Error::success();
  }
  return createStringError(inconvertibleErrorCode(),
                           "test ELF section occurrence was not found");
}

template <typename Mutator>
Error patchELF64SectionHeaderAtIndex(std::vector<uint8_t> &Bytes,
                                     unsigned Index, Mutator &&Apply) {
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();
  if (Index >= Sections->size())
    return createStringError(inconvertibleErrorCode(),
                             "test ELF section index is out of range");
  const object::ELF64LE::Shdr &Section = (*Sections)[Index];
  object::ELF64LE::Shdr Replacement = Section;
  Apply(Replacement);
  const auto *SectionBytes = reinterpret_cast<const uint8_t *>(&Section);
  if (SectionBytes < Bytes.data())
    return createStringError(inconvertibleErrorCode(),
                             "test section precedes ELF image");
  const size_t Offset = static_cast<size_t>(SectionBytes - Bytes.data());
  if (Offset > Bytes.size() || sizeof(Replacement) > Bytes.size() - Offset)
    return createStringError(inconvertibleErrorCode(),
                             "test section exceeds ELF image");
  std::memcpy(Bytes.data() + Offset, &Replacement, sizeof(Replacement));
  return Error::success();
}

template <typename Mutator>
Error patchELF64Header(std::vector<uint8_t> &Bytes, Mutator &&Apply) {
  if (Bytes.size() < sizeof(object::ELF64LE::Ehdr))
    return createStringError(inconvertibleErrorCode(),
                             "test ELF header is truncated");
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  object::ELF64LE::Ehdr Replacement = Parsed->getHeader();
  Apply(Replacement);
  std::memcpy(Bytes.data(), &Replacement, sizeof(Replacement));
  return Error::success();
}

Expected<std::vector<uint8_t>>
assembleValidAndroidReleaseInput(const BuiltinTargetRoute &Route) {
  constexpr StringLiteral Assembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    nop
    .size init_module, .-init_module

    .section .modinfo,"a",%progbits
    .asciz "license=GPL"

    .section __versions,"a",%progbits
    .balign 8
    .space 64

    .section .codetag.alloc_tags,"aw",%progbits
    .balign 8
    .xword 0

    .section .gnu.linkonce.this_module,"aw",%progbits
    .balign 64
    .space 1024

    .section .native_extra,"a",%progbits
    .balign 8
    .xword 0x8877665544332211

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 2, 1, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  return assembleBuiltinObject(Route, Assembly);
}

Expected<std::vector<uint8_t>>
assembleAndroidReleaseInputWithProtectedSectionSymbol(
    const BuiltinTargetRoute &Route) {
  constexpr StringLiteral Assembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    nop
    .size init_module, .-init_module

    .section .modinfo,"a",%progbits
    .globl modinfo_key
    .type modinfo_key, %object
modinfo_key:
    .asciz "license=GPL"
    .size modinfo_key, .-modinfo_key

    .section __versions,"a",%progbits
    .balign 8
    .space 64

    .section .codetag.alloc_tags,"aw",%progbits
    .balign 8
    .xword 0

    .section .gnu.linkonce.this_module,"aw",%progbits
    .balign 64
    .space 1024

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 2, 1, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  return assembleBuiltinObject(Route, Assembly);
}

Expected<std::vector<uint8_t>>
assembleAndroidReleaseInputWithTwoImports(const BuiltinTargetRoute &Route) {
  constexpr StringLiteral Assembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    .xword kernel_one
    .xword kernel_two
    ret
    .size init_module, .-init_module

    .section .modinfo,"a",%progbits
    .asciz "license=GPL"

    .section __versions,"a",%progbits
    .balign 8
    .space 64

    .section .codetag.alloc_tags,"aw",%progbits
    .balign 8
    .xword 0

    .section .gnu.linkonce.this_module,"aw",%progbits
    .balign 64
    .space 1024

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 2, 1, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  return assembleBuiltinObject(Route, Assembly);
}

Expected<std::vector<uint8_t>>
assembleAndroidReleaseInputWithInitPLT(const BuiltinTargetRoute &Route) {
  constexpr StringLiteral Assembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    ret
    .size init_module, .-init_module

    .section .init.plt,"ax",%progbits
    .balign 16
    .space 16

    .section .modinfo,"a",%progbits
    .asciz "license=GPL"

    .section __versions,"a",%progbits
    .balign 8
    .space 64

    .section .codetag.alloc_tags,"aw",%progbits
    .balign 8
    .xword 0

    .section .gnu.linkonce.this_module,"aw",%progbits
    .balign 64
    .space 1024

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 2, 1, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  return assembleBuiltinObject(Route, Assembly);
}

Expected<std::vector<uint8_t>>
assembleAndroidReleaseInputWithNamedSection(const BuiltinTargetRoute &Route) {
  constexpr StringLiteral Assembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    .xword section_key
    ret
    .size init_module, .-init_module

    .section .rodata,"a",%progbits
    .xword 0x8877665544332211

    .section .modinfo,"a",%progbits
    .asciz "license=GPL"

    .section __versions,"a",%progbits
    .balign 8
    .space 64

    .section .codetag.alloc_tags,"aw",%progbits
    .balign 8
    .xword 0

    .section .gnu.linkonce.this_module,"aw",%progbits
    .balign 64
    .space 1024

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 2, 1, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  return assembleBuiltinObject(Route, Assembly);
}

std::unique_ptr<PluginObjectGraph>
makeBuiltinObject(const BuiltinTargetRoute &Route, StringRef SymbolName) {
  auto Target = makeBuiltinTargetKey(Route);
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return nullptr;
  }
  auto Graph = std::make_unique<PluginObjectGraph>(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph->allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0xc3)};
  const uint64_t SectionID = Section.ID;
  Graph->sections().push_back(std::move(Section));

  PluginObjectSymbol Symbol;
  Symbol.ID = Graph->allocateEntityID();
  Symbol.Name = SymbolName.str();
  Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Symbol.SectionID = SectionID;
  Symbol.Size = 1;
  Symbol.Alignment = 1;
  Graph->symbols().push_back(std::move(Symbol));
  Graph->issueLayoutProof();
  return Graph;
}

SmallVector<uint8_t, 64> makeELFSectionExtension(uint32_t Version,
                                                 uint64_t Type, uint64_t Flags,
                                                 uint64_t EntrySize,
                                                 bool IncludeEntrySize = true,
                                                 uint64_t NativeIndex = 1) {
  SmallVector<uint8_t, 64> Bytes;
  neverc::plugin::builtinext::appendHeader(
      Bytes, neverc::plugin::builtinext::SectionTag, Version);
  neverc::plugin::builtinext::appendU64(Bytes, NativeIndex);
  neverc::plugin::builtinext::appendU64(Bytes, 0);
  neverc::plugin::builtinext::appendU64(Bytes, Type);
  neverc::plugin::builtinext::appendU64(Bytes, Flags);
  neverc::plugin::builtinext::appendU64(Bytes, 64);
  if (IncludeEntrySize)
    neverc::plugin::builtinext::appendU64(Bytes, EntrySize);
  return Bytes;
}

void attachCanonicalELFSectionFacts(PluginObjectGraph &Graph) {
  uint64_t NativeIndex = 1;
  for (PluginObjectSection &Section : Graph.sections()) {
    const bool ZeroFill =
        Section.Kind == NEVERC_OBJECT_SECTION_KIND_ZERO_FILL ||
        Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL;
    uint64_t NativeFlags = 0;
    if ((Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0)
      NativeFlags |= ELF::SHF_ALLOC;
    if ((Section.Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0)
      NativeFlags |= ELF::SHF_WRITE;
    if ((Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0)
      NativeFlags |= ELF::SHF_EXECINSTR;
    if ((Section.Flags & NEVERC_OBJECT_SECTION_MERGEABLE) != 0)
      NativeFlags |= ELF::SHF_MERGE;
    if ((Section.Flags & NEVERC_OBJECT_SECTION_STRINGS) != 0)
      NativeFlags |= ELF::SHF_STRINGS;
    if ((Section.Flags & NEVERC_OBJECT_SECTION_TLS) != 0)
      NativeFlags |= ELF::SHF_TLS;
    const uint64_t EntrySize =
        (Section.Flags & NEVERC_OBJECT_SECTION_MERGEABLE) != 0 ? 1 : 0;
    const SmallVector<uint8_t, 64> NativeFacts =
        makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                                ZeroFill ? ELF::SHT_NOBITS : ELF::SHT_PROGBITS,
                                NativeFlags, EntrySize, true, NativeIndex++);
    Section.Extension.Owner = Graph.formatID();
    Section.Extension.Version = neverc::plugin::builtinext::SectionVersion;
    Section.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
  }
}

SmallVector<uint8_t, 48> makeELFSymbolExtension(
    uint32_t Version, uint64_t Type, uint64_t Binding, uint64_t Other,
    uint64_t Auxiliary,
    uint64_t NameState = neverc::plugin::builtinext::SymbolNameNonEmpty) {
  SmallVector<uint8_t, 48> Bytes;
  neverc::plugin::builtinext::appendHeader(
      Bytes, neverc::plugin::builtinext::SymbolTag, Version);
  neverc::plugin::builtinext::appendU64(Bytes, Type);
  neverc::plugin::builtinext::appendU64(Bytes, Binding);
  neverc::plugin::builtinext::appendU64(Bytes, Other);
  neverc::plugin::builtinext::appendU64(Bytes, Auxiliary);
  if (Version >= 2)
    neverc::plugin::builtinext::appendU64(Bytes, NameState);
  return Bytes;
}

SmallVector<uint8_t, 80>
makeELFRelocationExtension(uint32_t Version, uint64_t Type, StringRef Name) {
  SmallVector<uint8_t, 80> Bytes;
  neverc::plugin::builtinext::appendHeader(
      Bytes, neverc::plugin::builtinext::RelocationTag, Version);
  neverc::plugin::builtinext::appendU64(Bytes, Type);
  neverc::plugin::builtinext::appendU32(Bytes,
                                        static_cast<uint32_t>(Name.size()));
  neverc::plugin::builtinext::appendBytes(Bytes, Name);
  return Bytes;
}

void overwriteExtensionU64(MutableArrayRef<uint8_t> Bytes, size_t Field,
                           uint64_t Value) {
  const size_t Offset =
      neverc::plugin::builtinext::HeaderSize + Field * sizeof(uint64_t);
  ASSERT_LE(Offset + sizeof(uint64_t), Bytes.size());
  for (unsigned I = 0; I != sizeof(uint64_t); ++I)
    Bytes[Offset + I] = static_cast<uint8_t>(Value >> (I * 8));
}

struct ELFSectionSemantics {
  std::string Name;
  uint32_t Type = 0;
  uint64_t Flags = 0;
  uint64_t Alignment = 0;
  uint64_t Size = 0;
  std::vector<uint8_t> Data;

  bool operator==(const ELFSectionSemantics &Other) const {
    return std::tie(Name, Type, Flags, Alignment, Size, Data) ==
           std::tie(Other.Name, Other.Type, Other.Flags, Other.Alignment,
                    Other.Size, Other.Data);
  }
};

struct ELFSymbolSemantics {
  std::string Name;
  std::string Section;
  uint64_t Value = 0;
  uint64_t Size = 0;
  uint8_t Type = 0;
  uint8_t Binding = 0;
  uint8_t Visibility = 0;

  bool operator==(const ELFSymbolSemantics &Other) const {
    return std::tie(Name, Section, Value, Size, Type, Binding, Visibility) ==
           std::tie(Other.Name, Other.Section, Other.Value, Other.Size,
                    Other.Type, Other.Binding, Other.Visibility);
  }
};

struct ELFRelocationSemantics {
  std::string Section;
  uint64_t Offset = 0;
  uint32_t Type = 0;
  std::string Target;
  int64_t Addend = 0;

  bool operator==(const ELFRelocationSemantics &Other) const {
    return std::tie(Section, Offset, Type, Target, Addend) ==
           std::tie(Other.Section, Other.Offset, Other.Type, Other.Target,
                    Other.Addend);
  }
};

struct ELFSemantics {
  uint16_t Machine = 0;
  uint32_t Flags = 0;
  uint8_t OSABI = 0;
  uint8_t ABIVersion = 0;
  std::set<std::string> SectionNames;
  std::vector<ELFSectionSemantics> OrdinarySections;
  std::vector<ELFSymbolSemantics> Symbols;
  std::vector<ELFRelocationSemantics> Relocations;
  unsigned StringTableCount = 0;
  bool HasSymbolStringTable = false;
  bool HasSectionStringTable = false;
  bool SymtabLinksSymbolStringTable = false;
};

bool isELFGeneratedMetadataSection(uint32_t Type) {
  return Type == ELF::SHT_SYMTAB || Type == ELF::SHT_STRTAB ||
         Type == ELF::SHT_RELA || Type == ELF::SHT_REL;
}

Expected<ELFSemantics> readELFSemantics(ArrayRef<uint8_t> Bytes) {
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();

  ELFSemantics Result;
  Result.Machine = Parsed->getHeader().e_machine;
  Result.Flags = Parsed->getHeader().e_flags;
  Result.OSABI = Parsed->getHeader().e_ident[ELF::EI_OSABI];
  Result.ABIVersion = Parsed->getHeader().e_ident[ELF::EI_ABIVERSION];

  const object::ELF64LE::Shdr *Symtab = nullptr;
  unsigned SymtabIndex = 0;
  for (unsigned I = 0; I < Sections->size(); ++I) {
    const object::ELF64LE::Shdr &Section = (*Sections)[I];
    auto Name = Parsed->getSectionName(Section);
    if (!Name)
      return Name.takeError();
    Result.SectionNames.insert(Name->str());
    if (Section.sh_type == ELF::SHT_STRTAB) {
      ++Result.StringTableCount;
      Result.HasSymbolStringTable |= *Name == ".strtab";
      Result.HasSectionStringTable |= *Name == ".shstrtab";
    }
    if (Section.sh_type == ELF::SHT_SYMTAB) {
      if (Symtab)
        return createStringError(inconvertibleErrorCode(),
                                 "test ELF has multiple symbol tables");
      Symtab = &Section;
      SymtabIndex = I;
    }
    if (I == 0 || isELFGeneratedMetadataSection(Section.sh_type))
      continue;
    ELFSectionSemantics Semantics;
    Semantics.Name = Name->str();
    Semantics.Type = Section.sh_type;
    Semantics.Flags = Section.sh_flags;
    Semantics.Alignment = Section.sh_addralign;
    Semantics.Size = Section.sh_size;
    if (Section.sh_type != ELF::SHT_NOBITS) {
      auto Data = Parsed->getSectionContents(Section);
      if (!Data)
        return Data.takeError();
      Semantics.Data.assign(Data->begin(), Data->end());
    }
    Result.OrdinarySections.push_back(std::move(Semantics));
  }
  if (!Symtab)
    return createStringError(inconvertibleErrorCode(),
                             "test ELF has no symbol table");
  if (Symtab->sh_link < Sections->size()) {
    auto LinkedName = Parsed->getSectionName((*Sections)[Symtab->sh_link]);
    if (!LinkedName)
      return LinkedName.takeError();
    Result.SymtabLinksSymbolStringTable = *LinkedName == ".strtab";
  }

  auto Symbols = Parsed->symbols(Symtab);
  if (!Symbols)
    return Symbols.takeError();
  auto StringTable = Parsed->getStringTableForSymtab(*Symtab);
  if (!StringTable)
    return StringTable.takeError();
  std::vector<std::string> Names(Symbols->size());
  for (unsigned I = 0; I < Symbols->size(); ++I) {
    const object::ELF64LE::Sym &Symbol = (*Symbols)[I];
    auto Name = Symbol.getName(*StringTable);
    if (!Name)
      return Name.takeError();
    Names[I] = Name->str();
    if (Name->empty())
      continue;
    ELFSymbolSemantics Semantics;
    Semantics.Name = Name->str();
    if (Symbol.st_shndx == ELF::SHN_UNDEF)
      Semantics.Section = "<undefined>";
    else if (Symbol.st_shndx == ELF::SHN_ABS)
      Semantics.Section = "<absolute>";
    else if (Symbol.st_shndx < Sections->size()) {
      auto SectionName = Parsed->getSectionName((*Sections)[Symbol.st_shndx]);
      if (!SectionName)
        return SectionName.takeError();
      Semantics.Section = SectionName->str();
    } else {
      Semantics.Section = ("<section-" + Twine(Symbol.st_shndx) + ">").str();
    }
    Semantics.Value = Symbol.st_value;
    Semantics.Size = Symbol.st_size;
    Semantics.Type = Symbol.getType();
    Semantics.Binding = Symbol.getBinding();
    Semantics.Visibility = Symbol.getVisibility();
    Result.Symbols.push_back(std::move(Semantics));
  }

  for (unsigned I = 0; I < Sections->size(); ++I) {
    const object::ELF64LE::Shdr &Section = (*Sections)[I];
    if (Section.sh_type != ELF::SHT_RELA)
      continue;
    if (Section.sh_link != SymtabIndex || Section.sh_info >= Sections->size())
      return createStringError(inconvertibleErrorCode(),
                               "test ELF has malformed relocation links");
    auto TargetSection = Parsed->getSectionName((*Sections)[Section.sh_info]);
    if (!TargetSection)
      return TargetSection.takeError();
    auto Relocations = Parsed->relas(Section);
    if (!Relocations)
      return Relocations.takeError();
    for (const object::ELF64LE::Rela &Relocation : *Relocations) {
      if (Relocation.getSymbol() >= Names.size())
        return createStringError(inconvertibleErrorCode(),
                                 "test ELF relocation symbol is out of range");
      Result.Relocations.push_back(
          {TargetSection->str(), Relocation.r_offset, Relocation.getType(),
           Names[Relocation.getSymbol()], Relocation.r_addend});
    }
  }

  const auto SectionOrder = [](const ELFSectionSemantics &Left,
                               const ELFSectionSemantics &Right) {
    return std::tie(Left.Name, Left.Type, Left.Flags, Left.Alignment, Left.Size,
                    Left.Data) < std::tie(Right.Name, Right.Type, Right.Flags,
                                          Right.Alignment, Right.Size,
                                          Right.Data);
  };
  const auto SymbolOrder = [](const ELFSymbolSemantics &Left,
                              const ELFSymbolSemantics &Right) {
    return std::tie(Left.Name, Left.Section, Left.Value, Left.Size, Left.Type,
                    Left.Binding, Left.Visibility) <
           std::tie(Right.Name, Right.Section, Right.Value, Right.Size,
                    Right.Type, Right.Binding, Right.Visibility);
  };
  const auto RelocationOrder = [](const ELFRelocationSemantics &Left,
                                  const ELFRelocationSemantics &Right) {
    return std::tie(Left.Section, Left.Offset, Left.Type, Left.Target,
                    Left.Addend) < std::tie(Right.Section, Right.Offset,
                                            Right.Type, Right.Target,
                                            Right.Addend);
  };
  llvm::sort(Result.OrdinarySections, SectionOrder);
  llvm::sort(Result.Symbols, SymbolOrder);
  llvm::sort(Result.Relocations, RelocationOrder);
  return Result;
}

bool isAArch64MappingSymbol(StringRef Name) {
  return Name.starts_with("$d.") || Name.starts_with("$x.");
}

bool containsBytes(ArrayRef<uint8_t> Bytes, StringRef Needle) {
  return std::search(Bytes.begin(), Bytes.end(), Needle.bytes_begin(),
                     Needle.bytes_end()) != Bytes.end();
}

NevercStatus NEVERC_CALL ignoreBinaryWrite(void *, NevercTaskHandle,
                                           NevercMutableBinaryBuilderHandle,
                                           NevercByteView) {
  return neverc_status_ok();
}

class LinkTaskScope {
public:
  LinkTaskScope()
      : Services("neverc-plugin-object-merge-tests", LLVM_VERSION_MAJOR) {}

  bool initialize(StringRef PluginPath = {},
                  StringRef AdditionalPluginPath = {}) {
    if (Error E = registerPluginIOInterface(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = registerPluginTargetInterfaces(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = registerPluginObjectPhaseInterface(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = registerPluginLinkInterfaces(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    std::vector<StringRef> Selected;
    for (StringRef Path : {PluginPath, AdditionalPluginPath}) {
      if (Path.empty())
        continue;
      auto Loaded = Services.registry().load(Path);
      if (!Loaded) {
        ADD_FAILURE() << errorText(Loaded.takeError());
        return false;
      }
      Plugins.push_back(*Loaded);
      Selected.push_back(Plugins.back()->descriptor().PluginID);
    }
    auto CreatedPlan = makePluginActivationPlan(Services.registry(), Selected);
    if (!CreatedPlan) {
      ADD_FAILURE() << errorText(CreatedPlan.takeError());
      return false;
    }
    Plan.emplace(std::move(*CreatedPlan));
    if (Error E = activatePluginPlan(Services, *Plan)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    auto CreatedSession = PluginSession::create(Services, *Plan);
    if (!CreatedSession) {
      ADD_FAILURE() << errorText(CreatedSession.takeError());
      return false;
    }
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_LINK);
    if (!CreatedTask) {
      ADD_FAILURE() << errorText(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    return true;
  }

  ~LinkTaskScope() {
    if (Task && !Task->isEnded())
      if (Error E = Task->end())
        ADD_FAILURE() << errorText(std::move(E));
    if (Session && !Session->isEnded())
      if (Error E = Session->end())
        ADD_FAILURE() << errorText(std::move(E));
    Plan.reset();
    if (Error E = Services.shutdown())
      ADD_FAILURE() << errorText(std::move(E));
  }

  PluginTaskContext &task() { return *Task; }
  PluginSession &session() { return *Session; }
  const std::shared_ptr<const PluginModule> &plugin(size_t Index) const {
    return Plugins.at(Index);
  }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  std::vector<std::shared_ptr<const PluginModule>> Plugins;
};

Expected<std::vector<uint8_t>>
mergeFinalAndroidReleaseImage(LinkTaskScope &Scope,
                              const BuiltinTargetRoute &Route,
                              ArrayRef<uint8_t> Input, StringRef LogicalURI) {
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  if (!Snapshot)
    return Snapshot.takeError();
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  if (!Reader)
    return Reader.takeError();
  auto ReadTarget = makeBuiltinTargetKey(Route);
  if (!ReadTarget)
    return ReadTarget.takeError();
  auto Graph = (*Reader)->read(Scope.task(), Input, LogicalURI, *ReadTarget,
                               Route.ObjectFormatID);
  if (!Graph)
    return Graph.takeError();

  std::array<PluginObjectGraph *, 1> Objects{Graph->get()};
  std::array<ArrayRef<uint8_t>, 1> Images{Input};
  auto MergeTarget = makeBuiltinTargetKey(Route);
  if (!MergeTarget)
    return MergeTarget.takeError();
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*MergeTarget), Objects, Images,
      NEVERC_LINK_OPTION_NONE, Config);
  if (!Merged)
    return Merged.takeError();
  return std::vector<uint8_t>(Merged->MergedImage.begin(),
                              Merged->MergedImage.end());
}

struct AndroidObjectLinkOutcome {
  bool Completed = false;
  bool Published = false;
  std::string Error;
  std::vector<uint8_t> Output;
};

AndroidObjectLinkOutcome runAndroidObjectLink(
    LinkTaskScope &Scope, const BuiltinTargetRoute &Route,
    std::vector<uint8_t> Input, StringRef OutputStem, bool FinalizeRelease,
    std::optional<NevercObjectFormatID> RequestedOutputFormat = std::nullopt,
    linker::LinkExecutionOutputKind RequestedOutputKind =
        linker::LinkExecutionOutputKind::Relocatable,
    std::optional<bool> ConfigRelocatable = std::nullopt) {
  AndroidObjectLinkOutcome Outcome;
  SmallString<128> Directory;
  if (std::error_code EC =
          sys::fs::createUniqueDirectory(OutputStem, Directory)) {
    Outcome.Error = EC.message();
    return Outcome;
  }
  auto RemoveDirectory =
      make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> OutputPath(Directory);
  sys::path::append(OutputPath, "android-object-output.ko");

  linker::LinkExecutionRequest Request;
  Request.TargetTriple = Route.CanonicalTriple.str();
  if (RequestedOutputFormat)
    Request.OutputFormat = {RequestedOutputFormat->High,
                            RequestedOutputFormat->Low};
  Request.OutputKind = RequestedOutputKind;
  Request.OutputURI = OutputPath.str().str();
  linker::LinkExecutionInput LinkInput;
  LinkInput.Kind = linker::LinkExecutionInputKind::Object;
  LinkInput.LogicalURI = "memory://android-object-link-input.o";
  LinkInput.AuthorizedBlob = std::move(Input);
  Request.Inputs.push_back(std::move(LinkInput));

  linker::LinkerDriverConfig Config;
  Config.pluginTask = &Scope.task();
  Config.relocatable = ConfigRelocatable.value_or(
      RequestedOutputKind == linker::LinkExecutionOutputKind::Relocatable);
  Config.androidKernelModule = true;
  Config.finalizeAndroidKernelModule = FinalizeRelease;
  Config.stripMode =
      FinalizeRelease ? linker::StripMode::All : linker::StripMode::None;

  neverc::OutputCoordinator Outputs;
  auto SessionAlias =
      std::shared_ptr<PluginSession>(&Scope.session(), [](PluginSession *) {});
  LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
  raw_null_ostream NullOutput;
  auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
  if (!Result) {
    Outcome.Error = errorText(Result.takeError());
    Outcome.Published = sys::fs::exists(OutputPath);
    return Outcome;
  }
  if (Result->Disposition != linker::LinkHookDisposition::Completed) {
    Outcome.Error = "Android object link did not complete";
    Bridge.complete(false);
    Outcome.Published = sys::fs::exists(OutputPath);
    return Outcome;
  }
  Bridge.complete(true);
  Outcome.Completed = true;
  Outcome.Published = sys::fs::exists(OutputPath);
  auto Output = MemoryBuffer::getFile(OutputPath);
  if (!Output) {
    Outcome.Error = Output.getError().message();
    return Outcome;
  }
  StringRef Bytes = (*Output)->getBuffer();
  Outcome.Output.assign(Bytes.bytes_begin(), Bytes.bytes_end());
  return Outcome;
}

struct MergeCallbackState {
  unsigned Calls = 0;
  bool ReturnForeignObject = false;
};

NevercStatus NEVERC_CALL mergeObjects(void *UserData, NevercTaskHandle Task,
                                      const NevercObjectMergeRequest *Request,
                                      NevercObjectMergeCandidate *Candidate) {
  auto *State = static_cast<MergeCallbackState *>(UserData);
  ++State->Calls;
  if (!Request || !Candidate ||
      Request->Objects.ElementStride != sizeof(NevercObjectMergeInput) ||
      !Request->OutputObject)
    return {NEVERC_STATUS_INVALID_ARGUMENT, NEVERC_STATUS_FLAG_NONE, 0};

  auto *Inputs =
      static_cast<const NevercObjectMergeInput *>(Request->Objects.Data);
  uint8_t SectionCount = 0;
  for (uint64_t I = 0; I != Request->Objects.Count; ++I) {
    NevercObjectGraphInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR,
                   NEVERC_OBJECT_API_MINOR, 0};
    NevercStatus Status = Inputs[I].Object->GetGraphInfo(
        Inputs[I].Object->Context, Task, Inputs[I].Graph, &Info);
    if (!neverc_status_is_ok(Status))
      return Status;
    SectionCount += static_cast<uint8_t>(Info.SectionCount);
  }

  const char Name[] = ".merged";
  NevercObjectSectionDescriptor Section{};
  Section.Header = {sizeof(Section), NEVERC_OBJECT_API_MAJOR,
                    NEVERC_OBJECT_API_MINOR, 0};
  Section.Name = {Name, sizeof(Name) - 1};
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  Section.Alignment = 1;
  Section.Data = {&SectionCount, 1};
  NevercObjectSectionHandle Created{};
  NevercStatus Status = Request->OutputObject->CreateSection(
      Request->OutputObject->Context, Task, Request->OutputMutation, &Section,
      &Created);
  if (!neverc_status_is_ok(Status))
    return Status;

  Candidate->Object = State->ReturnForeignObject
                          ? NevercObjectGraphHandle{UINT64_C(99), UINT64_C(101)}
                          : Request->OutputGraph;
  Candidate->ProductID = TestProductID;
  Candidate->ProducerRouteDigest[0] = 0x42;
  return neverc_status_ok();
}

struct NestedMergeMutationState {
  MergeCallbackState Merge;
  PluginTaskContext *Task = nullptr;
  std::string ObserverPluginID;
  const NevercObjectAPI *CachedOutputObject = nullptr;
  NevercTaskHandle CachedTask{};
  NevercObjectMutationHandle CachedOutputMutation{};
  NevercStatus ObserverDispatch{NEVERC_STATUS_INVALID_STATE, 0, 0};
  NevercStatus MutationAttempt{NEVERC_STATUS_INVALID_STATE, 0, 0};
};

NevercStatus NEVERC_CALL mergeAndAttemptMutationFromNestedObserver(
    void *UserData, NevercTaskHandle Task,
    const NevercObjectMergeRequest *Request,
    NevercObjectMergeCandidate *Candidate) {
  if (!UserData || !Request || !Candidate)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  auto &State = *static_cast<NestedMergeMutationState *>(UserData);
  if (!State.Task || State.ObserverPluginID.empty())
    return {NEVERC_STATUS_INVALID_STATE, 0, 0};

  NevercStatus Status = mergeObjects(&State.Merge, Task, Request, Candidate);
  if (!neverc_status_is_ok(Status))
    return Status;
  State.CachedOutputObject = Request->OutputObject;
  State.CachedTask = Task;
  State.CachedOutputMutation = Request->OutputMutation;

  auto Nested = State.Task->invokeCallback(
      State.ObserverPluginID, "object_merge_nested_read_only_observer",
      [&] {
        static const std::array<uint8_t, 1> Byte{{UINT8_C(0x5a)}};
        static const char Name[] = ".nested-observer-write";
        NevercObjectSectionDescriptor Section{};
        Section.Header = {sizeof(Section), NEVERC_OBJECT_API_MAJOR,
                          NEVERC_OBJECT_API_MINOR, 0};
        Section.Name = {Name, sizeof(Name) - 1};
        Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
        Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED;
        Section.Alignment = 1;
        Section.Data = {Byte.data(), Byte.size()};
        NevercObjectSectionHandle Created{};
        State.MutationAttempt = State.CachedOutputObject->CreateSection(
            State.CachedOutputObject->Context, State.CachedTask,
            State.CachedOutputMutation, &Section, &Created);
        return neverc_status_ok();
      },
      true, nullptr, false, nullptr);
  if (!Nested) {
    consumeError(Nested.takeError());
    return {NEVERC_STATUS_PLUGIN_FAILURE, 0, 801};
  }
  State.ObserverDispatch = *Nested;
  return *Nested;
}

} // namespace
