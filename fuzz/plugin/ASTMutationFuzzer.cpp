#include "PluginFrontendFuzzSupport.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>

using namespace llvm;
using namespace neverc;
using namespace neverc::fuzz;

namespace {

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

NevercDeclHandle findVariable(const NevercASTAPI &API,
                              NevercTaskHandle Task) {
  NevercDeclHandle Root{};
  if (API.GetTranslationUnit(API.Context, Task, &Root).Code !=
      NEVERC_STATUS_OK)
    return {};

  uint64_t ChildCount = 0;
  if (API.GetChildCount(API.Context, Task, Root, &ChildCount).Code !=
      NEVERC_STATUS_OK)
    return {};
  for (uint64_t Index = 0; Index != ChildCount; ++Index) {
    NevercASTNodeHandle Child{};
    if (API.GetChild(API.Context, Task, Root, Index, &Child).Code !=
        NEVERC_STATUS_OK)
      continue;
    NevercVarDeclInfo Variable{};
    Variable.Header.StructSize = sizeof(Variable);
    if (API.GetVarDeclInfo(API.Context, Task, Child, &Variable).Code ==
            NEVERC_STATUS_OK &&
        !neverc_handle_is_null(Variable.Initializer))
      return Child;
  }
  return {};
}

NevercExprHandle buildInteger(const NevercASTAPI &API,
                              NevercTaskHandle Task,
                              NevercDeclHandle VariableHandle,
                              uint64_t Word) {
  NevercVarDeclInfo Variable{};
  Variable.Header.StructSize = sizeof(Variable);
  if (API.GetVarDeclInfo(API.Context, Task, VariableHandle, &Variable).Code !=
      NEVERC_STATUS_OK)
    return {};
  NevercASTNodeInfo Node{};
  Node.Header.StructSize = sizeof(Node);
  if (API.GetNodeInfo(API.Context, Task, VariableHandle, &Node).Code !=
      NEVERC_STATUS_OK)
    return {};

  NevercASTBuilderHandle Builder{};
  if (API.CreateASTBuilder(API.Context, Task,
                           NEVERC_STMT_KIND_INTEGER_LITERAL, &Builder)
          .Code != NEVERC_STATUS_OK)
    return {};

  NevercStatus Status = neverc_status_ok();
  NevercASTValue Type{};
  Type.Header = {sizeof(Type), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Type.Type = NEVERC_AST_VALUE_TYPE;
  Type.NodeValue = Variable.Type;
  Status = API.ASTBuilderSetProperty(
      API.Context, Task, Builder, NEVERC_AST_PROPERTY_STMT_EXPR_TYPE, &Type);

  NevercASTValue Range{};
  Range.Header = {sizeof(Range), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Range.Type = NEVERC_AST_VALUE_SOURCE_RANGE;
  Range.SourceRangeValue = Node.SourceRange;
  if (Status.Code == NEVERC_STATUS_OK)
    Status = API.ASTBuilderSetProperty(
        API.Context, Task, Builder, NEVERC_AST_PROPERTY_AST_SOURCE_RANGE,
        &Range);

  NevercAPIntView Integer{};
  Integer.Header = {sizeof(Integer), NEVERC_AST_API_MAJOR,
                    NEVERC_AST_API_MINOR, 0};
  Integer.Words = &Word;
  Integer.WordCount = 1;
  Integer.BitWidth = 32;
  if (Status.Code == NEVERC_STATUS_OK)
    Status =
        API.ASTBuilderSetIntegerValue(API.Context, Task, Builder, &Integer);

  NevercExprHandle Result{};
  if (Status.Code == NEVERC_STATUS_OK)
    Status = API.ASTBuilderCommit(API.Context, Task, Builder, &Result);
  (void)API.DestroyASTBuilder(API.Context, Task, Builder);
  return Status.Code == NEVERC_STATUS_OK ? Result : NevercExprHandle{};
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);
  auto Created =
      PluginFrontendFuzzIteration::create(pluginFuzzRuntime(), true);
  if (!Created) {
    consumeError(Created.takeError());
    return 0;
  }

  std::unique_ptr<PluginFrontendFuzzIteration> Iteration =
      std::move(*Created);
  const NevercASTAPI *API = Iteration->astAPI();
  if (!API)
    return 0;
  const NevercTaskHandle Task = Iteration->task().handle();

  NevercDeclHandle Root{};
  (void)API->GetTranslationUnit(API->Context, Task, &Root);
  NevercDeclHandle VariableHandle = findVariable(*API, Task);
  if (neverc_handle_is_null(VariableHandle))
    return 0;

  NevercVarDeclInfo Before{};
  Before.Header.StructSize = sizeof(Before);
  if (API->GetVarDeclInfo(API->Context, Task, VariableHandle, &Before).Code !=
      NEVERC_STATUS_OK)
    return 0;

  // Always prove that a valid staged replacement is invisible after abort.
  NevercExprHandle Replacement =
      buildInteger(*API, Task, VariableHandle, Input.takeU64());
  if (!neverc_handle_is_null(Replacement)) {
    NevercASTMutationHandle Mutation{};
    if (API->BeginASTMutation(API->Context, Task, &Mutation).Code ==
        NEVERC_STATUS_OK) {
      NevercStatus Replace = API->ASTMutationReplaceChild(
          API->Context, Task, Mutation, VariableHandle,
          NEVERC_AST_CHILD_SLOT_DECL_VAR_INITIALIZER, 0, Replacement);
      if (Replace.Code == NEVERC_STATUS_OK)
        (void)API->AbortASTMutation(API->Context, Task, Mutation);
      NevercVarDeclInfo AfterAbort{};
      AfterAbort.Header.StructSize = sizeof(AfterAbort);
      if (API->GetVarDeclInfo(API->Context, Task, VariableHandle, &AfterAbort)
                  .Code == NEVERC_STATUS_OK &&
          !sameHandle(Before.Initializer, AfterAbort.Initializer))
        std::abort();
      (void)API->DestroyASTMutation(API->Context, Task, Mutation);
    }
  }

  // Decode arbitrary property IDs and output capacities against both valid
  // and forged handles. The bridge must reject malformed queries without
  // reading or writing beyond the caller-provided structures.
  const unsigned QueryCount = std::min<unsigned>(Input.takeByte(), 32U);
  for (unsigned Query = 0; Query != QueryCount; ++Query) {
    NevercASTNodeHandle Node =
        (Input.takeByte() & 1U) ? VariableHandle : arbitraryHandle(Input);
    NevercASTValue Value{};
    Value.Header.StructSize =
        std::min<uint32_t>(sizeof(Value), Input.takeU32());
    (void)API->GetProperty(API->Context, chooseTaskHandle(Input, Task), Node,
                           Input.takeU32(), &Value);
  }

  const unsigned BuilderCount = std::min<unsigned>(Input.takeByte(), 16U);
  for (unsigned Index = 0; Index != BuilderCount; ++Index) {
    NevercASTBuilderHandle Builder{};
    if (API->CreateASTBuilder(API->Context, Task, Input.takeU32(), &Builder)
            .Code != NEVERC_STATUS_OK)
      continue;

    NevercASTValue Value{};
    Value.Header.StructSize =
        std::min<uint32_t>(sizeof(Value), Input.takeU32());
    Value.Type = Input.takeU32();
    Value.UnsignedValue = Input.takeU64();
    NevercTaskHandle OperationTask = chooseTaskHandle(Input, Task);
    (void)API->ASTBuilderSetProperty(API->Context, OperationTask, Builder,
                                     Input.takeU32(), &Value);
    (void)API->ASTBuilderSetChild(API->Context, OperationTask, Builder,
                                  Input.takeU32(), Input.takeU64(),
                                  arbitraryHandle(Input));
    NevercASTNodeHandle Result{};
    (void)API->ASTBuilderCommit(API->Context, OperationTask, Builder, &Result);
    (void)API->DestroyASTBuilder(API->Context, Task, Builder);
  }

  NevercASTMutationHandle Mutation{};
  if (API->BeginASTMutation(API->Context, Task, &Mutation).Code ==
      NEVERC_STATUS_OK) {
    NevercTaskHandle OperationTask = chooseTaskHandle(Input, Task);
    NevercASTNodeHandle Parent =
        (Input.takeByte() & 1U) ? VariableHandle : arbitraryHandle(Input);
    NevercASTNodeHandle Candidate =
        (Input.takeByte() & 1U) ? Replacement : arbitraryHandle(Input);
    (void)API->ASTMutationReplaceChild(
        API->Context, OperationTask, Mutation, Parent, Input.takeU32(),
        Input.takeU64(), Candidate);
    if (Input.takeByte() & 1U)
      (void)API->CommitASTMutation(API->Context, OperationTask, Mutation);
    else
      (void)API->AbortASTMutation(API->Context, OperationTask, Mutation);
    (void)API->DestroyASTMutation(API->Context, Task, Mutation);
  }
  return 0;
}
