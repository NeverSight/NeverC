#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Compiler/FrontendAction.h"
#include "neverc/Compiler/FrontendActions.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Compiler/TextDiagnosticPrinter.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Config/config.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/Stack.h"
#include "neverc/Foundation/Core/Version.h"
#include "neverc/Foundation/Diagnostic/DiagnosticDriver.h"
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.h"
#include "neverc/Foundation/LangOpts/LangStandard.h"
#include "neverc/Invoke/InMemoryFileStore.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetInfo.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepOptions.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

using namespace neverc;

namespace {

class OutputTransactionStream final : public llvm::raw_pwrite_stream {
public:
  explicit OutputTransactionStream(
      std::shared_ptr<OutputTransaction> TransactionValue)
      : Transaction(std::move(TransactionValue)) {}

  ~OutputTransactionStream() override { flush(); }

private:
  void write_impl(const char *Data, size_t Size) override {
    OutputTransactionResult Result = Transaction->write(
        llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(Data), Size));
    Position += Size;
    if (Result != OutputTransactionResult::Success)
      (void)Transaction->abort();
  }

  void pwrite_impl(const char *Data, size_t Size, uint64_t Offset) override {
    flush();
    OutputTransactionResult Result = Transaction->writeAt(
        Offset,
        llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(Data), Size));
    if (Result != OutputTransactionResult::Success)
      (void)Transaction->abort();
  }

  uint64_t current_pos() const override { return Position; }

  std::shared_ptr<OutputTransaction> Transaction;
  uint64_t Position = 0;
};

llvm::StringRef outputTransactionResultName(OutputTransactionResult Result) {
  switch (Result) {
  case OutputTransactionResult::Success:
    return "success";
  case OutputTransactionResult::InvalidArgument:
    return "invalid argument";
  case OutputTransactionResult::InvalidState:
    return "invalid state";
  case OutputTransactionResult::ResourceExhausted:
    return "resource limit exceeded";
  case OutputTransactionResult::IOFailure:
    return "I/O failure";
  case OutputTransactionResult::FailedPartial:
    return "partial failure requiring recovery";
  }
  return "unknown failure";
}

} // namespace

namespace neverc {

class OutputTransactionCrashCleanup {
  class AbortCleanup final
      : public llvm::CrashRecoveryContextCleanupBase<AbortCleanup,
                                                     OutputTransaction> {
  public:
    AbortCleanup(llvm::CrashRecoveryContext *Context,
                 OutputTransaction *Transaction)
        : llvm::CrashRecoveryContextCleanupBase<AbortCleanup,
                                                OutputTransaction>(
              Context, Transaction) {}

    void recoverResources() override { (void)this->resource->abort(); }
  };

  using Registrar =
      llvm::CrashRecoveryContextCleanupRegistrar<OutputTransaction,
                                                 AbortCleanup>;

public:
  explicit OutputTransactionCrashCleanup(OutputTransaction *Transaction)
      : Registration(Transaction) {}

private:
  Registrar Registration;
};

CompilerInstance::OutputFile::OutputFile(
    std::string FilenameValue, std::optional<llvm::sys::fs::TempFile> FileValue,
    std::shared_ptr<OutputTransaction> TransactionValue,
    std::unique_ptr<OutputTransactionCrashCleanup> CrashCleanupValue)
    : Filename(std::move(FilenameValue)), File(std::move(FileValue)),
      Transaction(std::move(TransactionValue)),
      CrashCleanup(std::move(CrashCleanupValue)) {}

CompilerInstance::OutputFile::~OutputFile() = default;
CompilerInstance::OutputFile::OutputFile(OutputFile &&) noexcept = default;

} // namespace neverc

CompilerInstance::CompilerInstance() : Invocation(new CompilerInvocation()) {}

CompilerInstance::~CompilerInstance() {
  assert(OutputFiles.empty() && "Still output files in flight?");
}

// ===----------------------------------------------------------------------===
// Accessors
// ===----------------------------------------------------------------------===

void CompilerInstance::setInvocation(
    std::shared_ptr<CompilerInvocation> Value) {
  Invocation = std::move(Value);
}

void CompilerInstance::setPluginTaskContext(
    std::unique_ptr<plugin::PluginTaskContext> Value) {
  PluginSourcePhases.reset();
  PluginTask = std::move(Value);
  OutputCoordinatorState =
      PluginTask ? &PluginTask->processServices().outputCoordinator()
                 : ConfiguredOutputCoordinator;
}

std::unique_ptr<plugin::PluginTaskContext>
CompilerInstance::takePluginTaskContext() {
  PluginSourcePhases.reset();
  std::unique_ptr<plugin::PluginTaskContext> Result = std::move(PluginTask);
  OutputCoordinatorState = ConfiguredOutputCoordinator;
  return Result;
}

void CompilerInstance::setPluginSourcePhaseRuntime(
    std::unique_ptr<plugin::PluginSourcePhaseRuntime> Value) {
  if (Value && PP) {
    if (llvm::Error E = Value->attachPrepEngine(*PP)) {
      std::string Message = llvm::toString(std::move(E)).str().str();
      getDiagnostics().Report(diag::err_drv_plugin_phase) << Message;
    }
  }
  if (Value && Context) {
    if (llvm::Error E = Value->attachTreeContext(*Context)) {
      std::string Message = llvm::toString(std::move(E)).str().str();
      getDiagnostics().Report(diag::err_drv_plugin_phase) << Message;
    }
  }
  if (Value && TheSema) {
    if (llvm::Error E = Value->attachSema(*TheSema)) {
      std::string Message = llvm::toString(std::move(E)).str().str();
      getDiagnostics().Report(diag::err_drv_plugin_phase) << Message;
    }
  }
  PluginSourcePhases = std::move(Value);
}

void CompilerInstance::clearPluginSourcePhaseRuntime() {
  PluginSourcePhases.reset();
}

void CompilerInstance::setDiagnostics(DiagnosticsEngine *Value) {
  Diagnostics = Value;
}

void CompilerInstance::setVerboseOutputStream(llvm::raw_ostream &Value) {
  OwnedVerboseOutputStream.reset();
  VerboseOutputStream = &Value;
}

void CompilerInstance::setVerboseOutputStream(
    std::unique_ptr<llvm::raw_ostream> Value) {
  OwnedVerboseOutputStream.swap(Value);
  VerboseOutputStream = OwnedVerboseOutputStream.get();
}

void CompilerInstance::setTarget(TargetInfo *Value) { Target = Value; }

bool CompilerInstance::createTarget() {
  std::unique_ptr<TargetInfo> PluginTarget;
  if (PluginTask) {
    std::shared_ptr<const plugin::PluginTargetSnapshot> Snapshot =
        plugin::findPluginTargetSnapshot(
            PluginTask->processServices(),
            PluginTask->session().handle());
    if (Snapshot) {
      const plugin::PluginTargetSnapshot::TargetRecord *Record =
          Snapshot->matchTarget(getInvocation().TargetOpts->Triple);
      if (Record) {
        const auto *ABI = Snapshot->findABI(Record->DefaultABI);
        const auto *CallingConvention =
            Snapshot->findCallingConvention(
                Record->DefaultCallingConvention);
        PluginTarget = std::make_unique<plugin::PluginTargetInfo>(
            *Record, getInvocation().TargetOpts->Triple, ABI,
            CallingConvention, PluginTask.get());
      }
    }
  }

  if (PluginTarget)
    setTarget(TargetInfo::CreateTargetInfo(
        getDiagnostics(), getInvocation().TargetOpts,
        std::move(PluginTarget)));
  else
    setTarget(TargetInfo::CreateTargetInfo(
        getDiagnostics(), getInvocation().TargetOpts));
  if (!hasTarget())
    return false;

  if (!getTarget().hasStrictFP() && !getLangOpts().ExpStrictFP) {
    if (getLangOpts().RoundingMath) {
      getDiagnostics().Report(diag::warn_fe_backend_unsupported_fp_rounding);
      getLangOpts().RoundingMath = false;
    }
    auto FPExc = getLangOpts().getFPExceptionMode();
    if (FPExc != LangOptions::FPE_Default && FPExc != LangOptions::FPE_Ignore) {
      getDiagnostics().Report(diag::warn_fe_backend_unsupported_fp_exceptions);
      getLangOpts().setFPExceptionMode(LangOptions::FPE_Ignore);
    }
  }

  // We should do it here because target knows nothing about
  // language options when it's being created.
  // Inform the target of the language options.
  getTarget().adjust(getDiagnostics(), getLangOpts());

  return true;
}

llvm::vfs::FileSystem &CompilerInstance::getVirtualFileSystem() const {
  return getFileManager().getVirtualFileSystem();
}

void CompilerInstance::setFileManager(FileManager *Value) { FileMgr = Value; }

void CompilerInstance::setSourceManager(SourceManager *Value) {
  SourceMgr = Value;
}

void CompilerInstance::setPrepEngine(std::shared_ptr<PrepEngine> Value) {
  PP = std::move(Value);
}

void CompilerInstance::setTreeContext(TreeContext *Value) {
  Context = Value;

  if (Context && Consumer)
    getTreeConsumer().Initialize(getTreeContext());
}

void CompilerInstance::setSema(Sema *S) { TheSema.reset(S); }

void CompilerInstance::setTreeConsumer(std::unique_ptr<TreeConsumer> Value) {
  Consumer = std::move(Value);

  if (Context && Consumer)
    getTreeConsumer().Initialize(getTreeContext());
}

std::unique_ptr<Sema> CompilerInstance::takeSema() {
  return std::move(TheSema);
}

// ===----------------------------------------------------------------------===
// Subsystem creation
// ===----------------------------------------------------------------------===

void CompilerInstance::createDiagnostics(DiagnosticConsumer *Client,
                                         bool ShouldOwnClient) {
  Diagnostics = createDiagnostics(&getDiagnosticOpts(), Client, ShouldOwnClient,
                                  &getCodeGenOpts());
}

llvm::IntrusiveRefCntPtr<DiagnosticsEngine> CompilerInstance::createDiagnostics(
    DiagnosticOptions *Opts, DiagnosticConsumer *Client, bool ShouldOwnClient,
    const CodeGenOptions *CodeGenOpts) {
  llvm::IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  llvm::IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      new DiagnosticsEngine(DiagID, Opts));

  if (Client) {
    Diags->setClient(Client, ShouldOwnClient);
  } else
    Diags->setClient(new TextDiagnosticPrinter(llvm::errs(), Opts));

  // Configure our handling of diagnostics.
  Diags->TreatWarningsAsErrors = CodeGenOpts->TreatWarningsAsErrors;
  ProcessWarningOptions(*Diags, *Opts);

  return Diags;
}

// File Manager

FileManager *CompilerInstance::createFileManager(
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS) {
  bool HadFileManager = FileMgr != nullptr;
  if (!VFS)
    VFS = FileMgr ? &FileMgr->getVirtualFileSystem()
                  : createVFSFromCompilerInvocation(getInvocation(),
                                                    getDiagnostics());
  assert(VFS && "FileManager has no VFS?");
  if (PluginTask && !HadFileManager) {
    auto PluginVFS =
        plugin::createPluginFileSystem(*PluginTask, std::move(VFS));
    if (!PluginVFS) {
      std::string Message = llvm::toString(PluginVFS.takeError()).str().str();
      getDiagnostics().Report(diag::err_drv_plugin_phase) << Message;
      return nullptr;
    }
    VFS = std::move(*PluginVFS);
  }
  FileMgr = new FileManager(getFileSystemOpts(), std::move(VFS));
  return FileMgr.get();
}

// Source Manager

void CompilerInstance::createSourceManager(FileManager &FileMgr) {
  SourceMgr = new SourceManager(getDiagnostics(), FileMgr);
}

// PrepEngine

void CompilerInstance::createPrepEngine() {
  const PrepOptions &PPOpts = getPrepOpts();

  IncludeResolver *HeaderInfo =
      new IncludeResolver(getHeaderIdxOptsPtr(), getSourceManager(),
                          getDiagnostics(), getLangOpts(), &getTarget());
  PP = std::make_shared<PrepEngine>(Invocation->getPrepOptsPtr(),
                                    getDiagnostics(), getLangOpts(),
                                    getSourceManager(), *HeaderInfo,
                                    /*IdentifierInfoLookup=*/nullptr,
                                    /*OwnsIncludeResolver=*/true);
  getTarget().adjust(getDiagnostics(), getLangOpts());
  PP->Initialize(getTarget());

  InitializePrepEngine(*PP, PPOpts, getFrontendOpts());

  const llvm::Triple *IncludeResolverTriple = &PP->getTargetInfo().getTriple();

  ApplyHeaderIndexOptions(PP->getIncludeResolver(), getHeaderIdxOpts(),
                          PP->getLangOpts(), *IncludeResolverTriple);

  PP->setPreprocessedOutput(getPrepOutputOpts().ShowCPP);

  const DependencyOutputOptions &DepOpts = getDependencyOutputOpts();
  if (!DepOpts.OutputFile.empty())
    addDependencyCollector(std::make_shared<DependencyFileGenerator>(DepOpts));

  for (auto &Listener : DependencyCollectors)
    Listener->attachToPrepEngine(*PP);

  if (DepOpts.ShowHeaderIncludes)
    AttachHeaderIncludeGen(*PP, DepOpts);
  if (!DepOpts.HeaderIncludeOutputFile.empty()) {
    llvm::StringRef OutputPath = DepOpts.HeaderIncludeOutputFile;
    if (OutputPath == "-")
      OutputPath = "";
    AttachHeaderIncludeGen(*PP, DepOpts,
                           /*ShowAllHeaders=*/true, OutputPath,
                           /*ShowDepth=*/false);
  }

  if (DepOpts.ShowIncludesDest != ShowIncludesDestination::None) {
    AttachHeaderIncludeGen(*PP, DepOpts,
                           /*ShowAllHeaders=*/true, /*OutputPath=*/"",
                           /*ShowDepth=*/true, /*MSStyle=*/true);
  }
  if (PluginSourcePhases) {
    if (llvm::Error E = PluginSourcePhases->attachPrepEngine(*PP)) {
      std::string Message = llvm::toString(std::move(E)).str().str();
      getDiagnostics().Report(diag::err_drv_plugin_phase) << Message;
    }
  }
}

// TreeContext

void CompilerInstance::createTreeContext() {
  PrepEngine &PP = getPrepEngine();
  auto *Context = new TreeContext(getLangOpts(), PP.getSourceManager(),
                                  PP.getIdentifierTable(), PP.getBuiltinInfo());
  Context->InitBuiltinTypes(getTarget());
  setTreeContext(Context);
  if (PluginSourcePhases) {
    if (llvm::Error E = PluginSourcePhases->attachTreeContext(*Context)) {
      std::string Message = llvm::toString(std::move(E)).str().str();
      getDiagnostics().Report(diag::err_drv_plugin_phase) << Message;
    }
  }
}

void CompilerInstance::createFrontendTimer() {
  FrontendTimerGroup.reset(
      new llvm::TimerGroup("frontend", "NeverC front-end time report"));
  FrontendTimer.reset(new llvm::Timer("frontend", "NeverC front-end timer",
                                      *FrontendTimerGroup));
}

void CompilerInstance::clearFrontendTimer() {
  if (FrontendTimer && FrontendTimer->isRunning())
    FrontendTimer->stopTimer();
  FrontendTimer.reset();
  FrontendTimerGroup.reset();
}

void CompilerInstance::createSema() {
  TheSema.reset(new Sema(getPrepEngine(), getTreeContext(), getTreeConsumer()));
  if (PluginSourcePhases) {
    if (llvm::Error E = PluginSourcePhases->attachSema(*TheSema)) {
      std::string Message = llvm::toString(std::move(E)).str().str();
      getDiagnostics().Report(diag::err_drv_plugin_phase) << Message;
    }
  }
}

// ===----------------------------------------------------------------------===
// Output files
// ===----------------------------------------------------------------------===

void CompilerInstance::clearOutputFiles(bool EraseFiles) {
  assert(!hasTreeConsumer() && "TreeConsumer should be reset");
  for (OutputFile &OF : OutputFiles) {
    // Normal teardown owns this transaction again. Unregister the raw-pointer
    // crash cleanup before finish/commit/abort can change its state and before
    // releasing the shared owner below. A crash-abandoned CompilerInstance is
    // intentionally never destroyed after its CRC has fired the registrar.
    OF.CrashCleanup.reset();
    if (OF.Transaction) {
      if (EraseFiles) {
        OutputTransactionResult Result = OF.Transaction->abort();
        if (Result == OutputTransactionResult::FailedPartial)
          getDiagnostics().Report(diag::err_unable_to_rename_temp)
              << "<output transaction>" << OF.Filename
              << createStringError(llvm::inconvertibleErrorCode(),
                                   outputTransactionResultName(Result));
        continue;
      }

      OutputTransactionResult FinishResult = OF.Transaction->finish();
      if (FinishResult != OutputTransactionResult::Success) {
        getDiagnostics().Report(diag::err_unable_to_rename_temp)
            << "<output transaction>" << OF.Filename
            << createStringError(llvm::inconvertibleErrorCode(),
                                 outputTransactionResultName(FinishResult));
        continue;
      }
      auto Committed = OF.Transaction->commit();
      if (!Committed)
        getDiagnostics().Report(diag::err_unable_to_rename_temp)
            << "<output transaction>" << OF.Filename << Committed.takeError();
      continue;
    }

    if (EraseFiles) {
      if (OF.File)
        consumeError(OF.File->discard());
      if (!OF.Filename.empty())
        llvm::sys::fs::remove(OF.Filename);
      continue;
    }

    if (!OF.File)
      continue;

    if (OF.File->TmpName.empty()) {
      consumeError(OF.File->discard());
      continue;
    }

    llvm::Error E = OF.File->keep(OF.Filename);
    if (!E)
      continue;

    getDiagnostics().Report(diag::err_unable_to_rename_temp)
        << OF.File->TmpName << OF.Filename << std::move(E);

    llvm::sys::fs::remove(OF.File->TmpName);
  }
  OutputFiles.clear();
}

std::unique_ptr<raw_pwrite_stream> CompilerInstance::createDefaultOutputFile(
    bool Binary, llvm::StringRef InFile, llvm::StringRef Extension,
    bool RemoveFileOnSignal, bool CreateMissingDirectories,
    bool ForceUseTemporary) {
  llvm::StringRef OutputPath = getFrontendOpts().OutputFile;
  std::optional<llvm::SmallString<128>> PathStorage;
  if (OutputPath.empty()) {
    if (InFile == "-" || Extension.empty()) {
      OutputPath = "-";
    } else {
      PathStorage.emplace(InFile);
      llvm::sys::path::replace_extension(*PathStorage, Extension);
      OutputPath = *PathStorage;
    }
  }

  return createOutputFile(OutputPath, Binary, RemoveFileOnSignal,
                          getFrontendOpts().UseTemporary || ForceUseTemporary,
                          CreateMissingDirectories);
}

std::unique_ptr<raw_pwrite_stream> CompilerInstance::createNullOutputFile() {
  return std::make_unique<llvm::raw_null_ostream>();
}

std::unique_ptr<raw_pwrite_stream>
CompilerInstance::createOutputFile(llvm::StringRef OutputPath, bool Binary,
                                   bool RemoveFileOnSignal, bool UseTemporary,
                                   bool CreateMissingDirectories) {
  llvm::Expected<std::unique_ptr<raw_pwrite_stream>> OS =
      createOutputFileImpl(OutputPath, Binary, RemoveFileOnSignal, UseTemporary,
                           CreateMissingDirectories);
  if (OS)
    return std::move(*OS);
  getDiagnostics().Report(diag::err_fe_unable_to_open_output)
      << OutputPath << errorToErrorCode(OS.takeError()).message();
  return nullptr;
}

llvm::Expected<std::unique_ptr<llvm::raw_pwrite_stream>>
CompilerInstance::createOutputFileImpl(llvm::StringRef OutputPath, bool Binary,
                                       bool RemoveFileOnSignal,
                                       bool UseTemporary,
                                       bool CreateMissingDirectories) {
  assert((!CreateMissingDirectories || UseTemporary) &&
         "CreateMissingDirectories is only allowed when using temporary files");

  // If '-working-directory' was passed, the output filename should be
  // relative to that.
  std::optional<llvm::SmallString<128>> AbsPath;
  if (OutputPath != "-" && !llvm::sys::path::is_absolute(OutputPath)) {
    assert(hasFileManager() &&
           "File Manager is required to fix up relative path.\n");

    AbsPath.emplace(OutputPath);
    FileMgr->FixupRelativePath(*AbsPath);
    OutputPath = *AbsPath;
  }

  // In the integrated compiler+linker pipeline, LTO bitcode stays in memory.
  // The linker reads from InMemoryFileStore instead of the filesystem.
  if (Binary && OutputPath != "-" && hasInvocation() &&
      getCodeGenOpts().InMemoryLTOOutput) {
    // Estimate bitcode size from the source file to pre-allocate the buffer
    // and avoid SmallVector growth reallocations during WriteBitcodeToFile.
    // Bitcode is typically 1.5-2.5x the source size; use 2x as a safe
    // multiplier with a 64 KB floor.
    size_t Hint = 65536;
    if (hasSourceManager()) {
      if (auto *FE = getSourceManager().getFileEntryForID(
              getSourceManager().getMainFileID()))
        Hint = std::max(Hint, static_cast<size_t>(FE->getSize()) * 2);
    }
    auto &Buf = InMemoryFileStore::instance().create(OutputPath, Hint);
    OutputFiles.emplace_back(OutputPath.str(), std::nullopt);
    return std::make_unique<llvm::raw_svector_ostream>(Buf);
  }

  bool IsTransactionalFile = OutputPath != "-";
  if (IsTransactionalFile) {
    llvm::sys::fs::file_status Status;
    std::error_code StatusError = llvm::sys::fs::status(OutputPath, Status);
    if (!StatusError && llvm::sys::fs::exists(Status)) {
      if (!llvm::sys::fs::can_write(OutputPath))
        return llvm::errorCodeToError(
            make_error_code(llvm::errc::operation_not_permitted));
      IsTransactionalFile = llvm::sys::fs::is_regular_file(Status);
    }
  }

  if (IsTransactionalFile) {
    if (CreateMissingDirectories) {
      llvm::StringRef Parent = llvm::sys::path::parent_path(OutputPath);
      if (!Parent.empty()) {
        std::error_code DirectoryError =
            llvm::sys::fs::create_directories(Parent);
        if (DirectoryError)
          return llvm::errorCodeToError(DirectoryError);
      }
    }

    OutputLeaseOwner LeaseOwner{
        UINT64_MAX, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this))};
    if (PluginTask)
      LeaseOwner = {PluginTask->handle().Owner, PluginTask->handle().Value};
    auto Transaction = OutputTransaction::createFile(
        *OutputCoordinatorState, OutputPath,
        std::numeric_limits<uint64_t>::max(),
        [Task = PluginTask.get()] {
          return Task && Task->checkCancelled().Code == NEVERC_STATUS_CANCELLED;
        },
        {}, LeaseOwner);
    if (!Transaction)
      return Transaction.takeError();

    std::unique_ptr<OutputTransactionCrashCleanup> CrashCleanup;
    if (llvm::CrashRecoveryContext::GetCurrent())
      CrashCleanup =
          std::make_unique<OutputTransactionCrashCleanup>(Transaction->get());
    OutputFiles.emplace_back(OutputPath.str(), std::nullopt, *Transaction,
                             std::move(CrashCleanup));
    return std::make_unique<OutputTransactionStream>(std::move(*Transaction));
  }

  std::unique_ptr<llvm::raw_fd_ostream> OS;
  std::optional<llvm::StringRef> OSFile;

  if (UseTemporary) {
    if (OutputPath == "-")
      UseTemporary = false;
    else {
      llvm::sys::fs::file_status Status;
      llvm::sys::fs::status(OutputPath, Status);
      if (llvm::sys::fs::exists(Status)) {
        // Fail early if we can't write to the final destination.
        if (!llvm::sys::fs::can_write(OutputPath))
          return llvm::errorCodeToError(
              make_error_code(llvm::errc::operation_not_permitted));

        // Don't use a temporary if the output is a special file. This handles
        // things like '-o /dev/null'
        if (!llvm::sys::fs::is_regular_file(Status))
          UseTemporary = false;
      }
    }
  }

  std::optional<llvm::sys::fs::TempFile> Temp;
  if (UseTemporary) {
    // Create a temporary file. Insert -%%%%%%%% before the extension (if any)
    // so that build tooling can distinguish in-flight outputs from finalized
    // ones; we also append .tmp so external scrapers can ignore them.
    llvm::StringRef OutputExtension = llvm::sys::path::extension(OutputPath);
    llvm::SmallString<128> TempPath =
        llvm::StringRef(OutputPath).drop_back(OutputExtension.size());
    TempPath += "-%%%%%%%%";
    TempPath += OutputExtension;
    TempPath += ".tmp";
    llvm::sys::fs::OpenFlags BinaryFlags =
        Binary ? llvm::sys::fs::OF_None : llvm::sys::fs::OF_Text;
    llvm::Expected<llvm::sys::fs::TempFile> ExpectedFile =
        llvm::sys::fs::TempFile::create(
            TempPath, llvm::sys::fs::all_read | llvm::sys::fs::all_write,
            BinaryFlags);

    llvm::Error E = handleErrors(
        ExpectedFile.takeError(), [&](const llvm::ECError &E) -> llvm::Error {
          int ec_val = E.convertToErrorCode();
          std::error_code EC = std::error_code(ec_val, std::generic_category());
          if (CreateMissingDirectories && ec_val == ENOENT) {
            llvm::StringRef Parent = llvm::sys::path::parent_path(OutputPath);
            EC = llvm::sys::fs::create_directories(Parent);
            if (!EC) {
              ExpectedFile = llvm::sys::fs::TempFile::create(
                  TempPath, llvm::sys::fs::all_read | llvm::sys::fs::all_write,
                  BinaryFlags);
              if (!ExpectedFile)
                return llvm::errorCodeToError(
                    llvm::errc::no_such_file_or_directory);
            }
          }
          return llvm::errorCodeToError(EC);
        });

    if (E) {
      consumeError(std::move(E));
    } else {
      Temp = std::move(ExpectedFile.get());
      OS.reset(new llvm::raw_fd_ostream(Temp->FD, /*shouldClose=*/false));
      OSFile = Temp->TmpName;
    }
    // If we failed to create the temporary, fallback to writing to the file
    // directly. This handles the corner case where we cannot write to the
    // directory, but can write to the file.
  }

  if (!OS) {
    OSFile = OutputPath;
    std::error_code EC;
    OS.reset(new llvm::raw_fd_ostream(
        *OSFile, EC,
        (Binary ? llvm::sys::fs::OF_None : llvm::sys::fs::OF_TextWithCRLF)));
    if (EC)
      return llvm::errorCodeToError(EC);
  }

  // Add the output file -- but don't try to remove "-", since this means we are
  // using stdin.
  OutputFiles.emplace_back(((OutputPath != "-") ? OutputPath : "").str(),
                           std::move(Temp));

  if (!Binary || OS->supportsSeeking())
    return std::move(OS);

  return std::make_unique<llvm::buffer_unique_ostream>(std::move(OS));
}

// Initialization Utilities

// ===----------------------------------------------------------------------===
// Initialization & execution
// ===----------------------------------------------------------------------===

bool CompilerInstance::InitializeSourceManager(const FrontendInputFile &Input) {
  return InitializeSourceManager(Input, getDiagnostics(), getFileManager(),
                                 getSourceManager());
}

// static
bool CompilerInstance::InitializeSourceManager(const FrontendInputFile &Input,
                                               DiagnosticsEngine &Diags,
                                               FileManager &FileMgr,
                                               SourceManager &SourceMgr) {
  SrcMgr::CharacteristicKind Kind =
      Input.isSystem() ? SrcMgr::C_System : SrcMgr::C_User;

  if (Input.isBuffer()) {
    SourceMgr.setMainFileID(SourceMgr.createFileID(Input.getBuffer(), Kind));
    assert(SourceMgr.getMainFileID().isValid() &&
           "Couldn't establish MainFileID!");
    return true;
  }

  llvm::StringRef InputFile = Input.getFile();

  // Figure out where to get and map in the main file.
  auto FileOrErr = InputFile == "-"
                       ? FileMgr.getSTDIN()
                       : FileMgr.getFileRef(InputFile, /*OpenFile=*/true);
  if (!FileOrErr) {
    auto EC = llvm::errorToErrorCode(FileOrErr.takeError());
    if (InputFile != "-")
      Diags.Report(diag::err_fe_error_reading) << InputFile << EC.message();
    else
      Diags.Report(diag::err_fe_error_reading_stdin) << EC.message();
    return false;
  }

  SourceMgr.setMainFileID(
      SourceMgr.createFileID(*FileOrErr, SourceLocation(), Kind));

  assert(SourceMgr.getMainFileID().isValid() &&
         "Couldn't establish MainFileID!");
  return true;
}

// High-Level Operations

bool CompilerInstance::ExecuteAction(FrontendAction &Act) {
  assert(hasDiagnostics() && "Diagnostics engine is not initialized!");
  assert(!getFrontendOpts().ShowHelp && "Client must handle '-help'!");
  assert(!getFrontendOpts().ShowVersion && "Client must handle '-version'!");

  // Mark this point as the bottom of the stack if we don't have somewhere
  // better. We generally expect frontend actions to be invoked with (nearly)
  // DesiredStackSpace available.
  noteBottomOfStack();

  auto FinishDiagnosticClient = llvm::make_scope_exit([&]() {
    // Notify the diagnostic client that all files were processed.
    getDiagnosticClient().finish();
  });

  llvm::raw_ostream &OS = getVerboseOutputStream();

  if (!Act.PrepareToExecute(*this))
    return false;

  if (!createTarget())
    return false;

  // Validate/process some options.
  if (getHeaderIdxOpts().Verbose)
    OS << "neverc frontend version " NEVERC_VERSION_STRING
       << " based upon LLVM " << LLVM_VERSION_STRING << " default target "
       << llvm::sys::getDefaultTargetTriple() << "\n";

  if (getCodeGenOpts().TimePasses)
    createFrontendTimer();

  if (getFrontendOpts().ShowStats || !getFrontendOpts().StatsFile.empty())
    llvm::EnableStatistics(false);

  for (const FrontendInputFile &FIF : getFrontendOpts().Inputs) {
    // Reset the ID tables if we are reusing the SourceManager.
    if (hasSourceManager())
      getSourceManager().clearIDTables();

    if (Act.BeginSourceFile(*this, FIF)) {
      if (llvm::Error Err = Act.Execute()) {
        consumeError(std::move(Err));
      }
      Act.EndSourceFile();
    }
  }

  if (getDiagnosticOpts().ShowCarets) {
    // We can have multiple diagnostics sharing one diagnostic client.
    unsigned NumWarnings = getDiagnostics().getClient()->getNumWarnings();
    unsigned NumErrors = getDiagnostics().getClient()->getNumErrors();

    if (NumWarnings)
      OS << NumWarnings << " warning" << (NumWarnings == 1 ? "" : "s");
    if (NumWarnings && NumErrors)
      OS << " and ";
    if (NumErrors)
      OS << NumErrors << " error" << (NumErrors == 1 ? "" : "s");
    if (NumWarnings || NumErrors) {
      OS << " generated";
      OS << ".\n";
    }
  }

  if (getFrontendOpts().ShowStats) {
    if (hasFileManager()) {
      getFileManager().PrintStats();
      OS << '\n';
    }
    llvm::PrintStatistics(OS);
  }
  llvm::StringRef StatsFile = getFrontendOpts().StatsFile;
  if (!StatsFile.empty()) {
    llvm::sys::fs::OpenFlags FileFlags = llvm::sys::fs::OF_TextWithCRLF;
    if (getFrontendOpts().AppendStats)
      FileFlags |= llvm::sys::fs::OF_Append;
    std::error_code EC;
    auto StatS =
        std::make_unique<llvm::raw_fd_ostream>(StatsFile, EC, FileFlags);
    if (EC) {
      getDiagnostics().Report(diag::warn_fe_unable_to_open_stats_file)
          << StatsFile << EC.message();
    } else {
      llvm::PrintStatisticsJSON(*StatS);
    }
  }

  return !getDiagnostics().getClient()->getNumErrors();
}

void CompilerInstance::resetAndLeakSema() { llvm::BuryPointer(takeSema()); }
