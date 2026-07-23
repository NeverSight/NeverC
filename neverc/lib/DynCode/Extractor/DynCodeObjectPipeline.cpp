// The format-agnostic dyncode extraction pipeline: see DynCodeObjectPipeline.h.

#include "Extractor/DynCodeObjectPipeline.h"
#include "Binary/DynCodeBinaryPhaseExecutor.h"
#include "Binary/DynCodeCharsetRegistry.h"
#include "Binary/DynCodeRewriteRegistry.h"
#include "Extractor/DynCodeFinalVerifier.h"
#include "Extractor/DynCodeRelocationProvider.h"
#include "Extractor/ObjectGraphExtractor.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/ObjectGraph.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginTarget.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <optional>

using namespace llvm;

namespace neverc {
namespace dyncode {

namespace {

/// A self-contained plugin task scope for reading one relocatable object into
/// an ObjectGraph.  dyncode extraction runs after codegen as a leaf job; a
/// no-plugin compile has no driver session, so the extractor owns a minimal
/// PluginProcessServices/Session/Task rather than reaching for process-global
/// state.  There are no global singletons, so this coexists with any driver
/// plugin services.
class ObjectReadScope {
public:
  ObjectReadScope()
      : Services("neverc-dyncode-extractor", LLVM_VERSION_MAJOR) {}

  Error initialize() {
    if (Error E = Services.interfaces().freeze())
      return E;
    auto CreatedPlan = plugin::makePluginActivationPlan(Services.registry(), {});
    if (!CreatedPlan)
      return CreatedPlan.takeError();
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = plugin::PluginSession::create(Services, *Plan);
    if (!CreatedSession)
      return CreatedSession.takeError();
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_CODEGEN);
    if (!CreatedTask)
      return CreatedTask.takeError();
    Task = std::move(*CreatedTask);
    return Error::success();
  }

  ~ObjectReadScope() {
    if (Task)
      consumeError(Task->end());
    if (Session)
      consumeError(Session->end());
    Plan.reset();
    consumeError(Services.shutdown());
  }

  plugin::PluginTaskContext &task() { return *Task; }

private:
  plugin::PluginProcessServices Services;
  std::optional<plugin::PluginActivationPlan> Plan;
  std::unique_ptr<plugin::PluginSession> Session;
  std::unique_ptr<plugin::PluginTaskContext> Task;
};

NevercTargetExecutionLevel executionLevel(const DynCodeOptions &Opts) {
  return Opts.Target.Level == ExecutionLevel::Kernel
             ? NEVERC_TARGET_EXECUTION_KERNEL
             : NEVERC_TARGET_EXECUTION_USER;
}

/// Reads the relocatable object into a verified ObjectGraph through the built-in
/// LLVM object Reader.  The built-in ELF/COFF/Mach-O readers are always present
/// in the format registry, so an empty target snapshot suffices for native
/// targets.
Expected<std::unique_ptr<plugin::PluginObjectGraph>>
readObjectGraph(ObjectReadScope &Scope, ArrayRef<uint8_t> ObjectBytes,
                StringRef LogicalPath, const DynCodeOptions &Opts) {
  const plugin::BuiltinTargetRoute *Route =
      plugin::findBuiltinTargetRoute(Opts.TargetTriple);
  if (!Route)
    return createStringError(
        errc::invalid_argument,
        "dyncode extraction: no built-in target route for triple '%s'",
        Opts.TargetTriple.c_str());

  auto Target = plugin::createBuiltinTargetKey(
      *Route, Opts.TargetTriple,
      Opts.CPU.empty() ? Route->DefaultCPU : StringRef(Opts.CPU),
      NEVERC_TARGET_RELOCATION_PIC, NEVERC_TARGET_CODE_MODEL_SMALL,
      executionLevel(Opts));
  if (!Target)
    return Target.takeError();

  auto Snapshot = std::make_shared<plugin::PluginTargetSnapshot>();
  auto Reader = plugin::ObjectReaderProvider::create(std::move(Snapshot));
  if (!Reader)
    return Reader.takeError();

  auto Graph =
      (*Reader)->read(Scope.task(), ObjectBytes, LogicalPath, *Target);
  if (!Graph)
    return Graph.takeError();

  // The Reader produces a structurally-valid but un-laid-out graph; dyncode
  // re-lays-out fragments itself, so issue the layout proof the extractor
  // requires as its verified-input precondition.
  (*Graph)->issueLayoutProof();
  return Graph;
}

} // namespace

Expected<DynCodeExtractedOutput>
runDynCodeExtractionPipeline(ArrayRef<uint8_t> ObjectBytes,
                             StringRef LogicalPath, const DynCodeOptions &Opts) {
  ObjectReadScope Scope;
  if (Error E = Scope.initialize())
    return std::move(E);

  auto Graph = readObjectGraph(Scope, ObjectBytes, LogicalPath, Opts);
  if (!Graph)
    return Graph.takeError();

  // Plan sections/symbols/relocations and assemble the entry-first candidate
  // image (dyncode.extract.plan / layout / image).
  ObjectGraphExtractor Extractor(**Graph, Opts);
  auto Extracted = Extractor.run();
  if (!Extracted)
    return Extracted.takeError();

  DynCodeExtractedOutput Output;
  Output.Image = std::move(Extracted->Image);
  Output.Report = std::move(Extracted->Report);
  DynCodeExtractionPlan &Plan = Extracted->Plan;

  // Apply the intra-image relocations (dyncode.extract.relocate).
  if (Error E = resolveAndApplyDynCodeRelocations(Plan, Opts.Target,
                                                  Output.Image, Output.Report))
    return std::move(E);

  // Bounded binary phases: bad-byte rewrite, charset encode, size/align/pad
  // (dyncode.binary.*).  The default registries are empty; a disabled rewrite
  // and an absent charset run explicit no-op steps and the final audit still
  // runs.
  DynCodeRewriteRegistry Rewrites;
  DynCodeCharsetRegistry Charsets;
  if (Error E = runDynCodeBinaryPhases(Output.Image, Output.Report, Opts,
                                       Rewrites, Charsets))
    return std::move(E);

  // Sealed final verifier (dyncode.verify): plan + relocation + structural
  // binary checklist over the finished, immutable image.
  if (Error E = verifyDynCodeFinalImage(Plan, Output.Image, Output.Report, Opts))
    return std::move(E);

  if (Error E = Output.Report.freeze())
    return std::move(E);
  return Output;
}

} // namespace dyncode
} // namespace neverc
