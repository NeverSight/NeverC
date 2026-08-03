from pathlib import Path
pairs = [
("neverc/include/neverc/Analyze/DeclSpec.h","IK_OperatorFunctionId"),
("neverc/include/neverc/Analyze/DeclSpec.h","setDestructorName"),
("neverc/include/neverc/Analyze/DeclSpec.h","setOperatorFunctionId"),
("neverc/lib/Syntax/Decl/DeclParserDeclarator.cpp","kw_operator"),
("neverc/lib/Syntax/Decl/DeclParserDeclarator.cpp","ParseOptionalCXXScopeSpecifier"),
("neverc/lib/Syntax/Decl/DeclParserDeclarator.cpp","tok::tilde"),
("neverc/lib/Syntax/Decl/DeclParserType.cpp","ParseCXXBaseClause"),
("neverc/lib/Syntax/Decl/DeclParserType.cpp","Tok.is(tok::colon)"),
("neverc/include/neverc/Tree/Decl/Decl.h","NumBases"),
("neverc/include/neverc/Tree/Decl/Decl.h","setBases"),
("neverc/lib/Syntax/Decl/DeclParserCXX.cpp","void Parser::ParseCXXBaseClause"),
("neverc/lib/Analyze/Decl/SemaCXX.cpp","OnBaseTypeSpecifier"),
("neverc/include/neverc/Tree/Decl/CXXBaseSpecifier.h","class CXXBaseSpecifier"),
]
for f,s in pairs:
    t = Path(f).read_text(encoding="utf-8")
    print(("OK  " if s in t else "MISS"), s, "->", f)
# show UnqualifiedId snippet
t=Path("neverc/include/neverc/Analyze/DeclSpec.h").read_text(encoding="utf-8").replace("\r\n","\n")
i=t.find("class UnqualifiedId")
print("---UID---")
print(t[i:i+900])
t=Path("neverc/lib/Syntax/Decl/DeclParserDeclarator.cpp").read_text(encoding="utf-8").replace("\r\n","\n")
i=t.find("void Parser::ParseDirectDeclarator")
print("---PDD---")
print(t[i:i+700])
t=Path("neverc/lib/Syntax/Decl/DeclParserType.cpp").read_text(encoding="utf-8").replace("\r\n","\n")
i=t.find("else if (Tok.is(tok::l_brace)")
print("---TUK---")
print(t[i:i+250])
i=t.find("if (TUK == Sema::TUK_Definition)")
print("---BODY---")
print(t[i:i+500])
