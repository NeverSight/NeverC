#ifndef NEVERC_SCAN_PREPENGINE_H
#define NEVERC_SCAN_PREPENGINE_H

#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Scan/ExpansionLexer.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/MacroRecord.h"
#include "neverc/Scan/PrepObserver.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Scan/Token.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/SaveAndRestore.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

template <unsigned InternalLen> class SmallString;

} // namespace llvm

namespace neverc {

using llvm::cast;
using llvm::dyn_cast;
using llvm::dyn_cast_or_null;
using llvm::isa;
using llvm::SaveAndRestore;

class CommentHandler;
class EmptylineHandler;
class FileEntry;
class FileManager;
class IncludeResolver;
class MacroArgStorage;
class PragmaDispatch;
class PragmaRegistry;
class LexerCore;
class PrepOptions;
class TokenScratch;
class TargetInfo;

namespace Builtin {
class Context;
}

class TokenValue {
  tok::TokenKind Kind;
  IdentifierInfo *II;

public:
  TokenValue(tok::TokenKind Kind) : Kind(Kind), II(nullptr) {
    assert(Kind != tok::raw_identifier && "Raw identifiers are not supported.");
    assert(Kind != tok::identifier &&
           "Identifiers should be created by TokenValue(IdentifierInfo *)");
    assert(!tok::isLiteral(Kind) && "Literals are not supported.");
    assert(!tok::isAnnotation(Kind) && "Annotations are not supported.");
  }

  TokenValue(IdentifierInfo *II) : Kind(tok::identifier), II(II) {}

  bool operator==(const Token &Tok) const {
    return Tok.getKind() == Kind && (!II || II == Tok.getIdentifierInfo());
  }
  bool operator!=(const Token &Tok) const { return !(*this == Tok); }
};

enum MacroUse {
  // other than #define or #undef
  MU_Other = 0,

  // macro name specified in #define
  MU_Define = 1,

  // macro name specified in #undef
  MU_Undef = 2
};

class PrepEngine {
  friend class VAOptDefinitionContext;
  friend class VarArgScopeGuard;

public:
  llvm::unique_function<void(const neverc::Token &)> OnToken;
  std::shared_ptr<PrepOptions> PPOpts;
  DiagnosticsEngine *Diags;
  LangOptions &LangOpts;
  const TargetInfo *Target = nullptr;
  FileManager &FileMgr;
  SourceManager &SourceMgr;
  std::unique_ptr<TokenScratch> ScratchBuf;
  IncludeResolver &HeaderInfo;

  llvm::BumpPtrAllocator BP;

  enum class IntrinsicMacroKind : uint8_t {
    IMK_Line,
    IMK_RandomNumeric,
    IMK_File,
    IMK_BaseFile,
    IMK_FileName,
    IMK_Function,
    IMK_Date,
    IMK_Time,
    IMK_IncludeLevel,
    IMK_Timestamp,
    IMK_FltEvalMethod,
    IMK_Counter,
    IMK_HasFeature,
    IMK_HasExtension,
    IMK_HasBuiltin,
    IMK_HasConstexprBuiltin,
    IMK_IsIdentifier,
    IMK_HasAttribute,
    IMK_HasDeclspec,
    IMK_HasCAttribute,
    IMK_HasInclude,
    IMK_HasIncludeNext,
    IMK_HasWarning,
    IMK_MsIdentifier,
    IMK_IsTargetArch,
    IMK_IsTargetVendor,
    IMK_IsTargetOS,
    IMK_IsTargetEnvironment,
    IMK_Pragma,
    IMK_MsPragma,
  };

  llvm::DenseMap<IdentifierInfo *, IntrinsicMacroKind> IntrinsicMacroMap;

  IdentifierInfo *Ident__LINE__, *Ident__FILE__,
      *Ident__FUNCTION__; // __LINE__, __FILE__, __FUNCTION__(FIX MSVC
                          // Compatibility)
  IdentifierInfo *Ident__DATE__, *Ident__TIME__; // __DATE__, __TIME__
  IdentifierInfo *Ident__RANDOM__NUMERIC__;      // __RANDOM__NUMERIC__
  IdentifierInfo *Ident__INCLUDE_LEVEL__;        // __INCLUDE_LEVEL__
  IdentifierInfo *Ident__BASE_FILE__;            // __BASE_FILE__
  IdentifierInfo *Ident__FILE_NAME__;            // __FILE_NAME__
  IdentifierInfo *Ident__TIMESTAMP__;            // __TIMESTAMP__
  IdentifierInfo *Ident__COUNTER__;              // __COUNTER__
  IdentifierInfo *Ident_Pragma, *Ident__pragma;  // _Pragma, __pragma
  IdentifierInfo *Ident__identifier;             // __identifier
  IdentifierInfo *Ident__VA_ARGS__;              // __VA_ARGS__
  IdentifierInfo *Ident__VA_OPT__;               // __VA_OPT__
  IdentifierInfo *Ident__has_feature;            // __has_feature
  IdentifierInfo *Ident__has_extension;          // __has_extension
  IdentifierInfo *Ident__has_builtin;            // __has_builtin
  IdentifierInfo *Ident__has_constexpr_builtin;  // __has_constexpr_builtin
  IdentifierInfo *Ident__has_attribute;          // __has_attribute
  IdentifierInfo *Ident__has_include;            // __has_include
  IdentifierInfo *Ident__has_include_next;       // __has_include_next
  IdentifierInfo *Ident__has_warning;            // __has_warning
  IdentifierInfo *Ident__is_identifier;          // __is_identifier
  IdentifierInfo *Ident__has_c_attribute;        // __has_c_attribute
  IdentifierInfo *Ident__has_declspec;           // __has_declspec_attribute
  IdentifierInfo *Ident__is_target_arch;         // __is_target_arch
  IdentifierInfo *Ident__is_target_vendor;       // __is_target_vendor
  IdentifierInfo *Ident__is_target_os;           // __is_target_os
  IdentifierInfo *Ident__is_target_environment;  // __is_target_environment
  IdentifierInfo *Ident__FLT_EVAL_METHOD__;      // __FLT_EVAL_METHOD

  // Weak, only valid (and set) while InMacroArgStorage is true.
  Token *ArgMacro = nullptr;

  SourceLocation DATELoc, TIMELoc;

  // FEM_UnsetOnCommandLine means that an explicit evaluation method was
  // not specified on the command line. The target is queried to set the
  // default evaluation method.
  LangOptions::FPEvalMethodKind CurrentFPEvalMethod =
      LangOptions::FPEvalMethodKind::FEM_UnsetOnCommandLine;

  // The most recent pragma location where the floating point evaluation
  // method was modified. This is used to determine whether the
  // 'pragma neverc fp eval_method' was used whithin the current scope.
  SourceLocation LastFPEvalPragmaLocation;

  LangOptions::FPEvalMethodKind TUFPEvalMethod =
      LangOptions::FPEvalMethodKind::FEM_UnsetOnCommandLine;

  // Next __COUNTER__ value, starts at 0.
  unsigned CounterValue = 0;

  enum {
    /// Maximum depth of \#includes.
    MaxAllowedIncludeStackDepth = 200
  };

  // State that is set before the preprocessor begins.
  bool KeepComments : 1 = false;
  bool KeepMacroComments : 1 = false;
  bool SuppressIncludeNotFoundError : 1 = false;

  // State that changes while the preprocessor runs:
  bool InMacroArgStorage : 1 = false;

  bool OwnsIncludeResolver : 1 = false;

  bool DisableMacroExpansion : 1 = false;

  bool MacroExpansionInDirectivesOverride : 1 = false;

  class ResetMacroExpansionHelper;

  bool PragmasEnabled : 1 = true;

  bool PreprocessedOutput : 1 = false;

  bool ParsingIfOrElifDirective = false;

  bool InMacroArgPreExpansion = false;

  mutable IdentifierTable Identifiers;

  std::unique_ptr<Builtin::Context> BuiltinInfo;

  std::unique_ptr<PragmaRegistry> PragmaDispatchs;

  std::vector<CommentHandler *> CommentHandlers;

  EmptylineHandler *Emptyline = nullptr;

private:
  SourceLocation PragmaAssumeNonNullLoc;

  OptionalDirectoryEntryRef MainFileDir;

  bool HasReachedMaxIncludeDepth = false;

  unsigned LexLevel = 0;

  unsigned TokenCount = 0;

  bool PreprocessToken = false;

  unsigned MaxTokens = 0;
  SourceLocation MaxTokensOverrideLoc;

public:
  using IncludedFilesSet = llvm::SmallPtrSet<const FileEntry *, 16>;

private:
  friend class MacroArgStorage;

  std::unique_ptr<SourceScanner> CurLexer;

  LexerCore *CurPPLexer = nullptr;

  ConstSearchDirIterator CurDirLookup = nullptr;

  std::unique_ptr<ExpansionLexer> CurExpansionLexer;

  typedef bool (*LexerCallback)(PrepEngine &, Token &);
  LexerCallback CurLexerCallback = &DispatchFile;

  struct LexerFrame {
    LexerCallback CurLexerCallback;
    std::unique_ptr<SourceScanner> TheLexer;
    LexerCore *ThePPLexer;
    std::unique_ptr<ExpansionLexer> TheExpansionLexer;
    ConstSearchDirIterator TheDirLookup;

    LexerFrame(LexerCallback CurLexerCallback,
               std::unique_ptr<SourceScanner> &&TheLexer, LexerCore *ThePPLexer,
               std::unique_ptr<ExpansionLexer> &&TheExpansionLexer,
               ConstSearchDirIterator TheDirLookup)
        : CurLexerCallback(std::move(CurLexerCallback)),
          TheLexer(std::move(TheLexer)), ThePPLexer(std::move(ThePPLexer)),
          TheExpansionLexer(std::move(TheExpansionLexer)),
          TheDirLookup(std::move(TheDirLookup)) {}
  };
  std::vector<LexerFrame> IncludeMacroStack;

  std::unique_ptr<PrepObserver> Callbacks;

  struct DeferredExpansion {
    Token Tok;
    MacroDefinition MD;
    SourceRange Range;

    DeferredExpansion(Token Tok, MacroDefinition MD, SourceRange Range)
        : Tok(Tok), MD(MD), Range(Range) {}
  };
  llvm::SmallVector<DeferredExpansion, 2> DelayedMacroExpandsCallbacks;

  class MacroState {
    MacroDirective *MD = nullptr;

  public:
    MacroState() = default;
    MacroState(MacroDirective *MD) : MD(MD) {}

    MacroState(MacroState &&O) noexcept : MD(O.MD) { O.MD = nullptr; }

    MacroState &operator=(MacroState &&O) noexcept {
      MD = O.MD;
      O.MD = nullptr;
      return *this;
    }

    ~MacroState() = default;

    MacroDirective *getLatest() const { return MD; }
    void setLatest(MacroDirective *D) { MD = D; }

    MacroDirective::DefInfo findDirectiveAtLoc(SourceLocation Loc,
                                               SourceManager &SourceMgr) const {
      if (auto *Latest = getLatest())
        return Latest->findDirectiveAtLoc(Loc, SourceMgr);
      return {};
    }
  };

  using MacroMap = llvm::DenseMap<const IdentifierInfo *, MacroState>;

  MacroMap Macros;

  IncludedFilesSet IncludedFiles;

  using WarnUnusedMacroLocsTy = llvm::SmallDenseSet<SourceLocation, 32>;
  WarnUnusedMacroLocsTy WarnUnusedMacroLocs;

  using MsgLocationPair = std::pair<std::string, SourceLocation>;

  struct MacroAnnotationInfo {
    SourceLocation Location;
    std::string Message;
  };

  struct MacroAnnotations {
    std::optional<MacroAnnotationInfo> DeprecationInfo;
    std::optional<MacroAnnotationInfo> RestrictExpansionInfo;
    std::optional<SourceLocation> FinalAnnotationLoc;

    static MacroAnnotations makeDeprecation(SourceLocation Loc,
                                            std::string Msg) {
      return MacroAnnotations{MacroAnnotationInfo{Loc, std::move(Msg)},
                              std::nullopt, std::nullopt};
    }

    static MacroAnnotations makeRestrictExpansion(SourceLocation Loc,
                                                  std::string Msg) {
      return MacroAnnotations{
          std::nullopt, MacroAnnotationInfo{Loc, std::move(Msg)}, std::nullopt};
    }

    static MacroAnnotations makeFinal(SourceLocation Loc) {
      return MacroAnnotations{std::nullopt, std::nullopt, Loc};
    }
  };

  llvm::DenseMap<const IdentifierInfo *, MacroAnnotations> AnnotationInfos;

  MacroArgStorage *MacroArgCache = nullptr;

  llvm::DenseMap<IdentifierInfo *, std::vector<MacroRecord *>>
      PragmaPushMacroRecord;

  // Various statistics we track for performance analysis.
  unsigned NumDirectives = 0;
  unsigned NumDefined = 0;
  unsigned NumUndefined = 0;
  unsigned NumPragma = 0;
  unsigned NumIf = 0;
  unsigned NumElse = 0;
  unsigned NumEndif = 0;
  unsigned NumEnteredSourceFiles = 0;
  unsigned MaxIncludeStackDepth = 0;
  unsigned NumMacroExpanded = 0;
  unsigned NumFnMacroExpanded = 0;
  unsigned NumBuiltinMacroExpanded = 0;
  unsigned NumFastMacroExpanded = 0;
  unsigned NumTokenPaste = 0;
  unsigned NumFastTokenPaste = 0;
  unsigned NumSkipped = 0;

  std::string Predefines;

  FileID PredefinesFileID;

  llvm::SmallVector<std::unique_ptr<ExpansionLexer>, 8> ExpansionLexerCache;

  //
  llvm::SmallVector<Token, 16> MacroExpandedTokens;
  std::vector<std::pair<ExpansionLexer *, size_t>> MacroExpandingLexersStack;

  using CachedTokensTy = llvm::SmallVector<Token, 1>;

  CachedTokensTy CachedTokens;

  CachedTokensTy::size_type CachedLexPos = 0;

  std::vector<CachedTokensTy::size_type> BacktrackPositions;

  bool SkippingExcludedConditionalBlock = false;

  llvm::DenseMap<const char *, unsigned> RecordedSkippedRanges;

public:
  PrepEngine(std::shared_ptr<PrepOptions> PPOpts, DiagnosticsEngine &diags,
             const LangOptions &LangOpts, SourceManager &SM,
             IncludeResolver &Headers, IdentifierInfoLookup *IILookup = nullptr,
             bool OwnsIncludeResolver = false);

  ~PrepEngine();

  void Initialize(const TargetInfo &Target);

  PrepOptions &getPrepEngineOpts() const { return *PPOpts; }

  DiagnosticsEngine &getDiagnostics() const { return *Diags; }
  void setDiagnostics(DiagnosticsEngine &D) { Diags = &D; }

  const LangOptions &getLangOpts() const { return LangOpts; }
  const TargetInfo &getTargetInfo() const { return *Target; }
  FileManager &getFileManager() const { return FileMgr; }
  SourceManager &getSourceManager() const { return SourceMgr; }
  IncludeResolver &getIncludeResolver() const { return HeaderInfo; }

  IdentifierTable &getIdentifierTable() { return Identifiers; }
  const IdentifierTable &getIdentifierTable() const { return Identifiers; }
  Builtin::Context &getBuiltinInfo() { return *BuiltinInfo; }
  llvm::BumpPtrAllocator &getPrepEngineAllocator() { return BP; }

  unsigned getNumDirectives() const { return NumDirectives; }

  bool isParsingIfOrElifDirective() const { return ParsingIfOrElifDirective; }

  void SetCommentRetentionState(bool KeepComments, bool KeepMacroComments) {
    this->KeepComments = KeepComments || KeepMacroComments;
    this->KeepMacroComments = KeepMacroComments;
  }

  bool getCommentRetentionState() const { return KeepComments; }

  void setPragmasEnabled(bool Enabled) { PragmasEnabled = Enabled; }
  bool getPragmasEnabled() const { return PragmasEnabled; }

  void SetSuppressIncludeNotFoundError(bool Suppress) {
    SuppressIncludeNotFoundError = Suppress;
  }

  bool GetSuppressIncludeNotFoundError() {
    return SuppressIncludeNotFoundError;
  }

  void setPreprocessedOutput(bool IsPreprocessedOutput) {
    PreprocessedOutput = IsPreprocessedOutput;
  }

  bool isPreprocessedOutput() const { return PreprocessedOutput; }

  bool isCurrentLexer(const LexerCore *L) const { return CurPPLexer == L; }

  LexerCore *getCurrentLexer() const { return CurPPLexer; }

  LexerCore *getCurrentFileLexer() const;

  FileID getPredefinesFileID() const { return PredefinesFileID; }

  PrepObserver *getObserver() const { return Callbacks.get(); }
  void addObserver(std::unique_ptr<PrepObserver> C) {
    if (Callbacks)
      C = std::make_unique<ChainedPrepObserver>(std::move(C),
                                                std::move(Callbacks));
    Callbacks = std::move(C);
  }

  unsigned getTokenCount() const { return TokenCount; }

  unsigned getMaxTokens() const { return MaxTokens; }

  void overrideMaxTokens(unsigned Value, SourceLocation Loc) {
    MaxTokens = Value;
    MaxTokensOverrideLoc = Loc;
  };

  SourceLocation getMaxTokensOverrideLoc() const {
    return MaxTokensOverrideLoc;
  }

  void setTokenWatcher(llvm::unique_function<void(const neverc::Token &)> F) {
    OnToken = std::move(F);
  }

  void setPreprocessToken(bool Preprocess) { PreprocessToken = Preprocess; }

  bool isMacroDefined(llvm::StringRef Id) {
    return isMacroDefined(&Identifiers.get(Id));
  }
  bool isMacroDefined(const IdentifierInfo *II) {
    return II->hasMacroDefinition();
  }

  MacroDefinition getMacroDefinition(const IdentifierInfo *II) {
    if (!II->hasMacroDefinition())
      return {};

    auto It = Macros.find(II);
    if (LLVM_UNLIKELY(It == Macros.end()))
      return {};
    MacroState &S = It->second;
    auto *MD = S.getLatest();
    return MacroDefinition(dyn_cast_or_null<DefMacroDirective>(MD), {}, false);
  }

  MacroDefinition getMacroDefinitionAtLoc(const IdentifierInfo *II,
                                          SourceLocation Loc) {
    if (!II->hadMacroDefinition())
      return {};

    MacroState &S = Macros[II];
    MacroDirective::DefInfo DI;
    if (auto *MD = S.getLatest())
      DI = MD->findDirectiveAtLoc(Loc, getSourceManager());
    return MacroDefinition(DI.getDirective(), {}, false);
  }

  MacroDirective *getLocalMacroDirective(const IdentifierInfo *II) const {
    if (!II->hasMacroDefinition())
      return nullptr;

    auto *MD = getLocalMacroDirectiveHistory(II);
    if (!MD || MD->getDefinition().isUndefined())
      return nullptr;

    return MD;
  }

  const MacroRecord *getMacroRecord(const IdentifierInfo *II) const {
    return const_cast<PrepEngine *>(this)->getMacroRecord(II);
  }

  MacroRecord *getMacroRecord(const IdentifierInfo *II) {
    if (!II->hasMacroDefinition())
      return nullptr;
    if (auto MD = getMacroDefinition(II))
      return MD.getMacroRecord();
    return nullptr;
  }

  MacroDirective *getLocalMacroDirectiveHistory(const IdentifierInfo *II) const;

  void appendMacroDirective(IdentifierInfo *II, MacroDirective *MD);
  DefMacroDirective *appendDefMacroDirective(IdentifierInfo *II,
                                             MacroRecord *MI,
                                             SourceLocation Loc) {
    DefMacroDirective *MD = AllocDefDirective(MI, Loc);
    appendMacroDirective(II, MD);
    return MD;
  }
  DefMacroDirective *appendDefMacroDirective(IdentifierInfo *II,
                                             MacroRecord *MI) {
    return appendDefMacroDirective(II, MI, MI->getDefinitionLoc());
  }

  void setLoadedMacroDirective(IdentifierInfo *II, MacroDirective *ED,
                               MacroDirective *MD);

  using macro_iterator = MacroMap::const_iterator;

  macro_iterator macro_begin(bool IncludeExternalMacros = true) const;
  macro_iterator macro_end(bool IncludeExternalMacros = true) const;

  llvm::iterator_range<macro_iterator>
  macros(bool IncludeExternalMacros = true) const {
    macro_iterator begin = macro_begin(IncludeExternalMacros);
    macro_iterator end = macro_end(IncludeExternalMacros);
    return llvm::make_range(begin, end);
  }

  bool markIncluded(FileEntryRef File) {
    HeaderInfo.getFileInfo(File);
    return IncludedFiles.insert(File).second;
  }

  bool alreadyIncluded(FileEntryRef File) const {
    HeaderInfo.getFileInfo(File);
    return IncludedFiles.contains(File);
  }

  IncludedFilesSet &getIncludedFiles() { return IncludedFiles; }
  const IncludedFilesSet &getIncludedFiles() const { return IncludedFiles; }

  llvm::StringRef
  getLastMacroWithSpelling(SourceLocation Loc,
                           llvm::ArrayRef<TokenValue> Tokens) const;

  const std::string &getPredefines() const { return Predefines; }

  void setPredefines(std::string P) { Predefines = std::move(P); }

  IdentifierInfo *getIdentifierInfo(llvm::StringRef Name) const {
    return &Identifiers.get(Name);
  }

  void AddPragmaDispatch(llvm::StringRef Namespace, PragmaDispatch *Handler);
  void AddPragmaDispatch(PragmaDispatch *Handler) {
    AddPragmaDispatch(llvm::StringRef(), Handler);
  }

  void RemovePragmaDispatch(llvm::StringRef Namespace, PragmaDispatch *Handler);
  void RemovePragmaDispatch(PragmaDispatch *Handler) {
    RemovePragmaDispatch(llvm::StringRef(), Handler);
  }

  void IgnorePragmas();

  void setEmptylineHandler(EmptylineHandler *Handler) { Emptyline = Handler; }

  EmptylineHandler *getEmptylineHandler() const { return Emptyline; }

  void addCommentHandler(CommentHandler *Handler);

  void removeCommentHandler(CommentHandler *Handler);

  void InitMainInput();

  void FiniMainInput();

  bool PushSourceFile(FileID FID, ConstSearchDirIterator Dir,
                      SourceLocation Loc, bool IsFirstIncludeOfFile = true);

  void PushMacroContext(Token &Tok, SourceLocation ILEnd, MacroRecord *Macro,
                        MacroArgStorage *Args);

private:
  void PushTokenStream(const Token *Toks, unsigned NumToks,
                       bool DisableMacroExpansion, bool OwnsTokens,
                       bool IsReinject);

public:
  void PushTokenStream(std::unique_ptr<Token[]> Toks, unsigned NumToks,
                       bool DisableMacroExpansion, bool IsReinject) {
    PushTokenStream(Toks.release(), NumToks, DisableMacroExpansion, true,
                    IsReinject);
  }

  void PushTokenStream(llvm::ArrayRef<Token> Toks, bool DisableMacroExpansion,
                       bool IsReinject) {
    PushTokenStream(Toks.data(), Toks.size(), DisableMacroExpansion, false,
                    IsReinject);
  }

  void PopLexerLevel();

  void SaveLexState();

  void DropSavedState();

  void RestoreLexState();

  bool isBacktrackEnabled() const { return !BacktrackPositions.empty(); }

  void LexSlow(Token &Result);

  LLVM_ATTRIBUTE_ALWAYS_INLINE void Lex(Token &Result) {
    if (LLVM_LIKELY(CurLexerCallback == &DispatchFile &&
                    CurLexer->Lex(Result))) {
      if (LLVM_LIKELY(LexLevel == 0)) {
        ++TokenCount;
        if (LLVM_UNLIKELY(OnToken))
          OnToken(Result);
      }
      return;
    }
    LexSlow(Result);
  }

  void ConsumeAllTokens(std::vector<Token> *Tokens = nullptr);

  bool LexIncludePathTok(Token &Result, bool AllowMacroExpansion = true);

  bool LexStringContent(Token &Result, std::string &String,
                        const char *DiagnosticTag, bool AllowMacroExpansion) {
    if (AllowMacroExpansion)
      Lex(Result);
    else
      LexWithoutExpansion(Result);
    return CompleteStringLiteral(Result, String, DiagnosticTag,
                                 AllowMacroExpansion);
  }

  bool CompleteStringLiteral(Token &Result, std::string &String,
                             const char *DiagnosticTag,
                             bool AllowMacroExpansion);

  void LexSkipComments(Token &Result) {
    do
      Lex(Result);
    while (Result.getKind() == tok::comment);
  }

  void LexWithoutExpansion(Token &Result) {
    // Disable macro expansion.
    bool OldVal = DisableMacroExpansion;
    DisableMacroExpansion = true;
    // Lex the token.
    Lex(Result);

    // Reenable it.
    DisableMacroExpansion = OldVal;
  }

  void LexRawSkipComments(Token &Result) {
    do
      LexWithoutExpansion(Result);
    while (Result.getKind() == tok::comment);
  }

  bool parseBasicInteger(Token &Tok, uint64_t &Value);

  void SetMacroExpansionOnlyInDirectives() {
    DisableMacroExpansion = true;
    MacroExpansionInDirectivesOverride = true;
  }

  const Token &LookAhead(unsigned N) {
    assert(LexLevel == 0 && "cannot use lookahead while lexing");
    if (CachedLexPos + N < CachedTokens.size())
      return CachedTokens[CachedLexPos + N];
    else
      return PeekAhead(N + 1);
  }

  void RevertCachedTokens(unsigned N) {
    assert(isBacktrackEnabled() &&
           "Should only be called when tokens are cached for backtracking");
    assert(signed(CachedLexPos) - signed(N) >=
               signed(BacktrackPositions.back()) &&
           "Should revert tokens up to the last backtrack position, not more");
    assert(signed(CachedLexPos) - signed(N) >= 0 &&
           "Corrupted backtrack positions ?");
    CachedLexPos -= N;
  }

  void InjectToken(const Token &Tok, bool IsReinject) {
    if (LexLevel) {
      // It's not correct in general to enter caching lex mode while in the
      // middle of a nested lexing action.
      auto TokCopy = std::make_unique<Token[]>(1);
      TokCopy[0] = Tok;
      PushTokenStream(std::move(TokCopy), 1, true, IsReinject);
    } else {
      EnableCaching();
      assert(IsReinject && "new tokens in the middle of cached stream");
      CachedTokens.insert(CachedTokens.begin() + CachedLexPos, Tok);
    }
  }

  void AnnotateCachedTokens(const Token &Tok) {
    assert(Tok.isAnnotation() && "Expected annotation token");
    if (CachedLexPos != 0 && isBacktrackEnabled())
      SpliceCachedAnnotation(Tok);
  }

  SourceLocation getLastCachedTokenLocation() const {
    assert(CachedLexPos != 0);
    return CachedTokens[CachedLexPos - 1].getLastLoc();
  }

  bool IsLastCachedToken(const Token &Tok) const;

  void ReplaceLastCachedToken(llvm::ArrayRef<Token> NewToks);

  void ReplaceLastTokenWithAnnotation(const Token &Tok) {
    assert(Tok.isAnnotation() && "Expected annotation token");
    if (CachedLexPos != 0 && isBacktrackEnabled())
      CachedTokens[CachedLexPos - 1] = Tok;
  }

  void InjectAnnotation(SourceRange Range, tok::TokenKind Kind,
                        void *AnnotationVal);

  bool mightHavePendingAnnotationTokens() {
    return CurLexerCallback != DispatchFile;
  }

  void TypoCorrectToken(const Token &Tok) {
    assert(Tok.getIdentifierInfo() && "Expected identifier token");
    if (CachedLexPos != 0 && isBacktrackEnabled())
      CachedTokens[CachedLexPos - 1] = Tok;
  }

  void refreshDispatch();

  SourceLocation getPragmaAssumeNonNullLoc() const {
    return PragmaAssumeNonNullLoc;
  }

  void setPragmaAssumeNonNullLoc(SourceLocation Loc) {
    PragmaAssumeNonNullLoc = Loc;
  }

  void setMainFileDir(DirectoryEntryRef Dir) { MainFileDir = Dir; }

  DiagnosticBuilder Diag(SourceLocation Loc, unsigned DiagID) const {
    return Diags->Report(Loc, DiagID);
  }

  DiagnosticBuilder Diag(const Token &Tok, unsigned DiagID) const {
    return Diags->Report(Tok.getLocation(), DiagID);
  }

  llvm::StringRef getSpelling(SourceLocation loc,
                              llvm::SmallVectorImpl<char> &buffer,
                              bool *invalid = nullptr) const {
    return SourceScanner::getSpelling(loc, buffer, SourceMgr, LangOpts,
                                      invalid);
  }

  std::string getSpelling(const Token &Tok, bool *Invalid = nullptr) const {
    return SourceScanner::getSpelling(Tok, SourceMgr, LangOpts, Invalid);
  }

  unsigned getSpelling(const Token &Tok, const char *&Buffer,
                       bool *Invalid = nullptr) const {
    return SourceScanner::getSpelling(Tok, Buffer, SourceMgr, LangOpts,
                                      Invalid);
  }

  llvm::StringRef getSpelling(const Token &Tok,
                              llvm::SmallVectorImpl<char> &Buffer,
                              bool *Invalid = nullptr) const;

  bool scanRawToken(SourceLocation Loc, Token &Result,
                    bool IgnoreWhiteSpace = false) {
    return SourceScanner::scanRawToken(Loc, Result, SourceMgr, LangOpts,
                                       IgnoreWhiteSpace);
  }

  char
  getSpellingOfSingleCharacterNumericConstant(const Token &Tok,
                                              bool *Invalid = nullptr) const {
    assert(Tok.is(tok::numeric_constant) && Tok.getLength() == 1 &&
           "Called on unsupported token");
    assert(!Tok.needsCleaning() && "Token can't need cleaning with length 1");

    // If the token is carrying a literal data pointer, just use it.
    if (const char *D = Tok.getLiteralData())
      return *D;

    // Otherwise, fall back on getCharacterData, which is slower, but always
    // works.
    return *SourceMgr.getCharacterData(Tok.getLocation(), Invalid);
  }

  llvm::StringRef getImmediateMacroName(SourceLocation Loc) {
    return SourceScanner::getImmediateMacroName(Loc, SourceMgr, getLangOpts());
  }

  void WriteScratch(llvm::StringRef Str, Token &Tok,
                    SourceLocation ExpansionLocStart = SourceLocation(),
                    SourceLocation ExpansionLocEnd = SourceLocation());

  SourceLocation SplitTokenLoc(SourceLocation TokLoc, unsigned Length);

  SourceLocation getLocForEndOfToken(SourceLocation Loc, unsigned Offset = 0) {
    return SourceScanner::getLocForEndOfToken(Loc, Offset, SourceMgr, LangOpts);
  }

  bool isAtStartOfMacroExpansion(SourceLocation loc,
                                 SourceLocation *MacroBegin = nullptr) const {
    return SourceScanner::isAtStartOfMacroExpansion(loc, SourceMgr, LangOpts,
                                                    MacroBegin);
  }

  bool isAtEndOfMacroExpansion(SourceLocation loc,
                               SourceLocation *MacroEnd = nullptr) const {
    return SourceScanner::isAtEndOfMacroExpansion(loc, SourceMgr, LangOpts,
                                                  MacroEnd);
  }

  void DumpToken(const Token &Tok, bool ShowFlags = false) const;
  void DumpLoc(SourceLocation Loc) const;
  void DumpMacroTokens(const MacroRecord &MI) const;
  void traceMacroRecord(const IdentifierInfo *II);

  SourceLocation AdvanceToTokenCharacter(SourceLocation TokStart,
                                         unsigned Char) const {
    return SourceScanner::AdvanceToTokenCharacter(TokStart, Char, SourceMgr,
                                                  LangOpts);
  }

  void TallyTokenPaste(bool isFast) {
    if (isFast)
      ++NumFastTokenPaste;
    else
      ++NumTokenPaste;
  }

  void DumpStats();

  size_t getTotalMemory() const;

  void SkipPastedComment(Token &Tok);

  // --- Callback methods (invoked by scanners on directives/events) ---

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  IdentifierInfo *ResolveRawIdent(Token &Identifier) const {
    if (LLVM_LIKELY(!Identifier.needsCleaning() && !Identifier.hasUCN())) {
      IdentifierInfo *II = getIdentifierInfo(Identifier.getRawIdentifier());
      Identifier.setIdentifierInfo(II);
      Identifier.setKind(II->getTokenID());
      return II;
    }
    return ResolveRawIdentSlow(Identifier);
  }

  IdentifierInfo *ResolveRawIdentSlow(Token &Identifier) const;

private:
  llvm::DenseMap<IdentifierInfo *, unsigned> PoisonReasons;

public:
  void RecordPoisonReason(IdentifierInfo *II, unsigned DiagID);

  void DiagPoisonedIdent(Token &Identifier);

  void CheckIdentPoison(Token &Identifier) {
    if (IdentifierInfo *II = Identifier.getIdentifierInfo()) {
      if (II->isPoisoned()) {
        DiagPoisonedIdent(Identifier);
      }
    }
  }

private:
  const char *computeEffectiveEndPos();

public:
  bool ResolveIdentifier(Token &Identifier);

  bool FinalizeSourceEnd(Token &Result, bool isEndOfMacro = false);
  void checkHeaderGuardAtEOF();

  bool FinalizeExpansion(Token &Result);

  void DispatchDirective(Token &Result);

  SourceLocation VerifyDirectiveEnd(const char *DirType,
                                    bool EnableMacros = false);

  SourceRange DiscardDirectiveLine();

  bool SawDateOrTime() const {
    return DATELoc != SourceLocation() || TIMELoc != SourceLocation();
  }
  unsigned getCounterValue() const { return CounterValue; }
  void setCounterValue(unsigned V) { CounterValue = V; }

  LangOptions::FPEvalMethodKind getCurrentFPEvalMethod() const {
    assert(CurrentFPEvalMethod != LangOptions::FEM_UnsetOnCommandLine &&
           "FPEvalMethod should be set either from command line or from the "
           "target info");
    return CurrentFPEvalMethod;
  }

  LangOptions::FPEvalMethodKind getTUFPEvalMethod() const {
    return TUFPEvalMethod;
  }

  SourceLocation getLastFPEvalPragmaLocation() const {
    return LastFPEvalPragmaLocation;
  }

  void setCurrentFPEvalMethod(SourceLocation PragmaLoc,
                              LangOptions::FPEvalMethodKind Val) {
    assert(Val != LangOptions::FEM_UnsetOnCommandLine &&
           "FPEvalMethod should never be set to FEM_UnsetOnCommandLine");
    // This is the location of the '#pragma float_control" where the
    // execution state is modifed.
    LastFPEvalPragmaLocation = PragmaLoc;
    CurrentFPEvalMethod = Val;
    TUFPEvalMethod = Val;
  }

  void setTUFPEvalMethod(LangOptions::FPEvalMethodKind Val) {
    assert(Val != LangOptions::FEM_UnsetOnCommandLine &&
           "TUPEvalMethod should never be set to FEM_UnsetOnCommandLine");
    TUFPEvalMethod = Val;
  }

  MacroRecord *AllocMacroRecord(SourceLocation L);

  bool extractIncludeFilename(SourceLocation Loc, llvm::StringRef &Buffer);

  OptionalFileEntryRef
  ResolveInclude(SourceLocation FilenameLoc, llvm::StringRef Filename,
                 bool isAngled, ConstSearchDirIterator FromDir,
                 const FileEntry *FromFile, ConstSearchDirIterator *CurDir,
                 llvm::SmallVectorImpl<char> *SearchPath,
                 llvm::SmallVectorImpl<char> *RelativePath, bool *IsMapped,
                 bool *IsFrameworkFound, bool SkipCache = false,
                 bool OpenFile = true, bool CacheFailures = true);

  bool isInPrimaryFile() const;

  bool LexOnOffSwitch(tok::OnOffSwitch &Result);

  bool validateMacroName(Token &MacroNameTok, MacroUse isDefineUndef,
                         bool *ShadowFlag = nullptr);

private:
  friend void ExpansionLexer::substituteFunctionParams();

  void SaveLexerFrame() {
    assert(CurLexerCallback != DispatchCache && "cannot push a caching lexer");
    IncludeMacroStack.emplace_back(CurLexerCallback, std::move(CurLexer),
                                   CurPPLexer, std::move(CurExpansionLexer),
                                   CurDirLookup);
    CurPPLexer = nullptr;
  }

  void RestoreLexerFrame() {
    CurLexer = std::move(IncludeMacroStack.back().TheLexer);
    CurPPLexer = IncludeMacroStack.back().ThePPLexer;
    CurExpansionLexer = std::move(IncludeMacroStack.back().TheExpansionLexer);
    CurDirLookup = IncludeMacroStack.back().TheDirLookup;
    CurLexerCallback = IncludeMacroStack.back().CurLexerCallback;
    IncludeMacroStack.pop_back();
  }

  void CarryLineFlags(Token &Result);

  DefMacroDirective *AllocDefDirective(MacroRecord *MI, SourceLocation Loc);
  UndefMacroDirective *AllocUndefDirective(SourceLocation UndefLoc);

  void ValidateMacroNameTok(Token &MacroNameTok,
                            MacroUse IsDefineUndef = MU_Other,
                            bool *ShadowFlag = nullptr);

  MacroRecord *ParseMacroBody(const Token &MacroNameTok,
                              bool ImmediatelyAfterHeaderGuard);

  bool ParseMacroParamList(MacroRecord *MI, Token &LastTok);

  void SuggestDirectiveTypo(const Token &Tok, llvm::StringRef Directive) const;

  void SkipExcludedBlock(SourceLocation HashTokenLoc, SourceLocation IfTokenLoc,
                         bool FoundNonSkipPortion, bool FoundElse,
                         SourceLocation ElseLoc = SourceLocation());

  struct CondEvalOutcome {
    /// Whether the expression was evaluated as true or not.
    bool Conditional;

    /// True if the expression contained identifiers that were undefined.
    bool IncludedUndefinedIds;

    /// The source range for the expression.
    SourceRange ExprRange;
  };

  CondEvalOutcome EvalCondExpr(IdentifierInfo *&IfNDefMacro);

  bool CheckHasInclude(Token &Tok, IdentifierInfo *II);

  bool CheckHasIncludeNext(Token &Tok, IdentifierInfo *II);

  std::pair<ConstSearchDirIterator, const FileEntry *>
  getIncludeNextStart(const Token &IncludeNextTok) const;

  void InitBuiltinPragmas();

  void InitIntrinsicMacros();

  bool BeginMacroExpansion(Token &Identifier, const MacroDefinition &MD);

  //
  Token *stashExpandedTokens(ExpansionLexer *tokLexer,
                             llvm::ArrayRef<Token> tokens);

  void unstashLastLexerTokens();

  bool ProbeLeftParen();

  MacroArgStorage *CollectMacroArgs(Token &MacroName, MacroRecord *MI,
                                    SourceLocation &MacroEnd);

  void ExpandIntrinsicMacro(Token &Tok);

  void ExpandUnderscorePragma(Token &Tok);

  void ExpandMsPragma(Token &Tok);

  void PushLexer(SourceScanner *TheLexer, ConstSearchDirIterator Dir);

  void setPredefinesFileID(FileID FID) {
    assert(PredefinesFileID.isInvalid() && "PredefinesFileID already set!");
    PredefinesFileID = FID;
  }

  static bool IsFileLexer(const SourceScanner *L, const LexerCore *P) {
    return L ? !L->isPragmaLexer() : P != nullptr;
  }

  static bool IsFileLexer(const LexerFrame &I) {
    return IsFileLexer(I.TheLexer.get(), I.ThePPLexer);
  }

  bool IsFileLexer() const { return IsFileLexer(CurLexer.get(), CurPPLexer); }

  // --- Token caching ---
  void FetchCachedToken(Token &Result);

  bool IsCaching() const {
    return !CurPPLexer && !CurExpansionLexer && !IncludeMacroStack.empty();
  }

  void EnableCaching();
  void EnableCachingFast();

  void DisableCaching() {
    if (IsCaching())
      PopLexerLevel();
  }

  const Token &PeekAhead(unsigned N);
  void SpliceCachedAnnotation(const Token &Tok);

  // --- Directive handlers ---
  void ExecLineDir();
  void ExecDigitDir(Token &Tok);
  void ExecUserDiag(Token &Tok, bool isWarning);
  void ExecIdentSCCS(Token &Tok);

  OptionalFileEntryRef
  FindIncludeTarget(ConstSearchDirIterator *CurDir, llvm::StringRef &Filename,
                    SourceLocation FilenameLoc, CharSourceRange FilenameRange,
                    const Token &FilenameTok, bool &IsFrameworkFound,
                    bool &IsMapped, ConstSearchDirIterator LookupFrom,
                    const FileEntry *LookupFromFile,
                    llvm::StringRef &ResolveIncludename,
                    llvm::SmallVectorImpl<char> &RelativePath,
                    llvm::SmallVectorImpl<char> &SearchPath, bool isAngled);

  void ExecInclude(SourceLocation HashLoc, Token &Tok,
                   ConstSearchDirIterator LookupFrom = nullptr,
                   const FileEntry *LookupFromFile = nullptr);
  void ExecHeaderImport(SourceLocation HashLoc, Token &IncludeTok,
                        Token &FilenameTok, SourceLocation EndLoc,
                        ConstSearchDirIterator LookupFrom = nullptr,
                        const FileEntry *LookupFromFile = nullptr);
  void ExecIncludeNext(SourceLocation HashLoc, Token &Tok);
  void ExecIncludeMacros(SourceLocation HashLoc, Token &Tok);
  void ExecImport(SourceLocation HashLoc, Token &Tok);
  void ExecMsImport(Token &Tok);

public:
  OptionalFileEntryRef getHeaderToIncludeForDiagnostics(SourceLocation IncLoc,
                                                        SourceLocation MLoc);

private:
  // Macro handling.
  void ExecDefine(Token &Tok, bool ImmediatelyAfterHeaderGuard);
  void ExecUndef();

  // Conditional Inclusion.
  void ExecIfdef(Token &Result, const Token &HashToken, bool isIfndef,
                 bool ReadAnyTokensBeforeDirective);
  void ExecIf(Token &IfToken, const Token &HashToken,
              bool ReadAnyTokensBeforeDirective);
  void ExecEndif(Token &EndifToken);
  void ExecElse(Token &Result, const Token &HashToken);
  void ExecElifFamily(Token &ElifToken, const Token &HashToken,
                      tok::PPKeywordKind Kind);

  // Pragmas.
  void ExecPragma(PragmaIntroducer Introducer);

public:
  void ExecPragmaOnce(Token &OnceTok);
  void ExecPragmaMark(Token &MarkTok);
  void ExecPragmaPoison();
  void ExecPragmaSysHeader(Token &SysHeaderTok);
  void ExecPragmaDep(Token &DependencyTok);
  void ExecPragmaPush(Token &Tok);
  void ExecPragmaPop(Token &Tok);
  void ExecPragmaAlias(Token &Tok);
  IdentifierInfo *ParsePragmaPushPop(Token &Tok);

  // Return true and store the first token only if any CommentHandler
  // has inserted some tokens and getCommentRetentionState() is false.
  bool DispatchComment(Token &result, SourceRange Comment);

  void setMacroUsed(MacroRecord *MI);

  void addMacroDeprecationMsg(const IdentifierInfo *II, std::string Msg,
                              SourceLocation AnnotationLoc) {
    auto Annotations = AnnotationInfos.find(II);
    if (Annotations == AnnotationInfos.end())
      AnnotationInfos.insert(std::make_pair(
          II,
          MacroAnnotations::makeDeprecation(AnnotationLoc, std::move(Msg))));
    else
      Annotations->second.DeprecationInfo =
          MacroAnnotationInfo{AnnotationLoc, std::move(Msg)};
  }

  void addRestrictExpansionMsg(const IdentifierInfo *II, std::string Msg,
                               SourceLocation AnnotationLoc) {
    auto Annotations = AnnotationInfos.find(II);
    if (Annotations == AnnotationInfos.end())
      AnnotationInfos.insert(
          std::make_pair(II, MacroAnnotations::makeRestrictExpansion(
                                 AnnotationLoc, std::move(Msg))));
    else
      Annotations->second.RestrictExpansionInfo =
          MacroAnnotationInfo{AnnotationLoc, std::move(Msg)};
  }

  void addFinalLoc(const IdentifierInfo *II, SourceLocation AnnotationLoc) {
    auto Annotations = AnnotationInfos.find(II);
    if (Annotations == AnnotationInfos.end())
      AnnotationInfos.insert(
          std::make_pair(II, MacroAnnotations::makeFinal(AnnotationLoc)));
    else
      Annotations->second.FinalAnnotationLoc = AnnotationLoc;
  }

  const MacroAnnotations &getMacroAnnotations(const IdentifierInfo *II) const {
    return AnnotationInfos.find(II)->second;
  }

  void checkMacroWarnings(const Token &Identifier) const {
    if (LLVM_UNLIKELY(Identifier.getIdentifierInfo()->isDeprecatedMacro()))
      warnMacroDeprecation(Identifier);

    if (LLVM_UNLIKELY(Identifier.getIdentifierInfo()->isRestrictExpansion()) &&
        !SourceMgr.isInMainFile(Identifier.getLocation()))
      warnRestrictExpansion(Identifier);
  }

  static void formatPathForFileMacro(llvm::SmallVectorImpl<char> &Path,
                                     const LangOptions &LangOpts,
                                     const TargetInfo &TI);

  static void formatPathToFileName(llvm::SmallVectorImpl<char> &FileName,
                                   const PresumedLoc &PLoc,
                                   const LangOptions &LangOpts,
                                   const TargetInfo &TI);

private:
  void warnMacroDeprecation(const Token &Identifier) const;
  void warnRestrictExpansion(const Token &Identifier) const;
  void warnFinalMacro(const Token &Identifier, bool IsUndef) const;

  bool InSafeBufferOptOutRegion = false;

  SourceLocation
      CurrentSafeBufferOptOutStart; // It is used to report the start location
                                    // of an never-closed region.

  // An ordered sequence of "-Wunsafe-buffer-usage" opt-out regions in one
  // translation unit. Each region is represented by a pair of start and end
  // locations.  A region is "open" if its' start and end locations are
  // identical.
  llvm::SmallVector<std::pair<SourceLocation, SourceLocation>, 8>
      SafeBufferOptOutMap;

public:
  bool isSafeBufferOptOut(const SourceManager &SourceMgr,
                          const SourceLocation &Loc) const;

  bool enterOrExitSafeBufferOptOutRegion(bool isEnter,
                                         const SourceLocation &Loc);

  bool isPPInSafeBufferOptOutRegion();

  bool isPPInSafeBufferOptOutRegion(SourceLocation &StartLoc);

private:
  static bool DispatchFile(PrepEngine &P, Token &Result) {
    return P.CurLexer->Lex(Result);
  }
  static bool DispatchExpansion(PrepEngine &P, Token &Result) {
    return P.CurExpansionLexer->Lex(Result);
  }
  static bool DispatchCache(PrepEngine &P, Token &Result) {
    P.FetchCachedToken(Result);
    return true;
  }
};

class CommentHandler {
public:
  virtual ~CommentHandler();

  // The handler shall return true if it has pushed any tokens
  // to be read using e.g. InjectToken or PushTokenStream.
  virtual bool DispatchComment(PrepEngine &PP, SourceRange Comment) = 0;
};

class EmptylineHandler {
public:
  virtual ~EmptylineHandler();

  // The handler handles empty lines.
  virtual void ProcessEmptyline(SourceRange Range) = 0;
};

} // namespace neverc

#endif // NEVERC_SCAN_PREPENGINE_H
