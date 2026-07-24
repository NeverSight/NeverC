/*===-- PluginPrep.h - NeverC preprocessor plugin C ABI ------------ C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINPREP_H
#define NEVERC_PLUGIN_PLUGINPREP_H

#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginPhaseSchema.h" /* IWYU pragma: export */
#include "neverc/Plugin/PluginSource.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_PREP_API_MAJOR UINT16_C(1)
#define NEVERC_PREP_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_PREP_HIGH UINT64_C(0x4e43505052450001)
#define NEVERC_INTERFACE_PREP_LOW UINT64_C(0x0000000000000001)

typedef uint32_t NevercTokenKind;
typedef uint32_t NevercPPKeywordKind;
typedef uint32_t NevercTokenCategory;

#define NEVERC_TOKEN_CATEGORY_SPECIAL UINT32_C(0)
#define NEVERC_TOKEN_CATEGORY_COMMENT UINT32_C(1)
#define NEVERC_TOKEN_CATEGORY_IDENTIFIER UINT32_C(2)
#define NEVERC_TOKEN_CATEGORY_LITERAL UINT32_C(3)
#define NEVERC_TOKEN_CATEGORY_PUNCTUATOR UINT32_C(4)
#define NEVERC_TOKEN_CATEGORY_KEYWORD UINT32_C(5)
#define NEVERC_TOKEN_CATEGORY_ANNOTATION UINT32_C(6)

#include "neverc/Plugin/Schema/PluginPrepSchema.inc"

#if NEVERC_PREP_SCHEMA_CAPABILITY_MAJOR != NEVERC_PREP_API_MAJOR
#error "Prep schema capability major must match the Prep API major"
#endif

NEVERC_ABI_PACK_BEGIN

typedef NevercHandle NevercTokenHandle;
typedef NevercHandle NevercTokenStreamHandle;
typedef NevercHandle NevercIdentifierHandle;
typedef NevercHandle NevercMacroDefinitionHandle;
typedef NevercHandle NevercMacroDirectiveHandle;
typedef NevercHandle NevercMacroArgumentHandle;
typedef NevercHandle NevercPragmaHandle;
typedef NevercHandle NevercTokenBuilderHandle;
typedef NevercHandle NevercTokenStreamBuilderHandle;

#define NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS UINT64_C(16777216)

typedef uint32_t NevercTokenFlags;

#define NEVERC_TOKEN_FLAG_START_OF_LINE UINT32_C(0x00000001)
#define NEVERC_TOKEN_FLAG_LEADING_SPACE UINT32_C(0x00000002)
#define NEVERC_TOKEN_FLAG_DISABLE_EXPANSION UINT32_C(0x00000004)
#define NEVERC_TOKEN_FLAG_NEEDS_CLEANING UINT32_C(0x00000008)
#define NEVERC_TOKEN_FLAG_LEADING_EMPTY_MACRO UINT32_C(0x00000010)
#define NEVERC_TOKEN_FLAG_HAS_UCN UINT32_C(0x00000020)
#define NEVERC_TOKEN_FLAG_IGNORED_COMMA UINT32_C(0x00000040)
#define NEVERC_TOKEN_FLAG_STRINGIFIED_IN_MACRO UINT32_C(0x00000080)
#define NEVERC_TOKEN_FLAG_COMMA_AFTER_ELIDED UINT32_C(0x00000100)
#define NEVERC_TOKEN_FLAG_REINJECTED UINT32_C(0x00000200)
#define NEVERC_TOKEN_FLAG_ALL UINT32_C(0x000003ff)

typedef uint32_t NevercTokenOriginKind;

#define NEVERC_TOKEN_ORIGIN_FILE UINT32_C(0)
#define NEVERC_TOKEN_ORIGIN_MACRO_REPLACEMENT UINT32_C(1)
#define NEVERC_TOKEN_ORIGIN_MACRO_ARGUMENT UINT32_C(2)
#define NEVERC_TOKEN_ORIGIN_SYNTHESIZED UINT32_C(3)

typedef uint32_t NevercIdentifierFlags;

#define NEVERC_IDENTIFIER_KEYWORD UINT32_C(0x00000001)
#define NEVERC_IDENTIFIER_PP_KEYWORD UINT32_C(0x00000002)
#define NEVERC_IDENTIFIER_BUILTIN UINT32_C(0x00000004)
#define NEVERC_IDENTIFIER_HAS_MACRO UINT32_C(0x00000008)
#define NEVERC_IDENTIFIER_POISONED UINT32_C(0x00000010)
#define NEVERC_IDENTIFIER_EXTENSION_TOKEN UINT32_C(0x00000020)
#define NEVERC_IDENTIFIER_FUTURE_COMPAT_KEYWORD UINT32_C(0x00000040)
#define NEVERC_IDENTIFIER_RESERVED UINT32_C(0x00000080)

typedef uint32_t NevercMacroFlags;

#define NEVERC_MACRO_FUNCTION_LIKE UINT32_C(0x00000001)
#define NEVERC_MACRO_VARIADIC UINT32_C(0x00000002)
#define NEVERC_MACRO_C99_VARIADIC UINT32_C(0x00000004)
#define NEVERC_MACRO_GNU_VARIADIC UINT32_C(0x00000008)
#define NEVERC_MACRO_HAS_VA_OPT UINT32_C(0x00000010)
#define NEVERC_MACRO_BUILTIN UINT32_C(0x00000020)
#define NEVERC_MACRO_COMMA_PASTING UINT32_C(0x00000040)

typedef uint32_t NevercMacroDirectiveKind;

#define NEVERC_MACRO_DIRECTIVE_DEFINE UINT32_C(1)
#define NEVERC_MACRO_DIRECTIVE_UNDEFINE UINT32_C(2)

typedef uint32_t NevercPrepEventKind;
typedef uint64_t NevercPrepEventMask;

#define NEVERC_PREP_EVENT_FILE_CHANGED UINT32_C(1)
#define NEVERC_PREP_EVENT_LEXED_FILE_CHANGED UINT32_C(2)
#define NEVERC_PREP_EVENT_FILE_SKIPPED UINT32_C(3)
#define NEVERC_PREP_EVENT_FILE_NOT_FOUND UINT32_C(4)
#define NEVERC_PREP_EVENT_INCLUSION_DIRECTIVE UINT32_C(5)
#define NEVERC_PREP_EVENT_END_OF_MAIN_FILE UINT32_C(6)
#define NEVERC_PREP_EVENT_IDENT UINT32_C(7)
#define NEVERC_PREP_EVENT_PRAGMA_DIRECTIVE UINT32_C(8)
#define NEVERC_PREP_EVENT_PRAGMA_COMMENT UINT32_C(9)
#define NEVERC_PREP_EVENT_PRAGMA_MARK UINT32_C(10)
#define NEVERC_PREP_EVENT_PRAGMA_DETECT_MISMATCH UINT32_C(11)
#define NEVERC_PREP_EVENT_PRAGMA_DEBUG UINT32_C(12)
#define NEVERC_PREP_EVENT_PRAGMA_MESSAGE UINT32_C(13)
#define NEVERC_PREP_EVENT_PRAGMA_DIAGNOSTIC_PUSH UINT32_C(14)
#define NEVERC_PREP_EVENT_PRAGMA_DIAGNOSTIC_POP UINT32_C(15)
#define NEVERC_PREP_EVENT_PRAGMA_DIAGNOSTIC UINT32_C(16)
#define NEVERC_PREP_EVENT_PRAGMA_WARNING UINT32_C(17)
#define NEVERC_PREP_EVENT_PRAGMA_WARNING_PUSH UINT32_C(18)
#define NEVERC_PREP_EVENT_PRAGMA_WARNING_POP UINT32_C(19)
#define NEVERC_PREP_EVENT_PRAGMA_EXEC_CHARSET_PUSH UINT32_C(20)
#define NEVERC_PREP_EVENT_PRAGMA_EXEC_CHARSET_POP UINT32_C(21)
#define NEVERC_PREP_EVENT_PRAGMA_ASSUME_NONNULL_BEGIN UINT32_C(22)
#define NEVERC_PREP_EVENT_PRAGMA_ASSUME_NONNULL_END UINT32_C(23)
#define NEVERC_PREP_EVENT_MACRO_EXPANDS UINT32_C(24)
#define NEVERC_PREP_EVENT_MACRO_DEFINED UINT32_C(25)
#define NEVERC_PREP_EVENT_MACRO_UNDEFINED UINT32_C(26)
#define NEVERC_PREP_EVENT_DEFINED UINT32_C(27)
#define NEVERC_PREP_EVENT_HAS_INCLUDE UINT32_C(28)
#define NEVERC_PREP_EVENT_SOURCE_RANGE_SKIPPED UINT32_C(29)
#define NEVERC_PREP_EVENT_IF UINT32_C(30)
#define NEVERC_PREP_EVENT_ELIF UINT32_C(31)
#define NEVERC_PREP_EVENT_IFDEF UINT32_C(32)
#define NEVERC_PREP_EVENT_ELIFDEF UINT32_C(33)
#define NEVERC_PREP_EVENT_ELIFDEF_SKIPPED UINT32_C(34)
#define NEVERC_PREP_EVENT_IFNDEF UINT32_C(35)
#define NEVERC_PREP_EVENT_ELIFNDEF UINT32_C(36)
#define NEVERC_PREP_EVENT_ELIFNDEF_SKIPPED UINT32_C(37)
#define NEVERC_PREP_EVENT_ELSE UINT32_C(38)
#define NEVERC_PREP_EVENT_ENDIF UINT32_C(39)
#define NEVERC_PREP_EVENT_COUNT UINT32_C(39)

#define NEVERC_PREP_EVENT_MASK(EventKind)                                      \
  (UINT64_C(1) << ((EventKind) - UINT32_C(1)))
#define NEVERC_PREP_EVENT_MASK_ALL ((UINT64_C(1) << 39) - UINT64_C(1))

typedef uint32_t NevercPrepFileChangeReason;
#define NEVERC_PREP_FILE_ENTER UINT32_C(1)
#define NEVERC_PREP_FILE_EXIT UINT32_C(2)
#define NEVERC_PREP_FILE_SYSTEM_HEADER_PRAGMA UINT32_C(3)
#define NEVERC_PREP_FILE_RENAME UINT32_C(4)

typedef uint32_t NevercPrepPragmaIntroducer;
#define NEVERC_PREP_PRAGMA_HASH UINT32_C(1)
#define NEVERC_PREP_PRAGMA_OPERATOR UINT32_C(2)
#define NEVERC_PREP_PRAGMA_MS UINT32_C(3)

typedef uint32_t NevercPrepPragmaMessageKind;
#define NEVERC_PREP_PRAGMA_MESSAGE_NOTE UINT32_C(1)
#define NEVERC_PREP_PRAGMA_MESSAGE_WARNING UINT32_C(2)
#define NEVERC_PREP_PRAGMA_MESSAGE_ERROR UINT32_C(3)

typedef uint32_t NevercPrepDiagnosticSeverity;
#define NEVERC_PREP_DIAGNOSTIC_IGNORED UINT32_C(1)
#define NEVERC_PREP_DIAGNOSTIC_REMARK UINT32_C(2)
#define NEVERC_PREP_DIAGNOSTIC_WARNING UINT32_C(3)
#define NEVERC_PREP_DIAGNOSTIC_ERROR UINT32_C(4)
#define NEVERC_PREP_DIAGNOSTIC_FATAL UINT32_C(5)

typedef uint32_t NevercPrepWarningSpecifier;
#define NEVERC_PREP_WARNING_DEFAULT UINT32_C(1)
#define NEVERC_PREP_WARNING_DISABLE UINT32_C(2)
#define NEVERC_PREP_WARNING_ERROR UINT32_C(3)
#define NEVERC_PREP_WARNING_ONCE UINT32_C(4)
#define NEVERC_PREP_WARNING_SUPPRESS UINT32_C(5)
#define NEVERC_PREP_WARNING_LEVEL1 UINT32_C(6)
#define NEVERC_PREP_WARNING_LEVEL2 UINT32_C(7)
#define NEVERC_PREP_WARNING_LEVEL3 UINT32_C(8)
#define NEVERC_PREP_WARNING_LEVEL4 UINT32_C(9)

typedef uint32_t NevercPrepConditionValue;
#define NEVERC_PREP_CONDITION_NOT_EVALUATED UINT32_C(0)
#define NEVERC_PREP_CONDITION_FALSE UINT32_C(1)
#define NEVERC_PREP_CONDITION_TRUE UINT32_C(2)

typedef uint32_t NevercPrepIncludeAction;
#define NEVERC_PREP_INCLUDE_CONTINUE UINT32_C(0)
#define NEVERC_PREP_INCLUDE_SKIP UINT32_C(1)
#define NEVERC_PREP_INCLUDE_REDIRECT UINT32_C(2)

typedef uint32_t NevercPrepMacroOperation;
#define NEVERC_PREP_MACRO_DEFINE UINT32_C(1)
#define NEVERC_PREP_MACRO_UNDEFINE UINT32_C(2)
#define NEVERC_PREP_MACRO_EXPAND UINT32_C(3)
#define NEVERC_PREP_MACRO_EXPAND_BUILTIN UINT32_C(4)

typedef uint32_t NevercPrepMacroAction;
#define NEVERC_PREP_MACRO_CONTINUE UINT32_C(0)
#define NEVERC_PREP_MACRO_SUPPRESS UINT32_C(1)
#define NEVERC_PREP_MACRO_REPLACE_TOKENS UINT32_C(2)

typedef uint32_t NevercPrepPragmaAction;
#define NEVERC_PREP_PRAGMA_CONTINUE UINT32_C(0)
#define NEVERC_PREP_PRAGMA_HANDLED UINT32_C(1)
#define NEVERC_PREP_PRAGMA_REPLACE_TOKENS UINT32_C(2)

typedef uint32_t NevercPrepFeatureQueryKind;
#define NEVERC_PREP_QUERY_HAS_FEATURE UINT32_C(1)
#define NEVERC_PREP_QUERY_HAS_EXTENSION UINT32_C(2)
#define NEVERC_PREP_QUERY_HAS_BUILTIN UINT32_C(3)
#define NEVERC_PREP_QUERY_HAS_INCLUDE UINT32_C(4)
#define NEVERC_PREP_QUERY_HAS_INCLUDE_NEXT UINT32_C(5)

typedef uint32_t NevercPrepFeatureQueryAction;
#define NEVERC_PREP_QUERY_CONTINUE UINT32_C(0)
#define NEVERC_PREP_QUERY_REPLACE UINT32_C(1)

typedef struct NevercTokenInfo {
  NevercABITableHeader Header;
  NevercTokenKind Kind;
  NevercTokenFlags Flags;
  NevercTokenOriginKind Origin;
  uint32_t Reserved;
  NevercStringView Spelling;
  NevercSourceLocation Location;
  NevercSourceRange Range;
  NevercIdentifierHandle Identifier;
  NevercMacroDefinitionHandle MacroDefinition;
} NevercTokenInfo;

typedef struct NevercTokenView {
  NevercTokenKind Kind;
  NevercTokenFlags Flags;
  NevercTokenOriginKind Origin;
  uint32_t Reserved;
  NevercStringView Spelling;
} NevercTokenView;

typedef struct NevercTokenViewList {
  NevercABITableHeader Header;
  const NevercTokenView *Data;
  uint64_t Count;
  uint64_t Stride;
} NevercTokenViewList;

typedef struct NevercIdentifierInfo {
  NevercABITableHeader Header;
  NevercStringView Name;
  NevercTokenKind TokenKind;
  NevercPPKeywordKind PPKeywordKind;
  uint32_t BuiltinID;
  NevercIdentifierFlags Flags;
} NevercIdentifierInfo;

typedef struct NevercMacroDefinitionInfo {
  NevercABITableHeader Header;
  NevercIdentifierHandle Name;
  NevercMacroDirectiveHandle Directive;
  NevercSourceLocation DefinitionLocation;
  NevercSourceLocation DefinitionEndLocation;
  NevercSourceLocation UndefinitionLocation;
  uint32_t ParameterCount;
  uint32_t ReplacementTokenCount;
  NevercMacroFlags Flags;
  uint32_t Reserved;
} NevercMacroDefinitionInfo;

typedef struct NevercMacroDirectiveInfo {
  NevercABITableHeader Header;
  NevercMacroDirectiveKind Kind;
  uint32_t Reserved;
  NevercSourceLocation Location;
  NevercMacroDirectiveHandle Previous;
  NevercMacroDefinitionHandle Definition;
} NevercMacroDirectiveInfo;

typedef struct NevercMacroArgumentInfo {
  NevercABITableHeader Header;
  uint32_t ArgumentCount;
  NevercBool VarargsElided;
  uint8_t Reserved[3];
} NevercMacroArgumentInfo;

typedef struct NevercPrepFileEvent {
  NevercSourceLocation Location;
  NevercFileHandle File;
  NevercFileHandle PreviousFile;
  NevercTokenHandle FilenameToken;
  NevercPrepFileChangeReason Reason;
  NevercFileCharacteristic Characteristic;
} NevercPrepFileEvent;

typedef struct NevercPrepIncludeEvent {
  NevercSourceLocation Location;
  NevercTokenHandle IncludeToken;
  NevercSourceRange FilenameRange;
  NevercFileHandle File;
  NevercStringView Filename;
  NevercStringView IncludeSearchPath;
  NevercStringView RelativePath;
  NevercBool IsAngled;
  NevercFileCharacteristic Characteristic;
  uint16_t Reserved;
} NevercPrepIncludeEvent;

typedef struct NevercPrepTextEvent {
  NevercSourceLocation Location;
  NevercIdentifierHandle Identifier;
  NevercStringView Name;
  NevercStringView Value;
  const int32_t *Integers;
  uint64_t IntegerCount;
  uint32_t Detail;
  int32_t IntegerValue;
} NevercPrepTextEvent;

typedef struct NevercPrepMacroEvent {
  NevercTokenHandle NameToken;
  NevercMacroDefinitionHandle Definition;
  NevercMacroDirectiveHandle Directive;
  NevercMacroArgumentHandle Arguments;
  NevercSourceRange Range;
} NevercPrepMacroEvent;

typedef struct NevercPrepConditionEvent {
  NevercSourceLocation Location;
  NevercSourceLocation IfLocation;
  NevercSourceRange Range;
  NevercTokenHandle MacroNameToken;
  NevercMacroDefinitionHandle Definition;
  NevercPrepConditionValue Value;
  uint32_t Reserved;
} NevercPrepConditionEvent;

typedef union NevercPrepEventPayload {
  NevercPrepFileEvent File;
  NevercPrepIncludeEvent Include;
  NevercPrepTextEvent Text;
  NevercPrepMacroEvent Macro;
  NevercPrepConditionEvent Condition;
} NevercPrepEventPayload;

typedef struct NevercPrepEvent {
  NevercABITableHeader Header;
  NevercPrepEventKind Kind;
  uint32_t Reserved;
  NevercPrepEventPayload Payload;
} NevercPrepEvent;

/*
 * Event records and string/integer views are borrowed for the callback.
 * Handles published in an event are promoted to the enclosing task scope.
 */
typedef NevercStatus(NEVERC_CALL *NevercPrepEventObserverFn)(
    NevercTaskHandle Task, const NevercPrepEvent *Event, void *UserData);

typedef struct NevercPrepObserverDescriptor {
  NevercABITableHeader Header;
  NevercPrepEventMask Events;
  NevercPrepEventObserverFn Callback;
  void *UserData;
} NevercPrepObserverDescriptor;

typedef struct NevercPrepIncludePhaseInput {
  NevercABITableHeader Header;
  NevercSourceLocation Location;
  NevercTokenHandle IncludeToken;
  NevercStringView Filename;
  NevercStringView IncludeSearchPath;
  NevercStringView RelativePath;
  NevercPrepIncludeAction Action;
  NevercBool IsAngled;
  NevercBool IsImport;
  NevercBool IsIncludeNext;
  NevercBool ReplacementIsAngled;
  NevercStringView ReplacementFilename;
} NevercPrepIncludePhaseInput;

typedef struct NevercPrepIncludePhaseOutput {
  NevercABITableHeader Header;
  NevercPrepIncludeAction Action;
  NevercBool IsAngled;
  uint8_t Reserved[3];
  NevercStringView Filename;
} NevercPrepIncludePhaseOutput;

typedef struct NevercPrepMacroPhaseInput {
  NevercABITableHeader Header;
  NevercPrepMacroOperation Operation;
  NevercPrepMacroAction Action;
  NevercTokenHandle NameToken;
  NevercIdentifierHandle Name;
  NevercMacroDefinitionHandle Definition;
  NevercMacroArgumentHandle Arguments;
  NevercTokenStreamHandle ReplacementTokens;
} NevercPrepMacroPhaseInput;

typedef struct NevercPrepMacroPhaseOutput {
  NevercABITableHeader Header;
  NevercPrepMacroAction Action;
  uint32_t Reserved;
  const NevercTokenHandle *Tokens;
  uint64_t TokenCount;
} NevercPrepMacroPhaseOutput;

typedef struct NevercPrepPragmaPhaseInput {
  NevercABITableHeader Header;
  NevercSourceLocation Location;
  NevercPrepPragmaIntroducer Introducer;
  NevercPrepPragmaAction Action;
  NevercStringView Namespace;
  NevercStringView Name;
  NevercTokenStreamHandle Tokens;
  NevercTokenStreamHandle ReplacementTokens;
} NevercPrepPragmaPhaseInput;

typedef struct NevercPrepPragmaPhaseOutput {
  NevercABITableHeader Header;
  NevercPrepPragmaAction Action;
  uint32_t Reserved;
  const NevercTokenHandle *Tokens;
  uint64_t TokenCount;
} NevercPrepPragmaPhaseOutput;

typedef struct NevercPrepFeatureQueryPhaseInput {
  NevercABITableHeader Header;
  NevercSourceLocation Location;
  NevercPrepFeatureQueryKind Kind;
  NevercPrepFeatureQueryAction Action;
  NevercStringView Name;
  NevercBool BuiltinValue;
  NevercBool Value;
  uint8_t Reserved[2];
} NevercPrepFeatureQueryPhaseInput;

typedef struct NevercPrepFeatureQueryPhaseOutput {
  NevercABITableHeader Header;
  NevercPrepFeatureQueryAction Action;
  NevercBool Value;
  uint8_t Reserved[3];
} NevercPrepFeatureQueryPhaseOutput;

typedef struct NevercPrepTokenStreamPhaseInput {
  NevercABITableHeader Header;
  NevercSourceLocation StartLocation;
  NevercSourceLocation EndLocation;
  uint64_t MaximumTokenCount;
} NevercPrepTokenStreamPhaseInput;

typedef struct NevercPrepAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetTokenInfo)(void *Context, NevercTaskHandle Task,
                                          NevercTokenHandle Token,
                                          NevercTokenInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetTokenInfoBatch)(
      void *Context, NevercTaskHandle Task, const NevercTokenHandle *Tokens,
      uint64_t TokenCount, NevercTokenInfo *OutInfos, uint64_t OutInfoCapacity);
  NevercStatus(NEVERC_CALL *GetTokenStreamView)(void *Context,
                                                NevercTaskHandle Task,
                                                NevercTokenStreamHandle Stream,
                                                NevercTokenViewList *OutView);
  NevercStatus(NEVERC_CALL *GetTokenStreamToken)(void *Context,
                                                 NevercTaskHandle Task,
                                                 NevercTokenStreamHandle Stream,
                                                 uint64_t Index,
                                                 NevercTokenHandle *OutToken);
  NevercStatus(NEVERC_CALL *GetIdentifierInfo)(
      void *Context, NevercTaskHandle Task, NevercIdentifierHandle Identifier,
      NevercIdentifierInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetOrCreateIdentifier)(
      void *Context, NevercTaskHandle Task, NevercStringView Name,
      NevercIdentifierHandle *OutIdentifier);
  NevercStatus(NEVERC_CALL *GetMacroDefinitionForIdentifier)(
      void *Context, NevercTaskHandle Task, NevercIdentifierHandle Identifier,
      NevercMacroDefinitionHandle *OutDefinition);
  NevercStatus(NEVERC_CALL *GetMacroDefinitionInfo)(
      void *Context, NevercTaskHandle Task,
      NevercMacroDefinitionHandle Definition,
      NevercMacroDefinitionInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetMacroParameter)(
      void *Context, NevercTaskHandle Task,
      NevercMacroDefinitionHandle Definition, uint32_t Index,
      NevercIdentifierHandle *OutParameter);
  NevercStatus(NEVERC_CALL *GetMacroReplacementToken)(
      void *Context, NevercTaskHandle Task,
      NevercMacroDefinitionHandle Definition, uint32_t Index,
      NevercTokenHandle *OutToken);
  NevercStatus(NEVERC_CALL *GetMacroDirectiveInfo)(
      void *Context, NevercTaskHandle Task,
      NevercMacroDirectiveHandle Directive, NevercMacroDirectiveInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetMacroArgumentInfo)(
      void *Context, NevercTaskHandle Task, NevercMacroArgumentHandle Arguments,
      NevercMacroArgumentInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetMacroArgumentTokenStream)(
      void *Context, NevercTaskHandle Task, NevercMacroArgumentHandle Arguments,
      uint32_t Index, NevercTokenStreamHandle *OutStream);
  NevercStatus(NEVERC_CALL *CreateTokenBuilder)(
      void *Context, NevercTaskHandle Task,
      NevercTokenBuilderHandle *OutBuilder);
  NevercStatus(NEVERC_CALL *TokenBuilderSetKind)(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder,
      NevercTokenKind Kind);
  NevercStatus(NEVERC_CALL *TokenBuilderSetIdentifier)(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder,
      NevercIdentifierHandle Identifier);
  NevercStatus(NEVERC_CALL *TokenBuilderSetLiteral)(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder,
      NevercTokenKind Kind, NevercStringView Spelling);
  NevercStatus(NEVERC_CALL *TokenBuilderSetLocation)(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder,
      NevercSourceLocation Location);
  NevercStatus(NEVERC_CALL *TokenBuilderSetFlags)(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder,
      NevercTokenFlags Flags);
  NevercStatus(NEVERC_CALL *TokenBuilderCommit)(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder,
      NevercTokenHandle *OutToken);
  NevercStatus(NEVERC_CALL *DestroyTokenBuilder)(
      void *Context, NevercTaskHandle Task, NevercTokenBuilderHandle Builder);
  NevercStatus(NEVERC_CALL *RegisterEventObserver)(
      void *Context, NevercTaskHandle Task,
      const NevercPrepObserverDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *GetTokenPhaseInput)(void *Context,
                                                const NevercPhaseFrame *Frame,
                                                NevercArtifactHandle Input,
                                                NevercTokenHandle *OutToken);
  NevercStatus(NEVERC_CALL *CreateTokenPhaseOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercTokenHandle *Tokens, uint64_t TokenCount,
      NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *GetIncludePhaseInput)(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercPrepIncludePhaseInput *OutInput);
  NevercStatus(NEVERC_CALL *CreateIncludePhaseOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercPrepIncludePhaseOutput *Output,
      NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *GetMacroPhaseInput)(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercPrepMacroPhaseInput *OutInput);
  NevercStatus(NEVERC_CALL *CreateMacroPhaseOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercPrepMacroPhaseOutput *Output,
      NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *GetPragmaPhaseInput)(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercPrepPragmaPhaseInput *OutInput);
  NevercStatus(NEVERC_CALL *CreatePragmaPhaseOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercPrepPragmaPhaseOutput *Output,
      NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *GetFeatureQueryPhaseInput)(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercPrepFeatureQueryPhaseInput *OutInput);
  NevercStatus(NEVERC_CALL *CreateFeatureQueryPhaseOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercPrepFeatureQueryPhaseOutput *Output,
      NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *GetTokenStreamPhaseInput)(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercPrepTokenStreamPhaseInput *OutInput);
  NevercStatus(NEVERC_CALL *CreateTokenStreamBuilder)(
      void *Context, NevercTaskHandle Task,
      NevercTokenStreamBuilderHandle *OutBuilder);
  NevercStatus(NEVERC_CALL *TokenStreamBuilderAppend)(
      void *Context, NevercTaskHandle Task,
      NevercTokenStreamBuilderHandle Builder, const NevercTokenHandle *Tokens,
      uint64_t TokenCount);
  NevercStatus(NEVERC_CALL *TokenStreamBuilderCommit)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercTokenStreamBuilderHandle Builder, NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *DestroyTokenStreamBuilder)(
      void *Context, NevercTaskHandle Task,
      NevercTokenStreamBuilderHandle Builder);
} NevercPrepAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
}
#endif

#endif
