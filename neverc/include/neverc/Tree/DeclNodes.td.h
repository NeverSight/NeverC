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
#undef RECORD

DECL_RANGE(Tag, Enum, Record)

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
#undef FUNCTION

DECL_RANGE(Function, Function, Function)

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

DECL_RANGE(Declarator, Field, ParmVar)

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

#undef VALUE

DECL_RANGE(Named, Label, IndirectField)

#undef NAMED

// --- Top-level declarations ---

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
DECL_CONTEXT(TranslationUnit)
#undef DECL_CONTEXT
#undef DECL_CONTEXT_BASE
