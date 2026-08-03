from pathlib import Path
import re

def norm(s):
    return s.replace("\r\n", "\n").replace("\r", "\n")

def write_keep_nl(path, text, original):
    if "\r\n" in original:
        text = text.replace("\n", "\r\n")
    Path(path).write_text(text, encoding="utf-8", newline="")

# Sema.h
p = Path("neverc/include/neverc/Analyze/Sema.h")
orig = p.read_text(encoding="utf-8")
t = norm(orig)
if "FormReferenceType" not in t:
    m = re.search(r"QualType FormPointerType\([\s\S]*?\);", t)
    if not m:
        raise SystemExit("no FormPointerType")
    insert = (m.group(0) +
              "\n  QualType FormReferenceType(QualType T, bool LValueRef,\n"
              "                            SourceLocation Loc,\n"
              "                            DeclarationName Entity = DeclarationName());")
    t = t[:m.start()] + insert + t[m.end():]
    write_keep_nl(p, t, orig)
    print("Sema.h")
else:
    print("Sema.h already")

# SemaType
p = Path("neverc/lib/Analyze/Type/SemaType.cpp")
orig = p.read_text(encoding="utf-8")
t = norm(orig)
if "FormReferenceType" not in t:
    old = """QualType Sema::FormPointerType(QualType T, SourceLocation Loc,
                               DeclarationName Entity) {
  if (checkQualifiedFunction(*this, T, Loc))
    return QualType();

  return Context.getPointerType(T);
}
"""
    new = """QualType Sema::FormPointerType(QualType T, SourceLocation Loc,
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
  if (const ReferenceType *RT = T->getAs<ReferenceType>())
    T = RT->getPointeeType();
  if (LValueRef)
    return Context.getLValueReferenceType(T);
  return Context.getRValueReferenceType(T);
}
"""
    assert old in t, "FormPointer def missing"
    t = t.replace(old, new, 1)
    print("def")

if "FormReferenceType(T" not in t:
    old = """    case DeclaratorChunk::Pointer:
      inferPointerNullability(SimplePointerKind::Pointer, DeclType.Loc,
                              DeclType.EndLoc, DeclType.getAttrs(),
                              state.getDeclarator().getAttributePool());

      T = S.FormPointerType(T, DeclType.Loc, Name);
      if (DeclType.Ptr.TypeQuals)
        T = S.FormQualifiedType(T, DeclType.Loc, DeclType.Ptr.TypeQuals);
      break;
    case DeclaratorChunk::Array:"""
    new = """    case DeclaratorChunk::Pointer:
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
    case DeclaratorChunk::Array:"""
    assert old in t, "build missing"
    t = t.replace(old, new, 1)
    print("build")

pairs = [
("""    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Array:
      return result;
""",
 """    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::Array:
      return result;
"""),
("""        case DeclaratorChunk::Pointer:
          result = &ptrChunk;
          goto continue_outer;
""",
 """        case DeclaratorChunk::Pointer:
        case DeclaratorChunk::Reference:
          result = &ptrChunk;
          goto continue_outer;
"""),
("""    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Array:
      continue;
""",
 """    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::Array:
      continue;
"""),
("""    case DeclaratorChunk::Pointer: {
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
"""),
("""    case DeclaratorChunk::Array:
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
"""),
("""bool isNoDerefableChunk(const DeclaratorChunk &Chunk) {
  return (Chunk.Kind == DeclaratorChunk::Pointer ||
          Chunk.Kind == DeclaratorChunk::Array);
}
""",
 """bool isNoDerefableChunk(const DeclaratorChunk &Chunk) {
  return (Chunk.Kind == DeclaratorChunk::Pointer ||
          Chunk.Kind == DeclaratorChunk::Reference ||
          Chunk.Kind == DeclaratorChunk::Array);
}
"""),
("""      case DeclaratorChunk::Pointer:
        ++NumPointersRemaining;
        continue;
""",
 """      case DeclaratorChunk::Pointer:
      case DeclaratorChunk::Reference:
        ++NumPointersRemaining;
        continue;
"""),
("""  case DeclaratorChunk::Function:
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
"""),
("""    case DeclaratorChunk::Pointer:
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
"""),
]
for a,b in pairs:
    c = t.count(a)
    if c:
        t = t.replace(a,b)
        print("ok", a.splitlines()[0][:40], c)
    else:
        print("miss", a.splitlines()[0][:40])

# Generic: any remaining case Pointer without following Reference in same switch group
# Fill TypeLoc for Reference if there's PointerTypeLoc fill code
if "setStarLoc" in t and "setAmpLoc" not in t:
    # find PointerTypeLoc fill
    idx = t.find("setStarLoc")
    print("setStarLoc context:", repr(t[idx-120:idx+200]))

write_keep_nl(p, t, orig)
print("SemaType written")

# TypeEmitter
p = Path("neverc/lib/Emit/Core/TypeEmitter.cpp")
orig = p.read_text(encoding="utf-8")
t = norm(orig)
if "Type::LValueReference" not in t:
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
    assert old in t
    t = t.replace(old, new, 1)
    write_keep_nl(p, t, orig)
    print("TypeEmitter")
else:
    print("TypeEmitter already")

# TypeLoc.cpp
p = Path("neverc/lib/Tree/Type/TypeLoc.cpp")
orig = p.read_text(encoding="utf-8")
t = norm(orig)
if "VisitLValueReferenceTypeLoc" not in t:
    old = """  TypeLoc VisitPointerTypeLoc(PointerTypeLoc T) {
    return Visit(T.getPointeeLoc());
  }
"""
    new = """  TypeLoc VisitPointerTypeLoc(PointerTypeLoc T) {
    return Visit(T.getPointeeLoc());
  }

  TypeLoc VisitLValueReferenceTypeLoc(LValueReferenceTypeLoc T) {
    return Visit(T.getPointeeLoc());
  }

  TypeLoc VisitRValueReferenceTypeLoc(RValueReferenceTypeLoc T) {
    return Visit(T.getPointeeLoc());
  }
"""
    assert old in t
    t = t.replace(old, new, 1)
    write_keep_nl(p, t, orig)
    print("TypeLoc.cpp")
else:
    print("TypeLoc already")

# DeclSpec.cpp remaining
p = Path("neverc/lib/Analyze/Decl/DeclSpec.cpp")
orig = p.read_text(encoding="utf-8")
t = norm(orig)
lines = t.split("\n")
out=[]
added=0
for i,line in enumerate(lines):
    out.append(line)
    if "case DeclaratorChunk::Pointer:" in line:
        j=i+1
        while j < len(lines) and lines[j].strip()=="":
            j+=1
        if j < len(lines) and "DeclaratorChunk::Reference" not in lines[j]:
            nxt=lines[j].strip()
            if nxt.startswith("case ") or nxt.startswith("return") or nxt.startswith("continue") or nxt.startswith("break") or nxt.startswith("}"):
                ind=line[:len(line)-len(line.lstrip())]
                out.append(f"{ind}case DeclaratorChunk::Reference:")
                added += 1
t="\n".join(out)
write_keep_nl(p, t, orig)
print("DeclSpec added", added)

# TypeLoc fill in SemaType - search for castAs<PointerTypeLoc>
p = Path("neverc/lib/Analyze/Type/SemaType.cpp")
t = norm(p.read_text(encoding="utf-8"))
for needle in ["PointerTypeLoc", "setStarLoc", "TL.setStarLoc", "castAs<PointerTypeLoc>"]:
    i = t.find(needle)
    if i>=0:
        print(needle, repr(t[max(0,i-80):i+180]))
print("DONE")
