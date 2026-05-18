#include "neverc/Compiler/DependencyOutputOptions.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
using namespace neverc;

// ===----------------------------------------------------------------------===
// HeaderIncludesCallback
// ===----------------------------------------------------------------------===

namespace {
class HeaderIncludesCallback : public PrepObserver {
  SourceManager &SM;
  llvm::raw_ostream *OutputFile;
  const DependencyOutputOptions &DepOpts;
  unsigned CurrentIncludeDepth;
  bool HasProcessedPredefines;
  bool OwnsOutputFile;
  bool ShowAllHeaders;
  bool ShowDepth;
  bool MSStyle;

public:
  HeaderIncludesCallback(const PrepEngine *PP, bool ShowAllHeaders_,
                         llvm::raw_ostream *OutputFile_,
                         const DependencyOutputOptions &DepOpts,
                         bool OwnsOutputFile_, bool ShowDepth_, bool MSStyle_)
      : SM(PP->getSourceManager()), OutputFile(OutputFile_), DepOpts(DepOpts),
        CurrentIncludeDepth(0), HasProcessedPredefines(false),
        OwnsOutputFile(OwnsOutputFile_), ShowAllHeaders(ShowAllHeaders_),
        ShowDepth(ShowDepth_), MSStyle(MSStyle_) {}

  ~HeaderIncludesCallback() override {
    if (OwnsOutputFile)
      delete OutputFile;
  }

  HeaderIncludesCallback(const HeaderIncludesCallback &) = delete;
  HeaderIncludesCallback &operator=(const HeaderIncludesCallback &) = delete;

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override;

  void FileSkipped(const FileEntryRef &SkippedFile, const Token &FilenameTok,
                   SrcMgr::CharacteristicKind FileType) override;

private:
  bool ShouldShowHeader(SrcMgr::CharacteristicKind HeaderType) {
    if (!DepOpts.IncludeSystemHeaders && isSystem(HeaderType))
      return false;

    // Show the current header if we are (a) past the predefines, or (b) showing
    // all headers and in the predefines at a depth past the initial file and
    // command line buffers.
    return (HasProcessedPredefines ||
            (ShowAllHeaders && CurrentIncludeDepth > 2));
  }
};

// ===----------------------------------------------------------------------===
// HeaderIncludesJSONCallback
// ===----------------------------------------------------------------------===

class HeaderIncludesJSONCallback : public PrepObserver {
  SourceManager &SM;
  llvm::raw_ostream *OutputFile;
  bool OwnsOutputFile;
  llvm::SmallVector<std::string, 16> IncludedHeaders;

public:
  HeaderIncludesJSONCallback(const PrepEngine *PP,
                             llvm::raw_ostream *OutputFile_,
                             bool OwnsOutputFile_)
      : SM(PP->getSourceManager()), OutputFile(OutputFile_),
        OwnsOutputFile(OwnsOutputFile_) {}

  ~HeaderIncludesJSONCallback() override {
    if (OwnsOutputFile)
      delete OutputFile;
  }

  HeaderIncludesJSONCallback(const HeaderIncludesJSONCallback &) = delete;
  HeaderIncludesJSONCallback &
  operator=(const HeaderIncludesJSONCallback &) = delete;

  void EndOfMainFile() override;

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override;

  void FileSkipped(const FileEntryRef &SkippedFile, const Token &FilenameTok,
                   SrcMgr::CharacteristicKind FileType) override;
};
} // namespace

namespace {
void genHeaderInfo(llvm::raw_ostream *OutputFile, llvm::StringRef Filename,
                   bool ShowDepth, unsigned CurrentIncludeDepth, bool MSStyle) {
  // Write to a temporary string to avoid unnecessary flushing on errs().
  llvm::SmallString<512> Pathname(Filename);
  if (!MSStyle)
    SourceScanner::escapeStringLiteral(Pathname);

  llvm::SmallString<256> Msg;
  if (MSStyle)
    Msg += "Note: including file:";

  if (ShowDepth) {
    // The main source file is at depth 1, so skip one dot.
    for (unsigned i = 1; i != CurrentIncludeDepth; ++i)
      Msg += MSStyle ? ' ' : '.';

    if (!MSStyle)
      Msg += ' ';
  }
  Msg += Pathname;
  Msg += '\n';

  *OutputFile << Msg;
  OutputFile->flush();
}

} // namespace

// ===----------------------------------------------------------------------===
// AttachHeaderIncludeGen
// ===----------------------------------------------------------------------===

void neverc::AttachHeaderIncludeGen(PrepEngine &PP,
                                    const DependencyOutputOptions &DepOpts,
                                    bool ShowAllHeaders,
                                    llvm::StringRef OutputPath, bool ShowDepth,
                                    bool MSStyle) {
  llvm::raw_ostream *OutputFile = &llvm::errs();
  bool OwnsOutputFile = false;

  // Choose output stream, when printing in cl.exe /showIncludes style.
  if (MSStyle) {
    switch (DepOpts.ShowIncludesDest) {
    default:
      llvm_unreachable("Invalid destination for /showIncludes output!");
    case ShowIncludesDestination::Stderr:
      OutputFile = &llvm::errs();
      break;
    case ShowIncludesDestination::Stdout:
      OutputFile = &llvm::outs();
      break;
    }
  }

  // Open the output file, if used.
  if (!OutputPath.empty()) {
    std::error_code EC;
    llvm::raw_fd_ostream *OS = new llvm::raw_fd_ostream(
        OutputPath.str(), EC,
        llvm::sys::fs::OF_Append | llvm::sys::fs::OF_TextWithCRLF);
    if (EC) {
      PP.getDiagnostics().Report(
          neverc::diag::warn_fe_header_include_open_failure)
          << EC.message();
      delete OS;
    } else {
      OS->SetUnbuffered();
      OutputFile = OS;
      OwnsOutputFile = true;
    }
  }

  switch (DepOpts.HeaderIncludeFormat) {
  case HIFMT_None:
    llvm_unreachable("unexpected header format kind");
  case HIFMT_Textual: {
    assert(DepOpts.HeaderIncludeFiltering == HIFIL_None &&
           "header filtering is currently always disabled when output format is"
           "textual");
    // Print header info for extra headers, pretending they were discovered by
    // the regular preprocessor. The primary use case is to support proper
    // generation of Make / Ninja file dependencies for implicit includes, such
    // as sanitizer ignorelists. It's only important for cl.exe compatibility,
    // the GNU way to generate rules is -M / -MM / -MD / -MMD.
    for (const auto &Header : DepOpts.ExtraDeps)
      genHeaderInfo(OutputFile, Header.first, ShowDepth, 2, MSStyle);
    PP.addObserver(std::make_unique<HeaderIncludesCallback>(
        &PP, ShowAllHeaders, OutputFile, DepOpts, OwnsOutputFile, ShowDepth,
        MSStyle));
    break;
  }
  case HIFMT_JSON: {
    assert(DepOpts.HeaderIncludeFiltering == HIFIL_Only_Direct_System &&
           "only-direct-system is the only option for filtering");
    PP.addObserver(std::make_unique<HeaderIncludesJSONCallback>(
        &PP, OutputFile, OwnsOutputFile));
    break;
  }
  }
}

void HeaderIncludesCallback::FileChanged(SourceLocation Loc,
                                         FileChangeReason Reason,
                                         SrcMgr::CharacteristicKind NewFileType,
                                         FileID PrevFID) {
  // Unless we are exiting a #include, make sure to skip ahead to the line the
  // #include directive was at.
  PresumedLoc UserLoc = SM.getPresumedLoc(Loc);
  if (UserLoc.isInvalid())
    return;

  // Adjust the current include depth.
  if (Reason == PrepObserver::EnterFile) {
    ++CurrentIncludeDepth;
  } else if (Reason == PrepObserver::ExitFile) {
    if (CurrentIncludeDepth)
      --CurrentIncludeDepth;

    // We track when we are done with the predefines by watching for the first
    // place where we drop back to a nesting depth of 1.
    if (CurrentIncludeDepth == 1 && !HasProcessedPredefines)
      HasProcessedPredefines = true;

    return;
  } else {
    return;
  }

  if (!ShouldShowHeader(NewFileType))
    return;

  unsigned IncludeDepth = CurrentIncludeDepth;
  if (!HasProcessedPredefines)
    --IncludeDepth; // Ignore indent from <built-in>.

  if (Reason == PrepObserver::EnterFile &&
      UserLoc.getFilename() != llvm::StringRef("<command line>")) {
    genHeaderInfo(OutputFile, UserLoc.getFilename(), ShowDepth, IncludeDepth,
                  MSStyle);
  }
}

void HeaderIncludesCallback::FileSkipped(const FileEntryRef &SkippedFile,
                                         const Token &FilenameTok,
                                         SrcMgr::CharacteristicKind FileType) {
  if (!DepOpts.ShowSkippedHeaderIncludes)
    return;

  if (!ShouldShowHeader(FileType))
    return;

  genHeaderInfo(OutputFile, SkippedFile.getName(), ShowDepth,
                CurrentIncludeDepth + 1, MSStyle);
}

void HeaderIncludesJSONCallback::EndOfMainFile() {
  OptionalFileEntryRef FE = SM.getFileEntryRefForID(SM.getMainFileID());
  llvm::SmallString<256> MainFile(FE->getName());
  SM.getFileManager().makeAbsolutePath(MainFile);

  std::string Str;
  llvm::raw_string_ostream OS(Str);
  llvm::json::OStream JOS(OS);
  JOS.object([&] {
    JOS.attribute("source", MainFile.c_str());
    JOS.attributeArray("includes", [&] {
      llvm::StringSet<> SeenHeaders;
      for (const std::string &H : IncludedHeaders)
        if (SeenHeaders.insert(H).second)
          JOS.value(H);
    });
  });
  OS << "\n";

  if (OutputFile->get_kind() == llvm::raw_ostream::OStreamKind::OK_FDStream) {
    llvm::raw_fd_ostream *FDS = static_cast<llvm::raw_fd_ostream *>(OutputFile);
    if (auto L = FDS->lock())
      *OutputFile << Str;
  } else
    *OutputFile << Str;
}

namespace {
bool shouldRecordNewFile(SrcMgr::CharacteristicKind NewFileType,
                         SourceLocation PrevLoc, SourceManager &SM) {
  return SrcMgr::isSystem(NewFileType) && !SM.isInSystemHeader(PrevLoc);
}

} // namespace
void HeaderIncludesJSONCallback::FileChanged(
    SourceLocation Loc, FileChangeReason Reason,
    SrcMgr::CharacteristicKind NewFileType, FileID PrevFID) {
  if (PrevFID.isInvalid() ||
      !shouldRecordNewFile(NewFileType, SM.getLocForStartOfFile(PrevFID), SM))
    return;

  // Unless we are exiting a #include, make sure to skip ahead to the line the
  // #include directive was at.
  PresumedLoc UserLoc = SM.getPresumedLoc(Loc);
  if (UserLoc.isInvalid())
    return;

  if (Reason == PrepObserver::EnterFile &&
      UserLoc.getFilename() != llvm::StringRef("<command line>"))
    IncludedHeaders.push_back(UserLoc.getFilename());
}

void HeaderIncludesJSONCallback::FileSkipped(
    const FileEntryRef &SkippedFile, const Token &FilenameTok,
    SrcMgr::CharacteristicKind FileType) {
  if (!shouldRecordNewFile(FileType, FilenameTok.getLocation(), SM))
    return;

  IncludedHeaders.push_back(SkippedFile.getName().str());
}
