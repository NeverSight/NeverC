from pathlib import Path
import re

def norm(s):
    return s.replace("\r\n", "\n").replace("\r", "\n")

def write_keep_nl(path, text, original):
    # preserve CRLF if original used it
    if "\r\n" in original:
        text = text.replace("\n", "\r\n")
    Path(path).write_text(text, encoding="utf-8", newline="")

# ---- Parser ----
p = Path("neverc/lib/Syntax/Decl/DeclParserDeclarator.cpp")
orig = p.read_text(encoding="utf-8")
t = norm(orig)
old = '''namespace {
bool isPointerOpToken(tok::TokenKind Kind, const LangOptions &Lang,
                      DeclaratorContext TheContext) {
  return Kind == tok::star;
}
} // namespace

void Parser::ParseDeclaratorInternal(Declarator &D,
                                     DirectDeclParseFunction DirectDeclParser) {
  tok::TokenKind Kind = Tok.getKind();

  if (!isPointerOpToken(Kind, getLangOpts(), D.getContext())) {
    if (DirectDeclParser)
      (this->*DirectDeclParser)(D);
    return;
  }

  SourceLocation Loc = ConsumeToken();
  D.SetRangeEnd(Loc);

  assert(Kind == tok::star && "Only pointer operator expected");
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
  if (Lang.CPlusPlus && (Kind == tok::amp || Kind == tok::ampamp))
    return true;
  return false;
}
} // namespace

void Parser::ParseDeclaratorInternal(Declarator &D,
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
    assert(getLangOpts().CPlusPlus && "reference without C++");
    bool LValue = Kind == tok::amp;
    D.AddTypeInfo(
        DeclaratorChunk::getReference(LValue, DS.getTypeQualifiers(), Loc),
        std::move(DS.getAttributes()), SourceLocation());
  }
}
'''
if old not in t:
    raise SystemExit("parser block missing after norm")
t = t.replace(old, new, 1)
write_keep_nl(p, t, orig)
print("parser ok")

# ---- Sema.h ----
p = Path("neverc/include/neverc/Analyze/Sema.h")
orig = p.read_text(encoding="utf-8")
t = norm(orig)
if "FormReferenceType" not in t:
    m = re.search(
        r"QualType FormPointerType\(QualType T, SourceLocation Loc,\n"
        r"\s*DeclarationName Entity = DeclarationName\(\)\);",
        t,
    )
    if not m:
        # try two-line without default
        m = re.search(r"QualType FormPointerType\([\s\S]*?\);", t)
    if not m:
        raise SystemExit("FormPointerType decl missing")
    insert = (
        m.group(0)
        + "\n  QualType FormReferenceType(QualType T, bool LValueRef,\n"
        "                            SourceLocation Loc,\n"
        "                            DeclarationName Entity = DeclarationName());"
    )
    t = t[: m.start()] + insert + t[m.end() :]
    write_keep_nl(p, t, orig)
    print("Sema.h ok")
else:
    print("Sema.h already")

# ---- SemaType.cpp ----
p = Path("neverc/lib/Analyze/Type/SemaType.cpp")
orig = p.read_text(encoding="utf-8")
t = norm(orig)
if "FormReferenceType" not in t:
    old = '''QualType Sema::FormPointerType(QualType T, SourceLocation Loc,
                               DeclarationName Entity) {
  if (checkQualifiedFunction(*this, T, Loc))
    return QualType();

  return Context.getPointerType(T);
}
'''
    new = '''QualType Sema::FormPointerType(QualType T, SourceLocation Loc,
                               DeclarationName Entity) {
  if (checkQualifiedFunction(*this, T, Loc))
    return QualType();

  return Context.getPointerType(T);
}

QualType Sema::FormReferenceType(QualType T, bool LValueRef,
                                 SourceLocation Loc,
                                 DeclarationName Entity) {
  (void)Entity;
  if (checkQualifiedFunction(*this, T, Loc))
    return QualType();
  // Collapse reference-to-reference (C++ [dcl.ref]p6 simplified).
  if (const ReferenceType *RT = T->getAs<ReferenceType>())
    T = RT->getPointeeType();
  if (LValueRef)
    return Context.getLValueReferenceType(T);
  return Context.getRValueReferenceType(T);
}
'''
    if old not in t:
        raise SystemExit("FormPointerType def missing")
    t = t.replace(old, new, 1)
    print("FormReferenceType def")

old = '''    case DeclaratorChunk::Pointer:
      inferPointerNullability(SimplePointerKind::Pointer, DeclType.Loc,
                              DeclType.EndLoc, DeclType.getAttrs(),
                              state.getDeclarator().getAttributePool());

      T = S.FormPointerType(T, DeclType.Loc, Name);
      if (DeclType.Ptr.TypeQuals)
        T = S.FormQualifiedType(T, DeclType.Loc, DeclType.Ptr.TypeQuals);
      break;
    case DeclaratorChunk::Array:'''
new = '''    case DeclaratorChunk::Pointer:
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
    case DeclaratorChunk::Array:'''
if "FormReferenceType(T" not in t:
    if old not in t:
        raise SystemExit("build switch missing")
    t = t.replace(old, new, 1)
    print("build switch")

# add Reference fallthroughs carefully for exhaustive switches
pairs = [
(
'''    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Array:
      return result;
''',
'''    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::Array:
      return result;
'''
),
(
'''        case DeclaratorChunk::Pointer:
          result = &ptrChunk;
          goto continue_outer;
''',
'''        case DeclaratorChunk::Pointer:
        case DeclaratorChunk::Reference:
          result = &ptrChunk;
          goto continue_outer;
'''
),
(
'''    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Array:
      continue;
''',
'''    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::Array:
      continue;
'''
),
(
'''    case DeclaratorChunk::Pointer: {
      DeclaratorChunk::PointerTypeInfo &PTI = OuterChunk.Ptr;
      S.diagnoseIgnoredQualifiers(diag::warn_qual_return_type, PTI.TypeQuals,
                                  SourceLocation(), PTI.ConstQualLoc,
                                  PTI.VolatileQualLoc, PTI.RestrictQualLoc,
                                  PTI.AtomicQualLoc, PTI.UnalignedQualLoc);
      return;
    }

    case DeclaratorChunk::Function:
    case DeclaratorChunk::Array:
''',
'''    case DeclaratorChunk::Pointer: {
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
'''
),
(
'''    case DeclaratorChunk::Array:
    case DeclaratorChunk::Pointer:
      return true;
    case DeclaratorChunk::Function:
      break;
''',
'''    case DeclaratorChunk::Array:
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
      return true;
    case DeclaratorChunk::Function:
      break;
'''
),
(
'''bool isNoDerefableChunk(const DeclaratorChunk &Chunk) {
  return (Chunk.Kind == DeclaratorChunk::Pointer ||
          Chunk.Kind == DeclaratorChunk::Array);
}
''',
'''bool isNoDerefableChunk(const DeclaratorChunk &Chunk) {
  return (Chunk.Kind == DeclaratorChunk::Pointer ||
          Chunk.Kind == DeclaratorChunk::Reference ||
          Chunk.Kind == DeclaratorChunk::Array);
}
'''
),
(
'''      case DeclaratorChunk::Pointer:
        ++NumPointersRemaining;
        continue;
''',
'''      case DeclaratorChunk::Pointer:
      case DeclaratorChunk::Reference:
        ++NumPointersRemaining;
        continue;
'''
),
(
'''  case DeclaratorChunk::Function:
  case DeclaratorChunk::Array:
  case DeclaratorChunk::Paren:
  case DeclaratorChunk::Pointer:
    Loc = Chunk.Ptr.AtomicQualLoc;
    break;
''',
'''  case DeclaratorChunk::Function:
  case DeclaratorChunk::Array:
  case DeclaratorChunk::Paren:
  case DeclaratorChunk::Pointer:
    Loc = Chunk.Ptr.AtomicQualLoc;
    break;
  case DeclaratorChunk::Reference:
    Loc = Chunk.Loc;
    break;
'''
),
(
'''    case DeclaratorChunk::Pointer:
      return moveToChunk(chunk, false);

    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Array:
      continue;
''',
'''    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
      return moveToChunk(chunk, false);

    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Array:
      continue;
'''
),
]
for a,b in pairs:
    if a in t:
        t = t.replace(a,b)
        print("replaced", a.splitlines()[0][:50])
    else:
        print("miss", a.splitlines()[0][:50])

write_keep_nl(p, t, orig)
print("SemaType ok")

# TypeEmitter
p = Path("neverc/lib/Emit/Core/TypeEmitter.cpp")
orig = p.read_text(encoding="utf-8")
t = norm(orig)
if "Type::LValueReference" not in t:
    old = '''  case Type::Pointer: {
    const PointerType *PTy = cast<PointerType>(Ty);
    QualType ETy = PTy->getPointeeType();
    unsigned AS = getTargetAddressSpace(ETy);
    ResultType = llvm::PointerType::get(getLLVMContext(), AS);
    break;
  }
'''
    new = '''  case Type::Pointer: {
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
'''
    if old not in t:
        raise SystemExit("TypeEmitter case missing")
    t = t.replace(old, new, 1)
    write_keep_nl(p, t, orig)
    print("TypeEmitter ok")
else:
    print("TypeEmitter already")

# TypeLoc.cpp
p = Path("neverc/lib/Tree/Type/TypeLoc.cpp")
orig = p.read_text(encoding="utf-8")
t = norm(orig)
if "VisitLValueReferenceTypeLoc" not in t:
    old = '''  TypeLoc VisitPointerTypeLoc(PointerTypeLoc T) {
    return Visit(T.getPointeeLoc());
  }
'''
    new = '''  TypeLoc VisitPointerTypeLoc(PointerTypeLoc T) {
    return Visit(T.getPointeeLoc());
  }

  TypeLoc VisitLValueReferenceTypeLoc(LValueReferenceTypeLoc T) {
    return Visit(T.getPointeeLoc());
  }

  TypeLoc VisitRValueReferenceTypeLoc(RValueReferenceTypeLoc T) {
    return Visit(T.getPointeeLoc());
  }
'''
    if old not in t:
        raise SystemExit("TypeLoc visit missing")
    t = t.replace(old, new, 1)
    write_keep_nl(p, t, orig)
    print("TypeLoc.cpp ok")
else:
    print("TypeLoc.cpp already")

# DeclSpec.cpp ensure Reference in all kind switches - scan for incomplete
p = Path("neverc/lib/Analyze/Decl/DeclSpec.cpp")
orig = p.read_text(encoding="utf-8")
t = norm(orig)
# Find switches with Pointer without Reference nearby
count = t.count("DeclaratorChunk::Pointer")
print("DeclSpec.cpp Pointer mentions", count, "Reference", t.count("DeclaratorChunk::Reference"))
# generic add after Pointer-only lines that are case labels alone
lines = t.split("\n")
out = []
for i, line in enumerate(lines):
    out.append(line)
    if "case DeclaratorChunk::Pointer:" in line:
        # check next non-empty
        j = i+1
        while j < len(lines) and lines[j].strip()=="":
            j += 1
        if j < len(lines) and "DeclaratorChunk::Reference" not in lines[j]:
            # only auto-add if this looks like a fall-through group (next is case or return/continue shared)
            # If next line is also case, adding Reference as sibling is ok before next case? 
            # Safer: if next is case Array or case Function or return false
            nxt = lines[j].strip()
            if nxt.startswith("case DeclaratorChunk::") or nxt.startswith("return") or nxt.startswith("continue") or nxt.startswith("break"):
                ind = line[:len(line)-len(line.lstrip())]
                out.append(f"{ind}case DeclaratorChunk::Reference:")
t = "\n".join(out)
write_keep_nl(p, t, orig)
print("DeclSpec.cpp normalized")

print("REMAINING REF DONE")
