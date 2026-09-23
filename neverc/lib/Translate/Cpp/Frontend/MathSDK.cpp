#include "BuiltinCppSdkData.h"
#include "Frontend.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Index/USRGeneration.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
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

static bool supportedFunctionalScalar(QualType Type,
                                      const ASTContext &Context) {
  if (Type.isNull() || Type->isReferenceType() || Type.hasQualifiers())
    return false;
  return (Type->isIntegralType(Context) && Context.getTypeSize(Type) <= 64) ||
         Type->isSpecificBuiltinType(BuiltinType::Float) ||
         Type->isSpecificBuiltinType(BuiltinType::Double);
}

static bool supportedFunctionalCallableValue(QualType Type,
                                             const ASTContext &Context) {
  return supportedFunctionalScalar(Type, Context) ||
         (!Type.isNull() && !Type.hasQualifiers() &&
          Type->isFunctionPointerType());
}

static bool functionalObjectName(llvm::StringRef Name) {
  return Name == "plus" || Name == "minus" || Name == "multiplies" ||
         Name == "divides" || Name == "modulus" || Name == "negate" ||
         Name == "bit_and" || Name == "bit_or" || Name == "bit_xor" ||
         Name == "bit_not" || Name == "equal_to" || Name == "not_equal_to" ||
         Name == "less" || Name == "greater" || Name == "less_equal" ||
         Name == "greater_equal" || Name == "logical_and" ||
         Name == "logical_or" || Name == "logical_not" || Name == "hash";
}

static bool integralFunctionalObject(llvm::StringRef Name) {
  return Name == "modulus" || Name == "bit_and" || Name == "bit_or" ||
         Name == "bit_xor" || Name == "bit_not";
}

static bool directIntegralHashType(QualType Type) {
  if (Type.isNull() || Type.hasQualifiers())
    return false;
  const auto *Builtin = Type->getAs<BuiltinType>();
  if (!Builtin)
    return false;
  switch (Builtin->getKind()) {
  case BuiltinType::Bool:
  case BuiltinType::Char_U:
  case BuiltinType::UChar:
  case BuiltinType::Char16:
  case BuiltinType::Char32:
  case BuiltinType::WChar_U:
  case BuiltinType::UShort:
  case BuiltinType::UInt:
  case BuiltinType::ULong:
  case BuiltinType::Char_S:
  case BuiltinType::SChar:
  case BuiltinType::WChar_S:
  case BuiltinType::Short:
  case BuiltinType::Int:
  case BuiltinType::Long:
    return true;
  default:
    return false;
  }
}

static bool wideIntegralHashType(QualType Type) {
  if (Type.isNull() || Type.hasQualifiers())
    return false;
  return Type->isSpecificBuiltinType(BuiltinType::LongLong) ||
         Type->isSpecificBuiltinType(BuiltinType::ULongLong);
}

static bool floatingHashType(QualType Type) {
  if (Type.isNull() || Type.hasQualifiers())
    return false;
  return Type->isSpecificBuiltinType(BuiltinType::Float) ||
         Type->isSpecificBuiltinType(BuiltinType::Double);
}

static bool utilityObjectPointer(const ASTContext &Context, QualType Type);

static const ClassTemplatePartialSpecializationDecl *
pointerHashPartial(const State &S, const SourceManager &SM,
                   const ClassTemplateSpecializationDecl *Record,
                   QualType ValueType, const ASTContext &Context) {
  const auto *Definition = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Record ? Record->getDefinition() : nullptr);
  if (!Definition)
    return nullptr;
  const auto *Template = Definition->getSpecializedTemplate();
  const auto Specialized = Definition->getSpecializedTemplateOrPartial();
  const auto *Partial =
      Specialized.dyn_cast<ClassTemplatePartialSpecializationDecl *>();
  if (!utilityObjectPointer(Context, ValueType) || !Template ||
      !Partial || Partial->getName() != "hash" || Partial->isUnion() ||
      !Partial->isDependentContext() ||
      Partial->getSpecializedTemplate()->getCanonicalDecl() !=
          Template->getCanonicalDecl() ||
      !approvedStandardSDKDeclaration(S, SM, Partial) ||
      !cstddefOrigin(S, SM, Partial->getLocation(), "libcxx",
                     "__functional/hash.h"))
    return nullptr;
  const auto &Arguments = Partial->getTemplateArgs();
  const auto Pattern =
      Arguments.size() == 1 &&
              Arguments.get(0).getKind() == TemplateArgument::Type
          ? Arguments.get(0).getAsType()
          : QualType();
  return !Pattern.isNull() && Pattern->isPointerType() &&
                 Pattern->getPointeeType()->isDependentType()
             ? Partial
             : nullptr;
}

static QualType enumHashUnderlyingType(QualType Type,
                                       const ASTContext &Context) {
  if (Type.isNull() || Type.hasQualifiers())
    return {};
  const auto *Enum = Type->getAs<EnumType>();
  const auto *Declaration = Enum ? Enum->getDecl() : nullptr;
  const auto Underlying =
      Declaration && Declaration->isCompleteDefinition()
          ? Declaration->getIntegerType()
          : QualType();
  return !Underlying.isNull() && Context.getTypeSize(Underlying) <= 64 &&
                 (directIntegralHashType(Underlying) ||
                  wideIntegralHashType(Underlying))
             ? Underlying
             : QualType();
}

static const ClassTemplateSpecializationDecl *
enumHashBase(const State &S, const SourceManager &SM,
             const CXXRecordDecl *Record, QualType ValueType,
             const ASTContext &Context) {
  const auto Underlying = enumHashUnderlyingType(ValueType, Context);
  const auto *Definition = Record ? Record->getDefinition() : nullptr;
  const auto *Base =
      Definition && Definition->getNumBases() == 1
          ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                Definition->bases_begin()->getType()->getAsCXXRecordDecl())
          : nullptr;
  Base = Base ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                    Base->getDefinition())
              : nullptr;
  const auto *Template = Base ? Base->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  if (Underlying.isNull() || !Base || !Template || !CanonicalTemplate ||
      Base->getName() != "__enum_hash" || Base->isUnion() ||
      Base->isDependentContext() || !Base->isEmpty() ||
      !Base->isStandardLayout() || !Base->isTriviallyCopyable() ||
      !Base->hasTrivialDestructor() || !Base->field_empty() ||
      !approvedStandardSDKDeclaration(S, SM, Base) ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !cstddefOrigin(S, SM, Base->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      !cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      !cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                     "__functional/hash.h"))
    return nullptr;
  const auto &Arguments = Base->getTemplateArgs();
  return Arguments.size() == 2 &&
                 Arguments.get(0).getKind() == TemplateArgument::Type &&
                 Arguments.get(1).getKind() == TemplateArgument::Integral &&
                 Context.hasSameType(Arguments.get(0).getAsType(), ValueType) &&
                 Arguments.get(1).getAsIntegral() == 1
             ? Base
             : nullptr;
}

static const ClassTemplateSpecializationDecl *
scalarHashBase(const State &S, const SourceManager &SM,
               const CXXRecordDecl *Record, QualType ValueType,
               const ASTContext &Context) {
  const auto *Definition = Record ? Record->getDefinition() : nullptr;
  const auto *Base =
      Definition && Definition->getNumBases() == 1
          ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                Definition->bases_begin()->getType()->getAsCXXRecordDecl())
          : nullptr;
  Base = Base ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                    Base->getDefinition())
              : nullptr;
  const auto *Template = Base ? Base->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  if ((!wideIntegralHashType(ValueType) && !floatingHashType(ValueType)) ||
      !Base || !Template ||
      !CanonicalTemplate || Base->getName() != "__scalar_hash" ||
      Base->isUnion() || Base->isDependentContext() || !Base->isEmpty() ||
      !Base->isStandardLayout() || !Base->isTriviallyCopyable() ||
      !Base->hasTrivialDestructor() || !Base->field_empty() ||
      !approvedStandardSDKDeclaration(S, SM, Base) ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !cstddefOrigin(S, SM, Base->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      !cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      !cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                     "__functional/hash.h"))
    return nullptr;
  const auto &Arguments = Base->getTemplateArgs();
  if (Arguments.size() != 2 ||
      Arguments.get(0).getKind() != TemplateArgument::Type ||
      Arguments.get(1).getKind() != TemplateArgument::Integral ||
      !Context.hasSameType(Arguments.get(0).getAsType(), ValueType))
    return nullptr;
  const auto Ratio = Context.getTypeSize(ValueType) /
                     Context.getTypeSize(Context.getSizeType());
  if (Ratio > 2 ||
      Arguments.get(1).getAsIntegral() != Ratio)
    return nullptr;
  return Base;
}

static bool functionalObjectOrigin(const State &S, const SourceManager &SM,
                                   const NamedDecl *Declaration,
                                   llvm::StringRef Name) {
  if (Name != "hash")
    return cstddefOrigin(S, SM, Declaration->getLocation(), "libcxx",
                         "__functional/operations.h");
  return cstddefOrigin(S, SM, Declaration->getLocation(), "libcxx",
                       "__functional/hash.h");
}

static bool functionalObjectTemplateOrigin(const State &S,
                                           const SourceManager &SM,
                                           const NamedDecl *Declaration,
                                           llvm::StringRef Name,
                                           bool Canonical) {
  if (Name != "hash")
    return cstddefOrigin(S, SM, Declaration->getLocation(), "libcxx",
                         "__functional/operations.h");
  if (cstddefOrigin(S, SM, Declaration->getLocation(), "libcxx",
                    "__fwd/functional.h"))
    return true;
  if (Canonical)
    return false;
  if (cstddefOrigin(S, SM, Declaration->getLocation(), "libcxx",
                    "__functional/hash.h"))
    return true;
  const auto *Template = dyn_cast<ClassTemplateDecl>(Declaration);
  const auto *Definition =
      Template ? Template->getTemplatedDecl()->getDefinition() : nullptr;
  return Definition && approvedStandardSDKDeclaration(S, SM, Definition) &&
         cstddefOrigin(S, SM, Definition->getLocation(), "libcxx",
                       "__functional/hash.h");
}

std::optional<FunctionalObjectRecord>
approvedFunctionalObjectRecord(const State &S, const SourceManager &SM,
                               const CXXRecordDecl *Record,
                               const ASTContext &Context) {
  const auto *Definition = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Record ? Record->getDefinition() : nullptr);
  const auto *Template = Definition ? Definition->getSpecializedTemplate()
                                    : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  const auto Name = Definition ? Definition->getName() : llvm::StringRef();
  if (!Definition || !Template || !CanonicalTemplate ||
      !functionalObjectName(Name) || Definition->isUnion() ||
      Definition->isDependentContext() || !Definition->isEmpty() ||
      !Definition->isStandardLayout() || !Definition->isTriviallyCopyable() ||
      !Definition->hasTrivialDestructor() || !Definition->field_empty() ||
      Context.getTypeSize(Context.getRecordType(Definition)) !=
          Context.getCharWidth() ||
      Context.getTypeAlign(Context.getRecordType(Definition)) !=
          Context.getTypeAlign(Context.UnsignedCharTy) ||
      !approvedStandardSDKDeclaration(S, SM, Definition) ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !functionalObjectOrigin(S, SM, Definition, Name) ||
      !functionalObjectTemplateOrigin(S, SM, Template, Name, false) ||
      !functionalObjectTemplateOrigin(S, SM, CanonicalTemplate, Name, true))
    return std::nullopt;
  const auto &Arguments = Definition->getTemplateArgs();
  if (Arguments.size() != 1 ||
      Arguments.get(0).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto ValueType = Arguments.get(0).getAsType();
  if (Name == "hash") {
    const bool Enum = !enumHashUnderlyingType(ValueType, Context).isNull();
    const bool Pointer =
        pointerHashPartial(S, SM, Definition, ValueType, Context) != nullptr;
    if (((Enum || Pointer)
             ? Definition->getSpecializationKind() != TSK_ImplicitInstantiation
             : Definition->getSpecializationKind() !=
                   TSK_ExplicitSpecialization) ||
        (!directIntegralHashType(ValueType) &&
         !wideIntegralHashType(ValueType) && !floatingHashType(ValueType) &&
         !ValueType->isNullPtrType() && !Enum && !Pointer) ||
        ((wideIntegralHashType(ValueType) || floatingHashType(ValueType)) &&
         !scalarHashBase(S, SM, Definition, ValueType, Context)) ||
        (Enum && !enumHashBase(S, SM, Definition, ValueType, Context)))
      return std::nullopt;
    return FunctionalObjectRecord{Definition};
  }
  if (!ValueType.isNull() && ValueType->isVoidType()) {
    const TypedefNameDecl *TransparentMarker = nullptr;
    for (const auto *Declaration : Definition->decls())
      if (const auto *Alias = dyn_cast<TypedefNameDecl>(Declaration);
          Alias && Alias->getIdentifier() &&
          Alias->getName() == "is_transparent") {
        if (TransparentMarker)
          return std::nullopt;
        TransparentMarker = Alias;
      }
    if (Definition->getSpecializationKind() != TSK_ExplicitSpecialization ||
        !TransparentMarker ||
        !TransparentMarker->getUnderlyingType()->isVoidType() ||
        !approvedStandardSDKDeclaration(S, SM, TransparentMarker) ||
        !cstddefOrigin(S, SM, TransparentMarker->getLocation(), "libcxx",
                       "__functional/operations.h"))
      return std::nullopt;
  } else if (!supportedFunctionalScalar(ValueType, Context) ||
             (integralFunctionalObject(Definition->getName()) &&
              !ValueType->isIntegralType(Context))) {
    return std::nullopt;
  }
  return FunctionalObjectRecord{Definition};
}

bool approvedFunctionalObjectBaseCast(const State &S, const SourceManager &SM,
                                      const CastExpr *Cast,
                                      const ASTContext &Context) {
  if (!Cast ||
      (Cast->getCastKind() != CK_DerivedToBase &&
       Cast->getCastKind() != CK_UncheckedDerivedToBase) ||
      !Cast->getSubExpr() || !Cast->isGLValue() ||
      !Cast->getSubExpr()->isGLValue() || Cast->path_size() != 1)
    return false;
  const auto *Hash = Cast->getSubExpr()->getType()->getAsCXXRecordDecl();
  const auto Object = approvedFunctionalObjectRecord(S, SM, Hash, Context);
  const auto *Definition =
      Object ? dyn_cast<ClassTemplateSpecializationDecl>(Object->Record)
             : nullptr;
  if (!Definition)
    return false;
  const auto &Arguments = Definition->getTemplateArgs();
  const auto ValueType =
      Arguments.size() == 1 &&
              Arguments.get(0).getKind() == TemplateArgument::Type
          ? Arguments.get(0).getAsType()
          : QualType();
  auto *Base = scalarHashBase(S, SM, Definition, ValueType, Context);
  if (!Base)
    Base = enumHashBase(S, SM, Definition, ValueType, Context);
  const auto *Target = Cast->getType()->getAsCXXRecordDecl();
  Target = Target ? Target->getDefinition() : nullptr;
  return Base && Target &&
         Base->getCanonicalDecl() == Target->getCanonicalDecl() &&
         *Cast->path_begin() == &*Definition->bases_begin() &&
         (!Cast->getSubExpr()->getType().isConstQualified() ||
          Cast->getType().isConstQualified());
}

static bool supportedFunctionalReferenceValue(const State &S,
                                              const SourceManager &SM,
                                              const ASTContext &Context,
                                              QualType Type) {
  if (Type.isNull() || Type.isVolatileQualified() ||
      Type.isRestrictQualified() || Type.getAddressSpace() != LangAS::Default)
    return false;
  Type = Type.getUnqualifiedType();
  if (const auto *Array = Context.getAsConstantArrayType(Type))
    return Array->getSize().getLimitedValue(65537) <= 65536 &&
           supportedFunctionalReferenceValue(S, SM, Context,
                                             Array->getElementType());
  const auto *Record = Type->getAsCXXRecordDecl();
  if (approvedUtilityArrayRecord(S, SM, Record, Context))
    return true;
  const auto *Definition = Record ? Record->getDefinition() : nullptr;
  return (Type->isIntegralOrEnumerationType() &&
          Context.getTypeSize(Type) <= 64) ||
         Type->isSpecificBuiltinType(BuiltinType::Float) ||
         Type->isSpecificBuiltinType(BuiltinType::Double) ||
         Type->isNullPtrType() || Type->isFunctionPointerType() ||
         utilityObjectPointer(Context, Type) ||
         (Definition && !Definition->isUnion() &&
          !Definition->isInvalidDecl() &&
          S.owns(SM, Definition->getLocation()));
}

static bool supportedFunctionalByValue(const State &S,
                                       const SourceManager &SM,
                                       const ASTContext &Context,
                                       QualType Type) {
  if (Type.isNull() || Type->isReferenceType() || Type->isArrayType() ||
      Type.isVolatileQualified() || Type.isRestrictQualified() ||
      Type.getAddressSpace() != LangAS::Default)
    return false;
  if (supportedFunctionalCallableValue(Type, Context) ||
      utilityObjectPointer(Context, Type))
    return true;
  const auto *Record = Type->getAsCXXRecordDecl();
  const auto *Definition = Record ? Record->getDefinition() : nullptr;
  return Definition && !Definition->isUnion() &&
         !Definition->isInvalidDecl() && Definition->isStandardLayout() &&
         Definition->isTriviallyCopyable() &&
         Definition->hasTrivialDestructor() &&
         S.owns(SM, Definition->getLocation());
}

static bool supportedFunctionalResult(const State &S, const SourceManager &SM,
                                      const ASTContext &Context,
                                      QualType Type) {
  if (Type.isNull() || Type->isReferenceType() || Type->isArrayType() ||
      Type.isVolatileQualified() || Type.isRestrictQualified() ||
      Type.getAddressSpace() != LangAS::Default)
    return false;
  if (Type->isVoidType() || supportedFunctionalCallableValue(Type, Context) ||
      utilityObjectPointer(Context, Type))
    return true;
  const auto *Record = Type->getAsCXXRecordDecl();
  const auto *Definition = Record ? Record->getDefinition() : nullptr;
  return Definition && !Definition->isUnion() &&
         !Definition->isInvalidDecl() &&
         S.owns(SM, Definition->getLocation());
}

std::optional<FunctionalReferenceRecord> approvedFunctionalReferenceRecord(
    const State &S, const SourceManager &SM, const CXXRecordDecl *Record,
    const ASTContext &Context) {
  const auto *Definition = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Record ? Record->getDefinition() : nullptr);
  const auto *Template = Definition ? Definition->getSpecializedTemplate()
                                    : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  const auto *Pointer =
      Definition && std::distance(Definition->field_begin(),
                                  Definition->field_end()) == 1
          ? *Definition->field_begin()
          : nullptr;
  const auto *Base =
      Definition && Definition->getNumBases() == 1
          ? Definition->bases_begin()->getType()->getAsCXXRecordDecl()
          : nullptr;
  Base = Base ? Base->getDefinition() : nullptr;
  if (!Definition || Definition->getName() != "reference_wrapper")
    return std::nullopt;
  if (!Template || !CanonicalTemplate || !Pointer || !Base)
    return std::nullopt;
  if (Definition->isUnion() ||
      Definition->isDependentContext() || !Definition->isStandardLayout() ||
      !Definition->isTriviallyCopyable() ||
      !Definition->hasTrivialDestructor())
    return std::nullopt;
  if (!Pointer->getIdentifier() || Pointer->getName() != "__f_")
    return std::nullopt;
  if (
      Base->getName() != "__weak_result_type" || !Base->isEmpty() ||
      !Base->isStandardLayout() || !Base->field_empty() ||
      !Base->hasTrivialDestructor())
    return std::nullopt;
  if (!approvedStandardSDKDeclaration(S, SM, Definition) ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !approvedStandardSDKDeclaration(S, SM, Pointer))
    return std::nullopt;
  if (!cstddefOrigin(S, SM, Definition->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h"))
    return std::nullopt;
  if (!cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h"))
    return std::nullopt;
  if (!cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                     "__fwd/functional.h"))
    return std::nullopt;
  if (!cstddefOrigin(S, SM, Pointer->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h"))
    return std::nullopt;
  const auto &Arguments = Definition->getTemplateArgs();
  if (Arguments.size() != 1 ||
      Arguments.get(0).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto Referent = Arguments.get(0).getAsType();
  const auto *Function = !Referent.isNull() && Referent->isFunctionType()
                             ? Referent->getAs<FunctionProtoType>()
                             : nullptr;
  if (Referent.isNull() ||
      (!Referent->isObjectType() && !Function) ||
      Referent.isVolatileQualified() ||
      Referent.getAddressSpace() != LangAS::Default)
    return std::nullopt;
  if (Function) {
    const auto Result = Function->getReturnType();
    if (Function->isVariadic() ||
        (Result->isReferenceType()
             ? !supportedFunctionalReferenceValue(S, SM, Context,
                                                  Result->getPointeeType())
             : !supportedFunctionalResult(S, SM, Context, Result)))
      return std::nullopt;
    for (const auto Parameter : Function->param_types())
      if (Parameter->isReferenceType()
              ? !supportedFunctionalReferenceValue(S, SM, Context,
                                                   Parameter->getPointeeType())
              : !supportedFunctionalByValue(S, SM, Context, Parameter))
        return std::nullopt;
  }
  const auto PointerType = Context.getPointerType(Referent);
  if (!Context.hasSameType(Pointer->getType(), PointerType))
    return std::nullopt;
  const auto &Layout = Context.getASTRecordLayout(Definition);
  const auto PointerSize = Context.getTypeSizeInChars(PointerType);
  if (Layout.getAlignment() != Context.getTypeAlignInChars(PointerType))
    return std::nullopt;
  // Itanium applies empty-base optimization to __weak_result_type. The
  // Microsoft ABI reserves one pointer-sized slot for that base, so retain it
  // explicitly in the portable record before the stored pointer.
  const bool Compact = Layout.getSize() == PointerSize &&
                       Layout.getFieldOffset(0) == 0;
  const bool Padded = Layout.getSize() == PointerSize * 2 &&
                      Layout.getFieldOffset(0) ==
                          uint64_t(PointerSize.getQuantity()) * 8;
  if (!Compact && !Padded)
    return std::nullopt;
  return FunctionalReferenceRecord{Definition, Referent, PointerType, Pointer,
                                   Padded};
}

std::optional<FunctionalObjectConstruction>
approvedFunctionalObjectConstruction(const State &S, const SourceManager &SM,
                                     const CXXConstructExpr *Construction,
                                     const ASTContext &Context) {
  if (!Construction || Construction->isTypeDependent() ||
      Construction->isValueDependent() ||
      Construction->isInstantiationDependent() ||
      Construction->getConstructionKind() != CXXConstructionKind::Complete)
    return std::nullopt;
  const auto *Constructor = Construction->getConstructor();
  const auto Object = approvedFunctionalObjectRecord(
      S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
  const auto *Prototype =
      Constructor ? Constructor->getType()->getAs<FunctionProtoType>()
                  : nullptr;
  if (!Constructor || !Object || !Prototype || !Prototype->isNothrow() ||
      Constructor->getParent()->getCanonicalDecl() !=
          Object->Record->getCanonicalDecl() ||
      Constructor->isVariadic() || !Constructor->isImplicit() ||
      !Constructor->isTrivial() ||
      Construction->getNumArgs() != Constructor->getNumParams() ||
      !approvedStandardSDKDeclaration(S, SM, Constructor) ||
      !functionalObjectOrigin(S, SM, Constructor, Object->Record->getName()))
    return std::nullopt;
  if (!Construction->getNumArgs() && Constructor->isDefaultConstructor() &&
      Constructor->isDefaulted())
    return FunctionalObjectConstruction::Default;
  if (Construction->getNumArgs() != 1 ||
      !Constructor->isCopyOrMoveConstructor())
    return std::nullopt;
  const auto Source = approvedFunctionalObjectRecord(
      S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(), Context);
  const auto Parameter = Constructor->getParamDecl(0)->getType();
  if (!Source || Source->Record->getCanonicalDecl() !=
                     Object->Record->getCanonicalDecl() ||
      !Parameter->isReferenceType() ||
      Parameter->getPointeeType().isVolatileQualified() ||
      !Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                      Context.getRecordType(Source->Record)))
    return std::nullopt;
  return FunctionalObjectConstruction::CopyOrMove;
}

std::optional<FunctionalReferenceConstruction>
approvedFunctionalReferenceConstruction(
    const State &S, const SourceManager &SM,
    const CXXConstructExpr *Construction, const ASTContext &Context) {
  if (!Construction || Construction->isTypeDependent() ||
      Construction->isValueDependent() ||
      Construction->isInstantiationDependent() ||
      Construction->getConstructionKind() != CXXConstructionKind::Complete)
    return std::nullopt;
  const auto *Constructor = Construction->getConstructor();
  const auto Wrapper = approvedFunctionalReferenceRecord(
      S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
  const auto *Prototype =
      Constructor ? Constructor->getType()->getAs<FunctionProtoType>()
                  : nullptr;
  if (!Constructor || !Wrapper || !Prototype || !Prototype->isNothrow() ||
      Constructor->getParent()->getCanonicalDecl() !=
          Wrapper->Record->getCanonicalDecl() ||
      Constructor->isVariadic() || Construction->getNumArgs() != 1 ||
      Constructor->getNumParams() != 1 ||
      !approvedStandardSDKDeclaration(S, SM, Constructor))
    return std::nullopt;
  const auto Parameter = Constructor->getParamDecl(0)->getType();
  const auto Argument = Construction->getArg(0)->getType();
  if (Constructor->isCopyOrMoveConstructor()) {
    const auto Source = approvedFunctionalReferenceRecord(
        S, SM, Argument->getAsCXXRecordDecl(), Context);
    if (!Source || !Constructor->isImplicit() || !Constructor->isTrivial() ||
        Source->Record->getCanonicalDecl() !=
            Wrapper->Record->getCanonicalDecl() ||
        !Parameter->isReferenceType() ||
        Parameter->getPointeeType().isVolatileQualified() ||
        !Context.hasSameUnqualifiedType(
            Parameter->getPointeeType(),
            Context.getRecordType(Source->Record)))
      return std::nullopt;
    return FunctionalReferenceConstruction::CopyOrMove;
  }
  const auto *CanonicalPrimary = Constructor->getPrimaryTemplate();
  const auto *PatternDeclaration = CanonicalPrimary
                                       ? dyn_cast<CXXConstructorDecl>(
                                             CanonicalPrimary->getTemplatedDecl())
                                       : nullptr;
  const auto Pointee = Parameter->isLValueReferenceType()
                           ? Parameter->getPointeeType()
                           : QualType();
  if (!CanonicalPrimary || !PatternDeclaration ||
      !Constructor->isInlined() || !Constructor->hasBody() ||
      Pointee.isNull() || Pointee.isVolatileQualified() ||
      !Construction->getArg(0)->isLValue() ||
      !Context.hasSameUnqualifiedType(Pointee, Wrapper->ReferentType) ||
      (!Wrapper->ReferentType.isConstQualified() &&
       Pointee.isConstQualified()) ||
      !Context.hasSameUnqualifiedType(Argument, Pointee) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalPrimary) ||
      !approvedStandardSDKDeclaration(S, SM, PatternDeclaration) ||
      !cstddefOrigin(S, SM, CanonicalPrimary->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h") ||
      !cstddefOrigin(S, SM, PatternDeclaration->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h"))
    return std::nullopt;
  return FunctionalReferenceConstruction::Direct;
}

bool approvedFunctionalObjectAssignment(const State &S,
                                        const SourceManager &SM,
                                        const CXXOperatorCallExpr *Assignment,
                                        const ASTContext &Context) {
  if (!Assignment || Assignment->isTypeDependent() ||
      Assignment->isValueDependent() ||
      Assignment->isInstantiationDependent() ||
      Assignment->getOperator() != OO_Equal || Assignment->getNumArgs() != 2 ||
      !Assignment->isLValue())
    return false;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Assignment->getDirectCallee());
  const auto Destination = approvedFunctionalObjectRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  const auto Source = approvedFunctionalObjectRecord(
      S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  const auto *Reference = directMethodReference(Assignment);
  if (!Method || !Destination || !Source || !Reference || Method->isStatic() ||
      Method->isVariadic() || Method->getNumParams() != 1 ||
      Method->getOverloadedOperator() != OO_Equal || !Method->isImplicit() ||
      !Method->isTrivial() ||
      (!Method->isCopyAssignmentOperator() &&
       !Method->isMoveAssignmentOperator()) ||
      Destination->Record->getCanonicalDecl() !=
          Source->Record->getCanonicalDecl() ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !functionalObjectOrigin(S, SM, Method, Destination->Record->getName()) ||
      !S.owns(SM, Reference->getExprLoc()) ||
      !Context.hasSameUnqualifiedType(
          Assignment->getArg(0)->getType(),
          Context.getRecordType(Destination->Record)) ||
      !Context.hasSameUnqualifiedType(Assignment->getArg(1)->getType(),
                                      Context.getRecordType(Source->Record)) ||
      !Method->getReturnType()->isLValueReferenceType() ||
      !Context.hasSameUnqualifiedType(
          Method->getReturnType()->getPointeeType(),
          Context.getRecordType(Destination->Record)))
    return false;
  const auto Parameter = Method->getParamDecl(0)->getType();
  return Parameter->isReferenceType() &&
         !Parameter->getPointeeType().isVolatileQualified() &&
         Context.hasSameUnqualifiedType(
             Parameter->getPointeeType(), Context.getRecordType(Source->Record));
}

bool approvedFunctionalReferenceAssignment(
    const State &S, const SourceManager &SM,
    const CXXOperatorCallExpr *Assignment, const ASTContext &Context) {
  if (!Assignment || Assignment->isTypeDependent() ||
      Assignment->isValueDependent() ||
      Assignment->isInstantiationDependent() ||
      Assignment->getOperator() != OO_Equal || Assignment->getNumArgs() != 2 ||
      !Assignment->isLValue())
    return false;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Assignment->getDirectCallee());
  const auto Destination = approvedFunctionalReferenceRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  const auto Source = approvedFunctionalReferenceRecord(
      S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  const auto *Reference = directMethodReference(Assignment);
  if (!Method || !Destination || !Source || !Reference || Method->isStatic() ||
      Method->isVariadic() || Method->getNumParams() != 1 ||
      Method->getOverloadedOperator() != OO_Equal || !Method->isImplicit() ||
      !Method->isTrivial() ||
      (!Method->isCopyAssignmentOperator() &&
       !Method->isMoveAssignmentOperator()) ||
      Destination->Record->getCanonicalDecl() !=
          Source->Record->getCanonicalDecl() ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h") ||
      !S.owns(SM, Reference->getExprLoc()) ||
      !Context.hasSameUnqualifiedType(
          Assignment->getArg(0)->getType(),
          Context.getRecordType(Destination->Record)) ||
      !Context.hasSameUnqualifiedType(Assignment->getArg(1)->getType(),
                                      Context.getRecordType(Source->Record)) ||
      !Method->getReturnType()->isLValueReferenceType() ||
      !Context.hasSameUnqualifiedType(
          Method->getReturnType()->getPointeeType(),
          Context.getRecordType(Destination->Record)))
    return false;
  const auto Parameter = Method->getParamDecl(0)->getType();
  return Parameter->isReferenceType() &&
         !Parameter->getPointeeType().isVolatileQualified() &&
         Context.hasSameUnqualifiedType(
             Parameter->getPointeeType(), Context.getRecordType(Source->Record));
}

static std::optional<FunctionalOperationInfo> approvedFunctionalOperationImpl(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context, bool RequireOwnedReference);

static std::optional<FunctionalOperationInfo> approvedPointerHashOperation(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context, bool RequireOwnedReference) {
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call ? Call->getDirectCallee() : nullptr);
  const auto *Hash = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Method ? Method->getParent()->getDefinition() : nullptr);
  const auto *Reference = directMethodReference(Call);
  const auto Object = approvedFunctionalObjectRecord(S, SM, Hash, Context);
  if (!Call || !Operator || !Method || !Hash || !Reference || !Object ||
      Object->Record->getCanonicalDecl() != Hash->getCanonicalDecl() ||
      Operator->getOperator() != OO_Call ||
      Method->getOverloadedOperator() != OO_Call || Method->isStatic() ||
      !Method->isConst() || Method->isVariadic() || !Method->isInlined() ||
      !Method->hasBody() || !Call->isPRValue() ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      (RequireOwnedReference && !S.owns(SM, Reference->getExprLoc())))
    return std::nullopt;

  const auto &HashArguments = Hash->getTemplateArgs();
  const auto ValueType =
      HashArguments.size() == 1 &&
              HashArguments.get(0).getKind() == TemplateArgument::Type
          ? HashArguments.get(0).getAsType()
          : QualType();
  const auto *Partial = pointerHashPartial(S, SM, Hash, ValueType, Context);
  const auto *Pattern = Method->getInstantiatedFromMemberFunction();
  const auto *PatternParent =
      Pattern ? dyn_cast<ClassTemplatePartialSpecializationDecl>(
                    Pattern->getParent())
              : nullptr;
  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  if (!Partial || !Pattern || !PatternParent ||
      PatternParent->getCanonicalDecl() != Partial->getCanonicalDecl() ||
      !approvedStandardSDKDeclaration(S, SM, Pattern) ||
      !cstddefOrigin(S, SM, Pattern->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      !Body || Body->size() != 3 || Method->getNumParams() != 1 ||
      Call->getNumArgs() != 2 ||
      !Context.hasSameType(Method->getParamDecl(0)->getType(), ValueType) ||
      !Context.hasSameType(Call->getArg(1)->getType(), ValueType) ||
      !Context.hasSameType(Method->getReturnType(), Context.getSizeType()) ||
      !Context.hasSameType(Call->getType(), Context.getSizeType()) ||
      !Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                      Context.getRecordType(Hash)))
    return std::nullopt;

  auto Statement = Body->body_begin();
  const auto *Declaration = dyn_cast<DeclStmt>(*Statement++);
  const auto *Assignment = dyn_cast<BinaryOperator>(*Statement++);
  const auto *Return = dyn_cast<ReturnStmt>(*Statement);
  const VarDecl *Storage = nullptr;
  if (Declaration)
    for (const auto *Declared : Declaration->decls())
      if (const auto *Variable = dyn_cast<VarDecl>(Declared)) {
        if (Storage)
          return std::nullopt;
        Storage = Variable;
      }
  const auto *Union = Storage ? Storage->getType()->getAsCXXRecordDecl()
                              : nullptr;
  Union = Union ? Union->getDefinition() : nullptr;
  const FieldDecl *PointerField = nullptr;
  const FieldDecl *SizeField = nullptr;
  if (Union && std::distance(Union->field_begin(), Union->field_end()) == 2) {
    auto Field = Union->field_begin();
    PointerField = *Field++;
    SizeField = *Field;
  }
  if (!Storage || !Union || !Union->isUnion() ||
      !Union->isStandardLayout() || !Union->isTriviallyCopyable() ||
      !Union->hasTrivialDestructor() || !PointerField || !SizeField ||
      !PointerField->getIdentifier() || PointerField->getName() != "__t" ||
      !SizeField->getIdentifier() || SizeField->getName() != "__a" ||
      !Context.hasSameType(PointerField->getType(), ValueType) ||
      !Context.hasSameType(SizeField->getType(), Context.getSizeType()) ||
      Context.getASTRecordLayout(Union).getSize() !=
          Context.getTypeSizeInChars(ValueType) ||
      Context.getASTRecordLayout(Union).getAlignment() !=
          Context.getTypeAlignInChars(ValueType) ||
      !approvedStandardSDKDeclaration(S, SM, Storage) ||
      !approvedStandardSDKDeclaration(S, SM, Union) ||
      !approvedStandardSDKDeclaration(S, SM, PointerField) ||
      !approvedStandardSDKDeclaration(S, SM, SizeField) ||
      !cstddefOrigin(S, SM, Storage->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      !cstddefOrigin(S, SM, Union->getLocation(), "libcxx",
                     "__functional/hash.h"))
    return std::nullopt;

  const auto *StoredMember =
      Assignment && Assignment->getOpcode() == BO_Assign
          ? dyn_cast<MemberExpr>(Assignment->getLHS()->IgnoreParenImpCasts())
          : nullptr;
  const auto *StoredBase =
      StoredMember ? dyn_cast<DeclRefExpr>(
                         StoredMember->getBase()->IgnoreParenImpCasts())
                   : nullptr;
  const auto *StoredValue =
      Assignment ? dyn_cast<DeclRefExpr>(
                       Assignment->getRHS()->IgnoreParenImpCasts())
                 : nullptr;
  if (!StoredMember || StoredMember->getMemberDecl() != PointerField ||
      !StoredBase || StoredBase->getDecl() != Storage || !StoredValue ||
      StoredValue->getDecl() != Method->getParamDecl(0))
    return std::nullopt;

  const Expr *Returned = Return ? Return->getRetValue() : nullptr;
  if (const auto *Cleanup = dyn_cast_or_null<ExprWithCleanups>(Returned))
    Returned = Cleanup->getSubExpr();
  const auto *HashCall = dyn_cast_or_null<CXXOperatorCallExpr>(
      Returned ? Returned->IgnoreParenImpCasts() : nullptr);
  const auto *HashMethod = dyn_cast_or_null<CXXMethodDecl>(
      HashCall ? HashCall->getDirectCallee() : nullptr);
  const auto *Hasher = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      HashMethod ? HashMethod->getParent()->getDefinition() : nullptr);
  const auto *Address =
      HashCall && HashCall->getNumArgs() == 3
          ? dyn_cast<UnaryOperator>(
                HashCall->getArg(1)->IgnoreParenImpCasts())
          : nullptr;
  const auto *Addressed =
      Address && Address->getOpcode() == UO_AddrOf
          ? dyn_cast<DeclRefExpr>(Address->getSubExpr()->IgnoreParenImpCasts())
          : nullptr;
  const auto *Size =
      HashCall && HashCall->getNumArgs() == 3
          ? dyn_cast<UnaryExprOrTypeTraitExpr>(
                HashCall->getArg(2)->IgnoreParenImpCasts())
          : nullptr;
  const auto *Sized =
      Size && Size->getKind() == UETT_SizeOf && !Size->isArgumentType()
          ? dyn_cast<DeclRefExpr>(
                Size->getArgumentExpr()->IgnoreParenImpCasts())
          : nullptr;
  if (!HashCall || !HashMethod || !Hasher || !Address || !Addressed ||
      Addressed->getDecl() != Storage || !Size || !Sized ||
      Sized->getDecl() != Storage ||
      HashCall->getOperator() != OO_Call ||
      HashMethod->getOverloadedOperator() != OO_Call ||
      HashMethod->isStatic() || !HashMethod->isConst() ||
      HashMethod->isVariadic() || HashMethod->getNumParams() != 2 ||
      Hasher->getName() != "__murmur2_or_cityhash" || Hasher->isUnion() ||
      !Context.hasSameUnqualifiedType(HashCall->getArg(0)->getType(),
                                      Context.getRecordType(Hasher)) ||
      !Context.hasSameType(HashMethod->getParamDecl(0)->getType(),
                           HashCall->getArg(1)->getType()) ||
      !Context.hasSameType(HashMethod->getParamDecl(1)->getType(),
                           Context.getSizeType()) ||
      !Context.hasSameType(HashCall->getArg(2)->getType(),
                           Context.getSizeType()) ||
      !Context.hasSameType(HashMethod->getReturnType(), Context.getSizeType()) ||
      !Context.hasSameType(HashCall->getType(), Context.getSizeType()) ||
      !Context.hasSameType(Size->getType(), Context.getSizeType()) ||
      !approvedStandardSDKDeclaration(S, SM, Hasher) ||
      !approvedStandardSDKDeclaration(S, SM, HashMethod) ||
      !cstddefOrigin(S, SM, Hasher->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      !cstddefOrigin(S, SM, HashMethod->getLocation(), "libcxx",
                     "__functional/hash.h"))
    return std::nullopt;
  const auto &HasherArguments = Hasher->getTemplateArgs();
  if (HasherArguments.size() != 2 ||
      HasherArguments.get(0).getKind() != TemplateArgument::Type ||
      HasherArguments.get(1).getKind() != TemplateArgument::Integral ||
      !Context.hasSameType(HasherArguments.get(0).getAsType(),
                           Context.getSizeType()) ||
      HasherArguments.get(1).getAsIntegral() !=
          Context.getTypeSize(Context.getSizeType()))
    return std::nullopt;
  return FunctionalOperationInfo{FunctionalOperation::Hash,
                                 ValueType,
                                 {},
                                 Context.getSizeType(),
                                 Context.getSizeType()};
}

static std::optional<FunctionalOperationInfo> approvedEnumHashOperation(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context, bool RequireOwnedReference) {
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call ? Call->getDirectCallee() : nullptr);
  const auto *EnumBase = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Method ? Method->getParent()->getDefinition() : nullptr);
  const auto *Reference = directMethodReference(Call);
  const auto *ReceiverCast =
      Call && Call->getNumArgs()
          ? dyn_cast<ImplicitCastExpr>(Call->getArg(0)->IgnoreParens())
          : nullptr;
  const auto *Derived = ReceiverCast ? ReceiverCast->getSubExpr() : nullptr;
  const auto *Hash = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Derived ? Derived->getType()->getAsCXXRecordDecl() : nullptr);
  Hash = Hash ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                    Hash->getDefinition())
              : nullptr;
  const auto Object = approvedFunctionalObjectRecord(S, SM, Hash, Context);
  if (!Call || !Operator || !Method || !EnumBase || !Reference ||
      !ReceiverCast || !Derived || !Hash || !Object ||
      Operator->getOperator() != OO_Call ||
      Method->getOverloadedOperator() != OO_Call || Method->isStatic() ||
      !Method->isConst() || Method->isVariadic() || !Method->isInlined() ||
      !Method->hasBody() || !Call->isPRValue() ||
      EnumBase->getName() != "__enum_hash" || EnumBase->isUnion() ||
      EnumBase->isDependentContext() || !EnumBase->isEmpty() ||
      !EnumBase->isStandardLayout() || !EnumBase->isTriviallyCopyable() ||
      !EnumBase->hasTrivialDestructor() ||
      !approvedFunctionalObjectBaseCast(S, SM, ReceiverCast, Context) ||
      !approvedStandardSDKDeclaration(S, SM, EnumBase) ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !cstddefOrigin(S, SM, EnumBase->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      (RequireOwnedReference && !S.owns(SM, Reference->getExprLoc())))
    return std::nullopt;

  const auto &HashArguments = Hash->getTemplateArgs();
  const auto ValueType =
      HashArguments.size() == 1 &&
              HashArguments.get(0).getKind() == TemplateArgument::Type
          ? HashArguments.get(0).getAsType()
          : QualType();
  const auto Underlying = enumHashUnderlyingType(ValueType, Context);
  const auto *Base = enumHashBase(S, SM, Hash, ValueType, Context);
  const auto *Pattern = Method->getInstantiatedFromMemberFunction();
  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  const DeclStmt *Declaration = nullptr;
  const ReturnStmt *Return = nullptr;
  if (Body && Body->size() == 2) {
    auto Statement = Body->body_begin();
    Declaration = dyn_cast<DeclStmt>(*Statement++);
    Return = dyn_cast<ReturnStmt>(*Statement);
  }
  const auto *Alias =
      Declaration && Declaration->isSingleDecl()
          ? dyn_cast<TypedefNameDecl>(Declaration->getSingleDecl())
          : nullptr;
  const Expr *Returned = Return ? Return->getRetValue() : nullptr;
  if (const auto *Cleanup = dyn_cast_or_null<ExprWithCleanups>(Returned))
    Returned = Cleanup->getSubExpr();
  const auto *NestedCall =
      dyn_cast_or_null<CXXOperatorCallExpr>(
          Returned ? Returned->IgnoreParenImpCasts() : nullptr);
  const auto *Conversion =
      NestedCall && NestedCall->getNumArgs() == 2
          ? dyn_cast<CXXStaticCastExpr>(
                NestedCall->getArg(1)->IgnoreParenImpCasts())
          : nullptr;
  const auto *ParameterReference =
      Conversion ? dyn_cast<DeclRefExpr>(
                       Conversion->getSubExpr()->IgnoreParenImpCasts())
                 : nullptr;
  const auto Nested = approvedFunctionalOperationImpl(
      S, SM, NestedCall, Context, /*RequireOwnedReference=*/false);
  if (Underlying.isNull() || !Base ||
      Base->getCanonicalDecl() != EnumBase->getCanonicalDecl() || !Pattern ||
      !approvedStandardSDKDeclaration(S, SM, Pattern) ||
      !cstddefOrigin(S, SM, Pattern->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      Method->getNumParams() != 1 || Call->getNumArgs() != 2 ||
      !Context.hasSameType(Method->getParamDecl(0)->getType(), ValueType) ||
      !Context.hasSameType(Call->getArg(1)->getType(), ValueType) ||
      !Context.hasSameType(Method->getReturnType(), Context.getSizeType()) ||
      !Context.hasSameType(Call->getType(), Context.getSizeType()) ||
      !Context.hasSameUnqualifiedType(ReceiverCast->getType(),
                                      Context.getRecordType(EnumBase)) ||
      !Context.hasSameUnqualifiedType(Derived->getType(),
                                      Context.getRecordType(Hash)) ||
      !Alias || !approvedStandardSDKDeclaration(S, SM, Alias) ||
      !cstddefOrigin(S, SM, Alias->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      !Context.hasSameType(Alias->getUnderlyingType(), Underlying) ||
      !NestedCall || !Conversion ||
      (Conversion->getCastKind() != CK_IntegralCast &&
       Conversion->getCastKind() != CK_NoOp) ||
      !Context.hasSameType(Conversion->getType(), Underlying) ||
      !ParameterReference ||
      ParameterReference->getDecl() != Method->getParamDecl(0) || !Nested ||
      Nested->Operation != FunctionalOperation::Hash ||
      !Context.hasSameType(Nested->LeftType, Underlying) ||
      !Context.hasSameType(Nested->ResultType, Context.getSizeType()))
    return std::nullopt;
  return FunctionalOperationInfo{FunctionalOperation::Hash,
                                 Underlying,
                                 {},
                                 Context.getSizeType(),
                                 Context.getSizeType()};
}

static std::optional<FunctionalOperationInfo> approvedFloatingHashOperation(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context, bool RequireOwnedReference) {
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call ? Call->getDirectCallee() : nullptr);
  const auto *Hash = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Method ? Method->getParent()->getDefinition() : nullptr);
  const auto *Reference = directMethodReference(Call);
  const auto Object = approvedFunctionalObjectRecord(S, SM, Hash, Context);
  if (!Call || !Operator || !Method || !Hash || !Reference || !Object ||
      Operator->getOperator() != OO_Call ||
      Method->getOverloadedOperator() != OO_Call || Method->isStatic() ||
      !Method->isConst() || Method->isVariadic() || !Method->isInlined() ||
      !Method->hasBody() || !Call->isPRValue() ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !functionalObjectOrigin(S, SM, Method, "hash") ||
      (RequireOwnedReference && !S.owns(SM, Reference->getExprLoc())))
    return std::nullopt;

  const auto &HashArguments = Hash->getTemplateArgs();
  const auto ValueType =
      HashArguments.size() == 1 &&
              HashArguments.get(0).getKind() == TemplateArgument::Type
          ? HashArguments.get(0).getAsType()
          : QualType();
  const auto *Base = scalarHashBase(S, SM, Hash, ValueType, Context);
  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  if (!floatingHashType(ValueType) || !Base || !Body || Body->size() != 2 ||
      Method->getNumParams() != 1 || Call->getNumArgs() != 2 ||
      !Context.hasSameType(Method->getParamDecl(0)->getType(), ValueType) ||
      !Context.hasSameType(Call->getArg(1)->getType(), ValueType) ||
      !Context.hasSameType(Method->getReturnType(), Context.getSizeType()) ||
      !Context.hasSameType(Call->getType(), Context.getSizeType()) ||
      !Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                      Context.getRecordType(Hash)))
    return std::nullopt;

  auto Statement = Body->body_begin();
  const auto *ZeroIf = dyn_cast<IfStmt>(*Statement++);
  const auto *ScalarReturn = dyn_cast<ReturnStmt>(*Statement);
  const auto *Condition =
      ZeroIf ? dyn_cast<BinaryOperator>(ZeroIf->getCond()->IgnoreParenImpCasts())
             : nullptr;
  const auto *ConditionParameter =
      Condition ? dyn_cast<DeclRefExpr>(
                      Condition->getLHS()->IgnoreParenImpCasts())
                : nullptr;
  const auto *FloatingZero =
      Condition ? dyn_cast<FloatingLiteral>(
                      Condition->getRHS()->IgnoreParenImpCasts())
                : nullptr;
  const auto *ZeroReturn =
      ZeroIf ? dyn_cast<ReturnStmt>(ZeroIf->getThen()) : nullptr;
  const auto *IntegerZero =
      ZeroReturn && ZeroReturn->getRetValue()
          ? dyn_cast<IntegerLiteral>(
                ZeroReturn->getRetValue()->IgnoreParenImpCasts())
          : nullptr;
  const auto *ScalarCall =
      ScalarReturn && ScalarReturn->getRetValue()
          ? dyn_cast<CXXMemberCallExpr>(
                ScalarReturn->getRetValue()->IgnoreParenImpCasts())
          : nullptr;
  const auto *ScalarMethod = ScalarCall ? ScalarCall->getMethodDecl() : nullptr;
  const auto *Scalar = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      ScalarMethod ? ScalarMethod->getParent()->getDefinition() : nullptr);
  const auto *ScalarArgument =
      ScalarCall && ScalarCall->getNumArgs() == 1
          ? dyn_cast<DeclRefExpr>(
                ScalarCall->getArg(0)->IgnoreParenImpCasts())
          : nullptr;
  const auto *Pattern =
      ScalarMethod ? ScalarMethod->getInstantiatedFromMemberFunction()
                   : nullptr;
  if (!ZeroIf || ZeroIf->getInit() || ZeroIf->getConditionVariable() ||
      ZeroIf->getElse() || !Condition || Condition->getOpcode() != BO_EQ ||
      !ConditionParameter ||
      ConditionParameter->getDecl() != Method->getParamDecl(0) ||
      !FloatingZero || !FloatingZero->getValue().isZero() ||
      !Context.hasSameType(FloatingZero->getType(), ValueType) || !ZeroReturn ||
      !IntegerZero || !IntegerZero->getValue().isZero() || !ScalarCall ||
      !ScalarMethod || !Scalar ||
      Base->getCanonicalDecl() != Scalar->getCanonicalDecl() ||
      ScalarMethod->getOverloadedOperator() != OO_Call ||
      ScalarMethod->isStatic() || !ScalarMethod->isConst() ||
      ScalarMethod->isVariadic() || ScalarMethod->getNumParams() != 1 ||
      !Context.hasSameType(ScalarMethod->getParamDecl(0)->getType(), ValueType) ||
      !Context.hasSameType(ScalarMethod->getReturnType(), Context.getSizeType()) ||
      !ScalarArgument || ScalarArgument->getDecl() != Method->getParamDecl(0) ||
      !Pattern || !approvedStandardSDKDeclaration(S, SM, ScalarMethod) ||
      !approvedStandardSDKDeclaration(S, SM, Pattern) ||
      !cstddefOrigin(S, SM, ScalarMethod->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      !cstddefOrigin(S, SM, Pattern->getLocation(), "libcxx",
                     "__functional/hash.h"))
    return std::nullopt;
  return FunctionalOperationInfo{FunctionalOperation::Hash,
                                 ValueType,
                                 {},
                                 Context.getSizeType(),
                                 Context.getSizeType()};
}

static std::optional<FunctionalOperationInfo> approvedWideIntegralHashOperation(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context, bool RequireOwnedReference) {
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call ? Call->getDirectCallee() : nullptr);
  const auto *Scalar = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Method ? Method->getParent()->getDefinition() : nullptr);
  const auto *Reference = directMethodReference(Call);
  const auto *ReceiverCast =
      Call && Call->getNumArgs()
          ? dyn_cast<ImplicitCastExpr>(Call->getArg(0)->IgnoreParens())
          : nullptr;
  const auto *Derived = ReceiverCast ? ReceiverCast->getSubExpr() : nullptr;
  const auto *Hash = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Derived ? Derived->getType()->getAsCXXRecordDecl() : nullptr);
  Hash = Hash ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                    Hash->getDefinition())
              : nullptr;
  const auto Object = approvedFunctionalObjectRecord(S, SM, Hash, Context);
  if (!Call || !Operator || !Method || !Scalar || !Reference || !ReceiverCast ||
      !Derived || !Hash || !Object || Operator->getOperator() != OO_Call ||
      Method->getOverloadedOperator() != OO_Call || Method->isStatic() ||
      !Method->isConst() || Method->isVariadic() || !Method->isInlined() ||
      !Method->hasBody() || !Call->isPRValue() ||
      Scalar->getName() != "__scalar_hash" || Scalar->isUnion() ||
      Scalar->isDependentContext() || !Scalar->isEmpty() ||
      !Scalar->isStandardLayout() || !Scalar->isTriviallyCopyable() ||
      !Scalar->hasTrivialDestructor() ||
      !approvedFunctionalObjectBaseCast(S, SM, ReceiverCast, Context) ||
      !approvedStandardSDKDeclaration(S, SM, Scalar) ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !cstddefOrigin(S, SM, Scalar->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      (RequireOwnedReference && !S.owns(SM, Reference->getExprLoc())))
    return std::nullopt;

  const auto &HashArguments = Hash->getTemplateArgs();
  const auto ValueType =
      HashArguments.size() == 1 &&
              HashArguments.get(0).getKind() == TemplateArgument::Type
          ? HashArguments.get(0).getAsType()
          : QualType();
  const auto *Base = scalarHashBase(S, SM, Hash, ValueType, Context);
  const auto &ScalarArguments = Scalar->getTemplateArgs();
  if (ScalarArguments.size() != 2 ||
      ScalarArguments.get(0).getKind() != TemplateArgument::Type ||
      ScalarArguments.get(1).getKind() != TemplateArgument::Integral)
    return std::nullopt;
  const auto Ratio =
      ScalarArguments.get(1).getAsIntegral().getZExtValue();
  const auto *Pattern = Method->getInstantiatedFromMemberFunction();
  if (!Base || Base->getCanonicalDecl() != Scalar->getCanonicalDecl() ||
      !Pattern || !approvedStandardSDKDeclaration(S, SM, Pattern) ||
      !cstddefOrigin(S, SM, Pattern->getLocation(), "libcxx",
                     "__functional/hash.h") ||
      !Context.hasSameType(ScalarArguments.get(0).getAsType(), ValueType) ||
      Ratio != Context.getTypeSize(ValueType) /
                   Context.getTypeSize(Context.getSizeType()) ||
      (Ratio != 1 && Ratio != 2) || Method->getNumParams() != 1 ||
      Call->getNumArgs() != 2 ||
      !Context.hasSameType(Method->getParamDecl(0)->getType(), ValueType) ||
      !Context.hasSameType(Call->getArg(1)->getType(), ValueType) ||
      !Context.hasSameType(Method->getReturnType(), Context.getSizeType()) ||
      !Context.hasSameType(Call->getType(), Context.getSizeType()) ||
      !Context.hasSameUnqualifiedType(ReceiverCast->getType(),
                                      Context.getRecordType(Scalar)) ||
      !Context.hasSameUnqualifiedType(Derived->getType(),
                                      Context.getRecordType(Hash)))
    return std::nullopt;
  return FunctionalOperationInfo{FunctionalOperation::Hash,
                                 ValueType,
                                 {},
                                 Context.getSizeType(),
                                 Context.getSizeType()};
}

static std::optional<FunctionalOperationInfo> approvedFunctionalOperationImpl(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context, bool RequireOwnedReference) {
  if (auto Pointer = approvedPointerHashOperation(
          S, SM, Call, Context, RequireOwnedReference))
    return Pointer;
  if (auto Enum = approvedEnumHashOperation(
          S, SM, Call, Context, RequireOwnedReference))
    return Enum;
  if (auto Floating = approvedFloatingHashOperation(
          S, SM, Call, Context, RequireOwnedReference))
    return Floating;
  if (auto Wide = approvedWideIntegralHashOperation(
          S, SM, Call, Context, RequireOwnedReference))
    return Wide;
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call ? Call->getDirectCallee() : nullptr);
  const auto *Record = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Method ? Method->getParent()->getDefinition() : nullptr);
  const auto *Template = Record ? Record->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  const auto *Reference = directMethodReference(Call);
  const auto Name = Record ? Record->getName() : llvm::StringRef();
  if (!Call || !Operator || !Method || !Record || !Template ||
      !CanonicalTemplate || !Reference || Operator->getOperator() != OO_Call ||
      Method->getOverloadedOperator() != OO_Call || Method->isStatic() ||
      !Method->isConst() || Method->isVariadic() ||
      (!Method->isConstexpr() && Name != "hash") || !Method->isInlined() ||
      !Method->hasBody() || !Call->isPRValue() || Record->isUnion() ||
      Record->isDependentContext() || !Record->isEmpty() ||
      !Record->isStandardLayout() || !Record->isTriviallyCopyable() ||
      !Record->hasTrivialDestructor() ||
      !approvedStandardSDKDeclaration(S, SM, Record) ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !functionalObjectOrigin(S, SM, Record, Name) ||
      !functionalObjectTemplateOrigin(S, SM, Template, Name, false) ||
      !functionalObjectTemplateOrigin(S, SM, CanonicalTemplate, Name, true) ||
      !functionalObjectOrigin(S, SM, Method, Name) ||
      (RequireOwnedReference && !S.owns(SM, Reference->getExprLoc())))
    return std::nullopt;

  const auto FunctionalRecord =
      approvedFunctionalObjectRecord(S, SM, Record, Context);
  if (!FunctionalRecord || FunctionalRecord->Record->getCanonicalDecl() !=
                               Record->getCanonicalDecl())
    return std::nullopt;

  const auto &Arguments = Record->getTemplateArgs();
  if (Arguments.size() != 1 ||
      Arguments.get(0).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto ValueType = Arguments.get(0).getAsType();
  if (Name == "hash") {
    const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
    if (ValueType->isNullPtrType()) {
      const auto *NullReturn = Body && Body->size() == 1
                                   ? dyn_cast<ReturnStmt>(*Body->body_begin())
                                   : nullptr;
      const auto *Constant =
          NullReturn && NullReturn->getRetValue()
              ? dyn_cast<IntegerLiteral>(
                    NullReturn->getRetValue()->IgnoreParenImpCasts())
              : nullptr;
      if (Method->getNumParams() != 1 || Call->getNumArgs() != 2 ||
          !Context.hasSameType(Method->getParamDecl(0)->getType(), ValueType) ||
          !Context.hasSameType(Call->getArg(1)->getType(), ValueType) ||
          !Context.hasSameType(Method->getReturnType(),
                               Context.getSizeType()) ||
          !Context.hasSameType(Call->getType(), Context.getSizeType()) ||
          !Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                          Context.getRecordType(Record)) ||
          !Constant || Constant->getValue() != 662607004)
        return std::nullopt;
      return FunctionalOperationInfo{FunctionalOperation::Hash,
                                     ValueType,
                                     {},
                                     Context.getSizeType(),
                                     Context.getSizeType()};
    }
    const ReturnStmt *Return = nullptr;
    const StaticAssertDecl *SizeAssertion = nullptr;
    if (Body && Body->size() == 1) {
      Return = dyn_cast<ReturnStmt>(*Body->body_begin());
    } else if (Body && Body->size() == 2 &&
               ValueType->isSpecificBuiltinType(BuiltinType::ULong)) {
      auto Statement = Body->body_begin();
      const auto *Declaration = dyn_cast<DeclStmt>(*Statement++);
      SizeAssertion =
          Declaration && Declaration->isSingleDecl()
              ? dyn_cast<StaticAssertDecl>(Declaration->getSingleDecl())
              : nullptr;
      Return = dyn_cast<ReturnStmt>(*Statement);
    }
    const auto *Conversion =
        Return && Return->getRetValue()
            ? dyn_cast<CXXStaticCastExpr>(
                  Return->getRetValue()->IgnoreParenImpCasts())
            : nullptr;
    const auto *ParameterReference =
        Conversion ? dyn_cast<DeclRefExpr>(
                         Conversion->getSubExpr()->IgnoreParenImpCasts())
                   : nullptr;
    if (!directIntegralHashType(ValueType) || Method->getNumParams() != 1 ||
        Call->getNumArgs() != 2 ||
        !Context.hasSameType(Method->getParamDecl(0)->getType(), ValueType) ||
        !Context.hasSameType(Call->getArg(1)->getType(), ValueType) ||
        !Context.hasSameType(Method->getReturnType(), Context.getSizeType()) ||
        !Context.hasSameType(Call->getType(), Context.getSizeType()) ||
        !Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                        Context.getRecordType(Record)) ||
        (Body && Body->size() == 2 &&
         (!SizeAssertion ||
          !approvedStandardSDKDeclaration(S, SM, SizeAssertion) ||
          !cstddefOrigin(S, SM, SizeAssertion->getLocation(), "libcxx",
                         "__functional/hash.h"))) ||
        !Conversion ||
        (Conversion->getCastKind() != CK_IntegralCast &&
         Conversion->getCastKind() != CK_NoOp) ||
        !Context.hasSameType(Conversion->getType(), Context.getSizeType()) ||
        !ParameterReference ||
        ParameterReference->getDecl() != Method->getParamDecl(0))
      return std::nullopt;
    return FunctionalOperationInfo{FunctionalOperation::Hash,
                                   ValueType,
                                   {},
                                   Context.getSizeType(),
                                   Context.getSizeType()};
  }
  const bool Transparent = !ValueType.isNull() && ValueType->isVoidType();
  auto SupportedScalar = [&](QualType Type) {
    if (Type.isNull())
      return false;
    Type = Type.getNonReferenceType();
    if (Type.isVolatileQualified())
      return false;
    Type = Type.getUnqualifiedType();
    return (Type->isIntegralType(Context) && Context.getTypeSize(Type) <= 64) ||
           Type->isSpecificBuiltinType(BuiltinType::Float) ||
           Type->isSpecificBuiltinType(BuiltinType::Double);
  };
  if (ValueType.isNull() ||
      (!Transparent && (ValueType.hasQualifiers() ||
                        !SupportedScalar(ValueType))))
    return std::nullopt;

  struct Spec {
    llvm::StringLiteral Name;
    FunctionalOperation Operation;
    BinaryOperatorKind Binary;
    UnaryOperatorKind Unary;
    bool Integral;
    bool BooleanResult;
  };
  static constexpr Spec Specs[] = {
      {"plus", FunctionalOperation::Plus, BO_Add, UO_Plus, false, false},
      {"minus", FunctionalOperation::Minus, BO_Sub, UO_Plus, false, false},
      {"multiplies", FunctionalOperation::Multiplies, BO_Mul, UO_Plus, false,
       false},
      {"divides", FunctionalOperation::Divides, BO_Div, UO_Plus, false, false},
      {"modulus", FunctionalOperation::Modulus, BO_Rem, UO_Plus, true, false},
      {"negate", FunctionalOperation::Negate, BO_Comma, UO_Minus, false, false},
      {"bit_and", FunctionalOperation::BitAnd, BO_And, UO_Plus, true, false},
      {"bit_or", FunctionalOperation::BitOr, BO_Or, UO_Plus, true, false},
      {"bit_xor", FunctionalOperation::BitXor, BO_Xor, UO_Plus, true, false},
      {"bit_not", FunctionalOperation::BitNot, BO_Comma, UO_Not, true, false},
      {"equal_to", FunctionalOperation::Equal, BO_EQ, UO_Plus, false, true},
      {"not_equal_to", FunctionalOperation::NotEqual, BO_NE, UO_Plus, false,
       true},
      {"less", FunctionalOperation::Less, BO_LT, UO_Plus, false, true},
      {"greater", FunctionalOperation::Greater, BO_GT, UO_Plus, false, true},
      {"less_equal", FunctionalOperation::LessEqual, BO_LE, UO_Plus, false,
       true},
      {"greater_equal", FunctionalOperation::GreaterEqual, BO_GE, UO_Plus,
       false, true},
      {"logical_and", FunctionalOperation::LogicalAnd, BO_LAnd, UO_Plus, false,
       true},
      {"logical_or", FunctionalOperation::LogicalOr, BO_LOr, UO_Plus, false,
       true},
      {"logical_not", FunctionalOperation::LogicalNot, BO_Comma, UO_LNot, false,
       true},
  };
  const Spec *Selected = nullptr;
  for (const auto &Candidate : Specs)
    if (Candidate.Name == Record->getName()) {
      Selected = &Candidate;
      break;
    }
  if (!Selected ||
      (!Transparent && Selected->Integral &&
       !ValueType->isIntegralType(Context)))
    return std::nullopt;
  const bool Unary = Selected->Operation == FunctionalOperation::Negate ||
                     Selected->Operation == FunctionalOperation::BitNot ||
                     Selected->Operation == FunctionalOperation::LogicalNot;
  const unsigned Parameters = Unary ? 1u : 2u;
  const auto ExpectedResult = Selected->BooleanResult ? Context.BoolTy
                                                       : ValueType;
  if (Method->getNumParams() != Parameters ||
      Call->getNumArgs() != Parameters + 1 ||
      (!Transparent &&
       !Context.hasSameType(Method->getReturnType(), ExpectedResult)) ||
      !Context.hasSameType(Call->getType(), Method->getReturnType()) ||
      !Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                      Context.getRecordType(Record)))
    return std::nullopt;

  const FunctionTemplateDecl *Primary = nullptr;
  const CXXMethodDecl *Pattern = nullptr;
  const TemplateArgumentList *SpecializedArguments = nullptr;
  if (Transparent) {
    Primary = Method->getPrimaryTemplate();
    Pattern = Primary ? dyn_cast<CXXMethodDecl>(Primary->getTemplatedDecl())
                      : nullptr;
    SpecializedArguments = Method->getTemplateSpecializationArgs();
    const auto *ParametersList = Primary ? Primary->getTemplateParameters()
                                         : nullptr;
    const TypedefNameDecl *TransparentMarker = nullptr;
    for (const auto *Declaration : Record->decls())
      if (const auto *Alias = dyn_cast<TypedefNameDecl>(Declaration);
          Alias && Alias->getIdentifier() &&
          Alias->getName() == "is_transparent") {
        if (TransparentMarker)
          return std::nullopt;
        TransparentMarker = Alias;
      }
    if (Record->getSpecializationKind() != TSK_ExplicitSpecialization ||
        !Primary || !Pattern || !SpecializedArguments || !ParametersList ||
        ParametersList->size() != Parameters ||
        SpecializedArguments->size() != Parameters ||
        !Pattern->hasBody() || Pattern->getNumParams() != Parameters ||
        !approvedStandardSDKDeclaration(S, SM, Primary) ||
        !approvedStandardSDKDeclaration(S, SM, Pattern) ||
        !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                       "__functional/operations.h") ||
        !cstddefOrigin(S, SM, Pattern->getLocation(), "libcxx",
                       "__functional/operations.h") ||
        !TransparentMarker ||
        !TransparentMarker->getUnderlyingType()->isVoidType() ||
        !approvedStandardSDKDeclaration(S, SM, TransparentMarker) ||
        !cstddefOrigin(S, SM, TransparentMarker->getLocation(), "libcxx",
                       "__functional/operations.h"))
      return std::nullopt;
    for (unsigned I = 0; I < Parameters; ++I) {
      const auto *TypeParameter =
          dyn_cast<TemplateTypeParmDecl>(ParametersList->getParam(I));
      const auto Argument = SpecializedArguments->get(I);
      const auto Deduced = Argument.getKind() == TemplateArgument::Type
                               ? Argument.getAsType()
                               : QualType();
      const auto PatternParameter = Pattern->getParamDecl(I)->getType();
      const auto InstantiatedParameter = Method->getParamDecl(I)->getType();
      const auto ExpectedParameter =
          Deduced.isNull() || Deduced->isLValueReferenceType()
              ? Deduced
              : Context.getRValueReferenceType(Deduced);
      if (!TypeParameter || TypeParameter->isParameterPack() ||
          Deduced.isNull() || !SupportedScalar(Deduced) ||
          !PatternParameter->isRValueReferenceType() ||
          !Context.hasSameType(InstantiatedParameter, ExpectedParameter) ||
          !Context.hasSameUnqualifiedType(
              Call->getArg(I + 1)->getType(), Deduced.getNonReferenceType()))
        return std::nullopt;
    }
  } else {
    for (unsigned I = 0; I < Parameters; ++I) {
      const auto Parameter = Method->getParamDecl(I)->getType();
      if (!Parameter->isLValueReferenceType() ||
          !Parameter->getPointeeType().isConstQualified() ||
          Parameter->getPointeeType().isVolatileQualified() ||
          !Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                          ValueType) ||
          !Context.hasSameUnqualifiedType(Call->getArg(I + 1)->getType(),
                                          ValueType))
        return std::nullopt;
    }
  }

  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(*Body->body_begin())
                           : nullptr;
  const auto *Returned = Return ? Return->getRetValue() : nullptr;
  const auto *Operation = Returned ? Returned->IgnoreParenImpCasts() : nullptr;
  auto ParameterReference = [&](const Expr *Expression, unsigned Index) {
    Expression = Expression ? Expression->IgnoreParenImpCasts() : nullptr;
    if (Transparent) {
      const auto *Forward = dyn_cast_or_null<CallExpr>(Expression);
      const auto *Function = Forward ? Forward->getDirectCallee() : nullptr;
      const auto *ForwardPrimary =
          Function ? Function->getPrimaryTemplate() : nullptr;
      const auto *Reference = directFunctionReference(Forward);
      const auto *ForwardArguments =
          Function ? Function->getTemplateSpecializationArgs() : nullptr;
      const auto Specialized = SpecializedArguments->get(Index).getAsType();
      const auto *Ref = Forward && Forward->getNumArgs() == 1
                            ? dyn_cast<DeclRefExpr>(
                                  Forward->getArg(0)->IgnoreParenImpCasts())
                            : nullptr;
      return Function && ForwardPrimary && Reference && ForwardArguments &&
             ForwardArguments->size() == 1 &&
             ForwardArguments->get(0).getKind() == TemplateArgument::Type &&
             Context.hasSameType(ForwardArguments->get(0).getAsType(),
                                 Specialized) &&
             Function->getIdentifier() && Function->getName() == "forward" &&
             approvedStandardSDKDeclaration(S, SM, Function) &&
             approvedStandardSDKDeclaration(S, SM, ForwardPrimary) &&
             cstddefOrigin(S, SM, ForwardPrimary->getLocation(), "libcxx",
                            "__utility/forward.h") &&
             Ref && Ref->getDecl() == Method->getParamDecl(Index);
    }
    const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Expression);
    return Ref && Ref->getDecl() == Method->getParamDecl(Index);
  };
  QualType LeftType;
  QualType RightType;
  if (Unary) {
    const auto *Expression = dyn_cast_or_null<UnaryOperator>(Operation);
    if (!Expression || Expression->getOpcode() != Selected->Unary ||
        !ParameterReference(Expression->getSubExpr(), 0))
      return std::nullopt;
    LeftType = Expression->getSubExpr()->getType();
  } else {
    const auto *Expression = dyn_cast_or_null<BinaryOperator>(Operation);
    if (!Expression || Expression->getOpcode() != Selected->Binary ||
        !ParameterReference(Expression->getLHS(), 0) ||
        !ParameterReference(Expression->getRHS(), 1))
      return std::nullopt;
    LeftType = Expression->getLHS()->getType();
    RightType = Expression->getRHS()->getType();
  }
  if (!SupportedScalar(LeftType) || (!Unary && !SupportedScalar(RightType)) ||
      (Selected->Integral &&
       (!LeftType->isIntegralType(Context) ||
        (!Unary && !RightType->isIntegralType(Context)))) ||
      !Context.hasSameType(Returned->getType(), Method->getReturnType()))
    return std::nullopt;
  return FunctionalOperationInfo{Selected->Operation, LeftType, RightType,
                                 Operation->getType(), Method->getReturnType()};
}

std::optional<FunctionalOperationInfo>
approvedFunctionalOperation(const State &S, const SourceManager &SM,
                            const CallExpr *Call, const ASTContext &Context) {
  return approvedFunctionalOperationImpl(S, SM, Call, Context, true);
}

std::optional<MemoryTemplateMetadata>
approvedMemoryTemplateMetadata(const State &S, const SourceManager &SM,
                               const CXXRecordDecl *Record) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  const auto *Template =
      Specialization ? Specialization->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  if (!Specialization || !Template || !CanonicalTemplate ||
      Specialization->isUnion() || Specialization->isDependentContext() ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate))
    return std::nullopt;
  const auto &Arguments = Specialization->getTemplateArgs();
  auto OneTypeArgument = [&]() {
    return Arguments.size() == 1 &&
           Arguments.get(0).getKind() == TemplateArgument::Type;
  };
  const auto Name = Specialization->getName();
  if (Name == "pointer_traits" && OneTypeArgument() &&
      cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                    "__memory/pointer_traits.h") &&
      cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                    "__memory/pointer_traits.h")) {
    const auto Pointer = Arguments.get(0).getAsType();
    if (!Pointer.isNull() && Pointer.getAddressSpace() == LangAS::Default &&
        Pointer->isPointerType() && !Pointer->isFunctionPointerType() &&
        Pointer->getPointeeType().getAddressSpace() == LangAS::Default)
      return MemoryTemplateMetadata::PointerTraits;
  }
  if (Name == "default_delete" && OneTypeArgument() &&
      cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                    "__memory/unique_ptr.h") &&
      cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                    "__memory/unique_ptr.h")) {
    const auto Specialized = Arguments.get(0).getAsType();
    const auto *Array =
        Specialized.isNull() ? nullptr : Specialized->getAsArrayTypeUnsafe();
    const auto Element = Array ? Array->getElementType() : Specialized;
    const auto BaseElement =
        Element.isNull()
            ? QualType()
            : Specialization->getASTContext().getBaseElementType(Element);
    if (!Element.isNull() && !BaseElement.isNull() &&
        !BaseElement.isVolatileQualified() && Element->isObjectType() &&
        (!Element->isArrayType() || !Element->isIncompleteType()) &&
        (!Array || isa<IncompleteArrayType>(Array)))
      return MemoryTemplateMetadata::DefaultDelete;
  }
  if (Name == "unique_ptr" && Arguments.size() == 2 &&
      Arguments.get(0).getKind() == TemplateArgument::Type &&
      Arguments.get(1).getKind() == TemplateArgument::Type &&
      cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                    "__memory/unique_ptr.h") &&
      cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                    "__memory/unique_ptr.h"))
    return MemoryTemplateMetadata::UniquePtr;
  if (Name == "allocator" && OneTypeArgument() &&
      cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                    "__memory/allocator.h") &&
      cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                    "__fwd/memory.h")) {
    const auto Element = Arguments.get(0).getAsType();
    if (!Element.isNull() && !Element.isConstQualified() &&
        !Element.isVolatileQualified() &&
        (Element->isVoidType() ||
         (Element->isObjectType() && !Element->isArrayType())))
      return MemoryTemplateMetadata::Allocator;
  }
  if (Name == "allocator_traits" && OneTypeArgument() &&
      cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                    "__memory/allocator_traits.h") &&
      cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                    "__memory/allocator_traits.h")) {
    const auto Allocator = Arguments.get(0).getAsType();
    const auto Nested = approvedMemoryTemplateMetadata(
        S, SM, Allocator.isNull() ? nullptr : Allocator->getAsCXXRecordDecl());
    if (Nested == MemoryTemplateMetadata::Allocator)
      return MemoryTemplateMetadata::AllocatorTraits;
  }
  if (Name == "uses_allocator" && Arguments.size() == 2 &&
      Arguments.get(0).getKind() == TemplateArgument::Type &&
      Arguments.get(1).getKind() == TemplateArgument::Type &&
      cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                    "__memory/uses_allocator.h") &&
      cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                    "__memory/uses_allocator.h")) {
    const auto Allocator = Arguments.get(1).getAsType();
    const auto Nested = approvedMemoryTemplateMetadata(
        S, SM, Allocator.isNull() ? nullptr : Allocator->getAsCXXRecordDecl());
    if (Nested == MemoryTemplateMetadata::Allocator)
      return MemoryTemplateMetadata::UsesAllocator;
  }
  return std::nullopt;
}

std::optional<UtilityDefaultDeleteRecord>
approvedUtilityDefaultDeleteRecord(const State &S, const SourceManager &SM,
                                   const CXXRecordDecl *Record,
                                   const ASTContext &Context) {
  const auto *Definition = Record ? Record->getDefinition() : nullptr;
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Definition);
  if (!Definition || !Specialization || Definition->isInvalidDecl() ||
      Definition->isUnion() || Definition->isDependentContext() ||
      approvedMemoryTemplateMetadata(S, SM, Definition) !=
          MemoryTemplateMetadata::DefaultDelete ||
      !approvedStandardSDKDeclaration(S, SM, Definition) ||
      !cstddefOrigin(S, SM, Definition->getLocation(), "libcxx",
                     "__memory/unique_ptr.h") ||
      !Definition->field_empty() || Definition->getNumBases() ||
      Definition->isDynamicClass() || !Definition->isEmpty() ||
      !Definition->isStandardLayout() || !Definition->hasTrivialDestructor() ||
      !Definition->hasTrivialDefaultConstructor())
    return std::nullopt;
  const auto &Arguments = Specialization->getTemplateArgs();
  if (Arguments.size() != 1 ||
      Arguments.get(0).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto Specialized = Arguments.get(0).getAsType();
  const auto *Array =
      Specialized.isNull() ? nullptr : Context.getAsArrayType(Specialized);
  const auto Element = Array ? Array->getElementType() : Specialized;
  const auto BaseElement =
      Element.isNull() ? QualType() : Context.getBaseElementType(Element);
  const auto &Layout = Context.getASTRecordLayout(Definition);
  if (Element.isNull() || BaseElement.isNull() ||
      BaseElement.isVolatileQualified() || !Element->isObjectType() ||
      (!Array && Element->isArrayType()) ||
      (Element->isArrayType() && Element->isIncompleteType()) ||
      (Array && !isa<IncompleteArrayType>(Array)) ||
      Layout.getSize().getQuantity() != 1 ||
      Layout.getAlignment().getQuantity() != 1)
    return std::nullopt;
  return UtilityDefaultDeleteRecord{Definition, Element, Array != nullptr};
}

static const CXXDeleteExpr *
utilityDefaultDeleteExpression(const State &S, const SourceManager &SM,
                               const UtilityDefaultDeleteRecord &Deleter,
                               const ASTContext &Context,
                               const CXXMethodDecl *Expected = nullptr) {
  auto Inspect = [&](const CXXMethodDecl *Method) -> const CXXDeleteExpr * {
    const auto *Prototype =
        Method ? Method->getType()->getAs<FunctionProtoType>() : nullptr;
    if (!Method || !Prototype || !Prototype->isNothrow() ||
        Method->getParent()->getCanonicalDecl() !=
            Deleter.Record->getCanonicalDecl() ||
        Method->isStatic() || !Method->isConst() || Method->isVariadic() ||
        Method->getOverloadedOperator() != OO_Call ||
        Method->getNumParams() != 1 || !Method->getReturnType()->isVoidType() ||
        !Method->isInlined() || !Method->hasBody() ||
        !approvedStandardSDKDeclaration(S, SM, Method) ||
        !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                       "__memory/unique_ptr.h"))
      return nullptr;
    const auto Pointer = Context.getPointerType(Deleter.ElementType);
    if (!Context.hasSameType(Method->getParamDecl(0)->getType(), Pointer))
      return nullptr;

    const CXXDeleteExpr *Deletion = nullptr;
    unsigned Deletions = 0;
    auto FindDeletion = [&](auto &&Self, const Stmt *Node,
                            unsigned Depth) -> void {
      if (!Node || Depth > 32)
        return;
      if (const auto *Delete = dyn_cast<CXXDeleteExpr>(Node)) {
        ++Deletions;
        Deletion = Delete;
      }
      for (const auto *Child : Node->children())
        Self(Self, Child, Depth + 1);
    };
    FindDeletion(FindDeletion, Method->getBody(), 0);
    const auto *Argument =
        Deletion ? dyn_cast<DeclRefExpr>(
                       Deletion->getArgument()->IgnoreParenImpCasts())
                 : nullptr;
    const auto *DeleteFunction =
        Deletion ? Deletion->getOperatorDelete() : nullptr;
    const auto *DeleteMethod = dyn_cast_or_null<CXXMethodDecl>(DeleteFunction);
    if (!Deletion || Deletions != 1 ||
        Deletion->isArrayForm() != Deleter.Array || !Argument ||
        Argument->getDecl() != Method->getParamDecl(0) || !DeleteFunction ||
        DeleteFunction->getOverloadedOperator() !=
            (Deleter.Array ? OO_Array_Delete : OO_Delete) ||
        (!DeleteMethod && !DeleteFunction->getDeclContext()
                               ->getRedeclContext()
                               ->isTranslationUnit()) ||
        !Context.hasSameType(Deletion->getArgument()->getType(), Pointer) ||
        !Context.hasSameType(Deletion->getDestroyedType(),
                             Deleter.ElementType) ||
        !cstddefOrigin(S, SM, Deletion->getExprLoc(), "libcxx",
                       "__memory/unique_ptr.h"))
      return nullptr;
    return Deletion;
  };

  if (Expected)
    return Inspect(Expected);
  const CXXDeleteExpr *Result = nullptr;
  bool Ambiguous = false;
  std::set<const CXXMethodDecl *> Seen;
  auto Consider = [&](const CXXMethodDecl *Method) {
    if (!Method || !Seen.insert(Method->getCanonicalDecl()).second)
      return;
    const auto *Deletion = Inspect(Method);
    if (!Deletion)
      return;
    if (Result) {
      Ambiguous = true;
      return;
    }
    Result = Deletion;
  };
  for (const auto *Method : Deleter.Record->methods()) {
    if (Method->getOverloadedOperator() != OO_Call)
      continue;
    Consider(Method);
  }
  for (const auto *Member : Deleter.Record->decls()) {
    const auto *Template = dyn_cast<FunctionTemplateDecl>(Member);
    if (!Template ||
        Template->getTemplatedDecl()->getOverloadedOperator() != OO_Call)
      continue;
    for (const auto *Specialization : Template->specializations())
      Consider(dyn_cast<CXXMethodDecl>(Specialization));
  }
  return Ambiguous ? nullptr : Result;
}

std::optional<UtilityUniquePtrRecord>
approvedUtilityUniquePtrRecord(const State &S, const SourceManager &SM,
                               const CXXRecordDecl *Record,
                               const ASTContext &Context) {
  const auto *Definition = Record ? Record->getDefinition() : nullptr;
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Definition);
  if (!Definition || !Specialization || Definition->isInvalidDecl() ||
      Definition->isUnion() || Definition->isDependentContext() ||
      approvedMemoryTemplateMetadata(S, SM, Definition) !=
          MemoryTemplateMetadata::UniquePtr ||
      !approvedStandardSDKDeclaration(S, SM, Definition) ||
      !cstddefOrigin(S, SM, Definition->getLocation(), "libcxx",
                     "__memory/unique_ptr.h") ||
      Definition->getNumBases() || Definition->isDynamicClass() ||
      !Definition->isStandardLayout())
    return std::nullopt;
  const auto &Arguments = Specialization->getTemplateArgs();
  if (Arguments.size() != 2 ||
      Arguments.get(0).getKind() != TemplateArgument::Type ||
      Arguments.get(1).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto Specialized = Arguments.get(0).getAsType();
  const auto *Array =
      Specialized.isNull() ? nullptr : Context.getAsArrayType(Specialized);
  const auto Element = Array ? Array->getElementType() : Specialized;
  const auto BaseElement =
      Element.isNull() ? QualType() : Context.getBaseElementType(Element);
  const auto Pointer = Context.getPointerType(Element);
  if (Element.isNull() || BaseElement.isNull() ||
      BaseElement.isVolatileQualified() || !Element->isObjectType() ||
      (!Array && Element->isArrayType()) ||
      (Element->isArrayType() && Element->isIncompleteType()) ||
      (Array && !isa<IncompleteArrayType>(Array)) || Pointer.isNull())
    return std::nullopt;

  std::vector<const FieldDecl *> Fields;
  for (const auto *Field : Definition->fields())
    Fields.push_back(Field);
  const auto ExpectedFields = Array ? 5u : 4u;
  const std::string PaddingLine = Array ? "431" : "162";
  if (Fields.size() != ExpectedFields || Fields[0]->getName() != "__ptr_" ||
      Fields[1]->getName() != "__padding1_" + PaddingLine + "_" ||
      Fields[2]->getName() != "__deleter_" ||
      Fields[3]->getName() != "__padding2_" + PaddingLine + "_" ||
      (Array && Fields[4]->getName() != "__checker_") ||
      !Context.hasSameType(Fields[0]->getType(), Pointer))
    return std::nullopt;
  if (!Context.hasSameType(Arguments.get(1).getAsType(), Fields[2]->getType()))
    return std::nullopt;

  auto Deleter = approvedUtilityDefaultDeleteRecord(
      S, SM, Fields[2]->getType()->getAsCXXRecordDecl(), Context);
  const bool StandardDeleter = Deleter.has_value();
  const CXXMethodDecl *CustomDeleter = nullptr;
  if (Deleter) {
    if (!Context.hasSameType(Deleter->ElementType, Element) ||
        Deleter->Array != (Array != nullptr))
      return std::nullopt;
  } else {
    const auto *CustomRecord = Fields[2]->getType()->getAsCXXRecordDecl();
    const auto *CustomDefinition =
        CustomRecord ? CustomRecord->getDefinition() : nullptr;
    if (!CustomDefinition || CustomDefinition->isInvalidDecl() ||
        CustomDefinition->isUnion() || CustomDefinition->isDependentContext() ||
        !S.owns(SM, CustomDefinition->getLocation()) ||
        !CustomDefinition->field_empty() || CustomDefinition->getNumBases() ||
        CustomDefinition->isDynamicClass() || !CustomDefinition->isEmpty() ||
        !CustomDefinition->isStandardLayout() ||
        !CustomDefinition->isTrivial() ||
        !CustomDefinition->hasTrivialDefaultConstructor() ||
        !CustomDefinition->hasTrivialCopyConstructor() ||
        !CustomDefinition->hasTrivialMoveConstructor() ||
        !CustomDefinition->hasTrivialCopyAssignment() ||
        !CustomDefinition->hasTrivialMoveAssignment() ||
        !CustomDefinition->hasTrivialDestructor())
      return std::nullopt;
    const auto &CustomLayout = Context.getASTRecordLayout(CustomDefinition);
    if (CustomLayout.getSize().getQuantity() != 1 ||
        CustomLayout.getAlignment().getQuantity() != 1)
      return std::nullopt;
    unsigned CallOperators = 0;
    for (const auto *Method : CustomDefinition->methods()) {
      if (Method->getOverloadedOperator() != OO_Call)
        continue;
      ++CallOperators;
      const auto *Prototype = Method->getType()->getAs<FunctionProtoType>();
      const FunctionDecl *BodyDefinition = nullptr;
      if (!ordinaryOperator(Method) || !Prototype || !Prototype->isNothrow() ||
          Method->isStatic() || Method->getRefQualifier() != RQ_None ||
          Method->getNumParams() != 1 ||
          !Method->getReturnType()->isVoidType() ||
          !Context.hasSameType(Method->getParamDecl(0)->getType(), Pointer) ||
          !Method->hasBody(BodyDefinition) || !BodyDefinition ||
          !S.owns(SM, BodyDefinition->getLocation()))
        return std::nullopt;
      CustomDeleter = cast<CXXMethodDecl>(BodyDefinition);
    }
    if (CallOperators != 1 || !CustomDeleter)
      return std::nullopt;
    Deleter =
        UtilityDefaultDeleteRecord{CustomDefinition, Element, Array != nullptr};
  }

  auto Padding = [&](const FieldDecl *Field, QualType Padded) {
    const auto *PaddingDefinition =
        Field->getType()->getAsCXXRecordDecl()
            ? Field->getType()->getAsCXXRecordDecl()->getDefinition()
            : nullptr;
    const auto *PaddingSpecialization =
        dyn_cast_or_null<ClassTemplateSpecializationDecl>(PaddingDefinition);
    const auto *PaddingTemplate =
        PaddingSpecialization ? PaddingSpecialization->getSpecializedTemplate()
                              : nullptr;
    if (!PaddingDefinition || !PaddingSpecialization || !PaddingTemplate ||
        PaddingDefinition->isInvalidDecl() || PaddingDefinition->isUnion() ||
        PaddingDefinition->isDependentContext() ||
        PaddingDefinition->getName() != "__compressed_pair_padding" ||
        !approvedStandardSDKDeclaration(S, SM, PaddingDefinition) ||
        !approvedStandardSDKDeclaration(S, SM, PaddingTemplate) ||
        !cstddefOrigin(S, SM, PaddingDefinition->getLocation(), "libcxx",
                       "__memory/compressed_pair.h") ||
        !cstddefOrigin(S, SM, PaddingTemplate->getLocation(), "libcxx",
                       "__memory/compressed_pair.h") ||
        !PaddingDefinition->field_empty() || PaddingDefinition->getNumBases() ||
        !PaddingDefinition->isEmpty() || !PaddingDefinition->isStandardLayout())
      return false;
    const auto &PaddingArguments = PaddingSpecialization->getTemplateArgs();
    if (PaddingArguments.size() != 2 ||
        PaddingArguments.get(0).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(PaddingArguments.get(0).getAsType(), Padded))
      return false;
    const auto &PaddingLayout = Context.getASTRecordLayout(PaddingDefinition);
    return PaddingLayout.getSize().getQuantity() == 1 &&
           PaddingLayout.getAlignment().getQuantity() == 1;
  };
  if (!Padding(Fields[1], Pointer) || !Padding(Fields[3], Fields[2]->getType()))
    return std::nullopt;
  if (Array) {
    const auto *Checker = Fields[4]->getType()->getAsCXXRecordDecl();
    const auto *CheckerDefinition = Checker ? Checker->getDefinition() : nullptr;
    if (!CheckerDefinition || CheckerDefinition->isInvalidDecl() ||
        CheckerDefinition->isUnion() ||
        CheckerDefinition->isDependentContext() ||
        CheckerDefinition->getName() != "__unique_ptr_array_bounds_stateless" ||
        !approvedStandardSDKDeclaration(S, SM, CheckerDefinition) ||
        !cstddefOrigin(S, SM, CheckerDefinition->getLocation(), "libcxx",
                       "__memory/unique_ptr.h") ||
        !CheckerDefinition->field_empty() || CheckerDefinition->getNumBases() ||
        CheckerDefinition->isDynamicClass() || !CheckerDefinition->isEmpty() ||
        !CheckerDefinition->isStandardLayout() ||
        !CheckerDefinition->hasTrivialDestructor() ||
        !CheckerDefinition->hasTrivialDefaultConstructor())
      return std::nullopt;
    const auto &CheckerLayout = Context.getASTRecordLayout(CheckerDefinition);
    if (CheckerLayout.getSize().getQuantity() != 1 ||
        CheckerLayout.getAlignment().getQuantity() != 1)
      return std::nullopt;
  }

  const auto &Layout = Context.getASTRecordLayout(Definition);
  if (Layout.getSize().getQuantity() !=
          Context.getTypeSizeInChars(Pointer).getQuantity() ||
      Layout.getAlignment().getQuantity() !=
          Context.getTypeAlignInChars(Pointer).getQuantity())
    return std::nullopt;
  for (unsigned I = 0; I != ExpectedFields; ++I)
    if (Layout.getFieldOffset(I) != 0)
      return std::nullopt;
  const auto *DefaultDeletion =
      StandardDeleter ? utilityDefaultDeleteExpression(S, SM, *Deleter, Context)
                      : nullptr;
  if (StandardDeleter && !DefaultDeletion)
    return std::nullopt;
  return UtilityUniquePtrRecord{Definition, Element,         Pointer,
                                *Deleter,   DefaultDeletion, CustomDeleter};
}

std::optional<UtilityAllocatorRecord>
approvedUtilityAllocatorRecord(const State &S, const SourceManager &SM,
                               const CXXRecordDecl *Record,
                               const ASTContext &Context) {
  const auto *Definition = Record ? Record->getDefinition() : nullptr;
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Definition);
  if (!Definition || !Specialization || Definition->isInvalidDecl() ||
      Definition->isUnion() || Definition->isDependentContext() ||
      approvedMemoryTemplateMetadata(S, SM, Definition) !=
          MemoryTemplateMetadata::Allocator ||
      !approvedStandardSDKDeclaration(S, SM, Definition) ||
      !Definition->field_empty() || Definition->isDynamicClass() ||
      !Definition->isEmpty())
    return std::nullopt;
  const auto &Arguments = Specialization->getTemplateArgs();
  if (Arguments.size() != 1 ||
      Arguments.get(0).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto Element = Arguments.get(0).getAsType();
  const auto &Layout = Context.getASTRecordLayout(Definition);
  if (Element.isNull() || Layout.getSize().getQuantity() != 1 ||
      Layout.getAlignment().getQuantity() != 1)
    return std::nullopt;
  if (Element->isVoidType()) {
    if (Definition->getNumBases())
      return std::nullopt;
    return UtilityAllocatorRecord{Definition, Element};
  }
  if (Definition->getNumBases() != 1)
    return std::nullopt;
  const auto &Base = *Definition->bases_begin();
  const auto *BaseRecord = Base.getType()->getAsCXXRecordDecl();
  const auto *BaseDefinition =
      BaseRecord ? BaseRecord->getDefinition() : nullptr;
  const auto *BaseSpecialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(BaseDefinition);
  const auto *BaseTemplate = BaseSpecialization
                                 ? BaseSpecialization->getSpecializedTemplate()
                                 : nullptr;
  if (Base.isVirtual() || Base.isPackExpansion() || !Base.getTypeSourceInfo() ||
      !BaseDefinition || !BaseSpecialization || !BaseTemplate ||
      BaseDefinition->isInvalidDecl() || BaseDefinition->isUnion() ||
      BaseDefinition->isDependentContext() || !BaseDefinition->field_empty() ||
      BaseDefinition->getNumBases() || !BaseDefinition->isEmpty() ||
      BaseSpecialization->getName() != "__non_trivial_if" ||
      !approvedStandardSDKDeclaration(S, SM, BaseDefinition) ||
      !approvedStandardSDKDeclaration(S, SM, BaseTemplate) ||
      !cstddefOrigin(S, SM, BaseTemplate->getLocation(), "libcxx",
                     "__memory/allocator.h") ||
      !Layout.getBaseClassOffset(BaseDefinition).isZero())
    return std::nullopt;
  const auto &BaseArguments = BaseSpecialization->getTemplateArgs();
  if (BaseArguments.size() != 2 ||
      BaseArguments.get(0).getKind() != TemplateArgument::Integral ||
      BaseArguments.get(0).getAsIntegral().isZero() ||
      BaseArguments.get(1).getKind() != TemplateArgument::Type ||
      !Context.hasSameUnqualifiedType(BaseArguments.get(1).getAsType(),
                                      Context.getRecordType(Definition)))
    return std::nullopt;
  const auto &BaseLayout = Context.getASTRecordLayout(BaseDefinition);
  if (BaseLayout.getSize().getQuantity() != 1 ||
      BaseLayout.getAlignment().getQuantity() != 1)
    return std::nullopt;
  return UtilityAllocatorRecord{Definition, Element};
}

std::optional<UtilityAllocatorTraitsRecord>
approvedUtilityAllocatorTraitsRecord(const State &S, const SourceManager &SM,
                                     const CXXRecordDecl *Record,
                                     const ASTContext &Context) {
  const auto *Definition = Record ? Record->getDefinition() : nullptr;
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Definition);
  if (!Definition || !Specialization || Definition->isInvalidDecl() ||
      Definition->isUnion() || Definition->isDependentContext() ||
      approvedMemoryTemplateMetadata(S, SM, Definition) !=
          MemoryTemplateMetadata::AllocatorTraits ||
      !approvedStandardSDKDeclaration(S, SM, Definition) ||
      !Definition->field_empty() || Definition->getNumBases() ||
      Definition->isDynamicClass() || !Definition->isEmpty())
    return std::nullopt;
  const auto &Arguments = Specialization->getTemplateArgs();
  if (Arguments.size() != 1 ||
      Arguments.get(0).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto AllocatorType = Arguments.get(0).getAsType();
  const auto Allocator = approvedUtilityAllocatorRecord(
      S, SM,
      AllocatorType.isNull() ? nullptr : AllocatorType->getAsCXXRecordDecl(),
      Context);
  if (!Allocator ||
      !Context.hasSameUnqualifiedType(
          AllocatorType, Context.getRecordType(Allocator->Record)))
    return std::nullopt;
  return UtilityAllocatorTraitsRecord{Definition, *Allocator};
}

std::optional<UtilityAllocatorConstruction>
approvedUtilityAllocatorConstruction(const State &S, const SourceManager &SM,
                                     const CXXConstructExpr *Construction,
                                     const ASTContext &Context) {
  if (!Construction || Construction->isTypeDependent() ||
      Construction->isValueDependent() ||
      Construction->isInstantiationDependent() ||
      Construction->getConstructionKind() != CXXConstructionKind::Complete)
    return std::nullopt;
  const auto *Constructor = Construction->getConstructor();
  const auto Allocator = approvedUtilityAllocatorRecord(
      S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
  const auto *Prototype =
      Constructor ? Constructor->getType()->getAs<FunctionProtoType>()
                  : nullptr;
  if (!Constructor || !Allocator || !Prototype || !Prototype->isNothrow() ||
      Constructor->getParent()->getCanonicalDecl() !=
          Allocator->Record->getCanonicalDecl() ||
      Constructor->isVariadic() ||
      Construction->getNumArgs() != Constructor->getNumParams() ||
      !approvedStandardSDKDeclaration(S, SM, Constructor))
    return std::nullopt;
  if (!Construction->getNumArgs() && Constructor->isDefaultConstructor())
    return UtilityAllocatorConstruction::Default;
  if (Construction->getNumArgs() != 1)
    return std::nullopt;
  const auto Source = approvedUtilityAllocatorRecord(
      S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(), Context);
  const auto Parameter = Constructor->getParamDecl(0)->getType();
  if (!Source || !Parameter->isReferenceType() ||
      !Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                      Context.getRecordType(Source->Record)) ||
      !Context.hasSameUnqualifiedType(Construction->getArg(0)->getType(),
                                      Context.getRecordType(Source->Record)))
    return std::nullopt;
  if (Allocator->Record->getCanonicalDecl() ==
          Source->Record->getCanonicalDecl() &&
      Constructor->isCopyOrMoveConstructor())
    return UtilityAllocatorConstruction::CopyOrMove;
  const auto *Primary = Constructor->getPrimaryTemplate();
  if (!Primary || !Constructor->hasBody() ||
      !Parameter->isLValueReferenceType() ||
      !Parameter->getPointeeType().isConstQualified() ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__memory/allocator.h"))
    return std::nullopt;
  return UtilityAllocatorConstruction::Converting;
}

bool approvedUtilityAllocatorAssignment(const State &S, const SourceManager &SM,
                                        const CXXOperatorCallExpr *Assignment,
                                        const ASTContext &Context) {
  if (!Assignment || Assignment->isTypeDependent() ||
      Assignment->isValueDependent() ||
      Assignment->isInstantiationDependent() ||
      Assignment->getOperator() != OO_Equal || Assignment->getNumArgs() != 2 ||
      !Assignment->isLValue())
    return false;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Assignment->getDirectCallee());
  const auto Allocator = approvedUtilityAllocatorRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  const auto Source = approvedUtilityAllocatorRecord(
      S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  const auto *Reference = directMethodReference(Assignment);
  if (!Method || !Allocator || !Source || !Reference || Method->isStatic() ||
      Method->isVariadic() || Method->getNumParams() != 1 ||
      Method->getOverloadedOperator() != OO_Equal || !Method->isImplicit() ||
      !Method->isTrivial() ||
      (!Method->isCopyAssignmentOperator() &&
       !Method->isMoveAssignmentOperator()) ||
      Allocator->Record->getCanonicalDecl() !=
          Source->Record->getCanonicalDecl() ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !S.owns(SM, Reference->getExprLoc()) ||
      !Context.hasSameUnqualifiedType(
          Assignment->getArg(0)->getType(),
          Context.getRecordType(Allocator->Record)) ||
      !Context.hasSameUnqualifiedType(Assignment->getArg(1)->getType(),
                                      Context.getRecordType(Source->Record)) ||
      !Method->getReturnType()->isLValueReferenceType() ||
      !Context.hasSameUnqualifiedType(Method->getReturnType()->getPointeeType(),
                                      Context.getRecordType(Allocator->Record)))
    return false;
  const auto Parameter = Method->getParamDecl(0)->getType();
  return Parameter->isReferenceType() &&
         Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                        Context.getRecordType(Source->Record));
}

static bool utilityScalar(const ASTContext &Context, QualType Type) {
  if (Type.isNull() || Type->isReferenceType())
    return false;
  Type = Type.getUnqualifiedType();
  if (Type->isIntegralOrEnumerationType())
    return Context.getTypeSize(Type) <= 64;
  return Type->isSpecificBuiltinType(BuiltinType::Float) ||
         Type->isSpecificBuiltinType(BuiltinType::Double) ||
         (Type->isPointerType() && !Type->isFunctionPointerType()) ||
         Type->isNullPtrType();
}

std::optional<QualType> utilityScalarComparisonType(const ASTContext &Context,
                                                    QualType Left,
                                                    QualType Right,
                                                    bool RequireOrderedObject) {
  if (!utilityScalar(Context, Left) || !utilityScalar(Context, Right))
    return std::nullopt;
  Left = Left.getCanonicalType().getUnqualifiedType();
  Right = Right.getCanonicalType().getUnqualifiedType();
  if (Context.hasSameType(Left, Right)) {
    if (Left->isNullPtrType() && RequireOrderedObject)
      return std::nullopt;
    if (Left->isPointerType() && RequireOrderedObject) {
      const auto Pointee = Left->getPointeeType();
      if (!Pointee->isObjectType() || Pointee->isIncompleteType())
        return std::nullopt;
    }
    if (Context.isPromotableIntegerType(Left))
      Left = Context.getPromotedIntegerType(Left);
    return Left;
  }

  if (Left->isNullPtrType() || Right->isNullPtrType()) {
    if (RequireOrderedObject)
      return std::nullopt;
    const auto Pointer = Left->isPointerType() ? Left : Right;
    return Pointer->isPointerType() ? std::optional<QualType>(Pointer)
                                    : std::nullopt;
  }

  if (Left->isPointerType() || Right->isPointerType()) {
    if (!Left->isPointerType() || !Right->isPointerType())
      return std::nullopt;
    auto LeftPointee = Left->getPointeeType();
    auto RightPointee = Right->getPointeeType();
    auto SupportedPointee = [](QualType Type) {
      return !Type.isNull() && !Type.isVolatileQualified() &&
             !Type.isRestrictQualified() &&
             Type.getAddressSpace() == LangAS::Default &&
             (Type->isObjectType() || Type->isVoidType());
    };
    if (!SupportedPointee(LeftPointee) || !SupportedPointee(RightPointee))
      return std::nullopt;
    QualType Pointee;
    if (Context.hasSameUnqualifiedType(LeftPointee, RightPointee))
      Pointee = LeftPointee.getUnqualifiedType();
    else if (LeftPointee->isVoidType() && RightPointee->isObjectType())
      Pointee = Context.VoidTy;
    else if (RightPointee->isVoidType() && LeftPointee->isObjectType())
      Pointee = Context.VoidTy;
    else
      return std::nullopt;
    if (RequireOrderedObject &&
        (!Pointee->isObjectType() || Pointee->isIncompleteType()))
      return std::nullopt;
    if (LeftPointee.isConstQualified() || RightPointee.isConstQualified())
      Pointee = Pointee.withConst();
    return Context.getPointerType(Pointee);
  }

  // Distinct nullptr_t and enumerations need separate source rules. Keep those
  // outside the arithmetic surface.
  if (Left->isEnumeralType() || Right->isEnumeralType() ||
      !Left->isArithmeticType() || !Right->isArithmeticType())
    return std::nullopt;
  if (Left->isRealFloatingType() || Right->isRealFloatingType()) {
    if (Left->isSpecificBuiltinType(BuiltinType::Double) ||
        Right->isSpecificBuiltinType(BuiltinType::Double))
      return Context.DoubleTy;
    if (Left->isSpecificBuiltinType(BuiltinType::Float) ||
        Right->isSpecificBuiltinType(BuiltinType::Float))
      return Context.FloatTy;
    return std::nullopt;
  }
  if (!Left->isIntegralType(Context) || !Right->isIntegralType(Context))
    return std::nullopt;
  if (Context.isPromotableIntegerType(Left))
    Left = Context.getPromotedIntegerType(Left);
  if (Context.isPromotableIntegerType(Right))
    Right = Context.getPromotedIntegerType(Right);
  if (Context.hasSameType(Left, Right))
    return Left;

  const bool LeftUnsigned = Left->isUnsignedIntegerType();
  const bool RightUnsigned = Right->isUnsignedIntegerType();
  const int Order = Context.getIntegerTypeOrder(Left, Right);
  if (LeftUnsigned == RightUnsigned)
    return Order >= 0 ? Left : Right;

  const QualType Unsigned = LeftUnsigned ? Left : Right;
  const QualType Signed = LeftUnsigned ? Right : Left;
  const bool SignedOnLeft = !LeftUnsigned;
  if (Order != (SignedOnLeft ? 1 : -1))
    return Unsigned;
  if (Context.getIntWidth(Signed) != Context.getIntWidth(Unsigned))
    return Signed;
  return Context.getCorrespondingUnsignedType(Signed);
}

static bool utilityScalarDirectConversion(const ASTContext &Context,
                                          QualType From, QualType To) {
  if (!utilityScalar(Context, From) || !utilityScalar(Context, To))
    return false;
  From = From.getCanonicalType().getUnqualifiedType();
  To = To.getCanonicalType().getUnqualifiedType();
  if (Context.hasSameType(From, To))
    return true;
  if (From->isNullPtrType())
    return To->isBooleanType() || To->isPointerType();
  if (To->isNullPtrType())
    return false;
  if (From->isPointerType() || To->isPointerType()) {
    if (From->isPointerType() && To->isBooleanType())
      return true;
    if (!From->isPointerType() || !To->isPointerType())
      return false;
    const auto FromPointee = From->getPointeeType();
    const auto ToPointee = To->getPointeeType();
    return FromPointee->isVoidType() || ToPointee->isVoidType() ||
           Context.hasSameUnqualifiedType(FromPointee, ToPointee);
  }
  return true;
}

static bool utilityArrayValue(const State &S, const SourceManager &SM,
                              const ASTContext &Context, QualType Type) {
  if (utilityScalar(Context, Type))
    return true;
  Type = Type.getUnqualifiedType();
  const auto *Record = Type->getAsCXXRecordDecl();
  if (!Record)
    return false;
  if (approvedUtilityArrayMetadata(S, SM, Record))
    return approvedUtilityArrayRecord(S, SM, Record, Context).has_value();
  Record = Record->getDefinition();
  return Record && S.owns(SM, Record->getLocation()) && !Record->isUnion() &&
         Record->isStandardLayout() && Record->isTrivial() &&
         Record->hasTrivialDestructor();
}

static bool utilityArrayTriviallyAssignable(const ASTContext &Context,
                                            QualType Type) {
  if (Type.isConstQualified())
    return false;
  if (utilityScalar(Context, Type))
    return true;
  const auto *Record = Type->getAsCXXRecordDecl();
  Record = Record ? Record->getDefinition() : nullptr;
  return Record && Record->hasTrivialCopyAssignment() &&
         Record->hasTrivialDestructor();
}

static bool utilityPairValue(const State &S, const SourceManager &SM,
                             const ASTContext &Context, QualType Type,
                             unsigned Depth = 0);

bool approvedUtilityPairMetadata(const State &S, const SourceManager &SM,
                                 const CXXRecordDecl *Record) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  const auto *Template =
      Specialization ? Specialization->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  if (!Specialization || !Template || !CanonicalTemplate ||
      Specialization->isUnion() || Specialization->isDependentContext() ||
      Specialization->getName() != "pair" ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                     "__utility/pair.h") ||
      !cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                     "__fwd/pair.h"))
    return false;
  const auto &Arguments = Specialization->getTemplateArgs();
  return Arguments.size() == 2 &&
         Arguments.get(0).getKind() == TemplateArgument::Type &&
         Arguments.get(1).getKind() == TemplateArgument::Type;
}

static std::optional<UtilityPairRecord> approvedUtilityPairRecordImpl(
    const State &S, const SourceManager &SM, const CXXRecordDecl *Record,
    const ASTContext &Context, bool RequireReferenceElements,
    bool RequireMixedReferenceElements = false) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  Specialization = Specialization
                       ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                             Specialization->getDefinition())
                       : nullptr;
  const auto *Template =
      Specialization ? Specialization->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate = Template ? Template->getCanonicalDecl() : nullptr;
  if (!approvedUtilityPairMetadata(S, SM, Specialization) || !Specialization ||
      !Template || Specialization->isUnion() ||
      Specialization->isDependentContext() ||
      Specialization->getSpecializationKind() != TSK_ImplicitInstantiation ||
      Specialization->getName() != "pair" || Specialization->getNumBases() ||
      !approvedStandardSDKDeclaration(S, SM, Specialization) ||
      !approvedStandardSDKDeclaration(S, SM, Template) || !CanonicalTemplate ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !cstddefOrigin(S, SM, Specialization->getLocation(), "libcxx",
                     "__utility/pair.h") ||
      !cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                     "__utility/pair.h") ||
      !cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                     "__fwd/pair.h"))
    return std::nullopt;
  const auto &Arguments = Specialization->getTemplateArgs();
  if (Arguments.size() != 2 ||
      Arguments.get(0).getKind() != TemplateArgument::Type ||
      Arguments.get(1).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto FirstType = Arguments.get(0).getAsType();
  const auto SecondType = Arguments.get(1).getAsType();
  if (RequireMixedReferenceElements) {
    if (FirstType->isReferenceType() == SecondType->isReferenceType())
      return std::nullopt;
    for (const auto Type : {FirstType, SecondType}) {
      if (Type->isReferenceType()) {
        if (!supportedFunctionalReferenceValue(S, SM, Context,
                                               Type->getPointeeType()))
          return std::nullopt;
      } else if (!utilityPairValue(S, SM, Context, Type)) {
        return std::nullopt;
      }
    }
  } else if (RequireReferenceElements) {
    if (!FirstType->isReferenceType() || !SecondType->isReferenceType() ||
        !supportedFunctionalReferenceValue(S, SM, Context,
                                           FirstType->getPointeeType()) ||
        !supportedFunctionalReferenceValue(S, SM, Context,
                                           SecondType->getPointeeType()))
      return std::nullopt;
  } else if (!Specialization->isStandardLayout()) {
    return std::nullopt;
  }
  auto Fields = Specialization->fields();
  auto It = Fields.begin();
  const auto *First = It == Fields.end() ? nullptr : *It++;
  const auto *Second = It == Fields.end() ? nullptr : *It++;
  if (!First || !Second || It != Fields.end() || First->getName() != "first" ||
      Second->getName() != "second" || First->getAccess() != AS_public ||
      Second->getAccess() != AS_public || First->isBitField() ||
      Second->isBitField() || First->isMutable() || Second->isMutable() ||
      First->hasAttrs() || Second->hasAttrs() ||
      !approvedStandardSDKDeclaration(S, SM, First) ||
      !approvedStandardSDKDeclaration(S, SM, Second) ||
      !cstddefOrigin(S, SM, First->getLocation(), "libcxx",
                     "__utility/pair.h") ||
      !cstddefOrigin(S, SM, Second->getLocation(), "libcxx",
                     "__utility/pair.h") ||
      !Context.hasSameType(First->getType(), FirstType) ||
      !Context.hasSameType(Second->getType(), SecondType))
    return std::nullopt;
  return UtilityPairRecord{Specialization, First, Second};
}

std::optional<UtilityPairRecord>
approvedUtilityPairRecord(const State &S, const SourceManager &SM,
                          const CXXRecordDecl *Record,
                          const ASTContext &Context) {
  return approvedUtilityPairRecordImpl(S, SM, Record, Context, false);
}

std::optional<UtilityPairRecord>
approvedUtilityReferencePairRecord(const State &S, const SourceManager &SM,
                                   const CXXRecordDecl *Record,
                                   const ASTContext &Context) {
  return approvedUtilityPairRecordImpl(S, SM, Record, Context, true);
}

std::optional<UtilityPairRecord> approvedUtilityMixedReferencePairRecord(
    const State &S, const SourceManager &SM, const CXXRecordDecl *Record,
    const ASTContext &Context) {
  return approvedUtilityPairRecordImpl(S, SM, Record, Context, false, true);
}

static bool utilityPairValue(const State &S, const SourceManager &SM,
                             const ASTContext &Context, QualType Type,
                             unsigned Depth) {
  if (Depth > 64 || Type.isNull())
    return false;
  if (utilityArrayValue(S, SM, Context, Type))
    return true;
  const auto *Record =
      Type.getUnqualifiedType()->getAsCXXRecordDecl();
  if (const auto Wrapper = approvedFunctionalReferenceRecord(S, SM, Record, Context))
    return !Type.isVolatileQualified() && !Type.isRestrictQualified() &&
           Type.getAddressSpace() == LangAS::Default &&
           Wrapper->Record->hasTrivialCopyConstructor();
  const auto Pair = approvedUtilityPairRecord(S, SM, Record, Context);
  return Pair &&
         utilityPairValue(S, SM, Context, Pair->First->getType(), Depth + 1) &&
         utilityPairValue(S, SM, Context, Pair->Second->getType(), Depth + 1);
}

static bool utilityPairAssignableValue(const State &S,
                                       const SourceManager &SM,
                                       const ASTContext &Context,
                                       QualType Type, unsigned Depth = 0) {
  if (Depth > 64 || Type.isNull() || Type.isConstQualified())
    return false;
  if (utilityArrayValue(S, SM, Context, Type))
    return utilityArrayTriviallyAssignable(Context, Type);
  const auto *Record =
      Type.getUnqualifiedType()->getAsCXXRecordDecl();
  if (const auto Wrapper = approvedFunctionalReferenceRecord(S, SM, Record, Context))
    return !Type.isVolatileQualified() && !Type.isRestrictQualified() &&
           Type.getAddressSpace() == LangAS::Default &&
           Wrapper->Record->hasTrivialCopyAssignment();
  const auto Pair = approvedUtilityPairRecord(S, SM, Record, Context);
  return Pair && utilityPairAssignableValue(
                     S, SM, Context, Pair->First->getType(), Depth + 1) &&
         utilityPairAssignableValue(S, SM, Context, Pair->Second->getType(),
                                    Depth + 1);
}

std::optional<UtilityPairConstruction>
approvedUtilityPairConstruction(const State &S, const SourceManager &SM,
                                const CXXConstructExpr *Construction,
                                const ASTContext &Context) {
  if (!Construction || Construction->isTypeDependent() ||
      Construction->isValueDependent() ||
      Construction->isInstantiationDependent() ||
      Construction->getConstructionKind() !=
          CXXConstructionKind::Complete)
    return std::nullopt;
  const auto *Constructor = Construction->getConstructor();
  auto Pair = approvedUtilityPairRecord(
      S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
  bool ReferencePair = false;
  if (!Pair) {
    Pair = approvedUtilityReferencePairRecord(
        S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
    ReferencePair = Pair.has_value();
  }
  bool MixedReferencePair = false;
  if (!Pair) {
    Pair = approvedUtilityMixedReferencePairRecord(
        S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
    MixedReferencePair = Pair.has_value();
  }
  if (!Constructor || !Pair || Constructor->isVariadic() ||
      Constructor->getParent()->getCanonicalDecl() !=
          Pair->Record->getCanonicalDecl() ||
      Construction->getNumArgs() != Constructor->getNumParams() ||
      !approvedStandardSDKDeclaration(S, SM, Constructor) ||
      !cstddefOrigin(S, SM, Constructor->getLocation(), "libcxx",
                     "__utility/pair.h") ||
      (!ReferencePair && !MixedReferencePair &&
       (!utilityPairValue(S, SM, Context, Pair->First->getType()) ||
        !utilityPairValue(S, SM, Context, Pair->Second->getType()))))
    return std::nullopt;
  if (!Construction->getNumArgs() && Constructor->isDefaultConstructor() &&
      !ReferencePair && !MixedReferencePair)
    return UtilityPairConstruction::Default;
  if (Construction->getNumArgs() == 1 &&
      Constructor->isCopyOrMoveConstructor() && Constructor->isDefaulted() &&
      Constructor->isTrivial())
    return UtilityPairConstruction::CopyOrMove;
  const auto *Primary = Constructor->getPrimaryTemplate();
  auto SourcePair =
      Construction->getNumArgs() == 1
          ? approvedUtilityPairRecord(
                S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(),
                Context)
          : std::optional<UtilityPairRecord>();
  if (Construction->getNumArgs() == 1 && !SourcePair) {
    SourcePair = approvedUtilityReferencePairRecord(
        S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(),
        Context);
  }
  if (Construction->getNumArgs() == 1 && !SourcePair)
    SourcePair = approvedUtilityMixedReferencePairRecord(
        S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(),
        Context);
  if (SourcePair && Primary && Constructor->hasBody() &&
      SourcePair->Record->getCanonicalDecl() !=
          Pair->Record->getCanonicalDecl() &&
      approvedStandardSDKDeclaration(S, SM, Primary) &&
      cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__utility/pair.h")) {
    const auto Parameter = Constructor->getParamDecl(0)->getType();
    if (!Parameter->isReferenceType() ||
        Parameter->getPointeeType().isVolatileQualified() ||
        !Context.hasSameUnqualifiedType(
            Parameter->getPointeeType(),
            Context.getRecordType(SourcePair->Record)))
      return std::nullopt;
    for (unsigned I = 0; I != 2; ++I) {
      const auto Destination =
          I ? Pair->Second->getType() : Pair->First->getType();
      const auto SourceElement =
          I ? SourcePair->Second->getType() : SourcePair->First->getType();
      const bool SourceReference = SourceElement->isReferenceType();
      auto SourceValue = SourceReference ? SourceElement->getPointeeType()
                                         : SourceElement;
      if (!SourceReference &&
          Parameter->getPointeeType().isConstQualified())
        SourceValue = SourceValue.withConst();
      const bool SourceRValue =
          Parameter->isRValueReferenceType() &&
          (!SourceReference || SourceElement->isRValueReferenceType());
      if (Destination->isReferenceType()) {
        if (!Context.hasSameUnqualifiedType(Destination->getPointeeType(),
                                            SourceValue) ||
            !Destination->getPointeeType().isAtLeastAsQualifiedAs(SourceValue,
                                                                  Context) ||
            (Destination->isRValueReferenceType() && !SourceRValue) ||
            (Destination->isLValueReferenceType() &&
             !Destination->getPointeeType().isConstQualified() && SourceRValue))
          return std::nullopt;
        continue;
      }
      const bool Convertible =
          (utilityScalar(Context, SourceValue) ||
           utilityScalar(Context, Destination))
              ? utilityScalarDirectConversion(Context, SourceValue,
                                              Destination)
              : Context.hasSameUnqualifiedType(SourceValue, Destination);
      if (!Convertible)
        return std::nullopt;
    }
    return UtilityPairConstruction::Converting;
  }
  if (Construction->getNumArgs() == 2 && Primary &&
      approvedStandardSDKDeclaration(S, SM, Primary) &&
      cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__utility/pair.h")) {
    for (unsigned I = 0; I != 2; ++I) {
      const auto Parameter = Constructor->getParamDecl(I)->getType();
      const auto Element =
          I ? Pair->Second->getType() : Pair->First->getType();
      const auto Argument = Construction->getArg(I)->getType();
      if (!Parameter->isReferenceType() ||
          !Context.hasSameUnqualifiedType(Argument,
                                          Parameter->getPointeeType()))
        return std::nullopt;
      if (Element->isReferenceType()) {
        if (!Context.hasSameUnqualifiedType(Argument,
                                            Element->getPointeeType()) ||
            !Element->getPointeeType().isAtLeastAsQualifiedAs(Argument,
                                                              Context) ||
            (Element->isRValueReferenceType() &&
             Construction->getArg(I)->isLValue()) ||
            (Element->isLValueReferenceType() &&
             !Element->getPointeeType().isConstQualified() &&
             !Construction->getArg(I)->isLValue()))
          return std::nullopt;
        continue;
      }
      const bool Convertible =
          (utilityScalar(Context, Argument) || utilityScalar(Context, Element))
              ? utilityScalarDirectConversion(Context, Argument, Element)
              : Context.hasSameUnqualifiedType(Argument, Element);
      if (!Convertible)
        return std::nullopt;
    }
    return UtilityPairConstruction::Elements;
  }
  return std::nullopt;
}

std::optional<UtilityPairRecord>
approvedUtilityPairAssignment(const State &S, const SourceManager &SM,
                              const CXXOperatorCallExpr *Assignment,
                              const ASTContext &Context) {
  if (!Assignment || Assignment->isTypeDependent() ||
      Assignment->isValueDependent() ||
      Assignment->isInstantiationDependent() ||
      Assignment->getOperator() != OO_Equal ||
      Assignment->getNumArgs() != 2 || !Assignment->isLValue())
    return std::nullopt;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Assignment->getDirectCallee());
  auto Pair = approvedUtilityPairRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  bool ReferencePair = false;
  if (!Pair) {
    Pair = approvedUtilityReferencePairRecord(
        S, SM, Method ? Method->getParent() : nullptr, Context);
    ReferencePair = Pair.has_value();
  }
  bool MixedReferencePair = false;
  if (!Pair) {
    Pair = approvedUtilityMixedReferencePairRecord(
        S, SM, Method ? Method->getParent() : nullptr, Context);
    MixedReferencePair = Pair.has_value();
  }
  auto FirstType = Pair ? Pair->First->getType() : QualType();
  auto SecondType = Pair ? Pair->Second->getType() : QualType();
  if (!FirstType.isNull() && FirstType->isReferenceType())
    FirstType = FirstType->getPointeeType();
  if (!SecondType.isNull() && SecondType->isReferenceType())
    SecondType = SecondType->getPointeeType();
  if (!Method || !Pair || Method->isStatic() || Method->isVariadic() ||
      Method->getNumParams() != 1 ||
      Method->getOverloadedOperator() != OO_Equal || !Method->hasBody() ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__utility/pair.h") ||
      (!ReferencePair && !MixedReferencePair &&
       (!utilityPairValue(S, SM, Context, FirstType) ||
        !utilityPairValue(S, SM, Context, SecondType))) ||
      !utilityPairAssignableValue(S, SM, Context, FirstType) ||
      !utilityPairAssignableValue(S, SM, Context, SecondType))
    return std::nullopt;
  const auto Parameter = Method->getParamDecl(0)->getType();
  const auto Result = Method->getReturnType();
  const auto PairType = Context.getRecordType(Pair->Record);
  if (!Parameter->isReferenceType() || !Result->isLValueReferenceType() ||
      !Context.hasSameUnqualifiedType(Result->getPointeeType(), PairType) ||
      !Context.hasSameUnqualifiedType(Assignment->getArg(0)->getType(),
                                      PairType) ||
      !Context.hasSameUnqualifiedType(Assignment->getType(), PairType))
    return std::nullopt;
  if (Context.hasSameUnqualifiedType(Parameter->getPointeeType(), PairType) &&
      Context.hasSameUnqualifiedType(Assignment->getArg(1)->getType(),
                                     PairType))
    return Pair;
  if (Parameter->getPointeeType().isVolatileQualified())
    return std::nullopt;
  auto SourcePair = approvedUtilityPairRecord(
      S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  if (!SourcePair) {
    SourcePair = approvedUtilityReferencePairRecord(
        S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  }
  if (!SourcePair)
    SourcePair = approvedUtilityMixedReferencePairRecord(
        S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  const auto *Primary = Method->getPrimaryTemplate();
  if (!SourcePair || !Primary ||
      !Context.hasSameUnqualifiedType(
          Parameter->getPointeeType(),
          Context.getRecordType(SourcePair->Record)) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__utility/pair.h"))
    return std::nullopt;
  for (unsigned I = 0; I != 2; ++I) {
    const auto SourceElement =
        I ? SourcePair->Second->getType() : SourcePair->First->getType();
    const auto SourceValue = SourceElement->isReferenceType()
                                 ? SourceElement->getPointeeType()
                                 : SourceElement;
    const auto DestinationElement =
        I ? Pair->Second->getType() : Pair->First->getType();
    const auto DestinationValue = DestinationElement->isReferenceType()
                                      ? DestinationElement->getPointeeType()
                                      : DestinationElement;
    const bool Convertible =
        (utilityScalar(Context, SourceValue) ||
         utilityScalar(Context, DestinationValue))
            ? utilityScalarDirectConversion(Context, SourceValue,
                                            DestinationValue)
            : Context.hasSameUnqualifiedType(SourceValue, DestinationValue);
    if (!Convertible)
      return std::nullopt;
  }
  return Pair;
}

static std::optional<std::vector<QualType>>
utilityTupleTypes(const ClassTemplateSpecializationDecl *Specialization) {
  if (!Specialization)
    return std::nullopt;
  const auto &Arguments = Specialization->getTemplateArgs();
  if (Arguments.size() != 1 ||
      Arguments.get(0).getKind() != TemplateArgument::Pack ||
      Arguments.get(0).pack_size() > 64)
    return std::nullopt;
  std::vector<QualType> Types;
  Types.reserve(Arguments.get(0).pack_size());
  for (const auto &Argument : Arguments.get(0).pack_elements()) {
    if (Argument.getKind() != TemplateArgument::Type ||
        Argument.getAsType().isNull())
      return std::nullopt;
    Types.push_back(Argument.getAsType());
  }
  return Types;
}

static std::optional<UtilityTupleRecord>
approvedUtilityTupleRecordImpl(const State &S, const SourceManager &SM,
                               const CXXRecordDecl *Record,
                               const ASTContext &Context, unsigned Depth,
                               bool RequireReferenceElements = false,
                               bool RequireMixedReferenceElements = false);

static bool utilityTupleValue(const State &S, const SourceManager &SM,
                              const ASTContext &Context, QualType Type,
                              unsigned Depth = 0) {
  if (Depth > 64 || Type.isNull())
    return false;
  if (utilityPairValue(S, SM, Context, Type, Depth))
    return true;
  const auto *Record = Type.getUnqualifiedType()->getAsCXXRecordDecl();
  if (approvedFunctionalReferenceRecord(S, SM, Record, Context))
    return true;
  return approvedUtilityTupleRecordImpl(S, SM, Record, Context, Depth, false,
                                        false)
      .has_value();
}

static bool utilityTupleAssignableValue(const State &S, const SourceManager &SM,
                                        const ASTContext &Context,
                                        QualType Type, unsigned Depth = 0) {
  if (Depth > 64 || Type.isNull() || Type.isConstQualified())
    return false;
  if (utilityPairAssignableValue(S, SM, Context, Type, Depth))
    return true;
  const auto *Record = Type.getUnqualifiedType()->getAsCXXRecordDecl();
  if (approvedFunctionalReferenceRecord(S, SM, Record, Context)) {
    const auto *Definition = Record ? Record->getDefinition() : nullptr;
    return Definition && Definition->hasTrivialCopyAssignment();
  }
  const auto Tuple =
      approvedUtilityTupleRecordImpl(S, SM, Record, Context, Depth, false,
                                     false);
  if (!Tuple)
    return false;
  for (const auto *Element : Tuple->Elements)
    if (!utilityTupleAssignableValue(S, SM, Context, Element->getType(),
                                     Depth + 1))
      return false;
  return true;
}

static bool utilityTupleDirectConversion(const State &S,
                                         const SourceManager &SM,
                                         const ASTContext &Context,
                                         QualType From, QualType To) {
  if (utilityScalar(Context, From) || utilityScalar(Context, To))
    return utilityScalarDirectConversion(Context, From, To);
  return utilityTupleValue(S, SM, Context, From) &&
         utilityTupleValue(S, SM, Context, To) &&
         Context.hasSameUnqualifiedType(From, To);
}

bool approvedUtilityTupleMetadata(const State &S, const SourceManager &SM,
                                  const CXXRecordDecl *Record) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  const auto *Template =
      Specialization ? Specialization->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  return Specialization && Template && CanonicalTemplate &&
         !Specialization->isUnion() && !Specialization->isDependentContext() &&
         Specialization->getName() == "tuple" &&
         utilityTupleTypes(Specialization).has_value() &&
         approvedStandardSDKDeclaration(S, SM, Specialization) &&
         approvedStandardSDKDeclaration(S, SM, Template) &&
         approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) &&
         cstddefOrigin(S, SM, Specialization->getLocation(), "libcxx",
                       "tuple") &&
         cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                       "__fwd/tuple.h");
}

static std::optional<UtilityTupleRecord>
approvedUtilityTupleRecordImpl(const State &S, const SourceManager &SM,
                               const CXXRecordDecl *Record,
                               const ASTContext &Context, unsigned Depth,
                               bool RequireReferenceElements,
                               bool RequireMixedReferenceElements) {
  if (Depth > 64)
    return std::nullopt;
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  Specialization = Specialization
                       ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                             Specialization->getDefinition())
                       : nullptr;
  const auto Types = utilityTupleTypes(Specialization);
  if (!approvedUtilityTupleMetadata(S, SM, Specialization) || !Types ||
      !Specialization)
    return std::nullopt;
  const bool Empty = Types->empty();
  if ((Empty ? Specialization->getSpecializationKind() !=
                   TSK_ExplicitSpecialization
             : Specialization->getSpecializationKind() !=
                   TSK_ImplicitInstantiation) ||
      Specialization->getNumBases() || Specialization->getNumVBases() ||
      Specialization->isDynamicClass() ||
      !Specialization->hasTrivialCopyConstructor() ||
      !Specialization->hasTrivialDestructor() ||
      !approvedStandardSDKDeclaration(S, SM, Specialization) ||
      !cstddefOrigin(S, SM, Specialization->getLocation(), "libcxx", "tuple"))
    return std::nullopt;
  unsigned ReferenceElements = 0;
  for (QualType Type : *Types) {
    if (Type->isReferenceType()) {
      ++ReferenceElements;
      if ((!RequireReferenceElements && !RequireMixedReferenceElements) ||
          !supportedFunctionalReferenceValue(S, SM, Context,
                                             Type->getPointeeType()))
        return std::nullopt;
    } else if (RequireReferenceElements ||
               !utilityTupleValue(S, SM, Context, Type, Depth + 1) ||
               Type.isVolatileQualified() || Type.isRestrictQualified() ||
               Type.getAddressSpace() != LangAS::Default) {
      return std::nullopt;
    }
  }
  if (RequireMixedReferenceElements &&
      (!ReferenceElements || ReferenceElements == Types->size()))
    return std::nullopt;

  if (Empty) {
    const auto &Layout = Context.getASTRecordLayout(Specialization);
    if (!Specialization->field_empty() || !Specialization->isEmpty() ||
        !Specialization->isStandardLayout() || Layout.getFieldCount() ||
        Layout.getSize().getQuantity() != 1 ||
        Layout.getAlignment().getQuantity() != 1)
      return std::nullopt;
    return UtilityTupleRecord{Specialization, {}, {}};
  }

  auto Fields = Specialization->fields();
  auto Field = Fields.begin();
  const auto *BaseField = Field == Fields.end() ? nullptr : *Field++;
  const auto *Impl =
      BaseField ? BaseField->getType()->getAsCXXRecordDecl() : nullptr;
  Impl = Impl ? Impl->getDefinition() : nullptr;
  const auto *ImplSpecialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Impl);
  if (!BaseField || Field != Fields.end() ||
      BaseField->getName() != "__base_" ||
      BaseField->getAccess() != AS_private || BaseField->isBitField() ||
      BaseField->isMutable() || BaseField->hasAttrs() ||
      !approvedStandardSDKDeclaration(S, SM, BaseField) ||
      !cstddefOrigin(S, SM, BaseField->getLocation(), "libcxx", "tuple") ||
      !ImplSpecialization || ImplSpecialization->getName() != "__tuple_impl" ||
      ImplSpecialization->isUnion() ||
      ImplSpecialization->isDependentContext() ||
      ImplSpecialization->getNumVBases() ||
      ImplSpecialization->isDynamicClass() ||
      !ImplSpecialization->field_empty() ||
      ImplSpecialization->getNumBases() != Types->size() ||
      !approvedStandardSDKDeclaration(S, SM, ImplSpecialization) ||
      !cstddefOrigin(S, SM, ImplSpecialization->getLocation(), "libcxx",
                     "tuple"))
    return std::nullopt;

  const auto &ImplArguments = ImplSpecialization->getTemplateArgs();
  if (ImplArguments.size() != 2 ||
      ImplArguments.get(0).getKind() != TemplateArgument::Type ||
      ImplArguments.get(1).getKind() != TemplateArgument::Pack ||
      ImplArguments.get(1).pack_size() != Types->size())
    return std::nullopt;
  unsigned TypeIndex = 0;
  for (const auto &Argument : ImplArguments.get(1).pack_elements())
    if (Argument.getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Argument.getAsType(), (*Types)[TypeIndex++]))
      return std::nullopt;

  const auto &TopLayout = Context.getASTRecordLayout(Specialization);
  const auto &ImplLayout = Context.getASTRecordLayout(ImplSpecialization);
  if (TopLayout.getFieldCount() != 1 || TopLayout.getFieldOffset(0) != 0 ||
      TopLayout.getSize() != ImplLayout.getSize() ||
      TopLayout.getAlignment() != ImplLayout.getAlignment())
    return std::nullopt;

  std::vector<const FieldDecl *> Elements;
  std::vector<uint64_t> Offsets;
  Elements.reserve(Types->size());
  Offsets.reserve(Types->size());
  unsigned Index = 0;
  for (const auto &Base : ImplSpecialization->bases()) {
    const auto *Leaf = Base.getType()->getAsCXXRecordDecl();
    Leaf = Leaf ? Leaf->getDefinition() : nullptr;
    const auto *LeafSpecialization =
        dyn_cast_or_null<ClassTemplateSpecializationDecl>(Leaf);
    if (!LeafSpecialization || Base.isVirtual() ||
        Base.getAccessSpecifier() != AS_public ||
        LeafSpecialization->getName() != "__tuple_leaf" ||
        LeafSpecialization->isUnion() ||
        LeafSpecialization->isDependentContext() ||
        LeafSpecialization->getNumBases() ||
        LeafSpecialization->getNumVBases() ||
        LeafSpecialization->isDynamicClass() ||
        !approvedStandardSDKDeclaration(S, SM, LeafSpecialization) ||
        !cstddefOrigin(S, SM, LeafSpecialization->getLocation(), "libcxx",
                       "tuple"))
      return std::nullopt;
    const auto &LeafArguments = LeafSpecialization->getTemplateArgs();
    if (LeafArguments.size() != 3 ||
        LeafArguments.get(0).getKind() != TemplateArgument::Integral ||
        LeafArguments.get(0).getAsIntegral().isNegative() ||
        LeafArguments.get(0).getAsIntegral().getLimitedValue(65) != Index ||
        LeafArguments.get(1).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(LeafArguments.get(1).getAsType(),
                             (*Types)[Index]) ||
        LeafArguments.get(2).getKind() != TemplateArgument::Integral ||
        !LeafArguments.get(2).getIntegralType()->isBooleanType() ||
        !LeafArguments.get(2).getAsIntegral().isZero())
      return std::nullopt;
    const auto *ElementRecord =
        (*Types)[Index].getUnqualifiedType()->getAsCXXRecordDecl();
    // A leaf containing another tuple inherits its member's non-standard-layout
    // classification. The concrete field, size, alignment and offset checks
    // below still authenticate that leaf.
    if (!LeafSpecialization->isStandardLayout() &&
        !RequireReferenceElements && !RequireMixedReferenceElements &&
        !approvedUtilityTupleMetadata(S, SM, ElementRecord))
      return std::nullopt;
    auto LeafFields = LeafSpecialization->fields();
    auto LeafField = LeafFields.begin();
    const auto *Value = LeafField == LeafFields.end() ? nullptr : *LeafField++;
    if (!Value || LeafField != LeafFields.end() ||
        Value->getName() != "__value_" || Value->getAccess() != AS_private ||
        Value->isBitField() || Value->isMutable() || Value->hasAttrs() ||
        !Context.hasSameType(Value->getType(), (*Types)[Index]) ||
        !approvedStandardSDKDeclaration(S, SM, Value) ||
        !cstddefOrigin(S, SM, Value->getLocation(), "libcxx", "tuple"))
      return std::nullopt;
    const auto &LeafLayout = Context.getASTRecordLayout(LeafSpecialization);
    const uint64_t ElementBits = Context.getTypeSize((*Types)[Index]);
    const uint64_t ElementAlign = Context.getTypeAlign((*Types)[Index]);
    if (LeafLayout.getFieldCount() != 1 || LeafLayout.getFieldOffset(0) != 0 ||
        uint64_t(LeafLayout.getSize().getQuantity()) * 8 != ElementBits ||
        uint64_t(LeafLayout.getAlignment().getQuantity()) * 8 != ElementAlign)
      return std::nullopt;
    const uint64_t Offset =
        uint64_t(
            ImplLayout.getBaseClassOffset(LeafSpecialization).getQuantity()) *
        8;
    if (Offset + ElementBits > uint64_t(ImplLayout.getSize().getQuantity()) * 8)
      return std::nullopt;
    for (unsigned I = 0; I < Offsets.size(); ++I) {
      const uint64_t ExistingBits = Context.getTypeSize((*Types)[I]);
      if (Offset < Offsets[I] + ExistingBits &&
          Offsets[I] < Offset + ElementBits)
        return std::nullopt;
    }
    Elements.push_back(Value);
    Offsets.push_back(Offset);
    ++Index;
  }
  if (Index != Types->size())
    return std::nullopt;
  return UtilityTupleRecord{Specialization, std::move(Elements),
                            std::move(Offsets)};
}

std::optional<UtilityTupleRecord>
approvedUtilityTupleRecord(const State &S, const SourceManager &SM,
                           const CXXRecordDecl *Record,
                           const ASTContext &Context) {
  return approvedUtilityTupleRecordImpl(S, SM, Record, Context, 0, false,
                                        false);
}

std::optional<UtilityTupleRecord>
approvedUtilityReferenceTupleRecord(const State &S, const SourceManager &SM,
                                    const CXXRecordDecl *Record,
                                    const ASTContext &Context) {
  return approvedUtilityTupleRecordImpl(S, SM, Record, Context, 0, true,
                                        false);
}

std::optional<UtilityTupleRecord> approvedUtilityMixedReferenceTupleRecord(
    const State &S, const SourceManager &SM, const CXXRecordDecl *Record,
    const ASTContext &Context) {
  return approvedUtilityTupleRecordImpl(S, SM, Record, Context, 0, false, true);
}

std::optional<UtilityTupleConstruction>
approvedUtilityTupleConstruction(const State &S, const SourceManager &SM,
                                 const CXXConstructExpr *Construction,
                                 const ASTContext &Context) {
  if (!Construction || Construction->isTypeDependent() ||
      Construction->isValueDependent() ||
      Construction->isInstantiationDependent() ||
      Construction->getConstructionKind() != CXXConstructionKind::Complete)
    return std::nullopt;
  const auto *Constructor = Construction->getConstructor();
  auto Tuple = approvedUtilityTupleRecord(
      S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
  bool ReferenceTuple = false;
  if (!Tuple) {
    Tuple = approvedUtilityReferenceTupleRecord(
        S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
    ReferenceTuple = Tuple.has_value();
  }
  bool MixedReferenceTuple = false;
  if (!Tuple) {
    Tuple = approvedUtilityMixedReferenceTupleRecord(
        S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
    MixedReferenceTuple = Tuple.has_value();
  }
  if (!Constructor || !Tuple || Constructor->isVariadic() ||
      Constructor->getParent()->getCanonicalDecl() !=
          Tuple->Record->getCanonicalDecl() ||
      Construction->getNumArgs() != Constructor->getNumParams() ||
      !approvedStandardSDKDeclaration(S, SM, Constructor) ||
      !cstddefOrigin(S, SM, Constructor->getLocation(), "libcxx", "tuple"))
    return std::nullopt;
  if (!Construction->getNumArgs() && Constructor->isDefaultConstructor()) {
    if (ReferenceTuple || MixedReferenceTuple ||
        (Tuple->Elements.empty() &&
         (!Constructor->isDefaulted() || !Constructor->isTrivial())))
      return std::nullopt;
    return UtilityTupleConstruction::Default;
  }
  if (Construction->getNumArgs() == 1 &&
      Constructor->isCopyOrMoveConstructor() && Constructor->isDefaulted() &&
      Constructor->isTrivial() &&
      Context.hasSameUnqualifiedType(Construction->getArg(0)->getType(),
                                     Construction->getType()))
    return UtilityTupleConstruction::CopyOrMove;
  const auto *Primary = Constructor->getPrimaryTemplate();
  auto SourceTuple =
      Construction->getNumArgs() == 1
          ? approvedUtilityTupleRecord(
                S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(),
                Context)
          : std::optional<UtilityTupleRecord>();
  if (Construction->getNumArgs() == 1 && !SourceTuple) {
    SourceTuple = approvedUtilityReferenceTupleRecord(
        S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(),
        Context);
  }
  if (Construction->getNumArgs() == 1 && !SourceTuple)
    SourceTuple = approvedUtilityMixedReferenceTupleRecord(
        S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(),
        Context);
  if (SourceTuple && Primary && Constructor->hasBody() &&
      SourceTuple->Record->getCanonicalDecl() !=
          Tuple->Record->getCanonicalDecl() &&
      SourceTuple->Elements.size() == Tuple->Elements.size() &&
      approvedStandardSDKDeclaration(S, SM, Primary) &&
      cstddefOrigin(S, SM, Primary->getLocation(), "libcxx", "tuple")) {
    const auto Parameter = Constructor->getParamDecl(0)->getType();
    if (!Parameter->isReferenceType() ||
        Parameter->getPointeeType().isVolatileQualified() ||
        !Context.hasSameUnqualifiedType(
            Parameter->getPointeeType(),
            Context.getRecordType(SourceTuple->Record)))
      return std::nullopt;
    for (unsigned I = 0; I < Tuple->Elements.size(); ++I) {
      const auto Destination = Tuple->Elements[I]->getType();
      const auto SourceElement = SourceTuple->Elements[I]->getType();
      const bool SourceReference = SourceElement->isReferenceType();
      auto SourceValue = SourceReference ? SourceElement->getPointeeType()
                                         : SourceElement;
      if (!SourceReference &&
          Parameter->getPointeeType().isConstQualified())
        SourceValue = SourceValue.withConst();
      const bool SourceRValue =
          Parameter->isRValueReferenceType() &&
          (!SourceReference || SourceElement->isRValueReferenceType());
      if (Destination->isReferenceType()) {
        if (!Context.hasSameUnqualifiedType(Destination->getPointeeType(),
                                            SourceValue) ||
            !Destination->getPointeeType().isAtLeastAsQualifiedAs(SourceValue,
                                                                  Context) ||
            (Destination->isRValueReferenceType() && !SourceRValue) ||
            (Destination->isLValueReferenceType() &&
             !Destination->getPointeeType().isConstQualified() &&
             SourceRValue))
          return std::nullopt;
      } else if (!utilityTupleDirectConversion(S, SM, Context, SourceValue,
                                               Destination)) {
        return std::nullopt;
      }
    }
    return UtilityTupleConstruction::Converting;
  }
  auto SourcePair =
      Construction->getNumArgs() == 1
          ? approvedUtilityPairRecord(
                S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(),
                Context)
          : std::optional<UtilityPairRecord>();
  if (Construction->getNumArgs() == 1 && !SourcePair)
    SourcePair = approvedUtilityReferencePairRecord(
        S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(),
        Context);
  if (Construction->getNumArgs() == 1 && !SourcePair)
    SourcePair = approvedUtilityMixedReferencePairRecord(
        S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(),
        Context);
  if (SourcePair && Primary && Constructor->hasBody() &&
      Tuple->Elements.size() == 2 &&
      approvedStandardSDKDeclaration(S, SM, Primary) &&
      cstddefOrigin(S, SM, Primary->getLocation(), "libcxx", "tuple")) {
    const auto Parameter = Constructor->getParamDecl(0)->getType();
    if (!Parameter->isReferenceType() ||
        Parameter->getPointeeType().isVolatileQualified() ||
        !Context.hasSameUnqualifiedType(
            Parameter->getPointeeType(),
            Context.getRecordType(SourcePair->Record)))
      return std::nullopt;
    for (unsigned I = 0; I != 2; ++I) {
      const auto SourceElement =
          I ? SourcePair->Second->getType() : SourcePair->First->getType();
      const auto Destination = Tuple->Elements[I]->getType();
      const bool SourceReference = SourceElement->isReferenceType();
      auto SourceValue = SourceReference ? SourceElement->getPointeeType()
                                         : SourceElement;
      if (!SourceReference && Parameter->getPointeeType().isConstQualified())
        SourceValue = SourceValue.withConst();
      const bool SourceRValue =
          Parameter->isRValueReferenceType() &&
          (!SourceReference || SourceElement->isRValueReferenceType());
      if (Destination->isReferenceType()) {
        if (!Context.hasSameUnqualifiedType(Destination->getPointeeType(),
                                            SourceValue) ||
            !Destination->getPointeeType().isAtLeastAsQualifiedAs(SourceValue,
                                                                  Context) ||
            (Destination->isRValueReferenceType() && !SourceRValue) ||
            (Destination->isLValueReferenceType() &&
             !Destination->getPointeeType().isConstQualified() &&
             SourceRValue))
          return std::nullopt;
      } else if (!utilityTupleDirectConversion(S, SM, Context, SourceValue,
                                               Destination)) {
        return std::nullopt;
      }
    }
    return UtilityTupleConstruction::Pair;
  }
  if (!Primary || Construction->getNumArgs() != Tuple->Elements.size() ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx", "tuple"))
    return std::nullopt;
  for (unsigned I = 0; I < Construction->getNumArgs(); ++I) {
    auto Parameter = Constructor->getParamDecl(I)->getType();
    if (!Parameter->isReferenceType() ||
        !Context.hasSameUnqualifiedType(Construction->getArg(I)->getType(),
                                        Parameter->getPointeeType()))
      return std::nullopt;
    const auto Element = Tuple->Elements[I]->getType();
    if (Element->isReferenceType()) {
      const auto Argument = Construction->getArg(I)->getType();
      if (!Context.hasSameUnqualifiedType(Argument,
                                          Element->getPointeeType()) ||
          !Element->getPointeeType().isAtLeastAsQualifiedAs(Argument,
                                                            Context) ||
          (Element->isRValueReferenceType() &&
           Construction->getArg(I)->isLValue()) ||
          (Element->isLValueReferenceType() &&
           !Element->getPointeeType().isConstQualified() &&
           !Construction->getArg(I)->isLValue()))
        return std::nullopt;
    } else if (!utilityTupleDirectConversion(
                   S, SM, Context, Construction->getArg(I)->getType(),
                   Element))
      return std::nullopt;
  }
  return UtilityTupleConstruction::Elements;
}

std::optional<UtilityTupleAssignment>
approvedUtilityTupleAssignment(const State &S, const SourceManager &SM,
                               const CXXOperatorCallExpr *Assignment,
                               const ASTContext &Context) {
  if (!Assignment || Assignment->isTypeDependent() ||
      Assignment->isValueDependent() ||
      Assignment->isInstantiationDependent() ||
      Assignment->getOperator() != OO_Equal || Assignment->getNumArgs() != 2 ||
      !Assignment->isLValue())
    return std::nullopt;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Assignment->getDirectCallee());
  auto Tuple = approvedUtilityTupleRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  bool ReferenceTuple = false;
  if (!Tuple) {
    Tuple = approvedUtilityReferenceTupleRecord(
        S, SM, Method ? Method->getParent() : nullptr, Context);
    ReferenceTuple = Tuple.has_value();
  }
  if (!Tuple)
    Tuple = approvedUtilityMixedReferenceTupleRecord(
        S, SM, Method ? Method->getParent() : nullptr, Context);
  const bool EmptyDefaultedAssignment = !ReferenceTuple && Tuple &&
                                        Tuple->Elements.empty() &&
                                        Method && Method->isTrivial() &&
                                        defaultedAssignment(Method);
  if (!Method || !Tuple || Method->isStatic() || Method->isVariadic() ||
      Method->getNumParams() != 1 ||
      Method->getOverloadedOperator() != OO_Equal ||
      (!Method->hasBody() && !EmptyDefaultedAssignment) ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx", "tuple"))
    return std::nullopt;
  for (const auto *Element : Tuple->Elements) {
    const auto ElementType = Element->getType();
    const auto AssignedType = ElementType->isReferenceType()
                                  ? ElementType->getPointeeType()
                                  : ElementType;
    if (!utilityTupleAssignableValue(S, SM, Context, AssignedType))
      return std::nullopt;
  }
  const auto Parameter = Method->getParamDecl(0)->getType();
  const auto Result = Method->getReturnType();
  const auto TupleType = Context.getRecordType(Tuple->Record);
  if (!Result->isLValueReferenceType() ||
      !Context.hasSameUnqualifiedType(Result->getPointeeType(), TupleType) ||
      !Context.hasSameUnqualifiedType(Assignment->getArg(0)->getType(),
                                      TupleType) ||
      !Context.hasSameUnqualifiedType(Assignment->getType(), TupleType) ||
      !Parameter->isReferenceType() ||
      Parameter->getPointeeType().isVolatileQualified())
    return std::nullopt;
  auto SourceTuple = approvedUtilityTupleRecord(
      S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  if (!SourceTuple)
    SourceTuple = approvedUtilityReferenceTupleRecord(
        S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  if (!SourceTuple)
    SourceTuple = approvedUtilityMixedReferenceTupleRecord(
        S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  const auto *Primary = Method->getPrimaryTemplate();
  if (SourceTuple) {
    if (!Context.hasSameUnqualifiedType(
            Parameter->getPointeeType(),
            Context.getRecordType(SourceTuple->Record)))
      return std::nullopt;
    if (SourceTuple->Record->getCanonicalDecl() ==
        Tuple->Record->getCanonicalDecl())
      return UtilityTupleAssignment::CopyOrMove;
    if (!Primary || SourceTuple->Elements.size() != Tuple->Elements.size() ||
        !approvedStandardSDKDeclaration(S, SM, Primary) ||
        !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx", "tuple"))
      return std::nullopt;
    for (unsigned I = 0; I < Tuple->Elements.size(); ++I) {
      const auto SourceElement = SourceTuple->Elements[I]->getType();
      const auto DestinationElement = Tuple->Elements[I]->getType();
      const auto SourceValue = SourceElement->isReferenceType()
                                   ? SourceElement->getPointeeType()
                                   : SourceElement;
      const auto DestinationValue = DestinationElement->isReferenceType()
                                        ? DestinationElement->getPointeeType()
                                        : DestinationElement;
      if (!utilityTupleDirectConversion(S, SM, Context, SourceValue,
                                        DestinationValue))
        return std::nullopt;
    }
    return UtilityTupleAssignment::Converting;
  }
  auto SourcePair = approvedUtilityPairRecord(
      S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  if (!SourcePair)
    SourcePair = approvedUtilityReferencePairRecord(
        S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  if (!SourcePair)
    SourcePair = approvedUtilityMixedReferencePairRecord(
        S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  if (!SourcePair || Tuple->Elements.size() != 2 || !Primary ||
      !Context.hasSameUnqualifiedType(
          Parameter->getPointeeType(),
          Context.getRecordType(SourcePair->Record)) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx", "tuple"))
    return std::nullopt;
  for (unsigned I = 0; I != 2; ++I) {
    const auto SourceElement =
        I ? SourcePair->Second->getType() : SourcePair->First->getType();
    const auto DestinationElement = Tuple->Elements[I]->getType();
    const auto SourceValue = SourceElement->isReferenceType()
                                 ? SourceElement->getPointeeType()
                                 : SourceElement;
    const auto DestinationValue = DestinationElement->isReferenceType()
                                      ? DestinationElement->getPointeeType()
                                      : DestinationElement;
    if (!utilityTupleDirectConversion(S, SM, Context, SourceValue,
                                      DestinationValue))
      return std::nullopt;
  }
  return UtilityTupleAssignment::Pair;
}

bool approvedUtilityArrayMetadata(const State &S, const SourceManager &SM,
                                  const CXXRecordDecl *Record) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  const auto *Template =
      Specialization ? Specialization->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  if (!Specialization || !Template || !CanonicalTemplate ||
      Specialization->isUnion() || Specialization->isDependentContext() ||
      Specialization->getName() != "array" ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !cstddefOrigin(S, SM, Template->getLocation(), "libcxx", "array") ||
      !cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                     "__fwd/array.h"))
    return false;
  const auto &Arguments = Specialization->getTemplateArgs();
  if (Arguments.size() != 2 ||
      Arguments.get(0).getKind() != TemplateArgument::Type ||
      Arguments.get(1).getKind() != TemplateArgument::Integral)
    return false;
  const auto Size = Arguments.get(1).getAsIntegral();
  return !Size.isNegative() && Size.getLimitedValue(65537) <= 65536;
}

std::optional<UtilityArrayRecord>
approvedUtilityArrayRecord(const State &S, const SourceManager &SM,
                           const CXXRecordDecl *Record,
                           const ASTContext &Context) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  Specialization = Specialization
                       ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                             Specialization->getDefinition())
                       : nullptr;
  const auto *Template =
      Specialization ? Specialization->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  if (!approvedUtilityArrayMetadata(S, SM, Specialization) ||
      !Specialization || !Template || !CanonicalTemplate ||
      Specialization->getSpecializationKind() != TSK_ImplicitInstantiation ||
      Specialization->getNumBases() || !Specialization->isStandardLayout() ||
      !approvedStandardSDKDeclaration(S, SM, Specialization) ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !cstddefOrigin(S, SM, Specialization->getLocation(), "libcxx", "array") ||
      !cstddefOrigin(S, SM, Template->getLocation(), "libcxx", "array") ||
      !cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                     "__fwd/array.h"))
    return std::nullopt;
  const auto &Arguments = Specialization->getTemplateArgs();
  const auto SizeValue = Arguments.get(1).getAsIntegral();
  const uint64_t Size = SizeValue.getLimitedValue(65537);
  if (Size > 65536)
    return std::nullopt;
  auto Fields = Specialization->fields();
  auto It = Fields.begin();
  const auto *Elements = It == Fields.end() ? nullptr : *It++;
  const auto *Array =
      Elements ? Context.getAsConstantArrayType(Elements->getType()) : nullptr;
  auto Element = Arguments.get(0).getAsType();
  if (!Elements || It != Fields.end() || Elements->getName() != "__elems_" ||
      Elements->getAccess() != AS_public || Elements->isBitField() ||
      Elements->isMutable() || !Array || Element.isNull() ||
      !Element->isObjectType() || Element->isIncompleteType() ||
      !utilityArrayValue(S, SM, Context, Element) ||
      !approvedStandardSDKDeclaration(S, SM, Elements) ||
      !cstddefOrigin(S, SM, Elements->getLocation(), "libcxx", "array"))
    return std::nullopt;
  if (!Size) {
    unsigned AttributeCount = 0;
    for (const auto *Attribute : Elements->attrs()) {
      ++AttributeCount;
      if (!isa<AlignedAttr>(Attribute))
        return std::nullopt;
    }
    const auto StorageElement = Array->getElementType();
    const auto *Empty =
        StorageElement.getUnqualifiedType()->getAsCXXRecordDecl();
    Empty = Empty ? Empty->getDefinition() : nullptr;
    const auto &Layout = Context.getASTRecordLayout(Specialization);
    const auto ExpectedBytes =
        uint64_t(Context.getTypeSizeInChars(Element).getQuantity());
    const auto ExpectedBits = Context.getTypeSize(Element);
    const auto ExpectedAlign = Context.getTypeAlign(Element);
    if (AttributeCount != 1 ||
        Array->getSize().getLimitedValue(ExpectedBytes + 1) != ExpectedBytes ||
        StorageElement.isConstQualified() != Element.isConstQualified() ||
        StorageElement.isVolatileQualified() ||
        StorageElement.isRestrictQualified() || !Empty ||
        Empty->getName() != "__empty" || Empty->isUnion() ||
        Empty->isDependentContext() || Empty->getNumBases() ||
        Empty->getNumVBases() || Empty->isDynamicClass() ||
        !Empty->field_empty() || !Empty->isEmpty() ||
        !Empty->isStandardLayout() || !Empty->isTrivial() ||
        !Empty->hasTrivialDestructor() ||
        !approvedStandardSDKDeclaration(S, SM, Empty) ||
        !cstddefOrigin(S, SM, Empty->getLocation(), "libcxx",
                       "__utility/empty.h") ||
        Context.getTypeSize(StorageElement) != Context.getCharWidth() ||
        Context.getTypeAlign(StorageElement) != Context.getCharWidth() ||
        Layout.getFieldCount() != 1 || Layout.getFieldOffset(0) != 0 ||
        uint64_t(Layout.getSize().getQuantity()) * 8 != ExpectedBits ||
        uint64_t(Layout.getAlignment().getQuantity()) * 8 != ExpectedAlign)
      return std::nullopt;
    return UtilityArrayRecord{Specialization, Elements, Element, Size};
  }
  if (Elements->hasAttrs() ||
      Array->getSize().getLimitedValue(65537) != Size ||
      !Context.hasSameType(Array->getElementType(), Element))
    return std::nullopt;
  return UtilityArrayRecord{Specialization, Elements, Element, Size};
}

bool approvedUtilityArrayConstruction(const State &S, const SourceManager &SM,
                                      const CXXConstructExpr *Construction,
                                      const ASTContext &Context) {
  if (!Construction || Construction->isTypeDependent() ||
      Construction->isValueDependent() ||
      Construction->isInstantiationDependent() ||
      Construction->getConstructionKind() != CXXConstructionKind::Complete)
    return false;
  const auto *Constructor = Construction->getConstructor();
  const auto Array = approvedUtilityArrayRecord(
      S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
  if (!Constructor || !Array || !Constructor->isImplicit() ||
      !Constructor->isTrivial() || Constructor->isVariadic() ||
      Constructor->getParent()->getCanonicalDecl() !=
          Array->Record->getCanonicalDecl() ||
      Construction->getNumArgs() != Constructor->getNumParams())
    return false;
  return (!Construction->getNumArgs() && Constructor->isDefaultConstructor()) ||
         (Construction->getNumArgs() == 1 &&
          Constructor->isCopyOrMoveConstructor());
}

std::optional<UtilityArrayRecord>
approvedUtilityArrayAssignment(const State &S, const SourceManager &SM,
                               const CXXOperatorCallExpr *Assignment,
                               const ASTContext &Context) {
  if (!Assignment || Assignment->isTypeDependent() ||
      Assignment->isValueDependent() ||
      Assignment->isInstantiationDependent() ||
      Assignment->getOperator() != OO_Equal || Assignment->getNumArgs() != 2 ||
      !Assignment->isLValue())
    return std::nullopt;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Assignment->getDirectCallee());
  const auto Array = approvedUtilityArrayRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  if (!Method || !Array || !Method->isImplicit() || !Method->isTrivial() ||
      !Method->isDefaulted() || Method->isStatic() || Method->isVariadic() ||
      Method->getNumParams() != 1 ||
      Method->getOverloadedOperator() != OO_Equal ||
      !(Method->isCopyAssignmentOperator() ||
        Method->isMoveAssignmentOperator()))
    return std::nullopt;
  const auto Parameter = Method->getParamDecl(0)->getType();
  const auto Result = Method->getReturnType();
  const auto ArrayType = Context.getRecordType(Array->Record);
  if (!Parameter->isReferenceType() || !Result->isLValueReferenceType() ||
      !Context.hasSameUnqualifiedType(Parameter->getPointeeType(), ArrayType) ||
      !Context.hasSameUnqualifiedType(Result->getPointeeType(), ArrayType) ||
      !Context.hasSameUnqualifiedType(Assignment->getArg(0)->getType(),
                                      ArrayType) ||
      !Context.hasSameUnqualifiedType(Assignment->getArg(1)->getType(),
                                      ArrayType) ||
      !Context.hasSameUnqualifiedType(Assignment->getType(), ArrayType))
    return std::nullopt;
  return Array;
}

bool approvedUtilityInitializerListMetadata(const State &S,
                                            const SourceManager &SM,
                                            const CXXRecordDecl *Record) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  const auto *Template =
      Specialization ? Specialization->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  if (!Specialization || !Template || !CanonicalTemplate ||
      Specialization->isUnion() || Specialization->isDependentContext() ||
      Specialization->getName() != "initializer_list" ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                     "initializer_list") ||
      !cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                     "initializer_list"))
    return false;
  const auto &Arguments = Specialization->getTemplateArgs();
  return Arguments.size() == 1 &&
         Arguments.get(0).getKind() == TemplateArgument::Type;
}

std::optional<UtilityInitializerListRecord>
approvedUtilityInitializerListRecord(const State &S, const SourceManager &SM,
                                     const CXXRecordDecl *Record,
                                     const ASTContext &Context) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  Specialization = Specialization
                       ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                             Specialization->getDefinition())
                       : nullptr;
  const auto *Template =
      Specialization ? Specialization->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  if (!approvedUtilityInitializerListMetadata(S, SM, Specialization) ||
      !Specialization || !Template || !CanonicalTemplate ||
      Specialization->getSpecializationKind() != TSK_ImplicitInstantiation ||
      Specialization->getNumBases() || !Specialization->isStandardLayout() ||
      !Specialization->hasTrivialCopyConstructor() ||
      !Specialization->hasTrivialCopyAssignment() ||
      !Specialization->hasTrivialDestructor() ||
      !approvedStandardSDKDeclaration(S, SM, Specialization) ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !cstddefOrigin(S, SM, Specialization->getLocation(), "libcxx",
                     "initializer_list") ||
      !cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                     "initializer_list") ||
      !cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                     "initializer_list"))
    return std::nullopt;
  const auto &Arguments = Specialization->getTemplateArgs();
  const auto Element = Arguments.get(0).getAsType();
  if (Element.isNull() || Element->isReferenceType() ||
      !Element->isObjectType() || Element->isIncompleteType() ||
      Element.isVolatileQualified() || Element.isRestrictQualified() ||
      Element.getAddressSpace() != LangAS::Default)
    return std::nullopt;
  auto Fields = Specialization->fields();
  auto It = Fields.begin();
  const auto *Begin = It == Fields.end() ? nullptr : *It++;
  const auto *Size = It == Fields.end() ? nullptr : *It++;
  const auto BeginType = Context.getPointerType(Element.withConst());
  if (!Begin || !Size || It != Fields.end() || Begin->getName() != "__begin_" ||
      Size->getName() != "__size_" || Begin->getAccess() != AS_private ||
      Size->getAccess() != AS_private || Begin->isBitField() ||
      Size->isBitField() || Begin->isMutable() || Size->isMutable() ||
      Begin->hasAttrs() || Size->hasAttrs() ||
      !Context.hasSameType(Begin->getType(), BeginType) ||
      !Context.hasSameType(Size->getType(), Context.getSizeType()) ||
      !approvedStandardSDKDeclaration(S, SM, Begin) ||
      !approvedStandardSDKDeclaration(S, SM, Size) ||
      !cstddefOrigin(S, SM, Begin->getLocation(), "libcxx",
                     "initializer_list") ||
      !cstddefOrigin(S, SM, Size->getLocation(), "libcxx", "initializer_list"))
    return std::nullopt;
  const auto &Layout = Context.getASTRecordLayout(Specialization);
  const uint64_t PointerBits = Context.getTypeSize(BeginType);
  const uint64_t SizeBits = Context.getTypeSize(Context.getSizeType());
  if (Layout.getFieldCount() != 2 || Layout.getFieldOffset(0) != 0 ||
      Layout.getFieldOffset(1) != PointerBits || PointerBits != SizeBits ||
      uint64_t(Layout.getSize().getQuantity()) * 8 != PointerBits + SizeBits ||
      uint64_t(Layout.getAlignment().getQuantity()) * 8 !=
          Context.getTypeAlign(BeginType))
    return std::nullopt;
  return UtilityInitializerListRecord{Specialization, Begin, Size, Element};
}

std::optional<UtilityInitializerListConstruction>
approvedUtilityInitializerListConstruction(const State &S,
                                           const SourceManager &SM,
                                           const CXXConstructExpr *Construction,
                                           const ASTContext &Context) {
  if (!Construction || Construction->isTypeDependent() ||
      Construction->isValueDependent() ||
      Construction->isInstantiationDependent() ||
      Construction->getConstructionKind() != CXXConstructionKind::Complete)
    return std::nullopt;
  const auto *Constructor = Construction->getConstructor();
  const auto List = approvedUtilityInitializerListRecord(
      S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
  if (!Constructor || !List || Constructor->isVariadic() ||
      Constructor->getParent()->getCanonicalDecl() !=
          List->Record->getCanonicalDecl() ||
      Construction->getNumArgs() != Constructor->getNumParams())
    return std::nullopt;
  if (Constructor->isImplicit() && Constructor->isTrivial() &&
      Constructor->isCopyOrMoveConstructor() &&
      Construction->getNumArgs() == 1 &&
      Context.hasSameUnqualifiedType(Construction->getArg(0)->getType(),
                                     Construction->getType()))
    return UtilityInitializerListConstruction::CopyOrMove;
  if (!Construction->getNumArgs() && Constructor->isDefaultConstructor() &&
      Constructor->hasBody() &&
      approvedStandardSDKDeclaration(S, SM, Constructor) &&
      cstddefOrigin(S, SM, Constructor->getLocation(), "libcxx",
                    "initializer_list"))
    return UtilityInitializerListConstruction::Default;
  return std::nullopt;
}

std::optional<UtilityInitializerListRecord>
approvedUtilityInitializerListAssignment(const State &S,
                                         const SourceManager &SM,
                                         const CXXOperatorCallExpr *Assignment,
                                         const ASTContext &Context) {
  if (!Assignment || Assignment->isTypeDependent() ||
      Assignment->isValueDependent() ||
      Assignment->isInstantiationDependent() ||
      Assignment->getOperator() != OO_Equal || Assignment->getNumArgs() != 2 ||
      !Assignment->isLValue())
    return std::nullopt;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Assignment->getDirectCallee());
  const auto List = approvedUtilityInitializerListRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  if (!Method || !List || Method->isStatic() || Method->isVariadic() ||
      Method->getNumParams() != 1 ||
      Method->getOverloadedOperator() != OO_Equal || !Method->isImplicit() ||
      !Method->isTrivial() ||
      !(Method->isCopyAssignmentOperator() ||
        Method->isMoveAssignmentOperator()))
    return std::nullopt;
  const auto ListType = Context.getRecordType(List->Record);
  const auto Parameter = Method->getParamDecl(0)->getType();
  if (!Parameter->isReferenceType() ||
      !Method->getReturnType()->isLValueReferenceType() ||
      !Context.hasSameUnqualifiedType(Parameter->getPointeeType(), ListType) ||
      !Context.hasSameUnqualifiedType(Method->getReturnType()->getPointeeType(),
                                      ListType) ||
      !Context.hasSameUnqualifiedType(Assignment->getArg(0)->getType(),
                                      ListType) ||
      !Context.hasSameUnqualifiedType(Assignment->getArg(1)->getType(),
                                      ListType) ||
      !Context.hasSameUnqualifiedType(Assignment->getType(), ListType))
    return std::nullopt;
  return List;
}

std::optional<UtilityInitializerListExpression>
approvedUtilityInitializerListExpression(
    const State &S, const SourceManager &SM,
    const CXXStdInitializerListExpr *Expression, const ASTContext &Context) {
  if (!Expression || !Expression->isPRValue() ||
      Expression->isTypeDependent() || Expression->isValueDependent() ||
      Expression->isInstantiationDependent())
    return std::nullopt;
  const auto List = approvedUtilityInitializerListRecord(
      S, SM, Expression->getType()->getAsCXXRecordDecl(), Context);
  const auto *Backing =
      dyn_cast_or_null<MaterializeTemporaryExpr>(Expression->getSubExpr());
  const auto *Array =
      Backing ? Context.getAsConstantArrayType(Backing->getType()) : nullptr;
  const Expr *BackingValue = Backing ? Backing->getSubExpr() : nullptr;
  if (const auto *Bound = dyn_cast_or_null<CXXBindTemporaryExpr>(BackingValue))
    BackingValue = Bound->getSubExpr();
  const auto *Initializers = dyn_cast_or_null<InitListExpr>(BackingValue);
  if (!List || !Backing || !Array || !Initializers || !Backing->isXValue() ||
      !Backing->getSubExpr()->isPRValue() ||
      !Context.hasSameType(Backing->getType(),
                           Backing->getSubExpr()->getType()) ||
      !Context.hasSameType(Array->getElementType(),
                           List->ElementType.withConst()) ||
      Initializers->hasArrayFiller() || Initializers->hasDesignatedInit() ||
      Initializers->getInitializedFieldInUnion() ||
      Initializers->isStringLiteralInit())
    return std::nullopt;
  const uint64_t Size = Array->getSize().getLimitedValue(65537);
  if (!Size || Size > 65536 || Initializers->getNumInits() != Size)
    return std::nullopt;
  return UtilityInitializerListExpression{*List, Backing, Size};
}

bool approvedUtilityOptionalMetadata(const State &S, const SourceManager &SM,
                                     const CXXRecordDecl *Record) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  const auto *Template =
      Specialization ? Specialization->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  if (!Specialization || !Template || !CanonicalTemplate ||
      Specialization->isUnion() || Specialization->isDependentContext() ||
      Specialization->getName() != "optional" ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !cstddefOrigin(S, SM, Template->getLocation(), "libcxx", "optional") ||
      !cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                     "optional"))
    return false;
  const auto &Arguments = Specialization->getTemplateArgs();
  return Arguments.size() == 1 &&
         Arguments.get(0).getKind() == TemplateArgument::Type;
}

static const CXXRecordDecl *definedRecord(QualType Type) {
  const auto *Record = Type.isNull() ? nullptr : Type->getAsCXXRecordDecl();
  return Record ? Record->getDefinition() : nullptr;
}

static bool optionalInternalRecord(const State &S, const SourceManager &SM,
                                   const CXXRecordDecl *Record,
                                   llvm::StringRef Name,
                                   llvm::StringRef Path = "optional") {
  return Record && Record->getName() == Name && !Record->isUnion() &&
         !Record->isDependentContext() &&
         approvedStandardSDKDeclaration(S, SM, Record) &&
         cstddefOrigin(S, SM, Record->getLocation(), "libcxx", Path);
}

std::optional<UtilityOptionalRecord>
approvedUtilityOptionalRecord(const State &S, const SourceManager &SM,
                              const CXXRecordDecl *Record,
                              const ASTContext &Context) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  Specialization = Specialization
                       ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                             Specialization->getDefinition())
                       : nullptr;
  if (!approvedUtilityOptionalMetadata(S, SM, Specialization) ||
      !Specialization ||
      Specialization->getSpecializationKind() != TSK_ImplicitInstantiation ||
      Specialization->getNumBases() != 3 || Specialization->getNumVBases() ||
      !Specialization->field_empty() || Specialization->isDynamicClass() ||
      !Specialization->hasTrivialCopyConstructor() ||
      !Specialization->hasTrivialDestructor() ||
      !approvedStandardSDKDeclaration(S, SM, Specialization) ||
      !cstddefOrigin(S, SM, Specialization->getLocation(), "libcxx",
                     "optional"))
    return std::nullopt;

  const auto Element = Specialization->getTemplateArgs().get(0).getAsType();
  const auto *ElementRecord =
      Element.isNull() ? nullptr
                       : Element.getUnqualifiedType()->getAsCXXRecordDecl();
  if (Element.isNull() || Element->isReferenceType() ||
      !Element->isObjectType() || Element->isIncompleteType() ||
      Element.isVolatileQualified() || Element.isRestrictQualified() ||
      Element.getAddressSpace() != LangAS::Default ||
      !utilityTupleValue(S, SM, Context, Element) ||
      (!Specialization->isStandardLayout() &&
       !approvedUtilityTupleMetadata(S, SM, ElementRecord)))
    return std::nullopt;

  const auto &TopLayout = Context.getASTRecordLayout(Specialization);
  const bool MicrosoftABI = Context.getTargetInfo().getCXXABI().isMicrosoft();
  auto Bases = Specialization->bases();
  auto Base = Bases.begin();
  const auto *Main =
      Base == Bases.end() ? nullptr : definedRecord(Base->getType());
  const auto MainSize =
      Main ? Context.getASTRecordLayout(Main).getSize() : CharUnits::Zero();
  const bool MainBase = Base != Bases.end() && !Base->isVirtual() &&
                        Base->getAccessSpecifier() == AS_private && Main &&
                        TopLayout.getBaseClassOffset(Main).getQuantity() == 0;
  if (Base != Bases.end())
    ++Base;
  const auto *CtorSFINAE =
      Base == Bases.end() ? nullptr : definedRecord(Base->getType());
  const bool CtorBase = Base != Bases.end() && !Base->isVirtual() &&
                        Base->getAccessSpecifier() == AS_private && CtorSFINAE;
  if (Base != Bases.end())
    ++Base;
  const auto *AssignSFINAE =
      Base == Bases.end() ? nullptr : definedRecord(Base->getType());
  const bool AssignBase = Base != Bases.end() && !Base->isVirtual() &&
                          Base->getAccessSpecifier() == AS_private &&
                          AssignSFINAE;
  if (Base != Bases.end())
    ++Base;
  auto BooleanArguments = [](const CXXRecordDecl *Value) {
    const auto *Instantiation =
        dyn_cast_or_null<ClassTemplateSpecializationDecl>(Value);
    if (!Instantiation || Instantiation->getTemplateArgs().size() != 2)
      return false;
    for (const auto &Argument : Instantiation->getTemplateArgs().asArray())
      if (Argument.getKind() != TemplateArgument::Integral ||
          !Argument.getIntegralType()->isBooleanType())
        return false;
    return true;
  };
  const auto CtorOffset =
      CtorSFINAE ? TopLayout.getBaseClassOffset(CtorSFINAE) : CharUnits::Zero();
  const auto AssignOffset = AssignSFINAE
                                ? TopLayout.getBaseClassOffset(AssignSFINAE)
                                : CharUnits::Zero();
  const bool SharedSFINAEStorage =
      !MicrosoftABI && CtorOffset.isZero() && AssignOffset.isZero();
  const bool SeparateSFINAEStorage =
      MicrosoftABI && CtorOffset == MainSize &&
      AssignOffset == MainSize + CharUnits::One();
  // MS record layout inserts one byte before the empty SFINAE bases when the
  // main base ends in a zero-sized subobject (for example array<T, 0>).
  const bool PaddedSFINAEStorage =
      MicrosoftABI && CtorOffset == MainSize + CharUnits::One() &&
      AssignOffset == MainSize + CharUnits::fromQuantity(2);
  if (Base != Bases.end() || !MainBase || !CtorBase || !AssignBase ||
      (!SharedSFINAEStorage && !SeparateSFINAEStorage &&
       !PaddedSFINAEStorage) ||
      !optionalInternalRecord(S, SM, Main, "__optional_move_assign_base") ||
      !optionalInternalRecord(S, SM, CtorSFINAE, "__sfinae_ctor_base",
                              "__tuple/sfinae_helpers.h") ||
      !optionalInternalRecord(S, SM, AssignSFINAE, "__sfinae_assign_base",
                              "__tuple/sfinae_helpers.h") ||
      !BooleanArguments(CtorSFINAE) || !BooleanArguments(AssignSFINAE) ||
      !CtorSFINAE->isEmpty() || !AssignSFINAE->isEmpty() ||
      CtorSFINAE->getNumBases() || AssignSFINAE->getNumBases() ||
      !CtorSFINAE->field_empty() || !AssignSFINAE->field_empty())
    return std::nullopt;

  const llvm::StringRef ChainNames[] = {
      "__optional_move_assign_base", "__optional_copy_assign_base",
      "__optional_move_base",        "__optional_copy_base",
      "__optional_storage_base",     "__optional_destruct_base"};
  const CXXRecordDecl *Chain[std::size(ChainNames)]{};
  const auto *Current = Main;
  for (unsigned I = 0; I < std::size(ChainNames); ++I) {
    if (!optionalInternalRecord(S, SM, Current, ChainNames[I]) ||
        Current->getNumVBases() || Current->isDynamicClass())
      return std::nullopt;
    const auto *Internal = dyn_cast<ClassTemplateSpecializationDecl>(Current);
    if (!Internal || Internal->getTemplateArgs().size() == 0 ||
        Internal->getTemplateArgs().get(0).getKind() !=
            TemplateArgument::Type ||
        !Context.hasSameType(Internal->getTemplateArgs().get(0).getAsType(),
                             Element))
      return std::nullopt;
    for (unsigned J = 1; J < Internal->getTemplateArgs().size(); ++J) {
      const auto &Argument = Internal->getTemplateArgs().get(J);
      if (Argument.getKind() != TemplateArgument::Integral ||
          !Argument.getIntegralType()->isBooleanType())
        return std::nullopt;
    }
    Chain[I] = Current;
    if (I + 1 == std::size(ChainNames)) {
      if (Current->getNumBases())
        return std::nullopt;
      break;
    }
    if (Current->getNumBases() != 1 ||
        Current->field_begin() != Current->field_end())
      return std::nullopt;
    const auto &Layout = Context.getASTRecordLayout(Current);
    const auto &OnlyBase = *Current->bases_begin();
    const auto *Next = definedRecord(OnlyBase.getType());
    if (!Next || OnlyBase.isVirtual() ||
        OnlyBase.getAccessSpecifier() != AS_public ||
        Layout.getBaseClassOffset(Next).getQuantity() != 0)
      return std::nullopt;
    Current = Next;
  }
  const auto *Storage = Chain[4];
  const auto *Destruct = Chain[5];

  auto Fields = Destruct->fields();
  auto It = Fields.begin();
  const auto *Anonymous = It == Fields.end() ? nullptr : *It++;
  const auto *Engaged = It == Fields.end() ? nullptr : *It++;
  const auto *Union = Anonymous ? definedRecord(Anonymous->getType()) : nullptr;
  if (!Anonymous || !Engaged || It != Fields.end() || !Union ||
      !Union->isUnion() || !Anonymous->isAnonymousStructOrUnion() ||
      Anonymous->getAccess() != AS_public || Anonymous->isBitField() ||
      Anonymous->isMutable() || Engaged->getName() != "__engaged_" ||
      Engaged->getAccess() != AS_public || Engaged->isBitField() ||
      Engaged->isMutable() || !Engaged->getType()->isBooleanType() ||
      !approvedStandardSDKDeclaration(S, SM, Anonymous) ||
      !approvedStandardSDKDeclaration(S, SM, Engaged) ||
      !approvedStandardSDKDeclaration(S, SM, Union) ||
      !cstddefOrigin(S, SM, Anonymous->getLocation(), "libcxx", "optional") ||
      !cstddefOrigin(S, SM, Engaged->getLocation(), "libcxx", "optional") ||
      !cstddefOrigin(S, SM, Union->getLocation(), "libcxx", "optional"))
    return std::nullopt;
  auto UnionFields = Union->fields();
  auto UnionIt = UnionFields.begin();
  const auto *NullState = UnionIt == UnionFields.end() ? nullptr : *UnionIt++;
  const auto *Value = UnionIt == UnionFields.end() ? nullptr : *UnionIt++;
  const auto ValueType = Element.getUnqualifiedType();
  if (!NullState || !Value || UnionIt != UnionFields.end() ||
      NullState->getName() != "__null_state_" ||
      (!NullState->getType()->isSpecificBuiltinType(BuiltinType::Char_S) &&
       !NullState->getType()->isSpecificBuiltinType(BuiltinType::Char_U)) ||
      Value->getName() != "__val_" || NullState->getAccess() != AS_public ||
      Value->getAccess() != AS_public || NullState->isBitField() ||
      Value->isBitField() || NullState->isMutable() || Value->isMutable() ||
      !Context.hasSameType(Value->getType(), ValueType) ||
      !approvedStandardSDKDeclaration(S, SM, NullState) ||
      !approvedStandardSDKDeclaration(S, SM, Value) ||
      !cstddefOrigin(S, SM, NullState->getLocation(), "libcxx", "optional") ||
      !cstddefOrigin(S, SM, Value->getLocation(), "libcxx", "optional"))
    return std::nullopt;

  const auto &MainLayout = Context.getASTRecordLayout(Main);
  const auto &StorageLayout = Context.getASTRecordLayout(Storage);
  const auto &DestructLayout = Context.getASTRecordLayout(Destruct);
  const auto &UnionLayout = Context.getASTRecordLayout(Union);
  const uint64_t ValueBits = Context.getTypeSize(ValueType);
  const uint64_t ValueAlign = Context.getTypeAlign(ValueType);
  const uint64_t BoolBits = Context.getTypeSize(Context.BoolTy);
  const uint64_t ExpectedBits =
      ((ValueBits + BoolBits + ValueAlign - 1) / ValueAlign) * ValueAlign;
  const uint64_t SFINAEStorageBits = (SeparateSFINAEStorage ? 2
                                      : PaddedSFINAEStorage ? 3
                                                            : 0) *
                                     Context.getCharWidth();
  const uint64_t ExpectedTopBits =
      ((ExpectedBits + SFINAEStorageBits + ValueAlign - 1) / ValueAlign) *
      ValueAlign;
  if (MainLayout.getSize() != StorageLayout.getSize() ||
      TopLayout.getAlignment() != MainLayout.getAlignment() ||
      TopLayout.getAlignment() != StorageLayout.getAlignment() ||
      MainLayout.getSize() != DestructLayout.getSize() ||
      TopLayout.getAlignment() != DestructLayout.getAlignment() ||
      DestructLayout.getFieldCount() != 2 ||
      DestructLayout.getFieldOffset(0) != 0 ||
      DestructLayout.getFieldOffset(1) != ValueBits ||
      UnionLayout.getFieldCount() != 2 || UnionLayout.getFieldOffset(0) != 0 ||
      UnionLayout.getFieldOffset(1) != 0 ||
      uint64_t(UnionLayout.getSize().getQuantity()) * 8 != ValueBits ||
      uint64_t(MainLayout.getSize().getQuantity()) * 8 != ExpectedBits ||
      uint64_t(TopLayout.getSize().getQuantity()) * 8 != ExpectedTopBits ||
      uint64_t(TopLayout.getAlignment().getQuantity()) * 8 != ValueAlign)
    return std::nullopt;

  return UtilityOptionalRecord{Specialization, Storage, Destruct,
                               Value,          Engaged, Element};
}

static const CXXRecordDecl *approvedNulloptRecord(const State &S,
                                                  const SourceManager &SM,
                                                  QualType Type,
                                                  const ASTContext &Context) {
  const auto *Record = definedRecord(Type.getUnqualifiedType());
  if (!optionalInternalRecord(S, SM, Record, "nullopt_t") ||
      Record->getNumBases() || !Record->field_empty() || !Record->isEmpty() ||
      !Record->isStandardLayout() || !Record->hasTrivialCopyConstructor() ||
      !Record->hasTrivialDestructor())
    return nullptr;
  const auto &Layout = Context.getASTRecordLayout(Record);
  return Layout.getSize().getQuantity() == 1 &&
                 Layout.getAlignment().getQuantity() == 1
             ? Record
             : nullptr;
}

static bool approvedNulloptReference(const State &S, const SourceManager &SM,
                                     const DeclRefExpr *Reference,
                                     const ASTContext &Context) {
  const auto *Variable =
      Reference ? dyn_cast<VarDecl>(Reference->getDecl()) : nullptr;
  const auto *Record =
      Variable ? approvedNulloptRecord(S, SM, Variable->getType(), Context)
               : nullptr;
  return Variable && Record && Variable->getName() == "nullopt" &&
         Variable->isInline() && Variable->isConstexpr() &&
         Variable->getType().isConstQualified() &&
         approvedStandardSDKDeclaration(S, SM, Variable) &&
         cstddefOrigin(S, SM, Variable->getLocation(), "libcxx", "optional") &&
         S.owns(SM, Reference->getExprLoc());
}

bool approvedUtilityNulloptExpression(const State &S, const SourceManager &SM,
                                      const Expr *Expression,
                                      const ASTContext &Context) {
  if (!Expression || Expression->isTypeDependent() ||
      Expression->isValueDependent() || Expression->isInstantiationDependent())
    return false;
  if (const auto *Reference = dyn_cast<DeclRefExpr>(Expression))
    return approvedNulloptReference(S, SM, Reference, Context);
  if (const auto *Wrapper = dyn_cast<ExprWithCleanups>(Expression))
    return approvedUtilityNulloptExpression(S, SM, Wrapper->getSubExpr(),
                                            Context);
  if (const auto *Wrapper = dyn_cast<MaterializeTemporaryExpr>(Expression))
    return approvedUtilityNulloptExpression(S, SM, Wrapper->getSubExpr(),
                                            Context);
  if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Expression))
    return Cast->getCastKind() == CK_NoOp &&
           approvedUtilityNulloptExpression(S, SM, Cast->getSubExpr(), Context);
  const auto *Construction = dyn_cast<CXXConstructExpr>(Expression);
  const auto *Constructor =
      Construction ? Construction->getConstructor() : nullptr;
  const auto *Record =
      Construction
          ? approvedNulloptRecord(S, SM, Construction->getType(), Context)
          : nullptr;
  return Construction && Constructor && Record &&
         Construction->getConstructionKind() == CXXConstructionKind::Complete &&
         Construction->getNumArgs() == 1 && Constructor->isTrivial() &&
         Constructor->isCopyOrMoveConstructor() &&
         Constructor->getParent()->getCanonicalDecl() ==
             Record->getCanonicalDecl() &&
         approvedUtilityNulloptExpression(S, SM, Construction->getArg(0),
                                          Context);
}

static const CXXRecordDecl *approvedInPlaceRecord(const State &S,
                                                  const SourceManager &SM,
                                                  QualType Type,
                                                  const ASTContext &Context) {
  const auto *Record = definedRecord(Type.getUnqualifiedType());
  if (!optionalInternalRecord(S, SM, Record, "in_place_t",
                              "__utility/in_place.h") ||
      Record->getNumBases() || !Record->field_empty() || !Record->isEmpty() ||
      !Record->isStandardLayout() || !Record->hasTrivialCopyConstructor() ||
      !Record->hasTrivialDestructor())
    return nullptr;
  const auto &Layout = Context.getASTRecordLayout(Record);
  return Layout.getSize().getQuantity() == 1 &&
                 Layout.getAlignment().getQuantity() == 1
             ? Record
             : nullptr;
}

static bool approvedInPlaceReference(const State &S, const SourceManager &SM,
                                     const DeclRefExpr *Reference,
                                     const ASTContext &Context) {
  const auto *Variable =
      Reference ? dyn_cast<VarDecl>(Reference->getDecl()) : nullptr;
  const auto *Record =
      Variable ? approvedInPlaceRecord(S, SM, Variable->getType(), Context)
               : nullptr;
  return Variable && Record && Variable->getName() == "in_place" &&
         Variable->isInline() && Variable->isConstexpr() &&
         Variable->getType().isConstQualified() &&
         approvedStandardSDKDeclaration(S, SM, Variable) &&
         cstddefOrigin(S, SM, Variable->getLocation(), "libcxx",
                       "__utility/in_place.h") &&
         S.owns(SM, Reference->getExprLoc());
}

bool approvedUtilityInPlaceType(const State &S, const SourceManager &SM,
                                QualType Type, const ASTContext &Context) {
  return approvedInPlaceRecord(S, SM, Type, Context) != nullptr;
}

bool approvedUtilityInPlaceExpression(const State &S, const SourceManager &SM,
                                      const Expr *Expression,
                                      const ASTContext &Context) {
  if (!Expression || Expression->isTypeDependent() ||
      Expression->isValueDependent() || Expression->isInstantiationDependent())
    return false;
  if (const auto *Reference = dyn_cast<DeclRefExpr>(Expression))
    return approvedInPlaceReference(S, SM, Reference, Context);
  if (const auto *Wrapper = dyn_cast<ExprWithCleanups>(Expression))
    return approvedUtilityInPlaceExpression(S, SM, Wrapper->getSubExpr(),
                                            Context);
  if (const auto *Wrapper = dyn_cast<MaterializeTemporaryExpr>(Expression))
    return approvedUtilityInPlaceExpression(S, SM, Wrapper->getSubExpr(),
                                            Context);
  if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Expression))
    return Cast->getCastKind() == CK_NoOp &&
           approvedUtilityInPlaceExpression(S, SM, Cast->getSubExpr(), Context);
  const auto *Construction = dyn_cast<CXXConstructExpr>(Expression);
  const auto *Constructor =
      Construction ? Construction->getConstructor() : nullptr;
  const auto *Record =
      Construction
          ? approvedInPlaceRecord(S, SM, Construction->getType(), Context)
          : nullptr;
  if (!Construction || !Constructor || !Record ||
      Construction->getConstructionKind() != CXXConstructionKind::Complete ||
      Constructor->getParent()->getCanonicalDecl() !=
          Record->getCanonicalDecl() ||
      !approvedStandardSDKDeclaration(S, SM, Constructor))
    return false;
  if (!Construction->getNumArgs())
    return Constructor->isTrivial() && Constructor->isDefaulted() &&
           Constructor->isDefaultConstructor();
  return Construction->getNumArgs() == 1 && Constructor->isTrivial() &&
         Constructor->isCopyOrMoveConstructor() &&
         approvedUtilityInPlaceExpression(S, SM, Construction->getArg(0),
                                          Context);
}

std::optional<UtilityOptionalConstruction>
approvedUtilityOptionalConstruction(const State &S, const SourceManager &SM,
                                    const CXXConstructExpr *Construction,
                                    const ASTContext &Context) {
  if (!Construction || Construction->isTypeDependent() ||
      Construction->isValueDependent() ||
      Construction->isInstantiationDependent() ||
      Construction->getConstructionKind() != CXXConstructionKind::Complete)
    return std::nullopt;
  const auto *Constructor = Construction->getConstructor();
  const auto Optional = approvedUtilityOptionalRecord(
      S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
  if (!Constructor || !Optional || Constructor->isVariadic() ||
      Constructor->getParent()->getCanonicalDecl() !=
          Optional->Record->getCanonicalDecl() ||
      Construction->getNumArgs() != Constructor->getNumParams() ||
      !approvedStandardSDKDeclaration(S, SM, Constructor) ||
      !cstddefOrigin(S, SM, Constructor->getLocation(), "libcxx", "optional"))
    return std::nullopt;
  if (Constructor->isTrivial() && Constructor->isDefaulted() &&
      Constructor->isCopyOrMoveConstructor() &&
      Construction->getNumArgs() == 1 &&
      Context.hasSameUnqualifiedType(Construction->getArg(0)->getType(),
                                     Construction->getType()))
    return UtilityOptionalConstruction::CopyOrMove;
  if (!Construction->getNumArgs() && Constructor->isDefaultConstructor() &&
      Constructor->hasBody())
    return UtilityOptionalConstruction::Empty;
  if (Construction->getNumArgs() == 1 && Constructor->hasBody() &&
      approvedUtilityNulloptExpression(S, SM, Construction->getArg(0), Context))
    return UtilityOptionalConstruction::Empty;
  const auto *Primary = Constructor->getPrimaryTemplate();
  if ((Construction->getNumArgs() == 1 || Construction->getNumArgs() == 2) &&
      Primary && Constructor->hasBody() &&
      approvedStandardSDKDeclaration(S, SM, Primary) &&
      cstddefOrigin(S, SM, Primary->getLocation(), "libcxx", "optional") &&
      approvedUtilityInPlaceExpression(S, SM, Construction->getArg(0),
                                       Context) &&
      !Constructor->getParamDecl(0)->getType()->isReferenceType() &&
      approvedInPlaceRecord(S, SM, Constructor->getParamDecl(0)->getType(),
                            Context)) {
    if (Construction->getNumArgs() == 1)
      return UtilityOptionalConstruction::InPlaceDefault;
    const auto Argument = Construction->getArg(1)->getType();
    const auto Parameter = Constructor->getParamDecl(1)->getType();
    if (Parameter->isReferenceType() &&
        Context.hasSameUnqualifiedType(Argument, Parameter->getPointeeType()) &&
        utilityTupleDirectConversion(S, SM, Context, Argument,
                                     Optional->ElementType))
      return UtilityOptionalConstruction::InPlaceValue;
  }
  const auto Parameter = Construction->getNumArgs() == 1
                             ? Constructor->getParamDecl(0)->getType()
                             : QualType();
  const auto SourceOptional =
      Construction->getNumArgs() == 1
          ? approvedUtilityOptionalRecord(
                S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(),
                Context)
          : std::optional<UtilityOptionalRecord>();
  if (SourceOptional && Primary && Constructor->hasBody() &&
      SourceOptional->Record->getCanonicalDecl() !=
          Optional->Record->getCanonicalDecl() &&
      approvedStandardSDKDeclaration(S, SM, Primary) &&
      cstddefOrigin(S, SM, Primary->getLocation(), "libcxx", "optional") &&
      Parameter->isReferenceType() &&
      !Parameter->getPointeeType().isVolatileQualified() &&
      Context.hasSameUnqualifiedType(
          Parameter->getPointeeType(),
          Context.getRecordType(SourceOptional->Record)) &&
      utilityTupleDirectConversion(S, SM, Context, SourceOptional->ElementType,
                                   Optional->ElementType))
    return UtilityOptionalConstruction::Converting;
  if (Construction->getNumArgs() == 1 && Primary && Constructor->hasBody() &&
      approvedStandardSDKDeclaration(S, SM, Primary) &&
      cstddefOrigin(S, SM, Primary->getLocation(), "libcxx", "optional") &&
      Parameter->isReferenceType() &&
      Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                     Construction->getArg(0)->getType()) &&
      utilityTupleDirectConversion(S, SM, Context,
                                   Construction->getArg(0)->getType(),
                                   Optional->ElementType))
    return UtilityOptionalConstruction::Value;
  return std::nullopt;
}

std::optional<UtilityOptionalAssignment>
approvedUtilityOptionalAssignment(const State &S, const SourceManager &SM,
                                  const CXXOperatorCallExpr *Assignment,
                                  const ASTContext &Context) {
  if (!Assignment || Assignment->isTypeDependent() ||
      Assignment->isValueDependent() ||
      Assignment->isInstantiationDependent() ||
      Assignment->getOperator() != OO_Equal || Assignment->getNumArgs() != 2 ||
      !Assignment->isLValue())
    return std::nullopt;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Assignment->getDirectCallee());
  const auto Optional = approvedUtilityOptionalRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  if (!Method || !Optional || Method->isStatic() || Method->isVariadic() ||
      Method->getNumParams() != 1 ||
      Method->getOverloadedOperator() != OO_Equal ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx", "optional"))
    return std::nullopt;
  const auto OptionalType = Context.getRecordType(Optional->Record);
  if (!Context.hasSameUnqualifiedType(Assignment->getArg(0)->getType(),
                                      OptionalType) ||
      !Context.hasSameUnqualifiedType(Assignment->getType(), OptionalType) ||
      !Method->getReturnType()->isLValueReferenceType() ||
      !Context.hasSameUnqualifiedType(Method->getReturnType()->getPointeeType(),
                                      OptionalType))
    return std::nullopt;
  if (defaultedAssignment(Method) &&
      utilityTupleAssignableValue(S, SM, Context, Optional->ElementType) &&
      Context.hasSameUnqualifiedType(Assignment->getArg(1)->getType(),
                                     OptionalType))
    return UtilityOptionalAssignment::CopyOrMove;
  if (Method->hasBody() &&
      approvedUtilityNulloptExpression(S, SM, Assignment->getArg(1), Context))
    return UtilityOptionalAssignment::Empty;
  const auto *Primary = Method->getPrimaryTemplate();
  const auto Parameter = Method->getParamDecl(0)->getType();
  const auto SourceOptional = approvedUtilityOptionalRecord(
      S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  if (SourceOptional && Method->hasBody() && Primary &&
      SourceOptional->Record->getCanonicalDecl() !=
          Optional->Record->getCanonicalDecl() &&
      approvedStandardSDKDeclaration(S, SM, Primary) &&
      cstddefOrigin(S, SM, Primary->getLocation(), "libcxx", "optional") &&
      Parameter->isReferenceType() &&
      !Parameter->getPointeeType().isVolatileQualified() &&
      Context.hasSameUnqualifiedType(
          Parameter->getPointeeType(),
          Context.getRecordType(SourceOptional->Record)) &&
      utilityTupleAssignableValue(S, SM, Context, Optional->ElementType) &&
      utilityTupleDirectConversion(S, SM, Context, SourceOptional->ElementType,
                                   Optional->ElementType))
    return UtilityOptionalAssignment::Converting;
  if (Method->hasBody() && Primary &&
      approvedStandardSDKDeclaration(S, SM, Primary) &&
      cstddefOrigin(S, SM, Primary->getLocation(), "libcxx", "optional") &&
      Parameter->isReferenceType() &&
      Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                     Assignment->getArg(1)->getType()) &&
      utilityTupleAssignableValue(S, SM, Context, Optional->ElementType) &&
      utilityTupleDirectConversion(S, SM, Context,
                                   Assignment->getArg(1)->getType(),
                                   Optional->ElementType))
    return UtilityOptionalAssignment::Value;
  return std::nullopt;
}

bool approvedUtilityOptionalBaseCast(const State &S, const SourceManager &SM,
                                     const CastExpr *Cast,
                                     const ASTContext &Context) {
  if (!Cast ||
      (Cast->getCastKind() != CK_DerivedToBase &&
       Cast->getCastKind() != CK_UncheckedDerivedToBase) ||
      Cast->path_empty())
    return false;
  const auto *Source = Cast->getSubExpr();
  while (const auto *NoOp = dyn_cast<ImplicitCastExpr>(Source)) {
    if (NoOp->getCastKind() != CK_NoOp)
      break;
    Source = NoOp->getSubExpr();
  }
  const auto Optional = approvedUtilityOptionalRecord(
      S, SM, Source->getType()->getAsCXXRecordDecl(), Context);
  const auto *Destination = definedRecord(Cast->getType());
  if (!Optional || !Destination)
    return false;
  const auto *Current = Optional->Record;
  for (const auto *Step : Cast->path()) {
    if (!Step || Step->isVirtual())
      return false;
    const auto *Next = definedRecord(Step->getType());
    if (!Next || Current->getNumBases() == 0)
      return false;
    bool Direct = false;
    for (const auto &Base : Current->bases()) {
      const auto *DirectBase = definedRecord(Base.getType());
      if (!Base.isVirtual() && DirectBase &&
          DirectBase->getCanonicalDecl() == Next->getCanonicalDecl()) {
        Direct = true;
        break;
      }
    }
    if (!Direct)
      return false;
    Current = Next;
  }
  return Current->getCanonicalDecl() == Destination->getCanonicalDecl() &&
         (Destination->getCanonicalDecl() ==
              Optional->StorageBase->getCanonicalDecl() ||
          Destination->getCanonicalDecl() ==
              Optional->DestructBase->getCanonicalDecl());
}

static bool utilityObjectPointer(const ASTContext &Context, QualType Type) {
  return !Type.isNull() && !Type.isVolatileQualified() &&
         Type.getAddressSpace() == LangAS::Default && Type->isPointerType() &&
         !Type->isFunctionPointerType() &&
         Type->getPointeeType()->isObjectType() &&
         !Type->getPointeeType().isVolatileQualified() &&
         Type->getPointeeType().getAddressSpace() == LangAS::Default &&
         Context.getTypeSize(Type) == Context.getTypeSize(Context.VoidPtrTy) &&
         Context.getTypeAlign(Type) == Context.getTypeAlign(Context.VoidPtrTy);
}

static bool utilityAlgorithmScalarPointer(const ASTContext &Context,
                                          QualType Type) {
  return utilityObjectPointer(Context, Type) &&
         utilityScalar(Context, Type->getPointeeType());
}

static bool utilityMemoryDestructiblePointer(const State &S,
                                             const SourceManager &SM,
                                             const ASTContext &Context,
                                             QualType Type) {
  if (!utilityObjectPointer(Context, Type))
    return false;
  const auto Element = Type->getPointeeType();
  if (utilityScalar(Context, Element))
    return true;
  const auto *Record = definedRecord(Element.getUnqualifiedType());
  return Record && !Record->isUnion() && !Record->isDependentContext() &&
         S.owns(SM, Record->getLocation());
}

static bool utilityMemoryTrivialValue(const State &S, const SourceManager &SM,
                                      const ASTContext &Context,
                                      QualType Type) {
  if (utilityScalar(Context, Type))
    return true;
  const auto *Record = definedRecord(Type.getUnqualifiedType());
  return Record && !Record->isUnion() && !Record->isDependentContext() &&
         S.owns(SM, Record->getLocation()) && Record->isStandardLayout() &&
         Record->isTrivial() && Record->hasTrivialDestructor();
}

const CXXConstructorDecl *
approvedUtilityMemoryDefaultConstructor(const State &S, const SourceManager &SM,
                                        QualType Element,
                                        const ASTContext &Context) {
  const auto *Record = definedRecord(Element.getUnqualifiedType());
  if (!Record || Record->isUnion() || Record->isDependentContext() ||
      !S.owns(SM, Record->getLocation()))
    return nullptr;
  const CXXConstructorDecl *Constructor = nullptr;
  for (const auto *Candidate : Record->ctors()) {
    if (!Candidate->isDefaultConstructor())
      continue;
    if (Constructor &&
        Constructor->getCanonicalDecl() != Candidate->getCanonicalDecl())
      return nullptr;
    Constructor = Candidate;
  }
  const auto *Prototype =
      Constructor ? Constructor->getType()->getAs<FunctionProtoType>()
                  : nullptr;
  if (!Constructor || Constructor->isInvalidDecl() ||
      Constructor->isDeleted() || Constructor->getNumParams() ||
      !supportedConstructor(Constructor) || !Prototype ||
      !Prototype->isNothrow() || !S.owns(SM, Constructor->getLocation()))
    return nullptr;
  if (Constructor->isTrivial())
    return Constructor;
  const FunctionDecl *Definition = nullptr;
  if (!Constructor->hasBody(Definition) || !Definition ||
      !S.owns(SM, Definition->getLocation()))
    return nullptr;
  return cast<CXXConstructorDecl>(Definition);
}

static bool utilityMemoryDefaultPointer(const State &S, const SourceManager &SM,
                                        const ASTContext &Context,
                                        QualType Type) {
  if (!utilityObjectPointer(Context, Type))
    return false;
  const auto Element = Type->getPointeeType();
  return utilityMemoryTrivialValue(S, SM, Context, Element) ||
         approvedUtilityMemoryDefaultConstructor(S, SM, Element, Context);
}

static bool utilityMemoryWritableDefaultPointer(const State &S,
                                                const SourceManager &SM,
                                                const ASTContext &Context,
                                                QualType Type) {
  return utilityMemoryDefaultPointer(S, SM, Context, Type) &&
         !Type->getPointeeType().isConstQualified();
}

static llvm::StringRef utilityMemorySourceHelper(UtilityOperation Operation) {
  switch (Operation) {
  case UtilityOperation::MemoryUninitializedCopy:
    return "__uninitialized_copy";
  case UtilityOperation::MemoryUninitializedCopyN:
    return "__uninitialized_copy_n";
  case UtilityOperation::MemoryUninitializedFill:
    return "__uninitialized_fill";
  case UtilityOperation::MemoryUninitializedFillN:
    return "__uninitialized_fill_n";
  case UtilityOperation::MemoryUninitializedMove:
    return "__uninitialized_move";
  case UtilityOperation::MemoryUninitializedMoveN:
    return "__uninitialized_move_n";
  default:
    return {};
  }
}

const CXXConstructorDecl *approvedUtilityMemorySourceConstructor(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    UtilityOperation Operation, const ASTContext &Context) {
  const auto HelperName = utilityMemorySourceHelper(Operation);
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  const unsigned OutputIndex =
      Operation == UtilityOperation::MemoryUninitializedFill ||
              Operation == UtilityOperation::MemoryUninitializedFillN
          ? 0
          : 2;
  if (HelperName.empty() || !Function || OutputIndex >= Call->getNumArgs())
    return nullptr;
  const auto OutputType = Call->getArg(OutputIndex)->getType();
  if (!utilityObjectPointer(Context, OutputType) ||
      OutputType->getPointeeType().isConstQualified())
    return nullptr;
  const auto Element = OutputType->getPointeeType().getUnqualifiedType();
  if (utilityMemoryTrivialValue(S, SM, Context, Element))
    return nullptr;
  const auto *Record = definedRecord(Element);
  if (!Record || Record->isUnion() || Record->isDependentContext() ||
      !S.owns(SM, Record->getLocation()))
    return nullptr;

  // Use the instantiated pinned implementation as the overload-resolution
  // witness. Reconstructing copy-versus-move selection here would diverge for
  // valid copy fallbacks and future exact SDK revisions.
  const FunctionDecl *Helper = nullptr;
  unsigned HelperCalls = 0;
  auto FindHelper = [&](auto &&Self, const Stmt *Node, unsigned Depth) -> void {
    if (!Node || Depth > 64)
      return;
    if (const auto *Nested = dyn_cast<CallExpr>(Node)) {
      const auto *Callee = Nested->getDirectCallee();
      const auto Origin =
          Callee ? S.sdkFile(SM, Callee->getLocation()) : std::nullopt;
      if (Callee && Callee->getName() == HelperName && Origin &&
          Origin->Root == "libcxx" &&
          Origin->Path == "__memory/uninitialized_algorithms.h") {
        ++HelperCalls;
        if (!Helper || Helper->getCanonicalDecl() == Callee->getCanonicalDecl())
          Helper = Callee;
        else
          Helper = nullptr;
      }
    }
    for (const auto *Child : Node->children())
      Self(Self, Child, Depth + 1);
  };
  FindHelper(FindHelper, Function->getBody(), 0);
  if (!Helper || HelperCalls != 1 || !Helper->getPrimaryTemplate() ||
      !Helper->hasBody())
    return nullptr;

  const CXXNewExpr *Allocation = nullptr;
  unsigned Allocations = 0;
  auto FindAllocation = [&](auto &&Self, const Stmt *Node,
                            unsigned Depth) -> void {
    if (!Node || Depth > 64)
      return;
    if (const auto *New = dyn_cast<CXXNewExpr>(Node)) {
      const auto Origin = S.sdkFile(SM, New->getExprLoc());
      if (Origin && Origin->Root == "libcxx" &&
          Origin->Path == "__memory/uninitialized_algorithms.h") {
        ++Allocations;
        Allocation = New;
      }
    }
    for (const auto *Child : Node->children())
      Self(Self, Child, Depth + 1);
  };
  FindAllocation(FindAllocation, Helper->getBody(), 0);
  const auto *AllocationFunction =
      Allocation ? Allocation->getOperatorNew() : nullptr;
  const auto AllocationFunctionOrigin =
      AllocationFunction ? S.sdkFile(SM, AllocationFunction->getLocation())
                         : std::nullopt;
  if (!Allocation || Allocations != 1 || Allocation->isArray() ||
      Allocation->getNumPlacementArgs() != 1 || !AllocationFunction ||
      !AllocationFunctionOrigin || AllocationFunctionOrigin->Root != "libcxx" ||
      AllocationFunctionOrigin->Path != "__new/placement_new_delete.h" ||
      !Context.hasSameUnqualifiedType(Allocation->getAllocatedType(), Element))
    return nullptr;
  const auto *Construction = Allocation->getConstructExpr();
  const auto *Constructor =
      Construction ? Construction->getConstructor() : nullptr;
  const auto *Prototype =
      Constructor ? Constructor->getType()->getAs<FunctionProtoType>()
                  : nullptr;
  if (!Construction || !Constructor || Construction->getNumArgs() != 1 ||
      Constructor->getParent()->getCanonicalDecl() !=
          Record->getCanonicalDecl() ||
      Constructor->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
      !Constructor->isCopyOrMoveConstructor() ||
      Constructor->getNumParams() != 1 || !supportedConstructor(Constructor) ||
      !Prototype || !Prototype->isNothrow() ||
      !S.owns(SM, Constructor->getLocation()))
    return nullptr;
  const auto Parameter = Constructor->getParamDecl(0)->getType();
  if (!Parameter->isReferenceType() ||
      Parameter->getPointeeType().isVolatileQualified() ||
      Parameter->getPointeeType().isRestrictQualified() ||
      !Context.hasSameUnqualifiedType(Parameter->getPointeeType(), Element))
    return nullptr;
  if (Operation != UtilityOperation::MemoryUninitializedMove &&
      Operation != UtilityOperation::MemoryUninitializedMoveN &&
      !Parameter->isLValueReferenceType())
    return nullptr;
  if (Constructor->isTrivial())
    return Constructor;
  const FunctionDecl *Definition = nullptr;
  if (!Constructor->hasBody(Definition) || !Definition ||
      !S.owns(SM, Definition->getLocation()))
    return nullptr;
  return cast<CXXConstructorDecl>(Definition);
}

static bool utilityAlgorithmEqualityPointer(const ASTContext &Context,
                                            QualType Type) {
  if (!utilityAlgorithmScalarPointer(Context, Type))
    return false;
  auto Element = Type->getPointeeType().getUnqualifiedType();
  return (Element->isIntegralOrEnumerationType() &&
          Context.getTypeSize(Element) <= 64) ||
         Element->isSpecificBuiltinType(BuiltinType::Float) ||
         Element->isSpecificBuiltinType(BuiltinType::Double) ||
         (Element->isPointerType() && !Element->isFunctionPointerType()) ||
         Element->isNullPtrType();
}

static bool utilityEnumHasSourceOperator(const State &S,
                                         const SourceManager &SM,
                                         const ASTContext &Context,
                                         QualType Type,
                                         OverloadedOperatorKind Operator) {
  Type = Type.getUnqualifiedType();
  if (!Type->isEnumeralType())
    return false;
  auto Contains = [&](auto &&Self, const DeclContext *Scope) -> bool {
    if (!Scope)
      return false;
    for (const auto *Declaration : Scope->decls()) {
      const bool SourceDeclaration = S.owns(SM, Declaration->getLocation());
      if (const auto *Function = dyn_cast<FunctionDecl>(Declaration);
          Function && Function->isOverloadedOperator() &&
          Function->getOverloadedOperator() == Operator && SourceDeclaration) {
        for (const auto *Parameter : Function->parameters()) {
          auto ParameterType = Parameter->getType();
          if (ParameterType->isReferenceType())
            ParameterType = ParameterType->getPointeeType();
          if (Context.hasSameUnqualifiedType(Type, ParameterType))
            return true;
        }
      }
      if (!SourceDeclaration)
        continue;
      if (const auto *Nested = dyn_cast<DeclContext>(Declaration);
          Nested && Self(Self, Nested))
        return true;
    }
    return false;
  };
  return Contains(Contains, Context.getTranslationUnitDecl());
}

static bool utilityAlgorithmWritableScalarPointer(const ASTContext &Context,
                                                  QualType Type) {
  return utilityAlgorithmScalarPointer(Context, Type) &&
         !Type->getPointeeType().isConstQualified();
}

static bool utilityPointerConversion(const ASTContext &Context, QualType From,
                                     QualType To) {
  if (!utilityObjectPointer(Context, From) ||
      !utilityObjectPointer(Context, To))
    return false;
  const auto Source = From->getPointeeType();
  const auto Destination = To->getPointeeType();
  return Context.hasSameUnqualifiedType(Source, Destination) &&
         (!Source.isConstQualified() || Destination.isConstQualified());
}

std::optional<UtilityDefaultDeleteConstruction>
approvedUtilityDefaultDeleteConstruction(const State &S,
                                         const SourceManager &SM,
                                         const CXXConstructExpr *Construction,
                                         ASTContext &Context) {
  if (!Construction || Construction->isTypeDependent() ||
      Construction->isValueDependent() ||
      Construction->isInstantiationDependent() ||
      Construction->getConstructionKind() != CXXConstructionKind::Complete)
    return std::nullopt;
  const auto *Constructor = Construction->getConstructor();
  const auto Deleter = approvedUtilityDefaultDeleteRecord(
      S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
  const auto *Prototype =
      Constructor ? Constructor->getType()->getAs<FunctionProtoType>()
                  : nullptr;
  if (!Constructor || !Deleter || !Prototype || !Prototype->isNothrow() ||
      Constructor->getParent()->getCanonicalDecl() !=
          Deleter->Record->getCanonicalDecl() ||
      Constructor->isVariadic() ||
      Construction->getNumArgs() != Constructor->getNumParams() ||
      !approvedStandardSDKDeclaration(S, SM, Constructor) ||
      !cstddefOrigin(S, SM, Constructor->getLocation(), "libcxx",
                     "__memory/unique_ptr.h"))
    return std::nullopt;
  if (!Construction->getNumArgs() && Constructor->isDefaultConstructor() &&
      Constructor->isDefaulted())
    return UtilityDefaultDeleteConstruction::Default;
  if (!Construction->getNumArgs())
    return std::nullopt;
  const auto Source = approvedUtilityDefaultDeleteRecord(
      S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(), Context);
  const auto Parameter = Constructor->getParamDecl(0)->getType();
  if (!Source || !Parameter->isReferenceType() ||
      Parameter->getPointeeType().isVolatileQualified() ||
      !Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                      Context.getRecordType(Source->Record)))
    return std::nullopt;
  if (Deleter->Record->getCanonicalDecl() ==
          Source->Record->getCanonicalDecl() &&
      Constructor->isImplicit() && Constructor->isTrivial() &&
      Constructor->isCopyOrMoveConstructor())
    return UtilityDefaultDeleteConstruction::CopyOrMove;
  if (Construction->getNumArgs() != (Deleter->Array ? 2u : 1u))
    return std::nullopt;
  if (Deleter->Array) {
    const auto *Default = dyn_cast<CXXDefaultArgExpr>(Construction->getArg(1));
    const auto Enable = Constructor->getParamDecl(1)->getType();
    const auto *Init = Default ? Default->getExpr() : nullptr;
    if (!Default || !Enable->isPointerType() ||
        !Enable->getPointeeType()->isVoidType() || !Init ||
        Init->isTypeDependent() || Init->isValueDependent() ||
        Init->isInstantiationDependent() ||
        !Context.hasSameType(Default->getType(), Init->getType()) ||
        !Init->isNullPointerConstant(Context,
                                     Expr::NPC_ValueDependentIsNotNull))
      return std::nullopt;
  }
  const auto *Primary = Constructor->getPrimaryTemplate();
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Constructor->getBody());
  if (!Primary || !Body || !Body->body_empty() ||
      Source->Array != Deleter->Array || !Parameter->isLValueReferenceType() ||
      !Parameter->getPointeeType().isConstQualified() ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__memory/unique_ptr.h") ||
      !utilityPointerConversion(Context,
                                Context.getPointerType(Source->ElementType),
                                Context.getPointerType(Deleter->ElementType)))
    return std::nullopt;
  return UtilityDefaultDeleteConstruction::Converting;
}

std::optional<UtilityDefaultDeleteCall>
approvedUtilityDefaultDeleteCall(const State &S, const SourceManager &SM,
                                 const CallExpr *Call,
                                 const ASTContext &Context) {
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call ? Call->getDirectCallee() : nullptr);
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *Member = dyn_cast_or_null<CXXMemberCallExpr>(Call);
  const Expr *Object = nullptr;
  unsigned PointerIndex = 0;
  if (Operator) {
    if (Operator->getOperator() != OO_Call || Operator->getNumArgs() != 2)
      return std::nullopt;
    Object = Operator->getArg(0);
    PointerIndex = 1;
  } else if (Member) {
    Object = Member->getImplicitObjectArgument();
  }
  const auto *Reference = directMethodReference(Call);
  const auto Deleter = approvedUtilityDefaultDeleteRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  const auto *Prototype =
      Method ? Method->getType()->getAs<FunctionProtoType>() : nullptr;
  if (!Call || !Method || !Object || !Reference || !Deleter || !Prototype ||
      !Prototype->isNothrow() || Method->isStatic() || !Method->isConst() ||
      Method->isVariadic() || Method->getOverloadedOperator() != OO_Call ||
      Method->getNumParams() != 1 || !Method->getReturnType()->isVoidType() ||
      !Call->getType()->isVoidType() ||
      Call->getNumArgs() != PointerIndex + 1 || !Method->isInlined() ||
      !Method->hasBody() || !approvedStandardSDKDeclaration(S, SM, Method) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__memory/unique_ptr.h") ||
      !S.owns(SM, Reference->getExprLoc()) ||
      !Context.hasSameUnqualifiedType(Object->getType(),
                                      Context.getRecordType(Deleter->Record)))
    return std::nullopt;
  const auto Pointer = Context.getPointerType(Deleter->ElementType);
  if (!Context.hasSameType(Method->getParamDecl(0)->getType(), Pointer) ||
      !Context.hasSameType(Call->getArg(PointerIndex)->getType(), Pointer))
    return std::nullopt;

  const auto *Deletion =
      utilityDefaultDeleteExpression(S, SM, *Deleter, Context, Method);
  if (!Deletion)
    return std::nullopt;
  return UtilityDefaultDeleteCall{*Deleter, Deletion, Object, PointerIndex};
}

std::optional<UtilityUniquePtrConstruction>
approvedUtilityUniquePtrConstruction(const State &S, const SourceManager &SM,
                                     const CXXConstructExpr *Construction,
                                     const ASTContext &Context) {
  if (!Construction || Construction->isTypeDependent() ||
      Construction->isValueDependent() ||
      Construction->isInstantiationDependent() ||
      Construction->getConstructionKind() != CXXConstructionKind::Complete)
    return std::nullopt;
  const auto *Constructor = Construction->getConstructor();
  const auto Owner = approvedUtilityUniquePtrRecord(
      S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
  const auto *Prototype =
      Constructor ? Constructor->getType()->getAs<FunctionProtoType>()
                  : nullptr;
  if (!Constructor || !Owner || !Prototype || !Prototype->isNothrow() ||
      Constructor->getParent()->getCanonicalDecl() !=
          Owner->Record->getCanonicalDecl() ||
      Constructor->isVariadic() || Constructor->isDeleted() ||
      Construction->getNumArgs() != Constructor->getNumParams() ||
      !Constructor->isInlined() || !Constructor->hasBody() ||
      !approvedStandardSDKDeclaration(S, SM, Constructor) ||
      !cstddefOrigin(S, SM, Constructor->getLocation(), "libcxx",
                     "__memory/unique_ptr.h"))
    return std::nullopt;
  if (!Construction->getNumArgs())
    return Constructor->isDefaultConstructor()
               ? std::optional(UtilityUniquePtrConstruction::Default)
               : std::nullopt;
  if (Owner->Deleter.Array && Construction->getNumArgs() == 3) {
    const auto *Primary = Constructor->getPrimaryTemplate();
    const auto *Arguments = Constructor->getTemplateSpecializationArgs();
    const auto *Body = dyn_cast_or_null<CompoundStmt>(Constructor->getBody());
    const auto *TagConstruction = dyn_cast<CXXConstructExpr>(
        Construction->getArg(0)->IgnoreParenImpCasts());
    const auto *Tag = TagConstruction
                          ? TagConstruction->getType()->getAsCXXRecordDecl()
                          : nullptr;
    const auto *TagDefinition = Tag ? Tag->getDefinition() : nullptr;
    if (!Primary || !Arguments || !Body || !Body->body_empty() ||
        Arguments->size() != 3 ||
        Arguments->get(0).getKind() != TemplateArgument::Type ||
        Arguments->get(1).getKind() != TemplateArgument::Type ||
        Arguments->get(2).getKind() != TemplateArgument::Integral ||
        !Arguments->get(2).getIntegralType()->isIntegerType() ||
        !Arguments->get(2).getAsIntegral().isZero() || !TagDefinition ||
        TagDefinition->isInvalidDecl() || TagDefinition->isUnion() ||
        TagDefinition->isDependentContext() ||
        TagDefinition->getName() != "__private_constructor_tag" ||
        !approvedStandardSDKDeclaration(S, SM, Primary) ||
        !approvedStandardSDKDeclaration(S, SM, TagDefinition) ||
        !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                       "__memory/unique_ptr.h") ||
        !cstddefOrigin(S, SM, TagDefinition->getLocation(), "libcxx",
                       "__utility/private_constructor_tag.h") ||
        !TagDefinition->field_empty() || TagDefinition->getNumBases() ||
        TagDefinition->isDynamicClass() || !TagDefinition->isEmpty() ||
        !TagDefinition->isStandardLayout() ||
        !TagDefinition->hasTrivialDestructor() ||
        !TagDefinition->hasTrivialDefaultConstructor() ||
        !Context.hasSameType(Arguments->get(0).getAsType(),
                             Context.getRecordType(TagDefinition)) ||
        !Context.hasSameType(Arguments->get(1).getAsType(),
                             Owner->PointerType) ||
        !Context.hasSameType(Constructor->getParamDecl(0)->getType(),
                             Context.getRecordType(TagDefinition)) ||
        !Context.hasSameType(Constructor->getParamDecl(1)->getType(),
                             Owner->PointerType) ||
        !Context.hasSameType(Constructor->getParamDecl(2)->getType(),
                             Context.getSizeType()) ||
        !Context.hasSameType(Construction->getArg(0)->getType(),
                             Context.getRecordType(TagDefinition)) ||
        !Context.hasSameType(Construction->getArg(1)->getType(),
                             Owner->PointerType) ||
        !Context.hasSameType(Construction->getArg(2)->getType(),
                             Context.getSizeType()))
      return std::nullopt;
    const auto &TagLayout = Context.getASTRecordLayout(TagDefinition);
    if (TagLayout.getSize().getQuantity() != 1 ||
        TagLayout.getAlignment().getQuantity() != 1)
      return std::nullopt;
    return UtilityUniquePtrConstruction::FactoryArray;
  }
  if (Construction->getNumArgs() == 2) {
    const auto *Primary = Constructor->getPrimaryTemplate();
    const auto PointerParameter = Constructor->getParamDecl(0)->getType();
    const auto PointerArgument = Construction->getArg(0)->getType();
    const auto DeleterParameter = Constructor->getParamDecl(1)->getType();
    const auto DeleterArgument = Construction->getArg(1)->getType();
    const auto DeleterType = Context.getRecordType(Owner->Deleter.Record);
    const auto ReferencedDeleter = DeleterParameter->isReferenceType()
                                       ? DeleterParameter->getPointeeType()
                                       : QualType();
    const bool LValueDeleter =
        DeleterParameter->isLValueReferenceType() &&
        ReferencedDeleter.isConstQualified() &&
        Context.hasSameUnqualifiedType(ReferencedDeleter, DeleterType) &&
        Context.hasSameUnqualifiedType(DeleterArgument, DeleterType) &&
        Construction->getArg(1)->isLValue();
    const bool RValueDeleter =
        DeleterParameter->isRValueReferenceType() &&
        Context.hasSameUnqualifiedType(ReferencedDeleter, DeleterType) &&
        Context.hasSameUnqualifiedType(DeleterArgument, DeleterType) &&
        (Construction->getArg(1)->isXValue() ||
         Construction->getArg(1)->isPRValue());
    if (!Primary || (!LValueDeleter && !RValueDeleter) ||
        !approvedStandardSDKDeclaration(S, SM, Primary) ||
        !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                       "__memory/unique_ptr.h"))
      return std::nullopt;
    if (PointerParameter->isNullPtrType() && PointerArgument->isNullPtrType())
      return UtilityUniquePtrConstruction::NullDeleter;
    if ((!Owner->Deleter.Array &&
         Context.hasSameType(PointerParameter, Owner->PointerType) &&
         Context.hasSameType(PointerArgument, Owner->PointerType)) ||
        (Owner->Deleter.Array &&
         Context.hasSameType(PointerParameter, PointerArgument) &&
         utilityPointerConversion(Context, PointerParameter,
                                  Owner->PointerType)))
      return UtilityUniquePtrConstruction::PointerDeleter;
    return std::nullopt;
  }
  if (Construction->getNumArgs() != 1)
    return std::nullopt;
  const auto Parameter = Constructor->getParamDecl(0)->getType();
  const auto Argument = Construction->getArg(0)->getType();
  const auto OwnerType = Context.getRecordType(Owner->Record);
  if (Constructor->isMoveConstructor() && !Constructor->getPrimaryTemplate() &&
      Parameter->isRValueReferenceType() &&
      Context.hasSameType(Parameter->getPointeeType(), OwnerType) &&
      Context.hasSameUnqualifiedType(Argument, OwnerType) &&
      Construction->getArg(0)->isXValue())
    return UtilityUniquePtrConstruction::Move;
  const auto Source = approvedUtilityUniquePtrRecord(
      S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(), Context);
  const auto *Primary = Constructor->getPrimaryTemplate();
  if (Source && Primary &&
      Source->Record->getCanonicalDecl() != Owner->Record->getCanonicalDecl() &&
      Source->Deleter.Array == Owner->Deleter.Array && !Source->CustomDeleter &&
      !Owner->CustomDeleter && Parameter->isRValueReferenceType() &&
      Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                     Context.getRecordType(Source->Record)) &&
      Context.hasSameUnqualifiedType(Argument,
                                     Context.getRecordType(Source->Record)) &&
      Construction->getArg(0)->isXValue() &&
      approvedStandardSDKDeclaration(S, SM, Primary) &&
      cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                    "__memory/unique_ptr.h") &&
      utilityPointerConversion(Context, Source->PointerType,
                               Owner->PointerType))
    return UtilityUniquePtrConstruction::ConvertingMove;
  if (Parameter->isNullPtrType() && Argument->isNullPtrType())
    return UtilityUniquePtrConstruction::Null;
  if ((!Owner->Deleter.Array &&
       Context.hasSameType(Parameter, Owner->PointerType) &&
       Context.hasSameType(Argument, Owner->PointerType)) ||
      (Owner->Deleter.Array && Context.hasSameType(Parameter, Argument) &&
       utilityPointerConversion(Context, Parameter, Owner->PointerType)))
    return UtilityUniquePtrConstruction::Pointer;
  return std::nullopt;
}

std::optional<UtilityUniquePtrCall>
approvedUtilityUniquePtrCall(const State &S, const SourceManager &SM,
                             const CallExpr *Call, const ASTContext &Context) {
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call ? Call->getDirectCallee() : nullptr);
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *Member = dyn_cast_or_null<CXXMemberCallExpr>(Call);
  const Expr *Object = nullptr;
  unsigned ArgumentIndex = 0;
  if (Operator) {
    if (!Operator->getNumArgs())
      return std::nullopt;
    Object = Operator->getArg(0);
    ArgumentIndex = 1;
  } else if (Member) {
    Object = Member->getImplicitObjectArgument();
  }
  const auto *Reference = directMethodReference(Call);
  const auto *MemberReference = dyn_cast_or_null<MemberExpr>(Reference);
  const bool ObjectIsArrow =
      Member && MemberReference && MemberReference->isArrow();
  auto ObjectType = Object ? Object->getType() : QualType();
  if (ObjectIsArrow) {
    if (ObjectType.isNull() || !ObjectType->isPointerType() ||
        ObjectType.getAddressSpace() != LangAS::Default ||
        ObjectType->getPointeeType().getAddressSpace() != LangAS::Default)
      return std::nullopt;
    ObjectType = ObjectType->getPointeeType();
  }
  const auto Owner = approvedUtilityUniquePtrRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  const auto *Prototype =
      Method ? Method->getType()->getAs<FunctionProtoType>() : nullptr;
  const bool ArraySubscript =
      Owner && Owner->Deleter.Array && Method &&
      Method->getOverloadedOperator() == OO_Subscript;
  if (Member && Method && Object && Owner && Prototype &&
      Method->getDeclName().getNameKind() ==
          DeclarationName::CXXConversionFunctionName &&
      Method->getNameAsString() == "operator bool" && Prototype->isNothrow() &&
      !Method->isStatic() && !Method->isVariadic() && Method->isConst() &&
      !Method->getNumParams() && !Call->getNumArgs() && Method->isInlined() &&
      Method->hasBody() && approvedStandardSDKDeclaration(S, SM, Method) &&
      cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                    "__memory/unique_ptr.h") &&
      S.owns(SM, Call->getExprLoc()) &&
      Context.hasSameUnqualifiedType(ObjectType,
                                     Context.getRecordType(Owner->Record)) &&
      Context.hasSameType(Call->getType(), Context.BoolTy) && Call->isPRValue())
    return UtilityUniquePtrCall{
        *Owner,       UtilityUniquePtrOperation::Boolean,
        Object,       0,
        std::nullopt, ObjectIsArrow};
  if (!Call || !Method || !Object || !Reference || !Owner || !Prototype ||
      (!Prototype->isNothrow() && !ArraySubscript) || Method->isStatic() ||
      Method->isVariadic() ||
      !Method->isInlined() || !Method->hasBody() ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__memory/unique_ptr.h") ||
      !S.owns(SM, Reference->getExprLoc()) ||
      !Context.hasSameUnqualifiedType(ObjectType,
                                      Context.getRecordType(Owner->Record)) ||
      Call->getNumArgs() != Method->getNumParams() + ArgumentIndex)
    return std::nullopt;

  auto ResultIs = [&](QualType Type) {
    return Context.hasSameType(Method->getReturnType(), Type) &&
           Context.hasSameType(Call->getType(), Type);
  };
  const auto OwnerType = Context.getRecordType(Owner->Record);
  const auto AssignmentResult = [&] {
    return Method->getReturnType()->isLValueReferenceType() &&
           Context.hasSameType(Method->getReturnType()->getPointeeType(),
                               OwnerType) &&
           Context.hasSameUnqualifiedType(Call->getType(), OwnerType) &&
           Call->isLValue() && !ObjectType.isConstQualified();
  };
  UtilityUniquePtrOperation Operation;
  if (Method->getOverloadedOperator() == OO_Equal &&
      Method->isMoveAssignmentOperator() && !Method->getPrimaryTemplate() &&
      Method->getNumParams() == 1 && AssignmentResult() &&
      Method->getParamDecl(0)->getType()->isRValueReferenceType() &&
      Context.hasSameType(Method->getParamDecl(0)->getType()->getPointeeType(),
                          OwnerType) &&
      Context.hasSameUnqualifiedType(Call->getArg(ArgumentIndex)->getType(),
                                     OwnerType) &&
      Call->getArg(ArgumentIndex)->isXValue()) {
    Operation = UtilityUniquePtrOperation::MoveAssign;
  } else if (Method->getOverloadedOperator() == OO_Equal &&
             Method->getPrimaryTemplate() && Method->getNumParams() == 1 &&
             AssignmentResult()) {
    const auto Source = approvedUtilityUniquePtrRecord(
        S, SM, Call->getArg(ArgumentIndex)->getType()->getAsCXXRecordDecl(),
        Context);
    const auto *Primary = Method->getPrimaryTemplate();
    const auto Parameter = Method->getParamDecl(0)->getType();
    if (!Source || !Primary ||
        Source->Record->getCanonicalDecl() ==
            Owner->Record->getCanonicalDecl() ||
        !Parameter->isRValueReferenceType() ||
        !Context.hasSameUnqualifiedType(
            Parameter->getPointeeType(),
            Context.getRecordType(Source->Record)) ||
        !Context.hasSameUnqualifiedType(
            Call->getArg(ArgumentIndex)->getType(),
            Context.getRecordType(Source->Record)) ||
        !Call->getArg(ArgumentIndex)->isXValue() ||
        !approvedStandardSDKDeclaration(S, SM, Primary) ||
        !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                       "__memory/unique_ptr.h") ||
        Source->Deleter.Array != Owner->Deleter.Array ||
        Source->CustomDeleter || Owner->CustomDeleter ||
        !utilityPointerConversion(Context, Source->PointerType,
                                  Owner->PointerType))
      return std::nullopt;
    Operation = UtilityUniquePtrOperation::ConvertingMoveAssign;
    return UtilityUniquePtrCall{*Owner,        Operation, Object,
                                ArgumentIndex, *Source,   ObjectIsArrow};
  } else if (Method->getOverloadedOperator() == OO_Equal &&
             !Method->getPrimaryTemplate() && Method->getNumParams() == 1 &&
             AssignmentResult() &&
             Method->getParamDecl(0)->getType()->isNullPtrType() &&
             Call->getArg(ArgumentIndex)->getType()->isNullPtrType()) {
    Operation = UtilityUniquePtrOperation::NullAssign;
  } else if (Member && !Operator && Method->getIdentifier() &&
             Method->getName() == "swap" && Method->getNumParams() == 1 &&
             !Method->isConst() && !ObjectType.isConstQualified() &&
             Method->getReturnType()->isVoidType() &&
             Call->getType()->isVoidType() &&
             Method->getParamDecl(0)->getType()->isLValueReferenceType() &&
             !Method->getParamDecl(0)
                  ->getType()
                  ->getPointeeType()
                  .isConstQualified() &&
             Context.hasSameType(
                 Method->getParamDecl(0)->getType()->getPointeeType(),
                 OwnerType) &&
             Call->getArg(ArgumentIndex)->isLValue() &&
             !Call->getArg(ArgumentIndex)->getType().isConstQualified() &&
             Context.hasSameUnqualifiedType(
                 Call->getArg(ArgumentIndex)->getType(), OwnerType)) {
    Operation = UtilityUniquePtrOperation::Swap;
  } else if (Method->getOverloadedOperator() == OO_Arrow &&
             !Method->getNumParams() && Method->isConst() &&
             ResultIs(Owner->PointerType) && Call->isPRValue()) {
    Operation = UtilityUniquePtrOperation::Arrow;
  } else if (Method->getOverloadedOperator() == OO_Star &&
             !Method->getNumParams() && Method->isConst() &&
             Method->getReturnType()->isLValueReferenceType() &&
             Context.hasSameType(Method->getReturnType()->getPointeeType(),
                                 Owner->ElementType) &&
             Context.hasSameType(Call->getType(), Owner->ElementType) &&
             Call->isLValue()) {
    Operation = UtilityUniquePtrOperation::Dereference;
  } else if (Owner->Deleter.Array &&
             Method->getOverloadedOperator() == OO_Subscript &&
             Method->getNumParams() == 1 && Method->isConst() &&
             Context.hasSameType(Method->getParamDecl(0)->getType(),
                                 Context.getSizeType()) &&
             Context.hasSameType(Call->getArg(ArgumentIndex)->getType(),
                                 Context.getSizeType()) &&
             Method->getReturnType()->isLValueReferenceType() &&
             Context.hasSameType(Method->getReturnType()->getPointeeType(),
                                 Owner->ElementType) &&
             Context.hasSameType(Call->getType(), Owner->ElementType) &&
             Call->isLValue()) {
    Operation = UtilityUniquePtrOperation::Subscript;
  } else if (Method->getIdentifier() && Method->getName() == "get" &&
             !Method->getNumParams() && Method->isConst() &&
             ResultIs(Owner->PointerType) && Call->isPRValue()) {
    Operation = UtilityUniquePtrOperation::Get;
  } else if (Member && !Operator && Method->getIdentifier() &&
             Method->getName() == "get_deleter" && !Method->getNumParams() &&
             Method->getReturnType()->isLValueReferenceType() &&
             !Method->getReturnType()->getPointeeType().isVolatileQualified() &&
             Method->isConst() ==
                 Method->getReturnType()->getPointeeType().isConstQualified() &&
             (Method->isConst() || !ObjectType.isConstQualified()) &&
             Context.hasSameUnqualifiedType(
                 Method->getReturnType()->getPointeeType(),
                 Context.getRecordType(Owner->Deleter.Record)) &&
             Context.hasSameType(Call->getType(),
                                 Method->getReturnType()->getPointeeType()) &&
             Call->isLValue()) {
    Operation = UtilityUniquePtrOperation::GetDeleter;
  } else if (Method->getIdentifier() && Method->getName() == "release" &&
             !Method->getNumParams() && !Method->isConst() &&
             !ObjectType.isConstQualified() && ResultIs(Owner->PointerType) &&
             Call->isPRValue()) {
    Operation = UtilityUniquePtrOperation::Release;
  } else if (Method->getIdentifier() && Method->getName() == "reset" &&
             Method->getNumParams() == 1 && !Method->isConst() &&
             !ObjectType.isConstQualified() &&
             Method->getReturnType()->isVoidType() &&
             Call->getType()->isVoidType() &&
             ((!Owner->Deleter.Array &&
               Context.hasSameType(Method->getParamDecl(0)->getType(),
                                   Owner->PointerType) &&
               Context.hasSameType(Call->getArg(ArgumentIndex)->getType(),
                                   Owner->PointerType)) ||
              (Owner->Deleter.Array &&
               ((Method->getParamDecl(0)->getType()->isNullPtrType() &&
                 Call->getArg(ArgumentIndex)->getType()->isNullPtrType()) ||
                (Context.hasSameType(Method->getParamDecl(0)->getType(),
                                     Call->getArg(ArgumentIndex)->getType()) &&
                 utilityPointerConversion(
                     Context, Method->getParamDecl(0)->getType(),
                     Owner->PointerType)))))) {
    Operation = UtilityUniquePtrOperation::Reset;
  } else {
    return std::nullopt;
  }
  return UtilityUniquePtrCall{*Owner,        Operation,    Object,
                              ArgumentIndex, std::nullopt, ObjectIsArrow};
}

bool approvedUtilityUniquePtrDestructor(const State &S, const SourceManager &SM,
                                        const CXXDestructorDecl *Destructor,
                                        const ASTContext &Context) {
  const auto Owner = approvedUtilityUniquePtrRecord(
      S, SM, Destructor ? Destructor->getParent() : nullptr, Context);
  const auto *Prototype =
      Destructor ? Destructor->getType()->getAs<FunctionProtoType>() : nullptr;
  const auto *Body = dyn_cast_or_null<CompoundStmt>(
      Destructor ? Destructor->getBody() : nullptr);
  if (!Destructor || !Owner || !Prototype || !Prototype->isNothrow() ||
      Destructor->getParent()->getCanonicalDecl() !=
          Owner->Record->getCanonicalDecl() ||
      Destructor->isImplicit() || Destructor->isVirtual() ||
      Destructor->isDeleted() || Destructor->getNumParams() ||
      !Destructor->isInlined() || !Body || Body->size() != 1 ||
      !approvedStandardSDKDeclaration(S, SM, Destructor) ||
      !cstddefOrigin(S, SM, Destructor->getLocation(), "libcxx",
                     "__memory/unique_ptr.h"))
    return false;
  const auto *Reset = dyn_cast<CXXMemberCallExpr>(*Body->body_begin());
  const auto *Method = Reset ? Reset->getMethodDecl() : nullptr;
  const auto *Object = Reset ? Reset->getImplicitObjectArgument() : nullptr;
  const auto *This =
      Object ? dyn_cast<CXXThisExpr>(Object->IgnoreParenImpCasts()) : nullptr;
  return Reset && Method && This && Reset->getNumArgs() == 1 &&
         isa<CXXDefaultArgExpr>(Reset->getArg(0)) && Method->getIdentifier() &&
         Method->getName() == "reset" && !Method->isConst() &&
         Method->getParent()->getCanonicalDecl() ==
             Owner->Record->getCanonicalDecl() &&
         Method->getNumParams() == 1 &&
         ((Owner->Deleter.Array &&
           Method->getParamDecl(0)->getType()->isNullPtrType()) ||
          (!Owner->Deleter.Array &&
           Context.hasSameType(Method->getParamDecl(0)->getType(),
                               Owner->PointerType))) &&
         Method->getReturnType()->isVoidType() &&
         approvedStandardSDKDeclaration(S, SM, Method) &&
         cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                       "__memory/unique_ptr.h");
}

bool approvedUtilityReverseIteratorMetadata(const State &S,
                                            const SourceManager &SM,
                                            const CXXRecordDecl *Record) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  const auto *Template =
      Specialization ? Specialization->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  if (!Specialization || !Template || !CanonicalTemplate ||
      Specialization->isUnion() || Specialization->isDependentContext() ||
      Specialization->getName() != "reverse_iterator" ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                     "__iterator/reverse_iterator.h") ||
      !cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                     "__iterator/reverse_iterator.h"))
    return false;
  const auto &Arguments = Specialization->getTemplateArgs();
  return Arguments.size() == 1 &&
         Arguments.get(0).getKind() == TemplateArgument::Type;
}

std::optional<UtilityReverseIteratorRecord>
approvedUtilityReverseIteratorRecord(const State &S, const SourceManager &SM,
                                     const CXXRecordDecl *Record,
                                     const ASTContext &Context) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  Specialization = Specialization
                       ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                             Specialization->getDefinition())
                       : nullptr;
  const auto *Template =
      Specialization ? Specialization->getSpecializedTemplate() : nullptr;
  const auto *CanonicalTemplate =
      Template ? Template->getCanonicalDecl() : nullptr;
  if (!approvedUtilityReverseIteratorMetadata(S, SM, Specialization) ||
      !Specialization || !Template || !CanonicalTemplate ||
      Specialization->getSpecializationKind() != TSK_ImplicitInstantiation ||
      Specialization->getNumBases() != 1 || Specialization->getNumVBases() ||
      Specialization->isDynamicClass() ||
      !Specialization->hasTrivialCopyConstructor() ||
      !Specialization->hasTrivialCopyAssignment() ||
      !Specialization->hasTrivialDestructor() ||
      !approvedStandardSDKDeclaration(S, SM, Specialization) ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) ||
      !cstddefOrigin(S, SM, Specialization->getLocation(), "libcxx",
                     "__iterator/reverse_iterator.h") ||
      !cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                     "__iterator/reverse_iterator.h") ||
      !cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                     "__iterator/reverse_iterator.h"))
    return std::nullopt;
  const auto &Arguments = Specialization->getTemplateArgs();
  const auto Iterator = Arguments.get(0).getAsType();
  if (!utilityObjectPointer(Context, Iterator))
    return std::nullopt;

  auto Fields = Specialization->fields();
  auto It = Fields.begin();
  const auto *Legacy = It == Fields.end() ? nullptr : *It++;
  const auto *Current = It == Fields.end() ? nullptr : *It++;
  if (!Legacy || !Current || It != Fields.end() ||
      Legacy->getName() != "__t_" || Current->getName() != "current" ||
      Legacy->getAccess() != AS_private ||
      Current->getAccess() != AS_protected || Legacy->isBitField() ||
      Current->isBitField() || Legacy->isMutable() || Current->isMutable() ||
      Legacy->hasAttrs() || Current->hasAttrs() ||
      !Context.hasSameType(Legacy->getType(), Iterator) ||
      !Context.hasSameType(Current->getType(), Iterator) ||
      !approvedStandardSDKDeclaration(S, SM, Legacy) ||
      !approvedStandardSDKDeclaration(S, SM, Current) ||
      !cstddefOrigin(S, SM, Legacy->getLocation(), "libcxx",
                     "__iterator/reverse_iterator.h") ||
      !cstddefOrigin(S, SM, Current->getLocation(), "libcxx",
                     "__iterator/reverse_iterator.h"))
    return std::nullopt;

  const auto &Base = *Specialization->bases_begin();
  const auto *BaseSpecialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(
          Base.getType()->getAsCXXRecordDecl());
  BaseSpecialization = BaseSpecialization
                           ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                                 BaseSpecialization->getDefinition())
                           : nullptr;
  const auto *BaseTemplate = BaseSpecialization
                                 ? BaseSpecialization->getSpecializedTemplate()
                                 : nullptr;
  const auto *CanonicalBaseTemplate =
      BaseTemplate ? BaseTemplate->getCanonicalDecl() : nullptr;
  if (Base.isVirtual() || Base.isPackExpansion() ||
      Base.getAccessSpecifier() != AS_public || !Base.getTypeSourceInfo() ||
      !BaseSpecialization || !BaseTemplate || !CanonicalBaseTemplate ||
      BaseSpecialization->getName() != "iterator" ||
      BaseSpecialization->getNumBases() || !BaseSpecialization->field_empty() ||
      !BaseSpecialization->isStandardLayout() ||
      !BaseSpecialization->hasTrivialDestructor() ||
      BaseSpecialization->getTemplateArgs().size() != 5 ||
      !approvedStandardSDKDeclaration(S, SM, BaseSpecialization) ||
      !approvedStandardSDKDeclaration(S, SM, BaseTemplate) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalBaseTemplate) ||
      !cstddefOrigin(S, SM, Base.getBeginLoc(), "libcxx",
                     "__iterator/reverse_iterator.h") ||
      !cstddefOrigin(S, SM, BaseSpecialization->getLocation(), "libcxx",
                     "__iterator/iterator.h") ||
      !cstddefOrigin(S, SM, BaseTemplate->getLocation(), "libcxx",
                     "__iterator/iterator.h") ||
      !cstddefOrigin(S, SM, CanonicalBaseTemplate->getLocation(), "libcxx",
                     "__iterator/iterator.h"))
    return std::nullopt;
  for (const auto &Argument : BaseSpecialization->getTemplateArgs().asArray())
    if (Argument.getKind() != TemplateArgument::Type)
      return std::nullopt;

  const auto &BaseLayout = Context.getASTRecordLayout(BaseSpecialization);
  const auto &Layout = Context.getASTRecordLayout(Specialization);
  const uint64_t PointerBits = Context.getTypeSize(Iterator);
  if (BaseLayout.getSize().getQuantity() != 1 ||
      BaseLayout.getAlignment().getQuantity() != 1 ||
      Layout.getFieldCount() != 2 || Layout.getFieldOffset(0) != 0 ||
      Layout.getFieldOffset(1) != PointerBits ||
      Layout.getBaseClassOffset(BaseSpecialization).getQuantity() != 0 ||
      uint64_t(Layout.getSize().getQuantity()) * 8 != PointerBits * 2 ||
      uint64_t(Layout.getAlignment().getQuantity()) * 8 !=
          Context.getTypeAlign(Iterator))
    return std::nullopt;
  return UtilityReverseIteratorRecord{Specialization, Legacy, Current,
                                      Iterator};
}

std::optional<UtilityReverseIteratorConstruction>
approvedUtilityReverseIteratorConstruction(const State &S,
                                           const SourceManager &SM,
                                           const CXXConstructExpr *Construction,
                                           const ASTContext &Context) {
  if (!Construction || Construction->isTypeDependent() ||
      Construction->isValueDependent() ||
      Construction->isInstantiationDependent() ||
      Construction->getConstructionKind() != CXXConstructionKind::Complete)
    return std::nullopt;
  const auto *Constructor = Construction->getConstructor();
  const auto Destination = approvedUtilityReverseIteratorRecord(
      S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
  if (!Constructor || !Destination || Constructor->isVariadic() ||
      Constructor->getParent()->getCanonicalDecl() !=
          Destination->Record->getCanonicalDecl() ||
      Construction->getNumArgs() != Constructor->getNumParams())
    return std::nullopt;
  if (Constructor->isImplicit() && Constructor->isTrivial() &&
      Constructor->isCopyOrMoveConstructor() &&
      Construction->getNumArgs() == 1 &&
      Context.hasSameUnqualifiedType(Construction->getArg(0)->getType(),
                                     Construction->getType()))
    return UtilityReverseIteratorConstruction::CopyOrMove;
  if (!approvedStandardSDKDeclaration(S, SM, Constructor) ||
      !cstddefOrigin(S, SM, Constructor->getLocation(), "libcxx",
                     "__iterator/reverse_iterator.h") ||
      !Constructor->hasBody())
    return std::nullopt;
  if (!Construction->getNumArgs() && Constructor->isDefaultConstructor())
    return UtilityReverseIteratorConstruction::Default;
  if (Construction->getNumArgs() != 1)
    return std::nullopt;
  if (!Constructor->getPrimaryTemplate()) {
    return Context.hasSameType(Constructor->getParamDecl(0)->getType(),
                               Destination->IteratorType) &&
                   Context.hasSameType(Construction->getArg(0)->getType(),
                                       Destination->IteratorType)
               ? std::optional(UtilityReverseIteratorConstruction::Iterator)
               : std::nullopt;
  }
  const auto *Primary = Constructor->getPrimaryTemplate();
  auto Parameter = Constructor->getParamDecl(0)->getType();
  const auto Source = approvedUtilityReverseIteratorRecord(
      S, SM, Construction->getArg(0)->getType()->getAsCXXRecordDecl(), Context);
  if (!Primary || !Source || !Parameter->isLValueReferenceType() ||
      !Parameter->getPointeeType().isConstQualified() ||
      !Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                      Context.getRecordType(Source->Record)) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__iterator/reverse_iterator.h") ||
      !utilityPointerConversion(Context, Source->IteratorType,
                                Destination->IteratorType))
    return std::nullopt;
  return UtilityReverseIteratorConstruction::Converting;
}

std::optional<UtilityReverseIteratorAssignment>
approvedUtilityReverseIteratorAssignment(const State &S,
                                         const SourceManager &SM,
                                         const CXXOperatorCallExpr *Assignment,
                                         const ASTContext &Context) {
  if (!Assignment || Assignment->isTypeDependent() ||
      Assignment->isValueDependent() ||
      Assignment->isInstantiationDependent() ||
      Assignment->getOperator() != OO_Equal || Assignment->getNumArgs() != 2 ||
      !Assignment->isLValue())
    return std::nullopt;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Assignment->getDirectCallee());
  const auto Destination = approvedUtilityReverseIteratorRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  const auto Source = approvedUtilityReverseIteratorRecord(
      S, SM, Assignment->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
  if (!Method || !Destination || !Source || Method->isStatic() ||
      Method->isVariadic() || Method->getNumParams() != 1 ||
      Method->getOverloadedOperator() != OO_Equal ||
      !Context.hasSameUnqualifiedType(
          Assignment->getArg(0)->getType(),
          Context.getRecordType(Destination->Record)) ||
      !Context.hasSameUnqualifiedType(
          Assignment->getType(), Context.getRecordType(Destination->Record)) ||
      !Method->getReturnType()->isLValueReferenceType() ||
      !Context.hasSameUnqualifiedType(
          Method->getReturnType()->getPointeeType(),
          Context.getRecordType(Destination->Record)))
    return std::nullopt;
  const bool Same = Destination->Record->getCanonicalDecl() ==
                    Source->Record->getCanonicalDecl();
  if (Same && Method->isImplicit() && Method->isTrivial() &&
      (Method->isCopyAssignmentOperator() ||
       Method->isMoveAssignmentOperator()))
    return UtilityReverseIteratorAssignment{*Destination, *Source, false};
  const auto *Primary = Method->getPrimaryTemplate();
  auto Parameter = Method->getParamDecl(0)->getType();
  if (Same || !Primary || !Method->hasBody() ||
      !Parameter->isLValueReferenceType() ||
      !Parameter->getPointeeType().isConstQualified() ||
      !Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                      Context.getRecordType(Source->Record)) ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__iterator/reverse_iterator.h") ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__iterator/reverse_iterator.h") ||
      !utilityPointerConversion(Context, Source->IteratorType,
                                Destination->IteratorType))
    return std::nullopt;
  return UtilityReverseIteratorAssignment{*Destination, *Source, true};
}

static const DeclRefExpr *approvedUtilityReference(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const FunctionDecl *Function) {
  const auto *Reference = Call && Call->getCallee()
                              ? dyn_cast<DeclRefExpr>(
                                    Call->getCallee()->IgnoreParenImpCasts())
                              : nullptr;
  return Reference && Function && Reference->getDecl() == Function &&
                 S.owns(SM, Reference->getExprLoc())
             ? Reference
             : nullptr;
}

std::optional<FunctionalReferenceFactoryCall>
approvedFunctionalReferenceFactoryCall(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  const auto *Primary = Function ? Function->getPrimaryTemplate() : nullptr;
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Reference = approvedUtilityReference(S, SM, Call, Function);
  const auto Result = approvedFunctionalReferenceRecord(
      S, SM, Function ? Function->getReturnType()->getAsCXXRecordDecl()
                      : nullptr,
      Context);
  const bool Ref = Function && Function->getIdentifier() &&
                   Function->getName() == "ref";
  const bool Cref = Function && Function->getIdentifier() &&
                    Function->getName() == "cref";
  if (!Call || Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent() || !Call->isPRValue() || !Function ||
      !Primary || !Pattern || !Reference || !Result || (!Ref && !Cref) ||
      Function->isVariadic() || !Function->isInlined() ||
      !Function->hasBody() || !Pattern->hasBody() ||
      Function->getNumParams() != 1 || Call->getNumArgs() != 1 ||
      !Function->getType()->getAs<FunctionProtoType>() ||
      !Function->getType()->getAs<FunctionProtoType>()->isNothrow() ||
      !Context.hasSameType(Call->getType(), Function->getReturnType()) ||
      !Context.hasSameUnqualifiedType(
          Call->getType(), Context.getRecordType(Result->Record)) ||
      !approvedStandardSDKDeclaration(S, SM, Function) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !approvedStandardSDKDeclaration(S, SM, Pattern) ||
      !cstddefOrigin(S, SM, Function->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h") ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h") ||
      !cstddefOrigin(S, SM, Pattern->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h"))
    return std::nullopt;
  const auto Parameter = Function->getParamDecl(0)->getType();
  const auto Argument = Call->getArg(0)->getType();
  if (Parameter->isLValueReferenceType()) {
    const auto Pointee = Parameter->getPointeeType();
    const auto Expected = Cref && Pointee->isObjectType()
                              ? Pointee.withConst()
                              : Pointee;
    if (Pointee.isVolatileQualified() || !Call->getArg(0)->isLValue() ||
        !Context.hasSameUnqualifiedType(Argument, Pointee) ||
        !Context.hasSameType(Result->ReferentType, Expected))
      return std::nullopt;
    return FunctionalReferenceFactoryCall{*Result, std::nullopt, Cref};
  }

  const auto Source = approvedFunctionalReferenceRecord(
      S, SM, Parameter->getAsCXXRecordDecl(), Context);
  auto Expected = Source ? Source->ReferentType : QualType();
  if (!Expected.isNull() && Cref && Expected->isObjectType())
    Expected = Expected.withConst();
  const auto *Body = dyn_cast<CompoundStmt>(Function->getBody());
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(*Body->body_begin())
                           : nullptr;
  const auto *Construction =
      Return && Return->getRetValue()
          ? dyn_cast<CXXConstructExpr>(
                Return->getRetValue()->IgnoreParenImpCasts())
          : nullptr;
  const auto *Constructor = Construction ? Construction->getConstructor()
                                         : nullptr;
  const auto *ConstructorPrototype =
      Constructor ? Constructor->getType()->getAs<FunctionProtoType>()
                  : nullptr;
  const auto *ReturnedArgument =
      Construction && Construction->getNumArgs() == 1
          ? dyn_cast<DeclRefExpr>(
                Construction->getArg(0)->IgnoreParenImpCasts())
          : nullptr;
  if (!Source || Expected.isNull() || !Construction || !Constructor ||
      !ConstructorPrototype || !ConstructorPrototype->isNothrow() ||
      !ReturnedArgument || Constructor->isVariadic() ||
      Constructor->getNumParams() != 1 ||
      Construction->getConstructionKind() != CXXConstructionKind::Complete ||
      !Context.hasSameType(Parameter, Context.getRecordType(Source->Record)) ||
      !Context.hasSameType(Argument, Parameter) ||
      !Context.hasSameType(Result->ReferentType, Expected) ||
      !Context.hasSameType(Construction->getType(), Call->getType()) ||
      Constructor->getParent()->getCanonicalDecl() !=
          Result->Record->getCanonicalDecl() ||
      ReturnedArgument->getDecl() != Function->getParamDecl(0))
    return std::nullopt;
  if (Constructor->isCopyOrMoveConstructor()) {
    if (!Constructor->isImplicit() || !Constructor->isTrivial())
      return std::nullopt;
  } else {
    const auto *ConstructorPrimary = Constructor->getPrimaryTemplate();
    const auto *ConstructorPattern =
        ConstructorPrimary
            ? dyn_cast<CXXConstructorDecl>(
                  ConstructorPrimary->getTemplatedDecl())
            : nullptr;
    if (!ConstructorPrimary || !ConstructorPattern ||
        !approvedStandardSDKDeclaration(S, SM, Constructor) ||
        !approvedStandardSDKDeclaration(S, SM, ConstructorPrimary) ||
        !approvedStandardSDKDeclaration(S, SM, ConstructorPattern) ||
        !cstddefOrigin(S, SM, Constructor->getLocation(), "libcxx",
                       "__functional/reference_wrapper.h") ||
        !cstddefOrigin(S, SM, ConstructorPrimary->getLocation(), "libcxx",
                       "__functional/reference_wrapper.h") ||
        !cstddefOrigin(S, SM, ConstructorPattern->getLocation(), "libcxx",
                       "__functional/reference_wrapper.h"))
      return std::nullopt;
  }
  return FunctionalReferenceFactoryCall{*Result, *Source, Cref};
}

static std::optional<FunctionalReferenceAccessCall>
approvedFunctionalReferenceAccessCallImpl(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context, bool RequireOwnedReference) {
  const auto *MemberCall = dyn_cast_or_null<CXXMemberCallExpr>(Call);
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call ? Call->getDirectCallee() : nullptr);
  const auto *Reference =
      dyn_cast_or_null<MemberExpr>(directMethodReference(Call));
  const auto *Object =
      MemberCall ? MemberCall->getImplicitObjectArgument() : nullptr;
  const bool ObjectIsArrow = MemberCall && Reference && Reference->isArrow();
  auto ObjectType = Object ? Object->getType() : QualType();
  if (ObjectIsArrow) {
    if (ObjectType.isNull() || !ObjectType->isPointerType() ||
        ObjectType.getAddressSpace() != LangAS::Default ||
        ObjectType->getPointeeType().getAddressSpace() != LangAS::Default)
      return std::nullopt;
    ObjectType = ObjectType->getPointeeType();
  }
  const auto Wrapper = approvedFunctionalReferenceRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  const auto *Prototype =
      Method ? Method->getType()->getAs<FunctionProtoType>() : nullptr;
  const bool Get = Method && Method->getIdentifier() &&
                   Method->getName() == "get";
  const bool Conversion = isa_and_nonnull<CXXConversionDecl>(Method);
  if (!MemberCall || !Method || !Reference || !Object || !Wrapper ||
      !Prototype || !Prototype->isNothrow() || (!Get && !Conversion) ||
      Method->isStatic() || Method->isVariadic() || !Method->isConst() ||
      !Method->isInlined() || !Method->hasBody() || Method->getNumParams() ||
      Call->getNumArgs() || !Call->isLValue() ||
      !Method->getReturnType()->isLValueReferenceType() ||
      !Context.hasSameType(Method->getReturnType()->getPointeeType(),
                           Wrapper->ReferentType) ||
      !Context.hasSameType(Call->getType(), Wrapper->ReferentType) ||
      !Context.hasSameUnqualifiedType(
          ObjectType, Context.getRecordType(Wrapper->Record)) ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h") ||
      (RequireOwnedReference &&
       !(Conversion ? S.owns(SM, Call->getExprLoc())
                    : S.owns(SM, Reference->getExprLoc()))))
    return std::nullopt;
  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(*Body->body_begin())
                           : nullptr;
  const auto *Dereference =
      Return && Return->getRetValue()
          ? dyn_cast<UnaryOperator>(
                Return->getRetValue()->IgnoreParenImpCasts())
          : nullptr;
  const auto *Field =
      Dereference && Dereference->getOpcode() == UO_Deref
          ? dyn_cast<MemberExpr>(
                Dereference->getSubExpr()->IgnoreParenImpCasts())
          : nullptr;
  if (!Field || Field->getMemberDecl() != Wrapper->Pointer)
    return std::nullopt;
  return FunctionalReferenceAccessCall{*Wrapper, Object, ObjectIsArrow};
}

std::optional<FunctionalReferenceAccessCall>
approvedFunctionalReferenceAccessCall(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context) {
  return approvedFunctionalReferenceAccessCallImpl(S, SM, Call, Context, true);
}

static bool utilityConstructorTrailingDefaults(
    const State &S, const SourceManager &SM,
    const CXXConstructExpr *Construction, const CXXConstructorDecl *Constructor,
    unsigned ExplicitArguments, const ASTContext &Context) {
  if (!Construction || !Constructor ||
      Construction->getNumArgs() < ExplicitArguments ||
      Constructor->getNumParams() != Construction->getNumArgs())
    return false;
  for (unsigned I = 0; I < ExplicitArguments; ++I)
    if (isa<CXXDefaultArgExpr>(Construction->getArg(I)))
      return false;
  for (unsigned I = ExplicitArguments; I < Construction->getNumArgs(); ++I) {
    const auto *Default = dyn_cast<CXXDefaultArgExpr>(Construction->getArg(I));
    const auto *Init = selectedDefaultArgument(Default, Context);
    const auto *Parameter = Default ? Default->getParam() : nullptr;
    if (!Default || !Init || !Parameter || Default->hasRewrittenInit() ||
        Parameter != Constructor->getParamDecl(I) ||
        Parameter->getFunctionScopeIndex() != I ||
        !S.owns(SM, Init->getExprLoc()))
      return false;
  }
  return true;
}

std::optional<UtilityMakeUniqueCall>
approvedUtilityMakeUniqueCall(const State &S, const SourceManager &SM,
                              const CallExpr *Call, const ASTContext &Context) {
  if (!Call || Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent() || !Call->isPRValue())
    return std::nullopt;
  const auto *Function = Call->getDirectCallee();
  const auto *Primary = Function ? Function->getPrimaryTemplate() : nullptr;
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Reference = approvedUtilityReference(S, SM, Call, Function);
  const auto *Arguments =
      Function ? Function->getTemplateSpecializationArgs() : nullptr;
  const auto Owner = approvedUtilityUniquePtrRecord(
      S, SM,
      Function ? Function->getReturnType()->getAsCXXRecordDecl() : nullptr,
      Context);
  if (!Function || !Primary || !Pattern || !Reference || !Arguments || !Owner ||
      !Function->getIdentifier() || Function->getName() != "make_unique" ||
      Function->isVariadic() || !Function->isInlined() ||
      !Function->hasBody() || !Pattern->hasBody() ||
      Function->getNumParams() != Call->getNumArgs() ||
      !Context.hasSameType(Call->getType(), Function->getReturnType()) ||
      !Context.hasSameUnqualifiedType(Function->getReturnType(),
                                      Context.getRecordType(Owner->Record)) ||
      !approvedStandardSDKDeclaration(S, SM, Function) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !cstddefOrigin(S, SM, Function->getLocation(), "libcxx",
                     "__memory/unique_ptr.h") ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__memory/unique_ptr.h"))
    return std::nullopt;

  std::optional<uint64_t> ArrayCount;
  if (Owner->Deleter.Array) {
    const auto Specialized =
        Arguments->size() ? Arguments->get(0) : TemplateArgument();
    const auto *Array = Specialized.getKind() == TemplateArgument::Type
                            ? Context.getAsArrayType(Specialized.getAsType())
                            : nullptr;
    const auto Count = Call->getNumArgs() == 1
                           ? Call->getArg(0)->getIntegerConstantExpr(Context)
                           : std::optional<llvm::APSInt>();
    if (Arguments->size() != 2 || !Array || !isa<IncompleteArrayType>(Array) ||
        !Context.hasSameType(Array->getElementType(), Owner->ElementType) ||
        Arguments->get(1).getKind() != TemplateArgument::Integral ||
        !Arguments->get(1).getIntegralType()->isIntegerType() ||
        !Arguments->get(1).getAsIntegral().isZero() ||
        Function->getNumParams() != 1 ||
        !Context.hasSameType(Function->getParamDecl(0)->getType(),
                             Context.getSizeType()) ||
        !Context.hasSameType(Call->getArg(0)->getType(),
                             Context.getSizeType()) ||
        !Count || Count->getLimitedValue(65537) > 65536)
      return std::nullopt;
    ArrayCount = Count->getZExtValue();
  } else {
    // The pinned single-object overload has T, Args..., and its enable-if
    // parameter. Authenticate the concrete specialization so future overloads
    // with the same public name cannot enter this path.
    if (Arguments->size() != 3 ||
        Arguments->get(0).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(0).getAsType(),
                             Owner->ElementType) ||
        Arguments->get(1).getKind() != TemplateArgument::Pack ||
        Arguments->get(1).pack_size() != Call->getNumArgs() ||
        Arguments->get(2).getKind() != TemplateArgument::Integral ||
        !Arguments->get(2).getIntegralType()->isIntegerType() ||
        !Arguments->get(2).getAsIntegral().isZero())
      return std::nullopt;
    unsigned PackIndex = 0;
    for (const auto &Argument : Arguments->get(1).pack_elements()) {
      const auto Parameter = Function->getParamDecl(PackIndex)->getType();
      const auto *Actual = Call->getArg(PackIndex++);
      if (Argument.getKind() != TemplateArgument::Type ||
          !Parameter->isReferenceType() ||
          Parameter->getPointeeType().isVolatileQualified() ||
          Parameter->getPointeeType().isRestrictQualified() ||
          !Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                          Actual->getType()) ||
          (Parameter->isLValueReferenceType() ? !Actual->isLValue()
                                              : Actual->isLValue()))
        return std::nullopt;
      auto Deduced = Argument.getAsType();
      if (Deduced->isLValueReferenceType()) {
        if (!Parameter->isLValueReferenceType() ||
            !Context.hasSameType(Deduced->getPointeeType(),
                                 Parameter->getPointeeType()))
          return std::nullopt;
      } else if (Deduced->isReferenceType() ||
                 !Parameter->isRValueReferenceType() ||
                 !Context.hasSameType(Deduced, Parameter->getPointeeType())) {
        return std::nullopt;
      }
    }
  }

  const CXXNewExpr *Allocation = nullptr;
  const CXXConstructExpr *OwnerConstruction = nullptr;
  unsigned Allocations = 0;
  unsigned OwnerConstructions = 0;
  auto Inspect = [&](auto &&Self, const Stmt *Node, unsigned Depth) -> void {
    if (!Node || Depth > 32)
      return;
    if (const auto *New = dyn_cast<CXXNewExpr>(Node)) {
      ++Allocations;
      Allocation = New;
    }
    if (const auto *Construction = dyn_cast<CXXConstructExpr>(Node))
      if (Context.hasSameUnqualifiedType(
              Construction->getType(), Context.getRecordType(Owner->Record))) {
        ++OwnerConstructions;
        OwnerConstruction = Construction;
      }
    for (const auto *Child : Node->children())
      Self(Self, Child, Depth + 1);
  };
  Inspect(Inspect, Function->getBody(), 0);
  const auto *AllocationFunction =
      Allocation ? Allocation->getOperatorNew() : nullptr;
  if (!Allocation || Allocations != 1 || !OwnerConstruction ||
      OwnerConstructions != 1 ||
      Allocation->isArray() != Owner->Deleter.Array ||
      Allocation->getNumPlacementArgs() || Allocation->passAlignment() ||
      !AllocationFunction || AllocationFunction->isVariadic() ||
      AllocationFunction->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
      AllocationFunction->getNumParams() != 1 ||
      !Context.hasSameType(AllocationFunction->getReturnType(),
                           Context.VoidPtrTy) ||
      !Context.hasSameType(AllocationFunction->getParamDecl(0)->getType(),
                           Context.getSizeType()) ||
      AllocationFunction->getOverloadedOperator() !=
          (Owner->Deleter.Array ? OO_Array_New : OO_New) ||
      (!isa<CXXMethodDecl>(AllocationFunction) &&
       !AllocationFunction->getDeclContext()
            ->getRedeclContext()
            ->isTranslationUnit()) ||
      Context.getTypeAlign(Owner->ElementType) >
          Context.getTargetInfo().getNewAlign() ||
      !Context.hasSameType(Allocation->getAllocatedType(),
                           Owner->ElementType) ||
      !Context.hasSameType(Allocation->getType(), Owner->PointerType) ||
      !cstddefOrigin(S, SM, Allocation->getExprLoc(), "libcxx",
                     "__memory/unique_ptr.h"))
    return std::nullopt;

  if (Owner->Deleter.Array) {
    const auto Bound = Allocation->getArraySize();
    auto IsParameter = [&](const Expr *Expression) {
      const auto *Reference = dyn_cast_or_null<DeclRefExpr>(
          Expression ? Expression->IgnoreParenImpCasts() : nullptr);
      return Reference && Function->getNumParams() == 1 &&
             Reference->getDecl() == Function->getParamDecl(0);
    };
    if (!ArrayCount || !Bound ||
        approvedUtilityUniquePtrConstruction(S, SM, OwnerConstruction,
                                             Context) !=
            UtilityUniquePtrConstruction::FactoryArray ||
        OwnerConstruction->getArg(1)->IgnoreParenImpCasts() != Allocation ||
        !IsParameter(*Bound) || !IsParameter(OwnerConstruction->getArg(2)))
      return std::nullopt;
  } else if (approvedUtilityUniquePtrConstruction(S, SM, OwnerConstruction,
                                                  Context) !=
             UtilityUniquePtrConstruction::Pointer) {
    return std::nullopt;
  }

  const unsigned ArgumentCount = Owner->Deleter.Array ? 0 : Call->getNumArgs();
  const auto ValueElement = Context.getBaseElementType(Owner->ElementType);
  if (utilityScalar(Context, ValueElement)) {
    if (ArgumentCount > 1 || !Allocation->getInitializer())
      return std::nullopt;
    if (ArgumentCount &&
        !utilityScalarDirectConversion(
            Context, Function->getParamDecl(0)->getType()->getPointeeType(),
            ValueElement))
      return std::nullopt;
    return UtilityMakeUniqueCall{*Owner, Allocation, nullptr, nullptr,
                                 ArrayCount};
  }

  const auto *Record = definedRecord(ValueElement.getUnqualifiedType());
  const auto *Construction = Allocation->getConstructExpr();
  const auto *Constructor =
      Construction ? Construction->getConstructor() : nullptr;
  const auto *Prototype =
      Constructor ? Constructor->getType()->getAs<FunctionProtoType>()
                  : nullptr;
  if (!Record || Record->isUnion() || Record->isDependentContext() ||
      !S.owns(SM, Record->getLocation()) || !Construction || !Constructor ||
      Constructor->getParent()->getCanonicalDecl() !=
          Record->getCanonicalDecl() ||
      !utilityConstructorTrailingDefaults(S, SM, Construction, Constructor,
                                          ArgumentCount, Context) ||
      Constructor->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
      !supportedConstructor(Constructor) || !Prototype ||
      !Prototype->isNothrow() || !S.owns(SM, Constructor->getLocation()))
    return std::nullopt;
  for (unsigned I = 0; I < ArgumentCount; ++I) {
    const auto Forwarded =
        Function->getParamDecl(I)->getType()->getPointeeType();
    const auto Parameter = Constructor->getParamDecl(I)->getType();
    if (Parameter->isReferenceType()) {
      if (Parameter->getPointeeType().isVolatileQualified() ||
          Parameter->getPointeeType().isRestrictQualified() ||
          !Context.hasSameUnqualifiedType(Forwarded,
                                          Parameter->getPointeeType()))
        return std::nullopt;
    } else if (utilityScalar(Context, Parameter)) {
      if (!utilityScalarDirectConversion(Context, Forwarded, Parameter))
        return std::nullopt;
    } else if (!Parameter->isRecordType() ||
               !Context.hasSameUnqualifiedType(Forwarded, Parameter) ||
               !utilityMemoryTrivialValue(S, SM, Context, Parameter)) {
      return std::nullopt;
    }
  }
  if (!Constructor->isTrivial()) {
    const FunctionDecl *Definition = nullptr;
    if (!Constructor->hasBody(Definition) || !Definition ||
        !S.owns(SM, Definition->getLocation()))
      return std::nullopt;
    Constructor = cast<CXXConstructorDecl>(Definition);
  }
  return UtilityMakeUniqueCall{*Owner, Allocation, Construction, Constructor,
                               ArrayCount};
}

static bool utilityAllocatorForwardingArguments(
    const CallExpr *Call, const CXXMethodDecl *Method, unsigned Offset,
    const ASTContext &Context) {
  if (!Call || !Method || Call->getNumArgs() != Method->getNumParams() ||
      Offset > Method->getNumParams())
    return false;
  for (unsigned I = Offset; I < Method->getNumParams(); ++I) {
    const auto Parameter = Method->getParamDecl(I)->getType();
    const auto *Argument = Call->getArg(I);
    if (!Parameter->isReferenceType() ||
        Parameter->getPointeeType().isVolatileQualified() ||
        Parameter->getPointeeType().isRestrictQualified() ||
        !Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                        Argument->getType()) ||
        (Parameter->isLValueReferenceType() ? !Argument->isLValue()
                                            : Argument->isLValue()))
      return false;
  }
  return true;
}

static std::optional<UtilityAllocatorConstructCall>
utilityAllocatorMemberConstruct(const State &S, const SourceManager &SM,
                                const CXXMethodDecl *Method,
                                const ASTContext &Context) {
  const auto Allocator = approvedUtilityAllocatorRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  const auto *Primary = Method ? Method->getPrimaryTemplate() : nullptr;
  if (!Method || !Allocator || !Primary || !Method->getIdentifier() ||
      Method->getName() != "construct" || Method->isStatic() ||
      Method->isConst() || Method->isVariadic() || !Method->isInlined() ||
      !Method->hasBody() || Method->getNumParams() < 1 ||
      !Method->getReturnType()->isVoidType() ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__memory/allocator.h") ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__memory/allocator.h"))
    return std::nullopt;

  const auto Pointer = Method->getParamDecl(0)->getType();
  if (!utilityObjectPointer(Context, Pointer) ||
      Pointer->getPointeeType().isConstQualified() ||
      Pointer->getPointeeType().isVolatileQualified() ||
      Pointer->getPointeeType()->isIncompleteType())
    return std::nullopt;
  const auto Element = Pointer->getPointeeType().getUnqualifiedType();

  const CXXNewExpr *Allocation = nullptr;
  unsigned Allocations = 0;
  auto FindAllocation = [&](auto &&Self, const Stmt *Node,
                            unsigned Depth) -> void {
    if (!Node || Depth > 32)
      return;
    if (const auto *New = dyn_cast<CXXNewExpr>(Node)) {
      ++Allocations;
      Allocation = New;
    }
    for (const auto *Child : Node->children())
      Self(Self, Child, Depth + 1);
  };
  FindAllocation(FindAllocation, Method->getBody(), 0);
  const auto *AllocationFunction =
      Allocation ? Allocation->getOperatorNew() : nullptr;
  if (!Allocation || Allocations != 1 || Allocation->isArray() ||
      Allocation->getNumPlacementArgs() != 1 || !AllocationFunction ||
      !cstddefOrigin(S, SM, Allocation->getExprLoc(), "libcxx",
                     "__memory/allocator.h") ||
      !cstddefOrigin(S, SM, AllocationFunction->getLocation(), "libcxx",
                     "__new/placement_new_delete.h") ||
      !Context.hasSameUnqualifiedType(Allocation->getAllocatedType(),
                                      Element))
    return std::nullopt;

  const unsigned ArgumentCount = Method->getNumParams() - 1;
  if (utilityScalar(Context, Element)) {
    if (ArgumentCount > 1 || !Allocation->getInitializer())
      return std::nullopt;
    if (!ArgumentCount) {
      if (!isa<ImplicitValueInitExpr>(Allocation->getInitializer()))
        return std::nullopt;
    } else {
      const auto Forwarded =
          Method->getParamDecl(1)->getType()->getPointeeType();
      if (!utilityScalarDirectConversion(Context, Forwarded, Element))
        return std::nullopt;
    }
    return UtilityAllocatorConstructCall{Element, nullptr, nullptr};
  }

  const auto *Record = definedRecord(Element);
  const auto *Construction = Allocation->getConstructExpr();
  const auto *Constructor =
      Construction ? Construction->getConstructor() : nullptr;
  const auto *Prototype =
      Constructor ? Constructor->getType()->getAs<FunctionProtoType>()
                  : nullptr;
  if (!Record || Record->isUnion() || Record->isDependentContext() ||
      !S.owns(SM, Record->getLocation()) || !Construction || !Constructor ||
      Constructor->getParent()->getCanonicalDecl() !=
          Record->getCanonicalDecl() ||
      !utilityConstructorTrailingDefaults(S, SM, Construction, Constructor,
                                          ArgumentCount, Context) ||
      Constructor->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
      !supportedConstructor(Constructor) || !Prototype ||
      !Prototype->isNothrow() || !S.owns(SM, Constructor->getLocation()))
    return std::nullopt;
  for (unsigned I = 0; I < ArgumentCount; ++I) {
    const auto Forwarded =
        Method->getParamDecl(I + 1)->getType()->getPointeeType();
    const auto Parameter = Constructor->getParamDecl(I)->getType();
    if (Parameter->isReferenceType()) {
      if (Parameter->getPointeeType().isVolatileQualified() ||
          Parameter->getPointeeType().isRestrictQualified() ||
          !Context.hasSameUnqualifiedType(Forwarded,
                                          Parameter->getPointeeType()))
        return std::nullopt;
    } else if (utilityScalar(Context, Parameter)) {
      if (!utilityScalarDirectConversion(Context, Forwarded, Parameter))
        return std::nullopt;
    } else if (!Parameter->isRecordType() ||
               !Context.hasSameUnqualifiedType(Forwarded, Parameter) ||
               !utilityMemoryTrivialValue(S, SM, Context, Parameter)) {
      return std::nullopt;
    }
  }
  if (!Constructor->isTrivial()) {
    const FunctionDecl *Definition = nullptr;
    if (!Constructor->hasBody(Definition) || !Definition ||
        !S.owns(SM, Definition->getLocation()))
      return std::nullopt;
    Constructor = cast<CXXConstructorDecl>(Definition);
  }
  return UtilityAllocatorConstructCall{Element, Construction, Constructor};
}

static bool utilityAllocatorSafeCount(const Expr *Count, QualType Element,
                                      const ASTContext &Context) {
  if (!Count || Count->isTypeDependent() || Count->isValueDependent() ||
      Count->isInstantiationDependent() || Element.isNull() ||
      Element->isVoidType() ||
      Element->isIncompleteType() ||
      !Context.hasSameType(Count->getType(), Context.getSizeType()))
    return false;
  const auto Bits = Context.getTypeSize(Context.getSizeType());
  const auto Bytes = Context.getTypeSizeInChars(Element).getQuantity();
  if (!Bits || !Bytes)
    return false;
  // For one-byte elements every already-converted size_t count is within
  // max_size, so the pinned allocator's length-error branch is unreachable.
  // Runtime lowering still evaluates the complete count expression once.
  if (Bytes == 1)
    return true;
  const auto Maximum = llvm::APInt::getMaxValue(Bits).udiv(
      llvm::APInt(Bits, static_cast<uint64_t>(Bytes)));
  if (auto Value = Count->getIntegerConstantExpr(Context))
    return !Value->isNegative() && Value->extOrTrunc(Bits).ule(Maximum);

  // Inspect only the final implicit conversion to size_t. The immediately
  // preceding unsigned value has a type-wide bound, without tracing earlier
  // casts, promotions, declarations or control flow. Keep the original count
  // expression for source validation and one-time runtime evaluation.
  const auto *Conversion = dyn_cast<ImplicitCastExpr>(Count->IgnoreParens());
  if (!Conversion || Conversion->getCastKind() != CK_IntegralCast ||
      !Context.hasSameType(Conversion->getType(), Context.getSizeType()))
    return false;
  const auto SourceType = Conversion->getSubExpr()->getType();
  if (SourceType.isVolatileQualified() || SourceType.isRestrictQualified() ||
      SourceType.getAddressSpace() != LangAS::Default ||
      !SourceType->getAs<BuiltinType>() ||
      !SourceType->isUnsignedIntegerType())
    return false;
  // getIntWidth models bool's value range as one bit, not its storage width.
  const auto SourceBits = Context.getIntWidth(SourceType);
  if (!SourceBits || SourceBits > 64 || SourceBits > Bits)
    return false;
  return llvm::APInt::getMaxValue(SourceBits).zext(Bits).ule(Maximum);
}

static std::optional<UtilityAllocatorHeapCall>
utilityAllocatorMemberHeap(const State &S, const SourceManager &SM,
                           const CXXMethodDecl *Method,
                           const ASTContext &Context) {
  const auto Allocator = approvedUtilityAllocatorRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  const auto *Prototype =
      Method ? Method->getType()->getAs<FunctionProtoType>() : nullptr;
  if (!Method || !Allocator || !Prototype || !Method->getIdentifier() ||
      Method->isStatic() || Method->isConst() || Method->isVariadic() ||
      !Method->isInlined() || !Method->hasBody() ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__memory/allocator.h") ||
      Allocator->ElementType->isVoidType() ||
      Allocator->ElementType->isIncompleteType())
    return std::nullopt;

  const bool Allocate = Method->getName() == "allocate";
  if (!Allocate && Method->getName() != "deallocate")
    return std::nullopt;
  const auto Pointer = Context.getPointerType(Allocator->ElementType);
  const auto Size = Context.getSizeType();
  if (Allocate) {
    const bool Hint = Method->getNumParams() == 2;
    if ((!Hint && Method->getNumParams() != 1) ||
        !Context.hasSameType(Method->getParamDecl(0)->getType(), Size) ||
        !Context.hasSameType(Method->getReturnType(), Pointer))
      return std::nullopt;
    if (Hint) {
      const auto HintType = Method->getParamDecl(1)->getType();
      if (!HintType->isPointerType() ||
          !HintType->getPointeeType()->isVoidType() ||
          !HintType->getPointeeType().isConstQualified() ||
          HintType->getPointeeType().isVolatileQualified() ||
          HintType->getPointeeType().isRestrictQualified())
        return std::nullopt;
    }
    return UtilityAllocatorHeapCall{*Allocator, true, false, Hint};
  }

  if (Method->getNumParams() != 2 || !Prototype->isNothrow() ||
      !Method->getReturnType()->isVoidType() ||
      !Context.hasSameType(Method->getParamDecl(0)->getType(), Pointer) ||
      !Context.hasSameType(Method->getParamDecl(1)->getType(), Size))
    return std::nullopt;
  return UtilityAllocatorHeapCall{*Allocator, false, false, false};
}

std::optional<UtilityAllocatorHeapCall>
approvedUtilityAllocatorHeapCall(const State &S, const SourceManager &SM,
                                 const CallExpr *Call, bool Traits,
                                 const ASTContext &Context) {
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call ? Call->getDirectCallee() : nullptr);
  if (!Call || !Method)
    return std::nullopt;
  if (!Traits) {
    const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
    const auto *Reference = directMethodReference(Call);
    auto Info = utilityAllocatorMemberHeap(S, SM, Method, Context);
    if (!Member || !Reference || !Info ||
        !S.owns(SM, Reference->getExprLoc()) ||
        Call->getNumArgs() != Method->getNumParams() ||
        !Context.hasSameType(Call->getType(), Method->getReturnType()) ||
        !Context.hasSameUnqualifiedType(
            Member->getImplicitObjectArgument()->getType(),
            Context.getRecordType(Info->Allocator.Record)))
      return std::nullopt;
    for (unsigned I = 0; I < Call->getNumArgs(); ++I)
      if (!Context.hasSameType(Call->getArg(I)->getType(),
                               Method->getParamDecl(I)->getType()))
        return std::nullopt;
    if (Info->Allocate &&
        !utilityAllocatorSafeCount(Call->getArg(0),
                                   Info->Allocator.ElementType, Context))
      return std::nullopt;
    return Info;
  }

  const auto TraitsRecord =
      approvedUtilityAllocatorTraitsRecord(S, SM, Method->getParent(), Context);
  const auto *Prototype = Method->getType()->getAs<FunctionProtoType>();
  const auto *Primary = Method->getPrimaryTemplate();
  if (!TraitsRecord || !Prototype || !Method->isStatic() ||
      !Method->getIdentifier() || Method->isVariadic() ||
      !Method->isInlined() || !Method->hasBody() ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      (Primary && !approvedStandardSDKDeclaration(S, SM, Primary)) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__memory/allocator_traits.h") ||
      (Primary && !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                                 "__memory/allocator_traits.h")) ||
      !approvedUtilityReference(S, SM, Call, Method) ||
      Call->getNumArgs() != Method->getNumParams() ||
      !Context.hasSameType(Call->getType(), Method->getReturnType()))
    return std::nullopt;

  const bool Allocate = Method->getName() == "allocate";
  if (!Allocate && Method->getName() != "deallocate")
    return std::nullopt;
  const unsigned Expected =
      Allocate ? (Method->getNumParams() == 3 ? 3u : 2u) : 3u;
  if (Method->getNumParams() != Expected)
    return std::nullopt;
  const auto AllocatorType =
      Context.getRecordType(TraitsRecord->Allocator.Record);
  const auto AllocatorParameter = Method->getParamDecl(0)->getType();
  if (!AllocatorParameter->isLValueReferenceType() ||
      AllocatorParameter->getPointeeType().isConstQualified() ||
      AllocatorParameter->getPointeeType().isVolatileQualified() ||
      !Call->getArg(0)->isLValue() ||
      !Context.hasSameUnqualifiedType(AllocatorParameter->getPointeeType(),
                                      AllocatorType) ||
      !Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                      AllocatorType))
    return std::nullopt;
  for (unsigned I = 1; I < Call->getNumArgs(); ++I)
    if (!Context.hasSameType(Call->getArg(I)->getType(),
                             Method->getParamDecl(I)->getType()))
      return std::nullopt;
  if (Allocate &&
      !utilityAllocatorSafeCount(
          Call->getArg(1), TraitsRecord->Allocator.ElementType, Context))
    return std::nullopt;

  const CXXMemberCallExpr *Forward = nullptr;
  unsigned Forwards = 0;
  auto FindForward = [&](auto &&Self, const Stmt *Node,
                         unsigned Depth) -> void {
    if (!Node || Depth > 32)
      return;
    if (const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(Node)) {
      const auto *Callee = MemberCall->getDirectCallee();
      if (Callee && Callee->getIdentifier() &&
          Callee->getName() == Method->getName()) {
        ++Forwards;
        Forward = MemberCall;
      }
    }
    for (const auto *Child : Node->children())
      Self(Self, Child, Depth + 1);
  };
  FindForward(FindForward, Method->getBody(), 0);
  const auto *ForwardMethod = dyn_cast_or_null<CXXMethodDecl>(
      Forward ? Forward->getDirectCallee() : nullptr);
  const auto ForwardInfo =
      utilityAllocatorMemberHeap(S, SM, ForwardMethod, Context);
  if (!Forward || Forwards != 1 || !ForwardMethod || !ForwardInfo ||
      ForwardInfo->Allocate != Allocate ||
      Forward->getNumArgs() != ForwardMethod->getNumParams() ||
      Forward->getNumArgs() + 1 != Call->getNumArgs() ||
      ForwardMethod->getNumParams() + 1 != Method->getNumParams() ||
      !Context.hasSameUnqualifiedType(
          Forward->getImplicitObjectArgument()->getType(), AllocatorType))
    return std::nullopt;
  for (unsigned I = 0; I < ForwardMethod->getNumParams(); ++I)
    if (!Context.hasSameType(Method->getParamDecl(I + 1)->getType(),
                             ForwardMethod->getParamDecl(I)->getType()))
      return std::nullopt;
  return UtilityAllocatorHeapCall{TraitsRecord->Allocator, Allocate, true,
                                  Allocate && Method->getNumParams() == 3};
}

std::optional<UtilityAllocatorConstructCall>
approvedUtilityAllocatorConstructCall(const State &S,
                                      const SourceManager &SM,
                                      const CallExpr *Call, bool Traits,
                                      const ASTContext &Context) {
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call ? Call->getDirectCallee() : nullptr);
  if (!Call || !Method || !Call->getType()->isVoidType() ||
      !Context.hasSameType(Call->getType(), Method->getReturnType()))
    return std::nullopt;
  if (!Traits) {
    const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
    const auto *Reference = directMethodReference(Call);
    const auto Info = utilityAllocatorMemberConstruct(S, SM, Method, Context);
    if (!Member || !Reference || !Info ||
        !S.owns(SM, Reference->getExprLoc()) ||
        !utilityAllocatorForwardingArguments(Call, Method, 1, Context) ||
        !Context.hasSameType(Call->getArg(0)->getType(),
                             Method->getParamDecl(0)->getType()) ||
        !Context.hasSameUnqualifiedType(
            Member->getImplicitObjectArgument()->getType(),
            Context.getRecordType(Method->getParent())))
      return std::nullopt;
    return Info;
  }

  const auto TraitsRecord = approvedUtilityAllocatorTraitsRecord(
      S, SM, Method->getParent(), Context);
  const auto *Primary = Method->getPrimaryTemplate();
  if (!TraitsRecord || !Primary || !Method->isStatic() ||
      !Method->getIdentifier() || Method->getName() != "construct" ||
      Method->isVariadic() || !Method->isInlined() || !Method->hasBody() ||
      Method->getNumParams() < 2 ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__memory/allocator_traits.h") ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__memory/allocator_traits.h") ||
      !approvedUtilityReference(S, SM, Call, Method) ||
      !utilityAllocatorForwardingArguments(Call, Method, 2, Context))
    return std::nullopt;
  const auto AllocatorType =
      Context.getRecordType(TraitsRecord->Allocator.Record);
  const auto AllocatorParameter = Method->getParamDecl(0)->getType();
  if (!AllocatorParameter->isLValueReferenceType() ||
      AllocatorParameter->getPointeeType().isConstQualified() ||
      !Call->getArg(0)->isLValue() ||
      !Context.hasSameUnqualifiedType(AllocatorParameter->getPointeeType(),
                                      AllocatorType) ||
      !Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                      AllocatorType) ||
      !Context.hasSameType(Call->getArg(1)->getType(),
                           Method->getParamDecl(1)->getType()))
    return std::nullopt;

  const CXXMemberCallExpr *Forward = nullptr;
  unsigned Forwards = 0;
  auto FindForward = [&](auto &&Self, const Stmt *Node,
                         unsigned Depth) -> void {
    if (!Node || Depth > 32)
      return;
    if (const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(Node)) {
      const auto *Callee = MemberCall->getDirectCallee();
      if (Callee && Callee->getIdentifier() &&
          Callee->getName() == "construct") {
        ++Forwards;
        Forward = MemberCall;
      }
    }
    for (const auto *Child : Node->children())
      Self(Self, Child, Depth + 1);
  };
  FindForward(FindForward, Method->getBody(), 0);
  const auto *ForwardMethod = dyn_cast_or_null<CXXMethodDecl>(
      Forward ? Forward->getDirectCallee() : nullptr);
  const auto Info =
      utilityAllocatorMemberConstruct(S, SM, ForwardMethod, Context);
  if (!Forward || Forwards != 1 || !ForwardMethod || !Info ||
      Forward->getNumArgs() + 1 != Call->getNumArgs() ||
      !Context.hasSameUnqualifiedType(
          Forward->getImplicitObjectArgument()->getType(), AllocatorType) ||
      !Context.hasSameType(Method->getParamDecl(1)->getType(),
                           ForwardMethod->getParamDecl(0)->getType()))
    return std::nullopt;
  for (unsigned I = 2; I < Method->getNumParams(); ++I)
    if (!Context.hasSameType(Method->getParamDecl(I)->getType(),
                             ForwardMethod->getParamDecl(I - 1)->getType()))
      return std::nullopt;
  return Info;
}

static bool utilityComparableValue(const State &S, const SourceManager &SM,
                                   const ASTContext &Context, QualType Left,
                                   QualType Right, bool Ordered,
                                   unsigned Depth = 0) {
  if (Depth > 64 || Left.isNull() || Right.isNull() ||
      Left.isVolatileQualified() || Right.isVolatileQualified())
    return false;
  if (Left->isReferenceType() || Right->isReferenceType()) {
    if (Left->isReferenceType())
      Left = Left->getPointeeType();
    if (Right->isReferenceType())
      Right = Right->getPointeeType();
    return utilityComparableValue(S, SM, Context, Left, Right, Ordered,
                                  Depth + 1);
  }
  if (utilityScalarComparisonType(Context, Left, Right, Ordered))
    return true;

  const auto LeftArray = approvedUtilityArrayRecord(
      S, SM, Left.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  const auto RightArray = approvedUtilityArrayRecord(
      S, SM, Right.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  if (LeftArray || RightArray)
    return LeftArray && RightArray &&
           LeftArray->Record->getCanonicalDecl() ==
               RightArray->Record->getCanonicalDecl() &&
           utilityComparableValue(S, SM, Context, LeftArray->ElementType,
                                  RightArray->ElementType, Ordered, Depth + 1);

  auto LeftPair = approvedUtilityPairRecord(
      S, SM, Left.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  auto RightPair = approvedUtilityPairRecord(
      S, SM, Right.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  if (!LeftPair)
    LeftPair = approvedUtilityReferencePairRecord(
        S, SM, Left.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  if (!RightPair)
    RightPair = approvedUtilityReferencePairRecord(
        S, SM, Right.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  if (!LeftPair)
    LeftPair = approvedUtilityMixedReferencePairRecord(
        S, SM, Left.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  if (!RightPair)
    RightPair = approvedUtilityMixedReferencePairRecord(
        S, SM, Right.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  if (LeftPair || RightPair)
    return LeftPair && RightPair &&
           utilityComparableValue(S, SM, Context, LeftPair->First->getType(),
                                  RightPair->First->getType(), Ordered,
                                  Depth + 1) &&
           utilityComparableValue(S, SM, Context, LeftPair->Second->getType(),
                                  RightPair->Second->getType(), Ordered,
                                  Depth + 1);

  auto LeftTuple = approvedUtilityTupleRecord(
      S, SM, Left.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  auto RightTuple = approvedUtilityTupleRecord(
      S, SM, Right.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  if (!LeftTuple)
    LeftTuple = approvedUtilityReferenceTupleRecord(
        S, SM, Left.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  if (!RightTuple)
    RightTuple = approvedUtilityReferenceTupleRecord(
        S, SM, Right.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  if (!LeftTuple)
    LeftTuple = approvedUtilityMixedReferenceTupleRecord(
        S, SM, Left.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  if (!RightTuple)
    RightTuple = approvedUtilityMixedReferenceTupleRecord(
        S, SM, Right.getUnqualifiedType()->getAsCXXRecordDecl(), Context);
  if (!LeftTuple && !RightTuple)
    return false;
  if (!LeftTuple || !RightTuple ||
      LeftTuple->Elements.size() != RightTuple->Elements.size())
    return false;
  for (unsigned I = 0; I < LeftTuple->Elements.size(); ++I)
    if (!utilityComparableValue(
            S, SM, Context, LeftTuple->Elements[I]->getType(),
            RightTuple->Elements[I]->getType(), Ordered, Depth + 1))
      return false;
  return true;
}

bool approvedUtilityDefaultArgument(const State &S, const SourceManager &SM,
                                    const CXXDefaultArgExpr *Default,
                                    const FunctionDecl *Function,
                                    unsigned Index, ASTContext &Context) {
  if (!S.coreV2())
    return false;
  const auto *Parameter = Default ? Default->getParam() : nullptr;
  const auto *Owner =
      Parameter ? dyn_cast<FunctionDecl>(Parameter->getDeclContext()) : nullptr;
  if (const auto *Constructor =
          dyn_cast_or_null<CXXConstructorDecl>(Function)) {
    const auto Deleter = approvedUtilityDefaultDeleteRecord(
        S, SM, Constructor->getParent(), Context);
    const auto *Primary = Constructor->getPrimaryTemplate();
    const auto *Body = dyn_cast_or_null<CompoundStmt>(Constructor->getBody());
    const auto *Init = Default ? Default->getExpr() : nullptr;
    if (Default && Parameter && Owner && Deleter && Deleter->Array && Primary &&
        Body && Body->body_empty() &&
        Owner->getCanonicalDecl() == Constructor->getCanonicalDecl() &&
        Index == 1 && Constructor->getNumParams() == 2 &&
        Parameter == Constructor->getParamDecl(Index) &&
        Parameter->getFunctionScopeIndex() == Index &&
        !Constructor->isVariadic() && Constructor->isInlined() &&
        approvedStandardSDKDeclaration(S, SM, Constructor) &&
        approvedStandardSDKDeclaration(S, SM, Primary) &&
        cstddefOrigin(S, SM, Constructor->getLocation(), "libcxx",
                      "__memory/unique_ptr.h") &&
        cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                      "__memory/unique_ptr.h") &&
        Parameter->getType()->isPointerType() &&
        Parameter->getType()->getPointeeType()->isVoidType() && Init &&
        !Init->isTypeDependent() && !Init->isValueDependent() &&
        !Init->isInstantiationDependent() &&
        Context.hasSameType(Default->getType(), Init->getType()) &&
        cstddefOrigin(S, SM, Init->getExprLoc(), "libcxx",
                      "__memory/unique_ptr.h") &&
        Init->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNotNull))
      return true;
  }
  if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function)) {
    const auto Unique =
        approvedUtilityUniquePtrRecord(S, SM, Method->getParent(), Context);
    const auto *Prototype = Method->getType()->getAs<FunctionProtoType>();
    const auto *Init = selectedDefaultArgument(Default, Context);
    if (Default && Parameter && Owner && Unique && Prototype &&
        Prototype->isNothrow() &&
        Owner->getCanonicalDecl() == Function->getCanonicalDecl() &&
        Index == 0 && Function->getNumParams() == 1 &&
        Parameter == Function->getParamDecl(0) &&
        Parameter->getFunctionScopeIndex() == 0 && !Function->isVariadic() &&
        Function->isInlined() && Function->hasBody() &&
        Function->getIdentifier() && Function->getName() == "reset" &&
        approvedStandardSDKDeclaration(S, SM, Function) &&
        cstddefOrigin(S, SM, Function->getLocation(), "libcxx",
                      "__memory/unique_ptr.h") &&
        S.owns(SM, Default->getExprLoc()) && Init &&
        ((!Unique->Deleter.Array &&
          Context.hasSameType(Parameter->getType(), Unique->PointerType) &&
          Context.hasSameType(Init->getType(), Unique->PointerType) &&
          isa<CXXScalarValueInitExpr>(Init->IgnoreParenImpCasts())) ||
         (Unique->Deleter.Array && Parameter->getType()->isNullPtrType() &&
          Context.hasSameType(Init->getType(), Parameter->getType()) &&
          Init->isNullPointerConstant(
              Context, Expr::NPC_ValueDependentIsNotNull))))
      return true;
  }
  const auto *Primary = Function ? Function->getPrimaryTemplate() : nullptr;
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  if (!Default || !Parameter || !Owner || !Function || !Primary || !Pattern ||
      Owner->getCanonicalDecl() != Function->getCanonicalDecl() || Index != 1 ||
      Function->getNumParams() != 2 ||
      Parameter != Function->getParamDecl(Index) ||
      Parameter->getFunctionScopeIndex() != Index || Function->isVariadic() ||
      !Function->isInlined() || !Pattern->hasBody() ||
      !approvedStandardSDKDeclaration(S, SM, Function) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !S.owns(SM, Default->getExprLoc()) || !Function->getIdentifier() ||
      Function->getIdentifier()->getName() != "next")
    return false;
  auto Origin = S.sdkFile(SM, Primary->getLocation());
  auto Iterator = Function->getParamDecl(0)->getType();
  auto Distance = Parameter->getType();
  const auto Reverse = approvedUtilityReverseIteratorRecord(
      S, SM, Iterator->getAsCXXRecordDecl(), Context);
  const auto *Init = selectedDefaultArgument(Default, Context);
  if (!Origin || Origin->Root != "libcxx" ||
      Origin->Path != "__iterator/next.h" ||
      (!utilityObjectPointer(Context, Iterator) && !Reverse) ||
      !Distance->isIntegralType(Context) ||
      Context.getTypeSize(Distance) > 64 ||
      !Context.hasSameType(Distance, Context.getPointerDiffType()) ||
      !Context.hasSameType(Function->getReturnType(), Iterator) || !Init ||
      !Context.hasSameType(Init->getType(), Distance) ||
      Init->isTypeDependent() || Init->isValueDependent() ||
      Init->isInstantiationDependent())
    return false;
  Init = Init->IgnoreParenImpCasts();
  while (const auto *Constant = dyn_cast<ConstantExpr>(Init))
    Init = Constant->getSubExpr()->IgnoreParenImpCasts();
  const auto *Literal = dyn_cast<IntegerLiteral>(Init);
  return Literal && Literal->getValue() == 1;
}

std::optional<UtilityTupleLikeSource>
approvedUtilityTupleLikeSource(const State &S, const SourceManager &SM,
                               QualType Type, const ASTContext &Context) {
  if (Type.isNull() || Type.isVolatileQualified() || Type.isRestrictQualified() ||
      Type.getAddressSpace() != LangAS::Default)
    return std::nullopt;
  const auto *Record = Type.getUnqualifiedType()->getAsCXXRecordDecl();
  auto Tuple = approvedUtilityTupleRecord(S, SM, Record, Context);
  bool ReferenceTuple = false;
  if (!Tuple) {
    Tuple = approvedUtilityReferenceTupleRecord(S, SM, Record, Context);
    ReferenceTuple = Tuple.has_value();
  }
  bool MixedReferenceTuple = false;
  if (!Tuple) {
    Tuple = approvedUtilityMixedReferenceTupleRecord(S, SM, Record, Context);
    MixedReferenceTuple = Tuple.has_value();
  }
  if (Tuple) {
    for (const auto *Element : Tuple->Elements)
      if (!ReferenceTuple && !MixedReferenceTuple &&
          !utilityTupleValue(S, SM, Context, Element->getType()))
        return std::nullopt;
    return UtilityTupleLikeSource{Tuple->Elements, nullptr, {}, 0};
  }
  auto Pair = approvedUtilityPairRecord(S, SM, Record, Context);
  bool ReferencePair = false;
  if (!Pair) {
    Pair = approvedUtilityReferencePairRecord(S, SM, Record, Context);
    ReferencePair = Pair.has_value();
  }
  bool MixedReferencePair = false;
  if (!Pair) {
    Pair = approvedUtilityMixedReferencePairRecord(S, SM, Record, Context);
    MixedReferencePair = Pair.has_value();
  }
  if (Pair) {
    if (!ReferencePair && !MixedReferencePair &&
        (!utilityTupleValue(S, SM, Context, Pair->First->getType()) ||
         !utilityTupleValue(S, SM, Context, Pair->Second->getType())))
      return std::nullopt;
    return UtilityTupleLikeSource{{Pair->First, Pair->Second}, nullptr, {}, 0};
  }
  if (const auto Array = approvedUtilityArrayRecord(S, SM, Record, Context)) {
    if (!utilityTupleValue(S, SM, Context, Array->ElementType))
      return std::nullopt;
    return UtilityTupleLikeSource{
        {}, Array->Elements, Array->ElementType, Array->Size};
  }
  return std::nullopt;
}

std::optional<UtilityTupleCatCall>
approvedUtilityTupleCatCall(const State &S, const SourceManager &SM,
                            const CallExpr *Call, const ASTContext &Context) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  const auto *Primary = Function ? Function->getPrimaryTemplate() : nullptr;
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Identifier = Function ? Function->getIdentifier() : nullptr;
  const auto Origin =
      Function ? S.sdkFile(SM, Function->getLocation()) : std::nullopt;
  if (!Call || !Function || !Identifier ||
      Identifier->getName() != "tuple_cat" || !Origin ||
      Origin->Root != "libcxx" || Origin->Path != "tuple" ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent() || !Call->isPRValue() ||
      Function->isVariadic() || !Function->isInlined() ||
      !Function->isConstexpr() || !Function->hasBody() ||
      Function->getReturnType()->isReferenceType() ||
      Call->getNumArgs() != Function->getNumParams() ||
      !Context.hasSameType(Call->getType(), Function->getReturnType()) ||
      !approvedStandardSDKDeclaration(S, SM, Function) ||
      !approvedUtilityReference(S, SM, Call, Function))
    return std::nullopt;

  auto Result = approvedUtilityTupleRecord(
      S, SM, Call->getType()->getAsCXXRecordDecl(), Context);
  if (!Result)
    Result = approvedUtilityReferenceTupleRecord(
        S, SM, Call->getType()->getAsCXXRecordDecl(), Context);
  if (!Result)
    Result = approvedUtilityMixedReferenceTupleRecord(
        S, SM, Call->getType()->getAsCXXRecordDecl(), Context);
  if (!Result)
    return std::nullopt;
  if (!Call->getNumArgs()) {
    if (Primary || !Result->Elements.empty())
      return std::nullopt;
    return UtilityTupleCatCall{*Result, {}};
  }
  const auto PrimaryOrigin =
      Primary ? S.sdkFile(SM, Primary->getLocation()) : std::nullopt;
  if (!Primary || !Pattern || !Pattern->hasBody() || !PrimaryOrigin ||
      PrimaryOrigin->Root != "libcxx" || PrimaryOrigin->Path != "tuple" ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !approvedStandardSDKDeclaration(S, SM, Pattern))
    return std::nullopt;

  UtilityTupleCatCall Approved{*Result, {}};
  Approved.Sources.reserve(Call->getNumArgs());
  unsigned ResultIndex = 0;
  for (unsigned I = 0; I < Call->getNumArgs(); ++I) {
    const auto Parameter = Function->getParamDecl(I)->getType();
    if (!Parameter->isReferenceType() ||
        Parameter->getPointeeType().isVolatileQualified() ||
        !Context.hasSameType(Parameter->getPointeeType(),
                             Call->getArg(I)->getType()))
      return std::nullopt;
    auto Source = approvedUtilityTupleLikeSource(
        S, SM, Call->getArg(I)->getType(), Context);
    if (!Source)
      return std::nullopt;
    const uint64_t SourceSize = Source->size();
    if (ResultIndex > Result->Elements.size() ||
        SourceSize > Result->Elements.size() - ResultIndex)
      return std::nullopt;
    if (Source->ArrayElements) {
      for (uint64_t N = 0; N < Source->ArraySize; ++N)
        if (!Context.hasSameType(Source->ArrayElementType,
                                 Result->Elements[ResultIndex++]->getType()))
          return std::nullopt;
    } else {
      for (const auto *Element : Source->Elements)
        if (!Context.hasSameType(Element->getType(),
                                 Result->Elements[ResultIndex++]->getType()))
          return std::nullopt;
    }
    Approved.Sources.push_back(std::move(*Source));
  }
  if (ResultIndex != Result->Elements.size())
    return std::nullopt;
  return Approved;
}

static const Expr *functionalInvokeStrippedExpression(
    const Expr *Expression);

static const CallExpr *approvedFunctionalInvokeDispatch(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context, bool ReferenceResult = false) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  const auto *Primary = Function ? Function->getPrimaryTemplate() : nullptr;
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto Result = Function ? Function->getReturnType() : QualType();
  const bool MatchingValueCategory =
      ReferenceResult
          ? (!Result.isNull() && Result->isLValueReferenceType()
                 ? Call && Call->isLValue()
                 : !Result.isNull() && Result->isRValueReferenceType() &&
                       Call && Call->isXValue())
          : Call && Call->isPRValue();
  const auto OuterOrigin =
      Primary ? S.sdkFile(SM, Primary->getLocation()) : std::nullopt;
  if (!Call || Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent() || !Function || !Primary || !Pattern ||
      !Function->getIdentifier() || Function->getName() != "invoke" ||
      !OuterOrigin || OuterOrigin->Root != "libcxx" ||
      OuterOrigin->Path != "__functional/invoke.h" || Function->isVariadic() ||
      !Function->hasBody() || !Pattern->hasBody() ||
      !MatchingValueCategory ||
      Call->getNumArgs() < 1 ||
      Call->getNumArgs() != Function->getNumParams() ||
      (ReferenceResult
           ? (!Result->isReferenceType() ||
              !Context.hasSameType(
                  Call->getType(), Result->getPointeeType()))
           : !Context.hasSameType(Call->getType(),
                                  Result)) ||
      !approvedStandardSDKDeclaration(S, SM, Function) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !approvedStandardSDKDeclaration(S, SM, Pattern) ||
      !approvedUtilityReference(S, SM, Call, Function))
    return nullptr;
  for (unsigned I = 0; I < Call->getNumArgs(); ++I) {
    const auto Parameter = Function->getParamDecl(I)->getType();
    if (!Parameter->isReferenceType() ||
        Parameter->getPointeeType().isVolatileQualified() ||
        !Context.hasSameType(Parameter->getPointeeType(),
                             Call->getArg(I)->getType()))
      return nullptr;
  }

  const auto *OuterBody = dyn_cast<CompoundStmt>(Function->getBody());
  const auto *OuterReturn =
      OuterBody && OuterBody->size() == 1
          ? dyn_cast<ReturnStmt>(*OuterBody->body_begin())
          : nullptr;
  const auto *Dispatch =
      OuterReturn && OuterReturn->getRetValue()
          ? dyn_cast_or_null<CallExpr>(functionalInvokeStrippedExpression(
                OuterReturn->getRetValue()))
          : nullptr;
  const auto *DispatchFunction =
      Dispatch ? Dispatch->getDirectCallee() : nullptr;
  const auto *DispatchPrimary =
      DispatchFunction ? DispatchFunction->getPrimaryTemplate() : nullptr;
  const auto *DispatchPattern =
      DispatchPrimary ? DispatchPrimary->getTemplatedDecl() : nullptr;
  const auto DispatchOrigin = DispatchPrimary
                                  ? S.sdkFile(SM, DispatchPrimary->getLocation())
                                  : std::nullopt;
  const auto *DispatchReference =
      Dispatch ? dyn_cast_or_null<DeclRefExpr>(directFunctionReference(Dispatch))
               : nullptr;
  if (!Dispatch || !DispatchFunction || !DispatchPrimary || !DispatchPattern ||
      !DispatchReference || !DispatchFunction->getIdentifier() ||
      DispatchFunction->getName() != "__invoke" || !DispatchOrigin ||
      DispatchOrigin->Root != "libcxx" ||
      DispatchOrigin->Path != "__type_traits/invoke.h" ||
      DispatchFunction->isVariadic() || !DispatchFunction->isInlined() ||
      !DispatchFunction->isConstexpr() || !DispatchFunction->hasBody() ||
      !DispatchPattern->hasBody() ||
      Dispatch->getNumArgs() != DispatchFunction->getNumParams() ||
      Dispatch->getNumArgs() != Call->getNumArgs() ||
      !Context.hasSameType(Dispatch->getType(), Call->getType()) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchFunction) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchPrimary) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchPattern) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchReference->getDecl()))
    return nullptr;
  return Dispatch;
}

static const Expr *functionalInvokeStrippedExpression(const Expr *Expression) {
  while (Expression) {
    if (const auto *Parentheses = dyn_cast<ParenExpr>(Expression)) {
      Expression = Parentheses->getSubExpr();
      continue;
    }
    if (const auto *Cleanup = dyn_cast<ExprWithCleanups>(Expression)) {
      Expression = Cleanup->getSubExpr();
      continue;
    }
    if (const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(Expression)) {
      Expression = Temporary->getSubExpr();
      continue;
    }
    if (const auto *Bound = dyn_cast<CXXBindTemporaryExpr>(Expression)) {
      Expression = Bound->getSubExpr();
      continue;
    }
    if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Expression)) {
      Expression = Cast->getSubExpr();
      continue;
    }
    break;
  }
  return Expression;
}

static bool supportedFunctionalMemberValue(const ASTContext &Context,
                                           QualType Type);

static bool functionalErasedLocalVariable(const State &S,
                                          const SourceManager &SM,
                                          const VarDecl *Variable) {
  return Variable && Variable->getKind() == Decl::Var &&
         !Variable->isImplicit() && !Variable->hasAttrs() &&
         Variable->getStorageClass() == SC_None &&
         Variable->isLocalVarDecl() && Variable->hasLocalStorage() &&
         !Variable->isStaticLocal() && !Variable->hasExternalStorage() &&
         Variable->getTLSKind() == VarDecl::TLS_None &&
         !Variable->getType().isVolatileQualified() &&
         !Variable->getType().isRestrictQualified() &&
         S.owns(SM, Variable->getLocation());
}

static std::pair<const UnaryOperator *, const ValueDecl *>
functionalMemberAddress(const State &S, const SourceManager &SM,
                        const Expr *Expression, const ASTContext &Context) {
  const auto *Address = dyn_cast_or_null<UnaryOperator>(
      functionalInvokeStrippedExpression(Expression));
  const auto *Reference =
      Address && Address->getOpcode() == UO_AddrOf
          ? dyn_cast<DeclRefExpr>(functionalInvokeStrippedExpression(
                Address->getSubExpr()))
          : nullptr;
  const auto *Member =
      Reference ? dyn_cast<ValueDecl>(Reference->getDecl()) : nullptr;
  const auto *MemberPointer =
      Address ? Address->getType()->getAs<MemberPointerType>() : nullptr;
  const auto *MemberClass =
      MemberPointer ? MemberPointer->getClass()->getAsCXXRecordDecl() : nullptr;
  const auto *Parent =
      Member ? dyn_cast<CXXRecordDecl>(Member->getDeclContext()) : nullptr;
  if (!Address || !Reference || !Member || !MemberPointer || !MemberClass ||
      !Parent ||
      MemberClass->getCanonicalDecl() != Parent->getCanonicalDecl() ||
      !Context.hasSameType(MemberPointer->getPointeeType(), Member->getType()) ||
      !S.owns(SM, Address->getOperatorLoc()) ||
      !S.owns(SM, Reference->getExprLoc()) ||
      !S.owns(SM, Member->getLocation()))
    return {};
  return {Address, Member};
}

static bool supportedFunctionalStoredMember(const State &S,
                                            const SourceManager &SM,
                                            const ASTContext &Context,
                                            const ValueDecl *Member) {
  if (const auto *Field = dyn_cast_or_null<FieldDecl>(Member)) {
    const auto Type = Field->getType();
    return !Field->isBitField() && !Type.isVolatileQualified() &&
           !Type.isRestrictQualified() &&
           Type.getAddressSpace() == LangAS::Default &&
           (supportedFunctionalMemberValue(Context,
                                           Type.getUnqualifiedType()) ||
            Type->isFunctionPointerType());
  }
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Member);
  if (!Method)
    return false;
  const auto Result = Method->getReturnType();
  const auto Referent = Result->isReferenceType()
                            ? Result->getPointeeType()
                            : QualType();
  if (Method->isStatic() || !callableMethod(Method) || !Method->hasBody() ||
      (Result->isReferenceType()
           ? (Referent.isVolatileQualified() || Referent.isRestrictQualified() ||
              Referent.getAddressSpace() != LangAS::Default ||
              !supportedFunctionalReferenceValue(S, SM, Context, Referent))
           : !supportedFunctionalResult(S, SM, Context, Result)))
    return false;
  for (const auto *Parameter : Method->parameters()) {
    const auto Type = Parameter->getType();
    if (Type->isReferenceType()) {
      const auto ParameterReferent = Type->getPointeeType();
      if (ParameterReferent.isVolatileQualified() ||
          ParameterReferent.isRestrictQualified() ||
          ParameterReferent.getAddressSpace() != LangAS::Default ||
          !supportedFunctionalReferenceValue(S, SM, Context,
                                             ParameterReferent))
        return false;
    } else if (!supportedFunctionalByValue(S, SM, Context, Type)) {
      return false;
    }
  }
  return true;
}

std::optional<FunctionalStoredMemberPointer>
approvedFunctionalStoredMemberPointer(
    const State &S, const SourceManager &SM, const VarDecl *Variable,
    const ASTContext &Context) {
  const auto *Requested = Variable;
  const auto *RequestedInitializer = Variable ? Variable->getInit() : nullptr;
  const CallExpr *RequestedAdapter = nullptr;
  std::set<const VarDecl *> Seen;
  while (functionalErasedLocalVariable(S, SM, Variable)) {
    if (!Seen.insert(Variable->getCanonicalDecl()).second)
      return std::nullopt;
    const auto *MemberPointer =
        Variable->getType()->getAs<MemberPointerType>();
    const Expr *Initializer =
        functionalInvokeStrippedExpression(Variable->getInit());
    if (const auto *Adapter = dyn_cast_or_null<CallExpr>(Initializer)) {
      const auto Operation = approvedUtilityOperation(S, SM, Adapter, Context);
      if (!Operation || Adapter->getNumArgs() != 1 ||
          (*Operation != UtilityOperation::Move &&
           *Operation != UtilityOperation::Forward &&
           *Operation != UtilityOperation::MoveIfNoexcept &&
           *Operation != UtilityOperation::AsConst))
        return std::nullopt;
      if (Variable == Requested)
        RequestedAdapter = Adapter;
      Initializer =
          functionalInvokeStrippedExpression(Adapter->getArg(0));
    }
    const auto [Address, Member] =
        functionalMemberAddress(S, SM, Initializer, Context);
    if (Address) {
      if (!MemberPointer || !Member ||
          !supportedFunctionalStoredMember(S, SM, Context, Member) ||
          !Context.hasSameUnqualifiedType(Variable->getType(),
                                          Address->getType()))
        return std::nullopt;
      return FunctionalStoredMemberPointer{Requested, RequestedInitializer,
                                           RequestedAdapter, Address, Member};
    }
    const auto *Reference = dyn_cast_or_null<DeclRefExpr>(Initializer);
    const auto *Source =
        Reference ? dyn_cast<VarDecl>(Reference->getDecl()) : nullptr;
    if (!MemberPointer || !Reference || !Source ||
        !S.owns(SM, Reference->getExprLoc()) ||
        !Context.hasSameUnqualifiedType(Variable->getType(),
                                        Source->getType()))
      return std::nullopt;
    Variable = Source;
  }
  return std::nullopt;
}

static std::pair<const UnaryOperator *, const ValueDecl *>
functionalStoredMemberExpression(const State &S, const SourceManager &SM,
                                 const Expr *Expression,
                                 const ASTContext &Context) {
  if (auto Direct = functionalMemberAddress(S, SM, Expression, Context);
      Direct.first)
    return Direct;
  Expression = functionalInvokeStrippedExpression(Expression);
  if (const auto *Adapter = dyn_cast_or_null<CallExpr>(Expression)) {
    const auto Operation = approvedUtilityOperation(S, SM, Adapter, Context);
    if (!Operation || Adapter->getNumArgs() != 1 ||
        (*Operation != UtilityOperation::Move &&
         *Operation != UtilityOperation::Forward &&
         *Operation != UtilityOperation::MoveIfNoexcept &&
         *Operation != UtilityOperation::AsConst))
      return {};
    Expression = functionalInvokeStrippedExpression(Adapter->getArg(0));
    if (auto Direct = functionalMemberAddress(S, SM, Expression, Context);
        Direct.first)
      return Direct;
  }
  const auto *Reference = dyn_cast_or_null<DeclRefExpr>(Expression);
  const auto *Variable =
      Reference ? dyn_cast<VarDecl>(Reference->getDecl()) : nullptr;
  const auto Stored =
      approvedFunctionalStoredMemberPointer(S, SM, Variable, Context);
  if (!Stored || !Reference || !S.owns(SM, Reference->getExprLoc()))
    return {};
  return {dyn_cast<UnaryOperator>(Stored->Address), Stored->Member};
}

std::optional<NativeDataMemberPointerAccess>
approvedNativeDataMemberPointerAccess(
    const State &S, const SourceManager &SM, const BinaryOperator *Operation,
    const ASTContext &Context) {
  if (!Operation || (Operation->getOpcode() != BO_PtrMemD &&
                     Operation->getOpcode() != BO_PtrMemI) ||
      !Operation->isGLValue())
    return std::nullopt;
  const auto *Callable = Operation->getRHS();
  const auto [Address, Member] =
      functionalStoredMemberExpression(S, SM, Callable, Context);
  const auto *Field = dyn_cast_or_null<FieldDecl>(Member);
  const auto *MemberPointer =
      Callable->getType()->getAs<MemberPointerType>();
  const bool ObjectIsPointer = Operation->getOpcode() == BO_PtrMemI;
  const auto ObjectType =
      ObjectIsPointer && Operation->getLHS()->getType()->isPointerType()
          ? Operation->getLHS()->getType()->getPointeeType()
          : Operation->getLHS()->getType();
  if (!Field || !MemberPointer ||
      !Context.hasSameType(ObjectType.getUnqualifiedType().getTypePtr(),
                           MemberPointer->getClass()) ||
      !supportedFunctionalStoredMember(S, SM, Context, Field) ||
      ObjectType.isVolatileQualified() || ObjectType.isRestrictQualified() ||
      ObjectType.getAddressSpace() != LangAS::Default)
    return std::nullopt;
  return NativeDataMemberPointerAccess{Operation->getLHS(), Callable, Field,
                                       ObjectIsPointer};
}

std::optional<FunctionalStoredMemFn> approvedFunctionalStoredMemFn(
    const State &S, const SourceManager &SM, const VarDecl *Variable,
    const ASTContext &Context) {
  const auto *Requested = Variable;
  const auto *RequestedInitializer = Variable ? Variable->getInit() : nullptr;
  std::set<const VarDecl *> Seen;
  while (functionalErasedLocalVariable(S, SM, Variable)) {
    if (!Seen.insert(Variable->getCanonicalDecl()).second)
      return std::nullopt;
    const auto *Initializer = functionalInvokeStrippedExpression(
        Variable->getInit());
    if (isa_and_nonnull<CallExpr>(Initializer))
      break;
    const auto *Construction =
        dyn_cast_or_null<CXXConstructExpr>(Initializer);
    const auto *Constructor =
        Construction ? Construction->getConstructor() : nullptr;
    const Expr *Argument =
        Construction && Construction->getNumArgs() == 1
            ? functionalInvokeStrippedExpression(Construction->getArg(0))
            : nullptr;
    if (const auto *Adapter = dyn_cast_or_null<CallExpr>(Argument)) {
      const auto Operation = approvedUtilityOperation(S, SM, Adapter, Context);
      if (!Operation || Adapter->getNumArgs() != 1 ||
          (*Operation != UtilityOperation::Move &&
           *Operation != UtilityOperation::Forward &&
           *Operation != UtilityOperation::MoveIfNoexcept &&
           *Operation != UtilityOperation::AsConst))
        return std::nullopt;
      Argument = functionalInvokeStrippedExpression(Adapter->getArg(0));
    }
    const auto *Reference = dyn_cast_or_null<DeclRefExpr>(Argument);
    const auto *Source =
        Reference ? dyn_cast<VarDecl>(Reference->getDecl()) : nullptr;
    if (!Construction || !Constructor || !Reference || !Source ||
        !Constructor->isImplicit() || !Constructor->isTrivial() ||
        !Constructor->isCopyOrMoveConstructor() ||
        Constructor->getNumParams() != 1 ||
        !approvedStandardSDKDeclaration(S, SM, Constructor) ||
        !S.owns(SM, Construction->getExprLoc()) ||
        !S.owns(SM, Reference->getExprLoc()) ||
        !Context.hasSameUnqualifiedType(Variable->getType(),
                                        Construction->getType()) ||
        !Context.hasSameUnqualifiedType(Variable->getType(),
                                        Source->getType()))
      return std::nullopt;
    Variable = Source;
  }
  if (!functionalErasedLocalVariable(S, SM, Variable))
    return std::nullopt;
  const auto *Factory = dyn_cast_or_null<CallExpr>(
      functionalInvokeStrippedExpression(Variable->getInit()));
  const auto *Function = Factory ? Factory->getDirectCallee() : nullptr;
  const auto *Primary = Function ? Function->getPrimaryTemplate() : nullptr;
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Reference =
      dyn_cast_or_null<DeclRefExpr>(Factory ? directFunctionReference(Factory)
                                           : nullptr);
  const auto *Arguments =
      Function ? Function->getTemplateSpecializationArgs() : nullptr;
  const auto *Record = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Factory && Factory->getType()->getAsCXXRecordDecl()
          ? Factory->getType()->getAsCXXRecordDecl()->getDefinition()
          : nullptr);
  const auto *Stored =
      Record && std::distance(Record->field_begin(), Record->field_end()) == 1
          ? *Record->field_begin()
          : nullptr;
  const auto *FactoryArgument =
      Factory && Factory->getNumArgs() == 1 ? Factory->getArg(0) : nullptr;
  const auto [Address, Member] =
      functionalStoredMemberExpression(S, SM, FactoryArgument, Context);
  const auto *Field = dyn_cast_or_null<FieldDecl>(Member);
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Member);
  if (!Factory || !Function || !Primary || !Pattern || !Reference ||
      !Arguments || !Record || !Stored || !Address || (!Field && !Method) ||
      Function->getNumParams() != 1 || Function->isVariadic() ||
      !Function->isInlined() || !Function->hasBody() || !Pattern->hasBody() ||
      !Function->getIdentifier() || Function->getName() != "mem_fn" ||
      !Factory->isPRValue() || Record->getName() != "__mem_fn" ||
      Record->isUnion() || Record->isDependentContext() ||
      !Stored->getIdentifier() || Stored->getName() != "__f_" ||
      !Stored->getType()->isMemberPointerType() ||
      !Context.hasSameUnqualifiedType(Variable->getType(), Factory->getType()) ||
      !Context.hasSameType(Function->getReturnType(), Factory->getType()) ||
      !Context.hasSameType(Function->getParamDecl(0)->getType(),
                           Stored->getType()) ||
      !Context.hasSameType(Factory->getArg(0)->getType(), Stored->getType()) ||
      Arguments->size() != 2 ||
      Arguments->get(0).getKind() != TemplateArgument::Type ||
      Arguments->get(1).getKind() != TemplateArgument::Type ||
      !approvedStandardSDKDeclaration(S, SM, Function) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !approvedStandardSDKDeclaration(S, SM, Pattern) ||
      !approvedStandardSDKDeclaration(S, SM, Reference->getDecl()) ||
      !approvedStandardSDKDeclaration(S, SM, Record) ||
      !approvedStandardSDKDeclaration(S, SM, Stored) ||
      !cstddefOrigin(S, SM, Function->getLocation(), "libcxx",
                     "__functional/mem_fn.h") ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__functional/mem_fn.h") ||
      !cstddefOrigin(S, SM, Pattern->getLocation(), "libcxx",
                     "__functional/mem_fn.h") ||
      !cstddefOrigin(S, SM, Record->getLocation(), "libcxx",
                     "__functional/mem_fn.h") ||
      !cstddefOrigin(S, SM, Stored->getLocation(), "libcxx",
                     "__functional/mem_fn.h"))
    return std::nullopt;
  if (!supportedFunctionalStoredMember(S, SM, Context, Member))
    return std::nullopt;
  return FunctionalStoredMemFn{Requested, RequestedInitializer, Factory,
                               Address, Member};
}

static bool functionalInvokeParameterReference(
    const Expr *Expression, const ParmVarDecl *Parameter,
    bool Dereference = false) {
  Expression = functionalInvokeStrippedExpression(Expression);
  if (const auto *Cast = dyn_cast_or_null<CXXStaticCastExpr>(Expression)) {
    if (Cast->getCastKind() != CK_NoOp)
      return false;
    Expression = functionalInvokeStrippedExpression(Cast->getSubExpr());
  }
  if (Dereference) {
    const auto *Pointer = dyn_cast_or_null<UnaryOperator>(Expression);
    if (!Pointer || Pointer->getOpcode() != UO_Deref)
      return false;
    Expression = functionalInvokeStrippedExpression(Pointer->getSubExpr());
    if (const auto *Cast = dyn_cast_or_null<CXXStaticCastExpr>(Expression)) {
      if (Cast->getCastKind() != CK_NoOp)
        return false;
      Expression = functionalInvokeStrippedExpression(Cast->getSubExpr());
    }
  }
  const auto *Reference = dyn_cast_or_null<DeclRefExpr>(Expression);
  return Reference && Reference->getDecl() == Parameter;
}

static bool approvedFunctionalForwardingCall(
    const State &S, const SourceManager &SM, const Expr *Expression,
    const ParmVarDecl *Parameter) {
  const auto *Forward = dyn_cast_or_null<CallExpr>(
      Expression ? Expression->IgnoreParenImpCasts() : nullptr);
  if (!Forward)
    return false;
  const auto *Function = Forward ? Forward->getDirectCallee() : nullptr;
  const auto *Primary = Function ? Function->getPrimaryTemplate() : nullptr;
  const auto *Reference =
      dyn_cast_or_null<DeclRefExpr>(directFunctionReference(Forward));
  const auto *Argument =
      Forward && Forward->getNumArgs() == 1
          ? dyn_cast<DeclRefExpr>(
                Forward->getArg(0)->IgnoreParenImpCasts())
          : nullptr;
  return Function && Primary && Reference && Argument && Parameter &&
         Function->getIdentifier() && Function->getName() == "forward" &&
         Argument->getDecl() == Parameter &&
         approvedStandardSDKDeclaration(S, SM, Function) &&
         approvedStandardSDKDeclaration(S, SM, Primary) &&
         approvedStandardSDKDeclaration(S, SM, Reference->getDecl()) &&
         cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                       "__utility/forward.h");
}

static bool approvedFunctionalInvokeArgumentFlow(
    const State &S, const SourceManager &SM, const Expr *Expression,
    const ParmVarDecl *Parameter, QualType Target,
    const ASTContext &Context) {
  if (functionalInvokeParameterReference(Expression, Parameter) ||
      approvedFunctionalForwardingCall(S, SM, Expression, Parameter))
    return true;
  if (Target.isNull() || !Target->isRecordType())
    return false;
  Expression = functionalInvokeStrippedExpression(Expression);
  if (const auto *Temporary = dyn_cast_or_null<CXXBindTemporaryExpr>(Expression))
    Expression = functionalInvokeStrippedExpression(Temporary->getSubExpr());
  const auto *Construction = dyn_cast_or_null<CXXConstructExpr>(Expression);
  const auto *Constructor =
      Construction ? Construction->getConstructor() : nullptr;
  const auto *Definition = Target->getAsCXXRecordDecl();
  Definition = Definition ? Definition->getDefinition() : nullptr;
  return Construction && Constructor && Definition &&
         Construction->getNumArgs() == 1 &&
         Constructor->isCopyOrMoveConstructor() &&
         Constructor->getNumParams() == 1 &&
         Constructor->getParent()->getCanonicalDecl() ==
             Definition->getCanonicalDecl() &&
         S.owns(SM, Constructor->getLocation()) &&
         Context.hasSameUnqualifiedType(Construction->getType(), Target) &&
         functionalInvokeParameterReference(Construction->getArg(0), Parameter);
}

static bool supportedFunctionalMemberValue(const ASTContext &Context,
                                           QualType Type) {
  return supportedFunctionalCallableValue(Type, Context) ||
         utilityObjectPointer(Context, Type);
}

static bool supportedFunctionalInvokeReference(const State &S,
                                               const SourceManager &SM,
                                               const ASTContext &Context,
                                               QualType Type) {
  return supportedFunctionalReferenceValue(S, SM, Context, Type);
}

static bool
supportedFunctionalInvokeReferenceArgument(const State &S,
                                           const SourceManager &SM,
                                           const ASTContext &Context,
                                           QualType Parameter,
                                           const Expr *ArgumentExpression) {
  if (!Parameter->isReferenceType() || !ArgumentExpression)
    return false;
  const auto Referent = Parameter->getPointeeType();
  const auto Argument = ArgumentExpression->getType();
  return supportedFunctionalInvokeReference(S, SM, Context, Referent) &&
         (Parameter->isLValueReferenceType()
              ? (ArgumentExpression->isLValue() ||
                 (Referent.isConstQualified() &&
                  ArgumentExpression->isXValue()))
              : ArgumentExpression->isXValue()) &&
         !Argument.isVolatileQualified() &&
         Context.hasSameUnqualifiedType(Referent, Argument) &&
         (Referent.isConstQualified() || !Argument.isConstQualified());
}

static bool functionalMemberValueConversion(const ASTContext &Context,
                                            QualType From, QualType To) {
  // Reading a value argument drops its top-level const, including const hidden
  // by a typedef. Preserve all other qualifiers for the conversion checks below.
  From = From.getCanonicalType();
  From.removeLocalConst();
  if (To->isFunctionPointerType()) {
    if (From->isFunctionType())
      return Context.hasSameType(Context.getPointerType(From), To);
    return From->isFunctionPointerType() && Context.hasSameType(From, To);
  }
  if (From->isFunctionType() || From->isFunctionPointerType())
    return false;
  if (To->isRecordType())
    return From->isRecordType() &&
           Context.hasSameUnqualifiedType(From, To);
  if (supportedFunctionalCallableValue(From, Context) &&
      supportedFunctionalCallableValue(To, Context)) {
    return utilityScalarDirectConversion(Context, From, To);
  }
  if (!utilityObjectPointer(Context, To))
    return false;
  if (From->isNullPtrType())
    return true;
  return utilityPointerConversion(Context, From, To);
}

static bool functionalMemberReceiverValueCategory(
    const CXXMethodDecl *Method, const Expr *Object, bool ObjectIsPointer,
    bool ObjectIsWrapper = false) {
  if (!Method || !Object)
    return false;
  const bool LValueReceiver =
      ObjectIsPointer || ObjectIsWrapper || Object->isLValue();
  switch (Method->getRefQualifier()) {
  case RQ_None:
    return LValueReceiver || Object->isXValue();
  case RQ_LValue:
    return LValueReceiver;
  case RQ_RValue:
    return !ObjectIsPointer && !ObjectIsWrapper && Object->isXValue();
  }
  llvm_unreachable("unknown method ref qualifier");
}

std::optional<FunctionalMemberInvokeCall>
approvedNativeMemberPointerCall(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context) {
  const auto *MemberCall = dyn_cast_or_null<CXXMemberCallExpr>(Call);
  const auto *Operation = dyn_cast_or_null<BinaryOperator>(
      functionalInvokeStrippedExpression(Call ? Call->getCallee() : nullptr));
  if (!MemberCall || !Operation ||
      (Operation->getOpcode() != BO_PtrMemD &&
       Operation->getOpcode() != BO_PtrMemI) ||
      !S.owns(SM, Call->getExprLoc()) ||
      !S.owns(SM, Operation->getOperatorLoc()))
    return std::nullopt;

  const auto *Callable = Operation->getRHS();
  const auto [Address, Member] =
      functionalStoredMemberExpression(S, SM, Callable, Context);
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Member);
  const auto *MemberPointer =
      Callable->getType()->getAs<MemberPointerType>();
  const auto *MemberClass =
      MemberPointer ? MemberPointer->getClass()->getAsCXXRecordDecl() : nullptr;
  if (!Address || !Method || !MemberPointer || !MemberClass ||
      Method->isStatic() || !callableMethod(Method) || !Method->hasBody() ||
      Method->isVariadic() ||
      !Context.hasSameType(MemberPointer->getPointeeType(),
                           Method->getType()) ||
      MemberClass->getCanonicalDecl() !=
          Method->getParent()->getCanonicalDecl())
    return std::nullopt;

  const auto *Object = Operation->getLHS();
  const bool ObjectIsPointer = Operation->getOpcode() == BO_PtrMemI;
  const auto ObjectType =
      ObjectIsPointer && Object->getType()->isPointerType()
          ? Object->getType()->getPointeeType()
          : Object->getType();
  const auto *ObjectRecord = ObjectType->getAsCXXRecordDecl();
  if (!ObjectRecord ||
      ObjectRecord->getCanonicalDecl() != MemberClass->getCanonicalDecl() ||
      (!ObjectIsPointer && !Object->isGLValue()) ||
      !functionalMemberReceiverValueCategory(Method, Object,
                                             ObjectIsPointer) ||
      ObjectType.isVolatileQualified() || ObjectType.isRestrictQualified() ||
      ObjectType.getAddressSpace() != LangAS::Default ||
      (ObjectType.isConstQualified() && !Method->isConst()))
    return std::nullopt;

  const auto Result = Method->getReturnType();
  const bool ReferenceResult = Result->isReferenceType();
  const auto Referent =
      ReferenceResult ? Result->getPointeeType() : QualType();
  if (Method->getNumParams() != Call->getNumArgs() ||
      (ReferenceResult
           ? (Referent.isVolatileQualified() ||
              Referent.isRestrictQualified() ||
              Referent.getAddressSpace() != LangAS::Default ||
              !supportedFunctionalInvokeReference(S, SM, Context, Referent) ||
              (Result->isLValueReferenceType() ? !Call->isLValue()
                                               : !Call->isXValue()) ||
              !Context.hasSameType(Referent, Call->getType()))
           : (!Context.hasSameType(Result, Call->getType()) ||
              !supportedFunctionalResult(S, SM, Context, Result))))
    return std::nullopt;

  for (unsigned I = 0; I < Method->getNumParams(); ++I) {
    const auto Parameter = Method->getParamDecl(I)->getType();
    const auto *ArgumentExpression = Call->getArg(I);
    bool Supported = false;
    if (Parameter->isReferenceType()) {
      Supported = supportedFunctionalInvokeReferenceArgument(
          S, SM, Context, Parameter, ArgumentExpression);
    } else {
      const auto *Source =
          functionalInvokeStrippedExpression(ArgumentExpression);
      Supported = !Parameter->isReferenceType() && Source &&
                  supportedFunctionalByValue(S, SM, Context, Parameter) &&
                  functionalMemberValueConversion(Context, Source->getType(),
                                                  Parameter);
    }
    if (!Supported)
      return std::nullopt;
  }
  return FunctionalMemberInvokeCall{Call->getCallee(), Object, Method, nullptr,
                                    nullptr, nullptr, std::nullopt,
                                    ObjectIsPointer};
}

struct FunctionalMemberDispatch {
  const Expr *Callable;
  const CallExpr *Factory;
  const CallExpr *Dispatch;
  const CallExpr *Adapter;
};

static const Expr *functionalMemFnAdaptedUse(
    const State &S, const SourceManager &SM, const Expr *Expression,
    const ASTContext &Context, const CallExpr *&Adapter) {
  Expression = functionalInvokeStrippedExpression(Expression);
  const auto *Candidate = dyn_cast_or_null<CallExpr>(Expression);
  const auto Operation = approvedUtilityOperation(S, SM, Candidate, Context);
  if (!Operation || Candidate->getNumArgs() != 1 ||
      (*Operation != UtilityOperation::Move &&
       *Operation != UtilityOperation::Forward &&
       *Operation != UtilityOperation::MoveIfNoexcept &&
       *Operation != UtilityOperation::AsConst))
    return Expression;
  Adapter = Candidate;
  return functionalInvokeStrippedExpression(Candidate->getArg(0));
}

static std::optional<FunctionalMemberDispatch>
approvedFunctionalMemFnDispatch(const State &S, const SourceManager &SM,
                                const CallExpr *Call,
                                const ASTContext &Context,
                                const CallExpr *SuppliedFactory = nullptr) {
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call ? Call->getDirectCallee() : nullptr);
  const auto *Reference = Call ? directMethodReference(Call) : nullptr;
  const auto *Primary = Method ? Method->getPrimaryTemplate() : nullptr;
  const auto *Pattern =
      Primary ? dyn_cast<CXXMethodDecl>(Primary->getTemplatedDecl()) : nullptr;
  const auto *TemplateParameters =
      Primary ? Primary->getTemplateParameters() : nullptr;
  const auto *Pack =
      TemplateParameters && TemplateParameters->size() == 1
          ? dyn_cast<TemplateTypeParmDecl>(TemplateParameters->getParam(0))
          : nullptr;
  const auto *Record = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Method && Method->getParent() ? Method->getParent()->getDefinition()
                                    : nullptr);
  const auto *RecordTemplate =
      Record ? Record->getSpecializedTemplate() : nullptr;
  const auto *CanonicalRecordTemplate =
      RecordTemplate ? RecordTemplate->getCanonicalDecl() : nullptr;
  const auto *Stored =
      Record && std::distance(Record->field_begin(), Record->field_end()) == 1
          ? *Record->field_begin()
          : nullptr;
  const auto *Base =
      Record && Record->getNumBases() == 1
          ? Record->bases_begin()->getType()->getAsCXXRecordDecl()
          : nullptr;
  Base = Base ? Base->getDefinition() : nullptr;
  const auto MethodResult = Method ? Method->getReturnType() : QualType();
  const auto ExpressionResult =
      !MethodResult.isNull() && MethodResult->isReferenceType()
          ? MethodResult->getPointeeType()
          : MethodResult;
  if (!Operator || !Method || !Reference || !Primary || !Pattern || !Pack ||
      !Pack->isParameterPack() || !Record || !RecordTemplate ||
      !CanonicalRecordTemplate || !Stored || !Base ||
      Operator->getOperator() != OO_Call ||
      Method->getOverloadedOperator() != OO_Call || Method->isStatic() ||
      !Method->isConst() || Method->isVariadic() || !Method->isInlined() ||
      !Method->hasBody() || Call->getNumArgs() != Method->getNumParams() + 1 ||
      !Context.hasSameType(Call->getType(), ExpressionResult) ||
      !Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                      Context.getRecordType(Record)) ||
      Record->getName() != "__mem_fn" || Record->isUnion() ||
      Record->isDependentContext() || !Record->isStandardLayout() ||
      !Record->isTriviallyCopyable() || !Record->hasTrivialDestructor() ||
      !Stored->getIdentifier() || Stored->getName() != "__f_" ||
      !Stored->getType()->isMemberPointerType() ||
      Base->getName() != "__weak_result_type" || !Base->isEmpty() ||
      !Base->isStandardLayout() || !Base->field_empty() ||
      !Base->hasTrivialDestructor() ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !approvedStandardSDKDeclaration(S, SM, Pattern) ||
      !approvedStandardSDKDeclaration(S, SM, Record) ||
      !approvedStandardSDKDeclaration(S, SM, RecordTemplate) ||
      !approvedStandardSDKDeclaration(S, SM, CanonicalRecordTemplate) ||
      !approvedStandardSDKDeclaration(S, SM, Stored) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__functional/mem_fn.h") ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__functional/mem_fn.h") ||
      !cstddefOrigin(S, SM, Pattern->getLocation(), "libcxx",
                     "__functional/mem_fn.h") ||
      !cstddefOrigin(S, SM, Record->getLocation(), "libcxx",
                     "__functional/mem_fn.h") ||
      !cstddefOrigin(S, SM, RecordTemplate->getLocation(), "libcxx",
                     "__functional/mem_fn.h") ||
      !cstddefOrigin(S, SM, CanonicalRecordTemplate->getLocation(), "libcxx",
                     "__functional/mem_fn.h") ||
      !cstddefOrigin(S, SM, Stored->getLocation(), "libcxx",
                     "__functional/mem_fn.h"))
    return std::nullopt;
  const auto &RecordArguments = Record->getTemplateArgs();
  if (RecordArguments.size() != 1 ||
      RecordArguments.get(0).getKind() != TemplateArgument::Type ||
      !Context.hasSameType(RecordArguments.get(0).getAsType(),
                           Stored->getType()))
    return std::nullopt;
  for (unsigned I = 0; I < Method->getNumParams(); ++I) {
    const auto Parameter = Method->getParamDecl(I)->getType();
    const auto *Actual = Call->getArg(I + 1);
    if (!Parameter->isReferenceType() ||
        Parameter->getPointeeType().isVolatileQualified() ||
        !Context.hasSameType(Parameter->getPointeeType(), Actual->getType()) ||
        (Parameter->isLValueReferenceType() ? !Actual->isLValue()
                                            : Actual->isLValue()))
      return std::nullopt;
  }

  const CallExpr *UseAdapter = nullptr;
  const auto *Object = functionalMemFnAdaptedUse(
      S, SM, Call->getArg(0), Context, UseAdapter);
  const auto *StoredReference = dyn_cast_or_null<DeclRefExpr>(Object);
  const auto *StoredVariable =
      StoredReference ? dyn_cast<VarDecl>(StoredReference->getDecl()) : nullptr;
  const auto StoredObject = approvedFunctionalStoredMemFn(
      S, SM, StoredVariable, Context);
  const auto *Factory = SuppliedFactory
                            ? SuppliedFactory
                            : StoredObject
                                  ? StoredObject->Factory
                                  : dyn_cast_or_null<CallExpr>(Object);
  const auto *FactoryFunction = Factory ? Factory->getDirectCallee() : nullptr;
  const auto *FactoryPrimary =
      FactoryFunction ? FactoryFunction->getPrimaryTemplate() : nullptr;
  const auto *FactoryPattern =
      FactoryPrimary ? FactoryPrimary->getTemplatedDecl() : nullptr;
  const auto *FactoryReference =
      dyn_cast_or_null<DeclRefExpr>(Factory ? directFunctionReference(Factory)
                                           : nullptr);
  const auto *FactoryArguments =
      FactoryFunction ? FactoryFunction->getTemplateSpecializationArgs()
                      : nullptr;
  if (!Factory || !FactoryFunction || !FactoryPrimary || !FactoryPattern ||
      !FactoryReference || !FactoryArguments || Factory->getNumArgs() != 1 ||
      FactoryFunction->getNumParams() != 1 || FactoryFunction->isVariadic() ||
      !FactoryFunction->isInlined() || !FactoryFunction->hasBody() ||
      !FactoryPattern->hasBody() || !FactoryFunction->getIdentifier() ||
      FactoryFunction->getName() != "mem_fn" || !Factory->isPRValue() ||
      !Context.hasSameUnqualifiedType(Factory->getType(),
                                      Context.getRecordType(Record)) ||
      !Context.hasSameType(FactoryFunction->getReturnType(),
                           Factory->getType()) ||
      !Context.hasSameType(FactoryFunction->getParamDecl(0)->getType(),
                           Stored->getType()) ||
      !Context.hasSameType(Factory->getArg(0)->getType(), Stored->getType()) ||
      FactoryArguments->size() != 2 ||
      FactoryArguments->get(0).getKind() != TemplateArgument::Type ||
      FactoryArguments->get(1).getKind() != TemplateArgument::Type ||
      !approvedStandardSDKDeclaration(S, SM, FactoryFunction) ||
      !approvedStandardSDKDeclaration(S, SM, FactoryPrimary) ||
      !approvedStandardSDKDeclaration(S, SM, FactoryPattern) ||
      !approvedStandardSDKDeclaration(S, SM, FactoryReference->getDecl()) ||
      !cstddefOrigin(S, SM, FactoryFunction->getLocation(), "libcxx",
                     "__functional/mem_fn.h") ||
      !cstddefOrigin(S, SM, FactoryPrimary->getLocation(), "libcxx",
                     "__functional/mem_fn.h") ||
      !cstddefOrigin(S, SM, FactoryPattern->getLocation(), "libcxx",
                     "__functional/mem_fn.h"))
    return std::nullopt;
  const auto *MemberPointer = Stored->getType()->getAs<MemberPointerType>();
  if (!MemberPointer ||
      !Context.hasSameType(FactoryArguments->get(1).getAsType().getTypePtr(),
                           MemberPointer->getClass()))
    return std::nullopt;

  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(*Body->body_begin())
                           : nullptr;
  const auto *Dispatch =
      Return && Return->getRetValue()
          ? dyn_cast_or_null<CallExpr>(functionalInvokeStrippedExpression(
                Return->getRetValue()))
          : nullptr;
  const auto *DispatchFunction =
      Dispatch ? Dispatch->getDirectCallee() : nullptr;
  const auto *DispatchPrimary =
      DispatchFunction ? DispatchFunction->getPrimaryTemplate() : nullptr;
  const auto *DispatchPattern =
      DispatchPrimary ? DispatchPrimary->getTemplatedDecl() : nullptr;
  const auto *DispatchReference =
      Dispatch ? dyn_cast_or_null<DeclRefExpr>(directFunctionReference(Dispatch))
               : nullptr;
  if (!Dispatch || !DispatchFunction || !DispatchPrimary || !DispatchPattern ||
      !DispatchReference || !DispatchFunction->getIdentifier() ||
      DispatchFunction->getName() != "__invoke" ||
      DispatchFunction->isVariadic() || !DispatchFunction->isInlined() ||
      !DispatchFunction->isConstexpr() || !DispatchFunction->hasBody() ||
      !DispatchPattern->hasBody() ||
      Dispatch->getNumArgs() != DispatchFunction->getNumParams() ||
      Dispatch->getNumArgs() != Call->getNumArgs() ||
      !Context.hasSameType(Dispatch->getType(), Call->getType()) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchFunction) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchPrimary) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchPattern) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchReference->getDecl()) ||
      !cstddefOrigin(S, SM, DispatchPrimary->getLocation(), "libcxx",
                     "__type_traits/invoke.h"))
    return std::nullopt;
  const auto *StoredAccess =
      dyn_cast<MemberExpr>(Dispatch->getArg(0)->IgnoreParenImpCasts());
  const auto *StoredBase =
      StoredAccess ? StoredAccess->getBase()->IgnoreParenImpCasts() : nullptr;
  if (!StoredAccess || StoredAccess->getMemberDecl() != Stored ||
      !isa_and_nonnull<CXXThisExpr>(StoredBase))
    return std::nullopt;
  for (unsigned I = 0; I < Method->getNumParams(); ++I)
    if (!approvedFunctionalForwardingCall(
            S, SM, Dispatch->getArg(I + 1), Method->getParamDecl(I)))
      return std::nullopt;
  return FunctionalMemberDispatch{Factory->getArg(0), Factory, Dispatch,
                                  UseAdapter};
}

static std::optional<FunctionalMemberDispatch>
approvedFunctionalInvokeMemFnDispatch(const State &S,
                                      const SourceManager &SM,
                                      const CallExpr *Call,
                                      const ASTContext &Context) {
  if (!Call || Call->getNumArgs() < 2)
    return std::nullopt;
  const CallExpr *UseAdapter = nullptr;
  const auto *FactoryExpression = functionalMemFnAdaptedUse(
      S, SM, Call->getArg(0), Context, UseAdapter);
  const auto *StoredReference =
      dyn_cast_or_null<DeclRefExpr>(FactoryExpression);
  const auto *StoredVariable =
      StoredReference ? dyn_cast<VarDecl>(StoredReference->getDecl()) : nullptr;
  const auto StoredObject = approvedFunctionalStoredMemFn(
      S, SM, StoredVariable, Context);
  const auto *Factory = StoredObject
                            ? StoredObject->Factory
                            : dyn_cast_or_null<CallExpr>(FactoryExpression);
  const auto *OuterDispatch = approvedFunctionalInvokeDispatch(
      S, SM, Call, Context, Call->isGLValue());
  const auto *OuterFunction =
      OuterDispatch ? OuterDispatch->getDirectCallee() : nullptr;
  const auto *InvokeFunction = Call->getDirectCallee();
  const auto *Body = OuterFunction
                         ? dyn_cast<CompoundStmt>(OuterFunction->getBody())
                         : nullptr;
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(*Body->body_begin())
                           : nullptr;
  const auto *Operator =
      Return && Return->getRetValue()
          ? dyn_cast_or_null<CXXOperatorCallExpr>(
                functionalInvokeStrippedExpression(Return->getRetValue()))
          : nullptr;
  if (!Factory || !OuterDispatch || !OuterFunction || !InvokeFunction ||
      !Body || !Return || !Operator ||
      Operator->getNumArgs() != Call->getNumArgs() ||
      OuterFunction->getNumParams() != Call->getNumArgs() ||
      InvokeFunction->getNumParams() != Call->getNumArgs() ||
      !Context.hasSameType(Operator->getType(), Call->getType()))
    return std::nullopt;
  for (unsigned I = 0; I < Operator->getNumArgs(); ++I) {
    if (!approvedFunctionalForwardingCall(
            S, SM, OuterDispatch->getArg(I),
            InvokeFunction->getParamDecl(I)))
      return std::nullopt;
    if (!functionalInvokeParameterReference(
            Operator->getArg(I), OuterFunction->getParamDecl(I)))
      return std::nullopt;
  }
  auto Approved =
      approvedFunctionalMemFnDispatch(S, SM, Operator, Context, Factory);
  if (Approved)
    Approved->Adapter = UseAdapter;
  return Approved;
}

static std::optional<FunctionalMemberInvokeCall>
approvedFunctionalMemberInvokeCallImpl(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context,
    std::optional<FunctionalMemberDispatch> PreparedDispatch = std::nullopt) {
  if (!Call || Call->getNumArgs() < 2)
    return std::nullopt;
  const bool HasPreparedDispatch = PreparedDispatch.has_value();
  auto MemberDispatch = std::move(PreparedDispatch);
  if (!MemberDispatch) {
    MemberDispatch = approvedFunctionalMemFnDispatch(S, SM, Call, Context);
  }
  if (!MemberDispatch && !HasPreparedDispatch)
    MemberDispatch =
        approvedFunctionalInvokeMemFnDispatch(S, SM, Call, Context);
  const auto *WrittenCallable =
      MemberDispatch ? MemberDispatch->Callable : Call->getArg(0);
  const auto [Address, ResolvedMember] = functionalStoredMemberExpression(
      S, SM, WrittenCallable, Context);
  const auto *Reference =
      Address && Address->getOpcode() == UO_AddrOf
          ? dyn_cast<DeclRefExpr>(
                functionalInvokeStrippedExpression(Address->getSubExpr()))
          : nullptr;
  const auto *Member =
      Reference ? dyn_cast<ValueDecl>(Reference->getDecl()) : nullptr;
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Member);
  const auto *Field = dyn_cast_or_null<FieldDecl>(Member);
  const auto *MemberPointer =
      WrittenCallable->getType()->getAs<MemberPointerType>();
  const auto *MemberClass =
      MemberPointer ? MemberPointer->getClass()->getAsCXXRecordDecl() : nullptr;
  const auto *Parent = Member ? dyn_cast<CXXRecordDecl>(Member->getDeclContext())
                              : nullptr;
  if (!Address || !Reference || Member != ResolvedMember ||
      (!Method && !Field) || !MemberPointer ||
      !MemberClass || !Parent ||
      MemberClass->getCanonicalDecl() != Parent->getCanonicalDecl() ||
      !Context.hasSameType(MemberPointer->getPointeeType(),
                           Member->getType()) ||
      !S.owns(SM, Address->getOperatorLoc()) ||
      !S.owns(SM, Reference->getExprLoc()) ||
      !S.owns(SM, Member->getLocation()))
    return std::nullopt;

  const auto *Object = Call->getArg(1);
  const auto ObjectType = Object->getType();
  const bool ObjectIsPointer = ObjectType->isPointerType();
  auto ObjectPointee =
      ObjectIsPointer ? ObjectType->getPointeeType() : ObjectType;
  auto ObjectWrapper = approvedFunctionalReferenceRecord(
      S, SM, ObjectPointee->getAsCXXRecordDecl(), Context);
  if (ObjectWrapper)
    ObjectPointee = ObjectWrapper->ReferentType;
  const auto *ObjectRecord = ObjectPointee->getAsCXXRecordDecl();
  if (!ObjectRecord ||
      ObjectRecord->getCanonicalDecl() != MemberClass->getCanonicalDecl() ||
      ObjectPointee.isVolatileQualified() ||
      (!ObjectIsPointer && !ObjectWrapper && !Object->isGLValue()))
    return std::nullopt;

  const bool MethodReferenceResult =
      Method && Method->getReturnType()->isReferenceType();
  const bool ReferenceResult = Field || MethodReferenceResult;
  const auto *Dispatch =
      MemberDispatch
          ? MemberDispatch->Dispatch
          : approvedFunctionalInvokeDispatch(S, SM, Call, Context,
                                             ReferenceResult);
  const auto *DispatchFunction =
      Dispatch ? Dispatch->getDirectCallee() : nullptr;
  const auto *Body = DispatchFunction
                         ? dyn_cast<CompoundStmt>(DispatchFunction->getBody())
                         : nullptr;
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(*Body->body_begin())
                           : nullptr;
  const Expr *Operation =
      Return && Return->getRetValue()
          ? functionalInvokeStrippedExpression(Return->getRetValue())
          : nullptr;
  const BinaryOperator *MemberOperation = nullptr;
  const CXXMemberCallExpr *MemberCall = nullptr;
  if (Method) {
    MemberCall = dyn_cast_or_null<CXXMemberCallExpr>(Operation);
    MemberOperation =
        MemberCall
            ? dyn_cast<BinaryOperator>(MemberCall->getCallee()->IgnoreParens())
            : nullptr;
  } else {
    MemberOperation = dyn_cast_or_null<BinaryOperator>(Operation);
  }
  if (!Dispatch || !DispatchFunction || !Body || !Return || !MemberOperation ||
      MemberOperation->getOpcode() != BO_PtrMemD ||
      DispatchFunction->getNumParams() != Call->getNumArgs() ||
      !functionalInvokeParameterReference(MemberOperation->getRHS(),
                                          DispatchFunction->getParamDecl(0)))
    return std::nullopt;
  if (ObjectWrapper) {
    const auto *AccessCall = dyn_cast_or_null<CallExpr>(
        functionalInvokeStrippedExpression(MemberOperation->getLHS()));
    const auto Access = approvedFunctionalReferenceAccessCallImpl(
        S, SM, AccessCall, Context, false);
    if (!Access ||
        Access->Wrapper.Record->getCanonicalDecl() !=
            ObjectWrapper->Record->getCanonicalDecl() ||
        !functionalInvokeParameterReference(Access->Object,
                                            DispatchFunction->getParamDecl(1)))
      return std::nullopt;
  } else if (!functionalInvokeParameterReference(
                 MemberOperation->getLHS(), DispatchFunction->getParamDecl(1),
                 ObjectIsPointer)) {
    return std::nullopt;
  }

  if (Method) {
    const auto Result = Method->getReturnType();
    const auto Referent =
        MethodReferenceResult ? Result->getPointeeType() : QualType();
    if (Method->isStatic() || !callableMethod(Method) || !Method->hasBody() ||
        !functionalMemberReceiverValueCategory(
            Method, Object, ObjectIsPointer, ObjectWrapper.has_value()) ||
        Method->getNumParams() + 2 != Call->getNumArgs() ||
        (MethodReferenceResult
             ? (Referent.isVolatileQualified() ||
                Referent.isRestrictQualified() ||
                Referent.getAddressSpace() != LangAS::Default ||
                !supportedFunctionalInvokeReference(S, SM, Context,
                                                    Referent) ||
                (Result->isLValueReferenceType() ? !Call->isLValue()
                                                 : !Call->isXValue()) ||
                !Context.hasSameType(Referent, Call->getType()))
             : (!Context.hasSameType(Result, Call->getType()) ||
                !supportedFunctionalResult(S, SM, Context, Result))) ||
        !MemberCall || MemberCall->getNumArgs() != Method->getNumParams())
      return std::nullopt;
    for (unsigned I = 0; I < Method->getNumParams(); ++I) {
      const auto Parameter = Method->getParamDecl(I)->getType();
      const auto *ArgumentExpression = Call->getArg(I + 2);
      const auto Argument = ArgumentExpression->getType();
      bool Supported = false;
      if (Parameter->isReferenceType()) {
        Supported = supportedFunctionalInvokeReferenceArgument(
            S, SM, Context, Parameter, ArgumentExpression);
      } else {
        Supported = !Parameter->isReferenceType() &&
                    supportedFunctionalByValue(S, SM, Context, Parameter) &&
                    functionalMemberValueConversion(Context, Argument,
                                                    Parameter);
      }
      if (!Supported ||
          !approvedFunctionalInvokeArgumentFlow(
              S, SM, MemberCall->getArg(I),
              DispatchFunction->getParamDecl(I + 2), Parameter, Context))
        return std::nullopt;
    }
  } else {
    const auto FieldType = Field->getType();
    const auto CallType = Call->getType();
    const bool ResultIsConst =
        FieldType.isConstQualified() ||
        (ObjectPointee.isConstQualified() && !Field->isMutable());
    if (Call->getNumArgs() != 2 || Field->isBitField() ||
        FieldType.isVolatileQualified() || FieldType.isRestrictQualified() ||
        FieldType.getAddressSpace() != LangAS::Default ||
        (!supportedFunctionalMemberValue(Context,
                                         FieldType.getUnqualifiedType()) &&
         !FieldType->isFunctionPointerType()) ||
        !Call->isGLValue() || CallType.isVolatileQualified() ||
        CallType.isRestrictQualified() ||
        CallType.getAddressSpace() != LangAS::Default ||
        CallType.isConstQualified() != ResultIsConst ||
        !Context.hasSameUnqualifiedType(CallType, FieldType))
      return std::nullopt;
  }
  return FunctionalMemberInvokeCall{WrittenCallable, Object, Method, Field,
                                    MemberDispatch ? MemberDispatch->Factory
                                                   : nullptr,
                                    MemberDispatch ? MemberDispatch->Adapter
                                                   : nullptr,
                                    std::move(ObjectWrapper),
                                    ObjectIsPointer};
}

std::optional<FunctionalMemberInvokeCall>
approvedFunctionalMemberInvokeCall(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context) {
  return approvedFunctionalMemberInvokeCallImpl(S, SM, Call, Context);
}

std::optional<FunctionalOperationInfo> approvedFunctionalInvokeObjectOperation(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context) {
  if (!Call || Call->getNumArgs() < 2 ||
      !approvedFunctionalObjectRecord(
          S, SM, Call->getArg(0)->getType()->getAsCXXRecordDecl(), Context))
    return std::nullopt;
  const auto *Dispatch =
      approvedFunctionalInvokeDispatch(S, SM, Call, Context);
  const auto *DispatchFunction =
      Dispatch ? Dispatch->getDirectCallee() : nullptr;
  if (!Dispatch || !DispatchFunction)
    return std::nullopt;

  const auto *DispatchBody = dyn_cast<CompoundStmt>(DispatchFunction->getBody());
  const auto *DispatchReturn =
      DispatchBody && DispatchBody->size() == 1
          ? dyn_cast<ReturnStmt>(*DispatchBody->body_begin())
          : nullptr;
  const auto *OperationCall =
      DispatchReturn && DispatchReturn->getRetValue()
          ? dyn_cast_or_null<CXXOperatorCallExpr>(
                functionalInvokeStrippedExpression(
                    DispatchReturn->getRetValue()))
          : nullptr;
  auto Operation = approvedFunctionalOperationImpl(
      S, SM, OperationCall, Context, false);
  if (!Operation || !OperationCall ||
      OperationCall->getNumArgs() != Call->getNumArgs() ||
      !Context.hasSameType(Operation->ResultType, Call->getType()))
    return std::nullopt;
  for (unsigned I = 1; I < Call->getNumArgs(); ++I)
    if (!utilityScalarDirectConversion(Context, Call->getArg(I)->getType(),
                                       OperationCall->getArg(I)->getType()))
      return std::nullopt;
  return Operation;
}

std::optional<FunctionalMemberInvokeCall>
approvedFunctionalUserInvokeCall(const State &S, const SourceManager &SM,
                                 const CallExpr *Call,
                                 const ASTContext &Context) {
  if (!Call || Call->getNumArgs() < 1)
    return std::nullopt;
  const auto ObjectType = Call->getArg(0)->getType();
  const auto *ObjectRecord = ObjectType->getAsCXXRecordDecl();
  const auto *Definition = ObjectRecord ? ObjectRecord->getDefinition() : nullptr;
  if (!Definition || !S.owns(SM, Definition->getLocation()) ||
      ObjectType.isVolatileQualified())
    return std::nullopt;

  const bool ReferenceResult = Call->isGLValue();
  const auto *Dispatch =
      approvedFunctionalInvokeDispatch(S, SM, Call, Context, ReferenceResult);
  const auto *DispatchFunction =
      Dispatch ? Dispatch->getDirectCallee() : nullptr;
  const auto *Body = DispatchFunction
                         ? dyn_cast<CompoundStmt>(DispatchFunction->getBody())
                         : nullptr;
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(*Body->body_begin())
                           : nullptr;
  const auto *Operation =
      Return && Return->getRetValue()
          ? dyn_cast_or_null<CXXOperatorCallExpr>(
                functionalInvokeStrippedExpression(Return->getRetValue()))
          : nullptr;
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(
      Operation ? Operation->getDirectCallee() : nullptr);
  if (!Dispatch || !DispatchFunction || !Operation || !Method ||
      Method->getOverloadedOperator() != OO_Call || Method->isStatic() ||
      !ordinaryOperator(Method) || !callableMethod(Method) ||
      !Method->hasBody() || !S.owns(SM, Method->getLocation()) ||
      Method->getParent()->getCanonicalDecl() !=
          Definition->getCanonicalDecl() ||
      Method->getNumParams() + 1 != Call->getNumArgs() ||
      Operation->getNumArgs() != Call->getNumArgs() ||
      !Context.hasSameType(Method->getReturnType(),
                           DispatchFunction->getReturnType()) ||
      !Context.hasSameType(Call->getType(), Operation->getType()) ||
      !functionalMemberReceiverValueCategory(Method, Call->getArg(0), false) ||
      (!functionalInvokeParameterReference(
           Operation->getArg(0), DispatchFunction->getParamDecl(0)) &&
       !approvedFunctionalForwardingCall(
           S, SM, Operation->getArg(0), DispatchFunction->getParamDecl(0))))
    return std::nullopt;

  const auto Result = Method->getReturnType();
  const auto Referent =
      Result->isReferenceType() ? Result->getPointeeType() : QualType();
  if (Result->isReferenceType()
          ? (Referent.isVolatileQualified() ||
             Referent.isRestrictQualified() ||
             Referent.getAddressSpace() != LangAS::Default ||
             !supportedFunctionalInvokeReference(S, SM, Context, Referent) ||
             (Result->isLValueReferenceType() ? !Call->isLValue()
                                              : !Call->isXValue()) ||
             !Context.hasSameType(Referent, Call->getType()))
          : (!Call->isPRValue() ||
             !Context.hasSameType(Result, Call->getType()) ||
             !supportedFunctionalResult(S, SM, Context, Result)))
    return std::nullopt;

  for (unsigned I = 0; I < Method->getNumParams(); ++I) {
    const auto Parameter = Method->getParamDecl(I)->getType();
    const auto *ArgumentExpression = Call->getArg(I + 1);
    const bool Supported =
        Parameter->isReferenceType()
            ? supportedFunctionalInvokeReferenceArgument(
                  S, SM, Context, Parameter, ArgumentExpression)
            : supportedFunctionalByValue(S, SM, Context, Parameter) &&
                  functionalMemberValueConversion(
                      Context, ArgumentExpression->getType(), Parameter);
    if (!Supported ||
        !approvedFunctionalInvokeArgumentFlow(
            S, SM, Operation->getArg(I + 1),
            DispatchFunction->getParamDecl(I + 1), Parameter, Context))
      return std::nullopt;
  }
  return FunctionalMemberInvokeCall{Call->getArg(0), Call->getArg(0), Method,
                                    nullptr, nullptr, nullptr, std::nullopt,
                                    false};
}

// A matching value/type is insufficient: decomposition can select a user
// specialization of either trait. Follow only the actual instantiated SDK chain.
static const ClassTemplateSpecializationDecl *tupleTraitInstance(
    const State &S, const SourceManager &SM, const CXXRecordDecl *Record,
    llvm::StringRef Name, llvm::StringRef Path) {
  const auto *Instance = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Record ? Record->getDefinition() : nullptr);
  const auto *Primary = Instance ? Instance->getSpecializedTemplate() : nullptr;
  if (!Instance || !Primary || Instance->getName() != Name ||
      Instance->getSpecializationKind() != TSK_ImplicitInstantiation ||
      Instance->isInvalidDecl() || Instance->isDependentContext())
    return nullptr;
  auto Origin = [&](const Decl *D, llvm::StringRef File) {
    return approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx", File);
  };
  for (const auto *D : Instance->redecls())
    if (!Origin(D, Path))
      return nullptr;
  const auto TraitPath = Name == "tuple_size" ? "__tuple/tuple_size.h"
                                              : "__tuple/tuple_element.h";
  for (const auto *D : Primary->redecls())
    if (!(Origin(D, TraitPath) || Origin(D, "__fwd/tuple.h")) ||
        !approvedStandardSDKDeclaration(S, SM, D->getTemplatedDecl()))
      return nullptr;
  const auto Selected = Instance->getSpecializedTemplateOrPartial();
  const auto *Partial = Selected.dyn_cast<ClassTemplatePartialSpecializationDecl *>();
  if (!Partial || !Partial->getDefinition() ||
      Partial->getSpecializedTemplate()->getCanonicalDecl() != Primary->getCanonicalDecl())
    return nullptr;
  for (const auto *D : Partial->redecls())
    if (!Origin(D, Path))
      return nullptr;
  return Instance;
}

static const VarDecl *tupleSizeTraitValue(
    const State &S, const SourceManager &SM, const CXXRecordDecl *Record,
    QualType Object, const UtilityTupleLikeSource &Tuple,
    const ASTContext &Context, unsigned Depth) {
  if (Depth > 2 || Object.isVolatileQualified())
    return nullptr;
  const auto Path = Object.isConstQualified() ? "__tuple/tuple_size.h"
      : Tuple.ArrayElements ? "array"
      : Object->getAsCXXRecordDecl()->getName() == "pair" ? "__utility/pair.h"
                                                         : "__tuple/tuple_size.h";
  const auto *Instance = tupleTraitInstance(S, SM, Record, "tuple_size", Path);
  if (!Instance || Instance->getTemplateArgs().size() != 1 ||
      Instance->getTemplateArgs()[0].getKind() != TemplateArgument::Type ||
      !Context.hasSameType(Instance->getTemplateArgs()[0].getAsType(), Object) ||
      Instance->getNumBases() != 1 || !Instance->field_empty())
    return nullptr;
  const auto &Base = *Instance->bases_begin();
  const auto *Constant = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Base.getType()->getAsCXXRecordDecl());
  const auto *Primary = Constant ? Constant->getSpecializedTemplate() : nullptr;
  const auto *Definition = Constant ? Constant->getDefinition() : nullptr;
  const auto ConstantPath = "__type_traits/integral_constant.h";
  if (!Constant || !Primary || !Definition || Constant->getName() != "integral_constant" ||
      Constant->getSpecializationKind() != TSK_ImplicitInstantiation ||
      Constant->getSpecializedTemplateOrPartial().is<ClassTemplatePartialSpecializationDecl *>() ||
      Constant->getTemplateArgs().size() != 2 || Base.isVirtual() || Base.getAccessSpecifier() != AS_public)
    return nullptr;
  for (const auto *D : Constant->redecls())
    if (!approvedStandardSDKDeclaration(S, SM, D) ||
        !cstddefOrigin(S, SM, D->getLocation(), "libcxx", ConstantPath))
      return nullptr;
  for (const auto *D : Primary->redecls())
    if (!approvedStandardSDKDeclaration(S, SM, D) ||
        !cstddefOrigin(S, SM, D->getLocation(), "libcxx", ConstantPath) ||
        !approvedStandardSDKDeclaration(S, SM, D->getTemplatedDecl()))
      return nullptr;
  const auto &Arguments = Constant->getTemplateArgs();
  if (Arguments[0].getKind() != TemplateArgument::Type ||
      !Context.hasSameType(Arguments[0].getAsType(), Context.getSizeType()) ||
      Arguments[1].getKind() != TemplateArgument::Integral ||
      Arguments[1].getAsIntegral().isNegative() ||
      Arguments[1].getAsIntegral().getLimitedValue(Tuple.size() + 1) != Tuple.size())
    return nullptr;
  const VarDecl *Value = nullptr;
  for (const auto *D : Definition->decls())
    if (const auto *V = dyn_cast<VarDecl>(D); V && V->getName() == "value") {
      if (Value || !approvedStandardSDKDeclaration(S, SM, V) ||
          !V->isStaticDataMember() || !V->isConstexpr() ||
          !Context.hasSameType(V->getType(), Context.getSizeType().withConst()))
        return nullptr;
      for (const auto *Redeclaration : V->redecls())
        if (!approvedStandardSDKDeclaration(S, SM, Redeclaration) ||
            !cstddefOrigin(S, SM, Redeclaration->getLocation(), "libcxx", ConstantPath))
          return nullptr;
      Value = V;
    }
  if (!Value)
    return nullptr;
  if (Object.isConstQualified()) {
    // The instantiated base retains the real tuple_size<T>::value qualifier;
    // use it rather than searching for an instance with the same arguments.
    const auto *BaseType = Base.getType()->getAs<TemplateSpecializationType>();
    if (!BaseType || BaseType->template_arguments().size() != 2 ||
        BaseType->template_arguments()[1].getKind() != TemplateArgument::Expression)
      return nullptr;
    const Expr *Expression = BaseType->template_arguments()[1].getAsExpr();
    if (const auto *ConstantValue = dyn_cast<ConstantExpr>(Expression))
      Expression = ConstantValue->getSubExpr();
    const auto *Load = dyn_cast<ImplicitCastExpr>(Expression);
    if (!Load || Load->getCastKind() != CK_LValueToRValue)
      return nullptr;
    const auto *Reference = dyn_cast<DeclRefExpr>(Load->getSubExpr());
    const auto *Qualifier = Reference ? Reference->getQualifier() : nullptr;
    const auto *InnerType = Qualifier ? Qualifier->getAsType() : nullptr;
    const auto *InnerValue = InnerType ? tupleSizeTraitValue(
        S, SM, InnerType->getAsCXXRecordDecl(), Object.getUnqualifiedType(),
        Tuple, Context, Depth + 1) : nullptr;
    if (!InnerValue || Reference->getDecl()->getCanonicalDecl() != InnerValue->getCanonicalDecl())
      return nullptr;
  }
  return Value;
}

bool approvedUtilityTupleLikeSizeTrait(
    const State &S, const SourceManager &SM, const CXXRecordDecl *Record,
    QualType Object, const UtilityTupleLikeSource &Tuple, const ASTContext &Context) {
  return !Object.isNull() && Object->isRecordType() &&
         tupleSizeTraitValue(S, SM, Record, Object, Tuple, Context, 0);
}

static const TypedefNameDecl *tupleElementTraitType(
    const State &S, const SourceManager &SM, const CXXRecordDecl *Record,
    QualType Object, const UtilityTupleLikeSource &Tuple, unsigned Index,
    const ASTContext &Context, unsigned Depth, bool Internal = false) {
  if (Depth > 3 || Index >= Tuple.size() || Object.isVolatileQualified())
    return nullptr;
  const bool Pair = Object->getAsCXXRecordDecl()->getName() == "pair";
  const auto Path = Object.isConstQualified() || Internal ? "__tuple/tuple_element.h"
      : Tuple.ArrayElements ? "array" : Pair ? "__utility/pair.h"
                                             : "__tuple/sfinae_helpers.h";
  const auto *Instance = tupleTraitInstance(S, SM, Record, "tuple_element", Path);
  if (!Instance || Instance->getTemplateArgs().size() != 2 || Instance->getNumBases() ||
      !Instance->field_empty())
    return nullptr;
  const auto &Args = Instance->getTemplateArgs();
  if (Args[0].getKind() != TemplateArgument::Integral || Args[0].getAsIntegral().isNegative() ||
      Args[0].getAsIntegral().getLimitedValue(Tuple.size()) != Index ||
      Args[1].getKind() != TemplateArgument::Type ||
      !Context.hasSameType(Args[1].getAsType(), Object))
    return nullptr;
  auto Expected = Tuple.elementType(Index);
  if (Object.isConstQualified() && !Expected->isReferenceType())
    Expected = Expected.withConst();
  const TypedefNameDecl *Type = nullptr;
  for (const auto *D : Instance->decls())
    if (const auto *Alias = dyn_cast<TypedefNameDecl>(D); Alias && Alias->getName() == "type") {
      if (Type || !approvedStandardSDKDeclaration(S, SM, Alias) ||
          !cstddefOrigin(S, SM, Alias->getLocation(), "libcxx", Path) ||
          !Context.hasSameType(Alias->getUnderlyingType(), Expected))
        return nullptr;
      Type = Alias;
    }
  if (!Type)
    return nullptr;
  if (Object.isConstQualified() || (!Internal && !Pair && !Tuple.ArrayElements)) {
    const auto *Underlying = Type->getUnderlyingType()->getAs<TypedefType>();
    const auto *InnerAlias = Underlying ? Underlying->getDecl() : nullptr;
    const auto *Inner = InnerAlias
        ? dyn_cast<ClassTemplateSpecializationDecl>(InnerAlias->getDeclContext()) : nullptr;
    if (!Inner || Inner->getTemplateArgs().size() != 2 ||
        Inner->getTemplateArgs()[1].getKind() != TemplateArgument::Type)
      return nullptr;
    const auto InnerObject = Inner->getTemplateArgs()[1].getAsType();
    const bool ConstForward = Object.isConstQualified();
    if (ConstForward) {
      if (!Context.hasSameType(InnerObject, Object.getUnqualifiedType()))
        return nullptr;
    } else {
      const auto *Types = dyn_cast_or_null<ClassTemplateSpecializationDecl>(InnerObject->getAsCXXRecordDecl());
      const auto *Primary = Types ? Types->getSpecializedTemplate() : nullptr;
      if (!Types || !Primary || Types->getName() != "__tuple_types" ||
          !approvedStandardSDKDeclaration(S, SM, Types) ||
          !approvedStandardSDKDeclaration(S, SM, Primary) ||
          !cstddefOrigin(S, SM, Types->getLocation(), "libcxx", "__tuple/tuple_types.h") ||
          (Types->getSpecializationKind() != TSK_ImplicitInstantiation &&
           Types->getSpecializationKind() != TSK_Undeclared) ||
          Types->getTemplateArgs().size() != 1 ||
          Types->getTemplateArgs()[0].getKind() != TemplateArgument::Pack ||
          Types->getTemplateArgs()[0].pack_size() != Tuple.size())
        return nullptr;
      unsigned I = 0;
      for (const auto &Argument : Types->getTemplateArgs()[0].pack_elements())
        if (Argument.getKind() != TemplateArgument::Type ||
            !Context.hasSameType(Argument.getAsType(), Tuple.elementType(I++)))
          return nullptr;
    }
    const auto *Checked = tupleElementTraitType(S, SM, Inner, InnerObject, Tuple,
                                                Index, Context, Depth + 1, !ConstForward);
    if (!Checked || Checked->getCanonicalDecl() != InnerAlias->getCanonicalDecl())
      return nullptr;
  }
  return Type;
}

const TypedefNameDecl *approvedUtilityTupleLikeElementTrait(
    const State &S, const SourceManager &SM, const CXXRecordDecl *Record,
    QualType Object, const UtilityTupleLikeSource &Tuple, unsigned Index,
    const ASTContext &Context) {
  return !Object.isNull() && Object->isRecordType()
      ? tupleElementTraitType(S, SM, Record, Object, Tuple, Index, Context, 0) : nullptr;
}

struct UtilityTupleApplyDispatch {
  UtilityTupleLikeSource Tuple;
  const FunctionDecl *Function;
  const CallExpr *Dispatch;
  const FunctionDecl *DispatchFunction;
  const Expr *Operation;
};

// The caller proves the argument expression; this shared check authenticates
// the exact selected SDK get and its complete type/overload identity.
bool approvedUtilityTupleLikeGet(
    const State &S, const SourceManager &SM, const CallExpr *Get,
    QualType Parameter, const UtilityTupleLikeSource &Tuple,
    unsigned Index, const ASTContext &Context) {
  const auto *Function = Get ? Get->getDirectCallee() : nullptr;
  const auto *Primary = Function ? Function->getPrimaryTemplate() : nullptr;
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Function ? Function->getDefinition() : nullptr;
  const auto *PatternDefinition = Pattern ? Pattern->getDefinition() : nullptr;
  const auto *Reference = Get ? dyn_cast_or_null<DeclRefExpr>(
                                    directFunctionReference(Get))
                              : nullptr;
  const auto *Arguments =
      Function ? Function->getTemplateSpecializationArgs() : nullptr;
  if (!Get || Get->getNumArgs() != 1 || !Function || !Primary || !Pattern ||
      !Reference || !Arguments || Arguments->size() < 2 ||
      !Function->getIdentifier() || Function->getName() != "get" ||
      Function->isVariadic() || Function->getNumParams() != 1 ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !PatternDefinition || Parameter.isNull() ||
      !Parameter->isReferenceType() || Index >= Tuple.size() ||
      !approvedStandardSDKDeclaration(S, SM, Function) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !approvedStandardSDKDeclaration(S, SM, Pattern) ||
      !approvedStandardSDKDeclaration(S, SM, Reference->getDecl()) ||
      (Definition && !approvedStandardSDKDeclaration(S, SM, Definition)) ||
      !Context.hasSameType(Function->getParamDecl(0)->getType(), Parameter) ||
      !Context.hasSameType(Get->getArg(0)->getType(),
                           Parameter->getPointeeType()) ||
      Arguments->get(0).getKind() != TemplateArgument::Integral ||
      Arguments->get(0).getAsIntegral().isNegative() ||
      Arguments->get(0).getAsIntegral().getLimitedValue(Tuple.size()) != Index)
    return false;

  const auto *Record = Parameter->getPointeeType()->getAsCXXRecordDecl();
  const bool Pair = Record && Record->getName() == "pair";
  const llvm::StringRef Path = Tuple.ArrayElements ? "array"
                               : Pair ? "__utility/pair.h"
                                      : "tuple";
  const llvm::StringRef ForwardPath = Tuple.ArrayElements ? "__fwd/array.h"
                                     : Pair ? "__fwd/pair.h"
                                            : "__fwd/tuple.h";
  auto ApprovedDeclaration = [&](const Decl *Declaration) {
    return approvedStandardSDKDeclaration(S, SM, Declaration) &&
           (cstddefOrigin(S, SM, Declaration->getLocation(), "libcxx", Path) ||
            cstddefOrigin(S, SM, Declaration->getLocation(), "libcxx",
                           ForwardPath));
  };
  // Qualified lookup in apply can retain get's forward declaration when its
  // defining header is included later. Authenticate that exact redeclaration
  // chain and its real definition before replacing the call with a projection.
  if (!approvedStandardSDKDeclaration(S, SM, PatternDefinition) ||
      !cstddefOrigin(S, SM, PatternDefinition->getLocation(), "libcxx", Path) ||
      (Definition &&
       !cstddefOrigin(S, SM, Definition->getLocation(), "libcxx", Path)))
    return false;
  for (const auto *Redeclaration : Function->redecls())
    if (!ApprovedDeclaration(Redeclaration))
      return false;
  for (const auto *Redeclaration : Primary->redecls())
    if (!ApprovedDeclaration(Redeclaration) ||
        !ApprovedDeclaration(Redeclaration->getTemplatedDecl()))
      return false;
  for (const auto *Redeclaration : Pattern->redecls())
    if (!ApprovedDeclaration(Redeclaration))
      return false;
  if (Tuple.ArrayElements) {
    if (Arguments->size() != 3 ||
        Arguments->get(1).getKind() != TemplateArgument::Type ||
        Arguments->get(2).getKind() != TemplateArgument::Integral ||
        !Context.hasSameType(Arguments->get(1).getAsType(),
                             Tuple.ArrayElementType) ||
        Arguments->get(2).getAsIntegral().isNegative() ||
        Arguments->get(2).getAsIntegral().getLimitedValue(Tuple.size() + 1) !=
            Tuple.size())
      return false;
  } else if (Pair) {
    if (Arguments->size() != 3 || Tuple.size() != 2)
      return false;
    for (unsigned I = 0; I != 2; ++I)
      if (Arguments->get(I + 1).getKind() != TemplateArgument::Type ||
          !Context.hasSameType(Arguments->get(I + 1).getAsType(),
                               Tuple.elementType(I)))
        return false;
  } else {
    if (Arguments->size() != 2 ||
        Arguments->get(1).getKind() != TemplateArgument::Pack ||
        Arguments->get(1).pack_size() != Tuple.size())
      return false;
    unsigned I = 0;
    for (const auto &Argument : Arguments->get(1).pack_elements())
      if (Argument.getKind() != TemplateArgument::Type ||
          !Context.hasSameType(Argument.getAsType(), Tuple.elementType(I++)))
        return false;
  }

  const auto Stored = Tuple.elementType(Index);
  auto Element = Stored->isReferenceType() ? Stored->getPointeeType() : Stored;
  if (!Stored->isReferenceType() &&
      Parameter->getPointeeType().isConstQualified())
    Element = Element.withConst();
  const bool LValue = Stored->isLValueReferenceType() ||
                      Parameter->isLValueReferenceType();
  const auto Result = LValue ? Context.getLValueReferenceType(Element)
                             : Context.getRValueReferenceType(Element);
  return Context.hasSameType(Function->getReturnType(), Result) &&
         Context.hasSameType(Get->getType(), Element) &&
         (LValue ? Get->isLValue() : Get->isXValue());
}

// apply additionally requires its exact forwarding parameter path.
static bool approvedUtilityTupleApplyElement(
    const State &S, const SourceManager &SM, const Expr *Expression,
    const ParmVarDecl *TupleParameter, const UtilityTupleLikeSource &Tuple,
    unsigned Index, const ASTContext &Context) {
  const auto *Get = dyn_cast_or_null<CallExpr>(
      functionalInvokeStrippedExpression(Expression));
  return Get && Get->getNumArgs() == 1 && TupleParameter &&
         approvedFunctionalForwardingCall(S, SM, Get->getArg(0), TupleParameter) &&
         approvedUtilityTupleLikeGet(S, SM, Get, TupleParameter->getType(),
                                     Tuple, Index, Context);
}

static std::optional<UtilityTupleApplyDispatch>
approvedUtilityTupleApplyDispatch(const State &S, const SourceManager &SM,
                                  const CallExpr *Call,
                                  const ASTContext &Context) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  const auto *Primary = Function ? Function->getPrimaryTemplate() : nullptr;
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto Origin =
      Primary ? S.sdkFile(SM, Primary->getLocation()) : std::nullopt;
  const auto *Reference =
      Call ? dyn_cast_or_null<DeclRefExpr>(directFunctionReference(Call))
           : nullptr;
  auto Tuple = Call && Call->getNumArgs() == 2
                   ? approvedUtilityTupleLikeSource(
                         S, SM, Call->getArg(1)->getType(), Context)
                   : std::nullopt;
  if (!Call || Call->getNumArgs() != 2 || !Function || !Primary || !Pattern ||
      !Origin || Origin->Root != "libcxx" || Origin->Path != "tuple" ||
      !Function->getIdentifier() || Function->getName() != "apply" ||
      Function->isVariadic() || Function->getNumParams() != 2 ||
      !Function->hasBody() || !Pattern->hasBody() || !Reference || !Tuple ||
      !approvedStandardSDKDeclaration(S, SM, Function) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !approvedStandardSDKDeclaration(S, SM, Pattern) ||
      !approvedStandardSDKDeclaration(S, SM, Reference->getDecl()) ||
      !approvedUtilityReference(S, SM, Call, Function))
    return std::nullopt;

  const auto *Body = dyn_cast<CompoundStmt>(Function->getBody());
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(*Body->body_begin())
                           : nullptr;
  const auto *Helper =
      Return && Return->getRetValue()
          ? dyn_cast_or_null<CallExpr>(
                functionalInvokeStrippedExpression(Return->getRetValue()))
          : nullptr;
  const auto *HelperFunction = Helper ? Helper->getDirectCallee() : nullptr;
  const auto *HelperPrimary =
      HelperFunction ? HelperFunction->getPrimaryTemplate() : nullptr;
  const auto *HelperPattern =
      HelperPrimary ? HelperPrimary->getTemplatedDecl() : nullptr;
  const auto HelperOrigin = HelperPrimary
                                ? S.sdkFile(SM, HelperPrimary->getLocation())
                                : std::nullopt;
  const auto *HelperReference =
      Helper ? dyn_cast_or_null<DeclRefExpr>(directFunctionReference(Helper))
             : nullptr;
  const auto *HelperArguments =
      HelperFunction ? HelperFunction->getTemplateSpecializationArgs()
                     : nullptr;
  if (!Helper || !HelperFunction || !HelperPrimary || !HelperPattern ||
      !HelperOrigin || HelperOrigin->Root != "libcxx" ||
      HelperOrigin->Path != "tuple" || !HelperReference ||
      !HelperFunction->getIdentifier() ||
      HelperFunction->getName() != "__apply_tuple_impl" ||
      HelperFunction->isVariadic() || HelperFunction->getNumParams() != 3 ||
      Helper->getNumArgs() != 3 || !HelperFunction->hasBody() ||
      !HelperPattern->hasBody() || !HelperArguments ||
      HelperArguments->size() != 3 ||
      HelperArguments->get(2).getKind() != TemplateArgument::Pack ||
      HelperArguments->get(2).pack_size() != Tuple->size() ||
      !Context.hasSameType(Helper->getType(), Call->getType()) ||
      !Context.hasSameType(HelperFunction->getReturnType(),
                           Function->getReturnType()) ||
      !approvedStandardSDKDeclaration(S, SM, HelperFunction) ||
      !approvedStandardSDKDeclaration(S, SM, HelperPrimary) ||
      !approvedStandardSDKDeclaration(S, SM, HelperPattern) ||
      !approvedStandardSDKDeclaration(S, SM, HelperReference->getDecl()) ||
      !approvedFunctionalForwardingCall(S, SM, Helper->getArg(0),
                                        Function->getParamDecl(0)) ||
      !approvedFunctionalForwardingCall(S, SM, Helper->getArg(1),
                                        Function->getParamDecl(1)))
    return std::nullopt;
  unsigned ExpectedIndex = 0;
  for (const auto &Argument : HelperArguments->get(2).pack_elements()) {
    if (Argument.getKind() != TemplateArgument::Integral ||
        Argument.getAsIntegral().isNegative() ||
        Argument.getAsIntegral().getLimitedValue(Tuple->size() + 1) !=
            ExpectedIndex++)
      return std::nullopt;
  }

  const auto *HelperBody = dyn_cast<CompoundStmt>(HelperFunction->getBody());
  const auto *HelperReturn =
      HelperBody && HelperBody->size() == 1
          ? dyn_cast<ReturnStmt>(*HelperBody->body_begin())
          : nullptr;
  const auto *Dispatch =
      HelperReturn && HelperReturn->getRetValue()
          ? dyn_cast_or_null<CallExpr>(
                functionalInvokeStrippedExpression(HelperReturn->getRetValue()))
          : nullptr;
  const auto *DispatchFunction =
      Dispatch ? Dispatch->getDirectCallee() : nullptr;
  const auto *DispatchPrimary =
      DispatchFunction ? DispatchFunction->getPrimaryTemplate() : nullptr;
  const auto *DispatchPattern =
      DispatchPrimary ? DispatchPrimary->getTemplatedDecl() : nullptr;
  const auto DispatchOrigin =
      DispatchPrimary ? S.sdkFile(SM, DispatchPrimary->getLocation())
                      : std::nullopt;
  const auto *DispatchReference =
      Dispatch
          ? dyn_cast_or_null<DeclRefExpr>(directFunctionReference(Dispatch))
          : nullptr;
  if (!Dispatch || !DispatchFunction || !DispatchPrimary || !DispatchPattern ||
      !DispatchOrigin || DispatchOrigin->Root != "libcxx" ||
      DispatchOrigin->Path != "__type_traits/invoke.h" || !DispatchReference ||
      !DispatchFunction->getIdentifier() ||
      DispatchFunction->getName() != "__invoke" ||
      DispatchFunction->isVariadic() || !DispatchFunction->hasBody() ||
      !DispatchPattern->hasBody() ||
      Dispatch->getNumArgs() != Tuple->size() + 1 ||
      Dispatch->getNumArgs() != DispatchFunction->getNumParams() ||
      !Context.hasSameType(Dispatch->getType(), Call->getType()) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchFunction) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchPrimary) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchPattern) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchReference->getDecl()) ||
      !approvedFunctionalForwardingCall(S, SM, Dispatch->getArg(0),
                                        HelperFunction->getParamDecl(0)))
    return std::nullopt;

  for (unsigned I = 0; I < Tuple->size(); ++I)
    if (!approvedUtilityTupleApplyElement(
            S, SM, Dispatch->getArg(I + 1), HelperFunction->getParamDecl(1),
            *Tuple, I, Context))
      return std::nullopt;

  const auto *DispatchBody =
      dyn_cast<CompoundStmt>(DispatchFunction->getBody());
  const auto *DispatchReturn =
      DispatchBody && DispatchBody->size() == 1
          ? dyn_cast<ReturnStmt>(*DispatchBody->body_begin())
          : nullptr;
  const auto *Operation =
      DispatchReturn && DispatchReturn->getRetValue()
          ? functionalInvokeStrippedExpression(DispatchReturn->getRetValue())
          : nullptr;
  if (!Operation)
    return std::nullopt;
  return UtilityTupleApplyDispatch{*Tuple, Function, Dispatch,
                                   DispatchFunction, Operation};
}

std::optional<FunctionalMemberInvokeCall>
approvedUtilityTupleApplyUserCall(const State &S, const SourceManager &SM,
                                  const CallExpr *Call,
                                  const ASTContext &Context) {
  const auto Apply = approvedUtilityTupleApplyDispatch(S, SM, Call, Context);
  if (!Apply)
    return std::nullopt;
  const auto *Tuple = &Apply->Tuple;
  const auto *Function = Apply->Function;
  const auto *DispatchFunction = Apply->DispatchFunction;
  const auto *Operation =
      dyn_cast_or_null<CXXOperatorCallExpr>(Apply->Operation);
  const auto ObjectType = Call->getArg(0)->getType();
  const auto *ObjectRecord = ObjectType->getAsCXXRecordDecl();
  const auto *Definition =
      ObjectRecord ? ObjectRecord->getDefinition() : nullptr;
  if (!Definition || !S.owns(SM, Definition->getLocation()) ||
      ObjectType.isVolatileQualified())
    return std::nullopt;
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(
      Operation ? Operation->getDirectCallee() : nullptr);
  if (!Operation || !Method || Method->getOverloadedOperator() != OO_Call ||
      Method->isStatic() || !ordinaryOperator(Method) ||
      !callableMethod(Method) || !Method->hasBody() ||
      !S.owns(SM, Method->getLocation()) ||
      Method->getParent()->getCanonicalDecl() !=
          Definition->getCanonicalDecl() ||
      Method->getNumParams() != Tuple->size() ||
      Operation->getNumArgs() != Tuple->size() + 1 ||
      !Context.hasSameType(Method->getReturnType(),
                           Function->getReturnType()) ||
      !Context.hasSameType(Operation->getType(), Call->getType()) ||
      !functionalMemberReceiverValueCategory(Method, Call->getArg(0), false) ||
      !approvedFunctionalInvokeArgumentFlow(S, SM, Operation->getArg(0),
                                            DispatchFunction->getParamDecl(0),
                                            ObjectType, Context))
    return std::nullopt;

  const auto Result = Method->getReturnType();
  const auto Referent =
      Result->isReferenceType() ? Result->getPointeeType() : QualType();
  if (Result->isReferenceType()
          ? (Referent.isVolatileQualified() || Referent.isRestrictQualified() ||
             Referent.getAddressSpace() != LangAS::Default ||
             !supportedFunctionalInvokeReference(S, SM, Context, Referent) ||
             (Result->isLValueReferenceType() ? !Call->isLValue()
                                              : !Call->isXValue()) ||
             !Context.hasSameType(Referent, Call->getType()))
          : (!Call->isPRValue() ||
             !Context.hasSameType(Result, Call->getType()) ||
             !supportedFunctionalResult(S, SM, Context, Result)))
    return std::nullopt;

  const auto TupleArgument = Call->getArg(1)->getType();
  for (unsigned I = 0; I < Method->getNumParams(); ++I) {
    const auto Parameter = Method->getParamDecl(I)->getType();
    const auto StoredElement = Tuple->elementType(I);
    const auto Element = StoredElement->isReferenceType()
                             ? StoredElement->getPointeeType()
                             : StoredElement;
    bool Supported = false;
    if (Parameter->isReferenceType()) {
      const auto ParameterReferent = Parameter->getPointeeType();
      const bool ElementIsLValue =
          StoredElement->isLValueReferenceType() ||
          Call->getArg(1)->isLValue();
      const bool Category = Parameter->isLValueReferenceType()
                                ? (ElementIsLValue ||
                                   ParameterReferent.isConstQualified())
                                : !ElementIsLValue;
      Supported = Category &&
                  supportedFunctionalInvokeReference(S, SM, Context,
                                                     ParameterReferent) &&
                  Context.hasSameUnqualifiedType(ParameterReferent, Element) &&
                  (!StoredElement->isReferenceType() ||
                   ParameterReferent.isAtLeastAsQualifiedAs(Element,
                                                            Context)) &&
                  !ParameterReferent.isVolatileQualified() &&
                  (StoredElement->isReferenceType() ||
                   ParameterReferent.isConstQualified() ||
                   !TupleArgument.isConstQualified());
    } else {
      Supported = supportedFunctionalByValue(S, SM, Context, Parameter) &&
                  functionalMemberValueConversion(Context, Element, Parameter);
    }
    if (!Supported ||
        !approvedFunctionalInvokeArgumentFlow(
            S, SM, Operation->getArg(I + 1),
            DispatchFunction->getParamDecl(I + 1), Parameter, Context))
      return std::nullopt;
  }
  return FunctionalMemberInvokeCall{
      Call->getArg(0), Call->getArg(0), Method,       nullptr,
      nullptr,         nullptr,         std::nullopt, false};
}

std::optional<FunctionalOperationInfo>
approvedUtilityTupleApplyObjectOperation(const State &S,
                                         const SourceManager &SM,
                                         const CallExpr *Call,
                                         const ASTContext &Context) {
  const auto Apply = approvedUtilityTupleApplyDispatch(S, SM, Call, Context);
  if (!Apply)
    return std::nullopt;
  const auto *OperationCall =
      dyn_cast_or_null<CXXOperatorCallExpr>(Apply->Operation);
  if (!OperationCall)
    return std::nullopt;
  auto Operation = approvedFunctionalOperationImpl(
      S, SM, OperationCall, Context, false);
  const bool Unary = Operation && Operation->RightType.isNull();
  const unsigned Arity = Unary ? 1u : 2u;
  if (!Operation || Apply->Tuple.size() != Arity ||
      OperationCall->getNumArgs() != Arity + 1 ||
      !Context.hasSameType(Operation->ResultType,
                           Apply->Function->getReturnType()) ||
      !Context.hasSameType(Operation->ResultType, Call->getType()))
    return std::nullopt;
  for (unsigned I = 0; I < Arity; ++I)
    if (const auto Stored = Apply->Tuple.elementType(I);
        !utilityScalarDirectConversion(
            Context,
            Stored->isReferenceType() ? Stored->getPointeeType() : Stored,
            OperationCall->getArg(I + 1)->getType()))
      return std::nullopt;
  return Operation;
}

std::optional<FunctionalMemberInvokeCall>
approvedUtilityTupleApplyMemberCall(const State &S, const SourceManager &SM,
                                    const CallExpr *Call,
                                    const ASTContext &Context) {
  const auto Apply = approvedUtilityTupleApplyDispatch(S, SM, Call, Context);
  if (!Apply)
    return std::nullopt;
  const CallExpr *UseAdapter = nullptr;
  const auto *FactoryExpression = functionalMemFnAdaptedUse(
      S, SM, Call->getArg(0), Context, UseAdapter);
  const auto *StoredReference =
      dyn_cast_or_null<DeclRefExpr>(FactoryExpression);
  const auto *StoredVariable =
      StoredReference ? dyn_cast<VarDecl>(StoredReference->getDecl()) : nullptr;
  const auto StoredObject = approvedFunctionalStoredMemFn(
      S, SM, StoredVariable, Context);
  const auto *Factory =
      StoredObject ? StoredObject->Factory
                   : dyn_cast_or_null<CallExpr>(FactoryExpression);
  const CallExpr *Invoked = nullptr;
  std::optional<FunctionalMemberDispatch> Dispatch;
  if (Factory) {
    const auto *Operation =
        dyn_cast_or_null<CXXOperatorCallExpr>(Apply->Operation);
    if (!Operation ||
        (!functionalInvokeParameterReference(
             Operation->getArg(0),
             Apply->DispatchFunction->getParamDecl(0)) &&
         !approvedFunctionalForwardingCall(
             S, SM, Operation->getArg(0),
             Apply->DispatchFunction->getParamDecl(0))))
      return std::nullopt;
    Dispatch = approvedFunctionalMemFnDispatch(
        S, SM, Operation, Context, Factory);
    Invoked = Operation;
  } else {
    const auto [Address, Member] = functionalStoredMemberExpression(
        S, SM, FactoryExpression, Context);
    if (!Address || !Member ||
        !Context.hasSameUnqualifiedType(
            FactoryExpression->getType(), Apply->Dispatch->getArg(0)->getType()))
      return std::nullopt;
    Dispatch = FunctionalMemberDispatch{Call->getArg(0), nullptr,
                                        Apply->Dispatch, UseAdapter};
    Invoked = Apply->Dispatch;
  }
  if (!Dispatch)
    return std::nullopt;
  Dispatch->Adapter = UseAdapter;
  auto Member = approvedFunctionalMemberInvokeCallImpl(
      S, SM, Invoked, Context, std::move(Dispatch));
  if (!Member || Invoked->getNumArgs() != Apply->Tuple.size() + 1)
    return std::nullopt;
  for (unsigned I = 0; I < Apply->Tuple.size(); ++I) {
    const auto Stored = Apply->Tuple.elementType(I);
    const auto Element =
        Stored->isReferenceType() ? Stored->getPointeeType() : Stored;
    if (!Context.hasSameUnqualifiedType(Element,
                                        Invoked->getArg(I + 1)->getType()))
      return std::nullopt;
  }
  return Member;
}

static std::optional<FunctionalReferenceInvokeCall>
approvedFunctionalReferenceDirectInvoke(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context, bool RequireOwnedReference) {
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call ? Call->getDirectCallee() : nullptr);
  const auto Wrapper = approvedFunctionalReferenceRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  const auto *Reference = Call ? directMethodReference(Call) : nullptr;
  const auto *Primary = Method ? Method->getPrimaryTemplate() : nullptr;
  const auto *Pattern =
      Primary ? dyn_cast<CXXMethodDecl>(Primary->getTemplatedDecl()) : nullptr;
  const auto *TemplateParameters =
      Primary ? Primary->getTemplateParameters() : nullptr;
  const auto *Pack =
      TemplateParameters && TemplateParameters->size() == 1
          ? dyn_cast<TemplateTypeParmDecl>(TemplateParameters->getParam(0))
          : nullptr;
  const auto MethodResult = Method ? Method->getReturnType() : QualType();
  const bool ReferenceResult =
      !MethodResult.isNull() && MethodResult->isReferenceType();
  const auto ExpressionResult =
      ReferenceResult ? MethodResult->getPointeeType() : MethodResult;
  if (!Call || !Operator || !Method || !Wrapper || !Reference || !Primary ||
      !Pattern || !Pack || !Pack->isParameterPack() ||
      Operator->getOperator() != OO_Call ||
      Method->getOverloadedOperator() != OO_Call || Method->isStatic() ||
      !Method->isConst() || Method->isVariadic() || !Method->isInlined() ||
      !Method->hasBody() ||
      (ReferenceResult
           ? (MethodResult->isLValueReferenceType() ? !Call->isLValue()
                                                    : !Call->isXValue())
           : !Call->isPRValue()) ||
      Call->getNumArgs() != Method->getNumParams() + 1 ||
      !Context.hasSameType(Call->getType(), ExpressionResult) ||
      !Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                      Context.getRecordType(Wrapper->Record)) ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !approvedStandardSDKDeclaration(S, SM, Pattern) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h") ||
      !cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h") ||
      !cstddefOrigin(S, SM, Pattern->getLocation(), "libcxx",
                     "__functional/reference_wrapper.h") ||
      (RequireOwnedReference && !S.owns(SM, Reference->getExprLoc())))
    return std::nullopt;
  for (unsigned I = 0; I < Method->getNumParams(); ++I) {
    const auto Parameter = Method->getParamDecl(I)->getType();
    const auto *Actual = Call->getArg(I + 1);
    if (!Parameter->isReferenceType() ||
        Parameter->getPointeeType().isVolatileQualified() ||
        !Context.hasSameType(Parameter->getPointeeType(),
                             Actual->getType()) ||
        (Parameter->isLValueReferenceType() ? !Actual->isLValue()
                                            : Actual->isLValue()))
      return std::nullopt;
  }

  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  const auto *Return =
      Body && Body->size() == 1
          ? dyn_cast<ReturnStmt>(*Body->body_begin())
          : nullptr;
  const auto *Dispatch =
      Return && Return->getRetValue()
          ? dyn_cast_or_null<CallExpr>(functionalInvokeStrippedExpression(
                Return->getRetValue()))
          : nullptr;
  const auto *DispatchFunction = Dispatch ? Dispatch->getDirectCallee() : nullptr;
  const auto *DispatchPrimary =
      DispatchFunction ? DispatchFunction->getPrimaryTemplate() : nullptr;
  const auto *DispatchPattern =
      DispatchPrimary ? DispatchPrimary->getTemplatedDecl() : nullptr;
  const auto DispatchOrigin = DispatchPrimary
                                  ? S.sdkFile(SM, DispatchPrimary->getLocation())
                                  : std::nullopt;
  const auto *DispatchReference =
      Dispatch ? dyn_cast_or_null<DeclRefExpr>(directFunctionReference(Dispatch))
               : nullptr;
  if (!Dispatch || !DispatchFunction || !DispatchPrimary || !DispatchPattern ||
      !DispatchReference || !DispatchFunction->getIdentifier() ||
      DispatchFunction->getName() != "__invoke" || !DispatchOrigin ||
      DispatchOrigin->Root != "libcxx" ||
      DispatchOrigin->Path != "__type_traits/invoke.h" ||
      DispatchFunction->isVariadic() || !DispatchFunction->isInlined() ||
      !DispatchFunction->isConstexpr() || !DispatchFunction->hasBody() ||
      !DispatchPattern->hasBody() ||
      Dispatch->getNumArgs() != DispatchFunction->getNumParams() ||
      Dispatch->getNumArgs() != Call->getNumArgs() ||
      !Context.hasSameType(Dispatch->getType(), Call->getType()) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchFunction) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchPrimary) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchPattern) ||
      !approvedStandardSDKDeclaration(S, SM, DispatchReference->getDecl()))
    return std::nullopt;

  const auto Access = approvedFunctionalReferenceAccessCallImpl(
      S, SM, dyn_cast<CallExpr>(Dispatch->getArg(0)->IgnoreParenImpCasts()),
      Context, false);
  const auto *AccessObject =
      Access ? Access->Object->IgnoreParenImpCasts() : nullptr;
  if (!Access ||
      Access->Wrapper.Record->getCanonicalDecl() !=
          Wrapper->Record->getCanonicalDecl() ||
      !isa_and_nonnull<CXXThisExpr>(AccessObject))
    return std::nullopt;
  for (unsigned I = 0; I < Method->getNumParams(); ++I) {
    const auto *Forward = dyn_cast<CallExpr>(
        Dispatch->getArg(I + 1)->IgnoreParenImpCasts());
    const auto *Function = Forward ? Forward->getDirectCallee() : nullptr;
    const auto *ForwardPrimary =
        Function ? Function->getPrimaryTemplate() : nullptr;
    const auto *ForwardReference =
        dyn_cast_or_null<DeclRefExpr>(directFunctionReference(Forward));
    const auto *Argument =
        Forward && Forward->getNumArgs() == 1
            ? dyn_cast<DeclRefExpr>(
                  Forward->getArg(0)->IgnoreParenImpCasts())
            : nullptr;
    if (!Function || !ForwardPrimary || !ForwardReference || !Argument ||
        !Function->getIdentifier() || Function->getName() != "forward" ||
        Argument->getDecl() != Method->getParamDecl(I) ||
        !approvedStandardSDKDeclaration(S, SM, Function) ||
        !approvedStandardSDKDeclaration(S, SM, ForwardPrimary) ||
        !approvedStandardSDKDeclaration(S, SM, ForwardReference->getDecl()) ||
        !cstddefOrigin(S, SM, ForwardPrimary->getLocation(), "libcxx",
                       "__utility/forward.h"))
      return std::nullopt;
  }

  const auto *DispatchBody = dyn_cast<CompoundStmt>(DispatchFunction->getBody());
  const auto *DispatchReturn =
      DispatchBody && DispatchBody->size() == 1
          ? dyn_cast<ReturnStmt>(*DispatchBody->body_begin())
          : nullptr;
  const auto *Invoked =
      DispatchReturn && DispatchReturn->getRetValue()
          ? functionalInvokeStrippedExpression(
                DispatchReturn->getRetValue())
          : nullptr;
  if (!Invoked || !Context.hasSameType(Invoked->getType(), Call->getType()) ||
      (ReferenceResult
           ? (MethodResult->isLValueReferenceType() ? !Invoked->isLValue()
                                                    : !Invoked->isXValue())
           : !Invoked->isPRValue()))
    return std::nullopt;

  if (approvedFunctionalObjectRecord(
          S, SM, Wrapper->ReferentType->getAsCXXRecordDecl(), Context)) {
    const auto *OperationCall = dyn_cast<CXXOperatorCallExpr>(Invoked);
    auto Operation = approvedFunctionalOperationImpl(
        S, SM, OperationCall, Context, false);
    if (!Operation || !OperationCall ||
        OperationCall->getNumArgs() != Call->getNumArgs() ||
        !Context.hasSameType(Operation->ResultType, Call->getType()))
      return std::nullopt;
    for (unsigned I = 1; I < Call->getNumArgs(); ++I)
      if (!utilityScalarDirectConversion(
              Context, Call->getArg(I)->getType(),
              OperationCall->getArg(I)->getType()))
        return std::nullopt;
    return FunctionalReferenceInvokeCall{
        *Wrapper, FunctionalReferenceInvokeKind::FunctionObject, {},
        std::move(Operation), nullptr};
  }

  const auto *ReferentRecord =
      Wrapper->ReferentType->getAsCXXRecordDecl();
  const auto *ReferentDefinition =
      ReferentRecord ? ReferentRecord->getDefinition() : nullptr;
  const auto *OperationCall = dyn_cast<CXXOperatorCallExpr>(Invoked);
  const auto *OperationMethod = dyn_cast_or_null<CXXMethodDecl>(
      OperationCall ? OperationCall->getDirectCallee() : nullptr);
  if (ReferentDefinition &&
      S.owns(SM, ReferentDefinition->getLocation()) && OperationCall &&
      OperationMethod && OperationMethod->getOverloadedOperator() == OO_Call &&
      !OperationMethod->isStatic() && ordinaryOperator(OperationMethod) &&
      callableMethod(OperationMethod) && OperationMethod->hasBody() &&
      S.owns(SM, OperationMethod->getLocation()) &&
      OperationMethod->getParent()->getCanonicalDecl() ==
          ReferentDefinition->getCanonicalDecl() &&
      OperationMethod->getRefQualifier() != RQ_RValue &&
      OperationMethod->getNumParams() + 1 == Call->getNumArgs() &&
      OperationCall->getNumArgs() == Call->getNumArgs() &&
      Context.hasSameType(OperationMethod->getReturnType(), MethodResult) &&
      (functionalInvokeParameterReference(
           OperationCall->getArg(0), DispatchFunction->getParamDecl(0)) ||
       approvedFunctionalForwardingCall(
           S, SM, OperationCall->getArg(0),
           DispatchFunction->getParamDecl(0)))) {
    const auto Result = OperationMethod->getReturnType();
    const auto Referent =
        Result->isReferenceType() ? Result->getPointeeType() : QualType();
    bool Supported =
        Result->isReferenceType()
            ? !Referent.isVolatileQualified() &&
                  !Referent.isRestrictQualified() &&
                  Referent.getAddressSpace() == LangAS::Default &&
                  supportedFunctionalInvokeReference(S, SM, Context,
                                                     Referent) &&
                  (Result->isLValueReferenceType() ? Call->isLValue()
                                                   : Call->isXValue()) &&
                  Context.hasSameType(Referent, Call->getType())
            : Call->isPRValue() &&
                  Context.hasSameType(Result, Call->getType()) &&
                  supportedFunctionalResult(S, SM, Context, Result);
    for (unsigned I = 0; Supported && I < OperationMethod->getNumParams(); ++I) {
      const auto Parameter = OperationMethod->getParamDecl(I)->getType();
      const auto *ArgumentExpression = Call->getArg(I + 1);
      Supported =
          (Parameter->isReferenceType()
               ? supportedFunctionalInvokeReferenceArgument(
                     S, SM, Context, Parameter, ArgumentExpression)
               : supportedFunctionalByValue(S, SM, Context, Parameter) &&
                     functionalMemberValueConversion(
                         Context, ArgumentExpression->getType(), Parameter)) &&
          approvedFunctionalInvokeArgumentFlow(
              S, SM, OperationCall->getArg(I + 1),
              DispatchFunction->getParamDecl(I + 1), Parameter, Context);
    }
    if (Supported)
      return FunctionalReferenceInvokeCall{
          *Wrapper, FunctionalReferenceInvokeKind::UserFunctionObject, {},
          std::nullopt, OperationMethod};
  }

  const bool FunctionReferent = Wrapper->ReferentType->isFunctionType();
  const auto PointerType = FunctionReferent ? Wrapper->PointerType
                                            : Wrapper->ReferentType;
  const auto *Prototype =
      FunctionReferent
          ? Wrapper->ReferentType->getAs<FunctionProtoType>()
          : PointerType->isFunctionPointerType()
                ? PointerType->getPointeeType()->getAs<FunctionProtoType>()
                : nullptr;
  const auto *Indirect = dyn_cast<CallExpr>(Invoked);
  if (!Prototype || Prototype->isVariadic() || !Indirect ||
      Indirect->getDirectCallee() ||
      Prototype->getNumParams() != Method->getNumParams() ||
      Indirect->getNumArgs() != Prototype->getNumParams() ||
      !Context.hasSameType(Prototype->getReturnType(), MethodResult) ||
      (ReferenceResult
           ? !supportedFunctionalInvokeReference(S, SM, Context, ExpressionResult)
           : !supportedFunctionalResult(S, SM, Context,
                                        Call->getType())))
    return std::nullopt;
  for (unsigned I = 0; I < Prototype->getNumParams(); ++I) {
    const auto Parameter = Prototype->getParamType(I);
    const auto *ArgumentExpression = Call->getArg(I + 1);
    const auto Argument = ArgumentExpression->getType();
    const bool Supported =
        Parameter->isReferenceType()
            ? supportedFunctionalInvokeReferenceArgument(S, SM, Context, Parameter,
                                                         ArgumentExpression)
            : !Parameter->isReferenceType() &&
                  supportedFunctionalByValue(S, SM, Context, Parameter) &&
                  functionalMemberValueConversion(Context, Argument,
                                                  Parameter);
    if (!Supported)
      return std::nullopt;
  }
  return FunctionalReferenceInvokeCall{
      *Wrapper,
      FunctionReferent ? FunctionalReferenceInvokeKind::Function
                       : FunctionalReferenceInvokeKind::FunctionPointer,
      PointerType,
      std::nullopt,
      nullptr};
}

std::optional<FunctionalReferenceInvokeCall>
approvedFunctionalReferenceInvokeCall(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ASTContext &Context) {
  if (auto Direct = approvedFunctionalReferenceDirectInvoke(
          S, SM, Call, Context, true))
    return Direct;
  const auto *Dispatch = approvedFunctionalInvokeDispatch(
      S, SM, Call, Context, Call && Call->isGLValue());
  const auto Wrapper =
      Call && Call->getNumArgs()
          ? approvedFunctionalReferenceRecord(
                S, SM, Call->getArg(0)->getType()->getAsCXXRecordDecl(),
                Context)
          : std::nullopt;
  const auto *DispatchFunction = Dispatch ? Dispatch->getDirectCallee() : nullptr;
  const auto *Body =
      DispatchFunction
          ? dyn_cast<CompoundStmt>(DispatchFunction->getBody())
          : nullptr;
  const auto *Return =
      Body && Body->size() == 1
          ? dyn_cast<ReturnStmt>(*Body->body_begin())
          : nullptr;
  const auto *InnerCall =
      Return && Return->getRetValue()
          ? dyn_cast_or_null<CXXOperatorCallExpr>(
                functionalInvokeStrippedExpression(Return->getRetValue()))
          : nullptr;
  auto Inner = approvedFunctionalReferenceDirectInvoke(
      S, SM, InnerCall, Context, false);
  if (!Call || !Wrapper || !Inner || !InnerCall ||
      Wrapper->Record->getCanonicalDecl() !=
          Inner->Wrapper.Record->getCanonicalDecl() ||
      InnerCall->getNumArgs() != Call->getNumArgs() ||
      !Context.hasSameType(Call->getType(), InnerCall->getType()))
    return std::nullopt;
  if (Inner->Kind == FunctionalReferenceInvokeKind::FunctionObject) {
    for (unsigned I = 1; I < Call->getNumArgs(); ++I)
      if (!utilityScalarDirectConversion(Context, Call->getArg(I)->getType(),
                                         InnerCall->getArg(I)->getType()))
        return std::nullopt;
    return Inner;
  }
  if (Inner->Kind == FunctionalReferenceInvokeKind::UserFunctionObject) {
    if (!Inner->Method ||
        Inner->Method->getNumParams() + 1 != Call->getNumArgs())
      return std::nullopt;
    for (unsigned I = 0; I < Inner->Method->getNumParams(); ++I) {
      const auto Parameter = Inner->Method->getParamDecl(I)->getType();
      const auto *ArgumentExpression = Call->getArg(I + 1);
      const bool Supported =
          Parameter->isReferenceType()
              ? supportedFunctionalInvokeReferenceArgument(
                    S, SM, Context, Parameter, ArgumentExpression)
              : supportedFunctionalByValue(S, SM, Context, Parameter) &&
                    functionalMemberValueConversion(
                        Context, ArgumentExpression->getType(), Parameter);
      if (!Supported)
        return std::nullopt;
    }
    return Inner;
  }
  const auto *Prototype =
      !Inner->FunctionPointerType.isNull() &&
              Inner->FunctionPointerType->isFunctionPointerType()
          ? Inner->FunctionPointerType->getPointeeType()
                ->getAs<FunctionProtoType>()
          : nullptr;
  if (!Prototype || Prototype->getNumParams() + 1 != Call->getNumArgs())
    return std::nullopt;
  for (unsigned I = 1; I < Call->getNumArgs(); ++I) {
    const auto Parameter = Prototype->getParamType(I - 1);
    const bool Supported = Parameter->isReferenceType()
                               ? supportedFunctionalInvokeReferenceArgument(
                                     S, SM, Context, Parameter, Call->getArg(I))
                               : !Parameter->isReferenceType() &&
                                     functionalMemberValueConversion(
                                         Context, Call->getArg(I)->getType(),
                                         InnerCall->getArg(I)->getType());
    if (!Supported)
      return std::nullopt;
  }
  return Inner;
}

std::optional<FunctionalReferenceInvokeCall>
approvedUtilityTupleApplyReferenceCall(const State &S,
                                       const SourceManager &SM,
                                       const CallExpr *Call,
                                       const ASTContext &Context) {
  const auto Apply = approvedUtilityTupleApplyDispatch(S, SM, Call, Context);
  if (!Apply)
    return std::nullopt;
  const auto *Operation = dyn_cast_or_null<CallExpr>(Apply->Operation);
  auto Reference = approvedFunctionalReferenceDirectInvoke(
      S, SM, Operation, Context, false);
  const auto *WrapperRecord =
      Call->getArg(0)->getType()->getAsCXXRecordDecl();
  if (!Operation || !Reference || !WrapperRecord ||
      Reference->Wrapper.Record->getCanonicalDecl() !=
          WrapperRecord->getCanonicalDecl() ||
      !Context.hasSameType(Call->getType(), Operation->getType()))
    return std::nullopt;

  const bool StandardObject =
      Reference->Kind == FunctionalReferenceInvokeKind::FunctionObject;
  const bool UserObject =
      Reference->Kind == FunctionalReferenceInvokeKind::UserFunctionObject;
  const auto *Prototype =
      !StandardObject && !UserObject &&
              !Reference->FunctionPointerType.isNull() &&
              Reference->FunctionPointerType->isFunctionPointerType()
          ? Reference->FunctionPointerType->getPointeeType()
                ->getAs<FunctionProtoType>()
          : nullptr;
  if ((StandardObject && !Reference->Operation) ||
      (UserObject && !Reference->Method) ||
      (!StandardObject && !UserObject && !Prototype))
    return std::nullopt;

  auto Parameter = [&](unsigned I) -> QualType {
    if (StandardObject)
      return I == 0 ? Reference->Operation->LeftType
                    : Reference->Operation->RightType;
    if (UserObject)
      return Reference->Method->getParamDecl(I)->getType();
    return Prototype->getParamType(I);
  };
  const unsigned Arity =
      StandardObject
          ? (Reference->Operation->RightType.isNull() ? 1u : 2u)
          : UserObject ? Reference->Method->getNumParams()
                       : Prototype->getNumParams();
  if (Apply->Tuple.size() != Arity)
    return std::nullopt;
  const auto TupleArgument = Call->getArg(1)->getType();
  for (unsigned I = 0; I < Arity; ++I) {
    const auto Target = Parameter(I);
    const auto StoredElement = Apply->Tuple.elementType(I);
    const auto Element = StoredElement->isReferenceType()
                             ? StoredElement->getPointeeType()
                             : StoredElement;
    bool Supported = false;
    if (Target->isReferenceType()) {
      const auto Referent = Target->getPointeeType();
      const bool ElementIsLValue =
          StoredElement->isLValueReferenceType() ||
          Call->getArg(1)->isLValue();
      const bool Category = Target->isLValueReferenceType()
                                ? (ElementIsLValue ||
                                   Referent.isConstQualified())
                                : !ElementIsLValue;
      Supported = Category &&
                  supportedFunctionalInvokeReference(S, SM, Context,
                                                     Referent) &&
                  Context.hasSameUnqualifiedType(Referent, Element) &&
                  (!StoredElement->isReferenceType() ||
                   Referent.isAtLeastAsQualifiedAs(Element, Context)) &&
                  !Referent.isVolatileQualified() &&
                  (StoredElement->isReferenceType() ||
                   Referent.isConstQualified() ||
                   !TupleArgument.isConstQualified());
    } else {
      Supported = supportedFunctionalByValue(S, SM, Context, Target) &&
                  functionalMemberValueConversion(Context, Element, Target);
    }
    if (!Supported)
      return std::nullopt;
  }
  return Reference;
}

static bool utilitySwapSDKFunction(const State &S, const SourceManager &SM,
                                   const FunctionDecl *Function,
                                   llvm::StringRef Name, llvm::StringRef Path) {
  const auto *Primary = Function ? Function->getPrimaryTemplate() : nullptr;
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Function ? Function->getDefinition() : nullptr;
  const auto *PatternDefinition = Pattern ? Pattern->getDefinition() : nullptr;
  if (!Function || !Primary || !Pattern || !PatternDefinition ||
      !Function->getIdentifier() || Function->getName() != Name ||
      Function->isVariadic() ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedStandardSDKDeclaration(S, SM, PatternDefinition) ||
      !cstddefOrigin(S, SM, PatternDefinition->getLocation(), "libcxx", Path))
    return false;

  // Generic swap is first declared for is_swappable before its definition.
  // Only that pinned forward declaration may differ from the definition path.
  auto ApprovedDeclaration = [&](const Decl *Declaration) {
    return Declaration && approvedStandardSDKDeclaration(S, SM, Declaration) &&
           (cstddefOrigin(S, SM, Declaration->getLocation(), "libcxx", Path) ||
            (Name == "swap" && Path == "__utility/swap.h" &&
             cstddefOrigin(S, SM, Declaration->getLocation(), "libcxx",
                           "__type_traits/is_swappable.h")));
  };
  if (!ApprovedDeclaration(Function) || !ApprovedDeclaration(Primary) ||
      !ApprovedDeclaration(Pattern) ||
      (Definition &&
       (!ApprovedDeclaration(Definition) ||
        !cstddefOrigin(S, SM, Definition->getLocation(), "libcxx", Path))))
    return false;
  for (const auto *Redeclaration : Function->redecls())
    if (!ApprovedDeclaration(Redeclaration))
      return false;
  for (const auto *Redeclaration : Primary->redecls())
    if (!ApprovedDeclaration(Redeclaration) ||
        !ApprovedDeclaration(Redeclaration->getTemplatedDecl()))
      return false;
  return true;
}

// The generic pinned swap body only moves one temporary and assigns twice.
// Check its concrete move/assignment operations as well: a source
// specialization of std::move must not become an erased call merely because
// swap is from SDK.
static bool utilitySwapTrivialBody(const State &S, const SourceManager &SM,
                                   const Stmt *Statement, QualType Type,
                                   const ASTContext &Context) {
  if (!Statement)
    return true;
  if (const auto *Construction = dyn_cast<CXXConstructExpr>(Statement)) {
    const auto *Constructor = Construction->getConstructor();
    if (!Constructor || !Constructor->isTrivial() ||
        !Constructor->isCopyOrMoveConstructor() ||
        !Context.hasSameType(Construction->getType(), Type))
      return false;
  }
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Statement)) {
    const auto *Method =
        dyn_cast_or_null<CXXMethodDecl>(Operator->getDirectCallee());
    if (!Method || Operator->getOperator() != OO_Equal ||
        !Method->isTrivial() ||
        (!Method->isCopyAssignmentOperator() &&
         !Method->isMoveAssignmentOperator()) ||
        !Context.hasSameType(Context.getRecordType(Method->getParent()), Type))
      return false;
  } else if (const auto *Call = dyn_cast<CallExpr>(Statement)) {
    const auto *Function = Call->getDirectCallee();
    const auto *Arguments =
        Function ? Function->getTemplateSpecializationArgs() : nullptr;
    const auto Reference = Context.getLValueReferenceType(Type);
    if (!utilitySwapSDKFunction(S, SM, Function, "move", "__utility/move.h") ||
        Call->getNumArgs() != 1 || Function->getNumParams() != 1 ||
        !Arguments || Arguments->size() != 1 ||
        Arguments->get(0).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(0).getAsType(), Reference) ||
        !Context.hasSameType(Function->getParamDecl(0)->getType(), Reference) ||
        !Context.hasSameType(Function->getReturnType(),
                             Context.getRValueReferenceType(Type)) ||
        !Context.hasSameType(Call->getType(), Type) || !Call->isXValue())
      return false;
  }
  for (const auto *Child : Statement->children())
    if (!utilitySwapTrivialBody(S, SM, Child, Type, Context))
      return false;
  return true;
}

// Authenticate a concrete SDK member and every declaration used to obtain its
// template pattern. Signature, receiver and body semantics are checked by the
// owning operation below, including static members and member templates.
static bool utilityCompositeSwapMethod(const State &S, const SourceManager &SM,
                                       const CXXMethodDecl *Method,
                                       llvm::StringRef Name,
                                       llvm::StringRef Path) {
  auto Declaration = [&](const Decl *D) {
    return D && approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx", Path);
  };
  if (!Method ||
      (Name.empty()
           ? !isa<CXXConstructorDecl>(Method)
           : (!Method->getIdentifier() || Method->getName() != Name)) ||
      Method->isVariadic() || !Method->hasBody() ||
      Method->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !Declaration(Method->getDefinition()))
    return false;
  for (const auto *Redeclaration : Method->redecls())
    if (!Declaration(Redeclaration))
      return false;

  if (const auto *Primary = Method->getPrimaryTemplate()) {
    unsigned Depth = 0;
    for (const auto *Template = Primary; Template;
         Template = Template->getInstantiatedFromMemberTemplate()) {
      if (++Depth > 64)
        return false;
      for (const auto *Redeclaration : Template->redecls())
        if (!Declaration(Redeclaration) ||
            !Declaration(Redeclaration->getTemplatedDecl()))
          return false;
      const auto *Pattern = Template->getTemplatedDecl();
      for (const auto *Redeclaration : Pattern->redecls())
        if (!Declaration(Redeclaration))
          return false;
      if (!Template->getInstantiatedFromMemberTemplate())
        return Declaration(Pattern->getDefinition()) && Pattern->hasBody();
    }
    return false;
  }
  const auto *Pattern = Method->getInstantiatedFromMemberFunction();
  for (unsigned Depth = 0; Pattern && Depth <= 64; ++Depth) {
    for (const auto *Redeclaration : Pattern->redecls())
      if (!Declaration(Redeclaration))
        return false;
    const auto *Next = Pattern->getInstantiatedFromMemberFunction();
    if (!Next)
      return Declaration(Pattern->getDefinition()) && Pattern->hasBody();
    Pattern = Next;
  }
  return false;
}

// This context belongs to one root swap proof, never to State or an AST-wide
// cache. Repeated type DAG edges may share completed proofs, but an active edge
// is a cycle and cannot authorize itself. Keep the exact selected declaration;
// canonicalize only its reference-stripped type, preserving all qualifiers.
// Depth counts nested values. SDK free/member, implementation and leaf
// adapters retain it; only calls to an element swap increase it.
struct UtilitySwapProofContext {
  using Key = std::pair<const FunctionDecl *, void *>;
  llvm::DenseSet<Key> Active;
  // The greatest entry depth at which the complete proof succeeded. A cached
  // proof may be reused only at an equal or shallower depth; reusing it deeper
  // could skip a descendant that must fail the existing depth-64 boundary.
  llvm::DenseMap<Key, unsigned> Completed;
};

static bool approvedUtilityOptionalSwapBody(
    const State &S, const SourceManager &SM, const CXXMethodDecl *Method,
    const ASTContext &Context, unsigned Depth, UtilitySwapProofContext *Proof);

static bool approvedUtilityTupleSwapBody(
    const State &S, const SourceManager &SM, const CXXMethodDecl *Method,
    const ASTContext &Context, unsigned Depth, UtilitySwapProofContext *Proof);

static bool approvedUtilityArraySwapBody(
    const State &S, const SourceManager &SM, const CXXMethodDecl *Method,
    const ASTContext &Context, unsigned Depth, UtilitySwapProofContext *Proof);

static bool approvedUtilityPairSwapBody(const State &S, const SourceManager &SM,
                                        const CXXMethodDecl *Method,
                                        const ASTContext &Context,
                                        unsigned Depth,
                                        UtilitySwapProofContext *Proof);

static bool
approvedUtilityPairElementSwapImpl(const State &S, const SourceManager &SM,
                                   const FunctionDecl *Function, QualType Type,
                                   const ASTContext &Context, unsigned Depth,
                                   UtilitySwapProofContext *Proof);

static bool
approvedUtilityPairElementSwap(const State &S, const SourceManager &SM,
                               const FunctionDecl *Function, QualType Type,
                               const ASTContext &Context, unsigned Depth = 0,
                               UtilitySwapProofContext *Proof = nullptr) {
  if (Depth > 64 || !Function || Type.isNull())
    return false;
  if (Type->isReferenceType())
    Type = Type->getPointeeType();
  if (Type.hasQualifiers())
    return false;
  UtilitySwapProofContext LocalProof;
  if (!Proof)
    Proof = &LocalProof;
  const UtilitySwapProofContext::Key Key{
      Function, Type.getCanonicalType().getAsOpaquePtr()};
  if (Proof->Active.contains(Key))
    return false;
  const auto Found = Proof->Completed.find(Key);
  if (Found != Proof->Completed.end() && Depth <= Found->second)
    return true;
  Proof->Active.insert(Key);
  const bool Approved = approvedUtilityPairElementSwapImpl(
      S, SM, Function, Type, Context, Depth, Proof);
  Proof->Active.erase(Key);
  if (Approved) {
    auto [Entry, Inserted] = Proof->Completed.try_emplace(Key, Depth);
    if (!Inserted && Entry->second < Depth)
      Entry->second = Depth;
  }
  return Approved;
}

static bool
approvedUtilityPairElementSwapImpl(const State &S, const SourceManager &SM,
                                   const FunctionDecl *Function, QualType Type,
                                   const ASTContext &Context, unsigned Depth,
                                   UtilitySwapProofContext *Proof) {
  if (Depth > 64 || !Function || !Function->hasBody() ||
      Function->getNumParams() != 2 || !Function->getReturnType()->isVoidType())
    return false;
  if (Type->isReferenceType())
    Type = Type->getPointeeType();
  if (Type.hasQualifiers())
    return false;
  const auto Reference = Context.getLValueReferenceType(Type);
  for (const auto *Parameter : Function->parameters())
    if (!Context.hasSameType(Parameter->getType(), Reference))
      return false;
  const auto *Arguments = Function->getTemplateSpecializationArgs();
  if (utilitySwapSDKFunction(S, SM, Function, "swap", "__utility/swap.h"))
    return Arguments && Arguments->size() == 1 &&
           Arguments->get(0).getKind() == TemplateArgument::Type &&
           Context.hasSameType(Arguments->get(0).getAsType(), Type) &&
           utilityPairAssignableValue(S, SM, Context, Type) &&
           utilitySwapTrivialBody(S, SM, Function->getBody(), Type, Context);

  // Container overloads must delegate the original operands to the matching
  // member. Its proof follows the selected element swaps, including ADL.
  const bool Pair =
      utilitySwapSDKFunction(S, SM, Function, "swap", "__utility/pair.h");
  const bool Array = utilitySwapSDKFunction(S, SM, Function, "swap", "array");
  const bool Tuple = utilitySwapSDKFunction(S, SM, Function, "swap", "tuple");
  const bool Optional =
      utilitySwapSDKFunction(S, SM, Function, "swap", "optional");
  if (!Pair && !Array && !Tuple && !Optional)
    return false;
  const auto *Body = dyn_cast<CompoundStmt>(Function->getBody());
  const auto *Call = Body && Body->size() == 1
                         ? dyn_cast<CXXMemberCallExpr>(*Body->body_begin())
                         : nullptr;
  const auto *Method = Call ? Call->getMethodDecl() : nullptr;
  return Call && Call->getNumArgs() == 1 && Method &&
         Context.hasSameType(Context.getRecordType(Method->getParent()),
                             Type) &&
         functionalInvokeParameterReference(Call->getImplicitObjectArgument(),
                                            Function->getParamDecl(0)) &&
         functionalInvokeParameterReference(Call->getArg(0),
                                            Function->getParamDecl(1)) &&
         (Pair    ? approvedUtilityPairSwapBody(S, SM, Method, Context, Depth,
                                                Proof)
          : Array ? approvedUtilityArraySwapBody(S, SM, Method, Context, Depth,
                                                 Proof)
          : Tuple ? approvedUtilityTupleSwapBody(S, SM, Method, Context, Depth,
                                                 Proof)
                  : approvedUtilityOptionalSwapBody(S, SM, Method, Context,
                                                    Depth, Proof));
}

static bool
approvedUtilityPairSwapBody(const State &S, const SourceManager &SM,
                            const CXXMethodDecl *Method,
                            const ASTContext &Context, unsigned Depth = 0,
                            UtilitySwapProofContext *Proof = nullptr) {
  UtilitySwapProofContext LocalProof;
  if (!Proof)
    Proof = &LocalProof;
  if (Depth > 64 || !Method || !Method->getIdentifier() ||
      Method->getName() != "swap" || Method->isStatic() || Method->isConst() ||
      Method->isVolatile() || Method->isVariadic() ||
      Method->getNumParams() != 1 || !Method->getReturnType()->isVoidType() ||
      !utilityCompositeSwapMethod(S, SM, Method, "swap", "__utility/pair.h"))
    return false;
  auto Pair = approvedUtilityPairRecord(S, SM, Method->getParent(), Context);
  if (!Pair)
    Pair =
        approvedUtilityReferencePairRecord(S, SM, Method->getParent(), Context);
  if (!Pair)
    Pair = approvedUtilityMixedReferencePairRecord(S, SM, Method->getParent(),
                                                   Context);
  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  if (!Pair || !Body || Body->size() != 3 ||
      !Context.hasSameType(
          Method->getParamDecl(0)->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Pair->Record))))
    return false;
  auto Statement = Body->body_begin();
  if (!isa<DeclStmt>(*Statement++))
    return false;
  for (const auto *Field : {Pair->First, Pair->Second}) {
    const auto *Call = dyn_cast<CallExpr>(*Statement++);
    if (!Call || Call->getNumArgs() != 2 ||
        !approvedUtilityPairElementSwap(S, SM, Call->getDirectCallee(),
                                        Field->getType(), Context, Depth + 1,
                                        Proof))
      return false;
    const auto *Left = dyn_cast_or_null<MemberExpr>(
        functionalInvokeStrippedExpression(Call->getArg(0)));
    const auto *Right = dyn_cast_or_null<MemberExpr>(
        functionalInvokeStrippedExpression(Call->getArg(1)));
    if (!Left || !Right || Left->getMemberDecl() != Field ||
        Right->getMemberDecl() != Field || !Left->isArrow() ||
        Right->isArrow() ||
        !isa<CXXThisExpr>(
            functionalInvokeStrippedExpression(Left->getBase())) ||
        !functionalInvokeParameterReference(Right->getBase(),
                                            Method->getParamDecl(0)))
      return false;
  }
  return true;
}

// Every range-adapter edge must preserve the same native pointer. The SDK
// provenance check includes source redeclarations and explicit specializations.
static bool utilitySwapPointerAdapter(const State &S, const SourceManager &SM,
                                      const Expr *Expression,
                                      const ParmVarDecl *Parameter,
                                      QualType Pointer, QualType Argument,
                                      bool Forward, const ASTContext &Context) {
  const auto *Call = dyn_cast_or_null<CallExpr>(
      functionalInvokeStrippedExpression(Expression));
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  const auto *Arguments =
      Function ? Function->getTemplateSpecializationArgs() : nullptr;
  const auto Reference = Context.getLValueReferenceType(Pointer);
  const auto Result = Forward && Argument->isLValueReferenceType()
                          ? Reference
                          : Context.getRValueReferenceType(Pointer);
  return utilitySwapSDKFunction(S, SM, Function, Forward ? "forward" : "move",
                                Forward ? "__utility/forward.h"
                                        : "__utility/move.h") &&
         Call->getNumArgs() == 1 && Function->getNumParams() == 1 &&
         Arguments && Arguments->size() == 1 &&
         Arguments->get(0).getKind() == TemplateArgument::Type &&
         Context.hasSameType(Arguments->get(0).getAsType(), Argument) &&
         Context.hasSameType(Function->getParamDecl(0)->getType(), Reference) &&
         Context.hasSameType(Function->getReturnType(), Result) &&
         Context.hasSameType(Call->getType(), Pointer) &&
         (Result->isLValueReferenceType() ? Call->isLValue()
                                          : Call->isXValue()) &&
         functionalInvokeParameterReference(Call->getArg(0), Parameter);
}

static bool utilitySwapPointerTemplate(const FunctionDecl *Function,
                                       QualType Pointer, unsigned Parameters,
                                       unsigned Arguments,
                                       const ASTContext &Context) {
  const auto *TemplateArguments =
      Function ? Function->getTemplateSpecializationArgs() : nullptr;
  if (!Function || Function->getNumParams() != Parameters ||
      !TemplateArguments || TemplateArguments->size() != Arguments)
    return false;
  for (const auto *Parameter : Function->parameters())
    if (!Context.hasSameType(Parameter->getType(), Pointer))
      return false;
  for (unsigned I = 0; I != Arguments; ++I)
    if (TemplateArguments->get(I).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(TemplateArguments->get(I).getAsType(), Pointer))
      return false;
  return true;
}

static bool utilitySwapClassicPolicy(const State &S, const SourceManager &SM,
                                     QualType Type) {
  const auto *Record = Type->getAsCXXRecordDecl();
  if (Type.hasQualifiers() || !Record || !Record->getIdentifier() ||
      Record->getName() != "_ClassicAlgPolicy" ||
      !Record->isCompleteDefinition() || !Record->isEmpty() ||
      Record->getNumBases() || isa<ClassTemplateSpecializationDecl>(Record))
    return false;
  for (const auto *Declaration : Record->redecls())
    if (!approvedStandardSDKDeclaration(S, SM, Declaration) ||
        !cstddefOrigin(S, SM, Declaration->getLocation(), "libcxx",
                       "__algorithm/iterator_operations.h"))
      return false;
  return true;
}

static bool utilitySwapIteratorBody(const State &S, const SourceManager &SM,
                                    const FunctionDecl *Function,
                                    QualType Element, const ASTContext &Context,
                                    unsigned Depth,
                                    UtilitySwapProofContext *Proof) {
  const auto Pointer = Context.getPointerType(Element);
  if (!utilitySwapSDKFunction(S, SM, Function, "iter_swap",
                              "__algorithm/iter_swap.h") ||
      !utilitySwapPointerTemplate(Function, Pointer, 2, 2, Context) ||
      !Function->getReturnType()->isVoidType())
    return false;
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Function->getBody());
  const auto *Call = Body && Body->size() == 1
                         ? dyn_cast<CallExpr>(*Body->body_begin())
                         : nullptr;
  if (!Call || Call->getNumArgs() != 2 ||
      !approvedUtilityPairElementSwap(S, SM, Call->getDirectCallee(), Element,
                                      Context, Depth + 1, Proof))
    return false;
  for (unsigned I = 0; I != 2; ++I) {
    const auto *Dereference = dyn_cast_or_null<UnaryOperator>(
        functionalInvokeStrippedExpression(Call->getArg(I)));
    if (!Dereference || Dereference->getOpcode() != UO_Deref ||
        !Context.hasSameType(Dereference->getType(), Element) ||
        !Dereference->isLValue() ||
        !functionalInvokeParameterReference(Dereference->getSubExpr(),
                                            Function->getParamDecl(I)))
      return false;
  }
  return true;
}

static bool utilitySwapIteratorAdapter(const State &S, const SourceManager &SM,
                                       const CXXMethodDecl *Method,
                                       QualType Element, QualType Policy,
                                       const ASTContext &Context,
                                       unsigned Depth,
                                       UtilitySwapProofContext *Proof) {
  const auto Pointer = Context.getPointerType(Element);
  const auto Reference = Context.getLValueReferenceType(Pointer);
  if (!utilityCompositeSwapMethod(S, SM, Method, "iter_swap",
                                  "__algorithm/iterator_operations.h") ||
      !Method->isStatic() || !Method->getReturnType()->isVoidType() ||
      !utilitySwapPointerTemplate(Method, Reference, 2, 2, Context))
    return false;
  const auto *Record =
      dyn_cast<ClassTemplateSpecializationDecl>(Method->getParent());
  const auto *Primary = Record ? Record->getSpecializedTemplate() : nullptr;
  auto Declaration = [&](const Decl *D) {
    return D && approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx",
                         "__algorithm/iterator_operations.h");
  };
  if (!Record || !Primary || !Record->getIdentifier() ||
      Record->getName() != "_IterOps" ||
      Record->getSpecializationKind() != TSK_ExplicitSpecialization ||
      !Record->isCompleteDefinition() || !Record->isEmpty() ||
      Record->getNumBases() || !Declaration(Record->getDefinition()) ||
      Record->getTemplateArgs().size() != 1 ||
      Record->getTemplateArgs().get(0).getKind() != TemplateArgument::Type ||
      !Context.hasSameType(Record->getTemplateArgs().get(0).getAsType(),
                           Policy))
    return false;
  for (const auto *D : Record->redecls())
    if (!Declaration(D))
      return false;
  for (const auto *D : Primary->redecls())
    if (!Declaration(D) || !Declaration(D->getTemplatedDecl()))
      return false;
  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  const auto *Call = Body && Body->size() == 1
                         ? dyn_cast<CallExpr>(*Body->body_begin())
                         : nullptr;
  if (!Call || Call->getNumArgs() != 2 ||
      !utilitySwapIteratorBody(S, SM, Call->getDirectCallee(), Element, Context,
                               Depth, Proof))
    return false;
  for (unsigned I = 0; I != 2; ++I)
    if (!utilitySwapPointerAdapter(S, SM, Call->getArg(I),
                                   Method->getParamDecl(I), Pointer, Reference,
                                   true, Context))
      return false;
  return true;
}

// __swap_ranges returns a pair of native pointers. Even though array::swap
// discards that result, its selected constructor and forwarding calls must not
// hide source side effects.
static bool utilitySwapPointerPair(const State &S, const SourceManager &SM,
                                   const Expr *Expression,
                                   const FunctionDecl *Function,
                                   const UtilityPairRecord &Pair,
                                   QualType Pointer,
                                   const ASTContext &Context) {
  const auto *Construction = dyn_cast_or_null<CXXConstructExpr>(
      functionalInvokeStrippedExpression(Expression));
  const auto *Constructor =
      Construction ? Construction->getConstructor() : nullptr;
  if (!Constructor || Construction->getNumArgs() != 2 ||
      Constructor->getNumParams() != 2 ||
      Constructor->getNumCtorInitializers() != 2 ||
      !Context.hasSameType(Construction->getType(),
                           Context.getRecordType(Pair.Record)) ||
      !utilityCompositeSwapMethod(S, SM, Constructor, "", "__utility/pair.h"))
    return false;
  const auto *Body = dyn_cast<CompoundStmt>(Constructor->getBody());
  const auto *Arguments = Constructor->getTemplateSpecializationArgs();
  if (!Body || !Body->body_empty() || !Arguments || Arguments->size() != 3 ||
      Arguments->get(2).getKind() != TemplateArgument::Integral ||
      !Arguments->get(2).getAsIntegral().isZero())
    return false;
  auto Initializer = Constructor->init_begin();
  for (unsigned I = 0; I != 2; ++I, ++Initializer) {
    const auto *Field = I ? Pair.Second : Pair.First;
    if (Arguments->get(I).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(I).getAsType(), Pointer) ||
        !Context.hasSameType(Constructor->getParamDecl(I)->getType(),
                             Context.getRValueReferenceType(Pointer)) ||
        !(*Initializer)->isMemberInitializer() ||
        (*Initializer)->getMember() != Field ||
        !utilitySwapPointerAdapter(S, SM, (*Initializer)->getInit(),
                                   Constructor->getParamDecl(I), Pointer,
                                   Pointer, true, Context) ||
        !utilitySwapPointerAdapter(
            S, SM, Construction->getArg(I), Function->getParamDecl(I ? 2 : 0),
            Pointer, Context.getLValueReferenceType(Pointer), false, Context))
      return false;
  }
  return true;
}

static bool utilitySwapRangeBody(const State &S, const SourceManager &SM,
                                 const FunctionDecl *Function, QualType Element,
                                 const UtilityPairRecord &Pair,
                                 const ASTContext &Context, unsigned Depth,
                                 UtilitySwapProofContext *Proof) {
  const auto Pointer = Context.getPointerType(Element);
  const auto *Arguments =
      Function ? Function->getTemplateSpecializationArgs() : nullptr;
  if (!utilitySwapSDKFunction(S, SM, Function, "__swap_ranges",
                              "__algorithm/swap_ranges.h") ||
      Function->getNumParams() != 3 || !Arguments || Arguments->size() != 4 ||
      Arguments->get(0).getKind() != TemplateArgument::Type ||
      !utilitySwapClassicPolicy(S, SM, Arguments->get(0).getAsType()) ||
      !Context.hasSameType(Function->getReturnType(),
                           Context.getRecordType(Pair.Record)))
    return false;
  for (unsigned I = 0; I != 3; ++I)
    if (!Context.hasSameType(Function->getParamDecl(I)->getType(), Pointer) ||
        Arguments->get(I + 1).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(I + 1).getAsType(), Pointer))
      return false;
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Function->getBody());
  if (!Body || Body->size() != 2)
    return false;
  auto Statement = Body->body_begin();
  const auto *Loop = dyn_cast<WhileStmt>(*Statement++);
  const auto *Result = dyn_cast<ReturnStmt>(*Statement);
  const auto *Condition =
      Loop ? dyn_cast<BinaryOperator>(Loop->getCond()) : nullptr;
  const auto *Iteration =
      Loop ? dyn_cast<CompoundStmt>(Loop->getBody()) : nullptr;
  if (!Loop || Loop->getConditionVariable() || !Condition ||
      Condition->getOpcode() != BO_NE || !Iteration || Iteration->size() != 3 ||
      !functionalInvokeParameterReference(Condition->getLHS(),
                                          Function->getParamDecl(0)) ||
      !functionalInvokeParameterReference(Condition->getRHS(),
                                          Function->getParamDecl(1)) ||
      !Result ||
      !utilitySwapPointerPair(S, SM, Result->getRetValue(), Function, Pair,
                              Pointer, Context))
    return false;
  auto Step = Iteration->body_begin();
  const auto *Call = dyn_cast<CallExpr>(*Step++);
  if (!Call || Call->getNumArgs() != 2 ||
      !functionalInvokeParameterReference(Call->getArg(0),
                                          Function->getParamDecl(0)) ||
      !functionalInvokeParameterReference(Call->getArg(1),
                                          Function->getParamDecl(2)) ||
      !utilitySwapIteratorAdapter(
          S, SM, dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee()),
          Element, Arguments->get(0).getAsType(), Context, Depth, Proof))
    return false;
  for (unsigned I : {0u, 2u}) {
    const auto *Increment = dyn_cast<UnaryOperator>(*Step++);
    if (!Increment || Increment->getOpcode() != UO_PreInc ||
        !functionalInvokeParameterReference(Increment->getSubExpr(),
                                            Function->getParamDecl(I)))
      return false;
  }
  return true;
}

static bool utilitySwapRanges(const State &S, const SourceManager &SM,
                              const FunctionDecl *Function, QualType Element,
                              const ASTContext &Context, unsigned Depth,
                              UtilitySwapProofContext *Proof) {
  const auto Pointer = Context.getPointerType(Element);
  if (!utilitySwapSDKFunction(S, SM, Function, "swap_ranges",
                              "__algorithm/swap_ranges.h") ||
      !utilitySwapPointerTemplate(Function, Pointer, 3, 2, Context) ||
      !Context.hasSameType(Function->getReturnType(), Pointer))
    return false;
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Function->getBody());
  const auto *Result = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(*Body->body_begin())
                           : nullptr;
  const auto *Projection =
      Result ? dyn_cast_or_null<MemberExpr>(
                   functionalInvokeStrippedExpression(Result->getRetValue()))
             : nullptr;
  const auto *Call =
      Projection
          ? dyn_cast_or_null<CallExpr>(
                functionalInvokeStrippedExpression(Projection->getBase()))
          : nullptr;
  const auto Pair = approvedUtilityPairRecord(
      S, SM, Call ? Call->getType()->getAsCXXRecordDecl() : nullptr, Context);
  if (!Call || Call->getNumArgs() != 3 || !Pair || Projection->isArrow() ||
      Projection->getMemberDecl() != Pair->Second ||
      !Context.hasSameType(Pair->First->getType(), Pointer) ||
      !Context.hasSameType(Pair->Second->getType(), Pointer) ||
      !utilitySwapRangeBody(S, SM, Call->getDirectCallee(), Element, *Pair,
                            Context, Depth, Proof))
    return false;
  for (unsigned I = 0; I != 3; ++I)
    if (!utilitySwapPointerAdapter(
            S, SM, Call->getArg(I), Function->getParamDecl(I), Pointer,
            Context.getLValueReferenceType(Pointer), false, Context))
      return false;
  return true;
}

static bool utilitySwapArrayData(const State &S, const SourceManager &SM,
                                 const Expr *Expression,
                                 const UtilityArrayRecord &Array,
                                 const ParmVarDecl *Peer,
                                 const ASTContext &Context) {
  const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(
      functionalInvokeStrippedExpression(Expression));
  const auto *Method = Call ? Call->getMethodDecl() : nullptr;
  const auto Pointer = Context.getPointerType(Array.ElementType);
  if (!utilityCompositeSwapMethod(S, SM, Method, "data", "array") ||
      Method->isStatic() || Method->isConst() || Method->isVolatile() ||
      Method->getNumParams() || Call->getNumArgs() ||
      Method->getParent()->getCanonicalDecl() !=
          Array.Record->getCanonicalDecl() ||
      !Context.hasSameType(Method->getReturnType(), Pointer) ||
      !Context.hasSameType(Call->getType(), Pointer))
    return false;
  const auto *Object =
      functionalInvokeStrippedExpression(Call->getImplicitObjectArgument());
  if (Peer ? !functionalInvokeParameterReference(Object, Peer)
           : !isa_and_nonnull<CXXThisExpr>(Object))
    return false;
  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  const auto *Result = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(*Body->body_begin())
                           : nullptr;
  const auto *Decay =
      Result ? dyn_cast_or_null<ImplicitCastExpr>(Result->getRetValue())
             : nullptr;
  const auto *Storage =
      Decay ? dyn_cast<MemberExpr>(Decay->getSubExpr()) : nullptr;
  return Decay && Decay->getCastKind() == CK_ArrayToPointerDecay && Storage &&
         Storage->getMemberDecl() == Array.Elements && Storage->isArrow() &&
         isa<CXXThisExpr>(
             functionalInvokeStrippedExpression(Storage->getBase()));
}

static bool
approvedUtilityArraySwapBody(const State &S, const SourceManager &SM,
                             const CXXMethodDecl *Method,
                             const ASTContext &Context, unsigned Depth = 0,
                             UtilitySwapProofContext *Proof = nullptr) {
  UtilitySwapProofContext LocalProof;
  if (!Proof)
    Proof = &LocalProof;
  if (Depth > 64 ||
      !utilityCompositeSwapMethod(S, SM, Method, "swap", "array") ||
      Method->isStatic() || Method->isConst() || Method->isVolatile() ||
      Method->getNumParams() != 1 || !Method->getReturnType()->isVoidType())
    return false;
  const auto Array =
      approvedUtilityArrayRecord(S, SM, Method->getParent(), Context);
  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  if (!Array || !Body || Body->size() != 1 ||
      !Context.hasSameType(Method->getParamDecl(0)->getType(),
                           Context.getLValueReferenceType(
                               Context.getRecordType(Array->Record))) ||
      (Array->Size
           ? !utilityArrayTriviallyAssignable(Context, Array->ElementType)
           : Array->ElementType.isConstQualified()))
    return false;
  if (!Array->Size) {
    // The pinned zero-size specialization has only a compile-time assertion.
    // In particular, it does not instantiate any element swap operation.
    const auto *Declaration = dyn_cast<DeclStmt>(*Body->body_begin());
    const auto *Assertion =
        Declaration && Declaration->isSingleDecl()
            ? dyn_cast<StaticAssertDecl>(Declaration->getSingleDecl())
            : nullptr;
    return Assertion && !Assertion->isFailed();
  }
  const auto *Call = dyn_cast<CallExpr>(*Body->body_begin());
  const auto *End = Call && Call->getNumArgs() == 3
                        ? dyn_cast<BinaryOperator>(Call->getArg(1))
                        : nullptr;
  const auto *Extent =
      End ? dyn_cast<SubstNonTypeTemplateParmExpr>(End->getRHS()) : nullptr;
  const auto *Size =
      Extent ? dyn_cast<IntegerLiteral>(Extent->getReplacement()) : nullptr;
  return Call && Call->getNumArgs() == 3 && End && End->getOpcode() == BO_Add &&
         Size && Size->getValue() == Array->Size &&
         utilitySwapArrayData(S, SM, Call->getArg(0), *Array, nullptr,
                              Context) &&
         utilitySwapArrayData(S, SM, End->getLHS(), *Array, nullptr, Context) &&
         utilitySwapArrayData(S, SM, Call->getArg(2), *Array,
                              Method->getParamDecl(0), Context) &&
         utilitySwapRanges(S, SM, Call->getDirectCallee(), Array->ElementType,
                           Context, Depth, Proof);
}

static const Stmt *utilityCompositeSwapOnlyStatement(const Stmt *Statement) {
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Statement);
  if (!Body || Body->size() != 1)
    return nullptr;
  Statement = *Body->body_begin();
  if (const auto *Expression = dyn_cast<Expr>(Statement))
    return functionalInvokeStrippedExpression(Expression);
  return Statement;
}

static bool utilityCompositeSwapThis(const Expr *Expression,
                                     const CXXRecordDecl *Record) {
  Expression = functionalInvokeStrippedExpression(Expression);
  if (const auto *Dereference = dyn_cast_or_null<UnaryOperator>(Expression)) {
    if (Dereference->getOpcode() != UO_Deref)
      return false;
    Expression = functionalInvokeStrippedExpression(Dereference->getSubExpr());
  }
  const auto *This = dyn_cast_or_null<CXXThisExpr>(Expression);
  const auto *ThisRecord =
      This ? This->getType()->getPointeeType()->getAsCXXRecordDecl() : nullptr;
  return ThisRecord && Record &&
         ThisRecord->getCanonicalDecl() == Record->getCanonicalDecl();
}

static bool utilityTupleSwapGet(const State &S, const SourceManager &SM,
                                const Expr *Expression,
                                const ParmVarDecl *Parameter,
                                const FieldDecl *Element,
                                const ASTContext &Context) {
  const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(
      functionalInvokeStrippedExpression(Expression));
  const auto *Method = Call ? Call->getMethodDecl() : nullptr;
  const auto *Leaf = cast<CXXRecordDecl>(Element->getParent());
  QualType Type = Element->getType();
  if (Type->isReferenceType())
    Type = Type->getPointeeType();
  if (!Call || Call->getNumArgs() || !Method || Method->isStatic() ||
      Method->isConst() || Method->isVolatile() || Method->getNumParams() ||
      Method->getParent()->getCanonicalDecl() != Leaf->getCanonicalDecl() ||
      !utilityCompositeSwapMethod(S, SM, Method, "get", "tuple") ||
      !Context.hasSameType(Method->getReturnType(),
                           Context.getLValueReferenceType(Type)) ||
      !Context.hasSameType(Call->getType(), Type) || !Call->isLValue() ||
      !functionalInvokeParameterReference(Call->getImplicitObjectArgument(),
                                          Parameter))
    return false;
  const auto *Return = dyn_cast_or_null<ReturnStmt>(
      utilityCompositeSwapOnlyStatement(Method->getBody()));
  const auto *Access =
      Return ? dyn_cast_or_null<MemberExpr>(
                   functionalInvokeStrippedExpression(Return->getRetValue()))
             : nullptr;
  return Access && Access->getMemberDecl() == Element && Access->isArrow() &&
         utilityCompositeSwapThis(Access->getBase(), Leaf);
}

static bool utilityTupleLeafSwap(const State &S, const SourceManager &SM,
                                 const CXXMethodDecl *Method,
                                 const FieldDecl *Element,
                                 const ASTContext &Context, unsigned Depth,
                                 UtilitySwapProofContext *Proof) {
  const auto *Leaf = cast<CXXRecordDecl>(Element->getParent());
  const auto LeafType = Context.getRecordType(Leaf);
  if (Depth > 64 ||
      !utilityCompositeSwapMethod(S, SM, Method, "swap", "tuple") ||
      Method->isStatic() || Method->isConst() || Method->isVolatile() ||
      Method->getParent()->getCanonicalDecl() != Leaf->getCanonicalDecl() ||
      Method->getNumParams() != 1 ||
      !Context.hasSameType(Method->getReturnType(), Context.IntTy) ||
      !Context.hasSameType(Method->getParamDecl(0)->getType(),
                           Context.getLValueReferenceType(LeafType)))
    return false;
  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  if (!Body || Body->size() != 2)
    return false;
  auto Statement = Body->body_begin();
  const auto *Call = dyn_cast<CallExpr>(*Statement++);
  const auto *Return = dyn_cast<ReturnStmt>(*Statement);
  const auto *Zero =
      Return ? dyn_cast_or_null<IntegerLiteral>(
                   functionalInvokeStrippedExpression(Return->getRetValue()))
             : nullptr;
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  if (!Call || Call->getNumArgs() != 2 || !Zero || !Zero->getValue().isZero() ||
      !utilitySwapSDKFunction(S, SM, Function, "swap", "tuple") ||
      Function->getNumParams() != 2 ||
      !Function->getReturnType()->isVoidType() ||
      !utilityCompositeSwapThis(Call->getArg(0), Leaf) ||
      !functionalInvokeParameterReference(Call->getArg(1),
                                          Method->getParamDecl(0)))
    return false;
  for (const auto *Parameter : Function->parameters())
    if (!Context.hasSameType(Parameter->getType(),
                             Context.getLValueReferenceType(LeafType)))
      return false;
  const auto *Selected = dyn_cast_or_null<CallExpr>(
      utilityCompositeSwapOnlyStatement(Function->getBody()));
  return Selected && Selected->getNumArgs() == 2 &&
         utilityTupleSwapGet(S, SM, Selected->getArg(0),
                             Function->getParamDecl(0), Element, Context) &&
         utilityTupleSwapGet(S, SM, Selected->getArg(1),
                             Function->getParamDecl(1), Element, Context) &&
         approvedUtilityPairElementSwap(S, SM, Selected->getDirectCallee(),
                                        Element->getType(), Context, Depth + 1,
                                        Proof);
}

// Clang may represent the explicit leaf projection either directly as a
// derived-to-base cast, or as a no-op explicit cast around an implicit one.
// Both forms must select the single authenticated nonvirtual leaf base.
static bool utilityTupleSwapLeafBase(const CastExpr *Cast,
                                     const CXXRecordDecl *Leaf,
                                     const ASTContext &Context) {
  if (!Cast ||
      (Cast->getCastKind() != CK_DerivedToBase &&
       Cast->getCastKind() != CK_UncheckedDerivedToBase) ||
      Cast->path_size() != 1)
    return false;
  const auto *Base = *Cast->path_begin();
  return !Base->isVirtual() && Base->getAccessSpecifier() == AS_public &&
         Context.hasSameType(Base->getType(), Context.getRecordType(Leaf));
}

static bool utilityTupleSwapPeerLeaf(const Expr *Expression,
                                     const ParmVarDecl *Parameter,
                                     const CXXRecordDecl *Leaf,
                                     const ASTContext &Context) {
  const auto *Explicit = dyn_cast_or_null<CXXStaticCastExpr>(
      functionalInvokeStrippedExpression(Expression));
  if (!Explicit || !Explicit->isLValue() ||
      !Context.hasSameType(Explicit->getType(), Context.getRecordType(Leaf)) ||
      !Context.hasSameType(
          Explicit->getTypeAsWritten(),
          Context.getLValueReferenceType(Context.getRecordType(Leaf))))
    return false;
  const CastExpr *BaseCast = Explicit;
  if (Explicit->getCastKind() == CK_NoOp)
    BaseCast =
        dyn_cast<ImplicitCastExpr>(Explicit->getSubExpr()->IgnoreParens());
  return utilityTupleSwapLeafBase(BaseCast, Leaf, Context) &&
         functionalInvokeParameterReference(BaseCast->getSubExpr(), Parameter);
}

static bool utilityTupleSwapThisLeaf(const Expr *Expression,
                                     const CXXRecordDecl *Impl,
                                     const CXXRecordDecl *Leaf,
                                     const ASTContext &Context) {
  const auto *BaseCast = dyn_cast_or_null<ImplicitCastExpr>(
      Expression ? Expression->IgnoreParens() : nullptr);
  return utilityTupleSwapLeafBase(BaseCast, Leaf, Context) &&
         Context.hasSameType(
             BaseCast->getType(),
             Context.getPointerType(Context.getRecordType(Leaf))) &&
         utilityCompositeSwapThis(BaseCast->getSubExpr(), Impl);
}

static bool utilityTupleImplSwap(const State &S, const SourceManager &SM,
                                 const CXXMethodDecl *Method,
                                 const CXXRecordDecl *Impl,
                                 const UtilityTupleRecord &Tuple,
                                 const ASTContext &Context, unsigned Depth,
                                 UtilitySwapProofContext *Proof) {
  if (Depth > 64 ||
      !utilityCompositeSwapMethod(S, SM, Method, "swap", "tuple") ||
      Method->isStatic() || Method->isConst() || Method->isVolatile() ||
      Method->getParent()->getCanonicalDecl() != Impl->getCanonicalDecl() ||
      Method->getNumParams() != 1 || !Method->getReturnType()->isVoidType() ||
      !Context.hasSameType(
          Method->getParamDecl(0)->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Impl))))
    return false;
  const auto *Call = dyn_cast_or_null<CallExpr>(
      utilityCompositeSwapOnlyStatement(Method->getBody()));
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  const auto *Body =
      Function ? dyn_cast_or_null<CompoundStmt>(Function->getBody()) : nullptr;
  if (!Call || Call->getNumArgs() != Tuple.Elements.size() ||
      !utilitySwapSDKFunction(S, SM, Function, "__swallow", "tuple") ||
      Function->getNumParams() != Tuple.Elements.size() ||
      !Function->getReturnType()->isVoidType() || !Body || Body->size())
    return false;
  for (unsigned I = 0; I < Tuple.Elements.size(); ++I) {
    const auto *Element = Tuple.Elements[I];
    const auto *Leaf = cast<CXXRecordDecl>(Element->getParent());
    const auto *Selected = dyn_cast_or_null<CXXMemberCallExpr>(
        functionalInvokeStrippedExpression(Call->getArg(I)));
    if (!Context.hasSameType(Function->getParamDecl(I)->getType(),
                             Context.getRValueReferenceType(Context.IntTy)) ||
        !Selected || Selected->getNumArgs() != 1 ||
        !utilityTupleSwapThisLeaf(Selected->getImplicitObjectArgument(), Impl,
                                  Leaf, Context) ||
        !utilityTupleSwapPeerLeaf(Selected->getArg(0), Method->getParamDecl(0),
                                  Leaf, Context) ||
        !utilityTupleLeafSwap(S, SM, Selected->getMethodDecl(), Element,
                              Context, Depth, Proof))
      return false;
  }
  return true;
}

static bool
approvedUtilityTupleSwapBody(const State &S, const SourceManager &SM,
                             const CXXMethodDecl *Method,
                             const ASTContext &Context, unsigned Depth = 0,
                             UtilitySwapProofContext *Proof = nullptr) {
  UtilitySwapProofContext LocalProof;
  if (!Proof)
    Proof = &LocalProof;
  if (Depth > 64 || !Method || Method->isStatic() || Method->isConst() ||
      Method->isVolatile() || Method->isVariadic() ||
      !Method->getIdentifier() || Method->getName() != "swap" ||
      Method->getNumParams() != 1 || !Method->getReturnType()->isVoidType())
    return false;
  auto Tuple = approvedUtilityTupleRecord(S, SM, Method->getParent(), Context);
  if (!Tuple)
    Tuple = approvedUtilityReferenceTupleRecord(S, SM, Method->getParent(),
                                                Context);
  if (!Tuple)
    Tuple = approvedUtilityMixedReferenceTupleRecord(S, SM, Method->getParent(),
                                                     Context);
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Method->getBody());
  if (!Tuple || !Body ||
      !Context.hasSameType(
          Method->getParamDecl(0)->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Tuple->Record))))
    return false;
  // tuple<> is an authenticated explicit SDK class specialization. It has no
  // instantiated member pattern and performs no element operation.
  if (Tuple->Elements.empty()) {
    if (Body->size())
      return false;
    for (const auto *Redeclaration : Method->redecls())
      if (!approvedStandardSDKDeclaration(S, SM, Redeclaration) ||
          !cstddefOrigin(S, SM, Redeclaration->getLocation(), "libcxx",
                         "tuple"))
        return false;
    return true;
  }
  if (!utilityCompositeSwapMethod(S, SM, Method, "swap", "tuple"))
    return false;
  const auto *BaseField = *Tuple->Record->field_begin();
  const auto *Impl = BaseField->getType()->getAsCXXRecordDecl();
  const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(
      utilityCompositeSwapOnlyStatement(Method->getBody()));
  const auto *Left =
      Call ? dyn_cast_or_null<MemberExpr>(functionalInvokeStrippedExpression(
                 Call->getImplicitObjectArgument()))
           : nullptr;
  const auto *Right =
      Call && Call->getNumArgs() == 1
          ? dyn_cast_or_null<MemberExpr>(
                functionalInvokeStrippedExpression(Call->getArg(0)))
          : nullptr;
  return Left && Right && Left->getMemberDecl() == BaseField &&
         Right->getMemberDecl() == BaseField && Left->isArrow() &&
         !Right->isArrow() &&
         utilityCompositeSwapThis(Left->getBase(), Tuple->Record) &&
         functionalInvokeParameterReference(Right->getBase(),
                                            Method->getParamDecl(0)) &&
         utilityTupleImplSwap(S, SM, Call->getMethodDecl(), Impl, *Tuple,
                              Context, Depth, Proof);
}

// All projections in the optional swap proof remain tied to the authenticated
// storage and destructor bases. A matching spelling alone is not a projection.
static bool utilityOptionalSwapField(const Expr *Expression,
                                     const FieldDecl *Field,
                                     const CXXRecordDecl *ThisRecord,
                                     const UtilityOptionalRecord &Optional) {
  const auto *Access = dyn_cast_or_null<MemberExpr>(
      functionalInvokeStrippedExpression(Expression));
  if (!Access || Access->getMemberDecl() != Field)
    return false;
  const Expr *Base = Access->getBase();
  if (Field == Optional.Value) {
    const auto *Union =
        dyn_cast_or_null<MemberExpr>(functionalInvokeStrippedExpression(Base));
    const auto *Anonymous =
        Union ? dyn_cast<FieldDecl>(Union->getMemberDecl()) : nullptr;
    if (!Union || Access->isArrow() || !Union->isArrow() || !Anonymous ||
        !Anonymous->isAnonymousStructOrUnion() ||
        Anonymous->getParent() != Optional.DestructBase ||
        Anonymous->getType()->getAsCXXRecordDecl() != Field->getParent())
      return false;
    Base = Union->getBase();
  } else if (!Access->isArrow()) {
    return false;
  }
  return utilityCompositeSwapThis(Base, ThisRecord);
}

static bool utilityOptionalSwapReceiver(const Expr *Expression,
                                        const CXXMethodDecl *Outer, bool Peer) {
  return Peer ? functionalInvokeParameterReference(Expression,
                                                   Outer->getParamDecl(0))
              : utilityCompositeSwapThis(Expression, Outer->getParent());
}

static bool utilityOptionalSwapRead(const State &S, const SourceManager &SM,
                                    const Expr *Expression,
                                    const CXXMethodDecl *Outer, bool Peer,
                                    const UtilityOptionalRecord &Optional,
                                    bool Engagement,
                                    const ASTContext &Context) {
  const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(
      functionalInvokeStrippedExpression(Expression));
  const auto *Method = Call ? Call->getMethodDecl() : nullptr;
  const auto Result =
      Engagement ? Context.BoolTy
                 : Context.getLValueReferenceType(Optional.ElementType);
  if (!Call || Call->getNumArgs() || !Method || Method->isStatic() ||
      Method->isVolatile() || Method->isConst() != Engagement ||
      Method->getNumParams() ||
      Method->getParent()->getCanonicalDecl() !=
          Optional.StorageBase->getCanonicalDecl() ||
      !utilityCompositeSwapMethod(
          S, SM, Method, Engagement ? "has_value" : "__get", "optional") ||
      !Context.hasSameType(Method->getReturnType(), Result) ||
      (!Engagement &&
       (!Call->isLValue() || Method->getRefQualifier() != RQ_LValue)) ||
      !utilityOptionalSwapReceiver(Call->getImplicitObjectArgument(), Outer,
                                   Peer))
    return false;
  const auto *Return = dyn_cast_or_null<ReturnStmt>(
      utilityCompositeSwapOnlyStatement(Method->getBody()));
  return Return && utilityOptionalSwapField(Return->getRetValue(),
                                            Engagement ? Optional.Engaged
                                                       : Optional.Value,
                                            Optional.StorageBase, Optional);
}

static bool utilityOptionalSwapEngage(const Stmt *Statement, bool Value,
                                      const CXXRecordDecl *ThisRecord,
                                      const UtilityOptionalRecord &Optional) {
  const auto *Assign = dyn_cast_or_null<BinaryOperator>(Statement);
  const auto *Boolean =
      Assign ? dyn_cast_or_null<CXXBoolLiteralExpr>(
                   functionalInvokeStrippedExpression(Assign->getRHS()))
             : nullptr;
  return Assign && Assign->getOpcode() == BO_Assign && Boolean &&
         Boolean->getValue() == Value &&
         utilityOptionalSwapField(Assign->getLHS(), Optional.Engaged,
                                  ThisRecord, Optional);
}

static bool utilityOptionalSwapReset(const State &S, const SourceManager &SM,
                                     const Stmt *Statement,
                                     const CXXMethodDecl *Outer, bool Peer,
                                     const UtilityOptionalRecord &Optional) {
  const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(Statement);
  const auto *Method = Call ? Call->getMethodDecl() : nullptr;
  if (!Call || Call->getNumArgs() || !Method || Method->isStatic() ||
      Method->isConst() || Method->isVolatile() || Method->getNumParams() ||
      !Method->getReturnType()->isVoidType() ||
      Method->getParent()->getCanonicalDecl() !=
          Optional.DestructBase->getCanonicalDecl() ||
      !utilityCompositeSwapMethod(S, SM, Method, "reset", "optional") ||
      !utilityOptionalSwapReceiver(Call->getImplicitObjectArgument(), Outer,
                                   Peer))
    return false;
  const auto *Branch = dyn_cast_or_null<IfStmt>(
      utilityCompositeSwapOnlyStatement(Method->getBody()));
  return Branch && !Branch->getInit() && !Branch->getConditionVariable() &&
         !Branch->getElse() &&
         utilityOptionalSwapField(Branch->getCond(), Optional.Engaged,
                                  Optional.DestructBase, Optional) &&
         utilityOptionalSwapEngage(
             utilityCompositeSwapOnlyStatement(Branch->getThen()), false,
             Optional.DestructBase, Optional);
}

static bool utilityOptionalSwapNoop(const Expr *Expression) {
  Expression = Expression ? Expression->IgnoreParens() : nullptr;
  const auto *Cast = dyn_cast_or_null<CStyleCastExpr>(Expression);
  const auto *Zero =
      Cast ? dyn_cast<IntegerLiteral>(Cast->getSubExpr()) : nullptr;
  return Cast && Cast->getCastKind() == CK_ToVoid &&
         Cast->getType()->isVoidType() && Zero && Zero->getValue().isZero();
}

static bool utilityOptionalSwapForward(const State &S, const SourceManager &SM,
                                       const Expr *Expression,
                                       const ParmVarDecl *Parameter,
                                       QualType Type,
                                       const ASTContext &Context) {
  const auto *Call = dyn_cast_or_null<CallExpr>(
      functionalInvokeStrippedExpression(Expression));
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  const auto *Arguments =
      Function ? Function->getTemplateSpecializationArgs() : nullptr;
  return Call && Call->getNumArgs() == 1 &&
         utilitySwapSDKFunction(S, SM, Function, "forward",
                                "__utility/forward.h") &&
         Function->getNumParams() == 1 && Arguments && Arguments->size() == 1 &&
         Arguments->get(0).getKind() == TemplateArgument::Type &&
         Context.hasSameType(Arguments->get(0).getAsType(), Type) &&
         Context.hasSameType(Function->getParamDecl(0)->getType(),
                             Context.getLValueReferenceType(Type)) &&
         Context.hasSameType(Function->getReturnType(),
                             Context.getRValueReferenceType(Type)) &&
         Context.hasSameType(Call->getType(), Type) && Call->isXValue() &&
         functionalInvokeParameterReference(Call->getArg(0), Parameter);
}

static bool utilityOptionalSwapPlacement(const State &S,
                                         const SourceManager &SM,
                                         const FunctionDecl *Function,
                                         bool Allocation,
                                         const ASTContext &Context) {
  const auto *Definition = Function ? Function->getDefinition() : nullptr;
  if (!Definition || isa<CXXMethodDecl>(Definition) ||
      !Definition->isReservedGlobalPlacementOperator() ||
      Definition->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
      Definition->getOverloadedOperator() !=
          (Allocation ? OO_New : OO_Delete) ||
      !Definition->getDeclContext()->getRedeclContext()->isTranslationUnit() ||
      Definition->getNumParams() != 2 || Definition->isVariadic() ||
      !Context.hasSameType(Definition->getParamDecl(0)->getType(),
                           Allocation ? Context.getSizeType()
                                      : Context.VoidPtrTy) ||
      !Context.hasSameType(Definition->getParamDecl(1)->getType(),
                           Context.VoidPtrTy) ||
      !Context.hasSameType(Definition->getReturnType(),
                           Allocation ? Context.VoidPtrTy : Context.VoidTy))
    return false;
  for (const auto *Redeclaration : Function->redecls())
    if (!cstddefOrigin(S, SM, Redeclaration->getLocation(), "libcxx",
                       "__new/placement_new_delete.h"))
      return false;
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != (Allocation ? 1 : 0))
    return false;
  if (!Allocation)
    return true;
  const auto *Return = dyn_cast<ReturnStmt>(*Body->body_begin());
  return Return && functionalInvokeParameterReference(
                       Return->getRetValue(), Definition->getParamDecl(1));
}

static bool utilityOptionalSwapConstructAt(const State &S,
                                           const SourceManager &SM,
                                           const FunctionDecl *Function,
                                           QualType Type,
                                           const ASTContext &Context) {
  if (!utilitySwapSDKFunction(S, SM, Function, "__construct_at",
                              "__memory/construct_at.h") ||
      Function->getNumParams() != 2 ||
      !Context.hasSameType(Function->getParamDecl(0)->getType(),
                           Context.getPointerType(Type)) ||
      !Context.hasSameType(Function->getParamDecl(1)->getType(),
                           Context.getRValueReferenceType(Type)) ||
      !Context.hasSameType(Function->getReturnType(),
                           Context.getPointerType(Type)))
    return false;
  const auto *Return = dyn_cast_or_null<ReturnStmt>(
      utilityCompositeSwapOnlyStatement(Function->getBody()));
  const auto *Comma =
      Return ? dyn_cast_or_null<BinaryOperator>(Return->getRetValue())
             : nullptr;
  const auto *New = Comma ? dyn_cast<CXXNewExpr>(Comma->getRHS()) : nullptr;
  if (!Comma || Comma->getOpcode() != BO_Comma ||
      !utilityOptionalSwapNoop(Comma->getLHS()) || !New ||
      !New->isGlobalNew() || New->isArray() ||
      New->getNumPlacementArgs() != 1 ||
      !Context.hasSameType(New->getAllocatedType(), Type) ||
      !utilityOptionalSwapPlacement(S, SM, New->getOperatorNew(), true,
                                    Context) ||
      (New->getOperatorDelete() &&
       !utilityOptionalSwapPlacement(S, SM, New->getOperatorDelete(), false,
                                     Context)))
    return false;
  const auto *Placement = dyn_cast<CXXStaticCastExpr>(New->getPlacementArg(0));
  if (!Placement ||
      (Placement->getCastKind() != CK_NoOp &&
       Placement->getCastKind() != CK_BitCast) ||
      !Context.hasSameType(Placement->getType(), Context.VoidPtrTy) ||
      !functionalInvokeParameterReference(Placement->getSubExpr(),
                                          Function->getParamDecl(0)))
    return false;
  const Expr *Initializer = New->getInitializer();
  if (Type->isRecordType()) {
    const auto *Construction = dyn_cast_or_null<CXXConstructExpr>(Initializer);
    const auto *Constructor =
        Construction ? Construction->getConstructor() : nullptr;
    const auto *Record = Type->getAsCXXRecordDecl();
    if (!Construction || !Constructor || !Constructor->isTrivial() ||
        !Constructor->isCopyOrMoveConstructor() ||
        Construction->getNumArgs() != 1 ||
        !Context.hasSameType(Construction->getType(), Type) || !Record ||
        !Record->hasTrivialDestructor())
      return false;
    Initializer = Construction->getArg(0);
  }
  return utilityOptionalSwapForward(S, SM, Initializer,
                                    Function->getParamDecl(1), Type, Context);
}

static bool utilityOptionalSwapConstruct(const State &S,
                                         const SourceManager &SM,
                                         const CXXMethodDecl *Method,
                                         const UtilityOptionalRecord &Optional,
                                         const ASTContext &Context) {
  const auto Type = Optional.ElementType;
  if (!utilityCompositeSwapMethod(S, SM, Method, "__construct", "optional") ||
      Method->isStatic() || Method->isConst() || Method->isVolatile() ||
      Method->getParent()->getCanonicalDecl() !=
          Optional.StorageBase->getCanonicalDecl() ||
      Method->getNumParams() != 1 || !Method->getReturnType()->isVoidType() ||
      !Context.hasSameType(Method->getParamDecl(0)->getType(),
                           Context.getRValueReferenceType(Type)))
    return false;
  const auto *Body = dyn_cast<CompoundStmt>(Method->getBody());
  if (!Body || Body->size() != 3)
    return false;
  auto Statement = Body->body_begin();
  const auto *Noop = dyn_cast<Expr>(*Statement++);
  const auto *Call = dyn_cast<CallExpr>(*Statement++);
  if (!Noop || !utilityOptionalSwapNoop(Noop) || !Call ||
      Call->getNumArgs() != 2 ||
      !utilityOptionalSwapConstructAt(S, SM, Call->getDirectCallee(), Type,
                                      Context) ||
      !utilityOptionalSwapForward(S, SM, Call->getArg(1),
                                  Method->getParamDecl(0), Type, Context) ||
      !utilityOptionalSwapEngage(*Statement, true, Optional.StorageBase,
                                 Optional))
    return false;
  const auto *Address = dyn_cast_or_null<CallExpr>(
      functionalInvokeStrippedExpression(Call->getArg(0)));
  const auto *Function = Address ? Address->getDirectCallee() : nullptr;
  return Address && Address->getNumArgs() == 1 &&
         utilitySwapSDKFunction(S, SM, Function, "addressof",
                                "__memory/addressof.h") &&
         Function->getNumParams() == 1 &&
         Context.hasSameType(Function->getParamDecl(0)->getType(),
                             Context.getLValueReferenceType(Type)) &&
         Context.hasSameType(Function->getReturnType(),
                             Context.getPointerType(Type)) &&
         utilityOptionalSwapField(Address->getArg(0), Optional.Value,
                                  Optional.StorageBase, Optional);
}

static bool utilityOptionalSwapTransfer(const State &S, const SourceManager &SM,
                                        const Stmt *Statement,
                                        const CXXMethodDecl *Outer, bool ToPeer,
                                        const UtilityOptionalRecord &Optional,
                                        const ASTContext &Context) {
  const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(Statement);
  if (!Call || Call->getNumArgs() != 1 ||
      !utilityOptionalSwapReceiver(Call->getImplicitObjectArgument(), Outer,
                                   ToPeer) ||
      !utilityOptionalSwapConstruct(S, SM, Call->getMethodDecl(), Optional,
                                    Context))
    return false;
  const auto *Move = dyn_cast_or_null<CallExpr>(
      functionalInvokeStrippedExpression(Call->getArg(0)));
  const auto *Function = Move ? Move->getDirectCallee() : nullptr;
  const auto *Arguments =
      Function ? Function->getTemplateSpecializationArgs() : nullptr;
  const auto Type = Optional.ElementType;
  const auto Reference = Context.getLValueReferenceType(Type);
  return Move && Move->getNumArgs() == 1 && Move->isXValue() &&
         utilitySwapSDKFunction(S, SM, Function, "move", "__utility/move.h") &&
         Function->getNumParams() == 1 && Arguments && Arguments->size() == 1 &&
         Arguments->get(0).getKind() == TemplateArgument::Type &&
         Context.hasSameType(Arguments->get(0).getAsType(), Reference) &&
         Context.hasSameType(Function->getParamDecl(0)->getType(), Reference) &&
         Context.hasSameType(Function->getReturnType(),
                             Context.getRValueReferenceType(Type)) &&
         utilityOptionalSwapRead(S, SM, Move->getArg(0), Outer, !ToPeer,
                                 Optional, false, Context);
}

static bool
approvedUtilityOptionalSwapBody(const State &S, const SourceManager &SM,
                                const CXXMethodDecl *Method,
                                const ASTContext &Context, unsigned Depth = 0,
                                UtilitySwapProofContext *Proof = nullptr) {
  UtilitySwapProofContext LocalProof;
  if (!Proof)
    Proof = &LocalProof;
  if (Depth > 64 ||
      !utilityCompositeSwapMethod(S, SM, Method, "swap", "optional") ||
      Method->isStatic() || Method->isConst() || Method->isVolatile() ||
      Method->getNumParams() != 1 || !Method->getReturnType()->isVoidType())
    return false;
  const auto Optional =
      approvedUtilityOptionalRecord(S, SM, Method->getParent(), Context);
  if (!Optional ||
      !utilityTupleAssignableValue(S, SM, Context, Optional->ElementType) ||
      !Context.hasSameType(Method->getParamDecl(0)->getType(),
                           Context.getLValueReferenceType(
                               Context.getRecordType(Optional->Record))))
    return false;
  const auto *Branch = dyn_cast_or_null<IfStmt>(
      utilityCompositeSwapOnlyStatement(Method->getBody()));
  const auto *Condition =
      Branch ? dyn_cast_or_null<BinaryOperator>(Branch->getCond()) : nullptr;
  const auto *Equal =
      Branch ? dyn_cast_or_null<CompoundStmt>(Branch->getThen()) : nullptr;
  const auto *Different =
      Branch ? dyn_cast_or_null<IfStmt>(
                   utilityCompositeSwapOnlyStatement(Branch->getElse()))
             : nullptr;
  if (!Branch || Branch->getInit() || Branch->getConditionVariable() ||
      !Condition || Condition->getOpcode() != BO_EQ || !Equal ||
      Equal->size() != 2 || !Different || Different->getInit() ||
      Different->getConditionVariable() ||
      !utilityOptionalSwapRead(S, SM, Condition->getLHS(), Method, false,
                               *Optional, true, Context) ||
      !utilityOptionalSwapRead(S, SM, Condition->getRHS(), Method, true,
                               *Optional, true, Context) ||
      !utilityOptionalSwapRead(S, SM, Different->getCond(), Method, false,
                               *Optional, true, Context))
    return false;
  auto Statement = Equal->body_begin();
  const auto *Using = dyn_cast<DeclStmt>(*Statement++);
  if (!Using)
    return false;
  for (const auto *Declaration : Using->decls())
    if (!isa<UsingDecl>(Declaration) && !isa<UsingShadowDecl>(Declaration))
      return false;
  const auto *Present = dyn_cast<IfStmt>(*Statement);
  const auto *Swap =
      Present ? dyn_cast_or_null<CallExpr>(Present->getThen()) : nullptr;
  if (!Present || Present->getInit() || Present->getConditionVariable() ||
      Present->getElse() || !Swap || Swap->getNumArgs() != 2 ||
      !utilityOptionalSwapRead(S, SM, Present->getCond(), Method, false,
                               *Optional, true, Context) ||
      !utilityOptionalSwapRead(S, SM, Swap->getArg(0), Method, false, *Optional,
                               false, Context) ||
      !utilityOptionalSwapRead(S, SM, Swap->getArg(1), Method, true, *Optional,
                               false, Context) ||
      !approvedUtilityPairElementSwap(S, SM, Swap->getDirectCallee(),
                                      Optional->ElementType, Context, Depth + 1,
                                      Proof))
    return false;
  for (bool ToPeer : {true, false}) {
    const auto *Transfer = dyn_cast_or_null<CompoundStmt>(
        ToPeer ? Different->getThen() : Different->getElse());
    if (!Transfer || Transfer->size() != 2)
      return false;
    auto Part = Transfer->body_begin();
    if (!utilityOptionalSwapTransfer(S, SM, *Part++, Method, ToPeer, *Optional,
                                     Context) ||
        !utilityOptionalSwapReset(S, SM, *Part, Method, !ToPeer, *Optional))
      return false;
  }
  return true;
}

// These adapters authenticate the concrete SDK call graph without traversing
// implementation bodies as user source or instantiating missing definitions.
static bool utilityAlgorithmSDKReference(const State &S,
                                         const SourceManager &SM,
                                         const CallExpr *Call,
                                         const FunctionDecl *Function) {
  const auto *Reference =
      Call ? dyn_cast_or_null<DeclRefExpr>(directFunctionReference(Call))
           : nullptr;
  const auto Origin =
      Reference ? S.sdkFile(SM, Reference->getExprLoc()) : std::nullopt;
  return Reference && Reference->getDecl() == Function && Origin &&
         Origin->Root == "libcxx";
}

static bool utilityAlgorithmReference(const Expr *E, const ValueDecl *Value,
                                      const ASTContext &Context,
                                      bool Forwarded = false) {
  while (const auto *Cast = dyn_cast_or_null<ImplicitCastExpr>(E)) {
    if ((Cast->getCastKind() != CK_NoOp &&
         Cast->getCastKind() != CK_LValueToRValue) ||
        !Context.hasSameUnqualifiedType(Cast->getType(),
                                        Cast->getSubExpr()->getType()))
      return false;
    E = Cast->getSubExpr();
  }
  if (Forwarded) {
    const auto *Cast = dyn_cast_or_null<CXXStaticCastExpr>(E);
    if (!Cast || Cast->getCastKind() != CK_NoOp || !Cast->isLValue() ||
        !Cast->getTypeAsWritten()->isLValueReferenceType() ||
        !Context.hasSameType(Cast->getType(), Cast->getSubExpr()->getType()))
      return false;
    E = Cast->getSubExpr();
  }
  const auto *Reference = dyn_cast_or_null<DeclRefExpr>(E);
  return Reference && Reference->isLValue() && Reference->getDecl() == Value;
}

static const CXXOperatorCallExpr *
utilityAlgorithmUnaryDispatch(const State &S, const SourceManager &SM,
                              const CallExpr *Call, QualType Object,
                              QualType Element, bool Projection,
                              const ASTContext &Context) {
  const auto *F = Call ? Call->getDirectCallee() : nullptr;
  const auto *D = F ? F->getDefinition() : nullptr;
  const auto *Args = F ? F->getTemplateSpecializationArgs() : nullptr;
  const auto ObjectRef = Context.getLValueReferenceType(Object);
  const auto ElementRef = Context.getLValueReferenceType(Element);
  if (!Call || Call->getNumArgs() != 2 || !D ||
      !utilitySwapSDKFunction(S, SM, F, "__invoke", "__type_traits/invoke.h") ||
      !utilityAlgorithmSDKReference(S, SM, Call, F) || !F->isConstexpr() ||
      F->getNumParams() != 2 || !Args || Args->size() != 2 ||
      Args->get(0).getKind() != TemplateArgument::Type ||
      !Context.hasSameType(Args->get(0).getAsType(), ObjectRef) ||
      Args->get(1).getKind() != TemplateArgument::Pack ||
      Args->get(1).pack_size() != 1 ||
      Args->get(1).pack_begin()->getKind() != TemplateArgument::Type ||
      !Context.hasSameType(Args->get(1).pack_begin()->getAsType(),
                           ElementRef) ||
      !Context.hasSameType(F->getParamDecl(0)->getType(), ObjectRef) ||
      !Context.hasSameType(F->getParamDecl(1)->getType(), ElementRef) ||
      !Context.hasSameType(F->getReturnType(),
                           Projection ? ElementRef : Context.BoolTy) ||
      !Call->getArg(0)->isLValue() || !Call->getArg(1)->isLValue() ||
      !Context.hasSameType(Call->getArg(0)->getType(), Object) ||
      !Context.hasSameType(Call->getArg(1)->getType(), Element) ||
      !Context.hasSameType(Call->getType(),
                           Projection ? Element : Context.BoolTy) ||
      (Projection ? !Call->isLValue() : !Call->isPRValue()))
    return nullptr;
  const auto *Body = dyn_cast_or_null<CompoundStmt>(D->getBody());
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(*Body->body_begin())
                           : nullptr;
  const auto *Invocation =
      Return ? dyn_cast_or_null<CXXOperatorCallExpr>(Return->getRetValue())
             : nullptr;
  if (!Invocation || Invocation->getOperator() != OO_Call ||
      Invocation->getNumArgs() != 2 ||
      !Context.hasSameType(Invocation->getType(), Call->getType()) ||
      Invocation->getValueKind() != Call->getValueKind() ||
      !utilityAlgorithmReference(Invocation->getArg(0), D->getParamDecl(0),
                                 Context, true))
    return nullptr;
  const Expr *Argument = Invocation->getArg(1);
  while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Argument)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return nullptr;
    Argument = Cast->getSubExpr();
  }
  return utilityAlgorithmReference(Argument, D->getParamDecl(1), Context, true)
             ? Invocation
             : nullptr;
}

static bool utilityAlgorithmIdentity(const State &S, const SourceManager &SM,
                                     const CXXOperatorCallExpr *Call,
                                     QualType Object, QualType Element,
                                     const ASTContext &Context) {
  const auto *Method =
      Call ? dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee()) : nullptr;
  const auto *Record = Object->getAsCXXRecordDecl();
  const auto *Primary = Method ? Method->getPrimaryTemplate() : nullptr;
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Method ? Method->getDefinition() : nullptr;
  const auto *Args = Method ? Method->getTemplateSpecializationArgs() : nullptr;
  const auto Reference = Context.getLValueReferenceType(Element);
  auto Origin = [&](const Decl *D) {
    return D && approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx",
                         "__functional/identity.h");
  };
  if (!Record || !Record->getIdentifier() ||
      Record->getName() != "__identity" ||
      isa<ClassTemplateSpecializationDecl>(Record) ||
      !Record->isCompleteDefinition() || !Record->isEmpty() ||
      !Record->isTrivial() || Record->getNumBases() || !Method || !Definition ||
      !Primary || !Pattern || !Pattern->getDefinition() || !Method->isConst() ||
      Method->isVolatile() || Method->isStatic() ||
      Method->getOverloadedOperator() != OO_Call ||
      Method->getNumParams() != 1 ||
      Method->getParent()->getCanonicalDecl() != Record->getCanonicalDecl() ||
      Method->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !Origin(Definition) || !Origin(Pattern->getDefinition()) ||
      !Context.hasSameType(Method->getReturnType(), Reference) ||
      !Context.hasSameType(Method->getParamDecl(0)->getType(), Reference) ||
      !Args || Args->size() != 1 ||
      Args->get(0).getKind() != TemplateArgument::Type ||
      !Context.hasSameType(Args->get(0).getAsType(), Reference))
    return false;
  for (const auto *D : Record->redecls())
    if (!Origin(D))
      return false;
  for (const auto *D : Method->redecls())
    if (!Origin(D))
      return false;
  for (const auto *D : Pattern->redecls())
    if (!Origin(D))
      return false;
  for (const auto *D : Primary->redecls())
    if (!Origin(D) || !Origin(D->getTemplatedDecl()))
      return false;
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(*Body->body_begin())
                           : nullptr;
  const auto *Forward =
      Return ? dyn_cast_or_null<CallExpr>(Return->getRetValue()) : nullptr;
  const auto *F = Forward ? Forward->getDirectCallee() : nullptr;
  const auto *FArgs = F ? F->getTemplateSpecializationArgs() : nullptr;
  if (!Forward || Forward->getNumArgs() != 1 || !Forward->isLValue() ||
      !utilitySwapSDKFunction(S, SM, F, "forward", "__utility/forward.h") ||
      !utilityAlgorithmSDKReference(S, SM, Forward, F) ||
      F->getNumParams() != 1 ||
      !Context.hasSameType(F->getReturnType(), Reference) ||
      !Context.hasSameType(F->getParamDecl(0)->getType(), Reference) ||
      !Context.hasSameType(Forward->getType(), Element) || !FArgs ||
      FArgs->size() != 1 || FArgs->get(0).getKind() != TemplateArgument::Type ||
      !Context.hasSameType(FArgs->get(0).getAsType(), Reference) ||
      !utilityAlgorithmReference(Forward->getArg(0),
                                 Definition->getParamDecl(0), Context))
    return false;
  // Clang can recognize forward as a builtin without instantiating its body.
  // Its authenticated pattern must still be the single formal static cast.
  const auto *FP = F->getPrimaryTemplate()->getTemplatedDecl()->getDefinition();
  const auto *FB = FP ? dyn_cast_or_null<CompoundStmt>(FP->getBody()) : nullptr;
  const auto *FR =
      FB && FB->size() == 1 ? dyn_cast<ReturnStmt>(*FB->body_begin()) : nullptr;
  const auto *Cast =
      FR ? dyn_cast_or_null<CXXStaticCastExpr>(FR->getRetValue()) : nullptr;
  const auto *Ref = Cast ? dyn_cast<DeclRefExpr>(Cast->getSubExpr()) : nullptr;
  return Cast && Ref && Ref->getDecl() == FP->getParamDecl(0) &&
         Cast->getTypeAsWritten()->isRValueReferenceType();
}

static bool utilityAlgorithmCountType(const State &S, const SourceManager &SM,
                                      QualType Type, QualType Pointer,
                                      QualType Policy, bool Helper,
                                      const ASTContext &Context) {
  if (!Context.hasSameType(Type, Context.getPointerDiffType()))
    return false;
  auto Origin = [&](const Decl *D, llvm::StringRef Path) {
    return D && approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx", Path);
  };
  auto Record = [&](const ClassTemplateSpecializationDecl *R, bool Traits) {
    const llvm::StringRef Path = Traits ? "__iterator/iterator_traits.h"
                                        : "__algorithm/iterator_operations.h";
    const auto *Primary = R ? R->getSpecializedTemplate() : nullptr;
    if (!R || !R->isCompleteDefinition() || !R->getIdentifier() ||
        R->getName() != (Traits ? "iterator_traits" : "_IterOps") || !Primary ||
        R->getSpecializationKind() !=
            (Traits ? TSK_ImplicitInstantiation : TSK_ExplicitSpecialization) ||
        R->getTemplateArgs().size() != 1 ||
        R->getTemplateArgs().get(0).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(R->getTemplateArgs().get(0).getAsType(),
                             Traits ? Pointer : Policy))
      return false;
    for (const auto *D : R->redecls())
      if (!Origin(D, Path))
        return false;
    for (const auto *D : Primary->redecls())
      if (!Origin(D, Path) || !Origin(D->getTemplatedDecl(), Path))
        return false;
    if (Traits) {
      const auto *Partial =
          R->getSpecializedTemplateOrPartial()
              .dyn_cast<ClassTemplatePartialSpecializationDecl *>();
      if (!Partial || !Origin(Partial, Path))
        return false;
      for (const auto *D : Partial->redecls())
        if (!Origin(D, Path))
          return false;
    }
    return true;
  };
  unsigned Seen = 0;
  auto Walk = [&](auto &&Self, QualType T, unsigned Depth) -> bool {
    if (T.isNull() || Depth > 12)
      return false;
    if (const auto *E = dyn_cast<ElaboratedType>(T.getTypePtr()))
      return Self(Self, E->getNamedType(), Depth + 1);
    if (const auto *Alias =
            dyn_cast<TemplateSpecializationType>(T.getTypePtr())) {
      const auto *Template = dyn_cast_or_null<TypeAliasTemplateDecl>(
          Alias->getTemplateName().getAsTemplateDecl());
      if (!Template || !Alias->isTypeAlias())
        return false;
      const auto *D = Template->getTemplatedDecl();
      constexpr llvm::StringLiteral Path = "__algorithm/iterator_operations.h";
      for (const auto *Redeclaration : Template->redecls())
        if (!Origin(Redeclaration, Path) ||
            !Origin(Redeclaration->getTemplatedDecl(), Path))
          return false;
      const auto Args = Alias->template_arguments();
      if (D->getName() == "__policy_iter_diff_t") {
        if ((Seen & 1) || Args.size() != 2 ||
            Args[0].getKind() != TemplateArgument::Type ||
            Args[1].getKind() != TemplateArgument::Type ||
            !Context.hasSameType(Args[0].getAsType(), Policy) ||
            !Context.hasSameType(Args[1].getAsType(), Pointer))
          return false;
        Seen |= 1;
      } else if (D->getName() == "__difference_type") {
        if ((Seen & 2) || Args.size() != 1 ||
            Args[0].getKind() != TemplateArgument::Type ||
            !Context.hasSameType(Args[0].getAsType(), Pointer) ||
            !Record(
                dyn_cast<ClassTemplateSpecializationDecl>(D->getDeclContext()),
                false))
          return false;
        Seen |= 2;
      } else
        return false;
      return Self(Self, Alias->getAliasedType(), Depth + 1);
    }
    const auto *Alias = dyn_cast<TypedefType>(T.getTypePtr());
    const auto *D = Alias ? Alias->getDecl() : nullptr;
    if (!D || D->getName() != "difference_type" || (Seen & 4) ||
        !Origin(D, "__iterator/iterator_traits.h") ||
        !Record(dyn_cast<ClassTemplateSpecializationDecl>(D->getDeclContext()),
                true) ||
        !Context.hasSameType(D->getUnderlyingType(),
                             Context.getPointerDiffType()))
      return false;
    Seen |= 4;
    return true;
  };
  return Walk(Walk, Type, 0) && Seen == (Helper ? 7u : 4u);
}

static const CXXOperatorCallExpr *
utilityAlgorithmComposedPredicate(const State &S, const SourceManager &SM,
                                  const FunctionDecl *Function,
                                  llvm::StringRef Name, QualType Pointer,
                                  QualType Object, const ASTContext &Context) {
  const bool All = Name == "all_of", Count = Name == "count_if";
  const auto *Definition = Function->getDefinition();
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 2)
    return nullptr;
  auto Statement = Body->body_begin();
  const auto *Local = dyn_cast<DeclStmt>(*Statement++);
  const auto *Return = dyn_cast<ReturnStmt>(*Statement);
  const auto *Projection = Local && Local->isSingleDecl()
                               ? dyn_cast<VarDecl>(Local->getSingleDecl())
                               : nullptr;
  const auto *Construction =
      Projection ? dyn_cast_or_null<CXXConstructExpr>(Projection->getInit())
                 : nullptr;
  const auto *Call =
      Return ? dyn_cast_or_null<CallExpr>(Return->getRetValue()) : nullptr;
  const auto *F = Call ? Call->getDirectCallee() : nullptr;
  const auto *D = F ? F->getDefinition() : nullptr;
  const auto *Arguments = F ? F->getTemplateSpecializationArgs() : nullptr;
  const std::string Path = "__algorithm/" + Name.str() + ".h";
  if (!Projection || !Projection->hasLocalStorage() ||
      Projection->isImplicit() || Projection->getDeclContext() != Definition ||
      !Construction || Construction->getNumArgs() != 0 ||
      !Construction->getConstructor()->isTrivial() ||
      !Construction->getConstructor()->isDefaultConstructor() || !Call ||
      Call->getNumArgs() != 4 || !D || F->getNumParams() != 4 ||
      !utilitySwapSDKFunction(S, SM, F, "__" + Name.str(), Path) ||
      !utilityAlgorithmSDKReference(S, SM, Call, F) || !Arguments ||
      Arguments->size() != (Count ? 5u : 4u))
    return nullptr;
  const auto ProjType = Projection->getType();
  const auto *ProjRecord = ProjType->getAsCXXRecordDecl();
  if (!ProjRecord || ProjType.hasLocalQualifiers() ||
      Construction->getConstructor()->getParent()->getCanonicalDecl() !=
          ProjRecord->getCanonicalDecl() ||
      !Context.hasSameType(Construction->getType(), ProjType))
    return nullptr;
  for (unsigned I = 0; I != 3; ++I)
    if (!utilityAlgorithmReference(Call->getArg(I), Definition->getParamDecl(I),
                                   Context))
      return nullptr;
  if (!utilityAlgorithmReference(Call->getArg(3), Projection, Context))
    return nullptr;
  const QualType Types[] = {Pointer, Pointer, ProjType, Object};
  const QualType Parameters[] = {Pointer, Pointer,
                                 Context.getLValueReferenceType(Object),
                                 Context.getLValueReferenceType(ProjType)};
  for (unsigned I = 0; I != 4; ++I) {
    const auto &Arg = Arguments->get(I + (Count ? 1 : 0));
    if (Arg.getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arg.getAsType(), Types[I]) ||
        !Context.hasSameType(F->getParamDecl(I)->getType(), Parameters[I]))
      return nullptr;
  }
  QualType Policy;
  if (Count) {
    if (Arguments->get(0).getKind() != TemplateArgument::Type)
      return nullptr;
    Policy = Arguments->get(0).getAsType();
    if (!utilitySwapClassicPolicy(S, SM, Policy) ||
        !utilityAlgorithmCountType(S, SM, Function->getReturnType(), Pointer,
                                   Policy, false, Context) ||
        !utilityAlgorithmCountType(S, SM, F->getReturnType(), Pointer, Policy,
                                   true, Context))
      return nullptr;
  }
  if (!Context.hasSameType(F->getReturnType(),
                           Count ? Context.getPointerDiffType()
                                 : Context.BoolTy) ||
      !Context.hasSameType(Call->getType(), F->getReturnType()) ||
      !Call->isPRValue())
    return nullptr;
  const auto *HelperBody = dyn_cast_or_null<CompoundStmt>(D->getBody());
  if (!HelperBody || HelperBody->size() != (Count ? 3u : 2u))
    return nullptr;
  auto Part = HelperBody->body_begin();
  const VarDecl *Counter = nullptr;
  if (Count) {
    const auto *Declaration = dyn_cast<DeclStmt>(*Part++);
    Counter = Declaration && Declaration->isSingleDecl()
                  ? dyn_cast<VarDecl>(Declaration->getSingleDecl())
                  : nullptr;
    const auto *Zero =
        Counter && Counter->getInit()
            ? dyn_cast<IntegerLiteral>(Counter->getInit()->IgnoreImpCasts())
            : nullptr;
    if (!Counter || Counter->getDeclContext() != D ||
        !Counter->hasLocalStorage() || !Zero || !Zero->getValue().isZero() ||
        !utilityAlgorithmCountType(S, SM, Counter->getType(), Pointer, Policy,
                                   true, Context))
      return nullptr;
  }
  const auto *Loop = dyn_cast<ForStmt>(*Part++);
  const auto *End = dyn_cast<ReturnStmt>(*Part);
  const auto *Condition =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getCond()) : nullptr;
  const auto *Increment =
      Loop ? dyn_cast_or_null<UnaryOperator>(Loop->getInc()) : nullptr;
  const auto *LoopBody =
      Loop ? dyn_cast_or_null<CompoundStmt>(Loop->getBody()) : nullptr;
  const auto *Branch = LoopBody && LoopBody->size() == 1
                           ? dyn_cast<IfStmt>(*LoopBody->body_begin())
                           : nullptr;
  if (!Loop || Loop->getInit() || Loop->getConditionVariable() || !End ||
      !Condition || Condition->getOpcode() != BO_NE ||
      !utilityAlgorithmReference(Condition->getLHS(), D->getParamDecl(0),
                                 Context) ||
      !utilityAlgorithmReference(Condition->getRHS(), D->getParamDecl(1),
                                 Context) ||
      !Increment || Increment->getOpcode() != UO_PreInc ||
      !utilityAlgorithmReference(Increment->getSubExpr(), D->getParamDecl(0),
                                 Context) ||
      !Branch || Branch->getInit() || Branch->getConditionVariable() ||
      Branch->getElse())
    return nullptr;
  auto BooleanReturn = [](const Stmt *Statement, bool Value) {
    const auto *R = dyn_cast<ReturnStmt>(Statement);
    const auto *Literal =
        R ? dyn_cast_or_null<CXXBoolLiteralExpr>(R->getRetValue()) : nullptr;
    return Literal && Literal->getValue() == Value;
  };
  if (Count) {
    const auto *Add = dyn_cast<UnaryOperator>(Branch->getThen());
    if (!Add || Add->getOpcode() != UO_PreInc ||
        !utilityAlgorithmReference(Add->getSubExpr(), Counter, Context) ||
        !utilityAlgorithmReference(End->getRetValue(), Counter, Context))
      return nullptr;
  } else if (!BooleanReturn(End, All) ||
             !BooleanReturn(Branch->getThen(), !All))
    return nullptr;
  const Expr *Test = Branch->getCond();
  if (All) {
    const auto *Not = dyn_cast<UnaryOperator>(Test);
    if (!Not || Not->getOpcode() != UO_LNot)
      return nullptr;
    Test = Not->getSubExpr();
  }
  const auto *Invoke = dyn_cast<CallExpr>(Test);
  const auto Element = Pointer->getPointeeType();
  const auto *Invocation = utilityAlgorithmUnaryDispatch(
      S, SM, Invoke, Object, Element, false, Context);
  if (!Invocation || !utilityAlgorithmReference(Invoke->getArg(0),
                                                D->getParamDecl(2), Context))
    return nullptr;
  const auto *Project = dyn_cast<CallExpr>(Invoke->getArg(1));
  const auto *ProjectionCall = utilityAlgorithmUnaryDispatch(
      S, SM, Project, ProjType, Element, true, Context);
  if (!ProjectionCall ||
      !utilityAlgorithmIdentity(S, SM, ProjectionCall, ProjType, Element,
                                Context) ||
      !utilityAlgorithmReference(Project->getArg(0), D->getParamDecl(3),
                                 Context))
    return nullptr;
  const auto *Read = dyn_cast<UnaryOperator>(Project->getArg(1));
  if (!Read || Read->getOpcode() != UO_Deref || !Read->isLValue() ||
      !Context.hasSameType(Read->getType(), Element) ||
      !utilityAlgorithmReference(Read->getSubExpr(), D->getParamDecl(0),
                                 Context))
    return nullptr;
  return Invocation;
}

static const CXXOperatorCallExpr *
utilityAlgorithmReplacementPredicate(const FunctionDecl *Function, bool Copy,
                                     const ASTContext &Context) {
  const auto *Definition = Function->getDefinition();
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != (Copy ? 2u : 1u))
    return nullptr;
  const unsigned Output = Copy ? 2 : 0, Value = Copy ? 4 : 3;
  auto Parameter = [&](const Expr *E, unsigned I) {
    return utilityAlgorithmReference(E, Definition->getParamDecl(I), Context);
  };
  auto Dereference = [&](const Expr *E, unsigned I) {
    const auto *Read = dyn_cast_or_null<UnaryOperator>(E);
    return Read && Read->getOpcode() == UO_Deref && Read->isLValue() &&
           Context.hasSameType(
               Read->getType(),
               Definition->getParamDecl(I)->getType()->getPointeeType()) &&
           Parameter(Read->getSubExpr(), I);
  };
  auto Increment = [&](const Expr *E, unsigned I) {
    const auto *Add = dyn_cast_or_null<UnaryOperator>(E);
    return Add && Add->getOpcode() == UO_PreInc &&
           Parameter(Add->getSubExpr(), I);
  };
  auto Assignment = [&](const Stmt *S, bool Replacement) {
    const auto *Assign = dyn_cast_or_null<BinaryOperator>(S);
    if (!Assign || Assign->getOpcode() != BO_Assign ||
        !Dereference(Assign->getLHS(), Output) ||
        !Context.hasSameType(Assign->getType(), Assign->getLHS()->getType()))
      return false;
    const Expr *RHS = Assign->getRHS();
    while (const auto *Cast = dyn_cast<ImplicitCastExpr>(RHS)) {
      if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                         Cast->getType()))
        return false;
      RHS = Cast->getSubExpr();
    }
    return Replacement ? Parameter(RHS, Value) : Dereference(RHS, 0);
  };
  auto Statement = Body->body_begin();
  const auto *Loop = dyn_cast<ForStmt>(*Statement++);
  const auto *Condition =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getCond()) : nullptr;
  const auto *Branch =
      Loop ? dyn_cast_or_null<IfStmt>(Loop->getBody()) : nullptr;
  if (!Loop || Loop->getInit() || Loop->getConditionVariable() || !Condition ||
      Condition->getOpcode() != BO_NE || !Parameter(Condition->getLHS(), 0) ||
      !Parameter(Condition->getRHS(), 1) || !Branch || Branch->getInit() ||
      Branch->getConditionVariable() || !Assignment(Branch->getThen(), true))
    return nullptr;
  if (Copy) {
    const auto *Comma = dyn_cast_or_null<BinaryOperator>(Loop->getInc());
    const auto *Discard =
        Comma ? dyn_cast<CStyleCastExpr>(Comma->getRHS()) : nullptr;
    const auto *Return = dyn_cast<ReturnStmt>(*Statement);
    if (!Comma || Comma->getOpcode() != BO_Comma ||
        !Increment(Comma->getLHS(), 0) || !Discard ||
        Discard->getCastKind() != CK_ToVoid ||
        !Discard->getType()->isVoidType() ||
        !Increment(Discard->getSubExpr(), Output) ||
        !Assignment(Branch->getElse(), false) || !Return ||
        !Parameter(Return->getRetValue(), Output))
      return nullptr;
  } else if (!Increment(Loop->getInc(), 0) || Branch->getElse()) {
    return nullptr;
  }
  return dyn_cast<CXXOperatorCallExpr>(Branch->getCond());
}

// Filtering copy loops read each retained element again after the predicate.
// Keep both iterator increments and the conditional conversion/store explicit.
static const Expr *utilityAlgorithmFilterLoop(const FunctionDecl *Definition,
                                              const ASTContext &Context) {
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 2)
    return nullptr;
  auto Parameter = [&](const Expr *E, unsigned I) {
    return utilityAlgorithmReference(E, Definition->getParamDecl(I), Context);
  };
  auto Increment = [&](const Expr *E, unsigned I) {
    const auto *Add = dyn_cast_or_null<UnaryOperator>(E);
    return Add && Add->getOpcode() == UO_PreInc &&
           Parameter(Add->getSubExpr(), I);
  };
  auto Dereference = [&](const Expr *E, unsigned I) {
    const auto *Read = dyn_cast_or_null<UnaryOperator>(E);
    return Read && Read->getOpcode() == UO_Deref && Read->isLValue() &&
           Context.hasSameType(
               Read->getType(),
               Definition->getParamDecl(I)->getType()->getPointeeType()) &&
           Parameter(Read->getSubExpr(), I);
  };
  auto Statement = Body->body_begin();
  const auto *Loop = dyn_cast<ForStmt>(*Statement++);
  const auto *Condition =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getCond()) : nullptr;
  const auto *LoopBody =
      Loop ? dyn_cast_or_null<CompoundStmt>(Loop->getBody()) : nullptr;
  const auto *Branch = LoopBody && LoopBody->size() == 1
                           ? dyn_cast<IfStmt>(*LoopBody->body_begin())
                           : nullptr;
  const auto *Then =
      Branch ? dyn_cast<CompoundStmt>(Branch->getThen()) : nullptr;
  if (!Loop || Loop->getInit() || Loop->getConditionVariable() || !Condition ||
      Condition->getOpcode() != BO_NE || !Parameter(Condition->getLHS(), 0) ||
      !Parameter(Condition->getRHS(), 1) || !Increment(Loop->getInc(), 0) ||
      !Branch || Branch->getInit() || Branch->getConditionVariable() ||
      Branch->getElse() || !Then || Then->size() != 2)
    return nullptr;
  auto Part = Then->body_begin();
  const auto *Assign = dyn_cast<BinaryOperator>(*Part++);
  if (!Assign || Assign->getOpcode() != BO_Assign ||
      !Dereference(Assign->getLHS(), 2) ||
      !Context.hasSameType(Assign->getType(), Assign->getLHS()->getType()) ||
      !Increment(dyn_cast<Expr>(*Part), 2))
    return nullptr;
  const Expr *RHS = Assign->getRHS();
  while (const auto *Cast = dyn_cast<ImplicitCastExpr>(RHS)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return nullptr;
    RHS = Cast->getSubExpr();
  }
  return Dereference(RHS, 0) ? Branch->getCond() : nullptr;
}

static const CXXOperatorCallExpr *
utilityAlgorithmRemoveCopyPredicate(const FunctionDecl *Function,
                                    const ASTContext &Context) {
  const auto *Definition = Function->getDefinition();
  const auto *Condition = utilityAlgorithmFilterLoop(Definition, Context);
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  const auto *End = Body && Body->size() == 2
                        ? dyn_cast<ReturnStmt>(Body->body_back())
                        : nullptr;
  const auto *Not = dyn_cast_or_null<UnaryOperator>(Condition);
  if (!Not || Not->getOpcode() != UO_LNot || !End ||
      !utilityAlgorithmReference(End->getRetValue(),
                                 Definition->getParamDecl(2), Context))
    return nullptr;
  return dyn_cast<CXXOperatorCallExpr>(Not->getSubExpr());
}

static bool utilityAlgorithmScalarForward(
    const State &S, const SourceManager &SM, const CallExpr *Call,
    const ValueDecl *Source, QualType ValueType, bool Move,
    const ASTContext &Context, bool ReadElement = false) {
  const auto *F = Call ? Call->getDirectCallee() : nullptr;
  const auto *Args = F ? F->getTemplateSpecializationArgs() : nullptr;
  const auto Reference = Context.getLValueReferenceType(ValueType);
  if (!Call || !Call->isXValue() || Call->getNumArgs() != 1 || !F ||
      F->getNumParams() != 1 || !Args || Args->size() != 1 ||
      Args->get(0).getKind() != TemplateArgument::Type ||
      !Context.hasSameType(Args->get(0).getAsType(),
                           Move ? Reference : ValueType) ||
      !Context.hasSameType(F->getParamDecl(0)->getType(), Reference) ||
      !Context.hasSameType(F->getReturnType(),
                           Context.getRValueReferenceType(ValueType)) ||
      !Context.hasSameType(Call->getType(), ValueType) ||
      !utilitySwapSDKFunction(S, SM, F, Move ? "move" : "forward",
                              Move ? "__utility/move.h"
                                   : "__utility/forward.h") ||
      !utilityAlgorithmSDKReference(S, SM, Call, F))
    return false;
  const Expr *Argument = Call->getArg(0);
  if (ReadElement) {
    const auto *Read = dyn_cast<UnaryOperator>(Argument);
    if (!Read || Read->getOpcode() != UO_Deref || !Read->isLValue() ||
        !Context.hasSameType(Read->getType(), ValueType))
      return false;
    Argument = Read->getSubExpr();
  }
  if (!utilityAlgorithmReference(Argument, Source, Context))
    return false;
  // Builtin recognition may leave the actual body uninstantiated. The pinned
  // pattern must still return only a static cast of its own formal parameter.
  const auto *Pattern =
      F->getPrimaryTemplate()->getTemplatedDecl()->getDefinition();
  const auto *Body =
      Pattern ? dyn_cast_or_null<CompoundStmt>(Pattern->getBody()) : nullptr;
  const auto *Return = Body && Body->size() == (Move ? 2u : 1u)
                           ? dyn_cast<ReturnStmt>(Body->body_back())
                           : nullptr;
  const auto *Cast =
      Return ? dyn_cast_or_null<CXXStaticCastExpr>(Return->getRetValue())
             : nullptr;
  const auto *Ref = Cast ? dyn_cast<DeclRefExpr>(Cast->getSubExpr()) : nullptr;
  if (!Cast || !Ref || Ref->getDecl() != Pattern->getParamDecl(0) ||
      !Cast->getTypeAsWritten()->isRValueReferenceType())
    return false;
  if (Move) {
    const auto *Local = dyn_cast<DeclStmt>(*Body->body_begin());
    const auto *Alias = Local && Local->isSingleDecl()
                            ? dyn_cast<TypeAliasDecl>(Local->getSingleDecl())
                            : nullptr;
    if (!Alias || Alias->getDeclContext() != Pattern ||
        Alias->getName() != "_Up" ||
        !approvedStandardSDKDeclaration(S, SM, Alias) ||
        !cstddefOrigin(S, SM, Alias->getLocation(), "libcxx",
                       "__utility/move.h"))
      return false;
  }
  return true;
}

// The public wrapper projects the output pointer from the exact helper pair.
// Its predicate/projection remain references to the wrapper's own objects.
static const CXXOperatorCallExpr *
utilityAlgorithmCopyPredicate(const State &S, const SourceManager &SM,
                              const FunctionDecl *Function, QualType Pointer,
                              QualType Output, QualType Object,
                              const ASTContext &Context) {
  const auto *Definition = Function->getDefinition();
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 2)
    return nullptr;
  auto Statement = Body->body_begin();
  const auto *Local = dyn_cast<DeclStmt>(*Statement++);
  const auto *Return = dyn_cast<ReturnStmt>(*Statement);
  const auto *Projection = Local && Local->isSingleDecl()
                               ? dyn_cast<VarDecl>(Local->getSingleDecl())
                               : nullptr;
  const auto *Construction =
      Projection ? dyn_cast_or_null<CXXConstructExpr>(Projection->getInit())
                 : nullptr;
  const auto *Cleanup =
      Return ? dyn_cast_or_null<ExprWithCleanups>(Return->getRetValue())
             : nullptr;
  const auto *Load =
      Cleanup ? dyn_cast<ImplicitCastExpr>(Cleanup->getSubExpr()) : nullptr;
  const auto *Access =
      Load ? dyn_cast<MemberExpr>(Load->getSubExpr()) : nullptr;
  const auto *Temporary =
      Access ? dyn_cast<MaterializeTemporaryExpr>(Access->getBase()) : nullptr;
  const auto *Call =
      Temporary ? dyn_cast<CallExpr>(Temporary->getSubExpr()) : nullptr;
  const auto *F = Call ? Call->getDirectCallee() : nullptr;
  const auto *D = F ? F->getDefinition() : nullptr;
  const auto *Arguments = F ? F->getTemplateSpecializationArgs() : nullptr;
  if (!Projection || !Projection->hasLocalStorage() ||
      Projection->isImplicit() || Projection->getDeclContext() != Definition ||
      !Construction || Construction->getNumArgs() ||
      !Construction->getConstructor()->isTrivial() ||
      !Construction->getConstructor()->isDefaultConstructor() || !Cleanup ||
      Cleanup->getNumObjects() || !Load ||
      Load->getCastKind() != CK_LValueToRValue ||
      !Context.hasSameType(Load->getType(), Output) || !Access ||
      Access->isArrow() || !Access->isXValue() ||
      !Context.hasSameType(Access->getType(), Output) || !Temporary ||
      !Temporary->isXValue() || Temporary->getExtendingDecl() || !Call ||
      !Call->isPRValue() || Call->getNumArgs() != 5 || !D ||
      F->getNumParams() != 5 ||
      !utilitySwapSDKFunction(S, SM, F, "__copy_if", "__algorithm/copy_if.h") ||
      !utilityAlgorithmSDKReference(S, SM, Call, F) || !Arguments ||
      Arguments->size() != 5)
    return nullptr;
  const auto ProjType = Projection->getType();
  const auto *ProjRecord = ProjType->getAsCXXRecordDecl();
  if (!ProjRecord || ProjType.hasLocalQualifiers() ||
      Construction->getConstructor()->getParent()->getCanonicalDecl() !=
          ProjRecord->getCanonicalDecl() ||
      !Context.hasSameType(Construction->getType(), ProjType))
    return nullptr;
  for (unsigned I = 0; I != 4; ++I)
    if (!utilityAlgorithmReference(Call->getArg(I), Definition->getParamDecl(I),
                                   Context))
      return nullptr;
  if (!utilityAlgorithmReference(Call->getArg(4), Projection, Context))
    return nullptr;
  const QualType Types[] = {Pointer, Pointer, Output, ProjType, Object};
  const QualType Parameters[] = {Pointer, Pointer, Output,
                                 Context.getLValueReferenceType(Object),
                                 Context.getLValueReferenceType(ProjType)};
  for (unsigned I = 0; I != 5; ++I)
    if (Arguments->get(I).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(I).getAsType(), Types[I]) ||
        !Context.hasSameType(F->getParamDecl(I)->getType(), Parameters[I]))
      return nullptr;
  const auto Pair = approvedUtilityPairRecord(
      S, SM, Call->getType()->getAsCXXRecordDecl(), Context);
  if (!Pair || Access->getMemberDecl() != Pair->Second ||
      !Context.hasSameType(Pair->First->getType(), Pointer) ||
      !Context.hasSameType(Pair->Second->getType(), Output) ||
      !Context.hasSameType(F->getReturnType(), Call->getType()) ||
      !Context.hasSameType(Temporary->getType(), Call->getType()))
    return nullptr;
  const auto *Condition = utilityAlgorithmFilterLoop(D, Context);
  const auto *HelperBody = dyn_cast_or_null<CompoundStmt>(D->getBody());
  const auto *End = HelperBody && HelperBody->size() == 2
                        ? dyn_cast<ReturnStmt>(HelperBody->body_back())
                        : nullptr;
  const auto *Make =
      End ? dyn_cast_or_null<CallExpr>(End->getRetValue()) : nullptr;
  const auto *Maker = Make ? Make->getDirectCallee() : nullptr;
  const auto *MakeArgs =
      Maker ? Maker->getTemplateSpecializationArgs() : nullptr;
  if (!Condition || !Make || !Make->isPRValue() || Make->getNumArgs() != 2 ||
      !Maker || Maker->getNumParams() != 2 || !MakeArgs ||
      MakeArgs->size() != 2 ||
      !utilitySwapSDKFunction(S, SM, Maker, "make_pair", "__utility/pair.h") ||
      !utilityAlgorithmSDKReference(S, SM, Make, Maker) ||
      !Context.hasSameType(Make->getType(), Call->getType()))
    return nullptr;
  const auto *MakeDefinition = Maker->getDefinition();
  const auto *MakeBody =
      MakeDefinition ? dyn_cast_or_null<CompoundStmt>(MakeDefinition->getBody())
                     : nullptr;
  const auto *MakeReturn = MakeBody && MakeBody->size() == 1
                               ? dyn_cast<ReturnStmt>(*MakeBody->body_begin())
                               : nullptr;
  const auto *Construct =
      MakeReturn ? dyn_cast_or_null<CXXConstructExpr>(MakeReturn->getRetValue())
                 : nullptr;
  if (!Construct || Construct->getNumArgs() != 2 ||
      !Context.hasSameType(Construct->getType(), Call->getType()) ||
      !approvedUtilityPairConstruction(S, SM, Construct, Context))
    return nullptr;
  for (unsigned I = 0; I != 2; ++I) {
    const auto T = I ? Output : Pointer;
    if (MakeArgs->get(I).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(MakeArgs->get(I).getAsType(), T) ||
        !Context.hasSameType(Maker->getParamDecl(I)->getType(),
                             Context.getRValueReferenceType(T)) ||
        !utilityAlgorithmScalarForward(
            S, SM, dyn_cast<CallExpr>(Make->getArg(I)),
            D->getParamDecl(I ? 2 : 0), T, true, Context) ||
        !utilityAlgorithmScalarForward(
            S, SM, dyn_cast<CallExpr>(Construct->getArg(I)),
            MakeDefinition->getParamDecl(I), T, false, Context))
      return nullptr;
  }
  const auto *Invoke = dyn_cast<CallExpr>(Condition);
  const auto Element = Pointer->getPointeeType();
  const auto *Invocation = utilityAlgorithmUnaryDispatch(
      S, SM, Invoke, Object, Element, false, Context);
  if (!Invocation || !utilityAlgorithmReference(Invoke->getArg(0),
                                                D->getParamDecl(3), Context))
    return nullptr;
  const auto *Project = dyn_cast<CallExpr>(Invoke->getArg(1));
  const auto *ProjectionCall = utilityAlgorithmUnaryDispatch(
      S, SM, Project, ProjType, Element, true, Context);
  if (!ProjectionCall ||
      !utilityAlgorithmIdentity(S, SM, ProjectionCall, ProjType, Element,
                                Context) ||
      !utilityAlgorithmReference(Project->getArg(0), D->getParamDecl(4),
                                 Context))
    return nullptr;
  const auto *Read = dyn_cast<UnaryOperator>(Project->getArg(1));
  if (!Read || Read->getOpcode() != UO_Deref || !Read->isLValue() ||
      !Context.hasSameType(Read->getType(), Element) ||
      !utilityAlgorithmReference(Read->getSubExpr(), D->getParamDecl(0),
                                 Context))
    return nullptr;
  return Invocation;
}

// Authenticate the pointer-specialized binary search and its exact distance,
// positive-half and advance helpers before selecting the one predicate call.
static const CXXOperatorCallExpr *utilityAlgorithmPartitionPointPredicate(
    const State &S, const SourceManager &SM, const FunctionDecl *Function,
    QualType Pointer, const ASTContext &Context) {
  const auto *Definition = Function->getDefinition();
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 4)
    return nullptr;
  const auto Difference = Context.getPointerDiffType();
  auto Reference = [&](const Expr *Expression, const ValueDecl *Value) {
    return utilityAlgorithmReference(Expression, Value, Context);
  };
  auto Local = [&](const Stmt *Statement, QualType Type) {
    const auto *Declaration = dyn_cast_or_null<DeclStmt>(Statement);
    const auto *Variable = Declaration && Declaration->isSingleDecl()
                               ? dyn_cast<VarDecl>(Declaration->getSingleDecl())
                               : nullptr;
    return Variable && Variable->hasLocalStorage() && !Variable->isImplicit() &&
                   Variable->getDeclContext() == Definition &&
                   Context.hasSameType(Variable->getType(), Type)
               ? Variable
               : nullptr;
  };
  auto Integer = [](const Expr *Expression, uint64_t Value) {
    const auto *Literal = dyn_cast_or_null<IntegerLiteral>(
        Expression ? Expression->IgnoreParenImpCasts() : nullptr);
    return Literal && Literal->getValue() == Value;
  };
  auto SDKCall = [&](const CallExpr *Call, llvm::StringRef Name,
                     llvm::StringRef Path, unsigned Parameters) {
    const auto *Callee = Call ? Call->getDirectCallee() : nullptr;
    return Call && Call->getNumArgs() == Parameters && Callee &&
           Callee->getNumParams() == Parameters &&
           utilitySwapSDKFunction(S, SM, Callee, Name, Path) &&
           utilityAlgorithmSDKReference(S, SM, Call, Callee);
  };

  auto Statement = Body->body_begin();
  const auto *DifferenceDeclaration = dyn_cast<DeclStmt>(*Statement++);
  const auto *DifferenceAlias =
      DifferenceDeclaration && DifferenceDeclaration->isSingleDecl()
          ? dyn_cast<TypedefDecl>(DifferenceDeclaration->getSingleDecl())
          : nullptr;
  const auto *Length = Local(*Statement++, Difference);
  const auto *Loop = dyn_cast<WhileStmt>(*Statement++);
  const auto *Return = dyn_cast<ReturnStmt>(*Statement);
  const auto *Distance =
      Length ? dyn_cast_or_null<CallExpr>(Length->getInit()) : nullptr;
  const auto *DistanceFunction =
      Distance ? Distance->getDirectCallee() : nullptr;
  const auto *DistanceArguments =
      DistanceFunction ? DistanceFunction->getTemplateSpecializationArgs()
                       : nullptr;
  if (!DifferenceAlias || DifferenceAlias->isImplicit() ||
      DifferenceAlias->getDeclContext() != Definition ||
      DifferenceAlias->getName() != "difference_type" ||
      !Context.hasSameType(DifferenceAlias->getUnderlyingType(), Difference) ||
      !Length || !SDKCall(Distance, "distance", "__iterator/distance.h", 2) ||
      !Distance->isPRValue() ||
      !Context.hasSameType(Distance->getType(), Difference) ||
      !DistanceArguments || DistanceArguments->size() != 1 ||
      DistanceArguments->get(0).getKind() != TemplateArgument::Type ||
      !Context.hasSameType(DistanceArguments->get(0).getAsType(), Pointer) ||
      !Context.hasSameType(DistanceFunction->getParamDecl(0)->getType(),
                           Pointer) ||
      !Context.hasSameType(DistanceFunction->getParamDecl(1)->getType(),
                           Pointer) ||
      !Context.hasSameType(DistanceFunction->getReturnType(), Difference) ||
      !Reference(Distance->getArg(0), Definition->getParamDecl(0)) ||
      !Reference(Distance->getArg(1), Definition->getParamDecl(1)) || !Loop ||
      Loop->getConditionVariable() || !Loop->getBody() || !Return ||
      !Reference(Return->getRetValue(), Definition->getParamDecl(0)))
    return nullptr;

  const auto *Condition = dyn_cast<BinaryOperator>(Loop->getCond());
  const auto *LoopBody = dyn_cast<CompoundStmt>(Loop->getBody());
  if (!Condition || Condition->getOpcode() != BO_NE ||
      !Reference(Condition->getLHS(), Length) ||
      !Integer(Condition->getRHS(), 0) || !LoopBody || LoopBody->size() != 4)
    return nullptr;
  auto Part = LoopBody->body_begin();
  const auto *Half = Local(*Part++, Difference);
  const auto *Middle = Local(*Part++, Pointer);
  const auto *Advance = dyn_cast<CallExpr>(*Part++);
  const auto *Branch = dyn_cast<IfStmt>(*Part);
  const auto *HalfCall =
      Half ? dyn_cast_or_null<CallExpr>(Half->getInit()) : nullptr;
  const auto *HalfFunction = HalfCall ? HalfCall->getDirectCallee() : nullptr;
  const auto *HalfArguments =
      HalfFunction ? HalfFunction->getTemplateSpecializationArgs() : nullptr;
  if (!Half ||
      !SDKCall(HalfCall, "__half_positive", "__algorithm/half_positive.h", 1) ||
      !HalfCall->isPRValue() ||
      !Context.hasSameType(HalfCall->getType(), Difference) || !HalfArguments ||
      HalfArguments->size() < 1 ||
      HalfArguments->get(0).getKind() != TemplateArgument::Type ||
      !Context.hasSameType(HalfArguments->get(0).getAsType(), Difference) ||
      !Context.hasSameType(HalfFunction->getParamDecl(0)->getType(),
                           Difference) ||
      !Context.hasSameType(HalfFunction->getReturnType(), Difference) ||
      !Reference(HalfCall->getArg(0), Length) || !Middle ||
      !Reference(Middle->getInit(), Definition->getParamDecl(0)) ||
      !SDKCall(Advance, "advance", "__iterator/advance.h", 2) ||
      !Advance->getType()->isVoidType() ||
      !Context.hasSameType(
          Advance->getDirectCallee()->getParamDecl(0)->getType(),
          Context.getLValueReferenceType(Pointer)) ||
      !Context.hasSameType(
          Advance->getDirectCallee()->getParamDecl(1)->getType(), Difference) ||
      !Reference(Advance->getArg(0), Middle) ||
      !Reference(Advance->getArg(1), Half) || !Branch || Branch->getInit() ||
      Branch->getConditionVariable() || !Branch->getElse())
    return nullptr;

  const auto *Invocation = dyn_cast<CXXOperatorCallExpr>(Branch->getCond());
  const auto *Then = dyn_cast<CompoundStmt>(Branch->getThen());
  const auto *Else = dyn_cast<BinaryOperator>(Branch->getElse());
  if (!Invocation || Invocation->getOperator() != OO_Call ||
      Invocation->getNumArgs() != 2 || !Invocation->isPRValue() ||
      !Invocation->getType()->isBooleanType() ||
      !Reference(Invocation->getArg(0), Definition->getParamDecl(2)) || !Then ||
      Then->size() != 2 || !Else || Else->getOpcode() != BO_Assign ||
      !Reference(Else->getLHS(), Length) || !Reference(Else->getRHS(), Half))
    return nullptr;
  const Expr *Argument = Invocation->getArg(1);
  while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Argument)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return nullptr;
    Argument = Cast->getSubExpr();
  }
  const auto *Element = dyn_cast<UnaryOperator>(Argument);
  if (!Element || Element->getOpcode() != UO_Deref || !Element->isLValue() ||
      !Context.hasSameType(Element->getType(), Pointer->getPointeeType()) ||
      !Reference(Element->getSubExpr(), Middle))
    return nullptr;

  auto ThenPart = Then->body_begin();
  const auto *AdvanceFirst = dyn_cast<BinaryOperator>(*ThenPart++);
  const auto *Increment =
      AdvanceFirst
          ? dyn_cast<UnaryOperator>(AdvanceFirst->getRHS()->IgnoreImpCasts())
          : nullptr;
  const auto *Reduce = dyn_cast<CompoundAssignOperator>(*ThenPart);
  const auto *Reduction =
      Reduce ? dyn_cast<BinaryOperator>(Reduce->getRHS()) : nullptr;
  if (!AdvanceFirst || AdvanceFirst->getOpcode() != BO_Assign ||
      !Reference(AdvanceFirst->getLHS(), Definition->getParamDecl(0)) ||
      !Increment || Increment->getOpcode() != UO_PreInc ||
      !Reference(Increment->getSubExpr(), Middle) || !Reduce ||
      Reduce->getOpcode() != BO_SubAssign ||
      !Reference(Reduce->getLHS(), Length) || !Reduction ||
      Reduction->getOpcode() != BO_Add ||
      !Reference(Reduction->getLHS(), Half) || !Integer(Reduction->getRHS(), 1))
    return nullptr;
  return Invocation;
}

// The two scans share the one predicate parameter and skip the first false
// element between them. Verify both selected calls, including their receiver.
static const CXXOperatorCallExpr *
utilityAlgorithmPartitionPredicate(const FunctionDecl *Function,
                                   const ASTContext &Context) {
  const auto *Definition = Function->getDefinition();
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 5)
    return nullptr;
  auto Parameter = [&](const Expr *E, unsigned I) {
    return utilityAlgorithmReference(E, Definition->getParamDecl(I), Context);
  };
  auto Increment = [&](const Expr *E) {
    const auto *Add = dyn_cast_or_null<UnaryOperator>(E);
    return Add && Add->getOpcode() == UO_PreInc &&
           Parameter(Add->getSubExpr(), 0);
  };
  auto Compare = [&](const Expr *E, BinaryOperatorKind Kind) {
    const auto *Comparison = dyn_cast_or_null<BinaryOperator>(E);
    return Comparison && Comparison->getOpcode() == Kind &&
           Parameter(Comparison->getLHS(), 0) &&
           Parameter(Comparison->getRHS(), 1);
  };
  auto BooleanReturn = [](const Stmt *S, bool Value) {
    const auto *R = dyn_cast_or_null<ReturnStmt>(S);
    const auto *Literal =
        R ? dyn_cast_or_null<CXXBoolLiteralExpr>(R->getRetValue()) : nullptr;
    return Literal && Literal->getValue() == Value;
  };
  auto Scan = [&](const Stmt *S, bool Leading) -> const CXXOperatorCallExpr * {
    const auto *Loop = dyn_cast_or_null<ForStmt>(S);
    const auto *Branch =
        Loop ? dyn_cast_or_null<IfStmt>(Loop->getBody()) : nullptr;
    if (!Loop || Loop->getInit() || Loop->getConditionVariable() ||
        !Compare(Loop->getCond(), BO_NE) || !Increment(Loop->getInc()) ||
        !Branch || Branch->getInit() || Branch->getConditionVariable() ||
        Branch->getElse() ||
        (Leading ? !isa<BreakStmt>(Branch->getThen())
                 : !BooleanReturn(Branch->getThen(), false)))
      return nullptr;
    const Expr *Test = Branch->getCond();
    if (Leading) {
      const auto *Not = dyn_cast<UnaryOperator>(Test);
      if (!Not || Not->getOpcode() != UO_LNot)
        return nullptr;
      Test = Not->getSubExpr();
    }
    const auto *Call = dyn_cast<CXXOperatorCallExpr>(Test);
    if (!Call || Call->getOperator() != OO_Call || Call->getNumArgs() != 2 ||
        !Call->isPRValue() || !Call->getType()->isBooleanType() ||
        !Call->getArg(0)->isLValue() || !Parameter(Call->getArg(0), 2))
      return nullptr;
    const Expr *Argument = Call->getArg(1);
    while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Argument)) {
      if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                         Cast->getType()))
        return nullptr;
      Argument = Cast->getSubExpr();
    }
    const auto *Element = dyn_cast<UnaryOperator>(Argument);
    if (!Element || Element->getOpcode() != UO_Deref || !Element->isLValue() ||
        !Context.hasSameType(
            Element->getType(),
            Definition->getParamDecl(0)->getType()->getPointeeType()) ||
        !Parameter(Element->getSubExpr(), 0))
      return nullptr;
    return Call;
  };
  auto Part = Body->body_begin();
  const auto *Leading = Scan(*Part++, true);
  const auto *EndOfRange = dyn_cast<IfStmt>(*Part++);
  const auto *Advance = dyn_cast<Expr>(*Part++);
  const auto *Tail = Scan(*Part++, false);
  if (!Leading || !Tail || !Leading->getDirectCallee() ||
      Leading->getDirectCallee() != Tail->getDirectCallee() || !EndOfRange ||
      EndOfRange->getInit() || EndOfRange->getConditionVariable() ||
      EndOfRange->getElse() || !Compare(EndOfRange->getCond(), BO_EQ) ||
      !BooleanReturn(EndOfRange->getThen(), true) || !Increment(Advance) ||
      !BooleanReturn(*Part, true))
    return nullptr;
  return Leading;
}

static const CXXOperatorCallExpr *
utilityAlgorithmPartitionCopyPredicate(const State &S, const SourceManager &SM,
                                       const FunctionDecl *Function,
                                       const ASTContext &Context) {
  const auto *Definition = Function->getDefinition();
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 2)
    return nullptr;
  auto Parameter = [&](const Expr *E, unsigned I) {
    return utilityAlgorithmReference(E, Definition->getParamDecl(I), Context);
  };
  auto Increment = [&](const Expr *E, unsigned I) {
    const auto *Add = dyn_cast_or_null<UnaryOperator>(E);
    return Add && Add->getOpcode() == UO_PreInc &&
           Parameter(Add->getSubExpr(), I);
  };
  auto Dereference = [&](const Expr *E, unsigned I) {
    const auto *Read = dyn_cast_or_null<UnaryOperator>(E);
    return Read && Read->getOpcode() == UO_Deref && Read->isLValue() &&
           Context.hasSameType(
               Read->getType(),
               Definition->getParamDecl(I)->getType()->getPointeeType()) &&
           Parameter(Read->getSubExpr(), I);
  };
  auto Copy = [&](const Stmt *S, unsigned I) {
    const auto *Block = dyn_cast_or_null<CompoundStmt>(S);
    if (!Block || Block->size() != 2)
      return false;
    auto Part = Block->body_begin();
    const auto *Assign = dyn_cast<BinaryOperator>(*Part++);
    if (!Assign || Assign->getOpcode() != BO_Assign ||
        !Dereference(Assign->getLHS(), I) ||
        !Context.hasSameType(Assign->getType(), Assign->getLHS()->getType()) ||
        !Increment(dyn_cast<Expr>(*Part), I))
      return false;
    const Expr *RHS = Assign->getRHS();
    while (const auto *Cast = dyn_cast<ImplicitCastExpr>(RHS)) {
      if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                         Cast->getType()))
        return false;
      RHS = Cast->getSubExpr();
    }
    return Dereference(RHS, 0);
  };
  auto Part = Body->body_begin();
  const auto *Loop = dyn_cast<ForStmt>(*Part++);
  const auto *Return = dyn_cast<ReturnStmt>(*Part);
  const auto *Condition =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getCond()) : nullptr;
  const auto *LoopBody =
      Loop ? dyn_cast_or_null<CompoundStmt>(Loop->getBody()) : nullptr;
  const auto *Branch = LoopBody && LoopBody->size() == 1
                           ? dyn_cast<IfStmt>(*LoopBody->body_begin())
                           : nullptr;
  const auto *Construction =
      Return ? dyn_cast_or_null<CXXConstructExpr>(Return->getRetValue())
             : nullptr;
  if (!Loop || Loop->getInit() || Loop->getConditionVariable() || !Condition ||
      Condition->getOpcode() != BO_NE || !Parameter(Condition->getLHS(), 0) ||
      !Parameter(Condition->getRHS(), 1) || !Increment(Loop->getInc(), 0) ||
      !Branch || Branch->getInit() || Branch->getConditionVariable() ||
      !Copy(Branch->getThen(), 2) || !Copy(Branch->getElse(), 3) ||
      !Construction || Construction->getNumArgs() != 2 ||
      !Context.hasSameType(Construction->getType(),
                           Function->getReturnType()) ||
      !approvedUtilityPairConstruction(S, SM, Construction, Context) ||
      !Parameter(Construction->getArg(0), 2) ||
      !Parameter(Construction->getArg(1), 3))
    return nullptr;
  return dyn_cast<CXXOperatorCallExpr>(Branch->getCond());
}

static const CXXOperatorCallExpr *
utilityAlgorithmSearchPredicate(const FunctionDecl *Function,
                                llvm::StringRef Name,
                                const ASTContext &Context) {
  const auto *Definition = Function->getDefinition();
  const bool None = Name == "none_of", FindNot = Name == "find_if_not";
  auto Parameter = [&](const Expr *Expression, unsigned I) {
    while (const auto *Cast = dyn_cast_or_null<ImplicitCastExpr>(Expression)) {
      if ((Cast->getCastKind() != CK_LValueToRValue &&
           Cast->getCastKind() != CK_NoOp) ||
          !Context.hasSameUnqualifiedType(Cast->getType(),
                                          Cast->getSubExpr()->getType()))
        return false;
      Expression = Cast->getSubExpr();
    }
    const auto *Reference = dyn_cast_or_null<DeclRefExpr>(Expression);
    return Reference && Reference->isLValue() &&
           Reference->getDecl() == Definition->getParamDecl(I);
  };
  auto BooleanReturn = [](const Stmt *Statement, bool Value) {
    const auto *Return = dyn_cast_or_null<ReturnStmt>(Statement);
    const auto *Literal =
        Return ? dyn_cast_or_null<CXXBoolLiteralExpr>(Return->getRetValue())
               : nullptr;
    return Literal && Literal->getValue() == Value;
  };
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 2)
    return nullptr;
  auto Statement = Body->body_begin();
  const auto *Loop = dyn_cast<ForStmt>(*Statement++);
  const auto *Return = dyn_cast<ReturnStmt>(*Statement);
  const auto *Condition =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getCond()) : nullptr;
  const auto *Increment =
      Loop ? dyn_cast_or_null<UnaryOperator>(Loop->getInc()) : nullptr;
  const auto *Branch =
      Loop ? dyn_cast_or_null<IfStmt>(Loop->getBody()) : nullptr;
  if (!Loop || Loop->getInit() || Loop->getConditionVariable() || !Condition ||
      Condition->getOpcode() != BO_NE || !Parameter(Condition->getLHS(), 0) ||
      !Parameter(Condition->getRHS(), 1) || !Increment ||
      Increment->getOpcode() != UO_PreInc ||
      !Parameter(Increment->getSubExpr(), 0) || !Branch || Branch->getInit() ||
      Branch->getConditionVariable() || Branch->getElse() || !Return ||
      (None ? !BooleanReturn(Return, true)
            : !Parameter(Return->getRetValue(), 0)) ||
      (None ? !BooleanReturn(Branch->getThen(), false)
            : !isa<BreakStmt>(Branch->getThen())))
    return nullptr;
  const Expr *Predicate = Branch->getCond();
  if (FindNot) {
    const auto *Negation = dyn_cast<UnaryOperator>(Predicate);
    if (!Negation || Negation->getOpcode() != UO_LNot)
      return nullptr;
    Predicate = Negation->getSubExpr();
  }
  return dyn_cast<CXXOperatorCallExpr>(Predicate);
}

static const CXXOperatorCallExpr *
utilityAlgorithmRemovePredicate(const State &S, const SourceManager &SM,
                                const FunctionDecl *Function, QualType Pointer,
                                QualType Object, const ASTContext &Context) {
  const auto *Definition = Function->getDefinition();
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 3)
    return nullptr;
  auto Parameter = [&](const Expr *E, unsigned I) {
    return utilityAlgorithmReference(E, Definition->getParamDecl(I), Context);
  };
  auto Part = Body->body_begin();
  const auto *Start = dyn_cast<BinaryOperator>(*Part++);
  const auto *Outer = dyn_cast<IfStmt>(*Part++);
  const auto *End = dyn_cast<ReturnStmt>(*Part);
  const auto *Find = Start ? dyn_cast<CallExpr>(Start->getRHS()) : nullptr;
  const auto *F = Find ? Find->getDirectCallee() : nullptr;
  const auto *D = F ? F->getDefinition() : nullptr;
  const auto *Arguments = F ? F->getTemplateSpecializationArgs() : nullptr;
  const auto *Different =
      Outer ? dyn_cast<BinaryOperator>(Outer->getCond()) : nullptr;
  const auto *Block =
      Outer ? dyn_cast<CompoundStmt>(Outer->getThen()) : nullptr;
  if (!Start || Start->getOpcode() != BO_Assign ||
      !Parameter(Start->getLHS(), 0) || !Find || Find->getNumArgs() != 3 ||
      !Find->isPRValue() || !D ||
      !utilitySwapSDKFunction(S, SM, F, "find_if", "__algorithm/find_if.h") ||
      !utilityAlgorithmSDKReference(S, SM, Find, F) || F->getNumParams() != 3 ||
      !Arguments || Arguments->size() != 2 ||
      Arguments->get(0).getKind() != TemplateArgument::Type ||
      Arguments->get(1).getKind() != TemplateArgument::Type ||
      !Context.hasSameType(Arguments->get(0).getAsType(), Pointer) ||
      !Context.hasSameType(Arguments->get(1).getAsType(),
                           Context.getLValueReferenceType(Object)) ||
      !Context.hasSameType(F->getReturnType(), Pointer) ||
      !Context.hasSameType(Find->getType(), Pointer) ||
      !Context.hasSameType(F->getParamDecl(0)->getType(), Pointer) ||
      !Context.hasSameType(F->getParamDecl(1)->getType(), Pointer) ||
      !Context.hasSameType(F->getParamDecl(2)->getType(),
                           Context.getLValueReferenceType(Object)) ||
      !Outer || Outer->getInit() || Outer->getConditionVariable() ||
      Outer->getElse() || !Different || Different->getOpcode() != BO_NE ||
      !Parameter(Different->getLHS(), 0) ||
      !Parameter(Different->getRHS(), 1) || !Block || Block->size() != 2 ||
      !End || !Parameter(End->getRetValue(), 0))
    return nullptr;
  for (unsigned I = 0; I != 3; ++I)
    if (!Parameter(Find->getArg(I), I))
      return nullptr;
  const auto *FirstCall =
      utilityAlgorithmSearchPredicate(F, "find_if", Context);
  Part = Block->body_begin();
  const auto *Local = dyn_cast<DeclStmt>(*Part++);
  const auto *Scan = Local && Local->isSingleDecl()
                         ? dyn_cast<VarDecl>(Local->getSingleDecl())
                         : nullptr;
  const auto *Loop = dyn_cast<WhileStmt>(*Part);
  const auto *Condition =
      Loop ? dyn_cast<BinaryOperator>(Loop->getCond()) : nullptr;
  const auto *IncrementValue =
      Condition ? dyn_cast<ImplicitCastExpr>(Condition->getLHS()) : nullptr;
  const auto *Increment =
      IncrementValue ? dyn_cast<UnaryOperator>(IncrementValue->getSubExpr())
                     : nullptr;
  const auto *LoopBody =
      Loop ? dyn_cast<CompoundStmt>(Loop->getBody()) : nullptr;
  const auto *Branch = LoopBody && LoopBody->size() == 1
                           ? dyn_cast<IfStmt>(*LoopBody->body_begin())
                           : nullptr;
  const auto *Not =
      Branch ? dyn_cast<UnaryOperator>(Branch->getCond()) : nullptr;
  const auto *TailCall =
      Not ? dyn_cast<CXXOperatorCallExpr>(Not->getSubExpr()) : nullptr;
  const auto *Transfer =
      Branch ? dyn_cast<CompoundStmt>(Branch->getThen()) : nullptr;
  if (!FirstCall || !Scan || !Scan->hasLocalStorage() || Scan->isImplicit() ||
      Scan->getDeclContext() != Definition ||
      !Context.hasSameType(Scan->getType(), Pointer) ||
      !Parameter(Scan->getInit(), 0) || !Loop || Loop->getConditionVariable() ||
      !Condition || Condition->getOpcode() != BO_NE || !IncrementValue ||
      IncrementValue->getCastKind() != CK_LValueToRValue ||
      !Context.hasSameType(IncrementValue->getType(), Pointer) || !Increment ||
      Increment->getOpcode() != UO_PreInc ||
      !utilityAlgorithmReference(Increment->getSubExpr(), Scan, Context) ||
      !Parameter(Condition->getRHS(), 1) || !Branch || Branch->getInit() ||
      Branch->getConditionVariable() || Branch->getElse() || !Not ||
      Not->getOpcode() != UO_LNot || !TailCall || !Transfer ||
      Transfer->size() != 2 || !FirstCall->getDirectCallee() ||
      FirstCall->getDirectCallee() != TailCall->getDirectCallee())
    return nullptr;
  auto Invocation = [&](const CXXOperatorCallExpr *Call,
                        const ValueDecl *Receiver, const ValueDecl *Iterator) {
    if (Call->getOperator() != OO_Call || Call->getNumArgs() != 2 ||
        !Call->isPRValue() || !Call->getType()->isBooleanType() ||
        !Call->getArg(0)->isLValue() ||
        !utilityAlgorithmReference(Call->getArg(0), Receiver, Context))
      return false;
    const Expr *Argument = Call->getArg(1);
    while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Argument)) {
      if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                         Cast->getType()))
        return false;
      Argument = Cast->getSubExpr();
    }
    const auto *Element = dyn_cast<UnaryOperator>(Argument);
    return Element && Element->getOpcode() == UO_Deref && Element->isLValue() &&
           Context.hasSameType(Element->getType(), Pointer->getPointeeType()) &&
           utilityAlgorithmReference(Element->getSubExpr(), Iterator, Context);
  };
  if (!Invocation(FirstCall, D->getParamDecl(2), D->getParamDecl(0)) ||
      !Invocation(TailCall, Definition->getParamDecl(2), Scan))
    return nullptr;
  Part = Transfer->body_begin();
  const auto *Assign = dyn_cast<BinaryOperator>(*Part++);
  const auto *Advance = dyn_cast<UnaryOperator>(*Part);
  const auto *Place =
      Assign ? dyn_cast<UnaryOperator>(Assign->getLHS()) : nullptr;
  const auto *Read =
      Assign ? dyn_cast<ImplicitCastExpr>(Assign->getRHS()) : nullptr;
  const auto *Move = Read ? dyn_cast<CallExpr>(Read->getSubExpr()) : nullptr;
  if (!Assign || Assign->getOpcode() != BO_Assign || !Place ||
      Place->getOpcode() != UO_Deref || !Place->isLValue() ||
      !Context.hasSameType(Place->getType(), Pointer->getPointeeType()) ||
      !Parameter(Place->getSubExpr(), 0) || !Read ||
      Read->getCastKind() != CK_LValueToRValue ||
      !Context.hasSameType(Read->getType(), Pointer->getPointeeType()) ||
      !utilityAlgorithmScalarForward(
          S, SM, Move, Scan, Pointer->getPointeeType(), true, Context, true) ||
      !Advance || Advance->getOpcode() != UO_PreInc ||
      !Parameter(Advance->getSubExpr(), 0))
    return nullptr;
  return FirstCall;
}

// Authenticate the exact unary or binary transform loop before retaining a
// source-owned operation object.
static std::optional<UtilityAlgorithmPredicateCall>
utilityAlgorithmTransformObjectCall(const State &S, const SourceManager &SM,
                                    const CallExpr *Call,
                                    const ASTContext &Context) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  const bool Binary = Call && Call->getNumArgs() == 5;
  if (!S.coreV2() || !Call || !Function || Function->getName() != "transform" ||
      (Call->getNumArgs() != 4 && !Binary) ||
      Function->getNumParams() != Call->getNumArgs() || !Call->isPRValue() ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return std::nullopt;
  const auto Input = Function->getParamDecl(0)->getType();
  const auto Second =
      Binary ? Function->getParamDecl(2)->getType() : QualType{};
  const unsigned OutputIndex = Binary ? 3 : 2;
  const unsigned ObjectIndex = Binary ? 4 : 3;
  const auto Output = Function->getParamDecl(OutputIndex)->getType();
  const auto Object = Function->getParamDecl(ObjectIndex)->getType();
  const auto *Record = Object->getAsCXXRecordDecl();
  Record = Record ? Record->getDefinition() : nullptr;
  const bool SDKObject =
      Record && Binary &&
      approvedFunctionalObjectRecord(S, SM, Record, Context).has_value();
  if (!utilityAlgorithmScalarPointer(Context, Input) ||
      !Context.hasSameType(Function->getParamDecl(1)->getType(), Input) ||
      !utilityAlgorithmWritableScalarPointer(Context, Output) ||
      !Context.hasSameType(Call->getArg(0)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(1)->getType(), Input) ||
      (Binary && (!utilityAlgorithmScalarPointer(Context, Second) ||
                  !Context.hasSameType(Call->getArg(2)->getType(), Second))) ||
      !Context.hasSameType(Call->getArg(OutputIndex)->getType(), Output) ||
      !Context.hasSameType(Call->getArg(ObjectIndex)->getType(), Object) ||
      !Context.hasSameType(Function->getReturnType(), Output) ||
      !Context.hasSameType(Call->getType(), Output) || !Record ||
      Object.hasLocalQualifiers() || Record->isLambda() || Record->isUnion() ||
      !Record->isStandardLayout() || !Record->isTriviallyCopyable() ||
      !Record->hasTrivialCopyConstructor() || !Record->hasTrivialDestructor() ||
      (!S.owns(SM, Record->getLocation()) && !SDKObject))
    return std::nullopt;
  const auto *Primary = Function->getPrimaryTemplate();
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Function->getDefinition();
  const auto *PatternDefinition = Pattern ? Pattern->getDefinition() : nullptr;
  const auto *Arguments = Function->getTemplateSpecializationArgs();
  auto Origin = [&](const Decl *D) {
    return approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx",
                         "__algorithm/transform.h");
  };
  if (!Primary || !Pattern || !Definition || !PatternDefinition ||
      Function->isVariadic() || !Function->isInlined() ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedUtilityReference(S, SM, Call, Function) || !Origin(Definition) ||
      !Origin(PatternDefinition) || !Arguments ||
      Arguments->size() != (Binary ? 4u : 3u))
    return std::nullopt;
  for (const auto *D : Function->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto *D : Primary->redecls())
    if (!Origin(D) || !Origin(D->getTemplatedDecl()))
      return std::nullopt;
  for (const auto *D : Pattern->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (unsigned Index = 0; Index != Arguments->size(); ++Index) {
    const auto Expected = Index == 0                    ? Input
                          : Binary && Index == 1        ? Second
                          : Index == (Binary ? 2u : 1u) ? Output
                                                        : Object;
    if (Arguments->get(Index).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(Index).getAsType(), Expected))
      return std::nullopt;
  }

  auto Reference = [&](const Expr *Expression, const ValueDecl *Value) {
    return utilityAlgorithmReference(Expression, Value, Context);
  };
  auto Increment = [&](const Expr *Expression, unsigned Index) {
    const auto *Operation = dyn_cast_or_null<UnaryOperator>(Expression);
    return Operation && Operation->getOpcode() == UO_PreInc &&
           Reference(Operation->getSubExpr(), Definition->getParamDecl(Index));
  };
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 2)
    return std::nullopt;
  auto Part = Body->body_begin();
  const auto *Loop = dyn_cast<ForStmt>(*Part++);
  const auto *Return = dyn_cast<ReturnStmt>(*Part);
  const auto *Condition =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getCond()) : nullptr;
  const auto *Steps =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getInc()) : nullptr;
  const auto *InputSteps =
      Binary && Steps ? dyn_cast<BinaryOperator>(Steps->getLHS()) : nullptr;
  const auto *SecondStep =
      InputSteps ? dyn_cast<CStyleCastExpr>(InputSteps->getRHS()) : nullptr;
  const auto *UnaryOutputStep =
      !Binary && Steps ? dyn_cast<CStyleCastExpr>(Steps->getRHS()) : nullptr;
  const auto *Assignment =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getBody()) : nullptr;
  const auto *Destination =
      Assignment ? dyn_cast<UnaryOperator>(Assignment->getLHS()) : nullptr;
  if (!Loop || Loop->getInit() || Loop->getConditionVariable() || !Condition ||
      Condition->getOpcode() != BO_NE ||
      !Reference(Condition->getLHS(), Definition->getParamDecl(0)) ||
      !Reference(Condition->getRHS(), Definition->getParamDecl(1)) || !Steps ||
      Steps->getOpcode() != BO_Comma ||
      (Binary ? (!InputSteps || InputSteps->getOpcode() != BO_Comma ||
                 !Increment(InputSteps->getLHS(), 0) || !SecondStep ||
                 SecondStep->getCastKind() != CK_ToVoid ||
                 !Increment(SecondStep->getSubExpr(), 2) ||
                 !Increment(Steps->getRHS(), OutputIndex))
              : (!Increment(Steps->getLHS(), 0) || !UnaryOutputStep ||
                 UnaryOutputStep->getCastKind() != CK_ToVoid ||
                 !Increment(UnaryOutputStep->getSubExpr(), OutputIndex))) ||
      !Assignment || Assignment->getOpcode() != BO_Assign || !Destination ||
      Destination->getOpcode() != UO_Deref || !Destination->isLValue() ||
      !Context.hasSameType(Destination->getType(), Output->getPointeeType()) ||
      !Reference(Destination->getSubExpr(),
                 Definition->getParamDecl(OutputIndex)) ||
      !Return ||
      !Reference(Return->getRetValue(), Definition->getParamDecl(OutputIndex)))
    return std::nullopt;

  const Expr *Result = Assignment->getRHS();
  while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Result)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return std::nullopt;
    Result = Cast->getSubExpr();
  }
  const auto *Invocation = dyn_cast<CXXOperatorCallExpr>(Result);
  if (!Invocation || Invocation->getOperator() != OO_Call ||
      Invocation->getNumArgs() != (Binary ? 3u : 2u) ||
      !Invocation->isPRValue() ||
      !Reference(Invocation->getArg(0), Definition->getParamDecl(ObjectIndex)))
    return std::nullopt;
  for (unsigned Index = 0; Index != (Binary ? 2u : 1u); ++Index) {
    const Expr *ElementArgument = Invocation->getArg(Index + 1);
    while (const auto *Cast = dyn_cast<ImplicitCastExpr>(ElementArgument)) {
      if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                         Cast->getType()))
        return std::nullopt;
      ElementArgument = Cast->getSubExpr();
    }
    const auto *Element = dyn_cast<UnaryOperator>(ElementArgument);
    const auto ExpectedPointer = Index ? Second : Input;
    const auto *Parameter = Definition->getParamDecl(Index ? 2 : 0);
    if (!Element || Element->getOpcode() != UO_Deref || !Element->isLValue() ||
        !Context.hasSameType(Element->getType(),
                             ExpectedPointer->getPointeeType()) ||
        !Reference(Element->getSubExpr(), Parameter))
      return std::nullopt;
  }
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Invocation->getDirectCallee());
  const auto *MethodDefinition = Method ? Method->getDefinition() : nullptr;
  if (!Method || !MethodDefinition || Method->isStatic() ||
      Method->isVolatile() || Method->getOverloadedOperator() != OO_Call ||
      Method->getNumParams() != (Binary ? 2u : 1u) ||
      Method->getParent()->getCanonicalDecl() != Record->getCanonicalDecl() ||
      !functionalMemberReceiverValueCategory(Method, Invocation->getArg(0),
                                             false) ||
      !utilityScalar(Context, Method->getReturnType()) ||
      !utilityScalarDirectConversion(Context, Method->getReturnType(),
                                     Output->getPointeeType()))
    return std::nullopt;
  std::optional<FunctionalOperationInfo> SDKOperation;
  if (SDKObject) {
    SDKOperation =
        approvedFunctionalOperationImpl(S, SM, Invocation, Context, false);
    if (!SDKOperation || SDKOperation->RightType.isNull() ||
        !Context.hasSameUnqualifiedType(
            Input->getPointeeType(),
            Method->getParamDecl(0)->getType().getNonReferenceType()) ||
        !Context.hasSameUnqualifiedType(
            Second->getPointeeType(),
            Method->getParamDecl(1)->getType().getNonReferenceType()))
      return std::nullopt;
  } else {
    if ((Method->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
         Method->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization) ||
        Method->getPrimaryTemplate() ||
        Method->getDescribedFunctionTemplate() || !ordinaryOperator(Method) ||
        !callableMethod(Method) || !S.owns(SM, MethodDefinition->getLocation()))
      return std::nullopt;
    for (const auto *D : Method->redecls())
      if (!S.owns(SM, D->getLocation()))
        return std::nullopt;
  }
  const auto LeftType = SDKOperation ? SDKOperation->LeftType
                                     : Method->getParamDecl(0)->getType();
  const auto RightType = SDKOperation ? SDKOperation->RightType
                         : Binary     ? Method->getParamDecl(1)->getType()
                                      : QualType{};
  if (!utilityScalarDirectConversion(Context, Input->getPointeeType(),
                                     LeftType) ||
      (Binary && !utilityScalarDirectConversion(
                     Context, Second->getPointeeType(), RightType)))
    return std::nullopt;
  return UtilityAlgorithmPredicateCall{
      Binary ? UtilityOperation::AlgorithmTransformBinary
             : UtilityOperation::AlgorithmTransformUnary,
      Function,
      Invocation,
      Method,
      Object,
      ObjectIndex,
      SDKOperation};
}

// for_each_n discards the unary result but otherwise has the same by-value
// function-object boundary as the predicate algorithms. Authenticate its exact
// counted loop so source methods cannot replace an unchecked SDK callback.
static std::optional<UtilityAlgorithmPredicateCall>
utilityAlgorithmForEachNObjectCall(const State &S, const SourceManager &SM,
                                   const CallExpr *Call,
                                   const ASTContext &Context) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  if (!S.coreV2() || !Call || !Function ||
      Function->getName() != "for_each_n" || Call->getNumArgs() != 3 ||
      Function->getNumParams() != 3 || !Call->isPRValue() ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return std::nullopt;
  const auto Pointer = Function->getParamDecl(0)->getType();
  auto Count = Function->getParamDecl(1)->getType();
  const auto Object = Function->getParamDecl(2)->getType();
  const auto *Record = Object->getAsCXXRecordDecl();
  Record = Record ? Record->getDefinition() : nullptr;
  const bool SDKObject =
      Record &&
      approvedFunctionalObjectRecord(S, SM, Record, Context).has_value();
  if (!utilityAlgorithmScalarPointer(Context, Pointer) ||
      !Context.hasSameType(Call->getArg(0)->getType(), Pointer) ||
      !Context.hasSameType(Call->getArg(1)->getType(), Count) ||
      !Context.hasSameType(Call->getArg(2)->getType(), Object) ||
      !Context.hasSameType(Function->getReturnType(), Pointer) ||
      !Context.hasSameType(Call->getType(), Pointer) || !Record ||
      Object.hasLocalQualifiers() || Record->isLambda() || Record->isUnion() ||
      !Record->isStandardLayout() || !Record->isTriviallyCopyable() ||
      !Record->hasTrivialCopyConstructor() || !Record->hasTrivialDestructor() ||
      (!S.owns(SM, Record->getLocation()) && !SDKObject))
    return std::nullopt;
  if (const auto *Enumeration = Count->getAs<EnumType>()) {
    if (Enumeration->getDecl()->isScoped())
      return std::nullopt;
    Count = Enumeration->getDecl()->getPromotionType();
  } else if (Context.isPromotableIntegerType(Count)) {
    Count = Context.getPromotedIntegerType(Count);
  } else if (!Count->isIntegerType()) {
    return std::nullopt;
  }
  if (Count.isNull() || !Count->isIntegerType() ||
      Context.getTypeSize(Count) > 64)
    return std::nullopt;

  const auto *Primary = Function->getPrimaryTemplate();
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Function->getDefinition();
  const auto *PatternDefinition = Pattern ? Pattern->getDefinition() : nullptr;
  const auto *Arguments = Function->getTemplateSpecializationArgs();
  auto Origin = [&](const Decl *D) {
    return approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx",
                         "__algorithm/for_each_n.h");
  };
  if (!Primary || !Pattern || !Definition || !PatternDefinition ||
      Function->isVariadic() || !Function->isInlined() ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedUtilityReference(S, SM, Call, Function) || !Origin(Definition) ||
      !Origin(PatternDefinition) || !Arguments || Arguments->size() != 3)
    return std::nullopt;
  for (const auto *D : Function->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto *D : Primary->redecls())
    if (!Origin(D) || !Origin(D->getTemplatedDecl()))
      return std::nullopt;
  for (const auto *D : Pattern->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (unsigned I = 0; I != 3; ++I)
    if (Arguments->get(I).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(I).getAsType(),
                             Function->getParamDecl(I)->getType()))
      return std::nullopt;

  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 4)
    return std::nullopt;
  auto Part = Body->body_begin();
  const auto *AliasStatement = dyn_cast<DeclStmt>(*Part++);
  const auto *Alias =
      AliasStatement && AliasStatement->isSingleDecl()
          ? dyn_cast<TypedefDecl>(AliasStatement->getSingleDecl())
          : nullptr;
  const auto *CountStatement = dyn_cast<DeclStmt>(*Part++);
  const auto *Remaining =
      CountStatement && CountStatement->isSingleDecl()
          ? dyn_cast<VarDecl>(CountStatement->getSingleDecl())
          : nullptr;
  const auto *Loop = dyn_cast<WhileStmt>(*Part++);
  const auto *Return = dyn_cast<ReturnStmt>(*Part);
  auto Reference = [&](const Expr *Expression, const ValueDecl *Value) {
    return utilityAlgorithmReference(Expression, Value, Context);
  };
  const Expr *CountSource = Remaining ? Remaining->getInit() : nullptr;
  while (const auto *Cast = dyn_cast_or_null<ImplicitCastExpr>(CountSource)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return std::nullopt;
    CountSource = Cast->getSubExpr();
  }
  if (!Alias || Alias->isImplicit() || Alias->getDeclContext() != Definition ||
      Alias->getName() != "_IntegralSize" ||
      !Context.hasSameType(Alias->getUnderlyingType(), Count) || !Remaining ||
      Remaining->isImplicit() || Remaining->getDeclContext() != Definition ||
      !Context.hasSameType(Remaining->getType(), Count) ||
      !Reference(CountSource, Definition->getParamDecl(1)) || !Loop ||
      Loop->getConditionVariable() || !Return ||
      !Reference(Return->getRetValue(), Definition->getParamDecl(0)))
    return std::nullopt;
  const auto *Condition = dyn_cast<BinaryOperator>(Loop->getCond());
  const auto *LoopBody = dyn_cast<CompoundStmt>(Loop->getBody());
  const auto *Zero =
      Condition
          ? dyn_cast<IntegerLiteral>(Condition->getRHS()->IgnoreParenImpCasts())
          : nullptr;
  if (!Condition || Condition->getOpcode() != BO_GT ||
      !Reference(Condition->getLHS(), Remaining) || !Zero ||
      !Zero->getValue().isZero() || !LoopBody || LoopBody->size() != 3)
    return std::nullopt;
  auto LoopPart = LoopBody->body_begin();
  const auto *Invocation = dyn_cast<CXXOperatorCallExpr>(*LoopPart++);
  const auto *Advance = dyn_cast<UnaryOperator>(*LoopPart++);
  const auto *Reduce = dyn_cast<UnaryOperator>(*LoopPart);
  if (!Invocation || Invocation->getOperator() != OO_Call ||
      Invocation->getNumArgs() != 2 || !Invocation->isPRValue() ||
      !Reference(Invocation->getArg(0), Definition->getParamDecl(2)) ||
      !Advance || Advance->getOpcode() != UO_PreInc ||
      !Reference(Advance->getSubExpr(), Definition->getParamDecl(0)) ||
      !Reduce || Reduce->getOpcode() != UO_PreDec ||
      !Reference(Reduce->getSubExpr(), Remaining))
    return std::nullopt;
  const Expr *ElementArgument = Invocation->getArg(1);
  while (const auto *Cast = dyn_cast<ImplicitCastExpr>(ElementArgument)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return std::nullopt;
    ElementArgument = Cast->getSubExpr();
  }
  const auto *Element = dyn_cast<UnaryOperator>(ElementArgument);
  if (!Element || Element->getOpcode() != UO_Deref || !Element->isLValue() ||
      !Context.hasSameType(Element->getType(), Pointer->getPointeeType()) ||
      !Reference(Element->getSubExpr(), Definition->getParamDecl(0)))
    return std::nullopt;

  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Invocation->getDirectCallee());
  const auto *MethodDefinition = Method ? Method->getDefinition() : nullptr;
  if (!Method || !MethodDefinition || Method->isStatic() ||
      Method->isVolatile() || Method->getOverloadedOperator() != OO_Call ||
      Method->getNumParams() != 1 ||
      Method->getParent()->getCanonicalDecl() != Record->getCanonicalDecl() ||
      !functionalMemberReceiverValueCategory(Method, Invocation->getArg(0),
                                             false) ||
      (!Method->getReturnType()->isVoidType() &&
       !utilityScalar(Context, Method->getReturnType())))
    return std::nullopt;
  std::optional<FunctionalOperationInfo> SDKOperation;
  if (SDKObject) {
    SDKOperation =
        approvedFunctionalOperationImpl(S, SM, Invocation, Context, false);
    if (!SDKOperation ||
        SDKOperation->Operation != FunctionalOperation::LogicalNot ||
        !Context.hasSameUnqualifiedType(
            Pointer->getPointeeType(),
            Method->getParamDecl(0)->getType().getNonReferenceType()))
      return std::nullopt;
  } else {
    if ((Method->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
         Method->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization) ||
        Method->getPrimaryTemplate() ||
        Method->getDescribedFunctionTemplate() || !ordinaryOperator(Method) ||
        !callableMethod(Method) || !S.owns(SM, MethodDefinition->getLocation()))
      return std::nullopt;
    for (const auto *D : Method->redecls())
      if (!S.owns(SM, D->getLocation()))
        return std::nullopt;
  }
  const auto ArgumentType = SDKOperation ? SDKOperation->LeftType
                                         : Method->getParamDecl(0)->getType();
  if (!utilityScalarDirectConversion(Context, Pointer->getPointeeType(),
                                     ArgumentType))
    return std::nullopt;
  return UtilityAlgorithmPredicateCall{UtilityOperation::AlgorithmForEachN,
                                       Function,
                                       Invocation,
                                       Method,
                                       Object,
                                       2,
                                       SDKOperation};
}

// for_each returns the modified by-value object, so authenticate both its
// loop invocation and return before retaining a source-owned callable.
static std::optional<UtilityAlgorithmPredicateCall>
utilityAlgorithmForEachObjectCall(const State &S, const SourceManager &SM,
                                  const CallExpr *Call,
                                  const ASTContext &Context) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  if (!S.coreV2() || !Call || !Function || Function->getName() != "for_each" ||
      Call->getNumArgs() != 3 || Function->getNumParams() != 3 ||
      !Call->isPRValue() || Call->isTypeDependent() ||
      Call->isValueDependent() || Call->isInstantiationDependent())
    return std::nullopt;
  const auto Pointer = Function->getParamDecl(0)->getType();
  const auto Object = Function->getParamDecl(2)->getType();
  const auto *Record = Object->getAsCXXRecordDecl();
  Record = Record ? Record->getDefinition() : nullptr;
  const bool SDKObject =
      Record &&
      approvedFunctionalObjectRecord(S, SM, Record, Context).has_value();
  if (!utilityAlgorithmScalarPointer(Context, Pointer) ||
      !Context.hasSameType(Function->getParamDecl(1)->getType(), Pointer) ||
      !Context.hasSameType(Call->getArg(0)->getType(), Pointer) ||
      !Context.hasSameType(Call->getArg(1)->getType(), Pointer) ||
      !Context.hasSameType(Call->getArg(2)->getType(), Object) ||
      !Context.hasSameType(Function->getReturnType(), Object) ||
      !Context.hasSameType(Call->getType(), Object) || !Record ||
      Object.hasLocalQualifiers() || Record->isLambda() || Record->isUnion() ||
      !Record->isStandardLayout() || !Record->isTriviallyCopyable() ||
      !Record->hasTrivialCopyConstructor() || !Record->hasTrivialDestructor() ||
      (!S.owns(SM, Record->getLocation()) && !SDKObject))
    return std::nullopt;
  const auto *Primary = Function->getPrimaryTemplate();
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Function->getDefinition();
  const auto *PatternDefinition = Pattern ? Pattern->getDefinition() : nullptr;
  const auto *Arguments = Function->getTemplateSpecializationArgs();
  auto Origin = [&](const Decl *D) {
    return approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx",
                         "__algorithm/for_each.h");
  };
  if (!Primary || !Pattern || !Definition || !PatternDefinition ||
      Function->isVariadic() ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedUtilityReference(S, SM, Call, Function) || !Origin(Definition) ||
      !Origin(PatternDefinition) || !Arguments || Arguments->size() != 2)
    return std::nullopt;
  for (const auto *D : Function->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto *D : Primary->redecls())
    if (!Origin(D) || !Origin(D->getTemplatedDecl()))
      return std::nullopt;
  for (const auto *D : Pattern->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto [Index, Expected] :
       {std::pair{0u, Pointer}, std::pair{1u, Object}})
    if (Arguments->get(Index).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(Index).getAsType(), Expected))
      return std::nullopt;

  auto Reference = [&](const Expr *Expression, const ValueDecl *Value) {
    return utilityAlgorithmReference(Expression, Value, Context);
  };
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 2)
    return std::nullopt;
  auto Part = Body->body_begin();
  const auto *Loop = dyn_cast<ForStmt>(*Part++);
  const auto *Return = dyn_cast<ReturnStmt>(*Part);
  const auto *Condition =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getCond()) : nullptr;
  const auto *Advance =
      Loop ? dyn_cast_or_null<UnaryOperator>(Loop->getInc()) : nullptr;
  const auto *Invocation =
      Loop ? dyn_cast_or_null<CXXOperatorCallExpr>(Loop->getBody()) : nullptr;
  const auto *Result =
      Return ? dyn_cast_or_null<CXXConstructExpr>(Return->getRetValue())
             : nullptr;
  if (!Loop || Loop->getInit() || Loop->getConditionVariable() || !Condition ||
      Condition->getOpcode() != BO_NE ||
      !Reference(Condition->getLHS(), Definition->getParamDecl(0)) ||
      !Reference(Condition->getRHS(), Definition->getParamDecl(1)) ||
      !Advance || Advance->getOpcode() != UO_PreInc ||
      !Reference(Advance->getSubExpr(), Definition->getParamDecl(0)) ||
      !Invocation || Invocation->getOperator() != OO_Call ||
      Invocation->getNumArgs() != 2 || !Invocation->isPRValue() ||
      !Reference(Invocation->getArg(0), Definition->getParamDecl(2)) ||
      !Result || Result->getNumArgs() != 1 ||
      !Result->getConstructor()->isMoveConstructor() ||
      !Result->getConstructor()->isTrivial() ||
      !Reference(Result->getArg(0), Definition->getParamDecl(2)))
    return std::nullopt;
  const Expr *ElementArgument = Invocation->getArg(1);
  while (const auto *Cast = dyn_cast<ImplicitCastExpr>(ElementArgument)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return std::nullopt;
    ElementArgument = Cast->getSubExpr();
  }
  const auto *Element = dyn_cast<UnaryOperator>(ElementArgument);
  if (!Element || Element->getOpcode() != UO_Deref || !Element->isLValue() ||
      !Context.hasSameType(Element->getType(), Pointer->getPointeeType()) ||
      !Reference(Element->getSubExpr(), Definition->getParamDecl(0)))
    return std::nullopt;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Invocation->getDirectCallee());
  const auto *MethodDefinition = Method ? Method->getDefinition() : nullptr;
  if (!Method || !MethodDefinition || Method->isStatic() ||
      Method->isVolatile() || Method->getOverloadedOperator() != OO_Call ||
      Method->getNumParams() != 1 ||
      Method->getParent()->getCanonicalDecl() != Record->getCanonicalDecl() ||
      !functionalMemberReceiverValueCategory(Method, Invocation->getArg(0),
                                             false) ||
      (!Method->getReturnType()->isVoidType() &&
       !utilityScalar(Context, Method->getReturnType())))
    return std::nullopt;
  std::optional<FunctionalOperationInfo> SDKOperation;
  if (SDKObject) {
    SDKOperation =
        approvedFunctionalOperationImpl(S, SM, Invocation, Context, false);
    if (!SDKOperation ||
        SDKOperation->Operation != FunctionalOperation::LogicalNot ||
        !Context.hasSameUnqualifiedType(
            Pointer->getPointeeType(),
            Method->getParamDecl(0)->getType().getNonReferenceType()))
      return std::nullopt;
  } else {
    if ((Method->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
         Method->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization) ||
        Method->getPrimaryTemplate() ||
        Method->getDescribedFunctionTemplate() || !ordinaryOperator(Method) ||
        !callableMethod(Method) || !S.owns(SM, MethodDefinition->getLocation()))
      return std::nullopt;
    for (const auto *D : Method->redecls())
      if (!S.owns(SM, D->getLocation()))
        return std::nullopt;
  }
  const auto ArgumentType = SDKOperation ? SDKOperation->LeftType
                                         : Method->getParamDecl(0)->getType();
  if (!utilityScalarDirectConversion(Context, Pointer->getPointeeType(),
                                     ArgumentType))
    return std::nullopt;
  return UtilityAlgorithmPredicateCall{UtilityOperation::AlgorithmForEach,
                                       Function,
                                       Invocation,
                                       Method,
                                       Object,
                                       2,
                                       SDKOperation};
}

// Retain a source generator only after proving the selected libc++ loop and
// the exact zero-argument call made from its body.
static std::optional<UtilityAlgorithmPredicateCall>
utilityAlgorithmGenerateObjectCall(const State &S, const SourceManager &SM,
                                   const CallExpr *Call,
                                   const ASTContext &Context) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  const bool Counted = Function && Function->getName() == "generate_n";
  if (!S.coreV2() || !Call || !Function ||
      (Function->getName() != "generate" && !Counted) ||
      Call->getNumArgs() != 3 || Function->getNumParams() != 3 ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return std::nullopt;
  const auto Pointer = Function->getParamDecl(0)->getType();
  const auto Object = Function->getParamDecl(2)->getType();
  auto Count = Function->getParamDecl(1)->getType();
  const auto *Record = Object->getAsCXXRecordDecl();
  Record = Record ? Record->getDefinition() : nullptr;
  if (!utilityAlgorithmWritableScalarPointer(Context, Pointer) ||
      !Context.hasSameType(Call->getArg(0)->getType(), Pointer) ||
      !Context.hasSameType(Call->getArg(1)->getType(), Count) ||
      !Context.hasSameType(Call->getArg(2)->getType(), Object) ||
      (Counted ? (!Call->isPRValue() ||
                  !Context.hasSameType(Function->getReturnType(), Pointer))
               : (!Function->getReturnType()->isVoidType() ||
                  !Context.hasSameType(Count, Pointer))) ||
      !Context.hasSameType(Call->getType(), Function->getReturnType()) ||
      !Record || Object.hasLocalQualifiers() || Record->isLambda() ||
      Record->isUnion() || !Record->isStandardLayout() ||
      !Record->isTriviallyCopyable() || !Record->hasTrivialCopyConstructor() ||
      !Record->hasTrivialDestructor() || !S.owns(SM, Record->getLocation()))
    return std::nullopt;
  if (Counted) {
    if (const auto *Enumeration = Count->getAs<EnumType>()) {
      if (Enumeration->getDecl()->isScoped())
        return std::nullopt;
      Count = Enumeration->getDecl()->getPromotionType();
    } else if (Context.isPromotableIntegerType(Count)) {
      Count = Context.getPromotedIntegerType(Count);
    } else if (!Count->isIntegerType()) {
      return std::nullopt;
    }
    if (Count.isNull() || !Count->isIntegerType() ||
        Context.getTypeSize(Count) > 64)
      return std::nullopt;
  }
  const auto *Primary = Function->getPrimaryTemplate();
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Function->getDefinition();
  const auto *PatternDefinition = Pattern ? Pattern->getDefinition() : nullptr;
  const auto *Arguments = Function->getTemplateSpecializationArgs();
  auto Origin = [&](const Decl *D) {
    return approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx",
                         Counted ? "__algorithm/generate_n.h"
                                 : "__algorithm/generate.h");
  };
  if (!Primary || !Pattern || !Definition || !PatternDefinition ||
      Function->isVariadic() || !Function->isInlined() ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedUtilityReference(S, SM, Call, Function) || !Origin(Definition) ||
      !Origin(PatternDefinition) || !Arguments ||
      Arguments->size() != (Counted ? 3u : 2u))
    return std::nullopt;
  for (const auto *D : Function->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto *D : Primary->redecls())
    if (!Origin(D) || !Origin(D->getTemplatedDecl()))
      return std::nullopt;
  for (const auto *D : Pattern->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (unsigned Index = 0; Index != Arguments->size(); ++Index) {
    const auto Expected = Index == 0 ? Pointer
                          : Index == (Counted ? 2u : 1u)
                              ? Object
                              : Function->getParamDecl(1)->getType();
    if (Arguments->get(Index).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(Index).getAsType(), Expected))
      return std::nullopt;
  }

  auto Reference = [&](const Expr *Expression, const ValueDecl *Value) {
    return utilityAlgorithmReference(Expression, Value, Context);
  };
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != (Counted ? 4u : 1u))
    return std::nullopt;
  auto Part = Body->body_begin();
  const TypedefDecl *Alias = nullptr;
  const VarDecl *Remaining = nullptr;
  if (Counted) {
    const auto *AliasStatement = dyn_cast<DeclStmt>(*Part++);
    Alias = AliasStatement && AliasStatement->isSingleDecl()
                ? dyn_cast<TypedefDecl>(AliasStatement->getSingleDecl())
                : nullptr;
    const auto *CountStatement = dyn_cast<DeclStmt>(*Part++);
    Remaining = CountStatement && CountStatement->isSingleDecl()
                    ? dyn_cast<VarDecl>(CountStatement->getSingleDecl())
                    : nullptr;
    const Expr *CountSource = Remaining ? Remaining->getInit() : nullptr;
    while (const auto *Cast = dyn_cast_or_null<ImplicitCastExpr>(CountSource)) {
      if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                         Cast->getType()))
        return std::nullopt;
      CountSource = Cast->getSubExpr();
    }
    if (!Alias || Alias->isImplicit() ||
        Alias->getDeclContext() != Definition ||
        Alias->getName() != "_IntegralSize" ||
        !Context.hasSameType(Alias->getUnderlyingType(), Count) || !Remaining ||
        Remaining->isImplicit() || Remaining->getDeclContext() != Definition ||
        !Context.hasSameType(Remaining->getType(), Count) ||
        !Reference(CountSource, Definition->getParamDecl(1)))
      return std::nullopt;
  }
  const auto *Loop = dyn_cast<ForStmt>(*Part++);
  const auto *Return = Counted ? dyn_cast<ReturnStmt>(*Part) : nullptr;
  const auto *Condition =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getCond()) : nullptr;
  const auto *Steps = Counted && Loop
                          ? dyn_cast_or_null<BinaryOperator>(Loop->getInc())
                          : nullptr;
  const auto *Advance =
      Loop ? dyn_cast_or_null<UnaryOperator>(
                 Counted ? Steps ? Steps->getLHS() : nullptr : Loop->getInc())
           : nullptr;
  const auto *Assignment =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getBody()) : nullptr;
  const auto *Destination =
      Assignment ? dyn_cast<UnaryOperator>(Assignment->getLHS()) : nullptr;
  if (!Loop || Loop->getInit() || Loop->getConditionVariable() || !Condition ||
      !Advance || Advance->getOpcode() != UO_PreInc ||
      !Reference(Advance->getSubExpr(), Definition->getParamDecl(0)) ||
      !Assignment || Assignment->getOpcode() != BO_Assign || !Destination ||
      Destination->getOpcode() != UO_Deref || !Destination->isLValue() ||
      !Context.hasSameType(Destination->getType(), Pointer->getPointeeType()) ||
      !Reference(Destination->getSubExpr(), Definition->getParamDecl(0)))
    return std::nullopt;
  if (Counted) {
    const auto *Zero =
        dyn_cast<IntegerLiteral>(Condition->getRHS()->IgnoreParenImpCasts());
    const auto *Reduction =
        Steps ? dyn_cast<CStyleCastExpr>(Steps->getRHS()) : nullptr;
    const auto *Decrement =
        Reduction ? dyn_cast<UnaryOperator>(Reduction->getSubExpr()) : nullptr;
    if (Condition->getOpcode() != BO_GT ||
        !Reference(Condition->getLHS(), Remaining) || !Zero ||
        !Zero->getValue().isZero() || !Steps ||
        Steps->getOpcode() != BO_Comma || !Reduction ||
        Reduction->getCastKind() != CK_ToVoid || !Decrement ||
        Decrement->getOpcode() != UO_PreDec ||
        !Reference(Decrement->getSubExpr(), Remaining) || !Return ||
        !Reference(Return->getRetValue(), Definition->getParamDecl(0)))
      return std::nullopt;
  } else if (Condition->getOpcode() != BO_NE ||
             !Reference(Condition->getLHS(), Definition->getParamDecl(0)) ||
             !Reference(Condition->getRHS(), Definition->getParamDecl(1))) {
    return std::nullopt;
  }
  const Expr *Result = Assignment->getRHS();
  while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Result)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return std::nullopt;
    Result = Cast->getSubExpr();
  }
  const auto *Invocation = dyn_cast<CXXOperatorCallExpr>(Result);
  if (!Invocation || Invocation->getOperator() != OO_Call ||
      Invocation->getNumArgs() != 1 || !Invocation->isPRValue() ||
      !Reference(Invocation->getArg(0), Definition->getParamDecl(2)))
    return std::nullopt;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Invocation->getDirectCallee());
  const auto *MethodDefinition = Method ? Method->getDefinition() : nullptr;
  if (!Method || !MethodDefinition || Method->isStatic() ||
      Method->isVolatile() || Method->getOverloadedOperator() != OO_Call ||
      Method->getNumParams() != 0 ||
      Method->getParent()->getCanonicalDecl() != Record->getCanonicalDecl() ||
      !functionalMemberReceiverValueCategory(Method, Invocation->getArg(0),
                                             false) ||
      !utilityScalarDirectConversion(Context, Method->getReturnType(),
                                     Pointer->getPointeeType()) ||
      (Method->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
       Method->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization) ||
      Method->getPrimaryTemplate() || Method->getDescribedFunctionTemplate() ||
      !ordinaryOperator(Method) || !callableMethod(Method) ||
      !S.owns(SM, MethodDefinition->getLocation()))
    return std::nullopt;
  for (const auto *D : Method->redecls())
    if (!S.owns(SM, D->getLocation()))
      return std::nullopt;
  return UtilityAlgorithmPredicateCall{
      Counted ? UtilityOperation::AlgorithmGenerateN
              : UtilityOperation::AlgorithmGenerate,
      Function,
      Invocation,
      Method,
      Object,
      2,
      std::nullopt};
}

// The C++17 accumulate and reduce overloads carry their source operation by
// value. Prove the exact pinned loop before retaining the selected method.
static std::optional<UtilityAlgorithmPredicateCall>
utilityNumericReductionObjectCall(const State &S, const SourceManager &SM,
                                  const CallExpr *Call,
                                  const ASTContext &Context) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  const bool Reduce = Function && Function->getName() == "reduce";
  if (!S.coreV2() || !Call || !Function ||
      (Function->getName() != "accumulate" && !Reduce) ||
      Call->getNumArgs() != 4 || Function->getNumParams() != 4 ||
      !Call->isPRValue() || Call->isTypeDependent() ||
      Call->isValueDependent() || Call->isInstantiationDependent())
    return std::nullopt;
  const auto Pointer = Function->getParamDecl(0)->getType();
  const auto Initial = Function->getParamDecl(2)->getType();
  const auto Object = Function->getParamDecl(3)->getType();
  const auto ElementType = Pointer->isPointerType()
                               ? Pointer->getPointeeType().getUnqualifiedType()
                               : QualType{};
  const bool Arithmetic =
      !ElementType.isNull() &&
      ((!ElementType->isEnumeralType() && !ElementType->isBooleanType() &&
        ElementType->isIntegerType() &&
        Context.getTypeSize(ElementType) <= 64) ||
       ElementType->isSpecificBuiltinType(BuiltinType::Float) ||
       ElementType->isSpecificBuiltinType(BuiltinType::Double));
  const bool InitialArithmetic =
      !Initial->isEnumeralType() && !Initial->isBooleanType() &&
      ((Initial->isIntegerType() && Context.getTypeSize(Initial) <= 64) ||
       Initial->isSpecificBuiltinType(BuiltinType::Float) ||
       Initial->isSpecificBuiltinType(BuiltinType::Double));
  const auto *Record = Object->getAsCXXRecordDecl();
  Record = Record ? Record->getDefinition() : nullptr;
  const bool SDKObject =
      Record &&
      approvedFunctionalObjectRecord(S, SM, Record, Context).has_value();
  if (!Arithmetic || !InitialArithmetic ||
      !utilityScalarComparisonType(Context, Initial, ElementType, false) ||
      !utilityAlgorithmScalarPointer(Context, Pointer) ||
      !Context.hasSameType(Function->getParamDecl(1)->getType(), Pointer) ||
      !Context.hasSameType(Call->getArg(0)->getType(), Pointer) ||
      !Context.hasSameType(Call->getArg(1)->getType(), Pointer) ||
      !Context.hasSameType(Call->getArg(2)->getType(), Initial) ||
      !Context.hasSameType(Call->getArg(3)->getType(), Object) ||
      !Context.hasSameType(Function->getReturnType(), Initial) ||
      !Context.hasSameType(Call->getType(), Initial) || !Record ||
      Object.hasLocalQualifiers() || Record->isLambda() || Record->isUnion() ||
      !Record->isStandardLayout() || !Record->isTriviallyCopyable() ||
      !Record->hasTrivialCopyConstructor() || !Record->hasTrivialDestructor() ||
      (!S.owns(SM, Record->getLocation()) && !SDKObject))
    return std::nullopt;
  const auto *Primary = Function->getPrimaryTemplate();
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Function->getDefinition();
  const auto *PatternDefinition = Pattern ? Pattern->getDefinition() : nullptr;
  const auto *Arguments = Function->getTemplateSpecializationArgs();
  auto Origin = [&](const Decl *D) {
    return approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx",
                         Reduce ? "__numeric/reduce.h"
                                : "__numeric/accumulate.h");
  };
  if (!Primary || !Pattern || !Definition || !PatternDefinition ||
      Function->isVariadic() ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedUtilityReference(S, SM, Call, Function) || !Origin(Definition) ||
      !Origin(PatternDefinition) || !Arguments || Arguments->size() != 3)
    return std::nullopt;
  for (const auto *D : Function->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto *D : Primary->redecls())
    if (!Origin(D) || !Origin(D->getTemplatedDecl()))
      return std::nullopt;
  for (const auto *D : Pattern->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto [Index, Expected] :
       {std::pair{0u, Pointer}, std::pair{1u, Initial}, std::pair{2u, Object}})
    if (Arguments->get(Index).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(Index).getAsType(), Expected))
      return std::nullopt;

  auto Reference = [&](const Expr *Expression, const ValueDecl *Value) {
    return utilityAlgorithmReference(Expression, Value, Context);
  };
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 2)
    return std::nullopt;
  auto Part = Body->body_begin();
  const auto *Loop = dyn_cast<ForStmt>(*Part++);
  const auto *Return = dyn_cast<ReturnStmt>(*Part);
  const auto *Condition =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getCond()) : nullptr;
  const auto *Advance =
      Loop ? dyn_cast_or_null<UnaryOperator>(Loop->getInc()) : nullptr;
  const Stmt *LoopBody = Loop ? Loop->getBody() : nullptr;
  if (const auto *Cleanup = dyn_cast_or_null<ExprWithCleanups>(LoopBody)) {
    if (Cleanup->getNumObjects())
      return std::nullopt;
    LoopBody = Cleanup->getSubExpr();
  }
  const auto *Assignment = dyn_cast_or_null<BinaryOperator>(LoopBody);
  if (!Loop || Loop->getInit() || Loop->getConditionVariable() || !Condition ||
      Condition->getOpcode() != BO_NE ||
      !Reference(Condition->getLHS(), Definition->getParamDecl(0)) ||
      !Reference(Condition->getRHS(), Definition->getParamDecl(1)) ||
      !Advance || Advance->getOpcode() != UO_PreInc ||
      !Reference(Advance->getSubExpr(), Definition->getParamDecl(0)) ||
      !Assignment || Assignment->getOpcode() != BO_Assign ||
      !Reference(Assignment->getLHS(), Definition->getParamDecl(2)) ||
      !Return || !Reference(Return->getRetValue(), Definition->getParamDecl(2)))
    return std::nullopt;
  const Expr *Result = Assignment->getRHS();
  while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Result)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return std::nullopt;
    Result = Cast->getSubExpr();
  }
  const auto *Invocation = dyn_cast<CXXOperatorCallExpr>(Result);
  if (!Invocation || Invocation->getOperator() != OO_Call ||
      Invocation->getNumArgs() != 3 || !Invocation->isPRValue() ||
      !Reference(Invocation->getArg(0), Definition->getParamDecl(3)))
    return std::nullopt;
  if (Reduce) {
    const auto *Move = dyn_cast<CallExpr>(
        functionalInvokeStrippedExpression(Invocation->getArg(1)));
    const auto *MoveFunction = Move ? Move->getDirectCallee() : nullptr;
    const auto *MoveArguments =
        MoveFunction ? MoveFunction->getTemplateSpecializationArgs() : nullptr;
    const auto ReferenceType = Context.getLValueReferenceType(Initial);
    if (!Move || Move->getNumArgs() != 1 || !Move->isXValue() ||
        !utilitySwapSDKFunction(S, SM, MoveFunction, "move",
                                "__utility/move.h") ||
        MoveFunction->getNumParams() != 1 || !MoveArguments ||
        MoveArguments->size() != 1 ||
        MoveArguments->get(0).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(MoveArguments->get(0).getAsType(),
                             ReferenceType) ||
        !Context.hasSameType(MoveFunction->getParamDecl(0)->getType(),
                             ReferenceType) ||
        !Context.hasSameType(MoveFunction->getReturnType(),
                             Context.getRValueReferenceType(Initial)) ||
        !Context.hasSameType(Move->getType(), Initial) ||
        !Reference(Move->getArg(0), Definition->getParamDecl(2)))
      return std::nullopt;
  } else if (!Reference(Invocation->getArg(1), Definition->getParamDecl(2))) {
    return std::nullopt;
  }
  const Expr *ElementArgument = Invocation->getArg(2);
  while (ElementArgument) {
    if (const auto *Temporary =
            dyn_cast<MaterializeTemporaryExpr>(ElementArgument)) {
      if (!Temporary->isLValue() || Temporary->getExtendingDecl() ||
          !utilityScalar(Context, Temporary->getType()))
        return std::nullopt;
      ElementArgument = Temporary->getSubExpr();
      continue;
    }
    const auto *Cast = dyn_cast<ImplicitCastExpr>(ElementArgument);
    if (!Cast)
      break;
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return std::nullopt;
    ElementArgument = Cast->getSubExpr();
  }
  const auto *Element = dyn_cast<UnaryOperator>(ElementArgument);
  if (!Element || Element->getOpcode() != UO_Deref || !Element->isLValue() ||
      !Context.hasSameType(Element->getType(), Pointer->getPointeeType()) ||
      !Reference(Element->getSubExpr(), Definition->getParamDecl(0)))
    return std::nullopt;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Invocation->getDirectCallee());
  const auto *MethodDefinition = Method ? Method->getDefinition() : nullptr;
  if (!Method || !MethodDefinition || Method->isStatic() ||
      Method->isVolatile() || Method->getOverloadedOperator() != OO_Call ||
      Method->getNumParams() != 2 ||
      Method->getParent()->getCanonicalDecl() != Record->getCanonicalDecl() ||
      !functionalMemberReceiverValueCategory(Method, Invocation->getArg(0),
                                             false) ||
      !utilityScalar(Context, Method->getReturnType()) ||
      !utilityScalarDirectConversion(Context, Method->getReturnType(), Initial))
    return std::nullopt;
  std::optional<FunctionalOperationInfo> SDKOperation;
  if (SDKObject) {
    SDKOperation =
        approvedFunctionalOperationImpl(S, SM, Invocation, Context, false);
    if (!SDKOperation || SDKOperation->RightType.isNull() ||
        !Context.hasSameUnqualifiedType(
            Initial,
            Method->getParamDecl(0)->getType().getNonReferenceType()) ||
        !utilityScalarDirectConversion(
            Context, Pointer->getPointeeType(),
            Method->getParamDecl(1)->getType().getNonReferenceType()))
      return std::nullopt;
  } else {
    if ((Method->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
         Method->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization) ||
        Method->getPrimaryTemplate() ||
        Method->getDescribedFunctionTemplate() || !ordinaryOperator(Method) ||
        !callableMethod(Method) || !S.owns(SM, MethodDefinition->getLocation()))
      return std::nullopt;
    for (const auto *D : Method->redecls())
      if (!S.owns(SM, D->getLocation()))
        return std::nullopt;
  }
  const auto LeftType = SDKOperation ? SDKOperation->LeftType
                                     : Method->getParamDecl(0)->getType();
  const auto RightType = SDKOperation ? SDKOperation->RightType
                                      : Method->getParamDecl(1)->getType();
  if (!utilityScalarDirectConversion(Context, Initial, LeftType) ||
      !utilityScalarDirectConversion(Context, Pointer->getPointeeType(),
                                     RightType))
    return std::nullopt;
  return UtilityAlgorithmPredicateCall{
      Reduce ? UtilityOperation::NumericReduce
             : UtilityOperation::NumericAccumulate,
      Function,
      Invocation,
      Method,
      Object,
      3,
      SDKOperation};
}

// The pinned C++17 prefix algorithms retain one binary operation across the
// range. Prove the exact scalar loop and selected source or SDK method.
static std::optional<UtilityAlgorithmPredicateCall>
utilityNumericPrefixObjectCall(const State &S, const SourceManager &SM,
                               const CallExpr *Call,
                               const ASTContext &Context) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  const bool Adjacent =
      Function && Function->getName() == "adjacent_difference";
  if (!S.coreV2() || !Call || !Function ||
      (Function->getName() != "partial_sum" && !Adjacent) ||
      Call->getNumArgs() != 4 || Function->getNumParams() != 4 ||
      !Call->isPRValue() || Call->isTypeDependent() ||
      Call->isValueDependent() || Call->isInstantiationDependent())
    return std::nullopt;
  const auto Input = Function->getParamDecl(0)->getType();
  const auto Output = Function->getParamDecl(2)->getType();
  const auto Object = Function->getParamDecl(3)->getType();
  const auto Element = Input->isPointerType()
                           ? Input->getPointeeType().getUnqualifiedType()
                           : QualType{};
  const bool Arithmetic =
      !Element.isNull() &&
      ((!Element->isEnumeralType() && !Element->isBooleanType() &&
        Element->isIntegerType() && Context.getTypeSize(Element) <= 64) ||
       Element->isSpecificBuiltinType(BuiltinType::Float) ||
       Element->isSpecificBuiltinType(BuiltinType::Double));
  const auto *Record = Object->getAsCXXRecordDecl();
  Record = Record ? Record->getDefinition() : nullptr;
  const bool SDKObject =
      Record &&
      approvedFunctionalObjectRecord(S, SM, Record, Context).has_value();
  if (!Arithmetic || !utilityAlgorithmScalarPointer(Context, Input) ||
      !utilityAlgorithmWritableScalarPointer(Context, Output) ||
      !utilityScalarDirectConversion(Context, Element,
                                     Output->getPointeeType()) ||
      !Context.hasSameType(Function->getParamDecl(1)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(0)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(1)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(2)->getType(), Output) ||
      !Context.hasSameType(Call->getArg(3)->getType(), Object) ||
      !Context.hasSameType(Function->getReturnType(), Output) ||
      !Context.hasSameType(Call->getType(), Output) || !Record ||
      Object.hasLocalQualifiers() || Record->isLambda() || Record->isUnion() ||
      !Record->isStandardLayout() || !Record->isTriviallyCopyable() ||
      !Record->hasTrivialCopyConstructor() || !Record->hasTrivialDestructor() ||
      (!S.owns(SM, Record->getLocation()) && !SDKObject))
    return std::nullopt;
  const auto *Primary = Function->getPrimaryTemplate();
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Function->getDefinition();
  const auto *PatternDefinition = Pattern ? Pattern->getDefinition() : nullptr;
  const auto *Arguments = Function->getTemplateSpecializationArgs();
  auto Origin = [&](const Decl *D) {
    return approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx",
                         Adjacent ? "__numeric/adjacent_difference.h"
                                  : "__numeric/partial_sum.h");
  };
  if (!Primary || !Pattern || !Definition || !PatternDefinition ||
      Function->isVariadic() ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedUtilityReference(S, SM, Call, Function) || !Origin(Definition) ||
      !Origin(PatternDefinition) || !Arguments || Arguments->size() != 3)
    return std::nullopt;
  for (const auto *D : Function->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto *D : Primary->redecls())
    if (!Origin(D) || !Origin(D->getTemplatedDecl()))
      return std::nullopt;
  for (const auto *D : Pattern->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto [Index, Expected] :
       {std::pair{0u, Input}, std::pair{1u, Output}, std::pair{2u, Object}})
    if (Arguments->get(Index).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(Index).getAsType(), Expected))
      return std::nullopt;

  auto Reference = [&](const Expr *Expression, const ValueDecl *Value) {
    return utilityAlgorithmReference(Expression, Value, Context);
  };
  auto ScalarReference = [&](const Expr *Expression, const ValueDecl *Value) {
    while (const auto *Cast = dyn_cast_or_null<ImplicitCastExpr>(Expression)) {
      if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                         Cast->getType()))
        return false;
      Expression = Cast->getSubExpr();
    }
    return Reference(Expression, Value);
  };
  auto Dereference = [&](const Expr *Expression, const ValueDecl *Pointer,
                         QualType Expected) {
    const auto *Deref =
        dyn_cast_or_null<UnaryOperator>(Expression->IgnoreParenImpCasts());
    return Deref && Deref->getOpcode() == UO_Deref && Deref->isLValue() &&
           Context.hasSameType(Deref->getType(), Expected) &&
           Reference(Deref->getSubExpr(), Pointer);
  };
  auto Increment = [&](const Expr *Expression, const ValueDecl *Value) {
    const auto *Unary = dyn_cast_or_null<UnaryOperator>(Expression);
    return Unary && Unary->getOpcode() == UO_PreInc &&
           Reference(Unary->getSubExpr(), Value);
  };
  auto Steps = [&](const Expr *Expression) {
    const auto *Comma = dyn_cast_or_null<BinaryOperator>(Expression);
    const auto *VoidStep =
        Comma ? dyn_cast<CStyleCastExpr>(Comma->getRHS()) : nullptr;
    return Comma && Comma->getOpcode() == BO_Comma &&
           Increment(Comma->getLHS(), Definition->getParamDecl(0)) &&
           VoidStep && VoidStep->getCastKind() == CK_ToVoid &&
           Increment(VoidStep->getSubExpr(), Definition->getParamDecl(2));
  };
  auto Condition = [&](const Expr *Expression) {
    const auto *Compare = dyn_cast_or_null<BinaryOperator>(Expression);
    return Compare && Compare->getOpcode() == BO_NE &&
           Reference(Compare->getLHS(), Definition->getParamDecl(0)) &&
           Reference(Compare->getRHS(), Definition->getParamDecl(1));
  };
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 2)
    return std::nullopt;
  auto Part = Body->body_begin();
  const auto *Branch = dyn_cast<IfStmt>(*Part++);
  const auto *Return = dyn_cast<ReturnStmt>(*Part);
  const auto *Then =
      Branch ? dyn_cast<CompoundStmt>(Branch->getThen()) : nullptr;
  if (!Branch || Branch->getInit() || Branch->getConditionVariable() ||
      Branch->getElse() || !Condition(Branch->getCond()) || !Then ||
      Then->size() != 3 || !Return ||
      !Reference(Return->getRetValue(), Definition->getParamDecl(2)))
    return std::nullopt;
  auto Statement = Then->body_begin();
  const auto *Declaration = dyn_cast<DeclStmt>(*Statement++);
  const auto *Variable = Declaration && Declaration->isSingleDecl()
                             ? dyn_cast<VarDecl>(Declaration->getSingleDecl())
                             : nullptr;
  const auto *Initial = Variable ? Variable->getInit() : nullptr;
  const auto *FirstStore = dyn_cast<BinaryOperator>(*Statement++);
  const auto *Loop = dyn_cast<ForStmt>(*Statement);
  const auto *LoopBody =
      Loop ? dyn_cast<CompoundStmt>(Loop->getBody()) : nullptr;
  if (!Variable || Variable->isImplicit() ||
      Variable->getDeclContext() != Definition ||
      !Context.hasSameUnqualifiedType(Variable->getType(), Element) ||
      !Initial ||
      !Dereference(Initial, Definition->getParamDecl(0),
                   Input->getPointeeType()) ||
      !FirstStore || FirstStore->getOpcode() != BO_Assign ||
      !Dereference(FirstStore->getLHS(), Definition->getParamDecl(2),
                   Output->getPointeeType()) ||
      !ScalarReference(FirstStore->getRHS(), Variable) || !Loop ||
      Loop->getConditionVariable() ||
      !Steps(dyn_cast_or_null<Expr>(Loop->getInit())) ||
      !Condition(Loop->getCond()) || !Steps(Loop->getInc()) || !LoopBody ||
      LoopBody->size() != (Adjacent ? 3u : 2u))
    return std::nullopt;
  auto LoopStatement = LoopBody->body_begin();
  const auto *CurrentDeclaration =
      Adjacent ? dyn_cast<DeclStmt>(*LoopStatement++) : nullptr;
  const auto *Current =
      CurrentDeclaration && CurrentDeclaration->isSingleDecl()
          ? dyn_cast<VarDecl>(CurrentDeclaration->getSingleDecl())
          : nullptr;
  const auto *Combine =
      Adjacent ? nullptr : dyn_cast<BinaryOperator>(*LoopStatement++);
  const auto *Store = dyn_cast<BinaryOperator>(*LoopStatement++);
  const auto *Transfer =
      Adjacent ? dyn_cast<BinaryOperator>(*LoopStatement) : nullptr;
  if ((Adjacent &&
       (!Current || Current->isImplicit() ||
        Current->getDeclContext() != Definition ||
        !Context.hasSameUnqualifiedType(Current->getType(), Element) ||
        !Current->getInit() ||
        !Dereference(Current->getInit(), Definition->getParamDecl(0),
                     Input->getPointeeType()))) ||
      (!Adjacent && (!Combine || Combine->getOpcode() != BO_Assign ||
                     !Reference(Combine->getLHS(), Variable))) ||
      !Store || Store->getOpcode() != BO_Assign ||
      !Dereference(Store->getLHS(), Definition->getParamDecl(2),
                   Output->getPointeeType()) ||
      (!Adjacent && !ScalarReference(Store->getRHS(), Variable)))
    return std::nullopt;
  if (Adjacent) {
    const auto *Move =
        Transfer ? dyn_cast<CallExpr>(
                       functionalInvokeStrippedExpression(Transfer->getRHS()))
                 : nullptr;
    const auto *MoveFunction = Move ? Move->getDirectCallee() : nullptr;
    const auto *MoveArguments =
        MoveFunction ? MoveFunction->getTemplateSpecializationArgs() : nullptr;
    const auto ReferenceType = Context.getLValueReferenceType(Element);
    if (!Transfer || Transfer->getOpcode() != BO_Assign ||
        !Reference(Transfer->getLHS(), Variable) || !Move ||
        Move->getNumArgs() != 1 || !Move->isXValue() ||
        !utilitySwapSDKFunction(S, SM, MoveFunction, "move",
                                "__utility/move.h") ||
        MoveFunction->getNumParams() != 1 || !MoveArguments ||
        MoveArguments->size() != 1 ||
        MoveArguments->get(0).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(MoveArguments->get(0).getAsType(),
                             ReferenceType) ||
        !Context.hasSameType(MoveFunction->getParamDecl(0)->getType(),
                             ReferenceType) ||
        !Context.hasSameType(MoveFunction->getReturnType(),
                             Context.getRValueReferenceType(Element)) ||
        !Context.hasSameType(Move->getType(), Element) ||
        !Reference(Move->getArg(0), Current))
      return std::nullopt;
  }
  const Expr *Result = Adjacent ? Store->getRHS() : Combine->getRHS();
  while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Result)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return std::nullopt;
    Result = Cast->getSubExpr();
  }
  const auto *Invocation = dyn_cast<CXXOperatorCallExpr>(Result);
  if (!Invocation || Invocation->getOperator() != OO_Call ||
      Invocation->getNumArgs() != 3 || !Invocation->isPRValue() ||
      !Reference(Invocation->getArg(0), Definition->getParamDecl(3)) ||
      !Reference(Invocation->getArg(1), Adjacent ? Current : Variable) ||
      (Adjacent
           ? !Reference(Invocation->getArg(2), Variable)
           : !Dereference(Invocation->getArg(2), Definition->getParamDecl(0),
                          Input->getPointeeType())))
    return std::nullopt;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Invocation->getDirectCallee());
  const auto *MethodDefinition = Method ? Method->getDefinition() : nullptr;
  if (!Method || !MethodDefinition || Method->isStatic() ||
      Method->isVolatile() || Method->getOverloadedOperator() != OO_Call ||
      Method->getNumParams() != 2 ||
      Method->getParent()->getCanonicalDecl() != Record->getCanonicalDecl() ||
      !functionalMemberReceiverValueCategory(Method, Invocation->getArg(0),
                                             false) ||
      !utilityScalarDirectConversion(Context, Method->getReturnType(),
                                     Adjacent ? Output->getPointeeType()
                                              : Element))
    return std::nullopt;
  std::optional<FunctionalOperationInfo> SDKOperation;
  if (SDKObject) {
    SDKOperation =
        approvedFunctionalOperationImpl(S, SM, Invocation, Context, false);
    if (!SDKOperation || SDKOperation->RightType.isNull() ||
        !Context.hasSameUnqualifiedType(
            Element,
            Method->getParamDecl(0)->getType().getNonReferenceType()) ||
        !Context.hasSameUnqualifiedType(
            Element, Method->getParamDecl(1)->getType().getNonReferenceType()))
      return std::nullopt;
  } else {
    if ((Method->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
         Method->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization) ||
        Method->getPrimaryTemplate() ||
        Method->getDescribedFunctionTemplate() || !ordinaryOperator(Method) ||
        !callableMethod(Method) || !S.owns(SM, MethodDefinition->getLocation()))
      return std::nullopt;
    for (const auto *D : Method->redecls())
      if (!S.owns(SM, D->getLocation()))
        return std::nullopt;
  }
  const auto LeftType = SDKOperation ? SDKOperation->LeftType
                                     : Method->getParamDecl(0)->getType();
  const auto RightType = SDKOperation ? SDKOperation->RightType
                                      : Method->getParamDecl(1)->getType();
  if (!utilityScalarDirectConversion(Context, Element, LeftType) ||
      !utilityScalarDirectConversion(Context, Element, RightType))
    return std::nullopt;
  return UtilityAlgorithmPredicateCall{
      Adjacent ? UtilityOperation::NumericAdjacentDifference
               : UtilityOperation::NumericPartialSum,
      Function,
      Invocation,
      Method,
      Object,
      3,
      SDKOperation};
}

// The initialized C++17 inclusive_scan overload carries its operation by
// value. Authenticate the pinned loop and the selected source or SDK method.
static std::optional<UtilityAlgorithmPredicateCall>
utilityNumericInclusiveScanObjectCall(const State &S, const SourceManager &SM,
                                      const CallExpr *Call,
                                      const ASTContext &Context,
                                      bool SDKInternal = false) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  if (!S.coreV2() || !Call || !Function ||
      Function->getName() != "inclusive_scan" || Call->getNumArgs() != 5 ||
      Function->getNumParams() != 5 || !Call->isPRValue() ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return std::nullopt;
  const auto Input = Function->getParamDecl(0)->getType();
  const auto Output = Function->getParamDecl(2)->getType();
  const auto Object = Function->getParamDecl(3)->getType();
  const auto Initial = Function->getParamDecl(4)->getType();
  const auto Element = Input->isPointerType()
                           ? Input->getPointeeType().getUnqualifiedType()
                           : QualType{};
  const bool Arithmetic =
      !Element.isNull() &&
      ((!Element->isEnumeralType() && !Element->isBooleanType() &&
        Element->isIntegerType() && Context.getTypeSize(Element) <= 64) ||
       Element->isSpecificBuiltinType(BuiltinType::Float) ||
       Element->isSpecificBuiltinType(BuiltinType::Double));
  const bool InitialArithmetic =
      !Initial->isEnumeralType() && !Initial->isBooleanType() &&
      ((Initial->isIntegerType() && Context.getTypeSize(Initial) <= 64) ||
       Initial->isSpecificBuiltinType(BuiltinType::Float) ||
       Initial->isSpecificBuiltinType(BuiltinType::Double));
  const auto *Record = Object->getAsCXXRecordDecl();
  Record = Record ? Record->getDefinition() : nullptr;
  const bool SDKObject =
      Record &&
      approvedFunctionalObjectRecord(S, SM, Record, Context).has_value();
  if (!Arithmetic || !InitialArithmetic ||
      !utilityScalarComparisonType(Context, Initial, Element, false) ||
      !utilityAlgorithmScalarPointer(Context, Input) ||
      !utilityAlgorithmWritableScalarPointer(Context, Output) ||
      !utilityScalarDirectConversion(Context, Initial,
                                     Output->getPointeeType()) ||
      !Context.hasSameType(Function->getParamDecl(1)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(0)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(1)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(2)->getType(), Output) ||
      !Context.hasSameType(Call->getArg(3)->getType(), Object) ||
      !Context.hasSameType(Call->getArg(4)->getType(), Initial) ||
      !Context.hasSameType(Function->getReturnType(), Output) ||
      !Context.hasSameType(Call->getType(), Output) || !Record ||
      Object.hasLocalQualifiers() || Record->isLambda() || Record->isUnion() ||
      !Record->isStandardLayout() || !Record->isTriviallyCopyable() ||
      !Record->hasTrivialCopyConstructor() || !Record->hasTrivialDestructor() ||
      (!S.owns(SM, Record->getLocation()) && !SDKObject))
    return std::nullopt;
  const auto *Primary = Function->getPrimaryTemplate();
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Function->getDefinition();
  const auto *PatternDefinition = Pattern ? Pattern->getDefinition() : nullptr;
  const auto *Arguments = Function->getTemplateSpecializationArgs();
  auto Origin = [&](const Decl *D) {
    return approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx",
                         "__numeric/inclusive_scan.h");
  };
  if (!Primary || !Pattern || !Definition || !PatternDefinition ||
      Function->isVariadic() ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !(SDKInternal
            ? utilityAlgorithmSDKReference(S, SM, Call, Function)
            : approvedUtilityReference(S, SM, Call, Function) != nullptr) ||
      !Origin(Definition) || !Origin(PatternDefinition) || !Arguments ||
      Arguments->size() != 4)
    return std::nullopt;
  for (const auto *D : Function->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto *D : Primary->redecls())
    if (!Origin(D) || !Origin(D->getTemplatedDecl()))
      return std::nullopt;
  for (const auto *D : Pattern->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto [Index, Expected] :
       {std::pair{0u, Input}, std::pair{1u, Output}, std::pair{2u, Initial},
        std::pair{3u, Object}})
    if (Arguments->get(Index).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(Index).getAsType(), Expected))
      return std::nullopt;

  auto Reference = [&](const Expr *Expression, const ValueDecl *Value) {
    return utilityAlgorithmReference(Expression, Value, Context);
  };
  auto Dereference = [&](const Expr *Expression, const ValueDecl *Pointer,
                         QualType Expected) {
    const auto *Deref =
        dyn_cast_or_null<UnaryOperator>(Expression->IgnoreParenImpCasts());
    return Deref && Deref->getOpcode() == UO_Deref && Deref->isLValue() &&
           Context.hasSameType(Deref->getType(), Expected) &&
           Reference(Deref->getSubExpr(), Pointer);
  };
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 2)
    return std::nullopt;
  auto Part = Body->body_begin();
  const auto *Loop = dyn_cast<ForStmt>(*Part++);
  const auto *Return = dyn_cast<ReturnStmt>(*Part);
  const auto *Condition =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getCond()) : nullptr;
  const auto *Steps =
      Loop ? dyn_cast_or_null<BinaryOperator>(Loop->getInc()) : nullptr;
  const auto *InputStep =
      Steps ? dyn_cast<UnaryOperator>(Steps->getLHS()) : nullptr;
  const auto *OutputCast =
      Steps ? dyn_cast<CStyleCastExpr>(Steps->getRHS()) : nullptr;
  const auto *OutputStep =
      OutputCast ? dyn_cast<UnaryOperator>(OutputCast->getSubExpr()) : nullptr;
  const auto *LoopBody =
      Loop ? dyn_cast<CompoundStmt>(Loop->getBody()) : nullptr;
  if (!Loop || Loop->getInit() || Loop->getConditionVariable() || !Condition ||
      Condition->getOpcode() != BO_NE ||
      !Reference(Condition->getLHS(), Definition->getParamDecl(0)) ||
      !Reference(Condition->getRHS(), Definition->getParamDecl(1)) || !Steps ||
      Steps->getOpcode() != BO_Comma || !InputStep ||
      InputStep->getOpcode() != UO_PreInc ||
      !Reference(InputStep->getSubExpr(), Definition->getParamDecl(0)) ||
      !OutputCast || OutputCast->getCastKind() != CK_ToVoid || !OutputStep ||
      OutputStep->getOpcode() != UO_PreInc ||
      !Reference(OutputStep->getSubExpr(), Definition->getParamDecl(2)) ||
      !LoopBody || LoopBody->size() != 2 || !Return ||
      !Reference(Return->getRetValue(), Definition->getParamDecl(2)))
    return std::nullopt;
  auto Statement = LoopBody->body_begin();
  const Stmt *CombineStatement = *Statement++;
  if (const auto *Cleanup = dyn_cast<ExprWithCleanups>(CombineStatement)) {
    if (Cleanup->getNumObjects())
      return std::nullopt;
    CombineStatement = Cleanup->getSubExpr();
  }
  const auto *Combine = dyn_cast<BinaryOperator>(CombineStatement);
  const auto *Store = dyn_cast<BinaryOperator>(*Statement);
  if (!Combine || Combine->getOpcode() != BO_Assign ||
      !Reference(Combine->getLHS(), Definition->getParamDecl(4)) || !Store ||
      Store->getOpcode() != BO_Assign ||
      !Dereference(Store->getLHS(), Definition->getParamDecl(2),
                   Output->getPointeeType()))
    return std::nullopt;
  const Expr *Stored = Store->getRHS();
  while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Stored)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return std::nullopt;
    Stored = Cast->getSubExpr();
  }
  if (!Reference(Stored, Definition->getParamDecl(4)))
    return std::nullopt;
  const Expr *Result = Combine->getRHS();
  while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Result)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return std::nullopt;
    Result = Cast->getSubExpr();
  }
  const auto *Invocation = dyn_cast<CXXOperatorCallExpr>(Result);
  const Expr *ElementArgument = Invocation && Invocation->getNumArgs() == 3
                                    ? Invocation->getArg(2)
                                    : nullptr;
  while (ElementArgument) {
    if (const auto *Temporary =
            dyn_cast<MaterializeTemporaryExpr>(ElementArgument)) {
      if (!Temporary->isLValue() || Temporary->getExtendingDecl() ||
          !utilityScalar(Context, Temporary->getType()))
        return std::nullopt;
      ElementArgument = Temporary->getSubExpr();
      continue;
    }
    const auto *Cast = dyn_cast<ImplicitCastExpr>(ElementArgument);
    if (!Cast)
      break;
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return std::nullopt;
    ElementArgument = Cast->getSubExpr();
  }
  if (!Invocation || Invocation->getOperator() != OO_Call ||
      Invocation->getNumArgs() != 3 || !Invocation->isPRValue() ||
      !Reference(Invocation->getArg(0), Definition->getParamDecl(3)) ||
      !Reference(Invocation->getArg(1), Definition->getParamDecl(4)) ||
      !Dereference(ElementArgument, Definition->getParamDecl(0),
                   Input->getPointeeType()))
    return std::nullopt;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Invocation->getDirectCallee());
  const auto *MethodDefinition = Method ? Method->getDefinition() : nullptr;
  if (!Method || !MethodDefinition || Method->isStatic() ||
      Method->isVolatile() || Method->getOverloadedOperator() != OO_Call ||
      Method->getNumParams() != 2 ||
      Method->getParent()->getCanonicalDecl() != Record->getCanonicalDecl() ||
      !functionalMemberReceiverValueCategory(Method, Invocation->getArg(0),
                                             false) ||
      !utilityScalarDirectConversion(Context, Method->getReturnType(), Initial))
    return std::nullopt;
  std::optional<FunctionalOperationInfo> SDKOperation;
  if (SDKObject) {
    SDKOperation =
        approvedFunctionalOperationImpl(S, SM, Invocation, Context, false);
    if (!SDKOperation || SDKOperation->RightType.isNull() ||
        !Context.hasSameUnqualifiedType(
            Initial,
            Method->getParamDecl(0)->getType().getNonReferenceType()) ||
        !utilityScalarDirectConversion(
            Context, Element,
            Method->getParamDecl(1)->getType().getNonReferenceType()))
      return std::nullopt;
  } else {
    if ((Method->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
         Method->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization) ||
        Method->getPrimaryTemplate() ||
        Method->getDescribedFunctionTemplate() || !ordinaryOperator(Method) ||
        !callableMethod(Method) || !S.owns(SM, MethodDefinition->getLocation()))
      return std::nullopt;
    for (const auto *D : Method->redecls())
      if (!S.owns(SM, D->getLocation()))
        return std::nullopt;
  }
  const auto LeftType = SDKOperation ? SDKOperation->LeftType
                                     : Method->getParamDecl(0)->getType();
  const auto RightType = SDKOperation ? SDKOperation->RightType
                                      : Method->getParamDecl(1)->getType();
  if (!utilityScalarDirectConversion(Context, Initial, LeftType) ||
      !utilityScalarDirectConversion(Context, Element, RightType))
    return std::nullopt;
  return UtilityAlgorithmPredicateCall{UtilityOperation::NumericInclusiveScan,
                                       Function,
                                       Invocation,
                                       Method,
                                       Object,
                                       3,
                                       SDKOperation};
}

// The no-init overload seeds the first output and delegates the remaining
// range to the authenticated initialized overload with a trivial object copy.
static std::optional<UtilityAlgorithmPredicateCall>
utilityNumericInclusiveScanSeedObjectCall(const State &S,
                                          const SourceManager &SM,
                                          const CallExpr *Call,
                                          const ASTContext &Context) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  if (!S.coreV2() || !Call || !Function ||
      Function->getName() != "inclusive_scan" || Call->getNumArgs() != 4 ||
      Function->getNumParams() != 4 || !Call->isPRValue() ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return std::nullopt;
  const auto Input = Function->getParamDecl(0)->getType();
  const auto Output = Function->getParamDecl(2)->getType();
  const auto Object = Function->getParamDecl(3)->getType();
  const auto Element = Input->isPointerType()
                           ? Input->getPointeeType().getUnqualifiedType()
                           : QualType{};
  const bool Arithmetic =
      !Element.isNull() &&
      ((!Element->isEnumeralType() && !Element->isBooleanType() &&
        Element->isIntegerType() && Context.getTypeSize(Element) <= 64) ||
       Element->isSpecificBuiltinType(BuiltinType::Float) ||
       Element->isSpecificBuiltinType(BuiltinType::Double));
  if (!Arithmetic || !utilityAlgorithmScalarPointer(Context, Input) ||
      !utilityAlgorithmWritableScalarPointer(Context, Output) ||
      !utilityScalarDirectConversion(Context, Element,
                                     Output->getPointeeType()) ||
      !Context.hasSameType(Function->getParamDecl(1)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(0)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(1)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(2)->getType(), Output) ||
      !Context.hasSameType(Call->getArg(3)->getType(), Object) ||
      !Context.hasSameType(Function->getReturnType(), Output) ||
      !Context.hasSameType(Call->getType(), Output))
    return std::nullopt;
  const auto *Primary = Function->getPrimaryTemplate();
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Function->getDefinition();
  const auto *PatternDefinition = Pattern ? Pattern->getDefinition() : nullptr;
  const auto *Arguments = Function->getTemplateSpecializationArgs();
  auto Origin = [&](const Decl *D) {
    return approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx",
                         "__numeric/inclusive_scan.h");
  };
  if (!Primary || !Pattern || !Definition || !PatternDefinition ||
      Function->isVariadic() ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedUtilityReference(S, SM, Call, Function) || !Origin(Definition) ||
      !Origin(PatternDefinition) || !Arguments || Arguments->size() != 3)
    return std::nullopt;
  for (const auto *D : Function->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto *D : Primary->redecls())
    if (!Origin(D) || !Origin(D->getTemplatedDecl()))
      return std::nullopt;
  for (const auto *D : Pattern->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto [Index, Expected] :
       {std::pair{0u, Input}, std::pair{1u, Output}, std::pair{2u, Object}})
    if (Arguments->get(Index).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(Index).getAsType(), Expected))
      return std::nullopt;

  auto Reference = [&](const Expr *Expression, const ValueDecl *Value) {
    return utilityAlgorithmReference(Expression, Value, Context);
  };
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 2)
    return std::nullopt;
  auto Part = Body->body_begin();
  const auto *Branch = dyn_cast<IfStmt>(*Part++);
  const auto *Return = dyn_cast<ReturnStmt>(*Part);
  const auto *Condition =
      Branch ? dyn_cast<BinaryOperator>(Branch->getCond()) : nullptr;
  const auto *Then =
      Branch ? dyn_cast<CompoundStmt>(Branch->getThen()) : nullptr;
  if (!Branch || Branch->getInit() || Branch->getConditionVariable() ||
      Branch->getElse() || !Condition || Condition->getOpcode() != BO_NE ||
      !Reference(Condition->getLHS(), Definition->getParamDecl(0)) ||
      !Reference(Condition->getRHS(), Definition->getParamDecl(1)) || !Then ||
      Then->size() != 3 || !Return ||
      !Reference(Return->getRetValue(), Definition->getParamDecl(2)))
    return std::nullopt;
  auto Statement = Then->body_begin();
  const auto *Declaration = dyn_cast<DeclStmt>(*Statement++);
  const auto *Variable = Declaration && Declaration->isSingleDecl()
                             ? dyn_cast<VarDecl>(Declaration->getSingleDecl())
                             : nullptr;
  const auto *Initial = Variable ? Variable->getInit() : nullptr;
  const auto *InitialDeref =
      Initial ? dyn_cast<UnaryOperator>(Initial->IgnoreParenImpCasts())
              : nullptr;
  const auto *FirstStore = dyn_cast<BinaryOperator>(*Statement++);
  const auto *Destination =
      FirstStore ? dyn_cast<UnaryOperator>(FirstStore->getLHS()) : nullptr;
  const auto *OutputAdvance =
      Destination ? dyn_cast<UnaryOperator>(Destination->getSubExpr())
                  : nullptr;
  const auto *NextBranch = dyn_cast<IfStmt>(*Statement);
  const auto *NextCondition =
      NextBranch ? dyn_cast<BinaryOperator>(NextBranch->getCond()) : nullptr;
  const auto *InputAdvance =
      NextCondition ? dyn_cast<UnaryOperator>(
                          NextCondition->getLHS()->IgnoreParenImpCasts())
                    : nullptr;
  const auto *NextReturn =
      NextBranch ? dyn_cast<ReturnStmt>(NextBranch->getThen()) : nullptr;
  const auto *Nested =
      NextReturn ? dyn_cast<CallExpr>(NextReturn->getRetValue()) : nullptr;
  if (!Variable || Variable->isImplicit() ||
      Variable->getDeclContext() != Definition ||
      !Context.hasSameUnqualifiedType(Variable->getType(), Element) ||
      !InitialDeref || InitialDeref->getOpcode() != UO_Deref ||
      !InitialDeref->isLValue() ||
      !Context.hasSameType(InitialDeref->getType(), Input->getPointeeType()) ||
      !Reference(InitialDeref->getSubExpr(), Definition->getParamDecl(0)) ||
      !FirstStore || FirstStore->getOpcode() != BO_Assign || !Destination ||
      Destination->getOpcode() != UO_Deref || !Destination->isLValue() ||
      !Context.hasSameType(Destination->getType(), Output->getPointeeType()) ||
      !OutputAdvance || OutputAdvance->getOpcode() != UO_PostInc ||
      !Reference(OutputAdvance->getSubExpr(), Definition->getParamDecl(2)) ||
      !NextBranch || NextBranch->getInit() ||
      NextBranch->getConditionVariable() || NextBranch->getElse() ||
      !NextCondition || NextCondition->getOpcode() != BO_NE || !InputAdvance ||
      InputAdvance->getOpcode() != UO_PreInc ||
      !Reference(InputAdvance->getSubExpr(), Definition->getParamDecl(0)) ||
      !Reference(NextCondition->getRHS(), Definition->getParamDecl(1)) ||
      !Nested || Nested->getNumArgs() != 5 || !Nested->isPRValue() ||
      !Context.hasSameType(Nested->getType(), Output))
    return std::nullopt;
  const Expr *Stored = FirstStore->getRHS();
  while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Stored)) {
    if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                       Cast->getType()))
      return std::nullopt;
    Stored = Cast->getSubExpr();
  }
  if (!Reference(Stored, Variable) ||
      !Reference(Nested->getArg(0), Definition->getParamDecl(0)) ||
      !Reference(Nested->getArg(1), Definition->getParamDecl(1)) ||
      !Reference(Nested->getArg(2), Definition->getParamDecl(2)) ||
      !Reference(Nested->getArg(4), Variable))
    return std::nullopt;
  const auto *Copy = dyn_cast<CXXConstructExpr>(Nested->getArg(3));
  if (!Copy || Copy->getNumArgs() != 1 ||
      !Copy->getConstructor()->isCopyConstructor() ||
      !Copy->getConstructor()->isTrivial() ||
      !Context.hasSameType(Copy->getType(), Object) ||
      !Reference(Copy->getArg(0), Definition->getParamDecl(3)))
    return std::nullopt;
  auto NestedInfo =
      utilityNumericInclusiveScanObjectCall(S, SM, Nested, Context, true);
  if (!NestedInfo || NestedInfo->ParameterIndex != 3 ||
      !Context.hasSameType(NestedInfo->ObjectType, Object))
    return std::nullopt;
  return UtilityAlgorithmPredicateCall{UtilityOperation::NumericInclusiveScan,
                                       Function,
                                       NestedInfo->Invocation,
                                       NestedInfo->Method,
                                       Object,
                                       3,
                                       NestedInfo->SDKOperation};
}

// The pinned exclusive_scan loop computes the next accumulator before writing
// the old value. Authenticate both calls on the same copied operation object.
static std::optional<UtilityAlgorithmPredicateCall>
utilityNumericExclusiveScanObjectCall(const State &S, const SourceManager &SM,
                                      const CallExpr *Call,
                                      const ASTContext &Context) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  if (!S.coreV2() || !Call || !Function ||
      Function->getName() != "exclusive_scan" || Call->getNumArgs() != 5 ||
      Function->getNumParams() != 5 || !Call->isPRValue() ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return std::nullopt;
  const auto Input = Function->getParamDecl(0)->getType();
  const auto Output = Function->getParamDecl(2)->getType();
  const auto Initial = Function->getParamDecl(3)->getType();
  const auto Object = Function->getParamDecl(4)->getType();
  const auto Element = Input->isPointerType()
                           ? Input->getPointeeType().getUnqualifiedType()
                           : QualType{};
  const bool Arithmetic =
      !Element.isNull() &&
      ((!Element->isEnumeralType() && !Element->isBooleanType() &&
        Element->isIntegerType() && Context.getTypeSize(Element) <= 64) ||
       Element->isSpecificBuiltinType(BuiltinType::Float) ||
       Element->isSpecificBuiltinType(BuiltinType::Double));
  const bool InitialArithmetic =
      !Initial->isEnumeralType() && !Initial->isBooleanType() &&
      ((Initial->isIntegerType() && Context.getTypeSize(Initial) <= 64) ||
       Initial->isSpecificBuiltinType(BuiltinType::Float) ||
       Initial->isSpecificBuiltinType(BuiltinType::Double));
  const auto *Record = Object->getAsCXXRecordDecl();
  Record = Record ? Record->getDefinition() : nullptr;
  const bool SDKObject =
      Record &&
      approvedFunctionalObjectRecord(S, SM, Record, Context).has_value();
  if (!Arithmetic || !InitialArithmetic ||
      !utilityScalarComparisonType(Context, Initial, Element, false) ||
      !utilityAlgorithmScalarPointer(Context, Input) ||
      !utilityAlgorithmWritableScalarPointer(Context, Output) ||
      !utilityScalarDirectConversion(Context, Initial,
                                     Output->getPointeeType()) ||
      !Context.hasSameType(Function->getParamDecl(1)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(0)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(1)->getType(), Input) ||
      !Context.hasSameType(Call->getArg(2)->getType(), Output) ||
      !Context.hasSameType(Call->getArg(3)->getType(), Initial) ||
      !Context.hasSameType(Call->getArg(4)->getType(), Object) ||
      !Context.hasSameType(Function->getReturnType(), Output) ||
      !Context.hasSameType(Call->getType(), Output) || !Record ||
      Object.hasLocalQualifiers() || Record->isLambda() || Record->isUnion() ||
      !Record->isStandardLayout() || !Record->isTriviallyCopyable() ||
      !Record->hasTrivialCopyConstructor() || !Record->hasTrivialDestructor() ||
      (!S.owns(SM, Record->getLocation()) && !SDKObject))
    return std::nullopt;
  const auto *Primary = Function->getPrimaryTemplate();
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Function->getDefinition();
  const auto *PatternDefinition = Pattern ? Pattern->getDefinition() : nullptr;
  const auto *Arguments = Function->getTemplateSpecializationArgs();
  auto Origin = [&](const Decl *D) {
    return approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx",
                         "__numeric/exclusive_scan.h");
  };
  if (!Primary || !Pattern || !Definition || !PatternDefinition ||
      Function->isVariadic() ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedUtilityReference(S, SM, Call, Function) || !Origin(Definition) ||
      !Origin(PatternDefinition) || !Arguments || Arguments->size() != 4)
    return std::nullopt;
  for (const auto *D : Function->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto *D : Primary->redecls())
    if (!Origin(D) || !Origin(D->getTemplatedDecl()))
      return std::nullopt;
  for (const auto *D : Pattern->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto [Index, Expected] :
       {std::pair{0u, Input}, std::pair{1u, Output}, std::pair{2u, Initial},
        std::pair{3u, Object}})
    if (Arguments->get(Index).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(Index).getAsType(), Expected))
      return std::nullopt;

  auto Reference = [&](const Expr *Expression, const ValueDecl *Value) {
    return utilityAlgorithmReference(Expression, Value, Context);
  };
  auto Dereference = [&](const Expr *Expression, const ValueDecl *Pointer,
                         QualType Expected) {
    const auto *Deref =
        Expression ? dyn_cast<UnaryOperator>(Expression->IgnoreParenImpCasts())
                   : nullptr;
    return Deref && Deref->getOpcode() == UO_Deref && Deref->isLValue() &&
           Context.hasSameType(Deref->getType(), Expected) &&
           Reference(Deref->getSubExpr(), Pointer);
  };
  auto Move = [&](const Expr *Expression, const ValueDecl *Value) {
    while (const auto *Cast = dyn_cast_or_null<ImplicitCastExpr>(Expression)) {
      if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                         Cast->getType()))
        return false;
      Expression = Cast->getSubExpr();
    }
    const auto *MoveCall = dyn_cast<CallExpr>(Expression);
    const auto *MoveFunction = MoveCall ? MoveCall->getDirectCallee() : nullptr;
    const auto *MoveArguments =
        MoveFunction ? MoveFunction->getTemplateSpecializationArgs() : nullptr;
    const auto ReferenceType = Context.getLValueReferenceType(Initial);
    return MoveCall && MoveCall->getNumArgs() == 1 && MoveCall->isXValue() &&
           utilitySwapSDKFunction(S, SM, MoveFunction, "move",
                                  "__utility/move.h") &&
           MoveFunction->getNumParams() == 1 && MoveArguments &&
           MoveArguments->size() == 1 &&
           MoveArguments->get(0).getKind() == TemplateArgument::Type &&
           Context.hasSameType(MoveArguments->get(0).getAsType(),
                               ReferenceType) &&
           Context.hasSameType(MoveFunction->getParamDecl(0)->getType(),
                               ReferenceType) &&
           Context.hasSameType(MoveFunction->getReturnType(),
                               Context.getRValueReferenceType(Initial)) &&
           Context.hasSameType(MoveCall->getType(), Initial) &&
           Reference(MoveCall->getArg(0), Value);
  };
  auto StripScalar = [&](const Expr *Expression) -> const Expr * {
    while (Expression) {
      if (const auto *Cleanup = dyn_cast<ExprWithCleanups>(Expression)) {
        if (Cleanup->getNumObjects())
          return nullptr;
        Expression = Cleanup->getSubExpr();
        continue;
      }
      if (const auto *Temporary =
              dyn_cast<MaterializeTemporaryExpr>(Expression)) {
        if (!Temporary->isLValue() || Temporary->getExtendingDecl() ||
            !utilityScalar(Context, Temporary->getType()))
          return nullptr;
        Expression = Temporary->getSubExpr();
        continue;
      }
      const auto *Cast = dyn_cast<ImplicitCastExpr>(Expression);
      if (!Cast)
        break;
      if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                         Cast->getType()))
        return nullptr;
      Expression = Cast->getSubExpr();
    }
    return Expression;
  };
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  if (!Body || Body->size() != 2)
    return std::nullopt;
  auto Part = Body->body_begin();
  const auto *Branch = dyn_cast<IfStmt>(*Part++);
  const auto *Return = dyn_cast<ReturnStmt>(*Part);
  const auto *Condition =
      Branch ? dyn_cast<BinaryOperator>(Branch->getCond()) : nullptr;
  const auto *Then =
      Branch ? dyn_cast<CompoundStmt>(Branch->getThen()) : nullptr;
  if (!Branch || Branch->getInit() || Branch->getConditionVariable() ||
      Branch->getElse() || !Condition || Condition->getOpcode() != BO_NE ||
      !Reference(Condition->getLHS(), Definition->getParamDecl(0)) ||
      !Reference(Condition->getRHS(), Definition->getParamDecl(1)) || !Then ||
      Then->size() != 2 || !Return ||
      !Reference(Return->getRetValue(), Definition->getParamDecl(2)))
    return std::nullopt;
  auto Statement = Then->body_begin();
  const auto *Declaration = dyn_cast<DeclStmt>(*Statement++);
  const auto *Temporary = Declaration && Declaration->isSingleDecl()
                              ? dyn_cast<VarDecl>(Declaration->getSingleDecl())
                              : nullptr;
  const Expr *FirstResult =
      Temporary ? StripScalar(Temporary->getInit()) : nullptr;
  const auto *InitialInvocation =
      dyn_cast_or_null<CXXOperatorCallExpr>(FirstResult);
  const auto *Loop = dyn_cast<WhileStmt>(*Statement);
  const auto *True =
      Loop ? dyn_cast<CXXBoolLiteralExpr>(Loop->getCond()) : nullptr;
  const auto *LoopBody =
      Loop ? dyn_cast<CompoundStmt>(Loop->getBody()) : nullptr;
  if (!Temporary || Temporary->isImplicit() ||
      Temporary->getDeclContext() != Definition ||
      !Context.hasSameType(Temporary->getType(), Initial) ||
      !InitialInvocation || !True || !True->getValue() || !LoopBody ||
      LoopBody->size() != 6)
    return std::nullopt;
  auto LoopStatement = LoopBody->body_begin();
  const auto *Store = dyn_cast<BinaryOperator>(*LoopStatement++);
  const auto *OutputStep = dyn_cast<UnaryOperator>(*LoopStatement++);
  const auto *InputStep = dyn_cast<UnaryOperator>(*LoopStatement++);
  const auto *BreakBranch = dyn_cast<IfStmt>(*LoopStatement++);
  const auto *BreakCondition =
      BreakBranch ? dyn_cast<BinaryOperator>(BreakBranch->getCond()) : nullptr;
  const auto *Transfer = dyn_cast<BinaryOperator>(*LoopStatement++);
  const Stmt *RecomputeStatement = *LoopStatement;
  if (const auto *Cleanup = dyn_cast<ExprWithCleanups>(RecomputeStatement)) {
    if (Cleanup->getNumObjects())
      return std::nullopt;
    RecomputeStatement = Cleanup->getSubExpr();
  }
  const auto *Recompute = dyn_cast<BinaryOperator>(RecomputeStatement);
  if (!Store || Store->getOpcode() != BO_Assign ||
      !Dereference(Store->getLHS(), Definition->getParamDecl(2),
                   Output->getPointeeType()) ||
      !Move(Store->getRHS(), Definition->getParamDecl(3)) || !OutputStep ||
      OutputStep->getOpcode() != UO_PreInc ||
      !Reference(OutputStep->getSubExpr(), Definition->getParamDecl(2)) ||
      !InputStep || InputStep->getOpcode() != UO_PreInc ||
      !Reference(InputStep->getSubExpr(), Definition->getParamDecl(0)) ||
      !BreakBranch || BreakBranch->getInit() ||
      BreakBranch->getConditionVariable() || BreakBranch->getElse() ||
      !BreakCondition || BreakCondition->getOpcode() != BO_EQ ||
      !Reference(BreakCondition->getLHS(), Definition->getParamDecl(0)) ||
      !Reference(BreakCondition->getRHS(), Definition->getParamDecl(1)) ||
      !isa<BreakStmt>(BreakBranch->getThen()) || !Transfer ||
      Transfer->getOpcode() != BO_Assign ||
      !Reference(Transfer->getLHS(), Definition->getParamDecl(3)) ||
      !Move(Transfer->getRHS(), Temporary) || !Recompute ||
      Recompute->getOpcode() != BO_Assign ||
      !Reference(Recompute->getLHS(), Temporary))
    return std::nullopt;
  const Expr *SecondResult = StripScalar(Recompute->getRHS());
  const auto *SecondInvocation = dyn_cast<CXXOperatorCallExpr>(SecondResult);
  auto Invocation = [&](const CXXOperatorCallExpr *Selected) {
    return Selected && Selected->getOperator() == OO_Call &&
           Selected->getNumArgs() == 3 && Selected->isPRValue() &&
           Reference(Selected->getArg(0), Definition->getParamDecl(4)) &&
           Reference(Selected->getArg(1), Definition->getParamDecl(3)) &&
           Dereference(StripScalar(Selected->getArg(2)),
                       Definition->getParamDecl(0), Input->getPointeeType());
  };
  if (!Invocation(InitialInvocation) || !Invocation(SecondInvocation))
    return std::nullopt;
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(InitialInvocation->getDirectCallee());
  const auto *SecondMethod =
      dyn_cast_or_null<CXXMethodDecl>(SecondInvocation->getDirectCallee());
  const auto *MethodDefinition = Method ? Method->getDefinition() : nullptr;
  if (!Method || !SecondMethod || !MethodDefinition ||
      Method->getCanonicalDecl() != SecondMethod->getCanonicalDecl() ||
      Method->isStatic() || Method->isVolatile() ||
      Method->getOverloadedOperator() != OO_Call ||
      Method->getNumParams() != 2 ||
      Method->getParent()->getCanonicalDecl() != Record->getCanonicalDecl() ||
      !functionalMemberReceiverValueCategory(
          Method, InitialInvocation->getArg(0), false) ||
      !functionalMemberReceiverValueCategory(
          Method, SecondInvocation->getArg(0), false) ||
      !utilityScalarDirectConversion(Context, Method->getReturnType(), Initial))
    return std::nullopt;
  std::optional<FunctionalOperationInfo> SDKOperation;
  if (SDKObject) {
    SDKOperation = approvedFunctionalOperationImpl(S, SM, InitialInvocation,
                                                   Context, false);
    const auto SecondOperation = approvedFunctionalOperationImpl(
        S, SM, SecondInvocation, Context, false);
    if (!SDKOperation || !SecondOperation || SDKOperation->RightType.isNull() ||
        SDKOperation->Operation != SecondOperation->Operation ||
        !Context.hasSameUnqualifiedType(
            Initial,
            Method->getParamDecl(0)->getType().getNonReferenceType()) ||
        !utilityScalarDirectConversion(
            Context, Element,
            Method->getParamDecl(1)->getType().getNonReferenceType()))
      return std::nullopt;
  } else {
    if ((Method->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
         Method->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization) ||
        Method->getPrimaryTemplate() ||
        Method->getDescribedFunctionTemplate() || !ordinaryOperator(Method) ||
        !callableMethod(Method) || !S.owns(SM, MethodDefinition->getLocation()))
      return std::nullopt;
    for (const auto *D : Method->redecls())
      if (!S.owns(SM, D->getLocation()))
        return std::nullopt;
  }
  const auto LeftType = SDKOperation ? SDKOperation->LeftType
                                     : Method->getParamDecl(0)->getType();
  const auto RightType = SDKOperation ? SDKOperation->RightType
                                      : Method->getParamDecl(1)->getType();
  if (!utilityScalarDirectConversion(Context, Initial, LeftType) ||
      !utilityScalarDirectConversion(Context, Element, RightType))
    return std::nullopt;
  return UtilityAlgorithmPredicateCall{UtilityOperation::NumericExclusiveScan,
                                       Function,
                                       InitialInvocation,
                                       Method,
                                       Object,
                                       4,
                                       SDKOperation};
}

std::optional<UtilityAlgorithmPredicateCall>
approvedUtilityAlgorithmPredicateCall(const State &S, const SourceManager &SM,
                                      const CallExpr *Call,
                                      const ASTContext &Context) {
  const auto *Function = Call ? Call->getDirectCallee() : nullptr;
  if (!S.coreV2() || !Call || !Function || !Function->getIdentifier() ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent() || !Call->isPRValue())
    return std::nullopt;
  const auto Name = Function->getName();
  if (Name == "transform" &&
      (Call->getNumArgs() == 4 || Call->getNumArgs() == 5))
    return utilityAlgorithmTransformObjectCall(S, SM, Call, Context);
  if (Name == "for_each_n")
    return utilityAlgorithmForEachNObjectCall(S, SM, Call, Context);
  if (Name == "for_each")
    return utilityAlgorithmForEachObjectCall(S, SM, Call, Context);
  if (Name == "generate" || Name == "generate_n")
    return utilityAlgorithmGenerateObjectCall(S, SM, Call, Context);
  if (Name == "accumulate" || Name == "reduce")
    return utilityNumericReductionObjectCall(S, SM, Call, Context);
  if ((Name == "partial_sum" || Name == "adjacent_difference") &&
      Call->getNumArgs() == 4)
    return utilityNumericPrefixObjectCall(S, SM, Call, Context);
  if (Name == "inclusive_scan" && Call->getNumArgs() == 5)
    return utilityNumericInclusiveScanObjectCall(S, SM, Call, Context);
  if (Name == "inclusive_scan" && Call->getNumArgs() == 4)
    return utilityNumericInclusiveScanSeedObjectCall(S, SM, Call, Context);
  if (Name == "exclusive_scan" && Call->getNumArgs() == 5)
    return utilityNumericExclusiveScanObjectCall(S, SM, Call, Context);
  const bool Find = Name == "find_if", FindNot = Name == "find_if_not";
  const bool None = Name == "none_of", All = Name == "all_of";
  const bool Any = Name == "any_of", Count = Name == "count_if";
  const bool Copy = Name == "copy_if";
  const bool Remove = Name == "remove_if";
  const bool Partitioned = Name == "is_partitioned";
  const bool PartitionPoint = Name == "partition_point";
  const bool PartitionCopy = Name == "partition_copy";
  const bool Composed = All || Any || Count || Copy;
  const bool Replace = Name == "replace_if",
             ReplaceCopy = Name == "replace_copy_if";
  const bool Replacement = Replace || ReplaceCopy;
  const bool RemoveCopy = Name == "remove_copy_if";
  const bool CopyOutput = ReplaceCopy || RemoveCopy || Copy || PartitionCopy;
  if (!Find && !FindNot && !None && !Composed && !Replacement && !RemoveCopy &&
      !Partitioned && !PartitionPoint && !PartitionCopy && !Remove)
    return std::nullopt;
  const unsigned ParameterCount = (ReplaceCopy || PartitionCopy)    ? 5
                                  : (Replace || RemoveCopy || Copy) ? 4
                                                                    : 3;
  const unsigned PredicateIndex = PartitionCopy ? 4 : CopyOutput ? 3 : 2;
  if (Call->getNumArgs() != ParameterCount ||
      Function->getNumParams() != ParameterCount)
    return std::nullopt;
  const auto Object = Function->getParamDecl(PredicateIndex)->getType();
  const auto *Record = Object->getAsCXXRecordDecl();
  Record = Record ? Record->getDefinition() : nullptr;
  // Source operators and pinned SDK functional operators retain independent
  // declaration and body proofs, sharing the exact algorithm loop proof.
  const bool SDKObject =
      Record &&
      approvedFunctionalObjectRecord(S, SM, Record, Context).has_value();
  if (!Record || Object.hasLocalQualifiers() || Record->isLambda() ||
      Record->isUnion() || !Record->isStandardLayout() ||
      !Record->isTriviallyCopyable() || !Record->hasTrivialCopyConstructor() ||
      !Record->hasTrivialDestructor() ||
      (!S.owns(SM, Record->getLocation()) && !SDKObject) ||
      !Context.hasSameType(Call->getArg(PredicateIndex)->getType(), Object))
    return std::nullopt;
  const auto *Primary = Function->getPrimaryTemplate();
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  const auto *Definition = Function->getDefinition();
  const auto *PatternDefinition = Pattern ? Pattern->getDefinition() : nullptr;
  const auto *Arguments = Function->getTemplateSpecializationArgs();
  const std::string Path = "__algorithm/" + Name.str() + ".h";
  auto Origin = [&](const Decl *D) {
    return approvedStandardSDKDeclaration(S, SM, D) &&
           cstddefOrigin(S, SM, D->getLocation(), "libcxx", Path);
  };
  if (!Primary || !Pattern || !Definition || !PatternDefinition ||
      Function->isVariadic() ||
      (!Function->isInlined() && !Partitioned && !PartitionPoint &&
       !PartitionCopy && !Remove) ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedUtilityReference(S, SM, Call, Function) || !Origin(Definition) ||
      !Origin(PatternDefinition) || !Arguments ||
      Arguments->size() != ((ReplaceCopy || PartitionCopy)    ? 4u
                            : (Replace || RemoveCopy || Copy) ? 3u
                                                              : 2u))
    return std::nullopt;
  for (const auto *D : Function->redecls())
    if (!Origin(D))
      return std::nullopt;
  for (const auto *D : Primary->redecls())
    if (!Origin(D) || !Origin(D->getTemplatedDecl()))
      return std::nullopt;
  for (const auto *D : Pattern->redecls())
    if (!Origin(D))
      return std::nullopt;
  QualType PairResult;
  if (PartitionCopy) {
    auto Pair = approvedUtilityPairRecord(
        S, SM, Function->getReturnType()->getAsCXXRecordDecl(), Context);
    if (!Pair ||
        !Context.hasSameType(Pair->First->getType(),
                             Function->getParamDecl(2)->getType()) ||
        !Context.hasSameType(Pair->Second->getType(),
                             Function->getParamDecl(3)->getType()))
      return std::nullopt;
    PairResult = Context.getRecordType(Pair->Record);
  }
  const auto Pointer = Function->getParamDecl(0)->getType();
  if (!utilityAlgorithmScalarPointer(Context, Pointer) ||
      !Context.hasSameType(Pointer, Function->getParamDecl(1)->getType()) ||
      !Context.hasSameType(Pointer, Call->getArg(0)->getType()) ||
      !Context.hasSameType(Pointer, Call->getArg(1)->getType()) ||
      !Context.hasSameType(Call->getType(), Function->getReturnType()) ||
      !Context.hasSameType(Function->getReturnType(),
                           PartitionCopy ? PairResult
                           : Replace     ? Context.VoidTy
                           : CopyOutput  ? Function->getParamDecl(2)->getType()
                           : Count       ? Context.getPointerDiffType()
                           : (None || All || Any || Partitioned)
                               ? Context.BoolTy
                               : Pointer))
    return std::nullopt;
  const auto Output =
      CopyOutput ? Function->getParamDecl(2)->getType() : Pointer;
  const unsigned PredicateArgument = PartitionCopy ? 3 : CopyOutput ? 2 : 1;
  for (unsigned I = 0; I <= PredicateArgument; ++I) {
    const auto Expected = I == PredicateArgument ? Object
                          : I == 2 ? Function->getParamDecl(3)->getType()
                          : I      ? Output
                                   : Pointer;
    if (Arguments->get(I).getKind() != TemplateArgument::Type ||
        !Context.hasSameType(Arguments->get(I).getAsType(), Expected))
      return std::nullopt;
  }
  if (PartitionCopy) {
    for (unsigned I : {2u, 3u}) {
      const auto Destination = Function->getParamDecl(I)->getType();
      if (!utilityAlgorithmWritableScalarPointer(Context, Destination) ||
          !Context.hasSameType(Call->getArg(I)->getType(), Destination) ||
          !utilityScalarDirectConversion(Context, Pointer->getPointeeType(),
                                         Destination->getPointeeType()))
        return std::nullopt;
    }
  }
  if (Remove && !utilityAlgorithmWritableScalarPointer(Context, Pointer))
    return std::nullopt;
  if ((RemoveCopy || Copy) &&
      (!utilityAlgorithmWritableScalarPointer(Context, Output) ||
       !Context.hasSameType(Call->getArg(2)->getType(), Output) ||
       !utilityScalarDirectConversion(Context, Pointer->getPointeeType(),
                                      Output->getPointeeType())))
    return std::nullopt;
  if (Replacement) {
    const unsigned ValueIndex = ParameterCount - 1;
    const auto Value = Function->getParamDecl(ValueIndex)->getType();
    const auto &TemplateValue = Arguments->get(PredicateArgument + 1);
    if (!utilityAlgorithmWritableScalarPointer(Context, Output) ||
        (ReplaceCopy &&
         (!Context.hasSameType(Call->getArg(2)->getType(), Output) ||
          !utilityScalarDirectConversion(Context, Pointer->getPointeeType(),
                                         Output->getPointeeType()))) ||
        !Value->isLValueReferenceType() ||
        !Value->getPointeeType().isConstQualified() ||
        Value->getPointeeType().isVolatileQualified() ||
        !utilityScalar(Context, Value->getPointeeType()) ||
        !Context.hasSameUnqualifiedType(Call->getArg(ValueIndex)->getType(),
                                        Value->getPointeeType()) ||
        !utilityScalarDirectConversion(Context, Value->getPointeeType(),
                                       Output->getPointeeType()) ||
        TemplateValue.getKind() != TemplateArgument::Type ||
        !Context.hasSameType(
            Value, Context.getLValueReferenceType(
                       Context.getConstType(TemplateValue.getAsType()))))
      return std::nullopt;
  }
  // Use the actual instantiated definition's parameter identities. Its body is
  // not traversed as user code and no declaration lookup chooses the callback.
  auto Parameter = [&](const Expr *Expression, unsigned I) {
    while (const auto *Cast = dyn_cast_or_null<ImplicitCastExpr>(Expression)) {
      if ((Cast->getCastKind() != CK_LValueToRValue &&
           Cast->getCastKind() != CK_NoOp) ||
          !Context.hasSameUnqualifiedType(Cast->getType(),
                                          Cast->getSubExpr()->getType()))
        return false;
      Expression = Cast->getSubExpr();
    }
    const auto *Reference = dyn_cast_or_null<DeclRefExpr>(Expression);
    return Reference && Reference->isLValue() &&
           Reference->getDecl() == Definition->getParamDecl(I);
  };
  const CXXOperatorCallExpr *Invocation = nullptr;
  if (Remove) {
    Invocation = utilityAlgorithmRemovePredicate(S, SM, Function, Pointer,
                                                 Object, Context);
  } else if (PartitionPoint) {
    Invocation = utilityAlgorithmPartitionPointPredicate(S, SM, Function,
                                                         Pointer, Context);
  } else if (PartitionCopy) {
    Invocation =
        utilityAlgorithmPartitionCopyPredicate(S, SM, Function, Context);
  } else if (Partitioned) {
    Invocation = utilityAlgorithmPartitionPredicate(Function, Context);
  } else if (Copy) {
    Invocation = utilityAlgorithmCopyPredicate(S, SM, Function, Pointer, Output,
                                                Object, Context);
  } else if (RemoveCopy) {
    Invocation = utilityAlgorithmRemoveCopyPredicate(Function, Context);
  } else if (Replacement) {
    Invocation = utilityAlgorithmReplacementPredicate(Function, ReplaceCopy, Context);
  } else if (Composed) {
    Invocation = utilityAlgorithmComposedPredicate(S, SM, Function, Name,
                                                   Pointer, Object, Context);
  } else {
    Invocation = utilityAlgorithmSearchPredicate(Function, Name, Context);
  }
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(
      Invocation ? Invocation->getDirectCallee() : nullptr);
  const auto *MethodDefinition = Method ? Method->getDefinition() : nullptr;
  if (!Invocation || Invocation->getOperator() != OO_Call ||
      Invocation->getNumArgs() != 2 || !Invocation->isPRValue() ||
      !Invocation->getType()->isBooleanType() || !Method || !MethodDefinition ||
      Method->isStatic() || Method->isVolatile() ||
      Method->getOverloadedOperator() != OO_Call ||
      Method->getNumParams() != 1 ||
      !Method->getReturnType()->isBooleanType() ||
      Method->getParent()->getCanonicalDecl() != Record->getCanonicalDecl() ||
      !Invocation->getArg(0)->isLValue() ||
      (!Composed && !Remove && !PartitionPoint &&
       !Parameter(Invocation->getArg(0), PredicateIndex)) ||
      !functionalMemberReceiverValueCategory(Method, Invocation->getArg(0),
                                             false))
    return std::nullopt;
  std::optional<FunctionalOperationInfo> SDKOperation;
  if (SDKObject) {
    SDKOperation =
        approvedFunctionalOperationImpl(S, SM, Invocation, Context, false);
    if (!SDKOperation ||
        SDKOperation->Operation != FunctionalOperation::LogicalNot ||
        !SDKOperation->ResultType->isBooleanType() ||
        !Context.hasSameUnqualifiedType(
            Pointer->getPointeeType(),
            Method->getParamDecl(0)->getType().getNonReferenceType()))
      return std::nullopt;
  } else {
    if ((Method->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
         Method->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization) ||
        Method->getPrimaryTemplate() ||
        Method->getDescribedFunctionTemplate() || !ordinaryOperator(Method) ||
        !callableMethod(Method) || !S.owns(SM, MethodDefinition->getLocation()))
      return std::nullopt;
    for (const auto *D : Method->redecls())
      if (!S.owns(SM, D->getLocation()))
        return std::nullopt;
  }
  const auto ArgumentType = SDKOperation ? SDKOperation->LeftType
                                         : Method->getParamDecl(0)->getType();
  if (!utilityScalarDirectConversion(Context, Pointer->getPointeeType(),
                                     ArgumentType))
    return std::nullopt;
  if (!Composed && !Remove && !PartitionPoint) {
    const Expr *Argument = Invocation->getArg(1);
    while (const auto *Cast = dyn_cast<ImplicitCastExpr>(Argument)) {
      if (!utilityScalarDirectConversion(Context, Cast->getSubExpr()->getType(),
                                         Cast->getType()))
        return std::nullopt;
      Argument = Cast->getSubExpr();
    }
    const auto *Element = dyn_cast<UnaryOperator>(Argument);
    if (!Element || Element->getOpcode() != UO_Deref || !Element->isLValue() ||
        !Context.hasSameType(Element->getType(), Pointer->getPointeeType()) ||
        !Parameter(Element->getSubExpr(), 0))
      return std::nullopt;
  }
  return UtilityAlgorithmPredicateCall{
      Replace          ? UtilityOperation::AlgorithmReplaceIf
      : ReplaceCopy    ? UtilityOperation::AlgorithmReplaceCopyIf
      : RemoveCopy     ? UtilityOperation::AlgorithmRemoveCopyIf
      : Copy           ? UtilityOperation::AlgorithmCopyIf
      : Remove         ? UtilityOperation::AlgorithmRemoveIf
      : Partitioned    ? UtilityOperation::AlgorithmIsPartitioned
      : PartitionPoint ? UtilityOperation::AlgorithmPartitionPoint
      : PartitionCopy  ? UtilityOperation::AlgorithmPartitionCopy
      : All            ? UtilityOperation::AlgorithmAllOf
      : Any            ? UtilityOperation::AlgorithmAnyOf
      : Count          ? UtilityOperation::AlgorithmCountIf
      : Find           ? UtilityOperation::AlgorithmFindIf
      : FindNot        ? UtilityOperation::AlgorithmFindIfNot
                       : UtilityOperation::AlgorithmNoneOf,
      Function,
      Invocation,
      Method,
      Object,
      PredicateIndex,
      SDKOperation};
}

std::optional<UtilityOperation>
approvedUtilityOperation(const State &S, const SourceManager &SM,
                         const CallExpr *Call, const ASTContext &Context) {
  if (!Call || Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return std::nullopt;
  if (auto Predicate = approvedUtilityAlgorithmPredicateCall(S, SM, Call, Context))
    return Predicate->Operation;
  if (approvedUtilityTupleCatCall(S, SM, Call, Context))
    return UtilityOperation::TupleCat;
  if (approvedFunctionalReferenceFactoryCall(S, SM, Call, Context))
    return UtilityOperation::FunctionalReferenceFactory;
  if (approvedFunctionalReferenceAccessCall(S, SM, Call, Context))
    return UtilityOperation::FunctionalReferenceAccess;
  if (approvedFunctionalReferenceInvokeCall(S, SM, Call, Context))
    return UtilityOperation::FunctionalInvokeReference;
  if (approvedFunctionalMemberInvokeCall(S, SM, Call, Context))
    return UtilityOperation::FunctionalInvokeMember;
  if (approvedFunctionalInvokeObjectOperation(S, SM, Call, Context))
    return UtilityOperation::FunctionalInvokeObject;
  if (approvedFunctionalUserInvokeCall(S, SM, Call, Context))
    return UtilityOperation::FunctionalInvokeUserObject;
  const auto *Function = Call->getDirectCallee();
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function);
  const auto *OptionalObject = [&]() -> const Expr * {
    const Expr *Object = nullptr;
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call))
      Object = Operator->getNumArgs() ? Operator->getArg(0) : nullptr;
    else if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
      Object = Member->getImplicitObjectArgument();
    while (const auto *Cast = dyn_cast_or_null<ImplicitCastExpr>(Object)) {
      if (Cast->getCastKind() != CK_NoOp &&
          Cast->getCastKind() != CK_DerivedToBase &&
          Cast->getCastKind() != CK_UncheckedDerivedToBase)
        break;
      Object = Cast->getSubExpr();
    }
    return Object;
  }();
  const auto Optional = approvedUtilityOptionalRecord(
      S, SM,
      OptionalObject ? OptionalObject->getType()->getAsCXXRecordDecl()
                     : nullptr,
      Context);
  if (Method && Method->isStatic() && Method->getIdentifier() &&
      Method->getName() == "pointer_to" && !Method->isVariadic() &&
      Method->getNumParams() == 1 && Call->getNumArgs() == 1 &&
      Call->isPRValue() && Call->getArg(0)->isLValue() && Method->hasBody() &&
      Method->isInlined() && approvedStandardSDKDeclaration(S, SM, Method) &&
      cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                    "__memory/pointer_traits.h") &&
      approvedUtilityReference(S, SM, Call, Method)) {
    const auto *Traits =
        dyn_cast<ClassTemplateSpecializationDecl>(Method->getParent());
    const auto *Template = Traits ? Traits->getSpecializedTemplate() : nullptr;
    const auto *CanonicalTemplate =
        Template ? Template->getCanonicalDecl() : nullptr;
    const auto *Prototype = Method->getType()->getAs<FunctionProtoType>();
    const auto *Arguments = Traits ? &Traits->getTemplateArgs() : nullptr;
    const auto Parameter = Method->getParamDecl(0)->getType();
    const auto Result = Method->getReturnType();
    if (Traits && Template && CanonicalTemplate && Prototype &&
        Prototype->isNothrow() && Arguments && Arguments->size() == 1 &&
        Arguments->get(0).getKind() == TemplateArgument::Type &&
        !Traits->isUnion() && !Traits->isDependentContext() &&
        Traits->getName() == "pointer_traits" &&
        Template->getName() == "pointer_traits" &&
        approvedStandardSDKDeclaration(S, SM, Traits) &&
        approvedStandardSDKDeclaration(S, SM, Template) &&
        approvedStandardSDKDeclaration(S, SM, CanonicalTemplate) &&
        cstddefOrigin(S, SM, Traits->getLocation(), "libcxx",
                      "__memory/pointer_traits.h") &&
        cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                      "__memory/pointer_traits.h") &&
        cstddefOrigin(S, SM, CanonicalTemplate->getLocation(), "libcxx",
                      "__memory/pointer_traits.h") &&
        Parameter->isLValueReferenceType() &&
        utilityObjectPointer(Context, Result) &&
        Context.hasSameType(Arguments->get(0).getAsType(), Result) &&
        Context.hasSameType(Call->getType(), Result) &&
        Context.hasSameType(Parameter->getPointeeType(),
                            Result->getPointeeType()) &&
        Context.hasSameType(Call->getArg(0)->getType(),
                            Parameter->getPointeeType()))
      return UtilityOperation::MemoryPointerTo;
  }
  if (const auto Heap =
          approvedUtilityAllocatorHeapCall(S, SM, Call, true, Context))
    return Heap->Allocate ? UtilityOperation::MemoryAllocatorTraitsAllocate
                          : UtilityOperation::MemoryAllocatorTraitsDeallocate;
  if (const auto Heap =
          approvedUtilityAllocatorHeapCall(S, SM, Call, false, Context))
    return Heap->Allocate ? UtilityOperation::MemoryAllocatorAllocate
                          : UtilityOperation::MemoryAllocatorDeallocate;
  if (const auto Unique = approvedUtilityUniquePtrCall(S, SM, Call, Context)) {
    switch (Unique->Operation) {
    case UtilityUniquePtrOperation::Get:
      return UtilityOperation::MemoryUniquePtrGet;
    case UtilityUniquePtrOperation::GetDeleter:
      return UtilityOperation::MemoryUniquePtrGetDeleter;
    case UtilityUniquePtrOperation::Arrow:
      return UtilityOperation::MemoryUniquePtrArrow;
    case UtilityUniquePtrOperation::Dereference:
      return UtilityOperation::MemoryUniquePtrDereference;
    case UtilityUniquePtrOperation::Subscript:
      return UtilityOperation::MemoryUniquePtrSubscript;
    case UtilityUniquePtrOperation::Boolean:
      return UtilityOperation::MemoryUniquePtrBoolean;
    case UtilityUniquePtrOperation::Release:
      return UtilityOperation::MemoryUniquePtrRelease;
    case UtilityUniquePtrOperation::Reset:
      return UtilityOperation::MemoryUniquePtrReset;
    case UtilityUniquePtrOperation::MoveAssign:
      return UtilityOperation::MemoryUniquePtrMoveAssign;
    case UtilityUniquePtrOperation::ConvertingMoveAssign:
      return UtilityOperation::MemoryUniquePtrConvertingMoveAssign;
    case UtilityUniquePtrOperation::NullAssign:
      return UtilityOperation::MemoryUniquePtrNullAssign;
    case UtilityUniquePtrOperation::Swap:
      return UtilityOperation::MemoryUniquePtrMemberSwap;
    }
  }
  if (approvedUtilityMakeUniqueCall(S, SM, Call, Context))
    return UtilityOperation::MemoryMakeUnique;
  if (approvedUtilityDefaultDeleteCall(S, SM, Call, Context))
    return UtilityOperation::MemoryDefaultDelete;
  if (approvedUtilityAllocatorConstructCall(S, SM, Call, true, Context))
    return UtilityOperation::MemoryAllocatorTraitsConstruct;
  if (approvedUtilityAllocatorConstructCall(S, SM, Call, false, Context))
    return UtilityOperation::MemoryAllocatorConstruct;
  if (Method && Method->isStatic() && Method->getIdentifier() &&
      !Method->isVariadic() && Method->hasBody() && Method->isInlined() &&
      approvedStandardSDKDeclaration(S, SM, Method) &&
      cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                    "__memory/allocator_traits.h") &&
      approvedUtilityReference(S, SM, Call, Method)) {
    const auto Traits = approvedUtilityAllocatorTraitsRecord(
        S, SM, Method->getParent(), Context);
    const auto *Primary = Method->getPrimaryTemplate();
    const auto *Prototype = Method->getType()->getAs<FunctionProtoType>();
    if (Traits && Primary && Prototype &&
        Method->getParent()->getCanonicalDecl() ==
            Traits->Record->getCanonicalDecl() &&
        approvedStandardSDKDeclaration(S, SM, Primary) &&
        cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                      "__memory/allocator_traits.h") &&
        Method->getNumParams() == Call->getNumArgs() &&
        Method->getNumParams() >= 1) {
      const auto AllocatorType = Context.getRecordType(Traits->Allocator.Record);
      const auto AllocatorParameter = Method->getParamDecl(0)->getType();
      const auto *AllocatorArgument = Call->getArg(0);
      const bool ExactAllocatorReference =
          AllocatorParameter->isLValueReferenceType() &&
          !AllocatorParameter->getPointeeType().isVolatileQualified() &&
          AllocatorArgument->isLValue() &&
          Context.hasSameUnqualifiedType(
              AllocatorParameter->getPointeeType(), AllocatorType) &&
          Context.hasSameUnqualifiedType(AllocatorArgument->getType(),
                                         AllocatorType);
      if (ExactAllocatorReference && Method->getName() == "destroy" &&
          Method->getNumParams() == 2 && Call->getNumArgs() == 2 &&
          !AllocatorParameter->getPointeeType().isConstQualified() &&
          Method->getReturnType()->isVoidType() &&
          Context.hasSameType(Call->getType(), Method->getReturnType())) {
        const auto Pointer = Method->getParamDecl(1)->getType();
        if (utilityMemoryDestructiblePointer(S, SM, Context, Pointer) &&
            Context.hasSameType(Call->getArg(1)->getType(), Pointer))
          return UtilityOperation::MemoryAllocatorTraitsDestroy;
      }
      if (ExactAllocatorReference &&
          AllocatorParameter->getPointeeType().isConstQualified() &&
          Method->getName() == "max_size" && Method->getNumParams() == 1 &&
          Call->getNumArgs() == 1 && Call->isPRValue() &&
          Prototype->isNothrow() &&
          !Traits->Allocator.ElementType->isVoidType() &&
          !Traits->Allocator.ElementType->isIncompleteType() &&
          Context.hasSameType(Method->getReturnType(), Context.getSizeType()) &&
          Context.hasSameType(Call->getType(), Context.getSizeType()))
        return UtilityOperation::MemoryAllocatorTraitsMaxSize;
      if (ExactAllocatorReference &&
          AllocatorParameter->getPointeeType().isConstQualified() &&
          Method->getName() == "select_on_container_copy_construction" &&
          Method->getNumParams() == 1 && Call->getNumArgs() == 1 &&
          Call->isPRValue() &&
          Context.hasSameUnqualifiedType(Method->getReturnType(),
                                         AllocatorType) &&
          Context.hasSameUnqualifiedType(Call->getType(), AllocatorType))
        return UtilityOperation::MemoryAllocatorTraitsSelectOnCopy;
    }
  }
  if (const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(Call)) {
    const auto Allocator = approvedUtilityAllocatorRecord(
        S, SM, Method ? Method->getParent() : nullptr, Context);
    const auto *Reference = directMethodReference(Call);
    const auto *Prototype =
        Method ? Method->getType()->getAs<FunctionProtoType>() : nullptr;
    const auto *Object = MemberCall->getImplicitObjectArgument();
    if (Method && Allocator && Reference && Object &&
        Method->getIdentifier() && Method->getName() == "destroy" &&
        !Method->isStatic() && !Method->isConst() && !Method->isVariadic() &&
        Method->hasBody() && Method->isInlined() &&
        Method->getNumParams() == 1 && Call->getNumArgs() == 1 &&
        Method->getReturnType()->isVoidType() &&
        Context.hasSameType(Call->getType(), Method->getReturnType()) &&
        approvedStandardSDKDeclaration(S, SM, Method) &&
        cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                      "__memory/allocator.h") &&
        S.owns(SM, Reference->getExprLoc()) &&
        Context.hasSameUnqualifiedType(
            Object->getType(), Context.getRecordType(Allocator->Record))) {
      const auto Pointer = Method->getParamDecl(0)->getType();
      if (!Allocator->ElementType->isVoidType() &&
          Context.hasSameType(
              Pointer, Context.getPointerType(Allocator->ElementType)) &&
          Context.hasSameType(Call->getArg(0)->getType(), Pointer) &&
          utilityMemoryDestructiblePointer(S, SM, Context, Pointer))
        return UtilityOperation::MemoryAllocatorDestroy;
    }
    if (Method && Allocator && Reference && Prototype && Object &&
        Method->getIdentifier() && !Method->isStatic() && Method->isConst() &&
        !Method->isVariadic() && Method->hasBody() && Method->isInlined() &&
        Prototype->isNothrow() &&
        approvedStandardSDKDeclaration(S, SM, Method) &&
        cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                      "__memory/allocator.h") &&
        S.owns(SM, Reference->getExprLoc()) &&
        Context.hasSameUnqualifiedType(
            Object->getType(), Context.getRecordType(Allocator->Record))) {
      if (Method->getName() == "address" && Method->getNumParams() == 1 &&
          Call->getNumArgs() == 1 && Call->isPRValue() &&
          Call->getArg(0)->isLValue()) {
        const auto Parameter = Method->getParamDecl(0)->getType();
        const auto Result = Method->getReturnType();
        if (Parameter->isLValueReferenceType() &&
            utilityObjectPointer(Context, Result) &&
            Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                           Allocator->ElementType) &&
            Context.hasSameType(Parameter->getPointeeType(),
                                Result->getPointeeType()) &&
            Context.hasSameType(Call->getArg(0)->getType(),
                                Parameter->getPointeeType()) &&
            Context.hasSameType(Call->getType(), Result))
          return UtilityOperation::MemoryAllocatorAddress;
      }
      if (Method->getName() == "max_size" && !Method->getNumParams() &&
          !Call->getNumArgs() && Call->isPRValue() &&
          !Allocator->ElementType->isVoidType() &&
          !Allocator->ElementType->isIncompleteType() &&
          Context.hasSameType(Method->getReturnType(), Context.getSizeType()) &&
          Context.hasSameType(Call->getType(), Context.getSizeType()))
        return UtilityOperation::MemoryAllocatorMaxSize;
    }
  }
  if (Method && Optional) {
    const auto *Reference = directMethodReference(Call);
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
    const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(Call);
    const auto *ImplicitArrowReference = [&]() -> const DeclRefExpr * {
      if (Reference || !Operator || Operator->getOperator() != OO_Arrow)
        return nullptr;
      const Expr *Callee = Call->getCallee();
      while (true) {
        if (const auto *Paren = dyn_cast<ParenExpr>(Callee)) {
          Callee = Paren->getSubExpr();
          continue;
        }
        if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Callee);
            Cast && Cast->getCastKind() == CK_FunctionToPointerDecay) {
          Callee = Cast->getSubExpr();
          continue;
        }
        break;
      }
      const auto *Decl = dyn_cast<DeclRefExpr>(Callee);
      return Decl && Decl->getDecl()->getCanonicalDecl() ==
                         Method->getCanonicalDecl()
                 ? Decl
                 : nullptr;
    }();
    const auto *SelectedReference =
        Reference ? Reference : ImplicitArrowReference;
    const unsigned Offset = Operator ? 1 : 0;
    const auto OptionalType = Context.getRecordType(Optional->Record);
    auto Same = [&](QualType Left, QualType Right) {
      return !Left.isNull() && !Right.isNull() &&
             Context.hasSameType(Left, Right);
    };
    auto SameOptional = [&](QualType Type) {
      return !Type.isNull() &&
             Context.hasSameUnqualifiedType(Type, OptionalType);
    };
    const auto *Parent = Method->getParent()->getCanonicalDecl();
    const auto ReferenceLocation =
        SelectedReference && SelectedReference->getExprLoc().isValid()
            ? SelectedReference->getExprLoc()
            : Call->getExprLoc();
    if (!SelectedReference || (!Operator && !MemberCall) ||
        Method->isStatic() || Method->isVariadic() ||
        Call->getNumArgs() != Method->getNumParams() + Offset ||
        !SameOptional(OptionalObject->getType()) ||
        !approvedStandardSDKDeclaration(S, SM, Method) ||
        !cstddefOrigin(S, SM, Method->getLocation(), "libcxx", "optional") ||
        !S.owns(SM, ReferenceLocation) || !Method->hasBody())
      return std::nullopt;
    const llvm::StringRef Name = Method->getIdentifier()
                                     ? Method->getIdentifier()->getName()
                                     : llvm::StringRef();
    if (!Operator && !Method->getNumParams() && !Call->getNumArgs() &&
        Method->isConst() && Call->isPRValue() &&
        Method->getReturnType()->isBooleanType() &&
        Same(Call->getType(), Method->getReturnType())) {
      if (Name == "has_value" &&
          Parent == Optional->StorageBase->getCanonicalDecl())
        return UtilityOperation::OptionalHasValue;
      if (isa<CXXConversionDecl>(Method) &&
          Parent == Optional->Record->getCanonicalDecl())
        return UtilityOperation::OptionalHasValue;
    }
    if (!Operator && Name == "reset" && !Method->getNumParams() &&
        !Call->getNumArgs() && !Method->isConst() &&
        Parent == Optional->DestructBase->getCanonicalDecl() &&
        Method->getReturnType()->isVoidType() && Call->getType()->isVoidType())
      return UtilityOperation::OptionalReset;
    if (Method->getOverloadedOperator() == OO_Arrow &&
        !Method->getNumParams() && Call->getNumArgs() == Offset &&
        Call->isPRValue() && Parent == Optional->Record->getCanonicalDecl() &&
        Method->getReturnType()->isPointerType() &&
        Same(Call->getType(), Method->getReturnType()) &&
        Context.hasSameUnqualifiedType(
            Method->getReturnType()->getPointeeType(), Optional->ElementType))
      return UtilityOperation::OptionalArrow;
    if (Operator && Operator->getOperator() == OO_Star &&
        !Method->getNumParams() && Call->getNumArgs() == 1 &&
        Parent == Optional->Record->getCanonicalDecl() &&
        Method->getReturnType()->isReferenceType() &&
        Context.hasSameUnqualifiedType(
            Method->getReturnType()->getPointeeType(), Optional->ElementType) &&
        Context.hasSameUnqualifiedType(Call->getType(),
                                       Optional->ElementType) &&
        (Method->getReturnType()->isLValueReferenceType() ? Call->isLValue()
                                                          : Call->isXValue()))
      return UtilityOperation::OptionalDereference;
    if (!Operator && Name == "emplace" && Method->getNumParams() == 1 &&
        Call->getNumArgs() == 1 && !Method->isConst() &&
        Parent == Optional->Record->getCanonicalDecl() &&
        Method->getPrimaryTemplate() &&
        approvedStandardSDKDeclaration(S, SM, Method->getPrimaryTemplate()) &&
        cstddefOrigin(S, SM, Method->getPrimaryTemplate()->getLocation(),
                      "libcxx", "optional") &&
        Method->getReturnType()->isLValueReferenceType() && Call->isLValue() &&
        Context.hasSameUnqualifiedType(
            Method->getReturnType()->getPointeeType(), Optional->ElementType) &&
        Context.hasSameUnqualifiedType(Call->getType(),
                                       Optional->ElementType) &&
        Method->getParamDecl(0)->getType()->isReferenceType() &&
        Context.hasSameUnqualifiedType(
            Method->getParamDecl(0)->getType()->getPointeeType(),
            Call->getArg(0)->getType()) &&
        utilityTupleDirectConversion(S, SM, Context, Call->getArg(0)->getType(),
                                     Optional->ElementType))
      return UtilityOperation::OptionalEmplace;
    if (!Operator && Name == "value_or" && Method->getNumParams() == 1 &&
        Call->getNumArgs() == 1 && Call->isPRValue() &&
        Parent == Optional->Record->getCanonicalDecl() &&
        Method->getPrimaryTemplate() &&
        approvedStandardSDKDeclaration(S, SM, Method->getPrimaryTemplate()) &&
        cstddefOrigin(S, SM, Method->getPrimaryTemplate()->getLocation(),
                      "libcxx", "optional") &&
        Method->getReturnType()->isObjectType() &&
        Context.hasSameUnqualifiedType(Method->getReturnType(),
                                       Optional->ElementType) &&
        Context.hasSameUnqualifiedType(Call->getType(),
                                       Optional->ElementType) &&
        Method->getParamDecl(0)->getType()->isReferenceType() &&
        Context.hasSameUnqualifiedType(
            Method->getParamDecl(0)->getType()->getPointeeType(),
            Call->getArg(0)->getType()) &&
        utilityTupleDirectConversion(S, SM, Context, Call->getArg(0)->getType(),
                                     Optional->ElementType) &&
        ((Method->isConst() && Method->getRefQualifier() == RQ_LValue) ||
         (!Method->isConst() && Method->getRefQualifier() == RQ_RValue)))
      return UtilityOperation::OptionalValueOr;
    if (!Operator && Name == "swap" && Method->getNumParams() == 1 &&
        Call->getNumArgs() == 1 && !Method->isConst() &&
        Parent == Optional->Record->getCanonicalDecl() &&
        Method->getReturnType()->isVoidType() &&
        Call->getType()->isVoidType() &&
        Method->getParamDecl(0)->getType()->isLValueReferenceType() &&
        SameOptional(Method->getParamDecl(0)->getType()->getPointeeType()) &&
        SameOptional(Call->getArg(0)->getType()) &&
        utilityTupleAssignableValue(S, SM, Context, Optional->ElementType))
      if (approvedUtilityOptionalSwapBody(S, SM, Method, Context))
        return UtilityOperation::OptionalMemberSwap;
  }
  const auto InitializerList = approvedUtilityInitializerListRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  if (Method && InitializerList) {
    const auto *Reference = directMethodReference(Call);
    const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(Call);
    const auto *Object =
        MemberCall ? MemberCall->getImplicitObjectArgument() : nullptr;
    const auto ListType = Context.getRecordType(InitializerList->Record);
    if (!Reference || !Object || Method->isStatic() || Method->isVariadic() ||
        Method->getNumParams() || Call->getNumArgs() || !Method->isConst() ||
        !Method->hasBody() ||
        Method->getParent()->getCanonicalDecl() !=
            InitializerList->Record->getCanonicalDecl() ||
        !Context.hasSameUnqualifiedType(Object->getType(), ListType) ||
        !approvedStandardSDKDeclaration(S, SM, Method) ||
        !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                       "initializer_list") ||
        !S.owns(SM, Reference->getExprLoc()))
      return std::nullopt;
    const llvm::StringRef Name = Method->getIdentifier()
                                     ? Method->getIdentifier()->getName()
                                     : llvm::StringRef();
    if (Name == "size" && Call->isPRValue() && Method->isConstexpr() &&
        Context.hasSameType(Method->getReturnType(), Context.getSizeType()) &&
        Context.hasSameType(Call->getType(), Method->getReturnType()))
      return UtilityOperation::InitializerListSize;
    const auto Iterator =
        Context.getPointerType(InitializerList->ElementType.withConst());
    if ((Name == "begin" || Name == "end") && Call->isPRValue() &&
        Method->isConstexpr() &&
        Context.hasSameType(Method->getReturnType(), Iterator) &&
        Context.hasSameType(Call->getType(), Iterator))
      return Name == "begin" ? UtilityOperation::InitializerListBegin
                             : UtilityOperation::InitializerListEnd;
    return std::nullopt;
  }
  const auto Array = approvedUtilityArrayRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  if (Method && Array) {
    const auto *Reference = directMethodReference(Call);
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
    const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(Call);
    const Expr *Object = Operator && Call->getNumArgs()
                             ? Call->getArg(0)
                             : MemberCall
                                   ? MemberCall->getImplicitObjectArgument()
                                   : nullptr;
    const unsigned Offset = Operator ? 1 : 0;
    auto Same = [&](QualType Left, QualType Right) {
      return !Left.isNull() && !Right.isNull() &&
             Context.hasSameType(Left, Right);
    };
    auto SameArray = [&](QualType Type) {
      return !Type.isNull() &&
             Context.hasSameUnqualifiedType(
                 Type, Context.getRecordType(Array->Record));
    };
    auto ReferenceResult = [&] {
      auto Result = Method->getReturnType();
      return Result->isReferenceType() &&
             Context.hasSameUnqualifiedType(Result->getPointeeType(),
                                            Array->ElementType) &&
             Context.hasSameType(Call->getType(),
                                 Result->getPointeeType()) &&
             (Result->isLValueReferenceType() ? Call->isLValue()
                                              : Call->isXValue());
    };
    if (!Reference || !Object || Method->isStatic() || Method->isVariadic() ||
        Method->getParent()->getCanonicalDecl() !=
            Array->Record->getCanonicalDecl() ||
        Call->getNumArgs() != Method->getNumParams() + Offset ||
        !SameArray(Object->getType()) ||
        !approvedStandardSDKDeclaration(S, SM, Method) ||
        !cstddefOrigin(S, SM, Method->getLocation(), "libcxx", "array") ||
        !S.owns(SM, Reference->getExprLoc()) || !Method->hasBody())
      return std::nullopt;
    const llvm::StringRef Name = Method->getIdentifier()
                                     ? Method->getIdentifier()->getName()
                                     : llvm::StringRef();
    if (!Operator && !Method->getNumParams() && Call->getNumArgs() == 0) {
      if ((Name == "size" || Name == "max_size") &&
          Method->isConstexpr() && Call->isPRValue() &&
          Method->getReturnType()->isIntegralType(Context) &&
          Same(Call->getType(), Method->getReturnType()))
        return Name == "size" ? UtilityOperation::ArraySize
                              : UtilityOperation::ArrayMaxSize;
      if (Name == "empty" && Method->isConstexpr() && Call->isPRValue() &&
          Method->getReturnType()->isBooleanType() &&
          Same(Call->getType(), Method->getReturnType()))
        return UtilityOperation::ArrayEmpty;
      if ((Name == "data" || Name == "begin" || Name == "cbegin" ||
           Name == "end" || Name == "cend") &&
          Call->isPRValue() && Method->getReturnType()->isPointerType() &&
          Context.hasSameUnqualifiedType(
              Method->getReturnType()->getPointeeType(), Array->ElementType) &&
          Same(Call->getType(), Method->getReturnType())) {
        if (Name == "data")
          return UtilityOperation::ArrayData;
        if (Name == "begin" || Name == "cbegin")
          return UtilityOperation::ArrayBegin;
        return UtilityOperation::ArrayEnd;
      }
      if ((Name == "rbegin" || Name == "crbegin" || Name == "rend" ||
           Name == "crend") &&
          Call->isPRValue() && Same(Call->getType(), Method->getReturnType())) {
        const auto Reverse = approvedUtilityReverseIteratorRecord(
            S, SM, Method->getReturnType()->getAsCXXRecordDecl(), Context);
        if (Reverse &&
            Context.hasSameUnqualifiedType(
                Reverse->IteratorType->getPointeeType(), Array->ElementType) &&
            Reverse->IteratorType->getPointeeType().isConstQualified() ==
                Method->isConst())
          return Name == "rend" || Name == "crend"
                     ? UtilityOperation::ArrayREnd
                     : UtilityOperation::ArrayRBegin;
      }
      if (Array->Size && (Name == "front" || Name == "back") &&
          ReferenceResult())
        return Name == "front" ? UtilityOperation::ArrayFront
                               : UtilityOperation::ArrayBack;
    }
    if (Array->Size && Operator && Operator->getOperator() == OO_Subscript &&
        Method->getNumParams() == 1 && Call->getNumArgs() == 2 &&
        Method->getParamDecl(0)->getType()->isIntegralType(Context) &&
        Call->getArg(1)->getType()->isIntegralType(Context) &&
        ReferenceResult())
      return UtilityOperation::ArraySubscript;
    if (!Operator && Name == "at" && Method->getNumParams() == 1 &&
        Call->getNumArgs() == 1 &&
        Method->getParamDecl(0)->getType()->isIntegralType(Context) &&
        Call->getArg(0)->getType()->isIntegralType(Context) &&
        ReferenceResult()) {
      APValue Index;
      if (Call->getArg(0)->isCXX11ConstantExpr(Context, &Index) &&
          Index.isInt() && !Index.getInt().isNegative() &&
          Index.getInt().getLimitedValue(Array->Size) < Array->Size)
        return UtilityOperation::ArrayAt;
    }
    if (!Operator && Name == "fill" && Method->getNumParams() == 1 &&
        Call->getNumArgs() == 1 && Method->getReturnType()->isVoidType() &&
        !Object->getType().isConstQualified() &&
        (Array->Size
             ? utilityArrayTriviallyAssignable(Context, Array->ElementType)
             : !Array->ElementType.isConstQualified())) {
      auto Parameter = Method->getParamDecl(0)->getType();
      if (Parameter->isLValueReferenceType() &&
          Parameter->getPointeeType().isConstQualified() &&
          Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                         Array->ElementType) &&
          Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                         Array->ElementType))
        return UtilityOperation::ArrayFill;
    }
    if (!Operator && Name == "swap" && Method->getNumParams() == 1 &&
        Call->getNumArgs() == 1 && Method->getReturnType()->isVoidType() &&
        !Object->getType().isConstQualified() &&
        (Array->Size
             ? utilityArrayTriviallyAssignable(Context, Array->ElementType)
             : !Array->ElementType.isConstQualified())) {
      auto Parameter = Method->getParamDecl(0)->getType();
      if (Parameter->isLValueReferenceType() &&
          SameArray(Parameter->getPointeeType()) &&
          SameArray(Call->getArg(0)->getType()) &&
          approvedUtilityArraySwapBody(S, SM, Method, Context))
        return UtilityOperation::ArrayMemberSwap;
    }
  }
  const auto Reverse = approvedUtilityReverseIteratorRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  if (Method && Reverse) {
    const auto *Reference = directMethodReference(Call);
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
    const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(Call);
    const Expr *Object = Operator && Call->getNumArgs() ? Call->getArg(0)
                         : MemberCall ? MemberCall->getImplicitObjectArgument()
                                      : nullptr;
    const unsigned Offset = Operator ? 1 : 0;
    auto Same = [&](QualType Left, QualType Right) {
      return !Left.isNull() && !Right.isNull() &&
             Context.hasSameType(Left, Right);
    };
    auto SameReverse = [&](QualType Type) {
      return !Type.isNull() &&
             Context.hasSameUnqualifiedType(
                 Type, Context.getRecordType(Reverse->Record));
    };
    auto DifferenceParameter = [&] {
      return Method->getNumParams() == 1 &&
             Same(Method->getParamDecl(0)->getType(),
                  Context.getPointerDiffType()) &&
             Call->getNumArgs() == 1 + Offset &&
             Same(Call->getArg(Offset)->getType(),
                  Context.getPointerDiffType());
    };
    if (!Reference || !Object || Method->isStatic() || Method->isVariadic() ||
        Method->getParent()->getCanonicalDecl() !=
            Reverse->Record->getCanonicalDecl() ||
        Call->getNumArgs() != Method->getNumParams() + Offset ||
        !SameReverse(Object->getType()) ||
        !approvedStandardSDKDeclaration(S, SM, Method) ||
        !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                       "__iterator/reverse_iterator.h") ||
        !S.owns(SM, Reference->getExprLoc()) || !Method->hasBody())
      return std::nullopt;
    const llvm::StringRef Name = Method->getIdentifier()
                                     ? Method->getIdentifier()->getName()
                                     : llvm::StringRef();
    if (!Operator && !Method->getNumParams() && Call->getNumArgs() == 0 &&
        Method->isConst()) {
      if (Name == "base" && Call->isPRValue() &&
          Same(Method->getReturnType(), Reverse->IteratorType) &&
          Same(Call->getType(), Reverse->IteratorType))
        return UtilityOperation::ReverseBase;
      if (Method->getOverloadedOperator() == OO_Arrow && Call->isPRValue() &&
          Same(Method->getReturnType(), Reverse->IteratorType) &&
          Same(Call->getType(), Reverse->IteratorType))
        return UtilityOperation::ReverseArrow;
    }
    if (!Operator)
      return std::nullopt;
    const auto Kind = Operator->getOperator();
    const auto Pointee = Reverse->IteratorType->getPointeeType();
    if (Kind == OO_Star && !Method->getNumParams() && Method->isConst() &&
        Method->getReturnType()->isLValueReferenceType() && Call->isLValue() &&
        Same(Method->getReturnType()->getPointeeType(), Pointee) &&
        Same(Call->getType(), Pointee))
      return UtilityOperation::ReverseDereference;
    if (Kind == OO_Arrow && !Method->getNumParams() && Method->isConst() &&
        Call->isPRValue() &&
        Same(Method->getReturnType(), Reverse->IteratorType) &&
        Same(Call->getType(), Reverse->IteratorType))
      return UtilityOperation::ReverseArrow;
    if ((Kind == OO_PlusPlus || Kind == OO_MinusMinus) &&
        !Object->getType().isConstQualified()) {
      if (!Method->getNumParams() &&
          Method->getReturnType()->isLValueReferenceType() &&
          Call->isLValue() &&
          SameReverse(Method->getReturnType()->getPointeeType()) &&
          SameReverse(Call->getType())) {
        if (Kind == OO_PlusPlus)
          return UtilityOperation::ReversePreIncrement;
        return UtilityOperation::ReversePreDecrement;
      }
      if (Method->getNumParams() == 1 &&
          Method->getParamDecl(0)->getType()->isSpecificBuiltinType(
              BuiltinType::Int) &&
          Call->getNumArgs() == 2 &&
          Call->getArg(1)->getType()->isSpecificBuiltinType(BuiltinType::Int) &&
          Call->isPRValue() && SameReverse(Method->getReturnType()) &&
          SameReverse(Call->getType())) {
        if (Kind == OO_PlusPlus)
          return UtilityOperation::ReversePostIncrement;
        return UtilityOperation::ReversePostDecrement;
      }
    }
    if ((Kind == OO_Plus || Kind == OO_Minus) && Method->isConst() &&
        DifferenceParameter() && Call->isPRValue() &&
        SameReverse(Method->getReturnType()) && SameReverse(Call->getType()))
      return Kind == OO_Plus ? UtilityOperation::ReverseAdd
                             : UtilityOperation::ReverseSubtract;
    if ((Kind == OO_PlusEqual || Kind == OO_MinusEqual) &&
        !Object->getType().isConstQualified() && DifferenceParameter() &&
        Method->getReturnType()->isLValueReferenceType() && Call->isLValue() &&
        SameReverse(Method->getReturnType()->getPointeeType()) &&
        SameReverse(Call->getType()))
      return Kind == OO_PlusEqual ? UtilityOperation::ReverseAddAssign
                                  : UtilityOperation::ReverseSubtractAssign;
    if (Kind == OO_Subscript && Method->isConst() && DifferenceParameter() &&
        Method->getReturnType()->isLValueReferenceType() && Call->isLValue() &&
        Same(Method->getReturnType()->getPointeeType(), Pointee) &&
        Same(Call->getType(), Pointee))
      return UtilityOperation::ReverseSubscript;
  }
  if (const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(Call)) {
    const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function);
    const auto *Reference = directMethodReference(Call);
    auto Pair = approvedUtilityPairRecord(
        S, SM, Method ? Method->getParent() : nullptr, Context);
    bool ReferencePair = false;
    if (!Pair) {
      Pair = approvedUtilityReferencePairRecord(
          S, SM, Method ? Method->getParent() : nullptr, Context);
      ReferencePair = Pair.has_value();
    }
    bool MixedReferencePair = false;
    if (!Pair) {
      Pair = approvedUtilityMixedReferencePairRecord(
          S, SM, Method ? Method->getParent() : nullptr, Context);
      MixedReferencePair = Pair.has_value();
    }
    auto FirstType = Pair ? Pair->First->getType() : QualType();
    auto SecondType = Pair ? Pair->Second->getType() : QualType();
    if (!FirstType.isNull() && FirstType->isReferenceType())
      FirstType = FirstType->getPointeeType();
    if (!SecondType.isNull() && SecondType->isReferenceType())
      SecondType = SecondType->getPointeeType();
    if (Method && Reference && Pair && Method->getIdentifier() &&
        Method->getName() == "swap" && !Method->isStatic() &&
        !Method->isVariadic() && Method->getNumParams() == 1 &&
        Call->getNumArgs() == 1 && Function->getReturnType()->isVoidType() &&
        Method->hasBody() && approvedStandardSDKDeclaration(S, SM, Method) &&
        cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                      "__utility/pair.h") &&
        S.owns(SM, Reference->getExprLoc()) &&
        (ReferencePair || MixedReferencePair ||
         (utilityPairValue(S, SM, Context, FirstType) &&
          utilityPairValue(S, SM, Context, SecondType))) &&
        utilityPairAssignableValue(S, SM, Context, FirstType) &&
        utilityPairAssignableValue(S, SM, Context, SecondType) &&
        Context.hasSameUnqualifiedType(
            MemberCall->getImplicitObjectArgument()->getType(),
            Context.getRecordType(Pair->Record)) &&
        Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                       Context.getRecordType(Pair->Record)))
      if (approvedUtilityPairSwapBody(S, SM, Method, Context))
        return UtilityOperation::PairMemberSwap;
  }
  if (const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(Call)) {
    const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function);
    const auto *Reference = directMethodReference(Call);
    auto Tuple = approvedUtilityTupleRecord(
        S, SM, Method ? Method->getParent() : nullptr, Context);
    if (!Tuple)
      Tuple = approvedUtilityReferenceTupleRecord(
          S, SM, Method ? Method->getParent() : nullptr, Context);
    if (!Tuple)
      Tuple = approvedUtilityMixedReferenceTupleRecord(
          S, SM, Method ? Method->getParent() : nullptr, Context);
    bool Mutable = Tuple.has_value();
    if (Tuple)
      for (const auto *Element : Tuple->Elements) {
        auto ElementType = Element->getType();
        if (ElementType->isReferenceType())
          ElementType = ElementType->getPointeeType();
        Mutable &= utilityTupleAssignableValue(S, SM, Context, ElementType);
      }
    if (Method && Reference && Tuple && Mutable && Method->getIdentifier() &&
        Method->getName() == "swap" && !Method->isStatic() &&
        !Method->isVariadic() && Method->getNumParams() == 1 &&
        Call->getNumArgs() == 1 && Function->getReturnType()->isVoidType() &&
        Method->hasBody() && approvedStandardSDKDeclaration(S, SM, Method) &&
        cstddefOrigin(S, SM, Method->getLocation(), "libcxx", "tuple") &&
        S.owns(SM, Reference->getExprLoc()) &&
        Context.hasSameUnqualifiedType(
            MemberCall->getImplicitObjectArgument()->getType(),
            Context.getRecordType(Tuple->Record)) &&
        Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                       Context.getRecordType(Tuple->Record)) &&
        approvedUtilityTupleSwapBody(S, SM, Method, Context))
      return UtilityOperation::TupleMemberSwap;
  }
  const auto *Primary = Function ? Function->getPrimaryTemplate() : nullptr;
  const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
  if (!Function || !Primary || !Pattern || Function->isVariadic() ||
      !Pattern->hasBody() || !approvedStandardSDKDeclaration(S, SM, Function) ||
      !approvedStandardSDKDeclaration(S, SM, Primary) ||
      !approvedUtilityReference(S, SM, Call, Function) ||
      Call->getNumArgs() != Function->getNumParams())
    return std::nullopt;
  auto Origin = S.sdkFile(SM, Primary->getLocation());
  if (!Origin || Origin->Root != "libcxx")
    return std::nullopt;
  auto Same = [&](QualType Left, QualType Right) {
    return !Left.isNull() && !Right.isNull() &&
           Context.hasSameType(Left, Right);
  };
  const llvm::StringRef Name = Function->getIdentifier()
                                   ? Function->getIdentifier()->getName()
                                   : llvm::StringRef();
  if (Origin->Path == "__memory/unique_ptr.h" && Name == "swap" &&
      Function->isInlined() && Call->getNumArgs() == 2 &&
      Function->getNumParams() == 2 &&
      Function->getReturnType()->isVoidType() &&
      Call->getType()->isVoidType()) {
    const auto *Prototype = Function->getType()->getAs<FunctionProtoType>();
    const auto Left = approvedUtilityUniquePtrRecord(
        S, SM, Call->getArg(0)->getType()->getAsCXXRecordDecl(), Context);
    const auto Right = approvedUtilityUniquePtrRecord(
        S, SM, Call->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
    const auto LeftParameter = Function->getParamDecl(0)->getType();
    const auto RightParameter = Function->getParamDecl(1)->getType();
    if (Prototype && Prototype->isNothrow() && Left && Right &&
        Left->Record->getCanonicalDecl() == Right->Record->getCanonicalDecl() &&
        LeftParameter->isLValueReferenceType() &&
        RightParameter->isLValueReferenceType() &&
        !LeftParameter->getPointeeType().isConstQualified() &&
        !RightParameter->getPointeeType().isConstQualified() &&
        Context.hasSameUnqualifiedType(LeftParameter->getPointeeType(),
                                       Call->getArg(0)->getType()) &&
        Context.hasSameUnqualifiedType(RightParameter->getPointeeType(),
                                       Call->getArg(1)->getType()) &&
        Call->getArg(0)->isLValue() && Call->getArg(1)->isLValue() &&
        !Call->getArg(0)->getType().isConstQualified() &&
        !Call->getArg(1)->getType().isConstQualified())
      return UtilityOperation::MemoryUniquePtrSwap;
  }
  if (Origin->Path == "__memory/unique_ptr.h" && Call->getNumArgs() == 2 &&
      Function->getNumParams() == 2 && Call->isPRValue() &&
      Function->getReturnType()->isBooleanType() &&
      Same(Call->getType(), Function->getReturnType())) {
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
    const auto Kind = Operator ? Operator->getOperator() : OO_None;
    const auto Left = approvedUtilityUniquePtrRecord(
        S, SM, Call->getArg(0)->getType()->getAsCXXRecordDecl(), Context);
    const auto Right = approvedUtilityUniquePtrRecord(
        S, SM, Call->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
    const bool LeftNull = Call->getArg(0)->getType()->isNullPtrType();
    const bool RightNull = Call->getArg(1)->getType()->isNullPtrType();
    std::optional<UtilityOperation> Comparison;
    if (Operator) {
      switch (Kind) {
      case OO_EqualEqual:
        Comparison = UtilityOperation::MemoryUniquePtrEqual;
        break;
      case OO_ExclaimEqual:
        Comparison = UtilityOperation::MemoryUniquePtrNotEqual;
        break;
      case OO_Less:
        Comparison = UtilityOperation::MemoryUniquePtrLess;
        break;
      case OO_Greater:
        Comparison = UtilityOperation::MemoryUniquePtrGreater;
        break;
      case OO_LessEqual:
        Comparison = UtilityOperation::MemoryUniquePtrLessEqual;
        break;
      case OO_GreaterEqual:
        Comparison = UtilityOperation::MemoryUniquePtrGreaterEqual;
        break;
      default:
        break;
      }
    }
    auto ParameterMatches =
        [&](unsigned Index, const std::optional<UtilityUniquePtrRecord> &Unique,
            bool Null) {
          const auto Parameter = Function->getParamDecl(Index)->getType();
          if (Null)
            return Parameter->isNullPtrType() &&
                   Call->getArg(Index)->getType()->isNullPtrType();
          return Unique && Parameter->isLValueReferenceType() &&
                 Parameter->getPointeeType().isConstQualified() &&
                 !Parameter->getPointeeType().isVolatileQualified() &&
                 Call->getArg(Index)->isLValue() &&
                 Context.hasSameUnqualifiedType(
                     Parameter->getPointeeType(),
                     Call->getArg(Index)->getType()) &&
                 Context.hasSameUnqualifiedType(
                     Call->getArg(Index)->getType(),
                     Context.getRecordType(Unique->Record));
        };
    const bool Ordered =
        Comparison && *Comparison != UtilityOperation::MemoryUniquePtrEqual &&
        *Comparison != UtilityOperation::MemoryUniquePtrNotEqual;
    if (Comparison && (Left || LeftNull) && (Right || RightNull) &&
        !(LeftNull && RightNull) &&
        (!Ordered || ((!Left || !Left->ElementType->isIncompleteType()) &&
                      (!Right || !Right->ElementType->isIncompleteType()))) &&
        (!Left || !Right ||
         Left->Deleter.Array == Right->Deleter.Array) &&
        (!Left || !Right ||
         Left->Record->getCanonicalDecl() ==
             Right->Record->getCanonicalDecl() ||
         utilityPointerConversion(Context, Left->PointerType,
                                  Right->PointerType) ||
         utilityPointerConversion(Context, Right->PointerType,
                                  Left->PointerType)) &&
        ParameterMatches(0, Left, LeftNull) &&
        ParameterMatches(1, Right, RightNull))
      return Comparison;
  }
  if (Origin->Path == "__memory/allocator.h" && Call->getNumArgs() == 2 &&
      Function->getNumParams() == 2 && Function->isInlined() &&
      Call->isPRValue() && Function->getReturnType()->isBooleanType() &&
      Same(Call->getType(), Function->getReturnType())) {
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
    const auto Left = approvedUtilityAllocatorRecord(
        S, SM, Call->getArg(0)->getType()->getAsCXXRecordDecl(), Context);
    const auto Right = approvedUtilityAllocatorRecord(
        S, SM, Call->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
    auto Parameter = [&](unsigned Index,
                         const UtilityAllocatorRecord &Allocator) {
      const auto Type = Function->getParamDecl(Index)->getType();
      return Type->isLValueReferenceType() &&
             Type->getPointeeType().isConstQualified() &&
             Context.hasSameUnqualifiedType(
                 Type->getPointeeType(),
                 Context.getRecordType(Allocator.Record));
    };
    if (Operator && Left && Right && Parameter(0, *Left) &&
        Parameter(1, *Right)) {
      if (Function->getOverloadedOperator() == OO_EqualEqual &&
          Operator->getOperator() == OO_EqualEqual)
        return UtilityOperation::MemoryAllocatorEqual;
      if (Function->getOverloadedOperator() == OO_ExclaimEqual &&
          Operator->getOperator() == OO_ExclaimEqual)
        return UtilityOperation::MemoryAllocatorNotEqual;
    }
  }
  if (Origin->Path == "__new/launder.h" && Name == "launder" &&
      Function->isInlined() && Function->isConstexpr() &&
      Call->getNumArgs() == 1 && Function->getNumParams() == 1 &&
      Call->isPRValue()) {
    const auto *Prototype = Function->getType()->getAs<FunctionProtoType>();
    const auto Parameter = Function->getParamDecl(0)->getType();
    const auto Result = Function->getReturnType();
    if (Prototype && Prototype->isNothrow() &&
        utilityObjectPointer(Context, Parameter) && Same(Parameter, Result) &&
        Same(Call->getType(), Result) &&
        Same(Call->getArg(0)->getType(), Parameter))
      return UtilityOperation::NewLaunder;
  }
  if (Origin->Path == "__memory/addressof.h" && Name == "addressof" &&
      Function->isInlined() && Function->isConstexpr() &&
      Call->getNumArgs() == 1 && Function->getNumParams() == 1 &&
      Call->isPRValue() && Call->getArg(0)->isLValue()) {
    const auto *Prototype = Function->getType()->getAs<FunctionProtoType>();
    const auto Parameter = Function->getParamDecl(0)->getType();
    const auto Result = Function->getReturnType();
    if (Prototype && Prototype->isNothrow() &&
        Parameter->isLValueReferenceType() &&
        utilityObjectPointer(Context, Result) &&
        Same(Call->getType(), Result) &&
        Same(Call->getArg(0)->getType(), Parameter->getPointeeType()) &&
        Same(Parameter->getPointeeType(), Result->getPointeeType()))
      return UtilityOperation::MemoryAddressof;
  }
  auto OptionalFor = [&](QualType Type) {
    return approvedUtilityOptionalRecord(
        S, SM, Type.isNull() ? nullptr : Type->getAsCXXRecordDecl(), Context);
  };
  auto TupleFor = [&](QualType Type) {
    const auto *Record =
        Type.isNull() ? nullptr : Type->getAsCXXRecordDecl();
    auto Tuple = approvedUtilityTupleRecord(S, SM, Record, Context);
    if (!Tuple)
      Tuple = approvedUtilityReferenceTupleRecord(S, SM, Record, Context);
    return Tuple;
  };
  auto OptionalParameter = [&](unsigned Index,
                               const UtilityOptionalRecord &Optional,
                               bool RequireConst) {
    if (Index >= Function->getNumParams() || Index >= Call->getNumArgs())
      return false;
    const auto Parameter = Function->getParamDecl(Index)->getType();
    if (!Parameter->isLValueReferenceType() ||
        Parameter->getPointeeType().isConstQualified() != RequireConst ||
        Parameter->getPointeeType().isVolatileQualified())
      return false;
    const auto OptionalType = Context.getRecordType(Optional.Record);
    return Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                          OptionalType) &&
           Context.hasSameUnqualifiedType(Call->getArg(Index)->getType(),
                                          OptionalType);
  };
  auto NulloptParameter = [&](unsigned Index) {
    if (Index >= Function->getNumParams() || Index >= Call->getNumArgs())
      return false;
    const auto Parameter = Function->getParamDecl(Index)->getType();
    return !Parameter->isReferenceType() &&
           approvedNulloptRecord(S, SM, Parameter, Context) &&
           Context.hasSameUnqualifiedType(Call->getArg(Index)->getType(),
                                          Parameter) &&
           approvedUtilityNulloptExpression(S, SM, Call->getArg(Index),
                                            Context);
  };
  auto ComparableParameter = [&](unsigned Index, QualType Element,
                                 bool RequireOrderedObject) {
    if (Index >= Function->getNumParams() || Index >= Call->getNumArgs())
      return false;
    const auto Parameter = Function->getParamDecl(Index)->getType();
    return Parameter->isLValueReferenceType() &&
           Parameter->getPointeeType().isConstQualified() &&
           !Parameter->getPointeeType().isVolatileQualified() &&
           Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                          Call->getArg(Index)->getType()) &&
           utilityComparableValue(S, SM, Context, Element,
                                  Parameter->getPointeeType(),
                                  RequireOrderedObject);
  };
  if (Origin->Path == "optional" && Call->getNumArgs() == 2 &&
      Function->getNumParams() == 2 && Call->isPRValue() &&
      Function->getReturnType()->isBooleanType() &&
      Same(Call->getType(), Function->getReturnType())) {
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
    std::optional<UtilityOperation> Comparison;
    if (Operator) {
      switch (Operator->getOperator()) {
      case OO_EqualEqual:
        Comparison = UtilityOperation::OptionalEqual;
        break;
      case OO_ExclaimEqual:
        Comparison = UtilityOperation::OptionalNotEqual;
        break;
      case OO_Less:
        Comparison = UtilityOperation::OptionalLess;
        break;
      case OO_Greater:
        Comparison = UtilityOperation::OptionalGreater;
        break;
      case OO_LessEqual:
        Comparison = UtilityOperation::OptionalLessEqual;
        break;
      case OO_GreaterEqual:
        Comparison = UtilityOperation::OptionalGreaterEqual;
        break;
      default:
        break;
      }
    }
    if (Comparison) {
      const bool RequireOrderedObject =
          *Comparison != UtilityOperation::OptionalEqual &&
          *Comparison != UtilityOperation::OptionalNotEqual;
      const auto Left = OptionalFor(Call->getArg(0)->getType());
      const auto Right = OptionalFor(Call->getArg(1)->getType());
      const bool LeftNullopt = NulloptParameter(0);
      const bool RightNullopt = NulloptParameter(1);
      if (Left && Right && OptionalParameter(0, *Left, true) &&
          OptionalParameter(1, *Right, true) &&
          utilityComparableValue(S, SM, Context, Left->ElementType,
                                 Right->ElementType, RequireOrderedObject))
        return Comparison;
      if (Left && RightNullopt && OptionalParameter(0, *Left, true))
        return Comparison;
      if (LeftNullopt && Right && OptionalParameter(1, *Right, true))
        return Comparison;
      if (Left && OptionalParameter(0, *Left, true) &&
          ComparableParameter(1, Left->ElementType, RequireOrderedObject))
        return Comparison;
      if (Right &&
          ComparableParameter(0, Right->ElementType, RequireOrderedObject) &&
          OptionalParameter(1, *Right, true))
        return Comparison;
    }
  }
  if (Origin->Path == "optional" && Name == "swap" && Call->getNumArgs() == 2 &&
      Function->getNumParams() == 2 &&
      Function->getReturnType()->isVoidType() &&
      Call->getType()->isVoidType()) {
    const auto Left = OptionalFor(Call->getArg(0)->getType());
    const auto Right = OptionalFor(Call->getArg(1)->getType());
    if (Left && Right &&
        Left->Record->getCanonicalDecl() == Right->Record->getCanonicalDecl() &&
        OptionalParameter(0, *Left, false) &&
        OptionalParameter(1, *Right, false) &&
        utilityTupleAssignableValue(S, SM, Context, Left->ElementType))
      if (approvedUtilityPairElementSwap(
              S, SM, Function, Context.getRecordType(Left->Record), Context))
        return UtilityOperation::OptionalSwap;
  }
  if (Origin->Path == "optional" && Name == "make_optional" &&
      Call->getNumArgs() == Function->getNumParams() &&
      Call->getNumArgs() <= 1 && Call->isPRValue() &&
      Same(Call->getType(), Function->getReturnType())) {
    const auto Result = OptionalFor(Call->getType());
    if (Result && !Call->getNumArgs())
      return UtilityOperation::MakeOptional;
    if (Result && Call->getNumArgs() == 1) {
      const auto Parameter = Function->getParamDecl(0)->getType();
      if (Parameter->isReferenceType() &&
          Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                         Call->getArg(0)->getType()) &&
          utilityTupleDirectConversion(
              S, SM, Context, Call->getArg(0)->getType(), Result->ElementType))
        return UtilityOperation::MakeOptional;
    }
  }
  if (Call->getNumArgs() == 1 && Function->getNumParams() == 1) {
    const auto List = approvedUtilityInitializerListRecord(
        S, SM, Call->getArg(0)->getType()->getAsCXXRecordDecl(), Context);
    const auto Parameter = Function->getParamDecl(0)->getType();
    const auto ListType =
        List ? Context.getRecordType(List->Record) : QualType();
    const bool ParameterMatches =
        List &&
        (Parameter->isLValueReferenceType()
             ? Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                              ListType)
             : Context.hasSameUnqualifiedType(Parameter, ListType)) &&
        Context.hasSameUnqualifiedType(Call->getArg(0)->getType(), ListType);
    if (ParameterMatches && Call->isPRValue()) {
      const auto Iterator =
          Context.getPointerType(List->ElementType.withConst());
      if (((Origin->Path == "initializer_list" &&
            (Name == "begin" || Name == "end")) ||
           (Origin->Path == "__iterator/access.h" &&
            (Name == "begin" || Name == "cbegin" || Name == "end" ||
             Name == "cend"))) &&
          Context.hasSameType(Function->getReturnType(), Iterator) &&
          Context.hasSameType(Call->getType(), Iterator))
        return Name == "end" || Name == "cend"
                   ? UtilityOperation::InitializerListEnd
                   : UtilityOperation::InitializerListBegin;
      if (Origin->Path == "__iterator/data.h" && Name == "data" &&
          Context.hasSameType(Function->getReturnType(), Iterator) &&
          Context.hasSameType(Call->getType(), Iterator))
        return UtilityOperation::InitializerListBegin;
      if (Origin->Path == "__iterator/size.h" && Name == "size" &&
          Context.hasSameType(Function->getReturnType(),
                              Context.getSizeType()) &&
          Context.hasSameType(Call->getType(), Context.getSizeType()))
        return UtilityOperation::InitializerListSize;
      if (Origin->Path == "__iterator/empty.h" && Name == "empty" &&
          Function->getReturnType()->isBooleanType() &&
          Context.hasSameType(Call->getType(), Function->getReturnType()))
        return UtilityOperation::InitializerListEmpty;
      if (Origin->Path == "__iterator/reverse_access.h" &&
          (Name == "rbegin" || Name == "crbegin" || Name == "rend" ||
           Name == "crend")) {
        const auto Reverse = approvedUtilityReverseIteratorRecord(
            S, SM, Function->getReturnType()->getAsCXXRecordDecl(), Context);
        if (Reverse && Context.hasSameType(Reverse->IteratorType, Iterator) &&
            Context.hasSameType(Call->getType(), Function->getReturnType()))
          return Name == "rend" || Name == "crend"
                     ? UtilityOperation::InitializerListREnd
                     : UtilityOperation::InitializerListRBegin;
      }
    }
  }
  const bool ApprovedNonInlineOperation =
      (Origin->Path == "__numeric/iota.h" && Name == "iota") ||
      (Origin->Path == "__numeric/accumulate.h" && Name == "accumulate") ||
      (Origin->Path == "__numeric/inner_product.h" &&
       Name == "inner_product") ||
      (Origin->Path == "__numeric/partial_sum.h" && Name == "partial_sum") ||
      (Origin->Path == "__numeric/adjacent_difference.h" &&
       Name == "adjacent_difference") ||
      (Origin->Path == "__numeric/reduce.h" && Name == "reduce") ||
      (Origin->Path == "__numeric/transform_reduce.h" &&
       Name == "transform_reduce") ||
      (Origin->Path == "__numeric/inclusive_scan.h" &&
       Name == "inclusive_scan") ||
      (Origin->Path == "__numeric/exclusive_scan.h" &&
       Name == "exclusive_scan") ||
      (Origin->Path == "__numeric/transform_inclusive_scan.h" &&
       Name == "transform_inclusive_scan") ||
      (Origin->Path == "__numeric/transform_exclusive_scan.h" &&
       Name == "transform_exclusive_scan") ||
      (Origin->Path == "__numeric/gcd_lcm.h" &&
       (Name == "gcd" || Name == "lcm")) ||
      (Origin->Path == "__memory/construct_at.h" &&
       (Name == "destroy_at" || Name == "destroy" || Name == "destroy_n")) ||
      (Origin->Path == "__memory/uninitialized_algorithms.h" &&
       (Name == "uninitialized_copy" || Name == "uninitialized_copy_n" ||
        Name == "uninitialized_fill" || Name == "uninitialized_fill_n" ||
        Name == "uninitialized_default_construct" ||
        Name == "uninitialized_default_construct_n" ||
        Name == "uninitialized_value_construct" ||
        Name == "uninitialized_value_construct_n" ||
        Name == "uninitialized_move" || Name == "uninitialized_move_n")) ||
      (Origin->Path == "__algorithm/remove.h" && Name == "remove") ||
      (Origin->Path == "__algorithm/remove_if.h" && Name == "remove_if") ||
      (Origin->Path == "__algorithm/unique.h" && Name == "unique") ||
      (Origin->Path == "__algorithm/for_each.h" && Name == "for_each") ||
      (Origin->Path == "__algorithm/is_partitioned.h" &&
       Name == "is_partitioned") ||
      (Origin->Path == "__algorithm/stable_partition.h" &&
       Name == "stable_partition") ||
      (Origin->Path == "__algorithm/partition_copy.h" &&
       Name == "partition_copy") ||
      (Origin->Path == "__algorithm/partition_point.h" &&
       Name == "partition_point") ||
      (Origin->Path == "__algorithm/is_permutation.h" &&
       Name == "is_permutation") ||
      (Origin->Path == "__algorithm/equal_range.h" && Name == "equal_range") ||
      (Origin->Path == "__algorithm/minmax_element.h" &&
       Name == "minmax_element") ||
      (Origin->Path == "__algorithm/stable_sort.h" && Name == "stable_sort") ||
      (Origin->Path == "__algorithm/inplace_merge.h" &&
       Name == "inplace_merge") ||
      (Origin->Path == "__algorithm/set_union.h" && Name == "set_union") ||
      (Origin->Path == "__algorithm/set_symmetric_difference.h" &&
       Name == "set_symmetric_difference") ||
      (Origin->Path == "__functional/invoke.h" && Name == "invoke");
  if (!Function->isInlined() && !ApprovedNonInlineOperation)
    return std::nullopt;
  auto ReverseFor = [&](QualType Type) {
    return approvedUtilityReverseIteratorRecord(
        S, SM, Type.isNull() ? nullptr : Type->getAsCXXRecordDecl(), Context);
  };
  auto ReverseParameter = [&](unsigned Index,
                              const UtilityReverseIteratorRecord &Record) {
    if (Index >= Function->getNumParams())
      return false;
    auto Parameter = Function->getParamDecl(Index)->getType();
    return Parameter->isLValueReferenceType() &&
           Parameter->getPointeeType().isConstQualified() &&
           Context.hasSameUnqualifiedType(Parameter->getPointeeType(),
                                          Context.getRecordType(Record.Record));
  };
  auto AlgorithmPointerParameter = [&](unsigned Index) {
    if (Index >= Function->getNumParams() || Index >= Call->getNumArgs())
      return false;
    auto Parameter = Function->getParamDecl(Index)->getType();
    return utilityAlgorithmScalarPointer(Context, Parameter) &&
           Same(Call->getArg(Index)->getType(), Parameter);
  };
  auto AlgorithmTransferParameters = [&](unsigned InputIndex,
                                         unsigned OutputIndex) {
    if (!AlgorithmPointerParameter(InputIndex) ||
        !AlgorithmPointerParameter(OutputIndex))
      return false;
    auto Input = Function->getParamDecl(InputIndex)->getType();
    auto Output = Function->getParamDecl(OutputIndex)->getType();
    return utilityAlgorithmWritableScalarPointer(Context, Output) &&
           utilityScalarDirectConversion(Context, Input->getPointeeType(),
                                         Output->getPointeeType());
  };
  auto MemoryDestructionPointerParameter = [&](unsigned Index) {
    if (Index >= Function->getNumParams() || Index >= Call->getNumArgs())
      return false;
    auto Parameter = Function->getParamDecl(Index)->getType();
    return utilityMemoryDestructiblePointer(S, SM, Context, Parameter) &&
           Same(Call->getArg(Index)->getType(), Parameter);
  };
  auto MemoryConstructionPointerParameter = [&](unsigned Index) {
    if (Index >= Function->getNumParams() || Index >= Call->getNumArgs())
      return false;
    auto Parameter = Function->getParamDecl(Index)->getType();
    return utilityObjectPointer(Context, Parameter) &&
           Same(Call->getArg(Index)->getType(), Parameter);
  };
  auto MemoryDefaultPointerParameter = [&](unsigned Index) {
    if (Index >= Function->getNumParams() || Index >= Call->getNumArgs())
      return false;
    auto Parameter = Function->getParamDecl(Index)->getType();
    return utilityMemoryDefaultPointer(S, SM, Context, Parameter) &&
           Same(Call->getArg(Index)->getType(), Parameter);
  };
  auto AlgorithmEqualityPointerParameter = [&](unsigned Index) {
    if (Index >= Function->getNumParams() || Index >= Call->getNumArgs())
      return false;
    auto Parameter = Function->getParamDecl(Index)->getType();
    return utilityAlgorithmEqualityPointer(Context, Parameter) &&
           !utilityEnumHasSourceOperator(
               S, SM, Context, Parameter->getPointeeType(), OO_EqualEqual) &&
           Same(Call->getArg(Index)->getType(), Parameter);
  };
  auto AlgorithmEqualityParameters = [&](unsigned LeftIndex,
                                         unsigned RightIndex) {
    if (!AlgorithmEqualityPointerParameter(LeftIndex) ||
        !AlgorithmEqualityPointerParameter(RightIndex))
      return false;
    return utilityScalarComparisonType(
               Context,
               Function->getParamDecl(LeftIndex)->getType()->getPointeeType(),
               Function->getParamDecl(RightIndex)->getType()->getPointeeType(),
               false)
        .has_value();
  };
  auto SameAlgorithmElement = [&](QualType Left, QualType Right) {
    return utilityAlgorithmScalarPointer(Context, Left) &&
           utilityAlgorithmScalarPointer(Context, Right) &&
           Context.hasSameUnqualifiedType(Left->getPointeeType(),
                                          Right->getPointeeType());
  };
  auto SameMemoryConstructionElement = [&](QualType Left, QualType Right) {
    return utilityObjectPointer(Context, Left) &&
           utilityObjectPointer(Context, Right) &&
           Context.hasSameUnqualifiedType(Left->getPointeeType(),
                                          Right->getPointeeType());
  };
  auto AlgorithmOrderedPointerParameter = [&](unsigned Index) {
    if (!AlgorithmPointerParameter(Index))
      return false;
    auto Element = Function->getParamDecl(Index)
                       ->getType()
                       ->getPointeeType()
                       .getUnqualifiedType();
    return utilityScalarComparisonType(Context, Element, Element, true)
               .has_value() &&
           !utilityEnumHasSourceOperator(S, SM, Context, Element, OO_Less);
  };
  auto AlgorithmOrderedParameters = [&](unsigned LeftIndex,
                                        unsigned RightIndex) {
    if (!AlgorithmOrderedPointerParameter(LeftIndex) ||
        !AlgorithmOrderedPointerParameter(RightIndex))
      return false;
    return utilityScalarComparisonType(
               Context,
               Function->getParamDecl(LeftIndex)->getType()->getPointeeType(),
               Function->getParamDecl(RightIndex)->getType()->getPointeeType(),
               true)
        .has_value();
  };
  auto AlgorithmScalarValueParameter = [&](unsigned ValueIndex,
                                           unsigned IteratorIndex) {
    if (ValueIndex >= Function->getNumParams() ||
        ValueIndex >= Call->getNumArgs() ||
        IteratorIndex >= Function->getNumParams())
      return false;
    auto Iterator = Function->getParamDecl(IteratorIndex)->getType();
    auto Value = Function->getParamDecl(ValueIndex)->getType();
    return utilityAlgorithmScalarPointer(Context, Iterator) &&
           Value->isLValueReferenceType() &&
           Value->getPointeeType().isConstQualified() &&
           !Value->getPointeeType().isVolatileQualified() &&
           utilityScalar(Context, Value->getPointeeType()) &&
           Context.hasSameUnqualifiedType(Call->getArg(ValueIndex)->getType(),
                                          Value->getPointeeType());
  };
  auto AlgorithmEqualityValueParameter = [&](unsigned ValueIndex,
                                             unsigned IteratorIndex) {
    if (!AlgorithmScalarValueParameter(ValueIndex, IteratorIndex))
      return false;
    auto Iterator = Function->getParamDecl(IteratorIndex)->getType();
    auto Value = Function->getParamDecl(ValueIndex)->getType();
    return utilityScalarComparisonType(Context, Iterator->getPointeeType(),
                                       Value->getPointeeType(), false)
        .has_value();
  };
  auto AlgorithmOrderedValueParameter = [&](unsigned ValueIndex,
                                            unsigned IteratorIndex) {
    if (!AlgorithmEqualityValueParameter(ValueIndex, IteratorIndex))
      return false;
    auto Iterator = Function->getParamDecl(IteratorIndex)->getType();
    auto Value = Function->getParamDecl(ValueIndex)->getType();
    return utilityScalarComparisonType(Context, Iterator->getPointeeType(),
                                       Value->getPointeeType(), true)
        .has_value();
  };
  auto AlgorithmTransferValueParameter = [&](unsigned ValueIndex,
                                             unsigned OutputIndex) {
    if (ValueIndex >= Function->getNumParams() ||
        ValueIndex >= Call->getNumArgs() ||
        OutputIndex >= Function->getNumParams())
      return false;
    auto Value = Function->getParamDecl(ValueIndex)->getType();
    auto Output = Function->getParamDecl(OutputIndex)->getType();
    return Value->isLValueReferenceType() &&
           Value->getPointeeType().isConstQualified() &&
           !Value->getPointeeType().isVolatileQualified() &&
           utilityScalar(Context, Value->getPointeeType()) &&
           Context.hasSameUnqualifiedType(Call->getArg(ValueIndex)->getType(),
                                          Value->getPointeeType()) &&
           utilityAlgorithmWritableScalarPointer(Context, Output) &&
           utilityScalarDirectConversion(Context, Value->getPointeeType(),
                                         Output->getPointeeType());
  };
  auto MemoryConstructionValueParameter = [&](unsigned ValueIndex,
                                              unsigned IteratorIndex) {
    if (ValueIndex >= Function->getNumParams() ||
        ValueIndex >= Call->getNumArgs() ||
        IteratorIndex >= Function->getNumParams())
      return false;
    auto Iterator = Function->getParamDecl(IteratorIndex)->getType();
    auto Value = Function->getParamDecl(ValueIndex)->getType();
    return utilityObjectPointer(Context, Iterator) &&
           Value->isLValueReferenceType() &&
           Value->getPointeeType().isConstQualified() &&
           !Value->getPointeeType().isVolatileQualified() &&
           Value->getPointeeType()->isObjectType() &&
           Context.hasSameUnqualifiedType(Value->getPointeeType(),
                                          Iterator->getPointeeType()) &&
           Context.hasSameUnqualifiedType(Call->getArg(ValueIndex)->getType(),
                                          Value->getPointeeType());
  };
  auto AlgorithmCountParameter = [&](unsigned Index) {
    if (Index >= Function->getNumParams() || Index >= Call->getNumArgs())
      return false;
    auto Count = Function->getParamDecl(Index)->getType();
    if (!Same(Call->getArg(Index)->getType(), Count))
      return false;
    if (const auto *Enumeration = Count->getAs<EnumType>()) {
      if (Enumeration->getDecl()->isScoped())
        return false;
      Count = Enumeration->getDecl()->getPromotionType();
    } else if (Context.isPromotableIntegerType(Count)) {
      Count = Context.getPromotedIntegerType(Count);
    } else if (!Count->isIntegerType()) {
      return false;
    }
    return !Count.isNull() && Count->isIntegerType() &&
           Context.getTypeSize(Count) <= 64;
  };
  if (Origin->Path == "__memory/construct_at.h") {
    if (Name == "destroy_at" && Call->getNumArgs() == 1 &&
        Function->getNumParams() == 1 && MemoryDestructionPointerParameter(0) &&
        Function->getReturnType()->isVoidType() &&
        Same(Call->getType(), Function->getReturnType()))
      return UtilityOperation::MemoryDestroyAt;
    if (Name == "destroy" && Call->getNumArgs() == 2 &&
        Function->getNumParams() == 2 && MemoryDestructionPointerParameter(0) &&
        MemoryDestructionPointerParameter(1) &&
        Same(Function->getParamDecl(0)->getType(),
             Function->getParamDecl(1)->getType()) &&
        Function->getReturnType()->isVoidType() &&
        Same(Call->getType(), Function->getReturnType()))
      return UtilityOperation::MemoryDestroy;
    if (Name == "destroy_n" && Call->getNumArgs() == 2 &&
        Function->getNumParams() == 2 && Call->isPRValue() &&
        MemoryDestructionPointerParameter(0) && AlgorithmCountParameter(1) &&
        Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
        Same(Call->getType(), Function->getReturnType()))
      return UtilityOperation::MemoryDestroyN;
  }
  if (Origin->Path == "__memory/uninitialized_algorithms.h") {
    const bool WritableFirst = Function->getNumParams() != 0 &&
                               MemoryConstructionPointerParameter(0) &&
                               !Function->getParamDecl(0)
                                    ->getType()
                                    ->getPointeeType()
                                    .isConstQualified();
    const bool WritableDefaultFirst =
        Function->getNumParams() != 0 && MemoryDefaultPointerParameter(0) &&
        utilityMemoryWritableDefaultPointer(
            S, SM, Context, Function->getParamDecl(0)->getType());
    const auto SourceConstructible = [&](UtilityOperation Operation,
                                         unsigned OutputIndex) {
      const auto Element =
          Function->getParamDecl(OutputIndex)->getType()->getPointeeType();
      return utilityMemoryTrivialValue(S, SM, Context, Element) ||
             approvedUtilityMemorySourceConstructor(S, SM, Call, Operation,
                                                    Context);
    };
    if ((Name == "uninitialized_copy" || Name == "uninitialized_move") &&
        Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
        Call->isPRValue() && MemoryConstructionPointerParameter(0) &&
        MemoryConstructionPointerParameter(1) &&
        MemoryConstructionPointerParameter(2) &&
        Same(Function->getParamDecl(0)->getType(),
             Function->getParamDecl(1)->getType()) &&
        SameMemoryConstructionElement(Function->getParamDecl(0)->getType(),
                                      Function->getParamDecl(2)->getType()) &&
        !Function->getParamDecl(2)
             ->getType()
             ->getPointeeType()
             .isConstQualified() &&
        Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
        Same(Call->getType(), Function->getReturnType())) {
      const auto Operation = Name == "uninitialized_copy"
                                 ? UtilityOperation::MemoryUninitializedCopy
                                 : UtilityOperation::MemoryUninitializedMove;
      if (SourceConstructible(Operation, 2))
        return Operation;
    }
    if (Name == "uninitialized_copy_n" && Call->getNumArgs() == 3 &&
        Function->getNumParams() == 3 && Call->isPRValue() &&
        MemoryConstructionPointerParameter(0) && AlgorithmCountParameter(1) &&
        MemoryConstructionPointerParameter(2) &&
        SameMemoryConstructionElement(Function->getParamDecl(0)->getType(),
                                      Function->getParamDecl(2)->getType()) &&
        !Function->getParamDecl(2)
             ->getType()
             ->getPointeeType()
             .isConstQualified() &&
        Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
        Same(Call->getType(), Function->getReturnType()) &&
        SourceConstructible(UtilityOperation::MemoryUninitializedCopyN, 2))
      return UtilityOperation::MemoryUninitializedCopyN;
    if (Name == "uninitialized_fill" && Call->getNumArgs() == 3 &&
        Function->getNumParams() == 3 && WritableFirst &&
        MemoryConstructionPointerParameter(1) &&
        Same(Function->getParamDecl(0)->getType(),
             Function->getParamDecl(1)->getType()) &&
        MemoryConstructionValueParameter(2, 0) &&
        Function->getReturnType()->isVoidType() &&
        Same(Call->getType(), Function->getReturnType()) &&
        SourceConstructible(UtilityOperation::MemoryUninitializedFill, 0))
      return UtilityOperation::MemoryUninitializedFill;
    if (Name == "uninitialized_fill_n" && Call->getNumArgs() == 3 &&
        Function->getNumParams() == 3 && Call->isPRValue() && WritableFirst &&
        AlgorithmCountParameter(1) && MemoryConstructionValueParameter(2, 0) &&
        Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
        Same(Call->getType(), Function->getReturnType()) &&
        SourceConstructible(UtilityOperation::MemoryUninitializedFillN, 0))
      return UtilityOperation::MemoryUninitializedFillN;
    if ((Name == "uninitialized_default_construct" ||
         Name == "uninitialized_value_construct") &&
        Call->getNumArgs() == 2 && Function->getNumParams() == 2 &&
        WritableDefaultFirst && MemoryDefaultPointerParameter(1) &&
        Same(Function->getParamDecl(0)->getType(),
             Function->getParamDecl(1)->getType()) &&
        Function->getReturnType()->isVoidType() &&
        Same(Call->getType(), Function->getReturnType()))
      return Name == "uninitialized_default_construct"
                 ? UtilityOperation::MemoryUninitializedDefaultConstruct
                 : UtilityOperation::MemoryUninitializedValueConstruct;
    if ((Name == "uninitialized_default_construct_n" ||
         Name == "uninitialized_value_construct_n") &&
        Call->getNumArgs() == 2 && Function->getNumParams() == 2 &&
        Call->isPRValue() && WritableDefaultFirst &&
        AlgorithmCountParameter(1) &&
        Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
        Same(Call->getType(), Function->getReturnType()))
      return Name == "uninitialized_default_construct_n"
                 ? UtilityOperation::MemoryUninitializedDefaultConstructN
                 : UtilityOperation::MemoryUninitializedValueConstructN;
    if (Name == "uninitialized_move_n" && Call->getNumArgs() == 3 &&
        Function->getNumParams() == 3 && Call->isPRValue() &&
        MemoryConstructionPointerParameter(0) && AlgorithmCountParameter(1) &&
        MemoryConstructionPointerParameter(2) &&
        SameMemoryConstructionElement(Function->getParamDecl(0)->getType(),
                                      Function->getParamDecl(2)->getType()) &&
        !Function->getParamDecl(2)
             ->getType()
             ->getPointeeType()
             .isConstQualified() &&
        Same(Call->getType(), Function->getReturnType()) &&
        SourceConstructible(UtilityOperation::MemoryUninitializedMoveN, 2)) {
      auto Pair = approvedUtilityPairRecord(
          S, SM, Function->getReturnType()->getAsCXXRecordDecl(), Context);
      if (Pair &&
          Same(Pair->First->getType(), Function->getParamDecl(0)->getType()) &&
          Same(Pair->Second->getType(), Function->getParamDecl(2)->getType()))
        return UtilityOperation::MemoryUninitializedMoveN;
    }
  }
  auto AlgorithmReferenceParameter = [&](unsigned Index) {
    if (Index >= Function->getNumParams() || Index >= Call->getNumArgs())
      return false;
    auto Parameter = Function->getParamDecl(Index)->getType();
    if (!Parameter->isLValueReferenceType() ||
        !Parameter->getPointeeType().isConstQualified() ||
        Parameter->getPointeeType().isVolatileQualified() ||
        !utilityScalar(Context, Parameter->getPointeeType()) ||
        !Context.hasSameUnqualifiedType(Call->getArg(Index)->getType(),
                                        Parameter->getPointeeType()))
      return false;
    return true;
  };
  auto AlgorithmOrderedReferenceParameter = [&](unsigned Index) {
    if (!AlgorithmReferenceParameter(Index))
      return false;
    auto Parameter = Function->getParamDecl(Index)->getType();
    auto Element = Parameter->getPointeeType().getUnqualifiedType();
    return utilityScalarComparisonType(Context, Element, Element, true)
               .has_value() &&
           !utilityEnumHasSourceOperator(S, SM, Context, Element, OO_Less);
  };
  auto AlgorithmCallbackPrototype =
      [&](unsigned CallbackIndex) -> const FunctionProtoType * {
    if (CallbackIndex >= Function->getNumParams() ||
        CallbackIndex >= Call->getNumArgs())
      return nullptr;
    auto Callback = Function->getParamDecl(CallbackIndex)->getType();
    if (!Callback->isFunctionPointerType() ||
        !Same(Call->getArg(CallbackIndex)->getType(), Callback))
      return nullptr;
    const auto *Prototype =
        Callback->getPointeeType()->getAs<FunctionProtoType>();
    return Prototype && !Prototype->isVariadic() ? Prototype : nullptr;
  };
  auto AlgorithmUnaryPredicateParameter = [&](unsigned PredicateIndex,
                                              unsigned IteratorIndex) {
    if (PredicateIndex >= Function->getNumParams() ||
        PredicateIndex >= Call->getNumArgs() ||
        IteratorIndex >= Function->getNumParams())
      return false;
    auto Iterator = Function->getParamDecl(IteratorIndex)->getType();
    if (!utilityAlgorithmScalarPointer(Context, Iterator))
      return false;
    const auto *Prototype = AlgorithmCallbackPrototype(PredicateIndex);
    if (!Prototype || Prototype->getNumParams() != 1 ||
        !Prototype->getReturnType()->isBooleanType())
      return false;
    auto Parameter = Prototype->getParamType(0);
    return utilityScalarDirectConversion(Context, Iterator->getPointeeType(),
                                         Parameter);
  };
  auto AlgorithmBinaryPredicateParameter = [&](unsigned PredicateIndex,
                                               unsigned LeftIteratorIndex,
                                               unsigned RightIteratorIndex) {
    if (PredicateIndex >= Function->getNumParams() ||
        PredicateIndex >= Call->getNumArgs() ||
        LeftIteratorIndex >= Function->getNumParams() ||
        RightIteratorIndex >= Function->getNumParams())
      return false;
    auto Left = Function->getParamDecl(LeftIteratorIndex)->getType();
    auto Right = Function->getParamDecl(RightIteratorIndex)->getType();
    if (!utilityAlgorithmScalarPointer(Context, Left) ||
        !utilityAlgorithmScalarPointer(Context, Right))
      return false;
    const auto *Prototype = AlgorithmCallbackPrototype(PredicateIndex);
    if (!Prototype || Prototype->getNumParams() != 2 ||
        !Prototype->getReturnType()->isBooleanType())
      return false;
    auto LeftParameter = Prototype->getParamType(0);
    auto RightParameter = Prototype->getParamType(1);
    return utilityScalarDirectConversion(Context, Left->getPointeeType(),
                                         LeftParameter) &&
           utilityScalarDirectConversion(Context, Right->getPointeeType(),
                                         RightParameter);
  };
  auto AlgorithmBinaryPredicateValueParameter = [&](unsigned PredicateIndex,
                                                    unsigned IteratorIndex,
                                                    unsigned ValueIndex) {
    if (PredicateIndex >= Function->getNumParams() ||
        PredicateIndex >= Call->getNumArgs() ||
        IteratorIndex >= Function->getNumParams() ||
        ValueIndex >= Function->getNumParams() ||
        ValueIndex >= Call->getNumArgs())
      return false;
    auto Iterator = Function->getParamDecl(IteratorIndex)->getType();
    auto Value = Function->getParamDecl(ValueIndex)->getType();
    if (!utilityAlgorithmScalarPointer(Context, Iterator) ||
        !Value->isLValueReferenceType() ||
        !Value->getPointeeType().isConstQualified() ||
        Value->getPointeeType().isVolatileQualified() ||
        !utilityScalar(Context, Value->getPointeeType()) ||
        !Context.hasSameUnqualifiedType(Call->getArg(ValueIndex)->getType(),
                                        Value->getPointeeType()))
      return false;
    const auto *Prototype = AlgorithmCallbackPrototype(PredicateIndex);
    if (!Prototype || Prototype->getNumParams() != 2 ||
        !Prototype->getReturnType()->isBooleanType())
      return false;
    auto LeftParameter = Prototype->getParamType(0);
    auto RightParameter = Prototype->getParamType(1);
    return utilityScalarDirectConversion(Context, Iterator->getPointeeType(),
                                         LeftParameter) &&
           utilityScalarDirectConversion(Context, Value->getPointeeType(),
                                         RightParameter);
  };
  auto AlgorithmBinaryPredicateReversedValueParameter =
      [&](unsigned PredicateIndex, unsigned IteratorIndex,
          unsigned ValueIndex) {
        if (PredicateIndex >= Function->getNumParams() ||
            PredicateIndex >= Call->getNumArgs() ||
            IteratorIndex >= Function->getNumParams() ||
            ValueIndex >= Function->getNumParams() ||
            ValueIndex >= Call->getNumArgs())
          return false;
        auto Iterator = Function->getParamDecl(IteratorIndex)->getType();
        auto Value = Function->getParamDecl(ValueIndex)->getType();
        if (!utilityAlgorithmScalarPointer(Context, Iterator) ||
            !Value->isLValueReferenceType() ||
            !Value->getPointeeType().isConstQualified() ||
            Value->getPointeeType().isVolatileQualified() ||
            !utilityScalar(Context, Value->getPointeeType()) ||
            !Context.hasSameUnqualifiedType(Call->getArg(ValueIndex)->getType(),
                                            Value->getPointeeType()))
          return false;
        const auto *Prototype = AlgorithmCallbackPrototype(PredicateIndex);
        if (!Prototype || Prototype->getNumParams() != 2 ||
            !Prototype->getReturnType()->isBooleanType())
          return false;
        return utilityScalarDirectConversion(Context, Value->getPointeeType(),
                                             Prototype->getParamType(0)) &&
               utilityScalarDirectConversion(Context,
                                             Iterator->getPointeeType(),
                                             Prototype->getParamType(1));
      };
  auto AlgorithmBinaryPredicateReferenceParameter =
      [&](unsigned PredicateIndex, unsigned ReferenceIndex) {
        if (!AlgorithmReferenceParameter(ReferenceIndex))
          return false;
        const auto *Prototype = AlgorithmCallbackPrototype(PredicateIndex);
        if (!Prototype || Prototype->getNumParams() != 2 ||
            !Prototype->getReturnType()->isBooleanType())
          return false;
        auto Reference =
            Function->getParamDecl(ReferenceIndex)->getType()->getPointeeType();
        auto LeftParameter = Prototype->getParamType(0);
        auto RightParameter = Prototype->getParamType(1);
        return utilityScalarDirectConversion(Context, Reference,
                                             LeftParameter) &&
               utilityScalarDirectConversion(Context, Reference,
                                             RightParameter);
      };
  auto NumericArithmetic = [&](QualType Type, bool IncludeNarrow = false) {
    if (Type.isNull() || Type->isReferenceType())
      return false;
    Type = Type.getUnqualifiedType();
    return (!Type->isEnumeralType() && !Type->isBooleanType() &&
            Type->isIntegerType() &&
            (IncludeNarrow || !Context.isPromotableIntegerType(Type)) &&
            Context.getTypeSize(Type) <= 64) ||
           Type->isSpecificBuiltinType(BuiltinType::Float) ||
           Type->isSpecificBuiltinType(BuiltinType::Double);
  };
  auto NumericPointerParameter = [&](unsigned Index, bool Writable,
                                     bool IncludeNarrow = false) {
    if (!AlgorithmPointerParameter(Index))
      return false;
    auto Pointer = Function->getParamDecl(Index)->getType();
    return NumericArithmetic(Pointer->getPointeeType(), IncludeNarrow) &&
           (!Writable ||
            utilityAlgorithmWritableScalarPointer(Context, Pointer));
  };
  auto NumericCommonElements = [&](unsigned LeftIndex, unsigned RightIndex,
                                   bool IncludeNarrow = false) {
    if (!NumericPointerParameter(LeftIndex, false, IncludeNarrow) ||
        !NumericPointerParameter(RightIndex, false, IncludeNarrow))
      return false;
    return utilityScalarComparisonType(
               Context,
               Function->getParamDecl(LeftIndex)->getType()->getPointeeType(),
               Function->getParamDecl(RightIndex)->getType()->getPointeeType(),
               false)
        .has_value();
  };
  auto NumericValueParameter = [&](unsigned ValueIndex, unsigned IteratorIndex,
                                   bool IncludeNarrow = false) {
    if (ValueIndex >= Function->getNumParams() ||
        ValueIndex >= Call->getNumArgs() ||
        IteratorIndex >= Function->getNumParams())
      return false;
    auto Iterator = Function->getParamDecl(IteratorIndex)->getType();
    auto Value = Function->getParamDecl(ValueIndex)->getType();
    return utilityAlgorithmScalarPointer(Context, Iterator) &&
           NumericArithmetic(Iterator->getPointeeType(), IncludeNarrow) &&
           NumericArithmetic(Value, IncludeNarrow) &&
           Same(Call->getArg(ValueIndex)->getType(), Value) &&
           Context.hasSameUnqualifiedType(Value, Iterator->getPointeeType());
  };
  auto NumericReductionValueParameter = [&](unsigned ValueIndex,
                                            unsigned IteratorIndex,
                                            bool IncludeNarrow = false) {
    if (ValueIndex >= Function->getNumParams() ||
        ValueIndex >= Call->getNumArgs() ||
        IteratorIndex >= Function->getNumParams())
      return false;
    auto Iterator = Function->getParamDecl(IteratorIndex)->getType();
    auto Value = Function->getParamDecl(ValueIndex)->getType();
    return utilityAlgorithmScalarPointer(Context, Iterator) &&
           NumericArithmetic(Iterator->getPointeeType(), IncludeNarrow) &&
           NumericArithmetic(Value, IncludeNarrow) &&
           Same(Call->getArg(ValueIndex)->getType(), Value) &&
           utilityScalarComparisonType(Context, Value,
                                       Iterator->getPointeeType(), false)
               .has_value();
  };
  auto NumericIotaValueParameter = [&](unsigned ValueIndex,
                                       unsigned IteratorIndex) {
    if (ValueIndex >= Function->getNumParams() ||
        ValueIndex >= Call->getNumArgs() ||
        IteratorIndex >= Function->getNumParams())
      return false;
    auto Iterator = Function->getParamDecl(IteratorIndex)->getType();
    auto Value = Function->getParamDecl(ValueIndex)->getType();
    return utilityAlgorithmWritableScalarPointer(Context, Iterator) &&
           NumericArithmetic(Value, true) &&
           Same(Call->getArg(ValueIndex)->getType(), Value) &&
           utilityScalarDirectConversion(Context, Value,
                                         Iterator->getPointeeType());
  };
  auto NumericValueOutputParameter = [&](unsigned ValueIndex,
                                         unsigned OutputIndex,
                                         bool IncludeNarrow = false) {
    if (ValueIndex >= Function->getNumParams() ||
        OutputIndex >= Function->getNumParams())
      return false;
    auto Value = Function->getParamDecl(ValueIndex)->getType();
    auto Output = Function->getParamDecl(OutputIndex)->getType();
    return NumericArithmetic(Value, IncludeNarrow) &&
           utilityAlgorithmWritableScalarPointer(Context, Output) &&
           utilityScalarDirectConversion(Context, Value,
                                         Output->getPointeeType());
  };
  auto NumericIntegerValueParameter = [&](unsigned Index) {
    if (Index >= Function->getNumParams() || Index >= Call->getNumArgs())
      return false;
    auto Parameter = Function->getParamDecl(Index)->getType();
    return !Parameter.isNull() && !Parameter->isReferenceType() &&
           !Parameter->isEnumeralType() && Parameter->isIntegerType() &&
           !Parameter->isBooleanType() &&
           Context.getTypeSize(Parameter) <= 64 &&
           Same(Call->getArg(Index)->getType(), Parameter);
  };
  auto NumericCallback = [&](unsigned Index, QualType Element, unsigned Arity,
                             bool IncludeNarrow = false) {
    const auto *Prototype = AlgorithmCallbackPrototype(Index);
    if (!Prototype || Prototype->getNumParams() != Arity)
      return false;
    Element = Element.getUnqualifiedType();
    if (!NumericArithmetic(Element, IncludeNarrow) ||
        !utilityScalarDirectConversion(Context, Prototype->getReturnType(),
                                       Element))
      return false;
    for (QualType Parameter : Prototype->param_types())
      if (Parameter->isReferenceType() ||
          !utilityScalarDirectConversion(Context, Element, Parameter))
        return false;
    return true;
  };
  auto NumericUnaryCallback = [&](unsigned Index, QualType Element,
                                  bool IncludeNarrow = false) {
    return NumericCallback(Index, Element, 1, IncludeNarrow);
  };
  auto NumericBinaryCallback = [&](unsigned Index, QualType Element,
                                   bool IncludeNarrow = false) {
    return NumericCallback(Index, Element, 2, IncludeNarrow);
  };
  auto NumericBinaryTransformCallback = [&](unsigned Index, QualType Result,
                                            QualType Left, QualType Right,
                                            bool IncludeNarrow = false) {
    const auto *Prototype = AlgorithmCallbackPrototype(Index);
    if (!Prototype || Prototype->getNumParams() != 2)
      return false;
    Result = Result.getUnqualifiedType();
    Left = Left.getUnqualifiedType();
    Right = Right.getUnqualifiedType();
    return NumericArithmetic(Result, IncludeNarrow) &&
           NumericArithmetic(Left, IncludeNarrow) &&
           NumericArithmetic(Right, IncludeNarrow) &&
           utilityScalarDirectConversion(Context, Prototype->getReturnType(),
                                         Result) &&
           !Prototype->getParamType(0)->isReferenceType() &&
           !Prototype->getParamType(1)->isReferenceType() &&
           utilityScalarDirectConversion(Context, Left,
                                         Prototype->getParamType(0)) &&
           utilityScalarDirectConversion(Context, Right,
                                         Prototype->getParamType(1));
  };
  // Empty, directly value-initialized arithmetic function objects have no
  // state or construction effects. Typed operators explicitly narrow their
  // result to the exact operand type before the next operation.
  auto NumericArithmeticFunctionalPair = [&](bool Unary) {
    if (Call->getNumArgs() != (Unary ? 5u : 6u))
      return false;
    for (unsigned Offset = 0; Offset != 2; ++Offset) {
      const unsigned Index = (Unary ? 3u : 4u) + Offset;
      const bool UnaryTransform = Unary && Offset;
      const auto Object = Function->getParamDecl(Index)->getType();
      const auto *Record = Object->getAsCXXRecordDecl();
      const auto *Specialization =
          dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
      const auto OperatorName =
          Specialization ? Specialization->getName() : llvm::StringRef();
      const bool LogicalReduction = !Offset && (OperatorName == "logical_and" ||
                                                OperatorName == "logical_or");
      const bool IntegerOperator =
          OperatorName == "modulus" || OperatorName == "bit_and" ||
          OperatorName == "bit_or" || OperatorName == "bit_xor" ||
          OperatorName == "bit_not";
      const auto Approved =
          approvedFunctionalObjectRecord(S, SM, Record, Context);
      const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(
          Call->getArg(Index)->IgnoreParenImpCasts());
      const auto *List =
          Cast ? dyn_cast<InitListExpr>(Cast->getSubExpr()) : nullptr;
      const auto *EmptyBase = List && List->getNumInits() == 1
                                  ? dyn_cast<InitListExpr>(List->getInit(0))
                                  : nullptr;
      const auto ValueType =
          Specialization && Specialization->getTemplateArgs().size() == 1 &&
                  Specialization->getTemplateArgs().get(0).getKind() ==
                      TemplateArgument::Type
              ? Specialization->getTemplateArgs().get(0).getAsType()
              : QualType{};
      if (!Specialization || !Approved ||
          Approved->Record->getCanonicalDecl() !=
              Specialization->getCanonicalDecl() ||
          (UnaryTransform
               ? OperatorName != "negate" && OperatorName != "bit_not" &&
                     OperatorName != "logical_not"
               : OperatorName != "plus" && OperatorName != "minus" &&
                     OperatorName != "multiplies" &&
                     OperatorName != "divides" && OperatorName != "modulus" &&
                     OperatorName != "bit_and" && OperatorName != "bit_or" &&
                     OperatorName != "bit_xor" && !LogicalReduction) ||
          (IntegerOperator &&
           (!NumericArithmetic(Function->getParamDecl(Unary ? 2 : 3)->getType(),
                               true) ||
            !Function->getParamDecl(Unary ? 2 : 3)
                 ->getType()
                 ->isIntegerType() ||
            !Function->getParamDecl(0)
                 ->getType()
                 ->getPointeeType()
                 ->isIntegerType() ||
            (!Unary && !Function->getParamDecl(2)
                            ->getType()
                            ->getPointeeType()
                            ->isIntegerType()))) ||
          ValueType.isNull() ||
          (!ValueType->isVoidType() &&
           (!(NumericArithmetic(ValueType, true) ||
              ((UnaryTransform && OperatorName == "logical_not" ||
                LogicalReduction) &&
               ValueType->isBooleanType())) ||
            !utilityScalarDirectConversion(
                Context, Function->getParamDecl(Unary ? 2 : 3)->getType(),
                ValueType) ||
            !utilityScalarDirectConversion(
                Context, Function->getParamDecl(0)->getType()->getPointeeType(),
                ValueType) ||
            (!Unary &&
             !utilityScalarDirectConversion(
                 Context,
                 Function->getParamDecl(2)->getType()->getPointeeType(),
                 ValueType)))) ||
          !Same(Call->getArg(Index)->getType(), Object) || !Cast || !List ||
          (List->getNumInits() != 0 &&
           (!EmptyBase || EmptyBase->getNumInits() != 0)))
        return false;
    }
    return true;
  };
  if (Origin->Path == "__numeric/iota.h" && Name == "iota" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      NumericPointerParameter(0, true, true) &&
      NumericPointerParameter(1, true, true) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      NumericIotaValueParameter(2, 0) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::NumericIota;
  if (Origin->Path == "__numeric/accumulate.h" && Name == "accumulate" &&
      (Call->getNumArgs() == 3 || Call->getNumArgs() == 4) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      NumericPointerParameter(0, false, true) &&
      NumericPointerParameter(1, false, true) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      NumericReductionValueParameter(2, 0, true) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()) &&
      (Call->getNumArgs() == 3 ||
       NumericBinaryTransformCallback(
           3, Function->getParamDecl(2)->getType(),
           Function->getParamDecl(2)->getType(),
           Function->getParamDecl(0)->getType()->getPointeeType(), true)))
    return UtilityOperation::NumericAccumulate;
  if (Origin->Path == "__numeric/inner_product.h" && Name == "inner_product" &&
      (Call->getNumArgs() == 4 || Call->getNumArgs() == 6) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      NumericPointerParameter(0, false, true) &&
      NumericPointerParameter(1, false, true) &&
      NumericPointerParameter(2, false, true) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(3)->getType()) &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 4 && NumericReductionValueParameter(3, 0, true) &&
        NumericCommonElements(0, 2, true)) ||
       (Call->getNumArgs() == 6 && NumericReductionValueParameter(3, 0, true) &&
        ((NumericArithmeticFunctionalPair(false) &&
          NumericCommonElements(0, 2, true)) ||
         (NumericBinaryTransformCallback(
              4, Function->getParamDecl(3)->getType(),
              Function->getParamDecl(3)->getType(),
              Function->getParamDecl(3)->getType(), true) &&
          NumericBinaryTransformCallback(
              5, Function->getParamDecl(3)->getType(),
              Function->getParamDecl(0)->getType()->getPointeeType(),
              Function->getParamDecl(2)->getType()->getPointeeType(), true))))))
    return UtilityOperation::NumericInnerProduct;
  if (((Origin->Path == "__numeric/partial_sum.h" && Name == "partial_sum") ||
       (Origin->Path == "__numeric/adjacent_difference.h" &&
        Name == "adjacent_difference")) &&
      (Call->getNumArgs() == 3 || Call->getNumArgs() == 4) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      NumericPointerParameter(0, false, true) &&
      NumericPointerParameter(1, false, true) &&
      NumericPointerParameter(2, true, true) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmTransferParameters(0, 2) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()) &&
      (Call->getNumArgs() == 3 ||
       NumericBinaryCallback(
           3, Function->getParamDecl(0)->getType()->getPointeeType(), true)))
    return Name == "partial_sum" ? UtilityOperation::NumericPartialSum
                                 : UtilityOperation::NumericAdjacentDifference;
  if (Origin->Path == "__numeric/reduce.h" && Name == "reduce" &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3 ||
       Call->getNumArgs() == 4) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      NumericPointerParameter(0, false, true) &&
      NumericPointerParameter(1, false, true) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (Call->getNumArgs() == 2 &&
        Same(Function->getReturnType(), Function->getParamDecl(0)
                                            ->getType()
                                            ->getPointeeType()
                                            .getUnqualifiedType()))
      return UtilityOperation::NumericReduce;
    if (Call->getNumArgs() >= 3 && NumericReductionValueParameter(2, 0, true) &&
        Same(Function->getReturnType(), Function->getParamDecl(2)->getType())) {
      if (Call->getNumArgs() == 3 ||
          NumericBinaryTransformCallback(
              3, Function->getParamDecl(2)->getType(),
              Function->getParamDecl(2)->getType(),
              Function->getParamDecl(0)->getType()->getPointeeType(), true))
        return UtilityOperation::NumericReduce;
    }
  }
  if (Origin->Path == "__numeric/transform_reduce.h" &&
      Name == "transform_reduce" &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      Same(Call->getType(), Function->getReturnType()) &&
      NumericPointerParameter(0, false, true) &&
      NumericPointerParameter(1, false, true) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType())) {
    auto Element = Function->getParamDecl(0)->getType()->getPointeeType();
    if ((Call->getNumArgs() == 4 || Call->getNumArgs() == 6) &&
        NumericPointerParameter(2, false, true) &&
        NumericReductionValueParameter(3, 0, true) &&
        Same(Function->getReturnType(), Function->getParamDecl(3)->getType()) &&
        ((Call->getNumArgs() == 4 && NumericCommonElements(0, 2, true)) ||
         (Call->getNumArgs() == 6 &&
          ((NumericArithmeticFunctionalPair(false) &&
            NumericCommonElements(0, 2, true)) ||
           (NumericBinaryTransformCallback(
                4, Function->getParamDecl(3)->getType(),
                Function->getParamDecl(3)->getType(),
                Function->getParamDecl(3)->getType(), true) &&
            NumericBinaryTransformCallback(
                5, Function->getParamDecl(3)->getType(), Element,
                Function->getParamDecl(2)->getType()->getPointeeType(),
                true))))))
      return UtilityOperation::NumericTransformReduce;
    if (Call->getNumArgs() == 5 && NumericReductionValueParameter(2, 0, true) &&
        Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
        ((NumericArithmeticFunctionalPair(true) &&
          NumericArithmetic(Element, true)) ||
         (NumericBinaryTransformCallback(
              3, Function->getParamDecl(2)->getType(),
              Function->getParamDecl(2)->getType(),
              Function->getParamDecl(2)->getType(), true) &&
          NumericUnaryCallback(4, Element, true))))
      return UtilityOperation::NumericTransformReduce;
  }
  if (((Origin->Path == "__numeric/inclusive_scan.h" &&
        Name == "inclusive_scan" && Call->getNumArgs() >= 3 &&
        Call->getNumArgs() <= 5) ||
       (Origin->Path == "__numeric/exclusive_scan.h" &&
        Name == "exclusive_scan" &&
        (Call->getNumArgs() == 4 || Call->getNumArgs() == 5))) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      NumericPointerParameter(0, false, true) &&
      NumericPointerParameter(1, false, true) &&
      NumericPointerParameter(2, true, true) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    auto Element = Function->getParamDecl(0)->getType()->getPointeeType();
    if (Name == "inclusive_scan") {
      if (((Call->getNumArgs() == 3 || Call->getNumArgs() == 4) &&
           AlgorithmTransferParameters(0, 2) &&
           (Call->getNumArgs() == 3 ||
            NumericBinaryCallback(3, Element, true))) ||
          (Call->getNumArgs() == 5 &&
           NumericReductionValueParameter(4, 0, true) &&
           NumericValueOutputParameter(4, 2, true) &&
           NumericBinaryTransformCallback(
               3, Function->getParamDecl(4)->getType(),
               Function->getParamDecl(4)->getType(), Element, true)))
        return UtilityOperation::NumericInclusiveScan;
    } else if (NumericReductionValueParameter(3, 0, true) &&
               NumericValueOutputParameter(3, 2, true) &&
               (Call->getNumArgs() == 4 ||
                NumericBinaryTransformCallback(
                    4, Function->getParamDecl(3)->getType(),
                    Function->getParamDecl(3)->getType(), Element, true))) {
      return UtilityOperation::NumericExclusiveScan;
    }
  }
  if (((Origin->Path == "__numeric/transform_inclusive_scan.h" &&
        Name == "transform_inclusive_scan" &&
        (Call->getNumArgs() == 5 || Call->getNumArgs() == 6)) ||
       (Origin->Path == "__numeric/transform_exclusive_scan.h" &&
        Name == "transform_exclusive_scan" && Call->getNumArgs() == 6)) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      NumericPointerParameter(0, false, true) &&
      NumericPointerParameter(1, false, true) &&
      NumericPointerParameter(2, true, true) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    auto Element = Function->getParamDecl(0)->getType()->getPointeeType();
    if (Name == "transform_inclusive_scan") {
      if (NumericUnaryCallback(4, Element, true) &&
          ((Call->getNumArgs() == 5 && AlgorithmTransferParameters(0, 2) &&
            NumericBinaryCallback(3, Element, true)) ||
           (Call->getNumArgs() == 6 &&
            NumericReductionValueParameter(5, 0, true) &&
            NumericValueOutputParameter(5, 2, true) &&
            NumericBinaryTransformCallback(
                3, Function->getParamDecl(5)->getType(),
                Function->getParamDecl(5)->getType(), Element, true))))
        return UtilityOperation::NumericTransformInclusiveScan;
    } else if (NumericReductionValueParameter(3, 0, true) &&
               NumericValueOutputParameter(3, 2, true) &&
               NumericBinaryTransformCallback(
                   4, Function->getParamDecl(3)->getType(),
                   Function->getParamDecl(3)->getType(), Element, true) &&
               NumericUnaryCallback(5, Element, true)) {
      return UtilityOperation::NumericTransformExclusiveScan;
    }
  }
  if (Origin->Path == "__numeric/gcd_lcm.h" &&
      (Name == "gcd" || Name == "lcm") && Call->getNumArgs() == 2 &&
      Function->getNumParams() == 2 && Call->isPRValue() &&
      NumericIntegerValueParameter(0) && NumericIntegerValueParameter(1) &&
      !Function->getReturnType().isNull() &&
      !Function->getReturnType()->isEnumeralType() &&
      Function->getReturnType()->isIntegerType() &&
      !Function->getReturnType()->isBooleanType() &&
      Context.getTypeSize(Function->getReturnType()) <= 64 &&
      Same(Call->getType(), Function->getReturnType()))
    return Name == "gcd" ? UtilityOperation::NumericGcd
                         : UtilityOperation::NumericLcm;
  if ((Origin->Path == "__algorithm/find.h" ||
       Origin->Path == "__algorithm/count.h") &&
      (Name == "find" || Name == "count") && Call->getNumArgs() == 3 &&
      Function->getNumParams() == 3 && Call->isPRValue() &&
      AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType())) {
    auto Iterator = Function->getParamDecl(0)->getType();
    if (AlgorithmEqualityValueParameter(2, 0)) {
      if (Name == "find" && Origin->Path == "__algorithm/find.h" &&
          Same(Function->getReturnType(), Iterator) &&
          Same(Call->getType(), Function->getReturnType()))
        return UtilityOperation::AlgorithmFind;
      if (Name == "count" && Origin->Path == "__algorithm/count.h" &&
          Same(Function->getReturnType(), Context.getPointerDiffType()) &&
          Same(Call->getType(), Function->getReturnType()))
        return UtilityOperation::AlgorithmCount;
    }
  }
  if (Origin->Path == "__algorithm/equal.h" && Name == "equal" &&
      Call->getNumArgs() >= 3 && Call->getNumArgs() <= 5 &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      Function->getReturnType()->isBooleanType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType())) {
    const bool DefaultElements = AlgorithmEqualityPointerParameter(0) &&
                                 AlgorithmEqualityPointerParameter(1) &&
                                 AlgorithmEqualityParameters(0, 2);
    if (Call->getNumArgs() == 3 && DefaultElements)
      return UtilityOperation::AlgorithmEqual;
    if (Call->getNumArgs() == 4) {
      if (DefaultElements && AlgorithmEqualityPointerParameter(3) &&
          Same(Function->getParamDecl(2)->getType(),
               Function->getParamDecl(3)->getType()))
        return UtilityOperation::AlgorithmEqual;
      if (AlgorithmBinaryPredicateParameter(3, 0, 2))
        return UtilityOperation::AlgorithmEqual;
    }
    if (Call->getNumArgs() == 5 && AlgorithmPointerParameter(3) &&
        Same(Function->getParamDecl(2)->getType(),
             Function->getParamDecl(3)->getType()) &&
        AlgorithmBinaryPredicateParameter(4, 0, 2))
      return UtilityOperation::AlgorithmEqual;
  }
  if ((Origin->Path == "__algorithm/copy.h" ||
       Origin->Path == "__algorithm/move.h" ||
       Origin->Path == "__algorithm/copy_backward.h" ||
       Origin->Path == "__algorithm/move_backward.h") &&
      (Name == "copy" || Name == "move" || Name == "copy_backward" ||
       Name == "move_backward") &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      AlgorithmPointerParameter(1) && AlgorithmTransferParameters(0, 2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (Origin->Path == "__algorithm/copy.h" && Name == "copy")
      return UtilityOperation::AlgorithmCopy;
    if (Origin->Path == "__algorithm/move.h" && Name == "move")
      return UtilityOperation::AlgorithmMove;
    if (Origin->Path == "__algorithm/copy_backward.h" &&
        Name == "copy_backward")
      return UtilityOperation::AlgorithmCopyBackward;
    if (Origin->Path == "__algorithm/move_backward.h" &&
        Name == "move_backward")
      return UtilityOperation::AlgorithmMoveBackward;
  }
  if (Origin->Path == "__algorithm/fill.h" && Name == "fill" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      AlgorithmTransferValueParameter(2, 0) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmFill;
  if (Origin->Path == "__algorithm/fill_n.h" && Name == "fill_n" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      AlgorithmCountParameter(1) && AlgorithmTransferValueParameter(2, 0) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmFillN;
  if (Origin->Path == "__algorithm/swap_ranges.h" && Name == "swap_ranges" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      AlgorithmPointerParameter(1) && AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      SameAlgorithmElement(Function->getParamDecl(0)->getType(),
                           Function->getParamDecl(2)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(2)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmSwapRanges;
  if (Origin->Path == "__algorithm/reverse.h" && Name == "reverse" &&
      Call->getNumArgs() == 2 && Function->getNumParams() == 2 &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmReverse;
  if (Origin->Path == "__algorithm/reverse_copy.h" && Name == "reverse_copy" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      AlgorithmPointerParameter(1) && AlgorithmTransferParameters(0, 2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmReverseCopy;
  if (((Origin->Path == "__algorithm/min_element.h" && Name == "min_element") ||
       (Origin->Path == "__algorithm/max_element.h" &&
        Name == "max_element")) &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (!((Call->getNumArgs() == 2 && AlgorithmOrderedPointerParameter(0)) ||
          (Call->getNumArgs() == 3 &&
           AlgorithmBinaryPredicateParameter(2, 0, 0))))
      return std::nullopt;
    return Name == "min_element" ? UtilityOperation::AlgorithmMinElement
                                 : UtilityOperation::AlgorithmMaxElement;
  }
  if (((Origin->Path == "__algorithm/lower_bound.h" && Name == "lower_bound") ||
       (Origin->Path == "__algorithm/upper_bound.h" && Name == "upper_bound") ||
       (Origin->Path == "__algorithm/binary_search.h" &&
        Name == "binary_search")) &&
      (Call->getNumArgs() == 3 || Call->getNumArgs() == 4) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmScalarValueParameter(2, 0) &&
      Same(Call->getType(), Function->getReturnType())) {
    const bool Default = Call->getNumArgs() == 3;
    const bool ForwardComparator =
        Call->getNumArgs() == 4 &&
        AlgorithmBinaryPredicateValueParameter(3, 0, 2);
    const bool ReverseComparator =
        Call->getNumArgs() == 4 &&
        AlgorithmBinaryPredicateReversedValueParameter(3, 0, 2);
    if (!((Default && AlgorithmOrderedPointerParameter(0) &&
           AlgorithmOrderedValueParameter(2, 0)) ||
          (Name == "lower_bound" && ForwardComparator) ||
          (Name == "upper_bound" && ReverseComparator) ||
          (Name == "binary_search" && ForwardComparator && ReverseComparator)))
      return std::nullopt;
    if (Name == "binary_search") {
      if (Function->getReturnType()->isBooleanType())
        return UtilityOperation::AlgorithmBinarySearch;
    } else if (Same(Function->getReturnType(),
                    Function->getParamDecl(0)->getType())) {
      return Name == "lower_bound" ? UtilityOperation::AlgorithmLowerBound
                                   : UtilityOperation::AlgorithmUpperBound;
    }
  }
  if (((Origin->Path == "__algorithm/is_sorted.h" && Name == "is_sorted") ||
       (Origin->Path == "__algorithm/is_sorted_until.h" &&
        Name == "is_sorted_until")) &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (!((Call->getNumArgs() == 2 && AlgorithmOrderedPointerParameter(0)) ||
          (Call->getNumArgs() == 3 &&
           AlgorithmBinaryPredicateParameter(2, 0, 0))))
      return std::nullopt;
    if (Name == "is_sorted" && Function->getReturnType()->isBooleanType())
      return UtilityOperation::AlgorithmIsSorted;
    if (Name == "is_sorted_until" &&
        Same(Function->getReturnType(), Function->getParamDecl(0)->getType()))
      return UtilityOperation::AlgorithmIsSortedUntil;
  }
  if (Origin->Path == "__algorithm/adjacent_find.h" &&
      Name == "adjacent_find" &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (Call->getNumArgs() == 2 && AlgorithmEqualityPointerParameter(0) &&
        AlgorithmEqualityPointerParameter(1))
      return UtilityOperation::AlgorithmAdjacentFind;
    if (Call->getNumArgs() == 3 && AlgorithmBinaryPredicateParameter(2, 0, 0))
      return UtilityOperation::AlgorithmAdjacentFind;
  }
  if (Origin->Path == "__algorithm/remove.h" && Name == "remove" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      AlgorithmEqualityValueParameter(2, 0) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmRemove;
  if (Origin->Path == "__algorithm/remove_copy.h" && Name == "remove_copy" &&
      Call->getNumArgs() == 4 && Function->getNumParams() == 4 &&
      Call->isPRValue() && AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      AlgorithmTransferParameters(0, 2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmEqualityValueParameter(3, 0) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmRemoveCopy;
  if (Origin->Path == "__algorithm/replace.h" && Name == "replace" &&
      Call->getNumArgs() == 4 && Function->getNumParams() == 4 &&
      AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      AlgorithmEqualityValueParameter(2, 0) &&
      AlgorithmTransferValueParameter(3, 0) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmReplace;
  if (Origin->Path == "__algorithm/replace_copy.h" && Name == "replace_copy" &&
      Call->getNumArgs() == 5 && Function->getNumParams() == 5 &&
      Call->isPRValue() && AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      AlgorithmTransferParameters(0, 2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmEqualityValueParameter(3, 0) &&
      AlgorithmTransferValueParameter(4, 2) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmReplaceCopy;
  if (Origin->Path == "__algorithm/unique.h" && Name == "unique" &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (Call->getNumArgs() == 2 && AlgorithmEqualityPointerParameter(0) &&
        AlgorithmEqualityPointerParameter(1))
      return UtilityOperation::AlgorithmUnique;
    if (Call->getNumArgs() == 3 && AlgorithmBinaryPredicateParameter(2, 0, 0))
      return UtilityOperation::AlgorithmUnique;
  }
  if (Origin->Path == "__algorithm/unique_copy.h" && Name == "unique_copy" &&
      (Call->getNumArgs() == 3 || Call->getNumArgs() == 4) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmTransferParameters(0, 2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (Call->getNumArgs() == 3 && AlgorithmEqualityPointerParameter(0) &&
        AlgorithmEqualityPointerParameter(1))
      return UtilityOperation::AlgorithmUniqueCopy;
    if (Call->getNumArgs() == 4 && AlgorithmBinaryPredicateParameter(3, 0, 0))
      return UtilityOperation::AlgorithmUniqueCopy;
  }
  if (((Origin->Path == "__algorithm/search.h" && Name == "search") ||
       (Origin->Path == "__algorithm/find_end.h" && Name == "find_end") ||
       (Origin->Path == "__algorithm/find_first_of.h" &&
        Name == "find_first_of")) &&
      (Call->getNumArgs() == 4 || Call->getNumArgs() == 5) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmPointerParameter(2) && AlgorithmPointerParameter(3) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getParamDecl(2)->getType(),
           Function->getParamDecl(3)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    const bool DefaultElements = AlgorithmEqualityPointerParameter(0) &&
                                 AlgorithmEqualityPointerParameter(1) &&
                                 AlgorithmEqualityPointerParameter(3) &&
                                 AlgorithmEqualityParameters(0, 2);
    if (!((Call->getNumArgs() == 4 && DefaultElements) ||
          (Call->getNumArgs() == 5 &&
           AlgorithmBinaryPredicateParameter(4, 0, 2))))
      return std::nullopt;
    if (Name == "search")
      return UtilityOperation::AlgorithmSearch;
    if (Name == "find_end")
      return UtilityOperation::AlgorithmFindEnd;
    return UtilityOperation::AlgorithmFindFirstOf;
  }
  if (Origin->Path == "__algorithm/search_n.h" && Name == "search_n" &&
      (Call->getNumArgs() == 4 || Call->getNumArgs() == 5) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmCountParameter(2) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (Call->getNumArgs() == 4 && AlgorithmEqualityPointerParameter(0) &&
        AlgorithmEqualityPointerParameter(1) &&
        AlgorithmEqualityValueParameter(3, 0))
      return UtilityOperation::AlgorithmSearchN;
    if (Call->getNumArgs() == 5 &&
        AlgorithmBinaryPredicateValueParameter(4, 0, 3))
      return UtilityOperation::AlgorithmSearchN;
  }
  if (Origin->Path == "__algorithm/mismatch.h" && Name == "mismatch" &&
      Call->getNumArgs() >= 3 && Call->getNumArgs() <= 5 &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    auto Pair = approvedUtilityPairRecord(
        S, SM, Function->getReturnType()->getAsCXXRecordDecl(), Context);
    if (Pair &&
        Same(Pair->First->getType(), Function->getParamDecl(0)->getType()) &&
        Same(Pair->Second->getType(), Function->getParamDecl(2)->getType())) {
      const bool DefaultElements = AlgorithmEqualityPointerParameter(0) &&
                                   AlgorithmEqualityPointerParameter(1) &&
                                   AlgorithmEqualityParameters(0, 2);
      if (Call->getNumArgs() == 3 && DefaultElements)
        return UtilityOperation::AlgorithmMismatch;
      if (Call->getNumArgs() == 4) {
        if (DefaultElements && AlgorithmEqualityPointerParameter(3) &&
            Same(Function->getParamDecl(2)->getType(),
                 Function->getParamDecl(3)->getType()))
          return UtilityOperation::AlgorithmMismatch;
        if (AlgorithmBinaryPredicateParameter(3, 0, 2))
          return UtilityOperation::AlgorithmMismatch;
      }
      if (Call->getNumArgs() == 5 && AlgorithmPointerParameter(3) &&
          Same(Function->getParamDecl(2)->getType(),
               Function->getParamDecl(3)->getType()) &&
          AlgorithmBinaryPredicateParameter(4, 0, 2))
        return UtilityOperation::AlgorithmMismatch;
    }
  }
  if (Origin->Path == "__algorithm/copy_n.h" && Name == "copy_n" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      AlgorithmCountParameter(1) && AlgorithmTransferParameters(0, 2) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmCopyN;
  if (Origin->Path == "__algorithm/iter_swap.h" && Name == "iter_swap" &&
      Call->getNumArgs() == 2 && Function->getNumParams() == 2 &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      SameAlgorithmElement(Function->getParamDecl(0)->getType(),
                           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(1)->getType()) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmIterSwap;
  if (Origin->Path == "__algorithm/rotate.h" && Name == "rotate" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      AlgorithmPointerParameter(1) && AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(2)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmRotate;
  if (Origin->Path == "__algorithm/rotate_copy.h" && Name == "rotate_copy" &&
      Call->getNumArgs() == 4 && Function->getNumParams() == 4 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      AlgorithmPointerParameter(1) && AlgorithmPointerParameter(2) &&
      AlgorithmTransferParameters(0, 3) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(2)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(3)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmRotateCopy;
  if (Origin->Path == "__algorithm/equal_range.h" && Name == "equal_range" &&
      (Call->getNumArgs() == 3 || Call->getNumArgs() == 4) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmScalarValueParameter(2, 0) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (!((Call->getNumArgs() == 3 && AlgorithmOrderedPointerParameter(0) &&
           AlgorithmOrderedValueParameter(2, 0)) ||
          (Call->getNumArgs() == 4 &&
           AlgorithmBinaryPredicateValueParameter(3, 0, 2) &&
           AlgorithmBinaryPredicateReversedValueParameter(3, 0, 2))))
      return std::nullopt;
    auto Pair = approvedUtilityPairRecord(
        S, SM, Function->getReturnType()->getAsCXXRecordDecl(), Context);
    if (Pair &&
        Same(Pair->First->getType(), Function->getParamDecl(0)->getType()) &&
        Same(Pair->Second->getType(), Function->getParamDecl(0)->getType()))
      return UtilityOperation::AlgorithmEqualRange;
  }
  if (((Origin->Path == "__algorithm/lexicographical_compare.h" &&
        Name == "lexicographical_compare") ||
       (Origin->Path == "__algorithm/includes.h" && Name == "includes")) &&
      (Call->getNumArgs() == 4 || Call->getNumArgs() == 5) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      Function->getReturnType()->isBooleanType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmPointerParameter(2) && AlgorithmPointerParameter(3) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getParamDecl(2)->getType(),
           Function->getParamDecl(3)->getType()) &&
      ((Call->getNumArgs() == 4 && AlgorithmOrderedParameters(0, 2)) ||
       (Call->getNumArgs() == 5 && AlgorithmBinaryPredicateParameter(4, 0, 2))))
    return Name == "includes"
               ? UtilityOperation::AlgorithmIncludes
               : UtilityOperation::AlgorithmLexicographicalCompare;
  const bool OrderedOutputAlgorithm =
      (Origin->Path == "__algorithm/merge.h" && Name == "merge") ||
      (Origin->Path == "__algorithm/set_union.h" && Name == "set_union") ||
      (Origin->Path == "__algorithm/set_intersection.h" &&
       Name == "set_intersection") ||
      (Origin->Path == "__algorithm/set_difference.h" &&
       Name == "set_difference") ||
      (Origin->Path == "__algorithm/set_symmetric_difference.h" &&
       Name == "set_symmetric_difference");
  if (OrderedOutputAlgorithm &&
      (Call->getNumArgs() == 5 || Call->getNumArgs() == 6) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmPointerParameter(2) && AlgorithmPointerParameter(3) &&
      AlgorithmTransferParameters(0, 4) && AlgorithmTransferParameters(2, 4) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getParamDecl(2)->getType(),
           Function->getParamDecl(3)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(4)->getType()) &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 5 && AlgorithmOrderedParameters(0, 2)) ||
       (Call->getNumArgs() == 6 &&
        AlgorithmBinaryPredicateParameter(5, 0, 2)))) {
    if (Name == "merge")
      return UtilityOperation::AlgorithmMerge;
    if (Name == "set_union")
      return UtilityOperation::AlgorithmSetUnion;
    if (Name == "set_intersection")
      return UtilityOperation::AlgorithmSetIntersection;
    if (Name == "set_difference")
      return UtilityOperation::AlgorithmSetDifference;
    return UtilityOperation::AlgorithmSetSymmetricDifference;
  }
  if (((Origin->Path == "__algorithm/min.h" && Name == "min") ||
       (Origin->Path == "__algorithm/max.h" && Name == "max")) &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isLValue() &&
      AlgorithmReferenceParameter(0) && AlgorithmReferenceParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Function->getReturnType()->isLValueReferenceType() &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()->getPointeeType()) &&
      ((Call->getNumArgs() == 2 && AlgorithmOrderedReferenceParameter(0)) ||
       (Call->getNumArgs() == 3 &&
        AlgorithmBinaryPredicateReferenceParameter(2, 0))))
    return Name == "min" ? UtilityOperation::AlgorithmMin
                         : UtilityOperation::AlgorithmMax;
  if (Origin->Path == "__algorithm/clamp.h" && Name == "clamp" &&
      (Call->getNumArgs() == 3 || Call->getNumArgs() == 4) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isLValue() &&
      AlgorithmReferenceParameter(0) && AlgorithmReferenceParameter(1) &&
      AlgorithmReferenceParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(2)->getType()) &&
      Function->getReturnType()->isLValueReferenceType() &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()->getPointeeType()) &&
      ((Call->getNumArgs() == 3 && AlgorithmOrderedReferenceParameter(0)) ||
       (Call->getNumArgs() == 4 &&
        AlgorithmBinaryPredicateReferenceParameter(3, 0))))
    return UtilityOperation::AlgorithmClamp;
  if (Origin->Path == "__algorithm/minmax.h" && Name == "minmax" &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmReferenceParameter(0) && AlgorithmReferenceParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 2 && AlgorithmOrderedReferenceParameter(0)) ||
       (Call->getNumArgs() == 3 &&
        AlgorithmBinaryPredicateReferenceParameter(2, 0)))) {
    auto Pair = approvedUtilityReferencePairRecord(
        S, SM, Function->getReturnType()->getAsCXXRecordDecl(), Context);
    if (Pair &&
        Same(Pair->First->getType(), Function->getParamDecl(0)->getType()) &&
        Same(Pair->Second->getType(), Function->getParamDecl(0)->getType()))
      return UtilityOperation::AlgorithmMinmax;
  }
  if (Origin->Path == "__algorithm/minmax_element.h" &&
      Name == "minmax_element" &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 2 && AlgorithmOrderedPointerParameter(0)) ||
       (Call->getNumArgs() == 3 &&
        AlgorithmBinaryPredicateParameter(2, 0, 0)))) {
    auto Pair = approvedUtilityPairRecord(
        S, SM, Function->getReturnType()->getAsCXXRecordDecl(), Context);
    if (Pair &&
        Same(Pair->First->getType(), Function->getParamDecl(0)->getType()) &&
        Same(Pair->Second->getType(), Function->getParamDecl(0)->getType()))
      return UtilityOperation::AlgorithmMinmaxElement;
  }
  const bool HeapQueryAlgorithm =
      (Origin->Path == "__algorithm/is_heap.h" && Name == "is_heap") ||
      (Origin->Path == "__algorithm/is_heap_until.h" &&
       Name == "is_heap_until");
  if (HeapQueryAlgorithm &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 2 && AlgorithmOrderedPointerParameter(0)) ||
       (Call->getNumArgs() == 3 &&
        AlgorithmBinaryPredicateParameter(2, 0, 0)))) {
    if (Name == "is_heap" && Function->getReturnType()->isBooleanType())
      return UtilityOperation::AlgorithmIsHeap;
    if (Name == "is_heap_until" &&
        Same(Function->getReturnType(), Function->getParamDecl(0)->getType()))
      return UtilityOperation::AlgorithmIsHeapUntil;
  }
  const bool HeapMutationAlgorithm =
      (Origin->Path == "__algorithm/make_heap.h" && Name == "make_heap") ||
      (Origin->Path == "__algorithm/push_heap.h" && Name == "push_heap") ||
      (Origin->Path == "__algorithm/pop_heap.h" && Name == "pop_heap") ||
      (Origin->Path == "__algorithm/sort_heap.h" && Name == "sort_heap");
  if (HeapMutationAlgorithm &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3) &&
      Function->getNumParams() == Call->getNumArgs() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 2 && AlgorithmOrderedPointerParameter(0)) ||
       (Call->getNumArgs() == 3 &&
        AlgorithmBinaryPredicateParameter(2, 0, 0)))) {
    if (Name == "make_heap")
      return UtilityOperation::AlgorithmMakeHeap;
    if (Name == "push_heap")
      return UtilityOperation::AlgorithmPushHeap;
    if (Name == "pop_heap")
      return UtilityOperation::AlgorithmPopHeap;
    return UtilityOperation::AlgorithmSortHeap;
  }
  if (Origin->Path == "__algorithm/sort.h" && Name == "sort" &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3) &&
      Function->getNumParams() == Call->getNumArgs() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 2 && AlgorithmOrderedPointerParameter(0)) ||
       (Call->getNumArgs() == 3 && AlgorithmBinaryPredicateParameter(2, 0, 0))))
    return UtilityOperation::AlgorithmSort;
  if (Origin->Path == "__algorithm/stable_sort.h" && Name == "stable_sort" &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3) &&
      Function->getNumParams() == Call->getNumArgs() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 2 && AlgorithmOrderedPointerParameter(0)) ||
       (Call->getNumArgs() == 3 && AlgorithmBinaryPredicateParameter(2, 0, 0))))
    return UtilityOperation::AlgorithmStableSort;
  if (Origin->Path == "__algorithm/inplace_merge.h" &&
      Name == "inplace_merge" &&
      (Call->getNumArgs() == 3 || Call->getNumArgs() == 4) &&
      Function->getNumParams() == Call->getNumArgs() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(2)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 3 && AlgorithmOrderedPointerParameter(0)) ||
       (Call->getNumArgs() == 4 && AlgorithmBinaryPredicateParameter(3, 0, 0))))
    return UtilityOperation::AlgorithmInplaceMerge;
  if (Origin->Path == "__algorithm/partial_sort.h" && Name == "partial_sort" &&
      (Call->getNumArgs() == 3 || Call->getNumArgs() == 4) &&
      Function->getNumParams() == Call->getNumArgs() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(2)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 3 && AlgorithmOrderedPointerParameter(0)) ||
       (Call->getNumArgs() == 4 && AlgorithmBinaryPredicateParameter(3, 0, 0))))
    return UtilityOperation::AlgorithmPartialSort;
  if (Origin->Path == "__algorithm/partial_sort_copy.h" &&
      Name == "partial_sort_copy" &&
      (Call->getNumArgs() == 4 || Call->getNumArgs() == 5) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmTransferParameters(0, 2) && AlgorithmPointerParameter(3) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getParamDecl(2)->getType(),
           Function->getParamDecl(3)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 4 && AlgorithmOrderedPointerParameter(0) &&
        AlgorithmOrderedPointerParameter(2)) ||
       (Call->getNumArgs() == 5 && AlgorithmBinaryPredicateParameter(4, 0, 2) &&
        AlgorithmBinaryPredicateParameter(4, 2, 2))))
    return UtilityOperation::AlgorithmPartialSortCopy;
  if (Origin->Path == "__algorithm/nth_element.h" && Name == "nth_element" &&
      (Call->getNumArgs() == 3 || Call->getNumArgs() == 4) &&
      Function->getNumParams() == Call->getNumArgs() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(2)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 3 && AlgorithmOrderedPointerParameter(0)) ||
       (Call->getNumArgs() == 4 && AlgorithmBinaryPredicateParameter(3, 0, 0))))
    return UtilityOperation::AlgorithmNthElement;
  const bool PermutationMutation =
      (Origin->Path == "__algorithm/next_permutation.h" &&
       Name == "next_permutation") ||
      (Origin->Path == "__algorithm/prev_permutation.h" &&
       Name == "prev_permutation");
  if (PermutationMutation &&
      (Call->getNumArgs() == 2 || Call->getNumArgs() == 3) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      Function->getReturnType()->isBooleanType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      ((Call->getNumArgs() == 2 && AlgorithmOrderedPointerParameter(0)) ||
       (Call->getNumArgs() == 3 && AlgorithmBinaryPredicateParameter(2, 0, 0))))
    return Name == "next_permutation"
               ? UtilityOperation::AlgorithmNextPermutation
               : UtilityOperation::AlgorithmPrevPermutation;
  if (Origin->Path == "__algorithm/is_permutation.h" &&
      Name == "is_permutation" && Call->getNumArgs() >= 3 &&
      Call->getNumArgs() <= 5 &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      Function->getReturnType()->isBooleanType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType())) {
    const bool DefaultElements = AlgorithmEqualityPointerParameter(0) &&
                                 AlgorithmEqualityPointerParameter(1) &&
                                 AlgorithmEqualityParameters(0, 2);
    if (Call->getNumArgs() == 3 && DefaultElements)
      return UtilityOperation::AlgorithmIsPermutation;
    if (Call->getNumArgs() == 4) {
      if (DefaultElements && AlgorithmEqualityPointerParameter(3) &&
          Same(Function->getParamDecl(2)->getType(),
               Function->getParamDecl(3)->getType()))
        return UtilityOperation::AlgorithmIsPermutation;
      if (AlgorithmBinaryPredicateParameter(3, 0, 2))
        return UtilityOperation::AlgorithmIsPermutation;
    }
    if (Call->getNumArgs() == 5 && AlgorithmPointerParameter(3) &&
        Same(Function->getParamDecl(2)->getType(),
             Function->getParamDecl(3)->getType()) &&
        AlgorithmBinaryPredicateParameter(4, 0, 2))
      return UtilityOperation::AlgorithmIsPermutation;
  }
  const bool UnaryPredicateQuery =
      (Origin->Path == "__algorithm/find_if.h" && Name == "find_if") ||
      (Origin->Path == "__algorithm/find_if_not.h" && Name == "find_if_not") ||
      (Origin->Path == "__algorithm/count_if.h" && Name == "count_if") ||
      (Origin->Path == "__algorithm/all_of.h" && Name == "all_of") ||
      (Origin->Path == "__algorithm/any_of.h" && Name == "any_of") ||
      (Origin->Path == "__algorithm/none_of.h" && Name == "none_of");
  if (UnaryPredicateQuery && Call->getNumArgs() == 3 &&
      Function->getNumParams() == 3 && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmUnaryPredicateParameter(2, 0) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (Name == "find_if" &&
        Same(Function->getReturnType(), Function->getParamDecl(0)->getType()))
      return UtilityOperation::AlgorithmFindIf;
    if (Name == "find_if_not" &&
        Same(Function->getReturnType(), Function->getParamDecl(0)->getType()))
      return UtilityOperation::AlgorithmFindIfNot;
    if (Name == "count_if" &&
        Same(Function->getReturnType(), Context.getPointerDiffType()))
      return UtilityOperation::AlgorithmCountIf;
    if (Function->getReturnType()->isBooleanType()) {
      if (Name == "all_of")
        return UtilityOperation::AlgorithmAllOf;
      if (Name == "any_of")
        return UtilityOperation::AlgorithmAnyOf;
      if (Name == "none_of")
        return UtilityOperation::AlgorithmNoneOf;
    }
  }
  const bool PredicateCopy =
      (Origin->Path == "__algorithm/copy_if.h" && Name == "copy_if") ||
      (Origin->Path == "__algorithm/remove_copy_if.h" &&
       Name == "remove_copy_if");
  if (PredicateCopy && Call->getNumArgs() == 4 &&
      Function->getNumParams() == 4 && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmTransferParameters(0, 2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmUnaryPredicateParameter(3, 0) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return Name == "copy_if" ? UtilityOperation::AlgorithmCopyIf
                             : UtilityOperation::AlgorithmRemoveCopyIf;
  if (Origin->Path == "__algorithm/remove_if.h" && Name == "remove_if" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      AlgorithmUnaryPredicateParameter(2, 0) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmRemoveIf;
  if (Origin->Path == "__algorithm/replace_if.h" && Name == "replace_if" &&
      Call->getNumArgs() == 4 && Function->getNumParams() == 4 &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      AlgorithmUnaryPredicateParameter(2, 0) &&
      AlgorithmTransferValueParameter(3, 0) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmReplaceIf;
  if (Origin->Path == "__algorithm/replace_copy_if.h" &&
      Name == "replace_copy_if" && Call->getNumArgs() == 5 &&
      Function->getNumParams() == 5 && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmTransferParameters(0, 2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmUnaryPredicateParameter(3, 0) &&
      AlgorithmTransferValueParameter(4, 2) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmReplaceCopyIf;
  const bool PointerPartitionQuery =
      (Origin->Path == "__algorithm/is_partitioned.h" &&
       Name == "is_partitioned") ||
      (Origin->Path == "__algorithm/partition_point.h" &&
       Name == "partition_point");
  if (PointerPartitionQuery && Call->getNumArgs() == 3 &&
      Function->getNumParams() == 3 && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmUnaryPredicateParameter(2, 0) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (Name == "is_partitioned" && Function->getReturnType()->isBooleanType())
      return UtilityOperation::AlgorithmIsPartitioned;
    if (Name == "partition_point" &&
        Same(Function->getReturnType(), Function->getParamDecl(0)->getType()))
      return UtilityOperation::AlgorithmPartitionPoint;
  }
  if (Origin->Path == "__algorithm/partition.h" && Name == "partition" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      AlgorithmUnaryPredicateParameter(2, 0) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmPartition;
  if (Origin->Path == "__algorithm/stable_partition.h" &&
      Name == "stable_partition" && Call->getNumArgs() == 3 &&
      Function->getNumParams() == 3 && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      AlgorithmUnaryPredicateParameter(2, 0) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmStablePartition;
  if (Origin->Path == "__algorithm/partition_copy.h" &&
      Name == "partition_copy" && Call->getNumArgs() == 5 &&
      Function->getNumParams() == 5 && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      AlgorithmTransferParameters(0, 2) && AlgorithmTransferParameters(0, 3) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmUnaryPredicateParameter(4, 0) &&
      Same(Call->getType(), Function->getReturnType())) {
    auto Pair = approvedUtilityPairRecord(
        S, SM, Function->getReturnType()->getAsCXXRecordDecl(), Context);
    if (Pair &&
        Same(Pair->First->getType(), Function->getParamDecl(2)->getType()) &&
        Same(Pair->Second->getType(), Function->getParamDecl(3)->getType()))
      return UtilityOperation::AlgorithmPartitionCopy;
  }
  if (Origin->Path == "__algorithm/for_each.h" && Name == "for_each" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    const auto *Callback = AlgorithmCallbackPrototype(2);
    if (Callback && Callback->getNumParams() == 1 &&
        utilityScalarDirectConversion(
            Context, Function->getParamDecl(0)->getType()->getPointeeType(),
            Callback->getParamType(0)) &&
        (Callback->getReturnType()->isVoidType() ||
         utilityScalar(Context, Callback->getReturnType())))
      return UtilityOperation::AlgorithmForEach;
  }
  if (Origin->Path == "__algorithm/for_each_n.h" && Name == "for_each_n" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      AlgorithmCountParameter(1) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    const auto *Callback = AlgorithmCallbackPrototype(2);
    if (Callback && Callback->getNumParams() == 1 &&
        utilityScalarDirectConversion(
            Context, Function->getParamDecl(0)->getType()->getPointeeType(),
            Callback->getParamType(0)) &&
        (Callback->getReturnType()->isVoidType() ||
         utilityScalar(Context, Callback->getReturnType())))
      return UtilityOperation::AlgorithmForEachN;
  }
  if (Origin->Path == "__algorithm/transform.h" && Name == "transform" &&
      (Call->getNumArgs() == 4 || Call->getNumArgs() == 5) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmPointerParameter(0) && AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType())) {
    const bool Binary = Call->getNumArgs() == 5;
    const unsigned OutputIndex = Binary ? 3 : 2;
    const unsigned CallbackIndex = Binary ? 4 : 3;
    const auto *Callback = AlgorithmCallbackPrototype(CallbackIndex);
    bool Valid =
        AlgorithmPointerParameter(OutputIndex) &&
        utilityAlgorithmWritableScalarPointer(
            Context, Function->getParamDecl(OutputIndex)->getType()) &&
        Callback && Callback->getNumParams() == (Binary ? 2u : 1u) &&
        utilityScalarDirectConversion(
            Context, Function->getParamDecl(0)->getType()->getPointeeType(),
            Callback->getParamType(0)) &&
        utilityScalarDirectConversion(
            Context, Callback->getReturnType(),
            Function->getParamDecl(OutputIndex)->getType()->getPointeeType()) &&
        Same(Function->getReturnType(),
             Function->getParamDecl(OutputIndex)->getType()) &&
        Same(Call->getType(), Function->getReturnType());
    if (Binary)
      Valid =
          Valid && AlgorithmPointerParameter(2) &&
          utilityScalarDirectConversion(
              Context, Function->getParamDecl(2)->getType()->getPointeeType(),
              Callback->getParamType(1));
    if (Valid)
      return Binary ? UtilityOperation::AlgorithmTransformBinary
                    : UtilityOperation::AlgorithmTransformUnary;
  }
  if (((Origin->Path == "__algorithm/generate.h" && Name == "generate") ||
       (Origin->Path == "__algorithm/generate_n.h" && Name == "generate_n")) &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      AlgorithmPointerParameter(0) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType())) {
    const bool Counted = Name == "generate_n";
    const auto *Callback = AlgorithmCallbackPrototype(2);
    const bool Bound = Counted ? AlgorithmCountParameter(1)
                               : (AlgorithmPointerParameter(1) &&
                                  Same(Function->getParamDecl(0)->getType(),
                                       Function->getParamDecl(1)->getType()));
    const bool Result = Counted ? (Call->isPRValue() &&
                                   Same(Function->getReturnType(),
                                        Function->getParamDecl(0)->getType()))
                                : Function->getReturnType()->isVoidType();
    if (Bound && Result && Same(Call->getType(), Function->getReturnType()) &&
        Callback && Callback->getNumParams() == 0 &&
        utilityScalarDirectConversion(
            Context, Callback->getReturnType(),
            Function->getParamDecl(0)->getType()->getPointeeType()))
      return Counted ? UtilityOperation::AlgorithmGenerateN
                     : UtilityOperation::AlgorithmGenerate;
  }
  if (Origin->Path == "__iterator/reverse_iterator.h" &&
      Name == "make_reverse_iterator" && Call->getNumArgs() == 1 &&
      Function->getNumParams() == 1 && Call->isPRValue()) {
    const auto Result = ReverseFor(Function->getReturnType());
    auto Parameter = Function->getParamDecl(0)->getType();
    if (Result && utilityObjectPointer(Context, Parameter) &&
        Same(Parameter, Result->IteratorType) &&
        Same(Call->getArg(0)->getType(), Parameter) &&
        Same(Call->getType(), Function->getReturnType()))
      return UtilityOperation::MakeReverseIterator;
  }
  if (Origin->Path == "__iterator/reverse_iterator.h" &&
      Call->getNumArgs() == 2 && Function->getNumParams() == 2) {
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
    const auto Left = ReverseFor(Call->getArg(0)->getType());
    const auto Right = ReverseFor(Call->getArg(1)->getType());
    if (Operator && Left && Right && ReverseParameter(0, *Left) &&
        ReverseParameter(1, *Right) &&
        Context.hasSameUnqualifiedType(Left->IteratorType->getPointeeType(),
                                       Right->IteratorType->getPointeeType())) {
      if (Call->isPRValue() && Function->getReturnType()->isBooleanType() &&
          Same(Call->getType(), Function->getReturnType())) {
        switch (Operator->getOperator()) {
        case OO_EqualEqual:
          return UtilityOperation::ReverseEqual;
        case OO_ExclaimEqual:
          return UtilityOperation::ReverseNotEqual;
        case OO_Less:
          return UtilityOperation::ReverseLess;
        case OO_Greater:
          return UtilityOperation::ReverseGreater;
        case OO_LessEqual:
          return UtilityOperation::ReverseLessEqual;
        case OO_GreaterEqual:
          return UtilityOperation::ReverseGreaterEqual;
        default:
          break;
        }
      }
      if (Operator->getOperator() == OO_Minus && Call->isPRValue() &&
          Same(Function->getReturnType(), Context.getPointerDiffType()) &&
          Same(Call->getType(), Function->getReturnType()))
        return UtilityOperation::ReverseDifference;
    }
    if (Operator && Operator->getOperator() == OO_Plus && Call->isPRValue()) {
      const auto Result = ReverseFor(Function->getReturnType());
      const auto RightRecord = ReverseFor(Call->getArg(1)->getType());
      auto LeftParameter = Function->getParamDecl(0)->getType();
      if (Result && RightRecord &&
          Result->Record->getCanonicalDecl() ==
              RightRecord->Record->getCanonicalDecl() &&
          Same(LeftParameter, Context.getPointerDiffType()) &&
          Same(Call->getArg(0)->getType(), LeftParameter) &&
          ReverseParameter(1, *RightRecord) &&
          Same(Call->getType(), Function->getReturnType()))
        return UtilityOperation::ReverseAddLeft;
    }
  }
  auto IteratorRange =
      [&](QualType Type) -> std::optional<std::pair<QualType, uint64_t>> {
    if (Type.isNull())
      return std::nullopt;
    if (const auto *Native = Context.getAsConstantArrayType(Type)) {
      const auto Size = Native->getSize().getLimitedValue(65537);
      if (Size && Size <= 65536)
        return std::pair{Native->getElementType(), Size};
      return std::nullopt;
    }
    const auto Array =
        approvedUtilityArrayRecord(S, SM, Type->getAsCXXRecordDecl(), Context);
    if (!Array)
      return std::nullopt;
    return std::pair{Array->ElementType, Array->Size};
  };
  if ((Origin->Path == "__iterator/access.h" ||
       Origin->Path == "__iterator/data.h" ||
       Origin->Path == "__iterator/size.h" ||
       Origin->Path == "__iterator/empty.h") &&
      Call->getNumArgs() == 1 && Function->getNumParams() == 1) {
    auto Parameter = Function->getParamDecl(0)->getType();
    auto Result = Function->getReturnType();
    const auto Range =
        IteratorRange(Parameter->isReferenceType() ? Parameter->getPointeeType()
                                                   : QualType());
    if (Parameter->isLValueReferenceType() && Range &&
        Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                       Parameter->getPointeeType()) &&
        Same(Call->getType(), Result)) {
      auto Element = Range->first;
      if (Parameter->getPointeeType().isConstQualified())
        Element = Element.withConst();
      if (Origin->Path == "__iterator/access.h" &&
          (Name == "begin" || Name == "cbegin" || Name == "end" ||
           Name == "cend") &&
          Result->isPointerType() && Same(Result->getPointeeType(), Element))
        return Name == "end" || Name == "cend"
                   ? UtilityOperation::IteratorEnd
                   : UtilityOperation::IteratorBegin;
      if (Origin->Path == "__iterator/data.h" && Name == "data" &&
          Result->isPointerType() && Same(Result->getPointeeType(), Element))
        return UtilityOperation::IteratorData;
      if (Origin->Path == "__iterator/size.h" && Name == "size" &&
          Call->isPRValue() && Same(Result, Context.getSizeType()))
        return UtilityOperation::IteratorSize;
      if (Origin->Path == "__iterator/empty.h" && Name == "empty" &&
          Call->isPRValue() && Result->isBooleanType())
        return UtilityOperation::IteratorEmpty;
    }
  }
  if (Origin->Path == "__iterator/reverse_access.h" &&
      (Name == "rbegin" || Name == "rend" || Name == "crbegin" ||
       Name == "crend") &&
      Call->getNumArgs() == 1 && Function->getNumParams() == 1 &&
      Call->isPRValue()) {
    auto Parameter = Function->getParamDecl(0)->getType();
    auto Result = Function->getReturnType();
    const auto Range =
        IteratorRange(Parameter->isReferenceType() ? Parameter->getPointeeType()
                                                   : QualType());
    const auto ReverseResult = ReverseFor(Result);
    if (Parameter->isLValueReferenceType() && Range && ReverseResult &&
        Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                       Parameter->getPointeeType()) &&
        Same(Call->getType(), Result)) {
      auto Element = Range->first;
      if (Parameter->getPointeeType().isConstQualified())
        Element = Element.withConst();
      if (Same(ReverseResult->IteratorType->getPointeeType(), Element))
        return Name == "rend" || Name == "crend"
                   ? UtilityOperation::IteratorREnd
                   : UtilityOperation::IteratorRBegin;
    }
  }
  auto ObjectPointer = [&](QualType Type) {
    return !Type.isNull() && Type->isPointerType() &&
           !Type->isFunctionPointerType() &&
           !Type->getPointeeType()->isVoidType();
  };
  auto PointerOrReverse = [&](QualType Type) {
    return ObjectPointer(Type) || ReverseFor(Type).has_value();
  };
  if (Origin->Path == "__iterator/advance.h" && Name == "advance" &&
      Call->getNumArgs() == 2 && Function->getNumParams() == 2 &&
      Function->getReturnType()->isVoidType()) {
    auto Iterator = Function->getParamDecl(0)->getType();
    auto Distance = Function->getParamDecl(1)->getType();
    if (Iterator->isLValueReferenceType() &&
        PointerOrReverse(Iterator->getPointeeType()) &&
        !Iterator->getPointeeType().isConstQualified() &&
        Distance->isIntegralType(Context) &&
        Context.getTypeSize(Distance) <= 64 &&
        Same(Call->getArg(0)->getType(), Iterator->getPointeeType()) &&
        Same(Call->getArg(1)->getType(), Distance))
      return UtilityOperation::IteratorAdvance;
  }
  if (Origin->Path == "__iterator/distance.h" && Name == "distance" &&
      Call->getNumArgs() == 2 && Function->getNumParams() == 2 &&
      Call->isPRValue() &&
      Same(Function->getReturnType(), Context.getPointerDiffType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    auto First = Function->getParamDecl(0)->getType();
    auto Last = Function->getParamDecl(1)->getType();
    if (PointerOrReverse(First) && Same(First, Last) &&
        Same(Call->getArg(0)->getType(), First) &&
        Same(Call->getArg(1)->getType(), Last))
      return UtilityOperation::IteratorDistance;
  }
  if ((Origin->Path == "__iterator/next.h" ||
       Origin->Path == "__iterator/prev.h") &&
      (Name == "next" || Name == "prev") && Call->getNumArgs() == 2 &&
      Function->getNumParams() == 2 && Call->isPRValue()) {
    auto Iterator = Function->getParamDecl(0)->getType();
    auto Distance = Function->getParamDecl(1)->getType();
    if (PointerOrReverse(Iterator) &&
        Same(Distance, Context.getPointerDiffType()) &&
        Same(Call->getType(), Iterator) &&
        Same(Function->getReturnType(), Iterator) &&
        Same(Call->getArg(0)->getType(), Iterator) &&
        Same(Call->getArg(1)->getType(), Distance))
      return Name == "next" ? UtilityOperation::IteratorNext
                            : UtilityOperation::IteratorPrev;
  }
  if (Origin->Path == "__functional/invoke.h" && Name == "invoke" &&
      Call->getNumArgs() >= 1 &&
      Call->getNumArgs() == Function->getNumParams()) {
    const auto Callable = Call->getArg(0)->getType();
    const auto *Prototype =
        Callable->isFunctionType()
            ? Callable->getAs<FunctionProtoType>()
            : Callable->isFunctionPointerType()
                  ? Callable->getPointeeType()->getAs<FunctionProtoType>()
                  : nullptr;
    const auto Result = Prototype ? Prototype->getReturnType() : QualType();
    const bool ReferenceResult =
        !Result.isNull() && Result->isReferenceType();
    const auto Referent =
        ReferenceResult ? Result->getPointeeType() : QualType();
    if (!Prototype || Prototype->isVariadic() ||
        Prototype->getNumParams() + 1 != Call->getNumArgs() ||
        !Same(Result, Function->getReturnType()) ||
        (ReferenceResult
             ? (!supportedFunctionalInvokeReference(S, SM, Context, Referent) ||
                (Result->isLValueReferenceType() ? !Call->isLValue()
                                                 : !Call->isXValue()) ||
                !Same(Call->getType(), Referent))
             : (!Call->isPRValue() ||
                !Same(Call->getType(), Function->getReturnType()) ||
                !supportedFunctionalResult(S, SM, Context, Result))) ||
        !approvedFunctionalInvokeDispatch(S, SM, Call, Context,
                                          ReferenceResult))
      return std::nullopt;
    for (unsigned I = 0; I < Call->getNumArgs(); ++I) {
      const auto Parameter = Function->getParamDecl(I)->getType();
      if (!Parameter->isReferenceType() ||
          Parameter->getPointeeType().isVolatileQualified() ||
          !Same(Parameter->getPointeeType(), Call->getArg(I)->getType()))
        return std::nullopt;
    }
    for (unsigned I = 0; I < Prototype->getNumParams(); ++I) {
      const auto Parameter = Prototype->getParamType(I);
      const auto *ArgumentExpression = Call->getArg(I + 1);
      const auto Argument = ArgumentExpression->getType();
      bool Supported = false;
      if (Parameter->isReferenceType()) {
        Supported = supportedFunctionalInvokeReferenceArgument(
            S, SM, Context, Parameter, ArgumentExpression);
      } else {
        Supported = !Parameter->isReferenceType() &&
                    supportedFunctionalByValue(S, SM, Context, Parameter) &&
                    functionalMemberValueConversion(Context, Argument,
                                                    Parameter);
      }
      if (!Supported)
        return std::nullopt;
    }
    return UtilityOperation::FunctionalInvoke;
  }
  if (Origin->Path == "__iterator/prev.h" && Name == "prev" &&
      Call->getNumArgs() == 1 && Function->getNumParams() == 1 &&
      Call->isPRValue()) {
    auto Iterator = Function->getParamDecl(0)->getType();
    if (PointerOrReverse(Iterator) && Same(Call->getType(), Iterator) &&
        Same(Function->getReturnType(), Iterator) &&
        Same(Call->getArg(0)->getType(), Iterator))
      return UtilityOperation::IteratorPrev;
  }
  if (Origin->Path == "__utility/pair.h" && Name == "make_pair" &&
      Call->getNumArgs() == 2 && Call->isPRValue() &&
      !Function->getReturnType()->isReferenceType() &&
      Same(Call->getType(), Function->getReturnType())) {
    auto Pair = approvedUtilityPairRecord(
        S, SM, Call->getType()->getAsCXXRecordDecl(), Context);
    bool ReferencePair = false;
    if (!Pair) {
      Pair = approvedUtilityReferencePairRecord(
          S, SM, Call->getType()->getAsCXXRecordDecl(), Context);
      ReferencePair = Pair.has_value();
    }
    bool MixedReferencePair = false;
    if (!Pair) {
      Pair = approvedUtilityMixedReferencePairRecord(
          S, SM, Call->getType()->getAsCXXRecordDecl(), Context);
      MixedReferencePair = Pair.has_value();
    }
    if (!Pair ||
        (!ReferencePair && !MixedReferencePair &&
         (!utilityPairValue(S, SM, Context, Pair->First->getType()) ||
          !utilityPairValue(S, SM, Context, Pair->Second->getType()))))
      return std::nullopt;
    for (unsigned I = 0; I != 2; ++I) {
      auto Parameter = Function->getParamDecl(I)->getType();
      if (!Parameter->isReferenceType() ||
          !Context.hasSameUnqualifiedType(Call->getArg(I)->getType(),
                                          Parameter->getPointeeType()))
        return std::nullopt;
      const auto Element =
          I ? Pair->Second->getType() : Pair->First->getType();
      if (Element->isReferenceType()) {
        const auto Wrapper = approvedFunctionalReferenceRecord(
            S, SM, Call->getArg(I)->getType()->getAsCXXRecordDecl(), Context);
        if (!Wrapper || !Element->isLValueReferenceType() ||
            !Context.hasSameType(Wrapper->ReferentType,
                                 Element->getPointeeType()))
          return std::nullopt;
      } else if (!utilityPairValue(S, SM, Context, Element)) {
        return std::nullopt;
      }
    }
    return UtilityOperation::MakePair;
  }
  if (Origin->Path == "tuple" && Name == "make_tuple" && Call->isPRValue() &&
      !Function->getReturnType()->isReferenceType() &&
      Same(Call->getType(), Function->getReturnType())) {
    auto Tuple = TupleFor(Call->getType());
    if (!Tuple)
      Tuple = approvedUtilityMixedReferenceTupleRecord(
          S, SM, Call->getType()->getAsCXXRecordDecl(), Context);
    if (!Tuple || Call->getNumArgs() != Tuple->Elements.size())
      return std::nullopt;
    for (unsigned I = 0; I < Call->getNumArgs(); ++I) {
      auto Parameter = Function->getParamDecl(I)->getType();
      if (!Parameter->isReferenceType() ||
          !Context.hasSameUnqualifiedType(Call->getArg(I)->getType(),
                                          Parameter->getPointeeType()))
        return std::nullopt;
      const auto Element = Tuple->Elements[I]->getType();
      if (Element->isReferenceType()) {
        const auto Wrapper = approvedFunctionalReferenceRecord(
            S, SM, Call->getArg(I)->getType()->getAsCXXRecordDecl(), Context);
        if (!Wrapper || !Element->isLValueReferenceType() ||
            !Context.hasSameType(Wrapper->ReferentType,
                                 Element->getPointeeType()))
          return std::nullopt;
      } else if (!utilityTupleDirectConversion(
                     S, SM, Context, Call->getArg(I)->getType(), Element)) {
        return std::nullopt;
      }
    }
    return UtilityOperation::MakeTuple;
  }
  if (Origin->Path == "tuple" && Name == "tie" && Call->isPRValue() &&
      !Function->getReturnType()->isReferenceType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      Function->getNumParams() == Call->getNumArgs()) {
    const auto Tuple = approvedUtilityReferenceTupleRecord(
        S, SM, Call->getType()->getAsCXXRecordDecl(), Context);
    if (!Tuple || Call->getNumArgs() != Tuple->Elements.size())
      return std::nullopt;
    for (unsigned I = 0; I < Call->getNumArgs(); ++I) {
      const auto Parameter = Function->getParamDecl(I)->getType();
      if (!Parameter->isLValueReferenceType() ||
          !Call->getArg(I)->isLValue() ||
          !Same(Parameter, Tuple->Elements[I]->getType()) ||
          !Same(Call->getArg(I)->getType(), Parameter->getPointeeType()))
        return std::nullopt;
    }
    return UtilityOperation::Tie;
  }
  if (Origin->Path == "tuple" && Name == "forward_as_tuple" &&
      Call->isPRValue() && !Function->getReturnType()->isReferenceType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      Function->getNumParams() == Call->getNumArgs()) {
    const auto Tuple = approvedUtilityReferenceTupleRecord(
        S, SM, Call->getType()->getAsCXXRecordDecl(), Context);
    if (!Tuple || Call->getNumArgs() != Tuple->Elements.size())
      return std::nullopt;
    for (unsigned I = 0; I < Call->getNumArgs(); ++I) {
      const auto Parameter = Function->getParamDecl(I)->getType();
      if (!Parameter->isReferenceType() ||
          Parameter->isLValueReferenceType() != Call->getArg(I)->isLValue() ||
          !Same(Parameter, Tuple->Elements[I]->getType()) ||
          !Same(Call->getArg(I)->getType(), Parameter->getPointeeType()))
        return std::nullopt;
    }
    return UtilityOperation::ForwardAsTuple;
  }
  if (Origin->Path == "tuple" && Name == "apply" && Call->getNumArgs() == 2 &&
      Function->getNumParams() == 2) {
    // Named functions and stored function pointers need the same selected-get
    // proof as the other callback families.
    const auto Apply = approvedUtilityTupleApplyDispatch(S, SM, Call, Context);
    if (!Apply)
      return std::nullopt;
    const auto Callable = Call->getArg(0)->getType();
    const auto UserCallable =
        approvedUtilityTupleApplyUserCall(S, SM, Call, Context);
    const auto ObjectOperation =
        approvedUtilityTupleApplyObjectOperation(S, SM, Call, Context);
    const auto ReferenceCallable =
        approvedUtilityTupleApplyReferenceCall(S, SM, Call, Context);
    const auto MemberCallable =
        approvedUtilityTupleApplyMemberCall(S, SM, Call, Context);
    const auto ReferenceCallableResult = [&]() -> QualType {
      if (!ReferenceCallable)
        return {};
      if (ReferenceCallable->Kind ==
          FunctionalReferenceInvokeKind::FunctionObject)
        return ReferenceCallable->Operation->ResultType;
      if (ReferenceCallable->Kind ==
          FunctionalReferenceInvokeKind::UserFunctionObject)
        return ReferenceCallable->Method->getReturnType();
      const auto *ReferencePrototype =
          ReferenceCallable->FunctionPointerType->getPointeeType()
              ->getAs<FunctionProtoType>();
      return ReferencePrototype ? ReferencePrototype->getReturnType()
                                : QualType();
    }();
    const auto *Prototype =
        Callable->isFunctionType() ? Callable->getAs<FunctionProtoType>()
        : Callable->isFunctionPointerType()
            ? Callable->getPointeeType()->getAs<FunctionProtoType>()
            : nullptr;
    const auto CallableParameter = Function->getParamDecl(0)->getType();
    const auto TupleParameter = Function->getParamDecl(1)->getType();
    const auto Tuple = approvedUtilityTupleLikeSource(
        S, SM, Call->getArg(1)->getType(), Context);
    const auto Result =
        Prototype ? Prototype->getReturnType()
                  : UserCallable ? UserCallable->Method->getReturnType()
                  : ObjectOperation ? ObjectOperation->ResultType
                  : ReferenceCallable ? ReferenceCallableResult
                  : MemberCallable ? Function->getReturnType()
                                    : QualType();
    const unsigned Arity =
        Prototype ? Prototype->getNumParams()
        : UserCallable ? UserCallable->Method->getNumParams()
        : ObjectOperation ? (ObjectOperation->RightType.isNull() ? 1u : 2u)
        : ReferenceCallable ? Tuple->size()
        : MemberCallable ? Tuple->size()
                          : 0u;
    const bool ReferenceResult =
        !Result.isNull() && Result->isReferenceType();
    const auto Referent =
        ReferenceResult ? Result->getPointeeType() : QualType();
    if ((!Prototype && !UserCallable && !ObjectOperation &&
         !ReferenceCallable && !MemberCallable) ||
        (Prototype && Prototype->isVariadic()) ||
        !CallableParameter->isReferenceType() ||
        !Same(CallableParameter->getPointeeType(), Callable) ||
        !TupleParameter->isReferenceType() ||
        !Same(TupleParameter->getPointeeType(), Call->getArg(1)->getType()) ||
        !Tuple || Arity != Tuple->size() ||
        !Same(Result, Function->getReturnType()) ||
        (ReferenceResult
             ? (!supportedFunctionalInvokeReference(S, SM, Context, Referent) ||
                (Result->isLValueReferenceType() ? !Call->isLValue()
                                                 : !Call->isXValue()) ||
                !Same(Call->getType(), Referent))
             : (!Call->isPRValue() ||
                !Same(Call->getType(), Function->getReturnType()) ||
                (!Function->getReturnType()->isVoidType() &&
                 !utilityScalar(Context, Function->getReturnType()) &&
                 !supportedFunctionalResult(S, SM, Context,
                                            Function->getReturnType())))))
      return std::nullopt;
    for (unsigned I = 0; I < Tuple->size(); ++I) {
      if (ObjectOperation || ReferenceCallable || MemberCallable)
        continue;
      const auto StoredElement = Tuple->elementType(I);
      const auto Element = StoredElement->isReferenceType()
                               ? StoredElement->getPointeeType()
                               : StoredElement;
      const auto Parameter =
          Prototype ? Prototype->getParamType(I)
                    : UserCallable->Method->getParamDecl(I)->getType();
      if (Parameter->isReferenceType()) {
        const auto ParameterReferent = Parameter->getPointeeType();
        const auto TupleArgument = Call->getArg(1)->getType();
        const bool ElementIsLValue =
            StoredElement->isLValueReferenceType() ||
            Call->getArg(1)->isLValue();
        const bool Category = Parameter->isLValueReferenceType()
                                  ? (ElementIsLValue ||
                                     ParameterReferent.isConstQualified())
                                  : !ElementIsLValue;
        if (!Category ||
            !supportedFunctionalInvokeReference(S, SM, Context,
                                                ParameterReferent) ||
            !Context.hasSameUnqualifiedType(ParameterReferent, Element) ||
            (StoredElement->isReferenceType() &&
             !ParameterReferent.isAtLeastAsQualifiedAs(Element, Context)) ||
            ParameterReferent.isVolatileQualified() ||
            (!StoredElement->isReferenceType() &&
             !ParameterReferent.isConstQualified() &&
             TupleArgument.isConstQualified()))
          return std::nullopt;
        continue;
      }
      if (Parameter.isVolatileQualified() || Parameter.isRestrictQualified() ||
          Parameter.getAddressSpace() != LangAS::Default)
        return std::nullopt;
      const bool Scalar = utilityScalar(Context, Element) &&
                          utilityScalar(Context, Parameter) &&
                          utilityScalarDirectConversion(Context, Element,
                                                        Parameter);
      const bool Record = Element->isRecordType() &&
                          Parameter->isRecordType() &&
                          supportedFunctionalByValue(
                              S, SM, Context, Parameter) &&
                          utilityTupleDirectConversion(S, SM, Context, Element,
                                                       Parameter);
      if (!Scalar && !Record)
        return std::nullopt;
    }
    return UtilityOperation::TupleApply;
  }
  if (Origin->Path == "__utility/pair.h" && Call->getNumArgs() == 2 &&
      Call->isPRValue() && Function->getReturnType()->isBooleanType() &&
      Same(Call->getType(), Function->getReturnType())) {
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
    const auto *LeftRecord =
        Call->getArg(0)->getType()->getAsCXXRecordDecl();
    const auto *RightRecord =
        Call->getArg(1)->getType()->getAsCXXRecordDecl();
    auto Left = approvedUtilityPairRecord(S, SM, LeftRecord, Context);
    auto Right = approvedUtilityPairRecord(S, SM, RightRecord, Context);
    if (!Left)
      Left = approvedUtilityReferencePairRecord(S, SM, LeftRecord, Context);
    if (!Right)
      Right = approvedUtilityReferencePairRecord(S, SM, RightRecord, Context);
    if (!Left)
      Left = approvedUtilityMixedReferencePairRecord(S, SM, LeftRecord,
                                                     Context);
    if (!Right)
      Right = approvedUtilityMixedReferencePairRecord(S, SM, RightRecord,
                                                      Context);
    const auto LeftParameter = Function->getParamDecl(0)->getType();
    const auto RightParameter = Function->getParamDecl(1)->getType();
    if (Operator && Left && Right && LeftParameter->isLValueReferenceType() &&
        RightParameter->isLValueReferenceType() &&
        LeftParameter->getPointeeType().isConstQualified() &&
        RightParameter->getPointeeType().isConstQualified() &&
        !LeftParameter->getPointeeType().isVolatileQualified() &&
        !RightParameter->getPointeeType().isVolatileQualified() &&
        Context.hasSameUnqualifiedType(LeftParameter->getPointeeType(),
                                       Call->getArg(0)->getType()) &&
        Context.hasSameUnqualifiedType(RightParameter->getPointeeType(),
                                       Call->getArg(1)->getType())) {
      const auto Kind = Operator->getOperator();
      const bool Ordered = Kind != OO_EqualEqual && Kind != OO_ExclaimEqual;
      if (!utilityComparableValue(
              S, SM, Context, Context.getRecordType(Left->Record),
              Context.getRecordType(Right->Record), Ordered))
        return std::nullopt;
      switch (Operator->getOperator()) {
      case OO_EqualEqual: return UtilityOperation::PairEqual;
      case OO_ExclaimEqual: return UtilityOperation::PairNotEqual;
      case OO_Less: return UtilityOperation::PairLess;
      case OO_Greater: return UtilityOperation::PairGreater;
      case OO_LessEqual: return UtilityOperation::PairLessEqual;
      case OO_GreaterEqual: return UtilityOperation::PairGreaterEqual;
      default: break;
      }
    }
  }
  if (Origin->Path == "tuple" && Call->getNumArgs() == 2 && Call->isPRValue() &&
      Function->getReturnType()->isBooleanType() &&
      Same(Call->getType(), Function->getReturnType())) {
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
    auto Left = TupleFor(Call->getArg(0)->getType());
    auto Right = TupleFor(Call->getArg(1)->getType());
    if (!Left)
      Left = approvedUtilityMixedReferenceTupleRecord(
          S, SM, Call->getArg(0)->getType()->getAsCXXRecordDecl(), Context);
    if (!Right)
      Right = approvedUtilityMixedReferenceTupleRecord(
          S, SM, Call->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
    const auto LeftParameter = Function->getParamDecl(0)->getType();
    const auto RightParameter = Function->getParamDecl(1)->getType();
    if (Operator && Left && Right && LeftParameter->isLValueReferenceType() &&
        RightParameter->isLValueReferenceType() &&
        !LeftParameter->getPointeeType().isVolatileQualified() &&
        !RightParameter->getPointeeType().isVolatileQualified() &&
        Context.hasSameUnqualifiedType(LeftParameter->getPointeeType(),
                                       Call->getArg(0)->getType()) &&
        Context.hasSameUnqualifiedType(RightParameter->getPointeeType(),
                                       Call->getArg(1)->getType()) &&
        Left->Elements.size() == Right->Elements.size()) {
      const auto Kind = Operator->getOperator();
      const bool Ordered = Kind != OO_EqualEqual && Kind != OO_ExclaimEqual;
      if (utilityComparableValue(S, SM, Context,
                                 Context.getRecordType(Left->Record),
                                 Context.getRecordType(Right->Record), Ordered))
        switch (Kind) {
        case OO_EqualEqual:
          return UtilityOperation::TupleEqual;
        case OO_ExclaimEqual:
          return UtilityOperation::TupleNotEqual;
        case OO_Less:
          return UtilityOperation::TupleLess;
        case OO_Greater:
          return UtilityOperation::TupleGreater;
        case OO_LessEqual:
          return UtilityOperation::TupleLessEqual;
        case OO_GreaterEqual:
          return UtilityOperation::TupleGreaterEqual;
        default:
          break;
        }
    }
  }
  if (Origin->Path == "array" && Call->getNumArgs() == 2 &&
      Call->isPRValue() && Function->getReturnType()->isBooleanType() &&
      Same(Call->getType(), Function->getReturnType())) {
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
    const auto Left = approvedUtilityArrayRecord(
        S, SM, Call->getArg(0)->getType()->getAsCXXRecordDecl(), Context);
    const auto Right = approvedUtilityArrayRecord(
        S, SM, Call->getArg(1)->getType()->getAsCXXRecordDecl(), Context);
    if (Operator && Left && Right &&
        Left->Record->getCanonicalDecl() == Right->Record->getCanonicalDecl() &&
        utilityComparableValue(
            S, SM, Context, Context.getRecordType(Left->Record),
            Context.getRecordType(Right->Record),
            Operator->getOperator() != OO_EqualEqual &&
                Operator->getOperator() != OO_ExclaimEqual)) {
      switch (Operator->getOperator()) {
      case OO_EqualEqual: return UtilityOperation::ArrayEqual;
      case OO_ExclaimEqual: return UtilityOperation::ArrayNotEqual;
      case OO_Less: return UtilityOperation::ArrayLess;
      case OO_Greater: return UtilityOperation::ArrayGreater;
      case OO_LessEqual: return UtilityOperation::ArrayLessEqual;
      case OO_GreaterEqual: return UtilityOperation::ArrayGreaterEqual;
      default: break;
      }
    }
  }
  if (Origin->Path == "array" && Name == "swap" &&
      Call->getNumArgs() == 2 && Function->getReturnType()->isVoidType()) {
    auto LeftType = Function->getParamDecl(0)->getType();
    auto RightType = Function->getParamDecl(1)->getType();
    const auto Left = approvedUtilityArrayRecord(
        S, SM, LeftType->isReferenceType()
                   ? LeftType->getPointeeType()->getAsCXXRecordDecl()
                   : nullptr,
        Context);
    const auto Right = approvedUtilityArrayRecord(
        S, SM, RightType->isReferenceType()
                   ? RightType->getPointeeType()->getAsCXXRecordDecl()
                   : nullptr,
        Context);
    if (LeftType->isLValueReferenceType() &&
        RightType->isLValueReferenceType() && Left && Right &&
        Left->Record->getCanonicalDecl() == Right->Record->getCanonicalDecl() &&
        (Left->Size
             ? utilityArrayTriviallyAssignable(Context, Left->ElementType)
             : !Left->ElementType.isConstQualified()) &&
        Same(Call->getArg(0)->getType(), LeftType->getPointeeType()) &&
        Same(Call->getArg(1)->getType(), RightType->getPointeeType()) &&
        approvedUtilityPairElementSwap(
            S, SM, Function, Context.getRecordType(Left->Record), Context))
      return UtilityOperation::ArraySwap;
  }
  if (Origin->Path == "array" && Name == "get" &&
      Call->getNumArgs() == 1) {
    const auto *Arguments = Function->getTemplateSpecializationArgs();
    auto Parameter = Function->getParamDecl(0)->getType();
    auto Result = Function->getReturnType();
    const auto Array = approvedUtilityArrayRecord(
        S, SM, Parameter->isReferenceType()
                   ? Parameter->getPointeeType()->getAsCXXRecordDecl()
                   : nullptr,
        Context);
    if (Arguments && Arguments->size() == 3 &&
        Arguments->get(0).getKind() == TemplateArgument::Integral &&
        Parameter->isReferenceType() && Result->isReferenceType() && Array &&
        !Arguments->get(0).getAsIntegral().isNegative() &&
        Arguments->get(0).getAsIntegral().getLimitedValue(Array->Size) <
            Array->Size &&
        Same(Call->getArg(0)->getType(), Parameter->getPointeeType()) &&
        Same(Call->getType(), Result->getPointeeType()) &&
        Context.hasSameUnqualifiedType(Result->getPointeeType(),
                                       Array->ElementType) &&
        (Result->isLValueReferenceType() ? Call->isLValue()
                                         : Call->isXValue()))
      return UtilityOperation::ArrayGet;
  }
  const auto *Prototype = Function->getType()->getAs<FunctionProtoType>();
  if (!Prototype || !Prototype->isNothrow())
    return std::nullopt;
  auto ReferenceResult = [&] {
    auto Result = Function->getReturnType();
    if (!Result->isReferenceType() ||
        !Same(Call->getType(), Result->getPointeeType()) ||
        (Result->isLValueReferenceType() ? !Call->isLValue()
                                         : !Call->isXValue()))
      return false;
    auto Parameter = Function->getParamDecl(0)->getType();
    return Parameter->isReferenceType() &&
           Same(Call->getArg(0)->getType(), Parameter->getPointeeType()) &&
           Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                          Call->getType());
  };
  if (Call->getNumArgs() == 1 && ReferenceResult()) {
    if (Origin->Path == "__utility/move.h" && Name == "move" &&
        Function->getReturnType()->isRValueReferenceType())
      return UtilityOperation::Move;
    if (Origin->Path == "__utility/forward.h" && Name == "forward")
      return UtilityOperation::Forward;
    if (Origin->Path == "__utility/move.h" &&
        Name == "move_if_noexcept" &&
        Function->getParamDecl(0)->getType()->isLValueReferenceType())
      return UtilityOperation::MoveIfNoexcept;
    if (Origin->Path == "__utility/as_const.h" && Name == "as_const" &&
        Function->getParamDecl(0)->getType()->isLValueReferenceType() &&
        Function->getReturnType()->isLValueReferenceType() &&
        Function->getReturnType()->getPointeeType().isConstQualified())
      return UtilityOperation::AsConst;
  }
  if (Origin->Path == "__utility/exchange.h" && Name == "exchange" &&
      Call->getNumArgs() == 2 && Call->isPRValue()) {
    auto Object = Function->getParamDecl(0)->getType();
    auto Value = Function->getParamDecl(1)->getType();
    if (Object->isLValueReferenceType() && Value->isReferenceType() &&
        !Object->getPointeeType().isConstQualified() &&
        !Object->getPointeeType().isVolatileQualified() &&
        utilityScalar(Context, Object->getPointeeType()) &&
        utilityScalar(Context, Value->getPointeeType()) &&
        Same(Call->getArg(0)->getType(), Object->getPointeeType()) &&
        Same(Call->getArg(1)->getType(), Value->getPointeeType()) &&
        Same(Call->getType(), Object->getPointeeType()) &&
        Same(Function->getReturnType(), Object->getPointeeType()))
      return UtilityOperation::Exchange;
  }
  if (Origin->Path == "__utility/swap.h" && Name == "swap" &&
      Call->getNumArgs() == 2 && Function->getReturnType()->isVoidType()) {
    auto Left = Function->getParamDecl(0)->getType();
    auto Right = Function->getParamDecl(1)->getType();
    if (Left->isLValueReferenceType() && Right->isLValueReferenceType() &&
        !Left->getPointeeType().isConstQualified() &&
        !Left->getPointeeType().isVolatileQualified() &&
        utilityScalar(Context, Left->getPointeeType()) &&
        Same(Left->getPointeeType(), Right->getPointeeType()) &&
        Same(Call->getArg(0)->getType(), Left->getPointeeType()) &&
        Same(Call->getArg(1)->getType(), Right->getPointeeType()))
      return UtilityOperation::Swap;
  }
  if (Origin->Path == "__utility/pair.h" && Name == "swap" &&
      Call->getNumArgs() == 2 && Function->getReturnType()->isVoidType()) {
    auto LeftType = Function->getParamDecl(0)->getType();
    auto RightType = Function->getParamDecl(1)->getType();
    auto Left = approvedUtilityPairRecord(
        S, SM, LeftType->isReferenceType()
                   ? LeftType->getPointeeType()->getAsCXXRecordDecl()
                   : nullptr,
        Context);
    auto Right = approvedUtilityPairRecord(
        S, SM, RightType->isReferenceType()
                   ? RightType->getPointeeType()->getAsCXXRecordDecl()
                   : nullptr,
        Context);
    bool ReferencePair = false;
    if (!Left) {
      Left = approvedUtilityReferencePairRecord(
          S, SM, LeftType->isReferenceType()
                     ? LeftType->getPointeeType()->getAsCXXRecordDecl()
                     : nullptr,
          Context);
      ReferencePair = Left.has_value();
    }
    if (!Right)
      Right = approvedUtilityReferencePairRecord(
          S, SM, RightType->isReferenceType()
                     ? RightType->getPointeeType()->getAsCXXRecordDecl()
                     : nullptr,
          Context);
    bool MixedReferencePair = false;
    if (!Left) {
      Left = approvedUtilityMixedReferencePairRecord(
          S, SM, LeftType->isReferenceType()
                     ? LeftType->getPointeeType()->getAsCXXRecordDecl()
                     : nullptr,
          Context);
      MixedReferencePair = Left.has_value();
    }
    if (!Right)
      Right = approvedUtilityMixedReferencePairRecord(
          S, SM, RightType->isReferenceType()
                     ? RightType->getPointeeType()->getAsCXXRecordDecl()
                     : nullptr,
          Context);
    auto FirstType = Left ? Left->First->getType() : QualType();
    auto SecondType = Left ? Left->Second->getType() : QualType();
    if (!FirstType.isNull() && FirstType->isReferenceType())
      FirstType = FirstType->getPointeeType();
    if (!SecondType.isNull() && SecondType->isReferenceType())
      SecondType = SecondType->getPointeeType();
    if (LeftType->isLValueReferenceType() &&
        RightType->isLValueReferenceType() && Left && Right &&
        Left->Record->getCanonicalDecl() == Right->Record->getCanonicalDecl() &&
        (ReferencePair || MixedReferencePair ||
         (utilityPairValue(S, SM, Context, FirstType) &&
          utilityPairValue(S, SM, Context, SecondType))) &&
        utilityPairAssignableValue(S, SM, Context, FirstType) &&
        utilityPairAssignableValue(S, SM, Context, SecondType) &&
        Same(Call->getArg(0)->getType(), LeftType->getPointeeType()) &&
        Same(Call->getArg(1)->getType(), RightType->getPointeeType()))
      if (approvedUtilityPairElementSwap(
              S, SM, Function, Context.getRecordType(Left->Record), Context))
        return UtilityOperation::PairSwap;
  }
  if (Origin->Path == "tuple" && Name == "swap" && Call->getNumArgs() == 2 &&
      Function->getReturnType()->isVoidType()) {
    auto LeftType = Function->getParamDecl(0)->getType();
    auto RightType = Function->getParamDecl(1)->getType();
    auto Left = TupleFor(
        LeftType->isReferenceType() ? LeftType->getPointeeType() : QualType());
    auto Right =
        TupleFor(RightType->isReferenceType() ? RightType->getPointeeType()
                                              : QualType());
    if (!Left && LeftType->isReferenceType())
      Left = approvedUtilityMixedReferenceTupleRecord(
          S, SM, LeftType->getPointeeType()->getAsCXXRecordDecl(), Context);
    if (!Right && RightType->isReferenceType())
      Right = approvedUtilityMixedReferenceTupleRecord(
          S, SM, RightType->getPointeeType()->getAsCXXRecordDecl(), Context);
    bool Mutable = Left.has_value();
    if (Left)
      for (const auto *Element : Left->Elements) {
        auto ElementType = Element->getType();
        if (ElementType->isReferenceType())
          ElementType = ElementType->getPointeeType();
        Mutable &= utilityTupleAssignableValue(S, SM, Context, ElementType);
      }
    if (LeftType->isLValueReferenceType() &&
        RightType->isLValueReferenceType() && Left && Right && Mutable &&
        Left->Record->getCanonicalDecl() == Right->Record->getCanonicalDecl() &&
        Same(Call->getArg(0)->getType(), LeftType->getPointeeType()) &&
        Same(Call->getArg(1)->getType(), RightType->getPointeeType()) &&
        approvedUtilityPairElementSwap(
            S, SM, Function, Context.getRecordType(Left->Record), Context))
      return UtilityOperation::TupleSwap;
  }
  if (Origin->Path == "__utility/pair.h" && Name == "get" &&
      Call->getNumArgs() == 1) {
    const auto *Arguments = Function->getTemplateSpecializationArgs();
    auto Parameter = Function->getParamDecl(0)->getType();
    auto Result = Function->getReturnType();
    auto Pair = approvedUtilityPairRecord(
        S, SM, Parameter->isReferenceType()
                   ? Parameter->getPointeeType()->getAsCXXRecordDecl()
                   : nullptr,
        Context);
    if (!Pair)
      Pair = approvedUtilityReferencePairRecord(
          S, SM, Parameter->isReferenceType()
                     ? Parameter->getPointeeType()->getAsCXXRecordDecl()
                     : nullptr,
          Context);
    if (!Pair)
      Pair = approvedUtilityMixedReferencePairRecord(
          S, SM, Parameter->isReferenceType()
                     ? Parameter->getPointeeType()->getAsCXXRecordDecl()
                     : nullptr,
          Context);
    if (Arguments && (Arguments->size() == 2 || Arguments->size() == 3) &&
        Parameter->isReferenceType() &&
        Result->isReferenceType() && Pair &&
        Same(Call->getArg(0)->getType(), Parameter->getPointeeType()) &&
        Same(Call->getType(), Result->getPointeeType()) &&
        (Result->isLValueReferenceType() ? Call->isLValue()
                                         : Call->isXValue())) {
      if (Arguments->size() == 3 &&
          Arguments->get(0).getKind() == TemplateArgument::Integral) {
        const auto Index = Arguments->get(0).getAsIntegral();
        auto FirstType = Pair->First->getType();
        auto SecondType = Pair->Second->getType();
        if (FirstType->isReferenceType())
          FirstType = FirstType->getPointeeType();
        if (SecondType->isReferenceType())
          SecondType = SecondType->getPointeeType();
        if (Index == 0 && Context.hasSameUnqualifiedType(
                              Result->getPointeeType(), FirstType))
          return UtilityOperation::PairGetFirst;
        if (Index == 1 && Context.hasSameUnqualifiedType(
                              Result->getPointeeType(), SecondType))
          return UtilityOperation::PairGetSecond;
      }
      if (Arguments->size() == 2 &&
          Arguments->get(0).getKind() == TemplateArgument::Type &&
          Arguments->get(1).getKind() == TemplateArgument::Type) {
        auto Selected = Result->getPointeeType().getUnqualifiedType();
        auto FirstType = Pair->First->getType();
        auto SecondType = Pair->Second->getType();
        if (FirstType->isReferenceType())
          FirstType = FirstType->getPointeeType();
        if (SecondType->isReferenceType())
          SecondType = SecondType->getPointeeType();
        const bool First = Context.hasSameType(Selected, FirstType);
        const bool Second = Context.hasSameType(Selected, SecondType);
        if (First != Second &&
            Context.hasSameUnqualifiedType(Result->getPointeeType(),
                                           Selected))
          return First ? UtilityOperation::PairGetFirst
                       : UtilityOperation::PairGetSecond;
      }
    }
  }
  if (Origin->Path == "tuple" && Name == "get" && Call->getNumArgs() == 1) {
    const auto *Arguments = Function->getTemplateSpecializationArgs();
    auto Parameter = Function->getParamDecl(0)->getType();
    auto Result = Function->getReturnType();
    auto Tuple =
        TupleFor(Parameter->isReferenceType() ? Parameter->getPointeeType()
                                              : QualType());
    if (!Tuple && Parameter->isReferenceType())
      Tuple = approvedUtilityMixedReferenceTupleRecord(
          S, SM, Parameter->getPointeeType()->getAsCXXRecordDecl(), Context);
    if (!Arguments || Arguments->size() != 2 ||
        Arguments->get(1).getKind() != TemplateArgument::Pack ||
        !Parameter->isReferenceType() || !Result->isReferenceType() || !Tuple ||
        Arguments->get(1).pack_size() != Tuple->Elements.size() ||
        !Same(Call->getArg(0)->getType(), Parameter->getPointeeType()) ||
        !Same(Call->getType(), Result->getPointeeType()) ||
        (Result->isLValueReferenceType() ? !Call->isLValue()
                                         : !Call->isXValue()))
      return std::nullopt;
    unsigned PackIndex = 0;
    for (const auto &Argument : Arguments->get(1).pack_elements())
      if (Argument.getKind() != TemplateArgument::Type ||
          !Context.hasSameType(Argument.getAsType(),
                               Tuple->Elements[PackIndex++]->getType()))
        return std::nullopt;
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
        if (Context.hasSameType(Arguments->get(0).getAsType(),
                                Tuple->Elements[I]->getType())) {
          if (Index)
            return std::nullopt;
          Index = I;
        }
    }
    if (Index) {
      const auto Stored = Tuple->Elements[*Index]->getType();
      const auto Selected =
          Stored->isReferenceType() ? Stored->getPointeeType() : Stored;
      if (Context.hasSameUnqualifiedType(Result->getPointeeType(), Selected))
        return UtilityOperation::TupleGet;
    }
  }
  return std::nullopt;
}

bool approvedUtilityConstant(const State &S, const SourceManager &SM,
                             const CallExpr *Call, ASTContext &Context,
                             APValue &Value) {
  if (!Call || Call->getNumArgs() || !Call->isPRValue() ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return false;
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
  const auto *Record =
      Method ? dyn_cast<ClassTemplateSpecializationDecl>(Method->getParent())
             : nullptr;
  const auto *Template = Record ? Record->getSpecializedTemplate() : nullptr;
  const auto *Reference =
      Method ? approvedUtilityReference(S, SM, Call, Method) : nullptr;
  const auto *Qualifier = Reference ? Reference->getQualifier() : nullptr;
  const auto *QualifierType = Qualifier ? Qualifier->getAsType() : nullptr;
  const auto *QualifierRecord =
      QualifierType ? QualifierType->getAsCXXRecordDecl() : nullptr;
  if (!Method || !Record || !Template || !Reference || !QualifierRecord ||
      QualifierRecord->getCanonicalDecl() != Record->getCanonicalDecl() ||
      Method->getName() != "size" || !Method->isStatic() ||
      !Method->isConstexpr() || Method->getNumParams() ||
      Method->isVariadic() || Template->getName() != "integer_sequence" ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__utility/integer_sequence.h") ||
      !cstddefOrigin(S, SM, Template->getLocation(), "libcxx",
                     "__utility/integer_sequence.h") ||
      Method->getReturnType().isNull() ||
      !Method->getReturnType()->isIntegralType(Context) ||
      !Context.hasSameType(Call->getType(), Method->getReturnType()) ||
      !Call->isCXX11ConstantExpr(Context, &Value) || !Value.isInt())
    return false;
  const auto &Arguments = Record->getTemplateArgs();
  if (!Arguments.size() || Arguments.get(0).getKind() != TemplateArgument::Type)
    return false;
  auto Element = Arguments.get(0).getAsType();
  if (Element.isNull() || !Element->isIntegralOrEnumerationType())
    return false;
  auto Integral = [&](const TemplateArgument &Argument) {
    return Argument.getKind() == TemplateArgument::Integral &&
           Context.hasSameType(Argument.getIntegralType(), Element);
  };
  for (unsigned I = 1; I < Arguments.size(); ++I) {
    const auto &Argument = Arguments.get(I);
    if (Argument.getKind() == TemplateArgument::Pack) {
      for (const auto &Packed : Argument.pack_elements())
        if (!Integral(Packed))
          return false;
    } else if (!Integral(Argument)) {
      return false;
    }
  }
  return true;
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
