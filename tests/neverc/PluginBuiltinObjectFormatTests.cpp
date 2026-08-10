#include "../../neverc/lib/Plugin/Object/BuiltinELFTableCanonicalizer.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/ObjectSectionRole.h"
#include "neverc/Plugin/Host/ObjectWriterProvider.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

class BuiltinObjectTaskScope {
public:
  BuiltinObjectTaskScope()
      : Services("neverc-plugin-builtin-object-format-tests",
                 LLVM_VERSION_MAJOR) {}

  bool initialize() {
    if (Error E = registerPluginIOInterface(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    auto CreatedPlan = makePluginActivationPlan(Services.registry(), {});
    if (!CreatedPlan) {
      ADD_FAILURE() << errorText(CreatedPlan.takeError());
      return false;
    }
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    if (!CreatedSession) {
      ADD_FAILURE() << errorText(CreatedSession.takeError());
      return false;
    }
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_CODEGEN);
    if (!CreatedTask) {
      ADD_FAILURE() << errorText(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    return true;
  }

  ~BuiltinObjectTaskScope() {
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginTaskContext &task() { return *Task; }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
};

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

Expected<OwnedTargetKey> makeBuiltinTargetKey(const BuiltinTargetRoute &Route) {
  Triple Parsed(Triple::normalize(Route.CanonicalTriple));
  TargetKeyBuilder Builder;
  Builder.setTargetID(Route.TargetID)
      .setTriple(Route.CanonicalTriple.str(), Parsed.getArchName().str(),
                 Parsed.getVendorName().str(), Parsed.getOSName().str(),
                 Parsed.getEnvironmentName().str())
      .setCPU(Route.DefaultCPU.str(), Route.DefaultCPU.str())
      .setFeatures({})
      .setABI(Route.ABIID)
      .setCallingConvention({UINT64_C(0x4e43424f46434349), Route.TargetID.Low})
      .setObjectFormat(Route.ObjectFormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  return Builder.build();
}

Expected<std::vector<uint8_t>>
assembleRelocatable(const BuiltinTargetRoute &Route) {
  auto Target = lookupBuiltinLLVMTarget(Route);
  if (!Target)
    return Target.takeError();
  const Triple TargetTriple(Triple::normalize(Route.CanonicalTriple));
  const bool MachO = TargetTriple.isOSBinFormatMachO();
  std::string Assembly;
  raw_string_ostream Source(Assembly);
  Source << "\t.text\n"
         << "\t.globl\t" << (MachO ? "_roundtrip_entry" : "roundtrip_entry")
         << '\n'
         << (MachO ? "_roundtrip_entry" : "roundtrip_entry") << ":\n"
         << "\t.byte\t0xc3\n"
         << "\t.data\n"
         << "\t.globl\t" << (MachO ? "_roundtrip_pointer" : "roundtrip_pointer")
         << '\n'
         << (MachO ? "_roundtrip_pointer" : "roundtrip_pointer") << ":\n"
         << "\t.quad\t"
         << (MachO ? "_roundtrip_external" : "roundtrip_external") << '\n';
  Source.flush();

  SmallVector<char, 0> Bytes;
  raw_svector_ostream Output(Bytes);
  BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple = TargetTriple;
  Request.CPU = Route.DefaultCPU;
  Request.Input = MemoryBufferRef(Assembly, "<builtin-object-roundtrip>");
  Request.Output = &Output;
  if (Error E = runBuiltinLLVMAsmParser(Request))
    return std::move(E);
  return std::vector<uint8_t>(Bytes.begin(), Bytes.end());
}

struct RelocationSemantics {
  NevercObjectRelocationKind Kind = 0;
  NevercObjectRelocationTargetKind TargetKind = 0;
  uint32_t Width = 0;
  bool IsPCRelative = false;
  bool IsSigned = false;
  std::string TargetName;

  auto asTuple() const {
    return std::tie(Kind, TargetKind, Width, IsPCRelative, IsSigned,
                    TargetName);
  }

  friend bool operator==(const RelocationSemantics &Left,
                         const RelocationSemantics &Right) {
    return Left.asTuple() == Right.asTuple();
  }
};

std::vector<RelocationSemantics>
relocationSemantics(const PluginObjectGraph &Graph) {
  std::vector<RelocationSemantics> Result;
  for (const PluginObjectRelocation &Relocation : Graph.relocations()) {
    RelocationSemantics Semantics;
    Semantics.Kind = Relocation.Kind;
    Semantics.TargetKind = Relocation.TargetKind;
    Semantics.Width = Relocation.Width;
    Semantics.IsPCRelative = Relocation.IsPCRelative;
    Semantics.IsSigned = Relocation.IsSigned;
    if (Relocation.TargetKind == NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL) {
      if (const PluginObjectSymbol *Symbol =
              Graph.findSymbol(Relocation.TargetSymbolID))
        Semantics.TargetName = Symbol->Name;
    } else if (Relocation.TargetKind ==
               NEVERC_OBJECT_RELOCATION_TARGET_SECTION) {
      if (const PluginObjectSection *Section =
              Graph.findSection(Relocation.TargetSectionID))
        Semantics.TargetName = Section->Name;
    }
    Result.push_back(std::move(Semantics));
  }
  llvm::sort(Result, [](const RelocationSemantics &Left,
                        const RelocationSemantics &Right) {
    return Left.asTuple() < Right.asTuple();
  });
  return Result;
}

struct SectionSemantics {
  std::string Name;
  NevercObjectSectionKind Kind = 0;
  NevercObjectSectionFlags Flags = 0;
  uint64_t Size = 0;

  auto asTuple() const { return std::tie(Name, Kind, Flags, Size); }

  friend bool operator==(const SectionSemantics &Left,
                         const SectionSemantics &Right) {
    return Left.asTuple() == Right.asTuple();
  }
};

std::vector<SectionSemantics> sectionSemantics(const PluginObjectGraph &Graph) {
  std::vector<SectionSemantics> Result;
  for (const PluginObjectSection &Section : Graph.sections()) {
    if (Section.Kind != NEVERC_OBJECT_SECTION_KIND_TEXT &&
        Section.Kind != NEVERC_OBJECT_SECTION_KIND_DATA)
      continue;
    Result.push_back(
        {Section.Name, Section.Kind,
         Section.Flags & (NEVERC_OBJECT_SECTION_ALLOCATED |
                          NEVERC_OBJECT_SECTION_EXECUTABLE |
                          NEVERC_OBJECT_SECTION_WRITABLE),
         static_cast<uint64_t>(Section.Data.size()) + Section.ZeroFillSize});
  }
  llvm::sort(Result,
             [](const SectionSemantics &Left, const SectionSemantics &Right) {
               return Left.asTuple() < Right.asTuple();
             });
  return Result;
}

struct SymbolSemantics {
  std::string Name;
  NevercObjectSymbolBinding Binding = 0;
  NevercObjectSymbolVisibility Visibility = 0;
  NevercObjectSymbolType Type = 0;
  NevercObjectSymbolDefinition Definition = 0;
  std::string SectionName;

  auto asTuple() const {
    return std::tie(Name, Binding, Visibility, Type, Definition, SectionName);
  }

  friend bool operator==(const SymbolSemantics &Left,
                         const SymbolSemantics &Right) {
    return Left.asTuple() == Right.asTuple();
  }
};

std::vector<SymbolSemantics> symbolSemantics(const PluginObjectGraph &Graph) {
  std::vector<SymbolSemantics> Result;
  for (const PluginObjectSymbol &Symbol : Graph.symbols()) {
    if (StringRef(Symbol.Name).find("roundtrip_") == StringRef::npos)
      continue;
    SymbolSemantics Semantics{Symbol.Name, Symbol.Binding,    Symbol.Visibility,
                              Symbol.Type, Symbol.Definition, {}};
    if (const PluginObjectSection *Section =
            Graph.findSection(Symbol.SectionID))
      Semantics.SectionName = Section->Name;
    Result.push_back(std::move(Semantics));
  }
  llvm::sort(Result,
             [](const SymbolSemantics &Left, const SymbolSemantics &Right) {
               return Left.asTuple() < Right.asTuple();
             });
  return Result;
}

TEST(PluginBuiltinObjectFormatTest,
     ReadWriteReadPreservesRelocationSemanticsForSixBuiltinRoutes) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());

  std::set<std::pair<Triple::ArchType, BuiltinObjectFormat>> Tested;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    const auto RouteKey = std::make_pair(Parsed.getArch(), Route.ObjectFormat);
    if (!Route.SupportsObject || !Tested.insert(RouteKey).second)
      continue;
    SCOPED_TRACE(Route.CanonicalName.str());

    auto Target = makeBuiltinTargetKey(Route);
    ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
    auto Input = assembleRelocatable(Route);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
    auto Before = (*Reader)->read(Scope.task(), *Input, "before.o", *Target);
    ASSERT_TRUE(static_cast<bool>(Before)) << errorText(Before.takeError());
    ASSERT_GT((*Before)->relocationCount(), 0U);
    const auto ExpectedRelocations = relocationSemantics(**Before);
    const auto ExpectedSections = sectionSemantics(**Before);
    const auto ExpectedSymbols = symbolSemantics(**Before);
    (*Before)->issueLayoutProof();

    const std::string OutputName =
        "roundtrip-" + std::to_string(Tested.size()) + ".o";
    auto Candidate = (*Writer)->write(
        Scope.task(), **Before,
        ObjectOutputDestination::memory(OutputName, UINT64_C(1) << 20));
    ASSERT_TRUE(static_cast<bool>(Candidate))
        << errorText(Candidate.takeError());
    ASSERT_FALSE((*Candidate)->verify());
    auto Committed = (*Candidate)->commit();
    ASSERT_TRUE(static_cast<bool>(Committed))
        << errorText(Committed.takeError());
    auto Output = findPluginMemoryOutput(Scope.task(), OutputName);
    ASSERT_TRUE(Output.has_value());

    auto After =
        (*Reader)->read(Scope.task(), Output->Bytes, OutputName, *Target);
    ASSERT_TRUE(static_cast<bool>(After)) << errorText(After.takeError());
    EXPECT_EQ(relocationSemantics(**After), ExpectedRelocations);
    EXPECT_EQ(sectionSemantics(**After), ExpectedSections);
    EXPECT_EQ(symbolSemantics(**After), ExpectedSymbols);
  }
  EXPECT_EQ(Tested.size(), 6U);
}

TEST(PluginBuiltinObjectFormatTest,
     WritersPreserveTLSSectionsForSixBuiltinRoutes) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());

  std::set<std::pair<Triple::ArchType, BuiltinObjectFormat>> Tested;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    const auto RouteKey = std::make_pair(Parsed.getArch(), Route.ObjectFormat);
    if (!Route.SupportsObject || !Tested.insert(RouteKey).second)
      continue;
    SCOPED_TRACE(Route.CanonicalName.str());

    auto Target = makeBuiltinTargetKey(Route);
    ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
    OwnedTargetKey ReadTarget = *Target;
    PluginObjectGraph Graph(std::move(*Target));
    PluginObjectSection Section;
    Section.ID = Graph.allocateEntityID();
    // Each format names its thread-local section differently, and the reader
    // recognises COFF's by name because a COFF section header has no
    // thread-local bit. Using ".tdata" everywhere would test the ELF spelling
    // against all three readers and say nothing about the COFF one.
    switch (Route.ObjectFormat) {
    case BuiltinObjectFormat::MachO:
      Section.Name = "__thread_data";
      break;
    case BuiltinObjectFormat::COFF:
      Section.Name = ".tls$AAA";
      break;
    case BuiltinObjectFormat::ELF:
      Section.Name = ".tdata";
      break;
    }
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
    Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED |
                    NEVERC_OBJECT_SECTION_WRITABLE | NEVERC_OBJECT_SECTION_TLS;
    Section.Alignment = 8;
    Section.Data.assign(8, UINT8_C(0));
    const uint64_t SectionID = Section.ID;
    Graph.sections().push_back(std::move(Section));
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph.allocateEntityID();
    Symbol.Name = Route.ObjectFormat == BuiltinObjectFormat::MachO
                      ? "_roundtrip_tls"
                      : "roundtrip_tls";
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_TLS;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = SectionID;
    Symbol.Size = 8;
    Symbol.Alignment = 8;
    Graph.symbols().push_back(std::move(Symbol));
    Graph.issueLayoutProof();

    const std::string OutputName =
        "tls-" + std::to_string(Tested.size()) + ".o";
    auto Candidate = (*Writer)->write(
        Scope.task(), Graph,
        ObjectOutputDestination::memory(OutputName, UINT64_C(1) << 20));
    ASSERT_TRUE(static_cast<bool>(Candidate))
        << errorText(Candidate.takeError());
    ASSERT_FALSE((*Candidate)->verify());
    auto Committed = (*Candidate)->commit();
    ASSERT_TRUE(static_cast<bool>(Committed))
        << errorText(Committed.takeError());
    auto Output = findPluginMemoryOutput(Scope.task(), OutputName);
    ASSERT_TRUE(Output.has_value());
    auto Restored =
        (*Reader)->read(Scope.task(), Output->Bytes, OutputName, ReadTarget);
    ASSERT_TRUE(static_cast<bool>(Restored)) << errorText(Restored.takeError());
    const auto TLSSection = llvm::find_if(
        (*Restored)->sections(), [](const PluginObjectSection &Value) {
          return Value.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
        });
    ASSERT_NE(TLSSection, (*Restored)->sections().end());
    EXPECT_EQ(TLSSection->Flags & NEVERC_OBJECT_SECTION_TLS,
              NEVERC_OBJECT_SECTION_TLS);
    const auto TLSSymbol = llvm::find_if(
        (*Restored)->symbols(), [](const PluginObjectSymbol &Value) {
          return StringRef(Value.Name).find("roundtrip_tls") != StringRef::npos;
        });
    ASSERT_NE(TLSSymbol, (*Restored)->symbols().end());
    EXPECT_EQ(TLSSymbol->Type, NEVERC_OBJECT_SYMBOL_TYPE_TLS);
  }
  EXPECT_EQ(Tested.size(), 6U);
}

TEST(PluginBuiltinObjectFormatTest,
     ReadWriteReadPreservesELFAndCOFFComdatMembership) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());

  std::set<std::pair<Triple::ArchType, BuiltinObjectFormat>> Tested;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    if (Route.ObjectFormat == BuiltinObjectFormat::MachO)
      continue;
    Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    const auto RouteKey = std::make_pair(Parsed.getArch(), Route.ObjectFormat);
    if (!Route.SupportsObject || !Tested.insert(RouteKey).second)
      continue;
    SCOPED_TRACE(Route.CanonicalName.str());

    auto Target = makeBuiltinTargetKey(Route);
    ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
    OwnedTargetKey ReadTarget = *Target;
    PluginObjectGraph Graph(std::move(*Target));
    PluginObjectComdat Comdat;
    Comdat.ID = Graph.allocateEntityID();
    Comdat.Name = "roundtrip_comdat";
    Comdat.Selection = NEVERC_OBJECT_COMDAT_ANY;
    const uint64_t ComdatID = Comdat.ID;
    Graph.comdats().push_back(std::move(Comdat));
    PluginObjectSection Section;
    Section.ID = Graph.allocateEntityID();
    Section.Name = Route.ObjectFormat == BuiltinObjectFormat::ELF
                       ? ".text.roundtrip_comdat"
                       : ".text$roundtrip_comdat";
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Section.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    Section.Alignment = 1;
    Section.Data = {UINT8_C(0xc3)};
    Section.ComdatID = ComdatID;
    const uint64_t SectionID = Section.ID;
    Graph.sections().push_back(std::move(Section));
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph.allocateEntityID();
    Symbol.Name = "roundtrip_comdat";
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = SectionID;
    Symbol.Size = 1;
    Symbol.Alignment = 1;
    Symbol.ComdatID = ComdatID;
    Graph.symbols().push_back(std::move(Symbol));
    Graph.issueLayoutProof();

    const std::string OutputName =
        "comdat-" + std::to_string(Tested.size()) + ".o";
    auto Candidate = (*Writer)->write(
        Scope.task(), Graph,
        ObjectOutputDestination::memory(OutputName, UINT64_C(1) << 20));
    ASSERT_TRUE(static_cast<bool>(Candidate))
        << errorText(Candidate.takeError());
    ASSERT_FALSE((*Candidate)->verify());
    auto Committed = (*Candidate)->commit();
    ASSERT_TRUE(static_cast<bool>(Committed))
        << errorText(Committed.takeError());
    auto Output = findPluginMemoryOutput(Scope.task(), OutputName);
    ASSERT_TRUE(Output.has_value());
    auto Restored =
        (*Reader)->read(Scope.task(), Output->Bytes, OutputName, ReadTarget);
    ASSERT_TRUE(static_cast<bool>(Restored)) << errorText(Restored.takeError());
    ASSERT_EQ((*Restored)->comdatCount(), 1U);
    const PluginObjectComdat &RestoredComdat = (*Restored)->comdats().front();
    EXPECT_EQ(RestoredComdat.Name, "roundtrip_comdat");
    EXPECT_EQ(RestoredComdat.Selection, NEVERC_OBJECT_COMDAT_ANY);
    const auto Member = llvm::find_if(
        (*Restored)->sections(), [](const PluginObjectSection &Value) {
          return Value.Name.find("roundtrip_comdat") != std::string::npos;
        });
    ASSERT_NE(Member, (*Restored)->sections().end());
    EXPECT_EQ(Member->ComdatID, RestoredComdat.ID);
  }
  EXPECT_EQ(Tested.size(), 4U);
}

Expected<std::vector<uint8_t>> assembleSource(const BuiltinTargetRoute &Route,
                                              StringRef Assembly) {
  auto Target = lookupBuiltinLLVMTarget(Route);
  if (!Target)
    return Target.takeError();
  const Triple TargetTriple(Triple::normalize(Route.CanonicalTriple));
  SmallVector<char, 0> Bytes;
  raw_svector_ostream Output(Bytes);
  BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple = TargetTriple;
  Request.CPU = Route.DefaultCPU;
  Request.Input = MemoryBufferRef(Assembly, "<builtin-object-source>");
  Request.Output = &Output;
  if (Error E = runBuiltinLLVMAsmParser(Request))
    return std::move(E);
  return std::vector<uint8_t>(Bytes.begin(), Bytes.end());
}

const BuiltinTargetRoute *routeFor(BuiltinObjectFormat Format,
                                   Triple::ArchType Arch) {
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject && Route.ObjectFormat == Format &&
        Parsed.getArch() == Arch)
      return &Route;
  }
  return nullptr;
}

constexpr StringLiteral MixedDebugGroupAssembly = R"(
  .section .text.mixed_group,"axG",%progbits,mixed_group_signature,comdat
  .globl mixed_group_signature
  .type mixed_group_signature, %function
mixed_group_signature:
  .byte 0

  .section .debug_info.mixed_group,"G",%progbits,mixed_group_signature,comdat
  .byte 1
)";

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesPreservesELFComdatMembership) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  auto Target = makeBuiltinTargetKey(*Route);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  OwnedTargetKey ReadTarget = *Target;
  PluginObjectGraph Graph(std::move(*Target));

  PluginObjectComdat Comdat;
  Comdat.ID = Graph.allocateEntityID();
  Comdat.Name = "canonical_comdat";
  Comdat.Selection = NEVERC_OBJECT_COMDAT_ANY;
  const uint64_t ComdatID = Comdat.ID;
  Graph.comdats().push_back(std::move(Comdat));

  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text.canonical_comdat";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 4;
  Section.Data = {UINT8_C(0xc0), UINT8_C(0x03), UINT8_C(0x5f), UINT8_C(0xd6)};
  Section.ComdatID = ComdatID;
  const uint64_t SectionID = Section.ID;
  Graph.sections().push_back(std::move(Section));

  PluginObjectSymbol Symbol;
  Symbol.ID = Graph.allocateEntityID();
  Symbol.Name = "canonical_comdat";
  Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Symbol.SectionID = SectionID;
  Symbol.Size = 4;
  Symbol.Alignment = 4;
  Symbol.ComdatID = ComdatID;
  Graph.symbols().push_back(std::move(Symbol));
  Graph.issueLayoutProof();

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  ObjectOutputDestination Destination =
      ObjectOutputDestination::memory("canonical-comdat.o", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::CanonicalELFTables;
  auto Image = (*Writer)->beginWrite(Scope.task(), Graph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Bytes = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Bytes)) << errorText(Bytes.takeError());

  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Restored =
      (*Reader)->read(Scope.task(), *Bytes, "canonical-comdat.o", ReadTarget);
  ASSERT_TRUE(static_cast<bool>(Restored)) << errorText(Restored.takeError());
  ASSERT_EQ((*Restored)->comdatCount(), 1U);
  EXPECT_EQ((*Restored)->comdats().front().Name, "canonical_comdat");
  const auto Member = llvm::find_if(
      (*Restored)->sections(), [](const PluginObjectSection &Value) {
        return Value.Name == ".text.canonical_comdat";
      });
  ASSERT_NE(Member, (*Restored)->sections().end());
  EXPECT_EQ(Member->ComdatID, (*Restored)->comdats().front().ID);
  EXPECT_FALSE((*Image)->abort());
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesDoesNotMaterializeNobitsAlignmentPadding) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .bss
    .balign 8
    .globl canonical_large_aligned_bss
    .type canonical_large_aligned_bss, %object
canonical_large_aligned_bss:
    .zero 8
    .size canonical_large_aligned_bss, 8
)";
  auto Input = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

  const StringRef Original(reinterpret_cast<const char *>(Input->data()),
                           Input->size());
  auto ELFFile = object::ELFFile<object::ELF64LE>::create(Original);
  ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
  auto Sections = ELFFile->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());
  const object::ELF64LE::Shdr *BSS = nullptr;
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    auto Name = ELFFile->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name == ".bss")
      BSS = &Section;
  }
  ASSERT_NE(BSS, nullptr);
  ASSERT_EQ(BSS->sh_type, ELF::SHT_NOBITS);
  ASSERT_EQ(BSS->sh_size, 8U);

  // This alignment is legal in ELF but must remain conceptual for SHT_NOBITS:
  // materializing it as file padding would attempt a roughly 2 GiB allocation.
  constexpr uint64_t ProbeAlignment = UINT64_C(1) << 31;
  object::ELF64LE::Shdr Replacement = *BSS;
  Replacement.sh_addralign = ProbeAlignment;
  const auto *Raw = reinterpret_cast<const uint8_t *>(BSS);
  ASSERT_GE(Raw, Input->data());
  const size_t HeaderOffset = static_cast<size_t>(Raw - Input->data());
  ASSERT_LE(HeaderOffset, Input->size());
  ASSERT_LE(sizeof(Replacement), Input->size() - HeaderOffset);
  std::memcpy(Input->data() + HeaderOffset, &Replacement, sizeof(Replacement));

  const StringRef Mutated(reinterpret_cast<const char *>(Input->data()),
                          Input->size());
  auto Canonical = canonicalizeBuiltinELFTables(Mutated);
  ASSERT_TRUE(static_cast<bool>(Canonical)) << errorText(Canonical.takeError());
  EXPECT_LT(Canonical->size(), UINT64_C(1) << 16)
      << "SHT_NOBITS alignment must not become physical file padding";

  const StringRef Output(Canonical->data(), Canonical->size());
  auto CanonicalELF = object::ELFFile<object::ELF64LE>::create(Output);
  ASSERT_TRUE(static_cast<bool>(CanonicalELF))
      << errorText(CanonicalELF.takeError());
  auto CanonicalSections = CanonicalELF->sections();
  ASSERT_TRUE(static_cast<bool>(CanonicalSections))
      << errorText(CanonicalSections.takeError());
  bool SawAlignedBSS = false;
  for (const object::ELF64LE::Shdr &Section : *CanonicalSections) {
    auto Name = CanonicalELF->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name != ".bss")
      continue;
    EXPECT_EQ(Section.sh_type, ELF::SHT_NOBITS);
    EXPECT_EQ(Section.sh_size, 8U);
    EXPECT_EQ(Section.sh_addralign, ProbeAlignment);
    EXPECT_EQ(Section.sh_offset % ProbeAlignment, 0U);
    SawAlignedBSS = true;
  }
  EXPECT_TRUE(SawAlignedBSS);
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesRejectsIncompatibleELFIdentityAndVersion) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  auto Input = assembleSource(*Route, R"(
    .text
    .globl canonical_header_probe
canonical_header_probe:
    .byte 0
  )");
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  ASSERT_GE(Input->size(), sizeof(object::ELF64LE::Ehdr));

  enum class Field {
    Magic,
    Class,
    Data,
    IdentVersion,
    HeaderVersion,
  };
  for (Field TestField : {Field::Magic, Field::Class, Field::Data,
                          Field::IdentVersion, Field::HeaderVersion}) {
    SCOPED_TRACE(static_cast<unsigned>(TestField));
    std::vector<uint8_t> Corrupted = *Input;
    object::ELF64LE::Ehdr Header{};
    std::memcpy(&Header, Corrupted.data(), sizeof(Header));
    switch (TestField) {
    case Field::Magic:
      Header.e_ident[ELF::EI_MAG0] ^= UINT8_C(1);
      break;
    case Field::Class:
      Header.e_ident[ELF::EI_CLASS] = ELF::ELFCLASS32;
      break;
    case Field::Data:
      Header.e_ident[ELF::EI_DATA] = ELF::ELFDATA2MSB;
      break;
    case Field::IdentVersion:
      Header.e_ident[ELF::EI_VERSION] = ELF::EV_NONE;
      break;
    case Field::HeaderVersion:
      Header.e_version = ELF::EV_NONE;
      break;
    }
    std::memcpy(Corrupted.data(), &Header, sizeof(Header));

    const StringRef Image(reinterpret_cast<const char *>(Corrupted.data()),
                          Corrupted.size());
    auto Canonical = canonicalizeBuiltinELFTables(Image);
    EXPECT_FALSE(static_cast<bool>(Canonical));
    if (!Canonical)
      consumeError(Canonical.takeError());
  }
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesRejectsDroppingAGroupThatOwnsRetainedMembers) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  auto Input = assembleSource(*Route, MixedDebugGroupAssembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

  const StringRef Original(reinterpret_cast<const char *>(Input->data()),
                           Input->size());
  auto ELFFile = object::ELFFile<object::ELF64LE>::create(Original);
  ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
  auto Sections = ELFFile->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());
  const object::ELF64LE::Shdr *Group = nullptr;
  const object::ELF64LE::Shdr *DebugMember = nullptr;
  const object::ELF64LE::Shdr *TextMember = nullptr;
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    auto Name = ELFFile->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (Section.sh_type == ELF::SHT_GROUP)
      Group = &Section;
    if (*Name == ".debug_info.mixed_group")
      DebugMember = &Section;
    if (*Name == ".text.mixed_group")
      TextMember = &Section;
  }
  ASSERT_NE(Group, nullptr);
  ASSERT_NE(DebugMember, nullptr);
  ASSERT_NE(TextMember, nullptr);
  ASSERT_NE(TextMember->sh_flags & ELF::SHF_GROUP, 0U);
  ASSERT_NE(DebugMember->sh_flags & ELF::SHF_GROUP, 0U);

  // Make the group metadata itself debug-named while leaving one ordinary
  // member. Silently dropping this SHT_GROUP would orphan the retained text
  // section and corrupt its COMDAT semantics.
  object::ELF64LE::Shdr Replacement = *Group;
  Replacement.sh_name = DebugMember->sh_name;
  const auto *Raw = reinterpret_cast<const uint8_t *>(Group);
  ASSERT_GE(Raw, Input->data());
  const size_t HeaderOffset = static_cast<size_t>(Raw - Input->data());
  ASSERT_LE(HeaderOffset, Input->size());
  ASSERT_LE(sizeof(Replacement), Input->size() - HeaderOffset);
  std::memcpy(Input->data() + HeaderOffset, &Replacement, sizeof(Replacement));

  const StringRef Mutated(reinterpret_cast<const char *>(Input->data()),
                          Input->size());
  auto Canonical = canonicalizeBuiltinELFTables(Mutated, true);
  EXPECT_FALSE(static_cast<bool>(Canonical));
  if (!Canonical) {
    const std::string Message = errorText(Canonical.takeError());
    EXPECT_NE(Message.find("SHT_GROUP"), std::string::npos) << Message;
    EXPECT_NE(Message.find("retained"), std::string::npos) << Message;
  }
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesDropDebugKeepsMixedComdatMembershipClosed) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  auto Input = assembleSource(*Route, MixedDebugGroupAssembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

  const StringRef Image(reinterpret_cast<const char *>(Input->data()),
                        Input->size());
  auto Canonical = canonicalizeBuiltinELFTables(Image, true);
  ASSERT_TRUE(static_cast<bool>(Canonical)) << errorText(Canonical.takeError());
  const StringRef Output(Canonical->data(), Canonical->size());
  auto ELFFile = object::ELFFile<object::ELF64LE>::create(Output);
  ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
  auto Sections = ELFFile->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());

  const object::ELF64LE::Shdr *Group = nullptr;
  std::optional<uint32_t> TextIndex;
  for (uint32_t Index = 1; Index != Sections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*Sections)[Index];
    auto Name = ELFFile->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    EXPECT_NE(*Name, ".debug_info.mixed_group");
    if (Section.sh_type == ELF::SHT_GROUP)
      Group = &Section;
    if (*Name == ".text.mixed_group")
      TextIndex = Index;
  }
  ASSERT_NE(Group, nullptr);
  ASSERT_TRUE(TextIndex.has_value());
  EXPECT_NE((*Sections)[*TextIndex].sh_flags & ELF::SHF_GROUP, 0U);

  auto Members =
      ELFFile->template getSectionContentsAsArray<object::ELF64LE::Word>(
          *Group);
  ASSERT_TRUE(static_cast<bool>(Members)) << errorText(Members.takeError());
  ASSERT_EQ(Members->size(), 2U);
  EXPECT_EQ((*Members)[0], ELF::GRP_COMDAT);
  EXPECT_EQ((*Members)[1], *TextIndex);

  unsigned MembershipCount = 0;
  for (const object::ELF64LE::Shdr &Candidate : *Sections) {
    if (Candidate.sh_type != ELF::SHT_GROUP)
      continue;
    auto CandidateMembers =
        ELFFile->template getSectionContentsAsArray<object::ELF64LE::Word>(
            Candidate);
    ASSERT_TRUE(static_cast<bool>(CandidateMembers))
        << errorText(CandidateMembers.takeError());
    MembershipCount += llvm::count(CandidateMembers->drop_front(), *TextIndex);
  }
  EXPECT_EQ(MembershipCount, 1U);
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesRejectsDebugNamedRelaForRetainedTarget) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .text
    .globl retained_rela_symbol
    .type retained_rela_symbol, %function
retained_rela_symbol:
    .byte 0

    .section .data.retained_rela_target,"aw",%progbits
    .xword retained_rela_symbol

    .section .debug_info.relocation_alias,"",%progbits
    .byte 0
)";
  auto Input = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

  const StringRef Original(reinterpret_cast<const char *>(Input->data()),
                           Input->size());
  auto ELFFile = object::ELFFile<object::ELF64LE>::create(Original);
  ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
  auto Sections = ELFFile->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());

  const object::ELF64LE::Shdr *Rela = nullptr;
  std::optional<uint32_t> TargetIndex;
  std::optional<uint32_t> DebugNameOffset;
  for (uint32_t Index = 1; Index != Sections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*Sections)[Index];
    auto Name = ELFFile->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name == ".data.retained_rela_target")
      TargetIndex = Index;
    if (*Name == ".debug_info.relocation_alias")
      DebugNameOffset = Section.sh_name;
  }
  ASSERT_TRUE(TargetIndex.has_value());
  ASSERT_TRUE(DebugNameOffset.has_value());
  for (const object::ELF64LE::Shdr &Section : *Sections)
    if (Section.sh_type == ELF::SHT_RELA && Section.sh_info == *TargetIndex)
      Rela = &Section;
  ASSERT_NE(Rela, nullptr);

  object::ELF64LE::Shdr Replacement = *Rela;
  Replacement.sh_name = *DebugNameOffset;
  const auto *Raw = reinterpret_cast<const uint8_t *>(Rela);
  ASSERT_GE(Raw, Input->data());
  const size_t HeaderOffset = static_cast<size_t>(Raw - Input->data());
  ASSERT_LE(HeaderOffset, Input->size());
  ASSERT_LE(sizeof(Replacement), Input->size() - HeaderOffset);
  std::memcpy(Input->data() + HeaderOffset, &Replacement, sizeof(Replacement));

  const StringRef Mutated(reinterpret_cast<const char *>(Input->data()),
                          Input->size());
  auto Preserved = canonicalizeBuiltinELFTables(Mutated);
  ASSERT_TRUE(static_cast<bool>(Preserved)) << errorText(Preserved.takeError());
  const StringRef PreservedImage(Preserved->data(), Preserved->size());
  auto PreservedELF = object::ELFFile<object::ELF64LE>::create(PreservedImage);
  ASSERT_TRUE(static_cast<bool>(PreservedELF))
      << errorText(PreservedELF.takeError());
  auto PreservedSections = PreservedELF->sections();
  ASSERT_TRUE(static_cast<bool>(PreservedSections))
      << errorText(PreservedSections.takeError());
  bool SawRetainedTargetRela = false;
  for (const object::ELF64LE::Shdr &Section : *PreservedSections) {
    if (Section.sh_type != ELF::SHT_RELA)
      continue;
    ASSERT_LT(Section.sh_info, PreservedSections->size());
    auto TargetName =
        PreservedELF->getSectionName((*PreservedSections)[Section.sh_info]);
    ASSERT_TRUE(static_cast<bool>(TargetName))
        << errorText(TargetName.takeError());
    SawRetainedTargetRela |= *TargetName == ".data.retained_rela_target";
  }
  EXPECT_TRUE(SawRetainedTargetRela);
  auto Recanonical = canonicalizeBuiltinELFTables(PreservedImage);
  ASSERT_TRUE(static_cast<bool>(Recanonical))
      << (Recanonical ? std::string() : errorText(Recanonical.takeError()));
  EXPECT_EQ(StringRef(Recanonical->data(), Recanonical->size()),
            PreservedImage);

  auto Dropped = canonicalizeBuiltinELFTables(Mutated, true);
  EXPECT_FALSE(static_cast<bool>(Dropped));
  if (!Dropped) {
    const std::string Message = errorText(Dropped.takeError());
    EXPECT_NE(Message.find("relocation"), std::string::npos) << Message;
    EXPECT_NE(Message.find("retained"), std::string::npos) << Message;
  }
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesRejectsDebugNamedRelForRetainedTarget) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .text
    .globl retained_rel_symbol
    .type retained_rel_symbol, %function
retained_rel_symbol:
    .byte 0

    .section .data.retained_rel_target,"aw",%progbits
    .xword 0

    .section .synthetic.retained_rel,"",%progbits
    .zero 16

    .section .debug_info.rel_alias,"",%progbits
    .byte 0
)";
  auto Input = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

  const StringRef Original(reinterpret_cast<const char *>(Input->data()),
                           Input->size());
  auto ELFFile = object::ELFFile<object::ELF64LE>::create(Original);
  ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
  auto Sections = ELFFile->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());

  const object::ELF64LE::Shdr *Symtab = nullptr;
  const object::ELF64LE::Shdr *SyntheticRel = nullptr;
  std::optional<uint32_t> SymtabIndex;
  std::optional<uint32_t> TargetIndex;
  std::optional<uint32_t> DebugNameOffset;
  for (uint32_t Index = 1; Index != Sections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*Sections)[Index];
    auto Name = ELFFile->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (Section.sh_type == ELF::SHT_SYMTAB) {
      Symtab = &Section;
      SymtabIndex = Index;
    }
    if (*Name == ".synthetic.retained_rel")
      SyntheticRel = &Section;
    if (*Name == ".data.retained_rel_target")
      TargetIndex = Index;
    if (*Name == ".debug_info.rel_alias")
      DebugNameOffset = Section.sh_name;
  }
  ASSERT_NE(Symtab, nullptr);
  ASSERT_NE(SyntheticRel, nullptr);
  ASSERT_TRUE(SymtabIndex.has_value());
  ASSERT_TRUE(TargetIndex.has_value());
  ASSERT_TRUE(DebugNameOffset.has_value());

  auto Symbols = ELFFile->symbols(Symtab);
  auto Strings = ELFFile->getStringTableForSymtab(*Symtab);
  ASSERT_TRUE(static_cast<bool>(Symbols)) << errorText(Symbols.takeError());
  ASSERT_TRUE(static_cast<bool>(Strings)) << errorText(Strings.takeError());
  std::optional<uint32_t> SymbolIndex;
  for (uint32_t Index = 0; Index != Symbols->size(); ++Index) {
    auto Name = (*Symbols)[Index].getName(*Strings);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name == "retained_rel_symbol")
      SymbolIndex = Index;
  }
  ASSERT_TRUE(SymbolIndex.has_value());

  const uint64_t ContentsOffset = SyntheticRel->sh_offset;
  object::ELF64LE::Shdr RelHeader = *SyntheticRel;
  RelHeader.sh_name = *DebugNameOffset;
  RelHeader.sh_type = ELF::SHT_REL;
  RelHeader.sh_link = *SymtabIndex;
  RelHeader.sh_info = *TargetIndex;
  RelHeader.sh_size = sizeof(object::ELF64LE::Rel);
  RelHeader.sh_addralign = alignof(object::ELF64LE::Rel);
  RelHeader.sh_entsize = sizeof(object::ELF64LE::Rel);
  const auto *Raw = reinterpret_cast<const uint8_t *>(SyntheticRel);
  ASSERT_GE(Raw, Input->data());
  const size_t HeaderOffset = static_cast<size_t>(Raw - Input->data());
  ASSERT_LE(HeaderOffset, Input->size());
  ASSERT_LE(sizeof(RelHeader), Input->size() - HeaderOffset);
  std::memcpy(Input->data() + HeaderOffset, &RelHeader, sizeof(RelHeader));
  object::ELF64LE::Rel Relocation{};
  Relocation.r_offset = 0;
  Relocation.setSymbol(*SymbolIndex);
  Relocation.setType(ELF::R_AARCH64_ABS64);
  ASSERT_LE(ContentsOffset, Input->size());
  ASSERT_LE(sizeof(Relocation), Input->size() - ContentsOffset);
  std::memcpy(Input->data() + ContentsOffset, &Relocation, sizeof(Relocation));

  const StringRef Mutated(reinterpret_cast<const char *>(Input->data()),
                          Input->size());
  auto Preserved = canonicalizeBuiltinELFTables(Mutated);
  ASSERT_TRUE(static_cast<bool>(Preserved)) << errorText(Preserved.takeError());
  const StringRef PreservedImage(Preserved->data(), Preserved->size());
  auto PreservedELF = object::ELFFile<object::ELF64LE>::create(PreservedImage);
  ASSERT_TRUE(static_cast<bool>(PreservedELF))
      << errorText(PreservedELF.takeError());
  auto PreservedSections = PreservedELF->sections();
  ASSERT_TRUE(static_cast<bool>(PreservedSections))
      << errorText(PreservedSections.takeError());
  bool SawRetainedTargetRel = false;
  for (const object::ELF64LE::Shdr &Section : *PreservedSections) {
    if (Section.sh_type != ELF::SHT_REL)
      continue;
    ASSERT_LT(Section.sh_info, PreservedSections->size());
    auto TargetName =
        PreservedELF->getSectionName((*PreservedSections)[Section.sh_info]);
    ASSERT_TRUE(static_cast<bool>(TargetName))
        << errorText(TargetName.takeError());
    SawRetainedTargetRel |= *TargetName == ".data.retained_rel_target";
  }
  EXPECT_TRUE(SawRetainedTargetRel);
  auto Recanonical = canonicalizeBuiltinELFTables(PreservedImage);
  ASSERT_TRUE(static_cast<bool>(Recanonical))
      << (Recanonical ? std::string() : errorText(Recanonical.takeError()));
  EXPECT_EQ(StringRef(Recanonical->data(), Recanonical->size()),
            PreservedImage);

  auto Dropped = canonicalizeBuiltinELFTables(Mutated, true);
  EXPECT_FALSE(static_cast<bool>(Dropped));
  if (!Dropped) {
    const std::string Message = errorText(Dropped.takeError());
    EXPECT_NE(Message.find("relocation"), std::string::npos) << Message;
    EXPECT_NE(Message.find("retained"), std::string::npos) << Message;
  }
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesRejectsDebugNamesOnRetainedStructuralSemantics) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .text
    .globl structural_profile_from
    .type structural_profile_from, %function
structural_profile_from:
    .byte 0
    .globl structural_profile_to
    .type structural_profile_to, %function
structural_profile_to:
    .byte 0
    .cg_profile structural_profile_from, structural_profile_to, 17

    .section .text.structural_group,"axG",%progbits,structural_group_key,comdat
    .globl structural_group_key
    .type structural_group_key, %function
structural_group_key:
    .byte 0

    .section .candidate.link_order,"",%progbits
    .byte 0
    .section .candidate.info_link,"",%progbits
    .byte 0
    .section .debug_info.structural_alias,"",%progbits
    .byte 0

    .addrsig
    .addrsig_sym structural_profile_from
)";
  auto Assembled = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Assembled)) << errorText(Assembled.takeError());

  enum class Semantic {
    Allocated,
    Group,
    OpaqueLink,
    LinkOrder,
    InfoLink,
    AddrSig,
    CallGraphProfile,
  };
  for (Semantic Case :
       {Semantic::Allocated, Semantic::Group, Semantic::OpaqueLink,
        Semantic::LinkOrder, Semantic::InfoLink, Semantic::AddrSig,
        Semantic::CallGraphProfile}) {
    SCOPED_TRACE(static_cast<unsigned>(Case));
    std::vector<uint8_t> Bytes = *Assembled;
    const StringRef Image(reinterpret_cast<const char *>(Bytes.data()),
                          Bytes.size());
    auto ELFFile = object::ELFFile<object::ELF64LE>::create(Image);
    ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
    auto Sections = ELFFile->sections();
    ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());

    const object::ELF64LE::Shdr *Text = nullptr;
    const object::ELF64LE::Shdr *Group = nullptr;
    const object::ELF64LE::Shdr *LinkOrder = nullptr;
    const object::ELF64LE::Shdr *InfoLink = nullptr;
    const object::ELF64LE::Shdr *AddrSig = nullptr;
    const object::ELF64LE::Shdr *Profile = nullptr;
    std::optional<uint32_t> TextIndex;
    std::optional<uint32_t> DebugNameOffset;
    for (uint32_t Index = 1; Index != Sections->size(); ++Index) {
      const object::ELF64LE::Shdr &Section = (*Sections)[Index];
      auto Name = ELFFile->getSectionName(Section);
      ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
      if (*Name == ".text") {
        Text = &Section;
        TextIndex = Index;
      }
      if (Section.sh_type == ELF::SHT_GROUP)
        Group = &Section;
      if (*Name == ".candidate.link_order")
        LinkOrder = &Section;
      if (*Name == ".candidate.info_link")
        InfoLink = &Section;
      if (*Name == ".debug_info.structural_alias")
        DebugNameOffset = Section.sh_name;
      if (Section.sh_type == ELF::SHT_LLVM_ADDRSIG)
        AddrSig = &Section;
      if (Section.sh_type == ELF::SHT_LLVM_CALL_GRAPH_PROFILE)
        Profile = &Section;
    }
    ASSERT_NE(Text, nullptr);
    ASSERT_NE(Group, nullptr);
    ASSERT_NE(LinkOrder, nullptr);
    ASSERT_NE(InfoLink, nullptr);
    ASSERT_NE(AddrSig, nullptr);
    ASSERT_NE(Profile, nullptr);
    ASSERT_TRUE(TextIndex.has_value());
    ASSERT_TRUE(DebugNameOffset.has_value());

    const object::ELF64LE::Shdr *Selected = nullptr;
    StringRef ExpectedDiagnostic = "runtime or structural semantics";
    switch (Case) {
    case Semantic::Allocated:
      Selected = Text;
      break;
    case Semantic::Group:
      Selected = Group;
      ExpectedDiagnostic = "SHT_GROUP";
      break;
    case Semantic::OpaqueLink:
      Selected = LinkOrder;
      break;
    case Semantic::LinkOrder:
      Selected = LinkOrder;
      ExpectedDiagnostic = "SHF_LINK_ORDER";
      break;
    case Semantic::InfoLink:
      Selected = InfoLink;
      ExpectedDiagnostic = "SHF_INFO_LINK";
      break;
    case Semantic::AddrSig:
      Selected = AddrSig;
      break;
    case Semantic::CallGraphProfile:
      Selected = Profile;
      break;
    }
    ASSERT_NE(Selected, nullptr);
    object::ELF64LE::Shdr Replacement = *Selected;
    Replacement.sh_name = *DebugNameOffset;
    if (Case == Semantic::OpaqueLink)
      Replacement.sh_link = *TextIndex;
    if (Case == Semantic::LinkOrder) {
      Replacement.sh_flags |= ELF::SHF_LINK_ORDER;
      Replacement.sh_link = *TextIndex;
    }
    if (Case == Semantic::InfoLink) {
      Replacement.sh_flags |= ELF::SHF_INFO_LINK;
      Replacement.sh_info = *TextIndex;
    }
    const auto *Raw = reinterpret_cast<const uint8_t *>(Selected);
    ASSERT_GE(Raw, Bytes.data());
    const size_t HeaderOffset = static_cast<size_t>(Raw - Bytes.data());
    ASSERT_LE(HeaderOffset, Bytes.size());
    ASSERT_LE(sizeof(Replacement), Bytes.size() - HeaderOffset);
    std::memcpy(Bytes.data() + HeaderOffset, &Replacement, sizeof(Replacement));

    const StringRef Mutated(reinterpret_cast<const char *>(Bytes.data()),
                            Bytes.size());
    auto Preserved = canonicalizeBuiltinELFTables(Mutated);
    ASSERT_TRUE(static_cast<bool>(Preserved))
        << errorText(Preserved.takeError());
    const StringRef PreservedImage(Preserved->data(), Preserved->size());
    auto PreservedELF =
        object::ELFFile<object::ELF64LE>::create(PreservedImage);
    ASSERT_TRUE(static_cast<bool>(PreservedELF))
        << errorText(PreservedELF.takeError());
    auto PreservedSections = PreservedELF->sections();
    ASSERT_TRUE(static_cast<bool>(PreservedSections))
        << errorText(PreservedSections.takeError());
    unsigned DebugAliasCount = 0;
    for (const object::ELF64LE::Shdr &Section : *PreservedSections) {
      auto Name = PreservedELF->getSectionName(Section);
      ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
      DebugAliasCount += *Name == ".debug_info.structural_alias";
    }
    EXPECT_EQ(DebugAliasCount, 2U)
        << "canonicalization without DropDebug must preserve the alias";
    auto Recanonical = canonicalizeBuiltinELFTables(PreservedImage);
    ASSERT_TRUE(static_cast<bool>(Recanonical))
        << (Recanonical ? std::string() : errorText(Recanonical.takeError()));
    EXPECT_EQ(StringRef(Recanonical->data(), Recanonical->size()),
              PreservedImage);

    auto Dropped = canonicalizeBuiltinELFTables(Mutated, true);
    EXPECT_FALSE(static_cast<bool>(Dropped));
    if (!Dropped) {
      const std::string Message = errorText(Dropped.takeError());
      EXPECT_NE(Message.find(ExpectedDiagnostic.str()), std::string::npos)
          << Message;
    }
  }
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesRejectsPropagatedAllocatedSections) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .text
    .globl propagated_alloc_symbol
    .type propagated_alloc_symbol, %function
propagated_alloc_symbol:
    .byte 0

    .section .candidate.propagated_link_order,"",%progbits
    .byte 0
    .section .candidate.propagated_info_link,"",%progbits
    .byte 0
    .section .debug_info.propagated_alloc,"",%progbits
    .xword propagated_alloc_symbol
)";
  auto Assembled = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Assembled)) << errorText(Assembled.takeError());

  enum class Dependency { LinkOrder, InfoLink, RelocationTarget };
  for (Dependency Case : {Dependency::LinkOrder, Dependency::InfoLink,
                          Dependency::RelocationTarget}) {
    SCOPED_TRACE(static_cast<unsigned>(Case));
    std::vector<uint8_t> Bytes = *Assembled;
    const StringRef Image(reinterpret_cast<const char *>(Bytes.data()),
                          Bytes.size());
    auto ELFFile = object::ELFFile<object::ELF64LE>::create(Image);
    ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
    auto Sections = ELFFile->sections();
    ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());

    const object::ELF64LE::Shdr *LinkOrder = nullptr;
    const object::ELF64LE::Shdr *InfoLink = nullptr;
    const object::ELF64LE::Shdr *DebugRelocation = nullptr;
    std::optional<uint32_t> DebugIndex;
    for (uint32_t Index = 1; Index != Sections->size(); ++Index) {
      const object::ELF64LE::Shdr &Section = (*Sections)[Index];
      auto Name = ELFFile->getSectionName(Section);
      ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
      if (*Name == ".candidate.propagated_link_order")
        LinkOrder = &Section;
      if (*Name == ".candidate.propagated_info_link")
        InfoLink = &Section;
      if (*Name == ".debug_info.propagated_alloc")
        DebugIndex = Index;
    }
    ASSERT_NE(LinkOrder, nullptr);
    ASSERT_NE(InfoLink, nullptr);
    ASSERT_TRUE(DebugIndex.has_value());
    for (const object::ELF64LE::Shdr &Section : *Sections)
      if (Section.sh_type == ELF::SHT_RELA && Section.sh_info == *DebugIndex)
        DebugRelocation = &Section;
    ASSERT_NE(DebugRelocation, nullptr);

    const object::ELF64LE::Shdr *Selected = nullptr;
    switch (Case) {
    case Dependency::LinkOrder:
      Selected = LinkOrder;
      break;
    case Dependency::InfoLink:
      Selected = InfoLink;
      break;
    case Dependency::RelocationTarget:
      Selected = DebugRelocation;
      break;
    }
    ASSERT_NE(Selected, nullptr);
    object::ELF64LE::Shdr Replacement = *Selected;
    Replacement.sh_flags |= ELF::SHF_ALLOC;
    if (Case == Dependency::LinkOrder) {
      Replacement.sh_flags |= ELF::SHF_LINK_ORDER;
      Replacement.sh_link = *DebugIndex;
    }
    if (Case == Dependency::InfoLink) {
      Replacement.sh_flags |= ELF::SHF_INFO_LINK;
      Replacement.sh_info = *DebugIndex;
    }
    const auto *Raw = reinterpret_cast<const uint8_t *>(Selected);
    ASSERT_GE(Raw, Bytes.data());
    const size_t HeaderOffset = static_cast<size_t>(Raw - Bytes.data());
    ASSERT_LE(HeaderOffset, Bytes.size());
    ASSERT_LE(sizeof(Replacement), Bytes.size() - HeaderOffset);
    std::memcpy(Bytes.data() + HeaderOffset, &Replacement, sizeof(Replacement));

    const StringRef Mutated(reinterpret_cast<const char *>(Bytes.data()),
                            Bytes.size());
    auto Preserved = canonicalizeBuiltinELFTables(Mutated);
    ASSERT_TRUE(static_cast<bool>(Preserved))
        << errorText(Preserved.takeError());
    const StringRef PreservedImage(Preserved->data(), Preserved->size());
    auto Recanonical = canonicalizeBuiltinELFTables(PreservedImage);
    ASSERT_TRUE(static_cast<bool>(Recanonical))
        << (Recanonical ? std::string() : errorText(Recanonical.takeError()));
    EXPECT_EQ(StringRef(Recanonical->data(), Recanonical->size()),
              PreservedImage)
        << "canonicalization without DropDebug must preserve the dependency";

    auto Dropped = canonicalizeBuiltinELFTables(Mutated, true);
    EXPECT_FALSE(static_cast<bool>(Dropped));
    if (!Dropped) {
      const std::string Message = errorText(Dropped.takeError());
      EXPECT_NE(Message.find("allocated"), std::string::npos) << Message;
    }
  }
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesRejectsPropagatedRequiredTables) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .text
    .globl propagated_required_symbol_0
    .type propagated_required_symbol_0, %function
propagated_required_symbol_0:
    .byte 0
    .globl propagated_required_symbol_1
propagated_required_symbol_1:
    .byte 0
    .globl propagated_required_symbol_2
propagated_required_symbol_2:
    .byte 0
    .globl propagated_required_symbol_3
propagated_required_symbol_3:
    .byte 0
    .globl propagated_required_symbol_4
propagated_required_symbol_4:
    .byte 0
    .globl propagated_required_symbol_5
propagated_required_symbol_5:
    .byte 0
    .globl propagated_required_symbol_6
propagated_required_symbol_6:
    .byte 0
    .globl propagated_required_symbol_7
propagated_required_symbol_7:
    .byte 0

    .section .debug_info.propagated_required,"",%progbits
    .byte 1
)";
  auto Input = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

  const StringRef Image(reinterpret_cast<const char *>(Input->data()),
                        Input->size());
  auto Canonical = canonicalizeBuiltinELFTables(Image);
  ASSERT_TRUE(static_cast<bool>(Canonical)) << errorText(Canonical.takeError());
  const std::vector<uint8_t> BaseBytes(Canonical->begin(), Canonical->end());

  enum class Table { Symbols, SymbolStrings, SectionStrings };
  for (Table Case :
       {Table::Symbols, Table::SymbolStrings, Table::SectionStrings}) {
    SCOPED_TRACE(static_cast<unsigned>(Case));
    std::vector<uint8_t> Bytes = BaseBytes;
    const StringRef CanonicalImage(reinterpret_cast<const char *>(Bytes.data()),
                                   Bytes.size());
    auto ELFFile = object::ELFFile<object::ELF64LE>::create(CanonicalImage);
    ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
    auto Sections = ELFFile->sections();
    ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());

    const object::ELF64LE::Shdr *Symtab = nullptr;
    std::optional<uint32_t> DebugIndex;
    for (uint32_t Index = 1; Index != Sections->size(); ++Index) {
      const object::ELF64LE::Shdr &Section = (*Sections)[Index];
      auto Name = ELFFile->getSectionName(Section);
      ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
      if (*Name == ".debug_info.propagated_required")
        DebugIndex = Index;
      if (Section.sh_type == ELF::SHT_SYMTAB)
        Symtab = &Section;
    }
    ASSERT_NE(Symtab, nullptr);
    ASSERT_TRUE(DebugIndex.has_value());
    uint32_t SectionStringIndex = ELFFile->getHeader().e_shstrndx;
    if (SectionStringIndex == ELF::SHN_XINDEX)
      SectionStringIndex = (*Sections)[0].sh_link;
    ASSERT_GT(SectionStringIndex, 0U);
    ASSERT_LT(SectionStringIndex, Sections->size());
    ASSERT_GT(Symtab->sh_link, 0U);
    ASSERT_LT(Symtab->sh_link, Sections->size());
    ASSERT_NE(Symtab->sh_link, SectionStringIndex)
        << "the canonical fixture must have distinct required string tables";

    uint32_t SymbolTableIndex = 0;
    const auto *RawSymtab = reinterpret_cast<const uint8_t *>(Symtab);
    ASSERT_GE(RawSymtab, reinterpret_cast<const uint8_t *>(Sections->data()));
    const size_t SymtabHeaderOffset = static_cast<size_t>(
        RawSymtab - reinterpret_cast<const uint8_t *>(Sections->data()));
    ASSERT_EQ(SymtabHeaderOffset % sizeof(object::ELF64LE::Shdr), 0U);
    SymbolTableIndex = static_cast<uint32_t>(SymtabHeaderOffset /
                                             sizeof(object::ELF64LE::Shdr));
    const uint32_t SelectedIndex = Case == Table::Symbols ? SymbolTableIndex
                                   : Case == Table::SymbolStrings
                                       ? static_cast<uint32_t>(Symtab->sh_link)
                                       : SectionStringIndex;
    const object::ELF64LE::Shdr *Selected = &(*Sections)[SelectedIndex];
    object::ELF64LE::Shdr Replacement = *Selected;
    Replacement.sh_flags |= ELF::SHF_INFO_LINK;
    Replacement.sh_info = *DebugIndex;
    if (Case == Table::Symbols) {
      auto Symbols = ELFFile->symbols(Symtab);
      ASSERT_TRUE(static_cast<bool>(Symbols)) << errorText(Symbols.takeError());
      ASSERT_GT(Symbols->size(), *DebugIndex);
      for (uint32_t Index = 0; Index != Symbols->size(); ++Index) {
        object::ELF64LE::Sym Symbol = (*Symbols)[Index];
        Symbol.setBinding(Index < *DebugIndex ? ELF::STB_LOCAL
                                              : ELF::STB_GLOBAL);
        const auto *RawSymbol =
            reinterpret_cast<const uint8_t *>(&(*Symbols)[Index]);
        ASSERT_GE(RawSymbol, Bytes.data());
        const size_t SymbolOffset =
            static_cast<size_t>(RawSymbol - Bytes.data());
        ASSERT_LE(SymbolOffset, Bytes.size());
        ASSERT_LE(sizeof(Symbol), Bytes.size() - SymbolOffset);
        std::memcpy(Bytes.data() + SymbolOffset, &Symbol, sizeof(Symbol));
      }
    }
    const auto *Raw = reinterpret_cast<const uint8_t *>(Selected);
    ASSERT_GE(Raw, Bytes.data());
    const size_t HeaderOffset = static_cast<size_t>(Raw - Bytes.data());
    ASSERT_LE(HeaderOffset, Bytes.size());
    ASSERT_LE(sizeof(Replacement), Bytes.size() - HeaderOffset);
    std::memcpy(Bytes.data() + HeaderOffset, &Replacement, sizeof(Replacement));

    const StringRef Mutated(reinterpret_cast<const char *>(Bytes.data()),
                            Bytes.size());
    auto Preserved = canonicalizeBuiltinELFTables(Mutated);
    ASSERT_TRUE(static_cast<bool>(Preserved))
        << errorText(Preserved.takeError());
    const StringRef PreservedImage(Preserved->data(), Preserved->size());
    auto Recanonical = canonicalizeBuiltinELFTables(PreservedImage);
    ASSERT_TRUE(static_cast<bool>(Recanonical))
        << (Recanonical ? std::string() : errorText(Recanonical.takeError()));
    EXPECT_EQ(StringRef(Recanonical->data(), Recanonical->size()),
              PreservedImage);

    auto Dropped = canonicalizeBuiltinELFTables(Mutated, true);
    EXPECT_FALSE(static_cast<bool>(Dropped));
    if (!Dropped) {
      const std::string Message = errorText(Dropped.takeError());
      EXPECT_NE(Message.find("required"), std::string::npos) << Message;
    }
  }
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesRejectsPropagatedRetainedSemantics) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .text
    .globl propagated_profile_from
    .type propagated_profile_from, %function
propagated_profile_from:
    .byte 0
    .globl propagated_profile_to
    .type propagated_profile_to, %function
propagated_profile_to:
    .byte 0
    .cg_profile propagated_profile_from, propagated_profile_to, 23

    .section .candidate.propagated_opaque,"",%progbits
    .byte 0
    .section .candidate.propagated_dual,"",%progbits
    .byte 0
    .section .debug_info.propagated_semantics,"",%progbits
    .byte 1

    .addrsig
    .addrsig_sym propagated_profile_from
)";
  auto Assembled = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Assembled)) << errorText(Assembled.takeError());

  enum class Semantic { OpaqueLink, DualDependency, AddrSig, CallGraphProfile };
  for (Semantic Case : {Semantic::OpaqueLink, Semantic::DualDependency,
                        Semantic::AddrSig, Semantic::CallGraphProfile}) {
    SCOPED_TRACE(static_cast<unsigned>(Case));
    std::vector<uint8_t> Bytes = *Assembled;
    const StringRef Image(reinterpret_cast<const char *>(Bytes.data()),
                          Bytes.size());
    auto ELFFile = object::ELFFile<object::ELF64LE>::create(Image);
    ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
    auto Sections = ELFFile->sections();
    ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());

    const object::ELF64LE::Shdr *Opaque = nullptr;
    const object::ELF64LE::Shdr *Dual = nullptr;
    const object::ELF64LE::Shdr *AddrSig = nullptr;
    const object::ELF64LE::Shdr *Profile = nullptr;
    std::optional<uint32_t> TextIndex;
    std::optional<uint32_t> DebugIndex;
    for (uint32_t Index = 1; Index != Sections->size(); ++Index) {
      const object::ELF64LE::Shdr &Section = (*Sections)[Index];
      auto Name = ELFFile->getSectionName(Section);
      ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
      if (*Name == ".text")
        TextIndex = Index;
      if (*Name == ".candidate.propagated_opaque")
        Opaque = &Section;
      if (*Name == ".candidate.propagated_dual")
        Dual = &Section;
      if (*Name == ".debug_info.propagated_semantics")
        DebugIndex = Index;
      if (Section.sh_type == ELF::SHT_LLVM_ADDRSIG)
        AddrSig = &Section;
      if (Section.sh_type == ELF::SHT_LLVM_CALL_GRAPH_PROFILE)
        Profile = &Section;
    }
    ASSERT_NE(Opaque, nullptr);
    ASSERT_NE(Dual, nullptr);
    ASSERT_NE(AddrSig, nullptr);
    ASSERT_NE(Profile, nullptr);
    ASSERT_TRUE(TextIndex.has_value());
    ASSERT_TRUE(DebugIndex.has_value());

    const object::ELF64LE::Shdr *Selected = nullptr;
    switch (Case) {
    case Semantic::OpaqueLink:
      Selected = Opaque;
      break;
    case Semantic::DualDependency:
      Selected = Dual;
      break;
    case Semantic::AddrSig:
      Selected = AddrSig;
      break;
    case Semantic::CallGraphProfile:
      Selected = Profile;
      break;
    }
    ASSERT_NE(Selected, nullptr);
    object::ELF64LE::Shdr Replacement = *Selected;
    Replacement.sh_flags |= ELF::SHF_INFO_LINK;
    Replacement.sh_info = *DebugIndex;
    if (Case == Semantic::OpaqueLink)
      Replacement.sh_link = *TextIndex;
    if (Case == Semantic::DualDependency) {
      Replacement.sh_flags |= ELF::SHF_LINK_ORDER;
      Replacement.sh_link = *TextIndex;
    }
    const auto *Raw = reinterpret_cast<const uint8_t *>(Selected);
    ASSERT_GE(Raw, Bytes.data());
    const size_t HeaderOffset = static_cast<size_t>(Raw - Bytes.data());
    ASSERT_LE(HeaderOffset, Bytes.size());
    ASSERT_LE(sizeof(Replacement), Bytes.size() - HeaderOffset);
    std::memcpy(Bytes.data() + HeaderOffset, &Replacement, sizeof(Replacement));

    const StringRef Mutated(reinterpret_cast<const char *>(Bytes.data()),
                            Bytes.size());
    auto Preserved = canonicalizeBuiltinELFTables(Mutated);
    ASSERT_TRUE(static_cast<bool>(Preserved))
        << errorText(Preserved.takeError());
    const StringRef PreservedImage(Preserved->data(), Preserved->size());
    auto Recanonical = canonicalizeBuiltinELFTables(PreservedImage);
    ASSERT_TRUE(static_cast<bool>(Recanonical))
        << (Recanonical ? std::string() : errorText(Recanonical.takeError()));
    EXPECT_EQ(StringRef(Recanonical->data(), Recanonical->size()),
              PreservedImage);

    auto Dropped = canonicalizeBuiltinELFTables(Mutated, true);
    EXPECT_FALSE(static_cast<bool>(Dropped));
    if (!Dropped) {
      const std::string Message = errorText(Dropped.takeError());
      EXPECT_NE(Message.find("cannot be proven debug-owned"), std::string::npos)
          << Message;
    }
  }
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesPreservesEmptyNonDebugGroup) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .text
    .globl empty_group_signature
    .type empty_group_signature, %function
empty_group_signature:
    .byte 0

    .section .synthetic.empty_group,"",%progbits
    .long 0
)";
  auto Input = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

  const StringRef Image(reinterpret_cast<const char *>(Input->data()),
                        Input->size());
  auto ELFFile = object::ELFFile<object::ELF64LE>::create(Image);
  ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
  auto Sections = ELFFile->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());

  const object::ELF64LE::Shdr *Symtab = nullptr;
  const object::ELF64LE::Shdr *SyntheticGroup = nullptr;
  std::optional<uint32_t> SymtabIndex;
  for (uint32_t Index = 1; Index != Sections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*Sections)[Index];
    auto Name = ELFFile->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (Section.sh_type == ELF::SHT_SYMTAB) {
      Symtab = &Section;
      SymtabIndex = Index;
    }
    if (*Name == ".synthetic.empty_group")
      SyntheticGroup = &Section;
  }
  ASSERT_NE(Symtab, nullptr);
  ASSERT_NE(SyntheticGroup, nullptr);
  ASSERT_TRUE(SymtabIndex.has_value());
  auto Symbols = ELFFile->symbols(Symtab);
  auto Strings = ELFFile->getStringTableForSymtab(*Symtab);
  ASSERT_TRUE(static_cast<bool>(Symbols)) << errorText(Symbols.takeError());
  ASSERT_TRUE(static_cast<bool>(Strings)) << errorText(Strings.takeError());
  std::optional<uint32_t> SignatureIndex;
  for (uint32_t Index = 0; Index != Symbols->size(); ++Index) {
    auto Name = (*Symbols)[Index].getName(*Strings);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name == "empty_group_signature")
      SignatureIndex = Index;
  }
  ASSERT_TRUE(SignatureIndex.has_value());

  object::ELF64LE::Shdr Replacement = *SyntheticGroup;
  Replacement.sh_type = ELF::SHT_GROUP;
  Replacement.sh_link = *SymtabIndex;
  Replacement.sh_info = *SignatureIndex;
  Replacement.sh_size = sizeof(object::ELF64LE::Word);
  Replacement.sh_addralign = alignof(object::ELF64LE::Word);
  Replacement.sh_entsize = sizeof(object::ELF64LE::Word);
  const auto *Raw = reinterpret_cast<const uint8_t *>(SyntheticGroup);
  ASSERT_GE(Raw, Input->data());
  const size_t HeaderOffset = static_cast<size_t>(Raw - Input->data());
  ASSERT_LE(HeaderOffset, Input->size());
  ASSERT_LE(sizeof(Replacement), Input->size() - HeaderOffset);
  std::memcpy(Input->data() + HeaderOffset, &Replacement, sizeof(Replacement));
  object::ELF64LE::Word GroupFlags(ELF::GRP_COMDAT);
  ASSERT_LE(SyntheticGroup->sh_offset, Input->size());
  ASSERT_LE(sizeof(GroupFlags), Input->size() - SyntheticGroup->sh_offset);
  std::memcpy(Input->data() + SyntheticGroup->sh_offset, &GroupFlags,
              sizeof(GroupFlags));

  const StringRef Mutated(reinterpret_cast<const char *>(Input->data()),
                          Input->size());
  auto Canonical = canonicalizeBuiltinELFTables(Mutated, true);
  ASSERT_TRUE(static_cast<bool>(Canonical)) << errorText(Canonical.takeError());
  const StringRef OutputImage(Canonical->data(), Canonical->size());
  auto Output = object::ELFFile<object::ELF64LE>::create(OutputImage);
  ASSERT_TRUE(static_cast<bool>(Output)) << errorText(Output.takeError());
  auto OutputSections = Output->sections();
  ASSERT_TRUE(static_cast<bool>(OutputSections))
      << errorText(OutputSections.takeError());
  bool SawEmptyGroup = false;
  for (const object::ELF64LE::Shdr &Section : *OutputSections) {
    auto Name = Output->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    SawEmptyGroup |=
        Section.sh_type == ELF::SHT_GROUP && *Name == ".synthetic.empty_group";
  }
  EXPECT_TRUE(SawEmptyGroup)
      << "DropDebug must not delete a group with no dropped member";

  auto Recanonical = canonicalizeBuiltinELFTables(OutputImage, true);
  ASSERT_TRUE(static_cast<bool>(Recanonical))
      << (Recanonical ? std::string() : errorText(Recanonical.takeError()));
  EXPECT_EQ(StringRef(Recanonical->data(), Recanonical->size()), OutputImage);
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesDropsCompleteDebugOwnedGroupClosure) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .section .debug_info.owned_group,"G",%progbits,debug_group_signature,comdat
    .globl debug_group_signature
debug_group_signature:
    .byte 1
)";
  auto Input = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

  const StringRef InputImage(reinterpret_cast<const char *>(Input->data()),
                             Input->size());
  auto Canonical = canonicalizeBuiltinELFTables(InputImage, true);
  ASSERT_TRUE(static_cast<bool>(Canonical)) << errorText(Canonical.takeError());
  const StringRef OutputImage(Canonical->data(), Canonical->size());
  auto Output = object::ELFFile<object::ELF64LE>::create(OutputImage);
  ASSERT_TRUE(static_cast<bool>(Output)) << errorText(Output.takeError());
  auto Sections = Output->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    auto Name = Output->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    EXPECT_NE(*Name, ".debug_info.owned_group");
    EXPECT_NE(Section.sh_type, ELF::SHT_GROUP)
        << "a group with only removed debug members must be removed too";
  }

  auto Recanonical = canonicalizeBuiltinELFTables(OutputImage, true);
  ASSERT_TRUE(static_cast<bool>(Recanonical))
      << (Recanonical ? std::string() : errorText(Recanonical.takeError()));
  EXPECT_EQ(StringRef(Recanonical->data(), Recanonical->size()), OutputImage);
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesDropDebugWithoutApplyingLinkSemantics) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  auto Target = makeBuiltinTargetKey(*Route);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  OwnedTargetKey ReadTarget = *Target;
  PluginObjectGraph Graph(std::move(*Target));

  PluginObjectSection Debug;
  Debug.ID = Graph.allocateEntityID();
  Debug.Name = ".debug_info";
  Debug.Kind = NEVERC_OBJECT_SECTION_KIND_DEBUG;
  Debug.Flags = NEVERC_OBJECT_SECTION_DEBUG;
  Debug.Alignment = 1;
  Debug.Data = {UINT8_C(1), UINT8_C(2), UINT8_C(3)};
  Graph.sections().push_back(std::move(Debug));

  PluginObjectComdat Comdat;
  Comdat.ID = Graph.allocateEntityID();
  Comdat.Name = "drop_debug_comdat";
  Comdat.Selection = NEVERC_OBJECT_COMDAT_ANY;
  const uint64_t ComdatID = Comdat.ID;
  Graph.comdats().push_back(std::move(Comdat));

  PluginObjectSection Text;
  Text.ID = Graph.allocateEntityID();
  Text.Name = ".text.drop_debug_comdat";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Alignment = 4;
  Text.Data = {UINT8_C(0xc0), UINT8_C(0x03), UINT8_C(0x5f), UINT8_C(0xd6)};
  Text.ComdatID = ComdatID;
  const uint64_t TextID = Text.ID;
  Graph.sections().push_back(std::move(Text));

  PluginObjectSymbol Symbol;
  Symbol.ID = Graph.allocateEntityID();
  Symbol.Name = "drop_debug_comdat";
  Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Symbol.SectionID = TextID;
  Symbol.Size = 4;
  Symbol.Alignment = 4;
  Symbol.ComdatID = ComdatID;
  Graph.symbols().push_back(std::move(Symbol));
  Graph.issueLayoutProof();

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "canonical-drop-debug-comdat.o", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::CanonicalELFTables;
  Destination.DropDebugInfo = true;
  auto Image = (*Writer)->beginWrite(Scope.task(), Graph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Bytes = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Bytes)) << errorText(Bytes.takeError());

  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Restored = (*Reader)->read(Scope.task(), *Bytes,
                                  "canonical-drop-debug-comdat.o", ReadTarget);
  ASSERT_TRUE(static_cast<bool>(Restored)) << errorText(Restored.takeError());
  EXPECT_EQ(llvm::find_if((*Restored)->sections(),
                          [](const PluginObjectSection &Section) {
                            return Section.Name == ".debug_info";
                          }),
            (*Restored)->sections().end());
  ASSERT_EQ((*Restored)->comdatCount(), 1U);
  EXPECT_EQ((*Restored)->comdats().front().Name, "drop_debug_comdat");
  const auto RestoredSymbol = llvm::find_if(
      (*Restored)->symbols(), [](const PluginObjectSymbol &Value) {
        return Value.Name == "drop_debug_comdat";
      });
  ASSERT_NE(RestoredSymbol, (*Restored)->symbols().end());
  const PluginObjectSection *RestoredText =
      (*Restored)->findSection(RestoredSymbol->SectionID);
  ASSERT_NE(RestoredText, nullptr);
  EXPECT_EQ(RestoredText->Name, ".text.drop_debug_comdat");
  EXPECT_FALSE((*Image)->abort());
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesDropDebugRemapsSurvivingNativeIndices) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .section .text.debug_remap,"axG",%progbits,debug_remap_entry,comdat
    .globl debug_remap_entry
    .type debug_remap_entry, %function
debug_remap_entry:
    .byte 0

    .section .data.debug_remap,"aw",%progbits
    .xword debug_remap_entry

    .section .debug_info,"",%progbits
    .xword debug_remap_entry
    .section .debug_line,"",%progbits
    .byte 1, 2, 3
)";
  auto Input = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  const StringRef InputImage(reinterpret_cast<const char *>(Input->data()),
                             Input->size());
  auto Canonical = canonicalizeBuiltinELFTables(InputImage, true);
  ASSERT_TRUE(static_cast<bool>(Canonical)) << errorText(Canonical.takeError());
  const StringRef OutputImage(Canonical->data(), Canonical->size());
  auto Output = object::ELFFile<object::ELF64LE>::create(OutputImage);
  ASSERT_TRUE(static_cast<bool>(Output)) << errorText(Output.takeError());
  auto Sections = Output->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());

  const object::ELF64LE::Shdr *Symtab = nullptr;
  bool SawGroup = false;
  bool SawDataRelocation = false;
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    auto Name = Output->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    EXPECT_FALSE(is_contained(
        std::array<StringRef, 2>{".debug_info", ".debug_line"}, *Name));
    EXPECT_FALSE(Name->starts_with(".rela.debug"));
    SawGroup |= Section.sh_type == ELF::SHT_GROUP;
    if (Section.sh_type == ELF::SHT_SYMTAB)
      Symtab = &Section;
    if (Section.sh_type == ELF::SHT_RELA) {
      ASSERT_LT(Section.sh_info, Sections->size());
      auto TargetName = Output->getSectionName((*Sections)[Section.sh_info]);
      ASSERT_TRUE(static_cast<bool>(TargetName))
          << errorText(TargetName.takeError());
      SawDataRelocation |= *TargetName == ".data.debug_remap";
    }
  }
  EXPECT_TRUE(SawGroup);
  EXPECT_TRUE(SawDataRelocation);
  ASSERT_NE(Symtab, nullptr);
  auto Symbols = Output->symbols(Symtab);
  auto Strings = Output->getStringTableForSymtab(*Symtab);
  ASSERT_TRUE(static_cast<bool>(Symbols)) << errorText(Symbols.takeError());
  ASSERT_TRUE(static_cast<bool>(Strings)) << errorText(Strings.takeError());
  bool SawEntry = false;
  for (const object::ELF64LE::Sym &Symbol : *Symbols) {
    auto Name = Symbol.getName(*Strings);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name != "debug_remap_entry")
      continue;
    ASSERT_LT(Symbol.st_shndx, Sections->size());
    auto SectionName = Output->getSectionName((*Sections)[Symbol.st_shndx]);
    ASSERT_TRUE(static_cast<bool>(SectionName))
        << errorText(SectionName.takeError());
    EXPECT_EQ(*SectionName, ".text.debug_remap");
    SawEntry = true;
  }
  EXPECT_TRUE(SawEntry);

  // A second canonicalization is also a structural audit: every remapped
  // index must be accepted from the now-distinct name-table representation.
  auto Recanonical = canonicalizeBuiltinELFTables(OutputImage, true);
  ASSERT_TRUE(static_cast<bool>(Recanonical))
      << (Recanonical ? std::string() : errorText(Recanonical.takeError()));
  EXPECT_EQ(StringRef(Recanonical->data(), Recanonical->size()), OutputImage)
      << "dropping a debug target and its relocations must be byte-idempotent";
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesDropDebugCompactsEveryNativeSymbolConsumer) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .text
    .globl remap_live_before
    .type remap_live_before, %function
remap_live_before:
    .byte 0
    .globl remap_live_after
    .type remap_live_after, %function
remap_live_after:
    .byte 0

    .section .text.remap_group,"axG",%progbits,remap_group_signature,comdat
    .globl remap_group_signature
    .type remap_group_signature, %function
remap_group_signature:
    .byte 0

    .section .data.extended_live,"aw",%progbits
    .globl remap_extended_live
    .type remap_extended_live, %object
remap_extended_live:
    .xword 0
    .size remap_extended_live, 8

    .section .data.real_rela,"aw",%progbits
    .xword remap_live_after

    .section .data.synthetic_rel_target,"aw",%progbits
    .xword 0

    .section .debug_info.removed,"",%progbits
    .local remap_removed_debug
    .type remap_removed_debug, %object
remap_removed_debug:
    .xword 0
    .size remap_removed_debug, 8

    .section .synthetic.rel,"",%progbits
    .zero 16
    .section .synthetic.shndx,"",%progbits
    .zero 1024

    .addrsig
    .addrsig_sym remap_live_after
    .addrsig_sym remap_removed_debug
)";
  auto Input = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

  const StringRef Original(reinterpret_cast<const char *>(Input->data()),
                           Input->size());
  auto InputELF = object::ELFFile<object::ELF64LE>::create(Original);
  ASSERT_TRUE(static_cast<bool>(InputELF)) << errorText(InputELF.takeError());
  auto InputSections = InputELF->sections();
  ASSERT_TRUE(static_cast<bool>(InputSections))
      << errorText(InputSections.takeError());

  const object::ELF64LE::Shdr *Symtab = nullptr;
  const object::ELF64LE::Shdr *SyntheticRel = nullptr;
  const object::ELF64LE::Shdr *SyntheticShndx = nullptr;
  std::optional<uint32_t> SymtabIndex;
  std::optional<uint32_t> SyntheticRelTargetIndex;
  std::optional<uint32_t> ExtendedLiveSectionIndex;
  std::optional<uint32_t> DebugSectionNameOffset;
  for (uint32_t Index = 1; Index != InputSections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*InputSections)[Index];
    auto Name = InputELF->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (Section.sh_type == ELF::SHT_SYMTAB) {
      Symtab = &Section;
      SymtabIndex = Index;
    }
    if (*Name == ".synthetic.rel")
      SyntheticRel = &Section;
    if (*Name == ".synthetic.shndx")
      SyntheticShndx = &Section;
    if (*Name == ".data.synthetic_rel_target")
      SyntheticRelTargetIndex = Index;
    if (*Name == ".data.extended_live")
      ExtendedLiveSectionIndex = Index;
    if (*Name == ".debug_info.removed")
      DebugSectionNameOffset = Section.sh_name;
  }
  ASSERT_NE(Symtab, nullptr);
  ASSERT_NE(SyntheticRel, nullptr);
  ASSERT_NE(SyntheticShndx, nullptr);
  ASSERT_TRUE(SymtabIndex.has_value());
  ASSERT_TRUE(SyntheticRelTargetIndex.has_value());
  ASSERT_TRUE(ExtendedLiveSectionIndex.has_value());
  ASSERT_TRUE(DebugSectionNameOffset.has_value());

  auto InputSymbols = InputELF->symbols(Symtab);
  auto InputStrings = InputELF->getStringTableForSymtab(*Symtab);
  ASSERT_TRUE(static_cast<bool>(InputSymbols))
      << errorText(InputSymbols.takeError());
  ASSERT_TRUE(static_cast<bool>(InputStrings))
      << errorText(InputStrings.takeError());

  const auto FindInputSymbol =
      [&](StringRef Wanted) -> std::optional<uint32_t> {
    for (uint32_t Index = 0; Index != InputSymbols->size(); ++Index) {
      auto Name = (*InputSymbols)[Index].getName(*InputStrings);
      if (!Name) {
        ADD_FAILURE() << errorText(Name.takeError());
        return std::nullopt;
      }
      if (*Name == Wanted)
        return Index;
    }
    return std::nullopt;
  };
  const std::optional<uint32_t> LiveAfter = FindInputSymbol("remap_live_after");
  const std::optional<uint32_t> GroupSignature =
      FindInputSymbol("remap_group_signature");
  const std::optional<uint32_t> ExtendedLive =
      FindInputSymbol("remap_extended_live");
  const std::optional<uint32_t> RemovedDebug =
      FindInputSymbol("remap_removed_debug");
  ASSERT_TRUE(LiveAfter.has_value());
  ASSERT_TRUE(GroupSignature.has_value());
  ASSERT_TRUE(ExtendedLive.has_value());
  ASSERT_TRUE(RemovedDebug.has_value());
  ASSERT_LT(*RemovedDebug, *LiveAfter)
      << "the removed local must precede globals to exercise index compaction";
  ASSERT_LE(InputSymbols->size() * sizeof(object::ELF64LE::Word),
            SyntheticShndx->sh_size);

  const auto Replace = [&](const auto *OriginalRecord,
                           const auto &Replacement) {
    const auto *Raw = reinterpret_cast<const uint8_t *>(OriginalRecord);
    ASSERT_GE(Raw, Input->data());
    const size_t Offset = static_cast<size_t>(Raw - Input->data());
    ASSERT_LE(Offset, Input->size());
    ASSERT_LE(sizeof(Replacement), Input->size() - Offset);
    std::memcpy(Input->data() + Offset, &Replacement, sizeof(Replacement));
  };

  const uint64_t SyntheticRelOffset = SyntheticRel->sh_offset;
  object::ELF64LE::Shdr RelHeader = *SyntheticRel;
  RelHeader.sh_type = ELF::SHT_REL;
  RelHeader.sh_link = *SymtabIndex;
  RelHeader.sh_info = *SyntheticRelTargetIndex;
  RelHeader.sh_size = sizeof(object::ELF64LE::Rel);
  RelHeader.sh_addralign = alignof(object::ELF64LE::Rel);
  RelHeader.sh_entsize = sizeof(object::ELF64LE::Rel);
  Replace(SyntheticRel, RelHeader);
  object::ELF64LE::Rel Relocation{};
  Relocation.r_offset = 0;
  Relocation.setSymbol(*LiveAfter);
  Relocation.setType(ELF::R_AARCH64_ABS64);
  ASSERT_LE(SyntheticRelOffset, Input->size());
  ASSERT_LE(sizeof(Relocation), Input->size() - SyntheticRelOffset);
  std::memcpy(Input->data() + SyntheticRelOffset, &Relocation,
              sizeof(Relocation));

  const uint64_t SyntheticShndxOffset = SyntheticShndx->sh_offset;
  object::ELF64LE::Shdr ShndxHeader = *SyntheticShndx;
  ShndxHeader.sh_type = ELF::SHT_SYMTAB_SHNDX;
  // A structural companion remains structural even if an adversarial or
  // producer-specific name makes it look like debug payload.
  ShndxHeader.sh_name = *DebugSectionNameOffset;
  ShndxHeader.sh_link = *SymtabIndex;
  ShndxHeader.sh_info = 0;
  ShndxHeader.sh_size = InputSymbols->size() * sizeof(object::ELF64LE::Word);
  ShndxHeader.sh_addralign = alignof(object::ELF64LE::Word);
  ShndxHeader.sh_entsize = sizeof(object::ELF64LE::Word);
  Replace(SyntheticShndx, ShndxHeader);
  object::ELF64LE::Sym ExtendedSymbol = (*InputSymbols)[*ExtendedLive];
  ASSERT_EQ(ExtendedSymbol.st_shndx, *ExtendedLiveSectionIndex);
  ExtendedSymbol.st_shndx = ELF::SHN_XINDEX;
  Replace(&(*InputSymbols)[*ExtendedLive], ExtendedSymbol);
  object::ELF64LE::Word ExtendedSection(*ExtendedLiveSectionIndex);
  const uint64_t ExtendedWordOffset =
      SyntheticShndxOffset + *ExtendedLive * sizeof(object::ELF64LE::Word);
  ASSERT_LE(ExtendedWordOffset, Input->size());
  ASSERT_LE(sizeof(ExtendedSection), Input->size() - ExtendedWordOffset);
  std::memcpy(Input->data() + ExtendedWordOffset, &ExtendedSection,
              sizeof(ExtendedSection));

  constexpr uint32_t NoSymbol = std::numeric_limits<uint32_t>::max();
  std::vector<uint32_t> ExpectedSymbolMap(InputSymbols->size(), NoSymbol);
  uint32_t ExpectedOutputSymbolCount = 0;
  for (uint32_t Index = 0; Index != InputSymbols->size(); ++Index) {
    const object::ELF64LE::Sym &Symbol = (*InputSymbols)[Index];
    uint32_t SectionIndex = NoSymbol;
    if (Symbol.st_shndx == ELF::SHN_XINDEX) {
      ASSERT_EQ(Index, *ExtendedLive);
      SectionIndex = *ExtendedLiveSectionIndex;
    } else if (Symbol.st_shndx != ELF::SHN_UNDEF &&
               Symbol.st_shndx < ELF::SHN_LORESERVE) {
      SectionIndex = Symbol.st_shndx;
    }
    bool IsDebugDefined = false;
    if (SectionIndex != NoSymbol) {
      auto SectionName =
          InputELF->getSectionName((*InputSections)[SectionIndex]);
      ASSERT_TRUE(static_cast<bool>(SectionName))
          << errorText(SectionName.takeError());
      const bool HasDebugName = SectionName->starts_with(".debug_") ||
                                SectionName->starts_with(".zdebug_") ||
                                *SectionName == ".debug";
      IsDebugDefined = HasDebugName && (*InputSections)[SectionIndex].sh_type !=
                                           ELF::SHT_SYMTAB_SHNDX;
    }
    if (!IsDebugDefined)
      ExpectedSymbolMap[Index] = ExpectedOutputSymbolCount++;
  }
  ASSERT_EQ(ExpectedSymbolMap[*RemovedDebug], NoSymbol);
  ASSERT_NE(ExpectedSymbolMap[*LiveAfter], NoSymbol);
  ASSERT_NE(ExpectedSymbolMap[*GroupSignature], NoSymbol);
  ASSERT_NE(ExpectedSymbolMap[*ExtendedLive], NoSymbol);

  const StringRef Mutated(reinterpret_cast<const char *>(Input->data()),
                          Input->size());
  auto Canonical = canonicalizeBuiltinELFTables(Mutated, true);
  ASSERT_TRUE(static_cast<bool>(Canonical)) << errorText(Canonical.takeError());

  const StringRef OutputImage(Canonical->data(), Canonical->size());
  auto OutputELF = object::ELFFile<object::ELF64LE>::create(OutputImage);
  ASSERT_TRUE(static_cast<bool>(OutputELF)) << errorText(OutputELF.takeError());

  // The first pass used to succeed after name-dropping SHT_SYMTAB_SHNDX,
  // leaving the retained SHN_XINDEX symbol structurally corrupt. Require the
  // emitted object to survive the same public canonicalization gate again.
  auto Recanonical = canonicalizeBuiltinELFTables(OutputImage, true);
  ASSERT_TRUE(static_cast<bool>(Recanonical))
      << (Recanonical ? std::string() : errorText(Recanonical.takeError()));
  EXPECT_EQ(StringRef(Recanonical->data(), Recanonical->size()), OutputImage)
      << "canonical DROP_DEBUG_INFO output must be byte-idempotent";

  auto OutputSections = OutputELF->sections();
  ASSERT_TRUE(static_cast<bool>(OutputSections))
      << errorText(OutputSections.takeError());

  const object::ELF64LE::Shdr *OutputSymtab = nullptr;
  const object::ELF64LE::Shdr *OutputGroup = nullptr;
  const object::ELF64LE::Shdr *OutputRel = nullptr;
  const object::ELF64LE::Shdr *OutputRela = nullptr;
  const object::ELF64LE::Shdr *OutputAddrSig = nullptr;
  const object::ELF64LE::Shdr *OutputShndx = nullptr;
  std::optional<uint32_t> OutputExtendedSection;
  std::optional<uint32_t> OutputGroupMemberSection;
  for (uint32_t Index = 1; Index != OutputSections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*OutputSections)[Index];
    auto Name = OutputELF->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (Section.sh_type == ELF::SHT_SYMTAB_SHNDX)
      EXPECT_EQ(*Name, ".debug_info.removed")
          << "the structural companion must win over name-based filtering";
    else
      EXPECT_NE(*Name, ".debug_info.removed");
    if (Section.sh_type == ELF::SHT_SYMTAB)
      OutputSymtab = &Section;
    if (Section.sh_type == ELF::SHT_GROUP)
      OutputGroup = &Section;
    if (Section.sh_type == ELF::SHT_LLVM_ADDRSIG)
      OutputAddrSig = &Section;
    if (Section.sh_type == ELF::SHT_SYMTAB_SHNDX)
      OutputShndx = &Section;
    if (*Name == ".data.extended_live")
      OutputExtendedSection = Index;
    if (*Name == ".text.remap_group")
      OutputGroupMemberSection = Index;
    if (Section.sh_type != ELF::SHT_REL && Section.sh_type != ELF::SHT_RELA)
      continue;
    ASSERT_LT(Section.sh_info, OutputSections->size());
    auto TargetName =
        OutputELF->getSectionName((*OutputSections)[Section.sh_info]);
    ASSERT_TRUE(static_cast<bool>(TargetName))
        << errorText(TargetName.takeError());
    if (*TargetName == ".data.synthetic_rel_target")
      OutputRel = &Section;
    if (*TargetName == ".data.real_rela")
      OutputRela = &Section;
  }
  ASSERT_NE(OutputSymtab, nullptr);
  ASSERT_NE(OutputGroup, nullptr);
  ASSERT_NE(OutputRel, nullptr);
  ASSERT_NE(OutputRela, nullptr);
  ASSERT_NE(OutputAddrSig, nullptr);
  ASSERT_NE(OutputShndx, nullptr);
  ASSERT_TRUE(OutputExtendedSection.has_value());
  ASSERT_TRUE(OutputGroupMemberSection.has_value());

  auto OutputSymbols = OutputELF->symbols(OutputSymtab);
  auto OutputStrings = OutputELF->getStringTableForSymtab(*OutputSymtab);
  ASSERT_TRUE(static_cast<bool>(OutputSymbols))
      << errorText(OutputSymbols.takeError());
  ASSERT_TRUE(static_cast<bool>(OutputStrings))
      << errorText(OutputStrings.takeError());
  const auto FindOutputSymbol =
      [&](StringRef Wanted) -> std::optional<uint32_t> {
    for (uint32_t Index = 0; Index != OutputSymbols->size(); ++Index) {
      auto Name = (*OutputSymbols)[Index].getName(*OutputStrings);
      if (!Name) {
        ADD_FAILURE() << errorText(Name.takeError());
        return std::nullopt;
      }
      if (*Name == Wanted)
        return Index;
    }
    return std::nullopt;
  };
  const std::optional<uint32_t> NewLiveAfter =
      FindOutputSymbol("remap_live_after");
  const std::optional<uint32_t> NewGroupSignature =
      FindOutputSymbol("remap_group_signature");
  const std::optional<uint32_t> NewExtendedLive =
      FindOutputSymbol("remap_extended_live");
  ASSERT_TRUE(NewLiveAfter.has_value());
  ASSERT_TRUE(NewGroupSignature.has_value());
  ASSERT_TRUE(NewExtendedLive.has_value());
  EXPECT_FALSE(FindOutputSymbol("remap_removed_debug").has_value());
  EXPECT_EQ(*NewLiveAfter, ExpectedSymbolMap[*LiveAfter]);
  EXPECT_EQ(*NewGroupSignature, ExpectedSymbolMap[*GroupSignature]);
  EXPECT_EQ(*NewExtendedLive, ExpectedSymbolMap[*ExtendedLive]);
  EXPECT_EQ(OutputSymbols->size(), ExpectedOutputSymbolCount);
  EXPECT_LT(*NewLiveAfter, *LiveAfter);

  auto Rels = OutputELF->rels(*OutputRel);
  ASSERT_TRUE(static_cast<bool>(Rels)) << errorText(Rels.takeError());
  ASSERT_EQ(Rels->size(), 1U);
  EXPECT_EQ(Rels->front().getSymbol(), *NewLiveAfter);
  auto Relas = OutputELF->relas(*OutputRela);
  ASSERT_TRUE(static_cast<bool>(Relas)) << errorText(Relas.takeError());
  ASSERT_EQ(Relas->size(), 1U);
  EXPECT_EQ(Relas->front().getSymbol(), *NewLiveAfter);

  EXPECT_EQ(OutputGroup->sh_info, *NewGroupSignature);
  auto GroupWords =
      OutputELF->template getSectionContentsAsArray<object::ELF64LE::Word>(
          *OutputGroup);
  ASSERT_TRUE(static_cast<bool>(GroupWords))
      << errorText(GroupWords.takeError());
  ASSERT_EQ(GroupWords->size(), 2U);
  EXPECT_EQ(GroupWords->front(), ELF::GRP_COMDAT);
  EXPECT_EQ((*GroupWords)[1], *OutputGroupMemberSection);

  auto AddrSigBytes = OutputELF->getSectionContents(*OutputAddrSig);
  ASSERT_TRUE(static_cast<bool>(AddrSigBytes))
      << errorText(AddrSigBytes.takeError());
  const uint8_t *Current = AddrSigBytes->data();
  const uint8_t *End = Current + AddrSigBytes->size();
  std::vector<uint64_t> AddrSigSymbols;
  while (Current != End) {
    unsigned Length = 0;
    const char *DecodeError = nullptr;
    AddrSigSymbols.push_back(
        decodeULEB128(Current, &Length, End, &DecodeError));
    ASSERT_EQ(DecodeError, nullptr);
    ASSERT_GT(Length, 0U);
    Current += Length;
  }
  ASSERT_EQ(AddrSigSymbols.size(), 1U);
  EXPECT_EQ(AddrSigSymbols.front(), *NewLiveAfter);

  auto ExtendedIndices =
      OutputELF->template getSectionContentsAsArray<object::ELF64LE::Word>(
          *OutputShndx);
  ASSERT_TRUE(static_cast<bool>(ExtendedIndices))
      << errorText(ExtendedIndices.takeError());
  ASSERT_EQ(ExtendedIndices->size(), OutputSymbols->size());
  EXPECT_EQ((*OutputSymbols)[*NewExtendedLive].st_shndx, ELF::SHN_XINDEX);
  EXPECT_EQ((*ExtendedIndices)[*NewExtendedLive], *OutputExtendedSection);
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesDropDebugFailsClosedForRemovedOpaqueConsumers) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .section .text.fail_closed_group,"axG",%progbits,fail_closed_key,comdat
    .globl fail_closed_key
    .type fail_closed_key, %function
fail_closed_key:
    .byte 0

    .section .debug_info.fail_closed,"",%progbits
    .local fail_closed_removed_debug
    .type fail_closed_removed_debug, %object
fail_closed_removed_debug:
    .xword 0

    .section .synthetic.opaque_consumer,"",%progbits
    .byte 0
)";
  auto Assembled = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Assembled)) << errorText(Assembled.takeError());

  enum class Consumer { GroupSignature, OpaqueSection };
  for (Consumer Case : {Consumer::GroupSignature, Consumer::OpaqueSection}) {
    SCOPED_TRACE(static_cast<unsigned>(Case));
    std::vector<uint8_t> Bytes = *Assembled;
    const StringRef Image(reinterpret_cast<const char *>(Bytes.data()),
                          Bytes.size());
    auto ELFFile = object::ELFFile<object::ELF64LE>::create(Image);
    ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
    auto Sections = ELFFile->sections();
    ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());

    const object::ELF64LE::Shdr *Symtab = nullptr;
    const object::ELF64LE::Shdr *Group = nullptr;
    const object::ELF64LE::Shdr *Opaque = nullptr;
    std::optional<uint32_t> SymtabIndex;
    for (uint32_t Index = 1; Index != Sections->size(); ++Index) {
      const object::ELF64LE::Shdr &Section = (*Sections)[Index];
      auto Name = ELFFile->getSectionName(Section);
      ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
      if (Section.sh_type == ELF::SHT_SYMTAB) {
        Symtab = &Section;
        SymtabIndex = Index;
      }
      if (Section.sh_type == ELF::SHT_GROUP)
        Group = &Section;
      if (*Name == ".synthetic.opaque_consumer")
        Opaque = &Section;
    }
    ASSERT_NE(Symtab, nullptr);
    ASSERT_NE(Group, nullptr);
    ASSERT_NE(Opaque, nullptr);
    ASSERT_TRUE(SymtabIndex.has_value());

    auto Symbols = ELFFile->symbols(Symtab);
    auto Strings = ELFFile->getStringTableForSymtab(*Symtab);
    ASSERT_TRUE(static_cast<bool>(Symbols)) << errorText(Symbols.takeError());
    ASSERT_TRUE(static_cast<bool>(Strings)) << errorText(Strings.takeError());
    std::optional<uint32_t> RemovedDebug;
    for (uint32_t Index = 0; Index != Symbols->size(); ++Index) {
      auto Name = (*Symbols)[Index].getName(*Strings);
      ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
      if (*Name == "fail_closed_removed_debug")
        RemovedDebug = Index;
    }
    ASSERT_TRUE(RemovedDebug.has_value());

    const auto Replace = [&](const auto *OriginalRecord,
                             const auto &Replacement) {
      const auto *Raw = reinterpret_cast<const uint8_t *>(OriginalRecord);
      ASSERT_GE(Raw, Bytes.data());
      const size_t Offset = static_cast<size_t>(Raw - Bytes.data());
      ASSERT_LE(Offset, Bytes.size());
      ASSERT_LE(sizeof(Replacement), Bytes.size() - Offset);
      std::memcpy(Bytes.data() + Offset, &Replacement, sizeof(Replacement));
    };
    if (Case == Consumer::GroupSignature) {
      object::ELF64LE::Shdr Replacement = *Group;
      Replacement.sh_info = *RemovedDebug;
      Replace(Group, Replacement);
    } else {
      object::ELF64LE::Shdr Replacement = *Opaque;
      Replacement.sh_link = *SymtabIndex;
      Replace(Opaque, Replacement);
    }

    const StringRef Mutated(reinterpret_cast<const char *>(Bytes.data()),
                            Bytes.size());
    auto Canonical = canonicalizeBuiltinELFTables(Mutated, true);
    EXPECT_FALSE(static_cast<bool>(Canonical));
    if (!Canonical) {
      const std::string Message = errorText(Canonical.takeError());
      const StringRef Expected = Case == Consumer::GroupSignature
                                     ? "lost its signature symbol"
                                     : "opaque symbol-index section";
      EXPECT_NE(Message.find(Expected.str()), std::string::npos) << Message;
    }
  }
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesOnlyRebuildsSelectedNameTables) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .text
    .globl profile_from
    .type profile_from, %function
profile_from:
    .byte 0
    .globl profile_alias
    .set profile_alias, profile_from
    .globl profile_to
    .type profile_to, %function
profile_to:
    .byte 0
    .cg_profile profile_from, profile_to, 1234

    .section .text.group,"axG",%progbits,group_key,comdat
    .globl group_key
    .type group_key, %function
group_key:
    .byte 1

    .section .data.refs,"aw",%progbits
    .xword .text.group

    .section .vendor.strings,"",%progbits
    .asciz "vendor-name"

    .addrsig
    .addrsig_sym profile_alias
)";
  auto Input = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  {
    const StringRef Unpatched(reinterpret_cast<const char *>(Input->data()),
                              Input->size());
    auto ELF = object::ELFFile<object::ELF64LE>::create(Unpatched);
    ASSERT_TRUE(static_cast<bool>(ELF)) << errorText(ELF.takeError());
    auto Sections = ELF->sections();
    ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());
    bool Patched = false;
    for (const object::ELF64LE::Shdr &Section : *Sections) {
      auto Name = ELF->getSectionName(Section);
      ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
      if (*Name != ".vendor.strings")
        continue;
      object::ELF64LE::Shdr Replacement = Section;
      Replacement.sh_type = ELF::SHT_STRTAB;
      const auto *Raw = reinterpret_cast<const uint8_t *>(&Section);
      ASSERT_GE(Raw, Input->data());
      const size_t Offset = static_cast<size_t>(Raw - Input->data());
      ASSERT_LE(Offset, Input->size());
      ASSERT_LE(sizeof(Replacement), Input->size() - Offset);
      std::memcpy(Input->data() + Offset, &Replacement, sizeof(Replacement));
      Patched = true;
      break;
    }
    ASSERT_TRUE(Patched);
  }
  const StringRef InputImage(reinterpret_cast<const char *>(Input->data()),
                             Input->size());
  auto Canonical = canonicalizeBuiltinELFTables(InputImage);
  ASSERT_TRUE(static_cast<bool>(Canonical)) << errorText(Canonical.takeError());
  const StringRef OutputImage(Canonical->data(), Canonical->size());

  auto Before = object::ELFFile<object::ELF64LE>::create(InputImage);
  auto After = object::ELFFile<object::ELF64LE>::create(OutputImage);
  ASSERT_TRUE(static_cast<bool>(Before)) << errorText(Before.takeError());
  ASSERT_TRUE(static_cast<bool>(After)) << errorText(After.takeError());
  auto BeforeSections = Before->sections();
  auto AfterSections = After->sections();
  ASSERT_TRUE(static_cast<bool>(BeforeSections))
      << errorText(BeforeSections.takeError());
  ASSERT_TRUE(static_cast<bool>(AfterSections))
      << errorText(AfterSections.takeError());
  ASSERT_EQ(AfterSections->size(), BeforeSections->size() + 1);

  std::optional<uint32_t> BeforeSymtab;
  std::optional<uint32_t> VendorTable;
  bool SawGroup = false;
  bool SawProfile = false;
  bool SawAddrSig = false;
  for (uint32_t Index = 1; Index != BeforeSections->size(); ++Index) {
    const object::ELF64LE::Shdr &Old = (*BeforeSections)[Index];
    const object::ELF64LE::Shdr &New = (*AfterSections)[Index];
    auto OldName = Before->getSectionName(Old);
    auto NewName = After->getSectionName(New);
    ASSERT_TRUE(static_cast<bool>(OldName)) << errorText(OldName.takeError());
    ASSERT_TRUE(static_cast<bool>(NewName)) << errorText(NewName.takeError());

    EXPECT_EQ(New.sh_type, Old.sh_type) << OldName->str();
    EXPECT_EQ(New.sh_flags, Old.sh_flags) << OldName->str();
    EXPECT_EQ(New.sh_addr, Old.sh_addr) << OldName->str();
    EXPECT_EQ(New.sh_link, Old.sh_link) << OldName->str();
    EXPECT_EQ(New.sh_info, Old.sh_info) << OldName->str();
    EXPECT_EQ(New.sh_addralign, Old.sh_addralign) << OldName->str();
    EXPECT_EQ(New.sh_entsize, Old.sh_entsize) << OldName->str();

    if (Old.sh_type == ELF::SHT_SYMTAB) {
      ASSERT_FALSE(BeforeSymtab.has_value());
      BeforeSymtab = Index;
      EXPECT_EQ(New.sh_size, Old.sh_size);
      continue;
    }
    if (Index == Before->getHeader().e_shstrndx)
      continue;

    auto OldContents = Before->getSectionContents(Old);
    auto NewContents = After->getSectionContents(New);
    ASSERT_TRUE(static_cast<bool>(OldContents))
        << errorText(OldContents.takeError());
    ASSERT_TRUE(static_cast<bool>(NewContents))
        << errorText(NewContents.takeError());
    EXPECT_EQ(*NewContents, *OldContents) << OldName->str();
    EXPECT_EQ(New.sh_size, Old.sh_size) << OldName->str();
    EXPECT_EQ(*NewName, *OldName);

    if (*OldName == ".vendor.strings") {
      VendorTable = Index;
      EXPECT_EQ(Old.sh_type, ELF::SHT_STRTAB);
    }
    SawGroup |= Old.sh_type == ELF::SHT_GROUP;
    SawProfile |= Old.sh_type == ELF::SHT_LLVM_CALL_GRAPH_PROFILE;
    SawAddrSig |= Old.sh_type == ELF::SHT_LLVM_ADDRSIG;
  }
  ASSERT_TRUE(BeforeSymtab.has_value());
  ASSERT_TRUE(VendorTable.has_value());
  EXPECT_TRUE(SawGroup);
  EXPECT_TRUE(SawProfile);
  EXPECT_TRUE(SawAddrSig);

  const object::ELF64LE::Shdr &OldSymtab = (*BeforeSections)[*BeforeSymtab];
  const object::ELF64LE::Shdr &NewSymtab = (*AfterSections)[*BeforeSymtab];
  auto OldSymbols = Before->symbols(&OldSymtab);
  auto NewSymbols = After->symbols(&NewSymtab);
  auto OldStrings = Before->getStringTableForSymtab(OldSymtab);
  auto NewStrings = After->getStringTableForSymtab(NewSymtab);
  ASSERT_TRUE(static_cast<bool>(OldSymbols))
      << errorText(OldSymbols.takeError());
  ASSERT_TRUE(static_cast<bool>(NewSymbols))
      << errorText(NewSymbols.takeError());
  ASSERT_TRUE(static_cast<bool>(OldStrings))
      << errorText(OldStrings.takeError());
  ASSERT_TRUE(static_cast<bool>(NewStrings))
      << errorText(NewStrings.takeError());
  ASSERT_EQ(NewSymbols->size(), OldSymbols->size());
  bool SawAlias = false;
  bool SawSectionTargetRelocation = false;
  for (size_t Index = 0; Index != OldSymbols->size(); ++Index) {
    const object::ELF64LE::Sym &Old = (*OldSymbols)[Index];
    const object::ELF64LE::Sym &New = (*NewSymbols)[Index];
    auto OldName = Old.getName(*OldStrings);
    auto NewName = New.getName(*NewStrings);
    ASSERT_TRUE(static_cast<bool>(OldName)) << errorText(OldName.takeError());
    ASSERT_TRUE(static_cast<bool>(NewName)) << errorText(NewName.takeError());
    EXPECT_EQ(*NewName, *OldName);
    EXPECT_EQ(New.st_info, Old.st_info);
    EXPECT_EQ(New.st_other, Old.st_other);
    EXPECT_EQ(New.st_shndx, Old.st_shndx);
    EXPECT_EQ(New.st_value, Old.st_value);
    EXPECT_EQ(New.st_size, Old.st_size);
    SawAlias |=
        *OldName == "profile_alias" && Old.getBinding() == ELF::STB_GLOBAL;
  }
  for (const object::ELF64LE::Shdr &Section : *AfterSections) {
    if (Section.sh_type != ELF::SHT_RELA)
      continue;
    auto Relocations = After->relas(Section);
    ASSERT_TRUE(static_cast<bool>(Relocations))
        << errorText(Relocations.takeError());
    for (const object::ELF64LE::Rela &Relocation : *Relocations) {
      ASSERT_LT(Relocation.getSymbol(), NewSymbols->size());
      SawSectionTargetRelocation |=
          (*NewSymbols)[Relocation.getSymbol()].getType() == ELF::STT_SECTION;
    }
  }
  EXPECT_TRUE(SawAlias);
  EXPECT_TRUE(SawSectionTargetRelocation);

  const uint32_t OutputSectionStrings = After->getHeader().e_shstrndx;
  EXPECT_NE(OutputSectionStrings, NewSymtab.sh_link);
  auto SymbolTableName =
      After->getSectionName((*AfterSections)[NewSymtab.sh_link]);
  auto SectionTableName =
      After->getSectionName((*AfterSections)[OutputSectionStrings]);
  ASSERT_TRUE(static_cast<bool>(SymbolTableName))
      << errorText(SymbolTableName.takeError());
  ASSERT_TRUE(static_cast<bool>(SectionTableName))
      << errorText(SectionTableName.takeError());
  EXPECT_EQ(*SymbolTableName, ".strtab");
  EXPECT_EQ(*SectionTableName, ".shstrtab");
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesClearsConsumedExtendedNumberingFields) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .text
    .globl extended_numbering_entry
    .type extended_numbering_entry, %function
extended_numbering_entry:
    .byte 0
)";
  auto Input = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

  const StringRef OrdinaryImage(reinterpret_cast<const char *>(Input->data()),
                                Input->size());
  auto Ordinary = object::ELFFile<object::ELF64LE>::create(OrdinaryImage);
  ASSERT_TRUE(static_cast<bool>(Ordinary)) << errorText(Ordinary.takeError());
  auto OrdinarySections = Ordinary->sections();
  ASSERT_TRUE(static_cast<bool>(OrdinarySections))
      << errorText(OrdinarySections.takeError());
  ASSERT_LT(OrdinarySections->size(), ELF::SHN_LORESERVE);
  ASSERT_NE(Ordinary->getHeader().e_shstrndx, ELF::SHN_XINDEX);

  object::ELF64LE::Ehdr ExtendedHeader = Ordinary->getHeader();
  const uint32_t OriginalSectionStrings = ExtendedHeader.e_shstrndx;
  ExtendedHeader.e_shnum = 0;
  ExtendedHeader.e_shstrndx = ELF::SHN_XINDEX;
  object::ELF64LE::Shdr ExtendedZero = OrdinarySections->front();
  ExtendedZero.sh_size = OrdinarySections->size();
  ExtendedZero.sh_link = OriginalSectionStrings;
  std::memcpy(Input->data(), &ExtendedHeader, sizeof(ExtendedHeader));
  const uint64_t ZeroOffset = ExtendedHeader.e_shoff;
  ASSERT_LE(ZeroOffset, Input->size());
  ASSERT_LE(sizeof(ExtendedZero), Input->size() - ZeroOffset);
  std::memcpy(Input->data() + ZeroOffset, &ExtendedZero, sizeof(ExtendedZero));

  const StringRef ExtendedImage(reinterpret_cast<const char *>(Input->data()),
                                Input->size());
  auto ParsedExtended = object::ELFFile<object::ELF64LE>::create(ExtendedImage);
  ASSERT_TRUE(static_cast<bool>(ParsedExtended))
      << errorText(ParsedExtended.takeError());
  auto ParsedExtendedSections = ParsedExtended->sections();
  ASSERT_TRUE(static_cast<bool>(ParsedExtendedSections))
      << errorText(ParsedExtendedSections.takeError());
  ASSERT_EQ(ParsedExtendedSections->size(), OrdinarySections->size());

  auto Canonical = canonicalizeBuiltinELFTables(ExtendedImage);
  ASSERT_TRUE(static_cast<bool>(Canonical)) << errorText(Canonical.takeError());
  auto Output = object::ELFFile<object::ELF64LE>::create(
      StringRef(Canonical->data(), Canonical->size()));
  ASSERT_TRUE(static_cast<bool>(Output)) << errorText(Output.takeError());
  auto OutputSections = Output->sections();
  ASSERT_TRUE(static_cast<bool>(OutputSections))
      << errorText(OutputSections.takeError());
  ASSERT_LT(OutputSections->size(), ELF::SHN_LORESERVE);
  EXPECT_EQ(Output->getHeader().e_shnum, OutputSections->size());
  EXPECT_NE(Output->getHeader().e_shstrndx, ELF::SHN_XINDEX);

  const object::ELF64LE::Shdr &Zero = OutputSections->front();
  EXPECT_EQ(Zero.sh_name, 0U);
  EXPECT_EQ(Zero.sh_type, ELF::SHT_NULL);
  EXPECT_EQ(Zero.sh_flags, 0U);
  EXPECT_EQ(Zero.sh_addr, 0U);
  EXPECT_EQ(Zero.sh_offset, 0U);
  EXPECT_EQ(Zero.sh_size, 0U);
  EXPECT_EQ(Zero.sh_link, 0U);
  EXPECT_EQ(Zero.sh_info, 0U);
  EXPECT_EQ(Zero.sh_addralign, 0U);
  EXPECT_EQ(Zero.sh_entsize, 0U);
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesRejectsDanglingRetainedELFIndices) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .text
    .globl index_target
    .type index_target, %function
index_target:
    .byte 0
    .globl index_callee
    .type index_callee, %function
index_callee:
    .byte 0
    .cg_profile index_target, index_callee, 7

    .section .text.index_group,"axG",%progbits,index_group,comdat
    .globl index_group
    .type index_group, %function
index_group:
    .byte 1

    .section .data.index_ref,"aw",%progbits
    .xword index_target

    .addrsig
    .addrsig_sym index_target
)";
  auto Assembled = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Assembled)) << errorText(Assembled.takeError());

  enum class Corruption {
    SymbolSection,
    GroupLink,
    GroupSignature,
    GroupMember,
    RelocationLink,
    RelocationTargetSection,
    RelocationSymbol,
    AddrSigLink,
    CallGraphProfileLink,
  };
  const std::array Cases{
      Corruption::SymbolSection,        Corruption::GroupLink,
      Corruption::GroupSignature,       Corruption::GroupMember,
      Corruption::RelocationLink,       Corruption::RelocationTargetSection,
      Corruption::RelocationSymbol,     Corruption::AddrSigLink,
      Corruption::CallGraphProfileLink,
  };
  for (Corruption Case : Cases) {
    SCOPED_TRACE(static_cast<unsigned>(Case));
    std::vector<uint8_t> Bytes = *Assembled;
    const StringRef Image(reinterpret_cast<const char *>(Bytes.data()),
                          Bytes.size());
    auto ELFFile = object::ELFFile<object::ELF64LE>::create(Image);
    ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
    auto Sections = ELFFile->sections();
    ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());

    const object::ELF64LE::Shdr *Symtab = nullptr;
    const object::ELF64LE::Shdr *Group = nullptr;
    const object::ELF64LE::Shdr *Rela = nullptr;
    const object::ELF64LE::Shdr *AddrSig = nullptr;
    const object::ELF64LE::Shdr *Profile = nullptr;
    for (const object::ELF64LE::Shdr &Section : *Sections) {
      switch (Section.sh_type) {
      case ELF::SHT_SYMTAB:
        Symtab = &Section;
        break;
      case ELF::SHT_GROUP:
        Group = &Section;
        break;
      case ELF::SHT_RELA:
        if (!Rela)
          Rela = &Section;
        break;
      case ELF::SHT_LLVM_ADDRSIG:
        AddrSig = &Section;
        break;
      case ELF::SHT_LLVM_CALL_GRAPH_PROFILE:
        Profile = &Section;
        break;
      default:
        break;
      }
    }
    ASSERT_NE(Symtab, nullptr);
    ASSERT_NE(Group, nullptr);
    ASSERT_NE(Rela, nullptr);
    ASSERT_NE(AddrSig, nullptr);
    ASSERT_NE(Profile, nullptr);
    auto Symbols = ELFFile->symbols(Symtab);
    auto Strings = ELFFile->getStringTableForSymtab(*Symtab);
    ASSERT_TRUE(static_cast<bool>(Symbols)) << errorText(Symbols.takeError());
    ASSERT_TRUE(static_cast<bool>(Strings)) << errorText(Strings.takeError());

    const auto Replace = [&](const auto *Original, const auto &Replacement) {
      const auto *Raw = reinterpret_cast<const uint8_t *>(Original);
      ASSERT_GE(Raw, Bytes.data());
      const size_t Offset = static_cast<size_t>(Raw - Bytes.data());
      ASSERT_LE(Offset, Bytes.size());
      ASSERT_LE(sizeof(Replacement), Bytes.size() - Offset);
      std::memcpy(Bytes.data() + Offset, &Replacement, sizeof(Replacement));
    };
    const uint32_t BadSection = static_cast<uint32_t>(Sections->size());
    const uint32_t BadSymbol = static_cast<uint32_t>(Symbols->size());
    switch (Case) {
    case Corruption::SymbolSection: {
      const object::ELF64LE::Sym *Target = nullptr;
      for (const object::ELF64LE::Sym &Symbol : *Symbols) {
        auto Name = Symbol.getName(*Strings);
        ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
        if (*Name == "index_target")
          Target = &Symbol;
      }
      ASSERT_NE(Target, nullptr);
      object::ELF64LE::Sym Replacement = *Target;
      Replacement.st_shndx = static_cast<uint16_t>(BadSection);
      Replace(Target, Replacement);
      break;
    }
    case Corruption::GroupLink: {
      object::ELF64LE::Shdr Replacement = *Group;
      Replacement.sh_link = BadSection;
      Replace(Group, Replacement);
      break;
    }
    case Corruption::GroupSignature: {
      object::ELF64LE::Shdr Replacement = *Group;
      Replacement.sh_info = BadSymbol;
      Replace(Group, Replacement);
      break;
    }
    case Corruption::GroupMember: {
      auto Words =
          ELFFile->template getSectionContentsAsArray<object::ELF64LE::Word>(
              *Group);
      ASSERT_TRUE(static_cast<bool>(Words)) << errorText(Words.takeError());
      ASSERT_GT(Words->size(), 1U);
      object::ELF64LE::Word Replacement(BadSection);
      Replace(&(*Words)[1], Replacement);
      break;
    }
    case Corruption::RelocationLink: {
      object::ELF64LE::Shdr Replacement = *Rela;
      Replacement.sh_link = BadSection;
      Replace(Rela, Replacement);
      break;
    }
    case Corruption::RelocationTargetSection: {
      object::ELF64LE::Shdr Replacement = *Rela;
      Replacement.sh_info = BadSection;
      Replace(Rela, Replacement);
      break;
    }
    case Corruption::RelocationSymbol: {
      auto Relocations = ELFFile->relas(*Rela);
      ASSERT_TRUE(static_cast<bool>(Relocations))
          << errorText(Relocations.takeError());
      ASSERT_FALSE(Relocations->empty());
      object::ELF64LE::Rela Replacement = Relocations->front();
      Replacement.setSymbol(BadSymbol);
      Replace(&Relocations->front(), Replacement);
      break;
    }
    case Corruption::AddrSigLink: {
      object::ELF64LE::Shdr Replacement = *AddrSig;
      Replacement.sh_link = BadSection;
      Replace(AddrSig, Replacement);
      break;
    }
    case Corruption::CallGraphProfileLink: {
      object::ELF64LE::Shdr Replacement = *Profile;
      Replacement.sh_link = BadSection;
      Replace(Profile, Replacement);
      break;
    }
    }

    const StringRef Corrupted(reinterpret_cast<const char *>(Bytes.data()),
                              Bytes.size());
    auto Canonical = canonicalizeBuiltinELFTables(Corrupted);
    EXPECT_FALSE(static_cast<bool>(Canonical));
    if (!Canonical)
      consumeError(Canonical.takeError());
  }
}

TEST(PluginBuiltinObjectFormatTest,
     CanonicalELFTablesRejectsAmbiguousRetainedNameAndIndexTables) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  constexpr StringLiteral Assembly = R"(
    .text
    .globl retained_table_target
    .type retained_table_target, %function
retained_table_target:
    .byte 0

    .section .candidate.dynamic,"",%progbits
    .zero 48

    .section .candidate.dynstr,"",%progbits
    .byte 0
    .asciz "retained-dynamic-name"

    .section .debug_info,"",%progbits
    .byte 0

    .section .candidate.live,"a",%progbits
    .byte 0

    .section .candidate.symtab_shndx,"",%progbits
    .fill 64, 4, 0
)";
  auto Assembled = assembleSource(*Route, Assembly);
  ASSERT_TRUE(static_cast<bool>(Assembled)) << errorText(Assembled.takeError());

  enum class Corruption {
    AdditionalSelectedStringTableConsumer,
    ExtendedIndexForDirectSymbol,
    MissingExtendedIndexCompanion,
    DuplicateExtendedIndexCompanion,
    AdditionalSymbolTableDuringDebugRemoval,
  };
  for (Corruption Case :
       {Corruption::AdditionalSelectedStringTableConsumer,
        Corruption::ExtendedIndexForDirectSymbol,
        Corruption::MissingExtendedIndexCompanion,
        Corruption::DuplicateExtendedIndexCompanion,
        Corruption::AdditionalSymbolTableDuringDebugRemoval}) {
    SCOPED_TRACE(static_cast<unsigned>(Case));
    std::vector<uint8_t> Bytes = *Assembled;
    const StringRef Image(reinterpret_cast<const char *>(Bytes.data()),
                          Bytes.size());
    auto ELFFile = object::ELFFile<object::ELF64LE>::create(Image);
    ASSERT_TRUE(static_cast<bool>(ELFFile)) << errorText(ELFFile.takeError());
    auto Sections = ELFFile->sections();
    ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());

    const object::ELF64LE::Shdr *Symtab = nullptr;
    const object::ELF64LE::Shdr *DynamicCandidate = nullptr;
    const object::ELF64LE::Shdr *DynamicStrings = nullptr;
    const object::ELF64LE::Shdr *IndexCandidate = nullptr;
    const object::ELF64LE::Shdr *LiveSection = nullptr;
    for (const object::ELF64LE::Shdr &Section : *Sections) {
      if (Section.sh_type == ELF::SHT_SYMTAB)
        Symtab = &Section;
      auto Name = ELFFile->getSectionName(Section);
      ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
      if (*Name == ".candidate.dynamic")
        DynamicCandidate = &Section;
      if (*Name == ".candidate.dynstr")
        DynamicStrings = &Section;
      if (*Name == ".candidate.symtab_shndx")
        IndexCandidate = &Section;
      if (*Name == ".candidate.live")
        LiveSection = &Section;
    }
    ASSERT_NE(Symtab, nullptr);
    ASSERT_NE(DynamicCandidate, nullptr);
    ASSERT_NE(DynamicStrings, nullptr);
    ASSERT_NE(IndexCandidate, nullptr);
    ASSERT_NE(LiveSection, nullptr);
    auto Symbols = ELFFile->symbols(Symtab);
    auto Strings = ELFFile->getStringTableForSymtab(*Symtab);
    ASSERT_TRUE(static_cast<bool>(Symbols)) << errorText(Symbols.takeError());
    ASSERT_TRUE(static_cast<bool>(Strings)) << errorText(Strings.takeError());

    const auto Replace = [&](const auto *Original, const auto &Replacement) {
      const auto *Raw = reinterpret_cast<const uint8_t *>(Original);
      ASSERT_GE(Raw, Bytes.data());
      const size_t Offset = static_cast<size_t>(Raw - Bytes.data());
      ASSERT_LE(Offset, Bytes.size());
      ASSERT_LE(sizeof(Replacement), Bytes.size() - Offset);
      std::memcpy(Bytes.data() + Offset, &Replacement, sizeof(Replacement));
    };
    switch (Case) {
    case Corruption::AdditionalSelectedStringTableConsumer: {
      object::ELF64LE::Shdr Replacement = *DynamicCandidate;
      Replacement.sh_type = ELF::SHT_DYNAMIC;
      Replacement.sh_link = Symtab->sh_link;
      Replacement.sh_entsize = sizeof(object::ELF64LE::Dyn);
      Replacement.sh_size = 2U * sizeof(object::ELF64LE::Dyn);
      Replace(DynamicCandidate, Replacement);
      break;
    }
    case Corruption::ExtendedIndexForDirectSymbol: {
      const uint64_t RequiredSize =
          Symbols->size() * sizeof(object::ELF64LE::Word);
      ASSERT_LE(RequiredSize, IndexCandidate->sh_size);
      object::ELF64LE::Shdr Replacement = *IndexCandidate;
      Replacement.sh_type = ELF::SHT_SYMTAB_SHNDX;
      Replacement.sh_link = static_cast<uint32_t>(Symtab - Sections->data());
      Replacement.sh_entsize = sizeof(object::ELF64LE::Word);
      Replacement.sh_size = RequiredSize;
      const uint64_t ContentsOffset = Replacement.sh_offset;
      Replace(IndexCandidate, Replacement);
      ASSERT_LE(ContentsOffset, Bytes.size());
      ASSERT_LE(sizeof(object::ELF64LE::Word), Bytes.size() - ContentsOffset);
      object::ELF64LE::Word NonzeroDirectIndex(1);
      std::memcpy(Bytes.data() + ContentsOffset, &NonzeroDirectIndex,
                  sizeof(NonzeroDirectIndex));
      break;
    }
    case Corruption::MissingExtendedIndexCompanion: {
      const object::ELF64LE::Sym *Target = nullptr;
      for (const object::ELF64LE::Sym &Symbol : *Symbols) {
        auto Name = Symbol.getName(*Strings);
        ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
        if (*Name == "retained_table_target")
          Target = &Symbol;
      }
      ASSERT_NE(Target, nullptr);
      object::ELF64LE::Sym Replacement = *Target;
      Replacement.st_shndx = ELF::SHN_XINDEX;
      Replace(Target, Replacement);
      break;
    }
    case Corruption::DuplicateExtendedIndexCompanion: {
      const uint64_t RequiredSize =
          Symbols->size() * sizeof(object::ELF64LE::Word);
      ASSERT_LE(RequiredSize, IndexCandidate->sh_size);
      ASSERT_LE(RequiredSize, DynamicCandidate->sh_size);
      for (const object::ELF64LE::Shdr *Candidate :
           {IndexCandidate, DynamicCandidate}) {
        object::ELF64LE::Shdr Replacement = *Candidate;
        Replacement.sh_type = ELF::SHT_SYMTAB_SHNDX;
        Replacement.sh_link = static_cast<uint32_t>(Symtab - Sections->data());
        Replacement.sh_info = 0;
        Replacement.sh_entsize = sizeof(object::ELF64LE::Word);
        Replacement.sh_size = RequiredSize;
        Replace(Candidate, Replacement);
      }
      break;
    }
    case Corruption::AdditionalSymbolTableDuringDebugRemoval: {
      object::ELF64LE::Shdr Replacement = *DynamicCandidate;
      Replacement.sh_type = ELF::SHT_DYNSYM;
      Replacement.sh_link =
          static_cast<uint32_t>(DynamicStrings - Sections->data());
      Replacement.sh_info = 1;
      Replacement.sh_entsize = sizeof(object::ELF64LE::Sym);
      Replacement.sh_size = 2U * sizeof(object::ELF64LE::Sym);
      const uint64_t ContentsOffset = Replacement.sh_offset;
      Replace(DynamicCandidate, Replacement);
      ASSERT_LE(ContentsOffset, Bytes.size());
      ASSERT_LE(Replacement.sh_size, Bytes.size() - ContentsOffset);
      object::ELF64LE::Sym DynamicSymbol{};
      DynamicSymbol.st_name = 1;
      DynamicSymbol.st_info =
          static_cast<uint8_t>((ELF::STB_GLOBAL << 4) | ELF::STT_OBJECT);
      DynamicSymbol.st_shndx =
          static_cast<uint16_t>(LiveSection - Sections->data());
      std::memcpy(Bytes.data() + ContentsOffset + sizeof(DynamicSymbol),
                  &DynamicSymbol, sizeof(DynamicSymbol));
      break;
    }
    }

    const StringRef Corrupted(reinterpret_cast<const char *>(Bytes.data()),
                              Bytes.size());
    auto Canonical = canonicalizeBuiltinELFTables(
        Corrupted, Case == Corruption::AdditionalSymbolTableDuringDebugRemoval);
    EXPECT_FALSE(static_cast<bool>(Canonical));
    if (!Canonical) {
      const std::string Message = errorText(Canonical.takeError());
      if (Case == Corruption::MissingExtendedIndexCompanion)
        EXPECT_NE(Message.find("extended section index"), std::string::npos)
            << Message;
      if (Case == Corruption::DuplicateExtendedIndexCompanion)
        EXPECT_NE(Message.find("SHT_SYMTAB_SHNDX"), std::string::npos)
            << Message;
    }
  }
}

std::string hexBytes(ArrayRef<uint8_t> Bytes) {
  std::string Text;
  raw_string_ostream OS(Text);
  for (uint8_t Byte : Bytes)
    OS << format_hex_no_prefix(Byte, 2) << ' ';
  OS.flush();
  return Text;
}

// A section's bytes are the part of a rewrite that no metadata comparison
// covers: a relocation restated as a data directive can come back with matching
// kind, width and target while the instruction underneath it has been replaced.
struct RewriteOutcome {
  bool Written = false;
  std::vector<uint8_t> Before;
  std::vector<uint8_t> After;
};

Expected<RewriteOutcome> rewriteTextSection(PluginTaskContext &Task,
                                            const BuiltinTargetRoute &Route,
                                            StringRef Assembly,
                                            StringRef OutputName) {
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  if (!Snapshot)
    return Snapshot.takeError();
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  if (!Reader)
    return Reader.takeError();
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  if (!Writer)
    return Writer.takeError();
  auto Target = makeBuiltinTargetKey(Route);
  if (!Target)
    return Target.takeError();
  OwnedTargetKey ReadTarget = *Target;
  auto Input = assembleSource(Route, Assembly);
  if (!Input)
    return Input.takeError();

  auto Before = (*Reader)->read(Task, *Input, "before.o", *Target);
  if (!Before)
    return Before.takeError();
  const auto TextBefore = llvm::find_if(
      (*Before)->sections(), [](const PluginObjectSection &Value) {
        return Value.Kind == NEVERC_OBJECT_SECTION_KIND_TEXT;
      });
  if (TextBefore == (*Before)->sections().end())
    return createStringError(inconvertibleErrorCode(), "no text section");

  RewriteOutcome Outcome;
  Outcome.Before.assign(TextBefore->Data.begin(), TextBefore->Data.end());
  (*Before)->issueLayoutProof();
  auto Candidate = (*Writer)->write(
      Task, **Before,
      ObjectOutputDestination::memory(OutputName.str(), UINT64_C(1) << 20));
  if (!Candidate) {
    // A writer that cannot restate the relocation faithfully is expected to
    // say so; that is the outcome under test, not an error.
    consumeError(Candidate.takeError());
    return Outcome;
  }
  if (Error E = (*Candidate)->verify())
    return std::move(E);
  auto Committed = (*Candidate)->commit();
  if (!Committed)
    return Committed.takeError();
  auto Output = findPluginMemoryOutput(Task, OutputName.str());
  if (!Output)
    return createStringError(inconvertibleErrorCode(), "no output");
  auto After =
      (*Reader)->read(Task, Output->Bytes, OutputName.str(), ReadTarget);
  if (!After)
    return After.takeError();
  const auto TextAfter =
      llvm::find_if((*After)->sections(), [](const PluginObjectSection &Value) {
        return Value.Kind == NEVERC_OBJECT_SECTION_KIND_TEXT;
      });
  if (TextAfter == (*After)->sections().end())
    return createStringError(inconvertibleErrorCode(), "no text section");
  Outcome.Written = true;
  Outcome.After.assign(TextAfter->Data.begin(), TextAfter->Data.end());
  return Outcome;
}

TEST(PluginBuiltinObjectFormatTest, ELFRewriteKeepsInstructionBytesIntact) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);

  // A `bl` puts the relocated field inside the instruction. ELF states that
  // with .reloc, which leaves the bytes untouched.
  auto Outcome = rewriteTextSection(Scope.task(), *Route,
                                    "\t.text\n"
                                    "\t.globl\tcaller\n"
                                    "caller:\n"
                                    "\tbl\tcallee\n"
                                    "\tret\n",
                                    "elf-branch.o");
  ASSERT_TRUE(static_cast<bool>(Outcome)) << errorText(Outcome.takeError());
  EXPECT_TRUE(Outcome->Written) << "ELF should be able to rewrite a branch";
  EXPECT_EQ(Outcome->Before, Outcome->After)
      << "before = " << hexBytes(Outcome->Before)
      << "\nafter  = " << hexBytes(Outcome->After);
}

TEST(PluginBuiltinObjectFormatTest, COFFRefusesRewriteItCannotSpellFaithfully) {
  initializeBuiltinTargets();
  const std::array<std::pair<Triple::ArchType, const char *>, 2> Cases = {
      {{Triple::aarch64, "\t.text\n"
                         "\t.globl\tcaller\n"
                         "caller:\n"
                         "\tbl\tcallee\n"
                         "\tret\n"},
       {Triple::x86_64, "\t.text\n"
                        "\t.globl\tcaller\n"
                        "caller:\n"
                        "\tcallq\tcallee\n"
                        "\tretq\n"}}};
  for (const auto &[Arch, Source] : Cases) {
    BuiltinObjectTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    const BuiltinTargetRoute *Route = routeFor(BuiltinObjectFormat::COFF, Arch);
    ASSERT_NE(Route, nullptr);
    SCOPED_TRACE(Route->CanonicalName.str());

    // COFF has no .reloc for these forms, so the only way to write them would
    // be to overwrite the instruction with a data directive. The writer has to
    // refuse instead of producing an object whose code no longer matches.
    auto Outcome =
        rewriteTextSection(Scope.task(), *Route, Source, "coff-branch.o");
    ASSERT_TRUE(static_cast<bool>(Outcome)) << errorText(Outcome.takeError());
    if (Outcome->Written)
      EXPECT_EQ(Outcome->Before, Outcome->After)
          << "rewrite changed the instruction bytes\nbefore = "
          << hexBytes(Outcome->Before)
          << "\nafter  = " << hexBytes(Outcome->After);
  }
}

TEST(PluginBuiltinObjectFormatTest,
     ReaderClassifiesSectionsByRoleNotSubstring) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader));

  struct Case {
    BuiltinObjectFormat Format;
    Triple::ArchType Arch;
    const char *Source;
    const char *SectionName;
    NevercObjectSectionKind Expected;
  };
  // A function section whose name merely contains "unwind" is code, and COFF
  // thread-local data lives in ".tls$...". Both were misread while the reader
  // matched substrings.
  const std::array<Case, 5> Cases = {
      {{BuiltinObjectFormat::ELF, Triple::x86_64,
        "\t.section\t.text._Unwind_Resume,\"ax\",@progbits\n"
        "\t.globl\t_Unwind_Resume\n"
        "_Unwind_Resume:\n"
        "\t.byte\t0xc3\n",
        ".text._Unwind_Resume", NEVERC_OBJECT_SECTION_KIND_TEXT},
       {BuiltinObjectFormat::ELF, Triple::x86_64,
        "\t.section\t.eh_frame,\"a\",@progbits\n"
        "\t.long\t0\n",
        ".eh_frame", NEVERC_OBJECT_SECTION_KIND_UNWIND},
       // Unwind data is split per function under -ffunction-sections, and the
       // GNU toolchains put it in ".eh_frame" even when they target COFF.
       {BuiltinObjectFormat::ELF, Triple::x86_64,
        "\t.section\t.eh_frame.probe,\"a\",@progbits\n"
        "\t.long\t0\n",
        ".eh_frame.probe", NEVERC_OBJECT_SECTION_KIND_UNWIND},
       {BuiltinObjectFormat::COFF, Triple::x86_64,
        "\t.section\t.eh_frame,\"dr\"\n"
        "\t.long\t0\n",
        ".eh_frame", NEVERC_OBJECT_SECTION_KIND_UNWIND},
       {BuiltinObjectFormat::COFF, Triple::x86_64,
        "\t.section\t.tls$AAA,\"dw\"\n"
        "\t.globl\ttls_value\n"
        "tls_value:\n"
        "\t.quad\t0\n",
        ".tls$AAA", NEVERC_OBJECT_SECTION_KIND_TLS_DATA}}};

  for (const Case &Value : Cases) {
    const BuiltinTargetRoute *Route = routeFor(Value.Format, Value.Arch);
    ASSERT_NE(Route, nullptr);
    SCOPED_TRACE(Value.SectionName);
    auto Input = assembleSource(*Route, Value.Source);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
    auto Target = makeBuiltinTargetKey(*Route);
    ASSERT_TRUE(static_cast<bool>(Target));
    auto Graph = (*Reader)->read(Scope.task(), *Input, "roles.o", *Target);
    ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
    const auto It = llvm::find_if((*Graph)->sections(),
                                  [&](const PluginObjectSection &Section) {
                                    return Section.Name == Value.SectionName;
                                  });
    ASSERT_NE(It, (*Graph)->sections().end());
    EXPECT_EQ(It->Kind, Value.Expected);
  }
}

TEST(PluginBuiltinObjectFormatTest, WriterPlacesEveryZeroFillSymbol) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader));
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer));

  // Two globals in .bss: the second one sits at a non-zero offset inside the
  // zero fill, which is where a writer that appends the fill in one run has
  // nowhere to put it.
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::x86_64);
  ASSERT_NE(Route, nullptr);
  auto Target = makeBuiltinTargetKey(*Route);
  ASSERT_TRUE(static_cast<bool>(Target));
  OwnedTargetKey ReadTarget = *Target;
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".bss";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_ZERO_FILL;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  Section.Alignment = 8;
  Section.ZeroFillSize = 24;
  const uint64_t SectionID = Section.ID;
  Graph.sections().push_back(std::move(Section));
  for (unsigned I = 0; I != 3; ++I) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph.allocateEntityID();
    Symbol.Name = "bss_value_" + std::to_string(I);
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = SectionID;
    Symbol.Value = I * 8;
    Symbol.Size = 8;
    Symbol.Alignment = 8;
    Graph.symbols().push_back(std::move(Symbol));
  }
  Graph.issueLayoutProof();

  auto Candidate = (*Writer)->write(
      Scope.task(), Graph,
      ObjectOutputDestination::memory("bss.o", UINT64_C(1) << 20));
  ASSERT_TRUE(static_cast<bool>(Candidate)) << errorText(Candidate.takeError());
  ASSERT_FALSE((*Candidate)->verify());
  auto Committed = (*Candidate)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());
  auto Output = findPluginMemoryOutput(Scope.task(), "bss.o");
  ASSERT_TRUE(Output.has_value());
  auto Restored =
      (*Reader)->read(Scope.task(), Output->Bytes, "bss.o", ReadTarget);
  ASSERT_TRUE(static_cast<bool>(Restored)) << errorText(Restored.takeError());
  for (unsigned I = 0; I != 3; ++I) {
    const std::string Name = "bss_value_" + std::to_string(I);
    const auto It = llvm::find_if(
        (*Restored)->symbols(),
        [&](const PluginObjectSymbol &Symbol) { return Symbol.Name == Name; });
    ASSERT_NE(It, (*Restored)->symbols().end()) << Name << " was dropped";
    EXPECT_EQ(It->Value, I * 8) << Name << " moved";
  }
}

TEST(PluginBuiltinObjectFormatTest, ReaderHandlesTheRelocationsRealCodeEmits) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader));

  // The reader derives a relocation's width and addressing from its type
  // number and refuses types it does not know, so this exercises the range an
  // ordinary translation unit produces: a PLT call, a GOT load, the two
  // thread-local models, a page/offset address pair, and plain data pointers.
  struct Case {
    BuiltinObjectFormat Format;
    Triple::ArchType Arch;
    const char *Source;
  };
  const std::array<Case, 3> Cases = {
      {{BuiltinObjectFormat::ELF, Triple::x86_64,
        "\t.text\n"
        "\t.globl\tcaller\n"
        "caller:\n"
        "\tcallq\tcallee@PLT\n"
        "\tmovq\tglobal@GOTPCREL(%rip), %rax\n"
        "\tmovq\ttls_value@GOTTPOFF(%rip), %rcx\n"
        "\tleaq\tglobal(%rip), %rdx\n"
        "\tretq\n"
        "\t.data\n"
        "pointer:\n"
        "\t.quad\tcallee\n"
        "\t.long\tglobal-.\n"
        "\t.section\t.tdata,\"awT\",@progbits\n"
        "tls_value:\n"
        "\t.quad\t0\n"},
       {BuiltinObjectFormat::ELF, Triple::aarch64,
        "\t.text\n"
        "\t.globl\tcaller\n"
        "caller:\n"
        "\tbl\tcallee\n"
        "\tadrp\tx0, :got:global\n"
        "\tldr\tx0, [x0, :got_lo12:global]\n"
        "\tadrp\tx1, global\n"
        "\tadd\tx1, x1, :lo12:global\n"
        "\tadd\tx2, x2, :tprel_hi12:tls_value\n"
        "\tadd\tx2, x2, :tprel_lo12_nc:tls_value\n"
        "\tret\n"
        "\t.data\n"
        "pointer:\n"
        "\t.quad\tcallee\n"
        "\t.long\tglobal-.\n"
        "\t.section\t.tdata,\"awT\",@progbits\n"
        "tls_value:\n"
        "\t.quad\t0\n"},
       {BuiltinObjectFormat::COFF, Triple::x86_64,
        "\t.text\n"
        "\t.globl\tcaller\n"
        "caller:\n"
        "\tcallq\tcallee\n"
        "\tretq\n"
        "\t.data\n"
        "pointer:\n"
        "\t.quad\tcallee\n"
        "\t.long\tcaller\n"
        "\t.secrel32\tcaller\n"
        "\t.rva\tcaller\n"}}};

  for (const Case &Value : Cases) {
    const BuiltinTargetRoute *Route = routeFor(Value.Format, Value.Arch);
    ASSERT_NE(Route, nullptr);
    SCOPED_TRACE(Route->CanonicalName.str());
    auto Input = assembleSource(*Route, Value.Source);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
    auto Target = makeBuiltinTargetKey(*Route);
    ASSERT_TRUE(static_cast<bool>(Target));
    auto Graph = (*Reader)->read(Scope.task(), *Input, "coverage.o", *Target);
    ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
    EXPECT_GT((*Graph)->relocationCount(), 0U);
  }
}

TEST(PluginBuiltinObjectFormatTest, WriterAcceptsManglingsThatNeedQuoting) {
  initializeBuiltinTargets();
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));

  // Real manglings hold characters the assembler reads as syntax: MSVC uses
  // '?', Objective-C spells a method "-[Class method]".
  struct Case {
    BuiltinObjectFormat Format;
    Triple::ArchType Arch;
    const char *SectionName;
    const char *SymbolName;
  };
  const std::array<Case, 2> Cases = {
      {{BuiltinObjectFormat::COFF, Triple::x86_64, ".text", "?value@@YAXH@Z"},
       {BuiltinObjectFormat::MachO, Triple::aarch64, "__text",
        "-[ProbeClass probeMethod]"}}};

  for (const Case &Value : Cases) {
    BuiltinObjectTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    auto Writer = ObjectWriterProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Writer));
    auto Reader = ObjectReaderProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Reader));
    const BuiltinTargetRoute *Route = routeFor(Value.Format, Value.Arch);
    ASSERT_NE(Route, nullptr);
    SCOPED_TRACE(Value.SymbolName);
    auto Target = makeBuiltinTargetKey(*Route);
    ASSERT_TRUE(static_cast<bool>(Target));
    OwnedTargetKey ReadTarget = *Target;
    PluginObjectGraph Graph(std::move(*Target));
    PluginObjectSection Section;
    Section.ID = Graph.allocateEntityID();
    Section.Name = Value.SectionName;
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Section.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    Section.Alignment = 4;
    Section.Data = {UINT8_C(0xc0), UINT8_C(0x03), UINT8_C(0x5f), UINT8_C(0xd6)};
    const uint64_t SectionID = Section.ID;
    Graph.sections().push_back(std::move(Section));
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph.allocateEntityID();
    Symbol.Name = Value.SymbolName;
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = SectionID;
    Symbol.Size = 4;
    Symbol.Alignment = 4;
    Graph.symbols().push_back(std::move(Symbol));
    Graph.issueLayoutProof();

    auto Candidate = (*Writer)->write(
        Scope.task(), Graph,
        ObjectOutputDestination::memory("mangled.o", UINT64_C(1) << 20));
    ASSERT_TRUE(static_cast<bool>(Candidate))
        << errorText(Candidate.takeError());
    ASSERT_FALSE((*Candidate)->verify());
    auto Committed = (*Candidate)->commit();
    ASSERT_TRUE(static_cast<bool>(Committed))
        << errorText(Committed.takeError());
    auto Output = findPluginMemoryOutput(Scope.task(), "mangled.o");
    ASSERT_TRUE(Output.has_value());
    auto Restored =
        (*Reader)->read(Scope.task(), Output->Bytes, "mangled.o", ReadTarget);
    ASSERT_TRUE(static_cast<bool>(Restored)) << errorText(Restored.takeError());
    const auto It =
        llvm::find_if((*Restored)->symbols(), [&](const PluginObjectSymbol &S) {
          return S.Name == Value.SymbolName;
        });
    EXPECT_NE(It, (*Restored)->symbols().end())
        << "symbol did not survive the round trip";
  }
}

TEST(PluginBuiltinObjectFormatTest, ReaderAcceptsMarkerRelocations) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader));

  // A marker names an offset without patching a field there, so it has no
  // width to derive. Refusing one costs the whole object: R_*_NONE survives
  // any `ld -r`, and the descriptor call is in every -mtls-dialect=gnu2 build.
  struct Case {
    Triple::ArchType Arch;
    const char *Source;
  };
  const std::array<Case, 3> Cases = {
      {{Triple::x86_64, "\t.text\n"
                        "caller:\n"
                        "\tnop\n"
                        "\t.reloc\t0, R_X86_64_NONE, callee\n"},
       {Triple::aarch64, "\t.text\n"
                         "caller:\n"
                         "\tnop\n"
                         "\t.reloc\t0, R_AARCH64_NONE, callee\n"},
       {Triple::x86_64, "\t.text\n"
                        "caller:\n"
                        "\tleaq\ttls_value@tlsdesc(%rip), %rax\n"
                        "\tcallq\t*tls_value@tlscall(%rax)\n"}}};

  for (const Case &Value : Cases) {
    const BuiltinTargetRoute *Route =
        routeFor(BuiltinObjectFormat::ELF, Value.Arch);
    ASSERT_NE(Route, nullptr);
    SCOPED_TRACE(Value.Source);
    auto Input = assembleSource(*Route, Value.Source);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
    auto Target = makeBuiltinTargetKey(*Route);
    ASSERT_TRUE(static_cast<bool>(Target));
    auto Graph = (*Reader)->read(Scope.task(), *Input, "marker.o", *Target);
    EXPECT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
  }
}

TEST(PluginBuiltinObjectFormatTest, WriterRefusesNamesTheAssemblerCannotCarry) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer));
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::x86_64);
  ASSERT_NE(Route, nullptr);
  auto Target = makeBuiltinTargetKey(*Route);
  ASSERT_TRUE(static_cast<bool>(Target));

  // A NUL ends the assembler's input wherever it appears, quoted or not, so
  // this name would come back as "ab" with nothing to say it had been cut.
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 4;
  Section.Data = {UINT8_C(0x90)};
  const uint64_t SectionID = Section.ID;
  Graph.sections().push_back(std::move(Section));
  PluginObjectSymbol Symbol;
  Symbol.ID = Graph.allocateEntityID();
  Symbol.Name = std::string("ab\0cd", 5);
  Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Symbol.SectionID = SectionID;
  Symbol.Size = 1;
  Symbol.Alignment = 1;
  Graph.symbols().push_back(std::move(Symbol));
  Graph.issueLayoutProof();

  auto Candidate = (*Writer)->write(
      Scope.task(), Graph,
      ObjectOutputDestination::memory("nul.o", UINT64_C(1) << 20));
  EXPECT_FALSE(static_cast<bool>(Candidate))
      << "a name holding a NUL was written out truncated";
  if (!Candidate)
    consumeError(Candidate.takeError());
}

TEST(PluginBuiltinObjectFormatTest,
     WriterRefusesWidthThatContradictsNativeType) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader));
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer));
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::COFF, Triple::x86_64);
  ASSERT_NE(Route, nullptr);
  auto Input = assembleSource(*Route, "\t.text\n"
                                      "\t.globl\tcaller\n"
                                      "caller:\n"
                                      "\tretq\n"
                                      "\t.data\n"
                                      "\t.quad\tcaller\n"
                                      "\t.long\t0xdeadbeef\n");
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Target = makeBuiltinTargetKey(*Route);
  ASSERT_TRUE(static_cast<bool>(Target));
  auto Graph = (*Reader)->read(Scope.task(), *Input, "width.o", *Target);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

  // The width and the native type are two statements about one field. Left to
  // disagree, the writer picked .quad from the type and then advanced the
  // cursor by the width, which shifted every byte after it -- and the object
  // still assembled, so nothing said so.
  bool Narrowed = false;
  for (PluginObjectRelocation &Relocation : (*Graph)->relocations())
    if (Relocation.Width == 64) {
      Relocation.Width = 32;
      Narrowed = true;
    }
  ASSERT_TRUE(Narrowed) << "expected a 64-bit relocation to contradict";
  (*Graph)->issueLayoutProof();

  auto Candidate = (*Writer)->write(
      Scope.task(), **Graph,
      ObjectOutputDestination::memory("width.o", UINT64_C(1) << 20));
  EXPECT_FALSE(static_cast<bool>(Candidate))
      << "the contradiction was written out as a shifted section";
  if (!Candidate)
    consumeError(Candidate.takeError());
}

// The writer turns a graph into assembly text and hands it to MC, and MC
// answers a good many inputs it dislikes by calling report_fatal_error, which
// ends the host process instead of failing the write. Three of those were
// reachable from an ordinary graph -- a common symbol wanting more than 32
// bytes of alignment, two common symbols sharing a name, two sections claiming
// one COMDAT -- and nothing was pushing varied graphs through the writer to
// find them. The object-graph fuzzer builds a graph and stops at the verifier,
// which is also where a graph built from raw random bytes stops: almost none
// of them satisfy the invariants the verifier holds the graph to, so they
// never reach the assembly text at all.
//
// So the graph here starts as one the reader produced and is then altered a
// field at a time, which keeps it plausible enough to be written while still
// varying what the writer has to reason about.
TEST(PluginBuiltinObjectFormatTest, WriterAnswersAlteredGraphsWithoutAborting) {
  initializeBuiltinTargets();
  const std::array<std::pair<BuiltinObjectFormat, Triple::ArchType>, 4> Routes =
      {{{BuiltinObjectFormat::ELF, Triple::x86_64},
        {BuiltinObjectFormat::ELF, Triple::aarch64},
        {BuiltinObjectFormat::COFF, Triple::x86_64},
        {BuiltinObjectFormat::MachO, Triple::aarch64}}};
  // Holds a defined function, a pointer needing a relocation and a common
  // symbol, so every path the writer takes is represented. The alignment is
  // spelled twice because ".comm" reads it as a byte count on ELF and as a
  // log2 exponent elsewhere -- the same split the writer has to bridge.
  auto sourceFor = [](BuiltinObjectFormat Format) {
    return std::string("\t.text\n"
                       "\t.globl\tfn\n"
                       "fn:\n"
                       "\t.byte\t0\n"
                       "\t.data\n"
                       "\t.globl\tptr\n"
                       "ptr:\n"
                       "\t.quad\tfn\n"
                       "\t.comm\tshared,8,") +
           (Format == BuiltinObjectFormat::ELF ? "8" : "3") + "\n";
  };

  std::mt19937_64 Random(0x6e657665726331ULL);
  auto pick = [&Random](uint64_t Bound) { return Random() % Bound; };
  // Values on and around the boundaries the writer reasons about: the field
  // widths it knows, the alignments COFF stops at, and a size that overruns.
  const std::array<uint64_t, 11> Interesting = {
      {0, 1, 2, 4, 8, 16, 32, 64, 128, 4096, UINT64_MAX}};

  unsigned Written = 0;
  for (const auto &[Format, Arch] : Routes) {
    const BuiltinTargetRoute *Route = routeFor(Format, Arch);
    ASSERT_NE(Route, nullptr);
    SCOPED_TRACE(Route->CanonicalName.str());
    BuiltinObjectTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    auto Snapshot = PluginTargetRegistry::freeze(
        ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
    ASSERT_TRUE(static_cast<bool>(Snapshot));
    auto Reader = ObjectReaderProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Reader));
    auto Writer = ObjectWriterProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Writer));
    auto Input = assembleSource(*Route, sourceFor(Format));
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

    for (unsigned Iteration = 0; Iteration != 600; ++Iteration) {
      auto Target = makeBuiltinTargetKey(*Route);
      ASSERT_TRUE(static_cast<bool>(Target));
      auto Graph = (*Reader)->read(Scope.task(), *Input, "altered.o", *Target);
      ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

      // The unaltered graph has to survive the trip, or the alterations below
      // are being judged against a baseline that never worked.
      if (Iteration == 0) {
        (*Graph)->issueLayoutProof();
        auto Baseline = (*Writer)->write(
            Scope.task(), **Graph,
            ObjectOutputDestination::memory("base.o", UINT64_C(1) << 20));
        ASSERT_TRUE(static_cast<bool>(Baseline))
            << "baseline graph does not survive a rewrite: "
            << errorText(Baseline.takeError());
        continue;
      }

      // Mostly values the writer accepts, so that an iteration gets far enough
      // to produce assembly, with a boundary value a quarter of the time.
      auto edge = [&] { return Interesting[pick(Interesting.size())]; };
      // Weighted towards the boundary rather than away from it: the alignment
      // a common symbol asks for is one of the few fields that reaches a fatal
      // path in MC, so a generator that mostly picks safe values here would
      // take thousands of iterations to say anything about it.
      auto alignment = [&]() -> uint64_t {
        static const std::array<uint64_t, 6> Fine = {{1, 2, 4, 8, 16, 32}};
        return pick(2) == 0 ? edge() : Fine[pick(Fine.size())];
      };
      auto width = [&]() -> uint32_t {
        static const std::array<uint32_t, 4> Fine = {{8, 16, 32, 64}};
        return pick(4) == 0 ? static_cast<uint32_t>(edge())
                            : Fine[pick(Fine.size())];
      };
      auto offset = [&] { return pick(4) == 0 ? edge() : pick(9); };

      // Exactly one field per iteration. The writer now holds a graph to
      // several consistency rules of its own -- a width that matches the
      // native type, an offset inside the section -- so altering three or four
      // fields at once got the graph turned away before any assembly was
      // produced, and the pass beyond that point went untested.
      unsigned Site = 0;
      const unsigned Choice = static_cast<unsigned>(pick(34));
      auto here = [&] { return Site++ == Choice; };

      for (PluginObjectSection &Section : (*Graph)->sections()) {
        if (here())
          Section.Alignment = alignment();
        if (here())
          Section.Flags ^= UINT64_C(1) << pick(10);
        if (here())
          Section.ZeroFillSize = offset();
      }
      for (PluginObjectSymbol &Symbol : (*Graph)->symbols()) {
        if (here())
          Symbol.Alignment = alignment();
        if (here())
          Symbol.Value = offset();
        if (here())
          Symbol.Size = offset();
        if (here())
          Symbol.Definition =
              static_cast<NevercObjectSymbolDefinition>(pick(5));
        // A repeated name is what two ".comm" directives of one name come
        // from, and MC ends the process over that pair.
        if (here())
          Symbol.Name = "shared";
      }
      for (PluginObjectRelocation &Relocation : (*Graph)->relocations()) {
        if (here())
          Relocation.Width = width();
        if (here())
          Relocation.Kind = static_cast<NevercObjectRelocationKind>(pick(8));
        if (here())
          Relocation.Addend = static_cast<int64_t>(offset()) - 8;
        if (here())
          Relocation.Offset = offset();
        if (here())
          Relocation.IsPCRelative = !Relocation.IsPCRelative;
      }

      (*Graph)->issueLayoutProof();
      // Success and failure are both fine; reaching the next iteration is the
      // property under test. The output name has to differ each time -- a
      // repeat is refused by the task before the writer runs at all, which is
      // a failure that says nothing about the graph.
      auto Candidate = (*Writer)->write(
          Scope.task(), **Graph,
          ObjectOutputDestination::memory(
              "altered" + std::to_string(Iteration) + ".o", UINT64_C(1) << 20));
      if (Candidate)
        ++Written;
      else
        consumeError(Candidate.takeError());
    }
  }
  // Without this the test would still pass if every graph were turned away
  // before the writer ever produced assembly, which is how an earlier version
  // of it managed to hold no opinion at all.
  EXPECT_GT(Written, 100u)
      << "no altered graph reached the assembler, so nothing was exercised";
}

TEST(PluginBuiltinObjectFormatTest,
     WriterRefusesKindThatContradictsNativeType) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader));
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer));
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::COFF, Triple::x86_64);
  ASSERT_NE(Route, nullptr);
  auto Input = assembleSource(*Route, "\t.text\n"
                                      "\t.globl\tcaller\n"
                                      "caller:\n"
                                      "\tretq\n"
                                      "\t.data\n"
                                      "\t.long\tcaller\n");
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Target = makeBuiltinTargetKey(*Route);
  ASSERT_TRUE(static_cast<bool>(Target));
  auto Graph = (*Reader)->read(Scope.task(), *Input, "kind.o", *Target);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

  // A COFF relocation type number means different things on different
  // architectures -- 2 is AMD64's ADDR32 and ARM64's ADDR32NB -- and the object
  // format ID the extension is stamped with does not name an architecture. A
  // graph whose kind says image-relative while its native type reads as
  // absolute is what carrying one across arrives at, and the writer used to
  // follow the type and emit .long, quietly turning an image-relative
  // reference into an absolute one.
  bool Contradicted = false;
  for (PluginObjectRelocation &Relocation : (*Graph)->relocations())
    if (Relocation.Kind == NEVERC_OBJECT_RELOCATION_ABSOLUTE &&
        Relocation.Width == 32) {
      Relocation.Kind = NEVERC_OBJECT_RELOCATION_IMAGE_RELATIVE;
      Contradicted = true;
    }
  ASSERT_TRUE(Contradicted) << "expected a 32-bit absolute relocation";
  (*Graph)->issueLayoutProof();

  auto Candidate = (*Writer)->write(
      Scope.task(), **Graph,
      ObjectOutputDestination::memory("kind.o", UINT64_C(1) << 20));
  EXPECT_FALSE(static_cast<bool>(Candidate))
      << "the contradiction was written out as a different relocation";
  if (!Candidate)
    consumeError(Candidate.takeError());
}

// The role predicates are the one answer four call sites share -- the built-in
// reader and the ELF and COFF link adapters -- and three of those reach them
// with a native section type the others never see, so nothing but these tests
// compares the answers. Each case below is a spelling that one call site got
// wrong while each kept its own copy.
TEST(PluginBuiltinObjectFormatTest, SectionRolePredicatesAgreeOnRealSpellings) {
  struct Case {
    BuiltinObjectFormat Format;
    const char *Name;
    bool Unwind;
    bool Debug;
    bool ThreadLocal;
  };
  const std::array<Case, 22> Cases = {{
      // Code that merely mentions unwinding is still code.
      {BuiltinObjectFormat::ELF, ".text._Unwind_Resume", false, false, false},
      {BuiltinObjectFormat::ELF, ".eh_frame", true, false, false},
      {BuiltinObjectFormat::ELF, ".eh_frame.f", true, false, false},
      {BuiltinObjectFormat::ELF, ".eh_frame_hdr", true, false, false},
      {BuiltinObjectFormat::ELF, ".gcc_except_table.f", true, false, false},
      {BuiltinObjectFormat::ELF, ".ARM.exidx", true, false, false},
      {BuiltinObjectFormat::ELF, ".debug_info", false, true, false},
      {BuiltinObjectFormat::ELF, ".zdebug_info", false, true, false},
      {BuiltinObjectFormat::ELF, ".tdata", false, false, true},
      {BuiltinObjectFormat::ELF, ".tbss.f", false, false, true},
      // ".tdatafoo" is a different section, not thread-local data.
      {BuiltinObjectFormat::ELF, ".tdatafoo", false, false, false},
      // A COMDAT carries a "$key" suffix that the linker groups by; the part
      // before it is the real name, and matching the whole string missed it.
      {BuiltinObjectFormat::COFF, ".pdata", true, false, false},
      {BuiltinObjectFormat::COFF, ".pdata$foo", true, false, false},
      {BuiltinObjectFormat::COFF, ".xdata$bar", true, false, false},
      {BuiltinObjectFormat::COFF, ".eh_frame", true, false, false},
      {BuiltinObjectFormat::COFF, ".debug$S", false, true, false},
      // COFF records no thread-local bit, and no spelling of ".tls$..."
      // matched the test before.
      {BuiltinObjectFormat::COFF, ".tls", false, false, true},
      {BuiltinObjectFormat::COFF, ".tls$ZZZ", false, false, true},
      {BuiltinObjectFormat::COFF, ".text", false, false, false},
      {BuiltinObjectFormat::MachO, "__eh_frame", true, false, false},
      {BuiltinObjectFormat::MachO, "__debug_info", false, true, false},
      {BuiltinObjectFormat::MachO, "__thread_bss", false, false, true},
  }};
  for (const auto &[Format, Name, Unwind, Debug, ThreadLocal] : Cases) {
    SCOPED_TRACE(Name);
    EXPECT_EQ(isUnwindSectionName(Format, Name), Unwind);
    EXPECT_EQ(isDebugSectionName(Format, Name), Debug);
    EXPECT_EQ(isThreadLocalSectionName(Format, Name), ThreadLocal);
  }
}

TEST(PluginBuiltinObjectFormatTest,
     CommonSymbolKeepsItsAlignmentAcrossFormats) {
  initializeBuiltinTargets();
  struct Case {
    BuiltinObjectFormat Format;
    Triple::ArchType Arch;
    const char *Source;
  };
  // ".comm" states its alignment in bytes on ELF and as a log2 exponent on
  // Mach-O and COFF. Written the ELF way everywhere, an 8-byte alignment read
  // back as 2^8, so a common symbol came out of the rewrite wanting 256 bytes.
  const std::array<Case, 3> Cases = {
      {{BuiltinObjectFormat::ELF, Triple::x86_64, "\t.comm\tx,4,8\n"},
       {BuiltinObjectFormat::MachO, Triple::aarch64, "\t.comm\t_x,4,3\n"},
       {BuiltinObjectFormat::COFF, Triple::x86_64, "\t.comm\tx,4,3\n"}}};
  for (const auto &[Format, Arch, Source] : Cases) {
    BuiltinObjectTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    auto Snapshot = PluginTargetRegistry::freeze(
        ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
    ASSERT_TRUE(static_cast<bool>(Snapshot));
    auto Reader = ObjectReaderProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Reader));
    auto Writer = ObjectWriterProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Writer));
    const BuiltinTargetRoute *Route = routeFor(Format, Arch);
    ASSERT_NE(Route, nullptr);
    SCOPED_TRACE(Route->CanonicalName.str());
    auto Target = makeBuiltinTargetKey(*Route);
    ASSERT_TRUE(static_cast<bool>(Target));
    OwnedTargetKey ReadTarget = *Target;
    auto Input = assembleSource(*Route, Source);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

    auto Before = (*Reader)->read(Scope.task(), *Input, "before.o", *Target);
    ASSERT_TRUE(static_cast<bool>(Before)) << errorText(Before.takeError());
    auto findCommon = [](PluginObjectGraph &Graph) {
      return llvm::find_if(
          Graph.symbols(), [](const PluginObjectSymbol &Value) {
            return Value.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON;
          });
    };
    const auto CommonBefore = findCommon(**Before);
    ASSERT_NE(CommonBefore, (*Before)->symbols().end());
    EXPECT_EQ(CommonBefore->Alignment, UINT64_C(8))
        << "the source asked for 8-byte alignment";
    const uint64_t AlignmentBefore = CommonBefore->Alignment;
    (*Before)->issueLayoutProof();

    auto Candidate = (*Writer)->write(
        Scope.task(), **Before,
        ObjectOutputDestination::memory("common.o", UINT64_C(1) << 20));
    ASSERT_TRUE(static_cast<bool>(Candidate))
        << errorText(Candidate.takeError());
    ASSERT_FALSE(static_cast<bool>((*Candidate)->verify()));
    auto Committed = (*Candidate)->commit();
    ASSERT_TRUE(static_cast<bool>(Committed))
        << errorText(Committed.takeError());
    auto Output = findPluginMemoryOutput(Scope.task(), "common.o");
    ASSERT_TRUE(Output.has_value());
    auto After =
        (*Reader)->read(Scope.task(), Output->Bytes, "common.o", ReadTarget);
    ASSERT_TRUE(static_cast<bool>(After)) << errorText(After.takeError());
    const auto CommonAfter = findCommon(**After);
    ASSERT_NE(CommonAfter, (*After)->symbols().end());
    EXPECT_EQ(CommonAfter->Alignment, AlignmentBefore)
        << "the rewrite changed the alignment of a common symbol";
  }
}

TEST(PluginBuiltinObjectFormatTest, MachORewriteKeepsInstructionBytesIntact) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader));
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer));
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::MachO, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  auto Target = makeBuiltinTargetKey(*Route);
  ASSERT_TRUE(static_cast<bool>(Target));
  OwnedTargetKey ReadTarget = *Target;

  // ARM64_RELOC_PAGEOFF12 patches the immediate inside an `add`, and it is
  // neither PC-relative nor GOT- or TLS-bound, so it reaches the writer
  // looking exactly like a plain absolute pointer. What kept a .long off it
  // was the guard that refuses relocations in an executable section -- but
  // "executable" on Mach-O is read from S_ATTR_PURE_INSTRUCTIONS, which a
  // section holding code need not carry. This one does not, so the guard let
  // the instruction through to be overwritten.
  auto Input = assembleSource(*Route, "\t.section\t__TEXT,__mycode,regular\n"
                                      "\t.globl\t_f\n"
                                      "_f:\n"
                                      "\tadd\tx0, x0, _g@PAGEOFF\n"
                                      "\tret\n"
                                      "\t.section\t__DATA,__mydata\n"
                                      "_g:\n"
                                      "\t.quad\t0\n");
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Before = (*Reader)->read(Scope.task(), *Input, "before.o", *Target);
  ASSERT_TRUE(static_cast<bool>(Before)) << errorText(Before.takeError());

  auto findCode = [](PluginObjectGraph &Graph) {
    return llvm::find_if(Graph.sections(),
                         [](const PluginObjectSection &Value) {
                           return Value.Name == "__mycode";
                         });
  };
  const auto CodeBefore = findCode(**Before);
  ASSERT_NE(CodeBefore, (*Before)->sections().end());
  const std::vector<uint8_t> BeforeBytes(CodeBefore->Data.begin(),
                                         CodeBefore->Data.end());
  (*Before)->issueLayoutProof();

  auto Candidate = (*Writer)->write(
      Scope.task(), **Before,
      ObjectOutputDestination::memory("machocode.o", UINT64_C(1) << 20));
  if (!Candidate) {
    consumeError(Candidate.takeError());
    return;
  }
  ASSERT_FALSE(static_cast<bool>((*Candidate)->verify()));
  auto Committed = (*Candidate)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());
  auto Output = findPluginMemoryOutput(Scope.task(), "machocode.o");
  ASSERT_TRUE(Output.has_value());
  auto After =
      (*Reader)->read(Scope.task(), Output->Bytes, "machocode.o", ReadTarget);
  ASSERT_TRUE(static_cast<bool>(After)) << errorText(After.takeError());
  const auto CodeAfter = findCode(**After);
  ASSERT_NE(CodeAfter, (*After)->sections().end());
  const std::vector<uint8_t> AfterBytes(CodeAfter->Data.begin(),
                                        CodeAfter->Data.end());
  EXPECT_EQ(BeforeBytes, AfterBytes)
      << "rewrite replaced the instruction\nbefore = " << hexBytes(BeforeBytes)
      << "\nafter  = " << hexBytes(AfterBytes);
}

TEST(PluginBuiltinObjectFormatTest, WriterRefusesRepeatedCommonSymbolName) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer));
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::x86_64);
  ASSERT_NE(Route, nullptr);
  auto Target = makeBuiltinTargetKey(*Route);
  ASSERT_TRUE(static_cast<bool>(Target));

  // A symbol table holds one entry per name, so two common symbols called the
  // same thing is not a graph an object can be written from. The verifier lets
  // the pair through because it treats a common symbol as a tentative
  // definition rather than a strong one, and MC then answers the second
  // ".comm" -- with a size the first did not have -- by calling
  // report_fatal_error, taking the host process down instead of failing.
  PluginObjectGraph Graph(std::move(*Target));
  for (uint64_t Size : {UINT64_C(4), UINT64_C(16)}) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph.allocateEntityID();
    Symbol.Name = "shared_tentative";
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON;
    Symbol.Size = Size;
    Symbol.Alignment = 8;
    Graph.symbols().push_back(std::move(Symbol));
  }
  Graph.issueLayoutProof();

  auto Candidate = (*Writer)->write(
      Scope.task(), Graph,
      ObjectOutputDestination::memory("common.o", UINT64_C(1) << 20));
  EXPECT_FALSE(static_cast<bool>(Candidate))
      << "two common symbols of one name were written out";
  if (!Candidate)
    consumeError(Candidate.takeError());
}

TEST(PluginBuiltinObjectFormatTest, WriterRefusesTwoCOFFSectionsInOneComdat) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader));
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer));
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::COFF, Triple::x86_64);
  ASSERT_NE(Route, nullptr);
  auto Input = assembleSource(*Route, "\t.section\t.text$a,\"xr\",discard,a\n"
                                      "\t.globl\ta\n"
                                      "a:\n"
                                      "\tretq\n"
                                      "\t.section\t.text$b,\"xr\",discard,b\n"
                                      "\t.globl\tb\n"
                                      "b:\n"
                                      "\tretq\n");
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Target = makeBuiltinTargetKey(*Route);
  ASSERT_TRUE(static_cast<bool>(Target));
  auto Graph = (*Reader)->read(Scope.task(), *Input, "comdat.o", *Target);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

  // A COFF COMDAT belongs to exactly one section -- the symbol it is keyed on
  // names that section -- while an ELF group holds several, so the graph lets
  // any number of sections point at one COMDAT and only the COFF writer can
  // say no. Left to the assembler, MC answers a second section on the same
  // COMDAT by calling report_fatal_error, which takes the host process down
  // instead of failing the write.
  uint64_t FirstComdat = 0;
  unsigned Pointed = 0;
  for (PluginObjectSection &Section : (*Graph)->sections()) {
    if (Section.ComdatID == 0)
      continue;
    if (FirstComdat == 0)
      FirstComdat = Section.ComdatID;
    else
      Section.ComdatID = FirstComdat;
    ++Pointed;
  }
  ASSERT_GE(Pointed, 2u) << "expected two COMDAT sections";
  (*Graph)->issueLayoutProof();

  auto Candidate = (*Writer)->write(
      Scope.task(), **Graph,
      ObjectOutputDestination::memory("comdat.o", UINT64_C(1) << 20));
  EXPECT_FALSE(static_cast<bool>(Candidate))
      << "two sections sharing a COFF COMDAT were written out";
  if (!Candidate)
    consumeError(Candidate.takeError());
}

TEST(PluginBuiltinObjectFormatTest,
     RewriteDoesNotAddScratchLabelsToTheSymbolTable) {
  initializeBuiltinTargets();
  const std::array<std::pair<BuiltinObjectFormat, Triple::ArchType>, 3> Routes =
      {{{BuiltinObjectFormat::ELF, Triple::x86_64},
        {BuiltinObjectFormat::COFF, Triple::x86_64},
        {BuiltinObjectFormat::MachO, Triple::aarch64}}};
  for (const auto &[Format, Arch] : Routes) {
    BuiltinObjectTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    auto Snapshot = PluginTargetRegistry::freeze(
        ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
    ASSERT_TRUE(static_cast<bool>(Snapshot));
    auto Reader = ObjectReaderProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Reader));
    auto Writer = ObjectWriterProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Writer));
    const BuiltinTargetRoute *Route = routeFor(Format, Arch);
    ASSERT_NE(Route, nullptr);
    SCOPED_TRACE(Route->CanonicalName.str());
    auto Target = makeBuiltinTargetKey(*Route);
    ASSERT_TRUE(static_cast<bool>(Target));
    OwnedTargetKey ReadTarget = *Target;
    auto Input = assembleSource(*Route, "\t.data\n"
                                        "\t.globl\tkept\n"
                                        "kept:\n"
                                        "\t.quad\t0\n");
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

    auto Before = (*Reader)->read(Scope.task(), *Input, "before.o", *Target);
    ASSERT_TRUE(static_cast<bool>(Before)) << errorText(Before.takeError());
    (*Before)->issueLayoutProof();
    auto Candidate = (*Writer)->write(
        Scope.task(), **Before,
        ObjectOutputDestination::memory("scaffold.o", UINT64_C(1) << 20));
    ASSERT_TRUE(static_cast<bool>(Candidate))
        << errorText(Candidate.takeError());
    ASSERT_FALSE(static_cast<bool>((*Candidate)->verify()));
    auto Committed = (*Candidate)->commit();
    ASSERT_TRUE(static_cast<bool>(Committed))
        << errorText(Committed.takeError());
    auto Output = findPluginMemoryOutput(Scope.task(), "scaffold.o");
    ASSERT_TRUE(Output.has_value());

    // The per-section labels the writer emits are its own scaffolding, and
    // they stay out of the object only because each one is spelled with the
    // prefix its format reserves for assembler-invented labels -- ".L" on ELF
    // and COFF, an uppercase "L" on Mach-O, where a lowercase one would be
    // kept. That is a quiet dependency between the label spelling and the
    // assembler's naming rules, and nothing else would notice it breaking.
    //
    // Read from the bytes rather than through the reader: the reader drops
    // container-level symbols on its way in, so a leak would be invisible in
    // the graph while still sitting in the file every other tool sees.
    const StringRef Bytes(reinterpret_cast<const char *>(Output->Bytes.data()),
                          Output->Bytes.size());
    EXPECT_EQ(Bytes.find("neverc_section_"), StringRef::npos)
        << "a writer scratch label reached the object's symbol table";
  }
}

TEST(PluginBuiltinObjectFormatTest,
     WriterRefusesNamesReservedForScratchLabels) {
  initializeBuiltinTargets();
  struct Case {
    BuiltinObjectFormat Format;
    Triple::ArchType Arch;
    const char *SymbolName;
  };
  // Each format reserves a prefix for the labels an assembler invents for
  // itself, and classifies a name by that prefix before anything else -- a
  // local symbol wearing it is left out of the symbol table, and quoting does
  // not change the answer. Written out, the definition simply vanished.
  const std::array<Case, 3> Cases = {
      {{BuiltinObjectFormat::ELF, Triple::x86_64, ".Lreserved"},
       {BuiltinObjectFormat::COFF, Triple::x86_64, ".Lreserved"},
       {BuiltinObjectFormat::MachO, Triple::aarch64, "Lreserved"}}};
  for (const auto &[Format, Arch, SymbolName] : Cases) {
    BuiltinObjectTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    auto Snapshot = PluginTargetRegistry::freeze(
        ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
    ASSERT_TRUE(static_cast<bool>(Snapshot));
    auto Writer = ObjectWriterProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Writer));
    const BuiltinTargetRoute *Route = routeFor(Format, Arch);
    ASSERT_NE(Route, nullptr);
    SCOPED_TRACE(Route->CanonicalName.str());
    auto Target = makeBuiltinTargetKey(*Route);
    ASSERT_TRUE(static_cast<bool>(Target));

    PluginObjectGraph Graph(std::move(*Target));
    PluginObjectSection Section;
    Section.ID = Graph.allocateEntityID();
    Section.Name = Format == BuiltinObjectFormat::MachO ? "__data" : ".data";
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    Section.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
    Section.Alignment = 1;
    Section.Data = {UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0)};
    const uint64_t SectionID = Section.ID;
    Graph.sections().push_back(std::move(Section));
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph.allocateEntityID();
    Symbol.Name = SymbolName;
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = SectionID;
    Symbol.Size = 4;
    Symbol.Alignment = 1;
    Graph.symbols().push_back(std::move(Symbol));
    Graph.issueLayoutProof();

    auto Candidate = (*Writer)->write(
        Scope.task(), Graph,
        ObjectOutputDestination::memory("scratch.o", UINT64_C(1) << 20));
    EXPECT_FALSE(static_cast<bool>(Candidate))
        << "a symbol the assembler drops was written out as if it survived";
    if (!Candidate)
      consumeError(Candidate.takeError());
  }
}

TEST(PluginBuiltinObjectFormatTest,
     WriterRefusesSectionRelativeAddendItCannotSpell) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader));
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer));
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::COFF, Triple::x86_64);
  ASSERT_NE(Route, nullptr);
  auto Input = assembleSource(*Route, "\t.text\n"
                                      "\t.globl\tcaller\n"
                                      "caller:\n"
                                      "\tretq\n"
                                      "\t.data\n"
                                      "\t.secrel32\tcaller\n");
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Target = makeBuiltinTargetKey(*Route);
  ASSERT_TRUE(static_cast<bool>(Target));
  auto Graph = (*Reader)->read(Scope.task(), *Input, "secrel.o", *Target);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

  // ".secrel32" takes "sym+N" and nothing else: its parser looks for a '+' and
  // stops at anything else, and it rejects an offset that does not fit an
  // unsigned 32-bit field. An addend the directive cannot carry has to be
  // caught where the writer decides what it can spell, the way .secidx already
  // is, rather than reaching the assembler as a syntax error.
  bool Adjusted = false;
  for (PluginObjectRelocation &Relocation : (*Graph)->relocations())
    if (Relocation.Kind == NEVERC_OBJECT_RELOCATION_SECTION_RELATIVE &&
        Relocation.Width == 32) {
      Relocation.Addend = -4;
      Adjusted = true;
    }
  ASSERT_TRUE(Adjusted) << "expected a section-relative relocation";
  (*Graph)->issueLayoutProof();

  auto Candidate = (*Writer)->write(
      Scope.task(), **Graph,
      ObjectOutputDestination::memory("secrel.o", UINT64_C(1) << 20));
  EXPECT_FALSE(static_cast<bool>(Candidate))
      << "a negative section-relative addend was written out";
  if (!Candidate)
    consumeError(Candidate.takeError());
}

TEST(PluginBuiltinObjectFormatTest, WriterKeepsCommonSymbolBindingLocal) {
  initializeBuiltinTargets();
  const std::array<std::pair<BuiltinObjectFormat, Triple::ArchType>, 3> Routes =
      {{{BuiltinObjectFormat::ELF, Triple::x86_64},
        {BuiltinObjectFormat::COFF, Triple::x86_64},
        {BuiltinObjectFormat::MachO, Triple::aarch64}}};
  for (const auto &[Format, Arch] : Routes) {
    BuiltinObjectTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    auto Snapshot = PluginTargetRegistry::freeze(
        ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
    ASSERT_TRUE(static_cast<bool>(Snapshot));
    auto Reader = ObjectReaderProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Reader));
    auto Writer = ObjectWriterProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Writer));
    const BuiltinTargetRoute *Route = routeFor(Format, Arch);
    ASSERT_NE(Route, nullptr);
    SCOPED_TRACE(Route->CanonicalName.str());
    auto Target = makeBuiltinTargetKey(*Route);
    ASSERT_TRUE(static_cast<bool>(Target));
    OwnedTargetKey ReadTarget = *Target;

    // ".comm" always states a symbol the linker may match against one of the
    // same name elsewhere. A local tentative definition is a different thing
    // -- it belongs to this file alone -- and writing it as a plain ".comm"
    // hands it to every other translation unit.
    PluginObjectGraph Graph(std::move(*Target));
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph.allocateEntityID();
    Symbol.Name =
        Format == BuiltinObjectFormat::MachO ? "_file_private" : "file_private";
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON;
    Symbol.Size = 8;
    Symbol.Alignment = 8;
    const std::string Name = Symbol.Name;
    Graph.symbols().push_back(std::move(Symbol));
    Graph.issueLayoutProof();

    auto Candidate = (*Writer)->write(
        Scope.task(), Graph,
        ObjectOutputDestination::memory("localcommon.o", UINT64_C(1) << 20));
    if (!Candidate) {
      // Refusing is a sound answer; silently publishing the symbol is not.
      consumeError(Candidate.takeError());
      continue;
    }
    ASSERT_FALSE(static_cast<bool>((*Candidate)->verify()));
    auto Committed = (*Candidate)->commit();
    ASSERT_TRUE(static_cast<bool>(Committed))
        << errorText(Committed.takeError());
    auto Output = findPluginMemoryOutput(Scope.task(), "localcommon.o");
    ASSERT_TRUE(Output.has_value());
    auto After = (*Reader)->read(Scope.task(), Output->Bytes, "localcommon.o",
                                 ReadTarget);
    ASSERT_TRUE(static_cast<bool>(After)) << errorText(After.takeError());
    const auto Found = llvm::find_if(
        (*After)->symbols(),
        [&](const PluginObjectSymbol &Value) { return Value.Name == Name; });
    ASSERT_NE(Found, (*After)->symbols().end())
        << "the symbol did not survive the write";
    EXPECT_EQ(Found->Binding, NEVERC_OBJECT_SYMBOL_BINDING_LOCAL)
        << "a file-local tentative definition was published to the linker";
  }
}

TEST(PluginBuiltinObjectFormatTest,
     ReaderRefusesAnObjectForAnotherArchitecture) {
  initializeBuiltinTargets();

  // A relocation type number means nothing on its own: 4 is AMD64's REL32 and
  // ARM64's PAGEBASE_REL21, 2 is x86's BRANCH and ARM64's BRANCH26. What tells
  // them apart is the architecture, and the graph states that in its TargetKey
  // -- which the caller supplies rather than the object. So a reader that
  // checks only the format lets an object of one architecture be read as a
  // graph claiming the other, and every consumer that reads a native type back
  // through the TargetKey then reads it out of the wrong table.
  const std::array<std::pair<BuiltinObjectFormat, const char *>, 2> Formats = {
      {{BuiltinObjectFormat::ELF, "ELF"}, {BuiltinObjectFormat::COFF, "COFF"}}};
  for (const auto &[Format, Label] : Formats) {
    for (const auto &[Built, Claimed] :
         std::array<std::pair<Triple::ArchType, Triple::ArchType>, 2>{
             {{Triple::x86_64, Triple::aarch64},
              {Triple::aarch64, Triple::x86_64}}}) {
      const BuiltinTargetRoute *BuiltRoute = routeFor(Format, Built);
      const BuiltinTargetRoute *ClaimedRoute = routeFor(Format, Claimed);
      ASSERT_NE(BuiltRoute, nullptr);
      ASSERT_NE(ClaimedRoute, nullptr);
      SCOPED_TRACE(std::string(Label) + ": object built for " +
                   BuiltRoute->CanonicalName.str() + ", read as " +
                   ClaimedRoute->CanonicalName.str());

      BuiltinObjectTaskScope Scope;
      ASSERT_TRUE(Scope.initialize());
      auto Snapshot = PluginTargetRegistry::freeze(
          ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
      ASSERT_TRUE(static_cast<bool>(Snapshot));
      auto Reader = ObjectReaderProvider::create(*Snapshot);
      ASSERT_TRUE(static_cast<bool>(Reader));

      auto Input = assembleRelocatable(*BuiltRoute);
      ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
      auto ClaimedTarget = makeBuiltinTargetKey(*ClaimedRoute);
      ASSERT_TRUE(static_cast<bool>(ClaimedTarget));

      auto Graph = (*Reader)->read(Scope.task(), *Input, "foreign-arch.o",
                                   *ClaimedTarget);
      EXPECT_FALSE(static_cast<bool>(Graph))
          << "an object for one architecture was read into a graph that says "
             "it is the other";
      if (!Graph)
        consumeError(Graph.takeError());
    }
  }
}

TEST(PluginBuiltinObjectFormatTest,
     WriterDoesNotLetTheNameRewriteSectionFlags) {
  initializeBuiltinTargets();

  // The ELF assembler reads a meaning out of a section's name before it looks
  // at the flags beside it, and it adds what the name implies to what it was
  // told rather than letting the flags stand on their own: anything called
  // ".text" or ".text.<x>" comes out executable, ".data"/".bss" writable,
  // ".rodata" allocated, and ".tdata"/".tbss" thread-local. A section whose
  // flags say otherwise is written out as a different section, and it
  // assembles and reads back cleanly, so nothing says the flags changed.
  //
  // Either outcome is acceptable -- write it with the flags it was given, or
  // refuse it -- so long as it does not come back as something else.
  struct Case {
    const char *Name;
    NevercObjectSectionFlags Flags;
  };
  const std::array<Case, 3> Cases = {
      {{".text.cold", NEVERC_OBJECT_SECTION_ALLOCATED},
       {".rodata.str", 0},
       {".data.rel", NEVERC_OBJECT_SECTION_ALLOCATED}}};

  for (const Case &Value : Cases) {
    SCOPED_TRACE(Value.Name);
    BuiltinObjectTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    auto Snapshot = PluginTargetRegistry::freeze(
        ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
    ASSERT_TRUE(static_cast<bool>(Snapshot));
    auto Reader = ObjectReaderProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Reader));
    auto Writer = ObjectWriterProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Writer));
    const BuiltinTargetRoute *Route =
        routeFor(BuiltinObjectFormat::ELF, Triple::x86_64);
    ASSERT_NE(Route, nullptr);
    auto Target = makeBuiltinTargetKey(*Route);
    ASSERT_TRUE(static_cast<bool>(Target));
    OwnedTargetKey ReadTarget = *Target;

    PluginObjectGraph Graph(std::move(*Target));
    PluginObjectSection Section;
    Section.ID = Graph.allocateEntityID();
    Section.Name = Value.Name;
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    Section.Flags = Value.Flags;
    Section.Alignment = 1;
    Section.Data = {1, 2, 3, 4};
    Graph.sections().push_back(std::move(Section));
    ASSERT_FALSE(verifyPluginObjectGraph(Graph));
    Graph.issueLayoutProof();

    auto Candidate = (*Writer)->write(
        Scope.task(), Graph,
        ObjectOutputDestination::memory("named.o", UINT64_C(1) << 20));
    if (!Candidate) {
      consumeError(Candidate.takeError());
      continue;
    }
    ASSERT_FALSE(static_cast<bool>((*Candidate)->verify()));
    auto Committed = (*Candidate)->commit();
    ASSERT_TRUE(static_cast<bool>(Committed))
        << errorText(Committed.takeError());
    auto Output = findPluginMemoryOutput(Scope.task(), "named.o");
    ASSERT_TRUE(Output.has_value());
    auto After =
        (*Reader)->read(Scope.task(), Output->Bytes, "named.o", ReadTarget);
    ASSERT_TRUE(static_cast<bool>(After)) << errorText(After.takeError());
    const auto Found = llvm::find_if((*After)->sections(),
                                     [&](const PluginObjectSection &Candidate) {
                                       return Candidate.Name == Value.Name;
                                     });
    ASSERT_NE(Found, (*After)->sections().end());
    const NevercObjectSectionFlags Interesting =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE |
        NEVERC_OBJECT_SECTION_EXECUTABLE | NEVERC_OBJECT_SECTION_TLS;
    EXPECT_EQ(Found->Flags & Interesting, Value.Flags & Interesting)
        << "the assembler read the name and overrode the flags";
  }
}

TEST(PluginBuiltinObjectFormatTest, WriterRefusesWhatItCannotSpellItself) {
  initializeBuiltinTargets();

  // Each of these is a graph the verifier accepts and the assembly for it is
  // one the assembler does not: an alignment above 2^31 has no ".p2align"
  // exponent, a Mach-O section name has sixteen bytes of room, and ".comm"
  // reads its size as signed. Reaching the assembler turns all three into a
  // syntax error about a line the caller never wrote, so the writer has to
  // recognise its own limits first.
  struct Case {
    const char *Label;
    BuiltinObjectFormat Format;
    Triple::ArchType Arch;
    uint64_t Alignment;
    const char *SectionName;
    uint64_t CommonSize;
    std::string ExpectedDiagnostic;
  };
  const std::array<Case, 3> Cases = {
      {{"alignment above 2^31", BuiltinObjectFormat::ELF, Triple::x86_64,
        UINT64_C(1) << 32, ".data", 0,
        "alignment cannot be represented by the built-in assembly writer"},
       {"Mach-O section name of seventeen bytes", BuiltinObjectFormat::MachO,
        Triple::aarch64, 1, "__aaaaaaaaaaaaaaa", 0,
        "status " + std::to_string(NEVERC_STATUS_CAPABILITY_UNAVAILABLE)},
       {"common symbol larger than INT64_MAX", BuiltinObjectFormat::ELF,
        Triple::x86_64, 1, ".data",
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1,
        "status " + std::to_string(NEVERC_STATUS_CAPABILITY_UNAVAILABLE)}}};

  for (const Case &Value : Cases) {
    SCOPED_TRACE(Value.Label);
    BuiltinObjectTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    auto Snapshot = PluginTargetRegistry::freeze(
        ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
    ASSERT_TRUE(static_cast<bool>(Snapshot));
    auto Writer = ObjectWriterProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(Writer));
    const BuiltinTargetRoute *Route = routeFor(Value.Format, Value.Arch);
    ASSERT_NE(Route, nullptr);
    auto Target = makeBuiltinTargetKey(*Route);
    ASSERT_TRUE(static_cast<bool>(Target));

    PluginObjectGraph Graph(std::move(*Target));
    PluginObjectSection Section;
    Section.ID = Graph.allocateEntityID();
    Section.Name = Value.SectionName;
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    Section.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
    Section.Alignment = Value.Alignment;
    Section.Data = {0, 0, 0, 0};
    Graph.sections().push_back(std::move(Section));
    if (Value.CommonSize != 0) {
      PluginObjectSymbol Symbol;
      Symbol.ID = Graph.allocateEntityID();
      Symbol.Name = "tentative";
      Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
      Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
      Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON;
      Symbol.Size = Value.CommonSize;
      Symbol.Alignment = 8;
      Graph.symbols().push_back(std::move(Symbol));
    }
    ASSERT_FALSE(verifyPluginObjectGraph(Graph))
        << "the graph under test has to be one the verifier accepts";
    Graph.issueLayoutProof();

    auto Candidate = (*Writer)->write(
        Scope.task(), Graph,
        ObjectOutputDestination::memory("limits.o", UINT64_C(1) << 20));
    ASSERT_FALSE(static_cast<bool>(Candidate))
        << "the writer produced assembly it cannot have meant";
    // Not merely that the write failed: letting the assembler reject the text
    // fails too, and reports a parse error rather than the one thing the
    // caller can act on -- that this graph asks for something the format has
    // no room for.
    EXPECT_NE(errorText(Candidate.takeError()).find(Value.ExpectedDiagnostic),
              std::string::npos)
        << "the limit was left for the assembler to discover";
    EXPECT_FALSE(findPluginMemoryOutput(Scope.task(), "limits.o").has_value())
        << "a rejected graph must not publish an output image";
  }
}

TEST(PluginBuiltinObjectFormatTest, WriterKeepsSameNamedSectionsApart) {
  initializeBuiltinTargets();
  BuiltinObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader));
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer));
  const BuiltinTargetRoute *Route =
      routeFor(BuiltinObjectFormat::ELF, Triple::aarch64);
  ASSERT_NE(Route, nullptr);
  auto Target = makeBuiltinTargetKey(*Route);
  ASSERT_TRUE(static_cast<bool>(Target));
  OwnedTargetKey ReadTarget = *Target;

  // An ELF object may hold several sections of one name -- that is what
  // -fno-unique-section-names produces for every function and every global.
  // Naming a section that already exists switches back to it instead of
  // starting a new one, so writing such a graph out concatenates the two and
  // the object comes back with one section holding both.
  auto Input =
      assembleSource(*Route, "\t.section\t.mydata,\"a\",@progbits,unique,1\n"
                             "\t.byte\t0x11\n"
                             "\t.section\t.mydata,\"a\",@progbits,unique,2\n"
                             "\t.byte\t0x22\n");
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Before = (*Reader)->read(Scope.task(), *Input, "before.o", *Target);
  ASSERT_TRUE(static_cast<bool>(Before)) << errorText(Before.takeError());

  auto countNamed = [](PluginObjectGraph &Graph) {
    return llvm::count_if(Graph.sections(),
                          [](const PluginObjectSection &Value) {
                            return Value.Name == ".mydata";
                          });
  };
  ASSERT_EQ(countNamed(**Before), 2)
      << "the reader did not see two sections of one name";
  (*Before)->issueLayoutProof();

  auto Candidate = (*Writer)->write(
      Scope.task(), **Before,
      ObjectOutputDestination::memory("same-name.o", UINT64_C(1) << 20));
  if (!Candidate) {
    consumeError(Candidate.takeError());
    return;
  }
  ASSERT_FALSE(static_cast<bool>((*Candidate)->verify()));
  auto Committed = (*Candidate)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());
  auto Output = findPluginMemoryOutput(Scope.task(), "same-name.o");
  ASSERT_TRUE(Output.has_value());
  auto After =
      (*Reader)->read(Scope.task(), Output->Bytes, "same-name.o", ReadTarget);
  ASSERT_TRUE(static_cast<bool>(After)) << errorText(After.takeError());
  EXPECT_EQ(countNamed(**After), 2)
      << "two sections of one name were written out as a single section";
}

} // namespace
