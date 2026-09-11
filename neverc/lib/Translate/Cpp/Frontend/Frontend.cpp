#include "Frontend.h"
#include "FrontendEntry.h"
#include "BuildID.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <set>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace clang;
namespace nct {

std::string digest(llvm::StringRef Text) {
  return llvm::toHex(llvm::SHA256::hash(llvm::arrayRefFromStringRef(Text)),
                     true);
}

bool validExportName(llvm::StringRef N) {
  if (N.empty() || N.starts_with("_") || N.starts_with("nct_") ||
      N.starts_with("nc_") || N.starts_with("neverc_") || N.contains("__"))
    return false;
  for (char C : N)
    if (!llvm::isAlnum(C) && C != '_')
      return false;
  if (llvm::isDigit(N.front()))
    return false;
  static const std::set<std::string> Reserved{
      "auto",          "bool",      "break",         "case",         "char",
      "const",         "constexpr", "continue",      "default",      "do",
      "double",        "else",      "enum",          "extern",       "false",
      "float",         "for",       "goto",          "if",           "inline",
      "int",           "long",      "nullptr",       "register",     "restrict",
      "return",        "short",     "signed",        "sizeof",       "static",
      "static_assert", "struct",    "switch",        "thread_local", "true",
      "typedef",       "typeof",    "typeof_unqual", "union",        "unsigned",
      "void",          "volatile",  "while",         "alignas",      "alignof",
      "string",        "i8",        "i16",           "i32",          "i64",
      "i128",          "u8",        "u16",           "u32",          "u64",
      "u128",          "isize",     "usize"};
  return !Reserved.count(N.str());
}

void State::diagnose(llvm::StringRef Code, llvm::StringRef Construct,
                     llvm::StringRef Reason, llvm::StringRef Guidance,
                     unsigned Line, unsigned Column, llvm::StringRef File) {
  if (Diagnostics.size() >= 100)
    return;
  Diagnostics.push_back(json::Object{
      {"code", Code.str()},
      {"file",
       File.empty() ? (Relative.empty() ? Source : Relative) : File.str()},
      {"line", Line ? Line : 1},
      {"column", Column ? Column : 1},
      {"construct", Construct.str()},
      {"reason", Reason.str()},
      {"guidance", Guidance.str()}});
}

json::Object Adapter::loc(SourceLocation L) const {
  auto P = Sources.getPresumedLoc(Sources.getExpansionLoc(L));
  auto File = S.project() ? S.sourcePath(Sources, L) : S.Relative;
  return json::Object{{"file", File.empty() ? S.Relative : File},
                      {"line", P.isValid() ? P.getLine() : 1},
                      {"column", P.isValid() ? P.getColumn() : 1}};
}

void Adapter::reject(SourceLocation L, llvm::StringRef Construct,
                     llvm::StringRef Reason, llvm::StringRef Code) {
  auto P = Sources.getPresumedLoc(Sources.getExpansionLoc(L));
  S.diagnose(
      Code, Construct, Reason,
      "Rewrite using the documented constructs in the selected profile.",
      P.isValid() ? P.getLine() : 1, P.isValid() ? P.getColumn() : 1,
      S.project() ? S.sourcePath(Sources, L) : S.Relative);
}

std::string Adapter::name(const NamedDecl *D) {
  auto *Canonical = D->getCanonicalDecl();
  auto Found = Names.find(Canonical);
  if (Found != Names.end())
    return Found->second;
  std::string Name;
  if (const auto *F = dyn_cast<FunctionDecl>(D);
      F && ((F->isExternC() && F->getFormalLinkage() != Linkage::Internal) ||
            F->isMain())) {
    Name = F->getNameAsString();
  } else {
    Name = "nct_" + digest(identity(D)).substr(0, 24);
  }
  Names.emplace(Canonical, Name);
  return Name;
}

std::size_t Adapter::storageUnits(QualType T, unsigned Depth) {
  constexpr std::size_t Limit = 200000;
  if (Depth > 64)
    return Limit + 1;
  if (const auto *Array = Context.getAsConstantArrayType(T)) {
    auto Count = Array->getSize().getLimitedValue(65537);
    auto Element = storageUnits(Array->getElementType(), Depth + 1);
    return !Count || Count > 65536 || Element > Limit / Count
               ? Limit + 1 : Element * Count;
  }
  if (const auto *R = T->getAsCXXRecordDecl(); R && R->getDefinition()) {
    R = R->getDefinition();
    auto Found = StorageUnits.find(R);
    if (Found != StorageUnits.end())
      return Found->second;
    StorageUnits.emplace(R, Limit + 1);
    std::size_t Units = 0;
    for (const auto *F : R->fields()) {
      auto Added = storageUnits(F->getType(), Depth + 1);
      if (Added > Limit - Units)
        return Limit + 1;
      Units += Added;
    }
    StorageUnits[R] = Units;
    return Units;
  }
  // Pointers/references consume storage but do not expand their pointees.
  return 1;
}

void Adapter::chargeExpansion(std::size_t Nodes, SourceLocation L) {
  if (!S.coreV2())
    return;
  constexpr std::size_t Limit = 200000;
  if (Nodes > Limit - ExpandedNodes) {
    reject(L, "initialization expansion",
           "Expanded initializer/assignment nodes exceed the frontend budget.");
    throw Failure{};
  }
  ExpandedNodes += Nodes;
}

std::string Adapter::type(QualType T, SourceLocation L, bool AllowVoid,
                          unsigned Depth) {
  if (Depth > 64) {
    reject(L, "type", "Pointer type nesting exceeds the protocol limit.");
    return {};
  }
  if (T.isVolatileQualified() || T->isAtomicType() || T.isRestrictQualified()) {
    reject(L, "qualified type",
           "Volatile, atomic and restrict-qualified types are unsupported.");
    return {};
  }
  const Type *C = T.getCanonicalType().getTypePtr();
  if (S.coreV2())
    if (const auto *Array = Context.getAsConstantArrayType(T)) {
      auto Count = Array->getSize().getLimitedValue(65537);
      if (!Count || Count > 65536 || storageUnits(T) > 200000) {
        reject(L, "array extent", "Fixed arrays exceed the extent/storage limit.");
        return {};
      }
      auto Element = type(Array->getElementType(), L, false, Depth + 1);
      if (Element.empty())
        return {};
      return "arr:" + std::to_string(Count) + ":" + Element;
    }
  if (S.coreV2() && (C->isPointerType() || C->isLValueReferenceType())) {
    QualType Pointee = C->getPointeeType();
    auto Element = type(Pointee, L, C->isPointerType(), Depth + 1);
    if (Element.empty())
      return {};
    return std::string(Pointee.isConstQualified() ? "cptr:" : "ptr:") + Element;
  }
  if (const auto *B = dyn_cast<BuiltinType>(C)) {
    switch (B->getKind()) {
    case BuiltinType::Int:
      return "int";
    case BuiltinType::UInt:
      return "uint";
    case BuiltinType::Bool:
      return "bool";
    case BuiltinType::Double:
      if (S.math())
        return "double";
      break;
    case BuiltinType::Void:
      if (AllowVoid)
        return "void";
      break;
    default:
      break;
    }
  }
  if (S.coreV2())
    if (const auto *E = dyn_cast<EnumType>(C)) {
      QualType Underlying = E->getDecl()->getIntegerType();
      if (!Underlying.isNull() &&
          (Underlying->isSpecificBuiltinType(BuiltinType::Int) ||
           Underlying->isSpecificBuiltinType(BuiltinType::UInt)))
        return type(Underlying, L);
      reject(L, "enum underlying type",
             "Core v2 enums require an established int or unsigned int "
             "underlying type.");
      return {};
    }
  if (const auto *R = C->getAs<RecordType>()) {
    const auto *D = dyn_cast<CXXRecordDecl>(R->getDecl());
    if (D && D->getDefinition() && !D->isUnion())
      return name(D);
  }
  reject(L, "type",
         "Only the documented scalar, record and core v2 pointer/reference "
         "types are admitted.");
  return {};
}

json::Object Adapter::literal(const llvm::APSInt &V, llvm::StringRef T,
                              SourceLocation L) {
  json::Object O{{"kind", "literal"}, {"type", T.str()}, {"loc", loc(L)}};
  if (T == "bool")
    O["value"] = !V.isZero();
  else {
    llvm::SmallString<32> Text;
    V.toString(Text, 10);
    O["value"] = Text.str().str();
  }
  return O;
}

json::Object Adapter::zero(QualType T, SourceLocation L) {
  std::string Kind = type(T, L);
  if (Kind.empty())
    throw Failure{};
  chargeExpansion(2 + std::count(Kind.begin(), Kind.end(), ':'), L);
  if (const auto *Array = Context.getAsConstantArrayType(T); Array && S.coreV2()) {
    json::Array Values;
    auto Count = Array->getSize().getLimitedValue(65537);
    for (uint64_t I = 0; I < Count; ++I)
      Values.push_back(zero(Array->getElementType(), L));
    return json::Object{{"kind", "aggregate"}, {"type", Kind},
                        {"args", std::move(Values)}, {"loc", loc(L)}};
  }
  if (Kind == "double")
    return floatingLiteral(llvm::APFloat::getZero(llvm::APFloat::IEEEdouble()),
                           L);
  if (T->isPointerType())
    return json::Object{{"kind", "null"}, {"type", Kind}, {"loc", loc(L)}};
  if (const auto *R = T->getAsCXXRecordDecl()) {
    json::Array Args;
    for (const auto *F : R->getDefinition()->fields())
      Args.push_back(zero(F->getType(), L));
    return json::Object{{"kind", "aggregate"},
                        {"type", Kind},
                        {"args", std::move(Args)},
                        {"loc", loc(L)}};
  }
  return literal(llvm::APSInt(32, Kind == "uint"), Kind, L);
}

json::Object Adapter::constant(const APValue &V, QualType T, SourceLocation L) {
  auto Kind = type(T, L);
  if (V.isInt())
    return literal(V.getInt(), Kind, L);
  if (V.isFloat() && S.math())
    return floatingLiteral(V.getFloat(), L);
  if (V.isStruct()) {
    json::Array Args;
    unsigned I = 0;
    for (const auto *F : T->getAsCXXRecordDecl()->getDefinition()->fields())
      Args.push_back(constant(V.getStructField(I++), F->getType(), L));
    return json::Object{{"kind", "aggregate"},
                        {"type", Kind},
                        {"args", std::move(Args)},
                        {"loc", loc(L)}};
  }
  reject(L, "constant initializer",
         "Initializer is not a fully defined scalar/aggregate constant.");
  throw Failure{};
}

class Allowlist : public RecursiveASTVisitor<Allowlist> {
  Adapter &A;
  std::map<const CXXRecordDecl *, bool> HasArray;
  bool owned(const Decl *D) {
    return !D->isImplicit() && A.S.owns(A.Sources, D->getLocation());
  }
  // Follow the identity of an lvalue, not arbitrary call arguments. A by-value
  // temporary passed to a function returning some other live object is safe.
  bool temporaryArrayBase(const Expr *E) {
    E = E->IgnoreParens();
    if (E->getType()->isArrayType())
      return temporaryBinding(E);
    if (const auto *C = dyn_cast<CastExpr>(E)) {
      if (C->getCastKind() == CK_ArrayToPointerDecay)
        return temporaryBinding(C->getSubExpr());
      if (C->getCastKind() == CK_NoOp || C->getCastKind() == CK_BitCast)
        return temporaryArrayBase(C->getSubExpr());
    }
    if (const auto *W = dyn_cast<ExprWithCleanups>(E))
      return temporaryArrayBase(W->getSubExpr());
    if (const auto *C = dyn_cast<ConditionalOperator>(E))
      return temporaryArrayBase(C->getTrueExpr()) ||
             temporaryArrayBase(C->getFalseExpr());
    if (const auto *B = dyn_cast<BinaryOperator>(E);
        B && B->getOpcode() == BO_Comma)
      return temporaryArrayBase(B->getRHS());
    if (const auto *U = dyn_cast<UnaryOperator>(E); U && U->getOpcode() == UO_AddrOf)
      return temporaryBinding(U->getSubExpr());
    // A pointer prvalue (including a call result) is not a temporary pointee.
    return false;
  }
  bool temporaryBinding(const Expr *E) {
    E = E->IgnoreParens();
    if (isa<MaterializeTemporaryExpr, CXXBindTemporaryExpr>(E))
      return true;
    if (const auto *C = dyn_cast<CastExpr>(E))
      return temporaryBinding(C->getSubExpr());
    if (const auto *W = dyn_cast<ExprWithCleanups>(E))
      return temporaryBinding(W->getSubExpr());
    if (const auto *M = dyn_cast<MemberExpr>(E))
      return !M->isArrow() && temporaryBinding(M->getBase());
    if (const auto *Index = dyn_cast<ArraySubscriptExpr>(E))
      return temporaryArrayBase(Index->getBase());
    if (const auto *C = dyn_cast<ConditionalOperator>(E))
      return temporaryBinding(C->getTrueExpr()) ||
             temporaryBinding(C->getFalseExpr());
    if (const auto *B = dyn_cast<BinaryOperator>(E)) {
      if (B->getOpcode() == BO_Comma)
        return temporaryBinding(B->getRHS());
      if (B->isAssignmentOp())
        return temporaryBinding(B->getLHS());
    }
    if (const auto *U = dyn_cast<UnaryOperator>(E)) {
      if (U->getOpcode() == UO_Deref)
        return temporaryArrayBase(U->getSubExpr());
      if (U->isIncrementDecrementOp())
        return temporaryBinding(U->getSubExpr());
    }
    return !E->isLValue();
  }
  bool containsArray(QualType T, unsigned Depth = 0) {
    if (Depth > 64 || T->isArrayType())
      return true;
    if (const auto *R = T->getAsCXXRecordDecl(); R && R->getDefinition()) {
      R = R->getDefinition();
      auto Found = HasArray.find(R);
      if (Found != HasArray.end())
        return Found->second;
      HasArray.emplace(R, true);
      for (const auto *F : R->getDefinition()->fields())
        if (containsArray(F->getType(), Depth + 1))
          return true;
      HasArray[R] = false;
    }
    return false;
  }
  void checkBinding(const Expr *E) {
    if (E && temporaryBinding(E))
      A.reject(E->getExprLoc(), "reference binding",
               "Binding references to temporaries requires lifetime lowering.");
  }

public:
  explicit Allowlist(Adapter &A) : A(A) {}
  bool VisitDecl(Decl *D) {
    if (!owned(D))
      return true;
    if (D->hasAttrs())
      A.reject(D->getLocation(), "attribute",
               "Source declaration attributes are unsupported.");
    const bool ExtendedDeclaration =
        A.S.coreV2() &&
        isa<TypedefNameDecl, EnumDecl, EnumConstantDecl, StaticAssertDecl>(D);
    if (!ExtendedDeclaration &&
        !isa<NamespaceDecl, LinkageSpecDecl, FunctionDecl, VarDecl,
             CXXRecordDecl, FieldDecl>(D))
      A.reject(D->getLocation(), D->getDeclKindName(),
               "Declaration is outside the selected profile.");
    if (const auto *N = dyn_cast<NamespaceDecl>(D); N && N->isInline())
      A.reject(D->getLocation(), "inline namespace",
               "Inline namespaces are not in the core profile.");
    if (A.S.math())
      if (const auto *N = dyn_cast<NamespaceDecl>(D); N && N->isStdNamespace())
        A.reject(D->getLocation(), "owned std namespace",
                 "Owned declarations cannot extend or impersonate an approved "
                 "standard-library namespace.");
    return true;
  }
  bool VisitTypedefNameDecl(TypedefNameDecl *D) {
    if (owned(D) && A.S.coreV2())
      A.type(D->getUnderlyingType(), D->getLocation(), true);
    return true;
  }
  bool VisitEnumDecl(EnumDecl *D) {
    if (owned(D) && A.S.coreV2())
      A.type(A.Context.getTypeDeclType(D), D->getLocation());
    return true;
  }
  bool TraverseStaticAssertDecl(StaticAssertDecl *D) {
    if (!A.S.coreV2())
      return RecursiveASTVisitor<Allowlist>::TraverseStaticAssertDecl(D);
    if (!WalkUpFromStaticAssertDecl(D))
      return false;
    // Sema validates the assertion. Still inspect its condition so constant
    // evaluation cannot conceal unsupported source. The message is diagnostic
    // text, not a runtime string expression to translate.
    return TraverseStmt(D->getAssertExpr());
  }
  bool VisitFunctionDecl(FunctionDecl *D) {
    if (!owned(D))
      return true;
    A.type(D->getReturnType(), D->getLocation(), true);
    if (isa<CXXMethodDecl>(D) || D->isVariadic() ||
        D->getDescribedFunctionTemplate() ||
        D->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
        D->isDeletedAsWritten() || D->isExplicitlyDefaulted() ||
        D->isConsteval())
      A.reject(D->getLocation(), "function",
               "Members, templates, variadics and special function forms are "
               "unsupported.");
    const auto *Prototype = D->getType()->getAs<FunctionProtoType>();
    if (Prototype && Prototype->hasExceptionSpec())
      A.reject(D->getLocation(), "exception specification",
               "Exception specifications are outside the core profile.");
    if (D->isMain() &&
        (D->getNumParams() ||
         !D->getReturnType()->isSpecificBuiltinType(BuiltinType::Int)))
      A.reject(D->getLocation(), "main",
               "Only no-argument int main() is supported.");
    if (D->isExternC() &&
        (!A.S.project() || D->getFormalLinkage() != Linkage::Internal)) {
      if (!validExportName(D->getName()) || D->getName() == "main")
        A.reject(D->getLocation(), "C export",
                 "C export name is reserved or incompatible with NeverC.");
      if (D->getReturnType()->isRecordType())
        A.reject(D->getLocation(), "C export",
                 "C ABI exports require scalar results and parameters.");
      for (const auto *P : D->parameters())
        if (P->getType()->isRecordType())
          A.reject(P->getLocation(), "C export",
                   "C ABI exports require scalar results and parameters.");
    }
    if (!D->hasBody() &&
        (!A.S.project() || D->getFormalLinkage() == Linkage::Internal))
      A.reject(
          D->getLocation(), "function declaration",
          "Every declared function needs a definition in this source unit.",
          "TR0203");
    if (A.S.project()) {
      auto &Declaration = A.FunctionDeclarations[D->getCanonicalDecl()];
      if (!Declaration || D->doesThisDeclarationHaveABody())
        Declaration = D;
    }
    if (D->doesThisDeclarationHaveABody() && !D->isImplicit())
      A.Functions.push_back(D);
    return true;
  }
  bool VisitVarDecl(VarDecl *D) {
    if (!owned(D))
      return true;
    A.type(D->getType(), D->getLocation());
    if (A.S.coreV2() && D->getType()->isReferenceType() && D->getInit())
      checkBinding(D->getInit());
    if (!D->isLocalVarDeclOrParm() &&
        (D->getType()->isPointerType() || D->getType()->isReferenceType() ||
         (A.S.coreV2() && containsArray(D->getType())))) {
      A.reject(D->getLocation(), "global variable",
               "Pointer/reference/array globals require global lifetime lowering.");
      return true;
    }
    if (D->getTLSKind() != VarDecl::TLS_None || D->isStaticLocal() ||
        (D->isLocalVarDecl() && D->hasExternalStorage()))
      A.reject(D->getLocation(), "storage duration",
               "Static/thread-local locals and local extern declarations are "
               "unsupported.");
    if (const auto *P = dyn_cast<ParmVarDecl>(D); P && P->hasDefaultArg())
      A.reject(D->getLocation(), "default argument",
               "Default arguments are outside the core profile.");
    if (!D->isLocalVarDeclOrParm()) {
      if (A.S.project()) {
        auto &Declaration = A.GlobalDeclarations[D->getCanonicalDecl()];
        if (!Declaration || D->getInit())
          Declaration = D;
      }
      if (A.S.project() && D->getType().isConstQualified() && !D->getInit() &&
          D->hasExternalStorage() && D->getFormalLinkage() != Linkage::Internal)
        return true;
      if (!D->getType().isConstQualified() || !D->getInit()) {
        A.reject(D->getLocation(), "global variable",
                 "Only initialized compile-time const globals are supported.");
      } else {
        APValue Value;
        if (!D->getInit()->isCXX11ConstantExpr(A.Context, &Value))
          A.reject(D->getLocation(), "global initializer",
                   "Dynamic or partially undefined global initialization is "
                   "unsupported.");
        else
          A.Globals.push_back(D);
      }
    }
    return true;
  }
  bool VisitCXXRecordDecl(CXXRecordDecl *D) {
    if (!owned(D))
      return true;
    if (!D->isCompleteDefinition()) {
      if (!D->getDefinition())
        A.reject(D->getLocation(), "record",
                 "Incomplete records are unsupported.");
      return true;
    }
    if (D->isUnion() || !D->isAggregate() || D->field_empty() ||
        D->getNumBases() || D->getDescribedClassTemplate() ||
        D->getDeclContext()->isRecord())
      A.reject(D->getLocation(), "record",
               "Only nonempty, unnested trivial aggregates without bases are "
               "supported.");
    A.Records.push_back(D);
    return true;
  }
  bool VisitFieldDecl(FieldDecl *D) {
    if (!owned(D))
      return true;
    A.type(D->getType(), D->getLocation());
    if (D->isBitField() || D->hasInClassInitializer() || D->isMutable() ||
        D->getType().isConstQualified() || D->getType()->isReferenceType())
      A.reject(D->getLocation(), "field",
               "Bitfields, mutable/const/reference fields and default field "
               "initializers are unsupported.");
    return true;
  }
  bool VisitStmt(Stmt *S) {
    if (!S || !A.S.owns(A.Sources, S->getBeginLoc()))
      return true;
    if (const auto *E = dyn_cast<Expr>(S)) {
      if (const auto *WrittenCast = dyn_cast<ExplicitCastExpr>(E))
        A.type(WrittenCast->getTypeAsWritten(), E->getExprLoc(), true);
      if (A.S.coreV2())
        if (const auto *C = dyn_cast<CastExpr>(E)) {
          // Enum initializers and static assertions are erased after checking.
          // Validate their operations before erasure, not only in lowering.
          switch (C->getCastKind()) {
          case CK_LValueToRValue:
          case CK_NoOp:
          case CK_IntegralCast:
          case CK_IntegralToBoolean:
          case CK_FunctionToPointerDecay:
          case CK_NullToPointer:
          case CK_PointerToBoolean:
          case CK_ArrayToPointerDecay:
            break;
          case CK_BitCast:
            if (C->getType()->isPointerType() &&
                C->getSubExpr()->getType()->isPointerType() &&
                (C->getType()->getPointeeType()->isVoidType() ||
                 C->getSubExpr()->getType()->getPointeeType()->isVoidType()))
              break;
            [[fallthrough]];
          default:
            A.reject(E->getExprLoc(), "cast",
                     "This cast operation is outside the core v2 profile.");
          }
        }
      const auto *Cast = dyn_cast<ImplicitCastExpr>(E);
      bool FunctionDecay =
          Cast && Cast->getCastKind() == CK_FunctionToPointerDecay;
      if (!FunctionDecay && !E->getType()->isFunctionType() &&
          !(A.S.coreV2() &&
            isa<CXXNullPtrLiteralExpr>(E->IgnoreParens())))
        A.type(E->getType(), E->getExprLoc(), true);
    }
    if (!(A.S.math() && isa<FloatingLiteral>(S)) &&
        !(A.S.coreV2() &&
          isa<ConstantExpr, CXXNullPtrLiteralExpr, CXXConstCastExpr,
              CXXFunctionalCastExpr, ArraySubscriptExpr, SwitchStmt, CaseStmt,
              DefaultStmt, AttributedStmt>(S)) &&
        !isa<CompoundStmt, DeclStmt, NullStmt, ReturnStmt, IfStmt, WhileStmt,
             DoStmt, ForStmt, BreakStmt, ContinueStmt, IntegerLiteral,
             CXXBoolLiteralExpr, DeclRefExpr, ParenExpr, ImplicitCastExpr,
             CStyleCastExpr, CXXStaticCastExpr, BinaryOperator, UnaryOperator,
             CallExpr, MemberExpr, InitListExpr, ImplicitValueInitExpr,
             CXXScalarValueInitExpr, CXXConstructExpr, CXXTemporaryObjectExpr,
             MaterializeTemporaryExpr, ExprWithCleanups, CXXBindTemporaryExpr,
             ConditionalOperator>(S))
      A.reject(S->getBeginLoc(), S->getStmtClassName(),
               "Expression or statement is outside the selected profile.");
    if (A.S.coreV2()) {
      if (const auto *C = dyn_cast<CaseStmt>(S); C && C->getRHS())
        A.reject(S->getBeginLoc(), "case range",
                 "GNU case ranges are outside the core v2 profile.");
      if (const auto *Attributed = dyn_cast<AttributedStmt>(S)) {
        if (!isa<NullStmt>(Attributed->getSubStmt()) ||
            Attributed->getAttrs().empty() ||
            !std::all_of(Attributed->getAttrs().begin(),
                         Attributed->getAttrs().end(),
                         [](const Attr *A) { return isa<FallThroughAttr>(A); }))
          A.reject(S->getBeginLoc(), "statement attribute",
                   "Only a validated fallthrough annotation is supported.");
      }
    }
    if (const auto *C = dyn_cast<CallExpr>(S)) {
      const auto *F = C->getDirectCallee();
      if (A.S.coreV2() && F && !isa<CXXMethodDecl>(F))
        for (unsigned I = 0; I < C->getNumArgs() && I < F->getNumParams(); ++I)
          if (F->getParamDecl(I)->getType()->isReferenceType())
            checkBinding(C->getArg(I));
      if (A.S.math() && F &&
          (F->getBuiltinID() || !A.S.owns(A.Sources, F->getLocation()))) {
        if (A.mapping(C).empty())
          A.reject(S->getBeginLoc(), "library call",
                   "Call is not an approved std::fabs(double) or "
                   "std::floor(double) import from the pinned SDK.",
                   "TR0203");
        return true;
      }
      if (!F ||
          (!F->isImplicit() && !F->hasBody() &&
           (!A.S.project() || F->getFormalLinkage() == Linkage::Internal ||
            F->isInlined())))
        A.reject(
            S->getBeginLoc(), "call",
            "Call target has no supported definition in this translation unit.",
            "TR0203");
    }
    if (A.S.project())
      if (const auto *Reference = dyn_cast<DeclRefExpr>(S))
        if (const auto *V = dyn_cast<VarDecl>(Reference->getDecl());
            V && !V->isLocalVarDeclOrParm() && V->isInline() &&
            !V->getDefinition())
          A.reject(S->getBeginLoc(), "inline global",
                   "A used inline variable requires a definition in this "
                   "translation unit.",
                   "TR0203");
    if (const auto *C = dyn_cast<CXXConstructExpr>(S))
      if (!C->getConstructor()->isImplicit() ||
          !C->getConstructor()->isTrivial())
        A.reject(
            S->getBeginLoc(), "construction",
            "Only implicit trivial aggregate construction/copy is supported.");
    if (const auto *U = dyn_cast<UnaryOperator>(S))
      if ((!A.S.coreV2() &&
           (U->getOpcode() == UO_AddrOf || U->getOpcode() == UO_Deref)) ||
          (U->isIncrementDecrementOp() && U->getType()->isPointerType()) ||
          U->getOpcode() == UO_Extension)
        A.reject(S->getBeginLoc(), "unary operator",
                 "This pointer operation or GNU extension is unsupported.");
    if (const auto *M = dyn_cast<MemberExpr>(S))
      if ((!A.S.coreV2() && M->isArrow()) ||
          !isa<FieldDecl>(M->getMemberDecl()))
        A.reject(S->getBeginLoc(), "member access",
                 "Only direct aggregate field access is supported.");
    if (A.S.coreV2()) {
      if (const auto *R = dyn_cast<ReturnStmt>(S);
          R && R->getRetValue() && R->getRetValue()->isGLValue())
        checkBinding(R->getRetValue());
      if (const auto *B = dyn_cast<BinaryOperator>(S);
          B && (B->getLHS()->getType()->isPointerType() ||
                B->getRHS()->getType()->isPointerType()) &&
          B->getOpcode() != BO_Assign && B->getOpcode() != BO_Comma &&
          B->getOpcode() != BO_EQ && B->getOpcode() != BO_NE)
        A.reject(S->getBeginLoc(), "pointer binary operator",
                 "Pointer arithmetic, difference and ordering are unsupported.");
    }
    if (A.S.math()) {
      if (const auto *U = dyn_cast<UnaryOperator>(S);
          U && U->getSubExpr()->getType()->isRealFloatingType() &&
          U->getOpcode() != UO_Plus && U->getOpcode() != UO_Minus)
        A.reject(S->getBeginLoc(), "floating unary operator",
                 "Only unary plus/minus are admitted on double values.");
      if (const auto *B = dyn_cast<BinaryOperator>(S);
          B &&
          (B->getLHS()->getType()->isRealFloatingType() ||
           B->getRHS()->getType()->isRealFloatingType()) &&
          B->getOpcode() != BO_Assign && B->getOpcode() != BO_Comma &&
          !B->isComparisonOp())
        A.reject(S->getBeginLoc(), "floating binary operator",
                 "Double arithmetic and compound assignments are outside the "
                 "bounded math profile.");
    }
    if (const auto *I = dyn_cast<IfStmt>(S); I && I->isConstexpr())
      A.reject(S->getBeginLoc(), "if constexpr",
               "Compile-time branches are outside the core profile.");
    return true;
  }
};

void Adapter::run() {
  Allowlist Check(*this);
  Check.TraverseDecl(Context.getTranslationUnitDecl());
  if (!S.Diagnostics.empty())
    return;
  const auto &Target = Context.getTargetInfo();
  if (Target.getIntWidth() != 32 || Target.getCharWidth() != 8 ||
      !Target.isLittleEndian()) {
    S.diagnose("TR0204", "data model",
               "Core requires 32-bit int, 8-bit bytes and little endian.",
               "Select an admitted native target.");
    return;
  }
  json::Array RecordData, GlobalData, FunctionData;
  for (const auto *R : Records) {
    json::Array Fields;
    for (const auto *F : R->fields())
      Fields.push_back(json::Object{
          {"name", name(F)}, {"type", type(F->getType(), F->getLocation())}});
    RecordData.push_back(json::Object{{"id", name(R)},
                                      {"fields", std::move(Fields)},
                                      {"loc", loc(R->getLocation())}});
  }
  for (const auto *G : Globals) {
    APValue Value;
    if (!G->getInit()->isCXX11ConstantExpr(Context, &Value))
      throw Failure{};
    GlobalData.push_back(
        json::Object{{"name", name(G)},
                     {"type", type(G->getType(), G->getLocation())},
                     {"value", constant(Value, G->getType(), G->getLocation())},
                     {"loc", loc(G->getLocation())}});
  }
  for (auto *F : Functions)
    FunctionData.push_back(lower(F));
  if (S.project())
    addProjectMetadata();
  if (S.math()) {
    if (Target.getDoubleWidth() != 64 ||
        &Target.getDoubleFormat() != &llvm::APFloat::IEEEdouble()) {
      S.diagnose("TR0204", "double data model",
                 "Math profile requires IEEE binary64 double.",
                 "Use an approved math target.");
      return;
    }
    S.addSDKMetadata();
  }
  if (!S.Diagnostics.empty())
    return;
  S.Module["target"] =
      json::Object{{"triple", S.Target},
                   {"int_bits", Target.getIntWidth()},
                   {"pointer_bits", Target.getPointerWidth(LangAS::Default)},
                   {"little_endian", Target.isLittleEndian()}};
  S.Module["records"] = std::move(RecordData);
  S.Module["globals"] = std::move(GlobalData);
  // The allowlist resolves every owned call, including unreachable source.
  // Lowering may prune its instruction, so publish evidence only for mappings
  // still referenced by the final bodies consumed by the shared verifier.
  std::set<std::string> UsedMappings;
  for (const auto &F : FunctionData)
    for (const auto &I : *F.getAsObject()->getArray("body")) {
      const auto &Instruction = *I.getAsObject();
      if (Instruction.getString("op") == "mapped_call")
        UsedMappings.insert(Instruction.getString("mapping")->str());
    }
  S.Module["functions"] = std::move(FunctionData);
  json::Array Mappings;
  for (const auto &M : MappedFunctions)
    if (UsedMappings.count(M.first))
      Mappings.push_back(json::Object(M.second));
  S.Module["mappings"] = std::move(Mappings);
  json::Array Dependencies;
  Dependencies.push_back(
      json::Object{{"path", S.Relative}, {"sha256", digest(S.SourceText)}});
  if (S.project())
    for (const auto &Dependency : S.Dependencies)
      if (Dependency.first != S.Relative)
        Dependencies.push_back(json::Object{{"path", Dependency.first},
                                            {"sha256", Dependency.second}});
  S.Module["dependencies"] = std::move(Dependencies);
}

class Diagnostics : public DiagnosticConsumer {
  State &S;

public:
  explicit Diagnostics(State &S) : S(S) {}
  void HandleDiagnostic(DiagnosticsEngine::Level Level,
                        const Diagnostic &D) override {
    DiagnosticConsumer::HandleDiagnostic(Level, D);
    if (Level < DiagnosticsEngine::Error)
      return;
    llvm::SmallString<256> Message;
    D.FormatDiagnostic(Message);
    unsigned Line = 1, Column = 1;
    std::string File;
    if (D.hasSourceManager()) {
      auto P = D.getSourceManager().getPresumedLoc(D.getLocation());
      if (P.isValid()) {
        Line = P.getLine();
        Column = P.getColumn();
      }
      if (S.project())
        File = S.sourcePath(D.getSourceManager(), D.getLocation());
    }
    S.diagnose("TR0202", "C++ source", Message,
               "Fix the C++17 source diagnostic before translating.", Line,
               Column, File);
  }
};

class PreprocessorPolicy : public PPCallbacks {
  State &S;
  SourceManager &SM;
  Preprocessor &PP;
  std::set<unsigned> Checked;
  std::set<std::string> OncePragmas;
  void reject(SourceLocation L, llvm::StringRef What,
              llvm::StringRef Reason =
                  "Unsupported or environment-sensitive preprocessing.",
              llvm::StringRef Code = "TR0201") {
    if (!S.owns(SM, L))
      return;
    auto P = SM.getPresumedLoc(SM.getExpansionLoc(L));
    S.diagnose(Code, What, Reason,
               S.project() ? "Use declared project-owned headers and "
                             "deterministic macro inputs."
                           : "Use a self-contained C++17 source with "
                             "deterministic macro inputs.",
               P.isValid() ? P.getLine() : 1, P.isValid() ? P.getColumn() : 1,
               S.project() ? S.sourcePath(SM, L) : S.Relative);
  }

public:
  PreprocessorPolicy(State &S, Preprocessor &PP)
      : S(S), SM(PP.getSourceManager()), PP(PP) {}
  void InclusionDirective(SourceLocation L, const Token &, llvm::StringRef,
                          bool, CharSourceRange, OptionalFileEntryRef File,
                          llvm::StringRef, llvm::StringRef, const Module *,
                          bool, SrcMgr::CharacteristicKind) override {
    if (!S.project()) {
      reject(L, "include");
      return;
    }
    if (!File) {
      if (S.owns(SM, L))
        reject(L, "include",
               "Declared header is missing; generate or provide it before "
               "translation.",
               "TR0203");
      else
        S.diagnose("TR0203", "SDK include", "A required SDK header is missing.",
                   "Restore the exact approved SDK header distribution.");
    } else if (S.relativePath(File->getName()).empty() &&
               !(S.math() && S.sdkFile(*File))) {
      if (S.owns(SM, L))
        reject(L, "include",
               "Resolved header is outside the owned project and approved SDK "
               "boundaries.",
               "TR0203");
      else
        S.diagnose(
            "TR0203", "SDK include",
            "SDK include is outside the compiled approved header catalog.",
            "Restore the exact approved SDK header distribution.");
    }
  }
  void PragmaDirective(SourceLocation L, PragmaIntroducerKind) override {
    if (S.project() &&
        OncePragmas.count(S.sourcePath(SM, L) + ":" +
                          std::to_string(SM.getSpellingLineNumber(L))))
      return;
    reject(L, "pragma");
  }
  void LexedFileChanged(FileID FID, LexedFileChangeReason Reason,
                        SrcMgr::CharacteristicKind, FileID,
                        SourceLocation) override {
    if (!S.project() || Reason != LexedFileChangeReason::EnterFile)
      return;
    auto L = SM.getLocForStartOfFile(FID);
    auto Path = S.sourcePath(SM, L);
    if (Path.empty() && S.math() && S.sdkFile(SM, L)) {
      S.consumeSDKFile(SM, FID);
      return;
    }
    if (Path.empty())
      return; // Foreign includes already fail at their include site.
    bool Invalid = false;
    auto Bytes = SM.getBufferData(FID, &Invalid);
    if (Invalid) {
      reject(L, "header", "Cannot read the selected header buffer.", "TR0203");
      return;
    }
    S.Dependencies[Path] = digest(Bytes);
    checkDirectives(PP, FID);
  }
  void MacroExpands(const Token &T, const MacroDefinition &, SourceRange,
                    const MacroArgs *) override {
    if (!T.getIdentifierInfo())
      return;
    auto N = T.getIdentifierInfo()->getName();
    if (N == "__DATE__" || N == "__TIME__" || N == "__TIMESTAMP__" ||
        N == "__FILE__" || N == "__BASE_FILE__" || N == "__FILE_NAME__" ||
        N == "__COUNTER__" || N == "__INCLUDE_LEVEL__" ||
        N == "__has_include" || N == "__has_include_next")
      reject(T.getLocation(), N);
  }
  void checkDirectives(Preprocessor &PP, FileID FID = FileID()) {
    if (FID.isInvalid())
      FID = SM.getMainFileID();
    if (!Checked.insert(FID.getHashValue()).second)
      return;
    Lexer Raw(FID, SM.getBufferOrFake(FID), SM, PP.getLangOpts());
    Token T;
    bool AfterHash = false;
    while (!Raw.LexFromRawLexer(T)) {
      if (T.isAtStartOfLine())
        AfterHash = false;
      if (T.is(tok::hash) && T.isAtStartOfLine()) {
        AfterHash = true;
        continue;
      }
      if (!T.is(tok::raw_identifier)) {
        if (AfterHash && !T.is(tok::comment)) {
          reject(T.getLocation(), "preprocessor directive");
          AfterHash = false;
        }
        continue;
      }
      auto Text = Lexer::getSpelling(T, SM, PP.getLangOpts());
      if (AfterHash && S.project() && Text == "pragma") {
        auto L = T.getLocation();
        Token Kind;
        Raw.LexFromRawLexer(Kind);
        if (!Kind.isAtStartOfLine() && Kind.is(tok::raw_identifier) &&
            Lexer::getSpelling(Kind, SM, PP.getLangOpts()) == "once")
          OncePragmas.insert(S.sourcePath(SM, L) + ":" +
                             std::to_string(SM.getSpellingLineNumber(L)));
        else
          reject(L, "pragma");
        AfterHash = false;
        continue;
      }
      if (Text == "__DATE__" || Text == "__TIME__" || Text == "__TIMESTAMP__" ||
          Text == "__FILE__" || Text == "__BASE_FILE__" ||
          Text == "__FILE_NAME__" || Text == "__COUNTER__" ||
          Text == "__INCLUDE_LEVEL__" || Text == "__has_include" ||
          Text == "__has_include_next")
        reject(T.getLocation(), Text);
      if (AfterHash && Text != "define" && Text != "undef" && Text != "if" &&
          Text != "ifdef" && Text != "ifndef" && Text != "elif" &&
          Text != "else" && Text != "endif" && Text != "error" &&
          !(S.project() && Text == "include"))
        reject(T.getLocation(), Text);
      AfterHash = false;
    }
  }
};

class Consumer : public ASTConsumer {
  State &S;

public:
  explicit Consumer(State &S) : S(S) {}
  void HandleTranslationUnit(ASTContext &C) override {
    if (!S.Diagnostics.empty() || C.getDiagnostics().hasErrorOccurred())
      return;
    Adapter A(S, C);
    try {
      A.run();
    } catch (const Failure &) {
    }
  }
};

class Action : public ASTFrontendAction {
  State &S;

public:
  explicit Action(State &S) : S(S) {}
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    auto Policy = std::make_unique<PreprocessorPolicy>(S, CI.getPreprocessor());
    Policy->checkDirectives(CI.getPreprocessor());
    if (!S.Diagnostics.empty())
      return nullptr;
    if (S.project()) {
      auto &SM = CI.getSourceManager();
      auto &Lang = CI.getLangOpts();
      CI.getPreprocessor().setTokenWatcher([this, &SM, &Lang](const Token &T) {
        if (T.is(tok::eof) || T.is(tok::comment) || T.isAnnotation())
          return;
        auto L = SM.getExpansionLoc(T.getLocation());
        if (!S.owns(SM, L))
          return;
        auto Spelling = Lexer::getSpelling(T, SM, Lang);
        auto Bytes = std::to_string(static_cast<unsigned>(T.getKind())) + ":" +
                     std::to_string(Spelling.size()) + ":" + Spelling + ";";
        S.ExpandedTokens[SM.getFileID(L).getHashValue()].push_back(
            {SM.getFileOffset(L), std::move(Bytes)});
      });
    }
    CI.getPreprocessor().addPPCallbacks(std::move(Policy));
    return std::make_unique<Consumer>(S);
  }
};

class Factory : public tooling::FrontendActionFactory {
  State &S;

public:
  explicit Factory(State &S) : S(S) {}
  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<Action>(S);
  }
};

static bool options(State &S, const json::Array &Args) {
  for (size_t I = 0; I < Args.size(); ++I) {
    auto Text = Args[I].getAsString();
    if (!Text)
      return false;
    auto Arg = *Text;
    if (Arg.contains('\n') || Arg.contains('\r') || Arg.contains('\0'))
      return false;
    if (Arg == "-std=c++17" ||
        (S.project() && (Arg == "-O0" || Arg == "-O2"))) {
      S.Arguments.push_back(Arg.str());
      continue;
    }
    if (S.project() &&
        (Arg == "-Wall" || Arg == "-Wextra" || Arg == "-Wpedantic" ||
         Arg == "-Werror" || Arg == "-Wno-error" ||
         Arg == "-Wno-unused-parameter")) {
      S.Arguments.push_back(Arg.str());
      continue;
    }
    if (S.project()) {
      llvm::StringRef Include;
      for (auto Prefix : {llvm::StringRef("-iquote"),
                          llvm::StringRef("-isystem"), llvm::StringRef("-I")})
        if (Arg.starts_with(Prefix)) {
          Include = Prefix;
          break;
        }
      if (!Include.empty()) {
        auto Path = Arg.drop_front(Include.size());
        if (Path.empty()) {
          if (++I == Args.size())
            return false;
          auto Next = Args[I].getAsString();
          if (!Next)
            return false;
          Path = *Next;
        }
        if (Path.empty() || Path.contains('\0') || Path.contains('\r') ||
            Path.contains('\n'))
          return false;
        llvm::SmallString<256> Absolute(Path), Real;
        if (!llvm::sys::path::is_absolute(Absolute)) {
          Absolute = S.WorkingDirectory;
          llvm::sys::path::append(Absolute, Path);
        }
        const auto Error = llvm::sys::fs::real_path(Absolute, Real);
        const auto Canonical = llvm::sys::path::convert_to_slash(Real);
        if (Error || !llvm::sys::fs::is_directory(Canonical) ||
            (Canonical != S.Root && S.relativePath(Canonical).empty())) {
          S.diagnose("TR0203", "include directory",
                     "Include directories must exist inside the declared "
                     "project root.",
                     "Generate owned headers and declare their owned include "
                     "directories before translation.");
          return false;
        }
        S.Arguments.push_back(Include.str());
        S.Arguments.push_back(Canonical);
        continue;
      }
    }
    if (!Arg.starts_with("-D") && !Arg.starts_with("-U"))
      return false;
    auto Name = Arg.drop_front(2).split('=').first;
    if (Name.empty() || llvm::isDigit(Name.front()) || Name.starts_with("__") ||
        (Name.size() > 1 && Name[0] == '_' && llvm::isUpper(Name[1])))
      return false;
    for (char C : Name)
      if (!llvm::isAlnum(C) && C != '_')
        return false;
    if (Arg.starts_with("-U") && Arg.contains('='))
      return false;
    S.Arguments.push_back(Arg.str());
  }
  return true;
}

static std::optional<std::string> readRegularFile(llvm::StringRef Path,
                                                 uint64_t Limit) {
  if (Path.empty() || Path.contains('\0'))
    return std::nullopt;
#ifdef _WIN32
  auto Opened = llvm::sys::fs::openNativeFileForRead(Path);
  if (!Opened) {
    llvm::consumeError(Opened.takeError());
    return std::nullopt;
  }
  llvm::sys::fs::file_t FD = *Opened;
#else
  // A path-only stat followed by blocking open can race a FIFO replacement.
  // Inspect the exact nonblocking descriptor and never mmap mutable input.
  llvm::sys::fs::file_t FD =
      ::open(Path.str().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (FD == -1)
    return std::nullopt;
#endif
  auto Close = llvm::make_scope_exit([&]() { (void)llvm::sys::fs::closeFile(FD); });
  llvm::sys::fs::file_status Status;
  if (llvm::sys::fs::status(FD, Status) ||
      !llvm::sys::fs::is_regular_file(Status) || Status.getSize() > Limit)
    return std::nullopt;
  std::string Contents;
  char Buffer[64 * 1024];
  for (;;) {
    const uint64_t Remaining = Limit - Contents.size();
    const size_t Count = Remaining >= sizeof(Buffer)
                             ? sizeof(Buffer)
                             : static_cast<size_t>(Remaining) + 1;
    auto Read = llvm::sys::fs::readNativeFile(
        FD, llvm::MutableArrayRef<char>(Buffer, Count));
    if (!Read) {
      llvm::consumeError(Read.takeError());
      return std::nullopt;
    }
    if (*Read > Remaining)
      return std::nullopt;
    if (!*Read)
      return Contents;
    Contents.append(Buffer, *Read);
  }
}

static bool boundedRequestJSON(llvm::StringRef Text) {
  unsigned Depth = 0;
  bool String = false, Escape = false;
  for (char C : Text) {
    if (String) {
      if (Escape)
        Escape = false;
      else if (C == '\\')
        Escape = true;
      else if (C == '"')
        String = false;
    } else if (C == '"') {
      String = true;
    } else if (C == '[' || C == '{') {
      if (++Depth > 32)
        return false;
    } else if (C == ']' || C == '}') {
      if (!Depth)
        return false;
      --Depth;
    }
  }
  return !String && Depth == 0;
}

static bool request(State &S, llvm::StringRef Path) {
  auto Buffer = readRegularFile(Path, 1024 * 1024);
  if (!Buffer || !boundedRequestJSON(*Buffer))
    return false;
  auto Data = json::parse(*Buffer);
  if (!Data) {
    llvm::consumeError(Data.takeError());
    return false;
  }
  const auto *O = Data->getAsObject();
  if (!O || O->getInteger("protocol") != 1 ||
      (O->getString("profile") != "cpp-core-v1" &&
       O->getString("profile") != "cpp-core-v2" &&
       O->getString("profile") != "cpp-project-v1" &&
       O->getString("profile") != "cpp-math-v1"))
    return false;
  S.Profile = O->getString("profile")->str();
  S.Module["profile"] = S.Profile;
  // Required keys are checked below. An exact count also rejects unknown keys
  // and project/SDK context accidentally attached to a different profile.
  const size_t ExpectedFields = S.math() ? 10 : (S.project() ? 9 : 6);
  if (O->size() != ExpectedFields)
    return false;
  auto Root = O->getString("root"), Source = O->getString("source"),
       Target = O->getString("target");
  const auto *Args = O->getArray("arguments");
  if (!Root || !Source || !Target || !Args ||
      !llvm::sys::path::is_absolute(*Root) ||
      !llvm::sys::path::is_absolute(*Source) || Root->contains('\0') ||
      Source->contains('\0'))
    return false;
  S.Root = Root->str();
  S.Source = Source->str();
  S.Target = Target->str();
  llvm::SmallString<256> RealRoot, RealSource;
  if (llvm::sys::fs::real_path(S.Root, RealRoot) ||
      llvm::sys::fs::real_path(S.Source, RealSource))
    return false;
  // real_path uses native separators on Windows; all stored paths and wire
  // locations use slashes. On POSIX, literal backslashes remain unchanged.
  S.Root = llvm::sys::path::convert_to_slash(RealRoot);
  S.Source = llvm::sys::path::convert_to_slash(RealSource);
  auto Prefix = llvm::StringRef(S.Root).ends_with("/") ? S.Root : S.Root + "/";
  if (!llvm::StringRef(S.Source).starts_with(Prefix))
    return false;
  S.Relative = llvm::StringRef(S.Source).drop_front(Prefix.size()).str();
  S.WorkingDirectory = S.Root;
  if (S.project()) {
    auto TU = O->getString("translation_unit"),
         Configuration = O->getString("configuration_id"),
         Working = O->getString("working_directory");
    if (S.relativePath(S.Source).empty() || !TU || *TU != S.Relative ||
        !Configuration || Configuration->size() != 64 || !Working ||
        Working->contains('\0') || !llvm::sys::path::is_absolute(*Working))
      return false;
    for (char C : *Configuration)
      if (!(C >= '0' && C <= '9') && !(C >= 'a' && C <= 'f'))
        return false;
    llvm::SmallString<256> RealWorking;
    const auto Error = llvm::sys::fs::real_path(*Working, RealWorking);
    const auto Canonical = llvm::sys::path::convert_to_slash(RealWorking);
    if (Error || !llvm::sys::fs::is_directory(Canonical) ||
        (Canonical != S.Root && S.relativePath(Canonical).empty()))
      return false;
    S.WorkingDirectory = Canonical;
    S.ConfigurationID = Configuration->str();
  }
  llvm::Triple T(S.Target);
  if (S.math()) {
    const auto *SDK = O->getObject("sdk");
    llvm::VersionTuple Deployment;
    if (T.getVendor() != llvm::Triple::Apple || !T.isMacOSX() ||
        !T.getEnvironmentName().empty() || !T.getMacOSXVersion(Deployment) ||
        Deployment.getMajor() != 15 || Deployment.getMinor().value_or(0) != 0 ||
        Deployment.getSubminor().value_or(0) != 0) {
      S.diagnose(
          "TR0204", "math target",
          "The approved math SDK currently supports Apple macOS 15.0 targets "
          "only.",
          "Select arm64-apple-macosx15.0.0 or x86_64-apple-macosx15.0.0.");
      return false;
    }
    if (!SDK || !S.configureSDK(*SDK))
      return false;
  }
  if ((T.getArch() != llvm::Triple::aarch64 &&
       T.getArch() != llvm::Triple::x86_64) ||
      (!T.isMacOSX() && !(T.isOSLinux() && !T.isAndroid()) &&
       !T.isOSWindows())) {
    S.diagnose(
        "TR0204", "target",
        "Target architecture/OS is outside the core data-model contract.",
        "Use the current NeverC native target.");
    return false;
  }
  if (!options(S, *Args)) {
    if (S.Diagnostics.empty())
      S.diagnose("TR0004", "source option",
                 S.project() ? "Only C++17, -O0/-O2, -D/-U and owned "
                               "-I/-iquote/-isystem directories are supported."
                             : "Only -std=c++17 and attached "
                               "-DNAME[=VALUE]/-UNAME options are supported.",
                 "Remove options outside the documented profile allowlist.");
    return false;
  }
  auto SourceBuffer = readRegularFile(S.Source, 8 * 1024 * 1024);
  if (!SourceBuffer)
    return false;
  S.SourceText = std::move(*SourceBuffer);
  return true;
}

} // namespace nct

extern "C" int neverc_cpp_frontend_main(int Argc, const char **Argv) {
  using namespace nct;
  std::string Request, Output;
  if (Argc == 2 && std::string(Argv[1]) == "--version") {
    llvm::outs() << "neverc-cpp-frontend 20.1.8 protocol 1 " NEVERC_CPP_BUILD_ID
                    "\n";
    return 0;
  }
  for (int I = 1; I < Argc; ++I) {
    std::string Arg = Argv[I];
    if (I + 1 >= Argc || (Arg != "--request" && Arg != "--output")) {
      llvm::errs() << "usage: neverc __neverc_cpp_frontend --request request.json "
                      "--output response.json\n";
      return 1;
    }
    auto &Destination = Arg == "--request" ? Request : Output;
    if (!Destination.empty())
      return 1;
    Destination = Argv[++I];
  }
  if (Request.empty() || Output.empty() || Output == "-")
    return 1;
  State S;
  S.Module =
      json::Object{{"protocol", 1},
                   {"profile", "cpp-core-v1"},
                   {"frontend", json::Object{{"name", "neverc-cpp-frontend"},
                                             {"version", "20.1.8"},
                                             {"build", NEVERC_CPP_BUILD_ID}}}};
  bool Valid = request(S, Request);
  if (Valid && S.project() && !isolateProjectEnvironment()) {
    S.diagnose(
        "TR0101", "frontend environment",
        "Cannot isolate implicit compiler configuration from the environment.",
        "Run the internal frontend in an environment with removable compiler "
        "configuration variables.");
    Valid = false;
  }
  if (!Valid && S.Diagnostics.empty())
    S.diagnose("TR0103", "frontend request",
               "Invalid protocol request, paths or source size.",
               "Use a compatible NeverC translate driver and readable source "
               "under the declared root.");
  if (Valid) {
    std::vector<std::string> Args{
        "--no-default-config", "-x",        "c++",
        "-std=c++17",          "-nostdinc", "-nostdinc++",
        "-fsyntax-only",       "-target",   S.Target};
    Args.insert(Args.end(), S.Arguments.begin(), S.Arguments.end());
    if (S.math()) {
      Args.insert(Args.end(),
                  {"-fno-fast-math", "-ffp-contract=off", "-resource-dir",
                   S.SDKRoots.at("resource"), "-isystem",
                   S.SDKRoots.at("libcxx"), "-isystem",
                   S.SDKRoots.at("resource") + "/include", "-isystem",
                   S.SDKRoots.at("platform") + "/usr/include", "-isysroot",
                   S.SDKRoots.at("platform")});
    }
    if (auto FileSystem = S.createFileSystem()) {
      tooling::FixedCompilationDatabase Database(S.WorkingDirectory, Args);
      tooling::ClangTool Tool(Database, {S.Source},
                              std::make_shared<PCHContainerOperations>(),
                              FileSystem);
      Tool.mapVirtualFile(S.Source, S.SourceText);
      Diagnostics Diags(S);
      Tool.setDiagnosticConsumer(&Diags);
      Factory F(S);
      if (Tool.run(&F) && S.Diagnostics.empty())
        S.diagnose("TR0102", "Clang frontend",
                   "Source analysis failed without a structured diagnostic.",
                   "Inspect the frontend installation and input.");
    }
  }
  bool Failed = !S.Diagnostics.empty();
  S.Module["diagnostics"] = std::move(S.Diagnostics);
  std::error_code Error;
  llvm::raw_fd_ostream OS(Output, Error, llvm::sys::fs::CD_CreateNew,
                          llvm::sys::fs::FA_Write, llvm::sys::fs::OF_Text);
  if (Error) {
    llvm::errs() << Error.message() << "\n";
    return 1;
  }
  OS << llvm::formatv("{0:2}\n", json::Value(std::move(S.Module)));
  OS.flush();
  if (OS.has_error())
    return 1;
  return Failed ? 1 : 0;
}
