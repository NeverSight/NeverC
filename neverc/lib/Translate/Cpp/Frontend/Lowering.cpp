#include "Frontend.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtCXX.h"
#include "llvm/ADT/ScopeExit.h"
#include <algorithm>
#include <limits>
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
  Expression emitIndirectCall(Expression Callable, json::Array Args,
                              QualType Result, SourceLocation L) {
    chargeCall(Args, L);
    json::Object Instruction{{"op", "indirect_call"},
                             {"callable", std::move(Callable)},
                             {"args", std::move(Args)},
                             {"loc", A.loc(L)}};
    auto ResultType = type(Result, L, true);
    Expression Value;
    if (ResultType != "void") {
      Value = temporary(ResultType, L);
      Instruction["target"] = json::Object(Value);
    }
    Body.push_back(std::move(Instruction));
    if (Result->isReferenceType())
      return dereference(std::move(Value), L);
    return Value;
  }
  Expression emitAlgorithmCallback(Expression Callable, QualType CallbackType,
                                   json::Array Arguments, SourceLocation L) {
    const auto *Prototype =
        CallbackType->isFunctionPointerType()
            ? CallbackType->getPointeeType()->getAs<FunctionProtoType>()
            : nullptr;
    if (!Prototype || Prototype->isVariadic() ||
        Prototype->getNumParams() != Arguments.size())
      reject(L, "algorithm callback",
             "A checked fixed-arity function pointer is required.");
    json::Array Converted;
    for (unsigned I = 0; I < Arguments.size(); ++I) {
      const auto *Argument = Arguments[I].getAsObject();
      if (!Argument)
        reject(L, "algorithm callback",
               "A checked callback argument expression is required.");
      Converted.push_back(cast(json::Object(*Argument),
                               type(Prototype->getParamType(I), L), L));
    }
    return emitIndirectCall(std::move(Callable), std::move(Converted),
                            Prototype->getReturnType(), L);
  }
  Expression emitUnaryPredicate(Expression Callable, QualType PredicateType,
                                Expression Argument, SourceLocation L) {
    const auto *Prototype =
        PredicateType->getPointeeType()->getAs<FunctionProtoType>();
    if (!Prototype || Prototype->getNumParams() != 1 ||
        !Prototype->getReturnType()->isBooleanType())
      reject(L, "algorithm predicate",
             "A checked unary boolean callback is required.");
    json::Array Arguments;
    Arguments.push_back(std::move(Argument));
    return emitAlgorithmCallback(std::move(Callable), PredicateType,
                                 std::move(Arguments), L);
  }
  Expression emitBinaryPredicate(Expression Callable, QualType PredicateType,
                                 Expression Left, Expression Right,
                                 SourceLocation L) {
    const auto *Prototype =
        PredicateType->getPointeeType()->getAs<FunctionProtoType>();
    if (!Prototype || Prototype->getNumParams() != 2 ||
        !Prototype->getReturnType()->isBooleanType())
      reject(L, "algorithm predicate",
             "A checked binary boolean callback is required.");
    json::Array Arguments;
    Arguments.push_back(std::move(Left));
    Arguments.push_back(std::move(Right));
    return emitAlgorithmCallback(std::move(Callable), PredicateType,
                                 std::move(Arguments), L);
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
    return emitIndirectCall(std::move(Callable), std::move(Args),
                            Prototype->getReturnType(), L);
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

  Expression functionalOperationValues(
      SourceLocation L, Expression Left, std::optional<Expression> Right,
      const FunctionalOperationInfo &Info) {
    const auto Operation = Info.Operation;
    const bool Unary = Info.RightType.isNull();
    if (Unary != !Right)
      reject(L, "functional operation",
             "The checked operation arity must match its operands.");
    const auto LeftType = type(Info.LeftType, L);
    const auto RightType = Unary ? std::string() : type(Info.RightType, L);
    const auto OperationType = type(Info.OperationType, L);
    const auto ResultType = type(Info.ResultType, L);
    auto UnaryExpression = [&](llvm::StringRef Operator, Expression Value,
                               llvm::StringRef Result) {
      return Expression{{"kind", "unary"},
                        {"type", Result.str()},
                        {"operator", Operator.str()},
                        {"args", json::Array{std::move(Value)}},
                        {"loc", A.loc(L)}};
    };
    auto ArithmeticUnary = [&](llvm::StringRef Operator) {
      auto Value = UnaryExpression(
          Operator, cast(std::move(Left), LeftType, L), OperationType);
      return snapshot(cast(std::move(Value), ResultType, L), L);
    };
    auto ArithmeticBinary = [&](llvm::StringRef Operator) {
      auto Value = binary(Operator, cast(std::move(Left), LeftType, L),
                          cast(std::move(*Right), RightType, L), OperationType,
                          L);
      return snapshot(cast(std::move(Value), ResultType, L), L);
    };
    auto Comparison = [&](llvm::StringRef Operator) {
      return snapshot(
          binary(Operator, cast(std::move(Left), LeftType, L),
                 cast(std::move(*Right), RightType, L), "bool", L),
          L);
    };
    switch (Operation) {
    case FunctionalOperation::Plus:
      return ArithmeticBinary("+");
    case FunctionalOperation::Minus:
      return ArithmeticBinary("-");
    case FunctionalOperation::Multiplies:
      return ArithmeticBinary("*");
    case FunctionalOperation::Divides:
      return ArithmeticBinary("/");
    case FunctionalOperation::Modulus:
      return ArithmeticBinary("%");
    case FunctionalOperation::Negate:
      return ArithmeticUnary("-");
    case FunctionalOperation::BitAnd:
      return ArithmeticBinary("&");
    case FunctionalOperation::BitOr:
      return ArithmeticBinary("|");
    case FunctionalOperation::BitXor:
      return ArithmeticBinary("^");
    case FunctionalOperation::BitNot:
      return ArithmeticUnary("~");
    case FunctionalOperation::Equal:
      return Comparison("==");
    case FunctionalOperation::NotEqual:
      return Comparison("!=");
    case FunctionalOperation::Less:
      return Comparison("<");
    case FunctionalOperation::Greater:
      return Comparison(">");
    case FunctionalOperation::LessEqual:
      return Comparison("<=");
    case FunctionalOperation::GreaterEqual:
      return Comparison(">=");
    case FunctionalOperation::LogicalAnd:
    case FunctionalOperation::LogicalOr: {
      auto BooleanLeft = cast(std::move(Left), "bool", L);
      auto BooleanRight = cast(std::move(*Right), "bool", L);
      auto Value =
          binary(Operation == FunctionalOperation::LogicalAnd ? "&" : "|",
                 cast(std::move(BooleanLeft), "int", L),
                 cast(std::move(BooleanRight), "int", L), "int", L);
      return snapshot(cast(std::move(Value), "bool", L), L);
    }
    case FunctionalOperation::LogicalNot:
      return snapshot(
          UnaryExpression("!", cast(std::move(Left), "bool", L), "bool"), L);
    }
    llvm_unreachable("unknown functional operation");
  }

  Expression functionalOperation(const CallExpr *Call,
                                 const FunctionalOperationInfo &Info) {
    auto L = Call->getExprLoc();
    const auto *Method = llvm::cast<CXXMethodDecl>(Call->getDirectCallee());
    const bool Unary = Method->getNumParams() == 1;
    auto Argument = [&](unsigned Index) {
      auto Address = snapshot(
          bind(Call->getArg(Index + 1), Method->getParamDecl(Index)->getType()),
          L);
      return dereference(std::move(Address), L);
    };
    auto Left = Argument(0);
    std::optional<Expression> Right;
    if (!Unary)
      Right = Argument(1);
    return functionalOperationValues(L, std::move(Left), std::move(Right),
                                     Info);
  }

  void discardFunctionalObject(const Expr *Expression) {
    if (const auto *Cleanup = dyn_cast<ExprWithCleanups>(Expression)) {
      discardFunctionalObject(Cleanup->getSubExpr());
      return;
    }
    if (const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(Expression)) {
      discardFunctionalObject(Temporary->getSubExpr());
      return;
    }
    if (const auto *Bound = dyn_cast<CXXBindTemporaryExpr>(Expression)) {
      discardFunctionalObject(Bound->getSubExpr());
      return;
    }
    if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Expression)) {
      discardFunctionalObject(Cast->getSubExpr());
      return;
    }
    if (const auto *List = dyn_cast<InitListExpr>(Expression)) {
      for (const auto *Initializer : List->inits())
        discardFunctionalObject(Initializer);
      return;
    }
    if (const auto *Construction = dyn_cast<CXXConstructExpr>(Expression)) {
      for (const auto *Argument : Construction->arguments())
        discardFunctionalObject(Argument);
      return;
    }
    if (const auto *Parentheses = dyn_cast<ParenExpr>(Expression)) {
      discardFunctionalObject(Parentheses->getSubExpr());
      return;
    }
    if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Expression);
        Cast && Cast->getCastKind() == CK_NoOp) {
      discardFunctionalObject(Cast->getSubExpr());
      return;
    }
    if (const auto *Comma = dyn_cast<BinaryOperator>(Expression);
        Comma && Comma->getOpcode() == BO_Comma) {
      discard(Comma->getLHS());
      discardFunctionalObject(Comma->getRHS());
      return;
    }
    discard(Expression);
  }

  Expression functionalInvokeObjectOperation(
      const CallExpr *Call, const FunctionalOperationInfo &Info) {
    auto L = Call->getExprLoc();
    const bool Unary = Info.RightType.isNull();
    if (Call->getNumArgs() != (Unary ? 2u : 3u))
      reject(L, "functional invoke",
             "The checked function object arity must match its arguments.");
    discardFunctionalObject(Call->getArg(0));
    auto Left = snapshot(expression(Call->getArg(1)), L);
    std::optional<Expression> Right;
    if (!Unary)
      Right = snapshot(expression(Call->getArg(2)), L);
    return functionalOperationValues(L, std::move(Left), std::move(Right),
                                     Info);
  }

  void heapSiftDown(Expression First, Expression Size, Expression InitialRoot,
                    llvm::StringRef PointerType, llvm::StringRef DifferenceType,
                    SourceLocation L,
                    const std::optional<Expression> &Comparator = std::nullopt,
                    QualType ComparatorType = {}) {
    auto Root = snapshot(std::move(InitialRoot), L);
    auto Child = temporary(DifferenceType, L);
    auto Left = temporary(DifferenceType, L);
    auto Right = temporary(DifferenceType, L);
    auto At = [&](const Expression &Position) {
      return dereference(binary("+", json::Object(First),
                                json::Object(Position), PointerType, L),
                         L);
    };
    auto Less = [&](Expression LeftValue, Expression RightValue) {
      if (Comparator)
        return emitBinaryPredicate(json::Object(*Comparator), ComparatorType,
                                   std::move(LeftValue), std::move(RightValue),
                                   L);
      return binary("<", std::move(LeftValue), std::move(RightValue), "bool",
                    L);
    };
    const auto CheckChild = labelName(), SelectChild = labelName();
    const auto CompareChildren = labelName(), SelectRight = labelName();
    const auto CompareRoot = labelName(), SwapDown = labelName();
    const auto Done = labelName();
    jump(CheckChild, L);
    label(CheckChild, L);
    branch(binary("<", Root,
                  binary("/", Size, quantity(2, DifferenceType, L),
                         DifferenceType, L),
                  "bool", L),
           SelectChild, Done, L);
    label(SelectChild, L);
    assign(Left,
           binary("+",
                  binary("*", Root, quantity(2, DifferenceType, L),
                         DifferenceType, L),
                  quantity(1, DifferenceType, L), DifferenceType, L),
           L);
    assign(Child, Left, L);
    assign(Right,
           binary("+", Left, quantity(1, DifferenceType, L), DifferenceType, L),
           L);
    branch(binary("<", Right, Size, "bool", L), CompareChildren, CompareRoot,
           L);
    label(CompareChildren, L);
    branch(Less(At(Child), At(Right)), SelectRight, CompareRoot, L);
    label(SelectRight, L);
    assign(Child, Right, L);
    jump(CompareRoot, L);
    label(CompareRoot, L);
    branch(Less(At(Root), At(Child)), SwapDown, Done, L);
    label(SwapDown, L);
    auto RootValue = snapshot(At(Root), L);
    auto ChildValue = snapshot(At(Child), L);
    assign(At(Root), std::move(ChildValue), L);
    assign(At(Child), std::move(RootValue), L);
    assign(Root, Child, L);
    jump(CheckChild, L);
    label(Done, L);
  }

  void makeHeap(Expression First, Expression Size, llvm::StringRef PointerType,
                llvm::StringRef DifferenceType, SourceLocation L,
                const std::optional<Expression> &Comparator = std::nullopt,
                QualType ComparatorType = {}) {
    auto Start = temporary(DifferenceType, L);
    const auto Initialize = labelName(), Sift = labelName();
    const auto Previous = labelName(), Done = labelName();
    branch(binary(">", Size, quantity(1, DifferenceType, L), "bool", L),
           Initialize, Done, L);
    label(Initialize, L);
    assign(Start,
           binary("/",
                  binary("-", Size, quantity(2, DifferenceType, L),
                         DifferenceType, L),
                  quantity(2, DifferenceType, L), DifferenceType, L),
           L);
    jump(Sift, L);
    label(Sift, L);
    heapSiftDown(json::Object(First), json::Object(Size), json::Object(Start),
                 PointerType, DifferenceType, L, Comparator, ComparatorType);
    branch(binary(">", Start, quantity(0, DifferenceType, L), "bool", L),
           Previous, Done, L);
    label(Previous, L);
    assign(
        Start,
        binary("-", Start, quantity(1, DifferenceType, L), DifferenceType, L),
        L);
    jump(Sift, L);
    label(Done, L);
  }

  void sortHeap(Expression First, Expression Size, llvm::StringRef PointerType,
                llvm::StringRef DifferenceType, SourceLocation L,
                const std::optional<Expression> &Comparator = std::nullopt,
                QualType ComparatorType = {}) {
    auto HeapSize = snapshot(std::move(Size), L);
    auto At = [&](const Expression &Position) {
      return dereference(binary("+", json::Object(First),
                                json::Object(Position), PointerType, L),
                         L);
    };
    const auto Check = labelName(), Extract = labelName(), Done = labelName();
    jump(Check, L);
    label(Check, L);
    branch(binary(">", HeapSize, quantity(1, DifferenceType, L), "bool", L),
           Extract, Done, L);
    label(Extract, L);
    assign(HeapSize,
           binary("-", HeapSize, quantity(1, DifferenceType, L), DifferenceType,
                  L),
           L);
    auto TopValue = snapshot(dereference(json::Object(First), L), L);
    auto EndValue = snapshot(At(HeapSize), L);
    assign(dereference(json::Object(First), L), std::move(EndValue), L);
    assign(At(HeapSize), std::move(TopValue), L);
    heapSiftDown(json::Object(First), json::Object(HeapSize),
                 quantity(0, DifferenceType, L), PointerType, DifferenceType, L,
                 Comparator, ComparatorType);
    jump(Check, L);
    label(Done, L);
  }

  void stableMerge(Expression First, Expression Middle, Expression Last,
                   llvm::StringRef PointerType, llvm::StringRef ElementType,
                   llvm::StringRef DifferenceType, SourceLocation L,
                   const std::optional<Expression> &Comparator = std::nullopt,
                   QualType ComparatorType = {}) {
    auto Left = snapshot(std::move(First), L);
    auto Right = snapshot(std::move(Middle), L);
    auto End = snapshot(std::move(Last), L);
    auto Shift = temporary(PointerType, L);
    auto Previous = temporary(PointerType, L);
    auto Value = temporary(ElementType, L);
    auto Less = [&](Expression LeftValue, Expression RightValue) {
      if (Comparator)
        return emitBinaryPredicate(json::Object(*Comparator), ComparatorType,
                                   std::move(LeftValue), std::move(RightValue),
                                   L);
      return binary("<", std::move(LeftValue), std::move(RightValue), "bool",
                    L);
    };
    const auto CheckLeft = labelName(), CheckRight = labelName();
    const auto Compare = labelName(), AdvanceLeft = labelName();
    const auto Save = labelName(), CheckShift = labelName();
    const auto ShiftOne = labelName(), Place = labelName();
    const auto Done = labelName();
    jump(CheckLeft, L);
    label(CheckLeft, L);
    branch(binary("!=", Left, Right, "bool", L), CheckRight, Done, L);
    label(CheckRight, L);
    branch(binary("!=", Right, End, "bool", L), Compare, Done, L);
    label(Compare, L);
    branch(Less(dereference(json::Object(Right), L),
                dereference(json::Object(Left), L)),
           Save, AdvanceLeft, L);
    label(AdvanceLeft, L);
    assign(Left,
           binary("+", Left, quantity(1, DifferenceType, L), PointerType, L),
           L);
    jump(CheckLeft, L);
    label(Save, L);
    assign(Value, dereference(json::Object(Right), L), L);
    assign(Shift, Right, L);
    jump(CheckShift, L);
    label(CheckShift, L);
    branch(binary("!=", Shift, Left, "bool", L), ShiftOne, Place, L);
    label(ShiftOne, L);
    assign(Previous,
           binary("-", Shift, quantity(1, DifferenceType, L), PointerType, L),
           L);
    assign(dereference(json::Object(Shift), L),
           dereference(json::Object(Previous), L), L);
    assign(Shift, Previous, L);
    jump(CheckShift, L);
    label(Place, L);
    assign(dereference(json::Object(Left), L), Value, L);
    assign(Left,
           binary("+", Left, quantity(1, DifferenceType, L), PointerType, L),
           L);
    assign(Right,
           binary("+", Right, quantity(1, DifferenceType, L), PointerType, L),
           L);
    jump(CheckLeft, L);
    label(Done, L);
  }

  Expression utilityOperation(const CallExpr *Call, UtilityOperation Operation,
                              std::optional<Expression> Destination) {
    auto L = Call->getExprLoc();
    auto ArrayFor = [&](QualType Type) {
      return approvedUtilityArrayRecord(
          A.S, A.Sources,
          Type.isNull() ? nullptr : Type->getAsCXXRecordDecl(), A.Context);
    };
    auto InitializerListFor = [&](QualType Type) {
      return approvedUtilityInitializerListRecord(
          A.S, A.Sources, Type.isNull() ? nullptr : Type->getAsCXXRecordDecl(),
          A.Context);
    };
    auto OptionalFor = [&](QualType Type) {
      return approvedUtilityOptionalRecord(
          A.S, A.Sources, Type.isNull() ? nullptr : Type->getAsCXXRecordDecl(),
          A.Context);
    };
    auto TupleFor = [&](QualType Type) {
      return approvedUtilityTupleRecord(
          A.S, A.Sources, Type.isNull() ? nullptr : Type->getAsCXXRecordDecl(),
          A.Context);
    };
    auto MemberObject = [&]() -> const Expr * {
      if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call))
        return Operator->getNumArgs() ? Operator->getArg(0) : nullptr;
      if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
        return Member->getImplicitObjectArgument();
      return nullptr;
    };
    auto UniquePtrMember = [&](Expression Base,
                               const UtilityUniquePtrRecord &Unique) {
      return Expression{{"kind", "member"},
                        {"type", type(Unique.PointerType, L)},
                        {"name", "nct_unique_ptr_pointer"},
                        {"args", json::Array{std::move(Base)}},
                        {"loc", A.loc(L)}};
    };
    auto ReferenceMember = [&](Expression Base,
                               const FunctionalReferenceRecord &Wrapper) {
      return Expression{{"kind", "member"},
                        {"type", type(Wrapper.PointerType, L)},
                        {"name", "nct_reference_wrapper_pointer"},
                        {"args", json::Array{std::move(Base)}},
                        {"loc", A.loc(L)}};
    };
    auto OptionalObject = [&]() -> const Expr * {
      const Expr *Object = MemberObject();
      while (const auto *Cast = dyn_cast_or_null<ImplicitCastExpr>(Object)) {
        if (Cast->getCastKind() != CK_NoOp &&
            Cast->getCastKind() != CK_DerivedToBase &&
            Cast->getCastKind() != CK_UncheckedDerivedToBase)
          break;
        Object = Cast->getSubExpr();
      }
      return Object;
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
    using UtilityComparisonLeaves =
        std::vector<std::pair<Expression, Expression>>;
    auto CollectUtilityComparisonLeaves =
        [&](auto &&Collect, const Expression &LeftValue, QualType LeftType,
            const Expression &RightValue, QualType RightType, bool Ordered,
            UtilityComparisonLeaves &Leaves, unsigned Depth) -> bool {
      if (Depth > 64 || LeftType.isNull() || RightType.isNull())
        return false;
      if (const auto Common = utilityScalarComparisonType(A.Context, LeftType,
                                                          RightType, Ordered)) {
        const auto CommonType = type(*Common, L);
        Leaves.emplace_back(cast(json::Object(LeftValue), CommonType, L),
                            cast(json::Object(RightValue), CommonType, L));
        return true;
      }

      const auto LeftArray = ArrayFor(LeftType);
      const auto RightArray = ArrayFor(RightType);
      if (LeftArray || RightArray) {
        if (!LeftArray || !RightArray ||
            LeftArray->Record->getCanonicalDecl() !=
                RightArray->Record->getCanonicalDecl() ||
            LeftArray->Size != RightArray->Size)
          return false;
        const auto SizeType = type(A.Context.getSizeType(), L);
        for (uint64_t I = 0; I < LeftArray->Size; ++I) {
          auto LeftElement = ArrayElement(json::Object(LeftValue), *LeftArray,
                                          quantity(I, SizeType, L),
                                          LeftArray->ElementType, true);
          auto RightElement = ArrayElement(
              json::Object(RightValue), *RightArray, quantity(I, SizeType, L),
              RightArray->ElementType, true);
          if (!Collect(Collect, LeftElement, LeftArray->ElementType,
                       RightElement, RightArray->ElementType, Ordered, Leaves,
                       Depth + 1))
            return false;
        }
        return true;
      }

      const auto LeftPair = approvedUtilityPairRecord(
          A.S, A.Sources, LeftType.getUnqualifiedType()->getAsCXXRecordDecl(),
          A.Context);
      const auto RightPair = approvedUtilityPairRecord(
          A.S, A.Sources, RightType.getUnqualifiedType()->getAsCXXRecordDecl(),
          A.Context);
      if (LeftPair || RightPair) {
        if (!LeftPair || !RightPair)
          return false;
        auto LeftFirst =
            fieldStorage(json::Object(LeftValue), LeftPair->First, L);
        auto RightFirst =
            fieldStorage(json::Object(RightValue), RightPair->First, L);
        if (!Collect(Collect, LeftFirst, LeftPair->First->getType(), RightFirst,
                     RightPair->First->getType(), Ordered, Leaves, Depth + 1))
          return false;
        auto LeftSecond =
            fieldStorage(json::Object(LeftValue), LeftPair->Second, L);
        auto RightSecond =
            fieldStorage(json::Object(RightValue), RightPair->Second, L);
        return Collect(Collect, LeftSecond, LeftPair->Second->getType(),
                       RightSecond, RightPair->Second->getType(), Ordered,
                       Leaves, Depth + 1);
      }

      const auto LeftTuple = TupleFor(LeftType);
      const auto RightTuple = TupleFor(RightType);
      if (!LeftTuple && !RightTuple)
        return false;
      if (!LeftTuple || !RightTuple ||
          LeftTuple->Elements.size() != RightTuple->Elements.size())
        return false;
      for (unsigned I = 0; I < LeftTuple->Elements.size(); ++I) {
        auto LeftElement =
            fieldStorage(json::Object(LeftValue), LeftTuple->Elements[I], L);
        auto RightElement =
            fieldStorage(json::Object(RightValue), RightTuple->Elements[I], L);
        if (!Collect(Collect, LeftElement, LeftTuple->Elements[I]->getType(),
                     RightElement, RightTuple->Elements[I]->getType(), Ordered,
                     Leaves, Depth + 1))
          return false;
      }
      return true;
    };
    auto CompareUtilityValues =
        [&](llvm::StringRef Operator, const Expression &LeftValue,
            QualType LeftType, const Expression &RightValue,
            QualType RightType) -> Expression {
      const bool Ordered = Operator != "==" && Operator != "!=";
      UtilityComparisonLeaves Leaves;
      if (!CollectUtilityComparisonLeaves(CollectUtilityComparisonLeaves,
                                          LeftValue, LeftType, RightValue,
                                          RightType, Ordered, Leaves, 0))
        reject(L, "utility value comparison",
               "The selected values have no approved recursive comparison.");
      auto Equal = [&]() -> Expression {
        if (Leaves.empty())
          return boolean(true, L);
        auto Result = temporary("bool", L);
        const auto True = labelName(), False = labelName(), End = labelName();
        std::vector<std::string> Next;
        Next.reserve(Leaves.size() - 1);
        for (std::size_t I = 1; I < Leaves.size(); ++I)
          Next.push_back(labelName());
        for (std::size_t I = 0; I < Leaves.size(); ++I) {
          const auto Success = I + 1 == Leaves.size() ? True : Next[I];
          branch(binary("==", json::Object(Leaves[I].first),
                        json::Object(Leaves[I].second), "bool", L),
                 Success, False, L);
          if (I + 1 != Leaves.size())
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
      auto Less = [&](bool ReverseOrder) -> Expression {
        if (Leaves.empty())
          return boolean(false, L);
        auto Result = temporary("bool", L);
        const auto True = labelName(), False = labelName(), End = labelName();
        for (const auto &Leaf : Leaves) {
          const auto Reverse = labelName(), Next = labelName();
          const auto &First = ReverseOrder ? Leaf.second : Leaf.first;
          const auto &Second = ReverseOrder ? Leaf.first : Leaf.second;
          branch(
              binary("<", json::Object(First), json::Object(Second), "bool", L),
              True, Reverse, L);
          label(Reverse, L);
          branch(
              binary("<", json::Object(Second), json::Object(First), "bool", L),
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
      if (Operator == "==")
        Result = Equal();
      else if (Operator == "!=") {
        Result = Equal();
        Negate = true;
      } else if (Operator == "<")
        Result = Less(false);
      else if (Operator == ">")
        Result = Less(true);
      else if (Operator == "<=") {
        Result = Less(true);
        Negate = true;
      } else if (Operator == ">=") {
        Result = Less(false);
        Negate = true;
      } else {
        reject(L, "utility value comparison",
               "Unknown approved recursive comparison.");
      }
      if (!Negate)
        return Result;
      return snapshot(Expression{{"kind", "unary"},
                                 {"type", "bool"},
                                 {"operator", "!"},
                                 {"args", json::Array{std::move(Result)}},
                                 {"loc", A.loc(L)}},
                      L);
    };
    auto UtilityComparisonOperator = [&](UtilityOperation Comparison) {
      switch (Comparison) {
      case UtilityOperation::PairEqual:
      case UtilityOperation::TupleEqual:
      case UtilityOperation::ArrayEqual:
      case UtilityOperation::OptionalEqual:
        return llvm::StringRef("==");
      case UtilityOperation::PairNotEqual:
      case UtilityOperation::TupleNotEqual:
      case UtilityOperation::ArrayNotEqual:
      case UtilityOperation::OptionalNotEqual:
        return llvm::StringRef("!=");
      case UtilityOperation::PairLess:
      case UtilityOperation::TupleLess:
      case UtilityOperation::ArrayLess:
      case UtilityOperation::OptionalLess:
        return llvm::StringRef("<");
      case UtilityOperation::PairGreater:
      case UtilityOperation::TupleGreater:
      case UtilityOperation::ArrayGreater:
      case UtilityOperation::OptionalGreater:
        return llvm::StringRef(">");
      case UtilityOperation::PairLessEqual:
      case UtilityOperation::TupleLessEqual:
      case UtilityOperation::ArrayLessEqual:
      case UtilityOperation::OptionalLessEqual:
        return llvm::StringRef("<=");
      case UtilityOperation::PairGreaterEqual:
      case UtilityOperation::TupleGreaterEqual:
      case UtilityOperation::ArrayGreaterEqual:
      case UtilityOperation::OptionalGreaterEqual:
        return llvm::StringRef(">=");
      default:
        reject(L, "utility value comparison",
               "Unknown approved comparison operation.");
      }
    };
    auto AlgorithmEqual = [&](Expression Left, unsigned LeftIndex,
                              Expression Right, unsigned RightIndex) {
      return CompareUtilityValues(
          "==", Left, Call->getArg(LeftIndex)->getType()->getPointeeType(),
          Right, Call->getArg(RightIndex)->getType()->getPointeeType());
    };
    auto ReverseFor = [&](QualType Type) {
      return approvedUtilityReverseIteratorRecord(
          A.S, A.Sources, Type.isNull() ? nullptr : Type->getAsCXXRecordDecl(),
          A.Context);
    };
    auto ReverseCurrent = [&](Expression Base,
                              const UtilityReverseIteratorRecord &Reverse) {
      return fieldStorage(std::move(Base), Reverse.Current, L);
    };
    auto ReverseValue = [&](Expression Pointer,
                            const UtilityReverseIteratorRecord &Reverse) {
      auto RecordType = A.Context.getRecordType(Reverse.Record);
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(RecordType, L);
      Destination.reset();
      if (Place.getString("type") != type(RecordType, L))
        reject(
            L, "reverse iterator result",
            "The destination type differs from the reverse iterator result.");
      auto Value = snapshot(
          cast(std::move(Pointer), type(Reverse.IteratorType, L), L), L);
      assign(fieldStorage(json::Object(Place), Reverse.Legacy, L),
             json::Object(Value), L);
      assign(fieldStorage(json::Object(Place), Reverse.Current, L),
             std::move(Value), L);
      return Place;
    };
    auto AllocatorMaximum = [&](const UtilityAllocatorRecord &Allocator,
                                llvm::StringRef Description) {
      if (Allocator.ElementType->isVoidType() ||
          Allocator.ElementType->isIncompleteType())
        reject(L, Description,
               "A checked complete std::allocator element is required.");
      const auto ResultType = type(Call->getType(), L);
      const unsigned Bits = integerBits(ResultType);
      const uint64_t Maximum = Bits == 64
                                   ? std::numeric_limits<uint64_t>::max()
                                   : (uint64_t(1) << Bits) - 1;
      const uint64_t ElementBytes =
          A.Context.getTypeSizeInChars(Allocator.ElementType).getQuantity();
      if (!ElementBytes)
        reject(L, Description,
               "The allocator element must have positive complete size.");
      return quantity(Maximum / ElementBytes, ResultType, L);
    };
    auto ConstructAt = [&](Expression Place, QualType ElementType,
                           const CXXConstructorDecl *Constructor,
                           const CXXConstructExpr *Construction,
                           unsigned ArgumentIndex,
                           llvm::StringRef Description) {
      if (ArgumentIndex > Call->getNumArgs())
        reject(L, Description,
               "The checked construction arguments are missing.");
      const unsigned ArgumentCount = Call->getNumArgs() - ArgumentIndex;
      if (!Constructor) {
        if (!ArgumentCount) {
          initializeZero(std::move(Place), ElementType, L);
          return;
        }
        if (ArgumentCount != 1)
          reject(L, Description,
                 "A scalar construction needs zero or one argument.");
        assign(std::move(Place),
               cast(expression(Call->getArg(ArgumentIndex)),
                    type(ElementType, L), L),
               L);
        return;
      }

      const unsigned ConstructionArguments =
          Construction ? Construction->getNumArgs() : ArgumentCount;
      if (ConstructionArguments < ArgumentCount ||
          Constructor->getNumParams() != ConstructionArguments)
        reject(L, Description,
               "The selected constructor and argument counts differ.");
      if (!ConstructionArguments && Constructor->isDefaultConstructor()) {
        constructMemoryDefault(std::move(Place), ElementType, Constructor, true,
                               L);
        return;
      }
      if (ArgumentCount == 1 && ConstructionArguments == 1 &&
          Constructor->isCopyOrMoveConstructor()) {
        auto Source = argument(Call->getArg(ArgumentIndex),
                               Constructor->getParamDecl(0)->getType());
        constructMemorySource(std::move(Place), ElementType, Constructor,
                              std::move(Source), L);
        return;
      }
      if (Constructor->isTrivial() || !Constructor->hasBody())
        reject(L, Description,
               "The selected source constructor has no checked body.");
      json::Array Args;
      Args.push_back(snapshot(
          address(std::move(Place), ElementType.getUnqualifiedType(), L), L));
      for (unsigned I = 0; I < ConstructionArguments; ++I) {
        const auto Parameter = Constructor->getParamDecl(I)->getType();
        const auto *Actual = I < ArgumentCount ? Call->getArg(ArgumentIndex + I)
                                               : Construction->getArg(I);
        if (Parameter->isReferenceType() || recordValue(Parameter))
          Args.push_back(argument(Actual, Parameter));
        else
          Args.push_back(snapshot(
              cast(argument(Actual, Parameter), type(Parameter, L), L), L));
      }
      chargeCall(Args, L);
      Body.push_back(json::Object{{"op", "call"},
                                  {"callee", A.name(Constructor)},
                                  {"args", std::move(Args)},
                                  {"loc", A.loc(L)}});
    };
    auto AllocatorConstruct = [&](const UtilityAllocatorConstructCall &Info,
                                  unsigned PointerIndex,
                                  unsigned ArgumentIndex) {
      if (PointerIndex >= Call->getNumArgs() ||
          ArgumentIndex > Call->getNumArgs())
        reject(L, "allocator construct",
               "The checked pointer and construction arguments are missing.");
      auto Pointer = snapshot(expression(Call->getArg(PointerIndex)), L);
      ConstructAt(dereference(std::move(Pointer), L), Info.ElementType,
                  Info.Constructor, Info.Construction, ArgumentIndex,
                  "allocator construct");
    };
    auto AllocatorHeap = [&](const UtilityAllocatorHeapCall &Info) {
      if (Info.Traits) {
        if (!Call->getNumArgs())
          reject(L, "allocator heap operation",
                 "A checked allocator reference is required.");
        lvalue(Call->getArg(0));
      } else {
        const auto *Object = MemberObject();
        if (!Object)
          reject(L, "allocator heap operation",
                 "A checked std::allocator receiver is required.");
        lvalue(Object);
      }
      const unsigned First = Info.Traits ? 1u : 0u;
      const auto Size = type(A.Context.getSizeType(), L);
      const uint64_t ElementBytes =
          A.Context.getTypeSizeInChars(Info.Allocator.ElementType)
              .getQuantity();
      if (!ElementBytes)
        reject(L, "allocator heap operation",
               "The allocator element must have positive complete size.");

      if (Info.Allocate) {
        if (First >= Call->getNumArgs())
          reject(L, "allocator allocation",
                 "A checked constant element count is required.");
        auto Count = snapshot(expression(Call->getArg(First)), L);
        if (Info.Hint) {
          if (First + 1 >= Call->getNumArgs())
            reject(L, "allocator allocation",
                   "The allocation hint is missing.");
          discard(Call->getArg(First + 1));
        }
        const auto *Function =
            A.allocatorHeapFunction(true, Info.Allocator.ElementType, L);
        json::Array Args;
        Args.push_back(cast(binary("*", std::move(Count),
                                   quantity(ElementBytes, Size, L), Size, L),
                            type(Function->getParamDecl(0)->getType(), L), L));
        chargeCall(Args, L);
        auto Storage = temporary(type(Function->getReturnType(), L), L);
        Body.push_back(json::Object{{"op", "call"},
                                    {"callee", A.name(Function)},
                                    {"args", std::move(Args)},
                                    {"target", json::Object(Storage)},
                                    {"loc", A.loc(L)}});
        return snapshot(cast(std::move(Storage), type(Call->getType(), L), L),
                        L);
      }

      if (First + 1 >= Call->getNumArgs())
        reject(L, "allocator deallocation",
               "A checked pointer and element count are required.");
      auto Pointer = snapshot(expression(Call->getArg(First)), L);
      auto Count = snapshot(expression(Call->getArg(First + 1)), L);
      const auto *Function =
          A.allocatorHeapFunction(false, Info.Allocator.ElementType, L);
      json::Array Args;
      Args.push_back(cast(std::move(Pointer),
                          type(Function->getParamDecl(0)->getType(), L), L));
      if (Function->getNumParams() == 2)
        Args.push_back(cast(binary("*", std::move(Count),
                                   quantity(ElementBytes, Size, L), Size, L),
                            type(Function->getParamDecl(1)->getType(), L), L));
      chargeCall(Args, L);
      Body.push_back(json::Object{{"op", "call"},
                                  {"callee", A.name(Function)},
                                  {"args", std::move(Args)},
                                  {"loc", A.loc(L)}});
      return Expression();
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
    case UtilityOperation::FunctionalInvoke: {
      auto CallableType = Call->getArg(0)->getType();
      const auto *Prototype =
          CallableType->isFunctionType()
              ? CallableType->getAs<FunctionProtoType>()
              : CallableType->getPointeeType()->getAs<FunctionProtoType>();
      if (!Prototype || Prototype->isVariadic() ||
          Prototype->getNumParams() + 1 != Call->getNumArgs())
        reject(L, "functional invoke",
               "A checked fixed-arity function or function pointer is required.");
      auto Callable = snapshot(CallableType->isFunctionType()
                                   ? functionValue(Call->getArg(0))
                                   : expression(Call->getArg(0)),
                               L);
      json::Array Arguments;
      for (unsigned I = 0; I < Prototype->getNumParams(); ++I) {
        auto Parameter = Prototype->getParamType(I);
        Arguments.push_back(cast(argument(Call->getArg(I + 1), Parameter),
                                 type(Parameter, L), L));
      }
      return emitIndirectCall(std::move(Callable), std::move(Arguments),
                              Prototype->getReturnType(), L);
    }
    case UtilityOperation::FunctionalInvokeObject: {
      auto Approved = approvedFunctionalInvokeObjectOperation(
          A.S, A.Sources, Call, A.Context);
      if (!Approved)
        reject(L, "functional invoke",
               "A checked standard function object is required.");
      return functionalInvokeObjectOperation(Call, *Approved);
    }
    case UtilityOperation::FunctionalInvokeReference: {
      const auto Info = approvedFunctionalReferenceInvokeCall(
          A.S, A.Sources, Call, A.Context);
      if (!Info || !Call->getNumArgs())
        reject(L, "functional reference invoke",
               "A checked callable std::reference_wrapper is required.");
      auto Wrapper = lvalue(Call->getArg(0));
      auto Referent = snapshot(
          ReferenceMember(std::move(Wrapper), Info->Wrapper), L);
      if (Info->Kind == FunctionalReferenceInvokeKind::FunctionObject) {
        if (!Info->Operation)
          reject(L, "functional reference invoke",
                 "A checked standard function object is required.");
        const bool Unary = Info->Operation->RightType.isNull();
        if (Call->getNumArgs() != (Unary ? 2u : 3u))
          reject(L, "functional reference invoke",
                 "The checked function object arity must match its arguments.");
        auto Left = snapshot(expression(Call->getArg(1)), L);
        std::optional<Expression> Right;
        if (!Unary)
          Right = snapshot(expression(Call->getArg(2)), L);
        return functionalOperationValues(L, std::move(Left),
                                         std::move(Right), *Info->Operation);
      }
      const auto *Prototype =
          Info->FunctionPointerType->isFunctionPointerType()
              ? Info->FunctionPointerType->getPointeeType()
                    ->getAs<FunctionProtoType>()
              : nullptr;
      if (!Prototype || Prototype->isVariadic() ||
          Prototype->getNumParams() + 1 != Call->getNumArgs())
        reject(L, "functional reference invoke",
               "A checked fixed-arity function pointer is required.");
      auto Callable = snapshot(dereference(std::move(Referent), L), L);
      json::Array Arguments;
      for (unsigned I = 0; I < Prototype->getNumParams(); ++I) {
        const auto Parameter = Prototype->getParamType(I);
        Arguments.push_back(cast(argument(Call->getArg(I + 1), Parameter),
                                 type(Parameter, L), L));
      }
      return emitIndirectCall(std::move(Callable), std::move(Arguments),
                              Prototype->getReturnType(), L);
    }
    case UtilityOperation::FunctionalReferenceFactory: {
      const auto Info = approvedFunctionalReferenceFactoryCall(
          A.S, A.Sources, Call, A.Context);
      if (!Info || Call->getNumArgs() != 1)
        reject(L, "functional reference factory",
               "A checked std::ref or std::cref call is required.");
      auto Pointer = snapshot(
          cast(address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L),
               type(Info->Result.PointerType, L), L),
          L);
      const auto RecordType = A.Context.getRecordType(Info->Result.Record);
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(RecordType, L);
      Destination.reset();
      if (Place.getString("type") != type(RecordType, L))
        reject(L, "functional reference result",
               "The destination type differs from the reference wrapper.");
      if (Info->Result.PaddedBase) {
        Expression Base{{"kind", "member"},
                        {"type", type(A.Context.getSizeType(), L)},
                        {"name", "nct_reference_wrapper_base_storage"},
                        {"args", json::Array{json::Object(Place)}},
                        {"loc", A.loc(L)}};
        assign(std::move(Base), A.zero(A.Context.getSizeType(), L), L);
      }
      assign(ReferenceMember(json::Object(Place), Info->Result),
             std::move(Pointer), L);
      return Place;
    }
    case UtilityOperation::FunctionalReferenceAccess: {
      const auto Info = approvedFunctionalReferenceAccessCall(
          A.S, A.Sources, Call, A.Context);
      if (!Info)
        reject(L, "functional reference access",
               "A checked std::reference_wrapper access is required.");
      auto Object = Info->ObjectIsArrow
                        ? dereference(snapshot(expression(Info->Object), L), L)
                        : lvalue(Info->Object);
      auto Pointer = snapshot(
          ReferenceMember(std::move(Object), Info->Wrapper), L);
      return dereference(std::move(Pointer), L);
    }
    case UtilityOperation::NewLaunder:
      // The portable pointer model carries no stale C++ object provenance.
      // Retain the checked pointer value once; a later access observes the
      // lifetime selected by the source's authenticated std::launder call.
      return snapshot(expression(Call->getArg(0)), L);
    case UtilityOperation::MemoryDefaultDelete: {
      const auto Info =
          approvedUtilityDefaultDeleteCall(A.S, A.Sources, Call, A.Context);
      if (!Info || Info->PointerIndex >= Call->getNumArgs())
        reject(L, "default delete",
               "A checked std::default_delete call is required.");
      discard(Info->Object);
      if (Info->Deleter.Array) {
        deallocateArray(Info->Delete,
                        expression(Call->getArg(Info->PointerIndex)), L);
        return {};
      }
      const auto *Function =
          A.defaultDeleteFunction(Info->Deleter, Info->Delete, L);
      deallocateSingle(expression(Call->getArg(Info->PointerIndex)),
                       Info->Deleter.ElementType, Function, L);
      return {};
    }
    case UtilityOperation::MemoryMakeUnique: {
      const auto Info =
          approvedUtilityMakeUniqueCall(A.S, A.Sources, Call, A.Context);
      if (!Info)
        reject(L, "make_unique",
               "A checked std::make_unique call is required.");
      const bool Array = Info->Owner.Deleter.Array;
      const auto *Function = A.allocationFunction(
          Info->Allocation->getOperatorNew(), true, L, Array);
      A.uniquePtrDeleteFunction(Info->Owner, L);

      auto Pointer = [&]() -> Expression {
        if (Array) {
          if (!Info->ArrayCount)
            reject(L, "make_unique array extent",
                   "An authenticated constant array extent is required.");
          return allocateArray(Info->Allocation, Info->ArrayCount, L);
        }
        json::Array Args;
        Args.push_back(allocationExtent(Info->Owner.ElementType,
                                        Function->getParamDecl(0)->getType(),
                                        false, L));
        chargeCall(Args, L);
        auto Storage = temporary(type(Function->getReturnType(), L), L);
        Body.push_back(json::Object{{"op", "call"},
                                    {"callee", A.name(Function)},
                                    {"args", std::move(Args)},
                                    {"target", json::Object(Storage)},
                                    {"loc", A.loc(L)}});
        auto Result = snapshot(
            cast(std::move(Storage), type(Info->Owner.PointerType, L), L), L);
        auto MutablePointer =
            cast(json::Object(Result),
                 type(A.Context.getPointerType(
                          Info->Owner.ElementType.getUnqualifiedType()),
                      L),
                 L);
        ConstructAt(dereference(std::move(MutablePointer), L),
                    Info->Owner.ElementType, Info->Constructor,
                    Info->Construction, 0, "make_unique construction");
        return Result;
      }();

      const auto RecordType = A.Context.getRecordType(Info->Owner.Record);
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(RecordType, L);
      Destination.reset();
      if (Place.getString("type") != type(RecordType, L))
        reject(L, "make_unique result",
               "The destination type differs from the unique_ptr result.");
      assign(UniquePtrMember(json::Object(Place), Info->Owner),
             std::move(Pointer), L);
      return Place;
    }
    case UtilityOperation::MemoryUniquePtrGet:
    case UtilityOperation::MemoryUniquePtrGetDeleter:
    case UtilityOperation::MemoryUniquePtrArrow:
    case UtilityOperation::MemoryUniquePtrDereference:
    case UtilityOperation::MemoryUniquePtrSubscript:
    case UtilityOperation::MemoryUniquePtrBoolean:
    case UtilityOperation::MemoryUniquePtrRelease:
    case UtilityOperation::MemoryUniquePtrReset:
    case UtilityOperation::MemoryUniquePtrMoveAssign:
    case UtilityOperation::MemoryUniquePtrConvertingMoveAssign:
    case UtilityOperation::MemoryUniquePtrNullAssign:
    case UtilityOperation::MemoryUniquePtrMemberSwap: {
      const auto Info =
          approvedUtilityUniquePtrCall(A.S, A.Sources, Call, A.Context);
      if (!Info)
        reject(L, "unique pointer operation",
               "A checked std::unique_ptr operation is required.");
      auto Expected = [&] {
        switch (Operation) {
        case UtilityOperation::MemoryUniquePtrGet:
          return UtilityUniquePtrOperation::Get;
        case UtilityOperation::MemoryUniquePtrGetDeleter:
          return UtilityUniquePtrOperation::GetDeleter;
        case UtilityOperation::MemoryUniquePtrArrow:
          return UtilityUniquePtrOperation::Arrow;
        case UtilityOperation::MemoryUniquePtrDereference:
          return UtilityUniquePtrOperation::Dereference;
        case UtilityOperation::MemoryUniquePtrSubscript:
          return UtilityUniquePtrOperation::Subscript;
        case UtilityOperation::MemoryUniquePtrBoolean:
          return UtilityUniquePtrOperation::Boolean;
        case UtilityOperation::MemoryUniquePtrRelease:
          return UtilityUniquePtrOperation::Release;
        case UtilityOperation::MemoryUniquePtrReset:
          return UtilityUniquePtrOperation::Reset;
        case UtilityOperation::MemoryUniquePtrMoveAssign:
          return UtilityUniquePtrOperation::MoveAssign;
        case UtilityOperation::MemoryUniquePtrConvertingMoveAssign:
          return UtilityUniquePtrOperation::ConvertingMoveAssign;
        case UtilityOperation::MemoryUniquePtrNullAssign:
          return UtilityUniquePtrOperation::NullAssign;
        case UtilityOperation::MemoryUniquePtrMemberSwap:
          return UtilityUniquePtrOperation::Swap;
        default:
          llvm_unreachable("not a unique pointer operation");
        }
      }();
      if (Info->Operation != Expected)
        reject(
            L, "unique pointer operation",
            "The selected std::unique_ptr operation changed after checking.");

      auto OwnerObject = [&] {
        return Info->ObjectIsArrow ? dereference(expression(Info->Object), L)
                                   : lvalue(Info->Object);
      };
      if (Expected == UtilityUniquePtrOperation::GetDeleter) {
        auto OwnerAddress =
            Info->ObjectIsArrow
                ? expression(Info->Object)
                : address(lvalue(Info->Object), Info->Object->getType(), L);
        auto ResultPointer = A.Context.getPointerType(Call->getType());
        auto ErasedAddress = cast(
            std::move(OwnerAddress),
            Call->getType().isConstQualified() ? "cptr:void" : "ptr:void", L);
        return dereference(
            cast(std::move(ErasedAddress), type(ResultPointer, L), L), L);
      }
      if (Expected == UtilityUniquePtrOperation::Get ||
          Expected == UtilityUniquePtrOperation::Arrow) {
        return snapshot(UniquePtrMember(OwnerObject(), Info->Owner), L);
      }
      if (Expected == UtilityUniquePtrOperation::Dereference) {
        auto Pointer = snapshot(UniquePtrMember(OwnerObject(), Info->Owner), L);
        return dereference(std::move(Pointer), L);
      }
      if (Expected == UtilityUniquePtrOperation::Subscript) {
        if (Info->ArgumentIndex >= Call->getNumArgs())
          reject(L, "unique pointer subscript",
                 "The checked array index is missing.");
        auto Pointer = snapshot(UniquePtrMember(OwnerObject(), Info->Owner), L);
        auto Offset = expression(Call->getArg(Info->ArgumentIndex));
        return index(std::move(Pointer), std::move(Offset),
                     type(Info->Owner.ElementType, L), L);
      }
      if (Expected == UtilityUniquePtrOperation::Boolean) {
        auto Pointer = snapshot(UniquePtrMember(OwnerObject(), Info->Owner), L);
        return cast(std::move(Pointer), "bool", L);
      }

      const auto RecordType = A.Context.getRecordType(Info->Owner.Record);
      auto CaptureReceiver = [&] {
        return snapshot(
            Info->ObjectIsArrow
                ? cast(expression(Info->Object),
                       type(A.Context.getPointerType(RecordType), L), L)
                : address(lvalue(Info->Object), RecordType, L),
            L);
      };
      std::optional<Expression> Receiver;
      // An explicit member call sequences its postfix receiver before every
      // argument. Operator notation keeps the operator's own sequencing.
      if (isa<CXXMemberCallExpr>(Call))
        Receiver = CaptureReceiver();
      std::optional<Expression> MoveSourceAddress;
      std::optional<UtilityUniquePtrRecord> MoveSourceOwner;
      if (Expected == UtilityUniquePtrOperation::MoveAssign ||
          Expected == UtilityUniquePtrOperation::ConvertingMoveAssign) {
        if (Info->ArgumentIndex >= Call->getNumArgs())
          reject(L, "unique pointer move assignment",
                 "The checked source owner is missing.");
        MoveSourceOwner =
            Expected == UtilityUniquePtrOperation::MoveAssign
                ? std::optional<UtilityUniquePtrRecord>(Info->Owner)
                : Info->SourceOwner;
        if (!MoveSourceOwner)
          reject(L, "unique pointer move assignment",
                 "The checked source owner type is missing.");
        const auto *Source = Call->getArg(Info->ArgumentIndex);
        MoveSourceAddress = snapshot(
            address(lvalue(Source),
                    A.Context.getRecordType(MoveSourceOwner->Record), L),
            L);
      } else if (Expected == UtilityUniquePtrOperation::NullAssign) {
        if (Info->ArgumentIndex >= Call->getNumArgs())
          reject(L, "unique pointer null assignment",
                 "The checked null argument is missing.");
        discard(Call->getArg(Info->ArgumentIndex));
      }
      if (!Receiver)
        Receiver = CaptureReceiver();
      auto Member = [&] {
        return UniquePtrMember(dereference(json::Object(*Receiver), L),
                               Info->Owner);
      };
      if (Expected == UtilityUniquePtrOperation::Swap) {
        if (Info->ArgumentIndex >= Call->getNumArgs())
          reject(L, "unique pointer swap",
                 "The checked second owner is missing.");
        // A member call evaluates its postfix receiver before its argument.
        // Capture both designators before reading either stored pointer so a
        // self-swap and receivers with effects retain the source behavior.
        auto RightAddress = snapshot(
            address(lvalue(Call->getArg(Info->ArgumentIndex)), RecordType, L),
            L);
        auto RightMember = [&] {
          return UniquePtrMember(dereference(json::Object(RightAddress), L),
                                 Info->Owner);
        };
        auto OldLeft = snapshot(Member(), L);
        auto OldRight = snapshot(RightMember(), L);
        assign(Member(), std::move(OldRight), L);
        assign(RightMember(), std::move(OldLeft), L);
        return {};
      }
      if (Expected == UtilityUniquePtrOperation::Release) {
        auto Pointer = snapshot(Member(), L);
        assign(Member(), A.zero(Info->Owner.PointerType, L), L);
        return Pointer;
      }
      if (Expected == UtilityUniquePtrOperation::MoveAssign ||
          Expected == UtilityUniquePtrOperation::ConvertingMoveAssign) {
        if (!MoveSourceAddress || !MoveSourceOwner)
          reject(L, "unique pointer move assignment",
                 "The checked source owner was not captured.");
        auto SourceMember = [&] {
          return UniquePtrMember(
              dereference(json::Object(*MoveSourceAddress), L),
              *MoveSourceOwner);
        };
        auto Pointer = snapshot(SourceMember(), L);
        assign(SourceMember(), A.zero(MoveSourceOwner->PointerType, L), L);
        auto Previous = snapshot(Member(), L);
        assign(Member(),
               cast(std::move(Pointer), type(Info->Owner.PointerType, L), L),
               L);
        deallocateUniquePtr(std::move(Previous), json::Object(*Receiver),
                            Info->Owner, L);
        return dereference(std::move(*Receiver), L);
      }
      if (Expected == UtilityUniquePtrOperation::NullAssign) {
        auto Pointer = snapshot(Member(), L);
        assign(Member(), A.zero(Info->Owner.PointerType, L), L);
        deallocateUniquePtr(std::move(Pointer), json::Object(*Receiver),
                            Info->Owner, L);
        return dereference(std::move(*Receiver), L);
      }
      if (Info->ArgumentIndex >= Call->getNumArgs())
        reject(L, "unique pointer reset",
               "The checked replacement pointer is missing.");
      // C++17 sequences the postfix receiver before the argument. The reset
      // body observes the old pointer only after the argument has completed.
      const auto *Argument = Call->getArg(Info->ArgumentIndex);
      auto Replacement = snapshot(
          isa<CXXDefaultArgExpr>(Argument) ||
                  Argument->getType()->isNullPtrType()
              ? A.zero(Info->Owner.PointerType, L)
              : cast(expression(Argument), type(Info->Owner.PointerType, L), L),
          L);
      auto Pointer = snapshot(Member(), L);
      assign(Member(), std::move(Replacement), L);
      deallocateUniquePtr(std::move(Pointer), json::Object(*Receiver),
                          Info->Owner, L);
      return {};
    }
    case UtilityOperation::MemoryUniquePtrSwap: {
      if (Call->getNumArgs() != 2)
        reject(L, "unique pointer swap",
               "Two checked std::unique_ptr owners are required.");
      const auto Left = approvedUtilityUniquePtrRecord(
          A.S, A.Sources, Call->getArg(0)->getType()->getAsCXXRecordDecl(),
          A.Context);
      const auto Right = approvedUtilityUniquePtrRecord(
          A.S, A.Sources, Call->getArg(1)->getType()->getAsCXXRecordDecl(),
          A.Context);
      if (!Left || !Right ||
          Left->Record->getCanonicalDecl() != Right->Record->getCanonicalDecl())
        reject(L, "unique pointer swap",
               "Matching checked std::unique_ptr owners are required.");
      const auto RecordType = A.Context.getRecordType(Left->Record);
      auto LeftAddress =
          snapshot(address(lvalue(Call->getArg(0)), RecordType, L), L);
      auto RightAddress =
          snapshot(address(lvalue(Call->getArg(1)), RecordType, L), L);
      auto LeftMember = [&] {
        return UniquePtrMember(dereference(json::Object(LeftAddress), L),
                               *Left);
      };
      auto RightMember = [&] {
        return UniquePtrMember(dereference(json::Object(RightAddress), L),
                               *Right);
      };
      auto OldLeft = snapshot(LeftMember(), L);
      auto OldRight = snapshot(RightMember(), L);
      assign(LeftMember(), std::move(OldRight), L);
      assign(RightMember(), std::move(OldLeft), L);
      return {};
    }
    case UtilityOperation::MemoryUniquePtrEqual:
    case UtilityOperation::MemoryUniquePtrNotEqual:
    case UtilityOperation::MemoryUniquePtrLess:
    case UtilityOperation::MemoryUniquePtrGreater:
    case UtilityOperation::MemoryUniquePtrLessEqual:
    case UtilityOperation::MemoryUniquePtrGreaterEqual: {
      if (Call->getNumArgs() != 2)
        reject(L, "unique pointer comparison",
               "Two checked std::unique_ptr comparison operands are required.");
      const auto Left = approvedUtilityUniquePtrRecord(
          A.S, A.Sources, Call->getArg(0)->getType()->getAsCXXRecordDecl(),
          A.Context);
      const auto Right = approvedUtilityUniquePtrRecord(
          A.S, A.Sources, Call->getArg(1)->getType()->getAsCXXRecordDecl(),
          A.Context);
      if ((!Left && !Right) ||
          (Left && Right &&
           (Left->Deleter.Array != Right->Deleter.Array ||
            !A.Context.hasSameUnqualifiedType(Left->ElementType,
                                              Right->ElementType))))
        reject(L, "unique pointer comparison",
               "Compatible checked std::unique_ptr or nullptr operands are "
               "required.");
      const auto &Owner = Left ? *Left : *Right;
      auto ComparisonPointer = Owner.PointerType;
      if (Left && Right)
        ComparisonPointer = Left->ElementType.isConstQualified()
                                ? Left->PointerType
                                : Right->PointerType;
      auto Operand = [&](unsigned Index,
                         const std::optional<UtilityUniquePtrRecord> &Unique) {
        if (!Unique) {
          discard(Call->getArg(Index));
          return A.zero(ComparisonPointer, L);
        }
        return snapshot(
            cast(UniquePtrMember(lvalue(Call->getArg(Index)), *Unique),
                 type(ComparisonPointer, L), L),
            L);
      };
      auto LeftValue = Operand(0, Left);
      auto RightValue = Operand(1, Right);
      const char *Operator = nullptr;
      switch (Operation) {
      case UtilityOperation::MemoryUniquePtrEqual:
        Operator = "==";
        break;
      case UtilityOperation::MemoryUniquePtrNotEqual:
        Operator = "!=";
        break;
      case UtilityOperation::MemoryUniquePtrLess:
        Operator = "<";
        break;
      case UtilityOperation::MemoryUniquePtrGreater:
        Operator = ">";
        break;
      case UtilityOperation::MemoryUniquePtrLessEqual:
        Operator = "<=";
        break;
      case UtilityOperation::MemoryUniquePtrGreaterEqual:
        Operator = ">=";
        break;
      default:
        llvm_unreachable("not a unique pointer comparison");
      }
      return binary(Operator, std::move(LeftValue), std::move(RightValue),
                    "bool", L);
    }
    case UtilityOperation::MemoryAllocatorAddress: {
      const auto *Object = MemberObject();
      if (!Object || Call->getNumArgs() != 1)
        reject(
            L, "allocator address",
            "A checked std::allocator receiver and one object are required.");
      // The postfix object is sequenced before the bound argument. The
      // allocator is stateless, but evaluating its receiver remains observable.
      lvalue(Object);
      return snapshot(
          cast(address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L),
               type(Call->getType(), L), L),
          L);
    }
    case UtilityOperation::MemoryAllocatorAllocate: {
      const auto Info = approvedUtilityAllocatorHeapCall(A.S, A.Sources, Call,
                                                         false, A.Context);
      if (!Info || !Info->Allocate)
        reject(L, "allocator allocation",
               "A checked std::allocator allocation is required.");
      return AllocatorHeap(*Info);
    }
    case UtilityOperation::MemoryAllocatorDeallocate: {
      const auto Info = approvedUtilityAllocatorHeapCall(A.S, A.Sources, Call,
                                                         false, A.Context);
      if (!Info || Info->Allocate)
        reject(L, "allocator deallocation",
               "A checked std::allocator deallocation is required.");
      return AllocatorHeap(*Info);
    }
    case UtilityOperation::MemoryAllocatorMaxSize: {
      const auto *Object = MemberObject();
      const auto Allocator =
          Object ? approvedUtilityAllocatorRecord(
                       A.S, A.Sources, Object->getType()->getAsCXXRecordDecl(),
                       A.Context)
                 : std::optional<UtilityAllocatorRecord>();
      if (!Object || !Allocator || Allocator->ElementType->isVoidType() ||
          Allocator->ElementType->isIncompleteType())
        reject(L, "allocator max_size",
               "A checked complete std::allocator element is required.");
      lvalue(Object);
      return AllocatorMaximum(*Allocator, "allocator max_size");
    }
    case UtilityOperation::MemoryAllocatorConstruct: {
      const auto *Object = MemberObject();
      const auto Info = approvedUtilityAllocatorConstructCall(
          A.S, A.Sources, Call, false, A.Context);
      if (!Object || !Info)
        reject(L, "allocator construct",
               "A checked std::allocator construction is required.");
      lvalue(Object);
      AllocatorConstruct(*Info, 0, 1);
      return {};
    }
    case UtilityOperation::MemoryAllocatorDestroy: {
      const auto *Object = MemberObject();
      if (!Object || Call->getNumArgs() != 1)
        reject(L, "allocator destroy",
               "A checked std::allocator receiver and one pointer are "
               "required.");
      // The receiver is sequenced before the bound argument.
      lvalue(Object);
      auto Pointer = snapshot(expression(Call->getArg(0)), L);
      destroy(dereference(Pointer, L),
              Call->getArg(0)->getType()->getPointeeType(), L);
      return {};
    }
    case UtilityOperation::MemoryAllocatorTraitsConstruct: {
      const auto Info = approvedUtilityAllocatorConstructCall(
          A.S, A.Sources, Call, true, A.Context);
      if (!Info)
        reject(L, "allocator traits construct",
               "A checked allocator_traits construction is required.");
      lvalue(Call->getArg(0));
      AllocatorConstruct(*Info, 1, 2);
      return {};
    }
    case UtilityOperation::MemoryAllocatorTraitsAllocate: {
      const auto Info = approvedUtilityAllocatorHeapCall(A.S, A.Sources, Call,
                                                         true, A.Context);
      if (!Info || !Info->Allocate)
        reject(L, "allocator traits allocation",
               "A checked allocator_traits allocation is required.");
      return AllocatorHeap(*Info);
    }
    case UtilityOperation::MemoryAllocatorTraitsDeallocate: {
      const auto Info = approvedUtilityAllocatorHeapCall(A.S, A.Sources, Call,
                                                         true, A.Context);
      if (!Info || Info->Allocate)
        reject(L, "allocator traits deallocation",
               "A checked allocator_traits deallocation is required.");
      return AllocatorHeap(*Info);
    }
    case UtilityOperation::MemoryAllocatorTraitsDestroy: {
      if (Call->getNumArgs() != 2)
        reject(L, "allocator traits destroy",
               "A checked allocator reference and one pointer are required.");
      lvalue(Call->getArg(0));
      auto Pointer = snapshot(expression(Call->getArg(1)), L);
      destroy(dereference(Pointer, L),
              Call->getArg(1)->getType()->getPointeeType(), L);
      return {};
    }
    case UtilityOperation::MemoryAllocatorTraitsMaxSize: {
      const auto *Method =
          dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
      const auto Traits = approvedUtilityAllocatorTraitsRecord(
          A.S, A.Sources, Method ? Method->getParent() : nullptr, A.Context);
      if (!Traits || Call->getNumArgs() != 1)
        reject(L, "allocator traits max_size",
               "A checked allocator_traits specialization is required.");
      lvalue(Call->getArg(0));
      return AllocatorMaximum(Traits->Allocator,
                              "allocator traits max_size");
    }
    case UtilityOperation::MemoryAllocatorTraitsSelectOnCopy: {
      const auto *Method =
          dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
      const auto Traits = approvedUtilityAllocatorTraitsRecord(
          A.S, A.Sources, Method ? Method->getParent() : nullptr, A.Context);
      if (!Traits || Call->getNumArgs() != 1)
        reject(L, "allocator traits copy selection",
               "A checked allocator_traits specialization is required.");
      // std::allocator has no state, but the bound source expression and its
      // full-expression lifetime remain observable.
      expression(Call->getArg(0));
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(Call->getType(), L);
      Destination.reset();
      if (Place.getString("type") != type(Call->getType(), L))
        reject(L, "allocator traits copy selection",
               "The destination type differs from the allocator result.");
      assign(json::Object(Place), A.zero(Call->getType(), L), L);
      return Place;
    }
    case UtilityOperation::MemoryAllocatorEqual:
    case UtilityOperation::MemoryAllocatorNotEqual:
      if (Call->getNumArgs() != 2)
        reject(L, "allocator comparison",
               "Two checked std::allocator operands are required.");
      lvalue(Call->getArg(0));
      lvalue(Call->getArg(1));
      return boolean(Operation == UtilityOperation::MemoryAllocatorEqual, L);
    case UtilityOperation::MemoryAddressof:
    case UtilityOperation::MemoryPointerTo:
      // Both operations bypass an overloaded operator& and return the address
      // of the already-bound object. Keep the argument as a storage designator
      // so its expression is evaluated exactly once.
      return snapshot(
          cast(address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L),
               type(Call->getType(), L), L),
          L);
    case UtilityOperation::MemoryDestroyAt: {
      auto Pointer = snapshot(expression(Call->getArg(0)), L);
      destroy(dereference(Pointer, L),
              Call->getArg(0)->getType()->getPointeeType(), L);
      return {};
    }
    case UtilityOperation::MemoryDestroy: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      const auto ElementType = Call->getArg(0)->getType()->getPointeeType();
      if (!needsDestruction(ElementType))
        return {};
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto Check = labelName(), Destruct = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Destruct, End, L);
      label(Destruct, L);
      destroy(dereference(Current, L), ElementType, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      label(End, L);
      return {};
    }
    case UtilityOperation::MemoryUninitializedDefaultConstruct: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      const auto ElementType =
          Call->getArg(0)->getType()->getPointeeType().getUnqualifiedType();
      const auto *Constructor = approvedUtilityMemoryDefaultConstructor(
          A.S, A.Sources, ElementType, A.Context);
      if (!Constructor || Constructor->isTrivial())
        return {};
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto Check = labelName(), Construct = labelName(),
                 End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Construct, End, L);
      label(Construct, L);
      constructMemoryDefault(dereference(Current, L), ElementType, Constructor,
                             false, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      label(End, L);
      return {};
    }
    case UtilityOperation::MemoryDestroyN:
    case UtilityOperation::MemoryUninitializedDefaultConstructN: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      const auto ElementType =
          Call->getArg(0)->getType()->getPointeeType().getUnqualifiedType();
      const auto *Constructor =
          Operation == UtilityOperation::MemoryUninitializedDefaultConstructN
              ? approvedUtilityMemoryDefaultConstructor(A.S, A.Sources,
                                                        ElementType, A.Context)
              : nullptr;
      auto CountType = Call->getArg(1)->getType();
      if (const auto *Enumeration = CountType->getAs<EnumType>())
        CountType = Enumeration->getDecl()->getPromotionType();
      else if (A.Context.isPromotableIntegerType(CountType))
        CountType = A.Context.getPromotedIntegerType(CountType);
      const auto CountTypeName = type(CountType, L);
      auto Remaining =
          snapshot(cast(expression(Call->getArg(1)), CountTypeName, L), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto Check = labelName(), Advance = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary(">", Remaining, quantity(0, CountTypeName, L), "bool", L),
             Advance, End, L);
      label(Advance, L);
      if (Operation == UtilityOperation::MemoryDestroyN)
        destroy(dereference(Current, L), ElementType, L);
      else if (Constructor && !Constructor->isTrivial())
        constructMemoryDefault(dereference(Current, L), ElementType,
                               Constructor, false, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      assign(Remaining,
             binary("-", Remaining, one(CountTypeName, L), CountTypeName, L),
             L);
      jump(Check, L);
      label(End, L);
      return Current;
    }
    case UtilityOperation::MemoryUninitializedValueConstruct:
    case UtilityOperation::MemoryUninitializedValueConstructN: {
      const bool Counted =
          Operation == UtilityOperation::MemoryUninitializedValueConstructN;
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto BoundaryType = Call->getArg(1)->getType();
      if (Counted) {
        if (const auto *Enumeration = BoundaryType->getAs<EnumType>())
          BoundaryType = Enumeration->getDecl()->getPromotionType();
        else if (A.Context.isPromotableIntegerType(BoundaryType))
          BoundaryType = A.Context.getPromotedIntegerType(BoundaryType);
      }
      const auto BoundaryTypeName = type(BoundaryType, L);
      auto Boundary = snapshot(
          Counted ? cast(expression(Call->getArg(1)), BoundaryTypeName, L)
                  : expression(Call->getArg(1)),
          L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto ElementType =
          Call->getArg(0)->getType()->getPointeeType().getUnqualifiedType();
      const auto *Constructor = approvedUtilityMemoryDefaultConstructor(
          A.S, A.Sources, ElementType, A.Context);
      const auto Check = labelName(), Construct = labelName(),
                 End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(Counted ? binary(">", Boundary, quantity(0, BoundaryTypeName, L),
                              "bool", L)
                     : binary("!=", Current, Boundary, "bool", L),
             Construct, End, L);
      label(Construct, L);
      constructMemoryDefault(dereference(Current, L), ElementType, Constructor,
                             true, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      if (Counted)
        assign(Boundary,
               binary("-", Boundary, one(BoundaryTypeName, L), BoundaryTypeName,
                      L),
               L);
      jump(Check, L);
      label(End, L);
      return Counted ? Current : Expression{};
    }
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
    case UtilityOperation::NumericIota: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Value = snapshot(expression(Call->getArg(2)), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto ValueType = type(Call->getArg(2)->getType(), L);
      const auto ElementType =
          type(Call->getArg(0)->getType()->getPointeeType(), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto Check = labelName(), Store = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Store, End, L);
      label(Store, L);
      assign(dereference(Current, L), cast(Value, ElementType, L), L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      assign(Value, binary("+", Value, one(ValueType, L), ValueType, L), L);
      jump(Check, L);
      label(End, L);
      return {};
    }
    case UtilityOperation::NumericAccumulate:
    case UtilityOperation::NumericInnerProduct:
    case UtilityOperation::NumericReduce:
    case UtilityOperation::NumericTransformReduce: {
      const bool TransformReduce =
          Operation == UtilityOperation::NumericTransformReduce;
      const bool UnaryTransformReduce =
          TransformReduce && Call->getNumArgs() == 5;
      const bool Inner = Operation == UtilityOperation::NumericInnerProduct ||
                         (TransformReduce && !UnaryTransformReduce);
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Second;
      if (Inner)
        Second = snapshot(expression(Call->getArg(2)), L);
      const unsigned InitialIndex = Inner ? 3 : 2;
      const bool HasInitial = InitialIndex < Call->getNumArgs();
      const auto FirstType = type(Call->getArg(0)->getType(), L);
      const auto SecondType =
          Inner ? type(Call->getArg(2)->getType(), L) : std::string();
      const auto ResultQualType =
          HasInitial ? Call->getArg(InitialIndex)->getType() : Call->getType();
      const auto ResultType = type(ResultQualType, L);
      auto Result = temporary(ResultType, L);
      assign(Result,
             HasInitial ? expression(Call->getArg(InitialIndex))
                        : A.zero(ResultQualType, L),
             L);
      std::optional<Expression> ReductionCallback, TransformCallback;
      std::optional<QualType> ReductionCallbackType, TransformCallbackType;
      if ((Operation == UtilityOperation::NumericAccumulate ||
           Operation == UtilityOperation::NumericReduce) &&
          Call->getNumArgs() == 4) {
        ReductionCallbackType = Call->getArg(3)->getType();
        ReductionCallback = snapshot(expression(Call->getArg(3)), L);
      } else if (Operation == UtilityOperation::NumericInnerProduct &&
                 Call->getNumArgs() == 6) {
        ReductionCallbackType = Call->getArg(4)->getType();
        TransformCallbackType = Call->getArg(5)->getType();
        ReductionCallback = snapshot(expression(Call->getArg(4)), L);
        TransformCallback = snapshot(expression(Call->getArg(5)), L);
      } else if (TransformReduce && Call->getNumArgs() >= 5) {
        const unsigned ReductionIndex = UnaryTransformReduce ? 3 : 4;
        const unsigned TransformIndex = UnaryTransformReduce ? 4 : 5;
        ReductionCallbackType = Call->getArg(ReductionIndex)->getType();
        TransformCallbackType = Call->getArg(TransformIndex)->getType();
        ReductionCallback =
            snapshot(expression(Call->getArg(ReductionIndex)), L);
        TransformCallback =
            snapshot(expression(Call->getArg(TransformIndex)), L);
      }
      auto DefaultTermQualType =
          Call->getArg(0)->getType()->getPointeeType().getUnqualifiedType();
      if (Second && !TransformCallback) {
        auto Common = utilityScalarComparisonType(
            A.Context, DefaultTermQualType,
            Call->getArg(2)->getType()->getPointeeType(), false);
        if (!Common)
          reject(L, "numeric product",
                 "The input elements have no arithmetic common type.");
        DefaultTermQualType = *Common;
      }
      auto DefaultSumQualType = utilityScalarComparisonType(
          A.Context, ResultQualType, DefaultTermQualType, false);
      if (!DefaultSumQualType)
        reject(L, "numeric reduction",
               "The accumulator and term have no arithmetic common type.");
      const auto DefaultTermType = type(DefaultTermQualType, L);
      const auto DefaultSumType = type(*DefaultSumQualType, L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto Check = labelName(), Add = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", First, Last, "bool", L), Add, End, L);
      label(Add, L);
      auto Term = dereference(First, L);
      if (TransformCallback) {
        json::Array Arguments;
        Arguments.push_back(std::move(Term));
        if (Second)
          Arguments.push_back(dereference(*Second, L));
        Term = cast(emitAlgorithmCallback(json::Object(*TransformCallback),
                                          *TransformCallbackType,
                                          std::move(Arguments), L),
                    ResultType, L);
      } else if (Second) {
        Term = binary("*", cast(std::move(Term), DefaultTermType, L),
                      cast(dereference(*Second, L), DefaultTermType, L),
                      DefaultTermType, L);
      }
      if (ReductionCallback) {
        json::Array Arguments;
        Arguments.push_back(json::Object(Result));
        Arguments.push_back(std::move(Term));
        assign(Result,
               cast(emitAlgorithmCallback(json::Object(*ReductionCallback),
                                          *ReductionCallbackType,
                                          std::move(Arguments), L),
                    ResultType, L),
               L);
      } else {
        assign(Result,
               cast(binary("+", cast(json::Object(Result), DefaultSumType, L),
                           cast(std::move(Term), DefaultSumType, L),
                           DefaultSumType, L),
                    ResultType, L),
               L);
      }
      assign(First,
             binary("+", First, quantity(1, DifferenceType, L), FirstType, L),
             L);
      if (Second)
        assign(
            *Second,
            binary("+", *Second, quantity(1, DifferenceType, L), SecondType, L),
            L);
      jump(Check, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::NumericPartialSum:
    case UtilityOperation::NumericAdjacentDifference:
    case UtilityOperation::NumericInclusiveScan: {
      const bool Adjacent =
          Operation == UtilityOperation::NumericAdjacentDifference;
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Output = snapshot(expression(Call->getArg(2)), L);
      std::optional<Expression> Callback;
      std::optional<QualType> CallbackType;
      if (Call->getNumArgs() >= 4) {
        CallbackType = Call->getArg(3)->getType();
        Callback = snapshot(expression(Call->getArg(3)), L);
      }
      const auto FirstType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(2)->getType(), L);
      const auto ElementType =
          type(Call->getArg(0)->getType()->getPointeeType(), L);
      const auto OutputElementType =
          type(Call->getArg(2)->getType()->getPointeeType(), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      auto Previous = temporary(ElementType, L);
      auto CurrentValue = temporary(ElementType, L);
      if (Operation == UtilityOperation::NumericInclusiveScan &&
          Call->getNumArgs() == 5) {
        auto Value = snapshot(expression(Call->getArg(4)), L);
        const auto ValueType = type(Call->getArg(4)->getType(), L);
        const auto Check = labelName(), Store = labelName(), End = labelName();
        jump(Check, L);
        label(Check, L);
        branch(binary("!=", First, Last, "bool", L), Store, End, L);
        label(Store, L);
        assign(CurrentValue, dereference(First, L), L);
        json::Array Arguments;
        Arguments.push_back(json::Object(Value));
        Arguments.push_back(json::Object(CurrentValue));
        assign(
            Value,
            cast(emitAlgorithmCallback(json::Object(*Callback), *CallbackType,
                                       std::move(Arguments), L),
                 ValueType, L),
            L);
        assign(dereference(Output, L),
               cast(json::Object(Value), OutputElementType, L), L);
        assign(First,
               binary("+", First, quantity(1, DifferenceType, L), FirstType, L),
               L);
        assign(
            Output,
            binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
            L);
        jump(Check, L);
        label(End, L);
        return Output;
      }
      const auto CheckFirst = labelName(), StoreFirst = labelName();
      const auto CheckNext = labelName(), StoreNext = labelName();
      const auto End = labelName();
      jump(CheckFirst, L);
      label(CheckFirst, L);
      branch(binary("!=", First, Last, "bool", L), StoreFirst, End, L);
      label(StoreFirst, L);
      assign(Previous, dereference(First, L), L);
      assign(dereference(Output, L),
             cast(json::Object(Previous), OutputElementType, L), L);
      assign(First,
             binary("+", First, quantity(1, DifferenceType, L), FirstType, L),
             L);
      assign(Output,
             binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
             L);
      jump(CheckNext, L);
      label(CheckNext, L);
      branch(binary("!=", First, Last, "bool", L), StoreNext, End, L);
      label(StoreNext, L);
      assign(CurrentValue, dereference(First, L), L);
      auto Combine = [&](Expression Left, Expression Right,
                         llvm::StringRef DefaultOperator) {
        if (!Callback)
          return binary(DefaultOperator, std::move(Left), std::move(Right),
                        ElementType, L);
        json::Array Arguments;
        Arguments.push_back(std::move(Left));
        Arguments.push_back(std::move(Right));
        return cast(emitAlgorithmCallback(json::Object(*Callback),
                                          *CallbackType, std::move(Arguments),
                                          L),
                    ElementType, L);
      };
      if (Adjacent) {
        assign(dereference(Output, L),
               cast(Combine(CurrentValue, Previous, "-"), OutputElementType, L),
               L);
        assign(Previous, CurrentValue, L);
      } else {
        assign(Previous, Combine(Previous, CurrentValue, "+"), L);
        assign(dereference(Output, L),
               cast(json::Object(Previous), OutputElementType, L), L);
      }
      assign(First,
             binary("+", First, quantity(1, DifferenceType, L), FirstType, L),
             L);
      assign(Output,
             binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
             L);
      jump(CheckNext, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::NumericExclusiveScan: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Output = snapshot(expression(Call->getArg(2)), L);
      auto Value = snapshot(expression(Call->getArg(3)), L);
      std::optional<Expression> Callback;
      std::optional<QualType> CallbackType;
      if (Call->getNumArgs() == 5) {
        CallbackType = Call->getArg(4)->getType();
        Callback = snapshot(expression(Call->getArg(4)), L);
      }
      const auto FirstType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(2)->getType(), L);
      const auto ValueQualType = Call->getArg(3)->getType();
      const auto ElementQualType = Call->getArg(0)->getType()->getPointeeType();
      const auto ValueType = type(ValueQualType, L);
      const auto ElementType = type(ElementQualType, L);
      auto Common = utilityScalarComparisonType(A.Context, ValueQualType,
                                                ElementQualType, false);
      if (!Common)
        reject(L, "exclusive scan",
               "The accumulator and input have no arithmetic common type.");
      const auto DefaultSumType = type(*Common, L);
      const auto OutputElementType =
          type(Call->getArg(2)->getType()->getPointeeType(), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      auto InputValue = temporary(ElementType, L);
      auto Next = temporary(ValueType, L);
      const auto Check = labelName(), Store = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", First, Last, "bool", L), Store, End, L);
      label(Store, L);
      assign(InputValue, dereference(First, L), L);
      if (Callback) {
        json::Array Arguments;
        Arguments.push_back(json::Object(Value));
        Arguments.push_back(json::Object(InputValue));
        assign(
            Next,
            cast(emitAlgorithmCallback(json::Object(*Callback), *CallbackType,
                                       std::move(Arguments), L),
                 ValueType, L),
            L);
      } else {
        assign(Next,
               cast(binary("+", cast(json::Object(Value), DefaultSumType, L),
                           cast(json::Object(InputValue), DefaultSumType, L),
                           DefaultSumType, L),
                    ValueType, L),
               L);
      }
      assign(dereference(Output, L),
             cast(json::Object(Value), OutputElementType, L), L);
      assign(Value, Next, L);
      assign(First,
             binary("+", First, quantity(1, DifferenceType, L), FirstType, L),
             L);
      assign(Output,
             binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
             L);
      jump(Check, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::NumericTransformInclusiveScan:
    case UtilityOperation::NumericTransformExclusiveScan: {
      const bool Inclusive =
          Operation == UtilityOperation::NumericTransformInclusiveScan;
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Output = snapshot(expression(Call->getArg(2)), L);
      std::optional<Expression> Accumulator;
      std::optional<Expression> BinaryCallback, UnaryCallback;
      std::optional<QualType> BinaryCallbackType, UnaryCallbackType;
      if (Inclusive) {
        BinaryCallbackType = Call->getArg(3)->getType();
        UnaryCallbackType = Call->getArg(4)->getType();
        BinaryCallback = snapshot(expression(Call->getArg(3)), L);
        UnaryCallback = snapshot(expression(Call->getArg(4)), L);
        if (Call->getNumArgs() == 6)
          Accumulator = snapshot(expression(Call->getArg(5)), L);
      } else {
        Accumulator = snapshot(expression(Call->getArg(3)), L);
        BinaryCallbackType = Call->getArg(4)->getType();
        UnaryCallbackType = Call->getArg(5)->getType();
        BinaryCallback = snapshot(expression(Call->getArg(4)), L);
        UnaryCallback = snapshot(expression(Call->getArg(5)), L);
      }
      const auto FirstType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(2)->getType(), L);
      const auto ElementType =
          type(Call->getArg(0)->getType()->getPointeeType(), L);
      const auto AccumulatorType =
          Accumulator ? type(Call->getArg(Inclusive ? 5 : 3)->getType(), L)
                      : ElementType;
      const auto OutputElementType =
          type(Call->getArg(2)->getType()->getPointeeType(), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      if (!Accumulator)
        Accumulator = temporary(ElementType, L);
      auto InputValue = temporary(ElementType, L);
      auto Transformed = temporary(ElementType, L);
      auto Next = temporary(AccumulatorType, L);
      auto ApplyUnary = [&] {
        json::Array Arguments;
        Arguments.push_back(json::Object(InputValue));
        return cast(emitAlgorithmCallback(json::Object(*UnaryCallback),
                                          *UnaryCallbackType,
                                          std::move(Arguments), L),
                    ElementType, L);
      };
      auto ApplyBinary = [&] {
        json::Array Arguments;
        Arguments.push_back(json::Object(*Accumulator));
        Arguments.push_back(json::Object(Transformed));
        return cast(emitAlgorithmCallback(json::Object(*BinaryCallback),
                                          *BinaryCallbackType,
                                          std::move(Arguments), L),
                    AccumulatorType, L);
      };
      auto Advance = [&] {
        assign(First,
               binary("+", First, quantity(1, DifferenceType, L), FirstType, L),
               L);
        assign(
            Output,
            binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
            L);
      };
      const auto LoopCheck = labelName(), LoopBody = labelName();
      const auto End = labelName();
      if (Inclusive && Call->getNumArgs() == 5) {
        const auto FirstCheck = labelName(), FirstBody = labelName();
        jump(FirstCheck, L);
        label(FirstCheck, L);
        branch(binary("!=", First, Last, "bool", L), FirstBody, End, L);
        label(FirstBody, L);
        assign(InputValue, dereference(First, L), L);
        assign(*Accumulator, ApplyUnary(), L);
        assign(dereference(Output, L),
               cast(json::Object(*Accumulator), OutputElementType, L), L);
        Advance();
        jump(LoopCheck, L);
      } else {
        jump(LoopCheck, L);
      }
      label(LoopCheck, L);
      branch(binary("!=", First, Last, "bool", L), LoopBody, End, L);
      label(LoopBody, L);
      assign(InputValue, dereference(First, L), L);
      assign(Transformed, ApplyUnary(), L);
      assign(Next, ApplyBinary(), L);
      if (Inclusive) {
        assign(*Accumulator, Next, L);
        assign(dereference(Output, L),
               cast(json::Object(*Accumulator), OutputElementType, L), L);
      } else {
        assign(dereference(Output, L),
               cast(json::Object(*Accumulator), OutputElementType, L), L);
        assign(*Accumulator, Next, L);
      }
      Advance();
      jump(LoopCheck, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::NumericGcd:
    case UtilityOperation::NumericLcm: {
      const bool Lcm = Operation == UtilityOperation::NumericLcm;
      const auto ResultQualType = Call->getType().getUnqualifiedType();
      const auto UnsignedQualType =
          A.Context.getCorrespondingUnsignedType(ResultQualType);
      const auto ResultType = type(ResultQualType, L);
      const auto UnsignedType = type(UnsignedQualType, L);
      auto ComputationQualType = UnsignedQualType;
      if (A.Context.isPromotableIntegerType(ComputationQualType))
        ComputationQualType =
            A.Context.getPromotedIntegerType(ComputationQualType);
      const auto ComputationType = type(ComputationQualType, L);
      auto UnsignedBinary = [&](llvm::StringRef Operator, Expression Left,
                                Expression Right) {
        auto Value = binary(Operator, cast(std::move(Left), ComputationType, L),
                            cast(std::move(Right), ComputationType, L),
                            ComputationType, L);
        return cast(std::move(Value), UnsignedType, L);
      };
      auto Magnitude = [&](unsigned Index) {
        const auto SourceQualType =
            Call->getArg(Index)->getType().getUnqualifiedType();
        auto Source = snapshot(expression(Call->getArg(Index)), L);
        auto Result = temporary(UnsignedType, L);
        if (!SourceQualType->isSignedIntegerType()) {
          assign(Result, cast(std::move(Source), UnsignedType, L), L);
          return Result;
        }
        const auto Negative = labelName(), Nonnegative = labelName();
        const auto End = labelName();
        branch(binary("<", Source, A.zero(SourceQualType, L), "bool", L),
               Negative, Nonnegative, L);
        label(Negative, L);
        assign(Result,
               UnsignedBinary("-", A.zero(UnsignedQualType, L),
                              cast(Source, UnsignedType, L)),
               L);
        jump(End, L);
        label(Nonnegative, L);
        assign(Result, cast(std::move(Source), UnsignedType, L), L);
        jump(End, L);
        label(End, L);
        return Result;
      };

      auto Left = Magnitude(0);
      auto Right = Magnitude(1);
      auto Result = temporary(ResultType, L);
      const auto Zero = A.zero(UnsignedQualType, L);
      const auto CheckRightZero = labelName(), ReturnZero = labelName();
      const auto Prepare = labelName(), Check = labelName();
      const auto Step = labelName(), Finish = labelName(), End = labelName();
      if (Lcm) {
        branch(binary("==", Left, Zero, "bool", L), ReturnZero, CheckRightZero,
               L);
        label(CheckRightZero, L);
        branch(binary("==", Right, Zero, "bool", L), ReturnZero, Prepare, L);
        label(ReturnZero, L);
        assign(Result, A.zero(ResultQualType, L), L);
        jump(End, L);
        label(Prepare, L);
      }
      auto GcdLeft = snapshot(Left, L);
      auto GcdRight = snapshot(Right, L);
      auto Remainder = temporary(UnsignedType, L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", GcdRight, Zero, "bool", L), Step, Finish, L);
      label(Step, L);
      assign(Remainder, UnsignedBinary("%", GcdLeft, GcdRight), L);
      assign(GcdLeft, GcdRight, L);
      assign(GcdRight, Remainder, L);
      jump(Check, L);
      label(Finish, L);
      if (Lcm) {
        auto Quotient = UnsignedBinary("/", Left, GcdLeft);
        assign(Result,
               cast(UnsignedBinary("*", std::move(Quotient), Right), ResultType,
                    L),
               L);
      } else {
        assign(Result, cast(GcdLeft, ResultType, L), L);
      }
      jump(End, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::AlgorithmFind: {
      // Function arguments are all bound before the algorithm body. Choose
      // the permitted left-to-right C++17 order, retaining the value referent
      // so an lvalue argument keeps its ordinary reference identity.
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto ValueAddress = snapshot(
          address(lvalue(Call->getArg(2)), Call->getArg(2)->getType(), L), L);
      auto Common = utilityScalarComparisonType(
          A.Context, Call->getArg(0)->getType()->getPointeeType(),
          Call->getArg(2)->getType(), false);
      if (!Common)
        reject(L, "algorithm find",
               "The range element and value have no equality common type.");
      const auto ComparisonType = type(*Common, L);
      const auto Check = labelName(), Compare = labelName();
      const auto Increment = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Compare, End, L);
      label(Compare, L);
      branch(binary("==", cast(dereference(Current, L), ComparisonType, L),
                    cast(dereference(ValueAddress, L), ComparisonType, L),
                    "bool", L),
             End, Increment, L);
      label(Increment, L);
      assign(Current,
             binary("+", Current,
                    quantity(1, type(A.Context.getPointerDiffType(), L), L),
                    type(Call->getType(), L), L),
             L);
      jump(Check, L);
      label(End, L);
      return Current;
    }
    case UtilityOperation::AlgorithmCount: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto ValueAddress = snapshot(
          address(lvalue(Call->getArg(2)), Call->getArg(2)->getType(), L), L);
      auto Common = utilityScalarComparisonType(
          A.Context, Call->getArg(0)->getType()->getPointeeType(),
          Call->getArg(2)->getType(), false);
      if (!Common)
        reject(L, "algorithm count",
               "The range element and value have no equality common type.");
      const auto ComparisonType = type(*Common, L);
      const auto ResultType = type(Call->getType(), L);
      auto Result = temporary(ResultType, L);
      assign(Result, quantity(0, ResultType, L), L);
      const auto Check = labelName(), Compare = labelName();
      const auto Match = labelName(), Next = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Compare, End, L);
      label(Compare, L);
      branch(binary("==", cast(dereference(Current, L), ComparisonType, L),
                    cast(dereference(ValueAddress, L), ComparisonType, L),
                    "bool", L),
             Match, Next, L);
      label(Match, L);
      assign(Result, binary("+", Result, one(ResultType, L), ResultType, L),
             L);
      jump(Next, L);
      label(Next, L);
      assign(Current,
             binary("+", Current,
                    quantity(1, type(A.Context.getPointerDiffType(), L), L),
                    type(Call->getArg(0)->getType(), L), L),
             L);
      jump(Check, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::AlgorithmEqual: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Second = snapshot(expression(Call->getArg(2)), L);
      std::optional<Expression> SecondLast;
      std::optional<unsigned> PredicateIndex;
      if (Call->getNumArgs() == 4 &&
          Call->getArg(3)->getType()->isFunctionPointerType())
        PredicateIndex = 3;
      else if (Call->getNumArgs() == 5)
        PredicateIndex = 4;
      if ((Call->getNumArgs() == 4 && !PredicateIndex) ||
          Call->getNumArgs() == 5)
        SecondLast = snapshot(expression(Call->getArg(3)), L);
      std::optional<Expression> Predicate;
      if (PredicateIndex)
        Predicate = snapshot(expression(Call->getArg(*PredicateIndex)), L);
      auto Result = temporary("bool", L);
      const auto Check = labelName(), CheckSecond = labelName();
      const auto Compare = labelName(), Next = labelName();
      const auto False = labelName(), FirstDone = labelName();
      const auto End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", First, Last, "bool", L),
             SecondLast ? CheckSecond : Compare, FirstDone, L);
      if (SecondLast) {
        label(CheckSecond, L);
        branch(binary("!=", Second, *SecondLast, "bool", L), Compare, False,
               L);
      }
      label(Compare, L);
      branch(Predicate
                 ? emitBinaryPredicate(json::Object(*Predicate),
                                       Call->getArg(*PredicateIndex)->getType(),
                                       dereference(json::Object(First), L),
                                       dereference(json::Object(Second), L), L)
                 : AlgorithmEqual(dereference(First, L), 0,
                                  dereference(Second, L), 2),
             Next, False, L);
      label(Next, L);
      assign(First,
             binary("+", First,
                    quantity(1, type(A.Context.getPointerDiffType(), L), L),
                    type(Call->getArg(0)->getType(), L), L),
             L);
      assign(Second,
             binary("+", Second,
                    quantity(1, type(A.Context.getPointerDiffType(), L), L),
                    type(Call->getArg(2)->getType(), L), L),
             L);
      jump(Check, L);
      label(False, L);
      assign(Result, boolean(false, L), L);
      jump(End, L);
      label(FirstDone, L);
      assign(Result,
             SecondLast
                 ? binary("==", Second, *SecondLast, "bool", L)
                 : boolean(true, L),
             L);
      jump(End, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::AlgorithmCopy:
    case UtilityOperation::AlgorithmMove:
    case UtilityOperation::MemoryUninitializedCopy:
    case UtilityOperation::MemoryUninitializedMove:
    case UtilityOperation::AlgorithmCopyBackward:
    case UtilityOperation::AlgorithmMoveBackward: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Output = snapshot(expression(Call->getArg(2)), L);
      const bool Backward =
          Operation == UtilityOperation::AlgorithmCopyBackward ||
          Operation == UtilityOperation::AlgorithmMoveBackward;
      const auto Check = labelName(), Transfer = labelName(), End = labelName();
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(2)->getType(), L);
      const bool MemoryConstruction =
          Operation == UtilityOperation::MemoryUninitializedCopy ||
          Operation == UtilityOperation::MemoryUninitializedMove;
      const auto *Constructor =
          MemoryConstruction ? approvedUtilityMemorySourceConstructor(
                                   A.S, A.Sources, Call, Operation, A.Context)
                             : nullptr;
      const auto ElementType = MemoryConstruction ? Call->getArg(2)
                                                        ->getType()
                                                        ->getPointeeType()
                                                        .getUnqualifiedType()
                                                  : QualType();
      const auto OutputElementType =
          MemoryConstruction
              ? std::string()
              : type(Call->getArg(2)->getType()->getPointeeType(), L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Transfer, End, L);
      label(Transfer, L);
      if (Backward) {
        assign(Last,
               binary("-", Last, quantity(1, DifferenceType, L), InputType, L),
               L);
        assign(
            Output,
            binary("-", Output, quantity(1, DifferenceType, L), OutputType, L),
            L);
        assign(dereference(Output, L),
               cast(dereference(Last, L), OutputElementType, L), L);
      } else {
        if (MemoryConstruction && Constructor)
          constructMemorySource(dereference(Output, L), ElementType,
                                Constructor, json::Object(Current), L);
        else if (MemoryConstruction)
          assign(dereference(Output, L), dereference(Current, L), L);
        else
          assign(dereference(Output, L),
                 cast(dereference(Current, L), OutputElementType, L), L);
        assign(
            Current,
            binary("+", Current, quantity(1, DifferenceType, L), InputType, L),
            L);
        assign(
            Output,
            binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
            L);
      }
      jump(Check, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::AlgorithmFill:
    case UtilityOperation::AlgorithmFillN:
    case UtilityOperation::MemoryUninitializedFill:
    case UtilityOperation::MemoryUninitializedFillN: {
      const bool Counted =
          Operation == UtilityOperation::AlgorithmFillN ||
          Operation == UtilityOperation::MemoryUninitializedFillN;
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto BoundaryType = Call->getArg(1)->getType();
      if (Counted) {
        if (const auto *Enumeration = BoundaryType->getAs<EnumType>())
          BoundaryType = Enumeration->getDecl()->getPromotionType();
        else if (A.Context.isPromotableIntegerType(BoundaryType))
          BoundaryType = A.Context.getPromotedIntegerType(BoundaryType);
      }
      const auto BoundaryTypeName = type(BoundaryType, L);
      auto Boundary = snapshot(
          Counted ? cast(expression(Call->getArg(1)), BoundaryTypeName, L)
                  : expression(Call->getArg(1)),
          L);
      auto ValueAddress = snapshot(
          address(lvalue(Call->getArg(2)), Call->getArg(2)->getType(), L), L);
      const auto Check = labelName(), Store = labelName(), End = labelName();
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto OutputElementType =
          type(Call->getArg(0)->getType()->getPointeeType(), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const bool MemoryConstruction =
          Operation == UtilityOperation::MemoryUninitializedFill ||
          Operation == UtilityOperation::MemoryUninitializedFillN;
      const auto *Constructor =
          MemoryConstruction ? approvedUtilityMemorySourceConstructor(
                                   A.S, A.Sources, Call, Operation, A.Context)
                             : nullptr;
      const auto ElementType = MemoryConstruction ? Call->getArg(0)
                                                        ->getType()
                                                        ->getPointeeType()
                                                        .getUnqualifiedType()
                                                  : QualType();
      jump(Check, L);
      label(Check, L);
      branch(Counted ? binary(">", Boundary, quantity(0, BoundaryTypeName, L),
                              "bool", L)
                     : binary("!=", Current, Boundary, "bool", L),
             Store, End, L);
      label(Store, L);
      if (MemoryConstruction && Constructor)
        constructMemorySource(dereference(Current, L), ElementType, Constructor,
                              json::Object(ValueAddress), L);
      else
        assign(dereference(Current, L),
               cast(dereference(ValueAddress, L), OutputElementType, L), L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      if (Counted) {
        assign(Boundary,
               binary("-", Boundary, one(BoundaryTypeName, L), BoundaryTypeName,
                      L),
               L);
      }
      jump(Check, L);
      label(End, L);
      return Counted ? std::move(Current) : Expression();
    }
    case UtilityOperation::AlgorithmSwapRanges: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Second = snapshot(expression(Call->getArg(2)), L);
      const auto Check = labelName(), Swap = labelName(), End = labelName();
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto FirstType = type(Call->getArg(0)->getType(), L);
      const auto SecondType = type(Call->getArg(2)->getType(), L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", First, Last, "bool", L), Swap, End, L);
      label(Swap, L);
      auto FirstValue = snapshot(dereference(First, L), L);
      auto SecondValue = snapshot(dereference(Second, L), L);
      assign(dereference(First, L), std::move(SecondValue), L);
      assign(dereference(Second, L), std::move(FirstValue), L);
      assign(First,
             binary("+", First, quantity(1, DifferenceType, L), FirstType, L),
             L);
      assign(Second,
             binary("+", Second, quantity(1, DifferenceType, L), SecondType, L),
             L);
      jump(Check, L);
      label(End, L);
      return Second;
    }
    case UtilityOperation::AlgorithmReverse: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      const auto Check = labelName(), Decrement = labelName();
      const auto Swap = labelName(), End = labelName();
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", First, Last, "bool", L), Decrement, End, L);
      label(Decrement, L);
      assign(Last,
             binary("-", Last, quantity(1, DifferenceType, L), PointerType, L),
             L);
      branch(binary("!=", First, Last, "bool", L), Swap, End, L);
      label(Swap, L);
      auto FirstValue = snapshot(dereference(First, L), L);
      auto LastValue = snapshot(dereference(Last, L), L);
      assign(dereference(First, L), std::move(LastValue), L);
      assign(dereference(Last, L), std::move(FirstValue), L);
      assign(First,
             binary("+", First, quantity(1, DifferenceType, L), PointerType, L),
             L);
      jump(Check, L);
      label(End, L);
      return {};
    }
    case UtilityOperation::AlgorithmReverseCopy: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Output = snapshot(expression(Call->getArg(2)), L);
      const auto Check = labelName(), Transfer = labelName(), End = labelName();
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(2)->getType(), L);
      const auto OutputElementType =
          type(Call->getArg(2)->getType()->getPointeeType(), L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", First, Last, "bool", L), Transfer, End, L);
      label(Transfer, L);
      assign(Last,
             binary("-", Last, quantity(1, DifferenceType, L), InputType, L),
             L);
      assign(dereference(Output, L),
             cast(dereference(Last, L), OutputElementType, L), L);
      assign(Output,
             binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
             L);
      jump(Check, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::AlgorithmMinElement:
    case UtilityOperation::AlgorithmMaxElement: {
      const bool Minimum = Operation == UtilityOperation::AlgorithmMinElement;
      auto Candidate = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 3)
        Comparator = snapshot(expression(Call->getArg(2)), L);
      auto Current = snapshot(json::Object(Candidate), L);
      const auto Initialize = labelName(), Check = labelName();
      const auto Compare = labelName(), Select = labelName();
      const auto Next = labelName(), End = labelName();
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      branch(binary("!=", Candidate, Last, "bool", L), Initialize, End, L);
      label(Initialize, L);
      assign(Current,
             binary("+", Candidate, quantity(1, DifferenceType, L), PointerType,
                    L),
             L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Compare, End, L);
      label(Compare, L);
      branch(Comparator
                 ? emitBinaryPredicate(
                       json::Object(*Comparator), Call->getArg(2)->getType(),
                       Minimum ? dereference(json::Object(Current), L)
                               : dereference(json::Object(Candidate), L),
                       Minimum ? dereference(json::Object(Candidate), L)
                               : dereference(json::Object(Current), L),
                       L)
             : Minimum ? binary("<", dereference(Current, L),
                                dereference(Candidate, L), "bool", L)
                       : binary("<", dereference(Candidate, L),
                                dereference(Current, L), "bool", L),
             Select, Next, L);
      label(Select, L);
      assign(Candidate, json::Object(Current), L);
      jump(Next, L);
      label(Next, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      label(End, L);
      return Candidate;
    }
    case UtilityOperation::AlgorithmLowerBound:
    case UtilityOperation::AlgorithmUpperBound:
    case UtilityOperation::AlgorithmBinarySearch: {
      const bool Upper = Operation == UtilityOperation::AlgorithmUpperBound;
      const bool Search = Operation == UtilityOperation::AlgorithmBinarySearch;
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto ValueAddress = snapshot(
          address(lvalue(Call->getArg(2)), Call->getArg(2)->getType(), L), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 4)
        Comparator = snapshot(expression(Call->getArg(3)), L);
      std::optional<std::string> DefaultComparisonType;
      if (!Comparator) {
        auto Common = utilityScalarComparisonType(
            A.Context, Call->getArg(0)->getType()->getPointeeType(),
            Call->getArg(2)->getType(), true);
        if (!Common)
          reject(L, "ordered algorithm query",
                 "The range element and value have no ordered common type.");
        DefaultComparisonType = type(*Common, L);
      }
      auto Less = [&](Expression Left, Expression Right) {
        if (Comparator)
          return emitBinaryPredicate(json::Object(*Comparator),
                                     Call->getArg(3)->getType(),
                                     std::move(Left), std::move(Right), L);
        return binary("<", cast(std::move(Left), *DefaultComparisonType, L),
                      cast(std::move(Right), *DefaultComparisonType, L), "bool",
                      L);
      };
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto Length = temporary(DifferenceType, L);
      auto Half = temporary(DifferenceType, L);
      auto Middle = temporary(PointerType, L);
      assign(Length, binary("-", Last, First, DifferenceType, L), L);
      const auto Check = labelName(), Split = labelName();
      const auto Advance = labelName(), Narrow = labelName();
      const auto Found = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Length, quantity(0, DifferenceType, L), "bool", L),
             Split, Found, L);
      label(Split, L);
      assign(Half,
             binary("/", Length, quantity(2, DifferenceType, L), DifferenceType,
                    L),
             L);
      assign(Middle, binary("+", First, Half, PointerType, L), L);
      branch(Less(Upper ? dereference(json::Object(ValueAddress), L)
                        : dereference(json::Object(Middle), L),
                  Upper ? dereference(json::Object(Middle), L)
                        : dereference(json::Object(ValueAddress), L)),
             Upper ? Narrow : Advance, Upper ? Advance : Narrow, L);
      label(Advance, L);
      assign(
          First,
          binary("+", Middle, quantity(1, DifferenceType, L), PointerType, L),
          L);
      assign(Length,
             binary("-", binary("-", Length, Half, DifferenceType, L),
                    quantity(1, DifferenceType, L), DifferenceType, L),
             L);
      jump(Check, L);
      label(Narrow, L);
      assign(Length, Half, L);
      jump(Check, L);
      label(Found, L);
      if (!Search)
        return First;
      auto Result = temporary("bool", L);
      const auto Compare = labelName(), Present = labelName();
      const auto Absent = labelName(), End = labelName();
      branch(binary("!=", First, Last, "bool", L), Compare, Absent, L);
      label(Compare, L);
      branch(Less(dereference(json::Object(ValueAddress), L),
                  dereference(json::Object(First), L)),
             Absent, Present, L);
      label(Present, L);
      assign(Result, boolean(true, L), L);
      jump(End, L);
      label(Absent, L);
      assign(Result, boolean(false, L), L);
      jump(End, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::AlgorithmIsSorted:
    case UtilityOperation::AlgorithmIsSortedUntil: {
      const bool BooleanResult =
          Operation == UtilityOperation::AlgorithmIsSorted;
      auto Previous = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 3)
        Comparator = snapshot(expression(Call->getArg(2)), L);
      auto Current = snapshot(json::Object(Previous), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto Initialize = labelName(), Check = labelName();
      const auto Compare = labelName(), Next = labelName();
      const auto Unsorted = labelName(), Sorted = labelName();
      const auto End = labelName();
      branch(binary("!=", Previous, Last, "bool", L), Initialize, Sorted, L);
      label(Initialize, L);
      assign(
          Current,
          binary("+", Previous, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Compare, Sorted, L);
      label(Compare, L);
      branch(Comparator
                 ? emitBinaryPredicate(
                       json::Object(*Comparator), Call->getArg(2)->getType(),
                       dereference(json::Object(Current), L),
                       dereference(json::Object(Previous), L), L)
                 : binary("<", dereference(Current, L),
                          dereference(Previous, L), "bool", L),
             Unsorted, Next, L);
      label(Next, L);
      assign(Previous, json::Object(Current), L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      std::optional<Expression> Result;
      if (BooleanResult)
        Result = temporary("bool", L);
      label(Unsorted, L);
      if (Result)
        assign(*Result, boolean(false, L), L);
      jump(End, L);
      label(Sorted, L);
      if (Result)
        assign(*Result, boolean(true, L), L);
      jump(End, L);
      label(End, L);
      return Result ? std::move(*Result) : std::move(Current);
    }
    case UtilityOperation::AlgorithmAdjacentFind: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Predicate;
      if (Call->getNumArgs() == 3)
        Predicate = snapshot(expression(Call->getArg(2)), L);
      auto Current = snapshot(json::Object(First), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto Initialize = labelName(), Check = labelName();
      const auto Compare = labelName(), Next = labelName();
      const auto Exhausted = labelName(), End = labelName();
      branch(binary("!=", First, Last, "bool", L), Initialize, End, L);
      label(Initialize, L);
      assign(Current,
             binary("+", First, quantity(1, DifferenceType, L), PointerType, L),
             L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Compare, Exhausted, L);
      label(Compare, L);
      branch(Predicate
                 ? emitBinaryPredicate(json::Object(*Predicate),
                                       Call->getArg(2)->getType(),
                                       dereference(json::Object(First), L),
                                       dereference(json::Object(Current), L), L)
                 : binary("==", dereference(First, L), dereference(Current, L),
                          "bool", L),
             End, Next, L);
      label(Next, L);
      assign(First, json::Object(Current), L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      label(Exhausted, L);
      assign(First, Current, L);
      jump(End, L);
      label(End, L);
      return First;
    }
    case UtilityOperation::AlgorithmRemove:
    case UtilityOperation::AlgorithmRemoveCopy: {
      const bool Copying = Operation == UtilityOperation::AlgorithmRemoveCopy;
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Output = Copying ? snapshot(expression(Call->getArg(2)), L)
                            : snapshot(json::Object(Current), L);
      const unsigned ValueIndex = Copying ? 3 : 2;
      auto ValueAddress =
          snapshot(address(lvalue(Call->getArg(ValueIndex)),
                           Call->getArg(ValueIndex)->getType(), L),
                   L);
      auto Common = utilityScalarComparisonType(
          A.Context, Call->getArg(0)->getType()->getPointeeType(),
          Call->getArg(ValueIndex)->getType(), false);
      if (!Common)
        reject(L, "algorithm remove",
               "The range element and value have no equality common type.");
      const auto ComparisonType = type(*Common, L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(Copying ? 2 : 0)->getType(), L);
      const auto OutputElementType =
          Copying ? type(Call->getArg(2)->getType()->getPointeeType(), L)
                  : std::string();
      const auto Check = labelName(), Compare = labelName();
      const auto Transfer = labelName(), Next = labelName();
      const auto End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Compare, End, L);
      label(Compare, L);
      branch(binary("==", cast(dereference(Current, L), ComparisonType, L),
                    cast(dereference(ValueAddress, L), ComparisonType, L),
                    "bool", L),
             Next, Transfer, L);
      label(Transfer, L);
      assign(dereference(Output, L),
             Copying ? cast(dereference(Current, L), OutputElementType, L)
                     : dereference(Current, L),
             L);
      assign(Output,
             binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
             L);
      jump(Next, L);
      label(Next, L);
      assign(Current,
             binary("+", Current, quantity(1, DifferenceType, L), InputType, L),
             L);
      jump(Check, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::AlgorithmReplace:
    case UtilityOperation::AlgorithmReplaceCopy: {
      const bool Copying = Operation == UtilityOperation::AlgorithmReplaceCopy;
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Output;
      if (Copying)
        Output = snapshot(expression(Call->getArg(2)), L);
      const unsigned OldIndex = Copying ? 3 : 2;
      const unsigned NewIndex = OldIndex + 1;
      auto OldAddress = snapshot(address(lvalue(Call->getArg(OldIndex)),
                                         Call->getArg(OldIndex)->getType(), L),
                                 L);
      auto NewAddress = snapshot(address(lvalue(Call->getArg(NewIndex)),
                                         Call->getArg(NewIndex)->getType(), L),
                                 L);
      auto Common = utilityScalarComparisonType(
          A.Context, Call->getArg(0)->getType()->getPointeeType(),
          Call->getArg(OldIndex)->getType(), false);
      if (!Common)
        reject(L, "algorithm replace",
               "The range element and old value have no equality common type.");
      const auto ComparisonType = type(*Common, L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(Copying ? 2 : 0)->getType(), L);
      const auto OutputElementType =
          type(Call->getArg(Copying ? 2 : 0)->getType()->getPointeeType(), L);
      const auto Check = labelName(), Compare = labelName();
      const auto Match = labelName(), Mismatch = labelName();
      const auto Next = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Compare, End, L);
      label(Compare, L);
      branch(binary("==", cast(dereference(Current, L), ComparisonType, L),
                    cast(dereference(OldAddress, L), ComparisonType, L), "bool",
                    L),
             Match, Mismatch, L);
      label(Match, L);
      assign(dereference(Output ? *Output : Current, L),
             cast(dereference(NewAddress, L), OutputElementType, L), L);
      jump(Next, L);
      label(Mismatch, L);
      if (Output)
        assign(dereference(*Output, L),
               cast(dereference(Current, L), OutputElementType, L), L);
      jump(Next, L);
      label(Next, L);
      assign(Current,
             binary("+", Current, quantity(1, DifferenceType, L), InputType, L),
             L);
      if (Output)
        assign(
            *Output,
            binary("+", *Output, quantity(1, DifferenceType, L), OutputType, L),
            L);
      jump(Check, L);
      label(End, L);
      return Output ? std::move(*Output) : Expression();
    }
    case UtilityOperation::AlgorithmUnique: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Predicate;
      if (Call->getNumArgs() == 3)
        Predicate = snapshot(expression(Call->getArg(2)), L);
      auto Output = snapshot(json::Object(First), L);
      auto Current = snapshot(json::Object(First), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto Initialize = labelName(), Check = labelName();
      const auto Compare = labelName(), Select = labelName();
      const auto Next = labelName(), Finish = labelName();
      const auto End = labelName();
      branch(binary("!=", First, Last, "bool", L), Initialize, End, L);
      label(Initialize, L);
      assign(Current,
             binary("+", First, quantity(1, DifferenceType, L), PointerType, L),
             L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Compare, Finish, L);
      label(Compare, L);
      branch(Predicate
                 ? emitBinaryPredicate(json::Object(*Predicate),
                                       Call->getArg(2)->getType(),
                                       dereference(json::Object(Output), L),
                                       dereference(json::Object(Current), L), L)
                 : binary("==", dereference(Output, L), dereference(Current, L),
                          "bool", L),
             Next, Select, L);
      label(Select, L);
      assign(
          Output,
          binary("+", Output, quantity(1, DifferenceType, L), PointerType, L),
          L);
      assign(dereference(Output, L), dereference(Current, L), L);
      jump(Next, L);
      label(Next, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      label(Finish, L);
      assign(
          Output,
          binary("+", Output, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(End, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::AlgorithmUniqueCopy: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Output = snapshot(expression(Call->getArg(2)), L);
      std::optional<Expression> Predicate;
      if (Call->getNumArgs() == 4)
        Predicate = snapshot(expression(Call->getArg(3)), L);
      auto Previous = snapshot(json::Object(Current), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(2)->getType(), L);
      const auto OutputElementType =
          type(Call->getArg(2)->getType()->getPointeeType(), L);
      const auto Initialize = labelName(), Check = labelName();
      const auto Compare = labelName(), Transfer = labelName();
      const auto Next = labelName(), End = labelName();
      branch(binary("!=", Current, Last, "bool", L), Initialize, End, L);
      label(Initialize, L);
      assign(dereference(Output, L),
             cast(dereference(Current, L), OutputElementType, L), L);
      assign(Output,
             binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
             L);
      assign(Current,
             binary("+", Current, quantity(1, DifferenceType, L), InputType, L),
             L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Compare, End, L);
      label(Compare, L);
      branch(Predicate
                 ? emitBinaryPredicate(json::Object(*Predicate),
                                       Call->getArg(3)->getType(),
                                       dereference(json::Object(Previous), L),
                                       dereference(json::Object(Current), L), L)
                 : binary("==", dereference(Previous, L),
                          dereference(Current, L), "bool", L),
             Next, Transfer, L);
      label(Transfer, L);
      assign(dereference(Output, L),
             cast(dereference(Current, L), OutputElementType, L), L);
      assign(Output,
             binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
             L);
      assign(Previous, json::Object(Current), L);
      jump(Next, L);
      label(Next, L);
      assign(Current,
             binary("+", Current, quantity(1, DifferenceType, L), InputType, L),
             L);
      jump(Check, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::AlgorithmSearch:
    case UtilityOperation::AlgorithmFindEnd: {
      const bool LastMatch = Operation == UtilityOperation::AlgorithmFindEnd;
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Pattern = snapshot(expression(Call->getArg(2)), L);
      auto PatternLast = snapshot(expression(Call->getArg(3)), L);
      std::optional<Expression> Predicate;
      if (Call->getNumArgs() == 5)
        Predicate = snapshot(expression(Call->getArg(4)), L);
      auto Candidate = snapshot(json::Object(First), L);
      auto Current = snapshot(json::Object(First), L);
      auto PatternCurrent = snapshot(json::Object(Pattern), L);
      auto Result = snapshot(json::Object(Last), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto PatternType = type(Call->getArg(2)->getType(), L);
      const auto EmptyPattern = labelName(), Outer = labelName();
      const auto Start = labelName(), Inner = labelName();
      const auto CheckSource = labelName(), Compare = labelName();
      const auto Advance = labelName(), Mismatch = labelName();
      const auto Match = labelName(), End = labelName();
      branch(binary("!=", Pattern, PatternLast, "bool", L), Outer, EmptyPattern,
             L);
      label(EmptyPattern, L);
      if (!LastMatch)
        assign(Result, First, L);
      jump(End, L);
      label(Outer, L);
      branch(binary("!=", Candidate, Last, "bool", L), Start, End, L);
      label(Start, L);
      assign(Current, json::Object(Candidate), L);
      assign(PatternCurrent, json::Object(Pattern), L);
      jump(Inner, L);
      label(Inner, L);
      branch(binary("!=", PatternCurrent, PatternLast, "bool", L), CheckSource,
             Match, L);
      label(CheckSource, L);
      branch(binary("!=", Current, Last, "bool", L), Compare, End, L);
      label(Compare, L);
      branch(Predicate
                 ? emitBinaryPredicate(
                       json::Object(*Predicate), Call->getArg(4)->getType(),
                       dereference(json::Object(Current), L),
                       dereference(json::Object(PatternCurrent), L), L)
                 : AlgorithmEqual(dereference(Current, L), 0,
                                  dereference(PatternCurrent, L), 2),
             Advance, Mismatch, L);
      label(Advance, L);
      assign(Current,
             binary("+", Current, quantity(1, DifferenceType, L), InputType, L),
             L);
      assign(PatternCurrent,
             binary("+", PatternCurrent, quantity(1, DifferenceType, L),
                    PatternType, L),
             L);
      jump(Inner, L);
      label(Mismatch, L);
      assign(
          Candidate,
          binary("+", Candidate, quantity(1, DifferenceType, L), InputType, L),
          L);
      jump(Outer, L);
      label(Match, L);
      assign(Result, json::Object(Candidate), L);
      if (LastMatch) {
        assign(Candidate,
               binary("+", Candidate, quantity(1, DifferenceType, L), InputType,
                      L),
               L);
        jump(Outer, L);
      } else {
        jump(End, L);
      }
      label(End, L);
      return Result;
    }
    case UtilityOperation::AlgorithmFindFirstOf: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Choices = snapshot(expression(Call->getArg(2)), L);
      auto ChoicesLast = snapshot(expression(Call->getArg(3)), L);
      std::optional<Expression> Predicate;
      if (Call->getNumArgs() == 5)
        Predicate = snapshot(expression(Call->getArg(4)), L);
      auto Choice = snapshot(json::Object(Choices), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto ChoiceType = type(Call->getArg(2)->getType(), L);
      const auto Outer = labelName(), Start = labelName();
      const auto Inner = labelName(), Compare = labelName();
      const auto NextChoice = labelName(), NextInput = labelName();
      const auto End = labelName();
      jump(Outer, L);
      label(Outer, L);
      branch(binary("!=", Current, Last, "bool", L), Start, End, L);
      label(Start, L);
      assign(Choice, json::Object(Choices), L);
      jump(Inner, L);
      label(Inner, L);
      branch(binary("!=", Choice, ChoicesLast, "bool", L), Compare, NextInput,
             L);
      label(Compare, L);
      branch(Predicate
                 ? emitBinaryPredicate(json::Object(*Predicate),
                                       Call->getArg(4)->getType(),
                                       dereference(json::Object(Current), L),
                                       dereference(json::Object(Choice), L), L)
                 : AlgorithmEqual(dereference(Current, L), 0,
                                  dereference(Choice, L), 2),
             End, NextChoice, L);
      label(NextChoice, L);
      assign(Choice,
             binary("+", Choice, quantity(1, DifferenceType, L), ChoiceType, L),
             L);
      jump(Inner, L);
      label(NextInput, L);
      assign(Current,
             binary("+", Current, quantity(1, DifferenceType, L), InputType, L),
             L);
      jump(Outer, L);
      label(End, L);
      return Current;
    }
    case UtilityOperation::AlgorithmSearchN: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto CountType = Call->getArg(2)->getType();
      if (const auto *Enumeration = CountType->getAs<EnumType>())
        CountType = Enumeration->getDecl()->getPromotionType();
      else if (A.Context.isPromotableIntegerType(CountType))
        CountType = A.Context.getPromotedIntegerType(CountType);
      const auto CountTypeName = type(CountType, L);
      auto Count =
          snapshot(cast(expression(Call->getArg(2)), CountTypeName, L), L);
      auto ValueAddress = snapshot(
          address(lvalue(Call->getArg(3)), Call->getArg(3)->getType(), L), L);
      std::optional<Expression> Predicate;
      if (Call->getNumArgs() == 5)
        Predicate = snapshot(expression(Call->getArg(4)), L);
      std::optional<std::string> DefaultComparisonType;
      if (!Predicate) {
        auto Common = utilityScalarComparisonType(
            A.Context, Call->getArg(0)->getType()->getPointeeType(),
            Call->getArg(3)->getType(), false);
        if (!Common)
          reject(L, "algorithm search_n",
                 "The range element and value have no equality common type.");
        DefaultComparisonType = type(*Common, L);
      }
      auto Candidate = snapshot(json::Object(First), L);
      auto Current = snapshot(json::Object(First), L);
      auto Remaining = temporary(CountTypeName, L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto Outer = labelName(), Start = labelName();
      const auto Inner = labelName(), CheckSource = labelName();
      const auto Compare = labelName(), Advance = labelName();
      const auto Mismatch = labelName(), NotFound = labelName();
      const auto End = labelName();
      branch(binary(">", Count, quantity(0, CountTypeName, L), "bool", L),
             Outer, End, L);
      label(Outer, L);
      branch(binary("!=", Candidate, Last, "bool", L), Start, NotFound, L);
      label(Start, L);
      assign(Current, json::Object(Candidate), L);
      assign(Remaining, json::Object(Count), L);
      jump(Inner, L);
      label(Inner, L);
      branch(binary("!=", Remaining, quantity(0, CountTypeName, L), "bool", L),
             CheckSource, End, L);
      label(CheckSource, L);
      branch(binary("!=", Current, Last, "bool", L), Compare, NotFound, L);
      label(Compare, L);
      branch(
          Predicate
              ? emitBinaryPredicate(
                    json::Object(*Predicate), Call->getArg(4)->getType(),
                    dereference(json::Object(Current), L),
                    dereference(json::Object(ValueAddress), L), L)
              : binary("==",
                       cast(dereference(Current, L), *DefaultComparisonType, L),
                       cast(dereference(ValueAddress, L),
                            *DefaultComparisonType, L),
                       "bool", L),
          Advance, Mismatch, L);
      label(Advance, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      assign(Remaining,
             binary("-", Remaining, one(CountTypeName, L), CountTypeName, L),
             L);
      jump(Inner, L);
      label(Mismatch, L);
      assign(
          Candidate,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Outer, L);
      label(NotFound, L);
      assign(Candidate, Last, L);
      jump(End, L);
      label(End, L);
      return Candidate;
    }
    case UtilityOperation::AlgorithmMismatch: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Second = snapshot(expression(Call->getArg(2)), L);
      std::optional<Expression> SecondLast;
      std::optional<unsigned> PredicateIndex;
      if (Call->getNumArgs() == 4 &&
          Call->getArg(3)->getType()->isFunctionPointerType())
        PredicateIndex = 3;
      else if (Call->getNumArgs() == 5)
        PredicateIndex = 4;
      if ((Call->getNumArgs() == 4 && !PredicateIndex) ||
          Call->getNumArgs() == 5)
        SecondLast = snapshot(expression(Call->getArg(3)), L);
      std::optional<Expression> Predicate;
      if (PredicateIndex)
        Predicate = snapshot(expression(Call->getArg(*PredicateIndex)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto FirstType = type(Call->getArg(0)->getType(), L);
      const auto SecondType = type(Call->getArg(2)->getType(), L);
      const auto CheckFirst = labelName(), CheckSecond = labelName();
      const auto Compare = labelName(), Advance = labelName();
      const auto End = labelName();
      jump(CheckFirst, L);
      label(CheckFirst, L);
      branch(binary("!=", First, Last, "bool", L),
             SecondLast ? CheckSecond : Compare, End, L);
      if (SecondLast) {
        label(CheckSecond, L);
        branch(binary("!=", Second, *SecondLast, "bool", L), Compare, End, L);
      }
      label(Compare, L);
      branch(Predicate
                 ? emitBinaryPredicate(json::Object(*Predicate),
                                       Call->getArg(*PredicateIndex)->getType(),
                                       dereference(json::Object(First), L),
                                       dereference(json::Object(Second), L), L)
                 : AlgorithmEqual(dereference(First, L), 0,
                                  dereference(Second, L), 2),
             Advance, End, L);
      label(Advance, L);
      assign(First,
             binary("+", First, quantity(1, DifferenceType, L), FirstType, L),
             L);
      assign(Second,
             binary("+", Second, quantity(1, DifferenceType, L), SecondType, L),
             L);
      jump(CheckFirst, L);
      label(End, L);
      auto Pair = approvedUtilityPairRecord(
          A.S, A.Sources, Call->getType()->getAsCXXRecordDecl(), A.Context);
      if (!Pair)
        reject(L, "algorithm mismatch",
               "The selected std::pair layout is unavailable.");
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(Call->getType(), L);
      if (Place.getString("type") != type(Call->getType(), L))
        reject(L, "algorithm mismatch",
               "The std::mismatch destination type differs from its result.");
      assign(fieldStorage(json::Object(Place), Pair->First, L), First, L);
      assign(fieldStorage(json::Object(Place), Pair->Second, L), Second, L);
      return Place;
    }
    case UtilityOperation::AlgorithmCopyN:
    case UtilityOperation::MemoryUninitializedCopyN:
    case UtilityOperation::MemoryUninitializedMoveN: {
      auto Input = snapshot(expression(Call->getArg(0)), L);
      auto CountType = Call->getArg(1)->getType();
      if (const auto *Enumeration = CountType->getAs<EnumType>())
        CountType = Enumeration->getDecl()->getPromotionType();
      else if (A.Context.isPromotableIntegerType(CountType))
        CountType = A.Context.getPromotedIntegerType(CountType);
      const auto CountTypeName = type(CountType, L);
      auto Remaining =
          snapshot(cast(expression(Call->getArg(1)), CountTypeName, L), L);
      auto Output = snapshot(expression(Call->getArg(2)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(2)->getType(), L);
      const bool MemoryConstruction =
          Operation == UtilityOperation::MemoryUninitializedCopyN ||
          Operation == UtilityOperation::MemoryUninitializedMoveN;
      const auto *Constructor =
          MemoryConstruction ? approvedUtilityMemorySourceConstructor(
                                   A.S, A.Sources, Call, Operation, A.Context)
                             : nullptr;
      const auto ElementType = MemoryConstruction ? Call->getArg(2)
                                                        ->getType()
                                                        ->getPointeeType()
                                                        .getUnqualifiedType()
                                                  : QualType();
      const auto OutputElementType =
          MemoryConstruction
              ? std::string()
              : type(Call->getArg(2)->getType()->getPointeeType(), L);
      const auto Check = labelName(), Transfer = labelName();
      const auto End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary(">", Remaining, quantity(0, CountTypeName, L), "bool", L),
             Transfer, End, L);
      label(Transfer, L);
      if (MemoryConstruction && Constructor)
        constructMemorySource(dereference(Output, L), ElementType, Constructor,
                              json::Object(Input), L);
      else if (MemoryConstruction)
        assign(dereference(Output, L), dereference(Input, L), L);
      else
        assign(dereference(Output, L),
               cast(dereference(Input, L), OutputElementType, L), L);
      assign(Input,
             binary("+", Input, quantity(1, DifferenceType, L), InputType, L),
             L);
      assign(Output,
             binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
             L);
      assign(Remaining,
             binary("-", Remaining, one(CountTypeName, L), CountTypeName, L),
             L);
      jump(Check, L);
      label(End, L);
      if (Operation == UtilityOperation::MemoryUninitializedMoveN) {
        auto Pair = approvedUtilityPairRecord(
            A.S, A.Sources, Call->getType()->getAsCXXRecordDecl(), A.Context);
        if (!Pair)
          reject(L, "uninitialized move",
                 "The selected std::pair layout is unavailable.");
        auto Place = Destination ? std::move(*Destination)
                                 : objectTemporary(Call->getType(), L);
        if (Place.getString("type") != type(Call->getType(), L))
          reject(L, "uninitialized move",
                 "The std::uninitialized_move_n destination type differs from "
                 "its result.");
        assign(fieldStorage(json::Object(Place), Pair->First, L), Input, L);
        assign(fieldStorage(json::Object(Place), Pair->Second, L), Output, L);
        return Place;
      }
      return Output;
    }
    case UtilityOperation::AlgorithmIterSwap: {
      auto Left = snapshot(expression(Call->getArg(0)), L);
      auto Right = snapshot(expression(Call->getArg(1)), L);
      auto LeftValue = snapshot(dereference(Left, L), L);
      auto RightValue = snapshot(dereference(Right, L), L);
      assign(dereference(Left, L), std::move(RightValue), L);
      assign(dereference(Right, L), std::move(LeftValue), L);
      return {};
    }
    case UtilityOperation::AlgorithmRotate: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Middle = snapshot(expression(Call->getArg(1)), L);
      auto Last = snapshot(expression(Call->getArg(2)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto TailLength =
          snapshot(binary("-", Last, Middle, DifferenceType, L), L);
      auto Result = snapshot(binary("+", First, TailLength, PointerType, L), L);
      auto ReverseRange = [&](const Expression &RangeFirst,
                              const Expression &RangeLast) {
        auto Begin = snapshot(json::Object(RangeFirst), L);
        auto End = snapshot(json::Object(RangeLast), L);
        const auto Check = labelName(), Decrement = labelName();
        const auto Swap = labelName(), Done = labelName();
        jump(Check, L);
        label(Check, L);
        branch(binary("!=", Begin, End, "bool", L), Decrement, Done, L);
        label(Decrement, L);
        assign(End,
               binary("-", End, quantity(1, DifferenceType, L), PointerType, L),
               L);
        branch(binary("!=", Begin, End, "bool", L), Swap, Done, L);
        label(Swap, L);
        auto BeginValue = snapshot(dereference(Begin, L), L);
        auto EndValue = snapshot(dereference(End, L), L);
        assign(dereference(Begin, L), std::move(EndValue), L);
        assign(dereference(End, L), std::move(BeginValue), L);
        assign(
            Begin,
            binary("+", Begin, quantity(1, DifferenceType, L), PointerType, L),
            L);
        jump(Check, L);
        label(Done, L);
      };
      ReverseRange(First, Middle);
      ReverseRange(Middle, Last);
      ReverseRange(First, Last);
      return Result;
    }
    case UtilityOperation::AlgorithmRotateCopy: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Middle = snapshot(expression(Call->getArg(1)), L);
      auto Last = snapshot(expression(Call->getArg(2)), L);
      auto Output = snapshot(expression(Call->getArg(3)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(3)->getType(), L);
      const auto OutputElementType =
          type(Call->getArg(3)->getType()->getPointeeType(), L);
      auto CopyRange = [&](const Expression &RangeFirst,
                           const Expression &RangeLast) {
        auto Current = snapshot(json::Object(RangeFirst), L);
        auto End = snapshot(json::Object(RangeLast), L);
        const auto Check = labelName(), Transfer = labelName();
        const auto Done = labelName();
        jump(Check, L);
        label(Check, L);
        branch(binary("!=", Current, End, "bool", L), Transfer, Done, L);
        label(Transfer, L);
        assign(dereference(Output, L),
               cast(dereference(Current, L), OutputElementType, L), L);
        assign(
            Current,
            binary("+", Current, quantity(1, DifferenceType, L), InputType, L),
            L);
        assign(
            Output,
            binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
            L);
        jump(Check, L);
        label(Done, L);
      };
      CopyRange(Middle, Last);
      CopyRange(First, Middle);
      return Output;
    }
    case UtilityOperation::AlgorithmEqualRange: {
      auto RangeFirst = snapshot(expression(Call->getArg(0)), L);
      auto RangeLast = snapshot(expression(Call->getArg(1)), L);
      auto ValueAddress = snapshot(
          address(lvalue(Call->getArg(2)), Call->getArg(2)->getType(), L), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 4)
        Comparator = snapshot(expression(Call->getArg(3)), L);
      std::optional<std::string> DefaultComparisonType;
      if (!Comparator) {
        auto Common = utilityScalarComparisonType(
            A.Context, Call->getArg(0)->getType()->getPointeeType(),
            Call->getArg(2)->getType(), true);
        if (!Common)
          reject(L, "algorithm equal_range",
                 "The range element and value have no ordered common type.");
        DefaultComparisonType = type(*Common, L);
      }
      auto Less = [&](Expression Left, Expression Right) {
        if (Comparator)
          return emitBinaryPredicate(json::Object(*Comparator),
                                     Call->getArg(3)->getType(),
                                     std::move(Left), std::move(Right), L);
        return binary("<", cast(std::move(Left), *DefaultComparisonType, L),
                      cast(std::move(Right), *DefaultComparisonType, L), "bool",
                      L);
      };
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto Bound = [&](const Expression &Start, const Expression &End,
                       bool Upper) {
        auto First = snapshot(json::Object(Start), L);
        auto Last = snapshot(json::Object(End), L);
        auto Length = temporary(DifferenceType, L);
        auto Half = temporary(DifferenceType, L);
        auto Middle = temporary(PointerType, L);
        assign(Length, binary("-", Last, First, DifferenceType, L), L);
        const auto Check = labelName(), Split = labelName();
        const auto Advance = labelName(), Narrow = labelName();
        const auto Found = labelName();
        jump(Check, L);
        label(Check, L);
        branch(binary("!=", Length, quantity(0, DifferenceType, L), "bool", L),
               Split, Found, L);
        label(Split, L);
        assign(Half,
               binary("/", Length, quantity(2, DifferenceType, L),
                      DifferenceType, L),
               L);
        assign(Middle, binary("+", First, Half, PointerType, L), L);
        branch(Less(Upper ? dereference(json::Object(ValueAddress), L)
                          : dereference(json::Object(Middle), L),
                    Upper ? dereference(json::Object(Middle), L)
                          : dereference(json::Object(ValueAddress), L)),
               Upper ? Narrow : Advance, Upper ? Advance : Narrow, L);
        label(Advance, L);
        assign(
            First,
            binary("+", Middle, quantity(1, DifferenceType, L), PointerType, L),
            L);
        assign(Length,
               binary("-", binary("-", Length, Half, DifferenceType, L),
                      quantity(1, DifferenceType, L), DifferenceType, L),
               L);
        jump(Check, L);
        label(Narrow, L);
        assign(Length, Half, L);
        jump(Check, L);
        label(Found, L);
        return First;
      };
      auto Lower = Bound(RangeFirst, RangeLast, false);
      auto Upper = Bound(Lower, RangeLast, true);
      auto Pair = approvedUtilityPairRecord(
          A.S, A.Sources, Call->getType()->getAsCXXRecordDecl(), A.Context);
      if (!Pair)
        reject(L, "algorithm equal_range",
               "The selected std::pair layout is unavailable.");
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(Call->getType(), L);
      if (Place.getString("type") != type(Call->getType(), L))
        reject(
            L, "algorithm equal_range",
            "The std::equal_range destination type differs from its result.");
      assign(fieldStorage(json::Object(Place), Pair->First, L), Lower, L);
      assign(fieldStorage(json::Object(Place), Pair->Second, L), Upper, L);
      return Place;
    }
    case UtilityOperation::AlgorithmLexicographicalCompare: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Second = snapshot(expression(Call->getArg(2)), L);
      auto SecondLast = snapshot(expression(Call->getArg(3)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 5)
        Comparator = snapshot(expression(Call->getArg(4)), L);
      std::optional<std::string> DefaultComparisonType;
      if (!Comparator) {
        auto Common = utilityScalarComparisonType(
            A.Context, Call->getArg(0)->getType()->getPointeeType(),
            Call->getArg(2)->getType()->getPointeeType(), true);
        if (!Common)
          reject(L, "algorithm lexicographical comparison",
                 "The range elements have no ordered common type.");
        DefaultComparisonType = type(*Common, L);
      }
      auto Less = [&](Expression Left, Expression Right) {
        if (Comparator)
          return emitBinaryPredicate(json::Object(*Comparator),
                                     Call->getArg(4)->getType(),
                                     std::move(Left), std::move(Right), L);
        return binary("<", cast(std::move(Left), *DefaultComparisonType, L),
                      cast(std::move(Right), *DefaultComparisonType, L), "bool",
                      L);
      };
      auto Result = temporary("bool", L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto FirstType = type(Call->getArg(0)->getType(), L);
      const auto SecondType = type(Call->getArg(2)->getType(), L);
      const auto CheckFirst = labelName(), CheckSecond = labelName();
      const auto CompareFirst = labelName(), CompareSecond = labelName();
      const auto Advance = labelName(), FirstDone = labelName();
      const auto True = labelName(), False = labelName(), End = labelName();
      jump(CheckFirst, L);
      label(CheckFirst, L);
      branch(binary("!=", First, Last, "bool", L), CheckSecond, FirstDone, L);
      label(CheckSecond, L);
      branch(binary("!=", Second, SecondLast, "bool", L), CompareFirst, False,
             L);
      label(CompareFirst, L);
      branch(Less(dereference(json::Object(First), L),
                  dereference(json::Object(Second), L)),
             True, CompareSecond, L);
      label(CompareSecond, L);
      branch(Less(dereference(json::Object(Second), L),
                  dereference(json::Object(First), L)),
             False, Advance, L);
      label(Advance, L);
      assign(First,
             binary("+", First, quantity(1, DifferenceType, L), FirstType, L),
             L);
      assign(Second,
             binary("+", Second, quantity(1, DifferenceType, L), SecondType, L),
             L);
      jump(CheckFirst, L);
      label(FirstDone, L);
      branch(binary("!=", Second, SecondLast, "bool", L), True, False, L);
      label(True, L);
      assign(Result, boolean(true, L), L);
      jump(End, L);
      label(False, L);
      assign(Result, boolean(false, L), L);
      jump(End, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::AlgorithmIncludes: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Second = snapshot(expression(Call->getArg(2)), L);
      auto SecondLast = snapshot(expression(Call->getArg(3)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 5)
        Comparator = snapshot(expression(Call->getArg(4)), L);
      std::optional<std::string> DefaultComparisonType;
      if (!Comparator) {
        auto Common = utilityScalarComparisonType(
            A.Context, Call->getArg(0)->getType()->getPointeeType(),
            Call->getArg(2)->getType()->getPointeeType(), true);
        if (!Common)
          reject(L, "algorithm includes",
                 "The range elements have no ordered common type.");
        DefaultComparisonType = type(*Common, L);
      }
      auto Less = [&](Expression Left, Expression Right) {
        if (Comparator)
          return emitBinaryPredicate(json::Object(*Comparator),
                                     Call->getArg(4)->getType(),
                                     std::move(Left), std::move(Right), L);
        return binary("<", cast(std::move(Left), *DefaultComparisonType, L),
                      cast(std::move(Right), *DefaultComparisonType, L), "bool",
                      L);
      };
      auto Result = temporary("bool", L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto FirstType = type(Call->getArg(0)->getType(), L);
      const auto SecondType = type(Call->getArg(2)->getType(), L);
      const auto CheckSecond = labelName(), CheckFirst = labelName();
      const auto Missing = labelName(), Compare = labelName();
      const auto AdvanceFirst = labelName(), AdvanceBoth = labelName();
      const auto True = labelName(), False = labelName(), End = labelName();
      jump(CheckSecond, L);
      label(CheckSecond, L);
      branch(binary("!=", Second, SecondLast, "bool", L), CheckFirst, True, L);
      label(CheckFirst, L);
      branch(binary("!=", First, Last, "bool", L), Missing, False, L);
      label(Missing, L);
      branch(Less(dereference(json::Object(Second), L),
                  dereference(json::Object(First), L)),
             False, Compare, L);
      label(Compare, L);
      branch(Less(dereference(json::Object(First), L),
                  dereference(json::Object(Second), L)),
             AdvanceFirst, AdvanceBoth, L);
      label(AdvanceFirst, L);
      assign(First,
             binary("+", First, quantity(1, DifferenceType, L), FirstType, L),
             L);
      jump(CheckSecond, L);
      label(AdvanceBoth, L);
      assign(First,
             binary("+", First, quantity(1, DifferenceType, L), FirstType, L),
             L);
      assign(Second,
             binary("+", Second, quantity(1, DifferenceType, L), SecondType, L),
             L);
      jump(CheckSecond, L);
      label(True, L);
      assign(Result, boolean(true, L), L);
      jump(End, L);
      label(False, L);
      assign(Result, boolean(false, L), L);
      jump(End, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::AlgorithmMerge:
    case UtilityOperation::AlgorithmSetUnion:
    case UtilityOperation::AlgorithmSetIntersection:
    case UtilityOperation::AlgorithmSetDifference:
    case UtilityOperation::AlgorithmSetSymmetricDifference: {
      const bool Merge = Operation == UtilityOperation::AlgorithmMerge;
      const bool Union = Operation == UtilityOperation::AlgorithmSetUnion;
      const bool Intersection =
          Operation == UtilityOperation::AlgorithmSetIntersection;
      const bool Difference =
          Operation == UtilityOperation::AlgorithmSetDifference;
      const bool Symmetric =
          Operation == UtilityOperation::AlgorithmSetSymmetricDifference;
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Second = snapshot(expression(Call->getArg(2)), L);
      auto SecondLast = snapshot(expression(Call->getArg(3)), L);
      auto Output = snapshot(expression(Call->getArg(4)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 6)
        Comparator = snapshot(expression(Call->getArg(5)), L);
      std::optional<std::string> DefaultComparisonType;
      if (!Comparator) {
        auto Common = utilityScalarComparisonType(
            A.Context, Call->getArg(0)->getType()->getPointeeType(),
            Call->getArg(2)->getType()->getPointeeType(), true);
        if (!Common)
          reject(L, "ordered output algorithm",
                 "The input elements have no ordered common type.");
        DefaultComparisonType = type(*Common, L);
      }
      auto Less = [&](Expression Left, Expression Right) {
        if (Comparator)
          return emitBinaryPredicate(json::Object(*Comparator),
                                     Call->getArg(5)->getType(),
                                     std::move(Left), std::move(Right), L);
        return binary("<", cast(std::move(Left), *DefaultComparisonType, L),
                      cast(std::move(Right), *DefaultComparisonType, L), "bool",
                      L);
      };
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto FirstType = type(Call->getArg(0)->getType(), L);
      const auto SecondType = type(Call->getArg(2)->getType(), L);
      const auto OutputType = type(Call->getArg(4)->getType(), L);
      const auto OutputElementType =
          type(Call->getArg(4)->getType()->getPointeeType(), L);
      auto Emit = [&](Expression &Input, llvm::StringRef InputType,
                      bool Write) {
        if (Write) {
          assign(dereference(Output, L),
                 cast(dereference(Input, L), OutputElementType, L), L);
          assign(Output,
                 binary("+", Output, quantity(1, DifferenceType, L), OutputType,
                        L),
                 L);
        }
        assign(Input,
               binary("+", Input, quantity(1, DifferenceType, L), InputType, L),
               L);
      };
      const auto CheckFirst = labelName(), CheckSecond = labelName();
      const auto CompareFirst = labelName(), CompareSecond = labelName();
      const auto FirstLess = labelName(), SecondLess = labelName();
      const auto Equal = labelName(), FirstDone = labelName();
      const auto SecondDone = labelName(), End = labelName();
      jump(CheckFirst, L);
      label(CheckFirst, L);
      branch(binary("!=", First, Last, "bool", L), CheckSecond, FirstDone, L);
      label(CheckSecond, L);
      branch(binary("!=", Second, SecondLast, "bool", L), CompareFirst,
             SecondDone, L);
      label(CompareFirst, L);
      branch(Less(dereference(json::Object(First), L),
                  dereference(json::Object(Second), L)),
             FirstLess, CompareSecond, L);
      label(CompareSecond, L);
      branch(Less(dereference(json::Object(Second), L),
                  dereference(json::Object(First), L)),
             SecondLess, Equal, L);
      label(FirstLess, L);
      Emit(First, FirstType, Merge || Union || Difference || Symmetric);
      jump(CheckFirst, L);
      label(SecondLess, L);
      Emit(Second, SecondType, Merge || Union || Symmetric);
      jump(CheckFirst, L);
      label(Equal, L);
      Emit(First, FirstType, Merge || Union || Intersection);
      if (!Merge)
        Emit(Second, SecondType, false);
      jump(CheckFirst, L);
      const bool CopyFirstTail = Merge || Union || Difference || Symmetric;
      const bool CopySecondTail = Merge || Union || Symmetric;
      const auto CopyFirstCheck = labelName(), CopyFirst = labelName();
      const auto CopySecondCheck = labelName(), CopySecond = labelName();
      label(FirstDone, L);
      jump(CopySecondTail ? CopySecondCheck : End, L);
      label(SecondDone, L);
      jump(CopyFirstTail ? CopyFirstCheck : End, L);
      if (CopyFirstTail) {
        label(CopyFirstCheck, L);
        branch(binary("!=", First, Last, "bool", L), CopyFirst, End, L);
        label(CopyFirst, L);
        Emit(First, FirstType, true);
        jump(CopyFirstCheck, L);
      }
      if (CopySecondTail) {
        label(CopySecondCheck, L);
        branch(binary("!=", Second, SecondLast, "bool", L), CopySecond, End, L);
        label(CopySecond, L);
        Emit(Second, SecondType, true);
        jump(CopySecondCheck, L);
      }
      label(End, L);
      return Output;
    }
    case UtilityOperation::AlgorithmMin:
    case UtilityOperation::AlgorithmMax: {
      const bool Minimum = Operation == UtilityOperation::AlgorithmMin;
      auto LeftAddress =
          snapshot(bind(Call->getArg(0),
                        Call->getDirectCallee()->getParamDecl(0)->getType()),
                   L);
      auto RightAddress =
          snapshot(bind(Call->getArg(1),
                        Call->getDirectCallee()->getParamDecl(1)->getType()),
                   L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 3)
        Comparator = snapshot(expression(Call->getArg(2)), L);
      auto Result = temporary(*LeftAddress.getString("type"), L);
      const auto SelectLeft = labelName(), SelectRight = labelName();
      const auto End = labelName();
      branch(Comparator
                 ? emitBinaryPredicate(
                       json::Object(*Comparator), Call->getArg(2)->getType(),
                       Minimum ? dereference(json::Object(RightAddress), L)
                               : dereference(json::Object(LeftAddress), L),
                       Minimum ? dereference(json::Object(LeftAddress), L)
                               : dereference(json::Object(RightAddress), L),
                       L)
             : Minimum ? binary("<", dereference(RightAddress, L),
                                dereference(LeftAddress, L), "bool", L)
                       : binary("<", dereference(LeftAddress, L),
                                dereference(RightAddress, L), "bool", L),
             SelectRight, SelectLeft, L);
      label(SelectLeft, L);
      assign(Result, LeftAddress, L);
      jump(End, L);
      label(SelectRight, L);
      assign(Result, RightAddress, L);
      jump(End, L);
      label(End, L);
      return dereference(std::move(Result), L);
    }
    case UtilityOperation::AlgorithmClamp: {
      auto ValueAddress =
          snapshot(bind(Call->getArg(0),
                        Call->getDirectCallee()->getParamDecl(0)->getType()),
                   L);
      auto LowAddress =
          snapshot(bind(Call->getArg(1),
                        Call->getDirectCallee()->getParamDecl(1)->getType()),
                   L);
      auto HighAddress =
          snapshot(bind(Call->getArg(2),
                        Call->getDirectCallee()->getParamDecl(2)->getType()),
                   L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 4)
        Comparator = snapshot(expression(Call->getArg(3)), L);
      auto Result = temporary(*ValueAddress.getString("type"), L);
      const auto CheckHigh = labelName(), SelectValue = labelName();
      const auto SelectLow = labelName(), SelectHigh = labelName();
      const auto End = labelName();
      branch(Comparator
                 ? emitBinaryPredicate(
                       json::Object(*Comparator), Call->getArg(3)->getType(),
                       dereference(json::Object(ValueAddress), L),
                       dereference(json::Object(LowAddress), L), L)
                 : binary("<", dereference(ValueAddress, L),
                          dereference(LowAddress, L), "bool", L),
             SelectLow, CheckHigh, L);
      label(CheckHigh, L);
      branch(Comparator
                 ? emitBinaryPredicate(
                       json::Object(*Comparator), Call->getArg(3)->getType(),
                       dereference(json::Object(HighAddress), L),
                       dereference(json::Object(ValueAddress), L), L)
                 : binary("<", dereference(HighAddress, L),
                          dereference(ValueAddress, L), "bool", L),
             SelectHigh, SelectValue, L);
      label(SelectValue, L);
      assign(Result, ValueAddress, L);
      jump(End, L);
      label(SelectLow, L);
      assign(Result, LowAddress, L);
      jump(End, L);
      label(SelectHigh, L);
      assign(Result, HighAddress, L);
      jump(End, L);
      label(End, L);
      return dereference(std::move(Result), L);
    }
    case UtilityOperation::AlgorithmMinmax: {
      const auto *Function = Call->getDirectCallee();
      auto LeftAddress = snapshot(
          bind(Call->getArg(0), Function->getParamDecl(0)->getType()), L);
      auto RightAddress = snapshot(
          bind(Call->getArg(1), Function->getParamDecl(1)->getType()), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 3)
        Comparator = snapshot(expression(Call->getArg(2)), L);
      auto Pair = approvedUtilityReferencePairRecord(
          A.S, A.Sources, Call->getType()->getAsCXXRecordDecl(), A.Context);
      if (!Pair)
        reject(L, "algorithm minmax",
               "The selected reference std::pair layout is unavailable.");
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(Call->getType(), L);
      if (Place.getString("type") != type(Call->getType(), L))
        reject(L, "algorithm minmax",
               "The std::minmax destination type differs from its result.");
      const auto Forward = labelName(), Reverse = labelName(),
                 End = labelName();
      branch(Comparator
                 ? emitBinaryPredicate(
                       json::Object(*Comparator), Call->getArg(2)->getType(),
                       dereference(json::Object(RightAddress), L),
                       dereference(json::Object(LeftAddress), L), L)
                 : binary("<", dereference(RightAddress, L),
                          dereference(LeftAddress, L), "bool", L),
             Reverse, Forward, L);
      label(Forward, L);
      assign(fieldStorage(json::Object(Place), Pair->First, L), LeftAddress, L);
      assign(fieldStorage(json::Object(Place), Pair->Second, L), RightAddress,
             L);
      jump(End, L);
      label(Reverse, L);
      assign(fieldStorage(json::Object(Place), Pair->First, L), RightAddress,
             L);
      assign(fieldStorage(json::Object(Place), Pair->Second, L), LeftAddress,
             L);
      jump(End, L);
      label(End, L);
      return Place;
    }
    case UtilityOperation::AlgorithmMinmaxElement: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 3)
        Comparator = snapshot(expression(Call->getArg(2)), L);
      auto Less = [&](Expression Left, Expression Right) {
        if (Comparator)
          return emitBinaryPredicate(json::Object(*Comparator),
                                     Call->getArg(2)->getType(),
                                     std::move(Left), std::move(Right), L);
        return binary("<", std::move(Left), std::move(Right), "bool", L);
      };
      auto Minimum = snapshot(json::Object(First), L);
      auto Maximum = snapshot(json::Object(First), L);
      auto Current = snapshot(json::Object(First), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto Next = temporary(PointerType, L);
      auto Lower = temporary(PointerType, L);
      auto Upper = temporary(PointerType, L);
      const auto Initialize = labelName(), CompareInitial = labelName();
      const auto InitialSecondMinimum = labelName();
      const auto InitialSecondMaximum = labelName();
      const auto CheckPair = labelName(), PreparePair = labelName();
      const auto ComparePair = labelName(), NextLower = labelName();
      const auto CurrentLower = labelName(), UpdateMinimum = labelName();
      const auto SelectMinimum = labelName(), UpdateMaximum = labelName();
      const auto SelectMaximum = labelName(), AdvanceInitial = labelName();
      const auto AdvancePair = labelName();
      const auto Odd = labelName(), CheckOddMaximum = labelName();
      const auto SelectOddMinimum = labelName();
      const auto SelectOddMaximum = labelName(), End = labelName();
      branch(binary("!=", First, Last, "bool", L), Initialize, End, L);
      label(Initialize, L);
      assign(Current,
             binary("+", First, quantity(1, DifferenceType, L), PointerType, L),
             L);
      branch(binary("!=", Current, Last, "bool", L), CompareInitial, End, L);
      label(CompareInitial, L);
      branch(Less(dereference(json::Object(Current), L),
                  dereference(json::Object(First), L)),
             InitialSecondMinimum, InitialSecondMaximum, L);
      label(InitialSecondMinimum, L);
      assign(Minimum, Current, L);
      assign(Maximum, First, L);
      jump(AdvanceInitial, L);
      label(InitialSecondMaximum, L);
      assign(Minimum, First, L);
      assign(Maximum, Current, L);
      jump(AdvanceInitial, L);
      label(AdvanceInitial, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(CheckPair, L);
      label(CheckPair, L);
      branch(binary("!=", Current, Last, "bool", L), PreparePair, End, L);
      label(PreparePair, L);
      assign(
          Next,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      branch(binary("!=", Next, Last, "bool", L), ComparePair, Odd, L);
      label(ComparePair, L);
      branch(Less(dereference(json::Object(Next), L),
                  dereference(json::Object(Current), L)),
             NextLower, CurrentLower, L);
      label(NextLower, L);
      assign(Lower, Next, L);
      assign(Upper, Current, L);
      jump(UpdateMinimum, L);
      label(CurrentLower, L);
      assign(Lower, Current, L);
      assign(Upper, Next, L);
      jump(UpdateMinimum, L);
      label(UpdateMinimum, L);
      branch(Less(dereference(json::Object(Lower), L),
                  dereference(json::Object(Minimum), L)),
             SelectMinimum, UpdateMaximum, L);
      label(SelectMinimum, L);
      assign(Minimum, Lower, L);
      jump(UpdateMaximum, L);
      label(UpdateMaximum, L);
      branch(Less(dereference(json::Object(Upper), L),
                  dereference(json::Object(Maximum), L)),
             AdvancePair, SelectMaximum, L);
      label(SelectMaximum, L);
      assign(Maximum, Upper, L);
      jump(AdvancePair, L);
      label(AdvancePair, L);
      assign(Current,
             binary("+", Next, quantity(1, DifferenceType, L), PointerType, L),
             L);
      jump(CheckPair, L);
      label(Odd, L);
      branch(Less(dereference(json::Object(Current), L),
                  dereference(json::Object(Minimum), L)),
             SelectOddMinimum, CheckOddMaximum, L);
      label(SelectOddMinimum, L);
      assign(Minimum, Current, L);
      jump(End, L);
      label(CheckOddMaximum, L);
      branch(Less(dereference(json::Object(Current), L),
                  dereference(json::Object(Maximum), L)),
             End, SelectOddMaximum, L);
      label(SelectOddMaximum, L);
      assign(Maximum, Current, L);
      jump(End, L);
      label(End, L);
      auto Pair = approvedUtilityPairRecord(
          A.S, A.Sources, Call->getType()->getAsCXXRecordDecl(), A.Context);
      if (!Pair)
        reject(L, "algorithm minmax_element",
               "The selected std::pair layout is unavailable.");
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(Call->getType(), L);
      if (Place.getString("type") != type(Call->getType(), L))
        reject(L, "algorithm minmax_element",
               "The std::minmax_element destination type differs from its "
               "result.");
      assign(fieldStorage(json::Object(Place), Pair->First, L), Minimum, L);
      assign(fieldStorage(json::Object(Place), Pair->Second, L), Maximum, L);
      return Place;
    }
    case UtilityOperation::AlgorithmIsHeap:
    case UtilityOperation::AlgorithmIsHeapUntil: {
      const bool BooleanResult = Operation == UtilityOperation::AlgorithmIsHeap;
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 3)
        Comparator = snapshot(expression(Call->getArg(2)), L);
      auto Less = [&](Expression Left, Expression Right) {
        if (Comparator)
          return emitBinaryPredicate(json::Object(*Comparator),
                                     Call->getArg(2)->getType(),
                                     std::move(Left), std::move(Right), L);
        return binary("<", std::move(Left), std::move(Right), "bool", L);
      };
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto Length = snapshot(binary("-", Last, First, DifferenceType, L), L);
      auto Child = temporary(DifferenceType, L);
      auto Parent = temporary(DifferenceType, L);
      auto Result = temporary(BooleanResult ? llvm::StringRef("bool")
                                            : llvm::StringRef(PointerType),
                              L);
      const auto Check = labelName(), Compare = labelName();
      const auto Violation = labelName(), Advance = labelName();
      const auto Valid = labelName(), End = labelName();
      assign(Child, quantity(1, DifferenceType, L), L);
      jump(Check, L);
      label(Check, L);
      branch(binary("<", Child, Length, "bool", L), Compare, Valid, L);
      label(Compare, L);
      assign(Parent,
             binary("/",
                    binary("-", Child, quantity(1, DifferenceType, L),
                           DifferenceType, L),
                    quantity(2, DifferenceType, L), DifferenceType, L),
             L);
      branch(Less(dereference(binary("+", First, Parent, PointerType, L), L),
                  dereference(binary("+", First, Child, PointerType, L), L)),
             Violation, Advance, L);
      label(Advance, L);
      assign(
          Child,
          binary("+", Child, quantity(1, DifferenceType, L), DifferenceType, L),
          L);
      jump(Check, L);
      label(Violation, L);
      assign(Result,
             BooleanResult ? boolean(false, L)
                           : binary("+", First, Child, PointerType, L),
             L);
      jump(End, L);
      label(Valid, L);
      assign(Result, BooleanResult ? boolean(true, L) : Last, L);
      jump(End, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::AlgorithmMakeHeap: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 3)
        Comparator = snapshot(expression(Call->getArg(2)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto Length = snapshot(binary("-", Last, First, DifferenceType, L), L);
      makeHeap(json::Object(First), json::Object(Length), PointerType,
               DifferenceType, L, Comparator,
               Comparator ? Call->getArg(2)->getType() : QualType{});
      return {};
    }
    case UtilityOperation::AlgorithmPushHeap: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 3)
        Comparator = snapshot(expression(Call->getArg(2)), L);
      auto Less = [&](Expression Left, Expression Right) {
        if (Comparator)
          return emitBinaryPredicate(json::Object(*Comparator),
                                     Call->getArg(2)->getType(),
                                     std::move(Left), std::move(Right), L);
        return binary("<", std::move(Left), std::move(Right), "bool", L);
      };
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto Length = snapshot(binary("-", Last, First, DifferenceType, L), L);
      auto Child = temporary(DifferenceType, L);
      auto Parent = temporary(DifferenceType, L);
      auto At = [&](const Expression &Position) {
        return dereference(binary("+", json::Object(First),
                                  json::Object(Position), PointerType, L),
                           L);
      };
      const auto Initialize = labelName(), CheckParent = labelName();
      const auto CompareParent = labelName(), Swap = labelName();
      const auto End = labelName();
      branch(binary(">", Length, quantity(1, DifferenceType, L), "bool", L),
             Initialize, End, L);
      label(Initialize, L);
      assign(Child,
             binary("-", Length, quantity(1, DifferenceType, L), DifferenceType,
                    L),
             L);
      jump(CheckParent, L);
      label(CheckParent, L);
      branch(binary(">", Child, quantity(0, DifferenceType, L), "bool", L),
             CompareParent, End, L);
      label(CompareParent, L);
      assign(Parent,
             binary("/",
                    binary("-", Child, quantity(1, DifferenceType, L),
                           DifferenceType, L),
                    quantity(2, DifferenceType, L), DifferenceType, L),
             L);
      branch(Less(At(Parent), At(Child)), Swap, End, L);
      label(Swap, L);
      auto ParentValue = snapshot(At(Parent), L);
      auto ChildValue = snapshot(At(Child), L);
      assign(At(Parent), std::move(ChildValue), L);
      assign(At(Child), std::move(ParentValue), L);
      assign(Child, Parent, L);
      jump(CheckParent, L);
      label(End, L);
      return {};
    }
    case UtilityOperation::AlgorithmPopHeap:
    case UtilityOperation::AlgorithmSortHeap: {
      const bool Sort = Operation == UtilityOperation::AlgorithmSortHeap;
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 3)
        Comparator = snapshot(expression(Call->getArg(2)), L);
      const auto ComparatorType =
          Comparator ? Call->getArg(2)->getType() : QualType{};
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto HeapSize = snapshot(binary("-", Last, First, DifferenceType, L), L);
      if (Sort) {
        sortHeap(json::Object(First), json::Object(HeapSize), PointerType,
                 DifferenceType, L, Comparator, ComparatorType);
        return {};
      }
      auto At = [&](const Expression &Position) {
        return dereference(binary("+", json::Object(First),
                                  json::Object(Position), PointerType, L),
                           L);
      };
      const auto Extract = labelName(), End = labelName();
      branch(binary(">", HeapSize, quantity(1, DifferenceType, L), "bool", L),
             Extract, End, L);
      label(Extract, L);
      assign(HeapSize,
             binary("-", HeapSize, quantity(1, DifferenceType, L),
                    DifferenceType, L),
             L);
      auto TopValue = snapshot(dereference(json::Object(First), L), L);
      auto EndValue = snapshot(At(HeapSize), L);
      assign(dereference(json::Object(First), L), std::move(EndValue), L);
      assign(At(HeapSize), std::move(TopValue), L);
      heapSiftDown(json::Object(First), json::Object(HeapSize),
                   quantity(0, DifferenceType, L), PointerType, DifferenceType,
                   L, Comparator, ComparatorType);
      jump(End, L);
      label(End, L);
      return {};
    }
    case UtilityOperation::AlgorithmSort: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 3)
        Comparator = snapshot(expression(Call->getArg(2)), L);
      const auto ComparatorType =
          Comparator ? Call->getArg(2)->getType() : QualType{};
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto Length = snapshot(binary("-", Last, First, DifferenceType, L), L);
      makeHeap(json::Object(First), json::Object(Length), PointerType,
               DifferenceType, L, Comparator, ComparatorType);
      sortHeap(json::Object(First), json::Object(Length), PointerType,
               DifferenceType, L, Comparator, ComparatorType);
      return {};
    }
    case UtilityOperation::AlgorithmStableSort: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 3)
        Comparator = snapshot(expression(Call->getArg(2)), L);
      const auto ComparatorType =
          Comparator ? Call->getArg(2)->getType() : QualType{};
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto ElementType =
          type(Call->getArg(0)->getType()->getPointeeType(), L);
      auto Length = snapshot(binary("-", Last, First, DifferenceType, L), L);
      auto Width = temporary(DifferenceType, L);
      auto Remaining = temporary(DifferenceType, L);
      auto RunFirst = temporary(PointerType, L);
      auto Middle = temporary(PointerType, L);
      auto RunLast = temporary(PointerType, L);
      const auto Initialize = labelName(), Pass = labelName();
      const auto CheckRun = labelName(), CheckMiddle = labelName();
      const auto SelectMiddle = labelName(), CheckLast = labelName();
      const auto SelectFullLast = labelName(), SelectFinalLast = labelName();
      const auto Merge = labelName(), AdvanceRun = labelName();
      const auto PassDone = labelName(), DoubleWidth = labelName();
      const auto End = labelName();
      branch(binary(">", Length, quantity(1, DifferenceType, L), "bool", L),
             Initialize, End, L);
      label(Initialize, L);
      assign(Width, quantity(1, DifferenceType, L), L);
      jump(Pass, L);
      label(Pass, L);
      assign(RunFirst, First, L);
      jump(CheckRun, L);
      label(CheckRun, L);
      branch(binary("!=", RunFirst, Last, "bool", L), CheckMiddle, PassDone, L);
      label(CheckMiddle, L);
      assign(Remaining, binary("-", Last, RunFirst, DifferenceType, L), L);
      branch(binary(">", Remaining, Width, "bool", L), SelectMiddle, PassDone,
             L);
      label(SelectMiddle, L);
      assign(Middle, binary("+", RunFirst, Width, PointerType, L), L);
      jump(CheckLast, L);
      label(CheckLast, L);
      assign(Remaining, binary("-", Last, Middle, DifferenceType, L), L);
      branch(binary(">", Remaining, Width, "bool", L), SelectFullLast,
             SelectFinalLast, L);
      label(SelectFullLast, L);
      assign(RunLast, binary("+", Middle, Width, PointerType, L), L);
      jump(Merge, L);
      label(SelectFinalLast, L);
      assign(RunLast, Last, L);
      jump(Merge, L);
      label(Merge, L);
      stableMerge(json::Object(RunFirst), json::Object(Middle),
                  json::Object(RunLast), PointerType, ElementType,
                  DifferenceType, L, Comparator, ComparatorType);
      jump(AdvanceRun, L);
      label(AdvanceRun, L);
      assign(RunFirst, RunLast, L);
      jump(CheckRun, L);
      label(PassDone, L);
      branch(binary(">=", Width, binary("-", Length, Width, DifferenceType, L),
                    "bool", L),
             End, DoubleWidth, L);
      label(DoubleWidth, L);
      assign(
          Width,
          binary("*", Width, quantity(2, DifferenceType, L), DifferenceType, L),
          L);
      jump(Pass, L);
      label(End, L);
      return {};
    }
    case UtilityOperation::AlgorithmInplaceMerge: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Middle = snapshot(expression(Call->getArg(1)), L);
      auto Last = snapshot(expression(Call->getArg(2)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 4)
        Comparator = snapshot(expression(Call->getArg(3)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto ElementType =
          type(Call->getArg(0)->getType()->getPointeeType(), L);
      stableMerge(std::move(First), std::move(Middle), std::move(Last),
                  PointerType, ElementType, DifferenceType, L, Comparator,
                  Comparator ? Call->getArg(3)->getType() : QualType{});
      return {};
    }
    case UtilityOperation::AlgorithmPartialSort: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Middle = snapshot(expression(Call->getArg(1)), L);
      auto Last = snapshot(expression(Call->getArg(2)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 4)
        Comparator = snapshot(expression(Call->getArg(3)), L);
      const auto ComparatorType =
          Comparator ? Call->getArg(3)->getType() : QualType{};
      auto Less = [&](Expression Left, Expression Right) {
        if (Comparator)
          return emitBinaryPredicate(json::Object(*Comparator), ComparatorType,
                                     std::move(Left), std::move(Right), L);
        return binary("<", std::move(Left), std::move(Right), "bool", L);
      };
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto HeapSize =
          snapshot(binary("-", Middle, First, DifferenceType, L), L);
      auto Current = snapshot(json::Object(Middle), L);
      const auto NonEmpty = labelName(), Scan = labelName();
      const auto Compare = labelName(), Replace = labelName();
      const auto Advance = labelName(), Finish = labelName();
      const auto End = labelName();
      branch(binary(">", HeapSize, quantity(0, DifferenceType, L), "bool", L),
             NonEmpty, End, L);
      label(NonEmpty, L);
      makeHeap(json::Object(First), json::Object(HeapSize), PointerType,
               DifferenceType, L, Comparator, ComparatorType);
      jump(Scan, L);
      label(Scan, L);
      branch(binary("!=", Current, Last, "bool", L), Compare, Finish, L);
      label(Compare, L);
      branch(Less(dereference(json::Object(Current), L),
                  dereference(json::Object(First), L)),
             Replace, Advance, L);
      label(Replace, L);
      auto RootValue = snapshot(dereference(json::Object(First), L), L);
      auto CurrentValue = snapshot(dereference(Current, L), L);
      assign(dereference(json::Object(First), L), std::move(CurrentValue), L);
      assign(dereference(Current, L), std::move(RootValue), L);
      heapSiftDown(json::Object(First), json::Object(HeapSize),
                   quantity(0, DifferenceType, L), PointerType, DifferenceType,
                   L, Comparator, ComparatorType);
      jump(Advance, L);
      label(Advance, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Scan, L);
      label(Finish, L);
      sortHeap(json::Object(First), json::Object(HeapSize), PointerType,
               DifferenceType, L, Comparator, ComparatorType);
      jump(End, L);
      label(End, L);
      return {};
    }
    case UtilityOperation::AlgorithmPartialSortCopy: {
      auto InputFirst = snapshot(expression(Call->getArg(0)), L);
      auto InputLast = snapshot(expression(Call->getArg(1)), L);
      auto OutputFirst = snapshot(expression(Call->getArg(2)), L);
      auto OutputLast = snapshot(expression(Call->getArg(3)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 5)
        Comparator = snapshot(expression(Call->getArg(4)), L);
      const auto ComparatorType =
          Comparator ? Call->getArg(4)->getType() : QualType{};
      std::optional<std::string> DefaultComparisonType;
      if (!Comparator) {
        auto Common = utilityScalarComparisonType(
            A.Context, Call->getArg(0)->getType()->getPointeeType(),
            Call->getArg(2)->getType()->getPointeeType(), true);
        if (!Common)
          reject(L, "algorithm partial_sort_copy",
                 "The input and output elements have no ordered common type.");
        DefaultComparisonType = type(*Common, L);
      }
      auto Less = [&](Expression Left, Expression Right) {
        if (Comparator)
          return emitBinaryPredicate(json::Object(*Comparator), ComparatorType,
                                     std::move(Left), std::move(Right), L);
        return binary("<", cast(std::move(Left), *DefaultComparisonType, L),
                      cast(std::move(Right), *DefaultComparisonType, L), "bool",
                      L);
      };
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(2)->getType(), L);
      const auto OutputElementType =
          type(Call->getArg(2)->getType()->getPointeeType(), L);
      auto Input = snapshot(json::Object(InputFirst), L);
      auto Output = snapshot(json::Object(OutputFirst), L);
      auto HeapSize = temporary(DifferenceType, L);
      const auto FillCheck = labelName(), Fill = labelName();
      const auto CheckOutput = labelName(), Prepared = labelName();
      const auto NonEmpty = labelName(), Scan = labelName();
      const auto Compare = labelName(), Replace = labelName();
      const auto Advance = labelName(), Finish = labelName();
      const auto End = labelName();
      jump(FillCheck, L);
      label(FillCheck, L);
      branch(binary("!=", Input, InputLast, "bool", L), CheckOutput, Prepared,
             L);
      label(CheckOutput, L);
      branch(binary("!=", Output, OutputLast, "bool", L), Fill, Prepared, L);
      label(Fill, L);
      assign(dereference(Output, L),
             cast(dereference(Input, L), OutputElementType, L), L);
      assign(Input,
             binary("+", Input, quantity(1, DifferenceType, L), InputType, L),
             L);
      assign(Output,
             binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
             L);
      jump(FillCheck, L);
      label(Prepared, L);
      assign(HeapSize, binary("-", Output, OutputFirst, DifferenceType, L), L);
      branch(binary(">", HeapSize, quantity(0, DifferenceType, L), "bool", L),
             NonEmpty, End, L);
      label(NonEmpty, L);
      makeHeap(json::Object(OutputFirst), json::Object(HeapSize), OutputType,
               DifferenceType, L, Comparator, ComparatorType);
      jump(Scan, L);
      label(Scan, L);
      branch(binary("!=", Input, InputLast, "bool", L), Compare, Finish, L);
      label(Compare, L);
      branch(Less(dereference(json::Object(Input), L),
                  dereference(json::Object(OutputFirst), L)),
             Replace, Advance, L);
      label(Replace, L);
      assign(dereference(json::Object(OutputFirst), L),
             cast(dereference(Input, L), OutputElementType, L), L);
      heapSiftDown(json::Object(OutputFirst), json::Object(HeapSize),
                   quantity(0, DifferenceType, L), OutputType, DifferenceType,
                   L, Comparator, ComparatorType);
      jump(Advance, L);
      label(Advance, L);
      assign(Input,
             binary("+", Input, quantity(1, DifferenceType, L), InputType, L),
             L);
      jump(Scan, L);
      label(Finish, L);
      sortHeap(json::Object(OutputFirst), json::Object(HeapSize), OutputType,
               DifferenceType, L, Comparator, ComparatorType);
      jump(End, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::AlgorithmNthElement: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Nth = snapshot(expression(Call->getArg(1)), L);
      auto Last = snapshot(expression(Call->getArg(2)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 4)
        Comparator = snapshot(expression(Call->getArg(3)), L);
      auto Less = [&](Expression LeftValue, Expression RightValue) {
        if (Comparator)
          return emitBinaryPredicate(
              json::Object(*Comparator), Call->getArg(3)->getType(),
              std::move(LeftValue), std::move(RightValue), L);
        return binary("<", std::move(LeftValue), std::move(RightValue), "bool",
                      L);
      };
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto ElementType =
          type(Call->getArg(0)->getType()->getPointeeType(), L);
      auto Length = snapshot(binary("-", Last, First, DifferenceType, L), L);
      auto Target = snapshot(binary("-", Nth, First, DifferenceType, L), L);
      auto Left = temporary(DifferenceType, L);
      auto Right = temporary(DifferenceType, L);
      auto Low = temporary(DifferenceType, L);
      auto Current = temporary(DifferenceType, L);
      auto High = temporary(DifferenceType, L);
      auto PivotIndex = temporary(DifferenceType, L);
      auto Pivot = temporary(ElementType, L);
      auto At = [&](const Expression &Position) {
        return dereference(binary("+", json::Object(First),
                                  json::Object(Position), PointerType, L),
                           L);
      };
      const auto Initialize = labelName(), SelectPivot = labelName();
      const auto Partition = labelName(), Classify = labelName();
      const auto CheckHigher = labelName(), MoveLow = labelName();
      const auto MoveHigh = labelName(), Equal = labelName();
      const auto Partitioned = labelName(), CheckRight = labelName();
      const auto NarrowLeft = labelName(), NarrowRight = labelName();
      const auto End = labelName();
      branch(binary("!=", Nth, Last, "bool", L), Initialize, End, L);
      label(Initialize, L);
      assign(Left, quantity(0, DifferenceType, L), L);
      assign(Right, Length, L);
      jump(SelectPivot, L);
      label(SelectPivot, L);
      assign(PivotIndex,
             binary("+", Left,
                    binary("/", binary("-", Right, Left, DifferenceType, L),
                           quantity(2, DifferenceType, L), DifferenceType, L),
                    DifferenceType, L),
             L);
      assign(Pivot, At(PivotIndex), L);
      assign(Low, Left, L);
      assign(Current, Left, L);
      assign(High, Right, L);
      jump(Partition, L);
      label(Partition, L);
      branch(binary("<", Current, High, "bool", L), Classify, Partitioned, L);
      label(Classify, L);
      branch(Less(At(Current), json::Object(Pivot)), MoveLow, CheckHigher, L);
      label(CheckHigher, L);
      branch(Less(json::Object(Pivot), At(Current)), MoveHigh, Equal, L);
      label(MoveLow, L);
      {
        auto LowValue = snapshot(At(Low), L);
        auto CurrentValue = snapshot(At(Current), L);
        assign(At(Low), std::move(CurrentValue), L);
        assign(At(Current), std::move(LowValue), L);
      }
      assign(
          Low,
          binary("+", Low, quantity(1, DifferenceType, L), DifferenceType, L),
          L);
      assign(Current,
             binary("+", Current, quantity(1, DifferenceType, L),
                    DifferenceType, L),
             L);
      jump(Partition, L);
      label(MoveHigh, L);
      assign(
          High,
          binary("-", High, quantity(1, DifferenceType, L), DifferenceType, L),
          L);
      {
        auto CurrentValue = snapshot(At(Current), L);
        auto HighValue = snapshot(At(High), L);
        assign(At(Current), std::move(HighValue), L);
        assign(At(High), std::move(CurrentValue), L);
      }
      jump(Partition, L);
      label(Equal, L);
      assign(Current,
             binary("+", Current, quantity(1, DifferenceType, L),
                    DifferenceType, L),
             L);
      jump(Partition, L);
      label(Partitioned, L);
      branch(binary("<", Target, Low, "bool", L), NarrowLeft, CheckRight, L);
      label(CheckRight, L);
      branch(binary("<", Target, High, "bool", L), End, NarrowRight, L);
      label(NarrowLeft, L);
      assign(Right, Low, L);
      jump(SelectPivot, L);
      label(NarrowRight, L);
      assign(Left, High, L);
      jump(SelectPivot, L);
      label(End, L);
      return {};
    }
    case UtilityOperation::AlgorithmNextPermutation:
    case UtilityOperation::AlgorithmPrevPermutation: {
      const bool Next = Operation == UtilityOperation::AlgorithmNextPermutation;
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Comparator;
      if (Call->getNumArgs() == 3)
        Comparator = snapshot(expression(Call->getArg(2)), L);
      auto Less = [&](Expression Left, Expression Right) {
        if (Comparator)
          return emitBinaryPredicate(json::Object(*Comparator),
                                     Call->getArg(2)->getType(),
                                     std::move(Left), std::move(Right), L);
        return binary("<", std::move(Left), std::move(Right), "bool", L);
      };
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto Current = temporary(PointerType, L);
      auto Successor = temporary(PointerType, L);
      auto Result = temporary("bool", L);
      auto ReverseRange = [&](const Expression &RangeFirst,
                              const Expression &RangeLast) {
        auto Begin = snapshot(json::Object(RangeFirst), L);
        auto End = snapshot(json::Object(RangeLast), L);
        const auto Check = labelName(), Decrement = labelName();
        const auto Swap = labelName(), Done = labelName();
        jump(Check, L);
        label(Check, L);
        branch(binary("!=", Begin, End, "bool", L), Decrement, Done, L);
        label(Decrement, L);
        assign(End,
               binary("-", End, quantity(1, DifferenceType, L), PointerType, L),
               L);
        branch(binary("!=", Begin, End, "bool", L), Swap, Done, L);
        label(Swap, L);
        auto BeginValue = snapshot(dereference(Begin, L), L);
        auto EndValue = snapshot(dereference(End, L), L);
        assign(dereference(Begin, L), std::move(EndValue), L);
        assign(dereference(End, L), std::move(BeginValue), L);
        assign(
            Begin,
            binary("+", Begin, quantity(1, DifferenceType, L), PointerType, L),
            L);
        jump(Check, L);
        label(Done, L);
      };
      const auto NonEmpty = labelName(), Search = labelName();
      const auto ComparePivot = labelName(), CheckFirst = labelName();
      const auto FindSuccessor = labelName(), CheckSuccessor = labelName();
      const auto Exchange = labelName(), Wrap = labelName();
      const auto EmptyOrSingle = labelName(), End = labelName();
      branch(binary("!=", First, Last, "bool", L), NonEmpty, EmptyOrSingle, L);
      label(NonEmpty, L);
      assign(Current,
             binary("-", Last, quantity(1, DifferenceType, L), PointerType, L),
             L);
      branch(binary("!=", First, Current, "bool", L), Search, EmptyOrSingle, L);
      label(Search, L);
      assign(Successor, Current, L);
      assign(
          Current,
          binary("-", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(ComparePivot, L);
      label(ComparePivot, L);
      branch(Next ? Less(dereference(json::Object(Current), L),
                         dereference(json::Object(Successor), L))
                  : Less(dereference(json::Object(Successor), L),
                         dereference(json::Object(Current), L)),
             FindSuccessor, CheckFirst, L);
      label(CheckFirst, L);
      branch(binary("==", Current, First, "bool", L), Wrap, Search, L);
      label(FindSuccessor, L);
      assign(Successor, Last, L);
      jump(CheckSuccessor, L);
      label(CheckSuccessor, L);
      assign(Successor,
             binary("-", Successor, quantity(1, DifferenceType, L), PointerType,
                    L),
             L);
      branch(Next ? Less(dereference(json::Object(Current), L),
                         dereference(json::Object(Successor), L))
                  : Less(dereference(json::Object(Successor), L),
                         dereference(json::Object(Current), L)),
             Exchange, CheckSuccessor, L);
      label(Exchange, L);
      {
        auto CurrentValue = snapshot(dereference(Current, L), L);
        auto SuccessorValue = snapshot(dereference(Successor, L), L);
        assign(dereference(Current, L), std::move(SuccessorValue), L);
        assign(dereference(Successor, L), std::move(CurrentValue), L);
      }
      assign(
          Successor,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      ReverseRange(Successor, Last);
      assign(Result, boolean(true, L), L);
      jump(End, L);
      label(Wrap, L);
      ReverseRange(First, Last);
      jump(EmptyOrSingle, L);
      label(EmptyOrSingle, L);
      assign(Result, boolean(false, L), L);
      jump(End, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::AlgorithmIsPermutation: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Second = snapshot(expression(Call->getArg(2)), L);
      std::optional<unsigned> PredicateIndex;
      if (Call->getNumArgs() == 4 &&
          Call->getArg(3)->getType()->isFunctionPointerType())
        PredicateIndex = 3;
      else if (Call->getNumArgs() == 5)
        PredicateIndex = 4;
      std::optional<Expression> ExplicitSecondLast;
      if ((Call->getNumArgs() == 4 && !PredicateIndex) ||
          Call->getNumArgs() == 5)
        ExplicitSecondLast = snapshot(expression(Call->getArg(3)), L);
      std::optional<Expression> Predicate;
      if (PredicateIndex)
        Predicate = snapshot(expression(Call->getArg(*PredicateIndex)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto FirstType = type(Call->getArg(0)->getType(), L);
      const auto SecondType = type(Call->getArg(2)->getType(), L);
      auto Length = snapshot(binary("-", Last, First, DifferenceType, L), L);
      auto SecondLast =
          ExplicitSecondLast
              ? std::move(*ExplicitSecondLast)
              : snapshot(binary("+", Second, Length, SecondType, L), L);
      auto Current = snapshot(json::Object(First), L);
      auto Previous = temporary(FirstType, L);
      auto FirstScan = temporary(FirstType, L);
      auto SecondScan = temporary(SecondType, L);
      auto FirstCount = temporary(DifferenceType, L);
      auto SecondCount = temporary(DifferenceType, L);
      auto Result = temporary("bool", L);
      const auto OuterCheck = labelName(), BeginDuplicateSearch = labelName();
      const auto DuplicateCheck = labelName(), ComparePrevious = labelName();
      const auto AdvancePrevious = labelName(), BeginFirstCount = labelName();
      const auto FirstScanCheck = labelName(), CompareFirst = labelName();
      const auto IncrementFirst = labelName(), AdvanceFirstScan = labelName();
      const auto BeginSecondCount = labelName(), SecondScanCheck = labelName();
      const auto CompareSecond = labelName(), IncrementSecond = labelName();
      const auto AdvanceSecondScan = labelName(), CompareCounts = labelName();
      const auto AdvanceCurrent = labelName(), TrueResult = labelName();
      const auto FalseResult = labelName(), End = labelName();
      if (ExplicitSecondLast) {
        branch(binary("==", Length,
                      binary("-", SecondLast, Second, DifferenceType, L),
                      "bool", L),
               OuterCheck, FalseResult, L);
      } else {
        jump(OuterCheck, L);
      }
      label(OuterCheck, L);
      branch(binary("!=", Current, Last, "bool", L), BeginDuplicateSearch,
             TrueResult, L);
      label(BeginDuplicateSearch, L);
      assign(Previous, First, L);
      jump(DuplicateCheck, L);
      label(DuplicateCheck, L);
      branch(binary("!=", Previous, Current, "bool", L), ComparePrevious,
             BeginFirstCount, L);
      label(ComparePrevious, L);
      branch(Predicate
                 ? emitBinaryPredicate(json::Object(*Predicate),
                                       Call->getArg(*PredicateIndex)->getType(),
                                       dereference(json::Object(Previous), L),
                                       dereference(json::Object(Current), L), L)
                 : binary("==", dereference(Previous, L),
                          dereference(Current, L), "bool", L),
             AdvanceCurrent, AdvancePrevious, L);
      label(AdvancePrevious, L);
      assign(
          Previous,
          binary("+", Previous, quantity(1, DifferenceType, L), FirstType, L),
          L);
      jump(DuplicateCheck, L);
      label(BeginFirstCount, L);
      assign(FirstCount, quantity(0, DifferenceType, L), L);
      assign(FirstScan, Current, L);
      jump(FirstScanCheck, L);
      label(FirstScanCheck, L);
      branch(binary("!=", FirstScan, Last, "bool", L), CompareFirst,
             BeginSecondCount, L);
      label(CompareFirst, L);
      branch(Predicate
                 ? emitBinaryPredicate(json::Object(*Predicate),
                                       Call->getArg(*PredicateIndex)->getType(),
                                       dereference(json::Object(FirstScan), L),
                                       dereference(json::Object(Current), L), L)
                 : binary("==", dereference(FirstScan, L),
                          dereference(Current, L), "bool", L),
             IncrementFirst, AdvanceFirstScan, L);
      label(IncrementFirst, L);
      assign(FirstCount,
             binary("+", FirstCount, quantity(1, DifferenceType, L),
                    DifferenceType, L),
             L);
      jump(AdvanceFirstScan, L);
      label(AdvanceFirstScan, L);
      assign(
          FirstScan,
          binary("+", FirstScan, quantity(1, DifferenceType, L), FirstType, L),
          L);
      jump(FirstScanCheck, L);
      label(BeginSecondCount, L);
      assign(SecondCount, quantity(0, DifferenceType, L), L);
      assign(SecondScan, Second, L);
      jump(SecondScanCheck, L);
      label(SecondScanCheck, L);
      branch(binary("!=", SecondScan, SecondLast, "bool", L), CompareSecond,
             CompareCounts, L);
      label(CompareSecond, L);
      branch(Predicate ? emitBinaryPredicate(
                             json::Object(*Predicate),
                             Call->getArg(*PredicateIndex)->getType(),
                             dereference(json::Object(Current), L),
                             dereference(json::Object(SecondScan), L), L)
                       : AlgorithmEqual(dereference(Current, L), 0,
                                        dereference(SecondScan, L), 2),
             IncrementSecond, AdvanceSecondScan, L);
      label(IncrementSecond, L);
      assign(SecondCount,
             binary("+", SecondCount, quantity(1, DifferenceType, L),
                    DifferenceType, L),
             L);
      jump(AdvanceSecondScan, L);
      label(AdvanceSecondScan, L);
      assign(SecondScan,
             binary("+", SecondScan, quantity(1, DifferenceType, L), SecondType,
                    L),
             L);
      jump(SecondScanCheck, L);
      label(CompareCounts, L);
      branch(binary("==", FirstCount, SecondCount, "bool", L), AdvanceCurrent,
             FalseResult, L);
      label(AdvanceCurrent, L);
      assign(Current,
             binary("+", Current, quantity(1, DifferenceType, L), FirstType, L),
             L);
      jump(OuterCheck, L);
      label(TrueResult, L);
      assign(Result, boolean(true, L), L);
      jump(End, L);
      label(FalseResult, L);
      assign(Result, boolean(false, L), L);
      jump(End, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::AlgorithmFindIf:
    case UtilityOperation::AlgorithmFindIfNot: {
      const bool Match = Operation == UtilityOperation::AlgorithmFindIf;
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Predicate = snapshot(expression(Call->getArg(2)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto Check = labelName(), Test = labelName();
      const auto Advance = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Test, End, L);
      label(Test, L);
      auto Selected = emitUnaryPredicate(
          json::Object(Predicate), Call->getArg(2)->getType(),
          dereference(json::Object(Current), L), L);
      branch(std::move(Selected), Match ? End : Advance, Match ? Advance : End,
             L);
      label(Advance, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      label(End, L);
      return Current;
    }
    case UtilityOperation::AlgorithmCountIf: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Predicate = snapshot(expression(Call->getArg(2)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto Count = temporary(DifferenceType, L);
      const auto Check = labelName(), Test = labelName();
      const auto Increment = labelName(), Advance = labelName();
      const auto End = labelName();
      assign(Count, quantity(0, DifferenceType, L), L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Test, End, L);
      label(Test, L);
      {
        auto Selected = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(2)->getType(),
            dereference(json::Object(Current), L), L);
        branch(std::move(Selected), Increment, Advance, L);
      }
      label(Increment, L);
      assign(
          Count,
          binary("+", Count, quantity(1, DifferenceType, L), DifferenceType, L),
          L);
      jump(Advance, L);
      label(Advance, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      label(End, L);
      return Count;
    }
    case UtilityOperation::AlgorithmAllOf:
    case UtilityOperation::AlgorithmAnyOf:
    case UtilityOperation::AlgorithmNoneOf: {
      const bool All = Operation == UtilityOperation::AlgorithmAllOf;
      const bool Any = Operation == UtilityOperation::AlgorithmAnyOf;
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Predicate = snapshot(expression(Call->getArg(2)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto Result = temporary("bool", L);
      const auto Check = labelName(), Test = labelName();
      const auto Decisive = labelName(), Advance = labelName();
      const auto End = labelName();
      assign(Result, boolean(!Any, L), L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Test, End, L);
      label(Test, L);
      {
        auto Selected = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(2)->getType(),
            dereference(json::Object(Current), L), L);
        branch(std::move(Selected), All ? Advance : Decisive,
               All ? Decisive : Advance, L);
      }
      label(Decisive, L);
      assign(Result, boolean(Any, L), L);
      jump(End, L);
      label(Advance, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::AlgorithmCopyIf:
    case UtilityOperation::AlgorithmRemoveCopyIf: {
      const bool CopyMatches = Operation == UtilityOperation::AlgorithmCopyIf;
      auto Input = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Output = snapshot(expression(Call->getArg(2)), L);
      auto Predicate = snapshot(expression(Call->getArg(3)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(2)->getType(), L);
      const auto OutputElementType =
          type(Call->getArg(2)->getType()->getPointeeType(), L);
      const auto Check = labelName(), Test = labelName();
      const auto Copy = labelName(), Advance = labelName();
      const auto End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Input, Last, "bool", L), Test, End, L);
      label(Test, L);
      {
        auto Selected = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(3)->getType(),
            dereference(json::Object(Input), L), L);
        branch(std::move(Selected), CopyMatches ? Copy : Advance,
               CopyMatches ? Advance : Copy, L);
      }
      label(Copy, L);
      assign(dereference(Output, L),
             cast(dereference(Input, L), OutputElementType, L), L);
      assign(Output,
             binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
             L);
      jump(Advance, L);
      label(Advance, L);
      assign(Input,
             binary("+", Input, quantity(1, DifferenceType, L), InputType, L),
             L);
      jump(Check, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::AlgorithmRemoveIf: {
      auto Input = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Predicate = snapshot(expression(Call->getArg(2)), L);
      auto Output = snapshot(json::Object(Input), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto Check = labelName(), Test = labelName();
      const auto Keep = labelName(), CheckMove = labelName();
      const auto Move = labelName(), AdvanceOutput = labelName();
      const auto AdvanceInput = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Input, Last, "bool", L), Test, End, L);
      label(Test, L);
      {
        auto Selected = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(2)->getType(),
            dereference(json::Object(Input), L), L);
        branch(std::move(Selected), AdvanceInput, Keep, L);
      }
      label(Keep, L);
      jump(CheckMove, L);
      label(CheckMove, L);
      branch(binary("!=", Input, Output, "bool", L), Move, AdvanceOutput, L);
      label(Move, L);
      assign(dereference(Output, L), dereference(Input, L), L);
      jump(AdvanceOutput, L);
      label(AdvanceOutput, L);
      assign(
          Output,
          binary("+", Output, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(AdvanceInput, L);
      label(AdvanceInput, L);
      assign(Input,
             binary("+", Input, quantity(1, DifferenceType, L), PointerType, L),
             L);
      jump(Check, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::AlgorithmReplaceIf: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Predicate = snapshot(expression(Call->getArg(2)), L);
      auto ValueAddress = snapshot(
          address(lvalue(Call->getArg(3)), Call->getArg(3)->getType(), L), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto ElementType =
          type(Call->getArg(0)->getType()->getPointeeType(), L);
      const auto Check = labelName(), Test = labelName();
      const auto Replace = labelName(), Advance = labelName();
      const auto End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Test, End, L);
      label(Test, L);
      {
        auto Selected = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(2)->getType(),
            dereference(json::Object(Current), L), L);
        branch(std::move(Selected), Replace, Advance, L);
      }
      label(Replace, L);
      assign(dereference(Current, L),
             cast(dereference(ValueAddress, L), ElementType, L), L);
      jump(Advance, L);
      label(Advance, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      label(End, L);
      return {};
    }
    case UtilityOperation::AlgorithmReplaceCopyIf: {
      auto Input = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Output = snapshot(expression(Call->getArg(2)), L);
      auto Predicate = snapshot(expression(Call->getArg(3)), L);
      auto ValueAddress = snapshot(
          address(lvalue(Call->getArg(4)), Call->getArg(4)->getType(), L), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto OutputType = type(Call->getArg(2)->getType(), L);
      const auto OutputElementType =
          type(Call->getArg(2)->getType()->getPointeeType(), L);
      const auto Check = labelName(), Test = labelName();
      const auto Replace = labelName(), Copy = labelName();
      const auto Advance = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Input, Last, "bool", L), Test, End, L);
      label(Test, L);
      {
        auto Selected = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(3)->getType(),
            dereference(json::Object(Input), L), L);
        branch(std::move(Selected), Replace, Copy, L);
      }
      label(Replace, L);
      assign(dereference(Output, L),
             cast(dereference(ValueAddress, L), OutputElementType, L), L);
      jump(Advance, L);
      label(Copy, L);
      assign(dereference(Output, L),
             cast(dereference(Input, L), OutputElementType, L), L);
      jump(Advance, L);
      label(Advance, L);
      assign(Input,
             binary("+", Input, quantity(1, DifferenceType, L), InputType, L),
             L);
      assign(Output,
             binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
             L);
      jump(Check, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::AlgorithmIsPartitioned: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Predicate = snapshot(expression(Call->getArg(2)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto Result = temporary("bool", L);
      const auto LeadingCheck = labelName(), LeadingTest = labelName();
      const auto LeadingAdvance = labelName(), BeginTail = labelName();
      const auto TailCheck = labelName(), TailTest = labelName();
      const auto TailAdvance = labelName(), False = labelName();
      const auto End = labelName();
      assign(Result, boolean(true, L), L);
      jump(LeadingCheck, L);
      label(LeadingCheck, L);
      branch(binary("!=", Current, Last, "bool", L), LeadingTest, End, L);
      label(LeadingTest, L);
      {
        auto Selected = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(2)->getType(),
            dereference(json::Object(Current), L), L);
        branch(std::move(Selected), LeadingAdvance, BeginTail, L);
      }
      label(LeadingAdvance, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(LeadingCheck, L);
      label(BeginTail, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(TailCheck, L);
      label(TailCheck, L);
      branch(binary("!=", Current, Last, "bool", L), TailTest, End, L);
      label(TailTest, L);
      {
        auto Selected = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(2)->getType(),
            dereference(json::Object(Current), L), L);
        branch(std::move(Selected), False, TailAdvance, L);
      }
      label(TailAdvance, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(TailCheck, L);
      label(False, L);
      assign(Result, boolean(false, L), L);
      jump(End, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::AlgorithmPartition: {
      auto Boundary = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Predicate = snapshot(expression(Call->getArg(2)), L);
      auto Scan = snapshot(json::Object(Boundary), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto FindCheck = labelName(), FindTest = labelName();
      const auto AdvanceBoundary = labelName(), BeginScan = labelName();
      const auto ScanCheck = labelName(), ScanTest = labelName();
      const auto Swap = labelName(), AdvanceScan = labelName();
      const auto End = labelName();
      jump(FindCheck, L);
      label(FindCheck, L);
      branch(binary("!=", Boundary, Last, "bool", L), FindTest, End, L);
      label(FindTest, L);
      {
        auto Selected = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(2)->getType(),
            dereference(json::Object(Boundary), L), L);
        branch(std::move(Selected), AdvanceBoundary, BeginScan, L);
      }
      label(AdvanceBoundary, L);
      assign(
          Boundary,
          binary("+", Boundary, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(FindCheck, L);
      label(BeginScan, L);
      assign(
          Scan,
          binary("+", Boundary, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(ScanCheck, L);
      label(ScanCheck, L);
      branch(binary("!=", Scan, Last, "bool", L), ScanTest, End, L);
      label(ScanTest, L);
      {
        auto Selected = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(2)->getType(),
            dereference(json::Object(Scan), L), L);
        branch(std::move(Selected), Swap, AdvanceScan, L);
      }
      label(Swap, L);
      {
        auto BoundaryValue =
            snapshot(dereference(json::Object(Boundary), L), L);
        auto ScanValue = snapshot(dereference(json::Object(Scan), L), L);
        assign(dereference(Boundary, L), std::move(ScanValue), L);
        assign(dereference(Scan, L), std::move(BoundaryValue), L);
      }
      assign(
          Boundary,
          binary("+", Boundary, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(AdvanceScan, L);
      label(AdvanceScan, L);
      assign(Scan,
             binary("+", Scan, quantity(1, DifferenceType, L), PointerType, L),
             L);
      jump(ScanCheck, L);
      label(End, L);
      return Boundary;
    }
    case UtilityOperation::AlgorithmStablePartition: {
      auto Boundary = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Predicate = snapshot(expression(Call->getArg(2)), L);
      auto Current = snapshot(json::Object(Boundary), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto ElementType =
          type(Call->getArg(0)->getType()->getPointeeType(), L);
      auto Shift = temporary(PointerType, L);
      auto Previous = temporary(PointerType, L);
      auto Value = temporary(ElementType, L);
      const auto Check = labelName(), Test = labelName();
      const auto Selected = labelName(), CheckMove = labelName();
      const auto Save = labelName(), CheckShift = labelName();
      const auto ShiftOne = labelName(), Place = labelName();
      const auto AdvanceBoundary = labelName();
      const auto AdvanceCurrent = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Test, End, L);
      label(Test, L);
      {
        auto Matches = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(2)->getType(),
            dereference(json::Object(Current), L), L);
        branch(std::move(Matches), Selected, AdvanceCurrent, L);
      }
      label(Selected, L);
      jump(CheckMove, L);
      label(CheckMove, L);
      branch(binary("!=", Current, Boundary, "bool", L), Save, AdvanceBoundary,
             L);
      label(Save, L);
      assign(Value, dereference(json::Object(Current), L), L);
      assign(Shift, Current, L);
      jump(CheckShift, L);
      label(CheckShift, L);
      branch(binary("!=", Shift, Boundary, "bool", L), ShiftOne, Place, L);
      label(ShiftOne, L);
      assign(Previous,
             binary("-", Shift, quantity(1, DifferenceType, L), PointerType, L),
             L);
      assign(dereference(json::Object(Shift), L),
             dereference(json::Object(Previous), L), L);
      assign(Shift, Previous, L);
      jump(CheckShift, L);
      label(Place, L);
      assign(dereference(json::Object(Boundary), L), Value, L);
      jump(AdvanceBoundary, L);
      label(AdvanceBoundary, L);
      assign(
          Boundary,
          binary("+", Boundary, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(AdvanceCurrent, L);
      label(AdvanceCurrent, L);
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      label(End, L);
      return Boundary;
    }
    case UtilityOperation::AlgorithmPartitionCopy: {
      auto Input = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto TrueOutput = snapshot(expression(Call->getArg(2)), L);
      auto FalseOutput = snapshot(expression(Call->getArg(3)), L);
      auto Predicate = snapshot(expression(Call->getArg(4)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto InputType = type(Call->getArg(0)->getType(), L);
      const auto TrueOutputType = type(Call->getArg(2)->getType(), L);
      const auto FalseOutputType = type(Call->getArg(3)->getType(), L);
      const auto TrueOutputElementType =
          type(Call->getArg(2)->getType()->getPointeeType(), L);
      const auto FalseOutputElementType =
          type(Call->getArg(3)->getType()->getPointeeType(), L);
      const auto Check = labelName(), Test = labelName();
      const auto CopyTrue = labelName(), CopyFalse = labelName();
      const auto Advance = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Input, Last, "bool", L), Test, End, L);
      label(Test, L);
      {
        auto Selected = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(4)->getType(),
            dereference(json::Object(Input), L), L);
        branch(std::move(Selected), CopyTrue, CopyFalse, L);
      }
      label(CopyTrue, L);
      assign(dereference(TrueOutput, L),
             cast(dereference(Input, L), TrueOutputElementType, L), L);
      assign(TrueOutput,
             binary("+", TrueOutput, quantity(1, DifferenceType, L),
                    TrueOutputType, L),
             L);
      jump(Advance, L);
      label(CopyFalse, L);
      assign(dereference(FalseOutput, L),
             cast(dereference(Input, L), FalseOutputElementType, L), L);
      assign(FalseOutput,
             binary("+", FalseOutput, quantity(1, DifferenceType, L),
                    FalseOutputType, L),
             L);
      jump(Advance, L);
      label(Advance, L);
      assign(Input,
             binary("+", Input, quantity(1, DifferenceType, L), InputType, L),
             L);
      jump(Check, L);
      label(End, L);
      auto Pair = approvedUtilityPairRecord(
          A.S, A.Sources, Call->getType()->getAsCXXRecordDecl(), A.Context);
      if (!Pair)
        reject(L, "algorithm partition_copy",
               "The selected std::pair layout is unavailable.");
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(Call->getType(), L);
      if (Place.getString("type") != type(Call->getType(), L))
        reject(L, "algorithm partition_copy",
               "The std::partition_copy destination type differs from its "
               "result.");
      assign(fieldStorage(json::Object(Place), Pair->First, L), TrueOutput, L);
      assign(fieldStorage(json::Object(Place), Pair->Second, L), FalseOutput,
             L);
      return Place;
    }
    case UtilityOperation::AlgorithmPartitionPoint: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Predicate = snapshot(expression(Call->getArg(2)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      auto Length = temporary(DifferenceType, L);
      auto Half = temporary(DifferenceType, L);
      auto Middle = temporary(PointerType, L);
      const auto Check = labelName(), Test = labelName();
      const auto Advance = labelName(), Narrow = labelName();
      const auto End = labelName();
      assign(Length, binary("-", Last, First, DifferenceType, L), L);
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Length, quantity(0, DifferenceType, L), "bool", L),
             Test, End, L);
      label(Test, L);
      assign(Half,
             binary("/", Length, quantity(2, DifferenceType, L), DifferenceType,
                    L),
             L);
      assign(Middle, binary("+", First, Half, PointerType, L), L);
      {
        auto Selected = emitUnaryPredicate(
            json::Object(Predicate), Call->getArg(2)->getType(),
            dereference(json::Object(Middle), L), L);
        branch(std::move(Selected), Advance, Narrow, L);
      }
      label(Advance, L);
      assign(
          First,
          binary("+", Middle, quantity(1, DifferenceType, L), PointerType, L),
          L);
      assign(Length,
             binary("-", binary("-", Length, Half, DifferenceType, L),
                    quantity(1, DifferenceType, L), DifferenceType, L),
             L);
      jump(Check, L);
      label(Narrow, L);
      assign(Length, Half, L);
      jump(Check, L);
      label(End, L);
      return First;
    }
    case UtilityOperation::AlgorithmForEach: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Callback = snapshot(expression(Call->getArg(2)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto Check = labelName(), Invoke = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", Current, Last, "bool", L), Invoke, End, L);
      label(Invoke, L);
      {
        json::Array Arguments;
        Arguments.push_back(dereference(json::Object(Current), L));
        emitAlgorithmCallback(json::Object(Callback),
                              Call->getArg(2)->getType(), std::move(Arguments),
                              L);
      }
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      jump(Check, L);
      label(End, L);
      return Callback;
    }
    case UtilityOperation::AlgorithmForEachN: {
      auto Current = snapshot(expression(Call->getArg(0)), L);
      auto CountType = Call->getArg(1)->getType();
      if (const auto *Enumeration = CountType->getAs<EnumType>())
        CountType = Enumeration->getDecl()->getPromotionType();
      else if (A.Context.isPromotableIntegerType(CountType))
        CountType = A.Context.getPromotedIntegerType(CountType);
      const auto CountTypeName = type(CountType, L);
      auto Remaining =
          snapshot(cast(expression(Call->getArg(1)), CountTypeName, L), L);
      auto Callback = snapshot(expression(Call->getArg(2)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto Check = labelName(), Invoke = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary(">", Remaining, quantity(0, CountTypeName, L), "bool", L),
             Invoke, End, L);
      label(Invoke, L);
      {
        json::Array Arguments;
        Arguments.push_back(dereference(json::Object(Current), L));
        emitAlgorithmCallback(json::Object(Callback),
                              Call->getArg(2)->getType(), std::move(Arguments),
                              L);
      }
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      assign(Remaining,
             binary("-", Remaining, quantity(1, CountTypeName, L),
                    CountTypeName, L),
             L);
      jump(Check, L);
      label(End, L);
      return Current;
    }
    case UtilityOperation::AlgorithmTransformUnary:
    case UtilityOperation::AlgorithmTransformBinary: {
      const bool Binary =
          Operation == UtilityOperation::AlgorithmTransformBinary;
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      std::optional<Expression> Second;
      if (Binary)
        Second = snapshot(expression(Call->getArg(2)), L);
      const unsigned OutputIndex = Binary ? 3 : 2;
      const unsigned CallbackIndex = Binary ? 4 : 3;
      auto Output = snapshot(expression(Call->getArg(OutputIndex)), L);
      auto Callback = snapshot(expression(Call->getArg(CallbackIndex)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto FirstType = type(Call->getArg(0)->getType(), L);
      const auto SecondType =
          Binary ? type(Call->getArg(2)->getType(), L) : std::string();
      const auto OutputType = type(Call->getArg(OutputIndex)->getType(), L);
      const auto Check = labelName(), Invoke = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(binary("!=", First, Last, "bool", L), Invoke, End, L);
      label(Invoke, L);
      {
        json::Array Arguments;
        Arguments.push_back(dereference(json::Object(First), L));
        if (Second)
          Arguments.push_back(dereference(json::Object(*Second), L));
        auto Value = emitAlgorithmCallback(
            json::Object(Callback), Call->getArg(CallbackIndex)->getType(),
            std::move(Arguments), L);
        const auto ElementType =
            type(Call->getArg(OutputIndex)->getType()->getPointeeType(), L);
        assign(dereference(Output, L), cast(std::move(Value), ElementType, L),
               L);
      }
      assign(First,
             binary("+", First, quantity(1, DifferenceType, L), FirstType, L),
             L);
      if (Second)
        assign(
            *Second,
            binary("+", *Second, quantity(1, DifferenceType, L), SecondType, L),
            L);
      assign(Output,
             binary("+", Output, quantity(1, DifferenceType, L), OutputType, L),
             L);
      jump(Check, L);
      label(End, L);
      return Output;
    }
    case UtilityOperation::AlgorithmGenerate:
    case UtilityOperation::AlgorithmGenerateN: {
      const bool Counted = Operation == UtilityOperation::AlgorithmGenerateN;
      auto Current = snapshot(expression(Call->getArg(0)), L);
      std::optional<Expression> Last;
      std::optional<Expression> Remaining;
      std::string CountTypeName;
      if (Counted) {
        auto CountType = Call->getArg(1)->getType();
        if (const auto *Enumeration = CountType->getAs<EnumType>())
          CountType = Enumeration->getDecl()->getPromotionType();
        else if (A.Context.isPromotableIntegerType(CountType))
          CountType = A.Context.getPromotedIntegerType(CountType);
        CountTypeName = type(CountType, L);
        Remaining =
            snapshot(cast(expression(Call->getArg(1)), CountTypeName, L), L);
      } else {
        Last = snapshot(expression(Call->getArg(1)), L);
      }
      auto Callback = snapshot(expression(Call->getArg(2)), L);
      const auto DifferenceType = type(A.Context.getPointerDiffType(), L);
      const auto PointerType = type(Call->getArg(0)->getType(), L);
      const auto Check = labelName(), Invoke = labelName(), End = labelName();
      jump(Check, L);
      label(Check, L);
      branch(Counted ? binary(">", *Remaining, quantity(0, CountTypeName, L),
                              "bool", L)
                     : binary("!=", Current, *Last, "bool", L),
             Invoke, End, L);
      label(Invoke, L);
      {
        json::Array Arguments;
        auto Value = emitAlgorithmCallback(json::Object(Callback),
                                           Call->getArg(2)->getType(),
                                           std::move(Arguments), L);
        const auto ElementType =
            type(Call->getArg(0)->getType()->getPointeeType(), L);
        assign(dereference(Current, L), cast(std::move(Value), ElementType, L),
               L);
      }
      assign(
          Current,
          binary("+", Current, quantity(1, DifferenceType, L), PointerType, L),
          L);
      if (Remaining)
        assign(*Remaining,
               binary("-", *Remaining, quantity(1, CountTypeName, L),
                      CountTypeName, L),
               L);
      jump(Check, L);
      label(End, L);
      return Counted ? Current : Expression{};
    }
    case UtilityOperation::MakeOptional: {
      auto Optional = OptionalFor(Call->getType());
      if (!Optional)
        reject(L, "utility make_optional",
               "The selected std::optional layout is unavailable.");
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(Call->getType(), L);
      if (Place.getString("type") != type(Call->getType(), L))
        reject(
            L, "utility make_optional",
            "The std::make_optional destination type differs from its result.");
      auto Value = fieldStorage(json::Object(Place), Optional->Value, L);
      if (Call->getNumArgs()) {
        if (recordValue(Optional->Value->getType()))
          initialize(std::move(Value), Call->getArg(0), L);
        else
          assign(std::move(Value),
                 cast(expression(Call->getArg(0)),
                      type(Optional->Value->getType(), L), L),
                 L);
      } else {
        initializeZero(std::move(Value), Optional->Value->getType(), L);
      }
      assign(fieldStorage(json::Object(Place), Optional->Engaged, L),
             boolean(true, L), L);
      return Place;
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
    case UtilityOperation::MakeTuple: {
      auto Tuple = TupleFor(Call->getType());
      if (!Tuple)
        reject(L, "utility make_tuple",
               "The selected std::tuple layout is unavailable.");
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(Call->getType(), L);
      if (Place.getString("type") != type(Call->getType(), L))
        reject(L, "utility make_tuple",
               "The std::make_tuple destination type differs from its result.");
      if (Call->getNumArgs() != Tuple->Elements.size())
        reject(L, "utility make_tuple",
               "The std::make_tuple element count differs from its result.");
      for (unsigned I = 0; I < Tuple->Elements.size(); ++I)
        initialize(fieldStorage(json::Object(Place), Tuple->Elements[I], L),
                   Call->getArg(I), L);
      return Place;
    }
    case UtilityOperation::TupleCat: {
      auto Cat = approvedUtilityTupleCatCall(A.S, A.Sources, Call, A.Context);
      if (!Cat)
        reject(L, "utility tuple cat",
               "The selected std::tuple_cat operation is unavailable.");
      auto Place = Destination ? std::move(*Destination)
                               : objectTemporary(Call->getType(), L);
      if (Place.getString("type") != type(Call->getType(), L) ||
          Cat->Sources.size() != Call->getNumArgs())
        reject(L, "utility tuple cat",
               "The std::tuple_cat destination differs from its result.");

      std::vector<Expression> Sources;
      Sources.reserve(Call->getNumArgs());
      for (unsigned I = 0; I < Call->getNumArgs(); ++I) {
        auto Pointer = snapshot(
            address(lvalue(Call->getArg(I)), Call->getArg(I)->getType(), L), L);
        Sources.push_back(dereference(std::move(Pointer), L));
      }

      const auto SizeType = type(A.Context.getSizeType(), L);
      unsigned ResultIndex = 0;
      for (unsigned I = 0; I < Cat->Sources.size(); ++I) {
        const auto &Source = Cat->Sources[I];
        if (Source.ArrayElements) {
          if (!Source.ArraySize)
            continue;
          auto Storage =
              fieldStorage(json::Object(Sources[I]), Source.ArrayElements, L);
          auto Pointer = decay(std::move(Storage),
                               type(A.Context.getPointerType(
                                        Source.ArrayElementType.withConst()),
                                    L),
                               L);
          for (uint64_t N = 0; N < Source.ArraySize; ++N)
            assign(fieldStorage(json::Object(Place),
                                Cat->Result.Elements[ResultIndex++], L),
                   index(json::Object(Pointer), quantity(N, SizeType, L),
                         type(Source.ArrayElementType, L), L),
                   L);
          continue;
        }
        for (const auto *Element : Source.Elements)
          assign(fieldStorage(json::Object(Place),
                              Cat->Result.Elements[ResultIndex++], L),
                 fieldStorage(json::Object(Sources[I]), Element, L), L);
      }
      if (ResultIndex != Cat->Result.Elements.size())
        reject(L, "utility tuple cat",
               "The std::tuple_cat element count differs from its result.");
      return Place;
    }
    case UtilityOperation::TupleApply: {
      auto Tuple = TupleFor(Call->getArg(1)->getType());
      auto CallableType = Call->getArg(0)->getType();
      const auto *Prototype =
          CallableType->isFunctionType()
              ? CallableType->getAs<FunctionProtoType>()
          : CallableType->isFunctionPointerType()
              ? CallableType->getPointeeType()->getAs<FunctionProtoType>()
              : nullptr;
      if (!Tuple || !Prototype || Prototype->isVariadic() ||
          Prototype->getNumParams() != Tuple->Elements.size())
        reject(L, "utility tuple apply",
               "The selected std::tuple callback is unavailable.");
      auto Callable = snapshot(CallableType->isFunctionType()
                                   ? functionValue(Call->getArg(0))
                                   : expression(Call->getArg(0)),
                               L);
      auto TupleAddress = snapshot(
          address(lvalue(Call->getArg(1)), Call->getArg(1)->getType(), L), L);
      auto TupleValue = dereference(std::move(TupleAddress), L);
      json::Array Arguments;
      for (unsigned I = 0; I < Tuple->Elements.size(); ++I)
        Arguments.push_back(
            cast(fieldStorage(json::Object(TupleValue), Tuple->Elements[I], L),
                 type(Prototype->getParamType(I), L), L));
      return emitIndirectCall(std::move(Callable), std::move(Arguments),
                              Prototype->getReturnType(), L);
    }
    case UtilityOperation::TupleSwap:
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
    case UtilityOperation::TupleMemberSwap:
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
    case UtilityOperation::TupleGet: {
      auto Tuple = TupleFor(Call->getArg(0)->getType());
      const auto *Function = Call->getDirectCallee();
      const auto *Arguments =
          Function ? Function->getTemplateSpecializationArgs() : nullptr;
      if (!Tuple || !Arguments || Arguments->size() != 2 ||
          Arguments->get(1).getKind() != TemplateArgument::Pack ||
          Arguments->get(1).pack_size() != Tuple->Elements.size())
        reject(L, "utility tuple get",
               "The selected std::tuple element is unavailable.");
      std::optional<uint64_t> Index;
      if (Arguments->get(0).getKind() == TemplateArgument::Integral &&
          !Arguments->get(0).getAsIntegral().isNegative()) {
        const uint64_t Candidate =
            Arguments->get(0).getAsIntegral().getLimitedValue(
                Tuple->Elements.size());
        if (Candidate < Tuple->Elements.size())
          Index = Candidate;
      } else if (Arguments->get(0).getKind() == TemplateArgument::Type) {
        for (unsigned I = 0; I < Tuple->Elements.size(); ++I)
          if (A.Context.hasSameType(Arguments->get(0).getAsType(),
                                    Tuple->Elements[I]->getType())) {
            if (Index)
              reject(L, "utility tuple get",
                     "The selected std::tuple type is ambiguous.");
            Index = I;
          }
      }
      if (!Index)
        reject(L, "utility tuple get",
               "The selected std::tuple element is unavailable.");
      return fieldStorage(lvalue(Call->getArg(0)), Tuple->Elements[*Index], L);
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
      const auto LeftPair = approvedUtilityPairRecord(
          A.S, A.Sources, Call->getArg(0)->getType()->getAsCXXRecordDecl(),
          A.Context);
      const auto RightPair = approvedUtilityPairRecord(
          A.S, A.Sources, Call->getArg(1)->getType()->getAsCXXRecordDecl(),
          A.Context);
      if (!LeftPair || !RightPair)
        reject(L, "utility pair comparison",
               "A selected std::pair layout is unavailable.");
      auto LeftAddress = snapshot(
          address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
      auto RightAddress = snapshot(
          address(lvalue(Call->getArg(1)), Call->getArg(1)->getType(), L), L);
      auto Left = dereference(std::move(LeftAddress), L);
      auto Right = dereference(std::move(RightAddress), L);
      return CompareUtilityValues(UtilityComparisonOperator(Operation), Left,
                                  A.Context.getRecordType(LeftPair->Record),
                                  Right,
                                  A.Context.getRecordType(RightPair->Record));
    }
    case UtilityOperation::TupleEqual:
    case UtilityOperation::TupleNotEqual:
    case UtilityOperation::TupleLess:
    case UtilityOperation::TupleGreater:
    case UtilityOperation::TupleLessEqual:
    case UtilityOperation::TupleGreaterEqual: {
      const auto LeftTuple = TupleFor(Call->getArg(0)->getType());
      const auto RightTuple = TupleFor(Call->getArg(1)->getType());
      if (!LeftTuple || !RightTuple ||
          LeftTuple->Elements.size() != RightTuple->Elements.size())
        reject(L, "utility tuple comparison",
               "A selected std::tuple layout is unavailable.");
      auto LeftAddress = snapshot(
          address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
      auto RightAddress = snapshot(
          address(lvalue(Call->getArg(1)), Call->getArg(1)->getType(), L), L);
      auto Left = dereference(std::move(LeftAddress), L);
      auto Right = dereference(std::move(RightAddress), L);
      return CompareUtilityValues(UtilityComparisonOperator(Operation), Left,
                                  A.Context.getRecordType(LeftTuple->Record),
                                  Right,
                                  A.Context.getRecordType(RightTuple->Record));
    }
    case UtilityOperation::IteratorBegin:
    case UtilityOperation::IteratorEnd:
    case UtilityOperation::IteratorSize:
    case UtilityOperation::IteratorEmpty:
    case UtilityOperation::IteratorData: {
      const auto *Source = Call->getArg(0);
      const auto *Native = A.Context.getAsConstantArrayType(Source->getType());
      auto Array = ArrayFor(Source->getType());
      if (!Native && !Array)
        reject(L, "iterator range access",
               "The selected fixed range layout is unavailable.");
      const uint64_t Count =
          Native ? Native->getSize().getLimitedValue() : Array->Size;
      if (Operation == UtilityOperation::IteratorSize ||
          Operation == UtilityOperation::IteratorEmpty) {
        // Capacity queries still evaluate the range expression exactly once.
        lvalue(Source);
        if (Operation == UtilityOperation::IteratorEmpty)
          return boolean(!Count, L);
        return quantity(Count, type(Call->getType(), L), L);
      }
      if (Array && !Array->Size) {
        lvalue(Source);
        return A.zero(Call->getType(), L);
      }
      auto Base = lvalue(Source);
      auto Storage = Native ? std::move(Base)
                            : fieldStorage(std::move(Base), Array->Elements, L);
      auto Pointer = decay(std::move(Storage), type(Call->getType(), L), L);
      if (Operation != UtilityOperation::IteratorEnd)
        return Pointer;
      return binary("+", std::move(Pointer),
                    quantity(Count, type(A.Context.getSizeType(), L), L),
                    type(Call->getType(), L), L);
    }
    case UtilityOperation::IteratorAdvance: {
      // Bind the iterator object before evaluating the distance, choosing the
      // left-to-right argument order permitted by C++17.
      auto IteratorType = Call->getArg(0)->getType();
      auto IteratorAddress =
          snapshot(address(lvalue(Call->getArg(0)), IteratorType, L), L);
      const auto *Distance = Call->getArg(1);
      if (const auto *Default = dyn_cast<CXXDefaultArgExpr>(Distance))
        Distance = selectedDefaultArgument(Default, A.Context);
      if (!Distance)
        reject(L, "iterator advance",
               "The selected iterator distance is unavailable.");
      auto Offset = snapshot(cast(expression(Distance),
                                  type(A.Context.getPointerDiffType(), L), L),
                             L);
      auto Iterator = dereference(std::move(IteratorAddress), L);
      auto Reverse = ReverseFor(IteratorType);
      if (Reverse) {
        auto Current = ReverseCurrent(std::move(Iterator), *Reverse);
        auto Value = snapshot(Current, L);
        assign(Current,
               binary("-", std::move(Value), std::move(Offset),
                      type(Reverse->IteratorType, L), L),
               L);
      } else {
        auto Current = snapshot(Iterator, L);
        assign(Iterator,
               binary("+", std::move(Current), std::move(Offset),
                      type(IteratorType, L), L),
               L);
      }
      return {};
    }
    case UtilityOperation::IteratorDistance: {
      auto First = snapshot(expression(Call->getArg(0)), L);
      auto Last = snapshot(expression(Call->getArg(1)), L);
      auto Reverse = ReverseFor(Call->getArg(0)->getType());
      if (Reverse) {
        auto FirstCurrent =
            snapshot(ReverseCurrent(std::move(First), *Reverse), L);
        auto LastCurrent =
            snapshot(ReverseCurrent(std::move(Last), *Reverse), L);
        return snapshot(binary("-", std::move(FirstCurrent),
                               std::move(LastCurrent), type(Call->getType(), L),
                               L),
                        L);
      }
      return snapshot(binary("-", std::move(Last), std::move(First),
                             type(Call->getType(), L), L),
                      L);
    }
    case UtilityOperation::IteratorNext:
    case UtilityOperation::IteratorPrev: {
      auto Iterator = snapshot(expression(Call->getArg(0)), L);
      Expression Offset;
      if (Call->getNumArgs() == 1) {
        Offset = quantity(1, type(A.Context.getPointerDiffType(), L), L);
      } else {
        const auto *Distance = Call->getArg(1);
        if (const auto *Default = dyn_cast<CXXDefaultArgExpr>(Distance))
          Distance = selectedDefaultArgument(Default, A.Context);
        if (!Distance)
          reject(L, "iterator offset",
                 "The selected iterator distance is unavailable.");
        Offset = snapshot(cast(expression(Distance),
                               type(A.Context.getPointerDiffType(), L), L),
                          L);
      }
      auto Reverse = ReverseFor(Call->getArg(0)->getType());
      if (Reverse) {
        auto Current =
            snapshot(ReverseCurrent(std::move(Iterator), *Reverse), L);
        return ReverseValue(
            binary(Operation == UtilityOperation::IteratorNext ? "-" : "+",
                   std::move(Current), std::move(Offset),
                   type(Reverse->IteratorType, L), L),
            *Reverse);
      }
      return snapshot(
          binary(Operation == UtilityOperation::IteratorNext ? "+" : "-",
                 std::move(Iterator), std::move(Offset),
                 type(Call->getType(), L), L),
          L);
    }
    case UtilityOperation::IteratorRBegin:
    case UtilityOperation::IteratorREnd:
    case UtilityOperation::ArrayRBegin:
    case UtilityOperation::ArrayREnd: {
      const bool Member = Operation == UtilityOperation::ArrayRBegin ||
                          Operation == UtilityOperation::ArrayREnd;
      const auto *Source = Member ? MemberObject() : Call->getArg(0);
      const auto *Native =
          Source ? A.Context.getAsConstantArrayType(Source->getType())
                 : nullptr;
      auto Array = Source ? ArrayFor(Source->getType())
                          : std::optional<UtilityArrayRecord>();
      auto Reverse = ReverseFor(Call->getType());
      if (!Source || (!Native && !Array) || !Reverse)
        reject(L, "reverse iterator range access",
               "The selected fixed range or reverse iterator layout is "
               "unavailable.");
      const uint64_t Count =
          Native ? Native->getSize().getLimitedValue() : Array->Size;
      if (Array && !Array->Size) {
        lvalue(Source);
        return ReverseValue(A.zero(Reverse->IteratorType, L), *Reverse);
      }
      auto Base = lvalue(Source);
      auto Storage = Native ? std::move(Base)
                            : fieldStorage(std::move(Base), Array->Elements, L);
      auto Pointer =
          decay(std::move(Storage), type(Reverse->IteratorType, L), L);
      if (Operation == UtilityOperation::IteratorRBegin ||
          Operation == UtilityOperation::ArrayRBegin)
        Pointer = binary("+", std::move(Pointer),
                         quantity(Count, type(A.Context.getSizeType(), L), L),
                         type(Reverse->IteratorType, L), L);
      return ReverseValue(std::move(Pointer), *Reverse);
    }
    case UtilityOperation::MakeReverseIterator: {
      auto Reverse = ReverseFor(Call->getType());
      if (!Reverse)
        reject(L, "make reverse iterator",
               "The selected reverse iterator layout is unavailable.");
      return ReverseValue(expression(Call->getArg(0)), *Reverse);
    }
    case UtilityOperation::ReverseBase:
    case UtilityOperation::ReverseDereference:
    case UtilityOperation::ReverseArrow: {
      const auto *Object = MemberObject();
      auto Reverse = Object ? ReverseFor(Object->getType())
                            : std::optional<UtilityReverseIteratorRecord>();
      if (!Object || !Reverse)
        reject(L, "reverse iterator access",
               "The selected reverse iterator layout is unavailable.");
      auto Current = snapshot(ReverseCurrent(lvalue(Object), *Reverse), L);
      if (Operation == UtilityOperation::ReverseBase)
        return Current;
      auto Previous =
          binary("-", std::move(Current),
                 quantity(1, type(A.Context.getPointerDiffType(), L), L),
                 type(Reverse->IteratorType, L), L);
      if (Operation == UtilityOperation::ReverseArrow)
        return snapshot(std::move(Previous), L);
      return dereference(std::move(Previous), L);
    }
    case UtilityOperation::ReversePreIncrement:
    case UtilityOperation::ReversePostIncrement:
    case UtilityOperation::ReversePreDecrement:
    case UtilityOperation::ReversePostDecrement: {
      const auto *Object = MemberObject();
      auto Reverse = Object ? ReverseFor(Object->getType())
                            : std::optional<UtilityReverseIteratorRecord>();
      if (!Object || !Reverse)
        reject(L, "reverse iterator update",
               "The selected reverse iterator layout is unavailable.");
      auto ObjectAddress =
          snapshot(address(lvalue(Object), Object->getType(), L), L);
      auto Stored = dereference(std::move(ObjectAddress), L);
      const bool Post = Operation == UtilityOperation::ReversePostIncrement ||
                        Operation == UtilityOperation::ReversePostDecrement;
      Expression Result;
      if (Post) {
        auto RecordType = A.Context.getRecordType(Reverse->Record);
        Result = Destination ? std::move(*Destination)
                             : objectTemporary(RecordType, L);
        Destination.reset();
        assign(Result, snapshot(Stored, L), L);
      }
      auto Current = ReverseCurrent(json::Object(Stored), *Reverse);
      auto Value = snapshot(Current, L);
      const bool Increment =
          Operation == UtilityOperation::ReversePreIncrement ||
          Operation == UtilityOperation::ReversePostIncrement;
      assign(Current,
             binary(Increment ? "-" : "+", std::move(Value),
                    quantity(1, type(A.Context.getPointerDiffType(), L), L),
                    type(Reverse->IteratorType, L), L),
             L);
      return Post ? std::move(Result) : std::move(Stored);
    }
    case UtilityOperation::ReverseAdd:
    case UtilityOperation::ReverseAddAssign:
    case UtilityOperation::ReverseSubtract:
    case UtilityOperation::ReverseSubtractAssign:
    case UtilityOperation::ReverseSubscript: {
      const auto *Object = MemberObject();
      auto Reverse = Object ? ReverseFor(Object->getType())
                            : std::optional<UtilityReverseIteratorRecord>();
      if (!Object || !Reverse || Call->getNumArgs() != 2)
        reject(
            L, "reverse iterator arithmetic",
            "The selected reverse iterator layout or offset is unavailable.");
      auto ObjectAddress =
          snapshot(address(lvalue(Object), Object->getType(), L), L);
      auto Offset = snapshot(cast(expression(Call->getArg(1)),
                                  type(A.Context.getPointerDiffType(), L), L),
                             L);
      auto Stored = dereference(std::move(ObjectAddress), L);
      auto Current = ReverseCurrent(json::Object(Stored), *Reverse);
      auto Value = snapshot(Current, L);
      const bool Forward = Operation == UtilityOperation::ReverseAdd ||
                           Operation == UtilityOperation::ReverseAddAssign ||
                           Operation == UtilityOperation::ReverseSubscript;
      if (Operation == UtilityOperation::ReverseSubscript) {
        auto Position =
            binary("+", std::move(Offset),
                   quantity(1, type(A.Context.getPointerDiffType(), L), L),
                   type(A.Context.getPointerDiffType(), L), L);
        return dereference(binary("-", std::move(Value), std::move(Position),
                                  type(Reverse->IteratorType, L), L),
                           L);
      }
      auto Pointer =
          binary(Forward ? "-" : "+", std::move(Value), std::move(Offset),
                 type(Reverse->IteratorType, L), L);
      if (Operation == UtilityOperation::ReverseAddAssign ||
          Operation == UtilityOperation::ReverseSubtractAssign) {
        assign(Current, std::move(Pointer), L);
        return Stored;
      }
      return ReverseValue(std::move(Pointer), *Reverse);
    }
    case UtilityOperation::ReverseEqual:
    case UtilityOperation::ReverseNotEqual:
    case UtilityOperation::ReverseLess:
    case UtilityOperation::ReverseGreater:
    case UtilityOperation::ReverseLessEqual:
    case UtilityOperation::ReverseGreaterEqual:
    case UtilityOperation::ReverseDifference: {
      auto LeftReverse = ReverseFor(Call->getArg(0)->getType());
      auto RightReverse = ReverseFor(Call->getArg(1)->getType());
      if (!LeftReverse || !RightReverse)
        reject(L, "reverse iterator comparison",
               "The selected reverse iterator layouts are unavailable.");
      auto Left =
          snapshot(ReverseCurrent(lvalue(Call->getArg(0)), *LeftReverse), L);
      auto Right =
          snapshot(ReverseCurrent(lvalue(Call->getArg(1)), *RightReverse), L);
      if (Operation == UtilityOperation::ReverseDifference)
        return snapshot(binary("-", std::move(Right), std::move(Left),
                               type(Call->getType(), L), L),
                        L);
      const char *Operator = nullptr;
      switch (Operation) {
      case UtilityOperation::ReverseEqual:
        Operator = "==";
        break;
      case UtilityOperation::ReverseNotEqual:
        Operator = "!=";
        break;
      case UtilityOperation::ReverseLess:
        Operator = ">";
        break;
      case UtilityOperation::ReverseGreater:
        Operator = "<";
        break;
      case UtilityOperation::ReverseLessEqual:
        Operator = ">=";
        break;
      case UtilityOperation::ReverseGreaterEqual:
        Operator = "<=";
        break;
      default:
        reject(L, "reverse iterator comparison",
               "Unknown approved reverse iterator comparison.");
      }
      return snapshot(
          binary(Operator, std::move(Left), std::move(Right), "bool", L), L);
    }
    case UtilityOperation::ReverseAddLeft: {
      auto Offset = snapshot(cast(expression(Call->getArg(0)),
                                  type(A.Context.getPointerDiffType(), L), L),
                             L);
      auto Reverse = ReverseFor(Call->getArg(1)->getType());
      if (!Reverse)
        reject(L, "reverse iterator addition",
               "The selected reverse iterator layout is unavailable.");
      auto Current =
          snapshot(ReverseCurrent(lvalue(Call->getArg(1)), *Reverse), L);
      return ReverseValue(binary("-", std::move(Current), std::move(Offset),
                                 type(Reverse->IteratorType, L), L),
                          *Reverse);
    }
    case UtilityOperation::OptionalEqual:
    case UtilityOperation::OptionalNotEqual:
    case UtilityOperation::OptionalLess:
    case UtilityOperation::OptionalGreater:
    case UtilityOperation::OptionalLessEqual:
    case UtilityOperation::OptionalGreaterEqual: {
      const auto LeftOptional = OptionalFor(Call->getArg(0)->getType());
      const auto RightOptional = OptionalFor(Call->getArg(1)->getType());
      const bool LeftNullopt = approvedUtilityNulloptExpression(
          A.S, A.Sources, Call->getArg(0), A.Context);
      const bool RightNullopt = approvedUtilityNulloptExpression(
          A.S, A.Sources, Call->getArg(1), A.Context);
      if ((!LeftOptional && !LeftNullopt && !RightOptional) ||
          (!RightOptional && !RightNullopt && !LeftOptional) ||
          (!LeftOptional && !RightOptional))
        reject(L, "utility optional comparison",
               "The selected std::optional operands are unavailable.");
      const auto LeftValueType =
          LeftOptional ? LeftOptional->ElementType : Call->getArg(0)->getType();
      const auto RightValueType = RightOptional ? RightOptional->ElementType
                                                : Call->getArg(1)->getType();

      auto CaptureOptional = [&](const Expr *Source) {
        auto Address =
            snapshot(address(lvalue(Source), Source->getType(), L), L);
        return dereference(std::move(Address), L);
      };
      std::optional<Expression> LeftStored, RightStored;
      std::optional<Expression> LeftScalar, RightScalar;
      if (LeftOptional)
        LeftStored = CaptureOptional(Call->getArg(0));
      else if (!LeftNullopt)
        LeftScalar = snapshot(expression(Call->getArg(0)), L);
      if (RightOptional)
        RightStored = CaptureOptional(Call->getArg(1));
      else if (!RightNullopt)
        RightScalar = snapshot(expression(Call->getArg(1)), L);
      auto Engaged = [&](const UtilityOptionalRecord &Optional,
                         const Expression &Stored) {
        return fieldStorage(json::Object(Stored), Optional.Engaged, L);
      };
      auto Value = [&](const UtilityOptionalRecord &Optional,
                       const Expression &Stored) {
        return fieldStorage(json::Object(Stored), Optional.Value, L);
      };
      auto CompareValues = [&](llvm::StringRef Operator, Expression Left,
                               Expression Right) {
        return CompareUtilityValues(Operator, Left, LeftValueType, Right,
                                    RightValueType);
      };
      auto Negate = [&](Expression Result) {
        return snapshot(Expression{{"kind", "unary"},
                                   {"type", "bool"},
                                   {"operator", "!"},
                                   {"args", json::Array{std::move(Result)}},
                                   {"loc", A.loc(L)}},
                        L);
      };
      if (LeftOptional && RightOptional) {
        llvm::StringRef Operator;
        bool LeftEmptyRightPresent = false;
        bool LeftPresentRightEmpty = false;
        bool BothEmpty = false;
        switch (Operation) {
        case UtilityOperation::OptionalEqual:
          Operator = "==";
          BothEmpty = true;
          break;
        case UtilityOperation::OptionalNotEqual:
          Operator = "!=";
          LeftEmptyRightPresent = true;
          LeftPresentRightEmpty = true;
          break;
        case UtilityOperation::OptionalLess:
          Operator = "<";
          LeftEmptyRightPresent = true;
          break;
        case UtilityOperation::OptionalGreater:
          Operator = ">";
          LeftPresentRightEmpty = true;
          break;
        case UtilityOperation::OptionalLessEqual:
          Operator = "<=";
          LeftEmptyRightPresent = true;
          BothEmpty = true;
          break;
        case UtilityOperation::OptionalGreaterEqual:
          Operator = ">=";
          LeftPresentRightEmpty = true;
          BothEmpty = true;
          break;
        default:
          reject(L, "utility optional comparison",
                 "Unknown approved std::optional comparison.");
        }
        auto Result = temporary("bool", L);
        const auto CheckRight = labelName(), CheckLeftEmpty = labelName();
        const auto Compare = labelName(), LeftOnly = labelName();
        const auto RightOnly = labelName(), Neither = labelName();
        const auto End = labelName();
        branch(Engaged(*LeftOptional, *LeftStored), CheckRight, CheckLeftEmpty,
               L);
        label(CheckRight, L);
        branch(Engaged(*RightOptional, *RightStored), Compare, LeftOnly, L);
        label(CheckLeftEmpty, L);
        branch(Engaged(*RightOptional, *RightStored), RightOnly, Neither, L);
        label(Compare, L);
        assign(Result,
               CompareValues(Operator, Value(*LeftOptional, *LeftStored),
                             Value(*RightOptional, *RightStored)),
               L);
        jump(End, L);
        label(LeftOnly, L);
        assign(Result, boolean(LeftPresentRightEmpty, L), L);
        jump(End, L);
        label(RightOnly, L);
        assign(Result, boolean(LeftEmptyRightPresent, L), L);
        jump(End, L);
        label(Neither, L);
        assign(Result, boolean(BothEmpty, L), L);
        jump(End, L);
        label(End, L);
        return Result;
      }

      const bool OptionalOnLeft = LeftOptional.has_value();
      const auto &Optional = OptionalOnLeft ? *LeftOptional : *RightOptional;
      const auto &Stored = OptionalOnLeft ? *LeftStored : *RightStored;
      if (LeftNullopt || RightNullopt) {
        auto Present = snapshot(Engaged(Optional, Stored), L);
        switch (Operation) {
        case UtilityOperation::OptionalEqual:
          return Negate(std::move(Present));
        case UtilityOperation::OptionalNotEqual:
          return Present;
        case UtilityOperation::OptionalLess:
          return OptionalOnLeft ? boolean(false, L) : Present;
        case UtilityOperation::OptionalGreater:
          return OptionalOnLeft ? Present : boolean(false, L);
        case UtilityOperation::OptionalLessEqual:
          return OptionalOnLeft ? Negate(std::move(Present)) : boolean(true, L);
        case UtilityOperation::OptionalGreaterEqual:
          return OptionalOnLeft ? boolean(true, L) : Negate(std::move(Present));
        default:
          reject(L, "utility optional comparison",
                 "Unknown approved nullopt comparison.");
        }
      }

      auto Result = temporary("bool", L);
      const auto Compare = labelName(), Empty = labelName(), End = labelName();
      branch(Engaged(Optional, Stored), Compare, Empty, L);
      label(Compare, L);
      const llvm::StringRef Operator = [&] {
        switch (Operation) {
        case UtilityOperation::OptionalEqual:
          return llvm::StringRef("==");
        case UtilityOperation::OptionalNotEqual:
          return llvm::StringRef("!=");
        case UtilityOperation::OptionalLess:
          return llvm::StringRef("<");
        case UtilityOperation::OptionalGreater:
          return llvm::StringRef(">");
        case UtilityOperation::OptionalLessEqual:
          return llvm::StringRef("<=");
        case UtilityOperation::OptionalGreaterEqual:
          return llvm::StringRef(">=");
        default:
          reject(L, "utility optional comparison",
                 "Unknown approved value comparison.");
        }
      }();
      assign(Result,
             CompareValues(
                 Operator,
                 OptionalOnLeft ? Value(Optional, Stored) : *LeftScalar,
                 OptionalOnLeft ? *RightScalar : Value(Optional, Stored)),
             L);
      jump(End, L);
      label(Empty, L);
      bool EmptyResult = false;
      switch (Operation) {
      case UtilityOperation::OptionalEqual:
        EmptyResult = false;
        break;
      case UtilityOperation::OptionalNotEqual:
        EmptyResult = true;
        break;
      case UtilityOperation::OptionalLess:
      case UtilityOperation::OptionalLessEqual:
        EmptyResult = OptionalOnLeft;
        break;
      case UtilityOperation::OptionalGreater:
      case UtilityOperation::OptionalGreaterEqual:
        EmptyResult = !OptionalOnLeft;
        break;
      default:
        reject(L, "utility optional comparison",
               "Unknown approved empty comparison.");
      }
      assign(Result, boolean(EmptyResult, L), L);
      jump(End, L);
      label(End, L);
      return Result;
    }
    case UtilityOperation::OptionalHasValue:
    case UtilityOperation::OptionalDereference:
    case UtilityOperation::OptionalArrow:
    case UtilityOperation::OptionalReset:
    case UtilityOperation::OptionalEmplace:
    case UtilityOperation::OptionalValueOr:
    case UtilityOperation::OptionalMemberSwap:
    case UtilityOperation::OptionalSwap: {
      if (Operation == UtilityOperation::OptionalSwap) {
        const auto Left = OptionalFor(Call->getArg(0)->getType());
        const auto Right = OptionalFor(Call->getArg(1)->getType());
        if (!Left || !Right)
          reject(L, "utility optional swap",
                 "The selected std::optional layouts are unavailable.");
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
      const auto *Object = OptionalObject();
      auto Optional = Object ? OptionalFor(Object->getType())
                             : std::optional<UtilityOptionalRecord>();
      if (!Object || !Optional)
        reject(L, "utility optional operation",
               "The selected std::optional layout is unavailable.");
      if (Operation == UtilityOperation::OptionalMemberSwap) {
        const auto *RightSource =
            Call->getNumArgs() ? Call->getArg(0) : nullptr;
        if (!RightSource || !OptionalFor(RightSource->getType()))
          reject(L, "utility optional swap",
                 "The second std::optional layout is unavailable.");
        auto LeftAddress =
            snapshot(address(lvalue(Object), Object->getType(), L), L);
        auto RightAddress = snapshot(
            address(lvalue(RightSource), RightSource->getType(), L), L);
        auto OldLeft = snapshot(dereference(json::Object(LeftAddress), L), L);
        auto OldRight = snapshot(dereference(json::Object(RightAddress), L), L);
        assign(dereference(std::move(LeftAddress), L), std::move(OldRight), L);
        assign(dereference(std::move(RightAddress), L), std::move(OldLeft), L);
        return {};
      }
      auto ObjectAddress =
          snapshot(address(lvalue(Object), Object->getType(), L), L);
      auto Stored = dereference(json::Object(ObjectAddress), L);
      auto Value = fieldStorage(json::Object(Stored), Optional->Value, L);
      auto Engaged = fieldStorage(std::move(Stored), Optional->Engaged, L);
      if (Operation == UtilityOperation::OptionalHasValue)
        return Engaged;
      if (Operation == UtilityOperation::OptionalDereference)
        return Value;
      if (Operation == UtilityOperation::OptionalArrow)
        return snapshot(
            cast(address(std::move(Value), Optional->Value->getType(), L),
                 type(Call->getType(), L), L),
            L);
      if (Operation == UtilityOperation::OptionalValueOr) {
        auto Default = snapshot(expression(Call->getArg(0)), L);
        auto Result = Destination ? std::move(*Destination)
                      : recordValue(Call->getType())
                          ? objectTemporary(Call->getType(), L)
                          : temporary(type(Call->getType(), L), L);
        if (Result.getString("type") != type(Call->getType(), L))
          reject(L, "utility optional value_or",
                 "The std::optional::value_or destination type differs from "
                 "its result.");
        const auto Present = labelName(), Empty = labelName(),
                   End = labelName();
        branch(std::move(Engaged), Present, Empty, L);
        label(Present, L);
        if (recordValue(Optional->Value->getType()))
          assign(Result, std::move(Value), L);
        else
          assign(Result, cast(std::move(Value), type(Call->getType(), L), L),
                 L);
        jump(End, L);
        label(Empty, L);
        if (recordValue(Optional->Value->getType()))
          assign(Result, std::move(Default), L);
        else
          assign(Result, cast(std::move(Default), type(Call->getType(), L), L),
                 L);
        jump(End, L);
        label(End, L);
        return Result;
      }
      if (Operation == UtilityOperation::OptionalReset) {
        assign(std::move(Engaged), boolean(false, L), L);
        return {};
      }
      if (Operation == UtilityOperation::OptionalEmplace) {
        auto Argument = snapshot(expression(Call->getArg(0)), L);
        if (recordValue(Optional->Value->getType()))
          assign(std::move(Value), std::move(Argument), L);
        else
          assign(
              std::move(Value),
              cast(std::move(Argument), type(Optional->Value->getType(), L), L),
              L);
        assign(std::move(Engaged), boolean(true, L), L);
        return fieldStorage(dereference(std::move(ObjectAddress), L),
                            Optional->Value, L);
      }
      reject(L, "utility optional operation",
             "Unknown approved std::optional operation.");
    }
    case UtilityOperation::InitializerListSize:
    case UtilityOperation::InitializerListEmpty:
    case UtilityOperation::InitializerListBegin:
    case UtilityOperation::InitializerListEnd:
    case UtilityOperation::InitializerListRBegin:
    case UtilityOperation::InitializerListREnd: {
      const auto *Object = MemberObject();
      const auto *Source = Object               ? Object
                           : Call->getNumArgs() ? Call->getArg(0)
                                                : nullptr;
      auto List = Source ? InitializerListFor(Source->getType())
                         : std::optional<UtilityInitializerListRecord>();
      if (!Source || !List)
        reject(L, "initializer list access",
               "The selected std::initializer_list layout is unavailable.");
      auto Base = Object ? lvalue(Object) : expression(Source);
      auto Begin = fieldStorage(json::Object(Base), List->Begin, L);
      auto Size = fieldStorage(std::move(Base), List->Size, L);
      if (Operation == UtilityOperation::InitializerListSize)
        return Size;
      if (Operation == UtilityOperation::InitializerListEmpty)
        return snapshot(binary("==", std::move(Size),
                               quantity(0, type(A.Context.getSizeType(), L), L),
                               "bool", L),
                        L);
      if (Operation == UtilityOperation::InitializerListBegin)
        return Begin;
      auto End = binary("+", json::Object(Begin), std::move(Size),
                        type(List->Begin->getType(), L), L);
      if (Operation == UtilityOperation::InitializerListEnd)
        return End;
      auto Reverse = ReverseFor(Call->getType());
      if (!Reverse)
        reject(L, "initializer list reverse access",
               "The selected reverse iterator layout is unavailable.");
      return ReverseValue(Operation == UtilityOperation::InitializerListRBegin
                              ? std::move(End)
                              : std::move(Begin),
                          *Reverse);
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
        return boolean(!Array->Size, L);
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
      if (!Array->Size) {
        lvalue(Object);
        return A.zero(Call->getType(), L);
      }
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
      if (!Array->Size) {
        lvalue(Call->getArg(0));
        return {};
      }
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
      auto Array = LeftSource ? ArrayFor(LeftSource->getType())
                              : std::optional<UtilityArrayRecord>();
      if (!LeftSource || !RightSource || !Array)
        reject(L, "utility array swap",
               "The selected std::array layout is unavailable.");
      auto LeftAddress = snapshot(
          address(lvalue(LeftSource), LeftSource->getType(), L), L);
      auto RightAddress = snapshot(
          address(lvalue(RightSource), RightSource->getType(), L), L);
      if (!Array->Size)
        return {};
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
      const auto LeftArray = ArrayFor(Call->getArg(0)->getType());
      const auto RightArray = ArrayFor(Call->getArg(1)->getType());
      if (!LeftArray || !RightArray)
        reject(L, "utility array comparison",
               "A selected std::array layout is unavailable.");
      auto LeftAddress = snapshot(
          address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
      auto RightAddress = snapshot(
          address(lvalue(Call->getArg(1)), Call->getArg(1)->getType(), L), L);
      auto Left = dereference(std::move(LeftAddress), L);
      auto Right = dereference(std::move(RightAddress), L);
      return CompareUtilityValues(UtilityComparisonOperator(Operation), Left,
                                  A.Context.getRecordType(LeftArray->Record),
                                  Right,
                                  A.Context.getRecordType(RightArray->Record));
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
      if (auto Operation =
              approvedFunctionalOperation(A.S, A.Sources, Call, A.Context))
        return functionalOperation(Call, *Operation);
      APValue UtilityValue;
      if (approvedUtilityConstant(A.S, A.Sources, Call, A.Context,
                                  UtilityValue))
        return A.constant(UtilityValue, Call->getType(), L);
      if (auto Operation =
              approvedUtilityOperation(A.S, A.Sources, Call, A.Context))
        return utilityOperation(Call, *Operation, std::move(Destination));
      if (approvedFunctionalObjectAssignment(
              A.S, A.Sources, dyn_cast<CXXOperatorCallExpr>(Call), A.Context)) {
        if (Destination)
          reject(L, "functional object assignment",
                 "Standard function-object assignment cannot initialize a "
                 "record result.");
        expression(Call->getArg(1));
        auto LeftAddress = snapshot(
            address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
        auto Left = dereference(std::move(LeftAddress), L);
        assign(Left, A.zero(Call->getArg(0)->getType(), L), L);
        return Left;
      }
      if (approvedFunctionalReferenceAssignment(
              A.S, A.Sources, dyn_cast<CXXOperatorCallExpr>(Call), A.Context)) {
        if (Destination)
          reject(L, "functional reference assignment",
                 "std::reference_wrapper assignment cannot initialize a "
                 "record result.");
        auto Right = snapshot(expression(Call->getArg(1)), L);
        auto LeftAddress = snapshot(
            address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
        auto Left = dereference(std::move(LeftAddress), L);
        assign(Left, std::move(Right), L);
        return Left;
      }
      if (approvedUtilityAllocatorAssignment(
              A.S, A.Sources, dyn_cast<CXXOperatorCallExpr>(Call), A.Context)) {
        if (Destination)
          reject(
              L, "allocator assignment",
              "std::allocator assignment cannot initialize a record result.");
        auto Right = snapshot(expression(Call->getArg(1)), L);
        auto LeftAddress = snapshot(
            address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
        auto Left = dereference(std::move(LeftAddress), L);
        assign(Left, std::move(Right), L);
        return Left;
      }
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
      if (auto Assignment = approvedUtilityTupleAssignment(
              A.S, A.Sources, dyn_cast<CXXOperatorCallExpr>(Call), A.Context)) {
        if (Destination)
          reject(L, "utility tuple assignment",
                 "std::tuple assignment cannot initialize a record result.");
        auto RightAddress = snapshot(
            address(lvalue(Call->getArg(1)), Call->getArg(1)->getType(), L), L);
        auto LeftAddress = snapshot(
            address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
        auto Left = dereference(std::move(LeftAddress), L);
        auto Right = dereference(std::move(RightAddress), L);
        switch (*Assignment) {
        case UtilityTupleAssignment::CopyOrMove:
          assign(Left, std::move(Right), L);
          return Left;
        case UtilityTupleAssignment::Converting: {
          auto DestinationTuple = approvedUtilityTupleRecord(
              A.S, A.Sources, Call->getArg(0)->getType()->getAsCXXRecordDecl(),
              A.Context);
          auto SourceTuple = approvedUtilityTupleRecord(
              A.S, A.Sources, Call->getArg(1)->getType()->getAsCXXRecordDecl(),
              A.Context);
          if (!DestinationTuple || !SourceTuple ||
              DestinationTuple->Elements.size() != SourceTuple->Elements.size())
            reject(L, "utility tuple assignment",
                   "A selected std::tuple layout is unavailable.");
          for (unsigned I = 0; I < DestinationTuple->Elements.size(); ++I) {
            auto Destination = fieldStorage(json::Object(Left),
                                            DestinationTuple->Elements[I], L);
            auto Value =
                fieldStorage(json::Object(Right), SourceTuple->Elements[I], L);
            if (recordValue(DestinationTuple->Elements[I]->getType()))
              assign(std::move(Destination), std::move(Value), L);
            else
              assign(std::move(Destination),
                     cast(std::move(Value),
                          type(DestinationTuple->Elements[I]->getType(), L), L),
                     L);
          }
          return Left;
        }
        case UtilityTupleAssignment::Pair: {
          auto DestinationTuple = approvedUtilityTupleRecord(
              A.S, A.Sources, Call->getArg(0)->getType()->getAsCXXRecordDecl(),
              A.Context);
          auto SourcePair = approvedUtilityPairRecord(
              A.S, A.Sources, Call->getArg(1)->getType()->getAsCXXRecordDecl(),
              A.Context);
          if (!DestinationTuple || DestinationTuple->Elements.size() != 2 ||
              !SourcePair)
            reject(L, "utility tuple assignment",
                   "A selected tuple or pair layout is unavailable.");
          for (unsigned I = 0; I != 2; ++I) {
            const auto *SourceField =
                I ? SourcePair->Second : SourcePair->First;
            auto Destination = fieldStorage(json::Object(Left),
                                            DestinationTuple->Elements[I], L);
            auto Value = fieldStorage(json::Object(Right), SourceField, L);
            if (recordValue(DestinationTuple->Elements[I]->getType()))
              assign(std::move(Destination), std::move(Value), L);
            else
              assign(std::move(Destination),
                     cast(std::move(Value),
                          type(DestinationTuple->Elements[I]->getType(), L), L),
                     L);
          }
          return Left;
        }
        }
        reject(L, "utility tuple assignment",
               "Unknown approved std::tuple assignment.");
      }
      if (auto List = approvedUtilityInitializerListAssignment(
              A.S, A.Sources, dyn_cast<CXXOperatorCallExpr>(Call), A.Context)) {
        if (Destination)
          reject(L, "initializer list assignment",
                 "std::initializer_list assignment cannot initialize a "
                 "record result.");
        auto RightAddress = snapshot(
            address(lvalue(Call->getArg(1)), Call->getArg(1)->getType(), L), L);
        auto LeftAddress = snapshot(
            address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
        auto Left = dereference(std::move(LeftAddress), L);
        assign(Left, dereference(std::move(RightAddress), L), L);
        return Left;
      }
      if (auto Assignment = approvedUtilityOptionalAssignment(
              A.S, A.Sources, dyn_cast<CXXOperatorCallExpr>(Call), A.Context)) {
        if (Destination)
          reject(L, "utility optional assignment",
                 "std::optional assignment cannot initialize a record "
                 "result.");
        auto LeftAddress = snapshot(
            address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
        auto Left = dereference(json::Object(LeftAddress), L);
        switch (*Assignment) {
        case UtilityOptionalAssignment::CopyOrMove: {
          auto RightAddress = snapshot(
              address(lvalue(Call->getArg(1)), Call->getArg(1)->getType(), L),
              L);
          assign(Left, dereference(std::move(RightAddress), L), L);
          return Left;
        }
        case UtilityOptionalAssignment::Converting: {
          auto DestinationOptional = approvedUtilityOptionalRecord(
              A.S, A.Sources, Call->getArg(0)->getType()->getAsCXXRecordDecl(),
              A.Context);
          auto SourceOptional = approvedUtilityOptionalRecord(
              A.S, A.Sources, Call->getArg(1)->getType()->getAsCXXRecordDecl(),
              A.Context);
          if (!DestinationOptional || !SourceOptional)
            reject(L, "utility optional assignment",
                   "A selected std::optional layout is unavailable.");
          auto Source = snapshot(expression(Call->getArg(1)), L);
          const auto Convert = labelName(), Empty = labelName();
          const auto End = labelName();
          branch(fieldStorage(json::Object(Source), SourceOptional->Engaged, L),
                 Convert, Empty, L);
          label(Convert, L);
          {
            auto Destination =
                fieldStorage(json::Object(Left), DestinationOptional->Value, L);
            auto Value =
                fieldStorage(json::Object(Source), SourceOptional->Value, L);
            if (recordValue(DestinationOptional->Value->getType()))
              assign(std::move(Destination), std::move(Value), L);
            else
              assign(std::move(Destination),
                     cast(std::move(Value),
                          type(DestinationOptional->Value->getType(), L), L),
                     L);
          }
          assign(
              fieldStorage(json::Object(Left), DestinationOptional->Engaged, L),
              boolean(true, L), L);
          jump(End, L);
          label(Empty, L);
          assign(
              fieldStorage(json::Object(Left), DestinationOptional->Engaged, L),
              boolean(false, L), L);
          jump(End, L);
          label(End, L);
          return Left;
        }
        case UtilityOptionalAssignment::Empty: {
          auto Optional = approvedUtilityOptionalRecord(
              A.S, A.Sources, Call->getArg(0)->getType()->getAsCXXRecordDecl(),
              A.Context);
          if (!Optional)
            reject(L, "utility optional assignment",
                   "The destination std::optional layout is unavailable.");
          assign(fieldStorage(json::Object(Left), Optional->Engaged, L),
                 boolean(false, L), L);
          return Left;
        }
        case UtilityOptionalAssignment::Value: {
          auto Optional = approvedUtilityOptionalRecord(
              A.S, A.Sources, Call->getArg(0)->getType()->getAsCXXRecordDecl(),
              A.Context);
          if (!Optional)
            reject(L, "utility optional assignment",
                   "The destination std::optional layout is unavailable.");
          auto Source = snapshot(expression(Call->getArg(1)), L);
          auto Destination =
              fieldStorage(json::Object(Left), Optional->Value, L);
          if (recordValue(Optional->Value->getType()))
            assign(std::move(Destination), std::move(Source), L);
          else
            assign(
                std::move(Destination),
                cast(std::move(Source), type(Optional->Value->getType(), L), L),
                L);
          assign(fieldStorage(json::Object(Left), Optional->Engaged, L),
                 boolean(true, L), L);
          return Left;
        }
        }
        reject(L, "utility optional assignment",
               "Unknown approved std::optional assignment.");
      }
      if (auto Assignment = approvedUtilityReverseIteratorAssignment(
              A.S, A.Sources, dyn_cast<CXXOperatorCallExpr>(Call), A.Context)) {
        if (Destination)
          reject(L, "reverse iterator assignment",
                 "std::reverse_iterator assignment cannot initialize a "
                 "record result.");
        auto RightAddress = snapshot(
            address(lvalue(Call->getArg(1)), Call->getArg(1)->getType(), L), L);
        Expression Converted;
        if (Assignment->Converting) {
          auto Right = dereference(json::Object(RightAddress), L);
          Converted =
              snapshot(cast(fieldStorage(std::move(Right),
                                         Assignment->Source.Current, L),
                            type(Assignment->Destination.IteratorType, L), L),
                       L);
        }
        auto LeftAddress = snapshot(
            address(lvalue(Call->getArg(0)), Call->getArg(0)->getType(), L), L);
        auto Left = dereference(std::move(LeftAddress), L);
        if (!Assignment->Converting) {
          assign(Left, dereference(std::move(RightAddress), L), L);
        } else {
          assign(fieldStorage(json::Object(Left),
                              Assignment->Destination.Legacy, L),
                 json::Object(Converted), L);
          assign(fieldStorage(json::Object(Left),
                              Assignment->Destination.Current, L),
                 std::move(Converted), L);
        }
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
  Expression
  allocateArray(const CXXNewExpr *N,
                std::optional<uint64_t> AuthenticatedCount = std::nullopt,
                SourceLocation UseLocation = SourceLocation()) {
    const auto L = UseLocation.isValid() ? UseLocation : N->getExprLoc();
    const auto *F = A.allocationFunction(N->getOperatorNew(), true, L, true);
    const auto Object = N->getAllocatedType();
    ArrayNewInfo Info;
    if (AuthenticatedCount) {
      Info.Count = AuthenticatedCount;
      Info.Initializer = N->getInitializer();
    } else {
      Info = A.arrayNewInfo(N);
    }
    const auto Layout = A.arrayAllocationLayout(Object, N->doesUsualArrayDeleteWantSize(), L);
    const uint64_t ObjectBytes = A.Context.getTypeSizeInChars(Object).getQuantity();
    const auto Size = type(A.Context.getSizeType(), L);
    auto Result = temporary(type(N->getType(), L), L);
    assign(Result, A.zero(N->getType(), L), L);
    std::string End;
    Expression Count;
    if (Info.Count) {
      if (!AuthenticatedCount)
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
    auto Storage = temporary("ptr:void", L);
    if (A.standardPlacementAllocation(F, true)) {
      assign(Storage,
             argument(N->getPlacementArg(0), F->getParamDecl(1)->getType()), L);
    } else {
      for (unsigned I = 0; I < N->getNumPlacementArgs(); ++I)
        Args.push_back(argument(N->getPlacementArg(I),
                                F->getParamDecl(Prefix + I)->getType()));
      chargeCall(Args, L);
      Body.push_back(json::Object{{"op", "call"},
                                  {"callee", A.name(F)},
                                  {"args", std::move(Args)},
                                  {"target", json::Object(Storage)},
                                  {"loc", A.loc(L)}});
    }
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
    auto Storage = temporary(type(F->getReturnType(), L), L);
    if (A.standardPlacementAllocation(F, false)) {
      assign(Storage,
             argument(N->getPlacementArg(0), F->getParamDecl(1)->getType()), L);
    } else {
      for (unsigned I = 0; I < N->getNumPlacementArgs(); ++I)
        Args.push_back(argument(N->getPlacementArg(I),
                                F->getParamDecl(Prefix + I)->getType()));
      chargeCall(Args, L);
      Body.push_back(json::Object{{"op", "call"},
                                  {"callee", A.name(F)},
                                  {"args", std::move(Args)},
                                  {"target", json::Object(Storage)},
                                  {"loc", A.loc(L)}});
    }
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
  void deallocateArray(Expression Pointer, QualType Object,
                       const FunctionDecl *F, bool UsualDeleteWantsSize,
                       SourceLocation L) {
    const auto Layout =
        A.arrayAllocationLayout(Object, UsualDeleteWantsSize, L);
    Pointer = snapshot(std::move(Pointer), L);
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
  void deallocateArray(const CXXDeleteExpr *Delete, Expression Pointer,
                       SourceLocation L) {
    const auto *F =
        A.allocationFunction(Delete->getOperatorDelete(), false, L, true);
    deallocateArray(std::move(Pointer), Delete->getDestroyedType(), F,
                    Delete->doesUsualArrayDeleteWantSize(), L);
  }
  void deallocateArray(const CXXDeleteExpr *Delete) {
    deallocateArray(Delete, expression(Delete->getArgument()),
                    Delete->getExprLoc());
  }
  void deallocateSingle(Expression Pointer, QualType Object,
                        const FunctionDecl *F, SourceLocation L) {
    Object = Object.getUnqualifiedType();
    // A destructor may reseat the variable which supplied this pointer.
    Pointer = snapshot(std::move(Pointer), L);
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
  void deallocateUniquePtr(Expression Pointer, Expression OwnerAddress,
                           const UtilityUniquePtrRecord &Owner,
                           SourceLocation L) {
    if (Owner.CustomDeleter) {
      Pointer = snapshot(std::move(Pointer), L);
      auto Invoke = labelName(), End = labelName();
      branch(cast(Pointer, "bool", L), Invoke, End, L);
      label(Invoke, L);
      json::Array Args;
      const auto ThisType = Owner.CustomDeleter->getThisType();
      auto ErasedAddress =
          cast(std::move(OwnerAddress),
               ThisType->getPointeeType().isConstQualified() ? "cptr:void"
                                                             : "ptr:void",
               L);
      Args.push_back(cast(std::move(ErasedAddress), type(ThisType, L), L));
      Args.push_back(
          cast(std::move(Pointer),
               type(Owner.CustomDeleter->getParamDecl(0)->getType(), L), L));
      chargeCall(Args, L);
      Body.push_back(json::Object{{"op", "call"},
                                  {"callee", A.name(Owner.CustomDeleter)},
                                  {"args", std::move(Args)},
                                  {"loc", A.loc(L)}});
      jump(End, L);
      label(End, L);
      return;
    }
    const auto *Function = A.uniquePtrDeleteFunction(Owner, L);
    if (Owner.Deleter.Array) {
      deallocateArray(std::move(Pointer), Owner.ElementType, Function,
                      Owner.DefaultDeletion->doesUsualArrayDeleteWantSize(), L);
      return;
    }
    deallocateSingle(std::move(Pointer), Owner.ElementType, Function, L);
  }
  void deallocate(const CXXDeleteExpr *Delete) {
    if (Delete->isArrayForm()) {
      deallocateArray(Delete);
      return;
    }
    auto L = Delete->getExprLoc();
    const auto *F = A.allocationFunction(Delete->getOperatorDelete(), false, L);
    deallocateSingle(expression(Delete->getArgument()),
                     Delete->getDestroyedType(), F, L);
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
    if (A.S.coreV2() && isa<CXXStdInitializerListExpr>(E))
      return materialize(E, L);
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
  void constructMemoryDefault(Expression Place, QualType T,
                              const CXXConstructorDecl *Constructor,
                              bool ValueInitialize, SourceLocation L) {
    if (!T->isRecordType()) {
      if (Constructor)
        reject(L, "memory construction",
               "A scalar destination cannot select a constructor.");
      if (ValueInitialize)
        initializeZero(std::move(Place), T, L);
      return;
    }
    if (!Constructor ||
        T->getAsCXXRecordDecl()->getCanonicalDecl() !=
            Constructor->getParent()->getCanonicalDecl() ||
        Place.getString("type") != type(T, L))
      reject(L, "memory construction",
             "Constructor and destination types differ.");
    if (ValueInitialize && !Constructor->isUserProvided())
      initializeZero(Place, T, L);
    if (Constructor->isTrivial())
      return;
    if (!supportedConstructor(Constructor) || !Constructor->hasBody() ||
        Constructor->getNumParams())
      reject(L, "memory construction",
             "Unsupported selected default constructor.");
    json::Array Args;
    Args.push_back(
        snapshot(address(std::move(Place), T.getUnqualifiedType(), L), L));
    chargeCall(Args, L);
    Body.push_back(json::Object{{"op", "call"},
                                {"callee", A.name(Constructor)},
                                {"args", std::move(Args)},
                                {"loc", A.loc(L)}});
  }
  void constructMemorySource(Expression Place, QualType T,
                             const CXXConstructorDecl *Constructor,
                             Expression SourcePointer, SourceLocation L) {
    if (!Constructor || !T->isRecordType() ||
        T->getAsCXXRecordDecl()->getCanonicalDecl() !=
            Constructor->getParent()->getCanonicalDecl() ||
        Place.getString("type") != type(T, L))
      reject(L, "memory construction",
             "Constructor and destination types differ.");
    if (Constructor->isTrivial()) {
      assign(std::move(Place), dereference(std::move(SourcePointer), L), L);
      return;
    }
    if (!supportedConstructor(Constructor) || !Constructor->hasBody() ||
        Constructor->getNumParams() != 1 ||
        !Constructor->getParamDecl(0)->getType()->isReferenceType())
      reject(L, "memory construction",
             "Unsupported selected source constructor.");
    json::Array Args;
    Args.push_back(
        snapshot(address(std::move(Place), T.getUnqualifiedType(), L), L));
    Args.push_back(
        snapshot(cast(std::move(SourcePointer),
                      type(Constructor->getParamDecl(0)->getType(), L), L),
                 L));
    chargeCall(Args, L);
    Body.push_back(json::Object{{"op", "call"},
                                {"callee", A.name(Constructor)},
                                {"args", std::move(Args)},
                                {"loc", A.loc(L)}});
  }
  bool recordValue(QualType T) const {
    return A.S.coreV2() && T->isRecordType();
  }
  bool emptyRecord(QualType T) const {
    const auto *R = T->getAsCXXRecordDecl();
    if (!A.S.coreV2() || !R || !R->getDefinition())
      return false;
    if (R->getDefinition()->field_empty())
      return true;
    const auto Array =
        approvedUtilityArrayRecord(A.S, A.Sources, R, A.Context);
    return Array && !Array->Size;
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
    if (auto Kind = approvedUtilityUniquePtrConstruction(A.S, A.Sources, C,
                                                         A.Context)) {
      auto Unique = approvedUtilityUniquePtrRecord(
          A.S, A.Sources, T->getAsCXXRecordDecl(), A.Context);
      if (!Unique)
        reject(L, "unique pointer construction",
               "The selected std::unique_ptr layout is unavailable.");
      switch (*Kind) {
      case UtilityUniquePtrConstruction::Default:
        assign(std::move(Place), A.zero(T, L), L);
        return;
      case UtilityUniquePtrConstruction::Null:
        if (C->getNumArgs() != 1)
          reject(L, "unique pointer construction",
                 "A null std::unique_ptr construction needs one argument.");
        discard(C->getArg(0));
        assign(std::move(Place), A.zero(T, L), L);
        return;
      case UtilityUniquePtrConstruction::Pointer: {
        if (C->getNumArgs() != 1)
          reject(L, "unique pointer construction",
                 "An owning std::unique_ptr construction needs one pointer.");
        Expression Member{{"kind", "member"},
                          {"type", type(Unique->PointerType, L)},
                          {"name", "nct_unique_ptr_pointer"},
                          {"args", json::Array{json::Object(Place)}},
                          {"loc", A.loc(L)}};
        assign(std::move(Member),
               cast(expression(C->getArg(0)), type(Unique->PointerType, L), L),
               L);
        return;
      }
      case UtilityUniquePtrConstruction::NullDeleter:
        if (C->getNumArgs() != 2)
          reject(L, "unique pointer construction",
                 "A null std::unique_ptr deleter construction needs two "
                 "arguments.");
        discard(C->getArg(0));
        discard(C->getArg(1));
        assign(std::move(Place), A.zero(T, L), L);
        return;
      case UtilityUniquePtrConstruction::PointerDeleter: {
        if (C->getNumArgs() != 2)
          reject(L, "unique pointer construction",
                 "An owning std::unique_ptr deleter construction needs a "
                 "pointer and deleter.");
        auto Pointer = snapshot(
            cast(expression(C->getArg(0)), type(Unique->PointerType, L), L), L);
        discard(C->getArg(1));
        Expression Member{{"kind", "member"},
                          {"type", type(Unique->PointerType, L)},
                          {"name", "nct_unique_ptr_pointer"},
                          {"args", json::Array{json::Object(Place)}},
                          {"loc", A.loc(L)}};
        assign(std::move(Member), std::move(Pointer), L);
        return;
      }
      case UtilityUniquePtrConstruction::FactoryArray:
        reject(L, "unique pointer construction",
               "The private array owner construction is lowered only through "
               "its authenticated std::make_unique call.");
        return;
      case UtilityUniquePtrConstruction::Move:
      case UtilityUniquePtrConstruction::ConvertingMove: {
        if (C->getNumArgs() != 1)
          reject(L, "unique pointer move construction",
                 "A moving std::unique_ptr construction needs one owner.");
        const auto Source = approvedUtilityUniquePtrRecord(
            A.S, A.Sources, C->getArg(0)->getType()->getAsCXXRecordDecl(),
            A.Context);
        const bool Converting =
            *Kind == UtilityUniquePtrConstruction::ConvertingMove;
        if (!Source || Converting == (Source->Record->getCanonicalDecl() ==
                                      Unique->Record->getCanonicalDecl()))
          reject(L, "unique pointer move construction",
                 "The checked source std::unique_ptr type changed.");
        auto SourceAddress =
            snapshot(address(lvalue(C->getArg(0)),
                             A.Context.getRecordType(Source->Record), L),
                     L);
        Expression SourceMember{
            {"kind", "member"},
            {"type", type(Source->PointerType, L)},
            {"name", "nct_unique_ptr_pointer"},
            {"args", json::Array{dereference(std::move(SourceAddress), L)}},
            {"loc", A.loc(L)}};
        auto Pointer = snapshot(SourceMember, L);
        assign(std::move(SourceMember), A.zero(Source->PointerType, L), L);
        Expression DestinationMember{{"kind", "member"},
                                     {"type", type(Unique->PointerType, L)},
                                     {"name", "nct_unique_ptr_pointer"},
                                     {"args", json::Array{json::Object(Place)}},
                                     {"loc", A.loc(L)}};
        assign(std::move(DestinationMember),
               cast(std::move(Pointer), type(Unique->PointerType, L), L), L);
        return;
      }
      }
      reject(L, "unique pointer construction",
             "Unknown approved std::unique_ptr construction.");
    }
    if (auto Kind = approvedFunctionalReferenceConstruction(
            A.S, A.Sources, C, A.Context)) {
      const auto Wrapper = approvedFunctionalReferenceRecord(
          A.S, A.Sources, T->getAsCXXRecordDecl(), A.Context);
      if (!Wrapper || C->getNumArgs() != 1)
        reject(L, "functional reference construction",
               "A checked std::reference_wrapper construction is required.");
      if (*Kind == FunctionalReferenceConstruction::CopyOrMove) {
        assign(std::move(Place), snapshot(expression(C->getArg(0)), L), L);
        return;
      }
      auto Pointer = snapshot(
          cast(address(lvalue(C->getArg(0)), C->getArg(0)->getType(), L),
               type(Wrapper->PointerType, L), L),
          L);
      if (Wrapper->PaddedBase) {
        Expression Base{{"kind", "member"},
                        {"type", type(A.Context.getSizeType(), L)},
                        {"name", "nct_reference_wrapper_base_storage"},
                        {"args", json::Array{json::Object(Place)}},
                        {"loc", A.loc(L)}};
        assign(std::move(Base), A.zero(A.Context.getSizeType(), L), L);
      }
      Expression Member{{"kind", "member"},
                        {"type", type(Wrapper->PointerType, L)},
                        {"name", "nct_reference_wrapper_pointer"},
                        {"args", json::Array{json::Object(Place)}},
                        {"loc", A.loc(L)}};
      assign(std::move(Member), std::move(Pointer), L);
      return;
    }
    if (auto Kind = approvedFunctionalObjectConstruction(A.S, A.Sources, C,
                                                         A.Context)) {
      if (*Kind == FunctionalObjectConstruction::CopyOrMove) {
        if (C->getNumArgs() != 1)
          reject(L, "functional object construction",
                 "A copied standard function object needs one source.");
        expression(C->getArg(0));
      }
      assign(std::move(Place), A.zero(T, L), L);
      return;
    }
    if (auto Kind = approvedUtilityDefaultDeleteConstruction(A.S, A.Sources, C,
                                                             A.Context)) {
      if (*Kind != UtilityDefaultDeleteConstruction::Default) {
        if (!C->getNumArgs())
          reject(L, "default delete construction",
                 "A copied or converted std::default_delete needs a source.");
        expression(C->getArg(0));
      }
      assign(std::move(Place), A.zero(T, L), L);
      return;
    }
    if (auto Kind = approvedUtilityAllocatorConstruction(A.S, A.Sources, C,
                                                         A.Context)) {
      if (*Kind != UtilityAllocatorConstruction::Default) {
        if (C->getNumArgs() != 1)
          reject(L, "allocator construction",
                 "A copied or converted std::allocator needs one source.");
        // The state is empty, but the bound source expression and any
        // full-expression lifetime still have to be evaluated.
        expression(C->getArg(0));
      }
      assign(std::move(Place), A.zero(T, L), L);
      return;
    }
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
    if (auto Kind =
            approvedUtilityTupleConstruction(A.S, A.Sources, C, A.Context)) {
      auto Tuple = approvedUtilityTupleRecord(
          A.S, A.Sources, T->getAsCXXRecordDecl(), A.Context);
      if (!Tuple)
        reject(L, "utility tuple construction",
               "The selected std::tuple layout is unavailable.");
      auto Member = [&](const FieldDecl *Field) {
        return fieldStorage(json::Object(Place), Field, L);
      };
      switch (*Kind) {
      case UtilityTupleConstruction::Default:
        for (const auto *Element : Tuple->Elements)
          initializeZero(Member(Element), Element->getType(), L);
        return;
      case UtilityTupleConstruction::Elements:
        if (C->getNumArgs() != Tuple->Elements.size())
          reject(L, "utility tuple construction",
                 "The constructor and tuple element counts differ.");
        for (unsigned I = 0; I < Tuple->Elements.size(); ++I) {
          if (recordValue(Tuple->Elements[I]->getType()))
            initialize(Member(Tuple->Elements[I]), C->getArg(I), L);
          else
            assign(Member(Tuple->Elements[I]),
                   cast(expression(C->getArg(I)),
                        type(Tuple->Elements[I]->getType(), L), L),
                   L);
        }
        return;
      case UtilityTupleConstruction::CopyOrMove:
        assign(std::move(Place), expression(C->getArg(0)), L);
        return;
      case UtilityTupleConstruction::Converting: {
        auto SourceTuple = approvedUtilityTupleRecord(
            A.S, A.Sources, C->getArg(0)->getType()->getAsCXXRecordDecl(),
            A.Context);
        if (!SourceTuple ||
            SourceTuple->Elements.size() != Tuple->Elements.size())
          reject(L, "utility tuple construction",
                 "The source std::tuple layout is unavailable.");
        auto Source = snapshot(expression(C->getArg(0)), L);
        for (unsigned I = 0; I < Tuple->Elements.size(); ++I) {
          auto Value =
              fieldStorage(json::Object(Source), SourceTuple->Elements[I], L);
          if (recordValue(Tuple->Elements[I]->getType()))
            assign(Member(Tuple->Elements[I]), std::move(Value), L);
          else
            assign(Member(Tuple->Elements[I]),
                   cast(std::move(Value),
                        type(Tuple->Elements[I]->getType(), L), L),
                   L);
        }
        return;
      }
      case UtilityTupleConstruction::Pair: {
        auto SourcePair = approvedUtilityPairRecord(
            A.S, A.Sources, C->getArg(0)->getType()->getAsCXXRecordDecl(),
            A.Context);
        if (!SourcePair || Tuple->Elements.size() != 2)
          reject(L, "utility tuple construction",
                 "The source std::pair layout is unavailable.");
        auto Source = snapshot(expression(C->getArg(0)), L);
        for (unsigned I = 0; I != 2; ++I) {
          const auto *SourceField = I ? SourcePair->Second : SourcePair->First;
          auto Value = fieldStorage(json::Object(Source), SourceField, L);
          if (recordValue(Tuple->Elements[I]->getType()))
            assign(Member(Tuple->Elements[I]), std::move(Value), L);
          else
            assign(Member(Tuple->Elements[I]),
                   cast(std::move(Value),
                        type(Tuple->Elements[I]->getType(), L), L),
                   L);
        }
        return;
      }
      }
      reject(L, "utility tuple construction",
             "Unknown approved std::tuple construction.");
    }
    if (auto Kind = approvedUtilityInitializerListConstruction(A.S, A.Sources,
                                                               C, A.Context)) {
      auto List = approvedUtilityInitializerListRecord(
          A.S, A.Sources, T->getAsCXXRecordDecl(), A.Context);
      if (!List)
        reject(L, "initializer list construction",
               "The selected std::initializer_list layout is unavailable.");
      switch (*Kind) {
      case UtilityInitializerListConstruction::Default:
        initializeZero(fieldStorage(json::Object(Place), List->Begin, L),
                       List->Begin->getType(), L);
        initializeZero(fieldStorage(json::Object(Place), List->Size, L),
                       List->Size->getType(), L);
        return;
      case UtilityInitializerListConstruction::CopyOrMove:
        assign(std::move(Place), expression(C->getArg(0)), L);
        return;
      }
      reject(L, "initializer list construction",
             "Unknown approved std::initializer_list construction.");
    }
    if (auto Kind =
            approvedUtilityOptionalConstruction(A.S, A.Sources, C, A.Context)) {
      auto Optional = approvedUtilityOptionalRecord(
          A.S, A.Sources, T->getAsCXXRecordDecl(), A.Context);
      if (!Optional)
        reject(L, "utility optional construction",
               "The selected std::optional layout is unavailable.");
      auto Value = [&] {
        return fieldStorage(json::Object(Place), Optional->Value, L);
      };
      auto Engaged = [&] {
        return fieldStorage(json::Object(Place), Optional->Engaged, L);
      };
      switch (*Kind) {
      case UtilityOptionalConstruction::Empty:
        initializeZero(Value(), Optional->Value->getType(), L);
        initializeZero(Engaged(), Optional->Engaged->getType(), L);
        return;
      case UtilityOptionalConstruction::InPlaceDefault:
        initializeZero(Value(), Optional->Value->getType(), L);
        assign(Engaged(), boolean(true, L), L);
        return;
      case UtilityOptionalConstruction::InPlaceValue:
        if (recordValue(Optional->Value->getType()))
          initialize(Value(), C->getArg(1), L);
        else
          assign(Value(),
                 cast(expression(C->getArg(1)),
                      type(Optional->Value->getType(), L), L),
                 L);
        assign(Engaged(), boolean(true, L), L);
        return;
      case UtilityOptionalConstruction::CopyOrMove:
        assign(std::move(Place), expression(C->getArg(0)), L);
        return;
      case UtilityOptionalConstruction::Converting: {
        auto SourceOptional = approvedUtilityOptionalRecord(
            A.S, A.Sources, C->getArg(0)->getType()->getAsCXXRecordDecl(),
            A.Context);
        if (!SourceOptional)
          reject(L, "utility optional construction",
                 "The source std::optional layout is unavailable.");
        auto Source = snapshot(expression(C->getArg(0)), L);
        initializeZero(Value(), Optional->Value->getType(), L);
        const auto Convert = labelName(), Empty = labelName();
        const auto End = labelName();
        branch(fieldStorage(json::Object(Source), SourceOptional->Engaged, L),
               Convert, Empty, L);
        label(Convert, L);
        {
          auto SourceValue =
              fieldStorage(json::Object(Source), SourceOptional->Value, L);
          if (recordValue(Optional->Value->getType()))
            assign(Value(), std::move(SourceValue), L);
          else
            assign(Value(),
                   cast(std::move(SourceValue),
                        type(Optional->Value->getType(), L), L),
                   L);
        }
        assign(Engaged(), boolean(true, L), L);
        jump(End, L);
        label(Empty, L);
        assign(Engaged(), boolean(false, L), L);
        jump(End, L);
        label(End, L);
        return;
      }
      case UtilityOptionalConstruction::Value:
        if (recordValue(Optional->Value->getType()))
          initialize(Value(), C->getArg(0), L);
        else
          assign(Value(),
                 cast(expression(C->getArg(0)),
                      type(Optional->Value->getType(), L), L),
                 L);
        assign(Engaged(), boolean(true, L), L);
        return;
      }
      reject(L, "utility optional construction",
             "Unknown approved std::optional construction.");
    }
    if (auto Kind = approvedUtilityReverseIteratorConstruction(A.S, A.Sources,
                                                               C, A.Context)) {
      auto Reverse = approvedUtilityReverseIteratorRecord(
          A.S, A.Sources, T->getAsCXXRecordDecl(), A.Context);
      if (!Reverse)
        reject(L, "reverse iterator construction",
               "The selected std::reverse_iterator layout is unavailable.");
      auto Member = [&](const FieldDecl *Field) {
        return fieldStorage(json::Object(Place), Field, L);
      };
      auto InitializePointer = [&](Expression Pointer) {
        auto Value = snapshot(
            cast(std::move(Pointer), type(Reverse->IteratorType, L), L), L);
        assign(Member(Reverse->Legacy), json::Object(Value), L);
        assign(Member(Reverse->Current), std::move(Value), L);
      };
      switch (*Kind) {
      case UtilityReverseIteratorConstruction::Default:
        initializeZero(Member(Reverse->Legacy), Reverse->IteratorType, L);
        initializeZero(Member(Reverse->Current), Reverse->IteratorType, L);
        return;
      case UtilityReverseIteratorConstruction::Iterator:
        InitializePointer(expression(C->getArg(0)));
        return;
      case UtilityReverseIteratorConstruction::CopyOrMove:
        assign(std::move(Place), expression(C->getArg(0)), L);
        return;
      case UtilityReverseIteratorConstruction::Converting: {
        auto Source = approvedUtilityReverseIteratorRecord(
            A.S, A.Sources, C->getArg(0)->getType()->getAsCXXRecordDecl(),
            A.Context);
        if (!Source)
          reject(L, "reverse iterator conversion",
                 "The source std::reverse_iterator layout is unavailable.");
        InitializePointer(
            fieldStorage(lvalue(C->getArg(0)), Source->Current, L));
        return;
      }
      }
      reject(L, "reverse iterator construction",
             "Unknown approved std::reverse_iterator construction.");
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
      if (const auto *Expression = dyn_cast<CXXStdInitializerListExpr>(Init)) {
        auto List = approvedUtilityInitializerListExpression(
            A.S, A.Sources, Expression, A.Context);
        if (!List || Place.getString("type") != type(Expression->getType(), L))
          reject(L, "initializer list expression",
                 "The checked std::initializer_list destination or backing "
                 "array is unavailable.");
        auto Backing = lvalue(List->Backing);
        assign(
            fieldStorage(json::Object(Place), List->List.Begin, L),
            decay(std::move(Backing), type(List->List.Begin->getType(), L), L),
            L);
        assign(fieldStorage(std::move(Place), List->List.Size, L),
               quantity(List->Size, type(List->List.Size->getType(), L), L), L);
        return;
      }
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
      const auto UtilityArray = approvedUtilityArrayRecord(
          A.S, A.Sources, Record, A.Context);
      if (UtilityArray && !UtilityArray->Size) {
        const auto *Storage =
            I->getNumInits() == 1
                ? dyn_cast<InitListExpr>(I->getInit(0)->IgnoreParens())
                : nullptr;
        if (Storage && Storage->isSyntacticForm() &&
            Storage->getSemanticForm())
          Storage = Storage->getSemanticForm();
        const auto *Filler =
            Storage
                ? dyn_cast_or_null<InitListExpr>(Storage->getArrayFiller())
                : nullptr;
        if (Filler && Filler->isSyntacticForm() && Filler->getSemanticForm())
          Filler = Filler->getSemanticForm();
        if (!Storage || Storage->getNumInits() || !Filler ||
            Filler->getNumInits() ||
            !A.Context.hasSameType(Storage->getType(),
                                   UtilityArray->Elements->getType()))
          reject(L, "empty array initialization",
                 "A zero-sized std::array requires its implicit empty "
                 "storage initializer.");
        initializeZero(std::move(Place), I->getType(), L);
        return;
      }
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
    const auto *D = DestroyedRecord->getDestructor();
    const bool UniquePtrDestructor =
        approvedUtilityUniquePtrDestructor(A.S, A.Sources, D, A.Context);
    if (approvedUtilityUniquePtrRecord(A.S, A.Sources, DestroyedRecord,
                                       A.Context) &&
        !UniquePtrDestructor)
      reject(D ? D->getLocation() : DestroyedRecord->getLocation(),
             "unique pointer destructor",
             "The pinned std::unique_ptr destructor definition is required.");
    if (D && !UniquePtrDestructor && !D->isImplicit() &&
        !defaultedLifecycle(D)) {
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
      if (auto Unique = approvedUtilityUniquePtrRecord(
              A.S, A.Sources, DestroyedRecord, A.Context)) {
        Expression Member{{"kind", "member"},
                          {"type", type(Unique->PointerType, L)},
                          {"name", "nct_unique_ptr_pointer"},
                          {"args", json::Array{dereference(*ThisPointer, L)}},
                          {"loc", A.loc(L)}};
        auto Pointer = snapshot(Member, L);
        assign(std::move(Member), A.zero(Unique->PointerType, L), L);
        deallocateUniquePtr(std::move(Pointer), json::Object(*ThisPointer),
                            *Unique, L);
      } else {
        destructionMembers();
      }
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
