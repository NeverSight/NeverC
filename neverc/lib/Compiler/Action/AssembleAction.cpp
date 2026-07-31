#include "neverc/Compiler/AssembleAction.h"
#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Plugin/Host/AssemblyArtifacts.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/MCAsmPrinterProvider.h"
#include "neverc/Plugin/Host/MCUnit.h"
#include "neverc/Plugin/Host/ObjectGraph.h"
#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "neverc/Plugin/Host/PluginAssemblyPipeline.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "neverc/Plugin/Host/PluginTargetInfo.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>
#include <string>
#include <unordered_map>

using namespace llvm;

namespace neverc {
namespace {

constexpr uint64_t MaximumPluginAssemblyObjectSize =
    UINT64_C(64) * 1024 * 1024;

void reportAssemblerError(CompilerInstance &CI, const Twine &Message) {
  CI.getDiagnostics().Report(diag::err_fe_error_backend)
      << Message.str();
}

Expected<plugin::OwnedTargetKey>
buildPluginTargetKey(
    const plugin::PluginTargetSnapshot::TargetRecord &Target,
    const TargetOptions &Options) {
  const auto &Machine = Target.Machine;
  plugin::TargetKeyBuilder Builder;
  Builder
      .setTargetID(Target.ID)
      .setTriple(Options.Triple.empty() ? Machine.RawTriple
                                        : Options.Triple,
                 Machine.Architecture, Machine.Vendor,
                 Machine.OperatingSystem, Machine.Environment)
      .setCPU(Options.CPU.empty() ? Machine.DefaultCPU : Options.CPU,
              Options.TuneCPU.empty() ? Machine.TuneCPU
                                      : Options.TuneCPU)
      .setFeatures(Options.Features)
      .setABI(Target.DefaultABI)
      .setCallingConvention(Target.DefaultCallingConvention)
      .setObjectFormat(Target.DefaultObjectFormatID)
      .setCodeGeneration(Machine.DefaultRelocationModel,
                         Machine.DefaultCodeModel)
      .setExecution(Machine.DefaultExecutionLevel, Machine.PointerWidth,
                    Machine.Endianness)
      .setSchemaDigest(Machine.SchemaDigest);
  return Builder.build();
}

Expected<std::unique_ptr<plugin::PluginObjectGraph>>
lowerPluginAssemblyToObjectGraph(
    plugin::OwnedTargetKey Target, const plugin::PluginMCUnit &Unit) {
  auto Graph =
      std::make_unique<plugin::PluginObjectGraph>(std::move(Target));
  std::unordered_map<const plugin::PluginMCSection *, uint64_t>
      SectionIDs;

  for (const auto &MCSectionStorage : Unit.sections()) {
    const plugin::PluginMCSection &MCSection = *MCSectionStorage;
    plugin::PluginObjectSection Section;
    Section.ID = Graph->allocateEntityID();
    Section.Name = MCSection.Name;
    Section.Alignment = MCSection.Alignment;
    Section.Kind =
        (MCSection.Flags & NEVERC_MC_SECTION_EXECUTABLE) != 0
            ? NEVERC_OBJECT_SECTION_KIND_TEXT
            : (MCSection.Flags & NEVERC_MC_SECTION_DEBUG) != 0
                  ? NEVERC_OBJECT_SECTION_KIND_DEBUG
                  : NEVERC_OBJECT_SECTION_KIND_DATA;
    if ((MCSection.Flags & NEVERC_MC_SECTION_ALLOCATED) != 0)
      Section.Flags |= NEVERC_OBJECT_SECTION_ALLOCATED;
    if ((MCSection.Flags & NEVERC_MC_SECTION_EXECUTABLE) != 0)
      Section.Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
    if ((MCSection.Flags & NEVERC_MC_SECTION_WRITABLE) != 0)
      Section.Flags |= NEVERC_OBJECT_SECTION_WRITABLE;
    if ((MCSection.Flags & NEVERC_MC_SECTION_MERGEABLE) != 0)
      Section.Flags |= NEVERC_OBJECT_SECTION_MERGEABLE;
    if ((MCSection.Flags & NEVERC_MC_SECTION_DEBUG) != 0)
      Section.Flags |= NEVERC_OBJECT_SECTION_DEBUG;

    for (const auto &FragmentStorage : MCSection.Fragments) {
      const plugin::PluginMCFragment &Fragment = *FragmentStorage;
      if (!Fragment.Instructions.empty() || !Fragment.Fixups.empty())
        return createStringError(
            inconvertibleErrorCode(),
            "plugin assembly object lowering requires encoded fragments");
      if (Fragment.Alignment == 0 ||
          (Fragment.Alignment & (Fragment.Alignment - 1)) != 0)
        return createStringError(
            inconvertibleErrorCode(),
            "plugin assembly fragment alignment is invalid");
      uint64_t Offset = Fragment.ExplicitOffset;
      if (Offset == NEVERC_MC_AUTOMATIC_OFFSET) {
        const uint64_t Mask = Fragment.Alignment - 1;
        if (Section.Data.size() > UINT64_MAX - Mask)
          return createStringError(
              inconvertibleErrorCode(),
              "plugin assembly fragment offset overflows");
        Offset = (Section.Data.size() + Mask) & ~Mask;
      }
      if (Offset < Section.Data.size() ||
          Offset > MaximumPluginAssemblyObjectSize ||
          Fragment.Contents.size() >
              MaximumPluginAssemblyObjectSize - Offset)
        return createStringError(
            inconvertibleErrorCode(),
            "plugin assembly fragments overlap or exceed the size limit");
      Section.Data.resize(static_cast<size_t>(Offset), 0);
      Section.Data.insert(Section.Data.end(),
                          Fragment.Contents.begin(),
                          Fragment.Contents.end());
    }
    SectionIDs.emplace(&MCSection, Section.ID);
    Graph->sections().push_back(std::move(Section));
  }

  for (const auto &MCSymbolStorage : Unit.symbols()) {
    const plugin::PluginMCSymbol &MCSymbol = *MCSymbolStorage;
    plugin::PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = MCSymbol.Name;
    switch (MCSymbol.Binding) {
    case NEVERC_MC_SYMBOL_BINDING_LOCAL:
      Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
      break;
    case NEVERC_MC_SYMBOL_BINDING_GLOBAL:
      Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
      break;
    case NEVERC_MC_SYMBOL_BINDING_WEAK:
      Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_WEAK;
      break;
    default:
      return createStringError(
          inconvertibleErrorCode(),
          "plugin assembly symbol binding is unsupported");
    }
    switch (MCSymbol.Visibility) {
    case NEVERC_MC_SYMBOL_VISIBILITY_DEFAULT:
      Symbol.Visibility = NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT;
      break;
    case NEVERC_MC_SYMBOL_VISIBILITY_HIDDEN:
      Symbol.Visibility = NEVERC_OBJECT_SYMBOL_VISIBILITY_HIDDEN;
      break;
    case NEVERC_MC_SYMBOL_VISIBILITY_PROTECTED:
      Symbol.Visibility = NEVERC_OBJECT_SYMBOL_VISIBILITY_PROTECTED;
      break;
    default:
      return createStringError(
          inconvertibleErrorCode(),
          "plugin assembly symbol visibility is unsupported");
    }
    switch (MCSymbol.Type) {
    case NEVERC_MC_SYMBOL_TYPE_NONE:
      Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
      break;
    case NEVERC_MC_SYMBOL_TYPE_FUNCTION:
      Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
      break;
    case NEVERC_MC_SYMBOL_TYPE_OBJECT:
      Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
      break;
    case NEVERC_MC_SYMBOL_TYPE_SECTION:
      Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_SECTION;
      break;
    case NEVERC_MC_SYMBOL_TYPE_TLS:
      Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_TLS;
      break;
    default:
      return createStringError(
          inconvertibleErrorCode(),
          "plugin assembly symbol type is unsupported");
    }
    switch (MCSymbol.Definition) {
    case NEVERC_MC_SYMBOL_DEFINITION_UNDEFINED:
      Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
      Symbol.Flags |= NEVERC_OBJECT_SYMBOL_IMPORTED;
      break;
    case NEVERC_MC_SYMBOL_DEFINITION_SECTION:
      Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
      break;
    case NEVERC_MC_SYMBOL_DEFINITION_ABSOLUTE:
      Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE;
      break;
    case NEVERC_MC_SYMBOL_DEFINITION_COMMON:
      Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON;
      break;
    default:
      return createStringError(
          inconvertibleErrorCode(),
          "plugin assembly symbol definition is unsupported");
    }
    if (MCSymbol.Section) {
      auto It = SectionIDs.find(MCSymbol.Section);
      if (It == SectionIDs.end())
        return createStringError(
            inconvertibleErrorCode(),
            "plugin assembly symbol references an unknown section");
      Symbol.SectionID = It->second;
    }
    Symbol.Value = MCSymbol.Value;
    Symbol.Size = MCSymbol.Size;
    Symbol.Alignment = MCSymbol.Alignment;
    if (Symbol.Binding != NEVERC_OBJECT_SYMBOL_BINDING_LOCAL &&
        Symbol.Definition != NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED)
      Symbol.Flags |= NEVERC_OBJECT_SYMBOL_EXPORTED;
    Graph->symbols().push_back(std::move(Symbol));
  }
  return Graph;
}

Error emitPluginAssemblyObject(
    plugin::PluginTaskContext &Task,
    std::shared_ptr<const plugin::PluginTargetSnapshot> Snapshot,
    const plugin::PluginTargetSnapshot::TargetRecord &Target,
    const TargetOptions &Options, const plugin::PluginMCUnit &Unit,
    raw_pwrite_stream &Output) {
  auto Key = buildPluginTargetKey(Target, Options);
  if (!Key)
    return Key.takeError();
  auto Graph =
      lowerPluginAssemblyToObjectGraph(std::move(*Key), Unit);
  if (!Graph)
    return Graph.takeError();
  auto Pipeline =
      plugin::ObjectPhasePipeline::create(Task, std::move(Snapshot));
  if (!Pipeline)
    return Pipeline.takeError();
  const std::string LogicalName =
      "assembly-" + std::to_string(Task.handle().Value) + ".obj";
  auto Image = (*Pipeline)->execute(
      **Graph, plugin::ObjectOutputDestination::memory(
                   LogicalName, MaximumPluginAssemblyObjectSize));
  if (!Image)
    return Image.takeError();
  auto Result = plugin::findPluginMemoryOutput(Task, LogicalName);
  if (!Result)
    return createStringError(
        inconvertibleErrorCode(),
        "plugin assembly object pipeline published no output");
  Output.write(
      reinterpret_cast<const char *>(Result->Bytes.data()),
      Result->Bytes.size());
  return Error::success();
}

} // namespace

void AssembleAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  SourceManager &NeverCSourceManager = CI.getSourceManager();
  std::optional<MemoryBufferRef> Input =
      NeverCSourceManager.getBufferOrNone(
          NeverCSourceManager.getMainFileID());
  if (!Input) {
    reportAssemblerError(CI, "assembly input buffer is unavailable");
    return;
  }

  const TargetOptions &TargetOpts = CI.getTargetOpts();
  Triple TargetTriple = CI.getTarget().getTriple();
  std::string LookupError;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TargetTriple.str(), LookupError);

  StringRef OutputPath = CI.getFrontendOpts().OutputFile;
  if (OutputPath.empty())
    OutputPath = "-";
  std::unique_ptr<raw_pwrite_stream> Output =
      CI.createOutputFile(OutputPath, /*Binary=*/true,
                          /*RemoveFileOnSignal=*/true,
                          CI.getFrontendOpts().UseTemporary);
  if (!Output)
    return;

  const std::string Features =
      llvm::join(TargetOpts.FeaturesAsWritten, ",");
  MemoryBufferRef AssemblyInput = *Input;
  std::string TransformedAssembly;
  if (const auto *PluginTarget =
          CI.getTarget().getPluginTargetInfo()) {
    plugin::PluginTaskContext *Task =
        CI.getPluginTaskContext();
    if (!Task) {
      reportAssemblerError(
          CI, "plugin assembly target has no active plugin task");
      return;
    }
    auto Snapshot = plugin::findPluginTargetSnapshot(
        Task->processServices(), Task->session().handle());
    if (!Snapshot) {
      reportAssemblerError(
          CI, "plugin assembly target snapshot is unavailable");
      return;
    }
    auto Pipeline = plugin::PluginAssemblyPipelineRuntime::create(
        *Task, Snapshot);
    if (!Pipeline) {
      reportAssemblerError(CI, toString(Pipeline.takeError()));
      return;
    }
    if ((*Pipeline)->replacesParser()) {
      plugin::AssemblySourceArtifact Source;
      // MemoryBuffer identifiers are optional; the action input owns the
      // stable logical name required by the plugin artifact contract.
      Source.Identifier = getCurrentFileOrBufferName().str();
      Source.Buffer = Input->getBuffer().str();
      if (Error E = Source.verify()) {
        reportAssemblerError(CI, toString(std::move(E)));
        return;
      }
      auto Unit = (*Pipeline)->parse(
          Source, PluginTarget->record().ID,
          []() -> Expected<std::unique_ptr<plugin::PluginMCUnit>> {
            return createStringError(
                inconvertibleErrorCode(),
                "builtin parser cannot produce a plugin MC unit");
          });
      if (!Unit) {
        reportAssemblerError(CI, toString(Unit.takeError()));
        return;
      }
      if (!TheTarget) {
        if (Error E = emitPluginAssemblyObject(
                *Task, std::move(Snapshot),
                PluginTarget->record(), TargetOpts, **Unit, *Output))
          reportAssemblerError(CI, toString(std::move(E)));
        return;
      }
      auto Printed = (*Pipeline)->print(
          **Unit, [&]() -> Expected<std::string> {
            std::string Text;
            raw_string_ostream Stream(Text);
            if (Error E = plugin::BuiltinLLVMAsmPrinter::print(
                    *TheTarget, TargetTriple, TargetOpts.CPU,
                    Features, **Unit, Stream))
              return std::move(E);
            Stream.flush();
            return Text;
          });
      if (!Printed) {
        reportAssemblerError(CI, toString(Printed.takeError()));
        return;
      }
      TransformedAssembly = std::move(Printed->Text);
      AssemblyInput = MemoryBufferRef(
          TransformedAssembly, Input->getBufferIdentifier());
    }
  }
  if (!TheTarget) {
    reportAssemblerError(CI, LookupError);
    return;
  }
  plugin::BuiltinLLVMAsmParserRequest Request{
      TheTarget, TargetTriple, TargetOpts.CPU, Features,
      TargetOpts.SDKVersion, AssemblyInput, Output.get()};
  if (Error E = plugin::runBuiltinLLVMAsmParser(Request))
    reportAssemblerError(CI, toString(std::move(E)));
}

} // namespace neverc
