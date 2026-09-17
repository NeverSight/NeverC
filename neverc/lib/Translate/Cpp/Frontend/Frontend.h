#ifndef NEVERC_CPP_FRONTEND_H
#define NEVERC_CPP_FRONTEND_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateBase.h"
#include "clang/Basic/FileEntry.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <map>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace clang {
class APValue;
class CallExpr;
class ArrayTypeTraitExpr;
class CastExpr;
class CXXConstructExpr;
class CXXNewExpr;
class CXXDefaultArgExpr;
class CXXForRangeStmt;
class CXXPseudoDestructorExpr;
class MaterializeTemporaryExpr;
class SubstNonTypeTemplateParmExpr;
class SizeOfPackExpr;
class InitListExpr;
class StringLiteral;
class TypeTraitExpr;
class Expr;
class UnaryExprOrTypeTraitExpr;
struct ASTTemplateArgumentListInfo;
class DeclContext;
class FriendDecl;
class TemplateDecl;
class TemplateArgumentList;
class NonTypeTemplateParmDecl;
class VarTemplateSpecializationDecl;
}

namespace nct {
namespace json = llvm::json;
struct Failure {};
struct ExpandedToken {
  unsigned Offset;
  std::string Bytes;
};
struct SDKFile {
  std::string Root, Path, SHA256;
};
struct ExplicitFunctionInstantiationSource {
  clang::FunctionDecl *Function;
  const clang::ASTTemplateArgumentListInfo *Arguments;
  clang::TypeSourceInfo *Type;
  clang::DeclarationNameInfo Name;
  clang::NestedNameSpecifierLoc Qualifier;
  clang::SourceLocation Location;
  bool HasAttributes;
};

// Exact friend spelling is distinct from the selected substitution source.
struct FriendFunctionSource {
  clang::FunctionDecl *Function, *Incoming, *Selected;
  clang::CXXRecordDecl *GrantingClass;
};
// The copied primary and its original spelling are separate from inner uses.
struct FriendFunctionTemplateSource {
  clang::FunctionTemplateDecl *Template;
  clang::FunctionDecl *Incoming, *Selected;
  clang::CXXRecordDecl *GrantingClass;
};
// A canonical primary can have a different lexical owner from its actual body.
struct FunctionTemplateBodySource {
  clang::FunctionDecl *Function;
  clang::FunctionTemplateDecl *Compatible;
  const clang::FunctionDecl *Pattern;
  clang::DeclContext *LexicalContext;
};

// A class-friend copy joins a semantic target independently of its granting source.
struct FriendClassTemplateSource {
  clang::ClassTemplateDecl *Template, *Written;
  clang::CXXRecordDecl *GrantingClass;
  clang::DeclContext *Context;
  clang::ClassTemplateDecl *Previous;
};

struct FriendDeclarationSource {
  clang::FriendDecl *Declaration, *Written;
};

struct ExplicitStaticDataInstantiationSource {
  clang::VarDecl *Variable;
  clang::TypeSourceInfo *Type;
  clang::NestedNameSpecifierLoc Qualifier;
  clang::SourceLocation Location;
  bool HasAttributes;
};

// Explicit ordinary member-class directives have no separate AST declaration.
// Keep every written occurrence, including a successful no-effect directive.
struct ExplicitMemberClassInstantiationSource {
  clang::CXXRecordDecl *Record, *Origin;
  clang::NestedNameSpecifierLoc Qualifier;
  clang::SourceLocation Location, TemplateLocation, ExternLocation;
  bool HasAttributes;
};

// Each successful template use owns the defaults selected by that deduction.
// Type sugar and written locations remain distinct from canonical identities.
struct TemplateDefaultArgumentSource {
  const clang::NamedDecl *Parameter;
  clang::TemplateArgumentLoc Written, Converted;
};
struct NonTypeParameterSource {
  const clang::NamedDecl *Parameter;
  clang::TypeSourceInfo *Type;
  unsigned PackIndex; // Forward index, or ~0u for a deduced empty pack's type.
};
enum class TemplateSourceKind {
  Type, Function, ClassDeclaration, ClassFullDeclaration, PartialDeclaration, PartialDeduction, PartialPattern,
  VariableUse, VariableDeclaration, VariablePartialDeduction, VariablePartialPattern
};
struct TemplateUseSource {
  TemplateSourceKind Kind;
  clang::NamedDecl *Template; // A primary/alias or a partial parameter owner.
  const clang::Decl *Declaration;
  const clang::Type *Type;
  clang::TypeSourceInfo *Underlying;
  const clang::ASTTemplateArgumentListInfo *Written;
  const clang::TemplateArgumentList *Canonical, *Sugared;
  std::vector<TemplateDefaultArgumentSource> Defaults;
  std::vector<NonTypeParameterSource> ParameterTypes;
  clang::SourceLocation Location;
  bool DefaultsOverflow, Instantiation;
  // Identity of the successful deduction, transferred unchanged by Sema to
  // the selected instance: canonical for classes, sugared for variables.
  // Argument values alone do not identify the successful candidate.
  const clang::TemplateArgumentList *Selection;
  bool WrittenStorageClass;
  const clang::NamedDecl *Origin = nullptr; // Exact copied partial/full class source; null for a written event.
};
// Retain each type substitution already performed by Sema. The final
// VarDecl may keep its first declaration's TypeSourceInfo after completion.
struct VariableTypeSource {
  const clang::VarTemplateSpecializationDecl *Variable, *Previous;
  const clang::VarDecl *Pattern;
  clang::TypeSourceInfo *Type;
  clang::SourceLocation Location;
  bool Completion;
};
// The final semantic call/construction can have a different display location
// from the candidate set that performed its successful template deduction.
struct SelectedTemplateCallSource {
  const clang::Expr *Expression;
  const clang::FunctionDecl *Function;
  clang::SourceLocation Location;
};
struct OperationTraitSource {
  const clang::Expr *Root;
  std::vector<const clang::Expr *> Operands;
  bool Attempted, Complete;
  const clang::CXXDestructorDecl *Destructor;
  const clang::FunctionProtoType *DestructionPrototype;
  bool DestructionLookupAttempted, DestructionExceptionAttempted;
};
struct FunctionSpecializationSource {
  const clang::FunctionDecl *Declaration, *Selected;
  const clang::ASTTemplateArgumentListInfo *Written;
  clang::SourceLocation Location;
};

struct State {
  std::string Root, Source, Relative, Target, SourceText;
  std::string Profile = "cpp-core-v1", ConfigurationID, WorkingDirectory;
  std::map<std::string, std::string> Dependencies;
  std::map<unsigned, std::vector<ExpandedToken>> ExpandedTokens;
  std::map<std::string, std::string> SDKRoots;
  std::map<std::pair<std::string, std::string>, std::string> SDKHeaders,
      SDKDependencies;
  std::map<std::string, llvm::sys::fs::UniqueID> SDKVirtualFiles;
  std::string SDKDistribution, SDKCatalogHash;
  mutable std::map<std::string, std::string> PathCache;
  std::vector<std::string> Arguments;
  json::Array Diagnostics;
  json::Object Module;
  void diagnose(llvm::StringRef Code, llvm::StringRef Construct,
                llvm::StringRef Reason, llvm::StringRef Guidance,
                unsigned Line = 1, unsigned Column = 1,
                llvm::StringRef File = {});
  bool coreV2() const { return Profile == "cpp-core-v2"; }
  bool project() const { return Profile == "cpp-project-v1" || math(); }
  bool math() const { return Profile == "cpp-math-v1"; }
  bool sdk() const { return math() || (coreV2() && !SDKDistribution.empty()); }
  bool configureSDK(const json::Object &SDK);
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> createFileSystem();
  std::optional<SDKFile> sdkFile(llvm::StringRef Path) const;
  std::optional<SDKFile> sdkFile(clang::FileEntryRef File) const;
  std::optional<SDKFile> sdkFile(const clang::SourceManager &SM,
                                 clang::SourceLocation L) const;
  bool consumeSDKFile(const clang::SourceManager &SM, clang::FileID ID);
  void addSDKMetadata();
  std::string relativePath(llvm::StringRef Path) const;
  std::string sourcePath(const clang::SourceManager &SM,
                         clang::SourceLocation L) const;
  bool owns(const clang::SourceManager &SM, clang::SourceLocation L) const;
};

bool approvedSDKDeclaration(const State &S, const clang::SourceManager &SM,
                            const clang::Decl *D);
bool approvedStandardSDKDeclaration(const State &S,
                                    const clang::SourceManager &SM,
                                    const clang::Decl *D);
std::optional<llvm::APSInt>
approvedSDKIntegerConstant(const State &S, const clang::SourceManager &SM,
                           const clang::VarDecl *D,
                           const clang::ASTContext &Context);
bool approvedNumericLimitsConstant(const State &S,
                                   const clang::SourceManager &SM,
                                   const clang::CallExpr *Call,
                                   clang::ASTContext &Context,
                                   clang::APValue &Value);
enum class CstddefOperation {
  BitOr,
  BitAnd,
  BitXor,
  BitNot,
  BitOrAssign,
  BitAndAssign,
  BitXorAssign,
  ShiftLeft,
  ShiftRight,
  ShiftLeftAssign,
  ShiftRightAssign,
  ToInteger,
};
std::optional<CstddefOperation>
approvedCstddefOperation(const State &S, const clang::SourceManager &SM,
                         const clang::CallExpr *Call,
                         const clang::ASTContext &Context);
enum class UtilityOperation {
  Move,
  Forward,
  MoveIfNoexcept,
  AsConst,
  Exchange,
  Swap,
  MakePair,
  PairSwap,
  PairMemberSwap,
  PairEqual,
  PairNotEqual,
  PairLess,
  PairGreater,
  PairLessEqual,
  PairGreaterEqual,
  PairGetFirst,
  PairGetSecond,
  ArraySize,
  ArrayMaxSize,
  ArrayEmpty,
  ArrayData,
  ArrayBegin,
  ArrayEnd,
  ArraySubscript,
  ArrayAt,
  ArrayFront,
  ArrayBack,
  ArrayFill,
  ArraySwap,
  ArrayMemberSwap,
  ArrayEqual,
  ArrayNotEqual,
  ArrayLess,
  ArrayGreater,
  ArrayLessEqual,
  ArrayGreaterEqual,
  ArrayGet,
  IteratorBegin,
  IteratorEnd,
  IteratorSize,
  IteratorEmpty,
  IteratorData,
  IteratorAdvance,
  IteratorDistance,
  IteratorNext,
  IteratorPrev,
  IteratorRBegin,
  IteratorREnd,
  MakeReverseIterator,
  ReverseBase,
  ReverseDereference,
  ReverseArrow,
  ReversePreIncrement,
  ReversePostIncrement,
  ReversePreDecrement,
  ReversePostDecrement,
  ReverseAdd,
  ReverseAddAssign,
  ReverseSubtract,
  ReverseSubtractAssign,
  ReverseSubscript,
  ReverseEqual,
  ReverseNotEqual,
  ReverseLess,
  ReverseGreater,
  ReverseLessEqual,
  ReverseGreaterEqual,
  ReverseDifference,
  ReverseAddLeft,
  ArrayRBegin,
  ArrayREnd,
  AlgorithmFind,
  AlgorithmCount,
  AlgorithmEqual,
  AlgorithmCopy,
  AlgorithmMove,
  AlgorithmCopyBackward,
  AlgorithmMoveBackward,
  AlgorithmFill,
  AlgorithmFillN,
  AlgorithmSwapRanges,
  AlgorithmReverse,
  AlgorithmReverseCopy,
  AlgorithmMinElement,
  AlgorithmMaxElement,
  AlgorithmLowerBound,
  AlgorithmUpperBound,
  AlgorithmBinarySearch,
  AlgorithmIsSorted,
  AlgorithmIsSortedUntil,
  AlgorithmAdjacentFind,
  AlgorithmRemove,
  AlgorithmRemoveCopy,
  AlgorithmReplace,
  AlgorithmReplaceCopy,
  AlgorithmUnique,
  AlgorithmUniqueCopy,
  AlgorithmSearch,
  AlgorithmFindEnd,
  AlgorithmFindFirstOf,
  AlgorithmSearchN,
  AlgorithmMismatch,
  AlgorithmCopyN,
  AlgorithmIterSwap,
  AlgorithmRotate,
  AlgorithmRotateCopy,
  AlgorithmEqualRange,
  AlgorithmLexicographicalCompare,
  AlgorithmIncludes,
  AlgorithmMerge,
  AlgorithmSetUnion,
  AlgorithmSetIntersection,
  AlgorithmSetDifference,
  AlgorithmSetSymmetricDifference,
  AlgorithmMin,
  AlgorithmMax,
  AlgorithmClamp,
  AlgorithmMinmax,
  AlgorithmMinmaxElement,
  AlgorithmIsHeap,
  AlgorithmIsHeapUntil,
  AlgorithmMakeHeap,
  AlgorithmPushHeap,
  AlgorithmPopHeap,
  AlgorithmSortHeap,
  AlgorithmSort,
  AlgorithmStableSort,
  AlgorithmInplaceMerge,
  AlgorithmPartialSort,
  AlgorithmPartialSortCopy,
  AlgorithmNthElement,
  AlgorithmNextPermutation,
  AlgorithmPrevPermutation,
  AlgorithmIsPermutation,
  AlgorithmFindIf,
  AlgorithmFindIfNot,
  AlgorithmCountIf,
  AlgorithmAllOf,
  AlgorithmAnyOf,
  AlgorithmNoneOf,
  AlgorithmCopyIf,
  AlgorithmRemoveIf,
  AlgorithmRemoveCopyIf,
  AlgorithmReplaceIf,
  AlgorithmReplaceCopyIf,
  AlgorithmIsPartitioned,
  AlgorithmPartition,
  AlgorithmStablePartition,
  AlgorithmPartitionCopy,
  AlgorithmPartitionPoint,
  AlgorithmForEach,
  AlgorithmForEachN,
  AlgorithmTransformUnary,
  AlgorithmTransformBinary,
  AlgorithmGenerate,
  AlgorithmGenerateN,
};
struct UtilityPairRecord {
  const clang::CXXRecordDecl *Record;
  const clang::FieldDecl *First, *Second;
};
bool approvedUtilityPairMetadata(const State &S,
                                 const clang::SourceManager &SM,
                                 const clang::CXXRecordDecl *Record);
enum class UtilityPairConstruction {
  Default,
  Elements,
  CopyOrMove,
};
std::optional<UtilityPairRecord>
approvedUtilityPairRecord(const State &S, const clang::SourceManager &SM,
                          const clang::CXXRecordDecl *Record,
                          const clang::ASTContext &Context);
std::optional<UtilityPairRecord> approvedUtilityReferencePairRecord(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXRecordDecl *Record, const clang::ASTContext &Context);
std::optional<UtilityPairConstruction>
approvedUtilityPairConstruction(const State &S,
                                const clang::SourceManager &SM,
                                const clang::CXXConstructExpr *Construction,
                                const clang::ASTContext &Context);
std::optional<UtilityPairRecord>
approvedUtilityPairAssignment(const State &S,
                              const clang::SourceManager &SM,
                              const clang::CXXOperatorCallExpr *Assignment,
                              const clang::ASTContext &Context);
struct UtilityArrayRecord {
  const clang::CXXRecordDecl *Record;
  const clang::FieldDecl *Elements;
  clang::QualType ElementType;
  uint64_t Size;
};
bool approvedUtilityArrayMetadata(const State &S,
                                  const clang::SourceManager &SM,
                                  const clang::CXXRecordDecl *Record);
std::optional<UtilityArrayRecord>
approvedUtilityArrayRecord(const State &S, const clang::SourceManager &SM,
                           const clang::CXXRecordDecl *Record,
                           const clang::ASTContext &Context);
bool approvedUtilityArrayConstruction(const State &S,
                                      const clang::SourceManager &SM,
                                      const clang::CXXConstructExpr *Construction,
                                      const clang::ASTContext &Context);
std::optional<UtilityArrayRecord>
approvedUtilityArrayAssignment(const State &S,
                               const clang::SourceManager &SM,
                               const clang::CXXOperatorCallExpr *Assignment,
                               const clang::ASTContext &Context);
struct UtilityReverseIteratorRecord {
  const clang::CXXRecordDecl *Record;
  const clang::FieldDecl *Legacy, *Current;
  clang::QualType IteratorType;
};
bool approvedUtilityReverseIteratorMetadata(const State &S,
                                            const clang::SourceManager &SM,
                                            const clang::CXXRecordDecl *Record);
std::optional<UtilityReverseIteratorRecord>
approvedUtilityReverseIteratorRecord(const State &S,
                                     const clang::SourceManager &SM,
                                     const clang::CXXRecordDecl *Record,
                                     const clang::ASTContext &Context);
enum class UtilityReverseIteratorConstruction {
  Default,
  Iterator,
  CopyOrMove,
  Converting,
};
std::optional<UtilityReverseIteratorConstruction>
approvedUtilityReverseIteratorConstruction(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXConstructExpr *Construction,
    const clang::ASTContext &Context);
struct UtilityReverseIteratorAssignment {
  UtilityReverseIteratorRecord Destination, Source;
  bool Converting;
};
std::optional<UtilityReverseIteratorAssignment>
approvedUtilityReverseIteratorAssignment(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXOperatorCallExpr *Assignment,
    const clang::ASTContext &Context);
std::optional<UtilityOperation>
approvedUtilityOperation(const State &S, const clang::SourceManager &SM,
                         const clang::CallExpr *Call,
                         const clang::ASTContext &Context);
bool approvedUtilityDefaultArgument(const State &S,
                                    const clang::SourceManager &SM,
                                    const clang::CXXDefaultArgExpr *Default,
                                    const clang::FunctionDecl *Function,
                                    unsigned Index, clang::ASTContext &Context);
bool approvedUtilityConstant(const State &S, const clang::SourceManager &SM,
                             const clang::CallExpr *Call,
                             clang::ASTContext &Context,
                             clang::APValue &Value);
bool approvedCstddefNull(const State &S, const clang::SourceManager &SM,
                        const clang::Expr *Expression);
bool approvedCstddefTypeQuery(const State &S,
                             const clang::SourceManager &SM,
                             const clang::UnaryExprOrTypeTraitExpr *Query,
                             clang::ASTContext &Context);
std::optional<llvm::APSInt>
approvedCstddefOffset(const State &S, const clang::SourceManager &SM,
                      const clang::Expr *Expression,
                      clang::ASTContext &Context);

std::string digest(llvm::StringRef Text);
bool validExportName(llvm::StringRef Name);
bool isolateProjectEnvironment();
bool ordinaryMethod(const clang::CXXMethodDecl *Method);
bool ordinaryOperator(const clang::FunctionDecl *Function);
bool ordinaryConversion(const clang::CXXConversionDecl *Conversion);
const clang::CallExpr *userConversionCall(const clang::CastExpr *Cast,
                                        clang::ASTContext &Context);
bool ordinaryCopyAssignment(const clang::CXXMethodDecl *Method);
bool defaultedCopyAssignment(const clang::CXXMethodDecl *Method);
bool defaultedMoveAssignment(const clang::CXXMethodDecl *Method);
bool defaultedAssignment(const clang::CXXMethodDecl *Method);
bool supportedCopyAssignment(const clang::CXXMethodDecl *Method);
bool ordinaryMoveAssignment(const clang::CXXMethodDecl *Method);
bool supportedAssignment(const clang::CXXMethodDecl *Method);
struct GeneratedArrayAssignment {
  const clang::Expr *Destination, *Source;
  clang::QualType Type;
};
std::optional<GeneratedArrayAssignment> generatedArrayAssignment(
    const clang::CallExpr *Call, const clang::CXXMethodDecl *Owner,
    clang::ASTContext &Context);
bool callableMethod(const clang::CXXMethodDecl *Method);
bool fullExpressionTemporary(const clang::MaterializeTemporaryExpr *Temporary,
                             clang::ASTContext &Context);
const clang::VarDecl *automaticTemporaryOwner(
    const clang::MaterializeTemporaryExpr *Temporary, clang::ASTContext &Context);
const clang::Expr *referenceListInitializer(const clang::InitListExpr *List,
                                          clang::ASTContext &Context);
const clang::InitListExpr *emptyVoidInitializer(const clang::Expr *Expression);
std::optional<unsigned> concretePackSize(const clang::SizeOfPackExpr *E);
const clang::Expr *scalarTemplateReplacement(
    const clang::SubstNonTypeTemplateParmExpr *Substitution,
    clang::ASTContext &Context);
const clang::Expr *defaultArgumentInitializer(const clang::ParmVarDecl *Parameter,
                                               clang::ASTContext &Context);
const clang::Expr *selectedDefaultArgument(const clang::CXXDefaultArgExpr *Default,
                                          clang::ASTContext &Context);
struct RangeForComponents {
  const clang::VarDecl *Range, *Begin, *End, *Variable;
};
std::optional<RangeForComponents> rangeForComponents(const clang::CXXForRangeStmt *Loop);
bool ordinaryConstructor(const clang::CXXConstructorDecl *Constructor);
const clang::CXXConstructExpr *constructorConversion(const clang::CastExpr *Cast,
                                                   clang::ASTContext &Context);
bool ordinaryDestructor(const clang::CXXDestructorDecl *Destructor);
bool defaultedLifecycle(const clang::CXXMethodDecl *Method);
bool defaultedCopyConstructor(const clang::CXXConstructorDecl *Constructor);
bool defaultedMoveConstructor(const clang::CXXConstructorDecl *Constructor);
bool defaultedCopyOrMoveConstructor(const clang::CXXConstructorDecl *Constructor);
bool supportedConstructor(const clang::CXXConstructorDecl *Constructor);
bool needsDestruction(clang::QualType Type);
const clang::CXXPseudoDestructorExpr *scalarDestruction(
    const clang::CallExpr *Call, clang::ASTContext &Context);
const clang::Expr *directMethodReference(const clang::CallExpr *Call);
const clang::Expr *directFunctionReference(const clang::CallExpr *Call);

// Canonical integral carrier spellings after Clang resolves source types.
inline unsigned integerBits(llvm::StringRef T) {
  if (T == "bool") return 1;
  if (T == "i8" || T == "u8") return 8;
  if (T == "i16" || T == "u16") return 16;
  if (T == "i64" || T == "u64") return 64;
  if (T == "int" || T == "uint") return 32;
  return 0;
}
inline bool unsignedInteger(llvm::StringRef T) {
  return T == "uint" || T == "u8" || T == "u16" || T == "u64" || T == "bool";
}

struct StaticDestruction {
  std::string Global, Function;
  clang::QualType Type;
  clang::SourceLocation Location;
};
struct CheckedEmptyBase {
  const clang::CXXRecordDecl *Derived, *Base;
  const clang::CXXBaseSpecifier *Specifier;
  const clang::TypeSourceInfo *Source;
  std::string Member;
};
struct ArrayAllocationLayout {
  clang::QualType Element;
  uint64_t ElementBytes = 0, CookieBytes = 0, CountOffset = 0;
  bool StoresElementSize = false;
};
enum class RuntimeArrayInitialization { None, Zero, DefaultConstruction, Aggregate };
struct ArrayNewInfo {
  std::optional<uint64_t> Count;
  const clang::Expr *BoundBeforeConversion = nullptr;
  const clang::Expr *Initializer = nullptr, *Filler = nullptr;
  uint64_t PrefixCount = 0;
  RuntimeArrayInitialization Repeated = RuntimeArrayInitialization::None;
};
bool omittedDefaultConstruction(const clang::Expr *Init, clang::QualType Element,
                                clang::ASTContext &Context);
class Adapter {
public:
  State &S;
  clang::ASTContext &Context;
  clang::SourceManager &Sources;
  std::map<const clang::Decl *, std::string> Names;
  std::map<const clang::FunctionTemplateDecl *, std::size_t> TemplateOrdinals;
  std::map<const clang::FunctionTemplateDecl *, const clang::CXXRecordDecl *>
      FriendTemplateIdentityOwners;
  std::vector<clang::FunctionDecl *> Functions;
  std::vector<clang::CXXRecordDecl *> Records;
  std::vector<const clang::CXXRecordDecl *> Destructions;
  std::set<const clang::CXXRecordDecl *> RequiredDestructions;
  std::set<const clang::CXXRecordDecl *> BaseConstructorRecords;
  std::vector<const clang::CXXConstructorDecl *> BaseConstructors;
  std::map<const clang::CXXConstructorDecl *, const clang::CXXConstructorDecl *> RequiredBaseConstructors;
  std::vector<StaticDestruction> StaticDestructions;
  std::vector<clang::VarDecl *> Globals;
  std::map<const clang::VarDecl *, json::Object> ConstantStaticInitializers;
  std::map<const clang::VarDecl *, json::Object> StaticReferenceInitializers;
  std::set<const clang::VarDecl *> ConstantStaticTemporaryOwners;
  std::set<const clang::VarDecl *> CheckedConstantTemporaryOccurrences;
  std::set<const clang::Expr *> SeparateArrayFillers;
  std::map<const clang::TypeTraitExpr *, OperationTraitSource> OperationTraits;
  bool CheckingSource = false;
  std::vector<const clang::TypeTraitExpr *> PendingOperationQueries;
  std::set<const clang::TypeTraitExpr *> DeferredOperationQueries, VerifiedOperationQueries;
  std::map<const clang::StringLiteral *, std::string> StringObjects;
  json::Array StringGlobals;
  std::map<const clang::MaterializeTemporaryExpr *, std::string> StaticTemporaryObjects;
  std::size_t StaticTemporarySerial = 0;
  json::Array StaticTemporaryGlobals;
  std::set<const clang::VarDecl *> StaticLocals;
  std::set<const clang::VarDecl *> DynamicStaticObjects;
  std::map<const clang::VarDecl *, llvm::APSInt> StaticMemberValues;
  std::map<const clang::Decl *, clang::FunctionDecl *> FunctionDeclarations;
  std::map<const clang::Decl *, clang::VarDecl *> GlobalDeclarations;
  std::map<std::string, json::Object> MappedFunctions;
  std::map<const clang::CXXRecordDecl *, std::size_t> StorageUnits;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityPairs;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityArrays;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityReverseIterators;
  std::map<const clang::CXXRecordDecl *, CheckedEmptyBase> EmptyBases;
  std::map<const clang::VarDecl *, const clang::CXXForRangeStmt *> RangeDeclarations;
  std::size_t ExpandedNodes = 0;
  Adapter(State &S, clang::ASTContext &C)
      : S(S), Context(C), Sources(C.getSourceManager()) {}
  json::Object loc(clang::SourceLocation L) const;
  void reject(clang::SourceLocation L, llvm::StringRef Construct,
              llvm::StringRef Reason, llvm::StringRef Code = "TR0201");
  std::string name(const clang::NamedDecl *D);
  std::string identity(const clang::NamedDecl *D);
  std::string ownerTU(const clang::NamedDecl *D) const;
  json::Object evidence(const clang::NamedDecl *D, llvm::StringRef Kind);
  void addProjectMetadata();
  std::string type(clang::QualType T, clang::SourceLocation L,
                   bool AllowVoid = false, unsigned Depth = 0);
  bool requireUtilityPair(const clang::CXXRecordDecl *Record,
                          clang::SourceLocation Location,
                          unsigned Depth = 0);
  bool requireUtilityArray(const clang::CXXRecordDecl *Record,
                           clang::SourceLocation Location,
                           unsigned Depth = 0);
  bool requireUtilityReverseIterator(const clang::CXXRecordDecl *Record,
                                     clang::SourceLocation Location,
                                     unsigned Depth = 0);
  std::string functionPointerType(clang::QualType T, clang::SourceLocation L,
                                  unsigned Depth = 0);
  bool typeClassificationValue(const clang::TypeTraitExpr *Query);
  void checkQueryType(clang::QualType T, clang::SourceLocation L,
                      bool AllowIncompleteArrays = false,
                      bool AllowIncompleteRecords = false, unsigned Depth = 0);
  void checkTypeOnly(clang::QualType T, clang::SourceLocation L);
  uint64_t arrayTypeQueryValue(const clang::ArrayTypeTraitExpr *Query);
  bool emptyBaseChainShape(const clang::CXXRecordDecl *Record);
  const CheckedEmptyBase *emptyBase(const clang::CXXRecordDecl *Record);
  const CheckedEmptyBase *emptyBaseInitializer(const clang::CXXConstructorDecl *Constructor,
                                               const clang::CXXCtorInitializer *Initializer);
  std::vector<const CheckedEmptyBase *> emptyBaseCast(const clang::CastExpr *Cast);
  bool functionAddressTarget(const clang::FunctionDecl *F, clang::SourceLocation L);
  const clang::FunctionDecl *allocationFunction(const clang::FunctionDecl *F,
                                               bool Allocate, clang::SourceLocation L,
                                               bool Array = false);
  ArrayAllocationLayout arrayAllocationLayout(clang::QualType Object,
      bool UsualDeleteWantsSize, clang::SourceLocation L);
  ArrayNewInfo arrayNewInfo(const clang::CXXNewExpr *N);
  std::string nativeHeapImport(const clang::FunctionDecl *F, clang::SourceLocation L);
  json::Object functionAddress(const clang::FunctionDecl *F, clang::SourceLocation L);
  std::size_t storageUnits(clang::QualType T, unsigned Depth = 0);
  void chargeExpansion(std::size_t Nodes, clang::SourceLocation L);
  json::Object literal(const llvm::APSInt &Value, llvm::StringRef Type,
                       clang::SourceLocation L);
  json::Object floatingLiteral(const llvm::APFloat &Value,
                               clang::SourceLocation L);
  void checkStringLiteral(const clang::StringLiteral *Literal);
  json::Object stringInitializer(const clang::StringLiteral *Literal);
  json::Object stringObject(const clang::StringLiteral *Literal);
  const clang::VarDecl *staticTemporaryOwner(const clang::MaterializeTemporaryExpr *Temporary) const;
  json::Object staticTemporaryObject(const clang::MaterializeTemporaryExpr *Temporary);
  void checkConstantTemporaryOccurrences(const clang::VarDecl *Owner);
  json::Object dynamicStaticTemporaryObject(const clang::MaterializeTemporaryExpr *Temporary,
                                            const clang::VarDecl *Owner);
  json::Object constantPointer(const clang::APValue &Value, clang::QualType T,
                               clang::SourceLocation L, bool ReferenceBinding = false);
  std::string mapping(const clang::CallExpr *Call);
  json::Object zero(clang::QualType T, clang::SourceLocation L);
  json::Object constant(const clang::APValue &V, clang::QualType T,
                        clang::SourceLocation L);
  bool registerRangeFor(const clang::CXXForRangeStmt *Loop);
  const clang::CXXForRangeStmt *rangeForOwner(const clang::VarDecl *Variable) const;
  const clang::VarDecl *temporaryOwner(const clang::MaterializeTemporaryExpr *Temporary);
  json::Object lower(clang::FunctionDecl *Function);
  std::string baseConstructorName(const clang::CXXConstructorDecl *Constructor);
  std::string requireBaseConstructor(const clang::CXXConstructorDecl *Constructor,
                                     clang::SourceLocation Location);
  json::Object lowerBaseConstructor(const clang::CXXConstructorDecl *Constructor);
  json::Object lowerConstructorBody(const clang::CXXConstructorDecl *Constructor);
  json::Object lowerCompleteConstructor(const clang::CXXConstructorDecl *Constructor);
  json::Object lowerStartup(llvm::ArrayRef<const clang::VarDecl *> Objects);
  std::string requireStaticDestruction(llvm::StringRef Global, clang::QualType Type,
                                       clang::SourceLocation Location);
  json::Object lowerStaticDestruction(const StaticDestruction &Object);
  std::string destructionName(const clang::CXXRecordDecl *Record);
  void requireDestruction(const clang::CXXRecordDecl *Record,
                          clang::SourceLocation Location);
  json::Object lowerDestruction(const clang::CXXRecordDecl *Record);
  void run(llvm::ArrayRef<ExplicitFunctionInstantiationSource> Directives = {},
           llvm::ArrayRef<ExplicitStaticDataInstantiationSource> StaticDirectives = {},
           llvm::ArrayRef<TemplateUseSource> TemplateUses = {},
           llvm::ArrayRef<FunctionSpecializationSource> Specializations = {},
           llvm::ArrayRef<VariableTypeSource> VariableTypes = {},
           llvm::ArrayRef<SelectedTemplateCallSource> SelectedCalls = {},
           llvm::ArrayRef<ExplicitMemberClassInstantiationSource> MemberClassDirectives = {},
           llvm::ArrayRef<FriendFunctionSource> FriendFunctions = {},
           llvm::ArrayRef<FriendDeclarationSource> FriendDeclarations = {},
           llvm::ArrayRef<FriendFunctionTemplateSource> FriendTemplates = {},
           llvm::ArrayRef<FunctionTemplateBodySource> TemplateBodies = {},
           llvm::ArrayRef<FriendClassTemplateSource> FriendClasses = {});
};
} // namespace nct
#endif
