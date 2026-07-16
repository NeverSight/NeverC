#ifndef NEVERC_LIB_PLUGIN_FRONTEND_PLUGINPREPOBSERVER_H
#define NEVERC_LIB_PLUGIN_FRONTEND_PLUGINPREPOBSERVER_H

#include "neverc/Plugin/PluginPrep.h"
#include "neverc/Scan/PrepObserver.h"
#include "llvm/Support/Error.h"

namespace neverc::plugin {

class PluginPrepBridge;

class PluginPrepObserver final : public PrepObserver {
public:
  explicit PluginPrepObserver(PluginPrepBridge &Bridge);

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override;
  void LexedFileChanged(FileID FID, LexedFileChangeReason Reason,
                        SrcMgr::CharacteristicKind FileType, FileID PrevFID,
                        SourceLocation Loc) override;
  void FileSkipped(const FileEntryRef &SkippedFile, const Token &FilenameTok,
                   SrcMgr::CharacteristicKind FileType) override;
  bool FileNotFound(llvm::StringRef FileName) override;
  void InclusionDirective(
      SourceLocation HashLoc, const Token &IncludeTok,
      llvm::StringRef FileName, bool IsAngled,
      CharSourceRange FilenameRange, OptionalFileEntryRef File,
      llvm::StringRef SearchPath, llvm::StringRef RelativePath,
      SrcMgr::CharacteristicKind FileType) override;
  void EndOfMainFile() override;
  void Ident(SourceLocation Loc, llvm::StringRef Str) override;
  void PragmaDirective(SourceLocation Loc,
                       PragmaIntroducerKind Introducer) override;
  void PragmaComment(SourceLocation Loc, const IdentifierInfo *Kind,
                     llvm::StringRef Str) override;
  void PragmaMark(SourceLocation Loc, llvm::StringRef Trivia) override;
  void PragmaDetectMismatch(SourceLocation Loc, llvm::StringRef Name,
                            llvm::StringRef Value) override;
  void PragmaDebug(SourceLocation Loc, llvm::StringRef DebugType) override;
  void PragmaMessage(SourceLocation Loc, llvm::StringRef Namespace,
                     PragmaMessageKind Kind, llvm::StringRef Str) override;
  void PragmaDiagnosticPush(SourceLocation Loc,
                            llvm::StringRef Namespace) override;
  void PragmaDiagnosticPop(SourceLocation Loc,
                           llvm::StringRef Namespace) override;
  void PragmaDiagnostic(SourceLocation Loc, llvm::StringRef Namespace,
                        diag::Severity Mapping,
                        llvm::StringRef Str) override;
  void PragmaWarning(SourceLocation Loc, PragmaWarningSpecifier WarningSpec,
                     llvm::ArrayRef<int> IDs) override;
  void PragmaWarningPush(SourceLocation Loc, int Level) override;
  void PragmaWarningPop(SourceLocation Loc) override;
  void PragmaExecCharsetPush(SourceLocation Loc,
                             llvm::StringRef Str) override;
  void PragmaExecCharsetPop(SourceLocation Loc) override;
  void PragmaAssumeNonNullBegin(SourceLocation Loc) override;
  void PragmaAssumeNonNullEnd(SourceLocation Loc) override;
  void MacroExpands(const Token &MacroNameTok, const MacroDefinition &MD,
                    SourceRange Range,
                    const MacroArgStorage *Args) override;
  void MacroDefined(const Token &MacroNameTok,
                    const MacroDirective *MD) override;
  void MacroUndefined(const Token &MacroNameTok, const MacroDefinition &MD,
                      const MacroDirective *Undef) override;
  void Defined(const Token &MacroNameTok, const MacroDefinition &MD,
               SourceRange Range) override;
  void HasInclude(SourceLocation Loc, llvm::StringRef FileName,
                  bool IsAngled, OptionalFileEntryRef File,
                  SrcMgr::CharacteristicKind FileType) override;
  void SourceRangeSkipped(SourceRange Range,
                          SourceLocation EndifLoc) override;
  void If(SourceLocation Loc, SourceRange ConditionRange,
          ConditionValueKind ConditionValue) override;
  void Elif(SourceLocation Loc, SourceRange ConditionRange,
            ConditionValueKind ConditionValue,
            SourceLocation IfLoc) override;
  void Ifdef(SourceLocation Loc, const Token &MacroNameTok,
             const MacroDefinition &MD) override;
  void Elifdef(SourceLocation Loc, const Token &MacroNameTok,
               const MacroDefinition &MD) override;
  void Elifdef(SourceLocation Loc, SourceRange ConditionRange,
               SourceLocation IfLoc) override;
  void Ifndef(SourceLocation Loc, const Token &MacroNameTok,
              const MacroDefinition &MD) override;
  void Elifndef(SourceLocation Loc, const Token &MacroNameTok,
                const MacroDefinition &MD) override;
  void Elifndef(SourceLocation Loc, SourceRange ConditionRange,
                SourceLocation IfLoc) override;
  void Else(SourceLocation Loc, SourceLocation IfLoc) override;
  void Endif(SourceLocation Loc, SourceLocation IfLoc) override;

private:
  NevercPrepEvent makeEvent(NevercPrepEventKind Kind) const;
  void emit(const NevercPrepEvent &Event, SourceLocation DiagnosticLoc = {});
  void mappingFailure(SourceLocation Loc, llvm::Error ErrorValue);
  bool fillLocation(SourceLocation Loc, NevercSourceLocation &Out);
  bool fillRange(SourceRange Range, NevercSourceRange &Out);
  bool fillFile(FileID File, NevercFileHandle &Out);
  bool fillToken(const Token &Value, NevercTokenHandle &Out);
  bool fillMacro(const Token &NameToken,
                 const MacroDefinition &Definition,
                 NevercPrepMacroEvent &Out,
                 SourceLocation UndefinitionLocation = {});
  bool fillCondition(SourceLocation Loc, SourceLocation IfLoc,
                     SourceRange Range, const Token *MacroName,
                     const MacroDefinition *Definition,
                     ConditionValueKind Value,
                     NevercPrepConditionEvent &Out);

  PluginPrepBridge &Bridge;
  bool Failed = false;
};

} // namespace neverc::plugin

#endif
