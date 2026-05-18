#ifndef NEVERC_LEX_PREPOBSERVER_H
#define NEVERC_LEX_PREPOBSERVER_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticIDs.h"
#include "neverc/Scan/PragmaDispatch.h"
#include "llvm/ADT/StringRef.h"

namespace neverc {
class Token;
class IdentifierInfo;
class MacroDefinition;
class MacroDirective;
class MacroArgStorage;

class PrepObserver {
public:
  virtual ~PrepObserver();

  enum FileChangeReason { EnterFile, ExitFile, SystemHeaderPragma, RenameFile };

  virtual void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                           SrcMgr::CharacteristicKind FileType,
                           FileID PrevFID = FileID()) {}

  enum class LexedFileChangeReason { EnterFile, ExitFile };

  virtual void LexedFileChanged(FileID FID, LexedFileChangeReason Reason,
                                SrcMgr::CharacteristicKind FileType,
                                FileID PrevFID, SourceLocation Loc) {}

  virtual void FileSkipped(const FileEntryRef &SkippedFile,
                           const Token &FilenameTok,
                           SrcMgr::CharacteristicKind FileType) {}

  virtual bool FileNotFound(llvm::StringRef FileName) { return false; }

  virtual void
  InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                     llvm::StringRef FileName, bool IsAngled,
                     CharSourceRange FilenameRange, OptionalFileEntryRef File,
                     llvm::StringRef SearchPath, llvm::StringRef RelativePath,
                     SrcMgr::CharacteristicKind FileType) {}

  virtual void EndOfMainFile() {}

  virtual void Ident(SourceLocation Loc, llvm::StringRef str) {}

  virtual void PragmaDirective(SourceLocation Loc,
                               PragmaIntroducerKind Introducer) {}

  virtual void PragmaComment(SourceLocation Loc, const IdentifierInfo *Kind,
                             llvm::StringRef Str) {}

  virtual void PragmaMark(SourceLocation Loc, llvm::StringRef Trivia) {}

  virtual void PragmaDetectMismatch(SourceLocation Loc, llvm::StringRef Name,
                                    llvm::StringRef Value) {}

  virtual void PragmaDebug(SourceLocation Loc, llvm::StringRef DebugType) {}

  enum PragmaMessageKind {
    /// \#pragma message has been invoked.
    PMK_Message,

    /// \#pragma GCC warning has been invoked.
    PMK_Warning,

    /// \#pragma GCC error has been invoked.
    PMK_Error
  };

  virtual void PragmaMessage(SourceLocation Loc, llvm::StringRef Namespace,
                             PragmaMessageKind Kind, llvm::StringRef Str) {}

  virtual void PragmaDiagnosticPush(SourceLocation Loc,
                                    llvm::StringRef Namespace) {}

  virtual void PragmaDiagnosticPop(SourceLocation Loc,
                                   llvm::StringRef Namespace) {}

  virtual void PragmaDiagnostic(SourceLocation Loc, llvm::StringRef Namespace,
                                diag::Severity mapping, llvm::StringRef Str) {}

  enum PragmaWarningSpecifier {
    PWS_Default,
    PWS_Disable,
    PWS_Error,
    PWS_Once,
    PWS_Suppress,
    PWS_Level1,
    PWS_Level2,
    PWS_Level3,
    PWS_Level4,
  };
  virtual void PragmaWarning(SourceLocation Loc,
                             PragmaWarningSpecifier WarningSpec,
                             llvm::ArrayRef<int> Ids) {}

  virtual void PragmaWarningPush(SourceLocation Loc, int Level) {}

  virtual void PragmaWarningPop(SourceLocation Loc) {}

  virtual void PragmaExecCharsetPush(SourceLocation Loc, llvm::StringRef Str) {}

  virtual void PragmaExecCharsetPop(SourceLocation Loc) {}

  virtual void PragmaAssumeNonNullBegin(SourceLocation Loc) {}

  virtual void PragmaAssumeNonNullEnd(SourceLocation Loc) {}

  virtual void MacroExpands(const Token &MacroNameTok,
                            const MacroDefinition &MD, SourceRange Range,
                            const MacroArgStorage *Args) {}

  virtual void MacroDefined(const Token &MacroNameTok,
                            const MacroDirective *MD) {}

  virtual void MacroUndefined(const Token &MacroNameTok,
                              const MacroDefinition &MD,
                              const MacroDirective *Undef) {}

  virtual void Defined(const Token &MacroNameTok, const MacroDefinition &MD,
                       SourceRange Range) {}

  virtual void HasInclude(SourceLocation Loc, llvm::StringRef FileName,
                          bool IsAngled, OptionalFileEntryRef File,
                          SrcMgr::CharacteristicKind FileType);

  virtual void SourceRangeSkipped(SourceRange Range, SourceLocation EndifLoc) {}

  enum ConditionValueKind { CVK_NotEvaluated, CVK_False, CVK_True };

  virtual void If(SourceLocation Loc, SourceRange ConditionRange,
                  ConditionValueKind ConditionValue) {}

  virtual void Elif(SourceLocation Loc, SourceRange ConditionRange,
                    ConditionValueKind ConditionValue, SourceLocation IfLoc) {}

  virtual void Ifdef(SourceLocation Loc, const Token &MacroNameTok,
                     const MacroDefinition &MD) {}

  virtual void Elifdef(SourceLocation Loc, const Token &MacroNameTok,
                       const MacroDefinition &MD) {}
  virtual void Elifdef(SourceLocation Loc, SourceRange ConditionRange,
                       SourceLocation IfLoc) {}

  virtual void Ifndef(SourceLocation Loc, const Token &MacroNameTok,
                      const MacroDefinition &MD) {}

  virtual void Elifndef(SourceLocation Loc, const Token &MacroNameTok,
                        const MacroDefinition &MD) {}
  virtual void Elifndef(SourceLocation Loc, SourceRange ConditionRange,
                        SourceLocation IfLoc) {}

  virtual void Else(SourceLocation Loc, SourceLocation IfLoc) {}

  virtual void Endif(SourceLocation Loc, SourceLocation IfLoc) {}
};

class ChainedPrepObserver : public PrepObserver {
  std::unique_ptr<PrepObserver> First, Second;

public:
  ChainedPrepObserver(std::unique_ptr<PrepObserver> _First,
                      std::unique_ptr<PrepObserver> _Second)
      : First(std::move(_First)), Second(std::move(_Second)) {}

  ~ChainedPrepObserver() override;

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override {
    First->FileChanged(Loc, Reason, FileType, PrevFID);
    Second->FileChanged(Loc, Reason, FileType, PrevFID);
  }

  void LexedFileChanged(FileID FID, LexedFileChangeReason Reason,
                        SrcMgr::CharacteristicKind FileType, FileID PrevFID,
                        SourceLocation Loc) override {
    First->LexedFileChanged(FID, Reason, FileType, PrevFID, Loc);
    Second->LexedFileChanged(FID, Reason, FileType, PrevFID, Loc);
  }

  void FileSkipped(const FileEntryRef &SkippedFile, const Token &FilenameTok,
                   SrcMgr::CharacteristicKind FileType) override {
    First->FileSkipped(SkippedFile, FilenameTok, FileType);
    Second->FileSkipped(SkippedFile, FilenameTok, FileType);
  }

  bool FileNotFound(llvm::StringRef FileName) override {
    bool Skip = First->FileNotFound(FileName);
    // Make sure to invoke the second callback, no matter if the first already
    // returned true to skip the file.
    Skip |= Second->FileNotFound(FileName);
    return Skip;
  }

  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          llvm::StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange,
                          OptionalFileEntryRef File, llvm::StringRef SearchPath,
                          llvm::StringRef RelativePath,
                          SrcMgr::CharacteristicKind FileType) override {
    First->InclusionDirective(HashLoc, IncludeTok, FileName, IsAngled,
                              FilenameRange, File, SearchPath, RelativePath,
                              FileType);
    Second->InclusionDirective(HashLoc, IncludeTok, FileName, IsAngled,
                               FilenameRange, File, SearchPath, RelativePath,
                               FileType);
  }

  void EndOfMainFile() override {
    First->EndOfMainFile();
    Second->EndOfMainFile();
  }

  void Ident(SourceLocation Loc, llvm::StringRef str) override {
    First->Ident(Loc, str);
    Second->Ident(Loc, str);
  }

  void PragmaDirective(SourceLocation Loc,
                       PragmaIntroducerKind Introducer) override {
    First->PragmaDirective(Loc, Introducer);
    Second->PragmaDirective(Loc, Introducer);
  }

  void PragmaComment(SourceLocation Loc, const IdentifierInfo *Kind,
                     llvm::StringRef Str) override {
    First->PragmaComment(Loc, Kind, Str);
    Second->PragmaComment(Loc, Kind, Str);
  }

  void PragmaMark(SourceLocation Loc, llvm::StringRef Trivia) override {
    First->PragmaMark(Loc, Trivia);
    Second->PragmaMark(Loc, Trivia);
  }

  void PragmaDetectMismatch(SourceLocation Loc, llvm::StringRef Name,
                            llvm::StringRef Value) override {
    First->PragmaDetectMismatch(Loc, Name, Value);
    Second->PragmaDetectMismatch(Loc, Name, Value);
  }

  void PragmaDebug(SourceLocation Loc, llvm::StringRef DebugType) override {
    First->PragmaDebug(Loc, DebugType);
    Second->PragmaDebug(Loc, DebugType);
  }

  void PragmaMessage(SourceLocation Loc, llvm::StringRef Namespace,
                     PragmaMessageKind Kind, llvm::StringRef Str) override {
    First->PragmaMessage(Loc, Namespace, Kind, Str);
    Second->PragmaMessage(Loc, Namespace, Kind, Str);
  }

  void PragmaDiagnosticPush(SourceLocation Loc,
                            llvm::StringRef Namespace) override {
    First->PragmaDiagnosticPush(Loc, Namespace);
    Second->PragmaDiagnosticPush(Loc, Namespace);
  }

  void PragmaDiagnosticPop(SourceLocation Loc,
                           llvm::StringRef Namespace) override {
    First->PragmaDiagnosticPop(Loc, Namespace);
    Second->PragmaDiagnosticPop(Loc, Namespace);
  }

  void PragmaDiagnostic(SourceLocation Loc, llvm::StringRef Namespace,
                        diag::Severity mapping, llvm::StringRef Str) override {
    First->PragmaDiagnostic(Loc, Namespace, mapping, Str);
    Second->PragmaDiagnostic(Loc, Namespace, mapping, Str);
  }

  void HasInclude(SourceLocation Loc, llvm::StringRef FileName, bool IsAngled,
                  OptionalFileEntryRef File,
                  SrcMgr::CharacteristicKind FileType) override;

  void PragmaWarning(SourceLocation Loc, PragmaWarningSpecifier WarningSpec,
                     llvm::ArrayRef<int> Ids) override {
    First->PragmaWarning(Loc, WarningSpec, Ids);
    Second->PragmaWarning(Loc, WarningSpec, Ids);
  }

  void PragmaWarningPush(SourceLocation Loc, int Level) override {
    First->PragmaWarningPush(Loc, Level);
    Second->PragmaWarningPush(Loc, Level);
  }

  void PragmaWarningPop(SourceLocation Loc) override {
    First->PragmaWarningPop(Loc);
    Second->PragmaWarningPop(Loc);
  }

  void PragmaExecCharsetPush(SourceLocation Loc, llvm::StringRef Str) override {
    First->PragmaExecCharsetPush(Loc, Str);
    Second->PragmaExecCharsetPush(Loc, Str);
  }

  void PragmaExecCharsetPop(SourceLocation Loc) override {
    First->PragmaExecCharsetPop(Loc);
    Second->PragmaExecCharsetPop(Loc);
  }

  void PragmaAssumeNonNullBegin(SourceLocation Loc) override {
    First->PragmaAssumeNonNullBegin(Loc);
    Second->PragmaAssumeNonNullBegin(Loc);
  }

  void PragmaAssumeNonNullEnd(SourceLocation Loc) override {
    First->PragmaAssumeNonNullEnd(Loc);
    Second->PragmaAssumeNonNullEnd(Loc);
  }

  void MacroExpands(const Token &MacroNameTok, const MacroDefinition &MD,
                    SourceRange Range, const MacroArgStorage *Args) override {
    First->MacroExpands(MacroNameTok, MD, Range, Args);
    Second->MacroExpands(MacroNameTok, MD, Range, Args);
  }

  void MacroDefined(const Token &MacroNameTok,
                    const MacroDirective *MD) override {
    First->MacroDefined(MacroNameTok, MD);
    Second->MacroDefined(MacroNameTok, MD);
  }

  void MacroUndefined(const Token &MacroNameTok, const MacroDefinition &MD,
                      const MacroDirective *Undef) override {
    First->MacroUndefined(MacroNameTok, MD, Undef);
    Second->MacroUndefined(MacroNameTok, MD, Undef);
  }

  void Defined(const Token &MacroNameTok, const MacroDefinition &MD,
               SourceRange Range) override {
    First->Defined(MacroNameTok, MD, Range);
    Second->Defined(MacroNameTok, MD, Range);
  }

  void SourceRangeSkipped(SourceRange Range, SourceLocation EndifLoc) override {
    First->SourceRangeSkipped(Range, EndifLoc);
    Second->SourceRangeSkipped(Range, EndifLoc);
  }

  void If(SourceLocation Loc, SourceRange ConditionRange,
          ConditionValueKind ConditionValue) override {
    First->If(Loc, ConditionRange, ConditionValue);
    Second->If(Loc, ConditionRange, ConditionValue);
  }

  void Elif(SourceLocation Loc, SourceRange ConditionRange,
            ConditionValueKind ConditionValue, SourceLocation IfLoc) override {
    First->Elif(Loc, ConditionRange, ConditionValue, IfLoc);
    Second->Elif(Loc, ConditionRange, ConditionValue, IfLoc);
  }

  void Ifdef(SourceLocation Loc, const Token &MacroNameTok,
             const MacroDefinition &MD) override {
    First->Ifdef(Loc, MacroNameTok, MD);
    Second->Ifdef(Loc, MacroNameTok, MD);
  }

  void Elifdef(SourceLocation Loc, const Token &MacroNameTok,
               const MacroDefinition &MD) override {
    First->Elifdef(Loc, MacroNameTok, MD);
    Second->Elifdef(Loc, MacroNameTok, MD);
  }
  void Elifdef(SourceLocation Loc, SourceRange ConditionRange,
               SourceLocation IfLoc) override {
    First->Elifdef(Loc, ConditionRange, IfLoc);
    Second->Elifdef(Loc, ConditionRange, IfLoc);
  }

  void Ifndef(SourceLocation Loc, const Token &MacroNameTok,
              const MacroDefinition &MD) override {
    First->Ifndef(Loc, MacroNameTok, MD);
    Second->Ifndef(Loc, MacroNameTok, MD);
  }

  void Elifndef(SourceLocation Loc, const Token &MacroNameTok,
                const MacroDefinition &MD) override {
    First->Elifndef(Loc, MacroNameTok, MD);
    Second->Elifndef(Loc, MacroNameTok, MD);
  }
  void Elifndef(SourceLocation Loc, SourceRange ConditionRange,
                SourceLocation IfLoc) override {
    First->Elifndef(Loc, ConditionRange, IfLoc);
    Second->Elifndef(Loc, ConditionRange, IfLoc);
  }

  void Else(SourceLocation Loc, SourceLocation IfLoc) override {
    First->Else(Loc, IfLoc);
    Second->Else(Loc, IfLoc);
  }

  void Endif(SourceLocation Loc, SourceLocation IfLoc) override {
    First->Endif(Loc, IfLoc);
    Second->Endif(Loc, IfLoc);
  }
};

} // end namespace neverc

#endif
