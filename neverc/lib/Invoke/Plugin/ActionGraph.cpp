#include "Plugin/ActionGraph.h"
#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/Compilation.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <limits>
#include <new>

using namespace llvm;
using namespace llvm::opt;

namespace neverc::driver {
namespace {

constexpr size_t MaximumActionGraphNodes = UINT64_C(1) << 20;
constexpr size_t MaximumActionGraphEdges = UINT64_C(1) << 22;
constexpr size_t MaximumActionStringBytes = UINT64_C(1) << 20;

Error actionGraphError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

const DriverActionGraphNode *
findNode(ArrayRef<DriverActionGraphNode> Nodes, NevercActionNodeID ID) {
  auto It = llvm::find_if(
      Nodes, [&](const DriverActionGraphNode &Node) { return Node.ID == ID; });
  return It == Nodes.end() ? nullptr : &*It;
}

const DriverActionInputRecord *
findInput(ArrayRef<DriverActionInputRecord> Inputs, NevercDriverInputID ID) {
  auto It = llvm::find_if(Inputs, [&](const DriverActionInputRecord &Input) {
    return Input.ID == ID;
  });
  return It == Inputs.end() ? nullptr : &*It;
}

bool isValidText(StringRef Text, bool AllowEmpty) {
  return (AllowEmpty || !Text.empty()) &&
         Text.size() <= MaximumActionStringBytes && !Text.contains('\0') &&
         json::isUTF8(Text);
}

Expected<NevercActionKind> toPublicActionKind(Action::ActionClass Kind) {
  switch (Kind) {
  case Action::InputClass:
    return NEVERC_ACTION_INPUT;
  case Action::BindArchClass:
    return NEVERC_ACTION_BIND_ARCH;
  case Action::PreprocessJobClass:
    return NEVERC_ACTION_PREPROCESS;
  case Action::CompileJobClass:
    return NEVERC_ACTION_COMPILE;
  case Action::BackendJobClass:
    return NEVERC_ACTION_BACKEND;
  case Action::AssembleJobClass:
    return NEVERC_ACTION_ASSEMBLE;
  case Action::LinkJobClass:
    return NEVERC_ACTION_LINK;
  case Action::LipoJobClass:
    return NEVERC_ACTION_LIPO;
  case Action::DsymutilJobClass:
    return NEVERC_ACTION_DSYMUTIL;
  case Action::StaticLibJobClass:
    return NEVERC_ACTION_STATIC_LIB;
  case Action::DynCodeJobClass:
    return NEVERC_ACTION_DYNCODE;
  }
  return actionGraphError("unknown internal action kind");
}

bool validActionKind(NevercActionKind Kind) {
  return Kind >= NEVERC_ACTION_INPUT && Kind <= NEVERC_ACTION_DYNCODE;
}

bool typeIs(NevercDriverType Type,
            std::initializer_list<NevercDriverType> Allowed) {
  return llvm::is_contained(Allowed, Type);
}

Error verifyActionGraph(
    const DriverActionGraphData &Graph,
    ArrayRef<DriverActionInputRecord> AllowedInputs) {
  if (Graph.Nodes.empty())
    return actionGraphError("action graph has no nodes");
  if (Graph.Nodes.size() > MaximumActionGraphNodes)
    return actionGraphError("action graph exceeds the node limit");
  if (Graph.Roots.empty())
    return actionGraphError("action graph has no roots");

  DenseSet<NevercDriverInputID> InputIDs;
  for (const DriverActionInputRecord &Input : AllowedInputs) {
    if (Input.ID == 0 || !Input.Argument ||
        Input.PublicType == NEVERC_DRIVER_TYPE_INVALID ||
        !InputIDs.insert(Input.ID).second)
      return actionGraphError("action graph input catalog is invalid");
    auto InternalType = toInternalDriverType(Input.PublicType);
    if (!InternalType)
      return InternalType.takeError();
    if (*InternalType != Input.InternalType)
      return actionGraphError(
          "action graph input type mapping is inconsistent");
  }

  DenseSet<NevercActionNodeID> NodeIDs;
  size_t EdgeCount = 0;
  for (const DriverActionGraphNode &Node : Graph.Nodes) {
    if (Node.ID == 0 || !NodeIDs.insert(Node.ID).second)
      return actionGraphError("action graph node ID is invalid or duplicated");
    if (!validActionKind(Node.Kind))
      return actionGraphError("action graph node kind is invalid");
    auto InternalType = toInternalDriverType(Node.OutputType);
    if (!InternalType)
      return InternalType.takeError();
    if (EdgeCount > MaximumActionGraphEdges - Node.Inputs.size())
      return actionGraphError("action graph exceeds the edge limit");
    EdgeCount += Node.Inputs.size();
    for (NevercActionNodeID Input : Node.Inputs)
      if (Input == 0)
        return actionGraphError("action graph contains a null edge");
  }

  DenseSet<NevercActionNodeID> RootIDs;
  for (NevercActionNodeID Root : Graph.Roots) {
    if (!NodeIDs.contains(Root))
      return actionGraphError("action graph root references an unknown node");
    if (!RootIDs.insert(Root).second)
      return actionGraphError("action graph root is duplicated");
  }
  for (const DriverActionGraphNode &Node : Graph.Nodes)
    for (NevercActionNodeID Input : Node.Inputs)
      if (!NodeIDs.contains(Input))
        return actionGraphError(
            "action graph edge references an unknown node");

  DenseMap<NevercActionNodeID, uint8_t> VisitState;
  DenseSet<NevercActionNodeID> Reachable;
  std::function<Error(NevercActionNodeID, bool)> Visit =
      [&](NevercActionNodeID ID, bool MarkReachable) -> Error {
    uint8_t &State = VisitState[ID];
    if (State == 1)
      return actionGraphError("action graph contains a cycle");
    if (State == 2) {
      if (MarkReachable)
        Reachable.insert(ID);
      return Error::success();
    }
    State = 1;
    if (MarkReachable)
      Reachable.insert(ID);
    const DriverActionGraphNode *Node = findNode(Graph.Nodes, ID);
    for (NevercActionNodeID Input : Node->Inputs) {
      if (MarkReachable)
        Reachable.insert(Input);
      if (Error E = Visit(Input, MarkReachable))
        return E;
    }
    State = 2;
    return Error::success();
  };
  for (const DriverActionGraphNode &Node : Graph.Nodes)
    if (VisitState.lookup(Node.ID) == 0)
      if (Error E = Visit(Node.ID, false))
        return E;
  VisitState.clear();
  for (NevercActionNodeID Root : Graph.Roots)
    if (Error E = Visit(Root, true))
      return E;
  if (Reachable.size() != Graph.Nodes.size())
    return actionGraphError("action graph contains unreachable nodes");

  for (const DriverActionGraphNode &Node : Graph.Nodes) {
    auto requireInputs = [&](size_t Count) -> Error {
      if (Node.Inputs.size() != Count)
        return actionGraphError(
            "action graph node has an invalid input count");
      return Error::success();
    };
    auto requireAtLeast = [&](size_t Count) -> Error {
      if (Node.Inputs.size() < Count)
        return actionGraphError(
            "action graph node has an invalid input count");
      return Error::success();
    };
    auto inputType = [&](size_t Index) {
      return findNode(Graph.Nodes, Node.Inputs[Index])->OutputType;
    };

    switch (Node.Kind) {
    case NEVERC_ACTION_INPUT: {
      if (Error E = requireInputs(0))
        return E;
      const DriverActionInputRecord *Input =
          findInput(AllowedInputs, Node.DriverInput);
      if (!Input || Input->PublicType != Node.OutputType)
        return actionGraphError(
            "action graph input does not reference a declared driver input");
      if (!Node.BindArch.empty())
        return actionGraphError("input action has unexpected bind-arch data");
      break;
    }
    case NEVERC_ACTION_BIND_ARCH:
      if (Error E = requireInputs(1))
        return E;
      if (Node.DriverInput != 0 || !isValidText(Node.BindArch, false))
        return actionGraphError("bind-arch action payload is invalid");
      if (inputType(0) != Node.OutputType)
        return actionGraphError("incompatible action graph edge");
      break;
    case NEVERC_ACTION_PREPROCESS:
      if (Error E = requireInputs(1))
        return E;
      if (!typeIs(inputType(0), {NEVERC_DRIVER_TYPE_C,
                                 NEVERC_DRIVER_TYPE_C_HEADER,
                                 NEVERC_DRIVER_TYPE_ASM}) ||
          !typeIs(Node.OutputType,
                  {NEVERC_DRIVER_TYPE_PP_C, NEVERC_DRIVER_TYPE_PP_ASM,
                   NEVERC_DRIVER_TYPE_DEPENDENCIES, NEVERC_DRIVER_TYPE_C,
                   NEVERC_DRIVER_TYPE_ASM}))
        return actionGraphError("incompatible action graph edge");
      break;
    case NEVERC_ACTION_COMPILE:
      if (Error E = requireInputs(1))
        return E;
      if (!typeIs(inputType(0), {NEVERC_DRIVER_TYPE_PP_C,
                                 NEVERC_DRIVER_TYPE_LLVM_IR,
                                 NEVERC_DRIVER_TYPE_LLVM_BC}) ||
          !typeIs(Node.OutputType, {NEVERC_DRIVER_TYPE_LLVM_BC,
                                    NEVERC_DRIVER_TYPE_NOTHING}))
        return actionGraphError("incompatible action graph edge");
      break;
    case NEVERC_ACTION_BACKEND:
      if (Error E = requireInputs(1))
        return E;
      if (inputType(0) != NEVERC_DRIVER_TYPE_LLVM_BC ||
          !typeIs(Node.OutputType,
                  {NEVERC_DRIVER_TYPE_PP_ASM, NEVERC_DRIVER_TYPE_LLVM_IR,
                   NEVERC_DRIVER_TYPE_LLVM_BC, NEVERC_DRIVER_TYPE_LTO_IR,
                   NEVERC_DRIVER_TYPE_LTO_BC}))
        return actionGraphError("incompatible action graph edge");
      break;
    case NEVERC_ACTION_ASSEMBLE:
      if (Error E = requireInputs(1))
        return E;
      if (inputType(0) != NEVERC_DRIVER_TYPE_PP_ASM ||
          Node.OutputType != NEVERC_DRIVER_TYPE_OBJECT)
        return actionGraphError("incompatible action graph edge");
      break;
    case NEVERC_ACTION_LINK:
    case NEVERC_ACTION_STATIC_LIB:
      if (Error E = requireAtLeast(1))
        return E;
      for (size_t I = 0; I != Node.Inputs.size(); ++I)
        if (!typeIs(inputType(I), {NEVERC_DRIVER_TYPE_OBJECT,
                                   NEVERC_DRIVER_TYPE_LTO_BC}))
          return actionGraphError("incompatible action graph edge");
      if (Node.OutputType != NEVERC_DRIVER_TYPE_IMAGE)
        return actionGraphError("incompatible action graph edge");
      break;
    case NEVERC_ACTION_LIPO:
      if (Error E = requireAtLeast(2))
        return E;
      for (size_t I = 0; I != Node.Inputs.size(); ++I)
        if (inputType(I) != Node.OutputType)
          return actionGraphError("incompatible action graph edge");
      {
        auto InternalType = toInternalDriverType(Node.OutputType);
        if (!InternalType)
          return InternalType.takeError();
        if (!types::canLipoType(*InternalType))
          return actionGraphError("incompatible action graph edge");
      }
      break;
    case NEVERC_ACTION_DSYMUTIL:
      if (Error E = requireInputs(1))
        return E;
      if (inputType(0) != NEVERC_DRIVER_TYPE_IMAGE ||
          Node.OutputType != NEVERC_DRIVER_TYPE_DSYM)
        return actionGraphError("incompatible action graph edge");
      break;
    case NEVERC_ACTION_DYNCODE:
      // -fdyncode lowers exactly one relocatable object into a raw image.
      if (Error E = requireInputs(1))
        return E;
      if (!typeIs(inputType(0), {NEVERC_DRIVER_TYPE_OBJECT,
                                 NEVERC_DRIVER_TYPE_LTO_BC}) ||
          Node.OutputType != NEVERC_DRIVER_TYPE_IMAGE)
        return actionGraphError("incompatible action graph edge");
      break;
    default:
      return actionGraphError("action graph node kind is invalid");
    }
    if (Node.Kind != NEVERC_ACTION_INPUT && Node.DriverInput != 0)
      return actionGraphError(
          "non-input action references a driver input directly");
    if (Node.Kind != NEVERC_ACTION_BIND_ARCH && !Node.BindArch.empty())
      return actionGraphError(
          "non-bind action has unexpected bind-arch data");
  }
  return Error::success();
}

} // namespace

Expected<NevercDriverType> toPublicDriverType(types::ID Type) {
  switch (Type) {
  case types::TY_PP_C:
    return NEVERC_DRIVER_TYPE_PP_C;
  case types::TY_C:
    return NEVERC_DRIVER_TYPE_C;
  case types::TY_CHeader:
    return NEVERC_DRIVER_TYPE_C_HEADER;
  case types::TY_PP_Asm:
    return NEVERC_DRIVER_TYPE_PP_ASM;
  case types::TY_Asm:
    return NEVERC_DRIVER_TYPE_ASM;
  case types::TY_LLVM_IR:
    return NEVERC_DRIVER_TYPE_LLVM_IR;
  case types::TY_LLVM_BC:
    return NEVERC_DRIVER_TYPE_LLVM_BC;
  case types::TY_LTO_IR:
    return NEVERC_DRIVER_TYPE_LTO_IR;
  case types::TY_LTO_BC:
    return NEVERC_DRIVER_TYPE_LTO_BC;
  case types::TY_Object:
    return NEVERC_DRIVER_TYPE_OBJECT;
  case types::TY_Image:
    return NEVERC_DRIVER_TYPE_IMAGE;
  case types::TY_dSYM:
    return NEVERC_DRIVER_TYPE_DSYM;
  case types::TY_Dependencies:
    return NEVERC_DRIVER_TYPE_DEPENDENCIES;
  case types::TY_Nothing:
    return NEVERC_DRIVER_TYPE_NOTHING;
  case types::TY_INVALID:
  case types::TY_LAST:
    break;
  }
  return actionGraphError("internal driver type has no stable ABI mapping");
}

Expected<types::ID> toInternalDriverType(NevercDriverType Type) {
  switch (Type) {
  case NEVERC_DRIVER_TYPE_PP_C:
    return types::TY_PP_C;
  case NEVERC_DRIVER_TYPE_C:
    return types::TY_C;
  case NEVERC_DRIVER_TYPE_C_HEADER:
    return types::TY_CHeader;
  case NEVERC_DRIVER_TYPE_PP_ASM:
    return types::TY_PP_Asm;
  case NEVERC_DRIVER_TYPE_ASM:
    return types::TY_Asm;
  case NEVERC_DRIVER_TYPE_LLVM_IR:
    return types::TY_LLVM_IR;
  case NEVERC_DRIVER_TYPE_LLVM_BC:
    return types::TY_LLVM_BC;
  case NEVERC_DRIVER_TYPE_LTO_IR:
    return types::TY_LTO_IR;
  case NEVERC_DRIVER_TYPE_LTO_BC:
    return types::TY_LTO_BC;
  case NEVERC_DRIVER_TYPE_OBJECT:
    return types::TY_Object;
  case NEVERC_DRIVER_TYPE_IMAGE:
    return types::TY_Image;
  case NEVERC_DRIVER_TYPE_DSYM:
    return types::TY_dSYM;
  case NEVERC_DRIVER_TYPE_DEPENDENCIES:
    return types::TY_Dependencies;
  case NEVERC_DRIVER_TYPE_NOTHING:
    return types::TY_Nothing;
  default:
    return actionGraphError("stable driver type is invalid");
  }
}

Expected<NevercDriverInputID>
DriverActionGraphRequestArtifact::addInput(types::ID Type,
                                           const Arg &Argument) {
  auto PublicType = toPublicDriverType(Type);
  if (!PublicType)
    return PublicType.takeError();
  std::lock_guard<std::mutex> Lock(Mutex);
  if (NextInputID == 0 ||
      NextInputID == std::numeric_limits<NevercDriverInputID>::max())
    return actionGraphError("driver input ID space is exhausted");
  DriverActionInputRecord Input;
  Input.ID = NextInputID++;
  Input.PublicType = *PublicType;
  Input.InternalType = Type;
  Input.Argument = &Argument;
  Input.Value = Argument.getValue();
  Inputs.push_back(std::move(Input));
  return Inputs.back().ID;
}

Expected<NevercDriverInputID>
DriverActionGraphRequestArtifact::ensureInput(types::ID Type,
                                              const Arg &Argument) {
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    for (const DriverActionInputRecord &Input : Inputs)
      if (Input.Argument == &Argument && Input.InternalType == Type)
        return Input.ID;
  }
  return addInput(Type, Argument);
}

Error DriverActionGraphRequestArtifact::verify() const {
  std::vector<DriverActionInputRecord> Snapshot = snapshotInputs();
  DenseSet<NevercDriverInputID> IDs;
  for (const DriverActionInputRecord &Input : Snapshot) {
    if (Input.ID == 0 || !Input.Argument || !IDs.insert(Input.ID).second)
      return actionGraphError("driver action request input is invalid");
    auto PublicType = toPublicDriverType(Input.InternalType);
    if (!PublicType)
      return PublicType.takeError();
    if (*PublicType != Input.PublicType ||
        !isValidText(Input.Value, false))
      return actionGraphError("driver action request input is invalid");
  }
  return Error::success();
}

uint64_t DriverActionGraphRequestArtifact::inputCount() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Inputs.size();
}

bool DriverActionGraphRequestArtifact::describeInput(
    uint64_t Index, NevercDriverInput &OutInput) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (Index >= Inputs.size())
    return false;
  const DriverActionInputRecord &Input = Inputs[static_cast<size_t>(Index)];
  OutInput.Input = Input.ID;
  OutInput.Type = Input.PublicType;
  OutInput.Value = {Input.Value.data(), Input.Value.size()};
  return true;
}

std::vector<DriverActionInputRecord>
DriverActionGraphRequestArtifact::snapshotInputs() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Inputs;
}

bool DriverActionGraphRequestArtifact::findInput(
    NevercDriverInputID ID, DriverActionInputRecord &OutInput) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  const DriverActionInputRecord *Input =
      ::neverc::driver::findInput(Inputs, ID);
  if (!Input)
    return false;
  OutInput = *Input;
  return true;
}

DriverActionGraphArtifact::DriverActionGraphArtifact(
    DriverActionGraphData GraphValue,
    std::vector<DriverActionInputRecord> AllowedInputsValue)
    : Graph(std::move(GraphValue)),
      AllowedInputs(std::move(AllowedInputsValue)) {}

DriverActionGraphArtifact::DriverActionGraphArtifact(
    const DriverActionGraphArtifact &Other) {
  std::lock_guard<std::mutex> Lock(Other.Mutex);
  Graph = Other.Graph;
  AllowedInputs = Other.AllowedInputs;
  MaterializedActions = Other.MaterializedActions;
}

Error DriverActionGraphArtifact::verify() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return verifyActionGraph(Graph, AllowedInputs);
}

uint64_t DriverActionGraphArtifact::nodeCount() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Graph.Nodes.size();
}

uint64_t DriverActionGraphArtifact::rootCount() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Graph.Roots.size();
}

bool DriverActionGraphArtifact::describeNode(
    uint64_t Index, NevercActionNode &OutNode) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (Index >= Graph.Nodes.size())
    return false;
  const DriverActionGraphNode &Node = Graph.Nodes[static_cast<size_t>(Index)];
  OutNode.Node = Node.ID;
  OutNode.Kind = Node.Kind;
  OutNode.OutputType = Node.OutputType;
  OutNode.InputCount = Node.Inputs.size();
  OutNode.DriverInput = Node.DriverInput;
  OutNode.BindArch = {Node.BindArch.data(), Node.BindArch.size()};
  return true;
}

bool DriverActionGraphArtifact::getNodeInput(
    NevercActionNodeID NodeID, uint64_t Index,
    NevercActionNodeID &OutInput) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  const DriverActionGraphNode *Node =
      ::neverc::driver::findNode(Graph.Nodes, NodeID);
  if (!Node || Index >= Node->Inputs.size())
    return false;
  OutInput = Node->Inputs[static_cast<size_t>(Index)];
  return true;
}

bool DriverActionGraphArtifact::getRoot(
    uint64_t Index, NevercActionNodeID &OutRoot) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (Index >= Graph.Roots.size())
    return false;
  OutRoot = Graph.Roots[static_cast<size_t>(Index)];
  return true;
}

DriverActionGraphData DriverActionGraphArtifact::snapshot() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Graph;
}

std::vector<DriverActionInputRecord>
DriverActionGraphArtifact::inputCatalog() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return AllowedInputs;
}

void DriverActionGraphArtifact::setMaterializedActions(
    std::vector<DriverMaterializedAction> Actions) {
  std::lock_guard<std::mutex> Lock(Mutex);
  MaterializedActions = std::move(Actions);
}

std::vector<DriverMaterializedAction>
DriverActionGraphArtifact::materializedActions() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return MaterializedActions;
}

Action *DriverActionGraphArtifact::findMaterializedAction(
    NevercActionNodeID ID) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto It = llvm::find_if(
      MaterializedActions,
      [&](const DriverMaterializedAction &ActionValue) {
        return ActionValue.ID == ID;
      });
  return It == MaterializedActions.end() ? nullptr : It->Value;
}

Expected<std::unique_ptr<DriverActionGraphEdit>>
DriverActionGraphArtifact::beginMutation() {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (MutationActive)
    return actionGraphError("action graph already has an active mutation");
  auto *Edit = new (std::nothrow)
      DriverActionGraphEdit(this, Graph, AllowedInputs);
  if (!Edit)
    return actionGraphError("unable to allocate action graph mutation");
  MutationActive = true;
  return std::unique_ptr<DriverActionGraphEdit>(Edit);
}

Error DriverActionGraphArtifact::commitMutation(
    DriverActionGraphData NewGraph) {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (!MutationActive)
    return actionGraphError("action graph mutation is not active");
  Graph = std::move(NewGraph);
  MutationActive = false;
  return Error::success();
}

void DriverActionGraphArtifact::abortMutation() {
  std::lock_guard<std::mutex> Lock(Mutex);
  MutationActive = false;
}

DriverActionGraphEdit::DriverActionGraphEdit(
    DriverActionGraphArtifact *OwnerValue, DriverActionGraphData GraphValue,
    std::vector<DriverActionInputRecord> AllowedInputsValue)
    : Owner(OwnerValue), Graph(std::move(GraphValue)),
      AllowedInputs(std::move(AllowedInputsValue)) {
  for (const DriverActionGraphNode &Node : Graph.Nodes) {
    if (Node.ID == std::numeric_limits<NevercActionNodeID>::max()) {
      NextNodeID = 0;
      break;
    }
    NextNodeID = std::max(NextNodeID, Node.ID + 1);
  }
}

Expected<std::unique_ptr<DriverActionGraphEdit>>
DriverActionGraphEdit::createBuilder(
    const DriverActionGraphRequestArtifact &Request) {
  auto *Edit = new (std::nothrow)
      DriverActionGraphEdit(nullptr, {}, Request.snapshotInputs());
  if (!Edit)
    return actionGraphError("unable to allocate action graph builder");
  return std::unique_ptr<DriverActionGraphEdit>(Edit);
}

DriverActionGraphEdit::~DriverActionGraphEdit() { abort(); }

DriverActionGraphNode *
DriverActionGraphEdit::findNode(NevercActionNodeID NodeID) {
  auto It = llvm::find_if(Graph.Nodes, [&](const DriverActionGraphNode &Node) {
    return Node.ID == NodeID;
  });
  return It == Graph.Nodes.end() ? nullptr : &*It;
}

Expected<NevercActionNodeID> DriverActionGraphEdit::addNode(
    NevercActionKind Kind, NevercDriverType OutputType,
    NevercDriverInputID DriverInput, StringRef BindArch,
    ArrayRef<NevercActionNodeID> Inputs) {
  if (Finished)
    return actionGraphError("action graph edit is finished");
  if (!validActionKind(Kind))
    return actionGraphError("action graph node kind is invalid");
  auto InternalType = toInternalDriverType(OutputType);
  if (!InternalType)
    return InternalType.takeError();
  if (!isValidText(BindArch, true))
    return actionGraphError("action graph bind-arch value is invalid");
  if (Graph.Nodes.size() >= MaximumActionGraphNodes || NextNodeID == 0 ||
      NextNodeID == std::numeric_limits<NevercActionNodeID>::max())
    return actionGraphError("action graph node ID space is exhausted");
  DriverActionGraphNode Node;
  Node.ID = NextNodeID++;
  Node.Kind = Kind;
  Node.OutputType = OutputType;
  Node.Inputs.assign(Inputs.begin(), Inputs.end());
  Node.DriverInput = DriverInput;
  Node.BindArch = BindArch.str();
  Graph.Nodes.push_back(std::move(Node));
  return Graph.Nodes.back().ID;
}

Error DriverActionGraphEdit::removeNode(NevercActionNodeID NodeID) {
  if (Finished)
    return actionGraphError("action graph edit is finished");
  auto It = llvm::find_if(Graph.Nodes, [&](const DriverActionGraphNode &Node) {
    return Node.ID == NodeID;
  });
  if (It == Graph.Nodes.end())
    return actionGraphError("action graph node is unknown");
  Graph.Nodes.erase(It);
  return Error::success();
}

Error DriverActionGraphEdit::replaceInputs(
    NevercActionNodeID NodeID, ArrayRef<NevercActionNodeID> Inputs) {
  if (Finished)
    return actionGraphError("action graph edit is finished");
  DriverActionGraphNode *Node = findNode(NodeID);
  if (!Node)
    return actionGraphError("action graph node is unknown");
  Node->Inputs.assign(Inputs.begin(), Inputs.end());
  return Error::success();
}

Error DriverActionGraphEdit::setOutputType(
    NevercActionNodeID NodeID, NevercDriverType OutputType) {
  if (Finished)
    return actionGraphError("action graph edit is finished");
  auto InternalType = toInternalDriverType(OutputType);
  if (!InternalType)
    return InternalType.takeError();
  DriverActionGraphNode *Node = findNode(NodeID);
  if (!Node)
    return actionGraphError("action graph node is unknown");
  Node->OutputType = OutputType;
  return Error::success();
}

Error DriverActionGraphEdit::setBindArch(
    NevercActionNodeID NodeID, StringRef BindArch) {
  if (Finished)
    return actionGraphError("action graph edit is finished");
  if (!isValidText(BindArch, true))
    return actionGraphError("action graph bind-arch value is invalid");
  DriverActionGraphNode *Node = findNode(NodeID);
  if (!Node)
    return actionGraphError("action graph node is unknown");
  Node->BindArch = BindArch.str();
  return Error::success();
}

Error DriverActionGraphEdit::setRoots(
    ArrayRef<NevercActionNodeID> Roots) {
  if (Finished)
    return actionGraphError("action graph edit is finished");
  Graph.Roots.assign(Roots.begin(), Roots.end());
  return Error::success();
}

Expected<std::unique_ptr<DriverActionGraphArtifact>>
DriverActionGraphEdit::finishBuilder() {
  if (Finished || Owner)
    return actionGraphError("action graph builder is not publishable");
  auto *Artifact = new (std::nothrow)
      DriverActionGraphArtifact(std::move(Graph), std::move(AllowedInputs));
  if (!Artifact)
    return actionGraphError("unable to allocate action graph artifact");
  Finished = true;
  return std::unique_ptr<DriverActionGraphArtifact>(Artifact);
}

Error DriverActionGraphEdit::commitMutation() {
  if (Finished || !Owner)
    return actionGraphError("action graph mutation is not committable");
  if (Error E = verifyActionGraph(Graph, AllowedInputs))
    return E;
  if (Error E = Owner->commitMutation(std::move(Graph)))
    return E;
  Finished = true;
  Owner = nullptr;
  return Error::success();
}

void DriverActionGraphEdit::abort() {
  if (Finished)
    return;
  if (Owner)
    Owner->abortMutation();
  Owner = nullptr;
  Finished = true;
}

NevercInterfaceID driverActionGraphRequestArtifactID() {
  return {NEVERC_PHASE_DRIVER_BUILD_ACTIONS_INPUT_HIGH,
          NEVERC_PHASE_DRIVER_BUILD_ACTIONS_INPUT_LOW};
}

NevercInterfaceID driverActionGraphArtifactID() {
  return {NEVERC_PHASE_DRIVER_BUILD_ACTIONS_OUTPUT_HIGH,
          NEVERC_PHASE_DRIVER_BUILD_ACTIONS_OUTPUT_LOW};
}

NevercInterfaceID driverBuildActionsPhaseID() {
  return {NEVERC_PHASE_DRIVER_BUILD_ACTIONS_HIGH,
          NEVERC_PHASE_DRIVER_BUILD_ACTIONS_LOW};
}

Expected<DriverActionGraphArtifactTypes>
registerDriverActionGraphArtifacts(
    plugin::PluginArtifactRegistry &Registry) {
  plugin::PluginArtifactTypeDescriptor RequestDescriptor;
  RequestDescriptor.ID = driverActionGraphRequestArtifactID();
  RequestDescriptor.Name = "neverc.driver.action_request";
  RequestDescriptor.Ownership = plugin::PluginArtifactOwnership::Borrowed;
  RequestDescriptor.Verify = [](const void *Payload) {
    if (!Payload)
      return actionGraphError("action graph request payload is null");
    return static_cast<const DriverActionGraphRequestArtifact *>(Payload)
        ->verify();
  };
  auto RequestType = Registry.registerType(std::move(RequestDescriptor));
  if (!RequestType)
    return RequestType.takeError();

  plugin::PluginArtifactTypeDescriptor GraphDescriptor;
  GraphDescriptor.ID = driverActionGraphArtifactID();
  GraphDescriptor.Name = "neverc.driver.action_graph";
  GraphDescriptor.Ownership = plugin::PluginArtifactOwnership::Owned;
  GraphDescriptor.Clone = [](const void *Payload) -> Expected<void *> {
    if (!Payload)
      return actionGraphError("cannot clone a null action graph");
    auto *Copy = new (std::nothrow) DriverActionGraphArtifact(
        *static_cast<const DriverActionGraphArtifact *>(Payload));
    if (!Copy)
      return actionGraphError("unable to allocate action graph clone");
    return static_cast<void *>(Copy);
  };
  GraphDescriptor.Destroy = [](void *Payload) {
    delete static_cast<DriverActionGraphArtifact *>(Payload);
  };
  GraphDescriptor.Verify = [](const void *Payload) {
    if (!Payload)
      return actionGraphError("action graph payload is null");
    return static_cast<const DriverActionGraphArtifact *>(Payload)->verify();
  };
  auto GraphType = Registry.registerType(std::move(GraphDescriptor));
  if (!GraphType)
    return GraphType.takeError();
  return DriverActionGraphArtifactTypes{*RequestType, *GraphType};
}

Expected<std::unique_ptr<DriverActionGraphArtifact>>
snapshotDriverActionGraph(DriverActionGraphRequestArtifact &Request,
                          ArrayRef<Action *> Roots) {
  DriverActionGraphData Graph;
  DenseMap<const Action *, NevercActionNodeID> NodeIDs;
  DenseSet<const Action *> Visiting;
  std::function<Expected<NevercActionNodeID>(const Action *)> Snapshot =
      [&](const Action *ActionValue) -> Expected<NevercActionNodeID> {
    if (!ActionValue)
      return actionGraphError("builtin action graph contains a null node");
    auto Existing = NodeIDs.find(ActionValue);
    if (Existing != NodeIDs.end())
      return Existing->second;
    if (!Visiting.insert(ActionValue).second)
      return actionGraphError("builtin action graph contains a cycle");
    auto RemoveVisit =
        make_scope_exit([&] { Visiting.erase(ActionValue); });

    DriverActionGraphNode Node;
    auto Kind = toPublicActionKind(ActionValue->getKind());
    if (!Kind)
      return Kind.takeError();
    auto OutputType = toPublicDriverType(ActionValue->getType());
    if (!OutputType)
      return OutputType.takeError();
    Node.Kind = *Kind;
    Node.OutputType = *OutputType;
    for (const Action *Input : ActionValue->inputs()) {
      auto InputID = Snapshot(Input);
      if (!InputID)
        return InputID.takeError();
      Node.Inputs.push_back(*InputID);
    }
    if (const auto *Input = dyn_cast<InputAction>(ActionValue)) {
      auto DriverInput =
          Request.ensureInput(ActionValue->getType(), Input->getInputArg());
      if (!DriverInput)
        return DriverInput.takeError();
      Node.DriverInput = *DriverInput;
    } else if (const auto *Bind = dyn_cast<BindArchAction>(ActionValue)) {
      Node.BindArch = Bind->getArchName().str();
    }
    if (Graph.Nodes.size() >= MaximumActionGraphNodes)
      return actionGraphError("builtin action graph exceeds the node limit");
    Node.ID = static_cast<NevercActionNodeID>(Graph.Nodes.size() + 1);
    Graph.Nodes.push_back(std::move(Node));
    NodeIDs[ActionValue] = Graph.Nodes.back().ID;
    return Graph.Nodes.back().ID;
  };

  for (const Action *Root : Roots) {
    auto RootID = Snapshot(Root);
    if (!RootID)
      return RootID.takeError();
    Graph.Roots.push_back(*RootID);
  }
  auto *Artifact = new (std::nothrow)
      DriverActionGraphArtifact(std::move(Graph), Request.snapshotInputs());
  if (!Artifact)
    return actionGraphError("unable to allocate builtin action graph");
  std::unique_ptr<DriverActionGraphArtifact> Result(Artifact);
  if (Error E = Result->verify())
    return std::move(E);
  return std::move(Result);
}

Error materializeDriverActionGraph(
    Compilation &C, const DriverActionGraphRequestArtifact &Request,
    DriverActionGraphArtifact &Artifact) {
  if (Error E = Artifact.verify())
    return E;
  DriverActionGraphData Graph = Artifact.snapshot();
  DenseMap<NevercActionNodeID, Action *> Materialized;
  DenseSet<NevercActionNodeID> Visiting;
  std::function<Expected<Action *>(NevercActionNodeID)> Materialize =
      [&](NevercActionNodeID ID) -> Expected<Action *> {
    auto Existing = Materialized.find(ID);
    if (Existing != Materialized.end())
      return Existing->second;
    if (!Visiting.insert(ID).second)
      return actionGraphError("action graph contains a cycle");
    auto RemoveVisit = make_scope_exit([&] { Visiting.erase(ID); });
    const DriverActionGraphNode *Node = findNode(Graph.Nodes, ID);
    if (!Node)
      return actionGraphError(
          "action graph materialization found an unknown node");

    ActionList Inputs;
    for (NevercActionNodeID InputID : Node->Inputs) {
      auto Input = Materialize(InputID);
      if (!Input)
        return Input.takeError();
      Inputs.push_back(*Input);
    }
    auto InternalType = toInternalDriverType(Node->OutputType);
    if (!InternalType)
      return InternalType.takeError();

    Action *Result = nullptr;
    switch (Node->Kind) {
    case NEVERC_ACTION_INPUT: {
      DriverActionInputRecord Input;
      if (!Request.findInput(Node->DriverInput, Input) || !Input.Argument ||
          Input.InternalType != *InternalType)
        return actionGraphError(
            "action graph input is absent from the driver request");
      Result = C.MakeAction<InputAction>(*Input.Argument, *InternalType);
      break;
    }
    case NEVERC_ACTION_BIND_ARCH:
      Result = C.MakeAction<BindArchAction>(
          Inputs.front(), C.getArgs().MakeArgString(Node->BindArch));
      break;
    case NEVERC_ACTION_PREPROCESS:
      Result =
          C.MakeAction<PreprocessJobAction>(Inputs.front(), *InternalType);
      break;
    case NEVERC_ACTION_COMPILE:
      Result = C.MakeAction<CompileJobAction>(Inputs.front(), *InternalType);
      break;
    case NEVERC_ACTION_BACKEND:
      Result = C.MakeAction<BackendJobAction>(Inputs.front(), *InternalType);
      break;
    case NEVERC_ACTION_ASSEMBLE:
      Result = C.MakeAction<AssembleJobAction>(Inputs.front(), *InternalType);
      break;
    case NEVERC_ACTION_LINK:
      Result = C.MakeAction<LinkJobAction>(Inputs, *InternalType);
      break;
    case NEVERC_ACTION_LIPO:
      Result = C.MakeAction<LipoJobAction>(Inputs, *InternalType);
      break;
    case NEVERC_ACTION_DSYMUTIL:
      Result = C.MakeAction<DsymutilJobAction>(Inputs, *InternalType);
      break;
    case NEVERC_ACTION_STATIC_LIB:
      Result = C.MakeAction<StaticLibJobAction>(Inputs, *InternalType);
      break;
    case NEVERC_ACTION_DYNCODE:
      Result = C.MakeAction<DynCodeJobAction>(Inputs.front(), *InternalType);
      break;
    default:
      return actionGraphError("action graph node kind is invalid");
    }
    Materialized[ID] = Result;
    return Result;
  };

  ActionList NewRoots;
  for (NevercActionNodeID Root : Graph.Roots) {
    auto MaterializedRoot = Materialize(Root);
    if (!MaterializedRoot)
      return MaterializedRoot.takeError();
    NewRoots.push_back(*MaterializedRoot);
  }
  C.getActions() = std::move(NewRoots);
  std::vector<DriverMaterializedAction> MaterializedActions;
  MaterializedActions.reserve(Materialized.size());
  for (const DriverActionGraphNode &Node : Graph.Nodes) {
    auto It = Materialized.find(Node.ID);
    if (It == Materialized.end())
      return actionGraphError(
          "action graph materialization omitted a node");
    MaterializedActions.push_back({Node.ID, It->second});
  }
  Artifact.setMaterializedActions(std::move(MaterializedActions));
  return Error::success();
}

} // namespace neverc::driver
