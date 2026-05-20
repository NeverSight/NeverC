#ifndef NEVERC_COMPILER_COMPILERINVOCATION_H
#define NEVERC_COMPILER_COMPILERINVOCATION_H

#include "neverc/Compiler/DependencyOutputOptions.h"
#include "neverc/Compiler/FrontendOptions.h"
#include "neverc/Compiler/PrepOutputOptions.h"
#include "neverc/Foundation/Core/FileSystemOptions.h"
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/LangOpts/LangStandard.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include <memory>
#include <string>

namespace llvm {

class Triple;

namespace opt {

class ArgList;

} // namespace opt

namespace vfs {

class FileSystem;

} // namespace vfs

} // namespace llvm

namespace neverc {

class DiagnosticsEngine;
class HeaderIndexOptions;
class PrepOptions;
class TargetOptions;

// This lets us create the DiagnosticsEngine with a properly-filled-out
// DiagnosticOptions instance.
std::unique_ptr<DiagnosticOptions>
CreateAndPopulateDiagOpts(llvm::ArrayRef<const char *> Argv);

bool ParseDiagnosticArgs(DiagnosticOptions &Opts, llvm::opt::ArgList &Args,
                         DiagnosticsEngine *Diags = nullptr,
                         bool DefaultDiagColor = true);

class CompilerInvocationBase {
protected:
  std::shared_ptr<LangOptions> LangOpts;

  std::shared_ptr<TargetOptions> TargetOpts;

  llvm::IntrusiveRefCntPtr<DiagnosticOptions> DiagnosticOpts;

  std::shared_ptr<HeaderIndexOptions> HSOpts;

  std::shared_ptr<PrepOptions> PPOpts;

  std::shared_ptr<CodeGenOptions> CodeGenOpts;

  std::shared_ptr<FileSystemOptions> FSOpts;

  std::shared_ptr<FrontendOptions> FrontendOpts;

  std::shared_ptr<DependencyOutputOptions> DependencyOutputOpts;

  std::shared_ptr<PrepOutputOptions> PrepOutputOpts;

  struct EmptyConstructor {};

  CompilerInvocationBase();
  CompilerInvocationBase(EmptyConstructor) {}
  CompilerInvocationBase(const CompilerInvocationBase &X) = delete;
  CompilerInvocationBase(CompilerInvocationBase &&X) = default;
  CompilerInvocationBase &operator=(const CompilerInvocationBase &X) = delete;
  CompilerInvocationBase &deep_copy_assign(const CompilerInvocationBase &X);
  CompilerInvocationBase &shallow_copy_assign(const CompilerInvocationBase &X);
  CompilerInvocationBase &operator=(CompilerInvocationBase &&X) = default;
  ~CompilerInvocationBase() = default;

public:
  const LangOptions &getLangOpts() const { return *LangOpts; }
  const TargetOptions &getTargetOpts() const { return *TargetOpts; }
  const DiagnosticOptions &getDiagnosticOpts() const { return *DiagnosticOpts; }
  const HeaderIndexOptions &getHeaderIdxOpts() const { return *HSOpts; }
  const PrepOptions &getPrepOpts() const { return *PPOpts; }
  const CodeGenOptions &getCodeGenOpts() const { return *CodeGenOpts; }
  const FileSystemOptions &getFileSystemOpts() const { return *FSOpts; }
  const FrontendOptions &getFrontendOpts() const { return *FrontendOpts; }
  const DependencyOutputOptions &getDependencyOutputOpts() const {
    return *DependencyOutputOpts;
  }
  const PrepOutputOptions &getPrepOutputOpts() const { return *PrepOutputOpts; }

  using StringAllocator = llvm::function_ref<const char *(const llvm::Twine &)>;
  void generateFrontendCommandLine(llvm::SmallVectorImpl<const char *> &Args,
                                   StringAllocator SA) const {
    generateFrontendCommandLine([&](const llvm::Twine &Arg) {
      // No need to allocate static string literals.
      Args.push_back(Arg.isSingleStringLiteral()
                         ? Arg.getSingleStringRef().data()
                         : SA(Arg));
    });
  }

  using ArgumentConsumer = llvm::function_ref<void(const llvm::Twine &)>;
  void generateFrontendCommandLine(ArgumentConsumer Consumer) const;

  std::vector<std::string> getFrontendCommandLine() const;

private:
  static void GenerateDiagnosticArgs(const DiagnosticOptions &Opts,
                                     ArgumentConsumer Consumer,
                                     bool DefaultDiagColor);

  static void GenerateLangArgs(const LangOptions &Opts,
                               ArgumentConsumer Consumer, const llvm::Triple &T,
                               InputKind IK);

  // Generate command line options from CodeGenOptions.
  static void GenerateCodeGenArgs(const CodeGenOptions &Opts,
                                  ArgumentConsumer Consumer,
                                  const llvm::Triple &T,
                                  const std::string &OutputFile,
                                  const LangOptions *LangOpts);
};

class CompilerInvocation : public CompilerInvocationBase {
public:
  CompilerInvocation() = default;
  CompilerInvocation(const CompilerInvocation &X)
      : CompilerInvocationBase(EmptyConstructor{}) {
    deep_copy_assign(X);
  }
  CompilerInvocation(CompilerInvocation &&) = default;
  CompilerInvocation &operator=(const CompilerInvocation &X) {
    deep_copy_assign(X);
    return *this;
  }
  ~CompilerInvocation() = default;

  // Note: These need to be pulled in manually. Otherwise, they get hidden by
  // the mutable getters with the same names.
  using CompilerInvocationBase::getCodeGenOpts;
  using CompilerInvocationBase::getDependencyOutputOpts;
  using CompilerInvocationBase::getDiagnosticOpts;
  using CompilerInvocationBase::getFileSystemOpts;
  using CompilerInvocationBase::getFrontendOpts;
  using CompilerInvocationBase::getHeaderIdxOpts;
  using CompilerInvocationBase::getLangOpts;
  using CompilerInvocationBase::getPrepOpts;
  using CompilerInvocationBase::getPrepOutputOpts;
  using CompilerInvocationBase::getTargetOpts;

  LangOptions &getLangOpts() { return *LangOpts; }
  TargetOptions &getTargetOpts() { return *TargetOpts; }
  DiagnosticOptions &getDiagnosticOpts() { return *DiagnosticOpts; }
  HeaderIndexOptions &getHeaderIdxOpts() { return *HSOpts; }
  PrepOptions &getPrepOpts() { return *PPOpts; }
  CodeGenOptions &getCodeGenOpts() { return *CodeGenOpts; }
  FileSystemOptions &getFileSystemOpts() { return *FSOpts; }
  FrontendOptions &getFrontendOpts() { return *FrontendOpts; }
  DependencyOutputOptions &getDependencyOutputOpts() {
    return *DependencyOutputOpts;
  }
  PrepOutputOptions &getPrepOutputOpts() { return *PrepOutputOpts; }

  using CompilerInvocationBase::DiagnosticOpts;
  using CompilerInvocationBase::LangOpts;
  using CompilerInvocationBase::TargetOpts;
  std::shared_ptr<HeaderIndexOptions> getHeaderIdxOptsPtr() { return HSOpts; }
  std::shared_ptr<PrepOptions> getPrepOptsPtr() { return PPOpts; }

  static bool CreateFromArgs(CompilerInvocation &Res,
                             llvm::ArrayRef<const char *> CommandLineArgs,
                             DiagnosticsEngine &Diags,
                             const char *Argv0 = nullptr);

  static std::string GetResourcesPath(const char *Argv0, void *MainAddr);

private:
  static bool CreateFromArgsImpl(CompilerInvocation &Res,
                                 llvm::ArrayRef<const char *> CommandLineArgs,
                                 DiagnosticsEngine &Diags, const char *Argv0);

  static bool ParseLangArgs(LangOptions &Opts, llvm::opt::ArgList &Args,
                            InputKind IK, const llvm::Triple &T,
                            std::vector<std::string> &Includes,
                            DiagnosticsEngine &Diags);

  static bool ParseCodeGenArgs(CodeGenOptions &Opts, llvm::opt::ArgList &Args,
                               InputKind IK, DiagnosticsEngine &Diags,
                               const llvm::Triple &T,
                               const std::string &OutputFile,
                               const LangOptions &LangOptsRef);
};

class CowCompilerInvocation : public CompilerInvocationBase {
public:
  CowCompilerInvocation() = default;
  CowCompilerInvocation(const CowCompilerInvocation &X)
      : CompilerInvocationBase(EmptyConstructor{}) {
    shallow_copy_assign(X);
  }
  CowCompilerInvocation(CowCompilerInvocation &&) = default;
  CowCompilerInvocation &operator=(const CowCompilerInvocation &X) {
    shallow_copy_assign(X);
    return *this;
  }
  ~CowCompilerInvocation() = default;

  CowCompilerInvocation(const CompilerInvocation &X)
      : CompilerInvocationBase(EmptyConstructor{}) {
    deep_copy_assign(X);
  }

  CowCompilerInvocation(CompilerInvocation &&X)
      : CompilerInvocationBase(std::move(X)) {}

  // Const getters are inherited from the base class.

  LangOptions &getMutLangOpts();
  TargetOptions &getMutTargetOpts();
  DiagnosticOptions &getMutDiagnosticOpts();
  HeaderIndexOptions &getMutHeaderIdxOpts();
  PrepOptions &getMutPrepOpts();
  CodeGenOptions &getMutCodeGenOpts();
  FileSystemOptions &getMutFileSystemOpts();
  FrontendOptions &getMutFrontendOpts();
  DependencyOutputOptions &getMutDependencyOutputOpts();
  PrepOutputOptions &getMutPrepOutputOpts();
};

llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>
createVFSFromCompilerInvocation(const CompilerInvocation &CI,
                                DiagnosticsEngine &Diags);

llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> createVFSFromCompilerInvocation(
    const CompilerInvocation &CI, DiagnosticsEngine &Diags,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS);

llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> createVFSFromOverlayFiles(
    llvm::ArrayRef<std::string> VFSOverlayFiles, DiagnosticsEngine &Diags,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS);

} // namespace neverc

#endif // NEVERC_COMPILER_COMPILERINVOCATION_H
