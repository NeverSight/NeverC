#ifndef ABSTRACT_TYPE
#define ABSTRACT_TYPE(Class, Base) TYPE(Class, Base)
#endif
#ifndef NON_CANONICAL_TYPE
#define NON_CANONICAL_TYPE(Class, Base) TYPE(Class, Base)
#endif
#ifndef DEPENDENT_TYPE
#define DEPENDENT_TYPE(Class, Base) TYPE(Class, Base)
#endif
#ifndef NON_CANONICAL_UNLESS_DEPENDENT_TYPE
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) TYPE(Class, Base)
#endif

NON_CANONICAL_TYPE(Adjusted, Type)
NON_CANONICAL_TYPE(Decayed, AdjustedType)
ABSTRACT_TYPE(Array, Type)
TYPE(ConstantArray, ArrayType)
TYPE(IncompleteArray, ArrayType)
TYPE(VariableArray, ArrayType)
TYPE(Atomic, Type)
NON_CANONICAL_TYPE(Attributed, Type)
NON_CANONICAL_TYPE(BTFTagAttributed, Type)
TYPE(BitInt, Type)
TYPE(Builtin, Type)
TYPE(Complex, Type)
ABSTRACT_TYPE(Deduced, Type)
TYPE(Auto, DeducedType)
NON_CANONICAL_TYPE(Elaborated, Type)
ABSTRACT_TYPE(Function, Type)
TYPE(FunctionNoProto, FunctionType)
TYPE(FunctionProto, FunctionType)
NON_CANONICAL_TYPE(MacroQualified, Type)
ABSTRACT_TYPE(Matrix, Type)
TYPE(ConstantMatrix, MatrixType)
NON_CANONICAL_TYPE(Paren, Type)
TYPE(Pointer, Type)
ABSTRACT_TYPE(Tag, Type)
TYPE(Enum, TagType)
TYPE(Record, TagType)
NON_CANONICAL_UNLESS_DEPENDENT_TYPE(TypeOfExpr, Type)
NON_CANONICAL_UNLESS_DEPENDENT_TYPE(TypeOf, Type)
NON_CANONICAL_TYPE(Typedef, Type)
TYPE(Vector, Type)
TYPE(ExtVector, VectorType)

#ifdef LAST_TYPE
LAST_TYPE(ExtVector)
#undef LAST_TYPE
#endif

#ifdef LEAF_TYPE
LEAF_TYPE(Builtin)
LEAF_TYPE(Enum)
LEAF_TYPE(Record)
#undef LEAF_TYPE
#endif

#undef TYPE
#undef ABSTRACT_TYPE
#undef NON_CANONICAL_TYPE
#undef DEPENDENT_TYPE
#undef NON_CANONICAL_UNLESS_DEPENDENT_TYPE
