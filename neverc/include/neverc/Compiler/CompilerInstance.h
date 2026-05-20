#ifndef NEVERC_COMPILER_COMPILERINSTANCE_H_
#define NEVERC_COMPILER_COMPILERINSTANCE_H_

#include "neverc/Compiler/CompilerInvocation.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/HeaderIndexOptions.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace llvm {
class Timer;
class TimerGroup;
} // namespace llvm

namespace neverc {

using llvm::raw_pwrite_stream;

class TreeContext;

class DiagnosticsEngine;
class DiagnosticConsumer;
class FileManager;
class FrontendAction;
class PrepEngine;
class Sema;
class SourceManager;
class TargetInfo;

class CompilerInstance {
  std::shared_ptr<CompilerInvocation> Invocation;

  llvm::IntrusiveRefCntPtr<DiagnosticsEngine> Diagnostics;

  llvm::IntrusiveRefCntPtr<TargetInfo> Target;

  llvm::IntrusiveRefCntPtr<FileManager> FileMgr;

  llvm::IntrusiveRefCntPtr<SourceManager> SourceMgr;

  std::shared_ptr<PrepEngine> PP;

  llvm::IntrusiveRefCntPtr<TreeContext> Context;

  std::unique_ptr<TreeConsumer> Consumer;

  std::unique_ptr<Sema> TheSema;

  std::unique_ptr<llvm::TimerGroup> FrontendTimerGroup;

  std::unique_ptr<llvm::Timer> FrontendTimer;

  std::vector<std::shared_ptr<DependencyCollector>> DependencyCollectors;

  std::unique_ptr<llvm::raw_ostream> OwnedVerboseOutputStream;

  llvm::raw_ostream *VerboseOutputStream = &llvm::errs();

  struct OutputFile {
    std::string Filename;
    std::optional<llvm::sys::fs::TempFile> File;

    OutputFile(std::string filename,
               std::optional<llvm::sys::fs::TempFile> file)
        : Filename(std::move(filename)), File(std::move(file)) {}
  };

  std::list<OutputFile> OutputFiles;

  std::unique_ptr<llvm::raw_pwrite_stream> OutputStream;

  CompilerInstance(const CompilerInstance &) = delete;
  void operator=(const CompilerInstance &) = delete;

public:
  explicit CompilerInstance();
  ~CompilerInstance();

  //
  bool ExecuteAction(FrontendAction &Act);

  bool hasInvocation() const { return Invocation != nullptr; }

  CompilerInvocation &getInvocation() {
    assert(Invocation && "Compiler instance has no invocation!");
    return *Invocation;
  }

  std::shared_ptr<CompilerInvocation> getInvocationPtr() { return Invocation; }

  void setInvocation(std::shared_ptr<CompilerInvocation> Value);

  CodeGenOptions &getCodeGenOpts() { return Invocation->getCodeGenOpts(); }
  const CodeGenOptions &getCodeGenOpts() const {
    return Invocation->getCodeGenOpts();
  }

  DependencyOutputOptions &getDependencyOutputOpts() {
    return Invocation->getDependencyOutputOpts();
  }
  const DependencyOutputOptions &getDependencyOutputOpts() const {
    return Invocation->getDependencyOutputOpts();
  }

  DiagnosticOptions &getDiagnosticOpts() {
    return Invocation->getDiagnosticOpts();
  }
  const DiagnosticOptions &getDiagnosticOpts() const {
    return Invocation->getDiagnosticOpts();
  }

  FileSystemOptions &getFileSystemOpts() {
    return Invocation->getFileSystemOpts();
  }
  const FileSystemOptions &getFileSystemOpts() const {
    return Invocation->getFileSystemOpts();
  }

  FrontendOptions &getFrontendOpts() { return Invocation->getFrontendOpts(); }
  const FrontendOptions &getFrontendOpts() const {
    return Invocation->getFrontendOpts();
  }

  HeaderIndexOptions &getHeaderIdxOpts() {
    return Invocation->getHeaderIdxOpts();
  }
  const HeaderIndexOptions &getHeaderIdxOpts() const {
    return Invocation->getHeaderIdxOpts();
  }
  std::shared_ptr<HeaderIndexOptions> getHeaderIdxOptsPtr() const {
    return Invocation->getHeaderIdxOptsPtr();
  }

  LangOptions &getLangOpts() { return Invocation->getLangOpts(); }
  const LangOptions &getLangOpts() const { return Invocation->getLangOpts(); }

  PrepOptions &getPrepOpts() { return Invocation->getPrepOpts(); }
  const PrepOptions &getPrepOpts() const { return Invocation->getPrepOpts(); }

  PrepOutputOptions &getPrepOutputOpts() {
    return Invocation->getPrepOutputOpts();
  }
  const PrepOutputOptions &getPrepOutputOpts() const {
    return Invocation->getPrepOutputOpts();
  }

  TargetOptions &getTargetOpts() { return Invocation->getTargetOpts(); }
  const TargetOptions &getTargetOpts() const {
    return Invocation->getTargetOpts();
  }

  bool hasDiagnostics() const { return Diagnostics != nullptr; }

  DiagnosticsEngine &getDiagnostics() const {
    assert(Diagnostics && "Compiler instance has no diagnostics!");
    return *Diagnostics;
  }

  llvm::IntrusiveRefCntPtr<DiagnosticsEngine> getDiagnosticsPtr() const {
    assert(Diagnostics && "Compiler instance has no diagnostics!");
    return Diagnostics;
  }

  void setDiagnostics(DiagnosticsEngine *Value);

  DiagnosticConsumer &getDiagnosticClient() const {
    assert(Diagnostics && Diagnostics->getClient() &&
           "Compiler instance has no diagnostic client!");
    return *Diagnostics->getClient();
  }

  void setVerboseOutputStream(llvm::raw_ostream &Value);

  void setVerboseOutputStream(std::unique_ptr<llvm::raw_ostream> Value);

  llvm::raw_ostream &getVerboseOutputStream() { return *VerboseOutputStream; }

  bool hasTarget() const { return Target != nullptr; }

  TargetInfo &getTarget() const {
    assert(Target && "Compiler instance has no target!");
    return *Target;
  }

  llvm::IntrusiveRefCntPtr<TargetInfo> getTargetPtr() const {
    assert(Target && "Compiler instance has no target!");
    return Target;
  }

  void setTarget(TargetInfo *Value);

  // Create Target based on current options.
  bool createTarget();

  llvm::vfs::FileSystem &getVirtualFileSystem() const;

  bool hasFileManager() const { return FileMgr != nullptr; }

  FileManager &getFileManager() const {
    assert(FileMgr && "Compiler instance has no file manager!");
    return *FileMgr;
  }

  llvm::IntrusiveRefCntPtr<FileManager> getFileManagerPtr() const {
    assert(FileMgr && "Compiler instance has no file manager!");
    return FileMgr;
  }

  void resetAndLeakFileManager() {
    llvm::BuryPointer(FileMgr.get());
    FileMgr.resetWithoutRelease();
  }

  void setFileManager(FileManager *Value);

  bool hasSourceManager() const { return SourceMgr != nullptr; }

  SourceManager &getSourceManager() const {
    assert(SourceMgr && "Compiler instance has no source manager!");
    return *SourceMgr;
  }

  llvm::IntrusiveRefCntPtr<SourceManager> getSourceManagerPtr() const {
    assert(SourceMgr && "Compiler instance has no source manager!");
    return SourceMgr;
  }

  void resetAndLeakSourceManager() {
    llvm::BuryPointer(SourceMgr.get());
    SourceMgr.resetWithoutRelease();
  }

  void setSourceManager(SourceManager *Value);

  bool hasPrepEngine() const { return PP != nullptr; }

  PrepEngine &getPrepEngine() const {
    assert(PP && "Compiler instance has no preprocessor!");
    return *PP;
  }

  std::shared_ptr<PrepEngine> getPrepEnginePtr() { return PP; }

  void resetAndLeakPreprocessor() {
    llvm::BuryPointer(new std::shared_ptr<PrepEngine>(PP));
  }

  void setPrepEngine(std::shared_ptr<PrepEngine> Value);

  bool hasTreeContext() const { return Context != nullptr; }

  TreeContext &getTreeContext() const {
    assert(Context && "Compiler instance has no AST context!");
    return *Context;
  }

  llvm::IntrusiveRefCntPtr<TreeContext> getTreeContextPtr() const {
    assert(Context && "Compiler instance has no AST context!");
    return Context;
  }

  void resetAndLeakTreeContext() {
    llvm::BuryPointer(Context.get());
    Context.resetWithoutRelease();
  }

  void setTreeContext(TreeContext *Value);

  void setSema(Sema *S);

  bool hasTreeConsumer() const { return (bool)Consumer; }

  TreeConsumer &getTreeConsumer() const {
    assert(Consumer && "Compiler instance has no TreeConsumer!");
    return *Consumer;
  }

  std::unique_ptr<TreeConsumer> takeTreeConsumer() {
    return std::move(Consumer);
  }

  void setTreeConsumer(std::unique_ptr<TreeConsumer> Value);

  bool hasSema() const { return (bool)TheSema; }

  Sema &getSema() const {
    assert(TheSema && "Compiler instance has no Sema object!");
    return *TheSema;
  }

  std::unique_ptr<Sema> takeSema();
  void resetAndLeakSema();

  bool hasFrontendTimer() const { return (bool)FrontendTimer; }

  llvm::Timer &getFrontendTimer() const {
    assert(FrontendTimer && "Compiler instance has no frontend timer!");
    return *FrontendTimer;
  }

  void clearOutputFiles(bool EraseFiles);

  void createDiagnostics(DiagnosticConsumer *Client = nullptr,
                         bool ShouldOwnClient = true);

  static llvm::IntrusiveRefCntPtr<DiagnosticsEngine> createDiagnostics(
      DiagnosticOptions *Opts, DiagnosticConsumer *Client = nullptr,
      bool ShouldOwnClient = true, const CodeGenOptions *CodeGenOpts = nullptr);

  FileManager *createFileManager(
      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS = nullptr);

  void createSourceManager(FileManager &FileMgr);

  void createPrepEngine();

  void createTreeContext();

  void createSema();

  void createFrontendTimer();

  std::unique_ptr<raw_pwrite_stream> createDefaultOutputFile(
      bool Binary = true, llvm::StringRef BaseInput = "",
      llvm::StringRef Extension = "", bool RemoveFileOnSignal = true,
      bool CreateMissingDirectories = false, bool ForceUseTemporary = false);

  std::unique_ptr<raw_pwrite_stream>
  createOutputFile(llvm::StringRef OutputPath, bool Binary,
                   bool RemoveFileOnSignal, bool UseTemporary,
                   bool CreateMissingDirectories = false);

private:
  llvm::Expected<std::unique_ptr<raw_pwrite_stream>>
  createOutputFileImpl(llvm::StringRef OutputPath, bool Binary,
                       bool RemoveFileOnSignal, bool UseTemporary,
                       bool CreateMissingDirectories);

public:
  std::unique_ptr<raw_pwrite_stream> createNullOutputFile();

  bool InitializeSourceManager(const FrontendInputFile &Input);

  static bool InitializeSourceManager(const FrontendInputFile &Input,
                                      DiagnosticsEngine &Diags,
                                      FileManager &FileMgr,
                                      SourceManager &SourceMgr);

  void setOutputStream(std::unique_ptr<llvm::raw_pwrite_stream> OutStream) {
    OutputStream = std::move(OutStream);
  }

  std::unique_ptr<llvm::raw_pwrite_stream> takeOutputStream() {
    return std::move(OutputStream);
  }

public:
  void addDependencyCollector(std::shared_ptr<DependencyCollector> Listener) {
    DependencyCollectors.push_back(std::move(Listener));
  }
};

} // end namespace neverc

#endif
