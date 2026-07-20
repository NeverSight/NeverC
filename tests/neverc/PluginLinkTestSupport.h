#ifndef NEVERC_TESTS_PLUGINLINKTESTSUPPORT_H
#define NEVERC_TESTS_PLUGINLINKTESTSUPPORT_H

#include "Link/LinkGraph.h"
#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <memory>
#include <optional>
#include <string>

namespace neverc::plugin::test_support {

inline constexpr const char *LinkTestPluginID =
    "org.neverc.test.link-route";

inline std::string errorText(llvm::Error Value) {
  return llvm::toString(std::move(Value)).str().str();
}

inline llvm::Expected<OwnedTargetKey> makeTargetKey() {
  return TargetKeyBuilder()
      .setTargetID({UINT64_C(0x4e43504c47524150), UINT64_C(1)})
      .setTriple("x86_64-neverc-none", "x86_64", "neverc", "none", "")
      .setCPU("generic", "generic")
      .setFeatures({})
      .setABI({UINT64_C(0x4e43504142495401), UINT64_C(1)})
      .setCallingConvention({UINT64_C(0x4e43504343495401), UINT64_C(1)})
      .setObjectFormat({UINT64_C(0x4e43504f424a5446), UINT64_C(1)})
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef"
          "0123456789abcdef0123456789abcdef")
      .build();
}

class LinkTaskScope {
public:
  LinkTaskScope()
      : Services("neverc-plugin-link-mutation-tests",
                 LLVM_VERSION_MAJOR) {}

  bool initialize() {
    if (llvm::Error E = registerPluginIOInterface(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (llvm::Error E = registerPluginLinkInterfaces(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (llvm::Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    auto Loaded =
        Services.registry().load(NEVERC_TEST_LINK_ROUTE_PLUGIN);
    if (!Loaded) {
      ADD_FAILURE() << errorText(Loaded.takeError());
      return false;
    }
    auto PhaseQuery = Services.interfaces().query(
        {NEVERC_INTERFACE_LINK_PHASE_HIGH,
         NEVERC_INTERFACE_LINK_PHASE_LOW},
        NEVERC_LINK_PHASE_API_MAJOR, NEVERC_LINK_PHASE_API_MINOR);
    if (!PhaseQuery) {
      ADD_FAILURE() << errorText(PhaseQuery.takeError());
      return false;
    }
    PhaseAPI =
        static_cast<const NevercLinkPhaseAPI *>(PhaseQuery->Table);
    const std::array<llvm::StringRef, 1> Selected = {
        LinkTestPluginID};
    auto CreatedPlan =
        makePluginActivationPlan(Services.registry(), Selected);
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
    auto CreatedTask = Session->createTask(NEVERC_TASK_LINK);
    if (!CreatedTask) {
      ADD_FAILURE() << errorText(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    return true;
  }

  ~LinkTaskScope() {
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginTaskContext &task() { return *Task; }
  PluginSession &session() { return *Session; }
  const NevercLinkPhaseAPI &phaseAPI() const { return *PhaseAPI; }
  PluginProcessServices &services() { return Services; }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  const NevercLinkPhaseAPI *PhaseAPI = nullptr;
};

struct GraphEntities {
  uint64_t InputID = 0;
  uint64_t SectionID = 0;
  uint64_t AtomID = 0;
  uint64_t SymbolID = 0;
};

inline GraphEntities populateValidGraph(PluginLinkGraph &Graph) {
  GraphEntities Result;
  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///valid.o";
  Result.InputID = Graph.addInput(std::move(Input)).ID;

  PluginLinkSection Section;
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 16;
  Section.Size = 8;
  Section.Origin.InputID = Result.InputID;
  Result.SectionID = Graph.addSection(std::move(Section)).ID;

  PluginLinkAtom Atom;
  Atom.SectionID = Result.SectionID;
  Atom.Name = "entry";
  Atom.Alignment = 16;
  Atom.Content = {0x90, 0x90, 0x90, 0x90,
                  0x90, 0x90, 0x90, 0xc3};
  Atom.Origin.InputID = Result.InputID;
  Result.AtomID = Graph.addAtom(std::move(Atom)).ID;

  PluginLinkSymbol Symbol;
  Symbol.Name = "entry";
  Symbol.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  Symbol.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Symbol.AtomID = Result.AtomID;
  Symbol.IsPrevailing = true;
  Symbol.IsRoot = true;
  Symbol.Origin.InputID = Result.InputID;
  Result.SymbolID = Graph.addSymbol(std::move(Symbol)).ID;
  return Result;
}

} // namespace neverc::plugin::test_support

#endif
