#include "PluginPrepObserver.h"
#include "PrepBridgeInternal.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/DiagnosticDriver.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/MacroArgStorage.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/Support/Error.h"
#include <string>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercFileCharacteristic
fileCharacteristic(SrcMgr::CharacteristicKind Characteristic) {
  switch (Characteristic) {
  case SrcMgr::C_User:
    return NEVERC_FILE_CHARACTERISTIC_USER;
  case SrcMgr::C_System:
    return NEVERC_FILE_CHARACTERISTIC_SYSTEM;
  case SrcMgr::C_ExternCSystem:
    return NEVERC_FILE_CHARACTERISTIC_EXTERN_C_SYSTEM;
  }
  return NEVERC_FILE_CHARACTERISTIC_USER;
}

NevercPrepFileChangeReason
fileChangeReason(PrepObserver::FileChangeReason Reason) {
  switch (Reason) {
  case PrepObserver::EnterFile:
    return NEVERC_PREP_FILE_ENTER;
  case PrepObserver::ExitFile:
    return NEVERC_PREP_FILE_EXIT;
  case PrepObserver::SystemHeaderPragma:
    return NEVERC_PREP_FILE_SYSTEM_HEADER_PRAGMA;
  case PrepObserver::RenameFile:
    return NEVERC_PREP_FILE_RENAME;
  }
  return NEVERC_PREP_FILE_ENTER;
}

NevercPrepConditionValue
conditionValue(PrepObserver::ConditionValueKind Value) {
  switch (Value) {
  case PrepObserver::CVK_NotEvaluated:
    return NEVERC_PREP_CONDITION_NOT_EVALUATED;
  case PrepObserver::CVK_False:
    return NEVERC_PREP_CONDITION_FALSE;
  case PrepObserver::CVK_True:
    return NEVERC_PREP_CONDITION_TRUE;
  }
  return NEVERC_PREP_CONDITION_NOT_EVALUATED;
}

NevercPrepPragmaIntroducer
pragmaIntroducer(PragmaIntroducerKind Introducer) {
  switch (Introducer) {
  case PIK_HashPragma:
    return NEVERC_PREP_PRAGMA_HASH;
  case PIK__Pragma:
    return NEVERC_PREP_PRAGMA_OPERATOR;
  case PIK___pragma:
    return NEVERC_PREP_PRAGMA_MS;
  }
  return NEVERC_PREP_PRAGMA_HASH;
}

NevercPrepPragmaMessageKind
pragmaMessageKind(PrepObserver::PragmaMessageKind Kind) {
  switch (Kind) {
  case PrepObserver::PMK_Message:
    return NEVERC_PREP_PRAGMA_MESSAGE_NOTE;
  case PrepObserver::PMK_Warning:
    return NEVERC_PREP_PRAGMA_MESSAGE_WARNING;
  case PrepObserver::PMK_Error:
    return NEVERC_PREP_PRAGMA_MESSAGE_ERROR;
  }
  return NEVERC_PREP_PRAGMA_MESSAGE_NOTE;
}

NevercPrepWarningSpecifier
warningSpecifier(PrepObserver::PragmaWarningSpecifier Specifier) {
  switch (Specifier) {
  case PrepObserver::PWS_Default:
    return NEVERC_PREP_WARNING_DEFAULT;
  case PrepObserver::PWS_Disable:
    return NEVERC_PREP_WARNING_DISABLE;
  case PrepObserver::PWS_Error:
    return NEVERC_PREP_WARNING_ERROR;
  case PrepObserver::PWS_Once:
    return NEVERC_PREP_WARNING_ONCE;
  case PrepObserver::PWS_Suppress:
    return NEVERC_PREP_WARNING_SUPPRESS;
  case PrepObserver::PWS_Level1:
    return NEVERC_PREP_WARNING_LEVEL1;
  case PrepObserver::PWS_Level2:
    return NEVERC_PREP_WARNING_LEVEL2;
  case PrepObserver::PWS_Level3:
    return NEVERC_PREP_WARNING_LEVEL3;
  case PrepObserver::PWS_Level4:
    return NEVERC_PREP_WARNING_LEVEL4;
  }
  return NEVERC_PREP_WARNING_DEFAULT;
}

} // namespace

PluginPrepObserver::PluginPrepObserver(PluginPrepBridge &BridgeValue)
    : Bridge(BridgeValue) {}

NevercPrepEvent
PluginPrepObserver::makeEvent(NevercPrepEventKind Kind) const {
  NevercPrepEvent Event{};
  Event.Header = {sizeof(Event), NEVERC_PREP_API_MAJOR,
                  NEVERC_PREP_API_MINOR, 0};
  Event.Kind = Kind;
  return Event;
}

void PluginPrepObserver::mappingFailure(SourceLocation Loc,
                                        Error ErrorValue) {
  if (Failed) {
    consumeError(std::move(ErrorValue));
    return;
  }
  Failed = true;
  std::string Message = toString(std::move(ErrorValue)).str().str();
  Bridge.Prep.getDiagnostics().Report(Loc, diag::err_drv_plugin_phase)
      << Message;
}

void PluginPrepObserver::emit(const NevercPrepEvent &Event,
                              SourceLocation DiagnosticLoc) {
  if (Failed)
    return;
  NevercStatus Status = Bridge.dispatchEvent(Event);
  if (Status.Code == NEVERC_STATUS_OK)
    return;
  Failed = true;
  std::string Message =
      "plugin prep observer failed with status " +
      std::to_string(Status.Code);
  Bridge.Prep.getDiagnostics().Report(DiagnosticLoc,
                                      diag::err_drv_plugin_phase)
      << Message;
}

bool PluginPrepObserver::fillLocation(SourceLocation Loc,
                                      NevercSourceLocation &Out) {
  Out = {};
  if (Loc.isInvalid())
    return true;
  auto Public = Bridge.Locations.createLocation(Loc);
  if (!Public) {
    mappingFailure(Loc, Public.takeError());
    return false;
  }
  Out = *Public;
  return true;
}

bool PluginPrepObserver::fillRange(SourceRange Range,
                                   NevercSourceRange &Out) {
  Out = {};
  if (Range.isInvalid())
    return true;
  auto Public = Bridge.Locations.createRange(
      CharSourceRange::getTokenRange(Range));
  if (!Public) {
    mappingFailure(Range.getBegin(), Public.takeError());
    return false;
  }
  Out = *Public;
  return true;
}

bool PluginPrepObserver::fillFile(FileID File, NevercFileHandle &Out) {
  Out = {};
  if (File.isInvalid() ||
      !Bridge.Prep.getSourceManager().getFileEntryRefForID(File))
    return true;
  auto Public = Bridge.Locations.createFile(File);
  if (!Public) {
    mappingFailure({}, Public.takeError());
    return false;
  }
  Out = *Public;
  return true;
}

bool PluginPrepObserver::fillToken(const Token &Value,
                                   NevercTokenHandle &Out) {
  Out = {};
  auto Public = Bridge.createToken(Value);
  if (!Public) {
    mappingFailure(Value.getLocation(), Public.takeError());
    return false;
  }
  Out = *Public;
  return true;
}

bool PluginPrepObserver::fillMacro(
    const Token &NameToken, const MacroDefinition &Definition,
    NevercPrepMacroEvent &Out, SourceLocation UndefinitionLocation) {
  if (!fillToken(NameToken, Out.NameToken))
    return false;
  IdentifierInfo *Name = NameToken.getIdentifierInfo();
  if (!Name || !Definition.getLocalDirective())
    return true;
  auto Public = Bridge.createMacroDefinition(
      Name, Definition.getLocalDirective(), UndefinitionLocation);
  if (!Public) {
    mappingFailure(NameToken.getLocation(), Public.takeError());
    return false;
  }
  Out.Definition = *Public;
  return true;
}

bool PluginPrepObserver::fillCondition(
    SourceLocation Loc, SourceLocation IfLoc, SourceRange Range,
    const Token *MacroName, const MacroDefinition *Definition,
    ConditionValueKind Value, NevercPrepConditionEvent &Out) {
  if (!fillLocation(Loc, Out.Location) ||
      !fillLocation(IfLoc, Out.IfLocation) ||
      !fillRange(Range, Out.Range))
    return false;
  Out.Value = conditionValue(Value);
  if (!MacroName)
    return true;
  if (!fillToken(*MacroName, Out.MacroNameToken))
    return false;
  if (!Definition || !Definition->getLocalDirective())
    return true;
  IdentifierInfo *Name = MacroName->getIdentifierInfo();
  if (!Name)
    return true;
  auto Public =
      Bridge.createMacroDefinition(Name, Definition->getLocalDirective());
  if (!Public) {
    mappingFailure(Loc, Public.takeError());
    return false;
  }
  Out.Definition = *Public;
  return true;
}

void PluginPrepObserver::FileChanged(
    SourceLocation Loc, FileChangeReason Reason,
    SrcMgr::CharacteristicKind FileType, FileID PrevFID) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_FILE_CHANGED);
  Event.Payload.File.Reason = fileChangeReason(Reason);
  Event.Payload.File.Characteristic = fileCharacteristic(FileType);
  FileID Current;
  if (Loc.isValid())
    Current = Bridge.Prep.getSourceManager().getFileID(Loc);
  if (!fillLocation(Loc, Event.Payload.File.Location) ||
      !fillFile(Current, Event.Payload.File.File) ||
      !fillFile(PrevFID, Event.Payload.File.PreviousFile))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::LexedFileChanged(
    FileID FID, LexedFileChangeReason Reason,
    SrcMgr::CharacteristicKind FileType, FileID PrevFID,
    SourceLocation Loc) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_LEXED_FILE_CHANGED);
  Event.Payload.File.Reason =
      Reason == LexedFileChangeReason::EnterFile
          ? NEVERC_PREP_FILE_ENTER
          : NEVERC_PREP_FILE_EXIT;
  Event.Payload.File.Characteristic = fileCharacteristic(FileType);
  if (!fillLocation(Loc, Event.Payload.File.Location) ||
      !fillFile(FID, Event.Payload.File.File) ||
      !fillFile(PrevFID, Event.Payload.File.PreviousFile))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::FileSkipped(
    const FileEntryRef &SkippedFile, const Token &FilenameTok,
    SrcMgr::CharacteristicKind FileType) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_FILE_SKIPPED);
  Event.Payload.File.Characteristic = fileCharacteristic(FileType);
  FileID File =
      Bridge.Prep.getSourceManager().translateFile(SkippedFile);
  if (!fillFile(File, Event.Payload.File.File) ||
      !fillToken(FilenameTok, Event.Payload.File.FilenameToken))
    return;
  emit(Event, FilenameTok.getLocation());
}

bool PluginPrepObserver::FileNotFound(StringRef FileName) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_FILE_NOT_FOUND);
  Event.Payload.Include.Filename =
      prep_bridge_detail::stringView(FileName);
  emit(Event);
  return false;
}

void PluginPrepObserver::InclusionDirective(
    SourceLocation HashLoc, const Token &IncludeTok, StringRef FileName,
    bool IsAngled, CharSourceRange FilenameRange,
    OptionalFileEntryRef File, StringRef SearchPath,
    StringRef RelativePath, SrcMgr::CharacteristicKind FileType) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_INCLUSION_DIRECTIVE);
  auto &Include = Event.Payload.Include;
  Include.Filename = prep_bridge_detail::stringView(FileName);
  Include.SearchPath = prep_bridge_detail::stringView(SearchPath);
  Include.RelativePath =
      prep_bridge_detail::stringView(RelativePath);
  Include.IsAngled = IsAngled ? NEVERC_TRUE : NEVERC_FALSE;
  Include.Characteristic = fileCharacteristic(FileType);
  if (!fillLocation(HashLoc, Include.Location) ||
      !fillToken(IncludeTok, Include.IncludeToken))
    return;
  auto PublicRange = Bridge.Locations.createRange(FilenameRange);
  if (!PublicRange) {
    mappingFailure(HashLoc, PublicRange.takeError());
    return;
  }
  Include.FilenameRange = *PublicRange;
  if (File) {
    FileID FID = Bridge.Prep.getSourceManager().translateFile(*File);
    if (!fillFile(FID, Include.File))
      return;
  }
  emit(Event, HashLoc);
}

void PluginPrepObserver::EndOfMainFile() {
  emit(makeEvent(NEVERC_PREP_EVENT_END_OF_MAIN_FILE));
}

void PluginPrepObserver::Ident(SourceLocation Loc, StringRef Str) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_IDENT);
  Event.Payload.Text.Name = prep_bridge_detail::stringView(Str);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaDirective(
    SourceLocation Loc, PragmaIntroducerKind Introducer) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_DIRECTIVE);
  Event.Payload.Text.Detail = pragmaIntroducer(Introducer);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaComment(
    SourceLocation Loc, const IdentifierInfo *Kind, StringRef Str) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_COMMENT);
  Event.Payload.Text.Value = prep_bridge_detail::stringView(Str);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  if (Kind) {
    auto Public =
        Bridge.createIdentifier(const_cast<IdentifierInfo *>(Kind));
    if (!Public) {
      mappingFailure(Loc, Public.takeError());
      return;
    }
    Event.Payload.Text.Identifier = *Public;
    Event.Payload.Text.Name =
        prep_bridge_detail::stringView(Kind->getName());
  }
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaMark(SourceLocation Loc,
                                    StringRef Trivia) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_PRAGMA_MARK);
  Event.Payload.Text.Value = prep_bridge_detail::stringView(Trivia);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaDetectMismatch(
    SourceLocation Loc, StringRef Name, StringRef Value) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_DETECT_MISMATCH);
  Event.Payload.Text.Name = prep_bridge_detail::stringView(Name);
  Event.Payload.Text.Value = prep_bridge_detail::stringView(Value);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaDebug(SourceLocation Loc,
                                     StringRef DebugType) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_PRAGMA_DEBUG);
  Event.Payload.Text.Name =
      prep_bridge_detail::stringView(DebugType);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaMessage(
    SourceLocation Loc, StringRef Namespace, PragmaMessageKind Kind,
    StringRef Str) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_MESSAGE);
  Event.Payload.Text.Name =
      prep_bridge_detail::stringView(Namespace);
  Event.Payload.Text.Value = prep_bridge_detail::stringView(Str);
  Event.Payload.Text.Detail = pragmaMessageKind(Kind);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaDiagnosticPush(
    SourceLocation Loc, StringRef Namespace) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_DIAGNOSTIC_PUSH);
  Event.Payload.Text.Name =
      prep_bridge_detail::stringView(Namespace);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaDiagnosticPop(
    SourceLocation Loc, StringRef Namespace) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_DIAGNOSTIC_POP);
  Event.Payload.Text.Name =
      prep_bridge_detail::stringView(Namespace);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaDiagnostic(
    SourceLocation Loc, StringRef Namespace, diag::Severity Mapping,
    StringRef Str) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_DIAGNOSTIC);
  Event.Payload.Text.Name =
      prep_bridge_detail::stringView(Namespace);
  Event.Payload.Text.Value = prep_bridge_detail::stringView(Str);
  Event.Payload.Text.Detail = static_cast<uint32_t>(Mapping);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaWarning(
    SourceLocation Loc, PragmaWarningSpecifier WarningSpec,
    ArrayRef<int> IDs) {
  static_assert(sizeof(int) == sizeof(int32_t));
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_WARNING);
  Event.Payload.Text.Detail = warningSpecifier(WarningSpec);
  Event.Payload.Text.Integers =
      reinterpret_cast<const int32_t *>(IDs.data());
  Event.Payload.Text.IntegerCount = IDs.size();
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaWarningPush(SourceLocation Loc,
                                           int Level) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_WARNING_PUSH);
  Event.Payload.Text.IntegerValue = Level;
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaWarningPop(SourceLocation Loc) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_WARNING_POP);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaExecCharsetPush(SourceLocation Loc,
                                                StringRef Str) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_EXEC_CHARSET_PUSH);
  Event.Payload.Text.Value = prep_bridge_detail::stringView(Str);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaExecCharsetPop(SourceLocation Loc) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_EXEC_CHARSET_POP);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaAssumeNonNullBegin(SourceLocation Loc) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_ASSUME_NONNULL_BEGIN);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::PragmaAssumeNonNullEnd(SourceLocation Loc) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_PRAGMA_ASSUME_NONNULL_END);
  if (!fillLocation(Loc, Event.Payload.Text.Location))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::MacroExpands(
    const Token &MacroNameTok, const MacroDefinition &MD,
    SourceRange Range, const MacroArgStorage *Args) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_MACRO_EXPANDS);
  if (!fillMacro(MacroNameTok, MD, Event.Payload.Macro) ||
      !fillRange(Range, Event.Payload.Macro.Range))
    return;
  if (Args) {
    auto Public = Bridge.createMacroArguments(Args);
    if (!Public) {
      mappingFailure(MacroNameTok.getLocation(), Public.takeError());
      return;
    }
    Event.Payload.Macro.Arguments = *Public;
  }
  emit(Event, MacroNameTok.getLocation());
}

void PluginPrepObserver::MacroDefined(
    const Token &MacroNameTok, const MacroDirective *MD) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_MACRO_DEFINED);
  if (!fillToken(MacroNameTok, Event.Payload.Macro.NameToken))
    return;
  IdentifierInfo *Name = MacroNameTok.getIdentifierInfo();
  auto *Definition = dyn_cast_or_null<DefMacroDirective>(
      const_cast<MacroDirective *>(MD));
  if (Name && Definition) {
    auto PublicDefinition =
        Bridge.createMacroDefinition(Name, Definition);
    if (!PublicDefinition) {
      mappingFailure(MacroNameTok.getLocation(),
                     PublicDefinition.takeError());
      return;
    }
    Event.Payload.Macro.Definition = *PublicDefinition;
  }
  if (Name && MD) {
    auto PublicDirective = Bridge.createMacroDirective(
        Name, const_cast<MacroDirective *>(MD));
    if (!PublicDirective) {
      mappingFailure(MacroNameTok.getLocation(),
                     PublicDirective.takeError());
      return;
    }
    Event.Payload.Macro.Directive = *PublicDirective;
  }
  emit(Event, MacroNameTok.getLocation());
}

void PluginPrepObserver::MacroUndefined(
    const Token &MacroNameTok, const MacroDefinition &MD,
    const MacroDirective *Undef) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_MACRO_UNDEFINED);
  SourceLocation UndefLoc =
      Undef ? Undef->getLocation() : SourceLocation();
  if (!fillMacro(MacroNameTok, MD, Event.Payload.Macro, UndefLoc))
    return;
  IdentifierInfo *Name = MacroNameTok.getIdentifierInfo();
  if (Name && Undef) {
    auto Public = Bridge.createMacroDirective(
        Name, const_cast<MacroDirective *>(Undef));
    if (!Public) {
      mappingFailure(MacroNameTok.getLocation(), Public.takeError());
      return;
    }
    Event.Payload.Macro.Directive = *Public;
  }
  emit(Event, MacroNameTok.getLocation());
}

void PluginPrepObserver::Defined(const Token &MacroNameTok,
                                 const MacroDefinition &MD,
                                 SourceRange Range) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_DEFINED);
  if (!fillMacro(MacroNameTok, MD, Event.Payload.Macro) ||
      !fillRange(Range, Event.Payload.Macro.Range))
    return;
  emit(Event, MacroNameTok.getLocation());
}

void PluginPrepObserver::HasInclude(
    SourceLocation Loc, StringRef FileName, bool IsAngled,
    OptionalFileEntryRef File, SrcMgr::CharacteristicKind FileType) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_HAS_INCLUDE);
  auto &Include = Event.Payload.Include;
  Include.Filename = prep_bridge_detail::stringView(FileName);
  Include.IsAngled = IsAngled ? NEVERC_TRUE : NEVERC_FALSE;
  Include.Characteristic = fileCharacteristic(FileType);
  if (!fillLocation(Loc, Include.Location))
    return;
  if (File) {
    FileID FID = Bridge.Prep.getSourceManager().translateFile(*File);
    if (!fillFile(FID, Include.File))
      return;
  }
  emit(Event, Loc);
}

void PluginPrepObserver::SourceRangeSkipped(
    SourceRange Range, SourceLocation EndifLoc) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_SOURCE_RANGE_SKIPPED);
  if (!fillRange(Range, Event.Payload.Condition.Range) ||
      !fillLocation(EndifLoc, Event.Payload.Condition.IfLocation))
    return;
  emit(Event, Range.getBegin());
}

void PluginPrepObserver::If(SourceLocation Loc,
                            SourceRange ConditionRange,
                            ConditionValueKind ConditionValue) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_IF);
  if (!fillCondition(Loc, {}, ConditionRange, nullptr, nullptr,
                     ConditionValue, Event.Payload.Condition))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::Elif(SourceLocation Loc,
                              SourceRange ConditionRange,
                              ConditionValueKind ConditionValue,
                              SourceLocation IfLoc) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_ELIF);
  if (!fillCondition(Loc, IfLoc, ConditionRange, nullptr, nullptr,
                     ConditionValue, Event.Payload.Condition))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::Ifdef(SourceLocation Loc,
                               const Token &MacroNameTok,
                               const MacroDefinition &MD) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_IFDEF);
  if (!fillCondition(Loc, {}, {}, &MacroNameTok, &MD,
                     MD ? CVK_True : CVK_False,
                     Event.Payload.Condition))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::Elifdef(SourceLocation Loc,
                                 const Token &MacroNameTok,
                                 const MacroDefinition &MD) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_ELIFDEF);
  if (!fillCondition(Loc, {}, {}, &MacroNameTok, &MD,
                     MD ? CVK_True : CVK_False,
                     Event.Payload.Condition))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::Elifdef(SourceLocation Loc,
                                 SourceRange ConditionRange,
                                 SourceLocation IfLoc) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_ELIFDEF_SKIPPED);
  if (!fillCondition(Loc, IfLoc, ConditionRange, nullptr, nullptr,
                     CVK_NotEvaluated, Event.Payload.Condition))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::Ifndef(SourceLocation Loc,
                                const Token &MacroNameTok,
                                const MacroDefinition &MD) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_IFNDEF);
  if (!fillCondition(Loc, {}, {}, &MacroNameTok, &MD,
                     MD ? CVK_False : CVK_True,
                     Event.Payload.Condition))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::Elifndef(SourceLocation Loc,
                                  const Token &MacroNameTok,
                                  const MacroDefinition &MD) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_ELIFNDEF);
  if (!fillCondition(Loc, {}, {}, &MacroNameTok, &MD,
                     MD ? CVK_False : CVK_True,
                     Event.Payload.Condition))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::Elifndef(SourceLocation Loc,
                                  SourceRange ConditionRange,
                                  SourceLocation IfLoc) {
  NevercPrepEvent Event =
      makeEvent(NEVERC_PREP_EVENT_ELIFNDEF_SKIPPED);
  if (!fillCondition(Loc, IfLoc, ConditionRange, nullptr, nullptr,
                     CVK_NotEvaluated, Event.Payload.Condition))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::Else(SourceLocation Loc,
                              SourceLocation IfLoc) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_ELSE);
  if (!fillCondition(Loc, IfLoc, {}, nullptr, nullptr,
                     CVK_NotEvaluated, Event.Payload.Condition))
    return;
  emit(Event, Loc);
}

void PluginPrepObserver::Endif(SourceLocation Loc,
                               SourceLocation IfLoc) {
  NevercPrepEvent Event = makeEvent(NEVERC_PREP_EVENT_ENDIF);
  if (!fillCondition(Loc, IfLoc, {}, nullptr, nullptr,
                     CVK_NotEvaluated, Event.Payload.Condition))
    return;
  emit(Event, Loc);
}

} // namespace neverc::plugin
