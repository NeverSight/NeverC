#include "Frontend.h"
#include "BuiltinCppSdkData.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Index/USRGeneration.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <set>

using namespace clang;
namespace nct {

static bool safeRelative(llvm::StringRef P) {
  if (P.empty() || P.starts_with("/") || P.ends_with("/") || P.contains('\\') ||
      P.contains(':') || P.contains('\0') || P.contains('\n') ||
      P.contains('\r'))
    return false;
  while (!P.empty()) {
    auto Part = P.split('/');
    if (Part.first.empty() || Part.first == "." || Part.first == "..")
      return false;
    P = Part.second;
  }
  return true;
}

static std::string virtualPath(llvm::StringRef Path) {
  llvm::SmallString<256> Result(llvm::sys::path::convert_to_slash(Path));
  llvm::sys::path::remove_dots(Result, true, llvm::sys::path::Style::posix);
  return Result.str().str();
}

static std::string virtualRoot() {
#ifdef _WIN32
  return "Z:/__neverc_cpp_builtin_sdk__/v1";
#else
  return "/__neverc_cpp_builtin_sdk__/v1";
#endif
}

bool State::configureSDK(const json::Object &SDK) {
  auto fail = [&](llvm::StringRef Why) {
    diagnose("TR0203", "C++ SDK", Why,
             "Use the approved embedded C++ SDK in a compatible NeverC build; "
             "external SDK roots are not supported.");
    return false;
  };
  if (digest(neverc_cpp_sdk::CatalogJSON) != neverc_cpp_sdk::CatalogSHA256)
    return fail("The embedded SDK catalog bytes do not match their identity.");
  auto Parsed = json::parse(neverc_cpp_sdk::CatalogJSON);
  if (!Parsed) {
    llvm::consumeError(Parsed.takeError());
    return fail("The frontend's embedded SDK catalog is invalid.");
  }
  const auto *Catalog = Parsed->getAsObject();
  auto Distribution = SDK.getString("distribution_id"),
       Hash = SDK.getString("catalog_sha256");
  if (!Catalog || SDK.size() != 2 || !Distribution || !Hash ||
      Catalog->getString("distribution_id") != Distribution ||
      *Hash != neverc_cpp_sdk::CatalogSHA256)
    return fail("SDK request identity does not match the embedded approved "
                "catalog, or contains an external SDK override.");
  SDKDistribution = Distribution->str();
  SDKCatalogHash = Hash->str();
  const auto Virtual = virtualRoot();
  const auto Owned = virtualPath(Root);
  const auto OwnedPrefix =
      llvm::StringRef(Owned).ends_with("/") ? Owned : Owned + "/";
  if (Owned == Virtual || llvm::StringRef(Owned).starts_with(Virtual + "/") ||
      llvm::StringRef(Virtual).starts_with(OwnedPrefix) ||
      llvm::sys::fs::exists(Virtual))
    return fail("The reserved embedded SDK root overlaps owned source or a "
                "physical filesystem entry.");
  for (auto Name : {"libcxx", "resource", "platform"})
    SDKRoots.emplace(Name, Virtual + "/" + Name);

  using Key = std::pair<std::string, std::string>;
  std::map<Key, std::pair<std::string, bool>> Expected;
  for (auto Name : {"headers", "metadata"}) {
    const auto *Entries = Catalog->getArray(Name);
    if (!Entries || Entries->empty())
      return fail("The embedded catalog has no approved headers or metadata.");
    const bool Metadata = llvm::StringRef(Name) == "metadata";
    for (const auto &Entry : *Entries) {
      const auto *F = Entry.getAsObject();
      if (!F)
        return fail("The embedded catalog contains an invalid file entry.");
      auto R = F->getString("root"), P = F->getString("path"),
           H = F->getString("sha256");
      if (!R || !P || !H || !SDKRoots.count(R->str()) || !safeRelative(*P) ||
          H->size() != 64 ||
          !Expected.emplace(Key(R->str(), P->str()),
                            std::make_pair(H->str(), Metadata)).second)
        return fail("The embedded catalog contains invalid or duplicate files.");
      if (!Metadata)
        SDKHeaders.emplace(Key(R->str(), P->str()), H->str());
    }
  }
  if (Expected.size() != neverc_cpp_sdk::FileCount)
    return fail("The embedded SDK file inventory does not match its catalog.");
  for (const auto &File : neverc_cpp_sdk::Files) {
    auto Entry = Expected.find({File.Root, File.Path});
    if (Entry == Expected.end() || File.Size > 4 * 1024 * 1024 ||
        Entry->second.first != File.SHA256 ||
        Entry->second.second != File.Metadata ||
        digest(llvm::StringRef(File.Contents, File.Size)) != File.SHA256)
      return fail("Embedded SDK bytes or file identities differ from the "
                  "approved catalog.");
    Expected.erase(Entry);
  }
  return Expected.empty();
}

llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> State::createFileSystem() {
  auto fail = [&]() -> llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> {
    diagnose("TR0203", "frontend filesystem",
             "Cannot create the isolated source and embedded SDK filesystem.",
             "Use readable owned sources and a compatible NeverC build.");
    return nullptr;
  };
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> Physical(
      llvm::vfs::createPhysicalFileSystem().release());
  if (Physical->setCurrentWorkingDirectory(WorkingDirectory))
    return fail();
  if (!sdk())
    return Physical;
  auto Memory = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  if (Memory->setCurrentWorkingDirectory(WorkingDirectory))
    return fail();
  SDKVirtualFiles.clear();
  for (const auto &File : neverc_cpp_sdk::Files) {
    const auto Path = SDKRoots.at(File.Root) + "/" + File.Path;
    if (!Memory->addFile(Path, 0, llvm::MemoryBuffer::getMemBufferCopy(
                                    llvm::StringRef(File.Contents, File.Size),
                                    Path)))
      return fail();
    auto Status = Memory->status(Path);
    if (!Status)
      return fail();
    SDKVirtualFiles.emplace(Path, Status->getUniqueID());
  }
  auto Overlay = llvm::makeIntrusiveRefCnt<llvm::vfs::OverlayFileSystem>(Physical);
  Overlay->pushOverlay(Memory);
  return Overlay;
}

std::optional<SDKFile> State::sdkFile(llvm::StringRef Path) const {
  if (!sdk() || Path.empty() || Path.starts_with("<"))
    return std::nullopt;
  auto Name = virtualPath(Path);
  for (const auto &Root : SDKRoots) {
    auto Prefix = Root.second + "/";
    if (!llvm::StringRef(Name).starts_with(Prefix))
      continue;
    auto Relative = llvm::StringRef(Name).drop_front(Prefix.size()).str();
    auto Entry = SDKHeaders.find({Root.first, Relative});
    if (Entry == SDKHeaders.end())
      return std::nullopt;
    return SDKFile{Root.first, Relative, Entry->second};
  }
  return std::nullopt;
}

std::optional<SDKFile> State::sdkFile(FileEntryRef File) const {
  auto Path = virtualPath(File.getName());
  auto Identity = SDKVirtualFiles.find(Path);
  if (Identity == SDKVirtualFiles.end() ||
      Identity->second != File.getUniqueID())
    return std::nullopt;
  return sdkFile(Path);
}

std::optional<SDKFile> State::sdkFile(const SourceManager &SM,
                                      SourceLocation L) const {
  auto File = SM.getFileEntryRefForID(SM.getFileID(SM.getSpellingLoc(L)));
  return File ? sdkFile(*File) : std::nullopt;
}

bool approvedSDKDeclaration(const State &S, const SourceManager &SM,
                            const Decl *D) {
  return D && S.sdkFile(SM, D->getLocation()).has_value();
}

bool approvedStandardSDKDeclaration(const State &S, const SourceManager &SM,
                                    const Decl *D) {
  if (!approvedSDKDeclaration(S, SM, D))
    return false;
  for (const DeclContext *Context = D->getDeclContext(); Context;
       Context = Context->getParent())
    if (const auto *Namespace = dyn_cast<NamespaceDecl>(Context);
        Namespace && Namespace->isStdNamespace())
      return true;
  return false;
}

std::optional<llvm::APSInt>
approvedSDKIntegerConstant(const State &S, const SourceManager &SM,
                           const VarDecl *D, const ASTContext &Context) {
  if (!approvedStandardSDKDeclaration(S, SM, D) ||
      !D->isUsableInConstantExpressions(Context) || D->getType().isNull() ||
      D->getType().isVolatileQualified() ||
      !D->getType()->isIntegralOrEnumerationType())
    return std::nullopt;
  const auto *Value = D->evaluateValue();
  return Value && Value->isInt()
             ? std::optional<llvm::APSInt>(Value->getInt())
             : std::nullopt;
}

bool approvedNumericLimitsConstant(const State &S, const SourceManager &SM,
                                   const CallExpr *Call, ASTContext &Context,
                                   APValue &Value) {
  if (!Call || Call->getNumArgs() || !Call->isPRValue() ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return false;
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
  if (!Method || !Method->isStatic() || !Method->isConstexpr() ||
      Method->getNumParams() || Method->isVariadic() ||
      Method->getParent()->getName() != "numeric_limits" ||
      !approvedStandardSDKDeclaration(S, SM, Method))
    return false;
  auto Origin = S.sdkFile(SM, Method->getLocation());
  if (!Origin || Origin->Root != "libcxx" || Origin->Path != "limits")
    return false;
  auto Name = Method->getName();
  if (Name != "min" && Name != "max" && Name != "lowest" &&
      Name != "epsilon" && Name != "round_error" && Name != "infinity" &&
      Name != "quiet_NaN" && Name != "signaling_NaN" &&
      Name != "denorm_min")
    return false;
  const auto *Reference =
      dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreParenImpCasts());
  const auto *Qualifier = Reference ? Reference->getQualifier() : nullptr;
  const auto *QualifierType = Qualifier ? Qualifier->getAsType() : nullptr;
  const auto *QualifierRecord =
      QualifierType ? QualifierType->getAsCXXRecordDecl() : nullptr;
  if (!Reference || Reference->getDecl() != Method ||
      !S.owns(SM, Reference->getExprLoc()) || !QualifierRecord ||
      QualifierRecord->getName() != "numeric_limits" ||
      !approvedStandardSDKDeclaration(S, SM, QualifierRecord))
    return false;
  auto Result = Call->getType();
  if (Result.isNull() ||
      !(Result->isIntegralOrEnumerationType() ||
        Result->isSpecificBuiltinType(BuiltinType::Float) ||
        Result->isSpecificBuiltinType(BuiltinType::Double)) ||
      !Context.hasSameType(Result, Method->getReturnType()) ||
      !Call->isCXX11ConstantExpr(Context, &Value))
    return false;
  return Result->isIntegralOrEnumerationType() ? Value.isInt()
                                                : Value.isFloat();
}

static bool cstddefOrigin(const State &S, const SourceManager &SM,
                          SourceLocation Location, llvm::StringRef Root,
                          llvm::StringRef Path) {
  auto Origin = S.sdkFile(SM, Location);
  return Origin && Origin->Root == Root && Origin->Path == Path;
}

static const EnumDecl *approvedByteType(const State &S,
                                        const SourceManager &SM, QualType Type) {
  const auto *Enum = Type.isNull() ? nullptr : Type->getAs<EnumType>();
  const auto *Declaration = Enum ? Enum->getDecl()->getDefinition() : nullptr;
  if (!Declaration || Declaration->getName() != "byte" ||
      !Declaration->isScoped() || !Declaration->isFixed() ||
      !Declaration->getIntegerType()->isSpecificBuiltinType(
          BuiltinType::UChar) ||
      !approvedStandardSDKDeclaration(S, SM, Declaration) ||
      !cstddefOrigin(S, SM, Declaration->getLocation(), "libcxx",
                     "__cstddef/byte.h"))
    return nullptr;
  return Declaration;
}

std::optional<CstddefOperation>
approvedCstddefOperation(const State &S, const SourceManager &SM,
                         const CallExpr *Call, const ASTContext &Context) {
  if (!Call || Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return std::nullopt;
  const auto *Function = Call->getDirectCallee();
  if (!Function || Function->isVariadic() || !Function->isConstexpr() ||
      !approvedStandardSDKDeclaration(S, SM, Function) ||
      !cstddefOrigin(S, SM, Function->getLocation(), "libcxx",
                     "__cstddef/byte.h"))
    return std::nullopt;
  const auto *Reference =
      dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreParenImpCasts());
  if (!Reference || Reference->getDecl() != Function ||
      !S.owns(SM, Reference->getExprLoc()))
    return std::nullopt;
  auto Same = [&](QualType Left, QualType Right) {
    return !Left.isNull() && !Right.isNull() &&
           Context.hasSameType(Left, Right);
  };
  auto Byte = [&](QualType Type) { return approvedByteType(S, SM, Type); };
  auto ByteReference = [&](QualType Type) {
    return !Type.isNull() && Type->isLValueReferenceType() &&
           Byte(Type->getPointeeType());
  };
  const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
  if (Operator) {
    if (Call->getNumArgs() != Function->getNumParams())
      return std::nullopt;
    const auto Kind = Operator->getOperator();
    const bool Unary = Kind == OO_Tilde;
    const bool Compound =
        Kind == OO_PipeEqual || Kind == OO_AmpEqual ||
        Kind == OO_CaretEqual || Kind == OO_LessLessEqual ||
        Kind == OO_GreaterGreaterEqual;
    const bool Shift =
        Kind == OO_LessLess || Kind == OO_GreaterGreater ||
        Kind == OO_LessLessEqual || Kind == OO_GreaterGreaterEqual;
    if (Call->getNumArgs() != (Unary ? 1u : 2u) ||
        (Compound ? !ByteReference(Function->getParamDecl(0)->getType())
                  : !Byte(Function->getParamDecl(0)->getType())) ||
        (Compound ? !ByteReference(Function->getReturnType())
                  : !Byte(Function->getReturnType())) ||
        !Same(Call->getArg(0)->getType(),
              Compound ? Function->getParamDecl(0)->getType()->getPointeeType()
                       : Function->getParamDecl(0)->getType()) ||
        (Unary && !Same(Call->getType(), Function->getReturnType())))
      return std::nullopt;
    if (!Unary) {
      auto Right = Function->getParamDecl(1)->getType();
      if ((Shift ? !Right->isIntegralType(Context) : !Byte(Right)) ||
          !Same(Call->getArg(1)->getType(), Right) ||
          !Same(Call->getType(),
                Compound ? Function->getReturnType()->getPointeeType()
                         : Function->getReturnType()) ||
          (Compound ? !Call->isLValue() : !Call->isPRValue()))
        return std::nullopt;
    }
    switch (Kind) {
    case OO_Pipe: return CstddefOperation::BitOr;
    case OO_Amp: return CstddefOperation::BitAnd;
    case OO_Caret: return CstddefOperation::BitXor;
    case OO_Tilde: return CstddefOperation::BitNot;
    case OO_PipeEqual: return CstddefOperation::BitOrAssign;
    case OO_AmpEqual: return CstddefOperation::BitAndAssign;
    case OO_CaretEqual: return CstddefOperation::BitXorAssign;
    case OO_LessLess: return CstddefOperation::ShiftLeft;
    case OO_GreaterGreater: return CstddefOperation::ShiftRight;
    case OO_LessLessEqual: return CstddefOperation::ShiftLeftAssign;
    case OO_GreaterGreaterEqual: return CstddefOperation::ShiftRightAssign;
    default: return std::nullopt;
    }
  }
  if (Function->getName() != "to_integer" || Call->getNumArgs() != 1 ||
      Function->getNumParams() != 1 ||
      !Byte(Function->getParamDecl(0)->getType()) ||
      !Same(Call->getArg(0)->getType(), Function->getParamDecl(0)->getType()) ||
      Function->getReturnType().isNull() ||
      !Function->getReturnType()->isIntegralType(Context) ||
      !Same(Call->getType(), Function->getReturnType()))
    return std::nullopt;
  return CstddefOperation::ToInteger;
}

bool approvedCstddefNull(const State &S, const SourceManager &SM,
                        const Expr *Expression) {
  const auto *Null = dyn_cast_or_null<GNUNullExpr>(Expression);
  return Null && Null->isPRValue() && !Null->getType().isNull() &&
         Null->getType()->isIntegerType() && S.owns(SM, Null->getExprLoc()) &&
         cstddefOrigin(S, SM, SM.getSpellingLoc(Null->getExprLoc()),
                       "resource", "include/__stddef_null.h");
}

bool approvedCstddefTypeQuery(const State &S, const SourceManager &SM,
                             const UnaryExprOrTypeTraitExpr *Query,
                             ASTContext &Context) {
  if (!Query || !Query->isArgumentType() ||
      (Query->getKind() != UETT_SizeOf &&
       Query->getKind() != UETT_AlignOf) ||
      Query->isTypeDependent() || Query->isValueDependent() ||
      Query->isInstantiationDependent() || !Query->isPRValue() ||
      !S.owns(SM, Query->getExprLoc()))
    return false;
  const auto *Info = Query->getArgumentTypeInfo();
  const auto *Typedef = Query->getTypeOfArgument()->getAs<TypedefType>();
  const auto *Declaration = Typedef ? Typedef->getDecl() : nullptr;
  APValue Value;
  return Info && S.owns(SM, Info->getTypeLoc().getBeginLoc()) && Declaration &&
         Declaration->getName() == "max_align_t" &&
         approvedSDKDeclaration(S, SM, Declaration) &&
         cstddefOrigin(S, SM, Declaration->getLocation(), "resource",
                       "include/__stddef_max_align_t.h") &&
         Query->isCXX11ConstantExpr(Context, &Value) && Value.isInt();
}

std::optional<llvm::APSInt>
approvedCstddefOffset(const State &S, const SourceManager &SM,
                      const Expr *Expression, ASTContext &Context) {
  const auto *Offset = dyn_cast_or_null<OffsetOfExpr>(Expression);
  if (!Offset || Offset->isTypeDependent() || Offset->isValueDependent() ||
      Offset->isInstantiationDependent() || !Offset->isPRValue() ||
      Offset->getType().isNull() ||
      !Offset->getType()->isIntegralType(Context) ||
      !S.owns(SM, Offset->getOperatorLoc()) ||
      !cstddefOrigin(S, SM, SM.getSpellingLoc(Offset->getOperatorLoc()),
                     "resource", "include/__stddef_offsetof.h") ||
      !Offset->getTypeSourceInfo())
    return std::nullopt;
  auto Object = Offset->getTypeSourceInfo()->getType();
  if (Object.isNull() ||
      !S.owns(SM, Offset->getTypeSourceInfo()->getTypeLoc().getBeginLoc()))
    return std::nullopt;
  auto Current = Object;
  for (unsigned I = 0; I < Offset->getNumComponents(); ++I) {
    const auto &Component = Offset->getComponent(I);
    if (Component.getKind() == OffsetOfNode::Array) {
      const auto *Array = Context.getAsConstantArrayType(Current);
      const auto *Index = Offset->getIndexExpr(Component.getArrayExprIndex());
      APValue IndexValue;
      if (!Array || !Index || Index->isTypeDependent() ||
          Index->isValueDependent() || Index->isInstantiationDependent() ||
          !S.owns(SM, Index->getExprLoc()) ||
          !Index->isCXX11ConstantExpr(Context, &IndexValue) ||
          !IndexValue.isInt() || IndexValue.getInt().isNegative() ||
          IndexValue.getInt().getLimitedValue() >=
              Array->getSize().getLimitedValue())
        return std::nullopt;
      Current = Array->getElementType();
      continue;
    }
    if (Component.getKind() != OffsetOfNode::Field)
      return std::nullopt;
    auto *Record = Current->getAsCXXRecordDecl();
    Record = Record ? Record->getDefinition() : nullptr;
    const auto *Field = Component.getField();
    if (!Record || Record->isUnion() || !Record->isStandardLayout() ||
        !S.owns(SM, Record->getLocation()) || !Field ||
        !S.owns(SM, Field->getLocation()) ||
        Field->getParent()->getCanonicalDecl() != Record->getCanonicalDecl())
      return std::nullopt;
    Current = Field->getType();
  }
  Expr::EvalResult Result;
  if (!Offset->EvaluateAsInt(Result, Context) || !Result.Val.isInt())
    return std::nullopt;
  return Result.Val.getInt();
}

bool State::consumeSDKFile(const SourceManager &SM, FileID ID) {
  auto Entry = sdkFile(SM, SM.getLocForStartOfFile(ID));
  bool Invalid = false;
  auto Bytes = SM.getBufferData(ID, &Invalid);
  if (!Entry || Invalid || digest(Bytes) != Entry->SHA256) {
    diagnose("TR0203", "C++ SDK header",
             "Consumed SDK header bytes or filesystem identity differ from "
             "the embedded approved catalog.",
             "Use the immutable SDK embedded in a compatible NeverC build.");
    return false;
  }
  SDKDependencies[{Entry->Root, Entry->Path}] = Entry->SHA256;
  return true;
}

void State::addSDKMetadata() {
  if (math())
    Module["fp_contract"] = "cpp.math.binary64.masked.v1";
  Module["sdk_distribution_id"] = SDKDistribution;
  Module["sdk_catalog_sha256"] = SDKCatalogHash;
  json::Array Deps;
  for (const auto &Entry : SDKDependencies)
    Deps.push_back(json::Object{{"root", Entry.first.first},
                                {"path", Entry.first.second},
                                {"sha256", Entry.second}});
  Module["sdk_dependencies"] = std::move(Deps);
}

json::Object Adapter::floatingLiteral(const llvm::APFloat &V,
                                      SourceLocation L) {
  const bool Single = &V.getSemantics() == &llvm::APFloat::IEEEsingle();
  if ((!Single && &V.getSemantics() != &llvm::APFloat::IEEEdouble()) ||
      (Single ? !S.coreV2() : !S.coreV2() && !S.math())) {
    reject(L, "floating literal",
           "Only supported IEEE binary32/binary64 literal semantics are admitted.");
    throw Failure{};
  }
  auto Bits = llvm::utohexstr(V.bitcastToAPInt().getZExtValue(), true);
  Bits.insert(Bits.begin(), (Single ? 8 : 16) - Bits.size(), '0');
  return json::Object{
      {"kind", "literal"}, {"type", Single ? "float" : "double"},
      {"bits", Bits}, {"loc", loc(L)}};
}

static bool standardNamespace(const DeclContext *C) {
  while (const auto *N = dyn_cast_or_null<NamespaceDecl>(C)) {
    if (N->isStdNamespace())
      return true;
    if (!N->isInline())
      return false;
    C = N->getParent();
  }
  return false;
}

std::string Adapter::mapping(const CallExpr *Call) {
  if (!S.math())
    return {};
  const auto *F = Call->getDirectCallee();
  if (!F || F->getNumParams() != 1 || Call->getNumArgs() != 1 ||
      F->isVariadic() ||
      F->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
      !F->isExternC() ||
      !F->getReturnType()->isSpecificBuiltinType(BuiltinType::Double) ||
      !F->getParamDecl(0)->getType()->isSpecificBuiltinType(
          BuiltinType::Double))
    return {};
  auto Spelling = F->getName();
  if (Spelling != "fabs" && Spelling != "floor")
    return {};
  const auto *Reference =
      dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreParenImpCasts());
  if (!Reference || !Reference->getQualifier() ||
      !Reference->getQualifier()->getAsNamespace() ||
      !Reference->getQualifier()->getAsNamespace()->isStdNamespace())
    return {};
  const auto *Shadow = dyn_cast<UsingShadowDecl>(Reference->getFoundDecl());
  if (!Shadow || !standardNamespace(Shadow->getDeclContext()))
    return {};
  const auto *Introducer = Shadow->getIntroducer();
  if (Introducer->getLocation().isMacroID())
    return {};
  auto Import = S.sdkFile(Sources, Introducer->getLocation());
  if (!Import || Import->Root != "libcxx" || Import->Path != "cmath" ||
      !S.SDKDependencies.count({Import->Root, Import->Path}))
    return {};
  const NamedDecl *Target = Shadow->getTargetDecl();
  while (const auto *Next = dyn_cast<UsingShadowDecl>(Target))
    Target = Next->getTargetDecl();
  if (Target->getCanonicalDecl() != F->getCanonicalDecl())
    return {};
  std::optional<SDKFile> Origin;
  SourceLocation OriginLocation;
  for (const auto *Redeclaration : F->redecls()) {
    if (Redeclaration->isImplicit())
      continue;
    if (Redeclaration->getLocation().isMacroID())
      return {};
    auto File = S.sdkFile(Sources, Redeclaration->getLocation());
    if (!File || File->Root != "platform" ||
        File->Path != "usr/include/math.h" ||
        !S.SDKDependencies.count({File->Root, File->Path}))
      return {};
    Origin = File;
    OriginLocation = Redeclaration->getLocation();
  }
  if (!Origin)
    return {};
  if (Sources.getSpellingLineNumber(OriginLocation) !=
          (Spelling == "fabs" ? 423u : 466u) ||
      Sources.getSpellingColumnNumber(OriginLocation) != 15u)
    return {};
  llvm::SmallString<128> USR;
  if (index::generateUSRForDecl(F->getCanonicalDecl(), USR))
    return {};
  auto DeclarationID =
      digest(S.SDKDistribution + "\n" + Origin->Root + "\n" + Origin->Path +
             "\n" + USR.str().str() + "\ndouble(double)");
  auto ID = "cpp.math." + Spelling.str() + ".f64.v1";
  auto L = Sources.getSpellingLoc(OriginLocation);
  MappedFunctions[ID] = json::Object{
      {"id", ID},
      {"declaration_id", DeclarationID},
      {"result", "double"},
      {"parameters", json::Array{"double"}},
      {"origin", json::Object{{"root", Origin->Root},
                              {"path", Origin->Path},
                              {"sha256", Origin->SHA256},
                              {"line", Sources.getSpellingLineNumber(L)},
                              {"column", Sources.getSpellingColumnNumber(L)}}}};
  return ID;
}
} // namespace nct
