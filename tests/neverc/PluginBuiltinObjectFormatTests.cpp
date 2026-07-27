#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/ObjectSectionRole.h"
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

Expected<std::vector<uint8_t>>
assembleSource(const BuiltinTargetRoute &Route, StringRef Assembly) {
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
  auto After = (*Reader)->read(Task, Output->Bytes, OutputName.str(),
                               ReadTarget);
  if (!After)
    return After.takeError();
  const auto TextAfter = llvm::find_if(
      (*After)->sections(), [](const PluginObjectSection &Value) {
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

TEST(PluginBuiltinObjectFormatTest,
     COFFRefusesRewriteItCannotSpellFaithfully) {
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
    const BuiltinTargetRoute *Route =
        routeFor(BuiltinObjectFormat::COFF, Arch);
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

TEST(PluginBuiltinObjectFormatTest, ReaderClassifiesSectionsByRoleNotSubstring) {
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
    auto Graph =
        (*Reader)->read(Scope.task(), *Input, "roles.o", *Target);
    ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
    const auto It = llvm::find_if(
        (*Graph)->sections(), [&](const PluginObjectSection &Section) {
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
  ASSERT_TRUE(static_cast<bool>(Candidate))
      << errorText(Candidate.takeError());
  ASSERT_FALSE((*Candidate)->verify());
  auto Committed = (*Candidate)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed))
      << errorText(Committed.takeError());
  auto Output = findPluginMemoryOutput(Scope.task(), "bss.o");
  ASSERT_TRUE(Output.has_value());
  auto Restored =
      (*Reader)->read(Scope.task(), Output->Bytes, "bss.o", ReadTarget);
  ASSERT_TRUE(static_cast<bool>(Restored)) << errorText(Restored.takeError());
  for (unsigned I = 0; I != 3; ++I) {
    const std::string Name = "bss_value_" + std::to_string(I);
    const auto It = llvm::find_if(
        (*Restored)->symbols(), [&](const PluginObjectSymbol &Symbol) {
          return Symbol.Name == Name;
        });
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
    auto Graph =
        (*Reader)->read(Scope.task(), *Input, "coverage.o", *Target);
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
      {{BuiltinObjectFormat::COFF, Triple::x86_64, ".text",
        "?value@@YAXH@Z"},
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
    Section.Data = {UINT8_C(0xc0), UINT8_C(0x03), UINT8_C(0x5f),
                    UINT8_C(0xd6)};
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
    auto Restored = (*Reader)->read(Scope.task(), Output->Bytes, "mangled.o",
                                    ReadTarget);
    ASSERT_TRUE(static_cast<bool>(Restored))
        << errorText(Restored.takeError());
    const auto It = llvm::find_if(
        (*Restored)->symbols(), [&](const PluginObjectSymbol &S) {
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
  const std::array<std::pair<BuiltinObjectFormat, Triple::ArchType>, 4>
      Routes = {{{BuiltinObjectFormat::ELF, Triple::x86_64},
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
      auto Graph =
          (*Reader)->read(Scope.task(), *Input, "altered.o", *Target);
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
          Relocation.Kind =
              static_cast<NevercObjectRelocationKind>(pick(8));
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
              "altered" + std::to_string(Iteration) + ".o",
              UINT64_C(1) << 20));
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

TEST(PluginBuiltinObjectFormatTest, WriterRefusesKindThatContradictsNativeType) {
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

TEST(PluginBuiltinObjectFormatTest, CommonSymbolKeepsItsAlignmentAcrossFormats) {
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
      return llvm::find_if(Graph.symbols(),
                           [](const PluginObjectSymbol &Value) {
                             return Value.Definition ==
                                    NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON;
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
    ASSERT_TRUE(static_cast<bool>(Candidate)) << errorText(Candidate.takeError());
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
  auto After = (*Reader)->read(Scope.task(), Output->Bytes, "machocode.o",
                               ReadTarget);
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
  auto Input = assembleSource(*Route,
                              "\t.section\t.text$a,\"xr\",discard,a\n"
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

TEST(PluginBuiltinObjectFormatTest, RewriteDoesNotAddScratchLabelsToTheSymbolTable) {
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

TEST(PluginBuiltinObjectFormatTest, WriterRefusesNamesReservedForScratchLabels) {
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

TEST(PluginBuiltinObjectFormatTest, WriterRefusesSectionRelativeAddendItCannotSpell) {
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
    Symbol.Name = Format == BuiltinObjectFormat::MachO ? "_file_private"
                                                       : "file_private";
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
    const auto Found =
        llvm::find_if((*After)->symbols(),
                      [&](const PluginObjectSymbol &Value) {
                        return Value.Name == Name;
                      });
    ASSERT_NE(Found, (*After)->symbols().end())
        << "the symbol did not survive the write";
    EXPECT_EQ(Found->Binding, NEVERC_OBJECT_SYMBOL_BINDING_LOCAL)
        << "a file-local tentative definition was published to the linker";
  }
}

TEST(PluginBuiltinObjectFormatTest, ReaderRefusesAnObjectForAnotherArchitecture) {
  initializeBuiltinTargets();

  // A relocation type number means nothing on its own: 4 is AMD64's REL32 and
  // ARM64's PAGEBASE_REL21, 2 is x86's BRANCH and ARM64's BRANCH26. What tells
  // them apart is the architecture, and the graph states that in its TargetKey
  // -- which the caller supplies rather than the object. So a reader that
  // checks only the format lets an object of one architecture be read as a
  // graph claiming the other, and every consumer that reads a native type back
  // through the TargetKey then reads it out of the wrong table.
  const std::array<std::pair<BuiltinObjectFormat, const char *>, 2> Formats = {
      {{BuiltinObjectFormat::ELF, "ELF"},
       {BuiltinObjectFormat::COFF, "COFF"}}};
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

TEST(PluginBuiltinObjectFormatTest, WriterDoesNotLetTheNameRewriteSectionFlags) {
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
    auto After = (*Reader)->read(Scope.task(), Output->Bytes, "named.o",
                                 ReadTarget);
    ASSERT_TRUE(static_cast<bool>(After)) << errorText(After.takeError());
    const auto Found = llvm::find_if(
        (*After)->sections(), [&](const PluginObjectSection &Candidate) {
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
  };
  const std::array<Case, 3> Cases = {
      {{"alignment above 2^31", BuiltinObjectFormat::ELF, Triple::x86_64,
        UINT64_C(1) << 32, ".data", 0},
       {"Mach-O section name of seventeen bytes", BuiltinObjectFormat::MachO,
        Triple::aarch64, 1, "__aaaaaaaaaaaaaaa", 0},
       {"common symbol larger than INT64_MAX", BuiltinObjectFormat::ELF,
        Triple::x86_64, 1, ".data",
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1}}};

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
    Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED |
                    NEVERC_OBJECT_SECTION_WRITABLE;
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
    EXPECT_NE(errorText(Candidate.takeError())
                  .find("status " +
                        std::to_string(NEVERC_STATUS_CAPABILITY_UNAVAILABLE)),
              std::string::npos)
        << "the limit was left for the assembler to discover";
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
  auto Input = assembleSource(*Route,
                              "\t.section\t.mydata,\"a\",@progbits,unique,1\n"
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
  auto After = (*Reader)->read(Scope.task(), Output->Bytes, "same-name.o",
                               ReadTarget);
  ASSERT_TRUE(static_cast<bool>(After)) << errorText(After.takeError());
  EXPECT_EQ(countNamed(**After), 2)
      << "two sections of one name were written out as a single section";
}

} // namespace
