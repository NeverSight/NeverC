from pathlib import Path
import re

# Parser
p = Path("neverc/lib/Syntax/Decl/DeclParserDeclarator.cpp")
t = p.read_text(encoding="utf-8")
print("parser snippet:")
i = t.find("isPointerOpToken")
if i < 0:
    i = t.find("ParseDeclaratorInternal")
print(repr(t[i:i+700]))

# FormPointerType in Sema.h
p = Path("neverc/include/neverc/Analyze/Sema.h")
t = p.read_text(encoding="utf-8")
m = re.search(r".*FormPointerType.*", t)
print("Sema FormPointer:", m.group(0) if m else "none")
for m in re.finditer(r"FormPointerType", t):
    print(repr(t[m.start()-30:m.end()+80]))
    break

# SemaType FormPointerType
p = Path("neverc/lib/Analyze/Type/SemaType.cpp")
t = p.read_text(encoding="utf-8")
i = t.find("QualType Sema::FormPointerType")
print("SemaType FormPointer:", repr(t[i:i+250]))
i = t.find("case DeclaratorChunk::Pointer:")
# find the build one with FormPointerType
i = t.find("T = S.FormPointerType")
print("build:", repr(t[i-200:i+200]))
