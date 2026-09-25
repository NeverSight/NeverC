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
class ArrayInitLoopExpr;
class ArrayInitIndexExpr;
class CastExpr;
class CXXConstructExpr;
class CXXStdInitializerListExpr;
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
enum class FunctionalOperation {
  Plus,
  Minus,
  Multiplies,
  Divides,
  Modulus,
  Negate,
  BitAnd,
  BitOr,
  BitXor,
  BitNot,
  Equal,
  NotEqual,
  Less,
  Greater,
  LessEqual,
  GreaterEqual,
  LogicalAnd,
  LogicalOr,
  LogicalNot,
  Hash,
};
struct FunctionalObjectRecord {
  const clang::CXXRecordDecl *Record;
};
std::optional<FunctionalObjectRecord>
approvedFunctionalObjectRecord(const State &S, const clang::SourceManager &SM,
                               const clang::CXXRecordDecl *Record,
                               const clang::ASTContext &Context);
bool approvedFunctionalObjectBaseCast(
    const State &S, const clang::SourceManager &SM,
    const clang::CastExpr *Cast, const clang::ASTContext &Context);
enum class FunctionalObjectConstruction { Default, CopyOrMove };
std::optional<FunctionalObjectConstruction>
approvedFunctionalObjectConstruction(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXConstructExpr *Construction,
    const clang::ASTContext &Context);
bool approvedFunctionalObjectAssignment(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXOperatorCallExpr *Assignment,
    const clang::ASTContext &Context);
struct FunctionalOperationInfo {
  FunctionalOperation Operation;
  clang::QualType LeftType;
  clang::QualType RightType;
  clang::QualType OperationType;
  clang::QualType ResultType;
};
std::optional<FunctionalOperationInfo>
approvedFunctionalOperation(const State &S, const clang::SourceManager &SM,
                            const clang::CallExpr *Call,
                            const clang::ASTContext &Context);
std::optional<FunctionalOperationInfo> approvedDirectAlgorithmComparator(
    const State &S, const clang::SourceManager &SM,
    const clang::CallExpr *Call, const clang::ASTContext &Context);
std::optional<FunctionalOperationInfo> approvedRangeAlgorithmComparator(
    const State &S, const clang::SourceManager &SM,
    const clang::CallExpr *Call, unsigned ComparatorIndex,
    clang::QualType LeftElement, clang::QualType RightElement,
    const clang::ASTContext &Context);
struct FunctionalInvokeObjectCall {
  FunctionalOperationInfo Operation;
  const clang::CXXMethodDecl *Method;
};
std::optional<FunctionalInvokeObjectCall> approvedFunctionalInvokeObjectOperation(
    const State &S, const clang::SourceManager &SM,
    const clang::CallExpr *Call, const clang::ASTContext &Context);
struct FunctionalReferenceRecord {
  const clang::CXXRecordDecl *Record;
  clang::QualType ReferentType;
  clang::QualType PointerType;
  const clang::FieldDecl *Pointer;
  bool PaddedBase;
};
std::optional<FunctionalReferenceRecord> approvedFunctionalReferenceRecord(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXRecordDecl *Record, const clang::ASTContext &Context);
enum class FunctionalReferenceConstruction { Direct, CopyOrMove };
std::optional<FunctionalReferenceConstruction>
approvedFunctionalReferenceConstruction(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXConstructExpr *Construction,
    const clang::ASTContext &Context);
bool approvedFunctionalReferenceAssignment(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXOperatorCallExpr *Assignment,
    const clang::ASTContext &Context);
struct FunctionalReferenceFactoryCall {
  FunctionalReferenceRecord Result;
  std::optional<FunctionalReferenceRecord> Source;
  bool Constant;
};
std::optional<FunctionalReferenceFactoryCall>
approvedFunctionalReferenceFactoryCall(
    const State &S, const clang::SourceManager &SM,
    const clang::CallExpr *Call, const clang::ASTContext &Context);
struct FunctionalReferenceAccessCall {
  FunctionalReferenceRecord Wrapper;
  const clang::Expr *Object;
  bool ObjectIsArrow;
};
std::optional<FunctionalReferenceAccessCall>
approvedFunctionalReferenceAccessCall(
    const State &S, const clang::SourceManager &SM,
    const clang::CallExpr *Call, const clang::ASTContext &Context);
enum class FunctionalReferenceInvokeKind {
  Function,
  FunctionPointer,
  FunctionObject,
  UserFunctionObject,
};
struct FunctionalReferenceInvokeCall {
  FunctionalReferenceRecord Wrapper;
  FunctionalReferenceInvokeKind Kind;
  clang::QualType FunctionPointerType;
  std::optional<FunctionalOperationInfo> Operation;
  const clang::CXXMethodDecl *Method;
};
std::optional<FunctionalReferenceInvokeCall>
approvedFunctionalReferenceInvokeCall(
    const State &S, const clang::SourceManager &SM,
    const clang::CallExpr *Call, const clang::ASTContext &Context);
struct FunctionalMemberInvokeCall {
  const clang::Expr *Callable;
  const clang::Expr *Object;
  const clang::CXXMethodDecl *Method;
  const clang::FieldDecl *Field;
  const clang::CallExpr *ErasedFactory;
  const clang::CallExpr *ErasedAdapter;
  std::optional<FunctionalReferenceRecord> ObjectWrapper;
  bool ObjectIsPointer;
};
std::optional<FunctionalMemberInvokeCall>
approvedFunctionalUserInvokeCall(const State &S,
                                 const clang::SourceManager &SM,
                                 const clang::CallExpr *Call,
                                 const clang::ASTContext &Context);
std::optional<FunctionalMemberInvokeCall>
approvedUtilityTupleApplyUserCall(const State &S,
                                  const clang::SourceManager &SM,
                                  const clang::CallExpr *Call,
                                  const clang::ASTContext &Context);
struct UtilityTupleApplyObjectOperation {
  FunctionalOperationInfo Operation;
  const clang::CXXMethodDecl *Method;
};
std::optional<UtilityTupleApplyObjectOperation>
approvedUtilityTupleApplyObjectOperation(
    const State &S, const clang::SourceManager &SM,
    const clang::CallExpr *Call, const clang::ASTContext &Context);
std::optional<FunctionalReferenceInvokeCall>
approvedUtilityTupleApplyReferenceCall(
    const State &S, const clang::SourceManager &SM,
    const clang::CallExpr *Call, const clang::ASTContext &Context);
std::optional<FunctionalMemberInvokeCall>
approvedUtilityTupleApplyMemberCall(
    const State &S, const clang::SourceManager &SM,
    const clang::CallExpr *Call, const clang::ASTContext &Context);
struct FunctionalStoredMemberPointer {
  const clang::VarDecl *Variable;
  const clang::Expr *Initializer;
  const clang::CallExpr *Adapter;
  const clang::Expr *Address;
  const clang::ValueDecl *Member;
};
std::optional<FunctionalStoredMemberPointer>
approvedFunctionalStoredMemberPointer(
    const State &S, const clang::SourceManager &SM,
    const clang::VarDecl *Variable, const clang::ASTContext &Context);
struct NativeDataMemberPointerAccess {
  const clang::Expr *Object;
  const clang::Expr *Callable;
  const clang::FieldDecl *Field;
  bool ObjectIsPointer;
};
std::optional<NativeDataMemberPointerAccess>
approvedNativeDataMemberPointerAccess(
    const State &S, const clang::SourceManager &SM,
    const clang::BinaryOperator *Operation,
    const clang::ASTContext &Context);
struct FunctionalStoredMemFn {
  const clang::VarDecl *Variable;
  const clang::Expr *Initializer;
  const clang::CallExpr *Factory;
  const clang::Expr *Address;
  const clang::ValueDecl *Member;
};
std::optional<FunctionalStoredMemFn> approvedFunctionalStoredMemFn(
    const State &S, const clang::SourceManager &SM,
    const clang::VarDecl *Variable, const clang::ASTContext &Context);
std::optional<FunctionalMemberInvokeCall>
approvedFunctionalMemberInvokeCall(
    const State &S, const clang::SourceManager &SM,
    const clang::CallExpr *Call, const clang::ASTContext &Context);
std::optional<FunctionalMemberInvokeCall>
approvedNativeMemberPointerCall(
    const State &S, const clang::SourceManager &SM,
    const clang::CallExpr *Call, const clang::ASTContext &Context);
enum class MemoryTemplateMetadata {
  PointerTraits,
  DefaultDelete,
  UniquePtr,
  Allocator,
  AllocatorTraits,
  UsesAllocator,
};
std::optional<MemoryTemplateMetadata>
approvedMemoryTemplateMetadata(const State &S, const clang::SourceManager &SM,
                               const clang::CXXRecordDecl *Record);
struct UtilityDefaultDeleteRecord {
  const clang::CXXRecordDecl *Record;
  clang::QualType ElementType;
  bool Array;
};
std::optional<UtilityDefaultDeleteRecord> approvedUtilityDefaultDeleteRecord(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXRecordDecl *Record, const clang::ASTContext &Context);
enum class UtilityDefaultDeleteConstruction {
  Default,
  CopyOrMove,
  Converting,
};
std::optional<UtilityDefaultDeleteConstruction>
approvedUtilityDefaultDeleteConstruction(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXConstructExpr *Construction, clang::ASTContext &Context);
struct UtilityDefaultDeleteCall {
  UtilityDefaultDeleteRecord Deleter;
  const clang::CXXDeleteExpr *Delete;
  const clang::Expr *Object;
  unsigned PointerIndex;
};
std::optional<UtilityDefaultDeleteCall>
approvedUtilityDefaultDeleteCall(const State &S, const clang::SourceManager &SM,
                                 const clang::CallExpr *Call,
                                 const clang::ASTContext &Context);
struct UtilityUniquePtrRecord {
  const clang::CXXRecordDecl *Record;
  clang::QualType ElementType;
  clang::QualType PointerType;
  UtilityDefaultDeleteRecord Deleter;
  const clang::CXXDeleteExpr *DefaultDeletion = nullptr;
  const clang::CXXMethodDecl *CustomDeleter = nullptr;
};
std::optional<UtilityUniquePtrRecord>
approvedUtilityUniquePtrRecord(const State &S, const clang::SourceManager &SM,
                               const clang::CXXRecordDecl *Record,
                               const clang::ASTContext &Context);
enum class UtilityUniquePtrConstruction {
  Default,
  Null,
  Pointer,
  NullDeleter,
  PointerDeleter,
  FactoryArray,
  Move,
  ConvertingMove,
};
std::optional<UtilityUniquePtrConstruction>
approvedUtilityUniquePtrConstruction(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXConstructExpr *Construction,
    const clang::ASTContext &Context);
enum class UtilityUniquePtrOperation {
  Get,
  GetDeleter,
  Arrow,
  Dereference,
  Subscript,
  Boolean,
  Release,
  Reset,
  MoveAssign,
  ConvertingMoveAssign,
  NullAssign,
  Swap,
};
struct UtilityUniquePtrCall {
  UtilityUniquePtrRecord Owner;
  UtilityUniquePtrOperation Operation;
  const clang::Expr *Object;
  unsigned ArgumentIndex;
  std::optional<UtilityUniquePtrRecord> SourceOwner = std::nullopt;
  bool ObjectIsArrow = false;
};
std::optional<UtilityUniquePtrCall>
approvedUtilityUniquePtrCall(const State &S, const clang::SourceManager &SM,
                             const clang::CallExpr *Call,
                             const clang::ASTContext &Context);
struct UtilityMakeUniqueCall {
  UtilityUniquePtrRecord Owner;
  const clang::CXXNewExpr *Allocation;
  const clang::CXXConstructExpr *Construction;
  const clang::CXXConstructorDecl *Constructor;
  std::optional<uint64_t> ArrayCount = std::nullopt;
};
std::optional<UtilityMakeUniqueCall>
approvedUtilityMakeUniqueCall(const State &S, const clang::SourceManager &SM,
                              const clang::CallExpr *Call,
                              const clang::ASTContext &Context);
bool approvedUtilityUniquePtrDestructor(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXDestructorDecl *Destructor,
    const clang::ASTContext &Context);
struct UtilityAllocatorRecord {
  const clang::CXXRecordDecl *Record;
  clang::QualType ElementType;
};
std::optional<UtilityAllocatorRecord>
approvedUtilityAllocatorRecord(const State &S, const clang::SourceManager &SM,
                               const clang::CXXRecordDecl *Record,
                               const clang::ASTContext &Context);
struct UtilityAllocatorTraitsRecord {
  const clang::CXXRecordDecl *Record;
  UtilityAllocatorRecord Allocator;
};
std::optional<UtilityAllocatorTraitsRecord>
approvedUtilityAllocatorTraitsRecord(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXRecordDecl *Record, const clang::ASTContext &Context);
struct UtilityAllocatorConstructCall {
  clang::QualType ElementType;
  const clang::CXXConstructExpr *Construction;
  const clang::CXXConstructorDecl *Constructor;
};
std::optional<UtilityAllocatorConstructCall>
approvedUtilityAllocatorConstructCall(
    const State &S, const clang::SourceManager &SM,
    const clang::CallExpr *Call, bool Traits,
    const clang::ASTContext &Context);
struct UtilityAllocatorHeapCall {
  UtilityAllocatorRecord Allocator;
  bool Allocate;
  bool Traits;
  bool Hint;
};
std::optional<UtilityAllocatorHeapCall>
approvedUtilityAllocatorHeapCall(const State &S, const clang::SourceManager &SM,
                                 const clang::CallExpr *Call, bool Traits,
                                 const clang::ASTContext &Context);
enum class UtilityAllocatorConstruction {
  Default,
  CopyOrMove,
  Converting,
};
std::optional<UtilityAllocatorConstruction>
approvedUtilityAllocatorConstruction(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXConstructExpr *Construction,
    const clang::ASTContext &Context);
bool approvedUtilityAllocatorAssignment(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXOperatorCallExpr *Assignment,
    const clang::ASTContext &Context);
enum class UtilityOperation {
  CStringLength,
  CStringCompare,
  CStringCompareN,
  Move,
  Forward,
  MoveIfNoexcept,
  AsConst,
  FunctionalInvoke,
  FunctionalInvokeObject,
  FunctionalInvokeUserObject,
  FunctionalInvokeReference,
  FunctionalInvokeMember,
  FunctionalReferenceFactory,
  FunctionalReferenceAccess,
  NewLaunder,
  MemoryDefaultDelete,
  MemoryUniquePtrGet,
  MemoryUniquePtrGetDeleter,
  MemoryUniquePtrArrow,
  MemoryUniquePtrDereference,
  MemoryUniquePtrSubscript,
  MemoryUniquePtrBoolean,
  MemoryUniquePtrRelease,
  MemoryUniquePtrReset,
  MemoryUniquePtrMoveAssign,
  MemoryUniquePtrConvertingMoveAssign,
  MemoryUniquePtrNullAssign,
  MemoryUniquePtrMemberSwap,
  MemoryUniquePtrSwap,
  MemoryUniquePtrEqual,
  MemoryUniquePtrNotEqual,
  MemoryUniquePtrLess,
  MemoryUniquePtrGreater,
  MemoryUniquePtrLessEqual,
  MemoryUniquePtrGreaterEqual,
  MemoryMakeUnique,
  MemoryAllocatorAddress,
  MemoryAllocatorAllocate,
  MemoryAllocatorDeallocate,
  MemoryAllocatorMaxSize,
  MemoryAllocatorConstruct,
  MemoryAllocatorDestroy,
  MemoryAllocatorTraitsConstruct,
  MemoryAllocatorTraitsAllocate,
  MemoryAllocatorTraitsDeallocate,
  MemoryAllocatorTraitsDestroy,
  MemoryAllocatorTraitsMaxSize,
  MemoryAllocatorTraitsSelectOnCopy,
  MemoryAllocatorEqual,
  MemoryAllocatorNotEqual,
  MemoryAddressof,
  MemoryPointerTo,
  MemoryDestroyAt,
  MemoryDestroy,
  MemoryDestroyN,
  MemoryUninitializedCopy,
  MemoryUninitializedCopyN,
  MemoryUninitializedFill,
  MemoryUninitializedFillN,
  MemoryUninitializedDefaultConstruct,
  MemoryUninitializedDefaultConstructN,
  MemoryUninitializedValueConstruct,
  MemoryUninitializedValueConstructN,
  MemoryUninitializedMove,
  MemoryUninitializedMoveN,
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
  MakeTuple,
  Tie,
  ForwardAsTuple,
  TupleApply,
  TupleCat,
  TupleSwap,
  TupleMemberSwap,
  TupleEqual,
  TupleNotEqual,
  TupleLess,
  TupleGreater,
  TupleLessEqual,
  TupleGreaterEqual,
  TupleGet,
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
  NumericIota,
  NumericAccumulate,
  NumericInnerProduct,
  NumericPartialSum,
  NumericAdjacentDifference,
  NumericReduce,
  NumericTransformReduce,
  NumericInclusiveScan,
  NumericExclusiveScan,
  NumericTransformInclusiveScan,
  NumericTransformExclusiveScan,
  NumericGcd,
  NumericLcm,
  InitializerListSize,
  InitializerListEmpty,
  InitializerListBegin,
  InitializerListEnd,
  InitializerListRBegin,
  InitializerListREnd,
  StringSize,
  StringCapacity,
  StringMaxSize,
  StringEmpty,
  StringData,
  StringToView,
  StringBegin,
  StringEnd,
  StringRBegin,
  StringREnd,
  StringSubscript,
  StringFront,
  StringBack,
  StringClear,
  StringPushBack,
  StringPopBack,
  StringReserve,
  StringShrinkToFit,
  StringResize,
  StringAppendPointer,
  StringAppendCString,
  StringAppendString,
  StringAppendStringSlice,
  StringAppendView,
  StringAppendViewSlice,
  StringAppendList,
  StringAppendRange,
  StringAppendFill,
  StringAppendCharacter,
  StringAssignPointer,
  StringAssignCString,
  StringAssignString,
  StringAssignStringSlice,
  StringAssignView,
  StringAssignViewSlice,
  StringAssignList,
  StringAssignRange,
  StringAssignFill,
  StringAssignOperatorCString,
  StringAssignOperatorCharacter,
  StringAssignOperatorList,
  StringErase,
  StringEraseIterator,
  StringInsertPointer,
  StringInsertCString,
  StringInsertString,
  StringInsertStringSlice,
  StringInsertView,
  StringInsertViewSlice,
  StringInsertFill,
  StringInsertIteratorCharacter,
  StringInsertIteratorFill,
  StringInsertIteratorRange,
  StringInsertIteratorList,
  StringReplacePointer,
  StringReplaceCString,
  StringReplaceString,
  StringReplaceStringSlice,
  StringReplaceView,
  StringReplaceViewSlice,
  StringReplaceFill,
  StringReplaceIteratorPointer,
  StringReplaceIteratorCString,
  StringReplaceIteratorString,
  StringReplaceIteratorFill,
  StringReplaceIteratorRange,
  StringReplaceIteratorList,
  StringCopy,
  StringSubstr,
  StringConcat,
  StringMemberSwap,
  StringSwap,
  StringCompare,
  StringCompareCString,
  StringCompareSlice,
  StringCStringRelation,
  StringEqual,
  StringNotEqual,
  StringLess,
  StringGreater,
  StringLessEqual,
  StringGreaterEqual,
  StringFindCharacter,
  StringRFindCharacter,
  StringFindSubstring,
  StringRFindSubstring,
  StringFindFirstOf,
  StringFindLastOf,
  StringFindFirstNotOf,
  StringFindLastNotOf,
  StringViewSize,
  VectorSize,
  VectorCapacity,
  VectorMaxSize,
  VectorEmpty,
  VectorData,
  VectorSubscript,
  VectorFront,
  VectorBack,
  VectorClear,
  VectorMemberSwap,
  VectorSwap,
  VectorRelation,
  VectorPushBack,
  VectorEmplaceBack,
  VectorPopBack,
  VectorReserve,
  VectorShrinkToFit,
  VectorResize,
  VectorResizeFill,
  VectorBegin,
  VectorEnd,
  VectorRBegin,
  VectorREnd,
  VectorErase,
  VectorInsert,
  VectorInsertRange,
  VectorEmplace,
  VectorAssignFill,
  VectorAssignRange,
  VectorAssignList,
  WrapIteratorDereference,
  WrapIteratorArrow,
  WrapIteratorSubscript,
  WrapIteratorPreIncrement,
  WrapIteratorPreDecrement,
  WrapIteratorPostIncrement,
  WrapIteratorPostDecrement,
  WrapIteratorAddOffset,
  WrapIteratorSubtractOffset,
  WrapIteratorAddAssign,
  WrapIteratorSubtractAssign,
  WrapIteratorOffsetLeft,
  WrapIteratorBase,
  WrapIteratorEqual,
  WrapIteratorNotEqual,
  WrapIteratorDifference,
  StringViewMaxSize,
  StringViewEmpty,
  StringViewData,
  StringViewBegin,
  StringViewEnd,
  StringViewRBegin,
  StringViewREnd,
  StringViewSubscript,
  StringViewFront,
  StringViewBack,
  StringViewRemovePrefix,
  StringViewRemoveSuffix,
  StringViewCopy,
  StringViewSubstr,
  StringViewSwap,
  StringViewCompare,
  StringViewCompareSlice,
  StringViewEqual,
  StringViewNotEqual,
  StringViewLess,
  StringViewGreater,
  StringViewLessEqual,
  StringViewGreaterEqual,
  StringViewFindCharacter,
  StringViewFindView,
  StringViewRFindCharacter,
  StringViewRFindView,
  StringViewFindFirstOf,
  StringViewFindLastOf,
  StringViewFindFirstNotOf,
  StringViewFindLastNotOf,
  OptionalHasValue,
  OptionalDereference,
  OptionalArrow,
  OptionalReset,
  OptionalEmplace,
  OptionalValueOr,
  OptionalMemberSwap,
  OptionalSwap,
  OptionalEqual,
  OptionalNotEqual,
  OptionalLess,
  OptionalGreater,
  OptionalLessEqual,
  OptionalGreaterEqual,
  MakeOptional,
};
// A checked function object selected by an exact unary algorithm body. The
// outer algorithm owns a by-value predicate; Invocation uses that same lvalue.
struct UtilityAlgorithmPredicateCall {
  UtilityOperation Operation;
  const clang::FunctionDecl *Algorithm;
  const clang::CXXOperatorCallExpr *Invocation;
  const clang::CXXMethodDecl *Method;
  clang::QualType ObjectType;
  unsigned ParameterIndex;
  std::optional<FunctionalOperationInfo> SDKOperation;
};
std::optional<UtilityAlgorithmPredicateCall>
approvedUtilityAlgorithmPredicateCall(const State &S,
    const clang::SourceManager &SM, const clang::CallExpr *Call,
    const clang::ASTContext &Context);
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
  Converting,
};
std::optional<UtilityPairRecord>
approvedUtilityPairRecord(const State &S, const clang::SourceManager &SM,
                          const clang::CXXRecordDecl *Record,
                          const clang::ASTContext &Context);
std::optional<UtilityPairRecord> approvedUtilityReferencePairRecord(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXRecordDecl *Record, const clang::ASTContext &Context);
std::optional<UtilityPairRecord> approvedUtilityMixedReferencePairRecord(
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
struct UtilityTupleRecord {
  const clang::CXXRecordDecl *Record;
  std::vector<const clang::FieldDecl *> Elements;
  std::vector<uint64_t> Offsets;
};
bool approvedUtilityTupleMetadata(const State &S,
                                  const clang::SourceManager &SM,
                                  const clang::CXXRecordDecl *Record);
std::optional<UtilityTupleRecord>
approvedUtilityTupleRecord(const State &S, const clang::SourceManager &SM,
                           const clang::CXXRecordDecl *Record,
                           const clang::ASTContext &Context);
std::optional<UtilityTupleRecord> approvedUtilityReferenceTupleRecord(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXRecordDecl *Record, const clang::ASTContext &Context);
std::optional<UtilityTupleRecord> approvedUtilityMixedReferenceTupleRecord(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXRecordDecl *Record, const clang::ASTContext &Context);
enum class UtilityTupleConstruction {
  Default,
  Elements,
  CopyOrMove,
  Converting,
  Pair,
};
std::optional<UtilityTupleConstruction>
approvedUtilityTupleConstruction(const State &S, const clang::SourceManager &SM,
                                 const clang::CXXConstructExpr *Construction,
                                 const clang::ASTContext &Context);
enum class UtilityTupleAssignment {
  CopyOrMove,
  Converting,
  Pair,
};
std::optional<UtilityTupleAssignment>
approvedUtilityTupleAssignment(const State &S, const clang::SourceManager &SM,
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
// Authenticated tuple-like storage shared by tuple_cat and apply. Pair and
// tuple elements have individual fields; array elements share one fixed array.
struct UtilityTupleLikeSource {
  std::vector<const clang::FieldDecl *> Elements;
  const clang::FieldDecl *ArrayElements;
  clang::QualType ArrayElementType;
  uint64_t ArraySize;

  uint64_t size() const {
    return ArrayElements ? ArraySize : Elements.size();
  }
  clang::QualType elementType(unsigned Index) const {
    return ArrayElements ? ArrayElementType : Elements[Index]->getType();
  }
};
std::optional<UtilityTupleLikeSource>
approvedUtilityTupleLikeSource(const State &S, const clang::SourceManager &SM,
                               clang::QualType Type,
                               const clang::ASTContext &Context);
bool approvedUtilityTupleLikeGet(
    const State &S, const clang::SourceManager &SM, const clang::CallExpr *Get,
    clang::QualType Parameter, const UtilityTupleLikeSource &Tuple,
    unsigned Index, const clang::ASTContext &Context);
bool approvedUtilityTupleLikeSizeTrait(
    const State &S, const clang::SourceManager &SM, const clang::CXXRecordDecl *Record,
    clang::QualType Object, const UtilityTupleLikeSource &Tuple,
    const clang::ASTContext &Context);
const clang::TypedefNameDecl *approvedUtilityTupleLikeElementTrait(
    const State &S, const clang::SourceManager &SM, const clang::CXXRecordDecl *Record,
    clang::QualType Object, const UtilityTupleLikeSource &Tuple, unsigned Index,
    const clang::ASTContext &Context);
struct UtilityTupleCatCall {
  UtilityTupleRecord Result;
  std::vector<UtilityTupleLikeSource> Sources;
};
std::optional<UtilityTupleCatCall>
approvedUtilityTupleCatCall(const State &S, const clang::SourceManager &SM,
                            const clang::CallExpr *Call,
                            const clang::ASTContext &Context);
struct UtilityInitializerListRecord {
  const clang::CXXRecordDecl *Record;
  const clang::FieldDecl *Begin, *Size;
  clang::QualType ElementType;
};
bool approvedUtilityInitializerListMetadata(const State &S,
                                            const clang::SourceManager &SM,
                                            const clang::CXXRecordDecl *Record);
std::optional<UtilityInitializerListRecord>
approvedUtilityInitializerListRecord(const State &S,
                                     const clang::SourceManager &SM,
                                     const clang::CXXRecordDecl *Record,
                                     const clang::ASTContext &Context);
enum class UtilityInitializerListConstruction {
  Default,
  CopyOrMove,
};
std::optional<UtilityInitializerListConstruction>
approvedUtilityInitializerListConstruction(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXConstructExpr *Construction,
    const clang::ASTContext &Context);
std::optional<UtilityInitializerListRecord>
approvedUtilityInitializerListAssignment(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXOperatorCallExpr *Assignment,
    const clang::ASTContext &Context);
struct UtilityInitializerListExpression {
  UtilityInitializerListRecord List;
  const clang::MaterializeTemporaryExpr *Backing;
  uint64_t Size;
};
std::optional<UtilityInitializerListExpression>
approvedUtilityInitializerListExpression(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXStdInitializerListExpr *Expression,
    const clang::ASTContext &Context);
struct UtilityStringViewRecord {
  const clang::CXXRecordDecl *Record;
  const clang::FieldDecl *Data, *Size;
};
struct UtilityStringRecord {
  const clang::CXXRecordDecl *Record;
  clang::QualType PointerType;
  uint64_t ShortCapacity;
  bool AlternateLayout;
};
std::optional<UtilityStringRecord>
approvedUtilityStringRecord(const State &S, const clang::SourceManager &SM,
                            const clang::CXXRecordDecl *Record,
                            const clang::ASTContext &Context);
enum class UtilityStringConstruction {
  Default,
  CString,
  PointerLength,
  Range,
  Fill,
  InitializerList,
  Copy,
  Move,
  Substring,
  View,
  ViewSubstring,
};
std::optional<UtilityStringConstruction>
approvedUtilityStringConstruction(const State &S,
                                  const clang::SourceManager &SM,
                                  const clang::CXXConstructExpr *Construction,
                                  const clang::ASTContext &Context);
enum class UtilityStringAssignment { Copy, Move };
std::optional<UtilityStringAssignment>
approvedUtilityStringAssignment(const State &S,
                                const clang::SourceManager &SM,
                                const clang::CXXOperatorCallExpr *Assignment,
                                const clang::ASTContext &Context);
bool approvedUtilityStringDestructor(const State &S,
                                     const clang::SourceManager &SM,
                                     const clang::CXXDestructorDecl *Destructor,
                                     const clang::ASTContext &Context);
struct UtilityVectorRecord {
  const clang::CXXRecordDecl *Record;
  clang::QualType ElementType, PointerType;
  bool OwningElement;
};
std::optional<UtilityVectorRecord>
approvedUtilityVectorRecord(const State &S, const clang::SourceManager &SM,
                            const clang::CXXRecordDecl *Record,
                            const clang::ASTContext &Context);
enum class UtilityVectorConstruction {
  Default,
  Count,
  CountValue,
  Range,
  InitializerList,
  Copy,
  Move
};
std::optional<UtilityVectorConstruction>
approvedUtilityVectorConstruction(const State &S,
                                  const clang::SourceManager &SM,
                                  const clang::CXXConstructExpr *Construction,
                                  const clang::ASTContext &Context);
enum class UtilityVectorAssignment { Copy, Move };
std::optional<UtilityVectorAssignment>
approvedUtilityVectorAssignment(const State &S, const clang::SourceManager &SM,
                                const clang::CXXOperatorCallExpr *Assignment,
                                const clang::ASTContext &Context);
bool approvedUtilityVectorDestructor(const State &S,
                                     const clang::SourceManager &SM,
                                     const clang::CXXDestructorDecl *Destructor,
                                     const clang::ASTContext &Context);
bool approvedUtilityStringViewMetadata(const State &S,
                                       const clang::SourceManager &SM,
                                       const clang::CXXRecordDecl *Record);
std::optional<UtilityStringViewRecord>
approvedUtilityStringViewRecord(const State &S, const clang::SourceManager &SM,
                                const clang::CXXRecordDecl *Record,
                                const clang::ASTContext &Context);
enum class UtilityStringViewConstruction {
  Default,
  CopyOrMove,
  PointerAndSize,
  Pointer,
};
std::optional<UtilityStringViewConstruction>
approvedUtilityStringViewConstruction(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXConstructExpr *Construction,
    const clang::ASTContext &Context);
std::optional<UtilityStringViewRecord> approvedUtilityStringViewAssignment(
    const State &S, const clang::SourceManager &SM,
    const clang::CXXOperatorCallExpr *Assignment,
    const clang::ASTContext &Context);
struct UtilityOptionalRecord {
  const clang::CXXRecordDecl *Record;
  const clang::CXXRecordDecl *StorageBase, *DestructBase;
  const clang::FieldDecl *Value, *Engaged;
  clang::QualType ElementType;
};
bool approvedUtilityOptionalMetadata(const State &S,
                                     const clang::SourceManager &SM,
                                     const clang::CXXRecordDecl *Record);
std::optional<UtilityOptionalRecord>
approvedUtilityOptionalRecord(const State &S, const clang::SourceManager &SM,
                              const clang::CXXRecordDecl *Record,
                              const clang::ASTContext &Context);
std::optional<clang::QualType>
utilityScalarComparisonType(const clang::ASTContext &Context,
                            clang::QualType Left, clang::QualType Right,
                            bool RequireOrderedObject = false);
enum class UtilityOptionalConstruction {
  Empty,
  InPlaceDefault,
  InPlaceValue,
  CopyOrMove,
  Converting,
  Value,
};
std::optional<UtilityOptionalConstruction>
approvedUtilityOptionalConstruction(const State &S,
                                    const clang::SourceManager &SM,
                                    const clang::CXXConstructExpr *Construction,
                                    const clang::ASTContext &Context);
enum class UtilityOptionalAssignment {
  Empty,
  CopyOrMove,
  Converting,
  Value,
};
std::optional<UtilityOptionalAssignment>
approvedUtilityOptionalAssignment(const State &S,
                                  const clang::SourceManager &SM,
                                  const clang::CXXOperatorCallExpr *Assignment,
                                  const clang::ASTContext &Context);
bool approvedUtilityNulloptExpression(const State &S,
                                      const clang::SourceManager &SM,
                                      const clang::Expr *Expression,
                                      const clang::ASTContext &Context);
bool approvedUtilityInPlaceExpression(const State &S,
                                      const clang::SourceManager &SM,
                                      const clang::Expr *Expression,
                                      const clang::ASTContext &Context);
bool approvedUtilityInPlaceType(const State &S, const clang::SourceManager &SM,
                                clang::QualType Type,
                                const clang::ASTContext &Context);
bool approvedUtilityOptionalBaseCast(const State &S,
                                     const clang::SourceManager &SM,
                                     const clang::CastExpr *Cast,
                                     const clang::ASTContext &Context);
struct UtilityReverseIteratorRecord {
  const clang::CXXRecordDecl *Record;
  const clang::FieldDecl *Legacy, *Current;
  clang::QualType IteratorType;
  const clang::FieldDecl *WrappedCurrent;
  clang::QualType PointerType;
};
struct UtilityWrapIteratorRecord {
  const clang::CXXRecordDecl *Record;
  const clang::FieldDecl *Current;
  clang::QualType IteratorType;
};
bool approvedUtilityWrapIteratorMetadata(const State &S,
                                         const clang::SourceManager &SM,
                                         const clang::CXXRecordDecl *Record);
std::optional<UtilityWrapIteratorRecord>
approvedUtilityWrapIteratorRecord(const State &S,
                                  const clang::SourceManager &SM,
                                  const clang::CXXRecordDecl *Record,
                                  const clang::ASTContext &Context);
enum class UtilityWrapIteratorConstruction { Default, CopyOrMove, Converting };
std::optional<UtilityWrapIteratorConstruction>
approvedUtilityWrapIteratorConstruction(const State &S,
                                        const clang::SourceManager &SM,
                                        const clang::CXXConstructExpr *Construction,
                                        const clang::ASTContext &Context);
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
const clang::CXXConstructorDecl *approvedUtilityMemoryDefaultConstructor(
    const State &S, const clang::SourceManager &SM, clang::QualType Element,
    const clang::ASTContext &Context);
const clang::CXXConstructorDecl *approvedUtilityMemorySourceConstructor(
    const State &S, const clang::SourceManager &SM, const clang::CallExpr *Call,
    UtilityOperation Operation, const clang::ASTContext &Context);
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
const clang::Expr *
defaultArgumentInitializer(const clang::ParmVarDecl *Parameter,
                           const clang::ASTContext &Context);
const clang::Expr *
selectedDefaultArgument(const clang::CXXDefaultArgExpr *Default,
                        const clang::ASTContext &Context);
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
struct LocalDecomposition {
  // Native-array bindings have no FieldDecl; their exact projection is checked.
  std::vector<std::pair<const clang::BindingDecl *, const clang::FieldDecl *>>
      Bindings;
  bool TupleLike = false;
  bool Complete = false;
};
struct DecompositionTupleBinding {
  const clang::DecompositionDecl *Owner;
  const clang::VarDecl *Holding;
  const clang::CallExpr *Get;
  const clang::TypedefNameDecl *ElementTrait;
};
struct DecompositionArrayCopy {
  const clang::DecompositionDecl *Owner;
  const clang::ArrayInitLoopExpr *Loop, *Parent;
  const clang::ArrayInitIndexExpr *Index;
};
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
  std::set<const clang::CXXRecordDecl *> RequiredUtilityTuples;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityArrays;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityInitializerLists;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityStringViews;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityStrings;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityVectors;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityOptionals;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityReverseIterators;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityWrapIterators;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityDefaultDeletes;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityUniquePtrs;
  std::set<const clang::CXXRecordDecl *> RequiredUtilityAllocators;
  std::set<const clang::CXXRecordDecl *> RequiredFunctionalObjects;
  std::set<const clang::CXXRecordDecl *> RequiredFunctionalReferences;
  std::map<const clang::CXXRecordDecl *, CheckedEmptyBase> EmptyBases;
  std::map<const clang::VarDecl *, const clang::CXXForRangeStmt *> RangeDeclarations;
  std::map<const clang::VarDecl *, LocalDecomposition> Decompositions;
  std::map<const clang::BindingDecl *, const clang::FieldDecl *> DecompositionBindings;
  std::map<unsigned, std::vector<const TemplateUseSource *>> DecompositionTraitSources;
  std::map<const clang::BindingDecl *, DecompositionTupleBinding> DecompositionTupleBindings;
  std::map<const clang::VarDecl *, const clang::BindingDecl *> DecompositionHoldingBindings;
  std::map<const clang::CallExpr *, const clang::BindingDecl *> DecompositionGetBindings;
  std::map<const clang::ArrayInitLoopExpr *, DecompositionArrayCopy> DecompositionArrayCopies;
  std::map<const clang::Expr *, const clang::ArrayInitLoopExpr *> DecompositionArrayNodes;
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
  bool requireUtilityTuple(const clang::CXXRecordDecl *Record,
                           clang::SourceLocation Location, unsigned Depth = 0);
  bool requireUtilityArray(const clang::CXXRecordDecl *Record,
                           clang::SourceLocation Location,
                           unsigned Depth = 0);
  bool requireUtilityInitializerList(const clang::CXXRecordDecl *Record,
                                     clang::SourceLocation Location,
                                     unsigned Depth = 0);
  bool requireUtilityStringView(const clang::CXXRecordDecl *Record,
                                clang::SourceLocation Location,
                                unsigned Depth = 0);
  bool requireUtilityString(const clang::CXXRecordDecl *Record,
                            clang::SourceLocation Location,
                            unsigned Depth = 0);
  bool requireUtilityVector(const clang::CXXRecordDecl *Record,
                            clang::SourceLocation Location,
                            unsigned Depth = 0);
  bool requireUtilityOptional(const clang::CXXRecordDecl *Record,
                              clang::SourceLocation Location,
                              unsigned Depth = 0);
  bool requireUtilityReverseIterator(const clang::CXXRecordDecl *Record,
                                     clang::SourceLocation Location,
                                     unsigned Depth = 0);
  bool requireUtilityWrapIterator(const clang::CXXRecordDecl *Record,
                                  clang::SourceLocation Location,
                                  unsigned Depth = 0);
  bool requireUtilityDefaultDelete(const clang::CXXRecordDecl *Record,
                                   clang::SourceLocation Location,
                                   unsigned Depth = 0);
  bool requireUtilityUniquePtr(const clang::CXXRecordDecl *Record,
                               clang::SourceLocation Location,
                               unsigned Depth = 0);
  bool requireUtilityAllocator(const clang::CXXRecordDecl *Record,
                               clang::SourceLocation Location,
                               unsigned Depth = 0);
  bool requireFunctionalObject(const clang::CXXRecordDecl *Record,
                               clang::SourceLocation Location,
                               unsigned Depth = 0);
  bool requireFunctionalReference(const clang::CXXRecordDecl *Record,
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
  bool standardPlacementAllocation(const clang::FunctionDecl *F,
                                   bool Array) const;
  const clang::FunctionDecl *allocationFunction(const clang::FunctionDecl *F,
                                               bool Allocate, clang::SourceLocation L,
                                               bool Array = false);
  const clang::FunctionDecl *allocatorHeapFunction(bool Allocate,
                                                   clang::QualType Element,
                                                   clang::SourceLocation L,
                                                   bool Array = false);
  const clang::FunctionDecl *
  defaultDeleteFunction(const UtilityDefaultDeleteRecord &Deleter,
                        const clang::CXXDeleteExpr *Delete,
                        clang::SourceLocation L);
  const clang::FunctionDecl *
  uniquePtrDeleteFunction(const UtilityUniquePtrRecord &Owner,
                          clang::SourceLocation L);
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
  bool registerDecomposition(const clang::DecompositionDecl *Declaration);
  const LocalDecomposition *
  decomposition(const clang::VarDecl *Declaration) const;
  const clang::Expr *decompositionBinding(const clang::BindingDecl *Binding) const;
  const DecompositionTupleBinding *tupleDecompositionBinding(const clang::BindingDecl *Binding) const;
  const DecompositionTupleBinding *decompositionHolding(const clang::VarDecl *Variable) const;
  const DecompositionArrayCopy *decompositionArrayCopy(const clang::Stmt *Node) const;
  const clang::FieldDecl *
  recordBindingField(const clang::BindingDecl *Binding) const;
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
