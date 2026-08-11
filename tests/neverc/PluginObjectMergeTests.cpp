#include "Link/AndroidKernelModuleFinalizer.h"
#include "Link/AndroidKernelProfileContractVerifier.h"
#include "Link/AndroidKernelReleaseIdentitySeal.h"
#include "Link/AndroidKernelReleaseInputVerifier.h"
#include "Link/BuiltinObjectMergeAdapter.h"
#include "Link/LinkGraph.h"
#include "Link/ObjectGraphImporter.h"
#include "Link/ObjectMergeProvider.h"
#include "Object/AndroidKernelReleaseWriterPreflight.h"
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
  // Native contract for profile 612 with normalized KCFI.
  Section.Data = {UINT8_C(0x02), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
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
    .byte 2, 0, 0, 0, 100, 2, 0, 0
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
    .byte 2, 0, 0, 0, 100, 2, 0, 0
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
    .byte 2, 0, 0, 0, 100, 2, 0, 0
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
    .byte 2, 0, 0, 0, 100, 2, 0, 0
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
    .byte 2, 0, 0, 0, 100, 2, 0, 0
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

TEST(BuiltinLLVMObjectWriterPolicyABITest,
     RejectsOldUnknownAndIllegalFlagsBeforeTargetDispatch) {
  NevercObjectAPI Object{};
  NevercMutableBinaryAPI Binary{};
  Binary.Write = ignoreBinaryWrite;
  NevercObjectWriteRequest Request{};
  Request.Object = &Object;
  Request.Binary = &Binary;

  const auto Invoke = [&](uint16_t Minor, uint64_t Flags) {
    Request.Header = {sizeof(Request), NEVERC_OBJECT_FORMAT_API_MAJOR, Minor,
                      Flags};
    return writeBuiltinLLVMObject(nullptr, &Request);
  };

  EXPECT_EQ(Invoke(UINT16_C(0), 0).Code, NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  EXPECT_EQ(Invoke(UINT16_C(0), NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES).Code,
            NEVERC_STATUS_ABI_MISMATCH);
  EXPECT_EQ(Invoke(NEVERC_OBJECT_FORMAT_API_MINOR, UINT64_C(1) << 63).Code,
            NEVERC_STATUS_ABI_MISMATCH);
  EXPECT_EQ(Invoke(NEVERC_OBJECT_FORMAT_API_MINOR,
                   NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE)
                .Code,
            NEVERC_STATUS_ABI_MISMATCH);
  EXPECT_EQ(Invoke(NEVERC_OBJECT_FORMAT_API_MINOR,
                   NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO)
                .Code,
            NEVERC_STATUS_ABI_MISMATCH);

  for (uint64_t LegalFlags : {NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES,
                              NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES |
                                  NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO,
                              NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES |
                                  NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE,
                              NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES |
                                  NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE |
                                  NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO})
    EXPECT_EQ(Invoke(NEVERC_OBJECT_FORMAT_API_MINOR, LegalFlags).Code,
              NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
}

TEST(AndroidKernelProfileContractVerifierTest,
     FinalizationStripsContractEntitiesFromObjectGraph) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  const uint64_t Generation = Graph->generation();
  const uint64_t RetainedSectionID = Graph->sections().front().ID;
  const AndroidKernelContractEntities Contract =
      addAndroidKernelProfileContract(*Graph);
  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Contract.SectionID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SECTION;
  Relocation.Width = 64;
  Relocation.TargetSectionID = RetainedSectionID;
  Graph->relocations().push_back(std::move(Relocation));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
  ASSERT_EQ(Graph->sectionCount(), 2u);
  ASSERT_EQ(Graph->symbolCount(), 1u);
  ASSERT_EQ(Graph->relocationCount(), 1u);

  Error StripError =
      stripAndroidKernelProfileContract(*Graph, "test final output");
  ASSERT_FALSE(StripError) << errorText(std::move(StripError));
  EXPECT_EQ(Graph->sectionCount(), 1u);
  EXPECT_EQ(Graph->symbolCount(), 0u);
  EXPECT_EQ(Graph->relocationCount(), 0u);
  EXPECT_EQ(Graph->generation(), Generation + 1);
  EXPECT_FALSE(forbidAndroidKernelProfileContract(*Graph, "test final output"));
  EXPECT_FALSE(verifyPluginObjectGraph(*Graph));
}

TEST(AndroidKernelProfileContractVerifierTest,
     FinalizationRejectsRetainedRelocationToContract) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  const AndroidKernelContractEntities Contract =
      addAndroidKernelProfileContract(*Graph);

  PluginObjectRelocation ContractRelocation;
  ContractRelocation.ID = Graph->allocateEntityID();
  ContractRelocation.SectionID = Contract.SectionID;
  ContractRelocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  ContractRelocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SECTION;
  ContractRelocation.Width = 64;
  ContractRelocation.TargetSectionID = Graph->sections().front().ID;
  Graph->relocations().push_back(std::move(ContractRelocation));

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Graph->sections().front().ID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 8;
  Relocation.TargetSymbolID = Contract.SymbolID;
  Graph->relocations().push_back(std::move(Relocation));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  Error StripError =
      stripAndroidKernelProfileContract(*Graph, "test final output");
  ASSERT_TRUE(static_cast<bool>(StripError));
  EXPECT_NE(errorText(std::move(StripError))
                .find("retained section references the native Android kernel "
                      "profile contract"),
            std::string::npos);
  EXPECT_EQ(Graph->sectionCount(), 2u);
  EXPECT_EQ(Graph->symbolCount(), 1u);
  EXPECT_EQ(Graph->relocationCount(), 2u);
  EXPECT_FALSE(verifyPluginObjectGraph(*Graph));
}

TEST(AndroidKernelModuleFinalizerTest,
     ReleaseStripKeepsOnlyRelocationRequiredPrivateSymbols) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  Graph->sections().front().Data = {0, 0, 0, 0};
  const uint64_t TextSectionID = Graph->sections().front().ID;
  addAndroidKernelProfileContract(*Graph);

  PluginObjectSection Debug;
  Debug.ID = Graph->allocateEntityID();
  Debug.Name = ".debug_info";
  Debug.Kind = NEVERC_OBJECT_SECTION_KIND_DEBUG;
  Debug.Flags = NEVERC_OBJECT_SECTION_DEBUG;
  Debug.Alignment = 1;
  Debug.Data = {0};
  const uint64_t DebugSectionID = Debug.ID;
  Graph->sections().push_back(std::move(Debug));

  PluginObjectSection Comment;
  Comment.ID = Graph->allocateEntityID();
  Comment.Name = ".comment";
  Comment.Alignment = 1;
  Comment.Data = {'N', 'e', 'v', 'e', 'r', 'C'};
  Graph->sections().push_back(std::move(Comment));

  auto AddSymbol = [&](StringRef Name, NevercObjectSymbolBinding Binding,
                       NevercObjectSymbolDefinition Definition,
                       uint64_t SectionID, uint64_t Value) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = Binding;
    Symbol.Type = Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED
                      ? NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE
                      : NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
    Symbol.Definition = Definition;
    Symbol.SectionID = SectionID;
    Symbol.Value = Value;
    Symbol.Size = Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED ? 1 : 0;
    const uint64_t ID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));
    return ID;
  };

  const uint64_t NeededLocal =
      AddSymbol("release_needed_local", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID, 0);
  AddSymbol("release_unneeded_local", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID, 1);
  const uint64_t NeededImport =
      AddSymbol("release_needed_import", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED, 0, 0);
  AddSymbol("release_unneeded_import", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
            NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED, 0, 0);
  const uint64_t PublicDefinition = AddSymbol(
      "release_public_definition", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
      NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID, 2);
  AddSymbol("release_debug_only", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, DebugSectionID, 0);

  auto AddRelocation = [&](uint64_t SectionID, uint64_t Offset,
                           uint64_t TargetSymbolID) {
    PluginObjectRelocation Relocation;
    Relocation.ID = Graph->allocateEntityID();
    Relocation.SectionID = SectionID;
    Relocation.Offset = Offset;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.Width = 8;
    Relocation.TargetSymbolID = TargetSymbolID;
    Graph->relocations().push_back(std::move(Relocation));
  };
  AddRelocation(TextSectionID, 0, NeededLocal);
  AddRelocation(TextSectionID, 1, NeededImport);
  AddRelocation(DebugSectionID, 0, PublicDefinition);

  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
  const uint64_t Generation = Graph->generation();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  EXPECT_EQ(Graph->generation(), Generation + 1);
  EXPECT_FALSE(verifyPluginObjectGraph(*Graph));

  const auto HasSection = [&](StringRef Name) {
    return std::any_of(
        Graph->sections().begin(), Graph->sections().end(),
        [&](const PluginObjectSection &S) { return S.Name == Name; });
  };
  const auto HasSymbol = [&](StringRef Name) {
    return std::any_of(
        Graph->symbols().begin(), Graph->symbols().end(),
        [&](const PluginObjectSymbol &S) { return S.Name == Name; });
  };
  EXPECT_FALSE(HasSection(".neverc.android.kernel.profile"));
  EXPECT_FALSE(HasSection(".debug_info"));
  EXPECT_FALSE(HasSection(".comment"));
  EXPECT_TRUE(HasSymbol("obj_0"));
  EXPECT_FALSE(HasSymbol("release_unneeded_local"));
  EXPECT_TRUE(HasSymbol("release_needed_import"));
  EXPECT_FALSE(HasSymbol("release_unneeded_import"));
  EXPECT_TRUE(HasSymbol("obj_2"));
  EXPECT_FALSE(HasSymbol("release_debug_only"));
  EXPECT_EQ(Graph->relocationCount(), 2u);
  EXPECT_FALSE(verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module"));
}

TEST(AndroidKernelModuleFinalizerTest,
     DropDebugRemovesReclassifiedGDBIndexByName) {
  auto Graph = makeObject(0);
  ASSERT_NE(Graph, nullptr);

  PluginObjectSection DebugIndex;
  DebugIndex.ID = Graph->allocateEntityID();
  DebugIndex.Name = ".gdb_index";
  DebugIndex.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  DebugIndex.Alignment = 4;
  DebugIndex.Data = {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)};
  Graph->sections().push_back(std::move(DebugIndex));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  EXPECT_EQ(Graph->generation(), Generation + 1);
  EXPECT_TRUE(Graph->sections().empty());
  EXPECT_FALSE(verifyPluginObjectGraph(*Graph));
}

TEST(AndroidKernelModuleFinalizerTest,
     DropDebugRejectsAllocatedGDBIndexAtomically) {
  auto Graph = makeObject(0);
  ASSERT_NE(Graph, nullptr);

  PluginObjectSection DebugIndex;
  DebugIndex.ID = Graph->allocateEntityID();
  DebugIndex.Name = ".gdb_index";
  DebugIndex.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  DebugIndex.Flags = NEVERC_OBJECT_SECTION_ALLOCATED;
  DebugIndex.Alignment = 4;
  DebugIndex.Data = {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)};
  const uint64_t DebugIndexID = DebugIndex.ID;
  Graph->sections().push_back(std::move(DebugIndex));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Error Verify = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module");
  ASSERT_TRUE(static_cast<bool>(Verify));
  const std::string VerifyMessage = errorText(std::move(Verify));
  EXPECT_NE(VerifyMessage.find("allocated"), std::string::npos)
      << VerifyMessage;
  EXPECT_NE(VerifyMessage.find(".gdb_index"), std::string::npos)
      << VerifyMessage;

  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  const std::string Message = errorText(std::move(Finalize));
  EXPECT_NE(Message.find("allocated"), std::string::npos) << Message;
  EXPECT_NE(Message.find(".gdb_index"), std::string::npos) << Message;
  EXPECT_EQ(Graph->generation(), Generation);
  ASSERT_EQ(Graph->sectionCount(), 1U);
  EXPECT_EQ(Graph->sections().front().ID, DebugIndexID);
}

TEST(AndroidKernelModuleFinalizerTest,
     PlansIDAStyleNamesFromFinalRetainedSectionOrder) {
  auto Graph = makeObject(0);
  ASSERT_NE(Graph, nullptr);

  auto AddSection = [&](StringRef Name, NevercObjectSectionKind Kind,
                        NevercObjectSectionFlags Flags, uint64_t Alignment,
                        size_t DataSize, uint64_t ZeroFillSize = 0) {
    PluginObjectSection Section;
    Section.ID = Graph->allocateEntityID();
    Section.Name = Name.str();
    Section.Kind = Kind;
    Section.Flags = Flags;
    Section.Alignment = Alignment;
    Section.Data.resize(DataSize);
    Section.ZeroFillSize = ZeroFillSize;
    const uint64_t ID = Section.ID;
    Graph->sections().push_back(std::move(Section));
    return ID;
  };
  const uint64_t TextID = AddSection(".text", NEVERC_OBJECT_SECTION_KIND_TEXT,
                                     NEVERC_OBJECT_SECTION_ALLOCATED |
                                         NEVERC_OBJECT_SECTION_EXECUTABLE,
                                     16, 0x11);
  AddSection(".comment", NEVERC_OBJECT_SECTION_KIND_DATA, 0, 1, 3);
  const uint64_t DataID = AddSection(".data", NEVERC_OBJECT_SECTION_KIND_DATA,
                                     NEVERC_OBJECT_SECTION_ALLOCATED |
                                         NEVERC_OBJECT_SECTION_WRITABLE,
                                     0x20, 3);
  const uint64_t BssID = AddSection(
      ".bss", NEVERC_OBJECT_SECTION_KIND_ZERO_FILL,
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE, 0x40, 0,
      0x10);
  const uint64_t MetadataID =
      AddSection(".metadata", NEVERC_OBJECT_SECTION_KIND_DATA, 0, 4, 8);
  const uint64_t ModInfoID =
      AddSection(".modinfo", NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA,
                 NEVERC_OBJECT_SECTION_ALLOCATED, 1, 8);

  auto AddSymbol = [&](StringRef Name, NevercObjectSymbolBinding Binding,
                       NevercObjectSymbolType Type,
                       NevercObjectSymbolDefinition Definition,
                       uint64_t SectionID, uint64_t Value, uint64_t Size) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = Binding;
    Symbol.Type = Type;
    Symbol.Definition = Definition;
    Symbol.SectionID = SectionID;
    Symbol.Value = Value;
    Symbol.Size = Size;
    const uint64_t ID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));
    return ID;
  };

  const uint64_t FunctionA =
      AddSymbol("function_a", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextID, 0, 1);
  const uint64_t FunctionB =
      AddSymbol("function_b", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextID, 0, 1);
  const uint64_t ExecutableLabel =
      AddSymbol("executable_label", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextID, 8, 0);
  const uint64_t CanonicalLookingOriginal =
      AddSymbol("fn_0", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextID, 4, 1);
  const uint64_t DataObject =
      AddSymbol("data_object", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_OBJECT,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, DataID, 1, 1);
  const uint64_t DataLabel =
      AddSymbol("data_label", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, DataID, 2, 0);
  const uint64_t BssObject =
      AddSymbol("bss_object", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_OBJECT,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, BssID, 8, 1);
  const uint64_t Metadata =
      AddSymbol("metadata_label", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, MetadataID, 3, 0);
  const uint64_t Absolute =
      AddSymbol("absolute_value", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE, 0, 0x2a, 0);
  const uint64_t Import =
      AddSymbol("kernel_import", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED, 0, 0, 0);
  const uint64_t Loader =
      AddSymbol("init_module", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextID, 0xc, 1);
  const uint64_t ProtectedSection =
      AddSymbol("module_metadata", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_OBJECT,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, ModInfoID, 0, 1);

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = TextID;
  Relocation.Offset = 0;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 64;
  Relocation.TargetSymbolID = Import;
  Graph->relocations().push_back(std::move(Relocation));

  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test structural release names");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));

  std::vector<std::string> FunctionAliasNames{
      Graph->findSymbol(FunctionA)->Name, Graph->findSymbol(FunctionB)->Name};
  llvm::sort(FunctionAliasNames);
  EXPECT_EQ(FunctionAliasNames, (std::vector<std::string>{"fn_0", "fn_0_1"}));
  EXPECT_EQ(Graph->findSymbol(ExecutableLabel)->Name, "code_8");
  EXPECT_EQ(Graph->findSymbol(CanonicalLookingOriginal)->Name, "fn_4");
  EXPECT_EQ(Graph->findSymbol(DataObject)->Name, "obj_21");
  EXPECT_EQ(Graph->findSymbol(DataLabel)->Name, "sym_22");
  EXPECT_EQ(Graph->findSymbol(BssObject)->Name, "obj_48");
  EXPECT_EQ(Graph->findSymbol(Metadata)->Name, "sym_S4_3");
  EXPECT_EQ(Graph->findSymbol(Absolute)->Name, "abs_2A");
  EXPECT_EQ(Graph->findSymbol(Import)->Name, "kernel_import");
  EXPECT_EQ(Graph->findSymbol(Loader)->Name, "init_module");
  EXPECT_EQ(Graph->findSymbol(ProtectedSection)->Name, "module_metadata");

  std::swap(Graph->findSymbol(FunctionA)->Name,
            Graph->findSymbol(FunctionB)->Name);
  Graph->advanceGeneration();
  EXPECT_FALSE(verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test exact-tie release name exchange"));
  Graph->findSymbol(FunctionA)->Name = "fn_0_2";
  Graph->advanceGeneration();
  Error WrongAliasMultiset = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test wrong exact-tie release name multiset");
  ASSERT_TRUE(static_cast<bool>(WrongAliasMultiset));
  EXPECT_NE(
      errorText(std::move(WrongAliasMultiset)).find("release symbol plan"),
      std::string::npos);
}

TEST(AndroidKernelReleaseIdentitySealTest,
     GraphRejectsOtherwiseValidExactABINameReplacement) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data = {UINT8_C(0)};

  PluginObjectSymbol Entry;
  Entry.ID = Graph->allocateEntityID();
  Entry.Name = "init_module";
  Entry.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Entry.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Entry.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Entry.SectionID = Text.ID;
  Entry.Size = 1;
  const uint64_t EntryID = Entry.ID;
  Graph->symbols().push_back(std::move(Entry));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test graph exact-name manifest");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));

  auto Seal = captureAndroidKernelReleaseGraphIdentitySeal(
      *Graph, Policy.SymbolNameState, "test finalized authoritative graph");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  PluginObjectGraph Mutated(*Graph);
  PluginObjectSymbol *MutatedEntry = Mutated.findSymbol(EntryID);
  ASSERT_NE(MutatedEntry, nullptr);
  MutatedEntry->Name = "__cfi_check";
  Mutated.advanceGeneration();

  Error Standalone = verifyFinalAndroidKernelModuleObjectGraph(
      Mutated, Policy, "test structurally valid exact-name replacement");
  EXPECT_FALSE(Standalone) << errorText(std::move(Standalone));

  Error ExactName = verifyAndroidKernelReleaseGraphIdentitySeal(
      Mutated, Policy.SymbolNameState, *Seal,
      "test immutable graph exact-name contract");
  ASSERT_TRUE(static_cast<bool>(ExactName));
  const std::string Message = errorText(std::move(ExactName));
  EXPECT_NE(Message.find("immutable release identity seal"), std::string::npos)
      << Message;
  EXPECT_NE(Message.find("init_module"), std::string::npos) << Message;
  EXPECT_NE(Message.find("__cfi_check"), std::string::npos) << Message;
}

TEST(AndroidKernelReleaseIdentitySealTest,
     GraphRejectsOtherwiseValidMappedSymbolRelayout) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data.resize(32);

  PluginObjectSymbol Function;
  Function.ID = Graph->allocateEntityID();
  Function.Name = "ordinary_worker";
  Function.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Function.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Function.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Function.SectionID = Text.ID;
  Function.Size = 4;
  const uint64_t FunctionID = Function.ID;
  Graph->symbols().push_back(std::move(Function));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  neverc::AndroidKernelReleaseSymbolMap SymbolMap;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test mapped graph identity baseline", &SymbolMap);
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  ASSERT_EQ(SymbolMap.Symbols.size(), 1u);
  EXPECT_EQ(SymbolMap.Symbols.front().OriginalName, "ordinary_worker");
  EXPECT_EQ(SymbolMap.Symbols.front().ReleaseName, "fn_0");

  auto Seal = captureAndroidKernelReleaseGraphIdentitySeal(
      *Graph, Policy.SymbolNameState, "test mapped graph identity baseline");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  PluginObjectGraph Mutated(*Graph);
  PluginObjectSymbol *MutatedFunction = Mutated.findSymbol(FunctionID);
  ASSERT_NE(MutatedFunction, nullptr);
  MutatedFunction->Value = 8;
  MutatedFunction->Name = "fn_8";
  Mutated.advanceGeneration();

  Error Standalone = verifyFinalAndroidKernelModuleObjectGraph(
      Mutated, Policy, "test structurally valid mapped symbol relayout");
  ASSERT_FALSE(Standalone) << errorText(std::move(Standalone));

  Error ImmutableIdentity = verifyAndroidKernelReleaseGraphIdentitySeal(
      Mutated, Policy.SymbolNameState, *Seal,
      "test immutable mapped graph identity contract");
  ASSERT_TRUE(static_cast<bool>(ImmutableIdentity));
  const std::string Message = errorText(std::move(ImmutableIdentity));
  EXPECT_NE(Message.find("immutable release identity seal"), std::string::npos)
      << Message;
  EXPECT_NE(Message.find("fn_0"), std::string::npos) << Message;
  EXPECT_NE(Message.find("fn_8"), std::string::npos) << Message;
}

TEST(AndroidKernelReleaseIdentitySealTest,
     GraphBindsExactUndefinedNamesToTheirRelocationOwners) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data.resize(16);

  const auto AddImport = [&](StringRef Name) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
    const uint64_t ID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));
    return ID;
  };
  const uint64_t FirstImport = AddImport("kernel_one");
  const uint64_t SecondImport = AddImport("kernel_two");

  const auto AddRelocation = [&](uint64_t Offset, uint64_t TargetSymbolID) {
    PluginObjectRelocation Relocation;
    Relocation.ID = Graph->allocateEntityID();
    Relocation.SectionID = Text.ID;
    Relocation.Offset = Offset;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.TargetSymbolID = TargetSymbolID;
    Relocation.Width = 64;
    Relocation.Extension.Owner = TestFormatID;
    Relocation.Extension.Version =
        neverc::plugin::builtinext::RelocationVersion;
    const SmallVector<uint8_t, 80> NativeFacts = makeELFRelocationExtension(
        neverc::plugin::builtinext::RelocationVersion, ELF::R_AARCH64_ABS64,
        "R_AARCH64_ABS64");
    Relocation.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Graph->relocations().push_back(std::move(Relocation));
  };
  AddRelocation(0, FirstImport);
  AddRelocation(8, SecondImport);
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test exact undefined owner identities");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));

  auto Seal = captureAndroidKernelReleaseGraphIdentitySeal(
      *Graph, Policy.SymbolNameState,
      "test exact undefined owner identity baseline");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  PluginObjectGraph Mutated(*Graph);
  std::swap(Mutated.findSymbol(FirstImport)->Name,
            Mutated.findSymbol(SecondImport)->Name);
  Mutated.advanceGeneration();

  Error Standalone = verifyFinalAndroidKernelModuleObjectGraph(
      Mutated, Policy, "test structurally valid undefined-name exchange");
  ASSERT_FALSE(Standalone) << errorText(std::move(Standalone));

  Error ImmutableIdentity = verifyAndroidKernelReleaseGraphIdentitySeal(
      Mutated, Policy.SymbolNameState, *Seal,
      "test immutable graph owner identity contract");
  ASSERT_TRUE(static_cast<bool>(ImmutableIdentity));
  const std::string Message = errorText(std::move(ImmutableIdentity));
  EXPECT_NE(Message.find("immutable release identity seal"), std::string::npos)
      << Message;
  EXPECT_NE(Message.find("kernel_one"), std::string::npos) << Message;
  EXPECT_NE(Message.find("kernel_two"), std::string::npos) << Message;
}

TEST(AndroidKernelReleaseIdentitySealTest,
     GraphBindsEveryRetainedSectionOwnerNameAndFinalOrdinal) {
  auto Graph = makeAndroidObject(2);
  ASSERT_NE(Graph, nullptr);
  auto Section = Graph->sections().begin();
  Section->Name = ".text";
  Section->Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section->Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  ++Section;
  Section->Name = ".rodata";
  Section->Kind = NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA;
  Section->Flags = NEVERC_OBJECT_SECTION_ALLOCATED;

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test retained section identity baseline");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  auto Seal = captureAndroidKernelReleaseGraphIdentitySeal(
      *Graph, Policy.SymbolNameState,
      "test retained section identity baseline");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  PluginObjectGraph Renamed(*Graph);
  Renamed.sections().front().Name = ".code";
  Renamed.advanceGeneration();
  Error StandaloneRename = verifyFinalAndroidKernelModuleObjectGraph(
      Renamed, Policy, "test structurally valid section rename");
  ASSERT_FALSE(StandaloneRename) << errorText(std::move(StandaloneRename));
  Error Rename = verifyAndroidKernelReleaseGraphIdentitySeal(
      Renamed, Policy.SymbolNameState, *Seal,
      "test immutable graph section-name contract");
  ASSERT_TRUE(static_cast<bool>(Rename));
  const std::string RenameMessage = errorText(std::move(Rename));
  EXPECT_NE(RenameMessage.find("release layout identity seal"),
            std::string::npos)
      << RenameMessage;
  EXPECT_NE(RenameMessage.find(".text"), std::string::npos) << RenameMessage;
  EXPECT_NE(RenameMessage.find(".code"), std::string::npos) << RenameMessage;

  PluginObjectGraph Reordered(*Graph);
  auto Second = std::next(Reordered.sections().begin());
  Reordered.sections().splice(Reordered.sections().begin(),
                              Reordered.sections(), Second);
  Reordered.advanceGeneration();
  Error StandaloneOrder = verifyFinalAndroidKernelModuleObjectGraph(
      Reordered, Policy, "test structurally valid section reorder");
  ASSERT_FALSE(StandaloneOrder) << errorText(std::move(StandaloneOrder));
  Error Order = verifyAndroidKernelReleaseGraphIdentitySeal(
      Reordered, Policy.SymbolNameState, *Seal,
      "test immutable graph section-order contract");
  ASSERT_TRUE(static_cast<bool>(Order));
  EXPECT_NE(errorText(std::move(Order)).find("final ordinal"),
            std::string::npos);
}

TEST(AndroidKernelReleaseIdentitySealTest,
     GraphAuthorityRejectsNamedSectionSymbolItCannotSerialize) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data.resize(8);

  PluginObjectSymbol SectionSymbol;
  SectionSymbol.ID = Graph->allocateEntityID();
  SectionSymbol.Name = "section_key";
  SectionSymbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
  SectionSymbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_SECTION;
  SectionSymbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  SectionSymbol.SectionID = Text.ID;
  const uint64_t SectionSymbolID = SectionSymbol.ID;
  Graph->symbols().push_back(std::move(SectionSymbol));

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Text.ID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.TargetSymbolID = SectionSymbolID;
  Relocation.Width = 64;
  Graph->relocations().push_back(std::move(Relocation));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Standalone = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test named SECTION structural graph");
  ASSERT_FALSE(Standalone) << errorText(std::move(Standalone));

  auto Seal = captureAndroidKernelReleaseGraphIdentitySeal(
      *Graph, Policy.SymbolNameState,
      "test portable named SECTION authority boundary");
  ASSERT_FALSE(static_cast<bool>(Seal));
  const std::string Message = errorText(Seal.takeError());
  EXPECT_NE(Message.find("section_key"), std::string::npos) << Message;
  EXPECT_NE(Message.find("SECTION type"), std::string::npos) << Message;
  EXPECT_NE(Message.find("cannot round-trip"), std::string::npos) << Message;
}

TEST(AndroidKernelReleaseIdentitySealTest,
     ImageBindsExactUndefinedNamesToRawSymbolTableSlots) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  auto Input = assembleAndroidReleaseInputWithTwoImports(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Image = mergeFinalAndroidReleaseImage(
      Scope, *AndroidRoute, *Input, "memory://identity-slot-imports.o");
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  ASSERT_FALSE(verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test exact import-slot baseline"));
  auto Seal = captureAndroidKernelReleaseImageIdentitySeal(
      *Image, "test exact import-slot baseline");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  auto Before = readELFSemantics(*Image);
  ASSERT_TRUE(static_cast<bool>(Before)) << errorText(Before.takeError());
  Error Swap = swapELF64SymbolNameBytes(*Image, "kernel_one", "kernel_two");
  ASSERT_FALSE(Swap) << errorText(std::move(Swap));
  auto After = readELFSemantics(*Image);
  ASSERT_TRUE(static_cast<bool>(After)) << errorText(After.takeError());
  const auto RelocationTarget = [](const ELFSemantics &Semantics,
                                   uint64_t Offset) -> StringRef {
    const auto Found = llvm::find_if(
        Semantics.Relocations,
        [Offset](const ELFRelocationSemantics &Relocation) {
          return Relocation.Section == ".text" && Relocation.Offset == Offset;
        });
    return Found == Semantics.Relocations.end() ? StringRef() : Found->Target;
  };
  EXPECT_EQ(RelocationTarget(*Before, 0), "kernel_one");
  EXPECT_EQ(RelocationTarget(*Before, 8), "kernel_two");
  EXPECT_EQ(RelocationTarget(*After, 0), "kernel_two");
  EXPECT_EQ(RelocationTarget(*After, 8), "kernel_one");

  Error Standalone = verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test structurally valid exact import-slot exchange");
  ASSERT_FALSE(Standalone) << errorText(std::move(Standalone));
  Error ImmutableIdentity = verifyAndroidKernelReleaseImageIdentitySeal(
      *Image, *Seal, "test immutable image symbol-slot contract");
  ASSERT_TRUE(static_cast<bool>(ImmutableIdentity));
  const std::string Message = errorText(std::move(ImmutableIdentity));
  EXPECT_NE(Message.find("symbol-table slot"), std::string::npos) << Message;
  EXPECT_NE(Message.find("kernel_one"), std::string::npos) << Message;
  EXPECT_NE(Message.find("kernel_two"), std::string::npos) << Message;
}

TEST(AndroidKernelReleaseIdentitySealTest,
     ImageBindsNamedSectionSymbolsToRawSymbolTableSlots) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  auto Input = assembleAndroidReleaseInputWithNamedSection(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Image = mergeFinalAndroidReleaseImage(
      Scope, *AndroidRoute, *Input, "memory://identity-named-section.o");
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto ReadOnlySection = findELF64SectionIndex(*Image, ".rodata");
  ASSERT_TRUE(static_cast<bool>(ReadOnlySection))
      << errorText(ReadOnlySection.takeError());
  Error MakeNamedSection = patchELF64Symbol(
      *Image, "section_key",
      [SectionIndex = *ReadOnlySection](object::ELF64LE::Sym &Symbol) {
        Symbol.setBindingAndType(Symbol.getBinding(), ELF::STT_SECTION);
        Symbol.st_shndx = SectionIndex;
        Symbol.st_value = 0;
        Symbol.st_size = 0;
      });
  ASSERT_FALSE(MakeNamedSection) << errorText(std::move(MakeNamedSection));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  ASSERT_FALSE(verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test named SECTION baseline"));
  auto Before = readELFSemantics(*Image);
  ASSERT_TRUE(static_cast<bool>(Before)) << errorText(Before.takeError());
  const auto NamedSection =
      llvm::find_if(Before->Symbols, [](const ELFSymbolSemantics &Symbol) {
        return Symbol.Name == "section_key";
      });
  ASSERT_NE(NamedSection, Before->Symbols.end());
  EXPECT_EQ(NamedSection->Type, ELF::STT_SECTION);
  auto Seal = captureAndroidKernelReleaseImageIdentitySeal(
      *Image, "test named SECTION baseline");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  Error Rename =
      replaceELF64SymbolNameBytes(*Image, "section_key", "segment_key");
  ASSERT_FALSE(Rename) << errorText(std::move(Rename));
  Error Standalone = verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test structurally valid named SECTION replacement");
  ASSERT_FALSE(Standalone) << errorText(std::move(Standalone));
  Error ImmutableIdentity = verifyAndroidKernelReleaseImageIdentitySeal(
      *Image, *Seal, "test immutable named SECTION slot contract");
  ASSERT_TRUE(static_cast<bool>(ImmutableIdentity));
  const std::string Message = errorText(std::move(ImmutableIdentity));
  EXPECT_NE(Message.find("symbol-table slot"), std::string::npos) << Message;
  EXPECT_NE(Message.find("section_key"), std::string::npos) << Message;
  EXPECT_NE(Message.find("segment_key"), std::string::npos) << Message;
}

TEST(AndroidKernelReleaseIdentitySealTest,
     PrePostWritePipelineAcceptsStableNamedSectionAndRejectsItsRename) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope BuildScope;
  ASSERT_TRUE(BuildScope.initialize());
  auto Input = assembleAndroidReleaseInputWithNamedSection(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Image = mergeFinalAndroidReleaseImage(
      BuildScope, *AndroidRoute, *Input,
      "memory://pipeline-named-section-baseline.o");
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto ReadOnlySection = findELF64SectionIndex(*Image, ".rodata");
  ASSERT_TRUE(static_cast<bool>(ReadOnlySection))
      << errorText(ReadOnlySection.takeError());
  Error MakeNamedSection = patchELF64Symbol(
      *Image, "section_key",
      [SectionIndex = *ReadOnlySection](object::ELF64LE::Sym &Symbol) {
        Symbol.setBindingAndType(Symbol.getBinding(), ELF::STT_SECTION);
        Symbol.st_shndx = SectionIndex;
        Symbol.st_value = 0;
        Symbol.st_size = 0;
      });
  ASSERT_FALSE(MakeNamedSection) << errorText(std::move(MakeNamedSection));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  ASSERT_FALSE(verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test stable named SECTION pipeline baseline"));

  const auto Run = [&](LinkTaskScope &Scope, StringRef OutputName)
      -> Expected<std::shared_ptr<PluginObjectImage>> {
    auto Snapshot = PluginTargetRegistry::freeze(
        ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
    if (!Snapshot)
      return Snapshot.takeError();
    auto Reader = ObjectReaderProvider::create(*Snapshot);
    if (!Reader)
      return Reader.takeError();
    auto Target = makeBuiltinTargetKey(*AndroidRoute);
    if (!Target)
      return Target.takeError();
    auto Graph = (*Reader)->read(Scope.task(), *Image,
                                 "memory://named-section-pipeline-input.ko",
                                 *Target, AndroidRoute->ObjectFormatID);
    if (!Graph)
      return Graph.takeError();
    auto Pipeline = ObjectPhasePipeline::create(Scope.task(), *Snapshot);
    if (!Pipeline)
      return Pipeline.takeError();
    ObjectPhaseSemanticValidators Validators;
    Validators.BindPrePostWriteImage = [Policy](ArrayRef<uint8_t> Baseline)
        -> Expected<ObjectImageSemanticValidator> {
      if (Error E = verifyFinalAndroidKernelModuleImage(
              Baseline, Policy, "test trusted named SECTION baseline"))
        return std::move(E);
      auto Seal = captureAndroidKernelReleaseImageIdentitySeal(
          Baseline, "test trusted named SECTION baseline");
      if (!Seal)
        return Seal.takeError();
      return ObjectImageSemanticValidator([Policy, Seal = std::move(*Seal)](
                                              ArrayRef<uint8_t> Candidate) {
        if (Error E = verifyFinalAndroidKernelModuleImage(
                Candidate, Policy, "test named SECTION post-write candidate"))
          return E;
        return verifyAndroidKernelReleaseImageIdentitySeal(
            Candidate, Seal, "test immutable named SECTION pipeline contract");
      });
    };
    return (*Pipeline)->executeNative(
        **Graph, *Image,
        ObjectOutputDestination::memory(OutputName, UINT64_C(1) << 20),
        std::move(Validators));
  };

  LinkTaskScope StableScope;
  ASSERT_TRUE(StableScope.initialize());
  auto Stable = Run(StableScope, "stable-named-section.ko");
  ASSERT_TRUE(static_cast<bool>(Stable)) << errorText(Stable.takeError());
  EXPECT_TRUE(
      findPluginMemoryOutput(StableScope.task(), "stable-named-section.ko")
          .has_value());

  LinkTaskScope MutatingScope;
  ASSERT_TRUE(MutatingScope.initialize(
      NEVERC_TEST_OBJECT_SECTION_SYMBOL_CORRUPT_PLUGIN));
  auto Mutated = Run(MutatingScope, "mutated-named-section.ko");
  ASSERT_FALSE(static_cast<bool>(Mutated));
  const std::string Message = errorText(Mutated.takeError());
  EXPECT_NE(Message.find("immutable release identity seal"), std::string::npos)
      << Message;
  EXPECT_FALSE(
      findPluginMemoryOutput(MutatingScope.task(), "mutated-named-section.ko")
          .has_value());
}

TEST(AndroidKernelReleaseIdentitySealTest,
     ImageBindsEveryRetainedLogicalSectionNameToItsOrdinal) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  auto Input = assembleAndroidReleaseInputWithInitPLT(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Image = mergeFinalAndroidReleaseImage(Scope, *AndroidRoute, *Input,
                                             "memory://identity-init-plt.o");
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  ASSERT_FALSE(verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test retained image section baseline"));
  auto Seal = captureAndroidKernelReleaseImageIdentitySeal(
      *Image, "test retained image section baseline");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  Error Rename = replaceELF64SectionNameBytes(*Image, ".init.plt", ".hide.plt");
  ASSERT_FALSE(Rename) << errorText(std::move(Rename));
  Error Standalone = verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test structurally valid image section rename");
  ASSERT_FALSE(Standalone) << errorText(std::move(Standalone));
  Error ImmutableIdentity = verifyAndroidKernelReleaseImageIdentitySeal(
      *Image, *Seal, "test immutable image section-name contract");
  ASSERT_TRUE(static_cast<bool>(ImmutableIdentity));
  const std::string Message = errorText(std::move(ImmutableIdentity));
  EXPECT_NE(Message.find("release layout identity seal"), std::string::npos)
      << Message;
  EXPECT_NE(Message.find(".init.plt"), std::string::npos) << Message;
  EXPECT_NE(Message.find(".hide.plt"), std::string::npos) << Message;
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalAuditKeepsFullNativeOtherInExchangeClasses) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data = {UINT8_C(0)};
  attachCanonicalELFSectionFacts(*Graph);

  const auto AddFunction = [&](StringRef Name, uint64_t Other) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = Text.ID;
    Symbol.Size = 1;
    Symbol.Extension.Owner = TestFormatID;
    Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
    const SmallVector<uint8_t, 48> NativeFacts = makeELFSymbolExtension(
        neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC,
        ELF::STB_GLOBAL, Other, Symbol.Size);
    Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    const uint64_t ID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));
    return ID;
  };

  // Full st_other orders these as fn_0 then fn_0_1. Swapping the names is not
  // an exchange within one exact observable tie, even though both expose the
  // same stable DEFAULT visibility.
  const uint64_t Plain = AddFunction("fn_0_1", ELF::STV_DEFAULT);
  const uint64_t VariantPCS =
      AddFunction("fn_0", UINT64_C(0x80) | ELF::STV_DEFAULT);
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  Error Swapped = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test full native st_other exchange class");
  ASSERT_TRUE(static_cast<bool>(Swapped));
  EXPECT_NE(errorText(std::move(Swapped)).find("release symbol plan"),
            std::string::npos);

  Graph->findSymbol(Plain)->Name = "fn_0";
  Graph->findSymbol(VariantPCS)->Name = "fn_0_1";
  Graph->advanceGeneration();
  Error Canonical = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test canonical full native st_other plan");
  ASSERT_FALSE(Canonical) << errorText(std::move(Canonical));
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalAuditUsesNativeBindingAndAbsoluteSizeExchangeClasses) {
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;

  {
    auto Graph = makeAndroidObject(1);
    ASSERT_NE(Graph, nullptr);
    PluginObjectSection &Text = Graph->sections().front();
    Text.Name = ".text";
    Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Text.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    Text.Data = {UINT8_C(0)};
    attachCanonicalELFSectionFacts(*Graph);

    const auto AddFunction = [&](StringRef Name, uint64_t NativeBinding) {
      PluginObjectSymbol Symbol;
      Symbol.ID = Graph->allocateEntityID();
      Symbol.Name = Name.str();
      // llvm::object deliberately projects every non-local/non-weak ELF
      // binding, including STB_GNU_UNIQUE, to stable GLOBAL.
      Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
      Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
      Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
      Symbol.SectionID = Text.ID;
      Symbol.Size = 1;
      Symbol.Extension.Owner = TestFormatID;
      Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
      const SmallVector<uint8_t, 48> NativeFacts = makeELFSymbolExtension(
          neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC,
          NativeBinding, ELF::STV_DEFAULT, Symbol.Size);
      Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
      const uint64_t ID = Symbol.ID;
      Graph->symbols().push_back(std::move(Symbol));
      return ID;
    };

    const uint64_t Global = AddFunction("fn_0_1", ELF::STB_GLOBAL);
    const uint64_t Unique = AddFunction("fn_0", ELF::STB_GNU_UNIQUE);
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
    Error Swapped = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test native binding exchange class");
    ASSERT_TRUE(static_cast<bool>(Swapped));
    EXPECT_NE(errorText(std::move(Swapped)).find("release symbol plan"),
              std::string::npos);

    Graph->findSymbol(Global)->Name = "fn_0";
    Graph->findSymbol(Unique)->Name = "fn_0_1";
    Graph->advanceGeneration();
    Error Canonical = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test canonical native binding plan");
    ASSERT_FALSE(Canonical) << errorText(std::move(Canonical));
  }

  {
    auto Graph = makeAndroidObject(1);
    ASSERT_NE(Graph, nullptr);
    attachCanonicalELFSectionFacts(*Graph);
    const auto AddAbsolute = [&](StringRef Name, uint64_t NativeSize) {
      PluginObjectSymbol Symbol;
      Symbol.ID = Graph->allocateEntityID();
      Symbol.Name = Name.str();
      Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
      Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
      Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE;
      Symbol.Value = UINT64_C(0x2a);
      // The current built-in reader preserves ABS st_size in NCSY but projects
      // the stable size to zero because no section extent owns the symbol.
      Symbol.Size = 0;
      Symbol.Extension.Owner = TestFormatID;
      Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
      const SmallVector<uint8_t, 48> NativeFacts = makeELFSymbolExtension(
          neverc::plugin::builtinext::SymbolVersion, ELF::STT_NOTYPE,
          ELF::STB_GLOBAL, ELF::STV_DEFAULT, NativeSize);
      Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
      const uint64_t ID = Symbol.ID;
      Graph->symbols().push_back(std::move(Symbol));
      return ID;
    };

    const uint64_t Empty = AddAbsolute("abs_2A_1", 0);
    const uint64_t Sized = AddAbsolute("abs_2A", 7);
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
    Error Swapped = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test native absolute-size exchange class");
    ASSERT_TRUE(static_cast<bool>(Swapped));
    EXPECT_NE(errorText(std::move(Swapped)).find("release symbol plan"),
              std::string::npos);

    Graph->findSymbol(Empty)->Name = "abs_2A";
    Graph->findSymbol(Sized)->Name = "abs_2A_1";
    Graph->advanceGeneration();
    Error Canonical = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test canonical native absolute-size plan");
    ASSERT_FALSE(Canonical) << errorText(std::move(Canonical));
  }
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalAuditAcceptsReaderProjectedProtectedVisibility) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data = {UINT8_C(0)};
  attachCanonicalELFSectionFacts(*Graph);

  PluginObjectSymbol Symbol;
  Symbol.ID = Graph->allocateEntityID();
  Symbol.Name = "fn_0";
  Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  // llvm::object only exposes SF_Hidden; INTERNAL and PROTECTED both project
  // to the stable DEFAULT value while NCSY retains the exact st_other byte.
  Symbol.Visibility = NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT;
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Symbol.SectionID = Text.ID;
  Symbol.Size = 1;
  Symbol.Extension.Owner = TestFormatID;
  Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
  const SmallVector<uint8_t, 48> NativeFacts = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_PROTECTED, Symbol.Size);
  Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
  Graph->symbols().push_back(std::move(Symbol));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  Error Audit = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test reader-projected protected visibility");
  ASSERT_FALSE(Audit) << errorText(std::move(Audit));
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalAuditRejectsMalformedNativeSymbolFacts) {
  const auto ExpectRejected = [&](ArrayRef<uint8_t> NativeFacts,
                                  uint32_t OuterVersion,
                                  StringRef ExpectedReason) {
    auto Graph = makeAndroidObject(1);
    ASSERT_NE(Graph, nullptr);
    PluginObjectSection &Text = Graph->sections().front();
    Text.Name = ".text";
    Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Text.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    Text.Data = {UINT8_C(0)};
    attachCanonicalELFSectionFacts(*Graph);

    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = "fn_0";
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = Text.ID;
    Symbol.Size = 1;
    Symbol.Extension.Owner = TestFormatID;
    Symbol.Extension.Version = OuterVersion;
    Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Graph->symbols().push_back(std::move(Symbol));
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
    Error Audit = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test malformed canonical native st_other");
    ASSERT_TRUE(static_cast<bool>(Audit));
    EXPECT_NE(errorText(std::move(Audit)).find(ExpectedReason.str()),
              std::string::npos);
  };

  SmallVector<uint8_t, 48> Truncated = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_DEFAULT, 1);
  Truncated.pop_back();
  ExpectRejected(Truncated, neverc::plugin::builtinext::SymbolVersion,
                 "exact version-2 payload");

  const SmallVector<uint8_t, 48> TooWide = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      UINT64_C(0x100), 1);
  ExpectRejected(TooWide, neverc::plugin::builtinext::SymbolVersion,
                 "does not fit ELF st_other");

  const SmallVector<uint8_t, 48> VisibilityMismatch = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_HIDDEN, 1);
  ExpectRejected(VisibilityMismatch, neverc::plugin::builtinext::SymbolVersion,
                 "disagrees with stable visibility");

  const SmallVector<uint8_t, 48> TypeMismatch = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_OBJECT,
      ELF::STB_GLOBAL, ELF::STV_DEFAULT, 1);
  ExpectRejected(TypeMismatch, neverc::plugin::builtinext::SymbolVersion,
                 "SymbolType disagrees with stable type");

  const SmallVector<uint8_t, 48> BindingMismatch =
      makeELFSymbolExtension(neverc::plugin::builtinext::SymbolVersion,
                             ELF::STT_FUNC, ELF::STB_WEAK, ELF::STV_DEFAULT, 1);
  ExpectRejected(BindingMismatch, neverc::plugin::builtinext::SymbolVersion,
                 "SymbolBinding disagrees with stable binding");

  const SmallVector<uint8_t, 48> SizeMismatch = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_DEFAULT, 2);
  ExpectRejected(SizeMismatch, neverc::plugin::builtinext::SymbolVersion,
                 "SymbolAuxiliary disagrees with stable size projection");

  const SmallVector<uint8_t, 48> VersionOne = makeELFSymbolExtension(
      1, ELF::STT_FUNC, ELF::STB_GLOBAL, ELF::STV_DEFAULT, 1);
  ExpectRejected(VersionOne, 1, "exact version-2 payload");

  const SmallVector<uint8_t, 48> NameStateMismatch = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_DEFAULT, 1, neverc::plugin::builtinext::SymbolNameEmpty);
  ExpectRejected(NameStateMismatch, neverc::plugin::builtinext::SymbolVersion,
                 "SymbolNameState disagrees with the stable name");

  const SmallVector<uint8_t, 48> InvalidNameState = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_DEFAULT, 1, 2);
  ExpectRejected(InvalidNameState, neverc::plugin::builtinext::SymbolVersion,
                 "invalid native SymbolNameState");
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalSectionFactsReplayReaderProjectionAndRejectTampering) {
  struct ProjectionCase {
    StringLiteral Name;
    uint64_t Type;
    uint64_t NativeFlags;
    NevercObjectSectionKind ExpectedKind;
    NevercObjectSectionFlags ExpectedFlags;
  };
  constexpr ProjectionCase Cases[] = {
      {".note.android.ident", ELF::SHT_NOTE, ELF::SHF_ALLOC,
       NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA,
       NEVERC_OBJECT_SECTION_ALLOCATED},
      {".init_array", ELF::SHT_INIT_ARRAY, ELF::SHF_ALLOC | ELF::SHF_WRITE,
       NEVERC_OBJECT_SECTION_KIND_DATA,
       NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE},
      {".tdata.exec", ELF::SHT_PROGBITS,
       ELF::SHF_ALLOC | ELF::SHF_WRITE | ELF::SHF_TLS | ELF::SHF_EXECINSTR,
       NEVERC_OBJECT_SECTION_KIND_TLS_DATA,
       NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE |
           NEVERC_OBJECT_SECTION_EXECUTABLE | NEVERC_OBJECT_SECTION_TLS},
      {".opaque", ELF::SHT_PROGBITS, 0,
       NEVERC_OBJECT_SECTION_KIND_FORMAT_EXTENSION, 0},
      {".gdb_index", ELF::SHT_PROGBITS, 0, NEVERC_OBJECT_SECTION_KIND_DEBUG,
       NEVERC_OBJECT_SECTION_DEBUG},
      {".text.unusual", ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
       NEVERC_OBJECT_SECTION_KIND_TEXT,
       NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE},
  };

  for (const ProjectionCase &TestCase : Cases) {
    const NativeELFSectionProjection Projection = projectNativeELFSection(
        TestCase.Name, TestCase.Type, TestCase.NativeFlags);
    EXPECT_EQ(Projection.Kind, TestCase.ExpectedKind) << TestCase.Name.str();
    EXPECT_EQ(Projection.Flags, TestCase.ExpectedFlags) << TestCase.Name.str();

    PluginObjectSection Section;
    Section.ID = 1;
    Section.Name = TestCase.Name.str();
    Section.Kind = Projection.Kind;
    Section.Flags = Projection.Flags;
    Section.Alignment = 1;
    Section.Data = {0};
    Section.Extension.Owner = TestFormatID;
    Section.Extension.Version = neverc::plugin::builtinext::SectionVersion;
    const SmallVector<uint8_t, 64> NativeFacts =
        makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                                TestCase.Type, TestCase.NativeFlags, 0);
    Section.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Error Verify = verifyCanonicalAndroidKernelReleaseReaderSection(
        Section, 1, "test canonical reader section projection");
    EXPECT_FALSE(Verify) << TestCase.Name.str() << ": "
                         << errorText(std::move(Verify));
  }

  const auto ExpectTamperRejected = [&](size_t Field, uint64_t Value,
                                        StringRef ExpectedReason) {
    PluginObjectSection Section;
    Section.ID = 1;
    Section.Name = ".text";
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Section.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    Section.Alignment = 1;
    Section.Data = {0};
    Section.Extension.Owner = TestFormatID;
    Section.Extension.Version = neverc::plugin::builtinext::SectionVersion;
    const SmallVector<uint8_t, 64> NativeFacts = makeELFSectionExtension(
        neverc::plugin::builtinext::SectionVersion, ELF::SHT_PROGBITS,
        ELF::SHF_ALLOC | ELF::SHF_EXECINSTR, 0);
    Section.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    overwriteExtensionU64(Section.Extension.Bytes, Field, Value);
    Error Verify = verifyCanonicalAndroidKernelReleaseReaderSection(
        Section, 1, "test tampered canonical reader section facts");
    ASSERT_TRUE(static_cast<bool>(Verify));
    const std::string Message = errorText(std::move(Verify));
    EXPECT_NE(Message.find(ExpectedReason.str()), std::string::npos) << Message;
  };
  ExpectTamperRejected(neverc::plugin::builtinext::SectionIndex, 2,
                       "index disagrees");
  ExpectTamperRejected(neverc::plugin::builtinext::SectionAddress, 1,
                       "nonzero ET_REL sh_addr");
  ExpectTamperRejected(neverc::plugin::builtinext::SectionType,
                       UINT64_C(1) << 32, "invalid native section type");
  ExpectTamperRejected(neverc::plugin::builtinext::SectionFlags, ELF::SHF_ALLOC,
                       "stable kind");

  PluginObjectSection Truncated;
  Truncated.ID = 1;
  Truncated.Name = ".text";
  Truncated.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Truncated.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Truncated.Alignment = 1;
  Truncated.Data = {0};
  Truncated.Extension.Owner = TestFormatID;
  Truncated.Extension.Version = neverc::plugin::builtinext::SectionVersion;
  const SmallVector<uint8_t, 64> Complete = makeELFSectionExtension(
      neverc::plugin::builtinext::SectionVersion, ELF::SHT_PROGBITS,
      ELF::SHF_ALLOC | ELF::SHF_EXECINSTR, 0);
  Truncated.Extension.Bytes.assign(Complete.begin(), Complete.end() - 1);
  Error TruncatedError = verifyCanonicalAndroidKernelReleaseReaderSection(
      Truncated, 1, "test truncated canonical reader section facts");
  ASSERT_TRUE(static_cast<bool>(TruncatedError));
  EXPECT_NE(errorText(std::move(TruncatedError)).find("exact NCSE v2"),
            std::string::npos);
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalRelocationFactsPreserveNullTargetsAndRejectTampering) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  Graph->sections().front().Data.resize(16);

  const auto MakeRelocation = [&]() {
    PluginObjectRelocation Relocation;
    Relocation.ID = 2;
    Relocation.SectionID = Graph->sections().front().ID;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.Width = 64;
    Relocation.TargetSymbolID = 3;
    Relocation.Extension.Owner = TestFormatID;
    Relocation.Extension.Version =
        neverc::plugin::builtinext::RelocationVersion;
    const SmallVector<uint8_t, 80> NativeFacts = makeELFRelocationExtension(
        neverc::plugin::builtinext::RelocationVersion, ELF::R_AARCH64_ABS64,
        "R_AARCH64_ABS64");
    Relocation.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    return Relocation;
  };

  PluginObjectRelocation SymbolTarget = MakeRelocation();
  EXPECT_FALSE(verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, SymbolTarget, "test canonical symbol relocation"));

  PluginObjectRelocation SectionTarget = MakeRelocation();
  SectionTarget.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SECTION;
  SectionTarget.TargetSymbolID = 0;
  SectionTarget.TargetSectionID = Graph->sections().front().ID;
  SectionTarget.TargetValue = 16;
  EXPECT_FALSE(verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, SectionTarget, "test canonical section relocation"));

  PluginObjectRelocation NullTarget = MakeRelocation();
  NullTarget.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_FORMAT_EXTENSION;
  NullTarget.TargetSymbolID = 0;
  NullTarget.TargetExtensionKind = ELF::R_AARCH64_ABS64 + 1;
  EXPECT_FALSE(verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, NullTarget, "test canonical null-symbol relocation"));
  Error PortableNull = verifyPortableAndroidKernelReleaseWriterRelocation(
      *Graph, NullTarget, "test portable null-symbol relocation");
  ASSERT_TRUE(static_cast<bool>(PortableNull));
  EXPECT_NE(errorText(std::move(PortableNull)).find("target kind"),
            std::string::npos);

  NullTarget.TargetExtensionKind = ELF::R_AARCH64_ABS64 + 2;
  Error BadToken = verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, NullTarget, "test bad null-symbol token");
  ASSERT_TRUE(static_cast<bool>(BadToken));
  EXPECT_NE(errorText(std::move(BadToken)).find("null-symbol target"),
            std::string::npos);

  SymbolTarget.TargetValue = 1;
  Error BadSymbolValue = verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, SymbolTarget, "test bad symbol target value");
  ASSERT_TRUE(static_cast<bool>(BadSymbolValue));
  EXPECT_NE(errorText(std::move(BadSymbolValue)).find("nonzero target value"),
            std::string::npos);

  SectionTarget.TargetValue = 17;
  Error BadSectionValue = verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, SectionTarget, "test bad section target value");
  ASSERT_TRUE(static_cast<bool>(BadSectionValue));
  EXPECT_NE(errorText(std::move(BadSectionValue)).find("outside"),
            std::string::npos);

  PluginObjectRelocation BadName = MakeRelocation();
  const SmallVector<uint8_t, 80> BadNameFacts =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_CALL26");
  BadName.Extension.Bytes.assign(BadNameFacts.begin(), BadNameFacts.end());
  Error BadNameError = verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, BadName, "test bad relocation name");
  ASSERT_TRUE(static_cast<bool>(BadNameError));
  EXPECT_NE(errorText(std::move(BadNameError)).find("official relocation name"),
            std::string::npos);

  PluginObjectRelocation BadWidth = MakeRelocation();
  BadWidth.Width = 32;
  Error BadWidthError = verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, BadWidth, "test bad relocation stable facts");
  ASSERT_TRUE(static_cast<bool>(BadWidthError));
  EXPECT_NE(errorText(std::move(BadWidthError)).find("stable relocation facts"),
            std::string::npos);
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalNativeSymbolFactsApplyTLSSectionTypeOverride) {
  struct SymbolCase {
    uint64_t NativeType;
    StringLiteral CanonicalName;
  };
  constexpr SymbolCase Cases[] = {
      {ELF::STT_OBJECT, "obj_0"},
      {ELF::STT_NOTYPE, "sym_0"},
  };
  for (const SymbolCase &TestCase : Cases) {
    auto Graph = makeAndroidObject(1);
    ASSERT_NE(Graph, nullptr);
    PluginObjectSection &TLS = Graph->sections().front();
    TLS.Name = ".tdata";
    TLS.Kind = NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
    TLS.Flags = NEVERC_OBJECT_SECTION_ALLOCATED |
                NEVERC_OBJECT_SECTION_WRITABLE | NEVERC_OBJECT_SECTION_TLS;
    TLS.Data = {0};
    attachCanonicalELFSectionFacts(*Graph);

    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = TestCase.CanonicalName.str();
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_TLS;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = TLS.ID;
    Symbol.Size = 1;
    Symbol.Extension.Owner = TestFormatID;
    Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
    const SmallVector<uint8_t, 48> NativeFacts = makeELFSymbolExtension(
        neverc::plugin::builtinext::SymbolVersion, TestCase.NativeType,
        ELF::STB_GLOBAL, ELF::STV_DEFAULT, 1);
    Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Graph->symbols().push_back(std::move(Symbol));

    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
    Error Verify = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test canonical TLS reader projection");
    EXPECT_FALSE(Verify) << TestCase.CanonicalName.str() << ": "
                         << errorText(std::move(Verify));
  }
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalProvenanceRejectsWrongTargetWithoutRelocations) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  attachCanonicalELFSectionFacts(*Graph);
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  Error Verify = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test zero-relocation wrong canonical target");
  ASSERT_TRUE(static_cast<bool>(Verify));
  EXPECT_NE(errorText(std::move(Verify)).find("AArch64 ELF64 little-endian"),
            std::string::npos);
}

TEST(AndroidKernelModuleFinalizerTest,
     PreservesOnlyExactLoaderImportAndProtectedSectionNames) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data.resize(64);
  const uint64_t TextSectionID = Text.ID;

  auto AddSection = [&](StringRef Name) {
    PluginObjectSection Section;
    Section.ID = Graph->allocateEntityID();
    Section.Name = Name.str();
    Section.Kind = Name == ".text.ftrace_trampoline"
                       ? NEVERC_OBJECT_SECTION_KIND_TEXT
                       : NEVERC_OBJECT_SECTION_KIND_DATA;
    Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED;
    if (Name == ".text.ftrace_trampoline")
      Section.Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
    Section.Alignment = 1;
    Section.Data = {0};
    const uint64_t ID = Section.ID;
    Graph->sections().push_back(std::move(Section));
    return ID;
  };
  auto AddSymbol = [&](StringRef Name, NevercObjectSymbolBinding Binding,
                       NevercObjectSymbolType Type,
                       NevercObjectSymbolDefinition Definition,
                       uint64_t SectionID, uint64_t Value = 0) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = Binding;
    Symbol.Type = Type;
    Symbol.Definition = Definition;
    Symbol.SectionID = SectionID;
    Symbol.Value = Value;
    Symbol.Size = Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED &&
                          Type != NEVERC_OBJECT_SYMBOL_TYPE_SECTION
                      ? 1
                      : 0;
    const uint64_t ID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));
    return ID;
  };
  auto AddRelocation = [&](uint64_t Offset, uint64_t TargetSymbolID) {
    PluginObjectRelocation Relocation;
    Relocation.ID = Graph->allocateEntityID();
    Relocation.SectionID = TextSectionID;
    Relocation.Offset = Offset;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.Width = 64;
    Relocation.TargetSymbolID = TargetSymbolID;
    Graph->relocations().push_back(std::move(Relocation));
  };

  const uint64_t Ordinary =
      AddSymbol("ordinary_defined", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID);
  const uint64_t Absolute =
      AddSymbol("ordinary_absolute", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE, 0, 42);
  const uint64_t HexSpelledOriginal =
      AddSymbol("0123456789abcdef", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID);
  const uint64_t NeededLocalLabel =
      AddSymbol("needed_local_label", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID);
  const uint64_t Import =
      AddSymbol("external_import", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED, 0);
  AddRelocation(0, NeededLocalLabel);
  AddRelocation(8, Import);

  std::vector<std::pair<uint64_t, std::string>> PreservedNames;
  for (StringRef Name :
       neverc::AndroidKernelModuleSymbolPolicy::PreservedSymbolNames) {
    const uint64_t ID =
        AddSymbol(Name, NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                  NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                  NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID);
    PreservedNames.emplace_back(ID, Name.str());
  }

  std::vector<std::pair<uint64_t, std::string>> PreservedSectionSymbols;
  unsigned MetadataIndex = 0;
  for (StringRef SectionName :
       neverc::AndroidKernelModuleSymbolPolicy::SymbolNamePreservedSections) {
    const uint64_t SectionID = AddSection(SectionName);
    std::string Name = "metadata_symbol_" + std::to_string(MetadataIndex++);
    const uint64_t ID =
        AddSymbol(Name, NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                  NEVERC_OBJECT_SYMBOL_TYPE_OBJECT,
                  NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, SectionID);
    PreservedSectionSymbols.emplace_back(ID, std::move(Name));
  }

  // .plt is structurally preserved by the merger, but it is deliberately not
  // one of the five sections whose symbol spellings are loader ABI.
  const uint64_t PLTSectionID = AddSection(".plt");
  const uint64_t PLTSymbol =
      AddSymbol("ordinary_plt_symbol", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, PLTSectionID);

  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  neverc::AndroidKernelReleaseSymbolMap SymbolMap;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module structural names",
      &SymbolMap);
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));

  ASSERT_NE(Graph->findSymbol(Ordinary), nullptr);
  ASSERT_NE(Graph->findSymbol(HexSpelledOriginal), nullptr);
  const auto MappedName = [&](StringRef Original) -> StringRef {
    auto It = llvm::find_if(
        SymbolMap.Symbols,
        [&](const neverc::AndroidKernelReleaseSymbolMapEntry &Entry) {
          return Entry.OriginalName == Original;
        });
    return It == SymbolMap.Symbols.end() ? StringRef() : It->ReleaseName;
  };
  EXPECT_EQ(SymbolMap.Symbols.size(), 5u);
  EXPECT_EQ(MappedName("ordinary_defined"),
            Graph->findSymbol(Ordinary)->Name);
  EXPECT_EQ(MappedName("0123456789abcdef"),
            Graph->findSymbol(HexSpelledOriginal)->Name);
  EXPECT_EQ(MappedName("ordinary_absolute"),
            Graph->findSymbol(Absolute)->Name);
  EXPECT_EQ(MappedName("needed_local_label"),
            Graph->findSymbol(NeededLocalLabel)->Name);
  EXPECT_EQ(MappedName("ordinary_plt_symbol"),
            Graph->findSymbol(PLTSymbol)->Name);
  EXPECT_TRUE(MappedName("external_import").empty());
  std::vector<std::string> OrdinaryAliasNames{
      Graph->findSymbol(Ordinary)->Name,
      Graph->findSymbol(HexSpelledOriginal)->Name};
  llvm::sort(OrdinaryAliasNames);
  EXPECT_EQ(OrdinaryAliasNames, (std::vector<std::string>{"fn_0", "fn_0_1"}));
  ASSERT_NE(Graph->findSymbol(Absolute), nullptr);
  EXPECT_EQ(Graph->findSymbol(Absolute)->Name, "abs_2A");
  EXPECT_NE(Graph->findSymbol(HexSpelledOriginal)->Name, "0123456789abcdef");
  ASSERT_NE(Graph->findSymbol(PLTSymbol), nullptr);
  EXPECT_EQ(Graph->findSymbol(PLTSymbol)->Name, "fn_45");
  ASSERT_NE(Graph->findSymbol(NeededLocalLabel), nullptr);
  EXPECT_EQ(Graph->findSymbol(NeededLocalLabel)->Name, "code_0");
  ASSERT_NE(Graph->findSymbol(Import), nullptr);
  EXPECT_EQ(Graph->findSymbol(Import)->Name, "external_import");
  for (const auto &[ID, Name] : PreservedNames) {
    ASSERT_NE(Graph->findSymbol(ID), nullptr);
    EXPECT_EQ(Graph->findSymbol(ID)->Name, Name);
  }
  for (const auto &[ID, Name] : PreservedSectionSymbols) {
    ASSERT_NE(Graph->findSymbol(ID), nullptr);
    EXPECT_EQ(Graph->findSymbol(ID)->Name, Name);
  }
  EXPECT_FALSE(verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module structural names"));

  // A graph plugin cannot restore a readable ordinary definition after the
  // host finalizer and still pass the host-owned pre-write validator.
  Graph->findSymbol(Ordinary)->Name = "bypassed_readable_name";
  Graph->advanceGeneration();
  Error VerifyBypass = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module structural names");
  ASSERT_TRUE(static_cast<bool>(VerifyBypass));
  EXPECT_NE(errorText(std::move(VerifyBypass)).find("release symbol plan"),
            std::string::npos);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsUnsupportedAndInvalidSymbolTypesWithoutMutation) {
  auto ExpectRejected =
      [&](NevercObjectSymbolType Type, NevercObjectSymbolDefinition Definition,
          StringRef Name, bool TLSSection, bool FormatExtension) {
        auto Graph = makeObject(1);
        ASSERT_NE(Graph, nullptr);
        PluginObjectSection &Section = Graph->sections().front();
        Section.Data.resize(8);
        if (TLSSection) {
          Section.Kind = NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
          Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED |
                          NEVERC_OBJECT_SECTION_WRITABLE |
                          NEVERC_OBJECT_SECTION_TLS;
        }

        PluginObjectSymbol Symbol;
        Symbol.ID = Graph->allocateEntityID();
        Symbol.Name = Name.str();
        Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
        Symbol.Type = Type;
        Symbol.Definition = Definition;
        if (Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED) {
          Symbol.SectionID = Section.ID;
          Symbol.Size = 1;
        }
        if (FormatExtension) {
          Symbol.Extension.Owner = TestFormatID;
          Symbol.Extension.Version = 1;
          Symbol.Extension.Bytes = {1};
        }
        Graph->symbols().push_back(std::move(Symbol));

        const uint64_t Generation = Graph->generation();
        const std::string Snapshot = dumpPluginObjectGraph(*Graph);
        AndroidKernelModuleFinalizationPolicy Policy;
        Policy.StripUnneededSymbols = true;
        Error Finalize = finalizeAndroidKernelModuleObjectGraph(
            *Graph, Policy, "test unsupported release symbol type");
        EXPECT_TRUE(static_cast<bool>(Finalize));
        if (Finalize)
          consumeError(std::move(Finalize));
        EXPECT_EQ(Graph->generation(), Generation);
        EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
      };

  ExpectRejected(NEVERC_OBJECT_SYMBOL_TYPE_TLS,
                 NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED,
                 "__kcfi_typeid_bad_tls", true, false);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_TYPE_INDIRECT_FUNCTION,
                 NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, "init_module", false,
                 false);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_TYPE_FILE,
                 NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE,
                 "__typeid__source_global_addr", false, false);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_TYPE_FORMAT_EXTENSION,
                 NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, "init_module", false,
                 true);
  ExpectRejected(static_cast<NevercObjectSymbolType>(UINT32_C(0xffffffff)),
                 NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, "init_module", false,
                 false);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsWriterLossyBindingAndVisibilityWithoutMutation) {
  auto ExpectRejected = [&](NevercObjectSymbolBinding Binding,
                            NevercObjectSymbolVisibility Visibility) {
    auto Graph = makeObject(1);
    ASSERT_NE(Graph, nullptr);
    Graph->sections().front().Data.resize(8);

    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = "writer_round_trip_required";
    Symbol.Binding = Binding;
    Symbol.Visibility = Visibility;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = Graph->sections().front().ID;
    Symbol.Size = 1;
    if (Binding == NEVERC_OBJECT_SYMBOL_BINDING_FORMAT_EXTENSION ||
        Visibility == NEVERC_OBJECT_SYMBOL_VISIBILITY_FORMAT_EXTENSION) {
      Symbol.Extension.Owner = TestFormatID;
      Symbol.Extension.Version = 1;
      Symbol.Extension.Bytes = {1};
    }
    Graph->symbols().push_back(std::move(Symbol));
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

    const uint64_t Generation = Graph->generation();
    const std::string Snapshot = dumpPluginObjectGraph(*Graph);
    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Error Finalize = finalizeAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test writer-lossy release symbol");
    EXPECT_TRUE(static_cast<bool>(Finalize));
    if (Finalize)
      consumeError(std::move(Finalize));
    EXPECT_EQ(Graph->generation(), Generation);
    EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
  };

  ExpectRejected(NEVERC_OBJECT_SYMBOL_BINDING_UNIQUE,
                 NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_BINDING_FORMAT_EXTENSION,
                 NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                 NEVERC_OBJECT_SYMBOL_VISIBILITY_PROTECTED);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                 NEVERC_OBJECT_SYMBOL_VISIBILITY_INTERNAL);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                 NEVERC_OBJECT_SYMBOL_VISIBILITY_FORMAT_EXTENSION);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsLossyNativeSymbolExtensionBeforeMutation) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Graph = makeBuiltinObject(
      *ELFRoute, "native_extension_requires_unique_and_protected.__pcg1234");
  ASSERT_NE(Graph, nullptr);
  Graph->sections().front().Data.resize(8);

  PluginObjectSymbol &Symbol = Graph->symbols().front();
  Symbol.Extension.Owner = ELFRoute->ObjectFormatID;
  Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
  SmallVector<uint8_t, 48> NativeFacts;
  neverc::plugin::builtinext::appendHeader(
      NativeFacts, neverc::plugin::builtinext::SymbolTag,
      neverc::plugin::builtinext::SymbolVersion);
  neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STT_FUNC);
  neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STB_GNU_UNIQUE);
  neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STV_PROTECTED);
  neverc::plugin::builtinext::appendU64(NativeFacts, 1);
  neverc::plugin::builtinext::appendU64(
      NativeFacts, neverc::plugin::builtinext::SymbolNameNonEmpty);
  Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Graph->sections().front().ID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 64;
  Relocation.TargetSymbolID = Symbol.ID;
  Relocation.Extension.Owner = ELFRoute->ObjectFormatID;
  Relocation.Extension.Version = neverc::plugin::builtinext::RelocationVersion;
  const SmallVector<uint8_t, 80> RelocationFacts =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  Relocation.Extension.Bytes.assign(RelocationFacts.begin(),
                                    RelocationFacts.end());
  Graph->relocations().push_back(std::move(Relocation));
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  const std::string Snapshot = dumpPluginObjectGraph(*Graph);
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test lossy native symbol extension");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  const std::string FinalizeMessage = errorText(std::move(Finalize));
  EXPECT_NE(FinalizeMessage.find("cannot round-trip"), std::string::npos)
      << FinalizeMessage;
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsMalformedNativeSymbolExtensionBeforeMutation) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  const auto MakeNativeFacts = [](std::optional<uint64_t> Auxiliary) {
    SmallVector<uint8_t, 48> NativeFacts;
    neverc::plugin::builtinext::appendHeader(
        NativeFacts, neverc::plugin::builtinext::SymbolTag,
        neverc::plugin::builtinext::SymbolVersion);
    neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STT_FUNC);
    neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STB_GLOBAL);
    neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STV_DEFAULT);
    if (Auxiliary) {
      neverc::plugin::builtinext::appendU64(NativeFacts, *Auxiliary);
      neverc::plugin::builtinext::appendU64(
          NativeFacts, neverc::plugin::builtinext::SymbolNameNonEmpty);
    }
    return NativeFacts;
  };
  const auto ExpectAtomicRejection = [&](ArrayRef<uint8_t> NativeFacts,
                                         StringRef ExpectedReason) {
    auto Graph = makeBuiltinObject(*ELFRoute, "native_extension_symbol");
    ASSERT_NE(Graph, nullptr);
    addAndroidKernelProfileContract(*Graph);
    PluginObjectSymbol &Symbol = Graph->symbols().front();
    Symbol.Extension.Owner = ELFRoute->ObjectFormatID;
    Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
    Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

    const uint64_t Generation = Graph->generation();
    const size_t SectionCount = Graph->sectionCount();
    const size_t SymbolCount = Graph->symbolCount();
    const std::string Snapshot = dumpPluginObjectGraph(*Graph);
    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Error Finalize = finalizeAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test malformed native symbol extension");
    ASSERT_TRUE(static_cast<bool>(Finalize));
    EXPECT_NE(errorText(std::move(Finalize)).find(ExpectedReason.str()),
              std::string::npos);
    EXPECT_EQ(Graph->generation(), Generation);
    EXPECT_EQ(Graph->sectionCount(), SectionCount);
    EXPECT_EQ(Graph->symbolCount(), SymbolCount);
    EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
  };

  SmallVector<uint8_t, 48> MissingAuxiliary = MakeNativeFacts(std::nullopt);
  ExpectAtomicRejection(MissingAuxiliary, "exact version-2 payload");

  SmallVector<uint8_t, 48> ShortPayload = MissingAuxiliary;
  ShortPayload.pop_back();
  ExpectAtomicRejection(ShortPayload, "exact version-2 payload");

  SmallVector<uint8_t, 48> LongPayload = MakeNativeFacts(UINT64_C(1));
  LongPayload.push_back(UINT8_C(0));
  ExpectAtomicRejection(LongPayload, "exact version-2 payload");

  const SmallVector<uint8_t, 48> ConflictingAuxiliary =
      MakeNativeFacts(UINT64_C(2));
  ExpectAtomicRejection(ConflictingAuxiliary,
                        "native st_size differs from ObjectGraph size");
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsWriterUnrepresentableUndefinedAndAbsoluteSizesWithoutMutation) {
  const auto MakeGraph = [](NevercObjectSymbolDefinition Definition,
                            NevercObjectSymbolBinding Binding, uint64_t Size) {
    auto Graph = makeObject(1);
    if (!Graph)
      return Graph;
    PluginObjectSection &Text = Graph->sections().front();
    Text.Name = ".text";
    Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Text.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    Text.Data.resize(8);

    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED
                      ? "sized_import"
                      : "sized_absolute";
    Symbol.Binding = Binding;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
    Symbol.Definition = Definition;
    Symbol.Value = Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE
                       ? UINT64_C(0x2a)
                       : 0;
    Symbol.Size = Size;
    if (Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED)
      Symbol.Flags = NEVERC_OBJECT_SYMBOL_IMPORTED;
    const uint64_t SymbolID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));

    if (Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED) {
      PluginObjectRelocation Relocation;
      Relocation.ID = Graph->allocateEntityID();
      Relocation.SectionID = Text.ID;
      Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
      Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
      Relocation.Width = 64;
      Relocation.TargetSymbolID = SymbolID;
      Graph->relocations().push_back(std::move(Relocation));
    }
    addAndroidKernelProfileContract(*Graph);
    return Graph;
  };

  for (NevercObjectSymbolDefinition Definition :
       {NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED,
        NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE}) {
    auto Graph = MakeGraph(Definition, NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL, 1);
    ASSERT_NE(Graph, nullptr);
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
    const uint64_t Generation = Graph->generation();
    const size_t SectionCount = Graph->sectionCount();
    const size_t SymbolCount = Graph->symbolCount();
    const std::string Snapshot = dumpPluginObjectGraph(*Graph);

    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Error Finalize = finalizeAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test writer-unrepresentable symbol size");
    ASSERT_TRUE(static_cast<bool>(Finalize));
    EXPECT_NE(errorText(std::move(Finalize)).find("nonzero size"),
              std::string::npos);
    EXPECT_EQ(Graph->generation(), Generation);
    EXPECT_EQ(Graph->sectionCount(), SectionCount);
    EXPECT_EQ(Graph->symbolCount(), SymbolCount);
    EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
  }

  for (NevercObjectSymbolDefinition Definition :
       {NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED,
        NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE}) {
    auto Graph = MakeGraph(Definition, NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL, 0);
    ASSERT_NE(Graph, nullptr);
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Error Finalize = finalizeAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test writer-representable zero symbol size");
    ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
    Error Audit = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test writer-representable zero symbol size audit");
    ASSERT_FALSE(Audit) << errorText(std::move(Audit));
  }

  auto WeakUndefined = MakeGraph(NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED,
                                 NEVERC_OBJECT_SYMBOL_BINDING_WEAK, 0);
  ASSERT_NE(WeakUndefined, nullptr);
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error WeakFinalize = finalizeAndroidKernelModuleObjectGraph(
      *WeakUndefined, Policy, "test writer-representable weak import");
  ASSERT_FALSE(WeakFinalize) << errorText(std::move(WeakFinalize));
  Error WeakAudit = verifyFinalAndroidKernelModuleObjectGraph(
      *WeakUndefined, Policy, "test writer-representable weak import audit");
  ASSERT_FALSE(WeakAudit) << errorText(std::move(WeakAudit));

  auto LocalUndefined = MakeGraph(NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED,
                                  NEVERC_OBJECT_SYMBOL_BINDING_LOCAL, 0);
  ASSERT_NE(LocalUndefined, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*LocalUndefined));
  const uint64_t LocalGeneration = LocalUndefined->generation();
  const std::string LocalSnapshot = dumpPluginObjectGraph(*LocalUndefined);
  Error LocalFinalize = finalizeAndroidKernelModuleObjectGraph(
      *LocalUndefined, Policy, "test writer-lossy local import");
  ASSERT_TRUE(static_cast<bool>(LocalFinalize));
  EXPECT_NE(errorText(std::move(LocalFinalize)).find("LOCAL binding"),
            std::string::npos);
  EXPECT_EQ(LocalUndefined->generation(), LocalGeneration);
  EXPECT_EQ(dumpPluginObjectGraph(*LocalUndefined), LocalSnapshot);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsRetainedSymbolOnlyComdatBeforeMutationAndSink) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  const auto MakeGraph = [&] {
    auto Graph = makeBuiltinObject(*ELFRoute, "symbol_only_comdat");
    if (!Graph)
      return Graph;
    PluginObjectComdat Comdat;
    Comdat.ID = Graph->allocateEntityID();
    Comdat.Name = "symbol_only_group";
    Comdat.Selection = NEVERC_OBJECT_COMDAT_ANY;
    Graph->symbols().front().ComdatID = Comdat.ID;
    Graph->comdats().push_back(std::move(Comdat));
    Graph->advanceGeneration();
    Graph->issueLayoutProof();
    return Graph;
  };

  auto FinalizedGraph = MakeGraph();
  ASSERT_NE(FinalizedGraph, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*FinalizedGraph));
  const uint64_t Generation = FinalizedGraph->generation();
  const std::string Before = dumpPluginObjectGraph(*FinalizedGraph);
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *FinalizedGraph, Policy, "test retained symbol-only COMDAT");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("COMDAT metadata"),
            std::string::npos);
  EXPECT_EQ(FinalizedGraph->generation(), Generation);
  EXPECT_EQ(dumpPluginObjectGraph(*FinalizedGraph), Before);

  auto DirectGraph = MakeGraph();
  ASSERT_NE(DirectGraph, nullptr);
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "direct-symbol-only-comdat.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  auto Rejected =
      (*Writer)->beginWrite(Scope.task(), *DirectGraph, Destination);
  ASSERT_FALSE(static_cast<bool>(Rejected));
  EXPECT_NE(errorText(Rejected.takeError()).find("COMDAT metadata"),
            std::string::npos);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), Destination.Name).has_value());

  DirectGraph->symbols().front().ComdatID = 0;
  DirectGraph->comdats().clear();
  DirectGraph->advanceGeneration();
  DirectGraph->issueLayoutProof();
  auto Image = (*Writer)->beginWrite(Scope.task(), *DirectGraph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Pending = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Pending)) << errorText(Pending.takeError());
  EXPECT_FALSE(Pending->empty());
  EXPECT_FALSE((*Image)->abort());
}

TEST(AndroidKernelModuleFinalizerTest,
     DirectReleaseWriterRejectsMalformedSymbolFactsBeforeOpeningSink) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Graph = makeBuiltinObject(*ELFRoute, "direct_release_symbol");
  ASSERT_NE(Graph, nullptr);
  PluginObjectSymbol &Symbol = Graph->symbols().front();
  Symbol.Extension.Owner = ELFRoute->ObjectFormatID;
  Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
  SmallVector<uint8_t, 48> NativeFacts;
  neverc::plugin::builtinext::appendHeader(
      NativeFacts, neverc::plugin::builtinext::SymbolTag,
      neverc::plugin::builtinext::SymbolVersion);
  neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STT_FUNC);
  neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STB_GLOBAL);
  neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STV_DEFAULT);
  Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());

  Graph->sections().front().Data.resize(8);
  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Graph->sections().front().ID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 64;
  Relocation.TargetSymbolID = Symbol.ID;
  Relocation.Extension.Owner = ELFRoute->ObjectFormatID;
  Relocation.Extension.Version = neverc::plugin::builtinext::RelocationVersion;
  const SmallVector<uint8_t, 80> RelocationFacts =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  Relocation.Extension.Bytes.assign(RelocationFacts.begin(),
                                    RelocationFacts.end());
  Graph->relocations().push_back(std::move(Relocation));
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());

  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "direct-malformed-release.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  auto Rejected = (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_FALSE(static_cast<bool>(Rejected));
  EXPECT_NE(errorText(Rejected.takeError()).find("exact version-2 payload"),
            std::string::npos);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), Destination.Name).has_value());

  // Reuse the exact destination after repairing the graph. Success proves the
  // rejected request never opened (and therefore never reserved) its sink.
  neverc::plugin::builtinext::appendU64(NativeFacts, Symbol.Size);
  neverc::plugin::builtinext::appendU64(
      NativeFacts, neverc::plugin::builtinext::SymbolNameNonEmpty);
  Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  auto Image = (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Pending = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Pending)) << errorText(Pending.takeError());
  EXPECT_FALSE(Pending->empty());
  EXPECT_FALSE((*Image)->abort());
}

TEST(AndroidKernelModuleFinalizerTest,
     DirectReleaseWriterRejectsSectionAndRelocationLossBeforeOpeningSink) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Graph = makeBuiltinObject(*ELFRoute, "direct_release_definition");
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Data.resize(8);
  Text.Extension.Owner = ELFRoute->ObjectFormatID;
  Text.Extension.Version = neverc::plugin::builtinext::SectionVersion;
  const uint64_t TextFlags = ELF::SHF_ALLOC | ELF::SHF_EXECINSTR;
  const SmallVector<uint8_t, 64> LossySectionFacts = makeELFSectionExtension(
      neverc::plugin::builtinext::SectionVersion, ELF::SHT_NOTE, TextFlags, 0);
  Text.Extension.Bytes.assign(LossySectionFacts.begin(),
                              LossySectionFacts.end());

  PluginObjectSymbol Import;
  Import.ID = Graph->allocateEntityID();
  Import.Name = "direct_release_import";
  Import.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Import.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
  Import.Flags = NEVERC_OBJECT_SYMBOL_IMPORTED;
  const uint64_t ImportID = Import.ID;
  Graph->symbols().push_back(std::move(Import));

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Text.ID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 64;
  Relocation.TargetSymbolID = ImportID;
  Relocation.Extension.Owner = ELFRoute->ObjectFormatID;
  Relocation.Extension.Version = neverc::plugin::builtinext::RelocationVersion;
  const SmallVector<uint8_t, 80> LossyRelocationFacts =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS32");
  Relocation.Extension.Bytes.assign(LossyRelocationFacts.begin(),
                                    LossyRelocationFacts.end());
  const uint64_t RelocationID = Relocation.ID;
  Graph->relocations().push_back(std::move(Relocation));
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "direct-lossy-native-facts.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;

  auto RejectedSection =
      (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_FALSE(static_cast<bool>(RejectedSection));
  EXPECT_NE(errorText(RejectedSection.takeError()).find("native section type"),
            std::string::npos);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), Destination.Name).has_value());

  const SmallVector<uint8_t, 64> ValidSectionFacts =
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS, TextFlags, 0);
  Text.Extension.Bytes.assign(ValidSectionFacts.begin(),
                              ValidSectionFacts.end());
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  auto RejectedRelocation =
      (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_FALSE(static_cast<bool>(RejectedRelocation));
  EXPECT_NE(errorText(RejectedRelocation.takeError())
                .find("official relocation name"),
            std::string::npos);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), Destination.Name).has_value());

  PluginObjectRelocation *FinalRelocation = Graph->findRelocation(RelocationID);
  ASSERT_NE(FinalRelocation, nullptr);
  const SmallVector<uint8_t, 80> ValidRelocationFacts =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  FinalRelocation->Extension.Bytes.assign(ValidRelocationFacts.begin(),
                                          ValidRelocationFacts.end());
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  auto Image = (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Pending = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Pending)) << errorText(Pending.takeError());
  EXPECT_FALSE(Pending->empty());
  EXPECT_FALSE((*Image)->abort());
}

TEST(AndroidKernelModuleFinalizerTest,
     DemotedPCGNativeSymbolFactsRemainWriterConsistent) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Graph = makeBuiltinObject(*ELFRoute, "pcg_entry.__pcg1234");
  ASSERT_NE(Graph, nullptr);
  PluginObjectSymbol &Symbol = Graph->symbols().front();
  Symbol.Flags = NEVERC_OBJECT_SYMBOL_EXPORTED;
  Symbol.Extension.Owner = ELFRoute->ObjectFormatID;
  Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
  const SmallVector<uint8_t, 48> NativeFacts = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_DEFAULT, Symbol.Size);
  Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());

  Graph->sections().front().Data.resize(8);
  PluginObjectRelocation RequiredReference;
  RequiredReference.ID = Graph->allocateEntityID();
  RequiredReference.SectionID = Graph->sections().front().ID;
  RequiredReference.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  RequiredReference.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  RequiredReference.Width = 64;
  RequiredReference.TargetSymbolID = Symbol.ID;
  RequiredReference.Extension.Owner = ELFRoute->ObjectFormatID;
  RequiredReference.Extension.Version =
      neverc::plugin::builtinext::RelocationVersion;
  const SmallVector<uint8_t, 80> RequiredReferenceFacts =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  RequiredReference.Extension.Bytes.assign(RequiredReferenceFacts.begin(),
                                           RequiredReferenceFacts.end());
  Graph->relocations().push_back(std::move(RequiredReference));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test PCG native binding demotion");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  ASSERT_EQ(Graph->symbolCount(), 1U);
  const PluginObjectSymbol &Demoted = Graph->symbols().front();
  EXPECT_EQ(Demoted.Binding, NEVERC_OBJECT_SYMBOL_BINDING_LOCAL);
  EXPECT_EQ(Demoted.Flags & NEVERC_OBJECT_SYMBOL_EXPORTED, 0U);
  const std::optional<uint64_t> ExtendedBinding =
      neverc::plugin::builtinext::field(
          Demoted.Extension.Bytes, neverc::plugin::builtinext::SymbolBinding);
  ASSERT_TRUE(ExtendedBinding.has_value());
  EXPECT_EQ(*ExtendedBinding, ELF::STB_LOCAL);
  EXPECT_NE(Demoted.Name, "pcg_entry.__pcg1234");
  EXPECT_TRUE(neverc::hasCanonicalReleaseNameShape(Demoted.Name));

  Graph->issueLayoutProof();
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "pcg-native-binding-release.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  auto Image = (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Bytes = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Bytes)) << errorText(Bytes.takeError());
  auto Semantics = readELFSemantics(*Bytes);
  ASSERT_TRUE(static_cast<bool>(Semantics)) << errorText(Semantics.takeError());
  // MC may fold a local defined relocation target into its section symbol.
  // That is semantically equivalent and intentionally omits even the
  // canonical local PCG spelling from the serialized table.
  EXPECT_EQ(llvm::find_if(Semantics->Symbols,
                          [&](const ELFSymbolSemantics &Candidate) {
                            return Candidate.Name == Demoted.Name;
                          }),
            Semantics->Symbols.end());
  EXPECT_EQ(llvm::find_if(Semantics->Symbols,
                          [&](const ELFSymbolSemantics &Candidate) {
                            return Candidate.Name == "pcg_entry.__pcg1234";
                          }),
            Semantics->Symbols.end());
  EXPECT_FALSE(containsBytes(*Bytes, "pcg_entry.__pcg1234"));

  std::vector<const ELFSymbolSemantics *> NonMappingSymbols;
  for (const ELFSymbolSemantics &Candidate : Semantics->Symbols)
    if (!isAArch64MappingSymbol(Candidate.Name))
      NonMappingSymbols.push_back(&Candidate);
  ASSERT_EQ(NonMappingSymbols.size(), 2U);
  llvm::sort(NonMappingSymbols, [](const ELFSymbolSemantics *Left,
                                   const ELFSymbolSemantics *Right) {
    return Left->Name < Right->Name;
  });
  EXPECT_EQ(NonMappingSymbols[0]->Name, "__start_alloc_tags");
  EXPECT_EQ(NonMappingSymbols[1]->Name, "__stop_alloc_tags");
  for (const ELFSymbolSemantics *BoundarySymbol : NonMappingSymbols) {
    EXPECT_EQ(BoundarySymbol->Type, ELF::STT_NOTYPE);
    EXPECT_EQ(BoundarySymbol->Binding, ELF::STB_GLOBAL);
    EXPECT_EQ(BoundarySymbol->Section, ".codetag.alloc_tags");
    EXPECT_EQ(BoundarySymbol->Value, 0U);
  }
  ASSERT_EQ(Semantics->Relocations.size(), 1U);
  const ELFRelocationSemantics &SerializedRelocation =
      Semantics->Relocations.front();
  EXPECT_EQ(SerializedRelocation.Section, ".text");
  EXPECT_EQ(SerializedRelocation.Offset, 0U);
  EXPECT_EQ(SerializedRelocation.Type, ELF::R_AARCH64_ABS64);
  EXPECT_TRUE(SerializedRelocation.Target.empty());
  EXPECT_EQ(SerializedRelocation.Addend, 0);
  EXPECT_FALSE((*Image)->abort());
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsLossyNativeSectionExtensionBeforeMutation) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  const uint64_t TextFlags = ELF::SHF_ALLOC | ELF::SHF_EXECINSTR;
  const auto ExpectAtomicRejection = [&](ArrayRef<uint8_t> NativeFacts,
                                         uint32_t OuterVersion,
                                         StringRef ExpectedReason,
                                         NevercObjectSectionFlags StableFlags =
                                             NEVERC_OBJECT_SECTION_ALLOCATED |
                                             NEVERC_OBJECT_SECTION_EXECUTABLE,
                                         bool WithComdat = false) {
    auto Graph = makeBuiltinObject(*ELFRoute, "section_extension_symbol");
    ASSERT_NE(Graph, nullptr);
    PluginObjectSection &Section = Graph->sections().front();
    Section.Flags = StableFlags;
    Section.Extension.Owner = ELFRoute->ObjectFormatID;
    Section.Extension.Version = OuterVersion;
    Section.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    if (WithComdat) {
      PluginObjectComdat Comdat;
      Comdat.ID = Graph->allocateEntityID();
      Comdat.Name = "section_extension_group";
      Comdat.Selection = NEVERC_OBJECT_COMDAT_ANY;
      Section.ComdatID = Comdat.ID;
      Graph->comdats().push_back(std::move(Comdat));
      Graph->symbols().front().ComdatID = Section.ComdatID;
    }
    addAndroidKernelProfileContract(*Graph);
    Graph->advanceGeneration();
    Graph->issueLayoutProof();
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

    const uint64_t Generation = Graph->generation();
    const size_t SectionCount = Graph->sectionCount();
    const size_t SymbolCount = Graph->symbolCount();
    const size_t ComdatCount = Graph->comdatCount();
    const std::string Snapshot = dumpPluginObjectGraph(*Graph);
    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Error Finalize = finalizeAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test lossy native section extension");
    ASSERT_TRUE(static_cast<bool>(Finalize));
    EXPECT_NE(errorText(std::move(Finalize)).find(ExpectedReason.str()),
              std::string::npos);
    EXPECT_EQ(Graph->generation(), Generation);
    EXPECT_EQ(Graph->sectionCount(), SectionCount);
    EXPECT_EQ(Graph->symbolCount(), SymbolCount);
    EXPECT_EQ(Graph->comdatCount(), ComdatCount);
    EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
  };

  SmallVector<uint8_t, 64> Short =
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS, TextFlags, 0);
  Short.pop_back();
  ExpectAtomicRejection(Short, neverc::plugin::builtinext::SectionVersion,
                        "exact version-2 payload");

  SmallVector<uint8_t, 64> Long =
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS, TextFlags, 0);
  Long.push_back(0);
  ExpectAtomicRejection(Long, neverc::plugin::builtinext::SectionVersion,
                        "exact version-2 payload");

  SmallVector<uint8_t, 64> InnerV1 = makeELFSectionExtension(
      1, ELF::SHT_PROGBITS, TextFlags, 0, /*IncludeEntrySize=*/false);
  ExpectAtomicRejection(InnerV1, neverc::plugin::builtinext::SectionVersion,
                        "version metadata disagrees");

  ExpectAtomicRejection(
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_NOTE, TextFlags, 0),
      neverc::plugin::builtinext::SectionVersion, "native section type");
  ExpectAtomicRejection(
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_LLVM_ADDRSIG, TextFlags, 0),
      neverc::plugin::builtinext::SectionVersion, "native section type");
  ExpectAtomicRejection(
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS,
                              TextFlags | ELF::SHF_LINK_ORDER, 0),
      neverc::plugin::builtinext::SectionVersion, "native section flags");
  ExpectAtomicRejection(
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS, TextFlags | ELF::SHF_MERGE, 0),
      neverc::plugin::builtinext::SectionVersion, "nonzero entry size",
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE |
          NEVERC_OBJECT_SECTION_MERGEABLE);
  ExpectAtomicRejection(
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS, TextFlags | ELF::SHF_GROUP, 0),
      neverc::plugin::builtinext::SectionVersion, "COMDAT metadata",
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE,
      /*WithComdat=*/true);

  auto Valid = makeBuiltinObject(*ELFRoute, "valid_section_extension_symbol");
  ASSERT_NE(Valid, nullptr);
  PluginObjectSection &ValidSection = Valid->sections().front();
  ValidSection.Extension.Owner = ELFRoute->ObjectFormatID;
  ValidSection.Extension.Version = neverc::plugin::builtinext::SectionVersion;
  const SmallVector<uint8_t, 64> ValidFacts =
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS, TextFlags, 0);
  ValidSection.Extension.Bytes.assign(ValidFacts.begin(), ValidFacts.end());
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Valid, Policy, "test lossless native section extension");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));

  auto ValidV1 = makeBuiltinObject(*ELFRoute, "valid_v1_section_extension");
  ASSERT_NE(ValidV1, nullptr);
  PluginObjectSection &ValidV1Section = ValidV1->sections().front();
  ValidV1Section.Extension.Owner = ELFRoute->ObjectFormatID;
  ValidV1Section.Extension.Version = 1;
  const SmallVector<uint8_t, 64> ValidV1Facts = makeELFSectionExtension(
      1, ELF::SHT_PROGBITS, TextFlags, 0, /*IncludeEntrySize=*/false);
  ValidV1Section.Extension.Bytes.assign(ValidV1Facts.begin(),
                                        ValidV1Facts.end());
  Error FinalizeV1 = finalizeAndroidKernelModuleObjectGraph(
      *ValidV1, Policy, "test lossless version-1 native section extension");
  ASSERT_FALSE(FinalizeV1) << errorText(std::move(FinalizeV1));
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsContradictoryNativeRelocationExtensionBeforeMutation) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  const auto MakeGraph = [&](ArrayRef<uint8_t> NativeFacts,
                             uint32_t OuterVersion,
                             NevercObjectRelocationKind Kind, uint32_t Width,
                             bool PCRelative, bool Signed) {
    auto Graph = makeBuiltinObject(*ELFRoute, "relocation_source");
    if (!Graph)
      return Graph;
    PluginObjectSection &Text = Graph->sections().front();
    Text.Data.resize(8);

    PluginObjectSymbol Import;
    Import.ID = Graph->allocateEntityID();
    Import.Name = "relocation_import";
    Import.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Import.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
    Import.Flags = NEVERC_OBJECT_SYMBOL_IMPORTED;
    const uint64_t ImportID = Import.ID;
    Graph->symbols().push_back(std::move(Import));

    PluginObjectRelocation Relocation;
    Relocation.ID = Graph->allocateEntityID();
    Relocation.SectionID = Text.ID;
    Relocation.Kind = Kind;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.Width = Width;
    Relocation.IsPCRelative = PCRelative;
    Relocation.IsSigned = Signed;
    Relocation.TargetSymbolID = ImportID;
    Relocation.Extension.Owner = ELFRoute->ObjectFormatID;
    Relocation.Extension.Version = OuterVersion;
    Relocation.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Graph->relocations().push_back(std::move(Relocation));
    addAndroidKernelProfileContract(*Graph);
    Graph->advanceGeneration();
    Graph->issueLayoutProof();
    return Graph;
  };
  const auto ExpectAtomicRejection =
      [&](ArrayRef<uint8_t> NativeFacts, StringRef ExpectedReason,
          NevercObjectRelocationKind Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE,
          uint32_t Width = 64, bool PCRelative = false, bool Signed = false,
          uint32_t OuterVersion =
              neverc::plugin::builtinext::RelocationVersion) {
        auto Graph = MakeGraph(NativeFacts, OuterVersion, Kind, Width,
                               PCRelative, Signed);
        ASSERT_NE(Graph, nullptr);
        ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
        const uint64_t Generation = Graph->generation();
        const size_t SectionCount = Graph->sectionCount();
        const size_t SymbolCount = Graph->symbolCount();
        const size_t RelocationCount = Graph->relocationCount();
        const std::string Snapshot = dumpPluginObjectGraph(*Graph);

        AndroidKernelModuleFinalizationPolicy Policy;
        Policy.StripUnneededSymbols = true;
        Error Finalize = finalizeAndroidKernelModuleObjectGraph(
            *Graph, Policy, "test contradictory native relocation extension");
        ASSERT_TRUE(static_cast<bool>(Finalize));
        EXPECT_NE(errorText(std::move(Finalize)).find(ExpectedReason.str()),
                  std::string::npos);
        EXPECT_EQ(Graph->generation(), Generation);
        EXPECT_EQ(Graph->sectionCount(), SectionCount);
        EXPECT_EQ(Graph->symbolCount(), SymbolCount);
        EXPECT_EQ(Graph->relocationCount(), RelocationCount);
        EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
      };

  SmallVector<uint8_t, 80> Short =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  Short.pop_back();
  ExpectAtomicRejection(Short, "exact version-1 payload");

  SmallVector<uint8_t, 80> Long =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  Long.push_back(0);
  ExpectAtomicRejection(Long, "exact version-1 payload");

  ExpectAtomicRejection(
      makeELFRelocationExtension(0, ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64"),
      "version metadata disagrees");
  ExpectAtomicRejection(
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS32"),
      "official relocation name");
  ExpectAtomicRejection(
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_TLS_DTPMOD64,
                                 "R_AARCH64_TLS_DTPMOD64"),
      "Android AArch64 module loader", NEVERC_OBJECT_RELOCATION_TLS, 64, false,
      false);

  const SmallVector<uint8_t, 80> ABS64 =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  ExpectAtomicRejection(ABS64, "stable relocation facts",
                        NEVERC_OBJECT_RELOCATION_GOT_RELATIVE, 64, false,
                        false);
  ExpectAtomicRejection(ABS64, "stable relocation facts",
                        NEVERC_OBJECT_RELOCATION_ABSOLUTE, 32, false, false);
  ExpectAtomicRejection(ABS64, "stable relocation facts",
                        NEVERC_OBJECT_RELOCATION_ABSOLUTE, 64, true, false);
  ExpectAtomicRejection(ABS64, "stable relocation facts",
                        NEVERC_OBJECT_RELOCATION_ABSOLUTE, 64, false, true);

  auto Valid = MakeGraph(ABS64, neverc::plugin::builtinext::RelocationVersion,
                         NEVERC_OBJECT_RELOCATION_ABSOLUTE, 64, false, false);
  ASSERT_NE(Valid, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*Valid));
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Valid, Policy, "test lossless native relocation extension");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
}

TEST(AndroidKernelModuleFinalizerTest,
     IgnoresUnrepresentableNativeFactsOwnedOnlyByDroppedDebugSection) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Graph = makeBuiltinObject(*ELFRoute, "retained_debug_reference");
  ASSERT_NE(Graph, nullptr);
  Graph->sections().front().Data.resize(8);
  const uint64_t RetainedSymbolID = Graph->symbols().front().ID;

  PluginObjectSection Debug;
  Debug.ID = Graph->allocateEntityID();
  Debug.Name = ".debug_info";
  Debug.Kind = NEVERC_OBJECT_SECTION_KIND_DEBUG;
  Debug.Flags = NEVERC_OBJECT_SECTION_DEBUG;
  Debug.Alignment = 1;
  Debug.Data.resize(8);
  Debug.Extension.Owner = ELFRoute->ObjectFormatID;
  Debug.Extension.Version = neverc::plugin::builtinext::SectionVersion;
  Debug.Extension.Bytes = {UINT8_C(0x62), UINT8_C(0x61), UINT8_C(0x64)};
  const uint64_t DebugSectionID = Debug.ID;
  Graph->sections().push_back(std::move(Debug));

  PluginObjectRelocation DebugRelocation;
  DebugRelocation.ID = Graph->allocateEntityID();
  DebugRelocation.SectionID = DebugSectionID;
  DebugRelocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  DebugRelocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  DebugRelocation.Width = 64;
  DebugRelocation.TargetSymbolID = RetainedSymbolID;
  DebugRelocation.Extension.Owner = ELFRoute->ObjectFormatID;
  DebugRelocation.Extension.Version =
      neverc::plugin::builtinext::RelocationVersion;
  DebugRelocation.Extension.Bytes = {UINT8_C(0x62), UINT8_C(0x61),
                                     UINT8_C(0x64)};
  Graph->relocations().push_back(std::move(DebugRelocation));
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test dropped debug native facts");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  EXPECT_EQ(Graph->generation(), Generation + 1);
  EXPECT_EQ(Graph->sectionCount(), 1U);
  EXPECT_EQ(Graph->relocationCount(), 0U);
  EXPECT_EQ(Graph->sections().front().Name, ".text");
}

TEST(AndroidKernelModuleFinalizerTest,
     SerializedSectionRelocationPreservesTargetValue) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto MakeGraph = [&](uint64_t TargetValue, int64_t Addend) {
    auto Graph = makeBuiltinObject(*ELFRoute, "section_target_source");
    if (!Graph)
      return Graph;
    PluginObjectSection &Text = Graph->sections().front();
    Text.Data.resize(8);
    PluginObjectSection Data;
    Data.ID = Graph->allocateEntityID();
    Data.Name = ".data";
    Data.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    Data.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
    Data.Alignment = 8;
    Data.Data.resize(16);
    const uint64_t DataID = Data.ID;
    Graph->sections().push_back(std::move(Data));

    PluginObjectRelocation Relocation;
    Relocation.ID = Graph->allocateEntityID();
    Relocation.SectionID = Text.ID;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SECTION;
    Relocation.Width = 64;
    Relocation.TargetSectionID = DataID;
    Relocation.TargetValue = TargetValue;
    Relocation.Addend = Addend;
    Relocation.Extension.Owner = ELFRoute->ObjectFormatID;
    Relocation.Extension.Version =
        neverc::plugin::builtinext::RelocationVersion;
    const SmallVector<uint8_t, 80> NativeFacts = makeELFRelocationExtension(
        neverc::plugin::builtinext::RelocationVersion, ELF::R_AARCH64_ABS64,
        "R_AARCH64_ABS64");
    Relocation.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Graph->relocations().push_back(std::move(Relocation));
    Graph->advanceGeneration();
    Graph->issueLayoutProof();
    return Graph;
  };

  auto Graph = MakeGraph(4, 3);
  ASSERT_NE(Graph, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "section-target-value.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  auto Image = (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Bytes = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Bytes)) << errorText(Bytes.takeError());
  auto Semantics = readELFSemantics(*Bytes);
  ASSERT_TRUE(static_cast<bool>(Semantics)) << errorText(Semantics.takeError());
  ASSERT_EQ(Semantics->Relocations.size(), 1U);
  EXPECT_EQ(Semantics->Relocations.front().Section, ".text");
  EXPECT_EQ(Semantics->Relocations.front().Type, ELF::R_AARCH64_ABS64);
  EXPECT_TRUE(Semantics->Relocations.front().Target.empty());
  EXPECT_EQ(Semantics->Relocations.front().Addend, 7);
  EXPECT_FALSE((*Image)->abort());

  auto Outside = MakeGraph(17, 0);
  ASSERT_NE(Outside, nullptr);
  const uint64_t OutsideGeneration = Outside->generation();
  const std::string OutsideSnapshot = dumpPluginObjectGraph(*Outside);
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error OutsideFinalize = finalizeAndroidKernelModuleObjectGraph(
      *Outside, Policy, "test outside section target value");
  ASSERT_TRUE(static_cast<bool>(OutsideFinalize));
  EXPECT_NE(errorText(std::move(OutsideFinalize)).find("target section"),
            std::string::npos);
  EXPECT_EQ(Outside->generation(), OutsideGeneration);
  EXPECT_EQ(dumpPluginObjectGraph(*Outside), OutsideSnapshot);

  auto Overflow = MakeGraph(1, std::numeric_limits<int64_t>::max());
  ASSERT_NE(Overflow, nullptr);
  const uint64_t OverflowGeneration = Overflow->generation();
  const std::string OverflowSnapshot = dumpPluginObjectGraph(*Overflow);
  Error OverflowFinalize = finalizeAndroidKernelModuleObjectGraph(
      *Overflow, Policy, "test overflowing section target value");
  ASSERT_TRUE(static_cast<bool>(OverflowFinalize));
  EXPECT_NE(errorText(std::move(OverflowFinalize)).find("overflows"),
            std::string::npos);
  EXPECT_EQ(Overflow->generation(), OverflowGeneration);
  EXPECT_EQ(dumpPluginObjectGraph(*Overflow), OverflowSnapshot);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsReservedNameCollisionWithoutMutation) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data.resize(8);

  PluginObjectSymbol Function;
  Function.ID = Graph->allocateEntityID();
  Function.Name = "ordinary_function";
  Function.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Function.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Function.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Function.SectionID = Text.ID;
  Function.Size = 1;
  Graph->symbols().push_back(std::move(Function));

  PluginObjectSymbol Import;
  Import.ID = Graph->allocateEntityID();
  Import.Name = "fn_0";
  Import.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Import.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
  const uint64_t ImportID = Import.ID;
  Graph->symbols().push_back(std::move(Import));

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Text.ID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 64;
  Relocation.TargetSymbolID = ImportID;
  Graph->relocations().push_back(std::move(Relocation));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  const std::string Snapshot = dumpPluginObjectGraph(*Graph);
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test reserved release name collision");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("collides with reserved"),
            std::string::npos);
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsUnrepresentablePlannedNameOwnershipWithoutMutation) {
  const auto ExpectAtomicRejection =
      [](std::unique_ptr<PluginObjectGraph> Graph, StringRef ExpectedReason) {
        ASSERT_NE(Graph, nullptr);
        Graph->advanceGeneration();
        Error Verify = verifyPluginObjectGraph(*Graph);
        ASSERT_FALSE(Verify) << errorText(std::move(Verify));
        const uint64_t Generation = Graph->generation();
        const size_t SectionCount = Graph->sectionCount();
        const size_t SymbolCount = Graph->symbolCount();
        const std::string Snapshot = dumpPluginObjectGraph(*Graph);

        AndroidKernelModuleFinalizationPolicy Policy;
        Policy.StripUnneededSymbols = true;
        Error Finalize = finalizeAndroidKernelModuleObjectGraph(
            *Graph, Policy, "test duplicate planned release name ownership");
        ASSERT_TRUE(static_cast<bool>(Finalize));
        EXPECT_NE(errorText(std::move(Finalize)).find(ExpectedReason.str()),
                  std::string::npos);
        EXPECT_EQ(Graph->generation(), Generation);
        EXPECT_EQ(Graph->sectionCount(), SectionCount);
        EXPECT_EQ(Graph->symbolCount(), SymbolCount);
        EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
      };

  auto DuplicateDefinitions = makeObject(1);
  ASSERT_NE(DuplicateDefinitions, nullptr);
  PluginObjectSection &Metadata = DuplicateDefinitions->sections().front();
  Metadata.Name = ".modinfo";
  Metadata.Data.resize(8);
  for (unsigned I = 0; I != 2; ++I) {
    PluginObjectSymbol Symbol;
    Symbol.ID = DuplicateDefinitions->allocateEntityID();
    Symbol.Name = "duplicate_metadata";
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_WEAK;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = Metadata.ID;
    Symbol.Value = I * 4;
    Symbol.Size = 4;
    DuplicateDefinitions->symbols().push_back(std::move(Symbol));
  }
  addAndroidKernelProfileContract(*DuplicateDefinitions);
  ExpectAtomicRejection(std::move(DuplicateDefinitions),
                        "multiple retained symbols");

  auto ConflictingImports = makeObject(1);
  ASSERT_NE(ConflictingImports, nullptr);
  PluginObjectSection &Text = ConflictingImports->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data.resize(16);
  for (NevercObjectSymbolBinding Binding :
       {NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
        NEVERC_OBJECT_SYMBOL_BINDING_WEAK}) {
    PluginObjectSymbol Import;
    Import.ID = ConflictingImports->allocateEntityID();
    Import.Name = "duplicate_import";
    Import.Binding = Binding;
    Import.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
    Import.Flags = NEVERC_OBJECT_SYMBOL_IMPORTED;
    const uint64_t ImportID = Import.ID;
    ConflictingImports->symbols().push_back(std::move(Import));

    PluginObjectRelocation Relocation;
    Relocation.ID = ConflictingImports->allocateEntityID();
    Relocation.SectionID = Text.ID;
    Relocation.Offset = Binding == NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL ? 0 : 8;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.Width = 64;
    Relocation.TargetSymbolID = ImportID;
    ConflictingImports->relocations().push_back(std::move(Relocation));
  }
  ExpectAtomicRejection(std::move(ConflictingImports),
                        "different observable attributes");
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalProvenanceRequiresReleaseStripWithoutMutation) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  const uint64_t Generation = Graph->generation();
  const std::string Snapshot = dumpPluginObjectGraph(*Graph);

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test canonical provenance without release strip");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("requires the release"),
            std::string::npos);
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
}

TEST(AndroidKernelModuleFinalizerTest,
     PreservedNamesCannotBypassMalformedGraphValidation) {
  auto ExpectRejected = [&](std::unique_ptr<PluginObjectGraph> Graph) {
    ASSERT_NE(Graph, nullptr);
    const uint64_t Generation = Graph->generation();
    const std::string Snapshot = dumpPluginObjectGraph(*Graph);
    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Error Finalize = finalizeAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test malformed preserved release symbol");
    EXPECT_TRUE(static_cast<bool>(Finalize));
    if (Finalize)
      consumeError(std::move(Finalize));
    EXPECT_EQ(Graph->generation(), Generation);
    EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
  };

  auto MissingSection = makeObject(0);
  ASSERT_NE(MissingSection, nullptr);
  PluginObjectSymbol Missing;
  Missing.ID = MissingSection->allocateEntityID();
  Missing.Name = "init_module";
  Missing.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Missing.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Missing.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Missing.SectionID = UINT64_C(0xdead);
  MissingSection->symbols().push_back(std::move(Missing));
  ExpectRejected(std::move(MissingSection));

  auto OutOfRange = makeObject(1);
  ASSERT_NE(OutOfRange, nullptr);
  PluginObjectSymbol PastEnd;
  PastEnd.ID = OutOfRange->allocateEntityID();
  PastEnd.Name = "__kcfi_typeid_past_end";
  PastEnd.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  PastEnd.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  PastEnd.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  PastEnd.SectionID = OutOfRange->sections().front().ID;
  PastEnd.Value = 2;
  PastEnd.Size = 1;
  OutOfRange->symbols().push_back(std::move(PastEnd));
  ExpectRejected(std::move(OutOfRange));

  auto Overflow = makeObject(1);
  ASSERT_NE(Overflow, nullptr);
  PluginObjectSection &OverflowSection = Overflow->sections().front();
  OverflowSection.Kind = NEVERC_OBJECT_SECTION_KIND_ZERO_FILL;
  OverflowSection.ZeroFillSize = std::numeric_limits<uint64_t>::max();
  ExpectRejected(std::move(Overflow));
}

TEST(AndroidKernelModuleFinalizerTest, CommonSymbolRefusalIsAtomic) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  Graph->sections().front().Data.resize(8);
  const uint64_t SectionID = Graph->sections().front().ID;

  PluginObjectSymbol Ordinary;
  Ordinary.ID = Graph->allocateEntityID();
  Ordinary.Name = "ordinary_before_common_failure";
  Ordinary.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Ordinary.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  Ordinary.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Ordinary.SectionID = SectionID;
  Ordinary.Size = 1;
  const uint64_t OrdinaryID = Ordinary.ID;
  Graph->symbols().push_back(std::move(Ordinary));

  PluginObjectSymbol Common;
  Common.ID = Graph->allocateEntityID();
  Common.Name = "unsupported_common";
  // Exercise the prune-candidate case: COMMON is an unsupported input class,
  // not an unneeded symbol that may disappear before policy validation.
  Common.Binding = NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
  Common.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  Common.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON;
  Common.Size = 8;
  Common.Alignment = 8;
  Graph->symbols().push_back(std::move(Common));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  const size_t Symbols = Graph->symbolCount();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module COMMON");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("COMMON symbol"),
            std::string::npos);
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(Graph->symbolCount(), Symbols);
  ASSERT_NE(Graph->findSymbol(OrdinaryID), nullptr);
  EXPECT_EQ(Graph->findSymbol(OrdinaryID)->Name,
            "ordinary_before_common_failure");
}

TEST(AndroidKernelModuleFinalizerTest, LivePatchSectionRefusalIsAtomic) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);

  PluginObjectSection LivePatch;
  LivePatch.ID = Graph->allocateEntityID();
  LivePatch.Name = ".klp.rela.example";
  LivePatch.Alignment = 1;
  LivePatch.Data = {0};
  Graph->sections().push_back(std::move(LivePatch));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  const size_t Sections = Graph->sectionCount();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module livepatch");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("livepatch section"),
            std::string::npos);
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(Graph->sectionCount(), Sections);
}

TEST(AndroidKernelModuleFinalizerTest, LivePatchModInfoRefusalIsAtomic) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);

  PluginObjectSection ModInfo;
  ModInfo.ID = Graph->allocateEntityID();
  ModInfo.Name = ".modinfo";
  ModInfo.Alignment = 1;
  constexpr char Marker[] = "license=GPL\0livepatch=Y\0";
  ModInfo.Data.assign(Marker, Marker + sizeof(Marker));
  Graph->sections().push_back(std::move(ModInfo));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  const size_t Sections = Graph->sectionCount();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module livepatch metadata");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("marked livepatch"),
            std::string::npos);
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(Graph->sectionCount(), Sections);
}

TEST(AndroidKernelModuleFinalizerTest,
     BuiltinAdapterAuditsNativeLivePatchBeforeGraphSerialization) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Input = makeBuiltinObject(*ELFRoute, "livepatch_source");
  auto PartialTarget = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_NE(Input, nullptr);
  ASSERT_TRUE(static_cast<bool>(PartialTarget))
      << errorText(PartialTarget.takeError());
  addAndroidKernelProfileContract(*Input);

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  std::array<PluginObjectGraph *, 1> Inputs{Input.get()};
  BuiltinObjectMergeConfig PartialConfig;
  PartialConfig.AndroidKernelModule = true;
  auto Partial = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*PartialTarget), Inputs,
      ArrayRef<ArrayRef<uint8_t>>{}, NEVERC_LINK_OPTION_NONE, PartialConfig);
  ASSERT_TRUE(static_cast<bool>(Partial)) << errorText(Partial.takeError());

  std::vector<uint8_t> LivePatchImage(Partial->MergedImage.begin(),
                                      Partial->MergedImage.end());
  auto Parsed = object::ObjectFile::createObjectFile(MemoryBufferRef(
      StringRef(reinterpret_cast<const char *>(LivePatchImage.data()),
                LivePatchImage.size()),
      "livepatch adapter test input"));
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  auto *ELFObject = dyn_cast<object::ELF64LEObjectFile>(Parsed->get());
  ASSERT_NE(ELFObject, nullptr);
  bool Patched = false;
  for (const object::SymbolRef &Symbol : ELFObject->symbols()) {
    auto Name = Symbol.getName();
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name != "livepatch_source")
      continue;
    auto Native = ELFObject->getSymbol(Symbol.getRawDataRefImpl());
    ASSERT_TRUE(static_cast<bool>(Native)) << errorText(Native.takeError());
    object::ELF64LE::Sym Replacement = **Native;
    Replacement.st_shndx =
        neverc::AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex;
    const auto *NativeBytes = reinterpret_cast<const uint8_t *>(*Native);
    ASSERT_GE(NativeBytes, LivePatchImage.data());
    const size_t Offset = NativeBytes - LivePatchImage.data();
    ASSERT_LE(Offset + sizeof(Replacement), LivePatchImage.size());
    std::memcpy(LivePatchImage.data() + Offset, &Replacement,
                sizeof(Replacement));
    Patched = true;
    break;
  }
  ASSERT_TRUE(Patched);

  auto ReleaseTarget = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_TRUE(static_cast<bool>(ReleaseTarget))
      << errorText(ReleaseTarget.takeError());
  std::array<ArrayRef<uint8_t>, 1> NativeInputs{
      ArrayRef<uint8_t>(LivePatchImage)};
  std::array<PluginObjectGraph *, 1> ReleaseInputs{Partial->Object.get()};
  BuiltinObjectMergeConfig ReleaseConfig;
  ReleaseConfig.AndroidKernelModule = true;
  ReleaseConfig.FinalizeAndroidKernelModule = true;
  ReleaseConfig.StripUnneededSymbols = true;
  auto Release = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*ReleaseTarget),
      ArrayRef<PluginObjectGraph *>(ReleaseInputs), NativeInputs,
      NEVERC_LINK_OPTION_NONE, ReleaseConfig);
  ASSERT_FALSE(static_cast<bool>(Release));
  const std::string Message = errorText(Release.takeError());
  EXPECT_NE(Message.find("native input image 0"), std::string::npos);
  EXPECT_NE(Message.find("livepatch symbol 'livepatch_source'"),
            std::string::npos);
}

TEST(AndroidKernelModuleFinalizerTest,
     ReleaseStripRejectsRetainedRelocationToDroppedDebugEntity) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  const uint64_t TextSectionID = Graph->sections().front().ID;

  PluginObjectSection Debug;
  Debug.ID = Graph->allocateEntityID();
  Debug.Name = ".debug_info";
  Debug.Kind = NEVERC_OBJECT_SECTION_KIND_DEBUG;
  Debug.Flags = NEVERC_OBJECT_SECTION_DEBUG;
  Debug.Alignment = 1;
  Debug.Data = {0};
  const uint64_t DebugSectionID = Debug.ID;
  Graph->sections().push_back(std::move(Debug));

  PluginObjectSymbol DebugSymbol;
  DebugSymbol.ID = Graph->allocateEntityID();
  DebugSymbol.Name = "release_debug_target";
  DebugSymbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  DebugSymbol.SectionID = DebugSectionID;
  DebugSymbol.Size = 1;
  const uint64_t DebugSymbolID = DebugSymbol.ID;
  Graph->symbols().push_back(std::move(DebugSymbol));

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = TextSectionID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 8;
  Relocation.TargetSymbolID = DebugSymbolID;
  Graph->relocations().push_back(std::move(Relocation));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  const size_t Sections = Graph->sectionCount();
  const size_t Symbols = Graph->symbolCount();
  const size_t Relocations = Graph->relocationCount();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("retained section references"),
            std::string::npos);
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(Graph->sectionCount(), Sections);
  EXPECT_EQ(Graph->symbolCount(), Symbols);
  EXPECT_EQ(Graph->relocationCount(), Relocations);
}

TEST(AndroidKernelModuleFinalizerTest,
     ImageVerifierRejectsSymtabLinkedToSectionNameTable) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Input = makeBuiltinObject(*ELFRoute, "release_public_definition");
  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_NE(Input, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  addAndroidKernelProfileContract(*Input);

  auto AddSection = [&](StringRef Name, NevercObjectSectionFlags Flags,
                        uint64_t Alignment, size_t Size) {
    PluginObjectSection Section;
    Section.ID = Input->allocateEntityID();
    Section.Name = Name.str();
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    Section.Flags = Flags;
    Section.Alignment = Alignment;
    Section.Data.resize(Size);
    Input->sections().push_back(std::move(Section));
  };
  AddSection("__versions", NEVERC_OBJECT_SECTION_ALLOCATED, 8, 0);
  AddSection(".codetag.alloc_tags",
             NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE,
             8, 0);
  AddSection(".gnu.linkonce.this_module",
             NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE,
             64, 1024);
  Input->advanceGeneration();
  Input->issueLayoutProof();
  ASSERT_FALSE(verifyPluginObjectGraph(*Input));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  auto NativeInput =
      (*Writer)->beginWrite(Scope.task(), *Input,
                            ObjectOutputDestination::memory(
                                "symtab-link-input.o", UINT64_C(1) << 20));
  ASSERT_TRUE(static_cast<bool>(NativeInput))
      << errorText(NativeInput.takeError());
  auto NativeInputBytes = (*NativeInput)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(NativeInputBytes))
      << errorText(NativeInputBytes.takeError());
  std::vector<uint8_t> ImmutableInput(NativeInputBytes->begin(),
                                      NativeInputBytes->end());
  EXPECT_FALSE((*NativeInput)->abort());

  std::array<PluginObjectGraph *, 1> Inputs{Input.get()};
  std::array<ArrayRef<uint8_t>, 1> InputImages{ImmutableInput};
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Inputs, InputImages,
      NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  const uint64_t MergedGeneration = Merged->Object->generation();
  const SmallVector<char, 0> NativeImageBeforeFinalizer = Merged->MergedImage;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Merged->Object, Policy, "verified built-in Android module");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  EXPECT_EQ(Merged->Object->generation(), MergedGeneration);
  EXPECT_EQ(Merged->MergedImage, NativeImageBeforeFinalizer);

  constexpr StringLiteral PublicReleaseName = "fn_0";
  const auto HasMergedSymbol = [&](StringRef Name) {
    return std::any_of(
        Merged->Object->symbols().begin(), Merged->Object->symbols().end(),
        [&](const PluginObjectSymbol &Symbol) { return Symbol.Name == Name; });
  };
  EXPECT_TRUE(HasMergedSymbol(PublicReleaseName));
  EXPECT_FALSE(HasMergedSymbol("release_public_definition"));

  ArrayRef<uint8_t> ValidImage(
      reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
      Merged->MergedImage.size());
  EXPECT_FALSE(verifyFinalAndroidKernelModuleImage(
      ValidImage, Policy, "valid final Android module"));

  std::vector<uint8_t> ReadableNameBypass(ValidImage.begin(), ValidImage.end());
  auto ReleaseNamePosition =
      std::search(ReadableNameBypass.begin(), ReadableNameBypass.end(),
                  PublicReleaseName.begin(), PublicReleaseName.end());
  ASSERT_NE(ReleaseNamePosition, ReadableNameBypass.end());
  std::fill_n(ReleaseNamePosition, PublicReleaseName.size(),
              static_cast<uint8_t>('x'));
  Error NameBypass = verifyFinalAndroidKernelModuleImage(
      ReadableNameBypass, Policy,
      "final Android module with readable symbol bypass");
  ASSERT_TRUE(static_cast<bool>(NameBypass));
  EXPECT_NE(errorText(std::move(NameBypass)).find("release symbol"),
            std::string::npos);

  std::vector<uint8_t> Corrupted(ValidImage.begin(), ValidImage.end());
  StringRef CorruptedBytes(reinterpret_cast<const char *>(Corrupted.data()),
                           Corrupted.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(CorruptedBytes);
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  auto Sections = Parsed->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());
  std::optional<unsigned> SymtabIndex;
  std::optional<unsigned> ShstrtabIndex;
  for (unsigned I = 0; I < Sections->size(); ++I) {
    auto Name = Parsed->getSectionName((*Sections)[I]);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name == ".symtab")
      SymtabIndex = I;
    else if (*Name == ".shstrtab")
      ShstrtabIndex = I;
  }
  ASSERT_TRUE(SymtabIndex.has_value());
  ASSERT_TRUE(ShstrtabIndex.has_value());

  object::ELF64LE::Shdr CorruptedSymtab = (*Sections)[*SymtabIndex];
  CorruptedSymtab.sh_link = *ShstrtabIndex;
  const auto *OriginalSymtabBytes =
      reinterpret_cast<const uint8_t *>(&(*Sections)[*SymtabIndex]);
  ASSERT_GE(OriginalSymtabBytes, Corrupted.data());
  const size_t SymtabOffset = OriginalSymtabBytes - Corrupted.data();
  ASSERT_LE(SymtabOffset + sizeof(CorruptedSymtab), Corrupted.size());
  std::memcpy(Corrupted.data() + SymtabOffset, &CorruptedSymtab,
              sizeof(CorruptedSymtab));

  Error Verify = verifyFinalAndroidKernelModuleImage(
      Corrupted, Policy, "corrupted final Android module");
  ASSERT_TRUE(static_cast<bool>(Verify));
  const std::string VerifyMessage = errorText(std::move(Verify));
  EXPECT_NE(VerifyMessage.find("not a parseable ELF64LE object"),
            std::string::npos)
      << VerifyMessage;
}

TEST(AndroidKernelModuleFinalizerTest,
     OriginalProviderGraphSurvivesWriterReorderAndStandaloneAudit) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Input = makeBuiltinObject(*ELFRoute, "custom_release_entry");
  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_NE(Input, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectSection &Text = Input->sections().front();
  Text.Data.resize(32);
  const uint64_t TextID = Text.ID;

  auto AddSymbol = [&](StringRef Name, NevercObjectSymbolBinding Binding,
                       NevercObjectSymbolType Type,
                       NevercObjectSymbolDefinition Definition, uint64_t Value,
                       uint64_t Size) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Input->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = Binding;
    Symbol.Type = Type;
    Symbol.Definition = Definition;
    Symbol.SectionID =
        Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED ? TextID : 0;
    Symbol.Value = Value;
    Symbol.Size = Size;
    const uint64_t ID = Symbol.ID;
    Input->symbols().push_back(std::move(Symbol));
    return ID;
  };
  const uint64_t LocalA =
      AddSymbol("writer_local_b", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 8, 0);
  const uint64_t LocalB =
      AddSymbol("writer_local_a", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 8, 0);
  const uint64_t KCFI =
      AddSymbol("__kcfi_typeid_sample", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_OBJECT,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 16, 4);
  AddSymbol("init_module", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
            NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 0, 4);
  AddSymbol("cleanup_module", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
            NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 4, 4);
  AddSymbol("__cfi_check", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
            NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 12, 4);
  AddSymbol("__cfi_check_fail", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
            NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 20, 4);
  AddSymbol("__typeid__sample_global_addr", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
            NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
            NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE, 0x2a, 0);
  const uint64_t Import =
      AddSymbol("printk", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED, 0, 0);
  const uint64_t EquivalentImport =
      AddSymbol("printk", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED, 0, 0);

  auto AddRelocation = [&](uint64_t Offset, uint32_t Width,
                           uint64_t TargetSymbolID) {
    PluginObjectRelocation Relocation;
    Relocation.ID = Input->allocateEntityID();
    Relocation.SectionID = TextID;
    Relocation.Offset = Offset;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.Width = Width;
    Relocation.TargetSymbolID = TargetSymbolID;
    Relocation.Extension.Owner = ELFRoute->ObjectFormatID;
    Relocation.Extension.Version =
        neverc::plugin::builtinext::RelocationVersion;
    const uint64_t NativeType =
        Width == 64 ? ELF::R_AARCH64_ABS64 : ELF::R_AARCH64_ABS32;
    const StringRef NativeName =
        Width == 64 ? "R_AARCH64_ABS64" : "R_AARCH64_ABS32";
    SmallVector<uint8_t, 48> NativeFacts;
    neverc::plugin::builtinext::appendHeader(
        NativeFacts, neverc::plugin::builtinext::RelocationTag,
        neverc::plugin::builtinext::RelocationVersion);
    neverc::plugin::builtinext::appendU64(NativeFacts, NativeType);
    neverc::plugin::builtinext::appendU32(NativeFacts, NativeName.size());
    neverc::plugin::builtinext::appendBytes(NativeFacts, NativeName);
    Relocation.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Input->relocations().push_back(std::move(Relocation));
  };
  AddRelocation(0, 64, LocalA);
  AddRelocation(8, 64, LocalB);
  AddRelocation(16, 32, KCFI);
  AddRelocation(20, 32, Import);
  AddRelocation(24, 64, EquivalentImport);
  addAndroidKernelProfileContract(*Input);

  auto AddABISection = [&](StringRef Name, NevercObjectSectionFlags Flags,
                           uint64_t Alignment, size_t Size) {
    PluginObjectSection Section;
    Section.ID = Input->allocateEntityID();
    Section.Name = Name.str();
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    Section.Flags = Flags;
    Section.Alignment = Alignment;
    Section.Data.resize(Size);
    const uint64_t ID = Section.ID;
    Input->sections().push_back(std::move(Section));
    return ID;
  };
  AddABISection("__versions", NEVERC_OBJECT_SECTION_ALLOCATED, 8, 0);
  AddABISection(
      ".codetag.alloc_tags",
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE, 8, 0);
  AddABISection(".gnu.linkonce.this_module",
                NEVERC_OBJECT_SECTION_ALLOCATED |
                    NEVERC_OBJECT_SECTION_WRITABLE,
                64, 1024);
  const uint64_t DataID = AddABISection(
      ".data", NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE,
      8, 8);
  PluginObjectSection BSS;
  BSS.ID = Input->allocateEntityID();
  BSS.Name = ".bss";
  BSS.Kind = NEVERC_OBJECT_SECTION_KIND_ZERO_FILL;
  BSS.Flags = NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  BSS.Alignment = 16;
  BSS.ZeroFillSize = 16;
  const uint64_t BSSID = BSS.ID;
  Input->sections().push_back(std::move(BSS));
  AddABISection(".debug_info", NEVERC_OBJECT_SECTION_DEBUG, 1, 4);

  auto AddDataSymbol =
      [&](StringRef Name, uint64_t SectionID, NevercObjectSymbolBinding Binding,
          NevercObjectSymbolVisibility Visibility, uint64_t Size) {
        PluginObjectSymbol Symbol;
        Symbol.ID = Input->allocateEntityID();
        Symbol.Name = Name.str();
        Symbol.Binding = Binding;
        Symbol.Visibility = Visibility;
        Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
        Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
        Symbol.SectionID = SectionID;
        Symbol.Size = Size;
        Symbol.Alignment = Size;
        Input->symbols().push_back(std::move(Symbol));
      };
  AddDataSymbol("ordinary_hidden_weak", DataID,
                NEVERC_OBJECT_SYMBOL_BINDING_WEAK,
                NEVERC_OBJECT_SYMBOL_VISIBILITY_HIDDEN, 8);
  AddDataSymbol("ordinary_bss", BSSID, NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT, 16);
  Input->advanceGeneration();
  Input->issueLayoutProof();
  Error VerifyInput = verifyPluginObjectGraph(*Input);
  ASSERT_FALSE(VerifyInput) << errorText(std::move(VerifyInput));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto InputWriter = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(InputWriter))
      << errorText(InputWriter.takeError());
  auto InputImage =
      (*InputWriter)
          ->beginWrite(Scope.task(), *Input,
                       ObjectOutputDestination::memory("custom-release-input.o",
                                                       UINT64_C(1) << 20));
  ASSERT_TRUE(static_cast<bool>(InputImage))
      << errorText(InputImage.takeError());
  auto DefaultInputBytes = (*InputImage)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(DefaultInputBytes))
      << errorText(DefaultInputBytes.takeError());
  auto DefaultInputSemantics = readELFSemantics(*DefaultInputBytes);
  ASSERT_TRUE(static_cast<bool>(DefaultInputSemantics))
      << errorText(DefaultInputSemantics.takeError());
  // This is the historical LLVM MC path: valid ELF with one shared string
  // table. The explicit release policy must not affect ordinary writes.
  EXPECT_EQ(DefaultInputSemantics->StringTableCount, 1U);
  EXPECT_TRUE(DefaultInputSemantics->HasSymbolStringTable);
  EXPECT_FALSE(DefaultInputSemantics->HasSectionStringTable);
  EXPECT_FALSE((*InputImage)->abort());

  ObjectOutputDestination CanonicalInputDestination =
      ObjectOutputDestination::memory("custom-canonical-input.o", UINT64_C(1)
                                                                      << 20);
  CanonicalInputDestination.WritePolicy = ObjectWritePolicy::CanonicalELFTables;
  auto CanonicalInputImage =
      (*InputWriter)
          ->beginWrite(Scope.task(), *Input, CanonicalInputDestination);
  ASSERT_TRUE(static_cast<bool>(CanonicalInputImage))
      << errorText(CanonicalInputImage.takeError());
  auto CanonicalInputBytes = (*CanonicalInputImage)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(CanonicalInputBytes))
      << errorText(CanonicalInputBytes.takeError());
  auto CanonicalInputSemantics = readELFSemantics(*CanonicalInputBytes);
  ASSERT_TRUE(static_cast<bool>(CanonicalInputSemantics))
      << errorText(CanonicalInputSemantics.takeError());
  EXPECT_EQ(CanonicalInputSemantics->StringTableCount, 2U);
  EXPECT_TRUE(CanonicalInputSemantics->HasSymbolStringTable);
  EXPECT_TRUE(CanonicalInputSemantics->HasSectionStringTable);
  EXPECT_TRUE(CanonicalInputSemantics->SymtabLinksSymbolStringTable);
  EXPECT_TRUE(llvm::any_of(CanonicalInputSemantics->Symbols,
                           [](const ELFSymbolSemantics &Symbol) {
                             return isAArch64MappingSymbol(Symbol.Name);
                           }));
  EXPECT_TRUE(llvm::any_of(CanonicalInputSemantics->Symbols,
                           [](const ELFSymbolSemantics &Symbol) {
                             return Symbol.Name == "custom_release_entry";
                           }));
  EXPECT_NE(CanonicalInputSemantics->SectionNames.find(".debug_info"),
            CanonicalInputSemantics->SectionNames.end());
  EXPECT_FALSE((*CanonicalInputImage)->abort());

  ObjectOutputDestination DebugStrippedInputDestination =
      ObjectOutputDestination::memory("custom-canonical-debug-strip.o",
                                      UINT64_C(1) << 20);
  DebugStrippedInputDestination.WritePolicy =
      ObjectWritePolicy::CanonicalELFTables;
  DebugStrippedInputDestination.DropDebugInfo = true;
  auto DebugStrippedInputImage =
      (*InputWriter)
          ->beginWrite(Scope.task(), *Input, DebugStrippedInputDestination);
  ASSERT_TRUE(static_cast<bool>(DebugStrippedInputImage))
      << errorText(DebugStrippedInputImage.takeError());
  auto DebugStrippedInputBytes = (*DebugStrippedInputImage)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(DebugStrippedInputBytes))
      << errorText(DebugStrippedInputBytes.takeError());
  auto DebugStrippedInputSemantics = readELFSemantics(*DebugStrippedInputBytes);
  ASSERT_TRUE(static_cast<bool>(DebugStrippedInputSemantics))
      << errorText(DebugStrippedInputSemantics.takeError());
  EXPECT_EQ(DebugStrippedInputSemantics->SectionNames.find(".debug_info"),
            DebugStrippedInputSemantics->SectionNames.end());
  EXPECT_TRUE(llvm::any_of(DebugStrippedInputSemantics->Symbols,
                           [](const ELFSymbolSemantics &Symbol) {
                             return isAArch64MappingSymbol(Symbol.Name);
                           }));
  EXPECT_TRUE(llvm::any_of(DebugStrippedInputSemantics->Symbols,
                           [](const ELFSymbolSemantics &Symbol) {
                             return Symbol.Name == "custom_release_entry";
                           }));
  EXPECT_FALSE((*DebugStrippedInputImage)->abort());

  ObjectOutputDestination InvalidDebugPolicy = ObjectOutputDestination::memory(
      "invalid-debug-policy.o", UINT64_C(1) << 20);
  InvalidDebugPolicy.DropDebugInfo = true;
  auto InvalidDebugImage =
      (*InputWriter)->beginWrite(Scope.task(), *Input, InvalidDebugPolicy);
  ASSERT_FALSE(static_cast<bool>(InvalidDebugImage));
  EXPECT_NE(errorText(InvalidDebugImage.takeError())
                .find("explicit ELF object write policy"),
            std::string::npos);
  EXPECT_FALSE(findPluginMemoryOutput(Scope.task(), "invalid-debug-policy.o")
                   .has_value());

  std::array<PluginObjectGraph *, 1> Inputs{Input.get()};
  BuiltinObjectMergeConfig PartialConfig;
  PartialConfig.AndroidKernelModule = true;
  auto Partial = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Inputs,
      ArrayRef<ArrayRef<uint8_t>>{}, NEVERC_LINK_OPTION_NONE, PartialConfig);
  ASSERT_TRUE(static_cast<bool>(Partial)) << errorText(Partial.takeError());

  // A custom ObjectMergeProvider may return symbols in any list order. Keep
  // the graph deliberately unlike the ELF writer's local-first/value/name
  // ordering; exact structural ties own a name multiset, not one list slot.
  auto AddCustomLocal = [&](StringRef Name) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Partial->Object->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = Partial->Object->sections().front().ID;
    Symbol.Value = 8;
    const uint64_t ID = Symbol.ID;
    Partial->Object->symbols().push_back(std::move(Symbol));
    return ID;
  };
  const uint64_t CustomLocalA = AddCustomLocal("custom_local_b");
  const uint64_t CustomLocalB = AddCustomLocal("custom_local_a");
  auto Relocation = Partial->Object->relocations().begin();
  ASSERT_NE(Relocation, Partial->Object->relocations().end());
  Relocation->TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation->TargetSymbolID = CustomLocalA;
  Relocation->TargetSectionID = 0;
  ++Relocation;
  ASSERT_NE(Relocation, Partial->Object->relocations().end());
  Relocation->TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation->TargetSymbolID = CustomLocalB;
  Relocation->TargetSectionID = 0;
  Partial->Object->symbols().reverse();
  Partial->Object->advanceGeneration();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::Original;
  neverc::AndroidKernelReleaseSymbolMap ReleaseSymbolMap;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Partial->Object, Policy, "test custom-provider release graph",
      &ReleaseSymbolMap);
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  Error GraphAudit = verifyFinalAndroidKernelModuleObjectGraph(
      *Partial->Object, Policy,
      "test custom-provider release graph standalone audit");
  ASSERT_FALSE(GraphAudit) << errorText(std::move(GraphAudit));
  SCOPED_TRACE(dumpPluginObjectGraph(*Partial->Object));

  std::vector<std::string> LocalNames;
  for (const PluginObjectSymbol &Symbol : Partial->Object->symbols())
    if (Symbol.Value == 8 &&
        Symbol.Binding == NEVERC_OBJECT_SYMBOL_BINDING_LOCAL)
      LocalNames.push_back(Symbol.Name);
  llvm::sort(LocalNames);
  EXPECT_EQ(LocalNames, (std::vector<std::string>{"code_8", "code_8_1"}));
  const auto HasGraphName = [&](StringRef Name) {
    return llvm::any_of(
        Partial->Object->symbols(),
        [&](const PluginObjectSymbol &Symbol) { return Symbol.Name == Name; });
  };
  EXPECT_TRUE(HasGraphName("__kcfi_typeid_sample"));
  EXPECT_TRUE(HasGraphName("__typeid__sample_global_addr"));
  EXPECT_TRUE(HasGraphName("init_module"));
  EXPECT_TRUE(HasGraphName("cleanup_module"));
  EXPECT_TRUE(HasGraphName("__cfi_check"));
  EXPECT_TRUE(HasGraphName("__cfi_check_fail"));
  EXPECT_TRUE(HasGraphName("printk"));
  EXPECT_FALSE(HasGraphName("custom_release_entry"));
  EXPECT_FALSE(HasGraphName("writer_local_a"));
  EXPECT_FALSE(HasGraphName("writer_local_b"));

  Partial->Object->issueLayoutProof();
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());

  ObjectOutputDestination SerializedReferenceDestination =
      ObjectOutputDestination::memory("custom-release-reference.ko", UINT64_C(1)
                                                                         << 20);
  SerializedReferenceDestination.WritePolicy =
      ObjectWritePolicy::CanonicalELFTables;
  SerializedReferenceDestination.DropDebugInfo = true;
  auto SerializedReferenceImage = (*Writer)->beginWrite(
      Scope.task(), *Partial->Object, SerializedReferenceDestination);
  ASSERT_TRUE(static_cast<bool>(SerializedReferenceImage))
      << errorText(SerializedReferenceImage.takeError());
  auto SerializedReferenceBytes = (*SerializedReferenceImage)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(SerializedReferenceBytes))
      << errorText(SerializedReferenceBytes.takeError());
  auto SerializedReferenceSemantics =
      readELFSemantics(*SerializedReferenceBytes);
  ASSERT_TRUE(static_cast<bool>(SerializedReferenceSemantics))
      << errorText(SerializedReferenceSemantics.takeError());
  EXPECT_TRUE(llvm::any_of(SerializedReferenceSemantics->Symbols,
                           [](const ELFSymbolSemantics &Symbol) {
                             return isAArch64MappingSymbol(Symbol.Name);
                           }));

  ObjectOutputDestination ReleaseDestination =
      ObjectOutputDestination::memory("custom-release.ko", UINT64_C(1) << 20);
  ReleaseDestination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  ReleaseDestination.DropDebugInfo = true;
  auto Image =
      (*Writer)->beginWrite(Scope.task(), *Partial->Object, ReleaseDestination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Pending = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Pending)) << errorText(Pending.takeError());
  std::vector<uint8_t> Serialized(Pending->begin(), Pending->end());
  auto SerializedSemantics = readELFSemantics(Serialized);
  ASSERT_TRUE(static_cast<bool>(SerializedSemantics))
      << errorText(SerializedSemantics.takeError());
  Error BoundMap = bindAndroidKernelReleaseSymbolMapToImage(
      ReleaseSymbolMap, Serialized,
      "test custom-provider final release symbol map");
  ASSERT_FALSE(BoundMap) << errorText(std::move(BoundMap));
  for (const neverc::AndroidKernelReleaseSymbolMapEntry &Entry :
       ReleaseSymbolMap.Symbols)
    EXPECT_TRUE(llvm::any_of(
        SerializedSemantics->Symbols, [&](const ELFSymbolSemantics &Symbol) {
          return Symbol.Name == Entry.ReleaseName;
        }))
        << Entry.OriginalName << " -> " << Entry.ReleaseName;
  EXPECT_TRUE(llvm::none_of(
      ReleaseSymbolMap.Symbols,
      [](const neverc::AndroidKernelReleaseSymbolMapEntry &Entry) {
        return Entry.OriginalName == "custom_local_a" ||
               Entry.OriginalName == "custom_local_b";
      }));
  EXPECT_TRUE(llvm::any_of(
      ReleaseSymbolMap.Symbols,
      [](const neverc::AndroidKernelReleaseSymbolMapEntry &Entry) {
        return Entry.OriginalName == "ordinary_hidden_weak";
      }));

  EXPECT_EQ(SerializedReferenceSemantics->Machine,
            SerializedSemantics->Machine);
  EXPECT_EQ(SerializedReferenceSemantics->Flags, SerializedSemantics->Flags);
  EXPECT_EQ(SerializedReferenceSemantics->OSABI, SerializedSemantics->OSABI);
  EXPECT_EQ(SerializedReferenceSemantics->ABIVersion,
            SerializedSemantics->ABIVersion);
  EXPECT_EQ(SerializedReferenceSemantics->OrdinarySections,
            SerializedSemantics->OrdinarySections);
  EXPECT_EQ(SerializedReferenceSemantics->Relocations,
            SerializedSemantics->Relocations);

  // The MC writer may lower a symbol-target relocation to the equivalent
  // section-symbol-plus-addend form.  The exact relocation comparison above
  // proves that lowering preserved meaning; derive the release keep-set from
  // those serialized targets, where the native finalizer actually runs.
  std::set<std::string> SerializedRelocationTargets;
  for (const ELFRelocationSemantics &Relocation :
       SerializedReferenceSemantics->Relocations)
    SerializedRelocationTargets.insert(Relocation.Target);
  std::vector<ELFSymbolSemantics> ExpectedReleaseSymbols;
  for (const ELFSymbolSemantics &Symbol :
       SerializedReferenceSemantics->Symbols) {
    const bool RelocationRequired =
        SerializedRelocationTargets.find(Symbol.Name) !=
        SerializedRelocationTargets.end();
    const bool DefinedNonLocal =
        Symbol.Binding != ELF::STB_LOCAL && Symbol.Section != "<undefined>";
    if (RelocationRequired || DefinedNonLocal) {
      ExpectedReleaseSymbols.push_back(Symbol);
      continue;
    }
    EXPECT_TRUE(isAArch64MappingSymbol(Symbol.Name) ||
                Symbol.Binding == ELF::STB_LOCAL ||
                Symbol.Section == "<undefined>")
        << Symbol.Name;
  }
  EXPECT_EQ(ExpectedReleaseSymbols, SerializedSemantics->Symbols);
  for (StringRef ExactName :
       {"init_module", "cleanup_module", "__cfi_check", "__cfi_check_fail",
        "__kcfi_typeid_sample", "__typeid__sample_global_addr", "printk",
        "__start_alloc_tags", "__stop_alloc_tags"})
    EXPECT_TRUE(llvm::any_of(SerializedSemantics->Symbols,
                             [&](const ELFSymbolSemantics &Symbol) {
                               return Symbol.Name == ExactName;
                             }))
        << ExactName.str();
  EXPECT_EQ(llvm::count_if(SerializedSemantics->Symbols,
                           [](const ELFSymbolSemantics &Symbol) {
                             return Symbol.Name == "printk";
                           }),
            1U);
  EXPECT_EQ(llvm::count_if(SerializedSemantics->Relocations,
                           [](const ELFRelocationSemantics &Relocation) {
                             return Relocation.Target == "printk";
                           }),
            2U);
  EXPECT_TRUE(llvm::any_of(
      SerializedSemantics->Symbols, [](const ELFSymbolSemantics &Symbol) {
        return Symbol.Type == ELF::STT_OBJECT &&
               Symbol.Binding == ELF::STB_WEAK &&
               Symbol.Visibility == ELF::STV_HIDDEN && Symbol.Size == 8;
      }));
  EXPECT_TRUE(llvm::any_of(
      SerializedSemantics->Symbols, [](const ELFSymbolSemantics &Symbol) {
        return Symbol.Type == ELF::STT_OBJECT &&
               Symbol.Binding == ELF::STB_GLOBAL &&
               Symbol.Visibility == ELF::STV_DEFAULT && Symbol.Size == 16 &&
               Symbol.Section == ".bss";
      }));
  EXPECT_FALSE(llvm::any_of(SerializedSemantics->Symbols,
                            [](const ELFSymbolSemantics &Symbol) {
                              return isAArch64MappingSymbol(Symbol.Name);
                            }));
  EXPECT_FALSE(containsBytes(Serialized, "$d."));
  EXPECT_FALSE(containsBytes(Serialized, "$x."));
  for (StringRef Stale :
       {"custom_release_entry", "custom_local_a", "custom_local_b",
        "writer_local_a", "writer_local_b", "code_8", "code_8_1",
        "ordinary_hidden_weak", "ordinary_bss"})
    EXPECT_FALSE(containsBytes(Serialized, Stale)) << Stale.str();

  Error SerializedAudit = verifyFinalAndroidKernelModuleImage(
      Serialized, Policy, "custom-provider serialized release image");
  ASSERT_FALSE(SerializedAudit) << errorText(std::move(SerializedAudit));

  ObjectOutputDestination RepeatDestination = ObjectOutputDestination::memory(
      "custom-release-repeat.ko", UINT64_C(1) << 20);
  RepeatDestination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  RepeatDestination.DropDebugInfo = true;
  auto RepeatImage =
      (*Writer)->beginWrite(Scope.task(), *Partial->Object, RepeatDestination);
  ASSERT_TRUE(static_cast<bool>(RepeatImage))
      << errorText(RepeatImage.takeError());
  auto RepeatBytes = (*RepeatImage)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(RepeatBytes))
      << errorText(RepeatBytes.takeError());
  EXPECT_EQ(Serialized,
            (std::vector<uint8_t>(RepeatBytes->begin(), RepeatBytes->end())));
  EXPECT_FALSE((*RepeatImage)->abort());
  EXPECT_FALSE((*SerializedReferenceImage)->abort());
  EXPECT_FALSE((*Image)->finish());
  EXPECT_FALSE((*Image)->verify());
  EXPECT_FALSE((*Image)->abort());
}

TEST(AndroidKernelModuleFinalizerTest,
     DirectReleaseWriterRejectsUnsupportedArchitectureBeforeOpeningSink) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *X86ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::x86_64) {
      X86ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(X86ELFRoute, nullptr);

  auto Graph = makeBuiltinObject(*X86ELFRoute, "ordinary_name");
  ASSERT_NE(Graph, nullptr);
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());

  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "failed-serialized-release.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  auto Image = (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_FALSE(static_cast<bool>(Image));
  EXPECT_NE(errorText(Image.takeError()).find("requires AArch64 ELF"),
            std::string::npos);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), "failed-serialized-release.ko")
          .has_value());
}

TEST(PluginObjectGraphImportTest,
     PreservesNormalizedEntitiesExtensionsAndOrigins) {
  auto SourceTarget = makeTargetKey();
  auto LinkTarget = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(SourceTarget))
      << errorText(SourceTarget.takeError());
  ASSERT_TRUE(static_cast<bool>(LinkTarget))
      << errorText(LinkTarget.takeError());
  PluginObjectGraph Source(std::move(*SourceTarget));

  PluginObjectComdat Comdat;
  Comdat.ID = Source.allocateEntityID();
  Comdat.Name = "answer";
  Comdat.Selection = NEVERC_OBJECT_COMDAT_EXACT_MATCH;
  Comdat.Extension.Owner = TestFormatID;
  Comdat.Extension.Version = 3;
  Comdat.Extension.Bytes = {0xaa, 0xbb};
  const uint64_t ObjectComdatID = Comdat.ID;
  Source.comdats().push_back(std::move(Comdat));

  PluginObjectSection Text;
  Text.ID = Source.allocateEntityID();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags = NEVERC_OBJECT_SECTION_ALLOCATED |
               NEVERC_OBJECT_SECTION_EXECUTABLE | NEVERC_OBJECT_SECTION_RETAIN;
  Text.Alignment = 16;
  Text.Data = {0, 0, 0, 0, 0, 0, 0, 0};
  Text.ComdatID = ObjectComdatID;
  Text.Extension.Owner = TestFormatID;
  Text.Extension.Version = 7;
  Text.Extension.Bytes = {1, 2, 3};
  const uint64_t ObjectSectionID = Text.ID;
  Source.sections().push_back(std::move(Text));

  PluginObjectSymbol Defined;
  Defined.ID = Source.allocateEntityID();
  Defined.Name = "answer";
  Defined.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Defined.Visibility = NEVERC_OBJECT_SYMBOL_VISIBILITY_PROTECTED;
  Defined.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Defined.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Defined.SectionID = ObjectSectionID;
  Defined.Value = 0;
  Defined.Size = 8;
  Defined.Alignment = 1;
  Defined.ComdatID = ObjectComdatID;
  Defined.Flags = NEVERC_OBJECT_SYMBOL_EXPORTED;
  const uint64_t DefinedID = Defined.ID;
  Source.symbols().push_back(std::move(Defined));

  PluginObjectSymbol Undefined;
  Undefined.ID = Source.allocateEntityID();
  Undefined.Name = "external";
  Undefined.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Undefined.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
  Undefined.Alignment = 1;
  Undefined.Flags = NEVERC_OBJECT_SYMBOL_IMPORTED;
  const uint64_t UndefinedID = Undefined.ID;
  Source.symbols().push_back(std::move(Undefined));

  PluginObjectRelocation Relocation;
  Relocation.ID = Source.allocateEntityID();
  Relocation.SectionID = ObjectSectionID;
  Relocation.Offset = 0;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_PC_RELATIVE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 32;
  Relocation.IsPCRelative = true;
  Relocation.IsSigned = true;
  Relocation.Addend = -4;
  Relocation.TargetSymbolID = UndefinedID;
  const uint64_t RelocationID = Relocation.ID;
  Source.relocations().push_back(std::move(Relocation));

  ASSERT_FALSE(verifyPluginObjectGraph(Source));

  PluginLinkGraph Link(std::move(*LinkTarget));
  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///answer.o";
  const uint64_t InputID = Link.addInput(std::move(Input)).ID;
  ObjectGraphImportOptions Options;
  Options.InputID = InputID;
  Options.ObjectGraph = {UINT64_C(7), UINT64_C(11)};
  auto Imported = importObjectGraph(Link, Source, Options);
  ASSERT_TRUE(static_cast<bool>(Imported)) << errorText(Imported.takeError());
  ASSERT_FALSE(verifyPluginLinkGraph(Link));

  const PluginLinkSection *Section =
      Link.findSection(Imported->Sections.at(ObjectSectionID));
  ASSERT_NE(Section, nullptr);
  EXPECT_EQ(Section->Kind, NEVERC_OBJECT_SECTION_KIND_TEXT);
  EXPECT_EQ(Section->Flags, NEVERC_OBJECT_SECTION_ALLOCATED |
                                NEVERC_OBJECT_SECTION_EXECUTABLE |
                                NEVERC_OBJECT_SECTION_RETAIN);
  EXPECT_EQ(Section->ComdatID, Imported->Comdats.at(ObjectComdatID));
  ASSERT_EQ(Section->Extensions.values().size(), 1u);
  EXPECT_EQ(Section->Extensions.values()[0].Payload,
            (std::vector<uint8_t>{1, 2, 3}));

  const PluginLinkAtom *Atom =
      Link.findAtom(Imported->Atoms.at(ObjectSectionID));
  ASSERT_NE(Atom, nullptr);
  EXPECT_EQ(Atom->Content.size(), 8u);
  EXPECT_NE(Atom->Flags & NEVERC_LINK_ATOM_ROOT, 0u);
  EXPECT_EQ(Atom->Origin.InputID, InputID);
  EXPECT_EQ(Atom->Origin.ObjectEntityID, ObjectSectionID);
  EXPECT_EQ(Atom->Origin.ObjectGraph.Owner, UINT64_C(7));

  const PluginLinkSymbol *DefinedLink =
      Link.findSymbol(Imported->Symbols.at(DefinedID));
  ASSERT_NE(DefinedLink, nullptr);
  EXPECT_EQ(DefinedLink->AtomID, Atom->ID);
  EXPECT_EQ(DefinedLink->Type, NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION);
  EXPECT_TRUE(DefinedLink->IsExported);
  const PluginLinkSymbol *UndefinedLink =
      Link.findSymbol(Imported->Symbols.at(UndefinedID));
  ASSERT_NE(UndefinedLink, nullptr);
  EXPECT_TRUE(UndefinedLink->IsImported);
  ASSERT_EQ(Link.imports().size(), 1u);
  ASSERT_EQ(Link.exports().size(), 1u);

  const PluginLinkEdge *Edge =
      Link.findEdge(Imported->Relocations.at(RelocationID));
  ASSERT_NE(Edge, nullptr);
  EXPECT_EQ(Edge->SourceAtomID, Atom->ID);
  EXPECT_EQ(Edge->TargetSymbolID, UndefinedLink->ID);
  EXPECT_EQ(Edge->RelocationKind, NEVERC_OBJECT_RELOCATION_PC_RELATIVE);
  EXPECT_TRUE(Edge->IsPCRelative);
  EXPECT_TRUE(Edge->IsSigned);
  EXPECT_EQ(Edge->Addend, -4);
}

TEST(PluginObjectGraphImportTest, RejectsForeignTargetWithoutMutation) {
  auto SourceTarget = makeTargetKey();
  auto OtherTarget = makeTargetKey({UINT64_C(0xdead), UINT64_C(0xbeef)});
  ASSERT_TRUE(static_cast<bool>(SourceTarget))
      << errorText(SourceTarget.takeError());
  ASSERT_TRUE(static_cast<bool>(OtherTarget))
      << errorText(OtherTarget.takeError());
  PluginObjectGraph Source(std::move(*SourceTarget));
  PluginLinkGraph Link(std::move(*OtherTarget));

  auto Imported = importObjectGraph(Link, Source);
  ASSERT_FALSE(Imported);
  EXPECT_NE(errorText(Imported.takeError()).find("does not match"),
            std::string::npos);
  EXPECT_TRUE(Link.sections().empty());
  EXPECT_TRUE(Link.symbols().empty());
}

TEST(PluginObjectMergeProviderTest,
     ExposesEveryInputAndCommitsHostOwnedOutput) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto First = makeObject(1);
  auto Second = makeObject(2);
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  MergeCallbackState State;
  PluginLinkSnapshot::ObjectMergeProviderRecord Provider;
  Provider.PluginID = "org.neverc.builtin.test";
  Provider.ProviderID = "merge";
  Provider.TargetID = TestTargetID;
  Provider.FormatID = TestFormatID;
  Provider.ProductID = TestProductID;
  Provider.Merge = mergeObjects;
  Provider.UserData = &State;
  Provider.Builtin = true;
  std::array<PluginObjectGraph *, 2> Inputs{First.get(), Second.get()};

  auto Merged = executeObjectMergeProvider(Scope.task(), Provider,
                                           std::move(*Target), Inputs);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  ASSERT_NE(Merged->Object, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*Merged->Object));
  ASSERT_EQ(Merged->Object->sections().size(), 1u);
  EXPECT_EQ(Merged->Object->sections().front().Name, ".merged");
  EXPECT_EQ(Merged->Object->sections().front().Data, (std::vector<uint8_t>{3}));
  EXPECT_EQ(Merged->ProducerRouteDigest[0], 0x42);
  EXPECT_EQ(State.Calls, 1u);
}

TEST(PluginObjectMergeProviderTest, RejectsForeignOutputHandle) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Input = makeObject(1);
  auto Target = makeTargetKey();
  ASSERT_NE(Input, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  MergeCallbackState State;
  State.ReturnForeignObject = true;
  PluginLinkSnapshot::ObjectMergeProviderRecord Provider;
  Provider.PluginID = "org.neverc.builtin.test";
  Provider.ProviderID = "foreign-output";
  Provider.TargetID = TestTargetID;
  Provider.FormatID = TestFormatID;
  Provider.ProductID = TestProductID;
  Provider.Merge = mergeObjects;
  Provider.UserData = &State;
  Provider.Builtin = true;
  PluginObjectGraph *InputPointer = Input.get();

  auto Merged = executeObjectMergeProvider(
      Scope.task(), Provider, std::move(*Target),
      ArrayRef<PluginObjectGraph *>(&InputPointer, 1));
  ASSERT_FALSE(Merged);
  EXPECT_NE(errorText(Merged.takeError()).find("foreign output"),
            std::string::npos);
  EXPECT_EQ(State.Calls, 1u);
}

TEST(PluginObjectMergeProviderTest,
     DispatchesRegisteredPluginThroughPlannedRoute) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_MERGE_PLUGIN));
  auto First = makeObject(2);
  auto Second = makeObject(3);
  auto Target = makeTargetKey();
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  auto Snapshot = PluginLinkRegistry::freeze(Scope.session().plugins());
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  ASSERT_EQ((*Snapshot)->objectMergeProviders().size(), 2u);

  LinkRouteRequest Request;
  Request.TargetID = TestTargetID;
  Request.InputFormat = TestFormatID;
  Request.OutputFormat = TestFormatID;
  Request.OutputKind = NEVERC_LINK_OUTPUT_RELOCATABLE;
  auto Route =
      LinkRoutePlanner::plan((*Snapshot)->linkerProviders(),
                             (*Snapshot)->objectMergeProviders(), Request);
  ASSERT_TRUE(static_cast<bool>(Route)) << errorText(Route.takeError());
  ASSERT_EQ(Route->kind(), PlannedLinkRoute::Kind::ObjectMerge);
  ASSERT_NE(Route->objectMergeProvider(), nullptr);

  std::array<PluginObjectGraph *, 2> Inputs{First.get(), Second.get()};
  auto Merged = executeObjectMergeProvider(
      Scope.task(), *Route->objectMergeProvider(), std::move(*Target), Inputs);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  ASSERT_EQ(Merged->Object->sections().size(), 1u);
  EXPECT_EQ(Merged->Object->sections().front().Name, ".plugin-merged");
  EXPECT_EQ(Merged->Object->sections().front().Data, (std::vector<uint8_t>{5}));
  EXPECT_EQ(Merged->ProducerRouteDigest[0], 0x63);
  EXPECT_EQ(Merged->PluginID, "org.neverc.test.object-merge");
}

TEST(PluginObjectMergeProviderTest,
     NativeAndroidReleaseAuditValidatesEveryVersionsContribution) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64 && Parsed.isAndroid()) {
      AndroidRoute = &Route;
      break;
    }
  }
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl release_input_entry
    .type release_input_entry, %function
release_input_entry:
    nop
    .size release_input_entry, .-release_input_entry

    .section __versions,"a",%progbits,unique,1
    .balign 8
    .space 64
    .section __versions,"a",%progbits,unique,2
    .balign 16
    .space 128

    .section .native_extra,"",%progbits
    .byte 0
)";
  auto Original = assembleBuiltinObject(*AndroidRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Original)) << errorText(Original.takeError());
  auto Graph = makeBuiltinObject(*AndroidRoute, "release_graph_entry");
  ASSERT_NE(Graph, nullptr);
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  std::array<PluginObjectGraph *, 1> Objects{Graph.get()};

  const auto Verify = [&](ArrayRef<uint8_t> Image) {
    std::array<ArrayRef<uint8_t>, 1> Images{Image};
    return verifyAndroidKernelReleaseObjectMergeInputs(
        Objects, Images, Target->view(), "test native Android input audit");
  };
  auto Valid = Verify(*Original);
  ASSERT_TRUE(static_cast<bool>(Valid)) << errorText(Valid.takeError());
  EXPECT_EQ(Valid->abi().Machine, ELF::EM_AARCH64);
  EXPECT_FALSE(Valid->hasRetainedAnonymousSymbols());

  const auto ExpectSectionTamperRejected = [&](StringRef Name,
                                               unsigned Occurrence,
                                               auto Mutator,
                                               StringRef ExpectedReason) {
    std::vector<uint8_t> Bytes = *Original;
    Error Patch = patchELF64SectionHeader(Bytes, Name, Occurrence, Mutator);
    ASSERT_FALSE(Patch) << errorText(std::move(Patch));
    auto Result = Verify(Bytes);
    ASSERT_FALSE(Result);
    const std::string Message = errorText(Result.takeError());
    EXPECT_NE(Message.find(ExpectedReason.str()), std::string::npos) << Message;
  };

  for (unsigned Occurrence : {0U, 1U}) {
    ExpectSectionTamperRejected(
        "__versions", Occurrence,
        [](object::ELF64LE::Shdr &Section) { Section.sh_type = ELF::SHT_NOTE; },
        "allocated, uncompressed SHT_PROGBITS");
    ExpectSectionTamperRejected(
        "__versions", Occurrence,
        [](object::ELF64LE::Shdr &Section) {
          Section.sh_flags &= ~ELF::SHF_ALLOC;
        },
        "allocated, uncompressed SHT_PROGBITS");
    ExpectSectionTamperRejected(
        "__versions", Occurrence,
        [](object::ELF64LE::Shdr &Section) {
          Section.sh_flags |= ELF::SHF_COMPRESSED;
        },
        "allocated, uncompressed SHT_PROGBITS");
    ExpectSectionTamperRejected(
        "__versions", Occurrence,
        [](object::ELF64LE::Shdr &Section) { Section.sh_addralign = 4; },
        "power of two >= 8");
    ExpectSectionTamperRejected(
        "__versions", Occurrence,
        [](object::ELF64LE::Shdr &Section) { Section.sh_size = 65; },
        "multiple of 64 bytes");
  }

  ExpectSectionTamperRejected(
      ".native_extra", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_type = ELF::SHT_STRTAB; },
      "additional SHT_STRTAB");
  ExpectSectionTamperRejected(
      ".native_extra", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_type = ELF::SHT_DYNSYM; },
      "SHT_DYNSYM");
  ExpectSectionTamperRejected(
      ".native_extra", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_type = ELF::SHT_REL; },
      "SHT_REL");
  ExpectSectionTamperRejected(
      ".text", 0, [](object::ELF64LE::Shdr &Section) { Section.sh_addr = 1; },
      "nonzero sh_addr");
  ExpectSectionTamperRejected(
      ".native_extra", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_addralign = 3; },
      "non-power-of-two alignment");
}

TEST(PluginObjectMergeProviderTest,
     DirectBuiltinAndroidFinalizerAuditsInputAcrossStripModes) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  Error Patch = patchELF64SectionHeader(
      *Input, "__versions", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_addralign = 4; });
  ASSERT_FALSE(Patch) << errorText(std::move(Patch));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto ReadTarget = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(ReadTarget))
      << errorText(ReadTarget.takeError());
  auto Graph = (*Reader)->read(Scope.task(), *Input,
                               "memory://invalid-final-native-input.o",
                               *ReadTarget, AndroidRoute->ObjectFormatID);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

  std::array<PluginObjectGraph *, 1> Objects{Graph->get()};
  std::array<ArrayRef<uint8_t>, 1> Images{*Input};
  struct StripCase {
    const char *Name;
    bool DropDebugInfo;
    bool StripUnneededSymbols;
  };
  constexpr std::array<StripCase, 3> Cases{{
      {"none", false, false},
      {"debug-info", true, false},
      {"all", true, true},
  }};
  for (const StripCase &Case : Cases) {
    SCOPED_TRACE(Case.Name);
    auto MergeTarget = makeBuiltinTargetKey(*AndroidRoute);
    ASSERT_TRUE(static_cast<bool>(MergeTarget))
        << errorText(MergeTarget.takeError());
    BuiltinObjectMergeConfig Config;
    Config.AndroidKernelModule = true;
    Config.FinalizeAndroidKernelModule = true;
    Config.DropDebugInfo = Case.DropDebugInfo;
    Config.StripUnneededSymbols = Case.StripUnneededSymbols;
    auto Merged = executeBuiltinObjectMergeAdapter(
        Scope.task(), *Snapshot, std::move(*MergeTarget), Objects, Images,
        NEVERC_LINK_OPTION_NONE, Config);
    ASSERT_FALSE(Merged);
    const std::string Message = errorText(Merged.takeError());
    EXPECT_NE(Message.find("power of two >= 8"), std::string::npos) << Message;
  }
}

TEST(
    PluginObjectMergeProviderTest,
    NativeOnlyAndroidFinalizerRejectsUntrustedHooksBeforeSinkAcrossStripModes) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  struct HookCase {
    const char *Name;
    const char *PluginPath;
    const char *ExpectedReason;
  };
  const std::array<HookCase, 2> Hooks{{
      {"third-party-provider", NEVERC_TEST_OBJECT_MERGE_PLUGIN,
       "third-party ObjectMergeProvider cannot preserve"},
      {"replaceable-output-phase", NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN,
       "incompatible with registered ObjectGraph/output phase bindings"},
  }};
  struct StripCase {
    const char *Name;
    linker::StripMode Mode;
  };
  constexpr std::array<StripCase, 3> Modes{{
      {"none", linker::StripMode::None},
      {"debug-info", linker::StripMode::DebugInfo},
      {"all", linker::StripMode::All},
  }};

  for (const HookCase &Hook : Hooks) {
    for (const StripCase &Strip : Modes) {
      SCOPED_TRACE(Hook.Name);
      SCOPED_TRACE(Strip.Name);
      LinkTaskScope Scope;
      ASSERT_TRUE(Scope.initialize(Hook.PluginPath));

      auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
      ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
      Error Patch = patchELF64Header(*Input, [](object::ELF64LE::Ehdr &Header) {
        Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
      });
      ASSERT_FALSE(Patch) << errorText(std::move(Patch));

      SmallString<128> Directory;
      ASSERT_FALSE(sys::fs::createUniqueDirectory(
          "neverc-untrusted-finalize-native-input", Directory));
      auto RemoveDirectory = make_scope_exit(
          [&] { (void)sys::fs::remove_directories(Directory); });
      SmallString<160> OutputPath(Directory);
      sys::path::append(OutputPath, "must-not-open.ko");

      linker::LinkExecutionRequest Request;
      Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
      Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
      Request.OutputURI = OutputPath.str().str();
      linker::LinkExecutionInput LinkInput;
      LinkInput.Kind = linker::LinkExecutionInputKind::Object;
      LinkInput.LogicalURI = "memory://native-only-final-input.o";
      LinkInput.AuthorizedBlob = std::move(*Input);
      Request.Inputs.push_back(std::move(LinkInput));

      linker::LinkerDriverConfig Config;
      Config.pluginTask = &Scope.task();
      Config.relocatable = true;
      Config.androidKernelModule = true;
      Config.finalizeAndroidKernelModule = true;
      Config.stripMode = Strip.Mode;

      neverc::OutputCoordinator Outputs;
      auto SessionAlias = std::shared_ptr<PluginSession>(
          &Scope.session(), [](PluginSession *) {});
      LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
      raw_null_ostream NullOutput;
      auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
      ASSERT_FALSE(Result);
      const std::string Message = errorText(Result.takeError());
      EXPECT_NE(Message.find(Hook.ExpectedReason), std::string::npos)
          << Message;
      EXPECT_FALSE(sys::fs::exists(OutputPath));
    }
  }
}

TEST(PluginObjectMergeProviderTest,
     OrdinaryAndroidReleaseAllowsAuthorizedPostWriteMutation) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  const std::vector<uint8_t> InputBytes = std::move(*Input);

  const auto Run =
      [&](StringRef PluginPath,
          StringRef OutputStem) -> std::optional<std::vector<uint8_t>> {
    LinkTaskScope Scope;
    if (!Scope.initialize(PluginPath))
      return std::nullopt;

    SmallString<128> Directory;
    if (std::error_code EC =
            sys::fs::createUniqueDirectory(OutputStem, Directory)) {
      ADD_FAILURE() << EC.message();
      return std::nullopt;
    }
    auto RemoveDirectory =
        make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
    SmallString<160> OutputPath(Directory);
    sys::path::append(OutputPath, "post-write-release.ko");

    linker::LinkExecutionRequest Request;
    Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
    Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
    Request.OutputURI = OutputPath.str().str();
    linker::LinkExecutionInput LinkInput;
    LinkInput.Kind = linker::LinkExecutionInputKind::Object;
    LinkInput.LogicalURI = "memory://ordinary-post-write-release-input.o";
    LinkInput.AuthorizedBlob = InputBytes;
    Request.Inputs.push_back(std::move(LinkInput));

    linker::LinkerDriverConfig Config;
    Config.pluginTask = &Scope.task();
    Config.relocatable = true;
    Config.androidKernelModule = true;
    Config.finalizeAndroidKernelModule = true;
    Config.stripMode = linker::StripMode::All;

    neverc::OutputCoordinator Outputs;
    auto SessionAlias = std::shared_ptr<PluginSession>(&Scope.session(),
                                                       [](PluginSession *) {});
    LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
    raw_null_ostream NullOutput;
    auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
    if (!Result) {
      ADD_FAILURE() << errorText(Result.takeError());
      return std::nullopt;
    }
    if (Result->Disposition != linker::LinkHookDisposition::Completed) {
      ADD_FAILURE() << "ordinary Android release link did not complete";
      Bridge.complete(false);
      return std::nullopt;
    }
    Bridge.complete(true);

    auto Output = MemoryBuffer::getFile(OutputPath);
    if (!Output) {
      ADD_FAILURE() << Output.getError().message();
      return std::nullopt;
    }
    StringRef Bytes = (*Output)->getBuffer();
    return std::vector<uint8_t>(Bytes.bytes_begin(), Bytes.bytes_end());
  };

  auto Baseline = Run({}, "neverc-ordinary-release-baseline");
  ASSERT_TRUE(Baseline.has_value());
  auto Mutated = Run(NEVERC_TEST_OBJECT_TEXT_PAYLOAD_POST_WRITE_PLUGIN,
                     "neverc-ordinary-release-post-write");
  ASSERT_TRUE(Mutated.has_value());
  ASSERT_EQ(Mutated->size(), Baseline->size());

  auto TextRange = findELF64SectionFileRange(*Baseline, ".text");
  ASSERT_TRUE(static_cast<bool>(TextRange)) << errorText(TextRange.takeError());
  const uint64_t TextOffset = TextRange->first;
  const uint64_t TextSize = TextRange->second;

  size_t Differences = 0;
  size_t FirstDifference = 0;
  for (size_t Index = 0; Index != Baseline->size(); ++Index) {
    if ((*Baseline)[Index] == (*Mutated)[Index])
      continue;
    if (Differences == 0)
      FirstDifference = Index;
    ++Differences;
  }
  EXPECT_EQ(Differences, 1U);
  EXPECT_GE(FirstDifference, TextOffset);
  EXPECT_LT(FirstDifference, TextOffset + TextSize);
  EXPECT_EQ((*Mutated)[FirstDifference],
            static_cast<uint8_t>((*Baseline)[FirstDifference] ^ UINT8_C(1)));
}

TEST(PluginObjectMergeProviderTest,
     OrdinaryAndroidReleaseRejectsAuthorizedExactNameReplacement) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_EXACT_NAME_CORRUPT_PLUGIN));
  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  AndroidObjectLinkOutcome Outcome =
      runAndroidObjectLink(Scope, *AndroidRoute, std::move(*Input),
                           "neverc-ordinary-release-exact-name-corrupt", true);
  EXPECT_FALSE(Outcome.Completed);
  EXPECT_FALSE(Outcome.Published);
  EXPECT_NE(Outcome.Error.find("immutable release identity seal"),
            std::string::npos)
      << Outcome.Error;
}

TEST(PluginObjectMergeProviderTest,
     OrdinaryAndroidReleaseRejectsProtectedSectionNameReplacement) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(
      NEVERC_TEST_OBJECT_PROTECTED_SECTION_NAME_CORRUPT_PLUGIN));
  auto Input =
      assembleAndroidReleaseInputWithProtectedSectionSymbol(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
      Scope, *AndroidRoute, std::move(*Input),
      "neverc-ordinary-release-protected-symbol-corrupt", true);
  EXPECT_FALSE(Outcome.Completed);
  EXPECT_FALSE(Outcome.Published);
  EXPECT_NE(Outcome.Error.find("immutable release identity seal"),
            std::string::npos)
      << Outcome.Error;
}

TEST(PluginObjectMergeProviderTest,
     OrdinaryAndroidReleaseRejectsRawSectionNameReplacement) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_SECTION_NAME_CORRUPT_PLUGIN));
  auto Input = assembleAndroidReleaseInputWithInitPLT(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
      Scope, *AndroidRoute, std::move(*Input),
      "neverc-ordinary-release-section-name-corrupt", true);
  EXPECT_FALSE(Outcome.Completed);
  EXPECT_FALSE(Outcome.Published);
  EXPECT_NE(Outcome.Error.find("release layout identity seal"),
            std::string::npos)
      << Outcome.Error;
  EXPECT_NE(Outcome.Error.find(".init.plt"), std::string::npos)
      << Outcome.Error;
  EXPECT_NE(Outcome.Error.find(".hide.plt"), std::string::npos)
      << Outcome.Error;
}

TEST(PluginObjectMergeProviderTest,
     OrdinaryAndroidReleaseRejectsSectionTargetNameReplacement) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope Scope;
  ASSERT_TRUE(
      Scope.initialize(NEVERC_TEST_OBJECT_SECTION_SYMBOL_CORRUPT_PLUGIN));
  auto Input = assembleAndroidReleaseInputWithNamedSection(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
      Scope, *AndroidRoute, std::move(*Input),
      "neverc-ordinary-release-section-symbol-corrupt", true);
  EXPECT_FALSE(Outcome.Completed);
  EXPECT_FALSE(Outcome.Published);
  EXPECT_NE(Outcome.Error.find("immutable release identity seal"),
            std::string::npos)
      << Outcome.Error;
}

TEST(PluginObjectMergeProviderTest,
     FinalizedAndroidReleaseRejectsReplaceableWritePhaseBeforeOpeningSink) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  for (const auto &[PluginPath, Stem] :
       std::array<std::pair<StringRef, StringRef>, 2>{
           std::pair<StringRef, StringRef>{
               NEVERC_TEST_OBJECT_WRITE_INTERCEPTOR_PLUGIN,
               "neverc-release-write-interceptor"},
           std::pair<StringRef, StringRef>{
               NEVERC_TEST_OBJECT_WRITE_PROVIDER_PLUGIN,
               "neverc-release-write-provider"}}) {
    SCOPED_TRACE(Stem.str());
    LinkTaskScope Scope;
    ASSERT_TRUE(Scope.initialize(PluginPath));
    auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
    AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
        Scope, *AndroidRoute, std::move(*Input), Stem, true);
    EXPECT_FALSE(Outcome.Completed);
    EXPECT_FALSE(Outcome.Published);
    EXPECT_NE(Outcome.Error.find("replaceable object write phase"),
              std::string::npos)
        << Outcome.Error;
    EXPECT_NE(Outcome.Error.find("before the trusted image baseline"),
              std::string::npos)
        << Outcome.Error;
  }
}

TEST(PluginObjectMergeProviderTest,
     FinalizedAndroidReleaseIgnoresMismatchedWriteProviderRoute) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  for (const bool NativeOnly : {false, true}) {
    SCOPED_TRACE(NativeOnly ? "native-only" : "ordinary");
    LinkTaskScope Scope;
    ASSERT_TRUE(Scope.initialize(
        NEVERC_TEST_OBJECT_WRITE_MISMATCHED_ROUTE_PROVIDER_PLUGIN));
    auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
    if (NativeOnly) {
      Error HeaderPatch =
          patchELF64Header(*Input, [](object::ELF64LE::Ehdr &Header) {
            Header.e_flags = UINT32_C(0x6a31);
            Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
          });
      ASSERT_FALSE(HeaderPatch) << errorText(std::move(HeaderPatch));
      Error AnonymousPatch = patchELF64SectionHeader(
          *Input, ".native_extra", 0,
          [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
      ASSERT_FALSE(AnonymousPatch) << errorText(std::move(AnonymousPatch));
    }

    AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
        Scope, *AndroidRoute, std::move(*Input),
        NativeOnly ? "neverc-native-only-mismatched-write-provider"
                   : "neverc-release-mismatched-write-provider",
        true);
    EXPECT_TRUE(Outcome.Completed) << Outcome.Error;
    EXPECT_TRUE(Outcome.Published);
    EXPECT_FALSE(Outcome.Output.empty());
  }
}

TEST(PluginObjectMergeProviderTest,
     FinalizedAndroidReleaseRejectsFrozenOutputFormatConfusionBeforeProvider) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope Scope;
  ASSERT_TRUE(
      Scope.initialize(NEVERC_TEST_OBJECT_WRITE_ELF_ROUTE_PROVIDER_PLUGIN));
  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  constexpr NevercObjectFormatID FakeOutputFormat{UINT64_C(0x4e43524f55544542),
                                                  UINT64_C(0xdec0de)};

  AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
      Scope, *AndroidRoute, std::move(*Input),
      "neverc-release-output-format-confusion", true, FakeOutputFormat);
  EXPECT_FALSE(Outcome.Completed);
  EXPECT_FALSE(Outcome.Published);
  EXPECT_NE(Outcome.Error.find("input, target, and output object formats"),
            std::string::npos)
      << Outcome.Error;
  EXPECT_EQ(Outcome.Error.find("Provider callback"), std::string::npos)
      << Outcome.Error;
}

TEST(PluginObjectMergeProviderTest,
     FinalizedAndroidReleaseRejectsNonRelocatableRequestBeforeRouting) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope Scope;
  ASSERT_TRUE(
      Scope.initialize(NEVERC_TEST_OBJECT_WRITE_ELF_ROUTE_PROVIDER_PLUGIN));
  struct InvalidRelocatableState {
    const char *Name;
    linker::LinkExecutionOutputKind RequestKind;
    bool ConfigRelocatable;
  };
  constexpr std::array<InvalidRelocatableState, 3> Cases{{
      {"request-only", linker::LinkExecutionOutputKind::Executable, true},
      {"config-only", linker::LinkExecutionOutputKind::Relocatable, false},
      {"request-and-config", linker::LinkExecutionOutputKind::Executable,
       false},
  }};

  for (const InvalidRelocatableState &Case : Cases) {
    SCOPED_TRACE(Case.Name);
    auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

    AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
        Scope, *AndroidRoute, std::move(*Input),
        (Twine("neverc-release-nonrelocatable-") + Case.Name).str(), true,
        std::nullopt, Case.RequestKind, Case.ConfigRelocatable);
    EXPECT_FALSE(Outcome.Completed);
    EXPECT_FALSE(Outcome.Published);
    EXPECT_NE(Outcome.Error.find("requires a relocatable output request"),
              std::string::npos)
        << Outcome.Error;
    EXPECT_EQ(Outcome.Error.find("Provider callback"), std::string::npos)
        << Outcome.Error;
  }
}

TEST(PluginObjectMergeProviderTest,
     NonReleaseWriteBindingsRetainTheirExistingExecutionSemantics) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  LinkTaskScope InterceptorScope;
  ASSERT_TRUE(
      InterceptorScope.initialize(NEVERC_TEST_OBJECT_WRITE_INTERCEPTOR_PLUGIN));
  auto InterceptorInput = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(InterceptorInput))
      << errorText(InterceptorInput.takeError());
  AndroidObjectLinkOutcome Interceptor = runAndroidObjectLink(
      InterceptorScope, *AndroidRoute, std::move(*InterceptorInput),
      "neverc-nonrelease-write-interceptor", false);
  EXPECT_TRUE(Interceptor.Completed) << Interceptor.Error;
  EXPECT_TRUE(Interceptor.Published);
  EXPECT_FALSE(Interceptor.Output.empty());

  LinkTaskScope ProviderScope;
  ASSERT_TRUE(
      ProviderScope.initialize(NEVERC_TEST_OBJECT_WRITE_PROVIDER_PLUGIN));
  auto ProviderInput = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(ProviderInput))
      << errorText(ProviderInput.takeError());
  AndroidObjectLinkOutcome Provider = runAndroidObjectLink(
      ProviderScope, *AndroidRoute, std::move(*ProviderInput),
      "neverc-nonrelease-write-provider", false);
  EXPECT_FALSE(Provider.Completed);
  EXPECT_FALSE(Provider.Published);
  EXPECT_EQ(Provider.Error.find("finalized Android release"), std::string::npos)
      << Provider.Error;
  EXPECT_NE(Provider.Error.find("Provider"), std::string::npos)
      << Provider.Error;
}

TEST(PluginObjectMergeProviderTest,
     FinalizedThirdPartyMergeUsesItsGraphAndTheHostReleaseWriter) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_MERGE_PLUGIN));
  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

  AndroidObjectLinkOutcome Outcome =
      runAndroidObjectLink(Scope, *AndroidRoute, std::move(*Input),
                           "neverc-third-party-release-host-writer", true);
  ASSERT_TRUE(Outcome.Completed) << Outcome.Error;
  ASSERT_TRUE(Outcome.Published);
  auto Semantics = readELFSemantics(Outcome.Output);
  ASSERT_TRUE(static_cast<bool>(Semantics)) << errorText(Semantics.takeError());
  EXPECT_NE(Semantics->SectionNames.find(".plugin-merged"),
            Semantics->SectionNames.end());
  EXPECT_TRUE(Semantics->HasSymbolStringTable);
  EXPECT_TRUE(Semantics->HasSectionStringTable);
  EXPECT_TRUE(Semantics->SymtabLinksSymbolStringTable);

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  EXPECT_FALSE(verifyFinalAndroidKernelModuleImage(
      Outcome.Output, Policy,
      "test third-party graph serialized by host release writer"));
}

TEST(PluginObjectMergeProviderTest,
     AndroidReleaseSymbolCountRejectsOnlyValuesAboveELF64IndexRange) {
  EXPECT_FALSE(verifyAndroidKernelReleaseSymbolCount(
      std::numeric_limits<uint32_t>::max(), "test maximum symbol count"));
  Error TooLarge = verifyAndroidKernelReleaseSymbolCount(
      uint64_t{std::numeric_limits<uint32_t>::max()} + 1,
      "test excessive symbol count");
  ASSERT_TRUE(static_cast<bool>(TooLarge));
  const std::string Message = errorText(std::move(TooLarge));
  EXPECT_NE(Message.find("exceeds the ELF64 relocation-index range"),
            std::string::npos)
      << Message;
}

TEST(PluginObjectMergeProviderTest,
     DirectBuiltinBindsCompleteMultiInputImageAndBridgeConsumesSameToken) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  auto First = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(First)) << errorText(First.takeError());
  constexpr StringLiteral SecondAssembly = R"(
    .text
    .globl secondary_release_entry
    .type secondary_release_entry, %function
secondary_release_entry:
    nop
    .size secondary_release_entry, .-secondary_release_entry

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  auto Second = assembleBuiltinObject(*AndroidRoute, SecondAssembly);
  ASSERT_TRUE(static_cast<bool>(Second)) << errorText(Second.takeError());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto ReadTarget = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(ReadTarget))
      << errorText(ReadTarget.takeError());
  auto FirstGraph =
      (*Reader)->read(Scope.task(), *First, "memory://bound-first.o",
                      *ReadTarget, AndroidRoute->ObjectFormatID);
  auto SecondGraph =
      (*Reader)->read(Scope.task(), *Second, "memory://bound-second.o",
                      *ReadTarget, AndroidRoute->ObjectFormatID);
  ASSERT_TRUE(static_cast<bool>(FirstGraph))
      << errorText(FirstGraph.takeError());
  ASSERT_TRUE(static_cast<bool>(SecondGraph))
      << errorText(SecondGraph.takeError());

  std::array<PluginObjectGraph *, 2> Objects{FirstGraph->get(),
                                             SecondGraph->get()};
  std::array<ArrayRef<uint8_t>, 2> Images{*First, *Second};
  auto InputContract = verifyAndroidKernelReleaseObjectMergeInputs(
      Objects, Images, ReadTarget->view(),
      "test direct multi-input release contract");
  ASSERT_TRUE(static_cast<bool>(InputContract))
      << errorText(InputContract.takeError());

  auto MergeTarget = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(MergeTarget))
      << errorText(MergeTarget.takeError());
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*MergeTarget), Objects, Images,
      NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  ASSERT_FALSE(Merged->MergedImage.empty());

  const auto &ProducedToken = Merged->boundAndroidKernelReleaseOutput();
  ASSERT_NE(ProducedToken, nullptr);
  const ArrayRef<uint8_t> CompleteMergedImage(
      reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
      Merged->MergedImage.size());
  EXPECT_EQ(ProducedToken->nativeOutputDigest(),
            SHA256::hash(CompleteMergedImage));
  EXPECT_EQ(ProducedToken->nativeOutputDigest(), Merged->ProducerRouteDigest);
  auto ConsumedToken = consumeAndroidKernelReleaseBoundOutput(
      *Merged, *InputContract, "test Bridge bound-output consumption");
  ASSERT_TRUE(static_cast<bool>(ConsumedToken))
      << errorText(ConsumedToken.takeError());
  EXPECT_EQ(ConsumedToken->get(), ProducedToken.get());
  EXPECT_FALSE(ConsumedToken->owner_before(ProducedToken));
  EXPECT_FALSE(ProducedToken.owner_before(*ConsumedToken));
}

TEST(PluginObjectMergeProviderTest,
     AnonymousRegeneratedMetadataDoesNotRequireNativeSectionPassthrough) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    nop
    .size init_module, .-init_module

    .section .data.refs,"aw",%progbits
    .xword init_module

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x8877665544332211

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8

    .addrsig
    .addrsig_sym init_module
)";
  auto Assembled = assembleBuiltinObject(*AndroidRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Assembled)) << errorText(Assembled.takeError());

  const StringRef AssembledRef(
      reinterpret_cast<const char *>(Assembled->data()), Assembled->size());
  auto Canonical = canonicalizeBuiltinELFTables(AssembledRef);
  ASSERT_TRUE(static_cast<bool>(Canonical)) << errorText(Canonical.takeError());
  std::vector<uint8_t> CanonicalImage(Canonical->begin(), Canonical->end());

  StringRef OriginalRef(reinterpret_cast<const char *>(CanonicalImage.data()),
                        CanonicalImage.size());
  auto Original = object::ELFFile<object::ELF64LE>::create(OriginalRef);
  ASSERT_TRUE(static_cast<bool>(Original)) << errorText(Original.takeError());
  auto OriginalSections = Original->sections();
  ASSERT_TRUE(static_cast<bool>(OriginalSections))
      << errorText(OriginalSections.takeError());
  const unsigned SectionStringTableIndex = Original->getHeader().e_shstrndx;
  ASSERT_LT(SectionStringTableIndex, OriginalSections->size());

  std::optional<unsigned> SymbolTableIndex;
  std::optional<unsigned> SymbolStringTableIndex;
  std::set<unsigned> RegeneratedMetadataIndices{SectionStringTableIndex};
  bool SawRela = false;
  bool SawLLVMAddrSig = false;
  bool SawLLVMCallGraphProfile = false;
  for (unsigned Index = 1; Index != OriginalSections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*OriginalSections)[Index];
    switch (Section.sh_type) {
    case ELF::SHT_SYMTAB:
      ASSERT_FALSE(SymbolTableIndex.has_value());
      SymbolTableIndex = Index;
      ASSERT_LT(Section.sh_link, OriginalSections->size());
      SymbolStringTableIndex = Section.sh_link;
      RegeneratedMetadataIndices.insert(Index);
      RegeneratedMetadataIndices.insert(Section.sh_link);
      break;
    case ELF::SHT_RELA:
      SawRela = true;
      RegeneratedMetadataIndices.insert(Index);
      break;
    case ELF::SHT_LLVM_ADDRSIG:
      SawLLVMAddrSig = true;
      RegeneratedMetadataIndices.insert(Index);
      break;
    case ELF::SHT_LLVM_CALL_GRAPH_PROFILE:
      SawLLVMCallGraphProfile = true;
      RegeneratedMetadataIndices.insert(Index);
      break;
    default:
      break;
    }
  }
  ASSERT_TRUE(SymbolTableIndex.has_value());
  ASSERT_TRUE(SymbolStringTableIndex.has_value());
  ASSERT_NE(*SymbolStringTableIndex, SectionStringTableIndex);
  ASSERT_TRUE(SawRela);
  ASSERT_TRUE(SawLLVMAddrSig || SawLLVMCallGraphProfile);

  std::vector<uint8_t> MetadataOnly = CanonicalImage;
  for (unsigned Index : RegeneratedMetadataIndices) {
    Error Patch = patchELF64SectionHeaderAtIndex(
        MetadataOnly, Index,
        [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
    ASSERT_FALSE(Patch) << errorText(std::move(Patch));
  }

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  const auto Inspect = [&](ArrayRef<uint8_t> Image, StringRef LogicalPath)
      -> Expected<AndroidKernelReleaseInputContract> {
    auto Graph = (*Reader)->read(Scope.task(), Image, LogicalPath, *Target,
                                 AndroidRoute->ObjectFormatID);
    if (!Graph)
      return Graph.takeError();
    std::array<PluginObjectGraph *, 1> Objects{Graph->get()};
    std::array<ArrayRef<uint8_t>, 1> Images{Image};
    return verifyAndroidKernelReleaseObjectMergeInputs(
        Objects, Images, Target->view(), LogicalPath);
  };

  auto MetadataContract =
      Inspect(MetadataOnly, "test anonymous regenerated metadata only");
  ASSERT_TRUE(static_cast<bool>(MetadataContract))
      << errorText(MetadataContract.takeError());
  EXPECT_FALSE(MetadataContract->hasRetainedAnonymousSections());
  EXPECT_TRUE(MetadataContract->retainedAnonymousSections().empty());

  std::vector<uint8_t> WithOrdinaryAnonymous = MetadataOnly;
  Error OrdinaryPatch = patchELF64SectionHeader(
      WithOrdinaryAnonymous, ".native_anon", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
  ASSERT_FALSE(OrdinaryPatch) << errorText(std::move(OrdinaryPatch));
  auto OrdinaryContract = Inspect(WithOrdinaryAnonymous,
                                  "test ordinary anonymous PROGBITS contrast");
  ASSERT_TRUE(static_cast<bool>(OrdinaryContract))
      << errorText(OrdinaryContract.takeError());
  EXPECT_TRUE(OrdinaryContract->hasRetainedAnonymousSections());
  ASSERT_EQ(OrdinaryContract->retainedAnonymousSections().size(), 1U);
  EXPECT_EQ(OrdinaryContract->retainedAnonymousSections().front().Type,
            ELF::SHT_PROGBITS);
  EXPECT_EQ(OrdinaryContract->retainedAnonymousSections().front().Flags,
            ELF::SHF_ALLOC);
  EXPECT_EQ(OrdinaryContract->retainedAnonymousSections().front().Size, 8U);
}

TEST(PluginObjectMergeProviderTest,
     NativeAndroidReleaseAnonymousSectionRequiresExactPassthrough) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    nop
    .size init_module, .-init_module

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x0123456789abcdef

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  auto NamedImage = assembleBuiltinObject(*AndroidRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(NamedImage))
      << errorText(NamedImage.takeError());
  std::vector<uint8_t> AnonymousImage = *NamedImage;
  Error AnonymousPatch = patchELF64SectionHeader(
      AnonymousImage, ".native_anon", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
  ASSERT_FALSE(AnonymousPatch) << errorText(std::move(AnonymousPatch));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Graph = (*Reader)->read(Scope.task(), AnonymousImage,
                               "memory://anonymous-section.o", *Target,
                               AndroidRoute->ObjectFormatID);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
  ASSERT_TRUE(
      std::any_of((*Graph)->sections().begin(), (*Graph)->sections().end(),
                  [](const PluginObjectSection &Section) {
                    return StringRef(Section.Name).starts_with("$section.");
                  }))
      << "the built-in reader must expose its lossy anonymous-section "
         "placeholder";

  std::array<PluginObjectGraph *, 1> Objects{Graph->get()};
  std::array<ArrayRef<uint8_t>, 1> Images{AnonymousImage};
  auto Contract = verifyAndroidKernelReleaseObjectMergeInputs(
      Objects, Images, Target->view(),
      "test retained anonymous section input contract");
  ASSERT_TRUE(static_cast<bool>(Contract)) << errorText(Contract.takeError());
  EXPECT_TRUE(Contract->hasRetainedAnonymousSections());
  EXPECT_EQ(Contract->retainedAnonymousSections().size(), 1U);
  EXPECT_TRUE(Contract->requiresNativeImagePassthrough());
  Error UnboundOutput = verifyAndroidKernelReleaseOutputContract(
      AnonymousImage, *Contract,
      "test unbound retained anonymous section output");
  ASSERT_TRUE(static_cast<bool>(UnboundOutput));
  EXPECT_NE(errorText(std::move(UnboundOutput)).find("has not been bound"),
            std::string::npos);

  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Objects, Images,
      NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  const auto &Bound = Merged->boundAndroidKernelReleaseOutput();
  ASSERT_NE(Bound, nullptr);
  const ArrayRef<uint8_t> MergedImage(
      reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
      Merged->MergedImage.size());
  EXPECT_EQ(Bound->nativeOutputDigest(), SHA256::hash(MergedImage));
  EXPECT_FALSE(verifyAndroidKernelReleaseOutputContract(
      MergedImage, *Bound, "test unchanged retained anonymous section output"));

  Error RenamedOutput = verifyAndroidKernelReleaseOutputContract(
      *NamedImage, *Bound, "test renamed anonymous section output");
  ASSERT_TRUE(static_cast<bool>(RenamedOutput));
  EXPECT_NE(errorText(std::move(RenamedOutput)).find("anonymous section"),
            std::string::npos);
  std::vector<uint8_t> ByteTampered(MergedImage.begin(), MergedImage.end());
  StringRef ByteTamperedRef(reinterpret_cast<const char *>(ByteTampered.data()),
                            ByteTampered.size());
  auto ByteTamperedELF =
      object::ELFFile<object::ELF64LE>::create(ByteTamperedRef);
  ASSERT_TRUE(static_cast<bool>(ByteTamperedELF))
      << errorText(ByteTamperedELF.takeError());
  auto ByteTamperedSections = ByteTamperedELF->sections();
  ASSERT_TRUE(static_cast<bool>(ByteTamperedSections))
      << errorText(ByteTamperedSections.takeError());
  bool PatchedText = false;
  for (const object::ELF64LE::Shdr &Section : *ByteTamperedSections) {
    auto Name = ByteTamperedELF->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name != ".text")
      continue;
    ASSERT_LT(Section.sh_offset, ByteTampered.size());
    ByteTampered[Section.sh_offset] ^= UINT8_C(1);
    PatchedText = true;
    break;
  }
  ASSERT_TRUE(PatchedText);
  Error ByteContract = verifyAndroidKernelReleaseOutputContract(
      ByteTampered, *Bound, "test byte-tampered bound native output");
  ASSERT_TRUE(static_cast<bool>(ByteContract));
  EXPECT_NE(errorText(std::move(ByteContract)).find("output bytes"),
            std::string::npos);
}

TEST(PluginObjectMergeProviderTest,
     BoundNativeOutputContractIsImmutableAndImageSpecific) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    nop
    .size init_module, .-init_module

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x0123456789abcdef

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  auto Image = assembleBuiltinObject(*AndroidRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  Error AnonymousPatch = patchELF64SectionHeader(
      *Image, ".native_anon", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
  ASSERT_FALSE(AnonymousPatch) << errorText(std::move(AnonymousPatch));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Graph =
      (*Reader)->read(Scope.task(), *Image, "memory://atomic-bind-input.o",
                      *Target, AndroidRoute->ObjectFormatID);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

  std::array<PluginObjectGraph *, 1> Objects{Graph->get()};
  std::array<ArrayRef<uint8_t>, 1> Images{*Image};
  auto Contract = verifyAndroidKernelReleaseObjectMergeInputs(
      Objects, Images, Target->view(), "test atomic native-output bind input");
  ASSERT_TRUE(static_cast<bool>(Contract)) << errorText(Contract.takeError());
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Objects, Images,
      NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  const auto &Bound = Merged->boundAndroidKernelReleaseOutput();
  ASSERT_NE(Bound, nullptr);
  const ArrayRef<uint8_t> MergedImage(
      reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
      Merged->MergedImage.size());
  EXPECT_EQ(Bound->nativeOutputDigest(), SHA256::hash(MergedImage));

  std::vector<uint8_t> DifferentImage(MergedImage.begin(), MergedImage.end());
  StringRef DifferentRef(reinterpret_cast<const char *>(DifferentImage.data()),
                         DifferentImage.size());
  auto DifferentELF = object::ELFFile<object::ELF64LE>::create(DifferentRef);
  ASSERT_TRUE(static_cast<bool>(DifferentELF))
      << errorText(DifferentELF.takeError());
  auto DifferentSections = DifferentELF->sections();
  ASSERT_TRUE(static_cast<bool>(DifferentSections))
      << errorText(DifferentSections.takeError());
  bool ChangedText = false;
  for (const object::ELF64LE::Shdr &Section : *DifferentSections) {
    auto Name = DifferentELF->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name != ".text")
      continue;
    ASSERT_LT(Section.sh_offset, DifferentImage.size());
    DifferentImage[Section.sh_offset] ^= UINT8_C(1);
    ChangedText = true;
    break;
  }
  ASSERT_TRUE(ChangedText);

  EXPECT_FALSE(verifyAndroidKernelReleaseOutputContract(
      MergedImage, *Bound, "test original contract after failed verification"));

  Error DifferentOutput = verifyAndroidKernelReleaseOutputContract(
      DifferentImage, *Bound, "test different image after failed verification");
  ASSERT_TRUE(static_cast<bool>(DifferentOutput));
  EXPECT_NE(errorText(std::move(DifferentOutput)).find("output bytes"),
            std::string::npos);
}

TEST(PluginObjectMergeProviderTest,
     BuiltinAndroidReleaseBindsMergedAnonymousSectionMultiset) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral FirstAssembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    nop
    .size init_module, .-init_module

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x1111111111111111

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  constexpr StringLiteral SecondAssembly = R"(
    .text
    .globl second_partition_entry
    .type second_partition_entry, %function
second_partition_entry:
    nop
    .size second_partition_entry, .-second_partition_entry

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x2222222222222222

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  auto First = assembleBuiltinObject(*AndroidRoute, FirstAssembly);
  auto Second = assembleBuiltinObject(*AndroidRoute, SecondAssembly);
  ASSERT_TRUE(static_cast<bool>(First)) << errorText(First.takeError());
  ASSERT_TRUE(static_cast<bool>(Second)) << errorText(Second.takeError());
  for (std::vector<uint8_t> *Image : {&*First, &*Second}) {
    Error Patch = patchELF64SectionHeader(
        *Image, ".native_anon", 0,
        [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
    ASSERT_FALSE(Patch) << errorText(std::move(Patch));
  }

  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-native-anonymous-section-merge", Directory));
  auto RemoveDirectory =
      make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> OutputPath(Directory);
  sys::path::append(OutputPath, "anonymous-sections-release.ko");

  linker::LinkExecutionRequest Request;
  Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
  Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
  Request.OutputURI = OutputPath.str().str();
  unsigned InputIndex = 0;
  for (auto *Image : {&*First, &*Second}) {
    linker::LinkExecutionInput Input;
    Input.Kind = linker::LinkExecutionInputKind::Object;
    Input.Ordinal = InputIndex;
    Input.LogicalURI =
        (Twine("memory://anonymous-release-input-") + Twine(InputIndex) + ".o")
            .str();
    ++InputIndex;
    Input.AuthorizedBlob = std::move(*Image);
    Request.Inputs.push_back(std::move(Input));
  }

  linker::LinkerDriverConfig Config;
  Config.pluginTask = &Scope.task();
  Config.relocatable = true;
  Config.androidKernelModule = true;
  Config.finalizeAndroidKernelModule = true;
  Config.stripMode = linker::StripMode::All;

  neverc::OutputCoordinator Outputs;
  auto SessionAlias =
      std::shared_ptr<PluginSession>(&Scope.session(), [](PluginSession *) {});
  LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
  raw_null_ostream NullOutput;
  auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
  ASSERT_TRUE(static_cast<bool>(Result)) << errorText(Result.takeError());
  ASSERT_EQ(Result->Disposition, linker::LinkHookDisposition::Completed);
  Bridge.complete(true);

  auto Output = MemoryBuffer::getFile(OutputPath);
  ASSERT_TRUE(static_cast<bool>(Output));
  auto Parsed =
      object::ELFFile<object::ELF64LE>::create((*Output)->getBuffer());
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  auto Sections = Parsed->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());
  unsigned RetainedAnonymousCount = 0;
  for (unsigned Index = 1; Index != Sections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*Sections)[Index];
    auto Name = Parsed->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (!Name->empty() ||
        neverc::AndroidKernelModuleSectionPolicy::regeneratesReleaseInputType(
            Section.sh_type) ||
        Index == Parsed->getHeader().e_shstrndx)
      continue;
    ++RetainedAnonymousCount;
    EXPECT_EQ(Section.sh_type, ELF::SHT_PROGBITS);
    EXPECT_EQ(Section.sh_flags, ELF::SHF_ALLOC);
    EXPECT_EQ(Section.sh_size, 16U);
    auto Contents = Parsed->getSectionContents(Section);
    ASSERT_TRUE(static_cast<bool>(Contents)) << errorText(Contents.takeError());
    const StringRef Expected("\x11\x11\x11\x11\x11\x11\x11\x11"
                             "\x22\x22\x22\x22\x22\x22\x22\x22",
                             16);
    EXPECT_EQ(*Contents, ArrayRef<uint8_t>(
                             reinterpret_cast<const uint8_t *>(Expected.data()),
                             Expected.size()));
  }
  EXPECT_EQ(RetainedAnonymousCount, 1U);
}

TEST(
    PluginObjectMergeProviderTest,
    DirectBuiltinAndroidReleaseBypassesInternalProvidersAndFoldsAnonymousInputs) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(
      Scope.initialize(NEVERC_TEST_OBJECT_PHASE_OBSERVER_PROVIDER_PLUGIN));
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral FirstAssembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    nop
    .size init_module, .-init_module

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x1111111111111111

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  constexpr StringLiteral SecondAssembly = R"(
    .text
    .globl second_partition_entry
    .type second_partition_entry, %function
second_partition_entry:
    nop
    .size second_partition_entry, .-second_partition_entry

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x2222222222222222

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  auto First = assembleBuiltinObject(*AndroidRoute, FirstAssembly);
  auto Second = assembleBuiltinObject(*AndroidRoute, SecondAssembly);
  ASSERT_TRUE(static_cast<bool>(First)) << errorText(First.takeError());
  ASSERT_TRUE(static_cast<bool>(Second)) << errorText(Second.takeError());
  for (std::vector<uint8_t> *Image : {&*First, &*Second}) {
    Error Patch = patchELF64SectionHeader(
        *Image, ".native_anon", 0,
        [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
    ASSERT_FALSE(Patch) << errorText(std::move(Patch));
  }

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto FirstGraph =
      (*Reader)->read(Scope.task(), *First, "memory://direct-observer-first.o",
                      *Target, AndroidRoute->ObjectFormatID);
  auto SecondGraph = (*Reader)->read(Scope.task(), *Second,
                                     "memory://direct-observer-second.o",
                                     *Target, AndroidRoute->ObjectFormatID);
  ASSERT_TRUE(static_cast<bool>(FirstGraph))
      << errorText(FirstGraph.takeError());
  ASSERT_TRUE(static_cast<bool>(SecondGraph))
      << errorText(SecondGraph.takeError());

  std::array<PluginObjectGraph *, 2> Objects{FirstGraph->get(),
                                             SecondGraph->get()};
  std::array<ArrayRef<uint8_t>, 2> Images{*First, *Second};
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Objects, Images,
      NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  ASSERT_NE(Merged->Object, nullptr);
  ASSERT_FALSE(Merged->MergedImage.empty());

  const StringRef MergedBytes(Merged->MergedImage.data(),
                              Merged->MergedImage.size());
  EXPECT_FALSE(MergedBytes.contains("$section."));
  auto Parsed = object::ELFFile<object::ELF64LE>::create(MergedBytes);
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  auto Sections = Parsed->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());
  unsigned RetainedAnonymousCount = 0;
  for (unsigned Index = 1; Index != Sections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*Sections)[Index];
    auto Name = Parsed->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (!Name->empty() ||
        neverc::AndroidKernelModuleSectionPolicy::regeneratesReleaseInputType(
            Section.sh_type) ||
        Index == Parsed->getHeader().e_shstrndx)
      continue;
    ++RetainedAnonymousCount;
    EXPECT_EQ(Section.sh_type, ELF::SHT_PROGBITS);
    EXPECT_EQ(Section.sh_flags, ELF::SHF_ALLOC);
    EXPECT_EQ(Section.sh_size, 16U);
    auto Contents = Parsed->getSectionContents(Section);
    ASSERT_TRUE(static_cast<bool>(Contents)) << errorText(Contents.takeError());
    const StringRef Expected("\x11\x11\x11\x11\x11\x11\x11\x11"
                             "\x22\x22\x22\x22\x22\x22\x22\x22",
                             16);
    EXPECT_EQ(*Contents, ArrayRef<uint8_t>(
                             reinterpret_cast<const uint8_t *>(Expected.data()),
                             Expected.size()));
  }
  EXPECT_EQ(RetainedAnonymousCount, 1U);
}

TEST(PluginObjectMergeProviderTest,
     DirectBuiltinAndroidReleaseBypassesInternalInterceptors) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(
      Scope.initialize(NEVERC_TEST_OBJECT_PHASE_OBSERVER_INTERCEPTOR_PLUGIN));
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  Error AnonymousPatch = patchELF64SectionHeader(
      *Input, ".native_extra", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
  ASSERT_FALSE(AnonymousPatch) << errorText(std::move(AnonymousPatch));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Graph =
      (*Reader)->read(Scope.task(), *Input, "memory://direct-anonymous-input.o",
                      *Target, AndroidRoute->ObjectFormatID);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

  PluginObjectGraph *GraphPointer = Graph->get();
  std::array<PluginObjectGraph *, 1> Objects{GraphPointer};
  std::array<ArrayRef<uint8_t>, 1> Images{*Input};
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Objects, Images,
      NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  ASSERT_NE(Merged->Object, nullptr);
  ASSERT_FALSE(Merged->MergedImage.empty());
  ASSERT_NE(Merged->boundAndroidKernelReleaseOutput(), nullptr);

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  EXPECT_FALSE(verifyFinalAndroidKernelModuleImage(
      ArrayRef<uint8_t>(
          reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
          Merged->MergedImage.size()),
      Policy, "test direct release bypass of internal interceptors"));
}

TEST(PluginObjectMergeProviderTest,
     NativeAndroidReleaseABIContractRejectsMismatchAndOutputTampering) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());

  auto FirstImage = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(FirstImage))
      << errorText(FirstImage.takeError());
  Error FirstPatch =
      patchELF64Header(*FirstImage, [](object::ELF64LE::Ehdr &Header) {
        Header.e_flags = UINT32_C(0x13579);
        Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
        Header.e_ident[ELF::EI_ABIVERSION] = UINT8_C(9);
      });
  ASSERT_FALSE(FirstPatch) << errorText(std::move(FirstPatch));
  std::vector<uint8_t> SecondImage = *FirstImage;

  auto FirstGraph = makeBuiltinObject(*AndroidRoute, "first_abi_input");
  auto SecondGraph = makeBuiltinObject(*AndroidRoute, "second_abi_input");
  ASSERT_NE(FirstGraph, nullptr);
  ASSERT_NE(SecondGraph, nullptr);
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  std::array<PluginObjectGraph *, 2> Objects{FirstGraph.get(),
                                             SecondGraph.get()};
  std::array<ArrayRef<uint8_t>, 2> MatchingImages{*FirstImage, SecondImage};
  auto Contract = verifyAndroidKernelReleaseObjectMergeInputs(
      Objects, MatchingImages, Target->view(),
      "test matching native-only Android ABI inputs");
  ASSERT_TRUE(static_cast<bool>(Contract)) << errorText(Contract.takeError());
  EXPECT_EQ(Contract->abi().Machine, ELF::EM_AARCH64);
  EXPECT_EQ(Contract->abi().Flags, UINT32_C(0x13579));
  EXPECT_EQ(Contract->abi().OSABI, ELF::ELFOSABI_GNU);
  EXPECT_EQ(Contract->abi().ABIVersion, 9U);
  EXPECT_TRUE(Contract->requiresNativeImagePassthrough());

  Error MismatchPatch =
      patchELF64Header(SecondImage, [](object::ELF64LE::Ehdr &Header) {
        Header.e_flags = static_cast<uint32_t>(Header.e_flags) + UINT32_C(1);
      });
  ASSERT_FALSE(MismatchPatch) << errorText(std::move(MismatchPatch));
  std::array<ArrayRef<uint8_t>, 2> MismatchedImages{*FirstImage, SecondImage};
  auto Mismatched = verifyAndroidKernelReleaseObjectMergeInputs(
      Objects, MismatchedImages, Target->view(),
      "test mismatched native-only Android ABI inputs");
  ASSERT_FALSE(Mismatched);
  EXPECT_NE(errorText(Mismatched.takeError()).find("inconsistent ELF ABI"),
            std::string::npos);

  std::array<PluginObjectGraph *, 1> MergeObjects{FirstGraph.get()};
  std::array<ArrayRef<uint8_t>, 1> MergeImages{*FirstImage};
  auto MergeTarget = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(MergeTarget))
      << errorText(MergeTarget.takeError());
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*MergeTarget), MergeObjects,
      MergeImages, NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  const auto &Bound = Merged->boundAndroidKernelReleaseOutput();
  ASSERT_NE(Bound, nullptr);
  const ArrayRef<uint8_t> MergedImage(
      reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
      Merged->MergedImage.size());
  EXPECT_FALSE(verifyAndroidKernelReleaseOutputContract(
      MergedImage, *Bound, "test unchanged native-only Android output"));

  std::vector<uint8_t> TamperedOutput(MergedImage.begin(), MergedImage.end());
  Error OutputPatch =
      patchELF64Header(TamperedOutput, [](object::ELF64LE::Ehdr &Header) {
        Header.e_ident[ELF::EI_ABIVERSION] ^= UINT8_C(1);
      });
  ASSERT_FALSE(OutputPatch) << errorText(std::move(OutputPatch));
  Error OutputContract = verifyAndroidKernelReleaseOutputContract(
      TamperedOutput, *Bound, "test tampered native-only Android output");
  ASSERT_TRUE(static_cast<bool>(OutputContract));
  EXPECT_NE(errorText(std::move(OutputContract)).find("does not match"),
            std::string::npos);
}

TEST(PluginObjectMergeProviderTest,
     BuiltinAndroidReleasePreservesNativeOnlyABIWithoutGraphPhases) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  Error Patched = patchELF64Header(*Input, [](object::ELF64LE::Ehdr &Header) {
    Header.e_flags = UINT32_C(0x2468);
    Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
    Header.e_ident[ELF::EI_ABIVERSION] = UINT8_C(7);
  });
  ASSERT_FALSE(Patched) << errorText(std::move(Patched));

  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory("neverc-native-abi-passthrough",
                                              Directory));
  auto RemoveDirectory =
      make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> OutputPath(Directory);
  sys::path::append(OutputPath, "native-abi-release.ko");

  linker::LinkExecutionRequest Request;
  Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
  Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
  Request.OutputURI = OutputPath.str().str();
  linker::LinkExecutionInput LinkInput;
  LinkInput.Kind = linker::LinkExecutionInputKind::Object;
  LinkInput.LogicalURI = "memory://native-abi-release-input.o";
  LinkInput.AuthorizedBlob = std::move(*Input);
  Request.Inputs.push_back(std::move(LinkInput));

  linker::LinkerDriverConfig Config;
  Config.pluginTask = &Scope.task();
  Config.relocatable = true;
  Config.androidKernelModule = true;
  Config.finalizeAndroidKernelModule = true;
  Config.stripMode = linker::StripMode::All;

  neverc::OutputCoordinator Outputs;
  auto SessionAlias =
      std::shared_ptr<PluginSession>(&Scope.session(), [](PluginSession *) {});
  LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
  raw_null_ostream NullOutput;
  auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
  ASSERT_TRUE(static_cast<bool>(Result)) << errorText(Result.takeError());
  EXPECT_EQ(Result->Disposition, linker::LinkHookDisposition::Completed);
  Bridge.complete(true);

  auto Output = MemoryBuffer::getFile(OutputPath);
  ASSERT_TRUE(static_cast<bool>(Output));
  StringRef OutputBytes = (*Output)->getBuffer();
  auto Parsed = object::ELFFile<object::ELF64LE>::create(OutputBytes);
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  const object::ELF64LE::Ehdr &Header = Parsed->getHeader();
  EXPECT_EQ(Header.e_flags, UINT32_C(0x2468));
  EXPECT_EQ(Header.e_ident[ELF::EI_OSABI], ELF::ELFOSABI_GNU);
  EXPECT_EQ(Header.e_ident[ELF::EI_ABIVERSION], 7U);
}

TEST(PluginObjectMergeProviderTest,
     NativeOnlyAndroidABIRejectsCustomProviderAndObjectPhasesBeforeSink) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  const auto RunRejected = [&](StringRef PluginPath, StringRef ExpectedReason,
                               StringRef OutputStem, bool UseAnonymousSection) {
    LinkTaskScope Scope;
    ASSERT_TRUE(Scope.initialize(PluginPath));
    auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
    if (UseAnonymousSection) {
      Error SectionPatch = patchELF64SectionHeader(
          *Input, ".native_extra", 0,
          [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
      ASSERT_FALSE(SectionPatch) << errorText(std::move(SectionPatch));
    } else {
      Error HeaderPatch =
          patchELF64Header(*Input, [](object::ELF64LE::Ehdr &Header) {
            Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
          });
      ASSERT_FALSE(HeaderPatch) << errorText(std::move(HeaderPatch));
    }

    SmallString<128> Directory;
    ASSERT_FALSE(sys::fs::createUniqueDirectory(OutputStem, Directory));
    auto RemoveDirectory =
        make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
    SmallString<160> OutputPath(Directory);
    sys::path::append(OutputPath, "must-not-open.ko");

    linker::LinkExecutionRequest Request;
    Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
    Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
    Request.OutputURI = OutputPath.str().str();
    linker::LinkExecutionInput LinkInput;
    LinkInput.Kind = linker::LinkExecutionInputKind::Object;
    LinkInput.LogicalURI = "memory://native-only-rejected-input.o";
    LinkInput.AuthorizedBlob = std::move(*Input);
    Request.Inputs.push_back(std::move(LinkInput));

    linker::LinkerDriverConfig Config;
    Config.pluginTask = &Scope.task();
    Config.relocatable = true;
    Config.androidKernelModule = true;
    Config.finalizeAndroidKernelModule = true;
    Config.stripMode = linker::StripMode::All;

    neverc::OutputCoordinator Outputs;
    auto SessionAlias = std::shared_ptr<PluginSession>(&Scope.session(),
                                                       [](PluginSession *) {});
    LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
    raw_null_ostream NullOutput;
    auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
    ASSERT_FALSE(Result);
    const std::string Message = errorText(Result.takeError());
    EXPECT_NE(Message.find(ExpectedReason.str()), std::string::npos) << Message;
    EXPECT_FALSE(sys::fs::exists(OutputPath));
  };

  RunRejected(NEVERC_TEST_OBJECT_MERGE_PLUGIN,
              "third-party ObjectMergeProvider cannot preserve native-only",
              "neverc-native-abi-custom-provider", false);
  RunRejected(NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN,
              "incompatible with registered ObjectGraph/output phase bindings",
              "neverc-native-abi-object-phase", false);
  RunRejected(NEVERC_TEST_OBJECT_CONTRACT_CORRUPT_PLUGIN,
              "incompatible with registered ObjectGraph/output phase bindings",
              "neverc-native-abi-graph-phase", false);
  RunRejected(NEVERC_TEST_OBJECT_MERGE_PLUGIN,
              "third-party ObjectMergeProvider cannot preserve native-only",
              "neverc-native-anonymous-custom-provider", true);
  RunRejected(NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN,
              "incompatible with registered ObjectGraph/output phase bindings",
              "neverc-native-anonymous-object-phase", true);
  RunRejected(NEVERC_TEST_OBJECT_CONTRACT_CORRUPT_PLUGIN,
              "incompatible with registered ObjectGraph/output phase bindings",
              "neverc-native-anonymous-graph-phase", true);
}

TEST(PluginObjectMergeProviderTest,
     CustomProviderCannotBypassNativeAndroidReleaseInputAudit) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_MERGE_PLUGIN));

  const BuiltinTargetRoute *AndroidRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64 && Parsed.isAndroid()) {
      AndroidRoute = &Route;
      break;
    }
  }
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl release_input_entry
    .type release_input_entry, %function
release_input_entry:
    nop
    .size release_input_entry, .-release_input_entry
    .globl livepatch_target
    .type livepatch_target, %object
livepatch_target:
    .word 0
    .size livepatch_target, .-livepatch_target

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  auto Input = assembleBuiltinObject(*AndroidRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  Error Patched = patchELF64SymbolSectionIndex(
      *Input, "livepatch_target",
      neverc::AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex);
  ASSERT_FALSE(Patched) << errorText(std::move(Patched));

  linker::LinkExecutionRequest Request;
  Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
  Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
  Request.OutputURI = "custom-provider-invalid-release.ko";
  linker::LinkExecutionInput LinkInput;
  LinkInput.Kind = linker::LinkExecutionInputKind::Object;
  LinkInput.LogicalURI = "memory://livepatch-release-input.o";
  LinkInput.AuthorizedBlob = std::move(*Input);
  Request.Inputs.push_back(std::move(LinkInput));

  linker::LinkerDriverConfig Config;
  Config.pluginTask = &Scope.task();
  Config.relocatable = true;
  Config.androidKernelModule = true;
  Config.finalizeAndroidKernelModule = true;
  Config.stripMode = linker::StripMode::All;

  neverc::OutputCoordinator Outputs;
  auto SessionAlias =
      std::shared_ptr<PluginSession>(&Scope.session(), [](PluginSession *) {});
  LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
  raw_null_ostream NullOutput;
  auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
  ASSERT_FALSE(static_cast<bool>(Result));
  const std::string Message = errorText(Result.takeError());
  EXPECT_NE(Message.find("livepatch symbol"), std::string::npos) << Message;
}

TEST(PluginObjectMergeProviderTest,
     BuiltinAdapterRoundTripsTypedGraphsThroughRelocatableMerge) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::x86_64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto First = makeBuiltinObject(*ELFRoute, "merge_first");
  auto Second = makeBuiltinObject(*ELFRoute, "merge_second");
  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  std::array<PluginObjectGraph *, 2> Inputs{First.get(), Second.get()};
  auto Merged = executeBuiltinObjectMergeAdapter(Scope.task(), *Snapshot,
                                                 std::move(*Target), Inputs);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  ASSERT_NE(Merged->Object, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*Merged->Object));
  EXPECT_EQ(Merged->PluginID, "neverc.builtin");
  EXPECT_EQ(Merged->ProviderID, "neverc.builtin.object-merge");

  const auto HasSymbol = [&](StringRef Name) {
    const auto &Symbols = Merged->Object->symbols();
    return std::any_of(
        Symbols.begin(), Symbols.end(),
        [&](const PluginObjectSymbol &Symbol) { return Symbol.Name == Name; });
  };
  EXPECT_TRUE(HasSymbol("merge_first"));
  EXPECT_TRUE(HasSymbol("merge_second"));
}

TEST(PluginObjectMergeProviderTest,
     NativeOnlyAndroidReleaseAllowsReadOnlyObjectObservers) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  const auto Run =
      [&](StringRef PluginPath,
          StringRef OutputStem) -> std::optional<std::vector<uint8_t>> {
    LinkTaskScope Scope;
    if (!Scope.initialize(PluginPath))
      return std::nullopt;
    auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
    if (!Input) {
      ADD_FAILURE() << errorText(Input.takeError());
      return std::nullopt;
    }
    Error Patched = patchELF64Header(*Input, [](object::ELF64LE::Ehdr &Header) {
      Header.e_flags = UINT32_C(0x5a17);
      Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
      Header.e_ident[ELF::EI_ABIVERSION] = UINT8_C(11);
    });
    if (Patched) {
      ADD_FAILURE() << errorText(std::move(Patched));
      return std::nullopt;
    }
    Error AnonymousPatch = patchELF64SectionHeader(
        *Input, ".native_extra", 0,
        [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
    if (AnonymousPatch) {
      ADD_FAILURE() << errorText(std::move(AnonymousPatch));
      return std::nullopt;
    }

    SmallString<128> Directory;
    if (std::error_code EC =
            sys::fs::createUniqueDirectory(OutputStem, Directory)) {
      ADD_FAILURE() << EC.message();
      return std::nullopt;
    }
    auto RemoveDirectory =
        make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
    SmallString<160> OutputPath(Directory);
    sys::path::append(OutputPath, "observer-release.ko");

    linker::LinkExecutionRequest Request;
    Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
    Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
    Request.OutputURI = OutputPath.str().str();
    linker::LinkExecutionInput LinkInput;
    LinkInput.Kind = linker::LinkExecutionInputKind::Object;
    LinkInput.LogicalURI = "memory://native-only-observer-input.o";
    LinkInput.AuthorizedBlob = std::move(*Input);
    Request.Inputs.push_back(std::move(LinkInput));

    linker::LinkerDriverConfig Config;
    Config.pluginTask = &Scope.task();
    Config.relocatable = true;
    Config.androidKernelModule = true;
    Config.finalizeAndroidKernelModule = true;
    Config.stripMode = linker::StripMode::All;

    neverc::OutputCoordinator Outputs;
    auto SessionAlias = std::shared_ptr<PluginSession>(&Scope.session(),
                                                       [](PluginSession *) {});
    LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
    raw_null_ostream NullOutput;
    auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
    if (!Result) {
      ADD_FAILURE() << errorText(Result.takeError());
      return std::nullopt;
    }
    if (Result->Disposition != linker::LinkHookDisposition::Completed) {
      ADD_FAILURE() << "native-only observer link did not complete";
      Bridge.complete(false);
      return std::nullopt;
    }
    Bridge.complete(true);

    auto Output = MemoryBuffer::getFile(OutputPath);
    if (!Output) {
      ADD_FAILURE() << Output.getError().message();
      return std::nullopt;
    }
    StringRef Bytes = (*Output)->getBuffer();
    return std::vector<uint8_t>(Bytes.bytes_begin(), Bytes.bytes_end());
  };

  auto Baseline = Run({}, "neverc-native-only-observer-baseline");
  ASSERT_TRUE(Baseline.has_value());
  auto Observed = Run(NEVERC_TEST_OBJECT_PHASE_OBSERVER_PLUGIN,
                      "neverc-native-only-observer-plugin");
  ASSERT_TRUE(Observed.has_value());
  EXPECT_EQ(*Observed, *Baseline);

  StringRef ObservedImage(reinterpret_cast<const char *>(Observed->data()),
                          Observed->size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(ObservedImage);
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  EXPECT_EQ(Parsed->getHeader().e_flags, UINT32_C(0x5a17));
  EXPECT_EQ(Parsed->getHeader().e_ident[ELF::EI_OSABI], ELF::ELFOSABI_GNU);
  EXPECT_EQ(Parsed->getHeader().e_ident[ELF::EI_ABIVERSION], 11U);
  auto ObserverSections = Parsed->sections();
  ASSERT_TRUE(static_cast<bool>(ObserverSections))
      << errorText(ObserverSections.takeError());
  unsigned AnonymousLogicalSections = 0;
  for (unsigned Index = 1; Index != ObserverSections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*ObserverSections)[Index];
    auto Name = Parsed->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (Name->empty() &&
        !neverc::AndroidKernelModuleSectionPolicy::regeneratesReleaseInputType(
            Section.sh_type) &&
        Index != Parsed->getHeader().e_shstrndx)
      ++AnonymousLogicalSections;
  }
  EXPECT_EQ(AnonymousLogicalSections, 1U);
}

static std::optional<std::vector<uint8_t>>
runObjectCapabilityCachePipeline(StringRef PluginPath, StringRef OutputName) {
  LinkTaskScope Scope;
  if (!Scope.initialize(PluginPath))
    return std::nullopt;
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  if (!AndroidRoute) {
    ADD_FAILURE() << "AArch64 Android object route is unavailable";
    return std::nullopt;
  }
  auto Graph = makeBuiltinObject(*AndroidRoute, "capability_cache_entry");
  if (!Graph) {
    ADD_FAILURE() << "capability-cache graph could not be created";
    return std::nullopt;
  }
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  if (!Snapshot) {
    ADD_FAILURE() << errorText(Snapshot.takeError());
    return std::nullopt;
  }
  auto Pipeline = ObjectPhasePipeline::create(Scope.task(), *Snapshot);
  if (!Pipeline) {
    ADD_FAILURE() << errorText(Pipeline.takeError());
    return std::nullopt;
  }
  ObjectOutputDestination Destination =
      ObjectOutputDestination::memory(OutputName, UINT64_C(1) << 20);
  auto Image = (*Pipeline)->execute(*Graph, Destination);
  if (!Image) {
    ADD_FAILURE() << errorText(Image.takeError());
    return std::nullopt;
  }
  auto Output = findPluginMemoryOutput(Scope.task(), OutputName);
  if (!Output) {
    ADD_FAILURE() << "capability-cache pipeline published no memory output";
    return std::nullopt;
  }
  return std::move(Output->Bytes);
}

TEST(PluginObjectMergeProviderTest,
     CachedObjectCapabilitiesCannotMutateFromAfterObservers) {
  auto Baseline = runObjectCapabilityCachePipeline(
      {}, "capability-cache-after-observer-baseline.o");
  ASSERT_TRUE(Baseline.has_value());
  auto Protected = runObjectCapabilityCachePipeline(
      NEVERC_TEST_OBJECT_CAPABILITY_AFTER_OBSERVER_PLUGIN,
      "capability-cache-after-observer-protected.o");
  ASSERT_TRUE(Protected.has_value());
  EXPECT_EQ(*Protected, *Baseline);
}

TEST(PluginObjectMergeProviderTest,
     CachedObjectCapabilitiesCannotMutateAcrossThreads) {
  auto Baseline = runObjectCapabilityCachePipeline(
      {}, "capability-cache-cross-thread-baseline.o");
  ASSERT_TRUE(Baseline.has_value());
  auto Protected = runObjectCapabilityCachePipeline(
      NEVERC_TEST_OBJECT_CAPABILITY_CROSS_THREAD_PLUGIN,
      "capability-cache-cross-thread-protected.o");
  ASSERT_TRUE(Protected.has_value());
  EXPECT_EQ(*Protected, *Baseline);
}

TEST(PluginObjectMergeProviderTest,
     PreWriteGraphFacadeRemainsSafeThroughPostLayoutObserver) {
  auto Baseline = runObjectCapabilityCachePipeline(
      {}, "capability-cache-graph-cross-phase-baseline.o");
  ASSERT_TRUE(Baseline.has_value());
  auto Protected = runObjectCapabilityCachePipeline(
      NEVERC_TEST_OBJECT_CAPABILITY_GRAPH_CROSS_PHASE_PLUGIN,
      "capability-cache-graph-cross-phase-protected.o");
  ASSERT_TRUE(Protected.has_value());
  EXPECT_EQ(*Protected, *Baseline);
}

TEST(PluginObjectMergeProviderTest,
     PostWriteBinaryFacadeRemainsSafeThroughFinalVerifyObserver) {
  auto Baseline = runObjectCapabilityCachePipeline(
      {}, "capability-cache-binary-cross-phase-baseline.o");
  ASSERT_TRUE(Baseline.has_value());
  auto Protected = runObjectCapabilityCachePipeline(
      NEVERC_TEST_OBJECT_CAPABILITY_BINARY_CROSS_PHASE_PLUGIN,
      "capability-cache-binary-cross-phase-protected.o");
  ASSERT_TRUE(Protected.has_value());
  EXPECT_EQ(*Protected, *Baseline);
}

TEST(PluginObjectMergeProviderTest,
     NativeOnlyAndroidReleaseRejectsArtifactReplacementHooksBeforeSink) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  const auto RunRejected = [&](StringRef PluginPath, StringRef OutputStem) {
    LinkTaskScope Scope;
    ASSERT_TRUE(Scope.initialize(PluginPath));
    auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
    Error Patched = patchELF64Header(*Input, [](object::ELF64LE::Ehdr &Header) {
      Header.e_flags = UINT32_C(0x7b19);
      Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
    });
    ASSERT_FALSE(Patched) << errorText(std::move(Patched));
    Error AnonymousPatch = patchELF64SectionHeader(
        *Input, ".native_extra", 0,
        [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
    ASSERT_FALSE(AnonymousPatch) << errorText(std::move(AnonymousPatch));

    SmallString<128> Directory;
    ASSERT_FALSE(sys::fs::createUniqueDirectory(OutputStem, Directory));
    auto RemoveDirectory =
        make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
    SmallString<160> OutputPath(Directory);
    sys::path::append(OutputPath, "must-not-open.ko");

    linker::LinkExecutionRequest Request;
    Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
    Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
    Request.OutputURI = OutputPath.str().str();
    linker::LinkExecutionInput LinkInput;
    LinkInput.Kind = linker::LinkExecutionInputKind::Object;
    LinkInput.LogicalURI = "memory://native-only-replacement-hook.o";
    LinkInput.AuthorizedBlob = std::move(*Input);
    Request.Inputs.push_back(std::move(LinkInput));

    linker::LinkerDriverConfig Config;
    Config.pluginTask = &Scope.task();
    Config.relocatable = true;
    Config.androidKernelModule = true;
    Config.finalizeAndroidKernelModule = true;
    Config.stripMode = linker::StripMode::All;

    neverc::OutputCoordinator Outputs;
    auto SessionAlias = std::shared_ptr<PluginSession>(&Scope.session(),
                                                       [](PluginSession *) {});
    LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
    raw_null_ostream NullOutput;
    auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
    ASSERT_FALSE(Result);
    const std::string Message = errorText(Result.takeError());
    EXPECT_NE(Message.find("native-image passthrough"), std::string::npos)
        << Message;
    EXPECT_NE(Message.find("incompatible"), std::string::npos) << Message;
    EXPECT_FALSE(sys::fs::exists(OutputPath));
  };

  RunRejected(NEVERC_TEST_OBJECT_PHASE_OBSERVER_INTERCEPTOR_PLUGIN,
              "neverc-native-only-observer-interceptor");
  RunRejected(NEVERC_TEST_OBJECT_PHASE_OBSERVER_PROVIDER_PLUGIN,
              "neverc-native-only-observer-provider");
}

TEST(PluginObjectMergeProviderTest,
     NestedReadOnlyCallbackCannotReuseExternalMergeMutationFacade) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_MERGE_PLUGIN,
                               NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN));
  auto Input = makeObject(1);
  auto Target = makeTargetKey();
  ASSERT_NE(Input, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  NestedMergeMutationState State;
  State.Task = &Scope.task();
  State.ObserverPluginID = Scope.plugin(1)->descriptor().PluginID;

  PluginLinkSnapshot::ObjectMergeProviderRecord Provider;
  Provider.PluginID = Scope.plugin(0)->descriptor().PluginID;
  Provider.Owner = Scope.plugin(0);
  Provider.ProviderID = "nested-merge-capability";
  Provider.TargetID = TestTargetID;
  Provider.FormatID = TestFormatID;
  Provider.ProductID = TestProductID;
  Provider.Merge = mergeAndAttemptMutationFromNestedObserver;
  Provider.UserData = &State;
  Provider.Builtin = false;
  PluginObjectGraph *InputPointer = Input.get();

  auto Merged = executeObjectMergeProvider(
      Scope.task(), Provider, std::move(*Target),
      ArrayRef<PluginObjectGraph *>(&InputPointer, 1));
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  EXPECT_EQ(State.ObserverDispatch.Code, NEVERC_STATUS_OK);
  EXPECT_EQ(State.MutationAttempt.Code, NEVERC_STATUS_POLICY_VIOLATION);
}

} // namespace
