#ifndef ABSTRACT_DECL
#define ABSTRACT_DECL(Type) Type
#endif
#ifndef DECL_RANGE
#define DECL_RANGE(Base, First, Last)
#endif
#ifndef LAST_DECL_RANGE
#define LAST_DECL_RANGE(Base, First, Last) DECL_RANGE(Base, First, Last)
#endif

#ifndef EMPTY
#define EMPTY(Type, Base) DECL(Type, Base)
#endif
EMPTY(Empty, Decl)
#undef EMPTY

#ifndef EXTERNCCONTEXT
#define EXTERNCCONTEXT(Type, Base) DECL(Type, Base)
#endif
EXTERNCCONTEXT(ExternCContext, Decl)
#undef EXTERNCCONTEXT

#ifndef FILESCOPEASM
#define FILESCOPEASM(Type, Base) DECL(Type, Base)
#endif
FILESCOPEASM(FileScopeAsm, Decl)
#undef FILESCOPEASM

// --- Named hierarchy ---

#ifndef NAMED
#define NAMED(Type, Base) DECL(Type, Base)
#endif
ABSTRACT_DECL(NAMED(Named, Decl))
#ifndef LABEL
#define LABEL(Type, Base) NAMED(Type, Base)
#endif
LABEL(Label, NamedDecl)
#undef LABEL

// --- Type hierarchy (child of Named) ---

#ifndef TYPE
#define TYPE(Type, Base) NAMED(Type, Base)
#endif
ABSTRACT_DECL(TYPE(Type, NamedDecl))

#ifndef TAG
#define TAG(Type, Base) TYPE(Type, Base)
#endif
ABSTRACT_DECL(TAG(Tag, TypeDecl))
#ifndef ENUM
#define ENUM(Type, Base) TAG(Type, Base)
#endif
ENUM(Enum, TagDecl)
#undef ENUM

#ifndef RECORD
#define RECORD(Type, Base) TAG(Type, Base)
#endif
RECORD(Record, TagDecl)
#ifndef CXXRECORD
#define CXXRECORD(Type, Base) RECORD(Type, Base)
#endif
CXXRECORD(CXXRecord, RecordDecl)
#undef CXXRECORD
#undef RECORD

DECL_RANGE(Tag, Enum, CXXRecord)

#undef TAG

#ifndef TYPEDEFNAME
#define TYPEDEFNAME(Type, Base) TYPE(Type, Base)
#endif
ABSTRACT_DECL(TYPEDEFNAME(TypedefName, TypeDecl))
#ifndef TYPEDEF
#define TYPEDEF(Type, Base) TYPEDEFNAME(Type, Base)
#endif
TYPEDEF(Typedef, TypedefNameDecl)
#undef TYPEDEF

DECL_RANGE(TypedefName, Typedef, Typedef)

#undef TYPEDEFNAME

// C++ using-declaration / using-directive / namespace alias placeholders.
#ifndef USING
#define USING(Type, Base) TYPE(Type, Base)
#endif
// Using decls are NamedDecls, not TypeDecls; keep under Named below.
#undef USING

DECL_RANGE(Type, Enum, Typedef)

#undef TYPE

// --- Value hierarchy (child of Named) ---

#ifndef VALUE
#define VALUE(Type, Base) NAMED(Type, Base)
#endif
ABSTRACT_DECL(VALUE(Value, NamedDecl))

#ifndef DECLARATOR
#define DECLARATOR(Type, Base) VALUE(Type, Base)
#endif
ABSTRACT_DECL(DECLARATOR(Declarator, ValueDecl))
#ifndef FIELD
#define FIELD(Type, Base) DECLARATOR(Type, Base)
#endif
FIELD(Field, DeclaratorDecl)
#undef FIELD

#ifndef FUNCTION
#define FUNCTION(Type, Base) DECLARATOR(Type, Base)
#endif
FUNCTION(Function, DeclaratorDecl)
#ifndef CXXMETHOD
#define CXXMETHOD(Type, Base) FUNCTION(Type, Base)
#endif
CXXMETHOD(CXXMethod, FunctionDecl)
#ifndef CXXCONSTRUCTOR
#define CXXCONSTRUCTOR(Type, Base) CXXMETHOD(Type, Base)
#endif
CXXCONSTRUCTOR(CXXConstructor, CXXMethodDecl)
#undef CXXCONSTRUCTOR
#ifndef CXXDESTRUCTOR
#define CXXDESTRUCTOR(Type, Base) CXXMETHOD(Type, Base)
#endif
CXXDESTRUCTOR(CXXDestructor, CXXMethodDecl)
#undef CXXDESTRUCTOR
#ifndef CXXCONVERSION
#define CXXCONVERSION(Type, Base) CXXMETHOD(Type, Base)
#endif
CXXCONVERSION(CXXConversion, CXXMethodDecl)
#undef CXXCONVERSION
#undef CXXMETHOD
#undef FUNCTION

DECL_RANGE(Function, Function, CXXConversion)

#ifndef VAR
#define VAR(Type, Base) DECLARATOR(Type, Base)
#endif
VAR(Var, DeclaratorDecl)
#ifndef IMPLICITPARAM
#define IMPLICITPARAM(Type, Base) VAR(Type, Base)
#endif
IMPLICITPARAM(ImplicitParam, VarDecl)
#undef IMPLICITPARAM

#ifndef PARMVAR
#define PARMVAR(Type, Base) VAR(Type, Base)
#endif
PARMVAR(ParmVar, VarDecl)
#undef PARMVAR

DECL_RANGE(Var, Var, ParmVar)

#undef VAR

DECL_RANGE(Declarator, Field, CXXConversion)

#undef DECLARATOR

#ifndef ENUMCONSTANT
#define ENUMCONSTANT(Type, Base) VALUE(Type, Base)
#endif
ENUMCONSTANT(EnumConstant, ValueDecl)
#undef ENUMCONSTANT

#ifndef INDIRECTFIELD
#define INDIRECTFIELD(Type, Base) VALUE(Type, Base)
#endif
INDIRECTFIELD(IndirectField, ValueDecl)
#undef INDIRECTFIELD

DECL_RANGE(Value, Field, IndirectField)

// C++ named entities that are not value decls.
#ifndef NAMESPACE
#define NAMESPACE(Type, Base) NAMED(Type, Base)
#endif
NAMESPACE(Namespace, NamedDecl)
#undef NAMESPACE

#ifndef NAMESPACEALIAS
#define NAMESPACEALIAS(Type, Base) NAMED(Type, Base)
#endif
NAMESPACEALIAS(NamespaceAlias, NamedDecl)
#undef NAMESPACEALIAS

#ifndef USING
#define USING(Type, Base) NAMED(Type, Base)
#endif
USING(Using, NamedDecl)
#undef USING

#ifndef USINGDIRECTIVE
#define USINGDIRECTIVE(Type, Base) NAMED(Type, Base)
#endif
USINGDIRECTIVE(UsingDirective, NamedDecl)
#undef USINGDIRECTIVE

#ifndef USINGSHADOW
#define USINGSHADOW(Type, Base) NAMED(Type, Base)
#endif
USINGSHADOW(UsingShadow, NamedDecl)
#undef USINGSHADOW

#ifndef USINGENUM
#define USINGENUM(Type, Base) NAMED(Type, Base)
#endif
USINGENUM(UsingEnum, NamedDecl)
#undef USINGENUM

DECL_RANGE(Named, Label, UsingEnum)

#undef VALUE
#undef NAMED

// --- Top-level declarations ---

#ifndef LINKAGESPEC
#define LINKAGESPEC(Type, Base) DECL(Type, Base)
#endif
LINKAGESPEC(LinkageSpec, Decl)
#undef LINKAGESPEC

#ifndef PRAGMACOMMENT
#define PRAGMACOMMENT(Type, Base) DECL(Type, Base)
#endif
PRAGMACOMMENT(PragmaComment, Decl)
#undef PRAGMACOMMENT

#ifndef PRAGMADETECTMISMATCH
#define PRAGMADETECTMISMATCH(Type, Base) DECL(Type, Base)
#endif
PRAGMADETECTMISMATCH(PragmaDetectMismatch, Decl)
#undef PRAGMADETECTMISMATCH

#ifndef STATICASSERT
#define STATICASSERT(Type, Base) DECL(Type, Base)
#endif
STATICASSERT(StaticAssert, Decl)
#undef STATICASSERT

#ifndef TRANSLATIONUNIT
#define TRANSLATIONUNIT(Type, Base) DECL(Type, Base)
#endif
TRANSLATIONUNIT(TranslationUnit, Decl)
#undef TRANSLATIONUNIT


// --- C++ template / modern declarations ---
#ifndef TEMPLATE
#define TEMPLATE(Type, Base) DECL(Type, Base)
#endif
ABSTRACT_DECL(TEMPLATE(Template, Decl))
#ifndef TEMPLATETYPEPARM
#define TEMPLATETYPEPARM(Type, Base) DECL(Type, Base)
#endif
TEMPLATETYPEPARM(TemplateTypeParm, Decl)
#undef TEMPLATETYPEPARM
#ifndef NONTYPETEMPLATEPARM
#define NONTYPETEMPLATEPARM(Type, Base) DECL(Type, Base)
#endif
NONTYPETEMPLATEPARM(NonTypeTemplateParm, Decl)
#undef NONTYPETEMPLATEPARM
#ifndef TEMPLATETEMPLATEPARM
#define TEMPLATETEMPLATEPARM(Type, Base) TEMPLATE(Type, Base)
#endif
TEMPLATETEMPLATEPARM(TemplateTemplateParm, TemplateDecl)
#undef TEMPLATETEMPLATEPARM
#ifndef CLASSTEMPLATE
#define CLASSTEMPLATE(Type, Base) TEMPLATE(Type, Base)
#endif
CLASSTEMPLATE(ClassTemplate, TemplateDecl)
#undef CLASSTEMPLATE
#ifndef FUNCTIONTEMPLATE
#define FUNCTIONTEMPLATE(Type, Base) TEMPLATE(Type, Base)
#endif
FUNCTIONTEMPLATE(FunctionTemplate, TemplateDecl)
#undef FUNCTIONTEMPLATE
#ifndef TYPEALIASTEMPLATE
#define TYPEALIASTEMPLATE(Type, Base) TEMPLATE(Type, Base)
#endif
TYPEALIASTEMPLATE(TypeAliasTemplate, TemplateDecl)
#undef TYPEALIASTEMPLATE
#ifndef VARTEMPLATE
#define VARTEMPLATE(Type, Base) TEMPLATE(Type, Base)
#endif
VARTEMPLATE(VarTemplate, TemplateDecl)
#undef VARTEMPLATE
#ifndef CONCEPT
#define CONCEPT(Type, Base) TEMPLATE(Type, Base)
#endif
CONCEPT(Concept, TemplateDecl)
#undef CONCEPT
DECL_RANGE(Template, Template, Concept)
#undef TEMPLATE

#ifndef TYPEALIAS
#define TYPEALIAS(Type, Base) DECL(Type, Base)
#endif
TYPEALIAS(TypeAlias, Decl)
#undef TYPEALIAS

#ifndef BINDING
#define BINDING(Type, Base) DECL(Type, Base)
#endif
BINDING(Binding, Decl)
#undef BINDING

#ifndef DECOMPOSITION
#define DECOMPOSITION(Type, Base) DECL(Type, Base)
#endif
DECOMPOSITION(Decomposition, Decl)
#undef DECOMPOSITION

LAST_DECL_RANGE(Decl, Empty, TranslationUnit)

#undef DECL
#undef DECL_RANGE
#undef LAST_DECL_RANGE
#undef ABSTRACT_DECL

// --- DeclContext list ---

#ifndef DECL_CONTEXT
#define DECL_CONTEXT(DECL)
#endif
#ifndef DECL_CONTEXT_BASE
#define DECL_CONTEXT_BASE(DECL) DECL_CONTEXT(DECL)
#endif
DECL_CONTEXT_BASE(Tag)
DECL_CONTEXT(ExternCContext)
DECL_CONTEXT(Function)
DECL_CONTEXT(LinkageSpec)
DECL_CONTEXT(Namespace)
DECL_CONTEXT(ClassTemplate)
DECL_CONTEXT(FunctionTemplate)
DECL_CONTEXT(TranslationUnit)
#undef DECL_CONTEXT
#undef DECL_CONTEXT_BASE
