#include "Link/AndroidKernelProfileContractVerifier.h"
#include "Link/AndroidKernelModuleFinalizer.h"
#include "Link/BuiltinObjectMergeAdapter.h"
#include "Link/LinkGraph.h"
#include "Link/ObjectGraphImporter.h"
#include "Link/ObjectMergeProvider.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TestTargetID{UINT64_C(0x4e43504d45524745),
                                      UINT64_C(1)};
constexpr NevercObjectFormatID TestFormatID{
    UINT64_C(0x4e43504d45524746), UINT64_C(1)};
constexpr NevercInterfaceID TestProductID{
    UINT64_C(0x4e43504d45524750), UINT64_C(1)};

std::string errorText(Error Value) {
  return toString(std::move(Value)).str().str();
}

Expected<OwnedTargetKey> makeTargetKey(
    NevercTargetID TargetID = TestTargetID,
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
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef"
          "0123456789abcdef0123456789abcdef")
      .build();
}

std::unique_ptr<PluginObjectGraph> makeObject(unsigned SectionCount) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return nullptr;
  }
  auto Graph =
      std::make_unique<PluginObjectGraph>(std::move(*Target));
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

Expected<OwnedTargetKey>
makeBuiltinTargetKey(const BuiltinTargetRoute &Route) {
  Triple Parsed(Triple::normalize(Route.CanonicalTriple));
  return TargetKeyBuilder()
      .setTargetID(Route.TargetID)
      .setTriple(Route.CanonicalTriple.str(), Parsed.getArchName().str(),
                 Parsed.getVendorName().str(), Parsed.getOSName().str(),
                 Parsed.getEnvironmentName().str())
      .setCPU(Route.DefaultCPU.str(), Route.DefaultCPU.str())
      .setFeatures({})
      .setABI(Route.ABIID)
      .setCallingConvention(
          {UINT64_C(0x4e434f424a4d4343), Route.TargetID.Low})
      .setObjectFormat(Route.ObjectFormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef"
          "0123456789abcdef0123456789abcdef")
      .build();
}

std::unique_ptr<PluginObjectGraph>
makeBuiltinObject(const BuiltinTargetRoute &Route, StringRef SymbolName) {
  auto Target = makeBuiltinTargetKey(Route);
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return nullptr;
  }
  auto Graph =
      std::make_unique<PluginObjectGraph>(std::move(*Target));
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

class LinkTaskScope {
public:
  LinkTaskScope()
      : Services("neverc-plugin-object-merge-tests",
                 LLVM_VERSION_MAJOR) {}

  bool initialize(StringRef PluginPath = {}) {
    if (Error E = registerPluginIOInterface(Services)) {
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
    if (!PluginPath.empty()) {
      auto Loaded = Services.registry().load(PluginPath);
      if (!Loaded) {
        ADD_FAILURE() << errorText(Loaded.takeError());
        return false;
      }
      Selected.push_back((*Loaded)->descriptor().PluginID);
    }
    auto CreatedPlan =
        makePluginActivationPlan(Services.registry(), Selected);
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

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
};

struct MergeCallbackState {
  unsigned Calls = 0;
  bool ReturnForeignObject = false;
};

NevercStatus NEVERC_CALL mergeObjects(
    void *UserData, NevercTaskHandle Task,
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
      Request->OutputObject->Context, Task, Request->OutputMutation,
      &Section, &Created);
  if (!neverc_status_is_ok(Status))
    return Status;

  Candidate->Object = State->ReturnForeignObject
                          ? NevercObjectGraphHandle{UINT64_C(99),
                                                   UINT64_C(101)}
                          : Request->OutputGraph;
  Candidate->ProductID = TestProductID;
  Candidate->ProducerRouteDigest[0] = 0x42;
  return neverc_status_ok();
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
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
    Symbol.Definition = Definition;
    Symbol.SectionID = SectionID;
    Symbol.Value = Value;
    Symbol.Size = Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED ? 1 : 0;
    const uint64_t ID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));
    return ID;
  };

  const uint64_t NeededLocal = AddSymbol(
      "release_needed_local", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
      NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID, 0);
  AddSymbol("release_unneeded_local", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID, 1);
  const uint64_t NeededImport = AddSymbol(
      "release_needed_import", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
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
    return std::any_of(Graph->sections().begin(), Graph->sections().end(),
                       [&](const PluginObjectSection &S) {
                         return S.Name == Name;
                       });
  };
  const auto HasSymbol = [&](StringRef Name) {
    return std::any_of(Graph->symbols().begin(), Graph->symbols().end(),
                       [&](const PluginObjectSymbol &S) {
                         return S.Name == Name;
                       });
  };
  EXPECT_FALSE(HasSection(".neverc.android.kernel.profile"));
  EXPECT_FALSE(HasSection(".debug_info"));
  EXPECT_FALSE(HasSection(".comment"));
  EXPECT_TRUE(HasSymbol("release_needed_local"));
  EXPECT_FALSE(HasSymbol("release_unneeded_local"));
  EXPECT_TRUE(HasSymbol("release_needed_import"));
  EXPECT_FALSE(HasSymbol("release_unneeded_import"));
  EXPECT_TRUE(HasSymbol("release_public_definition"));
  EXPECT_FALSE(HasSymbol("release_debug_only"));
  EXPECT_EQ(Graph->relocationCount(), 2u);
  EXPECT_FALSE(verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module"));
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
  ASSERT_TRUE(static_cast<bool>(Target))
      << errorText(Target.takeError());
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
             NEVERC_OBJECT_SECTION_ALLOCATED |
                 NEVERC_OBJECT_SECTION_WRITABLE,
             8, 0);
  AddSection(".gnu.linkonce.this_module",
             NEVERC_OBJECT_SECTION_ALLOCATED |
                 NEVERC_OBJECT_SECTION_WRITABLE,
             64, 1024);
  Input->advanceGeneration();
  Input->issueLayoutProof();
  ASSERT_FALSE(verifyPluginObjectGraph(*Input));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  std::array<PluginObjectGraph *, 1> Inputs{Input.get()};
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Inputs,
      ArrayRef<ArrayRef<uint8_t>>{}, NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged))
      << errorText(Merged.takeError());

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  ArrayRef<uint8_t> ValidImage(
      reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
      Merged->MergedImage.size());
  EXPECT_FALSE(verifyFinalAndroidKernelModuleImage(
      ValidImage, Policy, "valid final Android module"));

  std::vector<uint8_t> Corrupted(ValidImage.begin(), ValidImage.end());
  StringRef CorruptedBytes(reinterpret_cast<const char *>(Corrupted.data()),
                           Corrupted.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(CorruptedBytes);
  ASSERT_TRUE(static_cast<bool>(Parsed))
      << errorText(Parsed.takeError());
  auto Sections = Parsed->sections();
  ASSERT_TRUE(static_cast<bool>(Sections))
      << errorText(Sections.takeError());
  std::optional<unsigned> SymtabIndex;
  std::optional<unsigned> ShstrtabIndex;
  for (unsigned I = 0; I < Sections->size(); ++I) {
    auto Name = Parsed->getSectionName((*Sections)[I]);
    ASSERT_TRUE(static_cast<bool>(Name))
        << errorText(Name.takeError());
    if (*Name == ".symtab")
      SymtabIndex = I;
    else if (*Name == ".shstrtab")
      ShstrtabIndex = I;
  }
  ASSERT_TRUE(SymtabIndex.has_value());
  ASSERT_TRUE(ShstrtabIndex.has_value());

  object::ELF64LE::Shdr CorruptedSymtab = (*Sections)[*SymtabIndex];
  CorruptedSymtab.sh_link = *ShstrtabIndex;
  const auto *OriginalSymtabBytes = reinterpret_cast<const uint8_t *>(
      &(*Sections)[*SymtabIndex]);
  ASSERT_GE(OriginalSymtabBytes, Corrupted.data());
  const size_t SymtabOffset = OriginalSymtabBytes - Corrupted.data();
  ASSERT_LE(SymtabOffset + sizeof(CorruptedSymtab), Corrupted.size());
  std::memcpy(Corrupted.data() + SymtabOffset, &CorruptedSymtab,
              sizeof(CorruptedSymtab));

  Error Verify = verifyFinalAndroidKernelModuleImage(
      Corrupted, Policy, "corrupted final Android module");
  ASSERT_TRUE(static_cast<bool>(Verify));
  EXPECT_NE(errorText(std::move(Verify))
                .find("symbol table must link to .strtab"),
            std::string::npos);
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
               NEVERC_OBJECT_SECTION_EXECUTABLE |
               NEVERC_OBJECT_SECTION_RETAIN;
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
  ASSERT_TRUE(static_cast<bool>(Imported))
      << errorText(Imported.takeError());
  ASSERT_FALSE(verifyPluginLinkGraph(Link));

  const PluginLinkSection *Section =
      Link.findSection(Imported->Sections.at(ObjectSectionID));
  ASSERT_NE(Section, nullptr);
  EXPECT_EQ(Section->Kind, NEVERC_OBJECT_SECTION_KIND_TEXT);
  EXPECT_EQ(Section->Flags, NEVERC_OBJECT_SECTION_ALLOCATED |
                                NEVERC_OBJECT_SECTION_EXECUTABLE |
                                NEVERC_OBJECT_SECTION_RETAIN);
  EXPECT_EQ(Section->ComdatID,
            Imported->Comdats.at(ObjectComdatID));
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
  EXPECT_EQ(DefinedLink->Type,
            NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION);
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
  EXPECT_EQ(Edge->RelocationKind,
            NEVERC_OBJECT_RELOCATION_PC_RELATIVE);
  EXPECT_TRUE(Edge->IsPCRelative);
  EXPECT_TRUE(Edge->IsSigned);
  EXPECT_EQ(Edge->Addend, -4);
}

TEST(PluginObjectGraphImportTest, RejectsForeignTargetWithoutMutation) {
  auto SourceTarget = makeTargetKey();
  auto OtherTarget =
      makeTargetKey({UINT64_C(0xdead), UINT64_C(0xbeef)});
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
  ASSERT_TRUE(static_cast<bool>(Target))
      << errorText(Target.takeError());

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

  auto Merged = executeObjectMergeProvider(
      Scope.task(), Provider, std::move(*Target), Inputs);
  ASSERT_TRUE(static_cast<bool>(Merged))
      << errorText(Merged.takeError());
  ASSERT_NE(Merged->Object, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*Merged->Object));
  ASSERT_EQ(Merged->Object->sections().size(), 1u);
  EXPECT_EQ(Merged->Object->sections().front().Name, ".merged");
  EXPECT_EQ(Merged->Object->sections().front().Data,
            (std::vector<uint8_t>{3}));
  EXPECT_EQ(Merged->ProducerRouteDigest[0], 0x42);
  EXPECT_EQ(State.Calls, 1u);
}

TEST(PluginObjectMergeProviderTest, RejectsForeignOutputHandle) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Input = makeObject(1);
  auto Target = makeTargetKey();
  ASSERT_NE(Input, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target))
      << errorText(Target.takeError());

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
  ASSERT_TRUE(static_cast<bool>(Target))
      << errorText(Target.takeError());

  auto Snapshot = PluginLinkRegistry::freeze(Scope.session().plugins());
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  ASSERT_EQ((*Snapshot)->objectMergeProviders().size(), 1u);

  LinkRouteRequest Request;
  Request.TargetID = TestTargetID;
  Request.InputFormat = TestFormatID;
  Request.OutputFormat = TestFormatID;
  Request.OutputKind = NEVERC_LINK_OUTPUT_RELOCATABLE;
  auto Route = LinkRoutePlanner::plan(
      (*Snapshot)->linkerProviders(),
      (*Snapshot)->objectMergeProviders(), Request);
  ASSERT_TRUE(static_cast<bool>(Route))
      << errorText(Route.takeError());
  ASSERT_EQ(Route->kind(), PlannedLinkRoute::Kind::ObjectMerge);
  ASSERT_NE(Route->objectMergeProvider(), nullptr);

  std::array<PluginObjectGraph *, 2> Inputs{First.get(), Second.get()};
  auto Merged = executeObjectMergeProvider(
      Scope.task(), *Route->objectMergeProvider(), std::move(*Target),
      Inputs);
  ASSERT_TRUE(static_cast<bool>(Merged))
      << errorText(Merged.takeError());
  ASSERT_EQ(Merged->Object->sections().size(), 1u);
  EXPECT_EQ(Merged->Object->sections().front().Name,
            ".plugin-merged");
  EXPECT_EQ(Merged->Object->sections().front().Data,
            (std::vector<uint8_t>{5}));
  EXPECT_EQ(Merged->ProducerRouteDigest[0], 0x63);
  EXPECT_EQ(Merged->PluginID, "org.neverc.test.object-merge");
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
  ASSERT_TRUE(static_cast<bool>(Target))
      << errorText(Target.takeError());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  std::array<PluginObjectGraph *, 2> Inputs{First.get(), Second.get()};
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Inputs);
  ASSERT_TRUE(static_cast<bool>(Merged))
      << errorText(Merged.takeError());
  ASSERT_NE(Merged->Object, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*Merged->Object));
  EXPECT_EQ(Merged->PluginID, "neverc.builtin");
  EXPECT_EQ(Merged->ProviderID, "neverc.builtin.object-merge");

  const auto HasSymbol = [&](StringRef Name) {
    const auto &Symbols = Merged->Object->symbols();
    return std::any_of(Symbols.begin(), Symbols.end(),
                       [&](const PluginObjectSymbol &Symbol) {
                         return Symbol.Name == Name;
                       });
  };
  EXPECT_TRUE(HasSymbol("merge_first"));
  EXPECT_TRUE(HasSymbol("merge_second"));
}

} // namespace
