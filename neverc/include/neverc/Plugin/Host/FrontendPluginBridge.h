#ifndef NEVERC_PLUGIN_HOST_FRONTENDPLUGINBRIDGE_H
#define NEVERC_PLUGIN_HOST_FRONTENDPLUGINBRIDGE_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginPrep.h"
#include "neverc/Plugin/PluginSema.h"
#include "neverc/Plugin/PluginSource.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace neverc {
class LangOptions;
class IdentifierInfo;
class Decl;
class Expr;
class MacroArgStorage;
class MacroDirective;
class MacroRecord;
class ParserPluginHooks;
class PrepEngine;
class QualType;
class Sema;
class SourceManager;
class Stmt;
class Token;
class TreeContext;
} // namespace neverc

namespace neverc::plugin {

class PluginTaskContext;
class PluginProcessServices;
class PluginPhaseExecutor;
class PluginPrepBridge;
class PluginPrepObserver;

class FrontendPluginBridge {
public:
  FrontendPluginBridge(PluginTaskContext &Task, SourceManager &SourceMgr);
  FrontendPluginBridge(PluginTaskContext &Task, SourceManager &SourceMgr,
                       const LangOptions &LangOpts);

  llvm::Expected<NevercSourceLocation> createLocation(SourceLocation Location);
  llvm::Expected<NevercSourceRange> createRange(CharSourceRange Range);
  NevercStatus resolvePublishedRange(NevercTaskHandle Task,
                                     NevercSourceRange Range,
                                     CharSourceRange *OutRange) {
    return resolveRange(Task, Range, OutRange);
  }
  NevercStatus resolvePublishedLocation(NevercTaskHandle Task,
                                        NevercSourceLocation Location,
                                        SourceLocation *OutLocation) {
    return resolveLocation(Task, Location, OutLocation);
  }

  const NevercSourceLocationAPI &sourceLocationAPI() const {
    return SourceLocationAPI;
  }

private:
  friend class PluginASTBridge;
  friend class PluginPrepBridge;
  friend class PluginPrepObserver;

  static NevercStatus NEVERC_CALL getLocationInfo(
      void *Context, NevercTaskHandle Task, NevercSourceLocation Location,
      NevercSourceLocationInfo *OutInfo);
  static NevercStatus NEVERC_CALL getSpellingLocation(
      void *Context, NevercTaskHandle Task, NevercSourceLocation Location,
      NevercSourceLocation *OutLocation);
  static NevercStatus NEVERC_CALL getExpansionLocation(
      void *Context, NevercTaskHandle Task, NevercSourceLocation Location,
      NevercSourceLocation *OutLocation);
  static NevercStatus NEVERC_CALL getFileLocation(
      void *Context, NevercTaskHandle Task, NevercSourceLocation Location,
      NevercSourceLocation *OutLocation);
  static NevercStatus NEVERC_CALL getRangeInfo(void *Context,
                                               NevercTaskHandle Task,
                                               NevercSourceRange Range,
                                               NevercSourceRangeInfo *OutInfo);
  static NevercStatus NEVERC_CALL getSourceText(void *Context,
                                                NevercTaskHandle Task,
                                                NevercSourceRange Range,
                                                NevercBufferView *OutText);
  static NevercStatus NEVERC_CALL getPresumedLocation(
      void *Context, NevercTaskHandle Task, NevercSourceLocation Location,
      NevercPresumedLocation *OutLocation);
  static NevercStatus NEVERC_CALL getLocationFile(void *Context,
                                                  NevercTaskHandle Task,
                                                  NevercSourceLocation Location,
                                                  NevercFileHandle *OutFile);
  static NevercStatus NEVERC_CALL getIncludeLocation(
      void *Context, NevercTaskHandle Task, NevercSourceLocation Location,
      NevercSourceLocation *OutLocation);
  static NevercStatus NEVERC_CALL getFileInfo(void *Context,
                                              NevercTaskHandle Task,
                                              NevercFileHandle File,
                                              NevercFileInfo *OutInfo);
  static NevercStatus NEVERC_CALL
  getCharacterData(void *Context, NevercTaskHandle Task,
                   NevercSourceLocation Location, NevercBufferView *OutData);
  static NevercStatus NEVERC_CALL
  getTokenEnd(void *Context, NevercTaskHandle Task,
              NevercSourceLocation Location, NevercSourceLocation *OutLocation);
  static NevercStatus NEVERC_CALL getLocationInfoBatch(
      void *Context, NevercTaskHandle Task,
      const NevercSourceLocation *Locations, uint64_t LocationCount,
      NevercSourceLocationInfo *OutInfos, uint64_t OutInfoCapacity);
  NevercStatus resolveLocation(NevercTaskHandle Task,
                               NevercSourceLocation Location,
                               SourceLocation *OutLocation);
  NevercStatus resolveRange(NevercTaskHandle Task, NevercSourceRange Range,
                            CharSourceRange *OutRange);
  NevercStatus publishLocation(SourceLocation Location,
                               NevercSourceLocation *OutLocation);
  llvm::Expected<NevercFileHandle> createFile(FileID File);

  PluginTaskContext &Task;
  SourceManager &SourceMgr;
  const LangOptions *LanguageOptions = nullptr;
  NevercSourceLocationAPI SourceLocationAPI{};
};

class PluginASTBridge {
public:
  PluginASTBridge(PluginTaskContext &Task, TreeContext &Context,
                  FrontendPluginBridge &Locations,
                  PluginPrepBridge *Identifiers = nullptr);
  ~PluginASTBridge();

  PluginASTBridge(const PluginASTBridge &) = delete;
  PluginASTBridge &operator=(const PluginASTBridge &) = delete;

  llvm::Error attachProcessInterface();
  const NevercASTAPI &astAPI() const;
  NevercStatus resolvePublishedNode(NevercTaskHandle Task,
                                    NevercASTNodeHandle Node,
                                    NevercASTSchemaDomain ExpectedDomain,
                                    const void **OutNode);
  NevercStatus resolvePublishedType(NevercTaskHandle Task,
                                    NevercTypeHandle Type, QualType *OutType);
  llvm::Expected<NevercDeclHandle> publishDecl(const Decl *Declaration);
  llvm::Expected<NevercStmtHandle> publishStmt(const Stmt *Statement);
  llvm::Expected<NevercExprHandle> publishExpr(const Expr *Expression);
  llvm::Expected<NevercTypeHandle> publishType(QualType Type);

private:
  void detachProcessInterface();

  struct Impl;
  std::unique_ptr<Impl> State;
  bool AttachedToProcess = false;
};

class PluginSemaExtensionAPI {
public:
  virtual ~PluginSemaExtensionAPI() = default;

  virtual NevercStatus getExpressionExtensionInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaExpressionExtensionInput *OutInput) = 0;
  virtual NevercStatus createExpressionExtensionOutput(
      const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaExpressionExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) = 0;
  virtual NevercStatus getStatementExtensionInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaStatementExtensionInput *OutInput) = 0;
  virtual NevercStatus createStatementExtensionOutput(
      const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaStatementExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) = 0;
  virtual NevercStatus getDeclarationExtensionInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaDeclarationExtensionInput *OutInput) = 0;
  virtual NevercStatus createDeclarationExtensionOutput(
      const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaDeclarationExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) = 0;
  virtual NevercStatus getTypeExtensionInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaTypeExtensionInput *OutInput) = 0;
  virtual NevercStatus createTypeExtensionOutput(
      const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaTypeExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) = 0;
  virtual NevercStatus getLookupExtensionInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaLookupExtensionInput *OutInput) = 0;
  virtual NevercStatus createLookupExtensionOutput(
      const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaLookupExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) = 0;
  virtual NevercStatus getConversionExtensionInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaConversionExtensionInput *OutInput) = 0;
  virtual NevercStatus createConversionExtensionOutput(
      const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaConversionExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) = 0;
};

class PluginSemaBridge {
public:
  PluginSemaBridge(PluginTaskContext &Task, Sema &SemanticAnalyzer,
                   PluginASTBridge &AST, FrontendPluginBridge &Locations);
  ~PluginSemaBridge();

  PluginSemaBridge(const PluginSemaBridge &) = delete;
  PluginSemaBridge &operator=(const PluginSemaBridge &) = delete;

  llvm::Error attachProcessInterface();
  const NevercSemaAPI &semaAPI() const;
  void setExtensionAPI(PluginSemaExtensionAPI *ExtensionAPI);

private:
  void detachProcessInterface();

  struct Impl;
  std::unique_ptr<Impl> State;
  bool AttachedToProcess = false;
};

class PluginPrepBridge {
public:
  PluginPrepBridge(PluginTaskContext &Task, PrepEngine &Prep,
                   FrontendPluginBridge &Locations);
  ~PluginPrepBridge();

  PluginPrepBridge(const PluginPrepBridge &) = delete;
  PluginPrepBridge &operator=(const PluginPrepBridge &) = delete;

  llvm::Error attachProcessInterface();
  const NevercPrepAPI &prepAPI() const { return PrepAPI; }

  llvm::Expected<NevercTokenHandle> createToken(const Token &Value);
  llvm::Expected<NevercTokenStreamHandle>
  createTokenStream(llvm::ArrayRef<Token> Tokens);
  void attachTokenPhaseExecutor(PluginPhaseExecutor &Executor);
  PluginTaskContext &taskContext() const { return Task; }
  PluginPhaseExecutor *phaseExecutor() const { return TokenPhaseExecutor; }
  FrontendPluginBridge &locationBridge() const { return Locations; }
  NevercStatus resolvePublishedToken(NevercTaskHandle TaskHandle,
                                     NevercTokenHandle TokenHandle,
                                     const Token **OutToken) {
    return resolveToken(TaskHandle, TokenHandle, OutToken);
  }

private:
  friend class PluginASTBridge;
  friend class PluginPrepProcessBridge;
  friend class PluginPrepObserver;

  llvm::Expected<NevercIdentifierHandle>
  createIdentifier(IdentifierInfo *Identifier);
  llvm::Expected<NevercMacroDefinitionHandle>
  createMacroDefinition(IdentifierInfo *Name, MacroDirective *Directive,
                        SourceLocation UndefinitionLocation = {});
  llvm::Expected<NevercMacroDefinitionHandle>
  createMacroDefinition(IdentifierInfo *Name, MacroRecord *Record);
  llvm::Expected<NevercMacroDirectiveHandle>
  createMacroDirective(IdentifierInfo *Name, MacroDirective *Directive);
  llvm::Expected<NevercMacroArgumentHandle>
  createMacroArguments(const MacroArgStorage *Arguments);
  llvm::Expected<NevercTokenHandle>
  createTokenWithOrigin(const Token &Value, NevercTokenOriginKind Origin);
  llvm::Error ensureObserverAttached();
  NevercStatus dispatchEvent(const NevercPrepEvent &Event);

  NevercStatus resolveToken(NevercTaskHandle Task,
                            NevercTokenHandle TokenHandle,
                            const Token **OutToken);
  NevercStatus resolveIdentifier(NevercTaskHandle Task,
                                 NevercIdentifierHandle Identifier,
                                 IdentifierInfo **OutIdentifier);

  static NevercStatus NEVERC_CALL getTokenInfo(void *Context,
                                               NevercTaskHandle Task,
                                               NevercTokenHandle Token,
                                               NevercTokenInfo *OutInfo);
  static NevercStatus NEVERC_CALL getTokenInfoBatch(
      void *Context, NevercTaskHandle Task, const NevercTokenHandle *Tokens,
      uint64_t TokenCount, NevercTokenInfo *OutInfos, uint64_t OutInfoCapacity);
  static NevercStatus NEVERC_CALL getTokenStreamView(
      void *Context, NevercTaskHandle Task, NevercTokenStreamHandle Stream,
      NevercTokenViewList *OutView);
  static NevercStatus NEVERC_CALL getTokenStreamToken(
      void *Context, NevercTaskHandle Task, NevercTokenStreamHandle Stream,
      uint64_t Index, NevercTokenHandle *OutToken);
  static NevercStatus NEVERC_CALL getIdentifierInfo(
      void *Context, NevercTaskHandle Task, NevercIdentifierHandle Identifier,
      NevercIdentifierInfo *OutInfo);
  static NevercStatus NEVERC_CALL getOrCreateIdentifier(
      void *Context, NevercTaskHandle Task, NevercStringView Name,
      NevercIdentifierHandle *OutIdentifier);
  static NevercStatus NEVERC_CALL getMacroDefinitionForIdentifier(
      void *Context, NevercTaskHandle Task, NevercIdentifierHandle Identifier,
      NevercMacroDefinitionHandle *OutDefinition);
  static NevercStatus NEVERC_CALL
  getMacroDefinitionInfo(void *Context, NevercTaskHandle Task,
                         NevercMacroDefinitionHandle Definition,
                         NevercMacroDefinitionInfo *OutInfo);
  static NevercStatus NEVERC_CALL
  getMacroParameter(void *Context, NevercTaskHandle Task,
                    NevercMacroDefinitionHandle Definition, uint32_t Index,
                    NevercIdentifierHandle *OutParameter);
  static NevercStatus NEVERC_CALL
  getMacroReplacementToken(void *Context, NevercTaskHandle Task,
                           NevercMacroDefinitionHandle Definition,
                           uint32_t Index, NevercTokenHandle *OutToken);
  static NevercStatus NEVERC_CALL getMacroDirectiveInfo(
      void *Context, NevercTaskHandle Task,
      NevercMacroDirectiveHandle Directive, NevercMacroDirectiveInfo *OutInfo);
  static NevercStatus NEVERC_CALL getMacroArgumentInfo(
      void *Context, NevercTaskHandle Task, NevercMacroArgumentHandle Arguments,
      NevercMacroArgumentInfo *OutInfo);
  static NevercStatus NEVERC_CALL getMacroArgumentTokenStream(
      void *Context, NevercTaskHandle Task, NevercMacroArgumentHandle Arguments,
      uint32_t Index, NevercTokenStreamHandle *OutStream);
  static NevercStatus NEVERC_CALL
  createTokenBuilder(void *Context, NevercTaskHandle Task,
                     NevercTokenBuilderHandle *OutBuilder);
  static NevercStatus NEVERC_CALL
  tokenBuilderSetKind(void *Context, NevercTaskHandle Task,
                      NevercTokenBuilderHandle Builder, NevercTokenKind Kind);
  static NevercStatus NEVERC_CALL tokenBuilderSetIdentifier(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder,
      NevercIdentifierHandle Identifier);
  static NevercStatus NEVERC_CALL tokenBuilderSetLiteral(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder,
      NevercTokenKind Kind, NevercStringView Spelling);
  static NevercStatus NEVERC_CALL tokenBuilderSetLocation(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder,
      NevercSourceLocation Location);
  static NevercStatus NEVERC_CALL tokenBuilderSetFlags(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder,
      NevercTokenFlags Flags);
  static NevercStatus NEVERC_CALL tokenBuilderCommit(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder,
      NevercTokenHandle *OutToken);
  static NevercStatus NEVERC_CALL destroyTokenBuilder(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder);
  static NevercStatus NEVERC_CALL
  registerEventObserver(void *Context, NevercTaskHandle Task,
                        const NevercPrepObserverDescriptor *Descriptor);
  static NevercStatus NEVERC_CALL
  getTokenPhaseInput(void *Context, const NevercPhaseFrame *Frame,
                     NevercArtifactHandle Input, NevercTokenHandle *OutToken);
  static NevercStatus NEVERC_CALL
  createTokenPhaseOutput(void *Context, const NevercPhaseFrame *Frame,
                         const NevercPhaseContinuation *Continuation,
                         const NevercTokenHandle *Tokens, uint64_t TokenCount,
                         NevercArtifactHandle *OutOutput);
  static NevercStatus NEVERC_CALL getTokenStreamPhaseInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercPrepTokenStreamPhaseInput *OutInput);
  static NevercStatus NEVERC_CALL
  createTokenStreamBuilder(void *Context, NevercTaskHandle Task,
                           NevercTokenStreamBuilderHandle *OutBuilder);
  static NevercStatus NEVERC_CALL tokenStreamBuilderAppend(
      void *Context, NevercTaskHandle Task,
      NevercTokenStreamBuilderHandle Builder, const NevercTokenHandle *Tokens,
      uint64_t TokenCount);
  static NevercStatus NEVERC_CALL tokenStreamBuilderCommit(
      void *Context, const NevercPhaseFrame *Frame,
      NevercTokenStreamBuilderHandle Builder, NevercArtifactHandle *OutOutput);
  static NevercStatus NEVERC_CALL
  destroyTokenStreamBuilder(void *Context, NevercTaskHandle Task,
                            NevercTokenStreamBuilderHandle Builder);
  static NevercStatus NEVERC_CALL getIncludePhaseInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercPrepIncludePhaseInput *OutInput);
  static NevercStatus NEVERC_CALL
  createIncludePhaseOutput(void *Context, const NevercPhaseFrame *Frame,
                           const NevercPhaseContinuation *Continuation,
                           const NevercPrepIncludePhaseOutput *Output,
                           NevercArtifactHandle *OutOutput);
  static NevercStatus NEVERC_CALL getMacroPhaseInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercPrepMacroPhaseInput *OutInput);
  static NevercStatus NEVERC_CALL
  createMacroPhaseOutput(void *Context, const NevercPhaseFrame *Frame,
                         const NevercPhaseContinuation *Continuation,
                         const NevercPrepMacroPhaseOutput *Output,
                         NevercArtifactHandle *OutOutput);
  static NevercStatus NEVERC_CALL getPragmaPhaseInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercPrepPragmaPhaseInput *OutInput);
  static NevercStatus NEVERC_CALL
  createPragmaPhaseOutput(void *Context, const NevercPhaseFrame *Frame,
                          const NevercPhaseContinuation *Continuation,
                          const NevercPrepPragmaPhaseOutput *Output,
                          NevercArtifactHandle *OutOutput);
  static NevercStatus NEVERC_CALL getFeatureQueryPhaseInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercPrepFeatureQueryPhaseInput *OutInput);
  static NevercStatus NEVERC_CALL
  createFeatureQueryPhaseOutput(void *Context, const NevercPhaseFrame *Frame,
                                const NevercPhaseContinuation *Continuation,
                                const NevercPrepFeatureQueryPhaseOutput *Output,
                                NevercArtifactHandle *OutOutput);

  void detachProcessInterface();

  PluginTaskContext &Task;
  PrepEngine &Prep;
  FrontendPluginBridge &Locations;
  PluginPhaseExecutor *TokenPhaseExecutor = nullptr;
  NevercPrepAPI PrepAPI{};
  bool AttachedToProcess = false;
  bool ObserverAttached = false;
};

struct PluginSourceInput {
  llvm::StringRef Path;
  llvm::ArrayRef<uint8_t> Buffer;
  uint32_t Language = 0;
  bool HasBuffer = false;
  bool System = false;
  bool Preprocessed = false;
};

class PluginSourcePhaseRuntime {
public:
  using BuiltinOpen = std::function<llvm::Error()>;
  struct Impl;

  static llvm::Expected<std::unique_ptr<PluginSourcePhaseRuntime>>
  create(PluginTaskContext &Task, SourceManager &SourceMgr,
         const LangOptions &LangOpts);
  ~PluginSourcePhaseRuntime();

  PluginSourcePhaseRuntime(const PluginSourcePhaseRuntime &) = delete;
  PluginSourcePhaseRuntime &
  operator=(const PluginSourcePhaseRuntime &) = delete;

  llvm::Error initialize(const PluginSourceInput &Input,
                         BuiltinOpen OpenBuiltin);
  llvm::Error attachPrepEngine(PrepEngine &Prep);
  llvm::Error attachTreeContext(TreeContext &Context);
  llvm::Error attachSema(Sema &SemanticAnalyzer);
  llvm::Error runParserPhase(Sema &SemanticAnalyzer, bool PrintStats);
  ParserPluginHooks *parserPluginHooks() const;

private:
  explicit PluginSourcePhaseRuntime(std::unique_ptr<Impl> State);

  std::unique_ptr<Impl> State;
};

llvm::Error registerPluginFrontendInterface(PluginProcessServices &Services);
llvm::Error registerPluginPrepInterface(PluginProcessServices &Services);
llvm::Error registerPluginSemaInterface(PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
