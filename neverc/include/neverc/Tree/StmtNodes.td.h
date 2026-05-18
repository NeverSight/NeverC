#ifndef ABSTRACT_STMT
#define ABSTRACT_STMT(Type) Type
#endif
#ifndef STMT_RANGE
#define STMT_RANGE(Base, First, Last)
#endif
#ifndef LAST_STMT_RANGE
#define LAST_STMT_RANGE(Base, First, Last) STMT_RANGE(Base, First, Last)
#endif

// --- Asm statements ---

#ifndef ASMSTMT
#define ASMSTMT(Type, Base) STMT(Type, Base)
#endif
ABSTRACT_STMT(ASMSTMT(AsmStmt, Stmt))
#ifndef GCCASMSTMT
#define GCCASMSTMT(Type, Base) ASMSTMT(Type, Base)
#endif
GCCASMSTMT(GCCAsmStmt, AsmStmt)
#undef GCCASMSTMT

#ifndef MSASMSTMT
#define MSASMSTMT(Type, Base) ASMSTMT(Type, Base)
#endif
MSASMSTMT(MSAsmStmt, AsmStmt)
#undef MSASMSTMT

STMT_RANGE(AsmStmt, GCCAsmStmt, MSAsmStmt)

#undef ASMSTMT

// --- Simple statements ---

#ifndef BREAKSTMT
#define BREAKSTMT(Type, Base) STMT(Type, Base)
#endif
BREAKSTMT(BreakStmt, Stmt)
#undef BREAKSTMT

#ifndef COMPOUNDSTMT
#define COMPOUNDSTMT(Type, Base) STMT(Type, Base)
#endif
COMPOUNDSTMT(CompoundStmt, Stmt)
#undef COMPOUNDSTMT

#ifndef CONTINUESTMT
#define CONTINUESTMT(Type, Base) STMT(Type, Base)
#endif
CONTINUESTMT(ContinueStmt, Stmt)
#undef CONTINUESTMT

#ifndef DECLSTMT
#define DECLSTMT(Type, Base) STMT(Type, Base)
#endif
DECLSTMT(DeclStmt, Stmt)
#undef DECLSTMT

#ifndef DOSTMT
#define DOSTMT(Type, Base) STMT(Type, Base)
#endif
DOSTMT(DoStmt, Stmt)
#undef DOSTMT

#ifndef FORSTMT
#define FORSTMT(Type, Base) STMT(Type, Base)
#endif
FORSTMT(ForStmt, Stmt)
#undef FORSTMT

#ifndef GOTOSTMT
#define GOTOSTMT(Type, Base) STMT(Type, Base)
#endif
GOTOSTMT(GotoStmt, Stmt)
#undef GOTOSTMT

#ifndef IFSTMT
#define IFSTMT(Type, Base) STMT(Type, Base)
#endif
IFSTMT(IfStmt, Stmt)
#undef IFSTMT

#ifndef INDIRECTGOTOSTMT
#define INDIRECTGOTOSTMT(Type, Base) STMT(Type, Base)
#endif
INDIRECTGOTOSTMT(IndirectGotoStmt, Stmt)
#undef INDIRECTGOTOSTMT

#ifndef NULLSTMT
#define NULLSTMT(Type, Base) STMT(Type, Base)
#endif
NULLSTMT(NullStmt, Stmt)
#undef NULLSTMT

#ifndef RETURNSTMT
#define RETURNSTMT(Type, Base) STMT(Type, Base)
#endif
RETURNSTMT(ReturnStmt, Stmt)
#undef RETURNSTMT

// --- SEH statements ---

#ifndef SEHEXCEPTSTMT
#define SEHEXCEPTSTMT(Type, Base) STMT(Type, Base)
#endif
SEHEXCEPTSTMT(SEHExceptStmt, Stmt)
#undef SEHEXCEPTSTMT

#ifndef SEHFINALLYSTMT
#define SEHFINALLYSTMT(Type, Base) STMT(Type, Base)
#endif
SEHFINALLYSTMT(SEHFinallyStmt, Stmt)
#undef SEHFINALLYSTMT

#ifndef SEHLEAVESTMT
#define SEHLEAVESTMT(Type, Base) STMT(Type, Base)
#endif
SEHLEAVESTMT(SEHLeaveStmt, Stmt)
#undef SEHLEAVESTMT

#ifndef SEHTRYSTMT
#define SEHTRYSTMT(Type, Base) STMT(Type, Base)
#endif
SEHTRYSTMT(SEHTryStmt, Stmt)
#undef SEHTRYSTMT

// --- SwitchCase ---

#ifndef SWITCHCASE
#define SWITCHCASE(Type, Base) STMT(Type, Base)
#endif
ABSTRACT_STMT(SWITCHCASE(SwitchCase, Stmt))
#ifndef CASESTMT
#define CASESTMT(Type, Base) SWITCHCASE(Type, Base)
#endif
CASESTMT(CaseStmt, SwitchCase)
#undef CASESTMT

#ifndef DEFAULTSTMT
#define DEFAULTSTMT(Type, Base) SWITCHCASE(Type, Base)
#endif
DEFAULTSTMT(DefaultStmt, SwitchCase)
#undef DEFAULTSTMT

STMT_RANGE(SwitchCase, CaseStmt, DefaultStmt)

#undef SWITCHCASE

#ifndef SWITCHSTMT
#define SWITCHSTMT(Type, Base) STMT(Type, Base)
#endif
SWITCHSTMT(SwitchStmt, Stmt)
#undef SWITCHSTMT

// --- ValueStmt hierarchy ---

#ifndef VALUESTMT
#define VALUESTMT(Type, Base) STMT(Type, Base)
#endif
ABSTRACT_STMT(VALUESTMT(ValueStmt, Stmt))
#ifndef ATTRIBUTEDSTMT
#define ATTRIBUTEDSTMT(Type, Base) VALUESTMT(Type, Base)
#endif
ATTRIBUTEDSTMT(AttributedStmt, ValueStmt)
#undef ATTRIBUTEDSTMT

// --- Expr hierarchy (child of ValueStmt) ---

#ifndef EXPR
#define EXPR(Type, Base) VALUESTMT(Type, Base)
#endif
ABSTRACT_STMT(EXPR(Expr, ValueStmt))

#ifndef ABSTRACTCONDITIONALOPERATOR
#define ABSTRACTCONDITIONALOPERATOR(Type, Base) EXPR(Type, Base)
#endif
ABSTRACT_STMT(ABSTRACTCONDITIONALOPERATOR(AbstractConditionalOperator, Expr))
#ifndef BINARYCONDITIONALOPERATOR
#define BINARYCONDITIONALOPERATOR(Type, Base)                                  \
  ABSTRACTCONDITIONALOPERATOR(Type, Base)
#endif
BINARYCONDITIONALOPERATOR(BinaryConditionalOperator,
                          AbstractConditionalOperator)
#undef BINARYCONDITIONALOPERATOR

#ifndef CONDITIONALOPERATOR
#define CONDITIONALOPERATOR(Type, Base) ABSTRACTCONDITIONALOPERATOR(Type, Base)
#endif
CONDITIONALOPERATOR(ConditionalOperator, AbstractConditionalOperator)
#undef CONDITIONALOPERATOR

STMT_RANGE(AbstractConditionalOperator, BinaryConditionalOperator,
           ConditionalOperator)

#undef ABSTRACTCONDITIONALOPERATOR

#ifndef ADDRLABELEXPR
#define ADDRLABELEXPR(Type, Base) EXPR(Type, Base)
#endif
ADDRLABELEXPR(AddrLabelExpr, Expr)
#undef ADDRLABELEXPR

#ifndef ARRAYINITINDEXEXPR
#define ARRAYINITINDEXEXPR(Type, Base) EXPR(Type, Base)
#endif
ARRAYINITINDEXEXPR(ArrayInitIndexExpr, Expr)
#undef ARRAYINITINDEXEXPR

#ifndef ARRAYINITLOOPEXPR
#define ARRAYINITLOOPEXPR(Type, Base) EXPR(Type, Base)
#endif
ARRAYINITLOOPEXPR(ArrayInitLoopExpr, Expr)
#undef ARRAYINITLOOPEXPR

#ifndef ARRAYSUBSCRIPTEXPR
#define ARRAYSUBSCRIPTEXPR(Type, Base) EXPR(Type, Base)
#endif
ARRAYSUBSCRIPTEXPR(ArraySubscriptExpr, Expr)
#undef ARRAYSUBSCRIPTEXPR

#ifndef ATOMICEXPR
#define ATOMICEXPR(Type, Base) EXPR(Type, Base)
#endif
ATOMICEXPR(AtomicExpr, Expr)
#undef ATOMICEXPR

#ifndef BINARYOPERATOR
#define BINARYOPERATOR(Type, Base) EXPR(Type, Base)
#endif
BINARYOPERATOR(BinaryOperator, Expr)
#ifndef COMPOUNDASSIGNOPERATOR
#define COMPOUNDASSIGNOPERATOR(Type, Base) BINARYOPERATOR(Type, Base)
#endif
COMPOUNDASSIGNOPERATOR(CompoundAssignOperator, BinaryOperator)
#undef COMPOUNDASSIGNOPERATOR

STMT_RANGE(BinaryOperator, BinaryOperator, CompoundAssignOperator)

#undef BINARYOPERATOR

#ifndef CALLEXPR
#define CALLEXPR(Type, Base) EXPR(Type, Base)
#endif
CALLEXPR(CallExpr, Expr)
#undef CALLEXPR

#ifndef CASTEXPR
#define CASTEXPR(Type, Base) EXPR(Type, Base)
#endif
ABSTRACT_STMT(CASTEXPR(CastExpr, Expr))
#ifndef EXPLICITCASTEXPR
#define EXPLICITCASTEXPR(Type, Base) CASTEXPR(Type, Base)
#endif
ABSTRACT_STMT(EXPLICITCASTEXPR(ExplicitCastExpr, CastExpr))
#ifndef CSTYLECASTEXPR
#define CSTYLECASTEXPR(Type, Base) EXPLICITCASTEXPR(Type, Base)
#endif
CSTYLECASTEXPR(CStyleCastExpr, ExplicitCastExpr)
#undef CSTYLECASTEXPR

STMT_RANGE(ExplicitCastExpr, CStyleCastExpr, CStyleCastExpr)

#undef EXPLICITCASTEXPR

#ifndef IMPLICITCASTEXPR
#define IMPLICITCASTEXPR(Type, Base) CASTEXPR(Type, Base)
#endif
IMPLICITCASTEXPR(ImplicitCastExpr, CastExpr)
#undef IMPLICITCASTEXPR

STMT_RANGE(CastExpr, CStyleCastExpr, ImplicitCastExpr)

#undef CASTEXPR

#ifndef CHARACTERLITERAL
#define CHARACTERLITERAL(Type, Base) EXPR(Type, Base)
#endif
CHARACTERLITERAL(CharacterLiteral, Expr)
#undef CHARACTERLITERAL

#ifndef CHOOSEEXPR
#define CHOOSEEXPR(Type, Base) EXPR(Type, Base)
#endif
CHOOSEEXPR(ChooseExpr, Expr)
#undef CHOOSEEXPR

#ifndef COMPOUNDLITERALEXPR
#define COMPOUNDLITERALEXPR(Type, Base) EXPR(Type, Base)
#endif
COMPOUNDLITERALEXPR(CompoundLiteralExpr, Expr)
#undef COMPOUNDLITERALEXPR

#ifndef CONVERTVECTOREXPR
#define CONVERTVECTOREXPR(Type, Base) EXPR(Type, Base)
#endif
CONVERTVECTOREXPR(ConvertVectorExpr, Expr)
#undef CONVERTVECTOREXPR

#ifndef DECLREFEXPR
#define DECLREFEXPR(Type, Base) EXPR(Type, Base)
#endif
DECLREFEXPR(DeclRefExpr, Expr)
#undef DECLREFEXPR

#ifndef DESIGNATEDINITEXPR
#define DESIGNATEDINITEXPR(Type, Base) EXPR(Type, Base)
#endif
DESIGNATEDINITEXPR(DesignatedInitExpr, Expr)
#undef DESIGNATEDINITEXPR

#ifndef DESIGNATEDINITUPDATEEXPR
#define DESIGNATEDINITUPDATEEXPR(Type, Base) EXPR(Type, Base)
#endif
DESIGNATEDINITUPDATEEXPR(DesignatedInitUpdateExpr, Expr)
#undef DESIGNATEDINITUPDATEEXPR

#ifndef EXTVECTORELEMENTEXPR
#define EXTVECTORELEMENTEXPR(Type, Base) EXPR(Type, Base)
#endif
EXTVECTORELEMENTEXPR(ExtVectorElementExpr, Expr)
#undef EXTVECTORELEMENTEXPR

#ifndef FIXEDPOINTLITERAL
#define FIXEDPOINTLITERAL(Type, Base) EXPR(Type, Base)
#endif
FIXEDPOINTLITERAL(FixedPointLiteral, Expr)
#undef FIXEDPOINTLITERAL

#ifndef FLOATINGLITERAL
#define FLOATINGLITERAL(Type, Base) EXPR(Type, Base)
#endif
FLOATINGLITERAL(FloatingLiteral, Expr)
#undef FLOATINGLITERAL

#ifndef FULLEXPR
#define FULLEXPR(Type, Base) EXPR(Type, Base)
#endif
ABSTRACT_STMT(FULLEXPR(FullExpr, Expr))
#ifndef CONSTANTEXPR
#define CONSTANTEXPR(Type, Base) FULLEXPR(Type, Base)
#endif
CONSTANTEXPR(ConstantExpr, FullExpr)
#undef CONSTANTEXPR

#ifndef EXPRWITHCLEANUPS
#define EXPRWITHCLEANUPS(Type, Base) FULLEXPR(Type, Base)
#endif
EXPRWITHCLEANUPS(ExprWithCleanups, FullExpr)
#undef EXPRWITHCLEANUPS

STMT_RANGE(FullExpr, ConstantExpr, ExprWithCleanups)

#undef FULLEXPR

#ifndef GENERICSELECTIONEXPR
#define GENERICSELECTIONEXPR(Type, Base) EXPR(Type, Base)
#endif
GENERICSELECTIONEXPR(GenericSelectionExpr, Expr)
#undef GENERICSELECTIONEXPR

#ifndef IMAGINARYLITERAL
#define IMAGINARYLITERAL(Type, Base) EXPR(Type, Base)
#endif
IMAGINARYLITERAL(ImaginaryLiteral, Expr)
#undef IMAGINARYLITERAL

#ifndef IMPLICITVALUEINITEXPR
#define IMPLICITVALUEINITEXPR(Type, Base) EXPR(Type, Base)
#endif
IMPLICITVALUEINITEXPR(ImplicitValueInitExpr, Expr)
#undef IMPLICITVALUEINITEXPR

#ifndef INITLISTEXPR
#define INITLISTEXPR(Type, Base) EXPR(Type, Base)
#endif
INITLISTEXPR(InitListExpr, Expr)
#undef INITLISTEXPR

#ifndef INTEGERLITERAL
#define INTEGERLITERAL(Type, Base) EXPR(Type, Base)
#endif
INTEGERLITERAL(IntegerLiteral, Expr)
#undef INTEGERLITERAL

#ifndef MATRIXSUBSCRIPTEXPR
#define MATRIXSUBSCRIPTEXPR(Type, Base) EXPR(Type, Base)
#endif
MATRIXSUBSCRIPTEXPR(MatrixSubscriptExpr, Expr)
#undef MATRIXSUBSCRIPTEXPR

#ifndef MEMBEREXPR
#define MEMBEREXPR(Type, Base) EXPR(Type, Base)
#endif
MEMBEREXPR(MemberExpr, Expr)
#undef MEMBEREXPR

#ifndef NOINITEXPR
#define NOINITEXPR(Type, Base) EXPR(Type, Base)
#endif
NOINITEXPR(NoInitExpr, Expr)
#undef NOINITEXPR

#ifndef NULLPTRLITERALEXPR
#define NULLPTRLITERALEXPR(Type, Base) EXPR(Type, Base)
#endif
NULLPTRLITERALEXPR(NullPtrLiteralExpr, Expr)
#undef NULLPTRLITERALEXPR

#ifndef OFFSETOFEXPR
#define OFFSETOFEXPR(Type, Base) EXPR(Type, Base)
#endif
OFFSETOFEXPR(OffsetOfExpr, Expr)
#undef OFFSETOFEXPR

#ifndef OPAQUEVALUEEXPR
#define OPAQUEVALUEEXPR(Type, Base) EXPR(Type, Base)
#endif
OPAQUEVALUEEXPR(OpaqueValueExpr, Expr)
#undef OPAQUEVALUEEXPR

#ifndef PARENEXPR
#define PARENEXPR(Type, Base) EXPR(Type, Base)
#endif
PARENEXPR(ParenExpr, Expr)
#undef PARENEXPR

#ifndef PARENLISTEXPR
#define PARENLISTEXPR(Type, Base) EXPR(Type, Base)
#endif
PARENLISTEXPR(ParenListExpr, Expr)
#undef PARENLISTEXPR

#ifndef PREDEFINEDEXPR
#define PREDEFINEDEXPR(Type, Base) EXPR(Type, Base)
#endif
PREDEFINEDEXPR(PredefinedExpr, Expr)
#undef PREDEFINEDEXPR

#ifndef PSEUDOOBJECTEXPR
#define PSEUDOOBJECTEXPR(Type, Base) EXPR(Type, Base)
#endif
PSEUDOOBJECTEXPR(PseudoObjectExpr, Expr)
#undef PSEUDOOBJECTEXPR

#ifndef RECOVERYEXPR
#define RECOVERYEXPR(Type, Base) EXPR(Type, Base)
#endif
RECOVERYEXPR(RecoveryExpr, Expr)
#undef RECOVERYEXPR

#ifndef SHUFFLEVECTOREXPR
#define SHUFFLEVECTOREXPR(Type, Base) EXPR(Type, Base)
#endif
SHUFFLEVECTOREXPR(ShuffleVectorExpr, Expr)
#undef SHUFFLEVECTOREXPR

#ifndef SOURCELOCEXPR
#define SOURCELOCEXPR(Type, Base) EXPR(Type, Base)
#endif
SOURCELOCEXPR(SourceLocExpr, Expr)
#undef SOURCELOCEXPR

#ifndef STMTEXPR
#define STMTEXPR(Type, Base) EXPR(Type, Base)
#endif
STMTEXPR(StmtExpr, Expr)
#undef STMTEXPR

#ifndef STRINGLITERAL
#define STRINGLITERAL(Type, Base) EXPR(Type, Base)
#endif
STRINGLITERAL(StringLiteral, Expr)
#undef STRINGLITERAL

#ifndef TYPOEXPR
#define TYPOEXPR(Type, Base) EXPR(Type, Base)
#endif
TYPOEXPR(TypoExpr, Expr)
#undef TYPOEXPR

#ifndef UNARYEXPRORTYPETRAITEXPR
#define UNARYEXPRORTYPETRAITEXPR(Type, Base) EXPR(Type, Base)
#endif
UNARYEXPRORTYPETRAITEXPR(UnaryExprOrTypeTraitExpr, Expr)
#undef UNARYEXPRORTYPETRAITEXPR

#ifndef UNARYOPERATOR
#define UNARYOPERATOR(Type, Base) EXPR(Type, Base)
#endif
UNARYOPERATOR(UnaryOperator, Expr)
#undef UNARYOPERATOR

#ifndef VAARGEXPR
#define VAARGEXPR(Type, Base) EXPR(Type, Base)
#endif
VAARGEXPR(VAArgExpr, Expr)
#undef VAARGEXPR

STMT_RANGE(Expr, BinaryConditionalOperator, VAArgExpr)

#undef EXPR

#ifndef LABELSTMT
#define LABELSTMT(Type, Base) VALUESTMT(Type, Base)
#endif
LABELSTMT(LabelStmt, ValueStmt)
#undef LABELSTMT

STMT_RANGE(ValueStmt, AttributedStmt, LabelStmt)

#undef VALUESTMT

// --- WhileStmt ---

#ifndef WHILESTMT
#define WHILESTMT(Type, Base) STMT(Type, Base)
#endif
WHILESTMT(WhileStmt, Stmt)
#undef WHILESTMT

LAST_STMT_RANGE(Stmt, GCCAsmStmt, WhileStmt)

#undef STMT
#undef STMT_RANGE
#undef LAST_STMT_RANGE
#undef ABSTRACT_STMT
