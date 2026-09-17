#include "BuiltinCppSdkData.h"
#include "Frontend.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecordLayout.h"
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

static bool utilityArrayComparable(const State &S, const SourceManager &SM,
                                   const ASTContext &Context, QualType Type,
                                   unsigned Depth = 0) {
  if (Depth > 64 || Type.isNull() || Type.isVolatileQualified())
    return false;
  if (utilityScalar(Context, Type))
    return true;
  const auto *Record = Type.getUnqualifiedType()->getAsCXXRecordDecl();
  if (!Record || !approvedUtilityArrayMetadata(S, SM, Record))
    return false;
  const auto Array = approvedUtilityArrayRecord(S, SM, Record, Context);
  return Array &&
         utilityArrayComparable(S, SM, Context, Array->ElementType, Depth + 1);
}

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

std::optional<UtilityPairRecord>
approvedUtilityPairRecord(const State &S, const SourceManager &SM,
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
  const auto *CanonicalTemplate = Template ? Template->getCanonicalDecl() : nullptr;
  if (!approvedUtilityPairMetadata(S, SM, Specialization) ||
      !Specialization || !Template || Specialization->isUnion() ||
      Specialization->isDependentContext() ||
      Specialization->getSpecializationKind() != TSK_ImplicitInstantiation ||
      Specialization->getName() != "pair" ||
      Specialization->getNumBases() || !Specialization->isStandardLayout() ||
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
      !Context.hasSameType(First->getType(), Arguments.get(0).getAsType()) ||
      !Context.hasSameType(Second->getType(), Arguments.get(1).getAsType()))
    return std::nullopt;
  return UtilityPairRecord{Specialization, First, Second};
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
  const auto Pair = approvedUtilityPairRecord(
      S, SM, Construction->getType()->getAsCXXRecordDecl(), Context);
  if (!Constructor || !Pair || Constructor->isVariadic() ||
      Constructor->getParent()->getCanonicalDecl() !=
          Pair->Record->getCanonicalDecl() ||
      Construction->getNumArgs() != Constructor->getNumParams() ||
      !approvedStandardSDKDeclaration(S, SM, Constructor) ||
      !cstddefOrigin(S, SM, Constructor->getLocation(), "libcxx",
                     "__utility/pair.h") ||
      !utilityScalar(Context, Pair->First->getType()) ||
      !utilityScalar(Context, Pair->Second->getType()))
    return std::nullopt;
  if (!Construction->getNumArgs() && Constructor->isDefaultConstructor())
    return UtilityPairConstruction::Default;
  if (Construction->getNumArgs() == 1 &&
      Constructor->isCopyOrMoveConstructor() && Constructor->isDefaulted() &&
      Constructor->isTrivial())
    return UtilityPairConstruction::CopyOrMove;
  const auto *Primary = Constructor->getPrimaryTemplate();
  if (Construction->getNumArgs() == 2 && Primary &&
      approvedStandardSDKDeclaration(S, SM, Primary) &&
      cstddefOrigin(S, SM, Primary->getLocation(), "libcxx",
                     "__utility/pair.h"))
    return UtilityPairConstruction::Elements;
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
  const auto Pair = approvedUtilityPairRecord(
      S, SM, Method ? Method->getParent() : nullptr, Context);
  if (!Method || !Pair || Method->isStatic() || Method->isVariadic() ||
      Method->getNumParams() != 1 ||
      Method->getOverloadedOperator() != OO_Equal || !Method->hasBody() ||
      !approvedStandardSDKDeclaration(S, SM, Method) ||
      !cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                     "__utility/pair.h") ||
      !utilityScalar(Context, Pair->First->getType()) ||
      !utilityScalar(Context, Pair->Second->getType()))
    return std::nullopt;
  const auto Parameter = Method->getParamDecl(0)->getType();
  const auto Result = Method->getReturnType();
  const auto PairType = Context.getRecordType(Pair->Record);
  if (!Parameter->isReferenceType() || !Result->isLValueReferenceType() ||
      !Context.hasSameUnqualifiedType(Parameter->getPointeeType(), PairType) ||
      !Context.hasSameUnqualifiedType(Result->getPointeeType(), PairType) ||
      !Context.hasSameUnqualifiedType(Assignment->getArg(0)->getType(),
                                      PairType) ||
      !Context.hasSameUnqualifiedType(Assignment->getArg(1)->getType(),
                                      PairType) ||
      !Context.hasSameUnqualifiedType(Assignment->getType(), PairType))
    return std::nullopt;
  return Pair;
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
  if (!Size || Size > 65536)
    return std::nullopt;
  auto Fields = Specialization->fields();
  auto It = Fields.begin();
  const auto *Elements = It == Fields.end() ? nullptr : *It++;
  const auto *Array =
      Elements ? Context.getAsConstantArrayType(Elements->getType()) : nullptr;
  auto Element = Arguments.get(0).getAsType();
  if (!Elements || It != Fields.end() || Elements->getName() != "__elems_" ||
      Elements->getAccess() != AS_public || Elements->isBitField() ||
      Elements->isMutable() || Elements->hasAttrs() || !Array ||
      Array->getSize().getLimitedValue(65537) != Size ||
      !Context.hasSameType(Array->getElementType(), Element) ||
      !utilityArrayValue(S, SM, Context, Element) ||
      !approvedStandardSDKDeclaration(S, SM, Elements) ||
      !cstddefOrigin(S, SM, Elements->getLocation(), "libcxx", "array"))
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

static bool utilityAlgorithmEqualityPointer(const ASTContext &Context,
                                            QualType Type) {
  if (!utilityAlgorithmScalarPointer(Context, Type))
    return false;
  auto Element = Type->getPointeeType().getUnqualifiedType();
  return (!Element->isEnumeralType() && Element->isIntegerType() &&
          Context.getTypeSize(Element) <= 64) ||
         Element->isSpecificBuiltinType(BuiltinType::Float) ||
         Element->isSpecificBuiltinType(BuiltinType::Double) ||
         (Element->isPointerType() && !Element->isFunctionPointerType()) ||
         Element->isNullPtrType();
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

bool approvedUtilityDefaultArgument(const State &S, const SourceManager &SM,
                                    const CXXDefaultArgExpr *Default,
                                    const FunctionDecl *Function,
                                    unsigned Index, ASTContext &Context) {
  if (!S.coreV2())
    return false;
  const auto *Parameter = Default ? Default->getParam() : nullptr;
  const auto *Owner =
      Parameter ? dyn_cast<FunctionDecl>(Parameter->getDeclContext()) : nullptr;
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

std::optional<UtilityOperation>
approvedUtilityOperation(const State &S, const SourceManager &SM,
                         const CallExpr *Call, const ASTContext &Context) {
  if (!Call || Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return std::nullopt;
  const auto *Function = Call->getDirectCallee();
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function);
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
      if ((Name == "front" || Name == "back") && ReferenceResult())
        return Name == "front" ? UtilityOperation::ArrayFront
                               : UtilityOperation::ArrayBack;
    }
    if (Operator && Operator->getOperator() == OO_Subscript &&
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
        utilityArrayTriviallyAssignable(Context, Array->ElementType)) {
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
        utilityArrayTriviallyAssignable(Context, Array->ElementType)) {
      auto Parameter = Method->getParamDecl(0)->getType();
      if (Parameter->isLValueReferenceType() &&
          SameArray(Parameter->getPointeeType()) &&
          SameArray(Call->getArg(0)->getType()))
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
    const auto Pair = approvedUtilityPairRecord(
        S, SM, Method ? Method->getParent() : nullptr, Context);
    if (Method && Reference && Pair && Method->getIdentifier() &&
        Method->getName() == "swap" && !Method->isStatic() &&
        !Method->isVariadic() && Method->getNumParams() == 1 &&
        Call->getNumArgs() == 1 && Function->getReturnType()->isVoidType() &&
        Method->hasBody() && approvedStandardSDKDeclaration(S, SM, Method) &&
        cstddefOrigin(S, SM, Method->getLocation(), "libcxx",
                      "__utility/pair.h") &&
        S.owns(SM, Reference->getExprLoc()) &&
        utilityScalar(Context, Pair->First->getType()) &&
        utilityScalar(Context, Pair->Second->getType()) &&
        Context.hasSameUnqualifiedType(
            MemberCall->getImplicitObjectArgument()->getType(),
            Context.getRecordType(Pair->Record)) &&
        Context.hasSameUnqualifiedType(Call->getArg(0)->getType(),
                                       Context.getRecordType(Pair->Record)))
      return UtilityOperation::PairMemberSwap;
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
  if (!Function->isInlined() &&
      !(Origin->Path == "__algorithm/remove.h" && Name == "remove"))
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
  auto AlgorithmEqualityPointerParameter = [&](unsigned Index) {
    if (Index >= Function->getNumParams() || Index >= Call->getNumArgs())
      return false;
    auto Parameter = Function->getParamDecl(Index)->getType();
    return utilityAlgorithmEqualityPointer(Context, Parameter) &&
           Same(Call->getArg(Index)->getType(), Parameter);
  };
  auto SameAlgorithmElement = [&](QualType Left, QualType Right) {
    return utilityAlgorithmScalarPointer(Context, Left) &&
           utilityAlgorithmScalarPointer(Context, Right) &&
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
    return (!Element->isEnumeralType() && Element->isIntegerType() &&
            Context.getTypeSize(Element) <= 64) ||
           Element->isSpecificBuiltinType(BuiltinType::Float) ||
           Element->isSpecificBuiltinType(BuiltinType::Double);
  };
  auto AlgorithmValueParameter = [&](unsigned ValueIndex,
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
  if ((Origin->Path == "__algorithm/find.h" ||
       Origin->Path == "__algorithm/count.h") &&
      (Name == "find" || Name == "count") && Call->getNumArgs() == 3 &&
      Function->getNumParams() == 3 && Call->isPRValue() &&
      AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType())) {
    auto Iterator = Function->getParamDecl(0)->getType();
    if (AlgorithmValueParameter(2, 0)) {
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
      (Call->getNumArgs() == 3 || Call->getNumArgs() == 4) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      Function->getReturnType()->isBooleanType() &&
      Same(Call->getType(), Function->getReturnType()) &&
      AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      AlgorithmEqualityPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      SameAlgorithmElement(Function->getParamDecl(0)->getType(),
                           Function->getParamDecl(2)->getType())) {
    if (Call->getNumArgs() == 3)
      return UtilityOperation::AlgorithmEqual;
    if (AlgorithmEqualityPointerParameter(3) &&
        Same(Function->getParamDecl(2)->getType(),
             Function->getParamDecl(3)->getType()))
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
      AlgorithmPointerParameter(1) && AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      SameAlgorithmElement(Function->getParamDecl(0)->getType(),
                           Function->getParamDecl(2)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(2)->getType()) &&
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
      AlgorithmValueParameter(2, 0) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmFill;
  if (Origin->Path == "__algorithm/fill_n.h" && Name == "fill_n" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      AlgorithmCountParameter(1) && AlgorithmValueParameter(2, 0) &&
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
      AlgorithmPointerParameter(1) && AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      SameAlgorithmElement(Function->getParamDecl(0)->getType(),
                           Function->getParamDecl(2)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(2)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmReverseCopy;
  if (((Origin->Path == "__algorithm/min_element.h" && Name == "min_element") ||
       (Origin->Path == "__algorithm/max_element.h" &&
        Name == "max_element")) &&
      Call->getNumArgs() == 2 && Function->getNumParams() == 2 &&
      Call->isPRValue() && AlgorithmOrderedPointerParameter(0) &&
      AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return Name == "min_element" ? UtilityOperation::AlgorithmMinElement
                                 : UtilityOperation::AlgorithmMaxElement;
  if (((Origin->Path == "__algorithm/lower_bound.h" && Name == "lower_bound") ||
       (Origin->Path == "__algorithm/upper_bound.h" && Name == "upper_bound") ||
       (Origin->Path == "__algorithm/binary_search.h" &&
        Name == "binary_search")) &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmOrderedPointerParameter(0) &&
      AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmValueParameter(2, 0) &&
      Same(Call->getType(), Function->getReturnType())) {
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
      Call->getNumArgs() == 2 && Function->getNumParams() == 2 &&
      Call->isPRValue() && AlgorithmOrderedPointerParameter(0) &&
      AlgorithmPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (Name == "is_sorted" && Function->getReturnType()->isBooleanType())
      return UtilityOperation::AlgorithmIsSorted;
    if (Name == "is_sorted_until" &&
        Same(Function->getReturnType(), Function->getParamDecl(0)->getType()))
      return UtilityOperation::AlgorithmIsSortedUntil;
  }
  if (Origin->Path == "__algorithm/adjacent_find.h" &&
      Name == "adjacent_find" && Call->getNumArgs() == 2 &&
      Function->getNumParams() == 2 && Call->isPRValue() &&
      AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmAdjacentFind;
  if (Origin->Path == "__algorithm/remove.h" && Name == "remove" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      AlgorithmValueParameter(2, 0) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmRemove;
  if (Origin->Path == "__algorithm/remove_copy.h" && Name == "remove_copy" &&
      Call->getNumArgs() == 4 && Function->getNumParams() == 4 &&
      Call->isPRValue() && AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) && AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      SameAlgorithmElement(Function->getParamDecl(0)->getType(),
                           Function->getParamDecl(2)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(2)->getType()) &&
      AlgorithmValueParameter(3, 0) &&
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
      AlgorithmValueParameter(2, 0) && AlgorithmValueParameter(3, 0) &&
      Function->getReturnType()->isVoidType() &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmReplace;
  if (Origin->Path == "__algorithm/replace_copy.h" && Name == "replace_copy" &&
      Call->getNumArgs() == 5 && Function->getNumParams() == 5 &&
      Call->isPRValue() && AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) && AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      SameAlgorithmElement(Function->getParamDecl(0)->getType(),
                           Function->getParamDecl(2)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(2)->getType()) &&
      AlgorithmValueParameter(3, 0) && AlgorithmValueParameter(4, 0) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmReplaceCopy;
  if (Origin->Path == "__algorithm/unique.h" && Name == "unique" &&
      Call->getNumArgs() == 2 && Function->getNumParams() == 2 &&
      Call->isPRValue() && AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(0)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmUnique;
  if (Origin->Path == "__algorithm/unique_copy.h" && Name == "unique_copy" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) && AlgorithmPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      SameAlgorithmElement(Function->getParamDecl(0)->getType(),
                           Function->getParamDecl(2)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(2)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmUniqueCopy;
  if (((Origin->Path == "__algorithm/search.h" && Name == "search") ||
       (Origin->Path == "__algorithm/find_end.h" && Name == "find_end") ||
       (Origin->Path == "__algorithm/find_first_of.h" &&
        Name == "find_first_of")) &&
      Call->getNumArgs() == 4 && Function->getNumParams() == 4 &&
      Call->isPRValue() && AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      AlgorithmEqualityPointerParameter(2) &&
      AlgorithmEqualityPointerParameter(3) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getParamDecl(2)->getType(),
           Function->getParamDecl(3)->getType()) &&
      SameAlgorithmElement(Function->getParamDecl(0)->getType(),
                           Function->getParamDecl(2)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    if (Name == "search")
      return UtilityOperation::AlgorithmSearch;
    if (Name == "find_end")
      return UtilityOperation::AlgorithmFindEnd;
    return UtilityOperation::AlgorithmFindFirstOf;
  }
  if (Origin->Path == "__algorithm/search_n.h" && Name == "search_n" &&
      Call->getNumArgs() == 4 && Function->getNumParams() == 4 &&
      Call->isPRValue() && AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      AlgorithmCountParameter(2) && AlgorithmValueParameter(3, 0) &&
      Same(Function->getReturnType(), Function->getParamDecl(0)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmSearchN;
  if (Origin->Path == "__algorithm/mismatch.h" && Name == "mismatch" &&
      (Call->getNumArgs() == 3 || Call->getNumArgs() == 4) &&
      Function->getNumParams() == Call->getNumArgs() && Call->isPRValue() &&
      AlgorithmEqualityPointerParameter(0) &&
      AlgorithmEqualityPointerParameter(1) &&
      AlgorithmEqualityPointerParameter(2) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      SameAlgorithmElement(Function->getParamDecl(0)->getType(),
                           Function->getParamDecl(2)->getType()) &&
      Same(Call->getType(), Function->getReturnType())) {
    auto Pair = approvedUtilityPairRecord(
        S, SM, Function->getReturnType()->getAsCXXRecordDecl(), Context);
    if (Pair &&
        Same(Pair->First->getType(), Function->getParamDecl(0)->getType()) &&
        Same(Pair->Second->getType(), Function->getParamDecl(2)->getType()) &&
        (Call->getNumArgs() == 3 ||
         (AlgorithmEqualityPointerParameter(3) &&
          Same(Function->getParamDecl(2)->getType(),
               Function->getParamDecl(3)->getType()))))
      return UtilityOperation::AlgorithmMismatch;
  }
  if (Origin->Path == "__algorithm/copy_n.h" && Name == "copy_n" &&
      Call->getNumArgs() == 3 && Function->getNumParams() == 3 &&
      Call->isPRValue() && AlgorithmPointerParameter(0) &&
      AlgorithmCountParameter(1) && AlgorithmPointerParameter(2) &&
      SameAlgorithmElement(Function->getParamDecl(0)->getType(),
                           Function->getParamDecl(2)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(2)->getType()) &&
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
      AlgorithmPointerParameter(3) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(1)->getType()) &&
      Same(Function->getParamDecl(0)->getType(),
           Function->getParamDecl(2)->getType()) &&
      SameAlgorithmElement(Function->getParamDecl(0)->getType(),
                           Function->getParamDecl(3)->getType()) &&
      utilityAlgorithmWritableScalarPointer(
          Context, Function->getParamDecl(3)->getType()) &&
      Same(Function->getReturnType(), Function->getParamDecl(3)->getType()) &&
      Same(Call->getType(), Function->getReturnType()))
    return UtilityOperation::AlgorithmRotateCopy;
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
    const auto Pair = approvedUtilityPairRecord(
        S, SM, Call->getType()->getAsCXXRecordDecl(), Context);
    if (!Pair || !utilityScalar(Context, Pair->First->getType()) ||
        !utilityScalar(Context, Pair->Second->getType()))
      return std::nullopt;
    for (unsigned I = 0; I != 2; ++I) {
      auto Parameter = Function->getParamDecl(I)->getType();
      if (!Parameter->isReferenceType() ||
          !Context.hasSameUnqualifiedType(Call->getArg(I)->getType(),
                                          Parameter->getPointeeType()))
        return std::nullopt;
    }
    return UtilityOperation::MakePair;
  }
  if (Origin->Path == "__utility/pair.h" && Call->getNumArgs() == 2 &&
      Call->isPRValue() && Function->getReturnType()->isBooleanType() &&
      Same(Call->getType(), Function->getReturnType())) {
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
    const auto *LeftRecord =
        Call->getArg(0)->getType()->getAsCXXRecordDecl();
    const auto *RightRecord =
        Call->getArg(1)->getType()->getAsCXXRecordDecl();
    const auto Left = approvedUtilityPairRecord(S, SM, LeftRecord, Context);
    const auto Right = approvedUtilityPairRecord(S, SM, RightRecord, Context);
    if (Operator && Left && Right &&
        Left->Record->getCanonicalDecl() == Right->Record->getCanonicalDecl() &&
        utilityScalar(Context, Left->First->getType()) &&
        utilityScalar(Context, Left->Second->getType()) &&
        !Left->First->getType().isVolatileQualified() &&
        !Left->Second->getType().isVolatileQualified()) {
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
        utilityArrayComparable(S, SM, Context, Left->ElementType)) {
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
        utilityArrayTriviallyAssignable(Context, Left->ElementType) &&
        Same(Call->getArg(0)->getType(), LeftType->getPointeeType()) &&
        Same(Call->getArg(1)->getType(), RightType->getPointeeType()))
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
    const auto Left = approvedUtilityPairRecord(
        S, SM, LeftType->isReferenceType()
                   ? LeftType->getPointeeType()->getAsCXXRecordDecl()
                   : nullptr,
        Context);
    const auto Right = approvedUtilityPairRecord(
        S, SM, RightType->isReferenceType()
                   ? RightType->getPointeeType()->getAsCXXRecordDecl()
                   : nullptr,
        Context);
    if (LeftType->isLValueReferenceType() &&
        RightType->isLValueReferenceType() && Left && Right &&
        Left->Record->getCanonicalDecl() == Right->Record->getCanonicalDecl() &&
        utilityScalar(Context, Left->First->getType()) &&
        utilityScalar(Context, Left->Second->getType()) &&
        Same(Call->getArg(0)->getType(), LeftType->getPointeeType()) &&
        Same(Call->getArg(1)->getType(), RightType->getPointeeType()))
      return UtilityOperation::PairSwap;
  }
  if (Origin->Path == "__utility/pair.h" && Name == "get" &&
      Call->getNumArgs() == 1) {
    const auto *Arguments = Function->getTemplateSpecializationArgs();
    auto Parameter = Function->getParamDecl(0)->getType();
    auto Result = Function->getReturnType();
    const auto Pair = approvedUtilityPairRecord(
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
        if (Index == 0 &&
            Context.hasSameUnqualifiedType(Result->getPointeeType(),
                                           Pair->First->getType()))
          return UtilityOperation::PairGetFirst;
        if (Index == 1 &&
            Context.hasSameUnqualifiedType(Result->getPointeeType(),
                                           Pair->Second->getType()))
          return UtilityOperation::PairGetSecond;
      }
      if (Arguments->size() == 2 &&
          Arguments->get(0).getKind() == TemplateArgument::Type &&
          Arguments->get(1).getKind() == TemplateArgument::Type) {
        auto Selected = Result->getPointeeType().getUnqualifiedType();
        const bool First =
            Context.hasSameType(Selected, Pair->First->getType());
        const bool Second =
            Context.hasSameType(Selected, Pair->Second->getType());
        if (First != Second &&
            Context.hasSameUnqualifiedType(Result->getPointeeType(),
                                           Selected))
          return First ? UtilityOperation::PairGetFirst
                       : UtilityOperation::PairGetSecond;
      }
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
