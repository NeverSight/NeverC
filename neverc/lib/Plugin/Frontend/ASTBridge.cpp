#include "FrontendPluginInterfaces.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus astStatus(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

NevercStringView stringView(StringRef Value) {
  return {Value.data(), static_cast<uint64_t>(Value.size())};
}

template <typename T> NevercStatus writeCallerRecord(T *Out, const T &Value) {
  if (!Out)
    return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = Out->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return astStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Bytes = std::min<size_t>(Capacity, sizeof(T));
  std::memcpy(Out, &Value, Bytes);
  Out->Header.StructSize = sizeof(T);
  if (Capacity < sizeof(T))
    return astStatus(NEVERC_STATUS_ABI_MISMATCH);
  return neverc_status_ok();
}

bool validASTInputHeader(const NevercABITableHeader &Header, size_t Size) {
  return Header.StructSize >= Size && Header.Major == NEVERC_AST_API_MAJOR &&
         Header.Minor <= NEVERC_AST_API_MINOR && Header.Flags == 0;
}

NevercASTNodeKind stableDeclKind(Decl::Kind Kind) {
  switch (Kind) {
#define NEVERC_AST_SCHEMA_INTERNAL_DECL(Internal, Symbol, ID)                  \
  case Decl::Internal:                                                         \
    return ID;
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_INTERNAL_DECL
  }
  return NEVERC_AST_NODE_KIND_INVALID;
}

NevercASTNodeKind stableStmtKind(Stmt::StmtClass Kind) {
  switch (Kind) {
#define NEVERC_AST_SCHEMA_INTERNAL_STMT(Internal, Symbol, ID)                  \
  case Stmt::Internal:                                                         \
    return ID;
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_INTERNAL_STMT
  case Stmt::NoStmtClass:
    break;
  }
  return NEVERC_AST_NODE_KIND_INVALID;
}

NevercASTNodeKind stableTypeKind(Type::TypeClass Kind) {
  switch (Kind) {
#define NEVERC_AST_SCHEMA_INTERNAL_TYPE(Internal, Symbol, ID)                  \
  case Type::Internal:                                                         \
    return ID;
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_INTERNAL_TYPE
  }
  return NEVERC_AST_NODE_KIND_INVALID;
}

NevercASTNodeKind stableAttrKind(attr::Kind Kind) {
  switch (Kind) {
#define NEVERC_AST_SCHEMA_INTERNAL_ATTR(Internal, Symbol, ID)                  \
  case attr::Internal:                                                         \
    return ID;
#include "neverc/Plugin/Schema/PluginASTSchema.inc"
#undef NEVERC_AST_SCHEMA_INTERNAL_ATTR
  }
  return NEVERC_AST_NODE_KIND_INVALID;
}

bool nativeBinaryOperatorKind(NevercBinaryOperatorKind Kind,
                              BinaryOperatorKind *OutKind) {
  if (!OutKind)
    return false;
  switch (Kind) {
  case NEVERC_BINARY_OPERATOR_MUL:
    *OutKind = BO_Mul;
    return true;
  case NEVERC_BINARY_OPERATOR_DIV:
    *OutKind = BO_Div;
    return true;
  case NEVERC_BINARY_OPERATOR_REM:
    *OutKind = BO_Rem;
    return true;
  case NEVERC_BINARY_OPERATOR_ADD:
    *OutKind = BO_Add;
    return true;
  case NEVERC_BINARY_OPERATOR_SUB:
    *OutKind = BO_Sub;
    return true;
  case NEVERC_BINARY_OPERATOR_SHL:
    *OutKind = BO_Shl;
    return true;
  case NEVERC_BINARY_OPERATOR_SHR:
    *OutKind = BO_Shr;
    return true;
  case NEVERC_BINARY_OPERATOR_LT:
    *OutKind = BO_LT;
    return true;
  case NEVERC_BINARY_OPERATOR_GT:
    *OutKind = BO_GT;
    return true;
  case NEVERC_BINARY_OPERATOR_LE:
    *OutKind = BO_LE;
    return true;
  case NEVERC_BINARY_OPERATOR_GE:
    *OutKind = BO_GE;
    return true;
  case NEVERC_BINARY_OPERATOR_EQ:
    *OutKind = BO_EQ;
    return true;
  case NEVERC_BINARY_OPERATOR_NE:
    *OutKind = BO_NE;
    return true;
  case NEVERC_BINARY_OPERATOR_AND:
    *OutKind = BO_And;
    return true;
  case NEVERC_BINARY_OPERATOR_XOR:
    *OutKind = BO_Xor;
    return true;
  case NEVERC_BINARY_OPERATOR_OR:
    *OutKind = BO_Or;
    return true;
  case NEVERC_BINARY_OPERATOR_LOGICAL_AND:
    *OutKind = BO_LAnd;
    return true;
  case NEVERC_BINARY_OPERATOR_LOGICAL_OR:
    *OutKind = BO_LOr;
    return true;
  case NEVERC_BINARY_OPERATOR_ASSIGN:
    *OutKind = BO_Assign;
    return true;
  case NEVERC_BINARY_OPERATOR_MUL_ASSIGN:
    *OutKind = BO_MulAssign;
    return true;
  case NEVERC_BINARY_OPERATOR_DIV_ASSIGN:
    *OutKind = BO_DivAssign;
    return true;
  case NEVERC_BINARY_OPERATOR_REM_ASSIGN:
    *OutKind = BO_RemAssign;
    return true;
  case NEVERC_BINARY_OPERATOR_ADD_ASSIGN:
    *OutKind = BO_AddAssign;
    return true;
  case NEVERC_BINARY_OPERATOR_SUB_ASSIGN:
    *OutKind = BO_SubAssign;
    return true;
  case NEVERC_BINARY_OPERATOR_SHL_ASSIGN:
    *OutKind = BO_ShlAssign;
    return true;
  case NEVERC_BINARY_OPERATOR_SHR_ASSIGN:
    *OutKind = BO_ShrAssign;
    return true;
  case NEVERC_BINARY_OPERATOR_AND_ASSIGN:
    *OutKind = BO_AndAssign;
    return true;
  case NEVERC_BINARY_OPERATOR_XOR_ASSIGN:
    *OutKind = BO_XorAssign;
    return true;
  case NEVERC_BINARY_OPERATOR_OR_ASSIGN:
    *OutKind = BO_OrAssign;
    return true;
  case NEVERC_BINARY_OPERATOR_COMMA:
    *OutKind = BO_Comma;
    return true;
  default:
    return false;
  }
}

NevercBinaryOperatorKind stableBinaryOperatorKind(BinaryOperatorKind Kind) {
  switch (Kind) {
  case BO_Mul:
    return NEVERC_BINARY_OPERATOR_MUL;
  case BO_Div:
    return NEVERC_BINARY_OPERATOR_DIV;
  case BO_Rem:
    return NEVERC_BINARY_OPERATOR_REM;
  case BO_Add:
    return NEVERC_BINARY_OPERATOR_ADD;
  case BO_Sub:
    return NEVERC_BINARY_OPERATOR_SUB;
  case BO_Shl:
    return NEVERC_BINARY_OPERATOR_SHL;
  case BO_Shr:
    return NEVERC_BINARY_OPERATOR_SHR;
  case BO_LT:
    return NEVERC_BINARY_OPERATOR_LT;
  case BO_GT:
    return NEVERC_BINARY_OPERATOR_GT;
  case BO_LE:
    return NEVERC_BINARY_OPERATOR_LE;
  case BO_GE:
    return NEVERC_BINARY_OPERATOR_GE;
  case BO_EQ:
    return NEVERC_BINARY_OPERATOR_EQ;
  case BO_NE:
    return NEVERC_BINARY_OPERATOR_NE;
  case BO_And:
    return NEVERC_BINARY_OPERATOR_AND;
  case BO_Xor:
    return NEVERC_BINARY_OPERATOR_XOR;
  case BO_Or:
    return NEVERC_BINARY_OPERATOR_OR;
  case BO_LAnd:
    return NEVERC_BINARY_OPERATOR_LOGICAL_AND;
  case BO_LOr:
    return NEVERC_BINARY_OPERATOR_LOGICAL_OR;
  case BO_Assign:
    return NEVERC_BINARY_OPERATOR_ASSIGN;
  case BO_MulAssign:
    return NEVERC_BINARY_OPERATOR_MUL_ASSIGN;
  case BO_DivAssign:
    return NEVERC_BINARY_OPERATOR_DIV_ASSIGN;
  case BO_RemAssign:
    return NEVERC_BINARY_OPERATOR_REM_ASSIGN;
  case BO_AddAssign:
    return NEVERC_BINARY_OPERATOR_ADD_ASSIGN;
  case BO_SubAssign:
    return NEVERC_BINARY_OPERATOR_SUB_ASSIGN;
  case BO_ShlAssign:
    return NEVERC_BINARY_OPERATOR_SHL_ASSIGN;
  case BO_ShrAssign:
    return NEVERC_BINARY_OPERATOR_SHR_ASSIGN;
  case BO_AndAssign:
    return NEVERC_BINARY_OPERATOR_AND_ASSIGN;
  case BO_XorAssign:
    return NEVERC_BINARY_OPERATOR_XOR_ASSIGN;
  case BO_OrAssign:
    return NEVERC_BINARY_OPERATOR_OR_ASSIGN;
  case BO_Comma:
    return NEVERC_BINARY_OPERATOR_COMMA;
  }
  return 0;
}

class PluginASTProcessBridge final : public PluginHostService {
public:
  PluginASTProcessBridge() {
    API.Header = {sizeof(API), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    API.Context = this;
    API.GetTranslationUnit = GetTranslationUnit;
    API.GetNodeInfo = GetNodeInfo;
    API.GetNodeInfoBatch = GetNodeInfoBatch;
    API.GetProperty = GetProperty;
    API.GetPropertyBatch = GetPropertyBatch;
    API.GetChildCount = GetChildCount;
    API.GetChild = GetChild;
    API.GetParent = GetParent;
    API.GetDeclContext = GetDeclContext;
    API.GetTypeInfo = GetTypeInfo;
    API.GetTypeElement = GetTypeElement;
    API.GetFunctionDeclInfo = GetFunctionDeclInfo;
    API.GetFunctionDeclParameter = GetFunctionDeclParameter;
    API.GetDeclAttributeCount = GetDeclAttributeCount;
    API.GetDeclAttribute = GetDeclAttribute;
    API.GetAttrInfo = GetAttrInfo;
    API.GetVarDeclInfo = GetVarDeclInfo;
    API.GetRecordDeclInfo = GetRecordDeclInfo;
    API.GetDeclRefExprInfo = GetDeclRefExprInfo;
    API.GetCallExprInfo = GetCallExprInfo;
    API.GetCallExprArgument = GetCallExprArgument;
    API.GetBinaryOperatorInfo = GetBinaryOperatorInfo;
    API.GetCompoundStmtInfo = GetCompoundStmtInfo;
    API.GetIntegerLiteralInfo = GetIntegerLiteralInfo;
    API.GetIntegerLiteralWord = GetIntegerLiteralWord;
    API.CreateASTBuilder = CreateASTBuilder;
    API.ASTBuilderSetProperty = ASTBuilderSetProperty;
    API.ASTBuilderSetIntegerValue = ASTBuilderSetIntegerValue;
    API.ASTBuilderSetBinaryOperatorKind = ASTBuilderSetBinaryOperatorKind;
    API.ASTBuilderSetChild = ASTBuilderSetChild;
    API.ASTBuilderCommit = ASTBuilderCommit;
    API.DestroyASTBuilder = DestroyASTBuilder;
    API.BeginASTMutation = BeginASTMutation;
    API.ASTMutationReplaceChild = ASTMutationReplaceChild;
    API.CommitASTMutation = CommitASTMutation;
    API.AbortASTMutation = AbortASTMutation;
    API.DestroyASTMutation = DestroyASTMutation;
    API.GetBuiltinType = GetBuiltinType;
  }

  const NevercASTAPI &api() const { return API; }

  Error attach(PluginTaskContext &Task, PluginASTBridge &Bridge) {
    const auto Key = std::make_pair(Task.handle().Owner, Task.handle().Value);
    std::lock_guard<std::mutex> Lock(Mutex);
    auto [It, Inserted] = Tasks.try_emplace(Key, &Bridge);
    if (!Inserted && It->second != &Bridge)
      return createStringError(inconvertibleErrorCode(),
                               "plugin AST task is already attached");
    return Error::success();
  }

  void detach(NevercTaskHandle Task) {
    std::lock_guard<std::mutex> Lock(Mutex);
    Tasks.erase(std::make_pair(Task.Owner, Task.Value));
  }

  void taskScopeUnregistered(NevercTaskHandle Task) noexcept override {
    detach(Task);
  }

private:
  template <typename CallbackT>
  NevercStatus forward(NevercTaskHandle Task, CallbackT &&Callback) {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = Tasks.find(std::make_pair(Task.Owner, Task.Value));
    if (It == Tasks.end() || !It->second)
      return astStatus(NEVERC_STATUS_STALE_HANDLE);
    return Callback(It->second->astAPI());
  }

  static PluginASTProcessBridge *bridge(void *Context) {
    return static_cast<PluginASTProcessBridge *>(Context);
  }

#define NEVERC_FORWARD_AST(Name, Signature, ...)                               \
  static NevercStatus NEVERC_CALL Name Signature {                             \
    if (!Context)                                                              \
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);                        \
    return bridge(Context)->forward(Task, [&](const NevercASTAPI &Local) {     \
      return Local.Name(Local.Context, __VA_ARGS__);                           \
    });                                                                        \
  }

  NEVERC_FORWARD_AST(GetTranslationUnit,
                     (void *Context, NevercTaskHandle Task,
                      NevercDeclHandle *OutDecl),
                     Task, OutDecl)
  NEVERC_FORWARD_AST(GetNodeInfo,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTNodeHandle Node, NevercASTNodeInfo *OutInfo),
                     Task, Node, OutInfo)
  NEVERC_FORWARD_AST(GetNodeInfoBatch,
                     (void *Context, NevercTaskHandle Task,
                      const NevercASTNodeHandle *Nodes, uint64_t NodeCount,
                      NevercASTNodeInfo *OutInfos, uint64_t OutInfoCapacity,
                      uint64_t OutInfoStride),
                     Task, Nodes, NodeCount, OutInfos, OutInfoCapacity,
                     OutInfoStride)
  NEVERC_FORWARD_AST(GetProperty,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTNodeHandle Node, NevercASTPropertyID Property,
                      NevercASTValue *OutValue),
                     Task, Node, Property, OutValue)
  NEVERC_FORWARD_AST(GetPropertyBatch,
                     (void *Context, NevercTaskHandle Task,
                      const NevercASTNodeHandle *Nodes,
                      const NevercASTPropertyID *Properties,
                      uint64_t QueryCount, NevercASTValue *OutValues,
                      uint64_t OutValueCapacity, uint64_t OutValueStride),
                     Task, Nodes, Properties, QueryCount, OutValues,
                     OutValueCapacity, OutValueStride)
  NEVERC_FORWARD_AST(GetChildCount,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTNodeHandle Node, uint64_t *OutCount),
                     Task, Node, OutCount)
  NEVERC_FORWARD_AST(GetChild,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTNodeHandle Node, uint64_t Index,
                      NevercASTNodeHandle *OutChild),
                     Task, Node, Index, OutChild)
  NEVERC_FORWARD_AST(GetParent,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTNodeHandle Node, NevercASTNodeHandle *OutParent),
                     Task, Node, OutParent)
  NEVERC_FORWARD_AST(GetDeclContext,
                     (void *Context, NevercTaskHandle Task,
                      NevercDeclHandle Decl,
                      NevercDeclContextHandle *OutContext),
                     Task, Decl, OutContext)
  NEVERC_FORWARD_AST(GetTypeInfo,
                     (void *Context, NevercTaskHandle Task,
                      NevercTypeHandle Type, NevercTypeInfo *OutInfo),
                     Task, Type, OutInfo)
  NEVERC_FORWARD_AST(GetTypeElement,
                     (void *Context, NevercTaskHandle Task,
                      NevercTypeHandle Type, uint64_t Index,
                      NevercTypeHandle *OutElement),
                     Task, Type, Index, OutElement)
  NEVERC_FORWARD_AST(GetFunctionDeclInfo,
                     (void *Context, NevercTaskHandle Task,
                      NevercDeclHandle Decl, NevercFunctionDeclInfo *OutInfo),
                     Task, Decl, OutInfo)
  NEVERC_FORWARD_AST(GetFunctionDeclParameter,
                     (void *Context, NevercTaskHandle Task,
                      NevercDeclHandle Decl, uint64_t Index,
                      NevercDeclHandle *OutParameter),
                     Task, Decl, Index, OutParameter)
  NEVERC_FORWARD_AST(GetDeclAttributeCount,
                     (void *Context, NevercTaskHandle Task,
                      NevercDeclHandle Decl, uint64_t *OutCount),
                     Task, Decl, OutCount)
  NEVERC_FORWARD_AST(GetDeclAttribute,
                     (void *Context, NevercTaskHandle Task,
                      NevercDeclHandle Decl, uint64_t Index,
                      NevercAttrHandle *OutAttribute),
                     Task, Decl, Index, OutAttribute)
  NEVERC_FORWARD_AST(GetAttrInfo,
                     (void *Context, NevercTaskHandle Task,
                      NevercAttrHandle Attribute, NevercAttrInfo *OutInfo),
                     Task, Attribute, OutInfo)
  NEVERC_FORWARD_AST(GetVarDeclInfo,
                     (void *Context, NevercTaskHandle Task,
                      NevercDeclHandle Decl, NevercVarDeclInfo *OutInfo),
                     Task, Decl, OutInfo)
  NEVERC_FORWARD_AST(GetRecordDeclInfo,
                     (void *Context, NevercTaskHandle Task,
                      NevercDeclHandle Decl, NevercRecordDeclInfo *OutInfo),
                     Task, Decl, OutInfo)
  NEVERC_FORWARD_AST(GetDeclRefExprInfo,
                     (void *Context, NevercTaskHandle Task,
                      NevercExprHandle Expression,
                      NevercDeclRefExprInfo *OutInfo),
                     Task, Expression, OutInfo)
  NEVERC_FORWARD_AST(GetCallExprInfo,
                     (void *Context, NevercTaskHandle Task,
                      NevercExprHandle Expression, NevercCallExprInfo *OutInfo),
                     Task, Expression, OutInfo)
  NEVERC_FORWARD_AST(GetCallExprArgument,
                     (void *Context, NevercTaskHandle Task,
                      NevercExprHandle Expression, uint64_t Index,
                      NevercExprHandle *OutArgument),
                     Task, Expression, Index, OutArgument)
  NEVERC_FORWARD_AST(GetBinaryOperatorInfo,
                     (void *Context, NevercTaskHandle Task,
                      NevercExprHandle Expression,
                      NevercBinaryOperatorInfo *OutInfo),
                     Task, Expression, OutInfo)
  NEVERC_FORWARD_AST(GetCompoundStmtInfo,
                     (void *Context, NevercTaskHandle Task,
                      NevercStmtHandle Statement,
                      NevercCompoundStmtInfo *OutInfo),
                     Task, Statement, OutInfo)
  NEVERC_FORWARD_AST(GetIntegerLiteralInfo,
                     (void *Context, NevercTaskHandle Task,
                      NevercExprHandle Expression,
                      NevercIntegerLiteralInfo *OutInfo),
                     Task, Expression, OutInfo)
  NEVERC_FORWARD_AST(GetIntegerLiteralWord,
                     (void *Context, NevercTaskHandle Task,
                      NevercExprHandle Expression, uint64_t Index,
                      uint64_t *OutWord),
                     Task, Expression, Index, OutWord)
  NEVERC_FORWARD_AST(CreateASTBuilder,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTNodeKind Kind,
                      NevercASTBuilderHandle *OutBuilder),
                     Task, Kind, OutBuilder)
  NEVERC_FORWARD_AST(ASTBuilderSetProperty,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTBuilderHandle Builder,
                      NevercASTPropertyID Property,
                      const NevercASTValue *Value),
                     Task, Builder, Property, Value)
  NEVERC_FORWARD_AST(ASTBuilderSetIntegerValue,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTBuilderHandle Builder,
                      const NevercAPIntView *Value),
                     Task, Builder, Value)
  NEVERC_FORWARD_AST(ASTBuilderSetBinaryOperatorKind,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTBuilderHandle Builder,
                      NevercBinaryOperatorKind Kind),
                     Task, Builder, Kind)
  NEVERC_FORWARD_AST(ASTBuilderSetChild,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTBuilderHandle Builder, NevercASTChildSlotID Slot,
                      uint64_t Index, NevercASTNodeHandle Child),
                     Task, Builder, Slot, Index, Child)
  NEVERC_FORWARD_AST(ASTBuilderCommit,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTBuilderHandle Builder,
                      NevercASTNodeHandle *OutNode),
                     Task, Builder, OutNode)
  NEVERC_FORWARD_AST(DestroyASTBuilder,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTBuilderHandle Builder),
                     Task, Builder)
  NEVERC_FORWARD_AST(BeginASTMutation,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTMutationHandle *OutMutation),
                     Task, OutMutation)
  NEVERC_FORWARD_AST(ASTMutationReplaceChild,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTMutationHandle Mutation,
                      NevercASTNodeHandle Parent, NevercASTChildSlotID Slot,
                      uint64_t Index, NevercASTNodeHandle Replacement),
                     Task, Mutation, Parent, Slot, Index, Replacement)
  NEVERC_FORWARD_AST(CommitASTMutation,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTMutationHandle Mutation),
                     Task, Mutation)
  NEVERC_FORWARD_AST(AbortASTMutation,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTMutationHandle Mutation),
                     Task, Mutation)
  NEVERC_FORWARD_AST(DestroyASTMutation,
                     (void *Context, NevercTaskHandle Task,
                      NevercASTMutationHandle Mutation),
                     Task, Mutation)
  NEVERC_FORWARD_AST(GetBuiltinType,
                     (void *Context, NevercTaskHandle Task,
                      NevercBuiltinTypeKind Kind, NevercTypeHandle *OutType),
                     Task, Kind, OutType)

#undef NEVERC_FORWARD_AST

  NevercASTAPI API{};
  std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>, PluginASTBridge *> Tasks;
};

std::shared_ptr<PluginASTProcessBridge>
findASTProcessBridge(PluginProcessServices &Services) {
  return std::static_pointer_cast<PluginASTProcessBridge>(
      Services.findHostService(astPluginInterfaceID()));
}

} // namespace

struct PluginASTBridge::Impl {
  enum class Domain : uint8_t {
    Decl,
    Stmt,
    Type,
    Attr,
  };

  struct Entity {
    Domain Kind = Domain::Decl;
    const void *Pointer = nullptr;
    QualType QualifiedType;
  };

  struct BuilderPayload {
    Impl *Owner = nullptr;
    NevercASTNodeKind Kind = NEVERC_AST_NODE_KIND_INVALID;
    std::map<NevercASTPropertyID, NevercASTValue> Properties;
    std::map<NevercASTChildSlotID, SmallVector<NevercASTNodeHandle, 2>>
        Children;
    SmallVector<uint64_t, 2> IntegerWords;
    uint32_t IntegerBitWidth = 0;
    NevercBinaryOperatorKind BinaryOperatorKind = 0;
    bool HasIntegerValue = false;
    bool HasBinaryOperatorKind = false;
    bool Committed = false;
  };

  struct ReplaceChildOperation {
    NevercASTNodeHandle Parent{};
    NevercASTChildSlotID Slot = NEVERC_AST_CHILD_SLOT_INVALID;
    uint64_t Index = 0;
    NevercASTNodeHandle Replacement{};
  };

  enum class MutationState : uint8_t {
    Open,
    Committed,
    Aborted,
  };

  struct MutationPayload {
    Impl *Owner = nullptr;
    SmallVector<ReplaceChildOperation, 4> Replacements;
    MutationState State = MutationState::Open;
  };

  PluginTaskContext &Task;
  TreeContext &Context;
  FrontendPluginBridge &Locations;
  PluginPrepBridge *Identifiers;
  NevercASTAPI API{};
  DenseMap<const Decl *, NevercHandle> DeclHandles;
  DenseMap<const Stmt *, NevercHandle> StmtHandles;
  DenseMap<const void *, NevercHandle> TypeHandles;
  DenseMap<const Attr *, NevercHandle> AttrHandles;
  DenseMap<const Stmt *, const Stmt *> StmtParents;
  DenseMap<const Stmt *, const Decl *> StmtDeclParents;
  DenseMap<const Attr *, const Decl *> AttrParents;
  std::mutex MutationMutex;

  Impl(PluginTaskContext &TaskValue, TreeContext &ContextValue,
       FrontendPluginBridge &LocationsValue, PluginPrepBridge *IdentifiersValue)
      : Task(TaskValue), Context(ContextValue), Locations(LocationsValue),
        Identifiers(IdentifiersValue) {
    API.Header = {sizeof(API), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    API.Context = this;
    API.GetTranslationUnit = getTranslationUnit;
    API.GetNodeInfo = getNodeInfo;
    API.GetNodeInfoBatch = getNodeInfoBatch;
    API.GetProperty = getProperty;
    API.GetPropertyBatch = getPropertyBatch;
    API.GetChildCount = getChildCount;
    API.GetChild = getChild;
    API.GetParent = getParent;
    API.GetDeclContext = getDeclContext;
    API.GetTypeInfo = getTypeInfo;
    API.GetTypeElement = getTypeElement;
    API.GetFunctionDeclInfo = getFunctionDeclInfo;
    API.GetFunctionDeclParameter = getFunctionDeclParameter;
    API.GetDeclAttributeCount = getDeclAttributeCount;
    API.GetDeclAttribute = getDeclAttribute;
    API.GetAttrInfo = getAttrInfo;
    API.GetVarDeclInfo = getVarDeclInfo;
    API.GetRecordDeclInfo = getRecordDeclInfo;
    API.GetDeclRefExprInfo = getDeclRefExprInfo;
    API.GetCallExprInfo = getCallExprInfo;
    API.GetCallExprArgument = getCallExprArgument;
    API.GetBinaryOperatorInfo = getBinaryOperatorInfo;
    API.GetCompoundStmtInfo = getCompoundStmtInfo;
    API.GetIntegerLiteralInfo = getIntegerLiteralInfo;
    API.GetIntegerLiteralWord = getIntegerLiteralWord;
    API.CreateASTBuilder = createASTBuilder;
    API.ASTBuilderSetProperty = astBuilderSetProperty;
    API.ASTBuilderSetIntegerValue = astBuilderSetIntegerValue;
    API.ASTBuilderSetBinaryOperatorKind = astBuilderSetBinaryOperatorKind;
    API.ASTBuilderSetChild = astBuilderSetChild;
    API.ASTBuilderCommit = astBuilderCommit;
    API.DestroyASTBuilder = destroyASTBuilder;
    API.BeginASTMutation = beginASTMutation;
    API.ASTMutationReplaceChild = astMutationReplaceChild;
    API.CommitASTMutation = commitASTMutation;
    API.AbortASTMutation = abortASTMutation;
    API.DestroyASTMutation = destroyASTMutation;
    API.GetBuiltinType = getBuiltinType;
    indexDecl(Context.getTranslationUnitDecl());
  }

  bool validTask(NevercTaskHandle Handle) const {
    return !Task.isEnded() && sameHandle(Handle, Task.handle());
  }

  Expected<NevercHandle> createEntity(Domain Kind, const void *Pointer,
                                      QualType QualifiedType = {}) {
    if (!Pointer)
      return createStringError(inconvertibleErrorCode(),
                               "cannot publish a null AST entity");

    DenseMap<const void *, NevercHandle> *Map = nullptr;
    const void *Key = Pointer;
    switch (Kind) {
    case Domain::Decl:
      if (auto It = DeclHandles.find(static_cast<const Decl *>(Pointer));
          It != DeclHandles.end())
        return It->second;
      break;
    case Domain::Stmt:
      if (auto It = StmtHandles.find(static_cast<const Stmt *>(Pointer));
          It != StmtHandles.end())
        return It->second;
      break;
    case Domain::Type:
      Key = QualifiedType.getAsOpaquePtr();
      Map = &TypeHandles;
      if (auto It = Map->find(Key); It != Map->end())
        return It->second;
      break;
    case Domain::Attr:
      if (auto It = AttrHandles.find(static_cast<const Attr *>(Pointer));
          It != AttrHandles.end())
        return It->second;
      break;
    }

    auto *Payload = new (std::nothrow) Entity{Kind, Pointer, QualifiedType};
    if (!Payload)
      return createStringError(inconvertibleErrorCode(),
                               "cannot allocate AST entity handle");
    auto Created = Task.handles().create(
        PluginASTEntityHandleKind, Payload,
        [](void *Value) { delete static_cast<Entity *>(Value); });
    if (!Created) {
      delete Payload;
      return Created.takeError();
    }

    switch (Kind) {
    case Domain::Decl:
      DeclHandles[static_cast<const Decl *>(Pointer)] = *Created;
      break;
    case Domain::Stmt:
      StmtHandles[static_cast<const Stmt *>(Pointer)] = *Created;
      break;
    case Domain::Type:
      (*Map)[Key] = *Created;
      break;
    case Domain::Attr:
      AttrHandles[static_cast<const Attr *>(Pointer)] = *Created;
      break;
    }
    return *Created;
  }

  Expected<NevercHandle> createDecl(const Decl *Value) {
    return createEntity(Domain::Decl, Value);
  }

  Expected<NevercHandle> createStmt(const Stmt *Value) {
    return createEntity(Domain::Stmt, Value);
  }

  Expected<NevercHandle> createType(QualType Value) {
    if (Value.isNull())
      return createStringError(inconvertibleErrorCode(),
                               "cannot publish a null AST type");
    return createEntity(Domain::Type, Value.getTypePtr(), Value);
  }

  Expected<NevercHandle> createAttr(const Attr *Value) {
    return createEntity(Domain::Attr, Value);
  }

  NevercStatus resolve(NevercTaskHandle TaskHandle, NevercASTNodeHandle Handle,
                       Entity **Out) {
    if (!Out)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *Out = nullptr;
    if (!validTask(TaskHandle))
      return astStatus(NEVERC_STATUS_WRONG_SCOPE);
    void *Payload = nullptr;
    NevercStatus Status =
        Task.handles().resolve(Handle, PluginASTEntityHandleKind, &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    *Out = static_cast<Entity *>(Payload);
    return neverc_status_ok();
  }

  NevercStatus resolvePublishedNode(NevercTaskHandle TaskHandle,
                                    NevercASTNodeHandle Handle,
                                    NevercASTSchemaDomain ExpectedDomain,
                                    const void **OutNode) {
    if (!OutNode)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutNode = nullptr;
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, Handle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;

    NevercASTSchemaDomain ActualDomain = 0;
    switch (Value->Kind) {
    case Domain::Decl:
      ActualDomain = NEVERC_AST_SCHEMA_DOMAIN_DECL;
      break;
    case Domain::Stmt:
      ActualDomain = NEVERC_AST_SCHEMA_DOMAIN_STMT;
      break;
    case Domain::Type:
      ActualDomain = NEVERC_AST_SCHEMA_DOMAIN_TYPE;
      break;
    case Domain::Attr:
      ActualDomain = NEVERC_AST_SCHEMA_DOMAIN_ATTR;
      break;
    }
    if (ActualDomain != ExpectedDomain)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    *OutNode = Value->Kind == Domain::Type
                   ? Value->QualifiedType.getAsOpaquePtr()
                   : Value->Pointer;
    return neverc_status_ok();
  }

  NevercStatus resolveBuilder(NevercTaskHandle TaskHandle,
                              NevercASTBuilderHandle Handle,
                              BuilderPayload **Out) {
    if (!Out)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *Out = nullptr;
    if (!validTask(TaskHandle))
      return astStatus(NEVERC_STATUS_WRONG_SCOPE);
    void *Payload = nullptr;
    NevercStatus Status =
        Task.handles().resolve(Handle, PluginASTBuilderHandleKind, &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Builder = static_cast<BuilderPayload *>(Payload);
    if (Builder->Owner != this)
      return astStatus(NEVERC_STATUS_WRONG_SCOPE);
    *Out = Builder;
    return neverc_status_ok();
  }

  NevercStatus resolveMutation(NevercTaskHandle TaskHandle,
                               NevercASTMutationHandle Handle,
                               MutationPayload **Out) {
    if (!Out)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *Out = nullptr;
    if (!validTask(TaskHandle))
      return astStatus(NEVERC_STATUS_WRONG_SCOPE);
    void *Payload = nullptr;
    NevercStatus Status =
        Task.handles().resolve(Handle, PluginASTMutationHandleKind, &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Mutation = static_cast<MutationPayload *>(Payload);
    if (Mutation->Owner != this)
      return astStatus(NEVERC_STATUS_WRONG_SCOPE);
    *Out = Mutation;
    return neverc_status_ok();
  }

  NevercStatus publish(Expected<NevercHandle> Created,
                       NevercHandle *OutHandle) {
    if (!OutHandle)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutHandle = {};
    if (!Created) {
      consumeError(Created.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutHandle = *Created;
    return neverc_status_ok();
  }

  void indexStmt(const Stmt *Value, const Stmt *ParentStmt,
                 const Decl *ParentDecl) {
    if (!Value)
      return;
    if (ParentStmt)
      StmtParents.try_emplace(Value, ParentStmt);
    else if (ParentDecl)
      StmtDeclParents.try_emplace(Value, ParentDecl);
    for (const Stmt *Child : Value->children())
      indexStmt(Child, Value, nullptr);
  }

  void indexDecl(const Decl *Value) {
    if (!Value)
      return;
    for (const Attr *Attribute : Value->attrs())
      AttrParents.try_emplace(Attribute, Value);
    if (const auto *Function = dyn_cast<FunctionDecl>(Value))
      indexStmt(Function->getBody(), nullptr, Function);
    else if (const auto *Variable = dyn_cast<VarDecl>(Value))
      indexStmt(Variable->getInit(), nullptr, Variable);
    if (const auto *Decls = dyn_cast<DeclContext>(Value))
      for (const Decl *Child : Decls->decls())
        indexDecl(Child);
  }

  SmallVector<Entity, 16> childEntities(const Entity &Value) {
    SmallVector<Entity, 16> Result;
    switch (Value.Kind) {
    case Domain::Decl: {
      const auto *Declaration = static_cast<const Decl *>(Value.Pointer);
      if (const auto *Function = dyn_cast<FunctionDecl>(Declaration)) {
        for (const ParmVarDecl *Parameter : Function->parameters())
          Result.push_back({Domain::Decl, Parameter, {}});
        if (const Stmt *Body = Function->getBody())
          Result.push_back({Domain::Stmt, Body, {}});
      } else if (const auto *Variable = dyn_cast<VarDecl>(Declaration)) {
        if (const Expr *Initializer = Variable->getInit())
          Result.push_back({Domain::Stmt, Initializer, {}});
      }
      if (const auto *Decls = dyn_cast<DeclContext>(Declaration))
        for (const Decl *Child : Decls->decls()) {
          if (isa<FunctionDecl>(Declaration) && isa<ParmVarDecl>(Child))
            continue;
          Result.push_back({Domain::Decl, Child, {}});
        }
      break;
    }
    case Domain::Stmt: {
      const auto *Statement = static_cast<const Stmt *>(Value.Pointer);
      for (const Stmt *Child : Statement->children()) {
        if (!Child)
          continue;
        Result.push_back({Domain::Stmt, Child, {}});
      }
      break;
    }
    case Domain::Type: {
      QualType TypeValue = Value.QualifiedType;
      const Type *TypePtr = TypeValue.getTypePtr();
      const auto AddType = [&](QualType Child) {
        if (Child.isNull())
          return;
        Result.push_back({Domain::Type, Child.getTypePtr(), Child});
      };
      if (const auto *Pointer = dyn_cast<PointerType>(TypePtr))
        AddType(Pointer->getPointeeType());
      else if (const auto *Array = dyn_cast<ArrayType>(TypePtr))
        AddType(Array->getElementType());
      else if (const auto *Function = dyn_cast<FunctionType>(TypePtr)) {
        AddType(Function->getReturnType());
        if (const auto *Prototype = dyn_cast<FunctionProtoType>(Function))
          for (QualType Parameter : Prototype->getParamTypes())
            AddType(Parameter);
      } else if (const auto *Complex = dyn_cast<ComplexType>(TypePtr))
        AddType(Complex->getElementType());
      else if (const auto *Atomic = dyn_cast<AtomicType>(TypePtr))
        AddType(Atomic->getValueType());
      break;
    }
    case Domain::Attr:
      break;
    }
    return Result;
  }

  NevercASTNodeKind kind(const Entity &Value) const {
    switch (Value.Kind) {
    case Domain::Decl:
      return stableDeclKind(
          static_cast<const Decl *>(Value.Pointer)->getKind());
    case Domain::Stmt:
      return stableStmtKind(
          static_cast<const Stmt *>(Value.Pointer)->getStmtClass());
    case Domain::Type:
      return stableTypeKind(
          static_cast<const Type *>(Value.Pointer)->getTypeClass());
    case Domain::Attr:
      return stableAttrKind(
          static_cast<const Attr *>(Value.Pointer)->getKind());
    }
    return NEVERC_AST_NODE_KIND_INVALID;
  }

  NevercASTSchemaDomain domain(const Entity &Value) const {
    switch (Value.Kind) {
    case Domain::Decl:
      return NEVERC_AST_SCHEMA_DOMAIN_DECL;
    case Domain::Stmt:
      return NEVERC_AST_SCHEMA_DOMAIN_STMT;
    case Domain::Type:
      return NEVERC_AST_SCHEMA_DOMAIN_TYPE;
    case Domain::Attr:
      return NEVERC_AST_SCHEMA_DOMAIN_ATTR;
    }
    return 0;
  }

  Expected<NevercSourceRange> sourceRange(const Entity &Value) {
    SourceRange Range;
    switch (Value.Kind) {
    case Domain::Decl:
      Range = static_cast<const Decl *>(Value.Pointer)->getSourceRange();
      break;
    case Domain::Stmt:
      Range = static_cast<const Stmt *>(Value.Pointer)->getSourceRange();
      break;
    case Domain::Attr:
      Range = static_cast<const Attr *>(Value.Pointer)->getRange();
      break;
    case Domain::Type:
      return NevercSourceRange{};
    }
    if (Range.isInvalid())
      return NevercSourceRange{};
    return Locations.createRange(CharSourceRange::getTokenRange(Range));
  }

  Expected<NevercHandle> parent(const Entity &Value) {
    switch (Value.Kind) {
    case Domain::Decl: {
      const auto *Declaration = static_cast<const Decl *>(Value.Pointer);
      const DeclContext *Parent = Declaration->getDeclContext();
      if (!Parent)
        return NevercHandle{};
      return createDecl(Decl::castFromDeclContext(Parent));
    }
    case Domain::Stmt: {
      const auto *Statement = static_cast<const Stmt *>(Value.Pointer);
      if (auto It = StmtParents.find(Statement); It != StmtParents.end())
        return createStmt(It->second);
      if (auto It = StmtDeclParents.find(Statement);
          It != StmtDeclParents.end())
        return createDecl(It->second);
      return NevercHandle{};
    }
    case Domain::Attr: {
      const auto *Attribute = static_cast<const Attr *>(Value.Pointer);
      if (auto It = AttrParents.find(Attribute); It != AttrParents.end())
        return createDecl(It->second);
      return NevercHandle{};
    }
    case Domain::Type:
      return NevercHandle{};
    }
    return NevercHandle{};
  }

  NevercStatus fillNodeInfo(NevercTaskHandle TaskHandle,
                            NevercASTNodeHandle Node,
                            NevercASTNodeInfo *OutInfo) {
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, Node, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;

    NevercASTNodeInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    Info.Kind = kind(*Value);
    Info.Domain = domain(*Value);
    if (Info.Kind == NEVERC_AST_NODE_KIND_INVALID)
      return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);

    auto Parent = parent(*Value);
    if (!Parent) {
      consumeError(Parent.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.Parent = *Parent;
    if (Value->Kind == Domain::Decl) {
      const auto *Declaration = static_cast<const Decl *>(Value->Pointer);
      if (const DeclContext *DC = Declaration->getDeclContext()) {
        auto ContextHandle = createDecl(Decl::castFromDeclContext(DC));
        if (!ContextHandle) {
          consumeError(ContextHandle.takeError());
          return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
        }
        Info.DeclContext = *ContextHandle;
      }
    }
    auto Range = sourceRange(*Value);
    if (!Range) {
      consumeError(Range.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.SourceRange = *Range;
    return writeCallerRecord(OutInfo, Info);
  }

  NevercStatus fillProperty(NevercTaskHandle TaskHandle,
                            NevercASTNodeHandle Node,
                            NevercASTPropertyID Property,
                            NevercASTValue *OutValue) {
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, Node, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;

    NevercASTValue Result{};
    Result.Header = {sizeof(Result), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR,
                     0};
    if (Property == NEVERC_AST_PROPERTY_AST_SOURCE_RANGE) {
      auto Range = sourceRange(*Value);
      if (!Range) {
        consumeError(Range.takeError());
        return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }
      Result.Type = NEVERC_AST_VALUE_SOURCE_RANGE;
      Result.SourceRangeValue = *Range;
      return writeCallerRecord(OutValue, Result);
    }

    switch (Value->Kind) {
    case Domain::Decl: {
      const auto *Declaration = static_cast<const Decl *>(Value->Pointer);
      if (Property == NEVERC_AST_PROPERTY_DECL_IS_IMPLICIT) {
        Result.Type = NEVERC_AST_VALUE_BOOL;
        Result.UnsignedValue =
            Declaration->isImplicit() ? NEVERC_TRUE : NEVERC_FALSE;
      } else if (Property == NEVERC_AST_PROPERTY_DECL_IS_INVALID) {
        Result.Type = NEVERC_AST_VALUE_BOOL;
        Result.UnsignedValue =
            Declaration->isInvalidDecl() ? NEVERC_TRUE : NEVERC_FALSE;
      } else if (Property == NEVERC_AST_PROPERTY_DECL_NAME) {
        const auto *Named = dyn_cast<NamedDecl>(Declaration);
        if (!Named)
          return astStatus(NEVERC_STATUS_WRONG_TYPE);
        Result.Type = NEVERC_AST_VALUE_IDENTIFIER;
        IdentifierInfo *Identifier = Named->getIdentifier();
        if (Identifier) {
          if (!Identifiers)
            return astStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
          auto IdentifierHandle = Identifiers->createIdentifier(Identifier);
          if (!IdentifierHandle) {
            consumeError(IdentifierHandle.takeError());
            return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
          }
          Result.NodeValue = *IdentifierHandle;
        }
      } else if (Property == NEVERC_AST_PROPERTY_DECL_TYPE) {
        const auto *Typed = dyn_cast<ValueDecl>(Declaration);
        if (!Typed)
          return astStatus(NEVERC_STATUS_WRONG_TYPE);
        auto TypeHandle = createType(Typed->getType());
        if (!TypeHandle) {
          consumeError(TypeHandle.takeError());
          return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
        }
        Result.Type = NEVERC_AST_VALUE_TYPE;
        Result.NodeValue = *TypeHandle;
      } else {
        return astStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
      }
      break;
    }
    case Domain::Stmt: {
      const auto *Statement = static_cast<const Stmt *>(Value->Pointer);
      const auto *Expression = dyn_cast<Expr>(Statement);
      if (Property == NEVERC_AST_PROPERTY_STMT_EXPR_TYPE) {
        if (!Expression)
          return astStatus(NEVERC_STATUS_WRONG_TYPE);
        auto TypeHandle = createType(Expression->getType());
        if (!TypeHandle) {
          consumeError(TypeHandle.takeError());
          return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
        }
        Result.Type = NEVERC_AST_VALUE_TYPE;
        Result.NodeValue = *TypeHandle;
      } else if (Property == NEVERC_AST_PROPERTY_STMT_VALUE_KIND) {
        if (!Expression)
          return astStatus(NEVERC_STATUS_WRONG_TYPE);
        Result.Type = NEVERC_AST_VALUE_ENUM;
        Result.UnsignedValue = Expression->getValueKind();
      } else {
        return astStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
      }
      break;
    }
    case Domain::Type: {
      const Type *TypeValue = Value->QualifiedType.getTypePtr();
      if (Property == NEVERC_AST_PROPERTY_TYPE_CANONICAL) {
        auto Canonical = createType(Value->QualifiedType.getCanonicalType());
        if (!Canonical) {
          consumeError(Canonical.takeError());
          return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
        }
        Result.Type = NEVERC_AST_VALUE_TYPE;
        Result.NodeValue = *Canonical;
      } else if (Property == NEVERC_AST_PROPERTY_TYPE_QUALIFIERS) {
        Result.Type = NEVERC_AST_VALUE_U64;
        Result.UnsignedValue =
            Value->QualifiedType.getQualifiers().getAsOpaqueValue();
      } else if (Property == NEVERC_AST_PROPERTY_TYPE_IS_DEPENDENT) {
        Result.Type = NEVERC_AST_VALUE_BOOL;
        Result.UnsignedValue =
            TypeValue->isDependentType() ? NEVERC_TRUE : NEVERC_FALSE;
      } else {
        return astStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
      }
      break;
    }
    case Domain::Attr: {
      const auto *Attribute = static_cast<const Attr *>(Value->Pointer);
      if (Property == NEVERC_AST_PROPERTY_ATTR_SPELLING) {
        Result.Type = NEVERC_AST_VALUE_STRING;
        Result.StringValue = stringView(Attribute->getSpelling());
      } else if (Property == NEVERC_AST_PROPERTY_ATTR_IS_IMPLICIT) {
        Result.Type = NEVERC_AST_VALUE_BOOL;
        Result.UnsignedValue =
            Attribute->isImplicit() ? NEVERC_TRUE : NEVERC_FALSE;
      } else if (Property == NEVERC_AST_PROPERTY_ATTR_IS_INHERITED) {
        Result.Type = NEVERC_AST_VALUE_BOOL;
        Result.UnsignedValue =
            Attribute->isInherited() ? NEVERC_TRUE : NEVERC_FALSE;
      } else if (Property == NEVERC_AST_PROPERTY_ATTR_DEPRECATED_MESSAGE) {
        const auto *Deprecated = dyn_cast<DeprecatedAttr>(Attribute);
        if (!Deprecated)
          return astStatus(NEVERC_STATUS_WRONG_TYPE);
        Result.Type = NEVERC_AST_VALUE_STRING;
        Result.StringValue = stringView(Deprecated->getMessage());
      } else if (Property == NEVERC_AST_PROPERTY_ATTR_DEPRECATED_REPLACEMENT) {
        const auto *Deprecated = dyn_cast<DeprecatedAttr>(Attribute);
        if (!Deprecated)
          return astStatus(NEVERC_STATUS_WRONG_TYPE);
        Result.Type = NEVERC_AST_VALUE_STRING;
        Result.StringValue = stringView(Deprecated->getReplacement());
      } else if (Property == NEVERC_AST_PROPERTY_ATTR_ANNOTATE_ANNOTATION) {
        const auto *Annotation = dyn_cast<AnnotateAttr>(Attribute);
        if (!Annotation)
          return astStatus(NEVERC_STATUS_WRONG_TYPE);
        Result.Type = NEVERC_AST_VALUE_STRING;
        Result.StringValue = stringView(Annotation->getAnnotation());
      } else if (Property ==
                 NEVERC_AST_PROPERTY_ATTR_ANNOTATE_TYPE_ANNOTATION) {
        const auto *Annotation = dyn_cast<AnnotateTypeAttr>(Attribute);
        if (!Annotation)
          return astStatus(NEVERC_STATUS_WRONG_TYPE);
        Result.Type = NEVERC_AST_VALUE_STRING;
        Result.StringValue = stringView(Annotation->getAnnotation());
      } else {
        return astStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
      }
      break;
    }
    }
    return writeCallerRecord(OutValue, Result);
  }

  NevercStatus fillTypeInfo(NevercTaskHandle TaskHandle,
                            NevercTypeHandle TypeHandle,
                            NevercTypeInfo *OutInfo) {
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, TypeHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Type)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);

    const QualType QualifiedType = Value->QualifiedType;
    const Type *TypeValue = QualifiedType.getTypePtr();
    NevercTypeInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    Info.Kind = stableTypeKind(TypeValue->getTypeClass());
    if (Info.Kind == NEVERC_TYPE_KIND_INVALID)
      return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);

    const Qualifiers Quals = QualifiedType.getQualifiers();
    if (Quals.hasConst())
      Info.QualifierFlags |= NEVERC_TYPE_QUALIFIER_CONST;
    if (Quals.hasRestrict())
      Info.QualifierFlags |= NEVERC_TYPE_QUALIFIER_RESTRICT;
    if (Quals.hasVolatile())
      Info.QualifierFlags |= NEVERC_TYPE_QUALIFIER_VOLATILE;
    if (Quals.hasUnaligned())
      Info.QualifierFlags |= NEVERC_TYPE_QUALIFIER_UNALIGNED;

    switch (Quals.getAddressSpace()) {
    case LangAS::Default:
      Info.AddressSpaceKind = NEVERC_TYPE_ADDRESS_SPACE_DEFAULT;
      break;
    case LangAS::ptr32_sptr:
      Info.AddressSpaceKind = NEVERC_TYPE_ADDRESS_SPACE_PTR32_SIGNED;
      break;
    case LangAS::ptr32_uptr:
      Info.AddressSpaceKind = NEVERC_TYPE_ADDRESS_SPACE_PTR32_UNSIGNED;
      break;
    case LangAS::ptr64:
      Info.AddressSpaceKind = NEVERC_TYPE_ADDRESS_SPACE_PTR64;
      break;
    default:
      Info.AddressSpaceKind = NEVERC_TYPE_ADDRESS_SPACE_TARGET;
      Info.TargetAddressSpace = toTargetAddressSpace(Quals.getAddressSpace());
      break;
    }

    if (QualifiedType.isCanonical())
      Info.Flags |= NEVERC_TYPE_FLAG_CANONICAL;
    if (TypeValue->isDependentType())
      Info.Flags |= NEVERC_TYPE_FLAG_DEPENDENT;
    if (TypeValue->isIncompleteType())
      Info.Flags |= NEVERC_TYPE_FLAG_INCOMPLETE;
    if (TypeValue->isFunctionType())
      Info.Flags |= NEVERC_TYPE_FLAG_FUNCTION;
    if (isa<PointerType>(TypeValue))
      Info.Flags |= NEVERC_TYPE_FLAG_POINTER;
    if (isa<ArrayType>(TypeValue))
      Info.Flags |= NEVERC_TYPE_FLAG_ARRAY;
    if (isa<VectorType>(TypeValue))
      Info.Flags |= NEVERC_TYPE_FLAG_VECTOR;
    if (isa<AtomicType>(TypeValue))
      Info.Flags |= NEVERC_TYPE_FLAG_ATOMIC;

    const QualType Desugared =
        QualifiedType.getSingleStepDesugaredType(Context);
    if (Desugared != QualifiedType)
      Info.Flags |= NEVERC_TYPE_FLAG_SUGARED;

    auto Canonical = createType(QualifiedType.getCanonicalType());
    auto DesugaredHandle = createType(Desugared);
    if (!Canonical || !DesugaredHandle) {
      if (!Canonical)
        consumeError(Canonical.takeError());
      if (!DesugaredHandle)
        consumeError(DesugaredHandle.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.CanonicalType = *Canonical;
    Info.DesugaredType = *DesugaredHandle;

    QualType Related;
    if (const auto *Pointer = dyn_cast<PointerType>(TypeValue)) {
      Related = Pointer->getPointeeType();
    } else if (const auto *Array = dyn_cast<ArrayType>(TypeValue)) {
      Related = Array->getElementType();
      if (const auto *Constant = dyn_cast<ConstantArrayType>(Array))
        Info.ElementCount = Constant->getSize().getLimitedValue();
    } else if (const auto *Vector = dyn_cast<VectorType>(TypeValue)) {
      Related = Vector->getElementType();
      Info.ElementCount = Vector->getNumElements();
    } else if (const auto *Matrix = dyn_cast<ConstantMatrixType>(TypeValue)) {
      Related = Matrix->getElementType();
      Info.ElementCount = Matrix->getNumElementsFlattened();
    } else if (const auto *Complex = dyn_cast<ComplexType>(TypeValue)) {
      Related = Complex->getElementType();
      Info.ElementCount = 1;
    } else if (const auto *Atomic = dyn_cast<AtomicType>(TypeValue)) {
      Related = Atomic->getValueType();
      Info.ElementCount = 1;
    } else if (const auto *Function = dyn_cast<FunctionType>(TypeValue)) {
      Related = Function->getReturnType();
      if (const auto *Prototype = dyn_cast<FunctionProtoType>(Function)) {
        Info.ElementCount = Prototype->getNumParams();
        if (Prototype->isVariadic())
          Info.Flags |= NEVERC_TYPE_FLAG_VARIADIC;
      }
    }
    if (!Related.isNull()) {
      auto RelatedHandle = createType(Related);
      if (!RelatedHandle) {
        consumeError(RelatedHandle.takeError());
        return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }
      Info.RelatedType = *RelatedHandle;
    }

    if (const auto *Builtin = dyn_cast<BuiltinType>(TypeValue))
      Info.Name = stringView(Builtin->getName(Context.getPrintingPolicy()));

    if (TypeValue->isObjectType() && !TypeValue->isDependentType() &&
        !TypeValue->isIncompleteType() && !TypeValue->isSizelessType()) {
      Info.Flags |= NEVERC_TYPE_FLAG_HAS_KNOWN_LAYOUT;
      Info.SizeInBits = Context.getTypeSize(QualifiedType);
      Info.AlignmentInBits = Context.getTypeAlign(QualifiedType);
    }

    return writeCallerRecord(OutInfo, Info);
  }

  NevercStatus fillFunctionDeclInfo(NevercTaskHandle TaskHandle,
                                    NevercDeclHandle DeclHandle,
                                    NevercFunctionDeclInfo *OutInfo) {
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, DeclHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Decl)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Function =
        dyn_cast<FunctionDecl>(static_cast<const Decl *>(Value->Pointer));
    if (!Function)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);

    NevercFunctionDeclInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    Info.Name = stringView(Function->getName());
    Info.ParameterCount = Function->getNumParams();
    Info.IsVariadic = Function->isVariadic() ? NEVERC_TRUE : NEVERC_FALSE;
    Info.IsDefinition =
        Function->isThisDeclarationADefinition() ? NEVERC_TRUE : NEVERC_FALSE;

    auto FunctionType = createType(Function->getType());
    auto ReturnType = createType(Function->getReturnType());
    if (!FunctionType || !ReturnType) {
      if (!FunctionType)
        consumeError(FunctionType.takeError());
      if (!ReturnType)
        consumeError(ReturnType.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.FunctionType = *FunctionType;
    Info.ReturnType = *ReturnType;
    if (const Stmt *Body = Function->getBody()) {
      auto BodyHandle = createStmt(Body);
      if (!BodyHandle) {
        consumeError(BodyHandle.takeError());
        return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }
      Info.Body = *BodyHandle;
    }
    return writeCallerRecord(OutInfo, Info);
  }

  NevercStatus fillAttrInfo(NevercTaskHandle TaskHandle,
                            NevercAttrHandle AttributeHandle,
                            NevercAttrInfo *OutInfo) {
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, AttributeHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Attr)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Attribute = static_cast<const Attr *>(Value->Pointer);

    NevercAttrInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    Info.Kind = stableAttrKind(Attribute->getKind());
    if (Info.Kind == NEVERC_ATTR_KIND_INVALID)
      return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);
    Info.Spelling = stringView(Attribute->getSpelling());
    Info.IsImplicit = Attribute->isImplicit() ? NEVERC_TRUE : NEVERC_FALSE;
    Info.IsInherited = Attribute->isInherited() ? NEVERC_TRUE : NEVERC_FALSE;
    auto Range = sourceRange(*Value);
    if (!Range) {
      consumeError(Range.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.SourceRange = *Range;
    return writeCallerRecord(OutInfo, Info);
  }

  NevercStatus fillVarDeclInfo(NevercTaskHandle TaskHandle,
                               NevercDeclHandle DeclHandle,
                               NevercVarDeclInfo *OutInfo) {
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, DeclHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Decl)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Variable =
        dyn_cast<VarDecl>(static_cast<const Decl *>(Value->Pointer));
    if (!Variable)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);

    NevercVarDeclInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    Info.Name = stringView(Variable->getName());
    Info.IsDefinition =
        Variable->isThisDeclarationADefinition() != VarDecl::DeclarationOnly
            ? NEVERC_TRUE
            : NEVERC_FALSE;
    Info.HasGlobalStorage =
        Variable->hasGlobalStorage() ? NEVERC_TRUE : NEVERC_FALSE;
    auto TypeHandle = createType(Variable->getType());
    if (!TypeHandle) {
      consumeError(TypeHandle.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.Type = *TypeHandle;
    if (const Expr *Initializer = Variable->getInit()) {
      auto InitializerHandle = createStmt(Initializer);
      if (!InitializerHandle) {
        consumeError(InitializerHandle.takeError());
        return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }
      Info.Initializer = *InitializerHandle;
    }
    return writeCallerRecord(OutInfo, Info);
  }

  NevercStatus fillRecordDeclInfo(NevercTaskHandle TaskHandle,
                                  NevercDeclHandle DeclHandle,
                                  NevercRecordDeclInfo *OutInfo) {
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, DeclHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Decl)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Record =
        dyn_cast<RecordDecl>(static_cast<const Decl *>(Value->Pointer));
    if (!Record)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);

    NevercRecordDeclInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    Info.Name = stringView(Record->getName());
    Info.FieldCount = static_cast<uint64_t>(
        std::distance(Record->field_begin(), Record->field_end()));
    Info.IsComplete =
        Record->isCompleteDefinition() ? NEVERC_TRUE : NEVERC_FALSE;
    Info.IsUnion = Record->isUnion() ? NEVERC_TRUE : NEVERC_FALSE;
    Info.HasFlexibleArrayMember =
        Record->hasFlexibleArrayMember() ? NEVERC_TRUE : NEVERC_FALSE;
    return writeCallerRecord(OutInfo, Info);
  }

  NevercStatus fillDeclRefExprInfo(NevercTaskHandle TaskHandle,
                                   NevercExprHandle ExpressionHandle,
                                   NevercDeclRefExprInfo *OutInfo) {
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, ExpressionHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Stmt)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Reference =
        dyn_cast<DeclRefExpr>(static_cast<const Stmt *>(Value->Pointer));
    if (!Reference)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);

    NevercDeclRefExprInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    auto ReferencedDecl = createDecl(Reference->getDecl());
    auto FoundDecl = createDecl(Reference->getFoundDecl());
    auto TypeHandle = createType(Reference->getType());
    if (!ReferencedDecl || !FoundDecl || !TypeHandle) {
      if (!ReferencedDecl)
        consumeError(ReferencedDecl.takeError());
      if (!FoundDecl)
        consumeError(FoundDecl.takeError());
      if (!TypeHandle)
        consumeError(TypeHandle.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.ReferencedDecl = *ReferencedDecl;
    Info.FoundDecl = *FoundDecl;
    Info.Type = *TypeHandle;
    return writeCallerRecord(OutInfo, Info);
  }

  NevercStatus fillCallExprInfo(NevercTaskHandle TaskHandle,
                                NevercExprHandle ExpressionHandle,
                                NevercCallExprInfo *OutInfo) {
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, ExpressionHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Stmt)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Call =
        dyn_cast<CallExpr>(static_cast<const Stmt *>(Value->Pointer));
    if (!Call)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);

    NevercCallExprInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    Info.ArgumentCount = Call->getNumArgs();
    auto Callee = createStmt(Call->getCallee());
    auto TypeHandle = createType(Call->getType());
    if (!Callee || !TypeHandle) {
      if (!Callee)
        consumeError(Callee.takeError());
      if (!TypeHandle)
        consumeError(TypeHandle.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.Callee = *Callee;
    Info.Type = *TypeHandle;
    if (const FunctionDecl *DirectCallee = Call->getDirectCallee()) {
      auto DirectCalleeHandle = createDecl(DirectCallee);
      if (!DirectCalleeHandle) {
        consumeError(DirectCalleeHandle.takeError());
        return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }
      Info.DirectCallee = *DirectCalleeHandle;
    }
    return writeCallerRecord(OutInfo, Info);
  }

  NevercStatus fillBinaryOperatorInfo(NevercTaskHandle TaskHandle,
                                      NevercExprHandle ExpressionHandle,
                                      NevercBinaryOperatorInfo *OutInfo) {
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, ExpressionHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Stmt)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Binary =
        dyn_cast<BinaryOperator>(static_cast<const Stmt *>(Value->Pointer));
    if (!Binary)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);

    NevercBinaryOperatorInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    Info.Operator = stringView(Binary->getOpcodeStr());
    Info.Kind = stableBinaryOperatorKind(Binary->getOpcode());
    auto Left = createStmt(Binary->getLHS());
    auto Right = createStmt(Binary->getRHS());
    auto TypeHandle = createType(Binary->getType());
    if (!Left || !Right || !TypeHandle) {
      if (!Left)
        consumeError(Left.takeError());
      if (!Right)
        consumeError(Right.takeError());
      if (!TypeHandle)
        consumeError(TypeHandle.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.Left = *Left;
    Info.Right = *Right;
    Info.Type = *TypeHandle;
    return writeCallerRecord(OutInfo, Info);
  }

  NevercStatus fillCompoundStmtInfo(NevercTaskHandle TaskHandle,
                                    NevercStmtHandle StatementHandle,
                                    NevercCompoundStmtInfo *OutInfo) {
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, StatementHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Stmt)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Compound =
        dyn_cast<CompoundStmt>(static_cast<const Stmt *>(Value->Pointer));
    if (!Compound)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);

    NevercCompoundStmtInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    Info.StatementCount = Compound->size();
    return writeCallerRecord(OutInfo, Info);
  }

  NevercStatus fillIntegerLiteralInfo(NevercTaskHandle TaskHandle,
                                      NevercExprHandle ExpressionHandle,
                                      NevercIntegerLiteralInfo *OutInfo) {
    Entity *Value = nullptr;
    NevercStatus Status = resolve(TaskHandle, ExpressionHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Stmt)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Literal =
        dyn_cast<IntegerLiteral>(static_cast<const Stmt *>(Value->Pointer));
    if (!Literal)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);

    NevercIntegerLiteralInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    const APInt Integer = Literal->getValue();
    Info.BitWidth = Integer.getBitWidth();
    Info.WordCount = Integer.getNumWords();
    auto TypeHandle = createType(Literal->getType());
    if (!TypeHandle) {
      consumeError(TypeHandle.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.Type = *TypeHandle;
    return writeCallerRecord(OutInfo, Info);
  }

  NevercStatus commitBuilder(NevercTaskHandle TaskHandle,
                             BuilderPayload &Builder,
                             NevercASTNodeHandle *OutNode) {
    if (!OutNode)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutNode = {};
    if (!validTask(TaskHandle) || Builder.Owner != this)
      return astStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Builder.Committed)
      return astStatus(NEVERC_STATUS_INVALID_STATE);

    const auto RangeProperty =
        Builder.Properties.find(NEVERC_AST_PROPERTY_AST_SOURCE_RANGE);
    if (RangeProperty == Builder.Properties.end() ||
        RangeProperty->second.Type != NEVERC_AST_VALUE_SOURCE_RANGE ||
        neverc_handle_is_null(RangeProperty->second.SourceRangeValue))
      return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);

    CharSourceRange NativeRange;
    NevercStatus Status = Locations.resolvePublishedRange(
        TaskHandle, RangeProperty->second.SourceRangeValue, &NativeRange);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (NativeRange.isInvalid())
      return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);

    if (Builder.Kind == NEVERC_DECL_KIND_EMPTY) {
      TranslationUnitDecl *TranslationUnit = Context.getTranslationUnitDecl();
      EmptyDecl *Created =
          EmptyDecl::Create(Context, TranslationUnit, NativeRange.getBegin());
      TranslationUnit->addDecl(Created);
      Status = publish(createDecl(Created), OutNode);
      if (Status.Code == NEVERC_STATUS_OK)
        indexDecl(Created);
      if (Status.Code == NEVERC_STATUS_OK)
        Builder.Committed = true;
      return Status;
    }

    if (Builder.Kind == NEVERC_STMT_KIND_NULL_STMT) {
      Stmt *Created = new (Context) NullStmt(NativeRange.getBegin());
      Status = publish(createStmt(Created), OutNode);
      if (Status.Code == NEVERC_STATUS_OK)
        indexStmt(Created, nullptr, nullptr);
      if (Status.Code == NEVERC_STATUS_OK)
        Builder.Committed = true;
      return Status;
    }

    const auto TypeProperty =
        Builder.Properties.find(NEVERC_AST_PROPERTY_STMT_EXPR_TYPE);
    if (TypeProperty == Builder.Properties.end() ||
        TypeProperty->second.Type != NEVERC_AST_VALUE_TYPE ||
        neverc_handle_is_null(TypeProperty->second.NodeValue))
      return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);

    Entity *TypeEntity = nullptr;
    Status = resolve(TaskHandle, TypeProperty->second.NodeValue, &TypeEntity);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (TypeEntity->Kind != Domain::Type)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const QualType LiteralType = TypeEntity->QualifiedType;

    Stmt *Created = nullptr;
    if (Builder.Kind == NEVERC_STMT_KIND_INTEGER_LITERAL) {
      if (!Builder.HasIntegerValue || !LiteralType->isIntegerType() ||
          Context.getIntWidth(LiteralType) != Builder.IntegerBitWidth)
        return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      const APInt Integer(Builder.IntegerBitWidth,
                          ArrayRef<uint64_t>(Builder.IntegerWords.data(),
                                             Builder.IntegerWords.size()));
      Created = IntegerLiteral::Create(Context, Integer, LiteralType,
                                       NativeRange.getBegin());
    } else if (Builder.Kind == NEVERC_STMT_KIND_BINARY_OPERATOR) {
      if (!Builder.HasBinaryOperatorKind)
        return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      const auto LeftChildren =
          Builder.Children.find(NEVERC_AST_CHILD_SLOT_STMT_BINARY_OPERATOR_LHS);
      const auto RightChildren =
          Builder.Children.find(NEVERC_AST_CHILD_SLOT_STMT_BINARY_OPERATOR_RHS);
      if (LeftChildren == Builder.Children.end() ||
          LeftChildren->second.size() != 1 ||
          RightChildren == Builder.Children.end() ||
          RightChildren->second.size() != 1)
        return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);

      Entity *LeftEntity = nullptr;
      Entity *RightEntity = nullptr;
      Status = resolve(TaskHandle, LeftChildren->second.front(), &LeftEntity);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
      Status = resolve(TaskHandle, RightChildren->second.front(), &RightEntity);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
      if (LeftEntity->Kind != Domain::Stmt || RightEntity->Kind != Domain::Stmt)
        return astStatus(NEVERC_STATUS_WRONG_TYPE);
      auto *Left = dyn_cast<Expr>(
          const_cast<Stmt *>(static_cast<const Stmt *>(LeftEntity->Pointer)));
      auto *Right = dyn_cast<Expr>(
          const_cast<Stmt *>(static_cast<const Stmt *>(RightEntity->Pointer)));
      if (!Left || !Right || Left == Right || StmtParents.count(Left) != 0 ||
          StmtDeclParents.count(Left) != 0 || StmtParents.count(Right) != 0 ||
          StmtDeclParents.count(Right) != 0)
        return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      BinaryOperatorKind Operator;
      if (!nativeBinaryOperatorKind(Builder.BinaryOperatorKind, &Operator))
        return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      Created = BinaryOperator::Create(
          Context, Left, Right, Operator, LiteralType, VK_PRValue, OK_Ordinary,
          NativeRange.getBegin(), FPOptionsOverride());
    } else {
      return astStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    }

    Status = publish(createStmt(Created), OutNode);
    if (Status.Code == NEVERC_STATUS_OK)
      indexStmt(Created, nullptr, nullptr);
    if (Status.Code == NEVERC_STATUS_OK)
      Builder.Committed = true;
    return Status;
  }

  NevercStatus commitMutation(NevercTaskHandle TaskHandle,
                              MutationPayload &Mutation) {
    if (!validTask(TaskHandle) || Mutation.Owner != this)
      return astStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Mutation.State != MutationState::Open)
      return astStatus(NEVERC_STATUS_INVALID_STATE);

    struct ResolvedReplacement {
      VarDecl *Parent = nullptr;
      Expr *Previous = nullptr;
      Expr *Replacement = nullptr;
    };
    SmallVector<ResolvedReplacement, 4> Resolved;
    Resolved.reserve(Mutation.Replacements.size());

    std::lock_guard<std::mutex> Lock(MutationMutex);
    for (size_t OperationIndex = 0;
         OperationIndex != Mutation.Replacements.size(); ++OperationIndex) {
      const ReplaceChildOperation &Operation =
          Mutation.Replacements[OperationIndex];
      if (Operation.Slot != NEVERC_AST_CHILD_SLOT_DECL_VAR_INITIALIZER ||
          Operation.Index != 0)
        return astStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
      for (size_t Earlier = 0; Earlier != OperationIndex; ++Earlier) {
        const ReplaceChildOperation &Previous = Mutation.Replacements[Earlier];
        if (sameHandle(Previous.Parent, Operation.Parent) &&
            Previous.Slot == Operation.Slot &&
            Previous.Index == Operation.Index)
          return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }

      Entity *ParentEntity = nullptr;
      Entity *ReplacementEntity = nullptr;
      NevercStatus Status =
          resolve(TaskHandle, Operation.Parent, &ParentEntity);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
      Status = resolve(TaskHandle, Operation.Replacement, &ReplacementEntity);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
      if (ParentEntity->Kind != Domain::Decl ||
          ReplacementEntity->Kind != Domain::Stmt)
        return astStatus(NEVERC_STATUS_WRONG_TYPE);
      auto *Parent = dyn_cast<VarDecl>(
          const_cast<Decl *>(static_cast<const Decl *>(ParentEntity->Pointer)));
      auto *Replacement = dyn_cast<Expr>(const_cast<Stmt *>(
          static_cast<const Stmt *>(ReplacementEntity->Pointer)));
      if (!Parent || !Replacement)
        return astStatus(NEVERC_STATUS_WRONG_TYPE);
      Expr *Previous = Parent->getInit();
      if (!Previous || Replacement == Previous ||
          !Context.hasSameType(Parent->getType(), Replacement->getType()) ||
          StmtParents.count(Replacement) != 0 ||
          StmtDeclParents.count(Replacement) != 0)
        return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      auto ExistingParent = StmtDeclParents.find(Previous);
      if (ExistingParent == StmtDeclParents.end() ||
          ExistingParent->second != Parent)
        return astStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      Resolved.push_back({Parent, Previous, Replacement});
    }

    for (const ResolvedReplacement &Operation : Resolved) {
      Operation.Parent->setInit(Operation.Replacement);
      StmtDeclParents.erase(Operation.Previous);
      StmtDeclParents[Operation.Replacement] = Operation.Parent;
    }
    Mutation.State = MutationState::Committed;
    return neverc_status_ok();
  }

  NevercStatus fillBuiltinType(NevercTaskHandle TaskHandle,
                               NevercBuiltinTypeKind Kind,
                               NevercTypeHandle *OutType) {
    if (!OutType)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutType = {};
    if (!validTask(TaskHandle))
      return astStatus(NEVERC_STATUS_WRONG_SCOPE);

    QualType Type;
    switch (Kind) {
    case NEVERC_BUILTIN_TYPE_VOID:
      Type = Context.VoidTy;
      break;
    case NEVERC_BUILTIN_TYPE_BOOL:
      Type = Context.BoolTy;
      break;
    case NEVERC_BUILTIN_TYPE_CHAR:
      Type = Context.CharTy;
      break;
    case NEVERC_BUILTIN_TYPE_SIGNED_CHAR:
      Type = Context.SignedCharTy;
      break;
    case NEVERC_BUILTIN_TYPE_UNSIGNED_CHAR:
      Type = Context.UnsignedCharTy;
      break;
    case NEVERC_BUILTIN_TYPE_SHORT:
      Type = Context.ShortTy;
      break;
    case NEVERC_BUILTIN_TYPE_UNSIGNED_SHORT:
      Type = Context.UnsignedShortTy;
      break;
    case NEVERC_BUILTIN_TYPE_INT:
      Type = Context.IntTy;
      break;
    case NEVERC_BUILTIN_TYPE_UNSIGNED_INT:
      Type = Context.UnsignedIntTy;
      break;
    case NEVERC_BUILTIN_TYPE_LONG:
      Type = Context.LongTy;
      break;
    case NEVERC_BUILTIN_TYPE_UNSIGNED_LONG:
      Type = Context.UnsignedLongTy;
      break;
    case NEVERC_BUILTIN_TYPE_LONG_LONG:
      Type = Context.LongLongTy;
      break;
    case NEVERC_BUILTIN_TYPE_UNSIGNED_LONG_LONG:
      Type = Context.UnsignedLongLongTy;
      break;
    case NEVERC_BUILTIN_TYPE_FLOAT:
      Type = Context.FloatTy;
      break;
    case NEVERC_BUILTIN_TYPE_DOUBLE:
      Type = Context.DoubleTy;
      break;
    case NEVERC_BUILTIN_TYPE_LONG_DOUBLE:
      Type = Context.LongDoubleTy;
      break;
    default:
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    return publish(createType(Type), OutType);
  }

  static Impl *impl(void *Context) { return static_cast<Impl *>(Context); }

  static NevercStatus NEVERC_CALL getTranslationUnit(
      void *Context, NevercTaskHandle Task, NevercDeclHandle *OutDecl) {
    if (!Context || !OutDecl)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Impl &Bridge = *impl(Context);
    if (!Bridge.validTask(Task))
      return astStatus(NEVERC_STATUS_WRONG_SCOPE);
    return Bridge.publish(
        Bridge.createDecl(Bridge.Context.getTranslationUnitDecl()), OutDecl);
  }

  static NevercStatus NEVERC_CALL getBuiltinType(void *Context,
                                                 NevercTaskHandle Task,
                                                 NevercBuiltinTypeKind Kind,
                                                 NevercTypeHandle *OutType) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillBuiltinType(Task, Kind, OutType);
  }

  static NevercStatus NEVERC_CALL getNodeInfo(void *Context,
                                              NevercTaskHandle Task,
                                              NevercASTNodeHandle Node,
                                              NevercASTNodeInfo *OutInfo) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillNodeInfo(Task, Node, OutInfo);
  }

  static NevercStatus NEVERC_CALL getNodeInfoBatch(
      void *Context, NevercTaskHandle Task, const NevercASTNodeHandle *Nodes,
      uint64_t NodeCount, NevercASTNodeInfo *OutInfos, uint64_t OutInfoCapacity,
      uint64_t OutInfoStride) {
    if (!Context || (NodeCount != 0 && (!Nodes || !OutInfos)) ||
        OutInfoCapacity < NodeCount ||
        OutInfoStride < sizeof(NevercASTNodeInfo) ||
        OutInfoStride > std::numeric_limits<size_t>::max() ||
        NodeCount > std::numeric_limits<size_t>::max() /
                        static_cast<size_t>(OutInfoStride))
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    for (uint64_t Index = 0; Index != NodeCount; ++Index) {
      auto *Out = reinterpret_cast<NevercASTNodeInfo *>(
          reinterpret_cast<unsigned char *>(OutInfos) +
          static_cast<size_t>(Index * OutInfoStride));
      NevercStatus Status =
          impl(Context)->fillNodeInfo(Task, Nodes[Index], Out);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
    }
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getProperty(void *Context,
                                              NevercTaskHandle Task,
                                              NevercASTNodeHandle Node,
                                              NevercASTPropertyID Property,
                                              NevercASTValue *OutValue) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillProperty(Task, Node, Property, OutValue);
  }

  static NevercStatus NEVERC_CALL getPropertyBatch(
      void *Context, NevercTaskHandle Task, const NevercASTNodeHandle *Nodes,
      const NevercASTPropertyID *Properties, uint64_t QueryCount,
      NevercASTValue *OutValues, uint64_t OutValueCapacity,
      uint64_t OutValueStride) {
    if (!Context ||
        (QueryCount != 0 && (!Nodes || !Properties || !OutValues)) ||
        OutValueCapacity < QueryCount ||
        OutValueStride < sizeof(NevercASTValue) ||
        OutValueStride > std::numeric_limits<size_t>::max() ||
        QueryCount > std::numeric_limits<size_t>::max() /
                         static_cast<size_t>(OutValueStride))
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    for (uint64_t Index = 0; Index != QueryCount; ++Index) {
      auto *Out = reinterpret_cast<NevercASTValue *>(
          reinterpret_cast<unsigned char *>(OutValues) +
          static_cast<size_t>(Index * OutValueStride));
      NevercStatus Status = impl(Context)->fillProperty(Task, Nodes[Index],
                                                        Properties[Index], Out);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
    }
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getChildCount(void *Context,
                                                NevercTaskHandle Task,
                                                NevercASTNodeHandle Node,
                                                uint64_t *OutCount) {
    if (!Context || !OutCount)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutCount = 0;
    Impl &Bridge = *impl(Context);
    Entity *Value = nullptr;
    NevercStatus Status = Bridge.resolve(Task, Node, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    *OutCount = Bridge.childEntities(*Value).size();
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getChild(void *Context, NevercTaskHandle Task,
                                           NevercASTNodeHandle Node,
                                           uint64_t Index,
                                           NevercASTNodeHandle *OutChild) {
    if (!Context || !OutChild)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutChild = {};
    Impl &Bridge = *impl(Context);
    Entity *Value = nullptr;
    NevercStatus Status = Bridge.resolve(Task, Node, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    SmallVector<Entity, 16> Children = Bridge.childEntities(*Value);
    if (Index >= Children.size())
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    const Entity &Child = Children[static_cast<size_t>(Index)];
    return Bridge.publish(
        Bridge.createEntity(Child.Kind, Child.Pointer, Child.QualifiedType),
        OutChild);
  }

  static NevercStatus NEVERC_CALL getParent(void *Context,
                                            NevercTaskHandle Task,
                                            NevercASTNodeHandle Node,
                                            NevercASTNodeHandle *OutParent) {
    if (!Context || !OutParent)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutParent = {};
    Impl &Bridge = *impl(Context);
    Entity *Value = nullptr;
    NevercStatus Status = Bridge.resolve(Task, Node, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return Bridge.publish(Bridge.parent(*Value), OutParent);
  }

  static NevercStatus NEVERC_CALL
  getDeclContext(void *Context, NevercTaskHandle Task, NevercDeclHandle Decl,
                 NevercDeclContextHandle *OutContext) {
    if (!Context || !OutContext)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutContext = {};
    Impl &Bridge = *impl(Context);
    Entity *Value = nullptr;
    NevercStatus Status = Bridge.resolve(Task, Decl, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Decl)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Declaration = static_cast<const neverc::Decl *>(Value->Pointer);
    const DeclContext *DC = Declaration->getDeclContext();
    if (!DC)
      return neverc_status_ok();
    return Bridge.publish(Bridge.createDecl(Decl::castFromDeclContext(DC)),
                          OutContext);
  }

  static NevercStatus NEVERC_CALL getTypeInfo(void *Context,
                                              NevercTaskHandle Task,
                                              NevercTypeHandle TypeHandle,
                                              NevercTypeInfo *OutInfo) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillTypeInfo(Task, TypeHandle, OutInfo);
  }

  static NevercStatus NEVERC_CALL getTypeElement(void *Context,
                                                 NevercTaskHandle Task,
                                                 NevercTypeHandle TypeHandle,
                                                 uint64_t Index,
                                                 NevercTypeHandle *OutElement) {
    if (!Context || !OutElement)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutElement = {};
    Impl &Bridge = *impl(Context);
    Entity *Value = nullptr;
    NevercStatus Status = Bridge.resolve(Task, TypeHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Type)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);

    const Type *TypeValue = Value->QualifiedType.getTypePtr();
    QualType Element;
    if (const auto *Function = dyn_cast<FunctionProtoType>(TypeValue)) {
      if (Index >= Function->getNumParams())
        return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Element = Function->getParamType(static_cast<unsigned>(Index));
    } else {
      if (Index != 0)
        return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      if (const auto *Pointer = dyn_cast<PointerType>(TypeValue))
        Element = Pointer->getPointeeType();
      else if (const auto *Array = dyn_cast<ArrayType>(TypeValue))
        Element = Array->getElementType();
      else if (const auto *Vector = dyn_cast<VectorType>(TypeValue))
        Element = Vector->getElementType();
      else if (const auto *Matrix = dyn_cast<ConstantMatrixType>(TypeValue))
        Element = Matrix->getElementType();
      else if (const auto *Complex = dyn_cast<ComplexType>(TypeValue))
        Element = Complex->getElementType();
      else if (const auto *Atomic = dyn_cast<AtomicType>(TypeValue))
        Element = Atomic->getValueType();
      else
        return astStatus(NEVERC_STATUS_WRONG_TYPE);
    }
    return Bridge.publish(Bridge.createType(Element), OutElement);
  }

  static NevercStatus NEVERC_CALL
  getFunctionDeclInfo(void *Context, NevercTaskHandle Task,
                      NevercDeclHandle Decl, NevercFunctionDeclInfo *OutInfo) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillFunctionDeclInfo(Task, Decl, OutInfo);
  }

  static NevercStatus NEVERC_CALL getFunctionDeclParameter(
      void *Context, NevercTaskHandle Task, NevercDeclHandle Decl,
      uint64_t Index, NevercDeclHandle *OutParameter) {
    if (!Context || !OutParameter)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutParameter = {};
    Impl &Bridge = *impl(Context);
    Entity *Value = nullptr;
    NevercStatus Status = Bridge.resolve(Task, Decl, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Decl)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Function = dyn_cast<FunctionDecl>(
        static_cast<const neverc::Decl *>(Value->Pointer));
    if (!Function)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    if (Index >= Function->getNumParams())
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return Bridge.publish(
        Bridge.createDecl(Function->getParamDecl(static_cast<unsigned>(Index))),
        OutParameter);
  }

  static NevercStatus NEVERC_CALL
  getDeclAttributeCount(void *Context, NevercTaskHandle Task,
                        NevercDeclHandle DeclHandle, uint64_t *OutCount) {
    if (!Context || !OutCount)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutCount = 0;
    Impl &Bridge = *impl(Context);
    Entity *Value = nullptr;
    NevercStatus Status = Bridge.resolve(Task, DeclHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Decl)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Declaration = static_cast<const neverc::Decl *>(Value->Pointer);
    *OutCount = static_cast<uint64_t>(
        std::distance(Declaration->attr_begin(), Declaration->attr_end()));
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getDeclAttribute(
      void *Context, NevercTaskHandle Task, NevercDeclHandle DeclHandle,
      uint64_t Index, NevercAttrHandle *OutAttribute) {
    if (!Context || !OutAttribute)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutAttribute = {};
    Impl &Bridge = *impl(Context);
    Entity *Value = nullptr;
    NevercStatus Status = Bridge.resolve(Task, DeclHandle, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Decl)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Declaration = static_cast<const neverc::Decl *>(Value->Pointer);
    const uint64_t Count = static_cast<uint64_t>(
        std::distance(Declaration->attr_begin(), Declaration->attr_end()));
    if (Index >= Count)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    const Attr *Attribute =
        *(Declaration->attr_begin() + static_cast<ptrdiff_t>(Index));
    return Bridge.publish(Bridge.createAttr(Attribute), OutAttribute);
  }

  static NevercStatus NEVERC_CALL getAttrInfo(void *Context,
                                              NevercTaskHandle Task,
                                              NevercAttrHandle Attribute,
                                              NevercAttrInfo *OutInfo) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillAttrInfo(Task, Attribute, OutInfo);
  }

  static NevercStatus NEVERC_CALL getVarDeclInfo(void *Context,
                                                 NevercTaskHandle Task,
                                                 NevercDeclHandle Decl,
                                                 NevercVarDeclInfo *OutInfo) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillVarDeclInfo(Task, Decl, OutInfo);
  }

  static NevercStatus NEVERC_CALL
  getRecordDeclInfo(void *Context, NevercTaskHandle Task, NevercDeclHandle Decl,
                    NevercRecordDeclInfo *OutInfo) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillRecordDeclInfo(Task, Decl, OutInfo);
  }

  static NevercStatus NEVERC_CALL getDeclRefExprInfo(
      void *Context, NevercTaskHandle Task, NevercExprHandle Expression,
      NevercDeclRefExprInfo *OutInfo) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillDeclRefExprInfo(Task, Expression, OutInfo);
  }

  static NevercStatus NEVERC_CALL getCallExprInfo(void *Context,
                                                  NevercTaskHandle Task,
                                                  NevercExprHandle Expression,
                                                  NevercCallExprInfo *OutInfo) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillCallExprInfo(Task, Expression, OutInfo);
  }

  static NevercStatus NEVERC_CALL getCallExprArgument(
      void *Context, NevercTaskHandle Task, NevercExprHandle Expression,
      uint64_t Index, NevercExprHandle *OutArgument) {
    if (!Context || !OutArgument)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutArgument = {};
    Impl &Bridge = *impl(Context);
    Entity *Value = nullptr;
    NevercStatus Status = Bridge.resolve(Task, Expression, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Stmt)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Call =
        dyn_cast<CallExpr>(static_cast<const Stmt *>(Value->Pointer));
    if (!Call)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    if (Index >= Call->getNumArgs())
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return Bridge.publish(
        Bridge.createStmt(Call->getArg(static_cast<unsigned>(Index))),
        OutArgument);
  }

  static NevercStatus NEVERC_CALL getBinaryOperatorInfo(
      void *Context, NevercTaskHandle Task, NevercExprHandle Expression,
      NevercBinaryOperatorInfo *OutInfo) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillBinaryOperatorInfo(Task, Expression, OutInfo);
  }

  static NevercStatus NEVERC_CALL getCompoundStmtInfo(
      void *Context, NevercTaskHandle Task, NevercStmtHandle Statement,
      NevercCompoundStmtInfo *OutInfo) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillCompoundStmtInfo(Task, Statement, OutInfo);
  }

  static NevercStatus NEVERC_CALL getIntegerLiteralInfo(
      void *Context, NevercTaskHandle Task, NevercExprHandle Expression,
      NevercIntegerLiteralInfo *OutInfo) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return impl(Context)->fillIntegerLiteralInfo(Task, Expression, OutInfo);
  }

  static NevercStatus NEVERC_CALL getIntegerLiteralWord(
      void *Context, NevercTaskHandle Task, NevercExprHandle Expression,
      uint64_t Index, uint64_t *OutWord) {
    if (!Context || !OutWord)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutWord = 0;
    Impl &Bridge = *impl(Context);
    Entity *Value = nullptr;
    NevercStatus Status = Bridge.resolve(Task, Expression, &Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Value->Kind != Domain::Stmt)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const auto *Literal =
        dyn_cast<IntegerLiteral>(static_cast<const Stmt *>(Value->Pointer));
    if (!Literal)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    const APInt Integer = Literal->getValue();
    if (Index >= Integer.getNumWords())
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutWord = Integer.getRawData()[static_cast<size_t>(Index)];
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL
  createASTBuilder(void *Context, NevercTaskHandle Task, NevercASTNodeKind Kind,
                   NevercASTBuilderHandle *OutBuilder) {
    if (!Context || !OutBuilder)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutBuilder = {};
    Impl &Bridge = *impl(Context);
    if (!Bridge.validTask(Task))
      return astStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Kind != NEVERC_DECL_KIND_EMPTY && Kind != NEVERC_STMT_KIND_NULL_STMT &&
        Kind != NEVERC_STMT_KIND_INTEGER_LITERAL &&
        Kind != NEVERC_STMT_KIND_BINARY_OPERATOR)
      return astStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    auto *Payload = new (std::nothrow) BuilderPayload;
    if (!Payload)
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    Payload->Owner = &Bridge;
    Payload->Kind = Kind;
    auto Created = Bridge.Task.handles().create(
        PluginASTBuilderHandleKind, Payload,
        [](void *Value) { delete static_cast<BuilderPayload *>(Value); });
    if (!Created) {
      delete Payload;
      consumeError(Created.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutBuilder = *Created;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL astBuilderSetProperty(
      void *Context, NevercTaskHandle Task,
      NevercASTBuilderHandle BuilderHandle, NevercASTPropertyID Property,
      const NevercASTValue *Value) {
    if (!Context || !Value ||
        !validASTInputHeader(Value->Header, sizeof(*Value)) ||
        Value->Reserved != 0)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Impl &Bridge = *impl(Context);
    BuilderPayload *Builder = nullptr;
    NevercStatus Status = Bridge.resolveBuilder(Task, BuilderHandle, &Builder);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Builder->Committed)
      return astStatus(NEVERC_STATUS_INVALID_STATE);
    if (Property == NEVERC_AST_PROPERTY_STMT_EXPR_TYPE) {
      if (Value->Type != NEVERC_AST_VALUE_TYPE ||
          neverc_handle_is_null(Value->NodeValue))
        return astStatus(NEVERC_STATUS_WRONG_TYPE);
    } else if (Property == NEVERC_AST_PROPERTY_AST_SOURCE_RANGE) {
      if (Value->Type != NEVERC_AST_VALUE_SOURCE_RANGE ||
          neverc_handle_is_null(Value->SourceRangeValue))
        return astStatus(NEVERC_STATUS_WRONG_TYPE);
    } else {
      return astStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    }
    Builder->Properties[Property] = *Value;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL astBuilderSetIntegerValue(
      void *Context, NevercTaskHandle Task,
      NevercASTBuilderHandle BuilderHandle, const NevercAPIntView *Value) {
    constexpr uint32_t MaximumIntegerBitWidth = UINT32_C(1) << 20;
    if (!Context || !Value ||
        !validASTInputHeader(Value->Header, sizeof(*Value)) ||
        Value->Reserved != 0 || Value->BitWidth == 0 ||
        Value->BitWidth > MaximumIntegerBitWidth)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    const uint64_t RequiredWords =
        static_cast<uint64_t>(APInt::getNumWords(Value->BitWidth));
    if (Value->WordCount != RequiredWords ||
        (RequiredWords != 0 && !Value->Words) ||
        RequiredWords > std::numeric_limits<size_t>::max())
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (const uint32_t PartialBits = Value->BitWidth % 64;
        PartialBits != 0 &&
        (Value->Words[RequiredWords - 1] >> PartialBits) != 0)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);

    Impl &Bridge = *impl(Context);
    BuilderPayload *Builder = nullptr;
    NevercStatus Status = Bridge.resolveBuilder(Task, BuilderHandle, &Builder);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Builder->Committed)
      return astStatus(NEVERC_STATUS_INVALID_STATE);
    if (Builder->Kind != NEVERC_STMT_KIND_INTEGER_LITERAL)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    Builder->IntegerWords.assign(
        Value->Words, Value->Words + static_cast<size_t>(RequiredWords));
    Builder->IntegerBitWidth = Value->BitWidth;
    Builder->HasIntegerValue = true;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL astBuilderSetBinaryOperatorKind(
      void *Context, NevercTaskHandle Task,
      NevercASTBuilderHandle BuilderHandle, NevercBinaryOperatorKind Kind) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    BinaryOperatorKind NativeKind;
    if (!nativeBinaryOperatorKind(Kind, &NativeKind))
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Impl &Bridge = *impl(Context);
    BuilderPayload *Builder = nullptr;
    NevercStatus Status = Bridge.resolveBuilder(Task, BuilderHandle, &Builder);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Builder->Committed)
      return astStatus(NEVERC_STATUS_INVALID_STATE);
    if (Builder->Kind != NEVERC_STMT_KIND_BINARY_OPERATOR)
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    Builder->BinaryOperatorKind = Kind;
    Builder->HasBinaryOperatorKind = true;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL astBuilderSetChild(
      void *Context, NevercTaskHandle Task,
      NevercASTBuilderHandle BuilderHandle, NevercASTChildSlotID Slot,
      uint64_t Index, NevercASTNodeHandle Child) {
    if (!Context || neverc_handle_is_null(Child))
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Impl &Bridge = *impl(Context);
    BuilderPayload *Builder = nullptr;
    NevercStatus Status = Bridge.resolveBuilder(Task, BuilderHandle, &Builder);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Builder->Committed)
      return astStatus(NEVERC_STATUS_INVALID_STATE);
    if (Builder->Kind != NEVERC_STMT_KIND_BINARY_OPERATOR ||
        (Slot != NEVERC_AST_CHILD_SLOT_STMT_BINARY_OPERATOR_LHS &&
         Slot != NEVERC_AST_CHILD_SLOT_STMT_BINARY_OPERATOR_RHS))
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    if (Index != 0)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);

    Entity *ChildEntity = nullptr;
    Status = Bridge.resolve(Task, Child, &ChildEntity);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (ChildEntity->Kind != Domain::Stmt ||
        !isa<Expr>(static_cast<const Stmt *>(ChildEntity->Pointer)))
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    auto &Children = Builder->Children[Slot];
    if (Children.empty())
      Children.push_back(Child);
    else
      Children.front() = Child;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL astBuilderCommit(
      void *Context, NevercTaskHandle Task,
      NevercASTBuilderHandle BuilderHandle, NevercASTNodeHandle *OutNode) {
    if (!Context || !OutNode)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutNode = {};
    Impl &Bridge = *impl(Context);
    BuilderPayload *Builder = nullptr;
    NevercStatus Status = Bridge.resolveBuilder(Task, BuilderHandle, &Builder);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return Bridge.commitBuilder(Task, *Builder, OutNode);
  }

  static NevercStatus NEVERC_CALL
  destroyASTBuilder(void *Context, NevercTaskHandle Task,
                    NevercASTBuilderHandle BuilderHandle) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Impl &Bridge = *impl(Context);
    BuilderPayload *Builder = nullptr;
    NevercStatus Status = Bridge.resolveBuilder(Task, BuilderHandle, &Builder);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return Bridge.Task.handles().release(BuilderHandle,
                                         PluginASTBuilderHandleKind);
  }

  static NevercStatus NEVERC_CALL
  beginASTMutation(void *Context, NevercTaskHandle Task,
                   NevercASTMutationHandle *OutMutation) {
    if (!Context || !OutMutation)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutMutation = {};
    Impl &Bridge = *impl(Context);
    if (!Bridge.validTask(Task))
      return astStatus(NEVERC_STATUS_WRONG_SCOPE);
    auto *Payload = new (std::nothrow) MutationPayload;
    if (!Payload)
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    Payload->Owner = &Bridge;
    auto Created = Bridge.Task.handles().create(
        PluginASTMutationHandleKind, Payload,
        [](void *Value) { delete static_cast<MutationPayload *>(Value); });
    if (!Created) {
      delete Payload;
      consumeError(Created.takeError());
      return astStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutMutation = *Created;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL
  astMutationReplaceChild(void *Context, NevercTaskHandle Task,
                          NevercASTMutationHandle MutationHandle,
                          NevercASTNodeHandle Parent, NevercASTChildSlotID Slot,
                          uint64_t Index, NevercASTNodeHandle Replacement) {
    if (!Context || neverc_handle_is_null(Parent) ||
        neverc_handle_is_null(Replacement))
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Impl &Bridge = *impl(Context);
    MutationPayload *Mutation = nullptr;
    NevercStatus Status =
        Bridge.resolveMutation(Task, MutationHandle, &Mutation);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Mutation->State != MutationState::Open)
      return astStatus(NEVERC_STATUS_INVALID_STATE);
    if (Slot != NEVERC_AST_CHILD_SLOT_DECL_VAR_INITIALIZER)
      return astStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    if (Index != 0)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Entity *ParentEntity = nullptr;
    Entity *ReplacementEntity = nullptr;
    Status = Bridge.resolve(Task, Parent, &ParentEntity);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Status = Bridge.resolve(Task, Replacement, &ReplacementEntity);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (ParentEntity->Kind != Domain::Decl ||
        !isa<VarDecl>(static_cast<const Decl *>(ParentEntity->Pointer)) ||
        ReplacementEntity->Kind != Domain::Stmt ||
        !isa<Expr>(static_cast<const Stmt *>(ReplacementEntity->Pointer)))
      return astStatus(NEVERC_STATUS_WRONG_TYPE);
    Mutation->Replacements.push_back({Parent, Slot, Index, Replacement});
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL
  commitASTMutation(void *Context, NevercTaskHandle Task,
                    NevercASTMutationHandle MutationHandle) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Impl &Bridge = *impl(Context);
    MutationPayload *Mutation = nullptr;
    NevercStatus Status =
        Bridge.resolveMutation(Task, MutationHandle, &Mutation);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return Bridge.commitMutation(Task, *Mutation);
  }

  static NevercStatus NEVERC_CALL
  abortASTMutation(void *Context, NevercTaskHandle Task,
                   NevercASTMutationHandle MutationHandle) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Impl &Bridge = *impl(Context);
    MutationPayload *Mutation = nullptr;
    NevercStatus Status =
        Bridge.resolveMutation(Task, MutationHandle, &Mutation);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Mutation->State != MutationState::Open)
      return astStatus(NEVERC_STATUS_INVALID_STATE);
    Mutation->Replacements.clear();
    Mutation->State = MutationState::Aborted;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL
  destroyASTMutation(void *Context, NevercTaskHandle Task,
                     NevercASTMutationHandle MutationHandle) {
    if (!Context)
      return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Impl &Bridge = *impl(Context);
    MutationPayload *Mutation = nullptr;
    NevercStatus Status =
        Bridge.resolveMutation(Task, MutationHandle, &Mutation);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return Bridge.Task.handles().release(MutationHandle,
                                         PluginASTMutationHandleKind);
  }
};

PluginASTBridge::PluginASTBridge(PluginTaskContext &Task, TreeContext &Context,
                                 FrontendPluginBridge &Locations,
                                 PluginPrepBridge *Identifiers)
    : State(std::make_unique<Impl>(Task, Context, Locations, Identifiers)) {}

PluginASTBridge::~PluginASTBridge() { detachProcessInterface(); }

const NevercASTAPI &PluginASTBridge::astAPI() const { return State->API; }

NevercStatus PluginASTBridge::resolvePublishedNode(
    NevercTaskHandle Task, NevercASTNodeHandle Node,
    NevercASTSchemaDomain ExpectedDomain, const void **OutNode) {
  return State->resolvePublishedNode(Task, Node, ExpectedDomain, OutNode);
}

NevercStatus PluginASTBridge::resolvePublishedType(NevercTaskHandle Task,
                                                   NevercTypeHandle Type,
                                                   QualType *OutType) {
  if (!OutType)
    return astStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutType = {};
  Impl::Entity *Value = nullptr;
  NevercStatus Status = State->resolve(Task, Type, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Value->Kind != Impl::Domain::Type)
    return astStatus(NEVERC_STATUS_WRONG_TYPE);
  *OutType = Value->QualifiedType;
  return neverc_status_ok();
}

Expected<NevercDeclHandle>
PluginASTBridge::publishDecl(const Decl *Declaration) {
  return State->createDecl(Declaration);
}

Expected<NevercStmtHandle>
PluginASTBridge::publishStmt(const Stmt *Statement) {
  return State->createStmt(Statement);
}

Expected<NevercExprHandle>
PluginASTBridge::publishExpr(const Expr *Expression) {
  return State->createStmt(Expression);
}

Expected<NevercTypeHandle> PluginASTBridge::publishType(QualType Type) {
  return State->createType(Type);
}

Error PluginASTBridge::attachProcessInterface() {
  if (AttachedToProcess)
    return Error::success();
  auto ProcessBridge = findASTProcessBridge(State->Task.processServices());
  if (!ProcessBridge)
    return createStringError(inconvertibleErrorCode(),
                             "plugin AST interface is not registered");
  if (Error E = ProcessBridge->attach(State->Task, *this))
    return E;
  AttachedToProcess = true;
  return Error::success();
}

void PluginASTBridge::detachProcessInterface() {
  if (!AttachedToProcess)
    return;
  if (auto ProcessBridge = findASTProcessBridge(State->Task.processServices()))
    ProcessBridge->detach(State->Task.handle());
  AttachedToProcess = false;
}

Error registerPluginASTInterface(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register plugin AST interface after interface freeze");
  auto Bridge = std::make_shared<PluginASTProcessBridge>();
  if (Error E = Services.registerHostService(astPluginInterfaceID(), Bridge))
    return E;
  return Services.interfaces().registerInterface(
      astPluginInterfaceID(), NEVERC_INTERFACE_STABLE, &Bridge->api(), {});
}

} // namespace neverc::plugin
