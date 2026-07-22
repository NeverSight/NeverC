#include "Plugin/JobGraph.h"
#include "Plugin/PluginCommand.h"
#include "ToolChains/NeverC.h"
#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/Compilation.h"
#include "neverc/Invoke/Tool.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <limits>
#include <new>

using namespace llvm;

namespace neverc::driver {
namespace {

constexpr size_t MaximumJobCount = UINT64_C(1) << 20;
constexpr size_t MaximumJobEdges = UINT64_C(1) << 22;
constexpr size_t MaximumJobStringBytes = UINT64_C(1) << 20;

Error jobGraphError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

const DriverJobGraphNode *
findJob(ArrayRef<DriverJobGraphNode> Jobs, NevercJobID ID) {
  auto It = llvm::find_if(
      Jobs, [&](const DriverJobGraphNode &Job) { return Job.ID == ID; });
  return It == Jobs.end() ? nullptr : &*It;
}

const DriverMaterializedAction *
findAction(ArrayRef<DriverMaterializedAction> Actions,
           NevercActionNodeID ID) {
  auto It = llvm::find_if(
      Actions, [&](const DriverMaterializedAction &ActionValue) {
        return ActionValue.ID == ID;
      });
  return It == Actions.end() ? nullptr : &*It;
}

bool validText(StringRef Text, bool AllowEmpty) {
  return (AllowEmpty || !Text.empty()) &&
         Text.size() <= MaximumJobStringBytes && !Text.contains('\0') &&
         json::isUTF8(Text);
}

bool validJobKind(NevercJobKind Kind) {
  return Kind >= NEVERC_JOB_COMMAND && Kind <= NEVERC_JOB_DYNCODE;
}

Expected<NevercJobKind> toPublicJobKind(Command::CommandKind Kind) {
  switch (Kind) {
  case Command::CK_Command:
    return NEVERC_JOB_COMMAND;
  case Command::CK_FrontendCommand:
    return NEVERC_JOB_FRONTEND;
  case Command::CK_LinkerCommand:
    return NEVERC_JOB_LINKER;
  case Command::CK_ArchiveCommand:
    return NEVERC_JOB_ARCHIVE;
  case Command::CK_PluginCommand:
    return NEVERC_JOB_PLUGIN;
  case Command::CK_DynCodeCommand:
    return NEVERC_JOB_DYNCODE;
  }
  return jobGraphError("unknown internal job kind");
}

Expected<std::pair<NevercResponseFileKind, NevercResponseFileEncoding>>
toPublicResponseFileSupport(const ResponseFileSupport &Support) {
  NevercResponseFileKind Kind = NEVERC_RESPONSE_FILE_NONE;
  switch (Support.ResponseKind) {
  case ResponseFileSupport::RF_None:
    Kind = NEVERC_RESPONSE_FILE_NONE;
    break;
  case ResponseFileSupport::RF_Full:
    Kind = NEVERC_RESPONSE_FILE_FULL;
    break;
  case ResponseFileSupport::RF_FileList:
    Kind = NEVERC_RESPONSE_FILE_LIST;
    break;
  }
  NevercResponseFileEncoding Encoding = NEVERC_RESPONSE_ENCODING_UTF8;
  switch (Support.ResponseEncoding) {
  case llvm::sys::WEM_UTF8:
    Encoding = NEVERC_RESPONSE_ENCODING_UTF8;
    break;
  case llvm::sys::WEM_CurrentCodePage:
    Encoding = NEVERC_RESPONSE_ENCODING_CURRENT_CODE_PAGE;
    break;
  case llvm::sys::WEM_UTF16:
    Encoding = NEVERC_RESPONSE_ENCODING_UTF16;
    break;
  }
  return std::make_pair(Kind, Encoding);
}

Expected<ResponseFileSupport>
toInternalResponseFileSupport(NevercResponseFileKind Kind,
                              NevercResponseFileEncoding Encoding) {
  ResponseFileSupport Result = ResponseFileSupport::None();
  switch (Kind) {
  case NEVERC_RESPONSE_FILE_NONE:
    Result.ResponseKind = ResponseFileSupport::RF_None;
    Result.ResponseFlag = nullptr;
    break;
  case NEVERC_RESPONSE_FILE_FULL:
    Result.ResponseKind = ResponseFileSupport::RF_Full;
    Result.ResponseFlag = "@";
    break;
  case NEVERC_RESPONSE_FILE_LIST:
    Result.ResponseKind = ResponseFileSupport::RF_FileList;
    Result.ResponseFlag = "-filelist";
    break;
  default:
    return jobGraphError("job response-file kind is invalid");
  }
  switch (Encoding) {
  case NEVERC_RESPONSE_ENCODING_UTF8:
    Result.ResponseEncoding = llvm::sys::WEM_UTF8;
    break;
  case NEVERC_RESPONSE_ENCODING_CURRENT_CODE_PAGE:
    Result.ResponseEncoding = llvm::sys::WEM_CurrentCodePage;
    break;
  case NEVERC_RESPONSE_ENCODING_UTF16:
    Result.ResponseEncoding = llvm::sys::WEM_UTF16;
    break;
  default:
    return jobGraphError("job response-file encoding is invalid");
  }
  return Result;
}

Expected<NevercLinkerFlavor> toPublicLinkerFlavor(LinkerFlavor Flavor) {
  switch (Flavor) {
  case LinkerFlavor::Invalid:
    return NEVERC_LINKER_FLAVOR_NONE;
  case LinkerFlavor::Gnu:
    return NEVERC_LINKER_FLAVOR_GNU;
  case LinkerFlavor::WinLink:
    return NEVERC_LINKER_FLAVOR_WIN_LINK;
  case LinkerFlavor::Darwin:
    return NEVERC_LINKER_FLAVOR_DARWIN;
  }
  return jobGraphError("unknown internal linker flavor");
}

Expected<LinkerFlavor> toInternalLinkerFlavor(NevercLinkerFlavor Flavor) {
  switch (Flavor) {
  case NEVERC_LINKER_FLAVOR_NONE:
    return LinkerFlavor::Invalid;
  case NEVERC_LINKER_FLAVOR_GNU:
    return LinkerFlavor::Gnu;
  case NEVERC_LINKER_FLAVOR_WIN_LINK:
    return LinkerFlavor::WinLink;
  case NEVERC_LINKER_FLAVOR_DARWIN:
    return LinkerFlavor::Darwin;
  default:
    return jobGraphError("stable linker flavor is invalid");
  }
}

Error verifyJobGraph(
    const DriverJobGraphData &Graph,
    ArrayRef<DriverMaterializedAction> AllowedActions,
    ArrayRef<std::string> AllowedExternalInputs) {
  if (Graph.Nodes.empty())
    return jobGraphError("job graph has no jobs");
  if (Graph.Nodes.size() > MaximumJobCount)
    return jobGraphError("job graph exceeds the job limit");

  DenseSet<NevercActionNodeID> ActionIDs;
  for (const DriverMaterializedAction &ActionValue : AllowedActions)
    if (ActionValue.ID == 0 || !ActionValue.Value ||
        !ActionIDs.insert(ActionValue.ID).second)
      return jobGraphError("job graph action catalog is invalid");

  StringSet<> ExternalInputs;
  for (const std::string &Input : AllowedExternalInputs) {
    if (!validText(Input, false))
      return jobGraphError("job graph external input catalog is invalid");
    ExternalInputs.insert(Input);
  }

  DenseSet<NevercJobID> JobIDs;
  DenseMap<NevercJobID, size_t> JobOrder;
  StringMap<NevercJobID> OutputProducers;
  StringSet<> CallbackIDs;
  size_t EdgeCount = 0;
  for (size_t Index = 0; Index != Graph.Nodes.size(); ++Index) {
    const DriverJobGraphNode &Job = Graph.Nodes[Index];
    if (Job.ID == 0 || !JobIDs.insert(Job.ID).second)
      return jobGraphError("job graph ID is invalid or duplicated");
    JobOrder[Job.ID] = Index;
    if (!validJobKind(Job.Kind))
      return jobGraphError("job graph kind is invalid");
    if (!ActionIDs.contains(Job.SourceAction))
      return jobGraphError("job graph source action is unknown");
    if (Job.Dependencies.size() > MaximumJobEdges - EdgeCount)
      return jobGraphError("job graph exceeds the dependency limit");
    EdgeCount += Job.Dependencies.size();
    auto Response = toInternalResponseFileSupport(
        Job.ResponseFileKind, Job.ResponseFileEncoding);
    if (!Response)
      return Response.takeError();
    if (Job.Kind == NEVERC_JOB_LINKER) {
      auto Flavor = toInternalLinkerFlavor(Job.LinkerFlavor);
      if (!Flavor || *Flavor == LinkerFlavor::Invalid)
        return Flavor ? jobGraphError("linker job has no linker flavor")
                      : Flavor.takeError();
    } else if (Job.LinkerFlavor != NEVERC_LINKER_FLAVOR_NONE) {
      return jobGraphError("non-linker job declares a linker flavor");
    }
    if (Job.Kind == NEVERC_JOB_PLUGIN) {
      if (!Job.InProcess || !Job.Callback ||
          !validText(Job.PluginID, false) ||
          !validText(Job.CallbackID, false) ||
          !Job.Environment.empty())
        return jobGraphError("plugin job callback descriptor is invalid");
      std::string Key =
          (Twine(Job.PluginID) + ":" + Job.CallbackID).str();
      if (!CallbackIDs.insert(Key).second)
        return jobGraphError("plugin job callback ID is duplicated");
    } else {
      if (!validText(Job.Executable, false) || Job.Callback ||
          !Job.PluginID.empty() || !Job.CallbackID.empty())
        return jobGraphError("builtin job descriptor is invalid");
      if (Job.InProcess && !Job.Environment.empty())
        return jobGraphError(
            "in-process job cannot override the environment");
    }
    for (const std::string &Argument : Job.Arguments)
      if (!validText(Argument, true))
        return jobGraphError("job argument is invalid");
    for (const std::string &Entry : Job.Environment)
      if (!validText(Entry, false))
        return jobGraphError("job environment entry is invalid");
    auto VerifyFiles = [&](ArrayRef<DriverJobFileRecord> Files,
                           bool IsOutput) -> Error {
      for (const DriverJobFileRecord &File : Files) {
        if (!validText(File.Path, false))
          return jobGraphError("job file path is invalid");
        auto Internal = toInternalDriverType(File.PublicType);
        if (!Internal)
          return Internal.takeError();
        if (*Internal != File.InternalType)
          return jobGraphError("job file type mapping is inconsistent");
        if (IsOutput &&
            !OutputProducers.try_emplace(File.Path, Job.ID).second)
          return jobGraphError("job graph output path is duplicated");
      }
      return Error::success();
    };
    if (Error E = VerifyFiles(Job.Inputs, false))
      return E;
    if (Error E = VerifyFiles(Job.Outputs, true))
      return E;
  }

  for (size_t Index = 0; Index != Graph.Nodes.size(); ++Index) {
    const DriverJobGraphNode &Job = Graph.Nodes[Index];
    DenseSet<NevercJobID> Dependencies;
    for (NevercJobID Dependency : Job.Dependencies) {
      auto Order = JobOrder.find(Dependency);
      if (Dependency == 0 || Order == JobOrder.end())
        return jobGraphError(
            "job dependency references an unknown job");
      if (Dependency == Job.ID || Order->second >= Index)
        return jobGraphError(
            "job graph is not in a stable topological order");
      if (!Dependencies.insert(Dependency).second)
        return jobGraphError("job dependency is duplicated");
    }
    for (const DriverJobFileRecord &Input : Job.Inputs) {
      auto Producer = OutputProducers.find(Input.Path);
      if (Producer != OutputProducers.end()) {
        if (Producer->second == Job.ID ||
            !Dependencies.contains(Producer->second))
          return jobGraphError(
              "job input producer is not an explicit dependency");
      } else if (!ExternalInputs.contains(Input.Path)) {
        return jobGraphError(
            "job input is neither produced nor declared external");
      }
    }
  }
  return Error::success();
}

DriverJobFileRecord snapshotFile(const InputInfo &Input) {
  DriverJobFileRecord File;
  File.Path = Input.getFilename();
  File.InternalType = Input.getType();
  auto PublicType = toPublicDriverType(Input.getType());
  if (PublicType)
    File.PublicType = *PublicType;
  else
    consumeError(PublicType.takeError());
  return File;
}

} // namespace

DriverJobGraphArtifact::DriverJobGraphArtifact(
    DriverJobGraphData GraphValue,
    std::vector<DriverMaterializedAction> AllowedActionsValue,
    std::vector<std::string> AllowedExternalInputsValue)
    : Graph(std::move(GraphValue)),
      AllowedActions(std::move(AllowedActionsValue)),
      AllowedExternalInputs(std::move(AllowedExternalInputsValue)) {}

DriverJobGraphArtifact::DriverJobGraphArtifact(
    const DriverJobGraphArtifact &Other) {
  std::lock_guard<std::mutex> Lock(Other.Mutex);
  Graph = Other.Graph;
  AllowedActions = Other.AllowedActions;
  AllowedExternalInputs = Other.AllowedExternalInputs;
}

Error DriverJobGraphArtifact::verify() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return verifyJobGraph(Graph, AllowedActions, AllowedExternalInputs);
}

uint64_t DriverJobGraphArtifact::jobCount() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Graph.Nodes.size();
}

bool DriverJobGraphArtifact::describeJob(
    uint64_t Index, NevercJob &OutJob) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (Index >= Graph.Nodes.size())
    return false;
  const DriverJobGraphNode &Job = Graph.Nodes[static_cast<size_t>(Index)];
  OutJob.Job = Job.ID;
  OutJob.Kind = Job.Kind;
  OutJob.ResponseFileKind = Job.ResponseFileKind;
  OutJob.ResponseFileEncoding = Job.ResponseFileEncoding;
  OutJob.InProcess = Job.InProcess ? NEVERC_TRUE : NEVERC_FALSE;
  OutJob.SourceAction = Job.SourceAction;
  OutJob.LinkerFlavor = Job.LinkerFlavor;
  OutJob.Executable = {Job.Executable.data(), Job.Executable.size()};
  OutJob.CallbackID = {Job.CallbackID.data(), Job.CallbackID.size()};
  OutJob.ArgumentCount = Job.Arguments.size();
  OutJob.EnvironmentCount = Job.Environment.size();
  OutJob.InputCount = Job.Inputs.size();
  OutJob.OutputCount = Job.Outputs.size();
  OutJob.DependencyCount = Job.Dependencies.size();
  return true;
}

bool DriverJobGraphArtifact::getDependency(
    NevercJobID JobID, uint64_t Index,
    NevercJobID &OutDependency) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  const DriverJobGraphNode *Job = findJob(Graph.Nodes, JobID);
  if (!Job || Index >= Job->Dependencies.size())
    return false;
  OutDependency = Job->Dependencies[static_cast<size_t>(Index)];
  return true;
}

bool DriverJobGraphArtifact::getArgument(
    NevercJobID JobID, uint64_t Index, NevercStringView &OutValue) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  const DriverJobGraphNode *Job = findJob(Graph.Nodes, JobID);
  if (!Job || Index >= Job->Arguments.size())
    return false;
  const std::string &Value = Job->Arguments[static_cast<size_t>(Index)];
  OutValue = {Value.data(), Value.size()};
  return true;
}

bool DriverJobGraphArtifact::getEnvironment(
    NevercJobID JobID, uint64_t Index, NevercStringView &OutValue) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  const DriverJobGraphNode *Job = findJob(Graph.Nodes, JobID);
  if (!Job || Index >= Job->Environment.size())
    return false;
  const std::string &Value = Job->Environment[static_cast<size_t>(Index)];
  OutValue = {Value.data(), Value.size()};
  return true;
}

bool DriverJobGraphArtifact::getInput(
    NevercJobID JobID, uint64_t Index, NevercJobFile &OutFile) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  const DriverJobGraphNode *Job = findJob(Graph.Nodes, JobID);
  if (!Job || Index >= Job->Inputs.size())
    return false;
  const DriverJobFileRecord &File =
      Job->Inputs[static_cast<size_t>(Index)];
  OutFile.Path = {File.Path.data(), File.Path.size()};
  OutFile.Type = File.PublicType;
  return true;
}

bool DriverJobGraphArtifact::getOutput(
    NevercJobID JobID, uint64_t Index, NevercJobFile &OutFile) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  const DriverJobGraphNode *Job = findJob(Graph.Nodes, JobID);
  if (!Job || Index >= Job->Outputs.size())
    return false;
  const DriverJobFileRecord &File =
      Job->Outputs[static_cast<size_t>(Index)];
  OutFile.Path = {File.Path.data(), File.Path.size()};
  OutFile.Type = File.PublicType;
  return true;
}

DriverJobGraphData DriverJobGraphArtifact::snapshot() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Graph;
}

std::vector<DriverMaterializedAction>
DriverJobGraphArtifact::actionCatalog() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return AllowedActions;
}

Expected<std::unique_ptr<DriverJobGraphEdit>>
DriverJobGraphArtifact::beginMutation() {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (MutationActive)
    return jobGraphError("job graph already has an active mutation");
  auto *Edit = new (std::nothrow)
      DriverJobGraphEdit(this, Graph, AllowedActions, AllowedExternalInputs);
  if (!Edit)
    return jobGraphError("unable to allocate job graph mutation");
  MutationActive = true;
  return std::unique_ptr<DriverJobGraphEdit>(Edit);
}

Error DriverJobGraphArtifact::commitMutation(DriverJobGraphData NewGraph) {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (!MutationActive)
    return jobGraphError("job graph mutation is not active");
  Graph = std::move(NewGraph);
  MutationActive = false;
  return Error::success();
}

void DriverJobGraphArtifact::abortMutation() {
  std::lock_guard<std::mutex> Lock(Mutex);
  MutationActive = false;
}

DriverJobGraphEdit::DriverJobGraphEdit(
    DriverJobGraphArtifact *OwnerValue, DriverJobGraphData GraphValue,
    std::vector<DriverMaterializedAction> AllowedActionsValue,
    std::vector<std::string> AllowedExternalInputsValue)
    : Owner(OwnerValue), Graph(std::move(GraphValue)),
      AllowedActions(std::move(AllowedActionsValue)),
      AllowedExternalInputs(std::move(AllowedExternalInputsValue)) {
  for (const DriverJobGraphNode &Job : Graph.Nodes) {
    if (Job.ID == std::numeric_limits<NevercJobID>::max()) {
      NextJobID = 0;
      break;
    }
    NextJobID = std::max(NextJobID, Job.ID + 1);
  }
}

Expected<std::unique_ptr<DriverJobGraphEdit>>
DriverJobGraphEdit::createBuilder(
    const DriverActionGraphArtifact &ActionGraph) {
  std::vector<DriverMaterializedAction> Actions =
      ActionGraph.materializedActions();
  if (Actions.empty())
    return jobGraphError(
        "job graph builder requires materialized action nodes");
  std::vector<std::string> ExternalInputs;
  for (const DriverActionInputRecord &Input : ActionGraph.inputCatalog())
    ExternalInputs.push_back(Input.Value);
  auto *Edit = new (std::nothrow)
      DriverJobGraphEdit(nullptr, {}, std::move(Actions),
                         std::move(ExternalInputs));
  if (!Edit)
    return jobGraphError("unable to allocate job graph builder");
  return std::unique_ptr<DriverJobGraphEdit>(Edit);
}

DriverJobGraphEdit::~DriverJobGraphEdit() { abort(); }

DriverJobGraphNode *DriverJobGraphEdit::findJob(NevercJobID JobID) {
  auto It = llvm::find_if(Graph.Nodes, [&](const DriverJobGraphNode &Job) {
    return Job.ID == JobID;
  });
  return It == Graph.Nodes.end() ? nullptr : &*It;
}

Expected<NevercJobID>
DriverJobGraphEdit::addJob(DriverJobGraphNode Node) {
  if (Finished)
    return jobGraphError("job graph edit is finished");
  if (Graph.Nodes.size() >= MaximumJobCount || NextJobID == 0 ||
      NextJobID == std::numeric_limits<NevercJobID>::max())
    return jobGraphError("job graph ID space is exhausted");
  Node.ID = NextJobID++;
  Node.Original = nullptr;
  Node.Modified = true;
  Graph.Nodes.push_back(std::move(Node));
  return Graph.Nodes.back().ID;
}

Error DriverJobGraphEdit::removeJob(NevercJobID JobID) {
  if (Finished)
    return jobGraphError("job graph edit is finished");
  auto It = llvm::find_if(Graph.Nodes, [&](const DriverJobGraphNode &Job) {
    return Job.ID == JobID;
  });
  if (It == Graph.Nodes.end())
    return jobGraphError("job graph job is unknown");
  Graph.Nodes.erase(It);
  return Error::success();
}

Error DriverJobGraphEdit::moveJobBefore(
    NevercJobID JobID, NevercJobID BeforeID) {
  if (Finished)
    return jobGraphError("job graph edit is finished");
  auto JobIt = llvm::find_if(Graph.Nodes, [&](const DriverJobGraphNode &Job) {
    return Job.ID == JobID;
  });
  auto BeforeIt =
      llvm::find_if(Graph.Nodes, [&](const DriverJobGraphNode &Job) {
        return Job.ID == BeforeID;
      });
  if (JobIt == Graph.Nodes.end() || BeforeIt == Graph.Nodes.end())
    return jobGraphError("job graph reorder references an unknown job");
  if (JobIt == BeforeIt)
    return Error::success();
  DriverJobGraphNode Value = std::move(*JobIt);
  size_t BeforeIndex = static_cast<size_t>(BeforeIt - Graph.Nodes.begin());
  size_t JobIndex = static_cast<size_t>(JobIt - Graph.Nodes.begin());
  Graph.Nodes.erase(Graph.Nodes.begin() + JobIndex);
  if (JobIndex < BeforeIndex)
    --BeforeIndex;
  Graph.Nodes.insert(Graph.Nodes.begin() + BeforeIndex, std::move(Value));
  return Error::success();
}

Error DriverJobGraphEdit::replaceJob(
    NevercJobID JobID, DriverJobGraphNode Node) {
  if (Finished)
    return jobGraphError("job graph edit is finished");
  DriverJobGraphNode *Existing = findJob(JobID);
  if (!Existing)
    return jobGraphError("job graph job is unknown");
  Node.ID = Existing->ID;
  Node.Original = Existing->Original;
  Node.Modified = true;
  *Existing = std::move(Node);
  return Error::success();
}

Error DriverJobGraphEdit::setArgument(
    NevercJobID JobID, uint64_t Index, StringRef Value) {
  DriverJobGraphNode *Job = findJob(JobID);
  if (Finished || !Job || Index >= Job->Arguments.size())
    return jobGraphError("job argument edit is invalid");
  if (!validText(Value, true))
    return jobGraphError("job argument is invalid");
  Job->Arguments[static_cast<size_t>(Index)] = Value.str();
  Job->Modified = true;
  return Error::success();
}

Error DriverJobGraphEdit::setEnvironment(
    NevercJobID JobID, uint64_t Index, StringRef Value) {
  DriverJobGraphNode *Job = findJob(JobID);
  if (Finished || !Job || Index > Job->Environment.size())
    return jobGraphError("job environment edit is invalid");
  if (!validText(Value, false))
    return jobGraphError("job environment entry is invalid");
  if (Index == Job->Environment.size())
    Job->Environment.push_back(Value.str());
  else
    Job->Environment[static_cast<size_t>(Index)] = Value.str();
  Job->Modified = true;
  return Error::success();
}

Error DriverJobGraphEdit::setInput(
    NevercJobID JobID, uint64_t Index, DriverJobFileRecord File) {
  DriverJobGraphNode *Job = findJob(JobID);
  if (Finished || !Job || Index > Job->Inputs.size())
    return jobGraphError("job input edit is invalid");
  if (Index == Job->Inputs.size())
    Job->Inputs.push_back(std::move(File));
  else
    Job->Inputs[static_cast<size_t>(Index)] = std::move(File);
  Job->Modified = true;
  return Error::success();
}

Error DriverJobGraphEdit::setOutput(
    NevercJobID JobID, uint64_t Index, DriverJobFileRecord File) {
  DriverJobGraphNode *Job = findJob(JobID);
  if (Finished || !Job || Index > Job->Outputs.size())
    return jobGraphError("job output edit is invalid");
  if (Index == Job->Outputs.size())
    Job->Outputs.push_back(std::move(File));
  else
    Job->Outputs[static_cast<size_t>(Index)] = std::move(File);
  Job->Modified = true;
  return Error::success();
}

Error DriverJobGraphEdit::replaceDependencies(
    NevercJobID JobID, ArrayRef<NevercJobID> Dependencies) {
  DriverJobGraphNode *Job = findJob(JobID);
  if (Finished || !Job)
    return jobGraphError("job dependency edit is invalid");
  Job->Dependencies.assign(Dependencies.begin(), Dependencies.end());
  Job->Modified = true;
  return Error::success();
}

Expected<std::unique_ptr<DriverJobGraphArtifact>>
DriverJobGraphEdit::finishBuilder() {
  if (Finished || Owner)
    return jobGraphError("job graph builder is not publishable");
  auto *Artifact = new (std::nothrow)
      DriverJobGraphArtifact(std::move(Graph), std::move(AllowedActions),
                             std::move(AllowedExternalInputs));
  if (!Artifact)
    return jobGraphError("unable to allocate job graph artifact");
  Finished = true;
  return std::unique_ptr<DriverJobGraphArtifact>(Artifact);
}

Error DriverJobGraphEdit::commitMutation() {
  if (Finished || !Owner)
    return jobGraphError("job graph mutation is not committable");
  if (Error E =
          verifyJobGraph(Graph, AllowedActions, AllowedExternalInputs))
    return E;
  if (Error E = Owner->commitMutation(std::move(Graph)))
    return E;
  Finished = true;
  Owner = nullptr;
  return Error::success();
}

void DriverJobGraphEdit::abort() {
  if (Finished)
    return;
  if (Owner)
    Owner->abortMutation();
  Owner = nullptr;
  Finished = true;
}

NevercInterfaceID driverJobGraphArtifactID() {
  return {NEVERC_PHASE_DRIVER_BUILD_JOBS_OUTPUT_HIGH,
          NEVERC_PHASE_DRIVER_BUILD_JOBS_OUTPUT_LOW};
}

NevercInterfaceID driverBuildJobsPhaseID() {
  return {NEVERC_PHASE_DRIVER_BUILD_JOBS_HIGH,
          NEVERC_PHASE_DRIVER_BUILD_JOBS_LOW};
}

Expected<DriverJobGraphArtifactTypes>
registerDriverJobGraphArtifacts(
    plugin::PluginArtifactRegistry &Registry) {
  plugin::PluginArtifactTypeDescriptor Descriptor;
  Descriptor.ID = driverJobGraphArtifactID();
  Descriptor.Name = "neverc.driver.job_graph";
  Descriptor.Ownership = plugin::PluginArtifactOwnership::Owned;
  Descriptor.Clone = [](const void *Payload) -> Expected<void *> {
    if (!Payload)
      return jobGraphError("cannot clone a null job graph");
    auto *Copy = new (std::nothrow) DriverJobGraphArtifact(
        *static_cast<const DriverJobGraphArtifact *>(Payload));
    if (!Copy)
      return jobGraphError("unable to allocate job graph clone");
    return static_cast<void *>(Copy);
  };
  Descriptor.Destroy = [](void *Payload) {
    delete static_cast<DriverJobGraphArtifact *>(Payload);
  };
  Descriptor.Verify = [](const void *Payload) {
    if (!Payload)
      return jobGraphError("job graph payload is null");
    return static_cast<const DriverJobGraphArtifact *>(Payload)->verify();
  };
  auto Type = Registry.registerType(std::move(Descriptor));
  if (!Type)
    return Type.takeError();
  return DriverJobGraphArtifactTypes{*Type};
}

Expected<std::unique_ptr<DriverJobGraphArtifact>>
snapshotDriverJobGraph(
    Compilation &C, const DriverActionGraphArtifact &ActionGraph) {
  DriverJobGraphData Graph;
  std::vector<DriverMaterializedAction> Actions =
      ActionGraph.materializedActions();
  if (Actions.empty())
    return jobGraphError("job snapshot has no materialized actions");

  Graph.Nodes.reserve(C.getJobs().size());
  NevercJobID NextID = 1;
  for (Command &CommandValue : C.getJobs()) {
    DriverJobGraphNode Node;
    Node.ID = NextID++;
    auto Kind = toPublicJobKind(CommandValue.getKind());
    if (!Kind)
      return Kind.takeError();
    Node.Kind = *Kind;
    auto ActionIt = llvm::find_if(
        Actions, [&](const DriverMaterializedAction &ActionValue) {
          return ActionValue.Value == &CommandValue.getSource();
        });
    if (ActionIt == Actions.end())
      return jobGraphError(
          "job source is absent from the action graph");
    Node.SourceAction = ActionIt->ID;
    Node.Executable = CommandValue.getExecutable();
    for (const char *Argument : CommandValue.getArguments())
      Node.Arguments.emplace_back(Argument);
    for (const char *Entry : CommandValue.getEnvironment())
      Node.Environment.emplace_back(Entry);
    for (const InputInfo &Input : CommandValue.getInputInfos())
      Node.Inputs.push_back(snapshotFile(Input));
    auto OutputType =
        toPublicDriverType(CommandValue.getSource().getType());
    if (!OutputType)
      return OutputType.takeError();
    for (const std::string &Output :
         CommandValue.getOutputFilenames()) {
      Node.Outputs.push_back(
          {Output, *OutputType, CommandValue.getSource().getType()});
    }
    auto Response =
        toPublicResponseFileSupport(
            CommandValue.getResponseFileSupport());
    if (!Response)
      return Response.takeError();
    Node.ResponseFileKind = Response->first;
    Node.ResponseFileEncoding = Response->second;
    Node.InProcess = CommandValue.InProcess;
    if (CommandValue.getKind() == Command::CK_LinkerCommand) {
      const auto *Linker =
          static_cast<const LinkerCommand *>(&CommandValue);
      auto Flavor = toPublicLinkerFlavor(Linker->getFlavor());
      if (!Flavor)
        return Flavor.takeError();
      Node.LinkerFlavor = *Flavor;
    }
    Node.Original = &CommandValue;
    Node.Modified = false;
    Graph.Nodes.push_back(std::move(Node));
  }

  StringMap<NevercJobID> Producers;
  for (const DriverJobGraphNode &Node : Graph.Nodes)
    for (const DriverJobFileRecord &Output : Node.Outputs)
      Producers[Output.Path] = Node.ID;
  StringSet<> ExternalSet;
  for (DriverJobGraphNode &Node : Graph.Nodes) {
    for (const DriverJobFileRecord &Input : Node.Inputs) {
      auto Producer = Producers.find(Input.Path);
      if (Producer == Producers.end()) {
        ExternalSet.insert(Input.Path);
        continue;
      }
      if (Producer->second != Node.ID &&
          !llvm::is_contained(Node.Dependencies, Producer->second))
        Node.Dependencies.push_back(Producer->second);
    }
  }
  std::vector<std::string> ExternalInputs;
  ExternalInputs.reserve(ExternalSet.size());
  for (const auto &Input : ExternalSet)
    ExternalInputs.push_back(Input.getKey().str());

  auto Result = std::make_unique<DriverJobGraphArtifact>(
      std::move(Graph), std::move(Actions),
      std::move(ExternalInputs));
  if (Error E = Result->verify())
    return std::move(E);
  return std::move(Result);
}

Expected<std::unique_ptr<DriverJobExecutionPlan>>
materializeDriverJobGraph(
    Compilation &C, const DriverJobGraphArtifact &Artifact,
    std::shared_ptr<plugin::PluginSession> Session) {
  if (!Session)
    return jobGraphError("job graph materialization has no plugin session");
  if (Error E = Artifact.verify())
    return std::move(E);
  DriverJobGraphData Graph = Artifact.snapshot();
  std::vector<DriverMaterializedAction> Actions =
      Artifact.actionCatalog();

  struct PendingCommand {
    std::unique_ptr<Command> Rebuilt;
    Command *Original = nullptr;
  };
  std::vector<PendingCommand> Pending;
  Pending.reserve(Graph.Nodes.size());
  std::vector<std::string> RegisteredCallbacks;
  auto RollbackCallbacks = make_scope_exit([&] {
    for (const std::string &Callback : RegisteredCallbacks)
      Session->unregisterDeferredCallback(
          "neverc.driver.job", Callback);
  });

  for (const DriverJobGraphNode &Node : Graph.Nodes) {
    const DriverMaterializedAction *ActionRecord =
        findAction(Actions, Node.SourceAction);
    if (!ActionRecord || !ActionRecord->Value)
      return jobGraphError(
          "job materialization found an unknown source action");
    if (!Node.Modified && Node.Original) {
      Pending.push_back({nullptr, Node.Original});
      continue;
    }

    llvm::opt::ArgStringList Arguments;
    Arguments.reserve(Node.Arguments.size());
    for (const std::string &Argument : Node.Arguments)
      Arguments.push_back(C.getArgs().MakeArgString(Argument));
    InputInfoList Inputs;
    for (const DriverJobFileRecord &Input : Node.Inputs)
      Inputs.emplace_back(
          Input.InternalType, C.getArgs().MakeArgString(Input.Path), nullptr);
    InputInfoList Outputs;
    for (const DriverJobFileRecord &Output : Node.Outputs)
      Outputs.emplace_back(
          Output.InternalType, C.getArgs().MakeArgString(Output.Path), nullptr);
    auto Response = toInternalResponseFileSupport(
        Node.ResponseFileKind, Node.ResponseFileEncoding);
    if (!Response)
      return Response.takeError();
    const char *Executable =
        C.getArgs().MakeArgString(Node.Executable);
    const char *Prepend =
        Node.Original ? Node.Original->getPrependArg() : nullptr;
    Tool &Creator = Node.Original
                        ? const_cast<Tool &>(Node.Original->getCreator())
                        : C.getPluginCommandTool();

    std::unique_ptr<Command> Rebuilt;
    switch (Node.Kind) {
    case NEVERC_JOB_COMMAND:
      Rebuilt = std::make_unique<Command>(
          *ActionRecord->Value, Creator, *Response, Executable,
          Arguments, Inputs, Outputs, Prepend);
      Rebuilt->InProcess = Node.InProcess;
      break;
    case NEVERC_JOB_FRONTEND: {
      auto Frontend = std::make_unique<FrontendCommand>(
          *ActionRecord->Value, Creator, *Response, Executable,
          Arguments, Inputs, Outputs, Prepend);
      if (Node.Original &&
          Node.Original->getKind() == Command::CK_FrontendCommand)
        Frontend->getDirectOpts() =
            static_cast<const FrontendCommand *>(Node.Original)
                ->getDirectOpts();
      if (Error E =
              tools::rebuildDirectInvocationOptsForFrontendJob(*Frontend))
        return std::move(E);
      Rebuilt = std::move(Frontend);
      break;
    }
    case NEVERC_JOB_LINKER: {
      auto Flavor = toInternalLinkerFlavor(Node.LinkerFlavor);
      if (!Flavor)
        return Flavor.takeError();
      auto Linker = std::make_unique<LinkerCommand>(
          *ActionRecord->Value, Creator, *Response, Executable,
          Arguments, Inputs, *Flavor, Outputs, Prepend);
      if (Node.Original &&
          Node.Original->getKind() == Command::CK_LinkerCommand)
        Linker->getDriverConfig() =
            static_cast<const LinkerCommand *>(Node.Original)
                ->getDriverConfig();
      Rebuilt = std::move(Linker);
      break;
    }
    case NEVERC_JOB_ARCHIVE:
      Rebuilt = std::make_unique<ArchiveCommand>(
          *ActionRecord->Value, Creator, *Response, Executable,
          Arguments, Inputs, Outputs);
      break;
    case NEVERC_JOB_DYNCODE:
      // The in-process dyncode command takes no response-file support or
      // prepended arguments; it lowers one object into a raw image in-process.
      Rebuilt = std::make_unique<DynCodeCommand>(
          *ActionRecord->Value, Creator, Executable, Arguments, Inputs,
          Outputs);
      break;
    case NEVERC_JOB_PLUGIN: {
      std::string CallbackKey =
          (Twine(Node.PluginID) + ":" + Node.CallbackID).str();
      NevercPluginJobCallbackFn Callback = Node.Callback;
      void *UserData = Node.CallbackUserData;
      if (Error E = Session->registerDeferredCallback(
              "neverc.driver.job", CallbackKey, Node.PluginID,
              [Callback, UserData](const void *Context,
                                   int32_t *OutExitCode) {
                return Callback(
                    static_cast<const NevercPluginJobContext *>(Context),
                    OutExitCode, UserData);
              }))
        return std::move(E);
      RegisteredCallbacks.push_back(CallbackKey);
      Rebuilt = std::make_unique<PluginCommand>(
          *ActionRecord->Value, Creator, Node.ID, CallbackKey,
          Session, Arguments, Inputs, Outputs);
      break;
    }
    default:
      return jobGraphError("job kind is invalid");
    }
    Rebuilt->PrintInputFilenames =
        Node.Original && Node.Original->PrintInputFilenames;
    if (!Node.Environment.empty()) {
      llvm::SmallVector<const char *, 8> Environment;
      for (const std::string &Entry : Node.Environment)
        Environment.push_back(C.getArgs().MakeArgString(Entry));
      Rebuilt->setEnvironment(Environment);
    }
    Pending.push_back({std::move(Rebuilt), nullptr});
  }

  DenseSet<Command *> Existing;
  for (Command &Job : C.getJobs())
    Existing.insert(&Job);
  for (const PendingCommand &CommandValue : Pending)
    if (CommandValue.Original &&
        !Existing.contains(CommandValue.Original))
      return jobGraphError(
          "job graph references an unavailable original command");

  JobList::list_type OldJobs = C.getJobs().takeJobs();
  DenseMap<Command *, std::unique_ptr<Command>> OriginalJobs;
  for (std::unique_ptr<Command> &Job : OldJobs)
    OriginalJobs[Job.get()] = std::move(Job);

  JobList::list_type NewJobs;
  NewJobs.reserve(Pending.size());
  for (PendingCommand &CommandValue : Pending) {
    if (CommandValue.Original) {
      auto It = OriginalJobs.find(CommandValue.Original);
      NewJobs.push_back(std::move(It->second));
      OriginalJobs.erase(It);
    } else {
      NewJobs.push_back(std::move(CommandValue.Rebuilt));
    }
  }
  C.getJobs().replaceJobs(std::move(NewJobs));
  C.propagatePluginSessionToJobs();

  std::vector<DriverJobExecutionPlanNode> PlanNodes;
  PlanNodes.reserve(Graph.Nodes.size());
  size_t Index = 0;
  for (Command &Job : C.getJobs()) {
    DriverJobGraphNode Request = Graph.Nodes[Index];
    Request.Original = nullptr;
    PlanNodes.push_back({Graph.Nodes[Index].ID, &Job,
                         Graph.Nodes[Index].Dependencies,
                         std::move(Request)});
    ++Index;
  }
  RollbackCallbacks.release();
  return std::make_unique<DriverJobExecutionPlan>(
      std::move(PlanNodes));
}

} // namespace neverc::driver
