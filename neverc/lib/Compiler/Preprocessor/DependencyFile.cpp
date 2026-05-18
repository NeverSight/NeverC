#include "neverc/Compiler/DependencyOutputOptions.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Scan/PathEntry.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepObserver.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace neverc;

// ===----------------------------------------------------------------------===
// DepCollectorPrepObserver
// ===----------------------------------------------------------------------===

namespace {
struct DepCollectorPrepObserver : public PrepObserver {
  DependencyCollector &DepCollector;
  PrepEngine &PP;
  DepCollectorPrepObserver(DependencyCollector &L, PrepEngine &PP)
      : DepCollector(L), PP(PP) {}

  void LexedFileChanged(FileID FID, LexedFileChangeReason Reason,
                        SrcMgr::CharacteristicKind FileType, FileID PrevFID,
                        SourceLocation Loc) override {
    if (Reason != PrepObserver::LexedFileChangeReason::EnterFile)
      return;

    // Dependency generation really does want to go all the way to the
    // file entry for a source location to find out what is depended on.
    // We do not want #line markers to affect dependency generation!
    if (std::optional<llvm::StringRef> Filename =
            PP.getSourceManager().getNonBuiltinFilenameForID(FID))
      DepCollector.maybeAddDependency(
          llvm::sys::path::remove_leading_dotslash(*Filename),
          isSystem(FileType), /*IsMissing*/ false);
  }

  void FileSkipped(const FileEntryRef &SkippedFile, const Token &FilenameTok,
                   SrcMgr::CharacteristicKind FileType) override {
    llvm::StringRef Filename =
        llvm::sys::path::remove_leading_dotslash(SkippedFile.getName());
    DepCollector.maybeAddDependency(Filename, isSystem(FileType),
                                    /*IsMissing=*/false);
  }

  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          llvm::StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange,
                          OptionalFileEntryRef File, llvm::StringRef SearchPath,
                          llvm::StringRef RelativePath,
                          SrcMgr::CharacteristicKind FileType) override {
    if (!File)
      DepCollector.maybeAddDependency(FileName,
                                      /*IsSystem*/ false,
                                      /*IsMissing*/ true);
    // Files that actually exist are handled by FileChanged.
  }

  void HasInclude(SourceLocation Loc, llvm::StringRef SpelledFilename,
                  bool IsAngled, OptionalFileEntryRef File,
                  SrcMgr::CharacteristicKind FileType) override {
    if (!File)
      return;
    llvm::StringRef Filename =
        llvm::sys::path::remove_leading_dotslash(File->getName());
    DepCollector.maybeAddDependency(Filename, isSystem(FileType),
                                    /*IsMissing=*/false);
  }

  void EndOfMainFile() override {
    DepCollector.finishedMainFile(PP.getDiagnostics());
  }
};

} // end anonymous namespace

// ===----------------------------------------------------------------------===
// DependencyCollector
// ===----------------------------------------------------------------------===

void DependencyCollector::maybeAddDependency(llvm::StringRef Filename,
                                             bool IsSystem, bool IsMissing) {
  if (sawDependency(Filename, IsSystem, IsMissing))
    addDependency(Filename);
}

bool DependencyCollector::addDependency(llvm::StringRef Filename) {
  llvm::StringRef SearchPath;
#ifdef _WIN32
  // Make the search insensitive to case and separators.
  llvm::SmallString<256> TmpPath = Filename;
  llvm::sys::path::native(TmpPath);
  std::transform(TmpPath.begin(), TmpPath.end(), TmpPath.begin(), ::tolower);
  SearchPath = TmpPath.str();
#else
  SearchPath = Filename;
#endif

  if (Seen.insert(SearchPath).second) {
    Dependencies.push_back(std::string(Filename));
    return true;
  }
  return false;
}

namespace {
bool isSpecialFilename(llvm::StringRef Filename) {
  return Filename == "<built-in>";
}

} // namespace
bool DependencyCollector::sawDependency(llvm::StringRef Filename, bool IsSystem,
                                        bool IsMissing) {
  return !isSpecialFilename(Filename) &&
         (needSystemDependencies() || !IsSystem);
}

DependencyCollector::~DependencyCollector() = default;
void DependencyCollector::attachToPrepEngine(PrepEngine &PP) {
  PP.addObserver(std::make_unique<DepCollectorPrepObserver>(*this, PP));
}
DependencyFileGenerator::DependencyFileGenerator(
    const DependencyOutputOptions &Opts)
    : OutputFile(Opts.OutputFile), Targets(Opts.Targets),
      IncludeSystemHeaders(Opts.IncludeSystemHeaders),
      PhonyTarget(Opts.UsePhonyTargets),
      AddMissingHeaderDeps(Opts.AddMissingHeaderDeps), SeenMissingHeader(false),
      OutputFormat(Opts.OutputFormat), InputFileIndex(0) {
  for (const auto &ExtraDep : Opts.ExtraDeps) {
    if (addDependency(ExtraDep.first))
      ++InputFileIndex;
  }
}

// ===----------------------------------------------------------------------===
// DependencyFileGenerator
// ===----------------------------------------------------------------------===

void DependencyFileGenerator::attachToPrepEngine(PrepEngine &PP) {
  // Disable the "file not found" diagnostic if the -MG option was given.
  if (AddMissingHeaderDeps)
    PP.SetSuppressIncludeNotFoundError(true);

  DependencyCollector::attachToPrepEngine(PP);
}

bool DependencyFileGenerator::sawDependency(llvm::StringRef Filename,
                                            bool IsSystem, bool IsMissing) {
  if (IsMissing) {
    if (AddMissingHeaderDeps)
      return true;
    SeenMissingHeader = true;
    return false;
  }

  if (isSpecialFilename(Filename))
    return false;

  if (IncludeSystemHeaders)
    return true;

  return !IsSystem;
}

void DependencyFileGenerator::finishedMainFile(DiagnosticsEngine &Diags) {
  outputDependencyFile(Diags);
}

namespace {
void printFilename(llvm::raw_ostream &OS, llvm::StringRef Filename,
                   DependencyOutputFormat OutputFormat) {
  llvm::SmallString<256> NativePath;
  llvm::sys::path::native(Filename.str(), NativePath);

  if (OutputFormat == DependencyOutputFormat::NMake) {
    // Add quotes if needed. These are the characters listed as "special" to
    // NMake, that are legal in a Windows filespec, and that could cause
    // misinterpretation of the dependency string.
    if (NativePath.find_first_of(" #${}^!") != llvm::StringRef::npos)
      OS << '\"' << NativePath << '\"';
    else
      OS << NativePath;
    return;
  }
  assert(OutputFormat == DependencyOutputFormat::Make);
  for (unsigned i = 0, e = NativePath.size(); i != e; ++i) {
    if (NativePath[i] == '#') // Handle '#' the broken gcc way.
      OS << '\\';
    else if (NativePath[i] == ' ') { // Handle space correctly.
      OS << '\\';
      unsigned j = i;
      while (j > 0 && NativePath[--j] == '\\')
        OS << '\\';
    } else if (NativePath[i] == '$') // $ is escaped by $$.
      OS << '$';
    OS << NativePath[i];
  }
}

} // namespace
void DependencyFileGenerator::outputDependencyFile(DiagnosticsEngine &Diags) {
  if (SeenMissingHeader) {
    llvm::sys::fs::remove(OutputFile);
    return;
  }

  std::error_code EC;
  llvm::raw_fd_ostream OS(OutputFile, EC, llvm::sys::fs::OF_TextWithCRLF);
  if (EC) {
    Diags.Report(diag::err_fe_error_opening) << OutputFile << EC.message();
    return;
  }

  outputDependencyFile(OS);
}

void DependencyFileGenerator::outputDependencyFile(llvm::raw_ostream &OS) {
  // Write out the dependency targets, trying to avoid overly long
  // lines when possible. We try our best to emit exactly the same
  // dependency file as GCC>=10, assuming the included files are the
  // same.
  const unsigned MaxColumns = 75;
  unsigned Columns = 0;

  for (llvm::StringRef Target : Targets) {
    unsigned N = Target.size();
    if (Columns == 0) {
      Columns += N;
    } else if (Columns + N + 2 > MaxColumns) {
      Columns = N + 2;
      OS << " \\\n  ";
    } else {
      Columns += N + 1;
      OS << ' ';
    }
    // Targets already quoted as needed.
    OS << Target;
  }

  OS << ':';
  Columns += 1;

  // Now add each dependency in the order it was seen, but avoiding
  // duplicates.
  llvm::ArrayRef<std::string> Files = getDependencies();
  for (llvm::StringRef File : Files) {
    if (File == "<stdin>")
      continue;
    // Start a new line if this would exceed the column limit. Make
    // sure to leave space for a trailing " \" in case we need to
    // break the line on the next iteration.
    unsigned N = File.size();
    if (Columns + (N + 1) + 2 > MaxColumns) {
      OS << " \\\n ";
      Columns = 2;
    }
    OS << ' ';
    printFilename(OS, File, OutputFormat);
    Columns += N + 1;
  }
  OS << '\n';

  if (PhonyTarget && !Files.empty()) {
    unsigned Index = 0;
    for (auto I = Files.begin(), E = Files.end(); I != E; ++I) {
      if (Index++ == InputFileIndex)
        continue;
      printFilename(OS, *I, OutputFormat);
      OS << ":\n";
    }
  }
}
