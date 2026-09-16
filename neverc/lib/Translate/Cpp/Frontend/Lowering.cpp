#include "Frontend.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtCXX.h"
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
  bool SharedConstruction = false;
  std::optional<Expression> BaseConstruction;
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
  struct AutomaticInitializer {
    const VarDecl *Variable;
    std::size_t ScopeIndex;
  };
  std::optional<AutomaticInitializer> ActiveAutomaticInitializer;
  const VarDecl *ActiveDynamicInitializer = nullptr;
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
    if (T == "float" || T == "double")
      return A.floatingLiteral(
          T == "float" ? llvm::APFloat(1.0f) : llvm::APFloat(1.0), L);
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
    if (const auto *W = dyn_cast<ExprWithCleanups>(E))
      return bind(W->getSubExpr(), ReferenceType);
    if (const auto *Default = dyn_cast<CXXDefaultInitExpr>(E)) {
      if (!DefaultReceiver ||
          Default->getField()->getParent()->getCanonicalDecl() != DefaultReceiver->Record ||
          !A.Context.hasSameType(Default->getField()->getType(), ReferenceType))
        reject(L, "default reference member", "The selected binding requires its actual construction receiver.");
      auto SavedThis = ThisPointer;
      ThisPointer = DefaultReceiver->Pointer;
      auto RestoreThis = llvm::make_scope_exit([&] { ThisPointer = std::move(SavedThis); });
      return bind(Default->getExpr(), ReferenceType);
    }
    auto Pointer = address(lvalue(E), E->getType(), L);
    return cast(std::move(Pointer), type(ReferenceType, L), L);
  }
  Expression fieldStorage(Expression Base, const FieldDecl *Field, SourceLocation L) {
    return Expression{{"kind", "member"}, {"type", type(Field->getType(), L)},
                      {"name", A.name(Field)}, {"args", json::Array{std::move(Base)}},
                      {"loc", A.loc(L)}};
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
    if (const auto *V = dyn_cast<VarDecl>(D);
        A.S.coreV2() && V && V->hasGlobalStorage() && V->getType()->isReferenceType()) {
      if (!A.StaticReferenceInitializers.count(V->getCanonicalDecl()))
        reject(L, "static reference storage", "No checked permanent binding exists.");
      return dereference(variable(A.name(V), type(V->getType(), L), L), L);
    }
    if (const auto *V = dyn_cast<VarDecl>(D);
        A.S.coreV2() && V && V->isStaticLocal()) {
      if (!A.StaticLocals.count(V->getCanonicalDecl()))
        reject(L, "static local storage", "No checked static definition exists.");
      return variable(A.name(V), type(V->getType(), L), L);
    }
    auto I = Storage.find(D->getCanonicalDecl());
    if (I != Storage.end())
      return I->second;
    if (const auto *V = dyn_cast<VarDecl>(D); V && !V->isLocalVarDeclOrParm())
      return variable(A.name(D), type(V->getType(), L), L);
    reject(L, "storage reference",
           "No supported local, parameter or global storage declaration.");
  }
  std::optional<Expression> staticMemberValue(const Expr *E) {
    if (!A.S.coreV2())
      return std::nullopt;
    const ValueDecl *Declaration = nullptr;
    const auto *Member = dyn_cast<MemberExpr>(E);
    NonOdrUseReason Reason = NOUR_None;
    if (const auto *R = dyn_cast<DeclRefExpr>(E)) {
      Declaration = R->getDecl();
      Reason = R->isNonOdrUse();
    } else if (Member) {
      Declaration = Member->getMemberDecl();
      Reason = Member->isNonOdrUse();
    }
    const auto *V = dyn_cast_or_null<VarDecl>(Declaration);
    if (!V)
      return std::nullopt;
    auto Found = A.StaticMemberValues.find(V->getCanonicalDecl());
    if (Found == A.StaticMemberValues.end())
      return std::nullopt;
    auto L = E->getExprLoc();
    if (Reason != NOUR_Constant)
      reject(L, "static constant value",
             "Only a checked non-ODR constant value can be materialized without a definition.");
    if (Member)
      discard(Member->getBase());
    return A.literal(Found->second, type(E->getType(), L), L);
  }
  Expression emptyBaseConversion(const CastExpr *C) {
    auto L = C->getExprLoc();
    const auto Path = A.emptyBaseCast(C);
    const bool Pointer = C->getType()->isPointerType();
    if (C->getCastKind() == CK_BaseToDerived) {
      auto Source = Pointer ? expression(C->getSubExpr())
                            : address(lvalue(C->getSubExpr()), C->getSubExpr()->getType(), L);
      auto Target = Pointer ? C->getType() : A.Context.getPointerType(C->getType());
      // Each edge names a real first member. C permits the inverse conversion
      // to its containing structure; void retains the existing wire cast rules.
      auto Result = cast(cast(std::move(Source), "ptr:void", L), type(Target, L), L);
      return Pointer ? std::move(Result) : dereference(std::move(Result), L);
    }
    auto Members = [&](Expression Place) {
      for (const auto *Base : Path)
        Place = Expression{{"kind", "member"}, {"name", Base->Member},
                           {"type", type(A.Context.getRecordType(Base->Base), L)},
                           {"args", json::Array{std::move(Place)}}, {"loc", A.loc(L)}};
      return Place;
    };
    if (!Pointer)
      return Members(lvalue(C->getSubExpr()));
    // Taking a member address through null is not a valid C expression. Save
    // the source once before branching, including calls and postfix updates.
    auto Source = snapshot(expression(C->getSubExpr()), L);
    auto Result = temporary(type(C->getType(), L), L);
    auto Yes = labelName(), No = labelName(), End = labelName();
    branch(cast(Source, "bool", L), Yes, No, L);
    label(Yes, L);
    assign(Result, address(Members(dereference(Source, L)),
                           C->getType()->getPointeeType(), L), L);
    jump(End, L);
    label(No, L);
    assign(Result, A.zero(C->getType(), L), L);
    jump(End, L);
    label(End, L);
    return Result;
  }
  Expression lvalue(const Expr *E) {
    E = E->IgnoreParens();
    auto L = E->getExprLoc();
    if (const auto *C = dyn_cast<CastExpr>(E);
        C && (C->getCastKind() == CK_DerivedToBase ||
              C->getCastKind() == CK_UncheckedDerivedToBase ||
              C->getCastKind() == CK_BaseToDerived))
      return emptyBaseConversion(C);
    if (const auto *Literal = dyn_cast<StringLiteral>(E); Literal && A.S.coreV2())
      return A.stringObject(Literal);
    if (auto Value = staticMemberValue(E)) {
      // A glvalue conditional read may need an internal address. Source ODR-use
      // was rejected before lowering; this fresh scalar is only a value carrier.
      auto Place = temporary(*Value->getString("type"), L);
      assign(Place, std::move(*Value), L);
      return Place;
    }
    if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(E)) {
      auto Found = ArraySources.find(Opaque);
      if (Found == ArraySources.end())
        reject(L, "array copy source", "No bound semantic array source is active.");
      return Found->second;
    }
    if (const auto *R = dyn_cast<DeclRefExpr>(E))
      return storage(R->getDecl(), E->getExprLoc());
    if (const auto *M = dyn_cast<MemberExpr>(E)) {
      if (A.S.coreV2())
        if (const auto *V = dyn_cast<VarDecl>(M->getMemberDecl());
            V && V->isStaticDataMember()) {
          // The receiver contributes effects, not storage or a dereference.
          // Any temporary receiver retains its ordinary full-expression cleanup.
          discard(M->getBase());
          return storage(V, L);
        }
      // Never snapshot a whole base record just to access one of its fields.
      const auto *Field = llvm::cast<FieldDecl>(M->getMemberDecl());
      auto Place = fieldStorage(M->isArrow()
                                    ? dereference(expression(M->getBase()), L)
                                    : lvalue(M->getBase()), Field, L);
      // A member expression denotes the referent, independent of the
      // containing object's cv qualifiers. Only construction writes its binding.
      return Field->getType()->isReferenceType() ? dereference(std::move(Place), L)
                                                : std::move(Place);
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
    for (const auto *Field : Fields) {
      Value = fieldStorage(std::move(Value), Field, L);
      if (Field->getType()->isReferenceType())
        Value = dereference(std::move(Value), L);
    }
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
  Expression functionValue(const Expr *E) {
    auto L = E->getExprLoc();
    if (const auto *P = dyn_cast<ParenExpr>(E))
      return functionValue(P->getSubExpr());
    if (const auto *C = dyn_cast<ConstantExpr>(E))
      return functionValue(C->getSubExpr());
    if (const auto *W = dyn_cast<ExprWithCleanups>(E))
      return functionValue(W->getSubExpr());
    if (const auto *R = dyn_cast<DeclRefExpr>(E))
      return A.functionAddress(dyn_cast<FunctionDecl>(R->getDecl()), L);
    if (const auto *M = dyn_cast<MemberExpr>(E)) {
      // Static member access evaluates the source base, including its cleanup.
      discard(M->getBase());
      return A.functionAddress(dyn_cast<FunctionDecl>(M->getMemberDecl()), L);
    }
    if (const auto *U = dyn_cast<UnaryOperator>(E);
        U && U->getOpcode() == UO_Deref &&
        U->getSubExpr()->getType()->isFunctionPointerType())
      return snapshot(expression(U->getSubExpr()), L);
    if (const auto *B = dyn_cast<BinaryOperator>(E);
        B && B->getOpcode() == BO_Comma) {
      discard(B->getLHS());
      return functionValue(B->getRHS());
    }
    if (const auto *C = dyn_cast<ConditionalOperator>(E)) {
      auto T = type(A.Context.getPointerType(C->getType()), L);
      auto Result = temporary(T, L);
      auto Condition = expression(C->getCond());
      auto Yes = labelName(), No = labelName(), End = labelName();
      branch(std::move(Condition), Yes, No, L, C->getCond());
      label(Yes, L);
      assign(Result, functionValue(C->getTrueExpr()), L);
      jump(End, L);
      label(No, L);
      assign(Result, functionValue(C->getFalseExpr()), L);
      jump(End, L);
      label(End, L);
      return Result;
    }
    reject(L, "function designator", "Function values require a checked named target or stored callback.");
  }
  Expression indirectCall(const CallExpr *Call) {
    auto L = Call->getExprLoc();
    auto Pointer = Call->getCallee()->getType();
    if (A.functionPointerType(Pointer, L).empty())
      throw Failure{};
    const auto *Prototype = Pointer->getPointeeType()->getAs<FunctionProtoType>();
    if (Call->getNumArgs() != Prototype->getNumParams())
      reject(L, "indirect call", "Callable and parameter counts differ.");
    // Snapshot the whole postfix before any explicit argument can reseat its
    // source storage. This also preserves compound direct-target expressions.
    auto Callable = snapshot(expression(Call->getCallee()), L);
    json::Array Args;
    for (unsigned I = 0; I < Call->getNumArgs(); ++I)
      Args.push_back(argument(Call->getArg(I), Prototype->getParamType(I)));
    chargeCall(Args, L);
    json::Object Instruction{{"op", "indirect_call"},
                             {"callable", std::move(Callable)},
                             {"args", std::move(Args)}, {"loc", A.loc(L)}};
    auto ResultType = type(Prototype->getReturnType(), L, true);
    Expression Result;
    if (ResultType != "void") {
      Result = temporary(ResultType, L);
      Instruction["target"] = json::Object(Result);
    }
    Body.push_back(std::move(Instruction));
    if (Prototype->getReturnType()->isReferenceType())
      return dereference(std::move(Result), L);
    return Result;
  }

  Expression cstddefOperation(const CallExpr *Call, CstddefOperation Operation) {
    auto L = Call->getExprLoc();
    auto ResultType = type(Call->getType(), L);
    auto ByteComputation = [&](llvm::StringRef Operator, Expression Left,
                               Expression Right) {
      auto Value = binary(Operator, cast(std::move(Left), "uint", L),
                          cast(std::move(Right), "uint", L), "uint", L);
      return cast(std::move(Value), "u8", L);
    };
    auto ShiftComputation = [&](llvm::StringRef Operator, Expression Left,
                                Expression Right) {
      auto Value = binary(Operator, cast(std::move(Left), "uint", L),
                          std::move(Right), "uint", L);
      return cast(std::move(Value), "u8", L);
    };
    auto ShiftArgument = [&] {
      auto Type = Call->getArg(1)->getType();
      if (A.Context.isPromotableIntegerType(Type))
        Type = A.Context.getPromotedIntegerType(Type);
      return cast(expression(Call->getArg(1)), type(Type, L), L);
    };
    switch (Operation) {
    case CstddefOperation::ToInteger:
      return cast(expression(Call->getArg(0)), ResultType, L);
    case CstddefOperation::BitNot: {
      auto Argument = cast(expression(Call->getArg(0)), "uint", L);
      auto Value = Expression{{"kind", "unary"},
                              {"type", "uint"},
                              {"operator", "~"},
                              {"args", json::Array{std::move(Argument)}},
                              {"loc", A.loc(L)}};
      return snapshot(cast(std::move(Value), ResultType, L), L);
    }
    case CstddefOperation::BitOr:
      return snapshot(ByteComputation("|", expression(Call->getArg(0)),
                                      expression(Call->getArg(1))),
                      L);
    case CstddefOperation::BitAnd:
      return snapshot(ByteComputation("&", expression(Call->getArg(0)),
                                      expression(Call->getArg(1))),
                      L);
    case CstddefOperation::BitXor:
      return snapshot(ByteComputation("^", expression(Call->getArg(0)),
                                      expression(Call->getArg(1))),
                      L);
    case CstddefOperation::ShiftLeft:
      return snapshot(ShiftComputation("<<", expression(Call->getArg(0)),
                                       ShiftArgument()),
                      L);
    case CstddefOperation::ShiftRight:
      return snapshot(ShiftComputation(">>", expression(Call->getArg(0)),
                                       ShiftArgument()),
                      L);
    default:
      break;
    }
    // C++17 assignment-operator syntax evaluates the right operand before
    // the left operand. Preserve that order while implementing libc++'s
    // byte-reference result directly in the scalar carrier.
    const bool ShiftAssignment =
        Operation == CstddefOperation::ShiftLeftAssign ||
        Operation == CstddefOperation::ShiftRightAssign;
    auto Right = ShiftAssignment ? ShiftArgument()
                                 : expression(Call->getArg(1));
    auto Left = lvalue(Call->getArg(0));
    auto Read = snapshot(Left, L);
    Expression Value;
    switch (Operation) {
    case CstddefOperation::BitOrAssign:
      Value = ByteComputation("|", std::move(Read), std::move(Right));
      break;
    case CstddefOperation::BitAndAssign:
      Value = ByteComputation("&", std::move(Read), std::move(Right));
      break;
    case CstddefOperation::BitXorAssign:
      Value = ByteComputation("^", std::move(Read), std::move(Right));
      break;
    case CstddefOperation::ShiftLeftAssign:
      Value = ShiftComputation("<<", std::move(Read), std::move(Right));
      break;
    case CstddefOperation::ShiftRightAssign:
      Value = ShiftComputation(">>", std::move(Read), std::move(Right));
      break;
    default:
      reject(L, "cstddef operation", "Unknown approved cstddef operation.");
    }
    assign(Left, std::move(Value), L);
    return Left;
  }

  Expression utilityOperation(const CallExpr *Call, UtilityOperation Operation,
                              std::optional<Expression> Destination) {
    auto L = Call->getExprLoc();
    auto ArrayFor = [&](QualType Type) {
      return approvedUtilityArrayRecord(
          A.S, A.Sources,
          Type.isNull() ? nullptr : Type->getAsCXXRecordDecl(), A.Context);
    };
    auto MemberObject = [&]() -> const Expr * {
      if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call))
        return Operator->getNumArgs() ? Operator->getArg(0) : nullptr;
      if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
        return Member->getImplicitObjectArgument();
      return nullptr;
    };
    auto ArrayElement = [&](Expression Base, const UtilityArrayRecord &Array,
                            Expression Position, QualType ResultType,
                            bool ReadOnly = false) {
      auto PointeeType = ReadOnly ? ResultType.withConst() : ResultType;
      auto PointerType = type(A.Context.getPointerType(PointeeType), L);
      auto Elements = fieldStorage(std::move(Base), Array.Elements, L);
      return index(decay(std::move(Elements), PointerType, L),
                   std::move(Position), type(ResultType, L), L);
    };
    switch (Operation) {
    case UtilityOperation::Move:
    case UtilityOperation::Forward:
    case UtilityOperation::MoveIfNoexcept:
    case UtilityOperation::AsConst:
      // These adapters change only the C++ value category or cv view. The
      // portable IR keeps the same storage designator; the selected outer
      // constructor/binding still observes Clang's checked result category.
      return lvalue(Call->getArg(0));
    case UtilityOperation::Exchange: {
      // Function arguments are bound before exchange reads the old value.
      // Capture the destination address and converted new scalar first, then
      // perform the header's move/read, assignment and value return directly.
      auto ObjectType = Call->getArg(0)->getType();
      auto ObjectAddress = snapshot(
          address(lvalue(Call->getArg(0)), ObjectType, L), L);
      auto NewValue = snapshot(expression(Call->getArg(1)), L);
      auto Object = dereference(std::move(ObjectAddress), L);
      auto OldValue = snapshot(Object, L);
      assign(Object,
             cast(std::move(NewValue), type(ObjectType, L), L), L);
      return OldValue;
    }
    case UtilityOperation::Swap: {
      // Choose the left-to-right order permitted for C++17 call arguments,
      // retaining both bound objects before executing swap's scalar body.
      auto ObjectType = Call->getArg(0)->getType();
      auto LeftAddress = snapshot(
          address(lvalue(Call->getArg(0)), ObjectType, L), L);
      auto RightAddress = snapshot(
          address(lvalue(Call->getArg(1)), ObjectType, L), L);
      auto Left = dereference(std::move(LeftAddress), L);
      auto Right = dereference(std::move(RightAddress), L);
      auto OldLeft = snapshot(Left, L);
      auto OldRight = snapshot(Right, L);
      assign(Left, std::move(OldRight), L);
      assign(Right, std::move(OldLeft), L);
      return {};
    }
    case UtilityOperation::MakePair: {
      auto Pair = approvedUtilityPairRecord(
          A.S, A.Sources, Call->getType()->getAsCXXRecordDecl(), A.Context);
      if (!Pair)
        reject(L, "utility make_pair",
               "The selected std::pair layout is unavailable.");
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(Call->getType(), L);
      if (Place.getString("type") != type(Call->getType(), L))
        reject(L, "utility make_pair",
               "The std::make_pair destination type differs from its result.");
      initialize(fieldStorage(json::Object(Place), Pair->First, L),
                 Call->getArg(0), L);
      initialize(fieldStorage(json::Object(Place), Pair->Second, L),
                 Call->getArg(1), L);
      return Place;
    }
    case UtilityOperation::PairSwap: {
      auto LeftAddress = snapshot(
          address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
      auto RightAddress = snapshot(
          address(lvalue(Call->getArg(1)), Call->getArg(1)->getType(), L), L);
      auto OldLeft = snapshot(dereference(json::Object(LeftAddress), L), L);
      auto OldRight = snapshot(dereference(json::Object(RightAddress), L), L);
      assign(dereference(std::move(LeftAddress), L), std::move(OldRight), L);
      assign(dereference(std::move(RightAddress), L), std::move(OldLeft), L);
      return {};
    }
    case UtilityOperation::PairMemberSwap: {
      const auto *MemberCall = llvm::cast<CXXMemberCallExpr>(Call);
      const auto *Object = MemberCall->getImplicitObjectArgument();
      auto LeftAddress =
          snapshot(address(lvalue(Object), Object->getType(), L), L);
      auto RightAddress = snapshot(
          address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
      auto OldLeft = snapshot(dereference(json::Object(LeftAddress), L), L);
      auto OldRight = snapshot(dereference(json::Object(RightAddress), L), L);
      assign(dereference(std::move(LeftAddress), L), std::move(OldRight), L);
      assign(dereference(std::move(RightAddress), L), std::move(OldLeft), L);
      return {};
    }
    case UtilityOperation::PairGetFirst:
    case UtilityOperation::PairGetSecond: {
      auto Pair = approvedUtilityPairRecord(
          A.S, A.Sources,
          Call->getArg(0)->getType()->getAsCXXRecordDecl(), A.Context);
      if (!Pair)
        reject(L, "utility pair get",
               "The selected std::pair layout is unavailable.");
      auto Base = lvalue(Call->getArg(0));
      return fieldStorage(
          std::move(Base),
          Operation == UtilityOperation::PairGetFirst ? Pair->First
                                                      : Pair->Second,
          L);
    }
    case UtilityOperation::PairEqual:
    case UtilityOperation::PairNotEqual:
    case UtilityOperation::PairLess:
    case UtilityOperation::PairGreater:
    case UtilityOperation::PairLessEqual:
    case UtilityOperation::PairGreaterEqual: {
      auto Pair = approvedUtilityPairRecord(
          A.S, A.Sources,
          Call->getArg(0)->getType()->getAsCXXRecordDecl(), A.Context);
      if (!Pair)
        reject(L, "utility pair comparison",
               "The selected std::pair layout is unavailable.");
      auto LeftAddress = snapshot(
          address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
      auto RightAddress = snapshot(
          address(lvalue(Call->getArg(1)), Call->getArg(1)->getType(), L), L);
      auto Left = dereference(std::move(LeftAddress), L);
      auto Right = dereference(std::move(RightAddress), L);
      auto Member = [&](const Expression &Base, const FieldDecl *Field) {
        return fieldStorage(json::Object(Base), Field, L);
      };
      auto Equal = [&](const Expression &A, const Expression &B) {
        auto Result = temporary("bool", L);
        auto Second = labelName(), False = labelName(), End = labelName();
        branch(binary("==", Member(A, Pair->First),
                      Member(B, Pair->First), "bool", L),
               Second, False, L);
        label(False, L);
        assign(Result, boolean(false, L), L);
        jump(End, L);
        label(Second, L);
        assign(Result,
               binary("==", Member(A, Pair->Second),
                      Member(B, Pair->Second), "bool", L),
               L);
        jump(End, L);
        label(End, L);
        return Result;
      };
      auto Less = [&](const Expression &A, const Expression &B) {
        auto Result = temporary("bool", L);
        auto True = labelName(), CheckReverse = labelName(), False = labelName();
        auto CheckSecond = labelName(), End = labelName();
        branch(binary("<", Member(A, Pair->First),
                      Member(B, Pair->First), "bool", L),
               True, CheckReverse, L);
        label(CheckReverse, L);
        branch(binary("<", Member(B, Pair->First),
                      Member(A, Pair->First), "bool", L),
               False, CheckSecond, L);
        label(CheckSecond, L);
        assign(Result,
               binary("<", Member(A, Pair->Second),
                      Member(B, Pair->Second), "bool", L),
               L);
        jump(End, L);
        label(True, L);
        assign(Result, boolean(true, L), L);
        jump(End, L);
        label(False, L);
        assign(Result, boolean(false, L), L);
        jump(End, L);
        label(End, L);
        return Result;
      };
      Expression Result;
      bool Negate = false;
      switch (Operation) {
      case UtilityOperation::PairEqual:
        Result = Equal(Left, Right);
        break;
      case UtilityOperation::PairNotEqual:
        Result = Equal(Left, Right);
        Negate = true;
        break;
      case UtilityOperation::PairLess:
        Result = Less(Left, Right);
        break;
      case UtilityOperation::PairGreater:
        Result = Less(Right, Left);
        break;
      case UtilityOperation::PairLessEqual:
        Result = Less(Right, Left);
        Negate = true;
        break;
      case UtilityOperation::PairGreaterEqual:
        Result = Less(Left, Right);
        Negate = true;
        break;
      default:
        reject(L, "utility pair comparison",
               "Unknown approved std::pair comparison.");
      }
      if (!Negate)
        return Result;
      return snapshot(Expression{{"kind", "unary"},
                                 {"type", "bool"},
                                 {"operator", "!"},
                                 {"args", json::Array{std::move(Result)}},
                                 {"loc", A.loc(L)}},
                      L);
    }
    case UtilityOperation::ArraySize:
    case UtilityOperation::ArrayMaxSize:
    case UtilityOperation::ArrayEmpty: {
      const auto *Object = MemberObject();
      auto Array = Object ? ArrayFor(Object->getType())
                          : std::optional<UtilityArrayRecord>();
      if (!Object || !Array)
        reject(L, "utility array capacity",
               "The selected std::array layout is unavailable.");
      // The receiver still evaluates even though these results depend only on
      // the template extent.
      lvalue(Object);
      if (Operation == UtilityOperation::ArrayEmpty)
        return boolean(false, L);
      return quantity(Array->Size, type(Call->getType(), L), L);
    }
    case UtilityOperation::ArrayData:
    case UtilityOperation::ArrayBegin:
    case UtilityOperation::ArrayEnd: {
      const auto *Object = MemberObject();
      auto Array = Object ? ArrayFor(Object->getType())
                          : std::optional<UtilityArrayRecord>();
      if (!Object || !Array)
        reject(L, "utility array iterator",
               "The selected std::array layout is unavailable.");
      auto Elements = fieldStorage(lvalue(Object), Array->Elements, L);
      auto Pointer = decay(std::move(Elements), type(Call->getType(), L), L);
      if (Operation != UtilityOperation::ArrayEnd)
        return Pointer;
      auto SizeType = type(A.Context.getSizeType(), L);
      return binary("+", std::move(Pointer),
                    quantity(Array->Size, SizeType, L),
                    type(Call->getType(), L), L);
    }
    case UtilityOperation::ArraySubscript:
    case UtilityOperation::ArrayAt:
    case UtilityOperation::ArrayFront:
    case UtilityOperation::ArrayBack: {
      const auto *Object = MemberObject();
      auto Array = Object ? ArrayFor(Object->getType())
                          : std::optional<UtilityArrayRecord>();
      if (!Object || !Array)
        reject(L, "utility array element",
               "The selected std::array layout is unavailable.");
      auto Base = lvalue(Object);
      Expression Position;
      if (Operation == UtilityOperation::ArraySubscript)
        Position = expression(Call->getArg(1));
      else if (Operation == UtilityOperation::ArrayAt)
        Position = expression(Call->getArg(0));
      else
        Position = quantity(Operation == UtilityOperation::ArrayFront
                                ? 0
                                : Array->Size - 1,
                            type(A.Context.getSizeType(), L), L);
      return ArrayElement(std::move(Base), *Array, std::move(Position),
                          Call->getType());
    }
    case UtilityOperation::ArrayGet: {
      auto Array = ArrayFor(Call->getArg(0)->getType());
      const auto *Function = Call->getDirectCallee();
      const auto *Arguments =
          Function ? Function->getTemplateSpecializationArgs() : nullptr;
      if (!Array || !Arguments || Arguments->size() != 3 ||
          Arguments->get(0).getKind() != TemplateArgument::Integral)
        reject(L, "utility array get",
               "The selected std::array element is unavailable.");
      auto Index = Arguments->get(0).getAsIntegral().getLimitedValue(Array->Size);
      return ArrayElement(
          lvalue(Call->getArg(0)), *Array,
          quantity(Index, type(A.Context.getSizeType(), L), L),
          Call->getType());
    }
    case UtilityOperation::ArrayFill: {
      const auto *Object = MemberObject();
      auto Array = Object ? ArrayFor(Object->getType())
                          : std::optional<UtilityArrayRecord>();
      if (!Object || !Array)
        reject(L, "utility array fill",
               "The selected std::array layout is unavailable.");
      auto ObjectAddress = snapshot(
          address(lvalue(Object), Object->getType(), L), L);
      auto Value = snapshot(expression(Call->getArg(0)), L);
      auto SizeType = type(A.Context.getSizeType(), L);
      for (uint64_t I = 0; I < Array->Size; ++I) {
        A.chargeExpansion(1, L);
        auto Base = dereference(json::Object(ObjectAddress), L);
        assign(ArrayElement(std::move(Base), *Array,
                            quantity(I, SizeType, L), Array->ElementType),
               json::Object(Value), L);
      }
      return {};
    }
    case UtilityOperation::ArraySwap:
    case UtilityOperation::ArrayMemberSwap: {
      const Expr *LeftSource = Operation == UtilityOperation::ArrayMemberSwap
                                   ? MemberObject()
                                   : Call->getArg(0);
      const Expr *RightSource = Operation == UtilityOperation::ArrayMemberSwap
                                    ? Call->getArg(0)
                                    : Call->getArg(1);
      if (!LeftSource || !RightSource || !ArrayFor(LeftSource->getType()))
        reject(L, "utility array swap",
               "The selected std::array layout is unavailable.");
      auto LeftAddress = snapshot(
          address(lvalue(LeftSource), LeftSource->getType(), L), L);
      auto RightAddress = snapshot(
          address(lvalue(RightSource), RightSource->getType(), L), L);
      auto OldLeft = snapshot(dereference(json::Object(LeftAddress), L), L);
      auto OldRight = snapshot(dereference(json::Object(RightAddress), L), L);
      assign(dereference(std::move(LeftAddress), L), std::move(OldRight), L);
      assign(dereference(std::move(RightAddress), L), std::move(OldLeft), L);
      return {};
    }
    case UtilityOperation::ArrayEqual:
    case UtilityOperation::ArrayNotEqual:
    case UtilityOperation::ArrayLess:
    case UtilityOperation::ArrayGreater:
    case UtilityOperation::ArrayLessEqual:
    case UtilityOperation::ArrayGreaterEqual: {
      auto Array = ArrayFor(Call->getArg(0)->getType());
      if (!Array)
        reject(L, "utility array comparison",
               "The selected std::array layout is unavailable.");
      auto LeftAddress = snapshot(
          address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
      auto RightAddress = snapshot(
          address(lvalue(Call->getArg(1)), Call->getArg(1)->getType(), L), L);
      auto Left = dereference(std::move(LeftAddress), L);
      auto Right = dereference(std::move(RightAddress), L);
      auto SizeType = type(A.Context.getSizeType(), L);
      auto Element = [&](const Expression &Base, uint64_t I) {
        return ArrayElement(json::Object(Base), *Array,
                            quantity(I, SizeType, L), Array->ElementType,
                            true);
      };
      auto Equal = [&](const Expression &First, const Expression &Second) {
        auto Result = temporary("bool", L);
        auto True = labelName(), False = labelName(), End = labelName();
        std::vector<std::string> Next;
        Next.reserve(Array->Size - 1);
        for (uint64_t I = 1; I < Array->Size; ++I)
          Next.push_back(labelName());
        for (uint64_t I = 0; I < Array->Size; ++I) {
          auto Success = I + 1 == Array->Size ? True : Next[I];
          branch(binary("==", Element(First, I), Element(Second, I),
                        "bool", L),
                 Success, False, L);
          if (I + 1 != Array->Size)
            label(Next[I], L);
        }
        label(True, L);
        assign(Result, boolean(true, L), L);
        jump(End, L);
        label(False, L);
        assign(Result, boolean(false, L), L);
        jump(End, L);
        label(End, L);
        return Result;
      };
      auto Less = [&](const Expression &First, const Expression &Second) {
        auto Result = temporary("bool", L);
        auto True = labelName(), False = labelName(), End = labelName();
        for (uint64_t I = 0; I < Array->Size; ++I) {
          auto Reverse = labelName();
          auto Next = labelName();
          branch(binary("<", Element(First, I), Element(Second, I),
                        "bool", L),
                 True, Reverse, L);
          label(Reverse, L);
          branch(binary("<", Element(Second, I), Element(First, I),
                        "bool", L),
                 False, Next, L);
          label(Next, L);
        }
        jump(False, L);
        label(True, L);
        assign(Result, boolean(true, L), L);
        jump(End, L);
        label(False, L);
        assign(Result, boolean(false, L), L);
        jump(End, L);
        label(End, L);
        return Result;
      };
      Expression Result;
      bool Negate = false;
      switch (Operation) {
      case UtilityOperation::ArrayEqual:
        Result = Equal(Left, Right);
        break;
      case UtilityOperation::ArrayNotEqual:
        Result = Equal(Left, Right);
        Negate = true;
        break;
      case UtilityOperation::ArrayLess:
        Result = Less(Left, Right);
        break;
      case UtilityOperation::ArrayGreater:
        Result = Less(Right, Left);
        break;
      case UtilityOperation::ArrayLessEqual:
        Result = Less(Right, Left);
        Negate = true;
        break;
      case UtilityOperation::ArrayGreaterEqual:
        Result = Less(Left, Right);
        Negate = true;
        break;
      default:
        reject(L, "utility array comparison",
               "Unknown approved std::array comparison.");
      }
      if (!Negate)
        return Result;
      return snapshot(Expression{{"kind", "unary"},
                                 {"type", "bool"},
                                 {"operator", "!"},
                                 {"args", json::Array{std::move(Result)}},
                                 {"loc", A.loc(L)}},
                      L);
    }
    }
    reject(L, "utility operation", "Unknown approved utility operation.");
  }

  Expression call(const CallExpr *Call,
                  std::optional<Expression> Destination = std::nullopt) {
    auto L = Call->getExprLoc();
    auto T = type(Call->getType(), L, true);
    auto *Callee = Call->getDirectCallee();
    const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
    if (A.S.coreV2()) {
      if (const auto *D = scalarDestruction(Call, A.Context)) {
        // Evaluate the base without reading an indeterminate destroyed scalar.
        // Arrow bases evaluate their pointer; dot bases designate storage only.
        if (D->isArrow())
          expression(D->getBase());
        else if (D->getBase()->isGLValue())
          lvalue(D->getBase());
        else
          discard(D->getBase());
        return {};
      }
      if (const auto *D = dyn_cast_or_null<CXXDestructorDecl>(Method)) {
        const auto *Member = dyn_cast_or_null<MemberExpr>(directMethodReference(Call));
        if (!Member || Call->getNumArgs())
          reject(L, "explicit destructor call", "A checked direct destructor receiver is required.");
        const auto *Base = Member->getBase();
        auto Receiver = Member->isArrow()
                            ? expression(Base)
                            : address(lvalue(Base), Base->getType(), L);
        if (auto Object = A.Context.getRecordType(D->getParent());
            needsDestruction(Object)) {
          // Destructors receive an unqualified this even for a const object.
          Receiver = snapshot(cast(std::move(Receiver), "ptr:" + type(Object, L), L), L);
          destroy(dereference(std::move(Receiver), L), Object, L);
        }
        // Explicit destruction never cancels an eventual automatic cleanup.
        return {};
      }
      APValue NumericLimit;
      if (approvedNumericLimitsConstant(A.S, A.Sources, Call, A.Context,
                                        NumericLimit))
        return A.constant(NumericLimit, Call->getType(), L);
      if (auto Operation =
              approvedCstddefOperation(A.S, A.Sources, Call, A.Context))
        return cstddefOperation(Call, *Operation);
      APValue UtilityValue;
      if (approvedUtilityConstant(A.S, A.Sources, Call, A.Context,
                                  UtilityValue))
        return A.constant(UtilityValue, Call->getType(), L);
      if (auto Operation =
              approvedUtilityOperation(A.S, A.Sources, Call, A.Context))
        return utilityOperation(Call, *Operation, std::move(Destination));
      if (auto Pair = approvedUtilityPairAssignment(
              A.S, A.Sources, dyn_cast<CXXOperatorCallExpr>(Call), A.Context)) {
        if (Destination)
          reject(L, "utility pair assignment",
                 "std::pair assignment cannot initialize a record result.");
        auto RightAddress = snapshot(
            address(lvalue(Call->getArg(1)), Call->getArg(1)->getType(), L), L);
        auto LeftAddress = snapshot(
            address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
        auto Left = dereference(std::move(LeftAddress), L);
        auto Right = dereference(std::move(RightAddress), L);
        assign(Left, std::move(Right), L);
        return Left;
      }
    }
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
    if (auto Operation = A.nativeHeapImport(Callee, L); !Operation.empty()) {
      if (Destination || !directFunctionReference(Call) || Call->getNumArgs() != Callee->getNumParams())
        reject(L, "native heap call", "A checked direct native heap call is required.");
      json::Array Args;
      for (const auto *Arg : Call->arguments())
        Args.push_back(snapshot(expression(Arg), Arg->getExprLoc()));
      chargeCall(Args, L);
      Expression Result;
      json::Object Instruction{{"op", "native_heap_call"}, {"operation", Operation},
                                {"args", std::move(Args)}, {"loc", A.loc(L)}};
      if (Operation != "free") {
        Result = temporary(T, L);
        Instruction["target"] = json::Object(Result);
      }
      Body.push_back(std::move(Instruction));
      A.S.Module["native_heap"] = true;
      A.S.Module["memory_lifetimes"] = true;
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
    if (A.S.coreV2() && !directFunctionReference(Call) &&
        Call->getCallee()->getType()->isFunctionPointerType()) {
      if (Destination)
        reject(L, "indirect record result", "By-value record callbacks require separate ownership lowering.");
      return indirectCall(Call);
    }
    const bool TrivialAssignment = A.S.coreV2() && defaultedAssignment(Method) &&
                                   Method->isTrivial();
    if (!Callee || (Method && (!A.S.coreV2() || !callableMethod(Method))))
      reject(L, "call",
             "Call target is not a supported defined function.");
    if (!TrivialAssignment && !Callee->hasBody() &&
        (!A.S.project() || Callee->getFormalLinkage() == Linkage::Internal)) {
      A.reject(L, "call", "Call target has no definition in this translation unit.",
               "TR0203");
      throw Failure{};
    }
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
  Expression allocationExtent(QualType Object, QualType Parameter, bool Alignment,
                              SourceLocation L, bool Deallocation = false) {
    auto T = type(Parameter, L);
    auto Quantity = Alignment ? A.Context.getTypeAlignInChars(Object)
                              : A.Context.getTypeSizeInChars(Object);
    if (Alignment && Deallocation)
      Quantity = A.Context.toCharUnitsFromBits(
          A.Context.getTypeAlignIfKnown(Object, /*NeedsPreferredAlignment=*/true));
    return A.literal(llvm::APSInt(llvm::APInt(integerBits(T), Quantity.getQuantity()),
                                  unsignedInteger(T)), T, L);
  }
  Expression quantity(uint64_t Value, llvm::StringRef T, SourceLocation L) {
    return A.literal(llvm::APSInt(llvm::APInt(integerBits(T), Value),
                                  unsignedInteger(T)), T, L);
  }
  Expression allocationBytes(Expression Pointer, SourceLocation L) {
    return cast(cast(std::move(Pointer), "ptr:void", L), "ptr:u8", L);
  }
  Expression cookieSlot(Expression Bytes, uint64_t Offset, SourceLocation L) {
    auto Size = type(A.Context.getSizeType(), L);
    auto Pointer = binary("+", std::move(Bytes), quantity(Offset, Size, L), "ptr:u8", L);
    return dereference(cast(cast(std::move(Pointer), "ptr:void", L), "ptr:" + Size, L), L);
  }
  void initializeNewArray(Expression Pointer, QualType Object, uint64_t Count,
                          const Expr *Init, SourceLocation L) {
    if (!Init)
      return;
    while (const auto *Wrapper = dyn_cast<ExprWithCleanups>(Init))
      Init = Wrapper->getSubExpr();
    auto Element = Object.getUnqualifiedType();
    auto T = type(Element, L);
    Pointer = cast(std::move(Pointer), "ptr:" + T, L);
    const auto *List = dyn_cast<InitListExpr>(Init);
    if (List && List->isSyntacticForm() && List->getSemanticForm())
      List = List->getSemanticForm();
    const auto *String = dyn_cast<StringLiteral>(Init);
    if (List && List->isStringLiteralInit())
      String = dyn_cast<StringLiteral>(List->getInit(0)->IgnoreParens());
    std::optional<Expression> StringValue;
    if (String)
      StringValue = A.stringInitializer(String);
    for (uint64_t N = 0; N < Count; ++N) {
      A.chargeExpansion(1, L);
      auto Place = index(Pointer, quantity(N, "uint", L), T, L);
      if (StringValue) {
        auto *Units = StringValue->getArray("args");
        if (N < Units->size())
          assign(std::move(Place), *(*Units)[N].getAsObject(), L);
        else
          initializeZero(std::move(Place), Element, L);
      } else if (const auto *Construction = dyn_cast<CXXConstructExpr>(Init)) {
        beginFullExpression();
        construct(std::move(Place), Element, Construction, L);
        endFullExpression();
      } else if (isa<ImplicitValueInitExpr>(Init)) {
        initializeZero(std::move(Place), Element, L);
      } else if (List) {
        const bool Shared = N >= List->getNumInits();
        const auto *Value = Shared ? List->getArrayFiller() : List->getInit(N);
        const bool Omitted = Shared || A.SeparateArrayFillers.count(Value);
        const bool Cleanup = Omitted && omittedDefaultConstruction(Value, Element, A.Context);
        if (Cleanup) beginFullExpression();
        if (Value) initialize(std::move(Place), Value, L);
        else initializeZero(std::move(Place), Element, L);
        if (Cleanup) endFullExpression();
      } else {
        reject(L, "new array initializer", "A checked array construction, value, list or string initializer is required.");
      }
    }
  }
  void initializeRuntimeNewArray(Expression Pointer, QualType Object,
                                  Expression Count, const ArrayNewInfo &Info,
                                  SourceLocation L) {
    if (Info.Repeated == RuntimeArrayInitialization::None)
      return;
    // Each written clause is emitted once and retains the enclosing FE. The
    // shared descriptor proves repeated aggregates need no separate temporaries.
    if (Info.PrefixCount)
      initializeNewArray(Pointer, Object, Info.PrefixCount, Info.Initializer, L);
    auto Element = Object.getUnqualifiedType();
    const auto T = type(Element, L), Size = type(A.Context.getSizeType(), L);
    Pointer = cast(std::move(Pointer), "ptr:" + T, L);
    auto Position = temporary(Size, L);
    assign(Position, quantity(Info.PrefixCount, Size, L), L);
    const auto Check = labelName(), BodyLabel = labelName(), End = labelName();
    jump(Check, L);
    label(Check, L);
    branch(binary("<", Position, Count, "bool", L), BodyLabel, End, L);
    label(BodyLabel, L);
    auto Place = index(Pointer, Position, T, L);
    if (Info.Repeated == RuntimeArrayInitialization::Zero) {
      initializeZero(std::move(Place), Element, L);
    } else if (Info.Repeated == RuntimeArrayInitialization::Aggregate) {
      // Aggregate initialization retains the enclosing new-expression's FE.
      // Its checked destination-only lists initialize each actual array slot.
      initialize(std::move(Place), Info.Filler, L);
    } else {
      // This boundary follows the semantic omitted default-constructor role,
      // never an ExprWithCleanups wrapper. All its argument temporaries end
      // before the next iteration can reuse the generated automatic storage.
      beginFullExpression();
      if (const auto *Construction = dyn_cast<CXXConstructExpr>(Info.Initializer))
        construct(std::move(Place), Element, Construction, L);
      else
        initialize(std::move(Place), Info.Filler, L);
      endFullExpression();
    }
    assign(Position, binary("+", Position, quantity(1, Size, L), Size, L), L);
    jump(Check, L);
    label(End, L);
  }
  Expression allocateArray(const CXXNewExpr *N) {
    const auto L = N->getExprLoc();
    const auto *F = A.allocationFunction(N->getOperatorNew(), true, L, true);
    const auto Object = N->getAllocatedType();
    const auto Info = A.arrayNewInfo(N);
    const auto Layout = A.arrayAllocationLayout(Object, N->doesUsualArrayDeleteWantSize(), L);
    const uint64_t ObjectBytes = A.Context.getTypeSizeInChars(Object).getQuantity();
    const auto Size = type(A.Context.getSizeType(), L);
    auto Result = temporary(type(N->getType(), L), L);
    assign(Result, A.zero(N->getType(), L), L);
    std::string End;
    Expression Count;
    if (Info.Count) {
      discard(*N->getArraySize());
      Count = quantity(*Info.Count, Size, L);
    } else {
      End = labelName();
      // Preserve bound temporary ownership in the enclosing FE, including
      // invalid lengths and allocator-null paths. Do not re-evaluate the bound.
      auto Before = snapshot(expression(Info.BoundBeforeConversion), L);
      Count = snapshot(cast(Before, Size, L), L);
      if (Info.BoundBeforeConversion->getType()->isSignedIntegerOrEnumerationType()) {
        auto Nonnegative = labelName();
        auto BeforeType = type(Info.BoundBeforeConversion->getType(), L);
        branch(binary("<", Before, quantity(0, BeforeType, L), "bool", L), End, Nonnegative, L);
        label(Nonnegative, L);
      }
      const auto Maximum = llvm::APInt::getMaxValue(integerBits(Size)).getZExtValue();
      const auto MaximumCount = (Maximum - Layout.CookieBytes) / ObjectBytes;
      auto Fits = labelName();
      branch(binary("<=", Count, quantity(MaximumCount, Size, L), "bool", L), Fits, End, L);
      label(Fits, L);
      if (Info.PrefixCount) {
        auto Enough = labelName();
        branch(binary(">=", Count, quantity(Info.PrefixCount, Size, L), "bool", L), Enough, End, L);
        label(Enough, L);
      }
    }
    json::Array Args;
    Args.push_back(Info.Count
        ? quantity(*Info.Count * ObjectBytes + Layout.CookieBytes, Size, L)
        : binary("+", binary("*", Count, quantity(ObjectBytes, Size, L), Size, L),
                 quantity(Layout.CookieBytes, Size, L), Size, L));
    unsigned Prefix = 1;
    if (N->passAlignment()) {
      Args.push_back(allocationExtent(Object, F->getParamDecl(1)->getType(), true, L));
      ++Prefix;
    }
    for (unsigned I = 0; I < N->getNumPlacementArgs(); ++I)
      Args.push_back(argument(N->getPlacementArg(I), F->getParamDecl(Prefix + I)->getType()));
    auto Storage = temporary("ptr:void", L);
    chargeCall(Args, L);
    Body.push_back(json::Object{{"op", "call"}, {"callee", A.name(F)},
                                {"args", std::move(Args)}, {"target", json::Object(Storage)},
                                {"loc", A.loc(L)}});
    if (N->shouldNullCheckAllocation()) {
      auto Initialize = labelName();
      if (End.empty()) End = labelName();
      branch(cast(Storage, "bool", L), Initialize, End, L);
      label(Initialize, L);
    }
    auto Bytes = allocationBytes(Storage, L);
    if (Layout.CookieBytes) {
      if (Layout.StoresElementSize)
        assign(cookieSlot(Bytes, 0, L), quantity(Layout.ElementBytes, Size, L), L);
      assign(cookieSlot(Bytes, Layout.CountOffset, L), Info.Count
          ? quantity(*Info.Count * (ObjectBytes / Layout.ElementBytes), Size, L)
          : binary("*", Count, quantity(ObjectBytes / Layout.ElementBytes, Size, L), Size, L), L);
      Bytes = binary("+", std::move(Bytes), quantity(Layout.CookieBytes, Size, L), "ptr:u8", L);
    }
    assign(Result, cast(cast(std::move(Bytes), "ptr:void", L), type(N->getType(), L), L), L);
    if (Info.Count)
      initializeNewArray(Result, Object, *Info.Count, N->getInitializer(), L);
    else
      initializeRuntimeNewArray(Result, Object, Count, Info, L);
    if (!End.empty()) {
      jump(End, L);
      label(End, L);
    }
    return Result;
  }
  Expression allocate(const CXXNewExpr *N) {
    if (N->isArray())
      return allocateArray(N);
    auto L = N->getExprLoc();
    const auto *F = A.allocationFunction(N->getOperatorNew(), true, L);
    auto Object = N->getAllocatedType();
    json::Array Args;
    Args.push_back(allocationExtent(Object, F->getParamDecl(0)->getType(), false, L));
    unsigned Prefix = 1;
    if (N->passAlignment()) {
      Args.push_back(allocationExtent(Object, F->getParamDecl(1)->getType(), true, L));
      ++Prefix;
    }
    for (unsigned I = 0; I < N->getNumPlacementArgs(); ++I)
      Args.push_back(argument(N->getPlacementArg(I), F->getParamDecl(Prefix + I)->getType()));
    auto Storage = temporary(type(F->getReturnType(), L), L);
    chargeCall(Args, L);
    Body.push_back(json::Object{{"op", "call"}, {"callee", A.name(F)},
                                {"args", std::move(Args)}, {"target", json::Object(Storage)},
                                {"loc", A.loc(L)}});
    auto Result = snapshot(cast(std::move(Storage), type(N->getType(), L), L), L);
    std::string End;
    if (N->shouldNullCheckAllocation() && N->hasInitializer()) {
      auto Initialize = labelName();
      End = labelName();
      branch(cast(Result, "bool", L), Initialize, End, L);
      label(Initialize, L);
    }
    if (N->hasInitializer()) {
      // Initialization gets an internal mutable view; the returned pointer
      // preserves source cv. New storage never acquires lexical ownership.
      auto Destination = cast(Result, "ptr:" + type(Object.getUnqualifiedType(), L), L);
      initialize(dereference(std::move(Destination), L), N->getInitializer(), L);
    }
    if (!End.empty()) {
      jump(End, L);
      label(End, L);
    }
    return Result;
  }
  void deallocateArray(const CXXDeleteExpr *Delete) {
    const auto L = Delete->getExprLoc();
    const auto *F = A.allocationFunction(Delete->getOperatorDelete(), false, L, true);
    const auto Layout = A.arrayAllocationLayout(Delete->getDestroyedType(),
                                                Delete->doesUsualArrayDeleteWantSize(), L);
    auto Pointer = snapshot(expression(Delete->getArgument()), L);
    auto Destroy = labelName(), End = labelName();
    branch(cast(Pointer, "bool", L), Destroy, End, L);
    label(Destroy, L);
    const auto Size = type(A.Context.getSizeType(), L);
    auto Bytes = allocationBytes(Pointer, L);
    if (Layout.CookieBytes)
      Bytes = binary("-", std::move(Bytes), quantity(Layout.CookieBytes, Size, L), "ptr:u8", L);
    auto Raw = snapshot(std::move(Bytes), L);
    auto Count = Layout.CookieBytes ? snapshot(cookieSlot(Raw, Layout.CountOffset, L), L)
                                    : quantity(0, Size, L);
    auto Total = snapshot(binary("+", binary("*", Count, quantity(Layout.ElementBytes, Size, L), Size, L),
                                 quantity(Layout.CookieBytes, Size, L), Size, L), L);
    if (needsDestruction(Layout.Element)) {
      auto Remaining = snapshot(Count, L);
      auto Check = labelName(), Element = labelName(), Complete = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Remaining, quantity(0, Size, L), "bool", L), Element, Complete, L);
      label(Element, L);
      assign(Remaining, binary("-", Remaining, quantity(1, Size, L), Size, L), L);
      // Locate flattened base elements through the allocation's byte storage.
      // Pointer-to-row arithmetic must not acquire a base-element stride.
      auto Offset = binary("+", binary("*", Remaining, quantity(Layout.ElementBytes, Size, L), Size, L),
                           quantity(Layout.CookieBytes, Size, L), Size, L);
      auto Address = binary("+", Raw, std::move(Offset), "ptr:u8", L);
      auto Receiver = cast(cast(std::move(Address), "ptr:void", L), "ptr:" + type(Layout.Element, L), L);
      destroy(dereference(std::move(Receiver), L), Layout.Element, L);
      jump(Check, L);
      label(Complete, L);
    }
    json::Array Args;
    Args.push_back(cast(Raw, type(F->getParamDecl(0)->getType(), L), L));
    for (unsigned I = 1; I < F->getNumParams(); ++I) {
      const auto Parameter = F->getParamDecl(I)->getType();
      if (Parameter->isAlignValT())
        Args.push_back(allocationExtent(Layout.Element, Parameter, true, L, true));
      else {
        if (!Layout.CookieBytes)
          reject(L, "sized array delete", "The native array ABI supplies no count for this sized deallocation.");
        Args.push_back(cast(Total, type(Parameter, L), L));
      }
    }
    chargeCall(Args, L);
    Body.push_back(json::Object{{"op", "call"}, {"callee", A.name(F)},
                                {"args", std::move(Args)}, {"loc", A.loc(L)}});
    jump(End, L);
    label(End, L);
  }
  void deallocate(const CXXDeleteExpr *Delete) {
    if (Delete->isArrayForm()) {
      deallocateArray(Delete);
      return;
    }
    auto L = Delete->getExprLoc();
    const auto *F = A.allocationFunction(Delete->getOperatorDelete(), false, L);
    auto Object = Delete->getDestroyedType().getUnqualifiedType();
    // A destructor may reseat the variable which supplied this pointer.
    auto Pointer = snapshot(expression(Delete->getArgument()), L);
    auto Destroy = labelName(), End = labelName();
    branch(cast(Pointer, "bool", L), Destroy, End, L);
    label(Destroy, L);
    auto Receiver = cast(Pointer, "ptr:" + type(Object, L), L);
    destroy(dereference(std::move(Receiver), L), Object, L);
    json::Array Args;
    Args.push_back(cast(std::move(Pointer), type(F->getParamDecl(0)->getType(), L), L));
    for (unsigned I = 1; I < F->getNumParams(); ++I) {
      auto Parameter = F->getParamDecl(I)->getType();
      Args.push_back(allocationExtent(Object, Parameter, Parameter->isAlignValT(), L, true));
    }
    chargeCall(Args, L);
    Body.push_back(json::Object{{"op", "call"}, {"callee", A.name(F)},
                                {"args", std::move(Args)}, {"loc", A.loc(L)}});
    jump(End, L);
    label(End, L);
  }
  Expression expression(const Expr *E) {
    if (A.S.coreV2() && E->getType()->isFunctionType())
      return functionValue(E);
    auto L = E->getExprLoc();
    auto T = type(E->getType(), L, true);
    if (A.S.coreV2()) {
      if (const auto *N = dyn_cast<CXXNewExpr>(E))
        return allocate(N);
      if (const auto *D = dyn_cast<CXXDeleteExpr>(E)) {
        deallocate(D);
        return {};
      }
    }
    if (auto Value = staticMemberValue(E))
      return std::move(*Value);
    if (const auto *Substitution = dyn_cast<SubstNonTypeTemplateParmExpr>(E)) {
      const auto *Replacement = scalarTemplateReplacement(Substitution, A.Context);
      if (!A.S.coreV2() || !Replacement)
        reject(L, "template value replacement", "A checked scalar template replacement is required.");
      return expression(Replacement);
    }
    if (const auto *Query = dyn_cast<SizeOfPackExpr>(E)) {
      auto Size = concretePackSize(Query);
      if (!A.S.coreV2() || !Size)
        reject(L, "pack size", "A checked concrete pack count is required.");
      return A.literal(llvm::APSInt(llvm::APInt(integerBits(T), *Size),
                                   unsignedInteger(T)), T, L);
    }
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
    if (A.S.coreV2() && isa<CXXNullPtrLiteralExpr>(E))
      return A.zero(E->getType(), L);
    if (A.S.coreV2() && approvedCstddefNull(A.S, A.Sources, E))
      return A.zero(E->getType(), L);
    if (A.S.coreV2())
      if (auto Offset =
              approvedCstddefOffset(A.S, A.Sources, E, A.Context))
        return A.literal(*Offset, T, L);
    if (const auto *C = dyn_cast<CharacterLiteral>(E); C && A.S.coreV2())
      // Clang exposes negative ordinary character literals such as '\xff'
      // through the unsigned CharacterLiteral API (for example UINT_MAX on
      // a signed-char target).  Preserve the target character's low bits
      // instead of asking APInt to prove that the widened spelling fits.
      return A.literal(llvm::APSInt(llvm::APInt(integerBits(T), C->getValue(),
                                               false, true),
                                   unsignedInteger(T)), T, L);
    if (const auto *Query = dyn_cast<CXXNoexceptExpr>(E); Query && A.S.coreV2()) {
      if (!Query->getOperand() || Query->isTypeDependent() ||
          Query->isValueDependent() || Query->isInstantiationDependent())
        reject(L, "noexcept query", "A resolved constant noexcept query is required.");
      // Clang accounts for selected function specifications and destruction.
      // The inspected operand creates no runtime calls, values or cleanup.
      return boolean(Query->getValue(), L);
    }
    if (const auto *Query = dyn_cast<TypeTraitExpr>(E); Query && A.S.coreV2())
      return boolean(A.typeClassificationValue(Query), L);
    if (const auto *Query = dyn_cast<ArrayTypeTraitExpr>(E); Query && A.S.coreV2())
      return A.literal(llvm::APSInt(llvm::APInt(integerBits(T),
                                               A.arrayTypeQueryValue(Query)),
                                    unsignedInteger(T)), T, L);
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
    if (const auto *Literal = dyn_cast<StringLiteral>(E); Literal && A.S.coreV2())
      return A.stringObject(Literal);
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
      if (A.S.coreV2())
        if (const auto *Variable = dyn_cast<VarDecl>(R->getDecl()))
          if (auto Value = approvedSDKIntegerConstant(
                  A.S, A.Sources, Variable, A.Context)) {
            if (R->isNonOdrUse() == NOUR_None)
              reject(L, "standard trait storage",
                     "An approved standard trait may be lowered only as a "
                     "constant value.");
            *Value = Value->extOrTrunc(integerBits(T));
            Value->setIsUnsigned(unsignedInteger(T));
            return A.literal(*Value, T, L);
          }
      return storage(R->getDecl(), L);
    }
    if (const auto *C = dyn_cast<CastExpr>(E)) {
      switch (C->getCastKind()) {
      case CK_DerivedToBase:
      case CK_UncheckedDerivedToBase:
      case CK_BaseToDerived:
        return emptyBaseConversion(C);
      case CK_ToVoid:
        if (!A.S.coreV2() || !C->isPRValue() || !C->getType()->isVoidType() ||
            C->isTypeDependent() || C->isValueDependent() ||
            C->isInstantiationDependent() || !C->getSubExpr())
          reject(L, "void expression",
                 "A resolved core-v2 void cast is required.");
        if (!emptyVoidInitializer(C)) {
          if (C->getSubExpr()->getType().isNull())
            reject(L, "void expression",
                   "An untyped operand is not a checked empty void initializer.");
          discard(C->getSubExpr());
        }
        return {};
      case CK_FunctionToPointerDecay:
        if (!A.S.coreV2())
          reject(L, "function value", "Function pointer values require core v2.");
        return cast(functionValue(C->getSubExpr()), T, L);
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
        if (A.S.coreV2()) {
          // A nullptr_t value can be produced by a call, conversion, comma or
          // conditional expression. Its conversion must retain those effects.
          discard(C->getSubExpr());
          return Expression{{"kind", "null"}, {"type", T}, {"loc", A.loc(L)}};
        }
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
      case CK_FloatingCast:
        if (A.S.math() || A.S.coreV2())
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
    if (A.S.coreV2() && isa<CXXScalarValueInitExpr>(E) &&
        E->getType()->isVoidType())
      return {};
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
    if (const auto *B = dyn_cast<BinaryOperator>(E);
        A.S.coreV2() && B && B->isComparisonOp()) {
      std::vector<const ArrayTypeTraitExpr *> Queries;
      for (const auto *Operand : {B->getLHS(), B->getRHS()})
        if (const auto *Query = dyn_cast<ArrayTypeTraitExpr>(
                Operand->IgnoreParenImpCasts()))
          Queries.push_back(Query);
      if (!Queries.empty()) {
        APValue Value;
        if (!B->isCXX11ConstantExpr(A.Context, &Value) || !Value.isInt())
          reject(L, "array query comparison",
                 "A comparison containing an array type query must be a "
                 "checked constant expression.");
        // Validate every query through the same source/type boundary as a
        // standalone query, then retain Clang's target-specific comparison
        // result as one literal. No operand is evaluated at runtime.
        for (const auto *Query : Queries)
          A.arrayTypeQueryValue(Query);
        return boolean(!Value.getInt().isZero(), L);
      }
    }
    if (const auto *U = dyn_cast<UnaryOperator>(E)) {
      if (A.S.coreV2() && U->getOpcode() == UO_Plus &&
          U->getSubExpr()->getType()->isFunctionPointerType())
        return cast(expression(U->getSubExpr()), T, L);
      if (A.S.coreV2() && U->getOpcode() == UO_AddrOf) {
        if (U->getSubExpr()->getType()->isFunctionType())
          return cast(functionValue(U->getSubExpr()), T, L);
        return address(lvalue(U->getSubExpr()), U->getSubExpr()->getType(), L);
      }
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
        auto Computation = integerBits(T) && integerBits(T) < 32
                               ? std::string("int") : T;
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
    if (A.S.coreV2()) {
      if (const auto *C = dyn_cast<ConstantExpr>(E)) {
        discard(C->getSubExpr());
        return;
      }
      if (const auto *C = dyn_cast<ImplicitCastExpr>(E);
          C && C->getCastKind() == CK_NoOp && C->isGLValue()) {
        discard(C->getSubExpr());
        return;
      }
      // A discarded literal has no effects and needs no value carrier.
      if (isa<CXXNullPtrLiteralExpr, StringLiteral>(E))
        return;
    }
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
    A.requireDestruction(Record, L);
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
    if (const auto *Base = A.emptyBase(DestroyedRecord)) {
      auto T = A.Context.getRecordType(Base->Base);
      Expression Member{{"kind", "member"}, {"type", type(T, L)},
                        {"name", Base->Member},
                        {"args", json::Array{dereference(*ThisPointer, L)}},
                        {"loc", A.loc(L)}};
      // The base belongs to this complete object's cleanup, never to a second
      // lexical lifetime. The derived body and fields have already finished.
      destroy(std::move(Member), T, L);
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
    if (A.S.coreV2())
      if (const auto *Default = dyn_cast<CXXDefaultArgExpr>(Arg)) {
        const auto *Init = selectedDefaultArgument(Default, A.Context);
        if (!Init)
          reject(L, "default argument",
                 "A checked selected default expression is required.");
        A.chargeExpansion(1, L);
        return argument(Init, ParameterType);
      }
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
    if (M->getStorageDuration() == SD_Static) {
      if (!ActiveDynamicInitializer ||
          A.staticTemporaryOwner(M) != ActiveDynamicInitializer)
        reject(L, "static temporary lifetime", "The exact dynamic static extending initializer is required.");
      auto Place = A.dynamicStaticTemporaryObject(M, ActiveDynamicInitializer);
      // Each lowered occurrence constructs its own permanent destination,
      // including repeated array fillers, without automatic destruction.
      initialize(Place, M->getSubExpr(), L);
      if (needsDestruction(M->getType()))
        registerStaticDestructor(*Place.getString("name"), L);
      return Place;
    }
    const auto *Owner = A.temporaryOwner(M);
    if (!Owner || !ActiveAutomaticInitializer ||
        Owner != ActiveAutomaticInitializer->Variable ||
        ActiveAutomaticInitializer->ScopeIndex >= Scopes.size())
      reject(L, "temporary lifetime", "The exact automatic extending initializer and scope are required.");
    return materialize(M->getSubExpr(), L, ActiveAutomaticInitializer->ScopeIndex);
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
                 SourceLocation L, bool BaseObject = false, bool Delegating = false) {
    if (const auto *Array = A.Context.getAsConstantArrayType(T)) {
      auto Element = Array->getElementType();
      auto Count = Array->getSize().getLimitedValue(65537);
      if (!Count || Count > 65536 || A.storageUnits(T) > 200000)
        reject(L, "array construction", "Array construction exceeds the storage limit.");
      for (unsigned N = 0; N < Count; ++N) {
        A.chargeExpansion(1, L);
        // A shared array construction has no explicit element clauses.
        // Default-argument temporaries end before constructing the next element.
        beginFullExpression();
        construct(initialElement(Place, Element, N, L), Element, C, L);
        endFullExpression();
      }
      return;
    }
    const auto *Constructor = C->getConstructor();
    if (!T->isRecordType() ||
        T->getAsCXXRecordDecl()->getCanonicalDecl() !=
            Constructor->getParent()->getCanonicalDecl() ||
        Place.getString("type") != type(T, L))
      reject(L, "construction", "Constructor and destination types differ.");
    if (auto Kind = approvedUtilityPairConstruction(
            A.S, A.Sources, C, A.Context)) {
      auto Pair = approvedUtilityPairRecord(
          A.S, A.Sources, T->getAsCXXRecordDecl(), A.Context);
      if (!Pair)
        reject(L, "utility pair construction",
               "The selected std::pair layout is unavailable.");
      auto Member = [&](const FieldDecl *Field) {
        return fieldStorage(json::Object(Place), Field, L);
      };
      switch (*Kind) {
      case UtilityPairConstruction::Default:
        initializeZero(Member(Pair->First), Pair->First->getType(), L);
        initializeZero(Member(Pair->Second), Pair->Second->getType(), L);
        return;
      case UtilityPairConstruction::Elements:
        initialize(Member(Pair->First), C->getArg(0), L);
        initialize(Member(Pair->Second), C->getArg(1), L);
        return;
      case UtilityPairConstruction::CopyOrMove: {
        auto Source = expression(C->getArg(0));
        assign(std::move(Place), std::move(Source), L);
        return;
      }
      }
      reject(L, "utility pair construction",
             "Unknown approved std::pair construction.");
    }
    const auto ZeroCompleteObject = [&] {
      if (!C->requiresZeroInitialization() || BaseObject)
        return;
      if (Delegating && BaseConstruction) {
        auto Zero = labelName(), Ready = labelName();
        branch(*BaseConstruction, Ready, Zero, L);
        label(Zero, L);
        initializeZero(Place, T, L);
        jump(Ready, L);
        label(Ready, L);
      } else {
        initializeZero(Place, T, L);
      }
    };
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
        ZeroCompleteObject();
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
      ZeroCompleteObject();
      return;
    }
    if (!supportedConstructor(Constructor) || !Constructor->hasBody() ||
        C->getNumArgs() != Constructor->getNumParams())
      reject(L, "construction", "Unsupported selected constructor or argument list.");
    ZeroCompleteObject();
    json::Array Args;
    Args.push_back(snapshot(address(std::move(Place), T.getUnqualifiedType(), L), L));
    for (unsigned I = 0; I < C->getNumArgs(); ++I)
      Args.push_back(argument(C->getArg(I),
                              Constructor->getParamDecl(I)->getType()));
    auto Callee = BaseObject ? A.requireBaseConstructor(Constructor, L) : A.name(Constructor);
    if (Delegating && BaseConstruction) {
      // Evaluate arguments only once, then forward the current role to the
      // target's single body. In particular, do not clone static/default sites.
      A.requireBaseConstructor(Constructor, L);
      Callee = A.name(Constructor) + "_construction";
      Args.push_back(json::Object(*BaseConstruction));
    }
    chargeCall(Args, L);
    Body.push_back(json::Object{{"op", "call"},
                                {"callee", std::move(Callee)},
                                {"args", std::move(Args)},
                                {"loc", A.loc(L)}});
  }
  void constructorInitializers(const CXXConstructorDecl *C) {
    auto SavedReceiver = DefaultReceiver;
    DefaultReceiver = InitializationReceiver{C->getParent()->getCanonicalDecl(), *ThisPointer};
    auto RestoreReceiver = llvm::make_scope_exit([&] { DefaultReceiver = std::move(SavedReceiver); });
    if (C->isDelegatingConstructor()) {
      const auto *Init = *C->init_begin();
      const auto *Target = C->getTargetConstructor();
      if (!Init->isWritten() || Init->isPackExpansion() || !Init->getInit() ||
          !Target || Target->getParent()->getCanonicalDecl() !=
                         C->getParent()->getCanonicalDecl())
        reject(C->getLocation(), "delegating initializer",
               "Delegation requires one written target in the same class.");
      // The target initializes this complete object. Its members must not be
      // initialized a second time before the delegating constructor's body.
      beginFullExpression();
      const Expr *Expression = Init->getInit()->IgnoreParens();
      if (const auto *Cleanup = dyn_cast<ExprWithCleanups>(Expression))
        Expression = Cleanup->getSubExpr()->IgnoreParens();
      const auto *Construction = dyn_cast<CXXConstructExpr>(Expression);
      if (!Construction || Construction->getConstructionKind() != CXXConstructionKind::Delegating ||
          Construction->getConstructor()->getCanonicalDecl() != Target->getCanonicalDecl())
        reject(C->getLocation(), "delegating construction", "The actual same-class constructor call must be retained.");
      construct(dereference(*ThisPointer, C->getLocation()),
                A.Context.getRecordType(C->getParent()), Construction,
                C->getLocation(), false, true);
      endFullExpression();
      return;
    }
    std::map<const Decl *, const Expr *> Initializers;
    auto L = C->getLocation();
    const auto *Base = A.emptyBase(C->getParent());
    const Expr *BaseInitializer = nullptr;
    for (const auto *I : C->inits()) {
      if (const auto *SelectedBase = A.emptyBaseInitializer(C, I)) {
        if (SelectedBase != Base || BaseInitializer)
          reject(L, "base initializer", "The exact direct base must be initialized once.");
        BaseInitializer = I->getInit();
        continue;
      }
      if (!I->isMemberInitializer() || I->isPackExpansion() ||
          I->getMember()->getParent() != C->getParent() || !I->getInit() ||
          !Initializers.emplace(I->getMember()->getCanonicalDecl(), I->getInit()).second)
        reject(L, "constructor initializer", "Unsupported or duplicate field initializer.");
    }
    if (Base) {
      if (!BaseInitializer)
        reject(L, "base initializer", "Missing semantic direct-base initialization.");
      Expression Member{{"kind", "member"}, {"name", Base->Member},
                        {"type", type(A.Context.getRecordType(Base->Base), L)},
                        {"args", json::Array{dereference(*ThisPointer, L)}}, {"loc", A.loc(L)}};
      beginFullExpression();
      initializeEmptyBase(std::move(Member), BaseInitializer, L);
      endFullExpression();
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
      if (Field->getType()->isReferenceType())
        assign(std::move(Member), bind(Found->second, Field->getType()), L);
      else
        initialize(std::move(Member), Found->second, L);
      endFullExpression();
    }
  }
  void initializeEmptyBase(Expression Place, const Expr *Init, SourceLocation L) {
    Init = Init->IgnoreParens();
    const auto *Record = Init->getType()->getAsCXXRecordDecl();
    if (!Record || !A.emptyBaseChainShape(Record) ||
        Place.getString("type") != type(Init->getType(), L))
      reject(L, "base initialization", "A base initializer requires its exact empty destination.");
    if (const auto *W = dyn_cast<ExprWithCleanups>(Init)) {
      initializeEmptyBase(std::move(Place), W->getSubExpr(), L);
      return;
    }
    if (const auto *Cast = dyn_cast<CastExpr>(Init);
        Cast && Cast->getCastKind() == CK_ConstructorConversion) {
      const auto *Construction = constructorConversion(Cast, A.Context);
      if (!Construction ||
          Construction->getConstructionKind() !=
              CXXConstructionKind::Complete)
        reject(L, "base constructor conversion",
               "The selected constructor conversion must retain its exact "
               "complete construction.");
      // Aggregate initialization of a derived object wraps a selected base
      // constructor in CK_ConstructorConversion. The wrapper, rather than the
      // inner complete-object node, proves that this construction owns the
      // base destination; do not materialize a separate temporary.
      construct(std::move(Place), Cast->getType(), Construction, L, true);
      return;
    }
    if (const auto *C = dyn_cast<CXXConstructExpr>(Init)) {
      if (C->getConstructionKind() != CXXConstructionKind::NonVirtualBase)
        reject(L, "base construction", "The selected construction must identify its nonvirtual base operation.");
      construct(std::move(Place), Init->getType(), C, L, true);
      return;
    }
    // Empty base subobjects have no semantic bytes to zero. Their real C
    // carrier provides an address, but its placeholder must not gain a store.
    if (isa<ImplicitValueInitExpr>(Init))
      return;
    const auto *List = dyn_cast<InitListExpr>(Init);
    if (!List)
      reject(L, "base initialization", "An empty base requires its checked semantic aggregate or construction initializer.");
    if (List->isSyntacticForm() && List->getSemanticForm())
      List = List->getSemanticForm();
    const auto *Base = A.emptyBase(Record);
    if (List->isGLValue() || List->getNumInits() != unsigned(Base != nullptr))
      reject(L, "base aggregate initialization", "The semantic list must retain the exact direct base initializer.");
    if (Base) {
      Expression Member{{"kind", "member"}, {"name", Base->Member},
                        {"type", type(A.Context.getRecordType(Base->Base), L)},
                        {"args", json::Array{std::move(Place)}}, {"loc", A.loc(L)}};
      initializeEmptyBase(std::move(Member), List->getInit(0), L);
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
      if (!defaultedCopyOrMoveConstructor(dyn_cast_or_null<CXXConstructorDecl>(Function)) ||
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
        // Copying an entire array gives each element's constructor defaults
        // their own temporary cleanup boundary, while the array owns elements.
        beginFullExpression();
        initialize(initialElement(Place, Array->getElementType(), unsigned(N), L),
                   Loop->getSubExpr(), L);
        endFullExpression();
      }
      return;
    }
    if (A.S.coreV2())
      if (const auto *Array = A.Context.getAsConstantArrayType(Init->getType())) {
        if (const auto *Literal = dyn_cast<StringLiteral>(Init->IgnoreParens())) {
          auto Value = A.stringInitializer(Literal);
          if (Place.getString("type") != Value.getString("type"))
            reject(L, "string initialization", "The checked literal must match its destination array.");
          unsigned N = 0;
          for (auto &Unit : *Value.getArray("args"))
            assign(initialElement(Place, Array->getElementType(), N++, L),
                   std::move(*Unit.getAsObject()), L);
          return;
        }
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
        if (I->isStringLiteralInit()) {
          initialize(std::move(Place), I->getInit(0), L);
          return;
        }
        auto Element = Array->getElementType();
        auto Count = Array->getSize().getLimitedValue(65537);
        if (I->getNumInits() > Count)
          reject(L, "array initialization", "Too many semantic initializers.");
        for (unsigned N = 0; N < Count; ++N) {
          auto Target = initialElement(Place, Element, N, L);
          const bool SharedFiller = N >= I->getNumInits();
          const Expr *Value = SharedFiller ? I->getArrayFiller() : I->getInit(N);
          const bool Omitted = SharedFiller || A.SeparateArrayFillers.count(Value);
          // C++17's exception concerns the default constructor of an omitted
          // array element. A constructor of a field inside an aggregate element
          // retains the whole initializer's full expression, as do explicit clauses.
          const bool ElementCleanup = Omitted && omittedDefaultConstruction(Value, Element, A.Context);
          if (ElementCleanup)
            beginFullExpression();
          if (Value)
            initialize(std::move(Target), Value, L);
          else
            initializeZero(std::move(Target), Element, L);
          if (ElementCleanup)
            endFullExpression();
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
      const auto *Base = A.emptyBase(Record);
      if (I->getNumInits() != std::distance(Fields.begin(), Fields.end()) + unsigned(Base != nullptr))
        reject(L, "aggregate initialization",
               "Incomplete semantic field initializer list.");
      unsigned Index = 0;
      if (Base) {
        Expression Member{{"kind", "member"}, {"name", Base->Member},
                          {"type", type(A.Context.getRecordType(Base->Base), L)},
                          {"args", json::Array{json::Object(Place)}}, {"loc", A.loc(L)}};
        initializeEmptyBase(std::move(Member), I->getInit(Index++), L);
      }
      // Initialization is observable through earlier destination subobjects.
      // Store each field before evaluating the next clause, including nested
      // lists. Ordinary record copy/assignment still uses the value path.
      for (const auto *Field : Fields) {
        Expression Member{{"kind", "member"},
                          {"type", type(Field->getType(), L)},
                          {"name", A.name(Field)},
                          {"args", json::Array{json::Object(Place)}},
                          {"loc", A.loc(L)}};
        const auto *Value = I->getInit(Index++);
        if (Field->getType()->isReferenceType())
          assign(std::move(Member), bind(Value, Field->getType()), L);
        else
          initialize(std::move(Member), Value, L);
      }
      return;
    }
    assign(std::move(Place), expression(Init), L);
  }
  Expression localStorage(const VarDecl *V) {
    // Switch entry pre-registration must never create an automatic shadow.
    if (A.S.coreV2() && V->isStaticLocal())
      return storage(V, V->getLocation());
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
  void registerStaticDestructor(llvm::StringRef Global, SourceLocation L) {
    A.chargeExpansion(1, L);
    Body.push_back(json::Object{{"op", "register_static_destructor"},
                                {"global", Global.str()}, {"loc", A.loc(L)}});
  }
  void initializeStatic(const VarDecl *V, const Expr *Init) {
    auto L = V->getLocation();
    const bool Dynamic = A.DynamicStaticObjects.count(V->getCanonicalDecl());
    if ((Dynamic && !Init) || (!Dynamic && !needsDestruction(V->getType())))
      reject(L, "static initializer", "Expected checked initialization or destructor registration.");
    A.chargeExpansion(2, L);
    const auto Initialize = labelName(), Ready = labelName();
    const auto Global = A.name(V);
    Body.push_back(json::Object{{"op", "static_init_begin"},
                                {"global", Global},
                                {"true", Initialize}, {"false", Ready},
                                {"loc", A.loc(L)}});
    Edges[Current].push_back(Initialize);
    Edges[Current].push_back(Ready);
    Open = false;
    label(Initialize, L);
    beginFullExpression();
    auto Previous = ActiveDynamicInitializer;
    auto Restore = llvm::make_scope_exit([&] { ActiveDynamicInitializer = Previous; });
    if (Dynamic) {
      ActiveDynamicInitializer = V->getCanonicalDecl();
      if (V->getType()->isReferenceType())
        assign(variable(Global, type(V->getType(), L), L), bind(Init, V->getType()), L);
      else
        initialize(storage(V, L), Init, L);
    }
    // Registration follows complete-object construction, before argument
    // cleanup can initialize and register some other static object.
    if (needsDestruction(V->getType()))
      registerStaticDestructor(Global, L);
    endFullExpression();
    Body.push_back(json::Object{{"op", "static_init_end"},
                                {"global", Global}, {"loc", A.loc(L)}});
    jump(Ready, L);
    label(Ready, L);
  }
  void declaration(const VarDecl *V) {
    auto L = V->getLocation();
    if (A.S.coreV2() && V->isStaticLocal()) {
      if (A.DynamicStaticObjects.count(V->getCanonicalDecl()) || needsDestruction(V->getType()))
        initializeStatic(V, V->getInit());
      return;
    }
    auto Place = localStorage(V);
    beginFullExpression();
    {
      auto Previous = ActiveAutomaticInitializer;
      auto Restore = llvm::make_scope_exit([&] { ActiveAutomaticInitializer = Previous; });
      ActiveAutomaticInitializer.reset();
      const auto Range = rangeForComponents(A.rangeForOwner(V));
      const bool RangeReference = Range &&
          Range->Range->getCanonicalDecl() == V->getCanonicalDecl();
      if (A.S.coreV2() && V->getKind() == Decl::Var &&
          (!V->isImplicit() || RangeReference) &&
          V->isLocalVarDecl() && V->hasLocalStorage() &&
          (V->getType()->isReferenceType() || aggregateValue(V->getType()))) {
        if (Scopes.empty())
          reject(L, "temporary lifetime", "An automatic extending declaration requires a lexical scope.");
        ActiveAutomaticInitializer = AutomaticInitializer{V->getCanonicalDecl(), Scopes.size() - 1};
      }
      if (V->getType()->isReferenceType())
        assign(Place, bind(V->getInit(), V->getType()), L);
      else {
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
    }
    endFullExpression();
  }
  void registerSwitchStorage(const Stmt *S) {
    if (!S)
      return;
    if (const auto *I = dyn_cast<IfStmt>(S); I && A.S.coreV2() && I->isConstexpr())
      return; // An outer case cannot enter it; selected lowering owns its locals.
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
                   (isa<TypedefNameDecl, EnumDecl, StaticAssertDecl, NamespaceAliasDecl,
                        UsingDirectiveDecl, UsingDecl>(Decl) ||
                    Decl->getKind() == clang::Decl::UsingShadow)))
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
      if (I->isConstexpr() && A.S.coreV2()) {
        auto Selected = I->getNondiscardedCase(A.Context);
        if (!Selected)
          reject(L, "if constexpr", "Expected a checked constant branch selection.");
        scopedStatement(*Selected);
        if (Open)
          cleanup(Scopes.back());
        Scopes.pop_back();
        return;
      }
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
    } else if (const auto *F = dyn_cast<CXXForRangeStmt>(S)) {
      const auto Parts = rangeForComponents(F);
      if (!A.S.coreV2() || !Parts || A.rangeForOwner(Parts->Range) != F ||
          A.rangeForOwner(Parts->Begin) != F || A.rangeForOwner(Parts->End) != F)
        reject(L, "range for", "A checked range and its exact hidden declarations are required.");
      Scopes.emplace_back(); // Range and iterators outlive all iterations.
      declaration(Parts->Range);
      declaration(Parts->Begin);
      declaration(Parts->End);
      const auto LoopDepth = Scopes.size();
      auto Test = labelName(), Loop = labelName(), Step = labelName(),
           End = labelName();
      jump(Test, L);
      label(Test, L);
      branch(condition(F->getCond()), Loop, End, L, F->getCond());
      label(Loop, L);
      Scopes.emplace_back(); // The loop variable owns only this iteration.
      declaration(Parts->Variable);
      ControlTargets.push_back({End, Step, LoopDepth, LoopDepth});
      scopedStatement(F->getBody());
      ControlTargets.pop_back();
      if (Open) {
        cleanup(Scopes.back());
        jump(Step, L);
      }
      Scopes.pop_back();
      label(Step, L);
      expressionStatement(F->getInc());
      jump(Test, L);
      label(End, L);
      cleanup(Scopes.back());
      Scopes.pop_back();
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
  FunctionLowering(Adapter &A, const FunctionDecl *F, bool SharedBody = false)
      : A(A), Function(F), SharedConstruction(SharedBody) {
    Prefix = "nct_f" + digest(A.name(F) + (SharedBody ? "_construction" : "")).substr(0, 12) + "_";
  }
  FunctionLowering(Adapter &A, const CXXRecordDecl *R)
      : A(A), Function(nullptr), DestroyedRecord(R->getDefinition()) {
    if (const auto *D = DestroyedRecord->getDestructor();
        D && !D->isImplicit() && !defaultedLifecycle(D)) {
      if (!ordinaryDestructor(D))
        reject(D->getLocation(), "destructor", "An admitted owned destructor definition is required.");
      if (!D->hasBody()) {
        A.reject(D->getLocation(), "destructor definition",
                 "A required destructor needs a definition in this source unit.",
                 "TR0203");
        throw Failure{};
      }
      Function = D->getDefinition();
    }
    Prefix = "nct_f" + digest(A.destructionName(R)).substr(0, 12) + "_";
  }
  json::Object run() {
    auto L = Function ? Function->getLocation() : DestroyedRecord->getLocation();
    auto Name = DestroyedRecord ? A.destructionName(DestroyedRecord)
        : SharedConstruction ? A.name(Function) + "_construction"
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
      if (SharedConstruction)
        BaseConstruction = parameter(A.Context.BoolTy, L);
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
    return finish(Name, ResultType, L,
                  SharedConstruction || DestroyedRecord || Function->getFormalLinkage() == Linkage::Internal,
                  !SharedConstruction && !DestroyedRecord && Function->isExternC() &&
                      Function->getFormalLinkage() != Linkage::Internal);
  }
  json::Object runConstructorEntry(bool BaseObject) {
    const auto *Constructor = llvm::cast<CXXConstructorDecl>(Function);
    const auto L = Constructor->getLocation();
    if (!A.BaseConstructorRecords.count(Constructor->getParent()->getCanonicalDecl()))
      reject(L, "constructor entry", "A shared body requires an already checked base record.");
    const auto Name = BaseObject ? A.baseConstructorName(Constructor) : A.name(Constructor);
    Prefix = "nct_f" + digest(Name).substr(0, 12) + "_";
    json::Array Args;
    Args.push_back(parameter(Constructor->getThisType(), L));
    for (const auto *P : Constructor->parameters())
      Args.push_back(parameter(parameterType(P->getType()), P->getLocation()));
    Args.push_back(boolean(BaseObject, L));
    Entry = labelName();
    label(Entry, L);
    // Forward the caller's existing carriers. Only the shared body owns and
    // cleans by-value parameters; a wrapper neither copies nor destroys them.
    chargeCall(Args, L);
    Body.push_back(json::Object{{"op", "call"},
                                {"callee", A.name(Constructor) + "_construction"},
                                {"args", std::move(Args)}, {"loc", A.loc(L)}});
    A.chargeExpansion(1, L);
    Body.push_back(json::Object{{"op", "return"}, {"loc", A.loc(L)}});
    Open = false;
    return finish(Name, "void", L,
                  BaseObject || Constructor->getFormalLinkage() == Linkage::Internal, false);
  }
  explicit FunctionLowering(Adapter &A) : A(A), Function(nullptr) {
    Prefix = "nct_f" + digest("startup:" + A.S.Relative).substr(0, 12) + "_";
  }
  json::Object runStartup(llvm::ArrayRef<const VarDecl *> Objects) {
    const auto L = Objects.front()->getLocation();
    const auto Name = "nct_startup_" + digest(A.S.Relative).substr(0, 16);
    Entry = labelName();
    label(Entry, L);
    Scopes.emplace_back();
    for (const auto *Object : Objects) {
      const VarDecl *InitializingDecl = nullptr;
      const auto *Init = Object->getAnyInitializer(InitializingDecl);
      if (Init && (!InitializingDecl ||
                   InitializingDecl->getCanonicalDecl() != Object->getCanonicalDecl()))
        reject(Object->getLocation(), "startup initializer", "Missing checked source initializer.");
      initializeStatic(Object, Init);
    }
    cleanupScopes(0);
    Scopes.pop_back();
    Body.push_back(json::Object{{"op", "return"}, {"loc", A.loc(L)}});
    Open = false;
    return finish(Name, "void", L, true, false);
  }
  json::Object runStaticDestruction(const StaticDestruction &Object) {
    const auto L = Object.Location;
    Prefix = "nct_f" + digest(Object.Function).substr(0, 12) + "_";
    Entry = labelName();
    label(Entry, L);
    Scopes.emplace_back();
    auto Place = variable(Object.Global, type(Object.Type, L), L);
    // Source-const storage is addressed through a const pointer, then an
    // explicit checked cv cast supplies the destructor's mutable receiver.
    auto Pointer = address(std::move(Place), A.Context.getConstType(Object.Type), L);
    Pointer = cast(std::move(Pointer), "ptr:" + type(Object.Type, L), L);
    destroy(dereference(std::move(Pointer), L), Object.Type, L);
    cleanupScopes(0);
    Scopes.pop_back();
    Body.push_back(json::Object{{"op", "return"}, {"loc", A.loc(L)}});
    Open = false;
    return finish(Object.Function, "void", L, true, false);
  }
private:
  json::Object finish(llvm::StringRef Name, llvm::StringRef ResultType,
                      SourceLocation L, bool Internal, bool CExport) {
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
        // The response is serialized after run() destroys its local strings.
        // Upstream JSON borrows StringRef, so retain owned copies here.
        {"name", Name.str()},
        {"result", ResultPlace ? std::string("void") : ResultType.str()},
        {"internal", Internal},
        {"c_export", CExport},
        {"params", std::move(Parameters)},
        {"locals", std::move(Locals)},
        {"body", std::move(Pruned)},
        {"loc", A.loc(L)}};
  }
};

json::Object Adapter::lower(FunctionDecl *Function) {
  return FunctionLowering(*this, Function).run();
}
json::Object Adapter::lowerBaseConstructor(const CXXConstructorDecl *Constructor) {
  return FunctionLowering(*this, Constructor).runConstructorEntry(true);
}
json::Object Adapter::lowerCompleteConstructor(const CXXConstructorDecl *Constructor) {
  return FunctionLowering(*this, Constructor).runConstructorEntry(false);
}
json::Object Adapter::lowerConstructorBody(const CXXConstructorDecl *Constructor) {
  return FunctionLowering(*this, Constructor, true).run();
}
json::Object Adapter::lowerStartup(llvm::ArrayRef<const VarDecl *> Objects) {
  return FunctionLowering(*this).runStartup(Objects);
}
json::Object Adapter::lowerStaticDestruction(const StaticDestruction &Object) {
  return FunctionLowering(*this).runStaticDestruction(Object);
}
json::Object Adapter::lowerDestruction(const CXXRecordDecl *Record) {
  return FunctionLowering(*this, Record).run();
}
} // namespace nct
