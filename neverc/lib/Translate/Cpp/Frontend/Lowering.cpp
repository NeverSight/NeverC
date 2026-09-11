#include "Frontend.h"
#include "clang/AST/ExprCXX.h"
#include <algorithm>
#include <optional>
#include <set>

using namespace clang;
namespace nct {
using Expression = json::Object;

class FunctionLowering {
  Adapter &A;
  FunctionDecl *Function;
  json::Array Parameters, Locals, Body;
  std::map<const Decl *, Expression> Storage;
  std::map<std::string, std::vector<std::string>> Edges;
  std::vector<std::pair<std::string, std::string>> Loops;
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
    return A.literal(llvm::APSInt(llvm::APInt(32, 1), T == "uint"), T, L);
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
      if (const auto *M = dyn_cast<MaterializeTemporaryExpr>(E);
          M && M->getType()->isRecordType()) {
        // Give a trivial record temporary addressable storage for array decay
        // and subobject reads in its full expression. Reference lifetime
        // extension remains guarded by the source allowlist.
        return snapshot(expression(M->getSubExpr()), L);
      }
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
      if (const auto *C = dyn_cast<ConditionalOperator>(E); C && C->isLValue()) {
        auto Condition = expression(C->getCond());
        auto Yes = labelName(), No = labelName(), End = labelName();
        auto Reference = A.Context.getLValueReferenceType(E->getType());
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
      if (const auto *Call = dyn_cast<CallExpr>(E); Call && Call->isLValue())
        return expression(Call);
    }
    reject(E->getExprLoc(), "lvalue",
           "Only direct scalar/aggregate storage and field lvalues are "
           "supported.");
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
  Expression expression(const Expr *E) {
    auto L = E->getExprLoc();
    auto T = type(E->getType(), L, true);
    if (const auto *I = dyn_cast<IntegerLiteral>(E))
      return A.literal(llvm::APSInt(I->getValue(), T == "uint"), T, L);
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
          llvm::APSInt Value = Enumerator->getInitVal().extOrTrunc(32);
          Value.setIsUnsigned(T == "uint");
          return A.literal(Value, T, L);
        }
      return storage(R->getDecl(), L);
    }
    if (const auto *C = dyn_cast<CastExpr>(E)) {
      switch (C->getCastKind()) {
      case CK_LValueToRValue:
        return snapshot(cast(expression(C->getSubExpr()), T, L), L);
      case CK_NoOp:
        if (A.S.coreV2() && C->isLValue())
          return lvalue(C);
        return cast(expression(C->getSubExpr()), T, L);
      case CK_IntegralCast:
      case CK_IntegralToBoolean:
        return cast(expression(C->getSubExpr()), T, L);
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
      if (A.S.coreV2() && M->isLValue())
        return lvalue(M);
      return project(M->getBase(), {llvm::cast<FieldDecl>(M->getMemberDecl())},
                     T, L);
    }
    if (A.S.coreV2() && isa<ArraySubscriptExpr>(E))
      return lvalue(E);
    if (isa<ImplicitValueInitExpr, CXXScalarValueInitExpr>(E))
      return A.zero(E->getType(), L);
    if (const auto *I = dyn_cast<InitListExpr>(E)) {
      if (I->isSyntacticForm() && I->getSemanticForm())
        I = I->getSemanticForm();
      if (const auto *Array = A.Context.getAsConstantArrayType(E->getType());
          Array && A.S.coreV2()) {
        json::Array Values;
        auto Count = Array->getSize().getLimitedValue(65537);
        if (I->getNumInits() > Count)
          reject(L, "array initialization", "Too many semantic initializers.");
        A.chargeExpansion(2, L);
        for (uint64_t N = 0; N < Count; ++N) {
          const Expr *Init = N < I->getNumInits() ? I->getInit(unsigned(N))
                                                 : I->getArrayFiller();
          auto Value = Init ? expression(Init) : A.zero(Array->getElementType(), L);
          A.chargeExpansion(generatedNodes(Value), L);
          Values.push_back(std::move(Value));
        }
        return Expression{{"kind", "aggregate"}, {"type", T},
                          {"args", std::move(Values)}, {"loc", A.loc(L)}};
      }
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
      return expression(M->getSubExpr());
    if (const auto *W = dyn_cast<ExprWithCleanups>(E))
      return expression(W->getSubExpr());
    if (const auto *B = dyn_cast<CXXBindTemporaryExpr>(E))
      return expression(B->getSubExpr());
    if (const auto *C = dyn_cast<CXXConstructExpr>(E)) {
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
          Method->isTrivial() && Call->getNumArgs() == 2) {
        auto Right = expression(Call->getArg(1));
        auto Left = lvalue(Call->getArg(0));
        assign(Left, std::move(Right), L);
        return Left;
      }
      reject(L, "overloaded operator",
             "Only implicit trivial aggregate copy assignment is supported.");
    }
    if (const auto *Call = dyn_cast<CallExpr>(E)) {
      auto *Callee = Call->getDirectCallee();
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
      if (!Callee ||
          (!Callee->hasBody() &&
           (!A.S.project() ||
            Callee->getFormalLinkage() == Linkage::Internal)) ||
          isa<CXXMethodDecl>(Callee))
        reject(L, "call",
               "Call target is not a supported defined free function.");
      json::Array Args;
      for (unsigned I = 0; I < Call->getNumArgs(); ++I) {
        const auto *Arg = Call->getArg(I);
        auto ParameterType = Callee->getParamDecl(I)->getType();
        if (ParameterType->isLValueReferenceType())
          Args.push_back(snapshot(bind(Arg, ParameterType), Arg->getExprLoc()));
        else
          Args.push_back(expression(Arg));
      }
      json::Object Instruction{{"op", "call"},
                               {"callee", A.name(Callee)},
                               {"args", std::move(Args)},
                               {"loc", A.loc(L)}};
      Expression Result;
      auto ResultType = type(Callee->getReturnType(), L, true);
      if (ResultType != "void") {
        Result = temporary(ResultType, L);
        Instruction["target"] = json::Object(Result);
      }
      Body.push_back(std::move(Instruction));
      if (Callee->getReturnType()->isLValueReferenceType())
        return dereference(std::move(Result), L);
      return Result;
    }
    if (const auto *U = dyn_cast<UnaryOperator>(E)) {
      if (A.S.coreV2() && U->getOpcode() == UO_AddrOf)
        return address(lvalue(U->getSubExpr()), U->getSubExpr()->getType(), L);
      if (A.S.coreV2() && U->getOpcode() == UO_Deref)
        return lvalue(U);
      if (U->isIncrementDecrementOp()) {
        auto Place = lvalue(U->getSubExpr());
        auto Old = snapshot(Place, L);
        auto New = binary(U->isIncrementOp() ? "+" : "-", Old, one(T, L), T, L);
        assign(Place, std::move(New), L);
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
      if (A.S.coreV2() && C->isLValue())
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
  void initialize(Expression Place, const Expr *Init, SourceLocation L) {
    Init = Init->IgnoreParens();
    if (const auto *W = dyn_cast<ExprWithCleanups>(Init)) {
      initialize(std::move(Place), W->getSubExpr(), L);
      return;
    }
    if (const auto *C = dyn_cast<ConstantExpr>(Init); C && A.S.coreV2()) {
      initialize(std::move(Place), C->getSubExpr(), L);
      return;
    }
    if (const auto *C = dyn_cast<CastExpr>(Init);
        A.S.coreV2() && C && C->getCastKind() == CK_NoOp && C->isPRValue() &&
        C->getType()->isRecordType() &&
        A.Context.hasSameUnqualifiedType(C->getType(), C->getSubExpr()->getType())) {
      // C++17 initializes the destination directly through typed construction
      // wrappers such as R r = R{1, r.first}. A real copy still takes the value
      // path once recursion reaches its constructor or source object.
      initialize(std::move(Place), C->getSubExpr(), L);
      return;
    }
    if (A.S.coreV2() && Init->isPRValue() && Init->getType()->isRecordType()) {
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
      auto Fields = I->getType()->getAsCXXRecordDecl()->getDefinition()->fields();
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
  void declaration(const VarDecl *V) {
    auto L = V->getLocation();
    auto Place = temporary(type(V->getType(), L), L);
    if (V->getType()->isLValueReferenceType()) {
      Storage.emplace(V->getCanonicalDecl(), dereference(Place, L));
      assign(std::move(Place), bind(V->getInit(), V->getType()), L);
      return;
    }
    Storage.emplace(V->getCanonicalDecl(), Place);
    if (!V->getInit())
      return;
    if (const auto *C = dyn_cast<CXXConstructExpr>(V->getInit());
        C && C->getConstructor()->isDefaultConstructor() &&
        C->getConstructor()->isTrivial() && !C->getNumArgs() &&
        !C->requiresZeroInitialization())
      return;
    initialize(std::move(Place), V->getInit(), L);
  }
  void statement(const Stmt *S) {
    if (!S || !Open)
      return; // The independent allowlist still inspects dead code.
    auto L = S->getBeginLoc();
    if (const auto *C = dyn_cast<CompoundStmt>(S)) {
      for (const auto *Child : C->body())
        statement(Child);
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
      if (R->getRetValue()) {
        auto Value = Function->getReturnType()->isLValueReferenceType()
                         ? bind(R->getRetValue(), Function->getReturnType())
                         : expression(R->getRetValue());
        if (!Value.empty())
          Return["value"] = std::move(Value);
      }
      Body.push_back(std::move(Return));
      Open = false;
    } else if (const auto *I = dyn_cast<IfStmt>(S)) {
      statement(I->getInit());
      if (I->getConditionVariable())
        declaration(I->getConditionVariable());
      auto Condition = expression(I->getCond());
      auto Yes = labelName(), No = labelName(), End = labelName();
      branch(std::move(Condition), Yes, No, L, I->getCond());
      label(Yes, L);
      statement(I->getThen());
      bool ThenOpen = Open;
      if (Open)
        jump(End, L);
      label(No, L);
      statement(I->getElse());
      bool ElseOpen = Open;
      if (Open)
        jump(End, L);
      if (ThenOpen || ElseOpen)
        label(End, L);
    } else if (const auto *W = dyn_cast<WhileStmt>(S)) {
      auto Test = labelName(), Loop = labelName(), End = labelName();
      jump(Test, L);
      label(Test, L);
      if (W->getConditionVariable())
        declaration(W->getConditionVariable());
      branch(expression(W->getCond()), Loop, End, L, W->getCond());
      label(Loop, L);
      Loops.emplace_back(End, Test);
      statement(W->getBody());
      Loops.pop_back();
      if (Open)
        jump(Test, L);
      label(End, L);
    } else if (const auto *D = dyn_cast<DoStmt>(S)) {
      auto Loop = labelName(), Test = labelName(), End = labelName();
      jump(Loop, L);
      label(Loop, L);
      Loops.emplace_back(End, Test);
      statement(D->getBody());
      Loops.pop_back();
      if (Open)
        jump(Test, L);
      label(Test, L);
      branch(expression(D->getCond()), Loop, End, L, D->getCond());
      label(End, L);
    } else if (const auto *F = dyn_cast<ForStmt>(S)) {
      statement(F->getInit());
      auto Test = labelName(), Loop = labelName(), Step = labelName(),
           End = labelName();
      jump(Test, L);
      label(Test, L);
      if (F->getConditionVariable())
        declaration(F->getConditionVariable());
      if (F->getCond())
        branch(expression(F->getCond()), Loop, End, L, F->getCond());
      else
        jump(Loop, L);
      label(Loop, L);
      Loops.emplace_back(End, Step);
      statement(F->getBody());
      Loops.pop_back();
      if (Open)
        jump(Step, L);
      label(Step, L);
      if (F->getInc())
        discard(F->getInc());
      jump(Test, L);
      label(End, L);
    } else if (isa<BreakStmt, ContinueStmt>(S)) {
      if (Loops.empty())
        reject(L, "loop control", "No enclosing supported loop.");
      jump(isa<BreakStmt>(S) ? Loops.back().first : Loops.back().second, L);
    } else if (const auto *E = dyn_cast<Expr>(S)) {
      discard(E);
    } else if (!isa<NullStmt>(S)) {
      reject(L, S->getStmtClassName(),
             "Statement has no supported core lowering.");
    }
  }

public:
  FunctionLowering(Adapter &A, FunctionDecl *F) : A(A), Function(F) {
    Prefix = "nct_f" + digest(A.name(F)).substr(0, 12) + "_";
  }
  json::Object run() {
    auto L = Function->getLocation();
    auto ResultType = type(Function->getReturnType(), L, true);
    for (const auto *P : Function->parameters()) {
      auto Name = Prefix + "p" + std::to_string(++Serial);
      auto T = type(P->getType(), P->getLocation());
      Parameters.push_back(json::Object{
          {"name", Name}, {"type", T}, {"loc", A.loc(P->getLocation())}});
      auto Place = variable(Name, T, P->getLocation());
      Storage.emplace(P->getCanonicalDecl(),
                      P->getType()->isLValueReferenceType()
                          ? dereference(std::move(Place), P->getLocation())
                          : std::move(Place));
    }
    Entry = labelName();
    label(Entry, L);
    statement(Function->getBody());
    if (Open && reachable().count(Current)) {
      if (Function->isMain()) {
        Body.push_back(json::Object{{"op", "return"},
                                    {"value", A.zero(A.Context.IntTy, L)},
                                    {"loc", A.loc(L)}});
      } else if (ResultType == "void") {
        Body.push_back(json::Object{{"op", "return"}, {"loc", A.loc(L)}});
      } else
        reject(L, "function return",
               "A reachable nonvoid function path falls through without "
               "returning.");
    }
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
        {"name", A.name(Function)},
        {"result", ResultType},
        {"internal", Function->getFormalLinkage() == Linkage::Internal},
        {"c_export", Function->isExternC() &&
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
} // namespace nct
