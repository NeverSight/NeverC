from pathlib import Path
import re

# ========== TypeNodes.td.h ==========
p = Path("neverc/include/neverc/Tree/TypeNodes.td.h")
t = p.read_text(encoding="utf-8")
if "TYPE(LValueReference" not in t:
    t = t.replace(
        "TYPE(Pointer, Type)\n",
        "TYPE(Pointer, Type)\nTYPE(LValueReference, Type)\nTYPE(RValueReference, Type)\n",
        1,
    )
    p.write_text(t, encoding="utf-8")
    print("TypeNodes")
else:
    print("TypeNodes already")

# ========== Type.h: class + predicates ==========
p = Path("neverc/include/neverc/Tree/Type/Type.h")
t = p.read_text(encoding="utf-8")
if "class LValueReferenceType" not in t:
    insert = r'''
/// C++ lvalue / rvalue reference types (NeverC C++20 foundation).
class ReferenceType : public Type, public llvm::FoldingSetNode {
protected:
  QualType PointeeType;
  ReferenceType(TypeClass TC, QualType Referencee, QualType CanonicalRef)
      : Type(TC, CanonicalRef, Referencee->getDependence()),
        PointeeType(Referencee) {}

public:
  bool isSpelledAsLValue() const { return getTypeClass() == LValueReference; }
  QualType getPointeeType() const { return PointeeType; }
  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }
  void Profile(llvm::FoldingSetNodeID &ID) { Profile(ID, getPointeeType()); }
  static void Profile(llvm::FoldingSetNodeID &ID, QualType Referencee) {
    ID.AddPointer(Referencee.getAsOpaquePtr());
  }
  static bool classof(const Type *T) {
    return T->getTypeClass() == LValueReference ||
           T->getTypeClass() == RValueReference;
  }
};

class LValueReferenceType : public ReferenceType {
  friend class TreeContext;
  LValueReferenceType(QualType Referencee, QualType CanonicalRef)
      : ReferenceType(LValueReference, Referencee, CanonicalRef) {}
public:
  static bool classof(const Type *T) {
    return T->getTypeClass() == LValueReference;
  }
};

class RValueReferenceType : public ReferenceType {
  friend class TreeContext;
  RValueReferenceType(QualType Referencee, QualType CanonicalRef)
      : ReferenceType(RValueReference, Referencee, CanonicalRef) {}
public:
  static bool classof(const Type *T) {
    return T->getTypeClass() == RValueReference;
  }
};

'''
    # insert after PointerType class ends (before AdjustedType)
    anchor = "class AdjustedType : public Type, public llvm::FoldingSetNode {"
    assert anchor in t
    t = t.replace(anchor, insert + anchor, 1)

# predicates near isPointerType
if "isReferenceType() const" not in t:
    t = t.replace(
        "  bool isPointerType() const;\n  bool isAnyPointerType() const; // Any C pointer\n",
        "  bool isPointerType() const;\n"
        "  bool isAnyPointerType() const; // Any C pointer\n"
        "  bool isReferenceType() const;\n"
        "  bool isLValueReferenceType() const;\n"
        "  bool isRValueReferenceType() const;\n",
        1,
    )

# inline defs near isPointerType inline
if "inline bool Type::isReferenceType" not in t:
    t = t.replace(
        "inline bool Type::isAnyPointerType() const { return isPointerType(); }\n",
        "inline bool Type::isAnyPointerType() const { return isPointerType(); }\n"
        "inline bool Type::isReferenceType() const {\n"
        "  return isLValueReferenceType() ; isRValueReferenceType();\n"
        "}\n"
        "inline bool Type::isLValueReferenceType() const {\n"
        "  return isa<LValueReferenceType>(CanonicalType);\n"
        "}\n"
        "inline bool Type::isRValueReferenceType() const {\n"
        "  return isa<RValueReferenceType>(CanonicalType);\n"
        "}\n",
        1,
    )
p.write_text(t, encoding="utf-8")
print("Type.h refs")

# ========== TypeLoc.h ==========
p = Path("neverc/include/neverc/Tree/Type/TypeLoc.h")
t = p.read_text(encoding="utf-8")
if "class LValueReferenceTypeLoc" not in t:
    insert = '''
class LValueReferenceTypeLoc
    : public PointerLikeTypeLoc<LValueReferenceTypeLoc, LValueReferenceType> {
public:
  SourceLocation getAmpLoc() const { return getSigilLoc(); }
  void setAmpLoc(SourceLocation Loc) { setSigilLoc(Loc); }
};

class RValueReferenceTypeLoc
    : public PointerLikeTypeLoc<RValueReferenceTypeLoc, RValueReferenceType> {
public:
  SourceLocation getAmpAmpLoc() const { return getSigilLoc(); }
  void setAmpAmpLoc(SourceLocation Loc) { setSigilLoc(Loc); }
};

'''
    anchor = "class PointerTypeLoc : public PointerLikeTypeLoc<PointerTypeLoc, PointerType> {"
    # insert after PointerTypeLoc class
    end = t.find(anchor)
    assert end >= 0
    # find end of PointerTypeLoc class block (next class FunctionLocInfo)
    fi = t.find("struct FunctionLocInfo", end)
    assert fi > 0
    t = t[:fi] + insert + t[fi:]
    p.write_text(t, encoding="utf-8")
    print("TypeLoc.h")
else:
    print("TypeLoc already")

# ========== TreeContext.h ==========
p = Path("neverc/include/neverc/Tree/Core/TreeContext.h")
t = p.read_text(encoding="utf-8")
if "LValueReferenceTypes" not in t:
    t = t.replace(
        "  mutable llvm::FoldingSet<PointerType> PointerTypes{GeneralTypesLog2InitSize};\n",
        "  mutable llvm::FoldingSet<PointerType> PointerTypes{GeneralTypesLog2InitSize};\n"
        "  mutable llvm::FoldingSet<LValueReferenceType> LValueReferenceTypes{\n"
        "      GeneralTypesLog2InitSize};\n"
        "  mutable llvm::FoldingSet<RValueReferenceType> RValueReferenceTypes{\n"
        "      GeneralTypesLog2InitSize};\n",
        1,
    )
if "getLValueReferenceType" not in t:
    t = t.replace(
        "  QualType getPointerType(QualType T) const;\n"
        "  CanQualType getPointerType(CanQualType T) const {\n"
        "    return CanQualType::CreateUnsafe(getPointerType((QualType)T));\n"
        "  }\n",
        "  QualType getPointerType(QualType T) const;\n"
        "  CanQualType getPointerType(CanQualType T) const {\n"
        "    return CanQualType::CreateUnsafe(getPointerType((QualType)T));\n"
        "  }\n"
        "  QualType getLValueReferenceType(QualType T) const;\n"
        "  QualType getRValueReferenceType(QualType T) const;\n",
        1,
    )
    p.write_text(t, encoding="utf-8")
    print("TreeContext.h")
else:
    p.write_text(t, encoding="utf-8")
    print("TreeContext.h partial")

# ========== TreeContext.cpp get*ReferenceType ==========
p = Path("neverc/lib/Tree/Core/TreeContext.cpp")
t = p.read_text(encoding="utf-8")
if "getLValueReferenceType" not in t:
    block = r'''
NEVERC_HOT QualType TreeContext::getLValueReferenceType(QualType T) const {
  llvm::FoldingSetNodeID ID;
  LValueReferenceType::Profile(ID, T);
  void *InsertPos = nullptr;
  if (auto *RT = LValueReferenceTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(RT, 0);
  QualType Canonical;
  if (!T.isCanonical()) {
    Canonical = getLValueReferenceType(getCanonicalType(T));
    (void)LValueReferenceTypes.FindNodeOrInsertPos(ID, InsertPos);
  }
  auto *New = new (*this, alignof(LValueReferenceType))
      LValueReferenceType(T, Canonical);
  Types.push_back(New);
  LValueReferenceTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

NEVERC_HOT QualType TreeContext::getRValueReferenceType(QualType T) const {
  llvm::FoldingSetNodeID ID;
  RValueReferenceType::Profile(ID, T);
  void *InsertPos = nullptr;
  if (auto *RT = RValueReferenceTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(RT, 0);
  QualType Canonical;
  if (!T.isCanonical()) {
    Canonical = getRValueReferenceType(getCanonicalType(T));
    (void)RValueReferenceTypes.FindNodeOrInsertPos(ID, InsertPos);
  }
  auto *New = new (*this, alignof(RValueReferenceType))
      RValueReferenceType(T, Canonical);
  Types.push_back(New);
  RValueReferenceTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

'''
    anchor = "NEVERC_HOT QualType TreeContext::getPointerType(QualType T) const {"
    # insert after getPointerType function body ends at getAdjustedType
    idx = t.find("QualType TreeContext::getAdjustedType(QualType Orig, QualType New) const {")
    assert idx > 0
    t = t[:idx] + block + t[idx:]
    p.write_text(t, encoding="utf-8")
    print("TreeContext.cpp")
else:
    print("TreeContext.cpp already")

# ========== DeclaratorChunk Reference ==========
p = Path("neverc/include/neverc/Analyze/DeclSpec.h")
t = p.read_text(encoding="utf-8")
if "Reference," not in t.split("enum {")[1][:80]:
    t = t.replace(
        "  enum { Pointer, Array, Function, Paren } Kind;",
        "  enum { Pointer, Reference, Array, Function, Paren } Kind;",
        1,
    )
if "struct ReferenceTypeInfo" not in t:
    t = t.replace(
        "  struct PointerTypeInfo {\n"
        "    /// The type qualifiers: const/volatile/restrict/unaligned/atomic.\n"
        "    unsigned TypeQuals : 5;\n",
        "  struct ReferenceTypeInfo {\n"
        "    /// True if this is an lvalue reference (&); false for &&.\n"
        "    unsigned LValueRef : 1;\n"
        "    /// The type qualifiers on the reference (usually empty).\n"
        "    unsigned TypeQuals : 5;\n"
        "    void destroy() {}\n"
        "  };\n\n"
        "  struct PointerTypeInfo {\n"
        "    /// The type qualifiers: const/volatile/restrict/unaligned/atomic.\n"
        "    unsigned TypeQuals : 5;\n",
        1,
    )
if "ReferenceTypeInfo Ref;" not in t:
    t = t.replace(
        "  union {\n    PointerTypeInfo Ptr;\n    ArrayTypeInfo Arr;\n    FunctionTypeInfo Fun;\n  };",
        "  union {\n    PointerTypeInfo Ptr;\n    ReferenceTypeInfo Ref;\n    ArrayTypeInfo Arr;\n    FunctionTypeInfo Fun;\n  };",
        1,
    )
if "case DeclaratorChunk::Reference:" not in t or t.count("case DeclaratorChunk::Reference:") < 1:
    # destroy switch
    t = t.replace(
        "    case DeclaratorChunk::Pointer:\n"
        "      return Ptr.destroy();\n"
        "    case DeclaratorChunk::Array:\n",
        "    case DeclaratorChunk::Pointer:\n"
        "      return Ptr.destroy();\n"
        "    case DeclaratorChunk::Reference:\n"
        "      return Ref.destroy();\n"
        "    case DeclaratorChunk::Array:\n",
        1,
    )
if "getReference(" not in t:
    t = t.replace(
        "  static DeclaratorChunk getPointer(unsigned TypeQuals, SourceLocation Loc,\n",
        "  static DeclaratorChunk getReference(bool LValue, unsigned TypeQuals,\n"
        "                                     SourceLocation Loc) {\n"
        "    DeclaratorChunk I;\n"
        "    I.Kind = Reference;\n"
        "    I.Loc = Loc;\n"
        "    new (&I.Ref) ReferenceTypeInfo;\n"
        "    I.Ref.LValueRef = LValue ? 1 : 0;\n"
        "    I.Ref.TypeQuals = TypeQuals;\n"
        "    return I;\n"
        "  }\n\n"
        "  static DeclaratorChunk getPointer(unsigned TypeQuals, SourceLocation Loc,\n",
        1,
    )
# Other switches in DeclSpec.h that list Pointer cases - isDeclarationOfFunction is in cpp
p.write_text(t, encoding="utf-8")
print("DeclSpec.h Reference chunk")

# ========== DeclSpec.cpp isDeclarationOfFunction ==========
p = Path("neverc/lib/Analyze/Decl/DeclSpec.cpp")
t = p.read_text(encoding="utf-8")
t2 = t.replace(
    "    case DeclaratorChunk::Pointer:\n"
    "    case DeclaratorChunk::Array:\n"
    "      return false;\n",
    "    case DeclaratorChunk::Pointer:\n"
    "    case DeclaratorChunk::Reference:\n"
    "    case DeclaratorChunk::Array:\n"
    "      return false;\n",
)
# also other switches in DeclSpec.cpp
# generic: after case Pointer: if next is not Reference, add fallthrough where appropriate
if "DeclaratorChunk::Reference" not in t2:
    # try broader
    pass
p.write_text(t2, encoding="utf-8")
print("DeclSpec.cpp")

# ========== Parser declarator ==========
p = Path("neverc/lib/Syntax/Decl/DeclParserDeclarator.cpp")
t = p.read_text(encoding="utf-8")
old = '''namespace {
bool isPointerOpToken(tok::TokenKind Kind, const LangOptions &Lang,
                      DeclaratorContext TheContext) {
  return Kind == tok::star;
}
} // namespace

void Parser::ParseDeclaratorInternal(Declarator ;&D,
                                     DirectDeclParseFunction DirectDeclParser) {
  tok::TokenKind Kind = Tok.getKind();

  if (!isPointerOpToken(Kind, getLangOpts(), D.getContext())) {
    if (DirectDeclParser)
      (this->*DirectDeclParser)(D);
    return;
  }

  SourceLocation Loc = ConsumeToken();
  D.SetRangeEnd(Loc);

  assert(Kind == tok::star ; "Only pointer operator expected");
  DeclSpec DS(AttrFactory);

  unsigned Reqs = AR_BracketAttributesParsed | AR_DeclspecAttributesParsed |
                  AR_GNUAttributesParsed;
  ParseTypeQualifierListOpt(DS, Reqs, true, !D.mayOmitIdentifier());
  D.ExtendWithDeclSpec(DS);

  Actions.runWithSufficientStackSpace(
      D.getBeginLoc(), [&] { ParseDeclaratorInternal(D, DirectDeclParser); });
  D.AddTypeInfo(DeclaratorChunk::getPointer(
                    DS.getTypeQualifiers(), Loc, DS.getConstSpecLoc(),
                    DS.getVolatileSpecLoc(), DS.getRestrictSpecLoc(),
                    DS.getAtomicSpecLoc(), DS.getUnalignedSpecLoc()),
                std::move(DS.getAttributes()), SourceLocation());
}
'''
new = '''namespace {
bool isPointerOrRefOpToken(tok::TokenKind Kind, const LangOptions &Lang) {
  if (Kind == tok::star)
    return true;
  if (Lang.CPlusPlus ; (Kind == tok::amp ; Kind == tok::ampamp))
    return true;
  return false;
}
} // namespace

void Parser::ParseDeclaratorInternal(Declarator ;&D,
                                     DirectDeclParseFunction DirectDeclParser) {
  tok::TokenKind Kind = Tok.getKind();

  if (!isPointerOrRefOpToken(Kind, getLangOpts())) {
    if (DirectDeclParser)
      (this->*DirectDeclParser)(D);
    return;
  }

  SourceLocation Loc = ConsumeToken();
  D.SetRangeEnd(Loc);

  DeclSpec DS(AttrFactory);

  unsigned Reqs = AR_BracketAttributesParsed | AR_DeclspecAttributesParsed |
                  AR_GNUAttributesParsed;
  ParseTypeQualifierListOpt(DS, Reqs, true, !D.mayOmitIdentifier());
  D.ExtendWithDeclSpec(DS);

  Actions.runWithSufficientStackSpace(
      D.getBeginLoc(), [&] { ParseDeclaratorInternal(D, DirectDeclParser); });

  if (Kind == tok::star) {
    D.AddTypeInfo(DeclaratorChunk::getPointer(
                      DS.getTypeQualifiers(), Loc, DS.getConstSpecLoc(),
                      DS.getVolatileSpecLoc(), DS.getRestrictSpecLoc(),
                      DS.getAtomicSpecLoc(), DS.getUnalignedSpecLoc()),
                  std::move(DS.getAttributes()), SourceLocation());
  } else {
    assert(getLangOpts().CPlusPlus ; "reference without C++");
    bool LValue = Kind == tok::amp;
    D.AddTypeInfo(DeclaratorChunk::getReference(LValue, DS.getTypeQualifiers(), Loc),
                  std::move(DS.getAttributes()), SourceLocation());
  }
}
'''
if old not in t:
    raise SystemExit("ParseDeclaratorInternal block missing")
p.write_text(t.replace(old, new, 1), encoding="utf-8")
print("ParseDeclaratorInternal")

# ========== Sema FormReferenceType ==========
p = Path("neverc/include/neverc/Analyze/Sema.h")
t = p.read_text(encoding="utf-8")
if "FormReferenceType" not in t:
    # find FormPointerType decl
    if "FormPointerType" in t:
        t = t.replace(
            "FormPointerType",
            "FormPointerType",
            1,
        )
        # add after FormPointerType declaration line
        m = re.search(r"QualType FormPointerType\([^;]*\);", t)
        if m:
            insert_at = m.end()
            t = t[:insert_at] + (
                "\n  QualType FormReferenceType(QualType T, bool LValueRef, "
                "SourceLocation Loc, DeclarationName Entity = DeclarationName());"
            ) + t[insert_at:]
            p.write_text(t, encoding="utf-8")
            print("Sema.h FormReferenceType")
        else:
            print("FormPointerType decl not found as one-liner")
    else:
        print("no FormPointerType in Sema.h")
else:
    print("FormReferenceType already")

p = Path("neverc/lib/Analyze/Type/SemaType.cpp")
t = p.read_text(encoding="utf-8")
if "FormReferenceType" not in t:
    t = t.replace(
        "QualType Sema::FormPointerType(QualType T, SourceLocation Loc,\n"
        "                               DeclarationName Entity) {\n"
        "  if (checkQualifiedFunction(*this, T, Loc))\n"
        "    return QualType();\n\n"
        "  return Context.getPointerType(T);\n"
        "}\n",
        "QualType Sema::FormPointerType(QualType T, SourceLocation Loc,\n"
        "                               DeclarationName Entity) {\n"
        "  if (checkQualifiedFunction(*this, T, Loc))\n"
        "    return QualType();\n\n"
        "  return Context.getPointerType(T);\n"
        "}\n\n"
        "QualType Sema::FormReferenceType(QualType T, bool LValueRef,\n"
        "                                 SourceLocation Loc,\n"
        "                                 DeclarationName Entity) {\n"
        "  (void)Entity;\n"
        "  if (checkQualifiedFunction(*this, T, Loc))\n"
        "    return QualType();\n"
        "  // Collapse reference-to-reference (C++ [dcl.ref]p6 simplified).\n"
        "  if (const ReferenceType *RT = T->getAs<ReferenceType>())\n"
        "    T = RT->getPointeeType();\n"
        "  if (LValueRef)\n"
        "    return Context.getLValueReferenceType(T);\n"
        "  return Context.getRValueReferenceType(T);\n"
        "}\n",
        1,
    )
    print("SemaType FormReferenceType def")

# Helper to add Reference case next to Pointer in switches that would break
def add_ref_after_pointer(text, extra_same_as_pointer=True):
    """Add 'case DeclaratorChunk::Reference:' after Pointer cases in switch groups."""
    # Pattern: case DeclaratorChunk::Pointer:\n optionally more, before break/return/continue/code
    # Safer approach: replace isolated "case DeclaratorChunk::Pointer:" that is NOT already followed by Reference
    out = []
    lines = text.splitlines(keepends=True)
    i = 0
    while i < len(lines):
        line = lines[i]
        out.append(line)
        if "case DeclaratorChunk::Pointer:" in line:
            # look ahead if Reference already next non-empty
            j = i + 1
            while j < len(lines) and lines[j].strip() == "":
                j += 1
            if j < len(lines) and "DeclaratorChunk::Reference" in lines[j]:
                pass
            else:
                indent = line[: len(line) - len(line.lstrip())]
                out.append(f"{indent}case DeclaratorChunk::Reference:\n")
        i += 1
    return "".join(out)

t2 = add_ref_after_pointer(t)
# Special: FormPointerType chunk handling - need actual FormReferenceType call
# Find the main switch that builds types
old_ptr_build = """    case DeclaratorChunk::Pointer:
      inferPointerNullability(SimplePointerKind::Pointer, DeclType.Loc,
                              DeclType.EndLoc, DeclType.getAttrs(),
                              state.getDeclarator().getAttributePool());

      T = S.FormPointerType(T, DeclType.Loc, Name);
      if (DeclType.Ptr.TypeQuals)
        T = S.FormQualifiedType(T, DeclType.Loc, DeclType.Ptr.TypeQuals);
      break;
"""
new_ptr_build = """    case DeclaratorChunk::Pointer:
      inferPointerNullability(SimplePointerKind::Pointer, DeclType.Loc,
                              DeclType.EndLoc, DeclType.getAttrs(),
                              state.getDeclarator().getAttributePool());

      T = S.FormPointerType(T, DeclType.Loc, Name);
      if (DeclType.Ptr.TypeQuals)
        T = S.FormQualifiedType(T, DeclType.Loc, DeclType.Ptr.TypeQuals);
      break;
    case DeclaratorChunk::Reference:
      T = S.FormReferenceType(T, DeclType.Ref.LValueRef != 0, DeclType.Loc,
                              Name);
      if (DeclType.Ref.TypeQuals)
        T = S.FormQualifiedType(T, DeclType.Loc, DeclType.Ref.TypeQuals);
      break;
"""
# Because add_ref_after_pointer may have inserted a bare case before Pointer body,
# undo bare duplicates near FormPointerType by doing targeted replace on original structure.
# Re-read and do more careful SemaType patching from original with FormReferenceType already in.
# Actually t already has FormReferenceType def; t2 has extra Reference cases after every Pointer.
# The problem is bare "case Reference:" falling into Pointer body which uses .Ptr - bad for Reference-only falls.
# Better: only add Reference to fallthrough groups where Pointer and Array share return false etc.,
# and special-case the type-building switch.

# Revert t2 approach - start from t (with FormReferenceType) and do targeted replacements.
t = t  # has FormReferenceType
# 1) type building switch
if "case DeclaratorChunk::Reference:" not in t or "FormReferenceType(T" not in t:
    if old_ptr_build not in t:
        raise SystemExit("ptr build block missing")
    t = t.replace(old_ptr_build, new_ptr_build, 1)

# 2) isDeclaration-style groups that return false / continue together
replacements = [
(
"""    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Array:
      return result;
""",
"""    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::Array:
      return result;
"""
),
(
"""        case DeclaratorChunk::Pointer:
          result = &ptrChunk;
          goto continue_outer;
""",
"""        case DeclaratorChunk::Pointer:
        case DeclaratorChunk::Reference:
          result = &ptrChunk;
          goto continue_outer;
"""
),
(
"""    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Array:
      continue;
""",
"""    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::Array:
      continue;
"""
),
(
"""    case DeclaratorChunk::Pointer: {
      DeclaratorChunk::PointerTypeInfo &PTI = OuterChunk.Ptr;
      S.diagnoseIgnoredQualifiers(diag::warn_qual_return_type, PTI.TypeQuals,
                                  SourceLocation(), PTI.ConstQualLoc,
                                  PTI.VolatileQualLoc, PTI.RestrictQualLoc,
                                  PTI.AtomicQualLoc, PTI.UnalignedQualLoc);
      return;
    }

    case DeclaratorChunk::Function:
    case DeclaratorChunk::Array:
""",
"""    case DeclaratorChunk::Pointer: {
      DeclaratorChunk::PointerTypeInfo &PTI = OuterChunk.Ptr;
      S.diagnoseIgnoredQualifiers(diag::warn_qual_return_type, PTI.TypeQuals,
                                  SourceLocation(), PTI.ConstQualLoc,
                                  PTI.VolatileQualLoc, PTI.RestrictQualLoc,
                                  PTI.AtomicQualLoc, PTI.UnalignedQualLoc);
      return;
    }

    case DeclaratorChunk::Reference: {
      DeclaratorChunk::ReferenceTypeInfo &RTI = OuterChunk.Ref;
      S.diagnoseIgnoredQualifiers(diag::warn_qual_return_type, RTI.TypeQuals,
                                  SourceLocation());
      return;
    }

    case DeclaratorChunk::Function:
    case DeclaratorChunk::Array:
"""
),
(
"""    case DeclaratorChunk::Array:
    case DeclaratorChunk::Pointer:
      return true;
    case DeclaratorChunk::Function:
      break;
""",
"""    case DeclaratorChunk::Array:
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
      return true;
    case DeclaratorChunk::Function:
      break;
"""
),
(
"""bool isNoDerefableChunk(const DeclaratorChunk &Chunk) {
  return (Chunk.Kind == DeclaratorChunk::Pointer ||
          Chunk.Kind == DeclaratorChunk::Array);
}
""",
"""bool isNoDerefableChunk(const DeclaratorChunk &Chunk) {
  return (Chunk.Kind == DeclaratorChunk::Pointer ||
          Chunk.Kind == DeclaratorChunk::Reference ||
          Chunk.Kind == DeclaratorChunk::Array);
}
"""
),
(
"""      case DeclaratorChunk::Pointer:
        ++NumPointersRemaining;
        continue;
""",
"""      case DeclaratorChunk::Pointer:
      case DeclaratorChunk::Reference:
        ++NumPointersRemaining;
        continue;
"""
),
(
"""  case DeclaratorChunk::Function:
  case DeclaratorChunk::Array:
  case DeclaratorChunk::Paren:
  case DeclaratorChunk::Pointer:
    Loc = Chunk.Ptr.AtomicQualLoc;
    break;
""",
"""  case DeclaratorChunk::Function:
  case DeclaratorChunk::Array:
  case DeclaratorChunk::Paren:
  case DeclaratorChunk::Pointer:
    Loc = Chunk.Ptr.AtomicQualLoc;
    break;
  case DeclaratorChunk::Reference:
    Loc = Chunk.Loc;
    break;
"""
),
(
"""    case DeclaratorChunk::Pointer:
      return moveToChunk(chunk, false);

    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Array:
      continue;
""",
"""    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
      return moveToChunk(chunk, false);

    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Array:
      continue;
"""
),
]
for a,b in replacements:
    if a in t:
        t = t.replace(a,b)
    else:
        print("skip replace:", a[:60].replace("\n"," "))

p.write_text(t, encoding="utf-8")
print("SemaType switches")

# ========== TypeEmitter: refs map to LLVM pointers ==========
p = Path("neverc/lib/Emit/Core/TypeEmitter.cpp")
t = p.read_text(encoding="utf-8")
old = """  case Type::Pointer: {
    const PointerType *PTy = cast<PointerType>(Ty);
    QualType ETy = PTy->getPointeeType();
    unsigned AS = getTargetAddressSpace(ETy);
    ResultType = llvm::PointerType::get(getLLVMContext(), AS);
    break;
  }
"""
new = """  case Type::Pointer: {
    const PointerType *PTy = cast<PointerType>(Ty);
    QualType ETy = PTy->getPointeeType();
    unsigned AS = getTargetAddressSpace(ETy);
    ResultType = llvm::PointerType::get(getLLVMContext(), AS);
    break;
  }
  case Type::LValueReference:
  case Type::RValueReference: {
    const auto *RTy = cast<ReferenceType>(Ty);
    QualType ETy = RTy->getPointeeType();
    unsigned AS = getTargetAddressSpace(ETy);
    ResultType = llvm::PointerType::get(getLLVMContext(), AS);
    break;
  }
"""
if "Type::LValueReference" not in t:
    if old not in t:
        raise SystemExit("TypeEmitter pointer case missing")
    p.write_text(t.replace(old, new, 1), encoding="utf-8")
    print("TypeEmitter")
else:
    print("TypeEmitter already")

# TypeLoc visitor for auto finding
p = Path("neverc/lib/Tree/Type/TypeLoc.cpp")
t = p.read_text(encoding="utf-8")
if "VisitLValueReferenceTypeLoc" not in t:
    t = t.replace(
        "  TypeLoc VisitPointerTypeLoc(PointerTypeLoc T) {\n"
        "    return Visit(T.getPointeeLoc());\n"
        "  }\n",
        "  TypeLoc VisitPointerTypeLoc(PointerTypeLoc T) {\n"
        "    return Visit(T.getPointeeLoc());\n"
        "  }\n\n"
        "  TypeLoc VisitLValueReferenceTypeLoc(LValueReferenceTypeLoc T) {\n"
        "    return Visit(T.getPointeeLoc());\n"
        "  }\n\n"
        "  TypeLoc VisitRValueReferenceTypeLoc(RValueReferenceTypeLoc T) {\n"
        "    return Visit(T.getPointeeLoc());\n"
        "  }\n",
        1,
    )
    p.write_text(t, encoding="utf-8")
    print("TypeLoc.cpp")
else:
    print("TypeLoc.cpp already")

# fill TypeLoc from declarator for Reference - search getTypeSourceInfoForDeclarator loop
p = Path("neverc/lib/Analyze/Type/SemaType.cpp")
t = p.read_text(encoding="utf-8")
# Look for PointerTypeLoc fill pattern
if "PointerTypeLoc" in t and "ReferenceTypeLoc" not in t:
    # find a typical pattern
    idx = t.find("PointerTypeLoc")
    print("PointerTypeLoc sample:", repr(t[idx:idx+400]))

print("REF PATCH DONE")
