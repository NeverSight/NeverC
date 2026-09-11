#ifndef NEVERC_CPP_FRONTEND_H
#define NEVERC_CPP_FRONTEND_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/FileEntry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <map>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

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

std::string digest(llvm::StringRef Text);
bool validExportName(llvm::StringRef Name);
bool isolateProjectEnvironment();

class Adapter {
public:
  State &S;
  clang::ASTContext &Context;
  clang::SourceManager &Sources;
  std::map<const clang::Decl *, std::string> Names;
  std::vector<clang::FunctionDecl *> Functions;
  std::vector<clang::CXXRecordDecl *> Records;
  std::vector<clang::VarDecl *> Globals;
  std::map<const clang::Decl *, clang::FunctionDecl *> FunctionDeclarations;
  std::map<const clang::Decl *, clang::VarDecl *> GlobalDeclarations;
  std::map<std::string, json::Object> MappedFunctions;
  std::map<const clang::CXXRecordDecl *, std::size_t> StorageUnits;
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
  std::size_t storageUnits(clang::QualType T, unsigned Depth = 0);
  void chargeExpansion(std::size_t Nodes, clang::SourceLocation L);
  json::Object literal(const llvm::APSInt &Value, llvm::StringRef Type,
                       clang::SourceLocation L);
  json::Object floatingLiteral(const llvm::APFloat &Value,
                               clang::SourceLocation L);
  std::string mapping(const clang::CallExpr *Call);
  json::Object zero(clang::QualType T, clang::SourceLocation L);
  json::Object constant(const clang::APValue &V, clang::QualType T,
                        clang::SourceLocation L);
  json::Object lower(clang::FunctionDecl *Function);
  void run();
};
} // namespace nct
#endif
