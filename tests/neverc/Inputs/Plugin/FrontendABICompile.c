#pragma pack(push, 1)
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include "neverc/Plugin/PluginPrep.h"
#include "neverc/Plugin/PluginSema.h"
#include "neverc/Plugin/PluginSource.h"

typedef struct NevercFrontendCallerPackProbe {
  uint8_t Prefix;
  uint64_t Value;
} NevercFrontendCallerPackProbe;
#pragma pack(pop)

_Static_assert(NEVERC_SOURCE_API_MAJOR == 1,
               "source ABI must start at major one");
_Static_assert(NEVERC_PREP_API_MAJOR == 1,
               "preprocessor ABI must start at major one");
_Static_assert(NEVERC_AST_API_MAJOR == 1, "AST ABI must start at major one");
_Static_assert(NEVERC_PARSER_API_MAJOR == 1,
               "Parser ABI must start at major one");
_Static_assert(NEVERC_SEMA_API_MAJOR == 1, "Sema ABI must start at major one");
_Static_assert(NEVERC_PREP_SCHEMA_CAPABILITY_MAJOR == NEVERC_PREP_API_MAJOR,
               "Prep schema must track the Prep ABI major");
_Static_assert(NEVERC_TOKEN_KIND_COUNT > 0,
               "Prep schema must publish token kinds");
_Static_assert(NEVERC_PP_KEYWORD_COUNT > 0,
               "Prep schema must publish preprocessor keywords");
_Static_assert(sizeof(NevercSourceLocation) == sizeof(NevercHandle),
               "source location must remain opaque");
_Static_assert(sizeof(NevercTokenHandle) == sizeof(NevercHandle),
               "token must remain opaque");
_Static_assert(sizeof(NevercTokenStreamBuilderHandle) == sizeof(NevercHandle),
               "token stream builder must remain opaque");
_Static_assert(sizeof(NevercASTNodeHandle) == sizeof(NevercHandle),
               "AST node must remain opaque");
_Static_assert(sizeof(NevercParserTokenCursorHandle) == sizeof(NevercHandle),
               "Parser token cursor must remain opaque");
_Static_assert(sizeof(NevercSemaScopeHandle) == sizeof(NevercHandle),
               "Sema scope must remain opaque");
_Static_assert(sizeof(NevercSemanticUnitHandle) == sizeof(NevercHandle),
               "semantic unit must remain opaque");
_Static_assert(offsetof(NevercSourceLocationAPI, Header) == 0,
               "source-location table must begin with ABI header");
_Static_assert(offsetof(NevercIOAPI, Header) == 0,
               "IO table must begin with ABI header");
_Static_assert(offsetof(NevercVFSProviderDescriptor, Header) == 0,
               "VFS provider descriptor must begin with ABI header");
_Static_assert(offsetof(NevercPrepAPI, Header) == 0,
               "Prep table must begin with ABI header");
_Static_assert(
    offsetof(NevercPrepAPI, DestroyTokenStreamBuilder) >
        offsetof(NevercPrepAPI, GetTokenInfo),
    "token stream provider functions must extend the Prep table prefix");
_Static_assert(offsetof(NevercASTAPI, Header) == 0,
               "AST table must begin with ABI header");
_Static_assert(offsetof(NevercASTLifecycleEvent, Header) == 0,
               "AST lifecycle event must begin with ABI header");
_Static_assert(offsetof(NevercASTAPI, RegisterLifecycleObserver) >
                   offsetof(NevercASTAPI, GetBuiltinType),
               "lifecycle observer must extend the AST table prefix");
_Static_assert(offsetof(NevercParserAPI, Header) == 0,
               "Parser table must begin with ABI header");
_Static_assert(offsetof(NevercParserParsedAttributeDescriptor, Header) == 0,
               "Parser attribute descriptor must begin with ABI header");
_Static_assert(offsetof(NevercSemaAPI, Header) == 0,
               "Sema table must begin with ABI header");
_Static_assert(offsetof(NevercSemanticUnitDescriptor, Header) == 0,
               "semantic unit descriptor must begin with ABI header");
_Static_assert(offsetof(NevercSemaAPI, GetAnalyzePhaseInput) >
                   offsetof(NevercSemaAPI, EmitDiagnostic),
               "Sema Provider functions must extend the Sema table prefix");
_Static_assert(offsetof(NevercFrontendCallerPackProbe, Value) == 1,
               "frontend headers did not restore caller packing");

int neverc_plugin_frontend_c_compile_fixture(void) {
  NevercSourceLocation Location = {0, 0};
  NevercTokenHandle Token = {0, 0};
  NevercASTNodeHandle Node = {0, 0};
  NevercSemaScopeHandle Scope = {0, 0};
  return neverc_handle_is_null(Location) == NEVERC_TRUE &&
         neverc_handle_is_null(Token) == NEVERC_TRUE &&
         neverc_handle_is_null(Node) == NEVERC_TRUE &&
         neverc_handle_is_null(Scope) == NEVERC_TRUE;
}
