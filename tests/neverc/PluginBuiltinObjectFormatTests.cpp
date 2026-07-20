#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/ObjectWriterProvider.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"
#include <memory>
#include <mutex>
#include <optional>
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

Expected<OwnedTargetKey>
makeBuiltinTargetKey(const BuiltinTargetRoute &Route) {
  Triple Parsed(Triple::normalize(Route.CanonicalTriple));
  TargetKeyBuilder Builder;
  Builder.setTargetID(Route.TargetID)
      .setTriple(Route.CanonicalTriple.str(), Parsed.getArchName().str(),
                 Parsed.getVendorName().str(), Parsed.getOSName().str(),
                 Parsed.getEnvironmentName().str())
      .setCPU(Route.DefaultCPU.str(), Route.DefaultCPU.str())
      .setFeatures({})
      .setABI(Route.ABIID)
      .setCallingConvention(
          {UINT64_C(0x4e43424f46434349), Route.TargetID.Low})
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
         << "\t.globl\t"
         << (MachO ? "_roundtrip_entry" : "roundtrip_entry") << '\n'
         << (MachO ? "_roundtrip_entry" : "roundtrip_entry") << ":\n"
         << "\t.byte\t0xc3\n"
         << "\t.data\n"
         << "\t.globl\t"
         << (MachO ? "_roundtrip_pointer" : "roundtrip_pointer") << '\n'
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
  Request.Input =
      MemoryBufferRef(Assembly, "<builtin-object-roundtrip>");
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
    if (Relocation.TargetKind ==
        NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL) {
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

std::vector<SectionSemantics>
sectionSemantics(const PluginObjectGraph &Graph) {
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
         static_cast<uint64_t>(Section.Data.size()) +
             Section.ZeroFillSize});
  }
  llvm::sort(Result,
             [](const SectionSemantics &Left,
                const SectionSemantics &Right) {
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
    return std::tie(Name, Binding, Visibility, Type, Definition,
                    SectionName);
  }

  friend bool operator==(const SymbolSemantics &Left,
                         const SymbolSemantics &Right) {
    return Left.asTuple() == Right.asTuple();
  }
};

std::vector<SymbolSemantics>
symbolSemantics(const PluginObjectGraph &Graph) {
  std::vector<SymbolSemantics> Result;
  for (const PluginObjectSymbol &Symbol : Graph.symbols()) {
    if (StringRef(Symbol.Name).find("roundtrip_") == StringRef::npos)
      continue;
    SymbolSemantics Semantics{Symbol.Name, Symbol.Binding,
                              Symbol.Visibility, Symbol.Type,
                              Symbol.Definition, {}};
    if (const PluginObjectSection *Section =
            Graph.findSection(Symbol.SectionID))
      Semantics.SectionName = Section->Name;
    Result.push_back(std::move(Semantics));
  }
  llvm::sort(Result, [](const SymbolSemantics &Left,
                        const SymbolSemantics &Right) {
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
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader))
      << errorText(Reader.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer))
      << errorText(Writer.takeError());

  std::set<std::pair<Triple::ArchType, BuiltinObjectFormat>> Tested;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    const auto RouteKey =
        std::make_pair(Parsed.getArch(), Route.ObjectFormat);
    if (!Route.SupportsObject || !Tested.insert(RouteKey).second)
      continue;
    SCOPED_TRACE(Route.CanonicalName.str());

    auto Target = makeBuiltinTargetKey(Route);
    ASSERT_TRUE(static_cast<bool>(Target))
        << errorText(Target.takeError());
    auto Input = assembleRelocatable(Route);
    ASSERT_TRUE(static_cast<bool>(Input))
        << errorText(Input.takeError());
    auto Before = (*Reader)->read(Scope.task(), *Input, "before.o",
                                  *Target);
    ASSERT_TRUE(static_cast<bool>(Before))
        << errorText(Before.takeError());
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

    auto After = (*Reader)->read(Scope.task(), Output->Bytes, OutputName,
                                 *Target);
    ASSERT_TRUE(static_cast<bool>(After))
        << errorText(After.takeError());
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
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader))
      << errorText(Reader.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer))
      << errorText(Writer.takeError());

  std::set<std::pair<Triple::ArchType, BuiltinObjectFormat>> Tested;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    const auto RouteKey =
        std::make_pair(Parsed.getArch(), Route.ObjectFormat);
    if (!Route.SupportsObject || !Tested.insert(RouteKey).second)
      continue;
    SCOPED_TRACE(Route.CanonicalName.str());

    auto Target = makeBuiltinTargetKey(Route);
    ASSERT_TRUE(static_cast<bool>(Target))
        << errorText(Target.takeError());
    OwnedTargetKey ReadTarget = *Target;
    PluginObjectGraph Graph(std::move(*Target));
    PluginObjectSection Section;
    Section.ID = Graph.allocateEntityID();
    Section.Name = Route.ObjectFormat == BuiltinObjectFormat::MachO
                       ? "__thread_data"
                       : ".tdata";
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
    Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED |
                    NEVERC_OBJECT_SECTION_WRITABLE |
                    NEVERC_OBJECT_SECTION_TLS;
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
    auto Restored = (*Reader)->read(Scope.task(), Output->Bytes, OutputName,
                                    ReadTarget);
    ASSERT_TRUE(static_cast<bool>(Restored))
        << errorText(Restored.takeError());
    const auto TLSSection = llvm::find_if(
        (*Restored)->sections(), [](const PluginObjectSection &Value) {
          return Value.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
        });
    ASSERT_NE(TLSSection, (*Restored)->sections().end());
    EXPECT_EQ(TLSSection->Flags & NEVERC_OBJECT_SECTION_TLS,
              NEVERC_OBJECT_SECTION_TLS);
    const auto TLSSymbol = llvm::find_if(
        (*Restored)->symbols(), [](const PluginObjectSymbol &Value) {
          return StringRef(Value.Name).find("roundtrip_tls") !=
                 StringRef::npos;
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
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader))
      << errorText(Reader.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer))
      << errorText(Writer.takeError());

  std::set<std::pair<Triple::ArchType, BuiltinObjectFormat>> Tested;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    if (Route.ObjectFormat == BuiltinObjectFormat::MachO)
      continue;
    Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    const auto RouteKey =
        std::make_pair(Parsed.getArch(), Route.ObjectFormat);
    if (!Route.SupportsObject || !Tested.insert(RouteKey).second)
      continue;
    SCOPED_TRACE(Route.CanonicalName.str());

    auto Target = makeBuiltinTargetKey(Route);
    ASSERT_TRUE(static_cast<bool>(Target))
        << errorText(Target.takeError());
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
    auto Restored = (*Reader)->read(Scope.task(), Output->Bytes, OutputName,
                                    ReadTarget);
    ASSERT_TRUE(static_cast<bool>(Restored))
        << errorText(Restored.takeError());
    ASSERT_EQ((*Restored)->comdatCount(), 1U);
    const PluginObjectComdat &RestoredComdat =
        (*Restored)->comdats().front();
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

} // namespace
