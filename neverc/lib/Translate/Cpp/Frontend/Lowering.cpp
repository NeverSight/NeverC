#include "Frontend.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/ADT/ScopeExit.h"
#include <algorithm>
#include <optional>
#include <set>

using namespace clang;
namespace nct {
using Expression = json::Object;

class FunctionLowering {
  Adapter &A;
  const FunctionDecl *Function;
  const CXXRecordDecl *DestroyedRecord = nullptr;
  json::Array Parameters, Locals, Body;
  struct OwnedObject {
    Expression Place, Live;
    QualType Type;
    SourceLocation Location;
  };
  struct CleanupFrame {
    std::vector<OwnedObject> Objects;
  };
  std::vector<CleanupFrame> Scopes, FullExpressions;
  std::vector<Expression> LiveFlags;
  std::string DestructionEnd;
  std::optional<Expression> ThisPointer, ResultPlace;
  struct InitializationReceiver {
    const CXXRecordDecl *Record;
    Expression Pointer;
  };
  std::optional<InitializationReceiver> DefaultReceiver;
  struct ReferenceInitializer {
    const VarDecl *Variable;
    std::size_t ScopeIndex;
  };
  std::optional<ReferenceInitializer> ActiveReferenceInitializer;
  std::map<const Decl *, Expression> Storage;
  std::map<const OpaqueValueExpr *, Expression> ArraySources;
  struct ArrayIndex {
    uint64_t Value;
    SourceLocation Location;
  };
  std::vector<ArrayIndex> ArrayIndices;
  std::map<std::string, std::vector<std::string>> Edges;
  // Every breakable construct has an exit; only loops have a continue target.
  struct ControlTarget {
    std::string Break, Continue;
    std::size_t BreakDepth, ContinueDepth;
  };
  std::vector<ControlTarget> ControlTargets;
  struct SwitchFrame {
    std::map<const SwitchCase *, std::string> Labels;
    std::set<const Stmt *> Entries;
  };
  std::vector<SwitchFrame> Switches;
  std::string Prefix, Current, Entry;
  unsigned Serial = 0;
  bool Open = false;

  [[noreturn]] void reject(SourceLocation L, llvm::StringRef What,
                           llvm::StringRef Reason) {
    A.reject(L, What, Reason);
    throw Failure{};
  }
  std::string type(QualType T, SourceLocation L, bool Void = false) {
    auto Result = A.type(T, L, Void);
    if (Result.empty())
      throw Failure{};
    return Result;
  }
  Expression variable(llvm::StringRef Name, llvm::StringRef Type,
                      SourceLocation L) {
    return Expression{{"kind", "var"},
                      {"name", Name.str()},
                      {"type", Type.str()},
                      {"loc", A.loc(L)}};
  }
  Expression temporary(llvm::StringRef Type, SourceLocation L) {
    auto Name = Prefix + "v" + std::to_string(++Serial);
    Locals.push_back(
        json::Object{{"name", Name}, {"type", Type.str()}, {"loc", A.loc(L)}});
    return variable(Name, Type, L);
  }
  Expression one(llvm::StringRef T, SourceLocation L) {
    return A.literal(llvm::APSInt(llvm::APInt(integerBits(T), 1), unsignedInteger(T)), T, L);
  }
  Expression boolean(bool Value, SourceLocation L) {
    return Expression{{"kind", "literal"},
                      {"type", "bool"},
                      {"value", Value},
                      {"loc", A.loc(L)}};
  }
  Expression cast(Expression E, llvm::StringRef T, SourceLocation L) {
    if (E.getString("type") == T)
      return E;
    return Expression{{"kind", "cast"},
                      {"type", T.str()},
                      {"args", json::Array{std::move(E)}},
                      {"loc", A.loc(L)}};
  }
  Expression address(Expression Place, QualType T, SourceLocation L) {
    return Expression{{"kind", "address"},
                      {"type", type(A.Context.getPointerType(T), L)},
                      {"args", json::Array{std::move(Place)}},
                      {"loc", A.loc(L)}};
  }
  Expression dereference(Expression Pointer, SourceLocation L) {
    auto T = *Pointer.getString("type");
    auto Pointee = T.drop_front(T.starts_with("cptr:") ? 5 : 4).str();
    return Expression{{"kind", "dereference"},
                      {"type", Pointee},
                      {"args", json::Array{std::move(Pointer)}},
                      {"loc", A.loc(L)}};
  }
  Expression bind(const Expr *E, QualType ReferenceType) {
    auto L = E->getExprLoc();
    auto Pointer = address(lvalue(E), E->getType(), L);
    return cast(std::move(Pointer), type(ReferenceType, L), L);
  }
  Expression decay(Expression Place, llvm::StringRef PointerType,
                   SourceLocation L) {
    return Expression{{"kind", "array_decay"}, {"type", PointerType.str()},
                      {"args", json::Array{std::move(Place)}}, {"loc", A.loc(L)}};
  }
  Expression index(Expression Pointer, Expression Index, llvm::StringRef T,
                   SourceLocation L) {
    return Expression{{"kind", "index"}, {"type", T.str()},
                      {"args", json::Array{std::move(Pointer), std::move(Index)}},
                      {"loc", A.loc(L)}};
  }
  std::size_t generatedNodes(const Expression &E) {
    auto T = *E.getString("type");
    std::size_t Nodes = 2 + std::count(T.begin(), T.end(), ':');
    if (const auto *Args = E.getArray("args"))
      for (const auto &Arg : *Args)
        Nodes += generatedNodes(*Arg.getAsObject());
    return Nodes;
  }
  Expression binary(llvm::StringRef Op, Expression LHS, Expression RHS,
                    llvm::StringRef T, SourceLocation L) {
    // Comparisons of a scoped enum can retain its narrow underlying type in
    // Clang's AST. Promote the normalized values explicitly for NC arithmetic.
    auto LeftType = LHS.getString("type")->str();
    auto RightType = RHS.getString("type")->str();
    auto IsPointer = [](llvm::StringRef Type) {
      return Type.starts_with("ptr:") || Type.starts_with("cptr:");
    };
    if ((Op == "+" || Op == "-") &&
        (IsPointer(LeftType) || IsPointer(RightType))) {
      if (integerBits(LeftType) && integerBits(LeftType) < 32)
        LHS = cast(std::move(LHS), "int", L);
      if (integerBits(RightType) && integerBits(RightType) < 32)
        RHS = cast(std::move(RHS), "int", L);
    }
    if (T == "bool" && LeftType == RightType && integerBits(LeftType) &&
        integerBits(LeftType) < 32) {
      LHS = cast(std::move(LHS), "int", L);
      RHS = cast(std::move(RHS), "int", L);
    }
    return Expression{{"kind", "binary"},
                      {"type", T.str()},
                      {"operator", Op.str()},
                      {"args", json::Array{std::move(LHS), std::move(RHS)}},
                      {"loc", A.loc(L)}};
  }
  void assign(Expression Target, Expression Value, SourceLocation L) {
    A.chargeExpansion(1 + generatedNodes(Target) + generatedNodes(Value), L);
    Body.push_back(json::Object{{"op", "assign"},
                                {"target", std::move(Target)},
                                {"value", std::move(Value)},
                                {"loc", A.loc(L)}});
  }
  Expression snapshot(Expression Value, SourceLocation L) {
    auto Target = temporary(*Value.getString("type"), L);
    assign(Target, std::move(Value), L);
    return Target;
  }
  std::string labelName() { return Prefix + "b" + std::to_string(++Serial); }
  void label(const std::string &Name, SourceLocation L) {
    if (Open)
      jump(Name, L);
    Body.push_back(
        json::Object{{"op", "label"}, {"label", Name}, {"loc", A.loc(L)}});
    Current = Name;
    Open = true;
  }
  void jump(const std::string &Destination, SourceLocation L) {
    Body.push_back(json::Object{
        {"op", "jump"}, {"label", Destination}, {"loc", A.loc(L)}});
    Edges[Current].push_back(Destination);
    Open = false;
  }
  void branch(Expression Condition, const std::string &True,
              const std::string &False, SourceLocation L,
              const Expr *Source = nullptr) {
    // Constant branches keep infinite-loop reachability accurate. Constant
    // evaluation is only used when Clang proves a C++ constant expression.
    APValue Value;
    if (Source && Source->isCXX11ConstantExpr(A.Context, &Value) &&
        Value.isInt()) {
      jump(Value.getInt().isZero() ? False : True, L);
      return;
    }
    Body.push_back(json::Object{{"op", "branch"},
                                {"condition", std::move(Condition)},
                                {"true", True},
                                {"false", False},
                                {"loc", A.loc(L)}});
    Edges[Current].push_back(True);
    Edges[Current].push_back(False);
    Open = false;
  }
  std::set<std::string> reachable() const {
    std::set<std::string> Seen;
    std::vector<std::string> Work{Entry};
    while (!Work.empty()) {
      auto L = Work.back();
      Work.pop_back();
      if (!Seen.insert(L).second)
        continue;
      auto I = Edges.find(L);
      if (I != Edges.end())
        Work.insert(Work.end(), I->second.begin(), I->second.end());
    }
    return Seen;
  }
  Expression storage(const NamedDecl *D, SourceLocation L) {
    auto I = Storage.find(D->getCanonicalDecl());
    if (I != Storage.end())
      return I->second;
    if (const auto *V = dyn_cast<VarDecl>(D); V && !V->isLocalVarDeclOrParm())
      return variable(A.name(D), type(V->getType(), L), L);
    reject(L, "storage reference",
           "No supported local, parameter or global storage declaration.");
  }
  Expression lvalue(const Expr *E) {
    E = E->IgnoreParens();
    auto L = E->getExprLoc();
    if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(E)) {
      auto Found = ArraySources.find(Opaque);
      if (Found == ArraySources.end())
        reject(L, "array copy source", "No bound semantic array source is active.");
      return Found->second;
    }
    if (const auto *R = dyn_cast<DeclRefExpr>(E))
      return storage(R->getDecl(), E->getExprLoc());
    if (const auto *M = dyn_cast<MemberExpr>(E)) {
      // Never snapshot a whole base record just to access one of its fields.
      return Expression{{"kind", "member"},
                        {"type", type(M->getType(), M->getExprLoc())},
                        {"name", A.name(M->getMemberDecl())},
                        {"args", json::Array{
                             M->isArrow()
                                 ? dereference(expression(M->getBase()), L)
                                 : lvalue(M->getBase())}},
                        {"loc", A.loc(M->getExprLoc())}};
    }
    if (const auto *C = dyn_cast<CastExpr>(E);
        C && C->getCastKind() == CK_NoOp) {
      auto Place = lvalue(C->getSubExpr());
      if (!A.S.coreV2())
        return Place;
      return dereference(
          cast(address(std::move(Place), C->getSubExpr()->getType(), L),
               type(A.Context.getPointerType(E->getType()), L), L), L);
    }
    if (A.S.coreV2()) {
      if (const auto *C = dyn_cast<CastExpr>(E);
          C && C->getCastKind() == CK_UserDefinedConversion && C->isGLValue()) {
        const auto *Selected = userConversionCall(C, A.Context);
        if (!Selected)
          reject(L, "user conversion", "Unsupported reference conversion wrapper.");
        return call(Selected);
      }
      if (const auto *List = dyn_cast<InitListExpr>(E)) {
        const auto *Init = referenceListInitializer(List, A.Context);
        if (!Init)
          reject(L, "reference initializer", "A transparent single-element reference list is required.");
        return lvalue(Init);
      }
      if (const auto *M = dyn_cast<MaterializeTemporaryExpr>(E))
        return materializeTemporary(M, L);
      if (const auto *Index = dyn_cast<ArraySubscriptExpr>(E)) {
        // C++17 sequences the syntactic left operand first, even for i[p].
        auto Left = expression(Index->getLHS());
        auto Right = expression(Index->getRHS());
        bool PointerOnLeft = Index->getLHS()->getType()->isPointerType();
        return PointerOnLeft
                   ? index(std::move(Left), std::move(Right), type(E->getType(), L), L)
                   : index(std::move(Right), std::move(Left), type(E->getType(), L), L);
      }
      if (const auto *W = dyn_cast<ExprWithCleanups>(E))
        return lvalue(W->getSubExpr());
      if (const auto *U = dyn_cast<UnaryOperator>(E)) {
        if (U->getOpcode() == UO_Deref)
          return dereference(expression(U->getSubExpr()), L);
        if (U->isIncrementDecrementOp() && !U->isPostfix())
          return expression(U);
      }
      if (const auto *C = dyn_cast<ConditionalOperator>(E); C && C->isGLValue()) {
        auto Condition = expression(C->getCond());
        auto Yes = labelName(), No = labelName(), End = labelName();
        auto Reference = C->isXValue()
                             ? A.Context.getRValueReferenceType(E->getType())
                             : A.Context.getLValueReferenceType(E->getType());
        auto Result = temporary(type(Reference, L), L);
        branch(std::move(Condition), Yes, No, L, C->getCond());
        label(Yes, L);
        assign(Result, bind(C->getTrueExpr(), Reference), L);
        jump(End, L);
        label(No, L);
        assign(Result, bind(C->getFalseExpr(), Reference), L);
        jump(End, L);
        label(End, L);
        return dereference(std::move(Result), L);
      }
      if (const auto *B = dyn_cast<BinaryOperator>(E)) {
        if (B->getOpcode() == BO_Comma) {
          discard(B->getLHS());
          return lvalue(B->getRHS());
        }
        if (B->isAssignmentOp())
          return expression(B);
      }
      if (const auto *Call = dyn_cast<CallExpr>(E); Call && Call->isGLValue())
        return expression(Call);
    }
    reject(E->getExprLoc(), "object storage",
           "Only admitted scalar/aggregate objects and subobject glvalues are supported.");
  }
  Expression project(const Expr *Base, std::vector<const FieldDecl *> Fields,
                     llvm::StringRef ResultType, SourceLocation L) {
    Base = Base->IgnoreParens();
    if (const auto *M = dyn_cast<MemberExpr>(Base)) {
      Fields.insert(Fields.begin(), llvm::cast<FieldDecl>(M->getMemberDecl()));
      return project(M->getBase(), std::move(Fields), ResultType, L);
    }
    if (const auto *C = dyn_cast<ConditionalOperator>(Base);
        C && C->isGLValue()) {
      auto Condition = expression(C->getCond());
      auto Yes = labelName(), No = labelName(), End = labelName();
      auto Result = temporary(ResultType, L);
      branch(std::move(Condition), Yes, No, L, C->getCond());
      label(Yes, L);
      assign(Result, project(C->getTrueExpr(), Fields, ResultType, L), L);
      jump(End, L);
      label(No, L);
      assign(Result,
             project(C->getFalseExpr(), std::move(Fields), ResultType, L), L);
      jump(End, L);
      label(End, L);
      return Result;
    }
    if (const auto *B = dyn_cast<BinaryOperator>(Base);
        B && B->getOpcode() == BO_Comma) {
      discard(B->getLHS());
      return project(B->getRHS(), std::move(Fields), ResultType, L);
    }
    auto Value = expression(Base);
    for (const auto *Field : Fields)
      Value = Expression{{"kind", "member"},
                         {"type", type(Field->getType(), L)},
                         {"name", A.name(Field)},
                         {"args", json::Array{std::move(Value)}},
                         {"loc", A.loc(L)}};
    return Value;
  }
  void copyAssignedArray(Expression To, Expression From, QualType T,
                         QualType SourceType, SourceLocation L) {
    const auto *Array = A.Context.getAsConstantArrayType(T);
    const auto *SourceArray = A.Context.getAsConstantArrayType(SourceType);
    if (!Array || !SourceArray ||
        !A.Context.hasSameUnqualifiedType(T, SourceType))
      reject(L, "generated array assignment", "Source and destination array types differ.");
    auto Count = Array->getSize().getLimitedValue(65537);
    if (!Count || Count > 65536 || A.storageUnits(T) > 200000)
      reject(L, "generated array assignment", "Array assignment exceeds the storage limit.");
    auto Element = Array->getElementType();
    auto SourceElement = SourceArray->getElementType();
    for (unsigned N = 0; N < Count; ++N) {
      A.chargeExpansion(1, L);
      auto Destination = initialElement(To, Element, N, L);
      auto Source = index(
          decay(From, type(A.Context.getPointerType(SourceElement), L), L),
          A.literal(llvm::APSInt(llvm::APInt(32, N), false), "int", L),
          type(SourceElement, L), L);
      if (Element->isArrayType())
        copyAssignedArray(std::move(Destination), std::move(Source),
                          Element, SourceElement, L);
      else if (!emptyRecord(Element))
        assign(std::move(Destination), std::move(Source), L);
    }
  }
  static bool reverseOperatorParameters(const FunctionDecl *F) {
    // Explicit calls to a free assignment operator choose the same permitted
    // order as operator notation. Its callee can then reverse that one order
    // when destroying by-value parameters, including on early returns.
    return F && !isa<CXXMethodDecl>(F) && F->isOverloadedOperator() &&
           CXXOperatorCallExpr::isAssignmentOp(F->getOverloadedOperator());
  }
  Expression call(const CallExpr *Call,
                  std::optional<Expression> Destination = std::nullopt) {
    auto L = Call->getExprLoc();
    auto T = type(Call->getType(), L, true);
    auto *Callee = Call->getDirectCallee();
    const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
    auto Mapping = A.mapping(Call);
    if (!Mapping.empty()) {
      json::Array Args;
      for (const auto *Arg : Call->arguments())
        Args.push_back(expression(Arg));
      auto Result = temporary(T, L);
      Body.push_back(json::Object{{"op", "mapped_call"},
                                  {"mapping", Mapping},
                                  {"args", std::move(Args)},
                                  {"target", json::Object(Result)},
                                  {"loc", A.loc(L)}});
      return Result;
    }
    if (A.S.coreV2())
      if (auto Copy = generatedArrayAssignment(
              Call, dyn_cast_or_null<CXXMethodDecl>(Function), A.Context)) {
        auto To = snapshot(address(lvalue(Copy->Destination), Copy->Type, L), L);
        auto From = snapshot(address(lvalue(Copy->Source), Copy->Source->getType(), L), L);
        copyAssignedArray(dereference(To, L), dereference(std::move(From), L),
                          Copy->Type, Copy->Source->getType(), L);
        return cast(std::move(To), type(Call->getType(), L), L);
      }
    const bool TrivialAssignment = A.S.coreV2() && defaultedAssignment(Method) &&
                                   Method->isTrivial();
    if (!Callee ||
        (!TrivialAssignment && !Callee->hasBody() &&
         (!A.S.project() ||
          Callee->getFormalLinkage() == Linkage::Internal)) ||
        (Method && (!A.S.coreV2() || !callableMethod(Method))))
      reject(L, "call",
             "Call target is not a supported defined function.");
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
    if (Operator && (!A.S.coreV2() ||
        !((ordinaryOperator(Callee) && Callee->getOverloadedOperator() == Operator->getOperator()) ||
          (supportedAssignment(Method) && Operator->getOperator() == OO_Equal))))
      reject(L, "operator call", "Unsupported selected operator function.");
    unsigned ArgumentOffset = Operator && Method && !Method->isStatic() ? 1 : 0;
    if (Call->getNumArgs() != Callee->getNumParams() + ArgumentOffset)
      reject(L, "call", "Call and source parameter counts differ.");
    json::Array Args;
    Expression Result;
    bool HasRecordResult = recordValue(Callee->getReturnType());
    if (HasRecordResult) {
      Result = Destination ? std::move(*Destination)
                           : objectTemporary(Callee->getReturnType(), L);
      if (Result.getString("type") != type(Callee->getReturnType(), L))
        reject(L, "call result", "Call and result destination types differ.");
      Args.push_back(snapshot(
          address(Result, Callee->getReturnType().getUnqualifiedType(), L), L));
    } else if (Destination) {
      reject(L, "call result", "Only record results accept a destination.");
    }
    if (Operator) {
      // Capture in C++17 source order, then assemble in parameter order.
      // Overloaded logical/comma/shift/subscript operators evaluate both
      // operands left-to-right; assignment operators evaluate RHS first.
      std::vector<Expression> Captured(Call->getNumArgs());
      for (unsigned N = 0; N < Call->getNumArgs(); ++N) {
        unsigned I = Operator->isAssignmentOp() ? Call->getNumArgs() - 1 - N : N;
        const auto *Source = Call->getArg(I);
        if (ArgumentOffset && I == 0) {
          auto Receiver = address(lvalue(Source), Source->getType(), L);
          Captured[I] = snapshot(
              cast(std::move(Receiver), type(Method->getThisType(), L), L), L);
        } else {
          Captured[I] = argument(Source, Callee->getParamDecl(I - ArgumentOffset)->getType());
        }
      }
      for (auto &Arg : Captured)
        Args.push_back(std::move(Arg));
    } else {
      if (Method) {
        const auto *Reference = directMethodReference(Call);
        if (!Reference)
          reject(L, "method call", "Methods require a checked direct callee.");
        if (const auto *Member = dyn_cast<MemberExpr>(Reference)) {
          const auto *Base = Member->getBase();
          if (Method->isStatic()) {
            discard(Base);
          } else {
            // Capture the receiver before explicit arguments can reseat a
            // source pointer alias, without reading unrelated record fields.
            auto Receiver = Member->isArrow()
                                ? expression(Base)
                                : address(lvalue(Base), Base->getType(), L);
            Args.push_back(snapshot(
                cast(std::move(Receiver), type(Method->getThisType(), L), L), L));
          }
        }
      }
      std::vector<Expression> Captured(Call->getNumArgs());
      for (unsigned N = 0; N < Call->getNumArgs(); ++N) {
        unsigned I = A.S.coreV2() && reverseOperatorParameters(Callee)
                         ? Call->getNumArgs() - 1 - N : N;
        Captured[I] = argument(Call->getArg(I), Callee->getParamDecl(I)->getType());
      }
      for (auto &Arg : Captured)
        Args.push_back(std::move(Arg));
    }
    if (TrivialAssignment) {
      // Both reference addresses are now captured in source sequencing order.
      // Read fields only here, after receiver effects, and return the receiver
      // lvalue. Assignment creates no complete-object lifetime or helper call.
      auto Receiver = dereference(*Args[0].getAsObject(), L);
      if (!emptyRecord(A.Context.getRecordType(Method->getParent())))
        assign(Receiver, dereference(*Args[1].getAsObject(), L), L);
      return Receiver;
    }
    chargeCall(Args, L);
    json::Object Instruction{{"op", "call"},
                             {"callee", A.name(Callee)},
                             {"args", std::move(Args)},
                             {"loc", A.loc(L)}};
    auto ResultType = type(Callee->getReturnType(), L, true);
    if (!HasRecordResult && ResultType != "void") {
      Result = temporary(ResultType, L);
      Instruction["target"] = json::Object(Result);
    }
    Body.push_back(std::move(Instruction));
    if (Callee->getReturnType()->isReferenceType())
      return dereference(std::move(Result), L);
    return Result;
  }
  Expression expression(const Expr *E) {
    auto L = E->getExprLoc();
    auto T = type(E->getType(), L, true);
    if (isa<ArrayInitIndexExpr>(E)) {
      if (!A.S.coreV2() || ArrayIndices.empty())
        reject(L, "array copy index", "No semantic element-copy index is active.");
      const auto &Index = ArrayIndices.back();
      return A.literal(llvm::APSInt(llvm::APInt(integerBits(T), Index.Value),
                                   unsignedInteger(T)), T, Index.Location);
    }
    if (isa<OpaqueValueExpr>(E))
      return lvalue(E);
    if (isa<CXXThisExpr>(E)) {
      if (!A.S.coreV2() || !ThisPointer)
        reject(L, "this", "No supported instance-method receiver is active.");
      return *ThisPointer;
    }
    if (const auto *I = dyn_cast<IntegerLiteral>(E))
      return A.literal(llvm::APSInt(I->getValue(), unsignedInteger(T)), T, L);
    if (const auto *C = dyn_cast<CharacterLiteral>(E); C && A.S.coreV2())
      return A.literal(llvm::APSInt(llvm::APInt(integerBits(T), C->getValue()),
                                   unsignedInteger(T)), T, L);
    if (const auto *Query = dyn_cast<CXXNoexceptExpr>(E); Query && A.S.coreV2()) {
      if (!Query->getOperand() || Query->isTypeDependent() ||
          Query->isValueDependent() || Query->isInstantiationDependent())
        reject(L, "noexcept query", "A resolved constant noexcept query is required.");
      // Clang accounts for selected function specifications and destruction.
      // The inspected operand creates no runtime calls, values or cleanup.
      return boolean(Query->getValue(), L);
    }
    if (const auto *Query = dyn_cast<UnaryExprOrTypeTraitExpr>(E); Query && A.S.coreV2()) {
      APValue Value;
      if (!Query->isCXX11ConstantExpr(A.Context, &Value) || !Value.isInt())
        reject(L, "size/alignment query", "A checked constant integer query is required.");
      // Its operand has been inspected by the allowlist. Never lower effects
      // from an unevaluated operand (for example sizeof(++value)).
      return A.literal(Value.getInt(), T, L);
    }
    if (const auto *F = dyn_cast<FloatingLiteral>(E))
      return A.floatingLiteral(F->getValue(), L);
    if (const auto *B = dyn_cast<CXXBoolLiteralExpr>(E))
      return boolean(B->getValue(), L);
    if (const auto *P = dyn_cast<ParenExpr>(E))
      return expression(P->getSubExpr());
    if (const auto *C = dyn_cast<ConstantExpr>(E); C && A.S.coreV2())
      return expression(C->getSubExpr());
    if (const auto *R = dyn_cast<DeclRefExpr>(E)) {
      if (A.S.coreV2())
        if (const auto *Enumerator = dyn_cast<EnumConstantDecl>(R->getDecl())) {
          llvm::APSInt Value = Enumerator->getInitVal().extOrTrunc(integerBits(T));
          Value.setIsUnsigned(unsignedInteger(T));
          return A.literal(Value, T, L);
        }
      return storage(R->getDecl(), L);
    }
    if (const auto *C = dyn_cast<CastExpr>(E)) {
      switch (C->getCastKind()) {
      case CK_LValueToRValue:
        return snapshot(cast(expression(C->getSubExpr()), T, L), L);
      case CK_NoOp:
        if (C->isPRValue() && aggregateValue(C->getType()))
          return materialize(C, L);
        if (A.S.coreV2() && C->isGLValue())
          return lvalue(C);
        return cast(expression(C->getSubExpr()), T, L);
      case CK_IntegralCast:
      case CK_IntegralToBoolean:
        return cast(expression(C->getSubExpr()), T, L);
      case CK_UserDefinedConversion: {
        const auto *Selected = A.S.coreV2() ? userConversionCall(C, A.Context) : nullptr;
        if (!Selected)
          reject(L, "user conversion", "Unsupported conversion-function wrapper.");
        return C->isPRValue() && recordValue(C->getType())
                   ? materialize(C, L) : call(Selected);
      }
      case CK_ConstructorConversion:
        if (A.S.coreV2())
          return materialize(C, L);
        [[fallthrough]];
      case CK_NullToPointer:
        if (A.S.coreV2())
          return Expression{{"kind", "null"}, {"type", T}, {"loc", A.loc(L)}};
        [[fallthrough]];
      case CK_ArrayToPointerDecay:
        if (A.S.coreV2())
          return snapshot(decay(lvalue(C->getSubExpr()), T, L), L);
        [[fallthrough]];
      case CK_PointerToBoolean:
      case CK_BitCast:
        if (A.S.coreV2())
          return cast(expression(C->getSubExpr()), T, L);
        [[fallthrough]];
      case CK_IntegralToFloating:
      case CK_FloatingToIntegral:
      case CK_FloatingToBoolean:
        if (A.S.math())
          return cast(expression(C->getSubExpr()), T, L);
        [[fallthrough]];
      default:
        reject(L, "cast",
               "Only integral/bool value conversions are supported.");
      }
    }
    if (const auto *M = dyn_cast<MemberExpr>(E)) {
      if (A.S.coreV2() && M->isGLValue())
        return lvalue(M);
      return project(M->getBase(), {llvm::cast<FieldDecl>(M->getMemberDecl())},
                     T, L);
    }
    if (A.S.coreV2() && isa<ArraySubscriptExpr>(E))
      return lvalue(E);
    if (isa<ImplicitValueInitExpr, CXXScalarValueInitExpr>(E))
      return A.zero(E->getType(), L);
    if (const auto *I = dyn_cast<InitListExpr>(E)) {
      if (A.S.coreV2() && E->isGLValue())
        return lvalue(I);
      if (aggregateValue(E->getType()))
        return materialize(I, L);
      if (I->isSyntacticForm() && I->getSemanticForm())
        I = I->getSemanticForm();
      if (!E->getType()->isRecordType()) {
        if (!I->getNumInits())
          return A.zero(E->getType(), L);
        if (I->getNumInits() == 1)
          return expression(I->getInit(0));
        reject(L, "initializer", "Invalid scalar initializer shape.");
      }
      json::Array Values;
      for (const auto *Init : I->inits())
        Values.push_back(expression(Init));
      auto Fields =
          E->getType()->getAsCXXRecordDecl()->getDefinition()->fields();
      auto Count = std::distance(Fields.begin(), Fields.end());
      if (Values.size() != Count)
        reject(L, "aggregate initialization",
               "Incomplete semantic field initializer list.");
      return Expression{{"kind", "aggregate"},
                        {"type", T},
                        {"args", std::move(Values)},
                        {"loc", A.loc(L)}};
    }
    if (const auto *M = dyn_cast<MaterializeTemporaryExpr>(E))
      return A.S.coreV2() ? materializeTemporary(M, L)
                          : expression(M->getSubExpr());
    if (const auto *W = dyn_cast<ExprWithCleanups>(E))
      return expression(W->getSubExpr());
    if (const auto *B = dyn_cast<CXXBindTemporaryExpr>(E))
      return expression(B->getSubExpr());
    if (const auto *C = dyn_cast<CXXConstructExpr>(E)) {
      if (A.S.coreV2())
        return materialize(C, L);
      if (C->getConstructor()->isCopyOrMoveConstructor() &&
          C->getNumArgs() == 1)
        return snapshot(expression(C->getArg(0)), L);
      if (C->getConstructor()->isDefaultConstructor() && !C->getNumArgs() &&
          C->requiresZeroInitialization())
        return A.zero(E->getType(), L);
      reject(L, "construction",
             "Uninitialized construction is supported only as a local storage "
             "declaration.");
    }
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E)) {
      const auto *Method =
          dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
      if (Call->getOperator() == OO_Equal && Method && Method->isImplicit() &&
          Method->isTrivial() && Call->getNumArgs() == 2 &&
          !A.S.coreV2()) {
        auto Right = expression(Call->getArg(1));
        auto Left = lvalue(Call->getArg(0));
        assign(Left, std::move(Right), L);
        return Left;
      }
      if (A.S.coreV2() &&
          (ordinaryOperator(Call->getDirectCallee()) || supportedAssignment(Method)))
        return Call->isPRValue() && recordValue(Call->getType())
                   ? materialize(Call, L) : call(Call);
      reject(L, "overloaded operator", "Unsupported selected operator function.");
    }
    if (const auto *Call = dyn_cast<CallExpr>(E))
      return Call->isPRValue() && recordValue(Call->getType())
                 ? materialize(Call, L) : call(Call);
    if (const auto *U = dyn_cast<UnaryOperator>(E)) {
      if (A.S.coreV2() && U->getOpcode() == UO_AddrOf)
        return address(lvalue(U->getSubExpr()), U->getSubExpr()->getType(), L);
      if (A.S.coreV2() && U->getOpcode() == UO_Deref)
        return lvalue(U);
      if (U->isIncrementDecrementOp()) {
        auto Place = lvalue(U->getSubExpr());
        auto Old = snapshot(Place, L);
        if (U->getType()->isPointerType()) {
          auto New = binary(U->isIncrementOp() ? "+" : "-", Old,
                            one("int", L), T, L);
          assign(Place, std::move(New), L);
          return U->isPostfix() ? Old : Place;
        }
        auto Computation = integerBits(T) < 32 ? std::string("int") : T;
        auto New = binary(U->isIncrementOp() ? "+" : "-", cast(Old, Computation, L),
                          one(Computation, L), Computation, L);
        assign(Place, cast(std::move(New), T, L), L);
        return U->isPostfix() ? Old : Place;
      }
      auto Arg = expression(U->getSubExpr());
      return snapshot(
          Expression{
              {"kind", "unary"},
              {"type", T},
              {"operator", UnaryOperator::getOpcodeStr(U->getOpcode()).str()},
              {"args", json::Array{std::move(Arg)}},
              {"loc", A.loc(L)}},
          L);
    }
    if (const auto *B = dyn_cast<BinaryOperator>(E)) {
      if (B->isAssignmentOp()) {
        auto Right = expression(B->getRHS());
        auto Left = lvalue(B->getLHS());
        if (const auto *Compound = dyn_cast<CompoundAssignOperator>(B)) {
          auto ComputationType = type(Compound->getComputationResultType(), L);
          auto LeftType = type(Compound->getComputationLHSType(), L);
          auto Read = cast(snapshot(Left, L), LeftType, L);
          Right = binary(B->getOpcodeStr().drop_back(), std::move(Read),
                         std::move(Right), ComputationType, L);
          Right = cast(std::move(Right), T, L);
        }
        assign(Left, std::move(Right), L);
        return Left;
      }
      if (B->getOpcode() == BO_Comma) {
        discard(B->getLHS());
        return expression(B->getRHS());
      }
      auto Left = expression(B->getLHS());
      if (B->isLogicalOp()) {
        auto Result = temporary("bool", L);
        auto RHS = labelName(), Short = labelName(), End = labelName();
        bool And = B->getOpcode() == BO_LAnd;
        branch(std::move(Left), And ? RHS : Short, And ? Short : RHS, L,
               B->getLHS());
        label(Short, L);
        assign(Result, boolean(!And, L), L);
        jump(End, L);
        label(RHS, L);
        assign(Result, expression(B->getRHS()), L);
        jump(End, L);
        label(End, L);
        return Result;
      }
      auto Right = expression(B->getRHS());
      return snapshot(
          binary(B->getOpcodeStr(), std::move(Left), std::move(Right), T, L),
          L);
    }
    if (const auto *C = dyn_cast<ConditionalOperator>(E)) {
      if (C->isPRValue() && recordValue(C->getType()))
        return materialize(C, L);
      if (A.S.coreV2() && C->isGLValue())
        return lvalue(C);
      auto Condition = expression(C->getCond());
      auto Yes = labelName(), No = labelName(), End = labelName();
      Expression Result;
      if (T != "void")
        Result = temporary(T, L);
      branch(std::move(Condition), Yes, No, L, C->getCond());
      label(Yes, L);
      auto True = expression(C->getTrueExpr());
      if (T != "void")
        assign(Result, std::move(True), L);
      jump(End, L);
      label(No, L);
      auto False = expression(C->getFalseExpr());
      if (T != "void")
        assign(Result, std::move(False), L);
      jump(End, L);
      label(End, L);
      return Result;
    }
    reject(L, E->getStmtClassName(),
           "Expression has no supported core lowering.");
  }
  void discard(const Expr *E) {
    if (const auto *P = dyn_cast<ParenExpr>(E)) {
      discard(P->getSubExpr());
      return;
    }
    if (const auto *W = dyn_cast<ExprWithCleanups>(E)) {
      discard(W->getSubExpr());
      return;
    }
    if (const auto *B = dyn_cast<BinaryOperator>(E);
        B && B->getOpcode() == BO_Comma) {
      discard(B->getLHS());
      discard(B->getRHS());
      return;
    }
    if (E->isGLValue()) {
      if (isa<DeclRefExpr>(E))
        return;
      if (const auto *M = dyn_cast<MemberExpr>(E)) {
        discard(M->getBase());
        return;
      }
      if (const auto *C = dyn_cast<ConditionalOperator>(E)) {
        auto L = E->getExprLoc();
        auto Condition = expression(C->getCond());
        auto Yes = labelName(), No = labelName(), End = labelName();
        branch(std::move(Condition), Yes, No, L, C->getCond());
        label(Yes, L);
        discard(C->getTrueExpr());
        jump(End, L);
        label(No, L);
        discard(C->getFalseExpr());
        jump(End, L);
        label(End, L);
        return;
      }
    }
    expression(E);
  }
  Expression initialElement(Expression Place, QualType Element, unsigned N,
                            SourceLocation L) {
    auto T = type(Element, L);
    // Initialization may write const destination objects. As for scalar/record
    // locals, their storage is unqualified internally; source accesses still
    // carry Clang's const-qualified decay/reference types.
    return index(decay(std::move(Place), "ptr:" + T, L),
                 A.literal(llvm::APSInt(llvm::APInt(32, N), false), "int", L), T, L);
  }
  void destroy(Expression Place, QualType T, SourceLocation L) {
    if (!A.S.coreV2() || !needsDestruction(T))
      return;
    if (const auto *Array = A.Context.getAsConstantArrayType(T)) {
      auto Element = Array->getElementType();
      auto Count = Array->getSize().getLimitedValue(65537);
      if (!Count || Count > 65536 || A.storageUnits(T) > 200000)
        reject(L, "array destruction", "Array destruction exceeds the storage limit.");
      for (unsigned N = Count; N > 0; --N) {
        A.chargeExpansion(1, L);
        destroy(initialElement(Place, Element, N - 1, L), Element, L);
      }
      return;
    }
    const auto *Record = T->getAsCXXRecordDecl();
    if (!Record || !Record->getDefinition())
      reject(L, "destruction", "A complete admitted record is required.");
    json::Array Args;
    Args.push_back(snapshot(address(std::move(Place), T.getUnqualifiedType(), L), L));
    chargeCall(Args, L);
    Body.push_back(json::Object{{"op", "call"},
                                {"callee", A.destructionName(Record)},
                                {"args", std::move(Args)},
                                {"loc", A.loc(L)}});
  }
  void own(Expression Place, QualType T, SourceLocation L, CleanupFrame &Frame) {
    if (!A.S.coreV2() || !needsDestruction(T))
      return;
    // Register only after initialization completes. A containing temporary's
    // initializer can itself complete other temporaries before this object.
    auto Live = temporary("bool", L);
    A.chargeExpansion(1 + generatedNodes(Live), L);
    LiveFlags.push_back(Live);
    assign(Live, boolean(true, L), L);
    Frame.Objects.push_back({std::move(Place), std::move(Live), T, L});
  }
  void cleanup(const CleanupFrame &Frame) {
    for (auto I = Frame.Objects.rbegin(); I != Frame.Objects.rend(); ++I) {
      auto Yes = labelName(), End = labelName();
      A.chargeExpansion(4 + generatedNodes(I->Live), I->Location);
      branch(I->Live, Yes, End, I->Location);
      label(Yes, I->Location);
      assign(I->Live, boolean(false, I->Location), I->Location);
      destroy(I->Place, I->Type, I->Location);
      jump(End, I->Location);
      label(End, I->Location);
    }
  }
  void cleanupScopes(std::size_t Depth) {
    for (std::size_t I = Scopes.size(); I > Depth; --I)
      cleanup(Scopes[I - 1]);
  }
  void beginFullExpression() { FullExpressions.emplace_back(); }
  void endFullExpression() {
    cleanup(FullExpressions.back());
    FullExpressions.pop_back();
  }
  void scopedStatement(const Stmt *S) {
    Scopes.emplace_back();
    statement(S);
    if (Open)
      cleanup(Scopes.back());
    Scopes.pop_back();
  }
  Expression condition(const Expr *E) {
    if (!A.S.coreV2())
      return expression(E);
    beginFullExpression();
    // Finish the contextual bool conversion before destruction can mutate its
    // source. Branches use only this captured value after cleanup.
    auto Value = snapshot(cast(expression(E), "bool", E->getExprLoc()), E->getExprLoc());
    endFullExpression();
    return Value;
  }
  void expressionStatement(const Expr *E) {
    beginFullExpression();
    discard(E);
    endFullExpression();
  }
  void destructionMembers() {
    auto L = DestroyedRecord->getLocation();
    std::vector<const FieldDecl *> Fields;
    for (const auto *Field : DestroyedRecord->getDefinition()->fields())
      Fields.push_back(Field);
    for (auto I = Fields.rbegin(); I != Fields.rend(); ++I) {
      const auto *Field = *I;
      if (!needsDestruction(Field->getType()))
        continue;
      Expression Member{{"kind", "member"},
                        {"type", type(Field->getType(), L)},
                        {"name", A.name(Field)},
                        {"args", json::Array{dereference(*ThisPointer, L)}},
                        {"loc", A.loc(L)}};
      destroy(std::move(Member), Field->getType(), L);
    }
  }
  void initializeZero(Expression Place, QualType T, SourceLocation L) {
    if (const auto *Array = A.Context.getAsConstantArrayType(T)) {
      auto Element = Array->getElementType();
      auto Count = Array->getSize().getLimitedValue(65537);
      for (unsigned N = 0; N < Count; ++N)
        initializeZero(initialElement(Place, Element, N, L), Element, L);
      return;
    }
    assign(std::move(Place), A.zero(T, L), L);
  }
  bool recordValue(QualType T) const {
    return A.S.coreV2() && T->isRecordType();
  }
  bool emptyRecord(QualType T) const {
    const auto *R = T->getAsCXXRecordDecl();
    return A.S.coreV2() && R && R->getDefinition() &&
           R->getDefinition()->field_empty();
  }
  bool aggregateValue(QualType T) const {
    return A.S.coreV2() && (T->isRecordType() || T->isArrayType());
  }
  QualType parameterType(QualType T) const {
    return recordValue(T) ? A.Context.getPointerType(T.getUnqualifiedType()) : T;
  }
  Expression parameter(QualType T, SourceLocation L) {
    auto Name = Prefix + "p" + std::to_string(++Serial);
    auto Kind = type(T, L);
    auto Place = variable(Name, Kind, L);
    A.chargeExpansion(1 + generatedNodes(Place), L);
    Parameters.push_back(json::Object{
        {"name", Name}, {"type", Kind}, {"loc", A.loc(L)}});
    return Place;
  }
  Expression objectTemporary(QualType T, SourceLocation L) {
    auto Kind = type(T, L);
    // These new caller-owned objects must count against the same bounded
    // expansion budget as their initialization and call instructions.
    A.chargeExpansion(A.storageUnits(T), L);
    return temporary(Kind, L);
  }
  Expression argument(const Expr *Arg, QualType ParameterType) {
    auto L = Arg->getExprLoc();
    if (ParameterType->isReferenceType())
      return snapshot(bind(Arg, ParameterType), L);
    if (recordValue(ParameterType)) {
      // A by-value parameter is a separate object even when the same lvalue
      // supplies multiple arguments. A prvalue constructs here directly; the
      // selected implicit copy/move expression retains intentional copies.
      auto Place = objectTemporary(ParameterType, L);
      initialize(Place, Arg, L);
      return snapshot(address(std::move(Place),
                              ParameterType.getUnqualifiedType(), L), L);
    }
    return expression(Arg);
  }
  void chargeCall(const json::Array &Args, SourceLocation L) {
    std::size_t Nodes = 1;
    for (const auto &Arg : Args)
      Nodes += generatedNodes(*Arg.getAsObject());
    A.chargeExpansion(Nodes, L);
  }
  void checkTemporary(const MaterializeTemporaryExpr *M, SourceLocation L) {
    if (!fullExpressionTemporary(M, A.Context) || FullExpressions.empty())
      reject(L, "temporary lifetime", "A checked enclosing full-expression is required.");
  }
  Expression materializeTemporary(const MaterializeTemporaryExpr *M, SourceLocation L) {
    if (fullExpressionTemporary(M, A.Context)) {
      checkTemporary(M, L);
      return materialize(M->getSubExpr(), L);
    }
    const auto *Owner = automaticTemporaryOwner(M, A.Context);
    if (!Owner || !ActiveReferenceInitializer ||
        Owner != ActiveReferenceInitializer->Variable ||
        ActiveReferenceInitializer->ScopeIndex >= Scopes.size())
      reject(L, "temporary lifetime", "The exact automatic reference initializer and scope are required.");
    return materialize(M->getSubExpr(), L, ActiveReferenceInitializer->ScopeIndex);
  }
  Expression materialize(const Expr *Init, SourceLocation L,
                         std::optional<std::size_t> ScopeIndex = std::nullopt) {
    // One addressable destination per evaluation, including a discarded array
    // for which Clang omitted an MTE. Elements initialize directly in this
    // complete object; reusing a filler AST never reuses an element lifetime.
    auto Place = aggregateValue(Init->getType())
                     ? objectTemporary(Init->getType(), L)
                     : temporary(type(Init->getType(), L), L);
    initialize(Place, Init, L);
    if (A.S.coreV2() && needsDestruction(Init->getType())) {
      // Resolve the frame after recursive initialization; vector growth must
      // never invalidate a retained frame pointer or reference.
      if (ScopeIndex) {
        if (*ScopeIndex >= Scopes.size())
          reject(L, "temporary lifetime", "The extending reference scope no longer exists.");
        own(Place, Init->getType(), L, Scopes[*ScopeIndex]);
      } else {
        if (FullExpressions.empty())
          reject(L, "temporary lifetime", "No enclosing full-expression cleanup frame.");
        own(Place, Init->getType(), L, FullExpressions.back());
      }
    }
    return Place;
  }
  void construct(Expression Place, QualType T, const CXXConstructExpr *C,
                 SourceLocation L) {
    if (const auto *Array = A.Context.getAsConstantArrayType(T)) {
      auto Element = Array->getElementType();
      auto Count = Array->getSize().getLimitedValue(65537);
      if (!Count || Count > 65536 || A.storageUnits(T) > 200000)
        reject(L, "array construction", "Array construction exceeds the storage limit.");
      for (unsigned N = 0; N < Count; ++N) {
        A.chargeExpansion(1, L);
        construct(initialElement(Place, Element, N, L), Element, C, L);
      }
      return;
    }
    const auto *Constructor = C->getConstructor();
    if (!T->isRecordType() ||
        T->getAsCXXRecordDecl()->getCanonicalDecl() !=
            Constructor->getParent()->getCanonicalDecl() ||
        Place.getString("type") != type(T, L))
      reject(L, "construction", "Constructor and destination types differ.");
    if (Constructor->isImplicit() && Constructor->isTrivial()) {
      if (Constructor->isCopyOrMoveConstructor() && C->getNumArgs() == 1) {
        auto Source = expression(C->getArg(0));
        // An empty copy still evaluates its source, including calls and
        // temporary lifetimes, but has no semantic data fields to copy.
        if (!emptyRecord(T))
          assign(std::move(Place), std::move(Source), L);
        return;
      }
      if (Constructor->isDefaultConstructor() && !C->getNumArgs()) {
        if (C->requiresZeroInitialization())
          initializeZero(std::move(Place), T, L);
        return;
      }
    }
    if (Constructor->isTrivial() && defaultedCopyOrMoveConstructor(Constructor) &&
        C->getNumArgs() == 1) {
      // A trivial copy preserves stored fields, including pointers into the
      // source. Empty records only retain the source evaluation effects.
      auto Source = expression(C->getArg(0));
      if (!emptyRecord(T))
        assign(std::move(Place), std::move(Source), L);
      return;
    }
    if (Constructor->isTrivial() && defaultedLifecycle(Constructor) &&
        Constructor->isDefaultConstructor() && !C->getNumArgs()) {
      // Clang can omit the body of an explicitly defaulted trivial constructor.
      // Its declaration still determines default versus value initialization.
      if (C->requiresZeroInitialization())
        initializeZero(std::move(Place), T, L);
      return;
    }
    if (!supportedConstructor(Constructor) || !Constructor->hasBody() ||
        C->getNumArgs() != Constructor->getNumParams())
      reject(L, "construction", "Unsupported selected constructor or argument list.");
    if (C->requiresZeroInitialization())
      initializeZero(Place, T, L);
    json::Array Args;
    Args.push_back(snapshot(address(std::move(Place), T.getUnqualifiedType(), L), L));
    for (unsigned I = 0; I < C->getNumArgs(); ++I)
      Args.push_back(argument(C->getArg(I),
                              Constructor->getParamDecl(I)->getType()));
    chargeCall(Args, L);
    Body.push_back(json::Object{{"op", "call"},
                                {"callee", A.name(Constructor)},
                                {"args", std::move(Args)},
                                {"loc", A.loc(L)}});
  }
  void constructorInitializers(const CXXConstructorDecl *C) {
    auto SavedReceiver = DefaultReceiver;
    DefaultReceiver = InitializationReceiver{C->getParent()->getCanonicalDecl(), *ThisPointer};
    auto RestoreReceiver = llvm::make_scope_exit([&] { DefaultReceiver = std::move(SavedReceiver); });
    std::map<const Decl *, const Expr *> Initializers;
    auto L = C->getLocation();
    for (const auto *I : C->inits()) {
      if (!I->isMemberInitializer() || I->isPackExpansion() ||
          I->getMember()->getParent() != C->getParent() || !I->getInit() ||
          !Initializers.emplace(I->getMember()->getCanonicalDecl(), I->getInit()).second)
        reject(L, "constructor initializer", "Unsupported or duplicate field initializer.");
    }
    for (const auto *Field : C->getParent()->fields()) {
      auto Found = Initializers.find(Field->getCanonicalDecl());
      if (Found == Initializers.end()) {
        auto Element = Field->getType();
        while (const auto *Array = A.Context.getAsConstantArrayType(Element))
          Element = Array->getElementType();
        if (Element->isRecordType())
          reject(L, "constructor initializer", "Missing semantic class-member initialization.");
        continue;
      }
      Expression Member{{"kind", "member"},
                        {"type", type(Field->getType(), L)},
                        {"name", A.name(Field)},
                        {"args", json::Array{dereference(*ThisPointer, L)}},
                        {"loc", A.loc(L)}};
      beginFullExpression();
      initialize(std::move(Member), Found->second, L);
      endFullExpression();
    }
  }
  void initialize(Expression Place, const Expr *Init, SourceLocation L) {
    Init = Init->IgnoreParens();
    if (const auto *W = dyn_cast<ExprWithCleanups>(Init)) {
      initialize(std::move(Place), W->getSubExpr(), L);
      return;
    }
    if (A.S.coreV2()) {
      if (const auto *Default = dyn_cast<CXXDefaultInitExpr>(Init)) {
        if (!DefaultReceiver ||
            Default->getField()->getParent()->getCanonicalDecl() != DefaultReceiver->Record ||
            Place.getString("type") != type(Default->getType(), L))
          reject(L, "default member initializer", "The selected default requires its actual owning destination.");
        auto SavedThis = ThisPointer;
        ThisPointer = DefaultReceiver->Pointer;
        auto RestoreThis = llvm::make_scope_exit([&] { ThisPointer = std::move(SavedThis); });
        initialize(std::move(Place), Default->getExpr(), L);
        return;
      }
      if (const auto *M = dyn_cast<MaterializeTemporaryExpr>(Init)) {
        checkTemporary(M, L);
        initialize(std::move(Place), M->getSubExpr(), L);
        return;
      }
      if (const auto *B = dyn_cast<CXXBindTemporaryExpr>(Init)) {
        initialize(std::move(Place), B->getSubExpr(), L);
        return;
      }
      if (const auto *C = dyn_cast<CXXConstructExpr>(Init)) {
        construct(std::move(Place), Init->getType(), C, L);
        return;
      }
      if (const auto *C = dyn_cast<CastExpr>(Init);
          C && C->getCastKind() == CK_UserDefinedConversion &&
          C->isPRValue() && recordValue(C->getType())) {
        const auto *Selected = userConversionCall(C, A.Context);
        if (!Selected)
          reject(L, "user conversion", "Unsupported object conversion wrapper.");
        // A prvalue initializes this destination. A reference conversion takes
        // the separate selected copy/move path and never creates another owner.
        initialize(std::move(Place), Selected, L);
        return;
      }
      if (const auto *C = dyn_cast<CastExpr>(Init);
          C && C->getCastKind() == CK_ConstructorConversion) {
        const auto *Construction = constructorConversion(C, A.Context);
        if (!Construction)
          reject(L, "constructor conversion", "Unsupported constructor conversion wrapper.");
        initialize(std::move(Place), Construction, L);
        return;
      }
    }
    if (const auto *C = dyn_cast<ConstantExpr>(Init); C && A.S.coreV2()) {
      initialize(std::move(Place), C->getSubExpr(), L);
      return;
    }
    if (const auto *C = dyn_cast<CastExpr>(Init);
        A.S.coreV2() && C && C->getCastKind() == CK_NoOp && C->isPRValue() &&
        aggregateValue(C->getType()) &&
        A.Context.hasSameUnqualifiedType(C->getType(), C->getSubExpr()->getType())) {
      // C++17 initializes the actual record or array destination through typed
      // construction wrappers. A real copy still takes the value
      // path once recursion reaches its constructor or source object.
      initialize(std::move(Place), C->getSubExpr(), L);
      return;
    }
    if (Init->isPRValue() && aggregateValue(Init->getType())) {
      if (const auto *Call = dyn_cast<CallExpr>(Init); Call && recordValue(Init->getType())) {
        call(Call, std::move(Place));
        return;
      }
      if (const auto *B = dyn_cast<BinaryOperator>(Init);
          B && B->getOpcode() == BO_Comma) {
        discard(B->getLHS());
        initialize(std::move(Place), B->getRHS(), L);
        return;
      }
      if (const auto *C = dyn_cast<ConditionalOperator>(Init)) {
        auto Condition = expression(C->getCond());
        auto Yes = labelName(), No = labelName(), End = labelName();
        branch(std::move(Condition), Yes, No, L, C->getCond());
        label(Yes, L);
        initialize(Place, C->getTrueExpr(), L);
        jump(End, L);
        label(No, L);
        initialize(std::move(Place), C->getFalseExpr(), L);
        jump(End, L);
        label(End, L);
        return;
      }
    }
    if (const auto *Loop = dyn_cast<ArrayInitLoopExpr>(Init); Loop && A.S.coreV2()) {
      const auto *Array = A.Context.getAsConstantArrayType(Loop->getType());
      const auto *Common = Loop->getCommonExpr();
      const auto *Source = Common ? Common->getSourceExpr() : nullptr;
      if (!defaultedCopyOrMoveConstructor(dyn_cast<CXXConstructorDecl>(Function)) ||
          !Array || !Source || !Source->isGLValue() ||
          Source->getValueKind() != Common->getValueKind() ||
          (llvm::cast<CXXConstructorDecl>(Function)->isCopyConstructor() && !Source->isLValue()) ||
          ArraySources.count(Common) ||
          Place.getString("type") != type(Loop->getType(), L))
        reject(L, "array initialization", "Expected admitted semantic member-array copying or moving.");
      auto Count = Array->getSize().getLimitedValue(65537);
      if (!Count || Count > 65536 || A.storageUnits(Loop->getType()) > 200000)
        reject(L, "array initialization", "Array initialization exceeds the storage limit.");
      // Capture the source address once before introducing the inner index.
      // This retains the outer index for a nested array's common expression.
      auto Pointer = snapshot(address(lvalue(Source), Source->getType(), L), L);
      ArraySources.emplace(Common, dereference(std::move(Pointer), L));
      auto RestoreSource = llvm::make_scope_exit([&] { ArraySources.erase(Common); });
      for (uint64_t N = 0; N < Count; ++N) {
        A.chargeExpansion(1, L);
        ArrayIndices.push_back({N, L});
        auto RestoreIndex = llvm::make_scope_exit([&] { ArrayIndices.pop_back(); });
        initialize(initialElement(Place, Array->getElementType(), unsigned(N), L),
                   Loop->getSubExpr(), L);
      }
      return;
    }
    if (A.S.coreV2())
      if (const auto *Array = A.Context.getAsConstantArrayType(Init->getType())) {
        const auto *I = dyn_cast<InitListExpr>(Init);
        if (!I) {
          if (isa<ImplicitValueInitExpr>(Init)) {
            initializeZero(std::move(Place), Init->getType(), L);
            return;
          }
          reject(L, "array initialization", "Only semantic initializer lists and value initialization are supported.");
        }
        if (I->isSyntacticForm() && I->getSemanticForm())
          I = I->getSemanticForm();
        auto Element = Array->getElementType();
        auto Count = Array->getSize().getLimitedValue(65537);
        if (I->getNumInits() > Count)
          reject(L, "array initialization", "Too many semantic initializers.");
        for (unsigned N = 0; N < Count; ++N) {
          auto Target = initialElement(Place, Element, N, L);
          const Expr *Value = N < I->getNumInits() ? I->getInit(N) : I->getArrayFiller();
          if (Value)
            initialize(std::move(Target), Value, L);
          else
            initializeZero(std::move(Target), Element, L);
        }
        return;
      }
    if (const auto *I = dyn_cast<InitListExpr>(Init);
        I && I->getType()->isRecordType()) {
      if (I->isSyntacticForm() && I->getSemanticForm())
        I = I->getSemanticForm();
      const auto *Record = I->getType()->getAsCXXRecordDecl()->getDefinition();
      auto SavedReceiver = DefaultReceiver;
      if (A.S.coreV2())
        DefaultReceiver = InitializationReceiver{
            Record->getCanonicalDecl(), address(Place, I->getType().getUnqualifiedType(), L)};
      auto RestoreReceiver = llvm::make_scope_exit([&] { DefaultReceiver = std::move(SavedReceiver); });
      // Explicit clauses keep the enclosing expression's this. Only a selected
      // default temporarily rebinds it to this aggregate's construction storage.
      auto Fields = Record->fields();
      if (I->getNumInits() != std::distance(Fields.begin(), Fields.end()))
        reject(L, "aggregate initialization",
               "Incomplete semantic field initializer list.");
      unsigned Index = 0;
      // Initialization is observable through earlier destination subobjects.
      // Store each field before evaluating the next clause, including nested
      // lists. Ordinary record copy/assignment still uses the value path.
      for (const auto *Field : Fields) {
        Expression Member{{"kind", "member"},
                          {"type", type(Field->getType(), L)},
                          {"name", A.name(Field)},
                          {"args", json::Array{json::Object(Place)}},
                          {"loc", A.loc(L)}};
        initialize(std::move(Member), I->getInit(Index++), L);
      }
      return;
    }
    assign(std::move(Place), expression(Init), L);
  }
  Expression localStorage(const VarDecl *V) {
    auto Found = Storage.find(V->getCanonicalDecl());
    if (Found != Storage.end()) {
      // Reference identity is represented by dereferencing its hidden pointer.
      if (V->getType()->isReferenceType())
        return *(*Found->second.getArray("args"))[0].getAsObject();
      return Found->second;
    }
    auto L = V->getLocation();
    auto Place = temporary(type(V->getType(), L), L);
    Storage.emplace(V->getCanonicalDecl(),
                    V->getType()->isReferenceType()
                        ? dereference(Place, L) : Place);
    return Place;
  }
  void declaration(const VarDecl *V) {
    auto L = V->getLocation();
    auto Place = localStorage(V);
    beginFullExpression();
    if (V->getType()->isReferenceType()) {
      auto Previous = ActiveReferenceInitializer;
      auto Restore = llvm::make_scope_exit([&] { ActiveReferenceInitializer = Previous; });
      ActiveReferenceInitializer.reset();
      if (A.S.coreV2() && V->getKind() == Decl::Var && !V->isImplicit() &&
          V->isLocalVarDecl() && V->hasLocalStorage()) {
        if (Scopes.empty())
          reject(L, "reference lifetime", "An automatic reference requires a lexical scope.");
        ActiveReferenceInitializer = ReferenceInitializer{V->getCanonicalDecl(), Scopes.size() - 1};
      }
      assign(Place, bind(V->getInit(), V->getType()), L);
    } else {
      bool DefaultOnly = false;
      if (const auto *C = dyn_cast_or_null<CXXConstructExpr>(V->getInit()))
        DefaultOnly = C->getConstructor()->isDefaultConstructor() &&
                      C->getConstructor()->isTrivial() && !C->getNumArgs() &&
                      !C->requiresZeroInitialization();
      if (V->getInit() && !DefaultOnly)
        initialize(Place, V->getInit(), L);
      if (A.S.coreV2() && needsDestruction(V->getType()))
        own(Place, V->getType(), L, Scopes.back());
    }
    endFullExpression();
  }
  void registerSwitchStorage(const Stmt *S) {
    if (!S)
      return;
    if (const auto *D = dyn_cast<DeclStmt>(S))
      for (const auto *Declaration : D->decls())
        if (const auto *V = dyn_cast<VarDecl>(Declaration))
          localStorage(V);
    for (const auto *Child : S->children())
      registerSwitchStorage(Child);
  }
  bool markSwitchEntries(const Stmt *S, SwitchFrame &Frame) {
    if (!S || isa<SwitchStmt>(S))
      return false;
    const auto *Case = dyn_cast<SwitchCase>(S);
    bool HasEntry = Case && Frame.Labels.count(Case);
    for (const auto *Child : S->children())
      HasEntry |= markSwitchEntries(Child, Frame);
    if (HasEntry)
      Frame.Entries.insert(S);
    return HasEntry;
  }
  void switchStatement(const SwitchStmt *S) {
    auto L = S->getSwitchLoc();
    Scopes.emplace_back();
    statement(S->getInit());
    if (S->getConditionVariable())
      declaration(S->getConditionVariable());
    beginFullExpression();
    auto Selector = snapshot(expression(S->getCond()), L);
    auto SelectorType = Selector.getString("type")->str();
    if (!integerBits(SelectorType))
      reject(L, "switch selector", "Only supported integral selectors are supported.");
    if (integerBits(SelectorType) < 32) {
      SelectorType = "int";
      Selector = snapshot(cast(std::move(Selector), SelectorType, L), L);
    }
    endFullExpression();
    auto Normalize = [&](const llvm::APSInt &Value) {
      auto Result = Value.extOrTrunc(integerBits(SelectorType));
      Result.setIsUnsigned(unsignedInteger(SelectorType));
      return Result;
    };
    std::optional<llvm::APSInt> Known;
    APValue Constant;
    if (S->getCond()->isCXX11ConstantExpr(A.Context, &Constant) && Constant.isInt())
      Known = Normalize(Constant.getInt());
    SwitchFrame Frame;
    std::vector<std::pair<const CaseStmt *, llvm::APSInt>> Cases;
    auto End = labelName();
    auto Default = End;
    for (const auto *C = S->getSwitchCaseList(); C; C = C->getNextSwitchCase()) {
      auto Name = labelName();
      Frame.Labels.emplace(C, Name);
      if (const auto *Case = dyn_cast<CaseStmt>(C)) {
        APValue Value;
        if (Case->getRHS() ||
            !Case->getLHS()->isCXX11ConstantExpr(A.Context, &Value) || !Value.isInt())
          reject(Case->getBeginLoc(), "case value", "A checked constant integer case is required.");
        Cases.emplace_back(Case, Normalize(Value.getInt()));
      } else {
        Default = Name;
      }
    }
    markSwitchEntries(S->getBody(), Frame);
    registerSwitchStorage(S->getBody());
    if (Known) {
      auto Destination = Default;
      for (const auto &Case : Cases)
        if (Case.second == *Known) {
          Destination = Frame.Labels.at(Case.first);
          break;
        }
      jump(Destination, L);
    } else if (Cases.empty()) {
      jump(Default, L);
    } else {
      for (std::size_t I = 0; I < Cases.size(); ++I) {
        const auto &Case = Cases[I];
        auto Next = I + 1 == Cases.size() ? Default : labelName();
        branch(binary("==", Selector,
                      A.literal(Case.second, SelectorType, Case.first->getBeginLoc()),
                      "bool", L), Frame.Labels.at(Case.first), Next, L);
        if (I + 1 != Cases.size())
          label(Next, L);
      }
    }
    // Dispatch bypasses this entry. Source case labels reconnect reachable
    // portions of the body; ordinary pre-case effects are pruned afterwards.
    label(labelName(), L);
    Switches.push_back(std::move(Frame));
    ControlTargets.push_back({End, {}, Scopes.size(), Scopes.size()});
    scopedStatement(S->getBody());
    ControlTargets.pop_back();
    Switches.pop_back();
    if (Open)
      jump(End, L);
    label(End, L);
    cleanup(Scopes.back());
    Scopes.pop_back();
  }
  void statement(const Stmt *S) {
    if (!S)
      return;
    auto L = S->getBeginLoc();
    if (!Open) {
      if (Switches.empty() || !Switches.back().Entries.count(S))
        return; // The independent allowlist still inspects dead code.
      label(labelName(), L);
    }
    if (const auto *Case = dyn_cast<SwitchCase>(S); Case && A.S.coreV2()) {
      if (Switches.empty() || !Switches.back().Labels.count(Case))
        reject(L, "case label", "No enclosing supported switch.");
      label(Switches.back().Labels.at(Case), L);
      statement(Case->getSubStmt());
      return;
    }
    if (const auto *Switch = dyn_cast<SwitchStmt>(S); Switch && A.S.coreV2()) {
      switchStatement(Switch);
      return;
    }
    if (const auto *Attributed = dyn_cast<AttributedStmt>(S); Attributed && A.S.coreV2()) {
      // The allowlist admits only validated fallthrough on a null statement.
      statement(Attributed->getSubStmt());
      return;
    }
    if (const auto *C = dyn_cast<CompoundStmt>(S)) {
      Scopes.emplace_back();
      for (const auto *Child : C->body())
        statement(Child);
      if (Open)
        cleanup(Scopes.back());
      Scopes.pop_back();
    } else if (const auto *D = dyn_cast<DeclStmt>(S)) {
      for (const auto *Decl : D->decls()) {
        if (const auto *V = dyn_cast<VarDecl>(Decl))
          declaration(V);
        else if (!isa<CXXRecordDecl>(Decl) &&
                 !(A.S.coreV2() &&
                   isa<TypedefNameDecl, EnumDecl, StaticAssertDecl>(Decl)))
          reject(L, "declaration statement", "Unsupported local declaration.");
      }
    } else if (const auto *R = dyn_cast<ReturnStmt>(S)) {
      json::Object Return{{"op", "return"}, {"loc", A.loc(L)}};
      beginFullExpression();
      if (R->getRetValue() && ResultPlace) {
        initialize(*ResultPlace, R->getRetValue(), L);
      } else if (R->getRetValue()) {
        auto Value = Function->getReturnType()->isReferenceType()
                         ? bind(R->getRetValue(), Function->getReturnType())
                         : expression(R->getRetValue());
        if (!Value.empty())
          Return["value"] = A.S.coreV2() ? snapshot(std::move(Value), L)
                                         : std::move(Value);
      }
      endFullExpression();
      cleanupScopes(0);
      if (DestroyedRecord) {
        jump(DestructionEnd, L);
      } else {
        Body.push_back(std::move(Return));
        Open = false;
      }
    } else if (const auto *I = dyn_cast<IfStmt>(S)) {
      Scopes.emplace_back();
      statement(I->getInit());
      if (I->getConditionVariable())
        declaration(I->getConditionVariable());
      auto Condition = condition(I->getCond());
      auto Yes = labelName(), No = labelName(), End = labelName();
      branch(std::move(Condition), Yes, No, L, I->getCond());
      label(Yes, L);
      scopedStatement(I->getThen());
      bool ThenOpen = Open;
      if (Open)
        jump(End, L);
      label(No, L);
      scopedStatement(I->getElse());
      bool ElseOpen = Open;
      if (Open)
        jump(End, L);
      if (ThenOpen || ElseOpen) {
        label(End, L);
        cleanup(Scopes.back());
      }
      Scopes.pop_back();
    } else if (const auto *W = dyn_cast<WhileStmt>(S)) {
      Scopes.emplace_back();
      auto Test = labelName(), Loop = labelName(), End = labelName();
      jump(Test, L);
      label(Test, L);
      if (W->getConditionVariable())
        declaration(W->getConditionVariable());
      branch(condition(W->getCond()), Loop, End, L, W->getCond());
      label(Loop, L);
      ControlTargets.push_back({End, Test, Scopes.size(), Scopes.size() - 1});
      scopedStatement(W->getBody());
      ControlTargets.pop_back();
      if (Open) {
        cleanup(Scopes.back());
        jump(Test, L);
      }
      label(End, L);
      cleanup(Scopes.back());
      Scopes.pop_back();
    } else if (const auto *D = dyn_cast<DoStmt>(S)) {
      auto Loop = labelName(), Test = labelName(), End = labelName();
      jump(Loop, L);
      label(Loop, L);
      ControlTargets.push_back({End, Test, Scopes.size(), Scopes.size()});
      scopedStatement(D->getBody());
      ControlTargets.pop_back();
      if (Open)
        jump(Test, L);
      label(Test, L);
      branch(condition(D->getCond()), Loop, End, L, D->getCond());
      label(End, L);
    } else if (const auto *F = dyn_cast<ForStmt>(S)) {
      Scopes.emplace_back(); // for-init storage outlives every iteration.
      statement(F->getInit());
      Scopes.emplace_back(); // The condition variable also lives through Step.
      auto Test = labelName(), Loop = labelName(), Step = labelName(),
           End = labelName();
      jump(Test, L);
      label(Test, L);
      if (F->getConditionVariable())
        declaration(F->getConditionVariable());
      if (F->getCond())
        branch(condition(F->getCond()), Loop, End, L, F->getCond());
      else
        jump(Loop, L);
      label(Loop, L);
      ControlTargets.push_back({End, Step, Scopes.size(), Scopes.size()});
      scopedStatement(F->getBody());
      ControlTargets.pop_back();
      if (Open)
        jump(Step, L);
      label(Step, L);
      if (F->getInc())
        expressionStatement(F->getInc());
      cleanup(Scopes.back());
      jump(Test, L);
      label(End, L);
      cleanup(Scopes.back());
      Scopes.pop_back();
      cleanup(Scopes.back());
      Scopes.pop_back();
    } else if (isa<BreakStmt, ContinueStmt>(S)) {
      if (isa<BreakStmt>(S)) {
        if (ControlTargets.empty())
          reject(L, "break", "No enclosing supported loop or switch.");
        cleanupScopes(ControlTargets.back().BreakDepth);
        jump(ControlTargets.back().Break, L);
      } else {
        auto Loop = std::find_if(ControlTargets.rbegin(), ControlTargets.rend(),
                                 [](const auto &Target) { return !Target.Continue.empty(); });
        if (Loop == ControlTargets.rend())
          reject(L, "continue", "No enclosing supported loop.");
        cleanupScopes(Loop->ContinueDepth);
        jump(Loop->Continue, L);
      }
    } else if (const auto *E = dyn_cast<Expr>(S)) {
      expressionStatement(E);
    } else if (!isa<NullStmt>(S)) {
      reject(L, S->getStmtClassName(),
             "Statement has no supported core lowering.");
    }
  }

public:
  FunctionLowering(Adapter &A, const FunctionDecl *F) : A(A), Function(F) {
    Prefix = "nct_f" + digest(A.name(F)).substr(0, 12) + "_";
  }
  FunctionLowering(Adapter &A, const CXXRecordDecl *R)
      : A(A), Function(nullptr), DestroyedRecord(R->getDefinition()) {
    if (const auto *D = DestroyedRecord->getDestructor();
        D && !D->isImplicit() && !defaultedLifecycle(D)) {
      if (!ordinaryDestructor(D) || !D->hasBody())
        reject(D->getLocation(), "destructor", "An admitted owned destructor definition is required.");
      Function = D->getDefinition();
    }
    Prefix = "nct_f" + digest(A.destructionName(R)).substr(0, 12) + "_";
  }
  json::Object run() {
    auto L = Function ? Function->getLocation() : DestroyedRecord->getLocation();
    auto Name = DestroyedRecord ? A.destructionName(DestroyedRecord)
                                : A.name(Function);
    auto ResultType = DestroyedRecord ? std::string("void")
                                     : type(Function->getReturnType(), L, true);
    if (DestroyedRecord) {
      ThisPointer = parameter(A.Context.getPointerType(
                                  A.Context.getRecordType(DestroyedRecord)), L);
      DestructionEnd = labelName();
    } else {
      if (recordValue(Function->getReturnType()))
        ResultPlace = dereference(
            parameter(parameterType(Function->getReturnType()), L), L);
      if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
          Method && !Method->isStatic()) {
        if (!A.S.coreV2() ||
            (!callableMethod(Method) &&
             !supportedConstructor(dyn_cast<CXXConstructorDecl>(Method))))
          reject(L, "method", "Unsupported instance-method or constructor definition.");
        ThisPointer = parameter(Method->getThisType(), L);
      }
      for (const auto *P : Function->parameters()) {
        auto Place = parameter(parameterType(P->getType()), P->getLocation());
        Storage.emplace(P->getCanonicalDecl(),
                        (P->getType()->isReferenceType() ||
                         recordValue(P->getType()))
                            ? dereference(std::move(Place), P->getLocation())
                            : std::move(Place));
      }
    }
    Entry = labelName();
    label(Entry, L);
    Scopes.emplace_back(); // By-value parameters end after body locals.
    if (Function && !DestroyedRecord)
      for (unsigned N = 0; N < Function->getNumParams(); ++N) {
        unsigned I = A.S.coreV2() && reverseOperatorParameters(Function)
                         ? Function->getNumParams() - 1 - N : N;
        const auto *P = Function->getParamDecl(I);
        if (recordValue(P->getType()) && needsDestruction(P->getType()))
          own(storage(P, P->getLocation()), P->getType(), P->getLocation(), Scopes.back());
      }
    if (const auto *Constructor = dyn_cast_or_null<CXXConstructorDecl>(Function))
      constructorInitializers(Constructor);
    if (Function)
      statement(Function->getBody());
    if (DestroyedRecord) {
      if (Open) {
        cleanupScopes(0);
        jump(DestructionEnd, L);
      }
      label(DestructionEnd, L);
      destructionMembers();
      Body.push_back(json::Object{{"op", "return"}, {"loc", A.loc(L)}});
      Open = false;
    } else if (Open && reachable().count(Current)) {
      if (!Function->isMain() && ResultType != "void")
        reject(L, "function return",
               "A reachable nonvoid function path falls through without returning.");
      cleanupScopes(0);
      if (Function->isMain()) {
        Body.push_back(json::Object{{"op", "return"},
                                    {"value", A.zero(A.Context.IntTy, L)},
                                    {"loc", A.loc(L)}});
      } else {
        Body.push_back(json::Object{{"op", "return"}, {"loc", A.loc(L)}});
      }
      Open = false;
    }
    Scopes.pop_back();
    // Flags discovered in later branches must be initialized before any path
    // can inspect them. Reset-on-cleanup makes the same source place reusable
    // on the next loop iteration, without branch-local uninitialized flags.
    json::Array WithFlags;
    for (auto &V : Body) {
      bool IsEntry = V.getAsObject()->getString("op") == "label" &&
                     V.getAsObject()->getString("label") == Entry;
      WithFlags.push_back(std::move(V));
      if (IsEntry)
        for (const auto &Flag : LiveFlags) {
          auto Zero = boolean(false, L);
          A.chargeExpansion(1 + generatedNodes(Flag) + generatedNodes(Zero), L);
          WithFlags.push_back(json::Object{{"op", "assign"},
                                           {"target", json::Object(Flag)},
                                           {"value", std::move(Zero)},
                                           {"loc", A.loc(L)}});
        }
    }
    Body = std::move(WithFlags);
    auto Reachable = reachable();
    json::Array Pruned;
    bool Keep = false;
    for (auto &V : Body) {
      auto &O = *V.getAsObject();
      if (O.getString("op") == "label")
        Keep = Reachable.count(O.getString("label")->str());
      if (Keep)
        Pruned.push_back(std::move(V));
    }
    return json::Object{
        {"name", Name},
        {"result", ResultPlace ? "void" : ResultType},
        {"internal", DestroyedRecord || Function->getFormalLinkage() == Linkage::Internal},
        {"c_export", !DestroyedRecord && Function->isExternC() &&
                         Function->getFormalLinkage() != Linkage::Internal},
        {"params", std::move(Parameters)},
        {"locals", std::move(Locals)},
        {"body", std::move(Pruned)},
        {"loc", A.loc(L)}};
  }
};

json::Object Adapter::lower(FunctionDecl *Function) {
  return FunctionLowering(*this, Function).run();
}
json::Object Adapter::lowerDestruction(const CXXRecordDecl *Record) {
  return FunctionLowering(*this, Record).run();
}
} // namespace nct
