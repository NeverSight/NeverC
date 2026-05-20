#ifndef NEVERC_SYNTAX_SYNTAXPARSER_H
#define NEVERC_SYNTAX_SYNTAXPARSER_H

#include "neverc/Analyze/Sema.h"
#include "neverc/Foundation/Core/OperatorPrecedence.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/SaveAndRestore.h"

namespace neverc {
class PragmaDispatch;
class Scope;
class BalancedDelimiterTracker;
class CorrectionCandidateCallback;
class DeclGroupRef;
class DiagnosticBuilder;
class Parser;
class ParsingDeclSpec;
class ParsingDeclarator;
class ParsingFieldDeclarator;
class ColonProtectionRAIIObject;

class Parser {
  friend class ColonProtectionRAIIObject;
  friend class OffsetOfStateRAIIObject;
  friend class ParenBraceBracketBalancer;
  friend class BalancedDelimiterTracker;

  PrepEngine &PP;
  Token Tok;
  SourceLocation PrevTokLocation;

  unsigned short ParenCount = 0, BracketCount = 0, BraceCount = 0;

  Sema &Actions;

  DiagnosticsEngine &Diags;

  enum { ScopeCacheSize = 16 };
  unsigned NumCachedScopes;
  Scope *ScopeCache[ScopeCacheSize];

  IdentifierInfo *Ident__except;
  IdentifierInfo *Ident_introduced;
  IdentifierInfo *Ident_deprecated;
  IdentifierInfo *Ident_obsoleted;
  IdentifierInfo *Ident_unavailable;
  IdentifierInfo *Ident_message;
  IdentifierInfo *Ident_strict;
  IdentifierInfo *Ident_replacement;

  std::unique_ptr<PragmaDispatch> AlignHandler;
  std::unique_ptr<PragmaDispatch> GCCVisibilityHandler;
  std::unique_ptr<PragmaDispatch> OptionsHandler;
  std::unique_ptr<PragmaDispatch> PackHandler;
  std::unique_ptr<PragmaDispatch> MSStructHandler;
  std::unique_ptr<PragmaDispatch> UnusedHandler;
  std::unique_ptr<PragmaDispatch> WeakHandler;
  std::unique_ptr<PragmaDispatch> FPContractHandler;
  std::unique_ptr<PragmaDispatch> PCSectionHandler;
  std::unique_ptr<PragmaDispatch> MSCommentHandler;
  std::unique_ptr<PragmaDispatch> MSDetectMismatchHandler;
  std::unique_ptr<PragmaDispatch> FPEvalMethodHandler;
  std::unique_ptr<PragmaDispatch> FloatControlHandler;
  std::unique_ptr<PragmaDispatch> MSInitSeg;
  std::unique_ptr<PragmaDispatch> MSDataSeg;
  std::unique_ptr<PragmaDispatch> MSBSSSeg;
  std::unique_ptr<PragmaDispatch> MSConstSeg;
  std::unique_ptr<PragmaDispatch> MSCodeSeg;
  std::unique_ptr<PragmaDispatch> MSSection;
  std::unique_ptr<PragmaDispatch> MSStrictGuardStackCheck;
  std::unique_ptr<PragmaDispatch> MSRuntimeChecks;
  std::unique_ptr<PragmaDispatch> MSIntrinsic;
  std::unique_ptr<PragmaDispatch> MSFunction;
  std::unique_ptr<PragmaDispatch> MSOptimize;
  std::unique_ptr<PragmaDispatch> MSFenvAccess;
  std::unique_ptr<PragmaDispatch> MSAllocText;
  std::unique_ptr<PragmaDispatch> OptimizeHandler;
  std::unique_ptr<PragmaDispatch> FPHandler;
  std::unique_ptr<PragmaDispatch> STDCFenvAccessHandler;
  std::unique_ptr<PragmaDispatch> STDCFenvRoundHandler;
  std::unique_ptr<PragmaDispatch> STDCCXLIMITHandler;
  std::unique_ptr<PragmaDispatch> STDCUnknownHandler;
  std::unique_ptr<PragmaDispatch> AttributePragmaHandler;
  std::unique_ptr<PragmaDispatch> MaxTokensHerePragmaHandler;
  std::unique_ptr<PragmaDispatch> MaxTokensTotalPragmaHandler;

  bool ColonIsSacred;

  Sema::OffsetOfKind OffsetOfState = Sema::OffsetOfKind::OOK_Outside;

  AttributeFactory AttrFactory;

  llvm::SmallVector<IdentifierInfo *, 8> TentativelyDeclaredIdentifiers;

  IdentifierInfo *getSEHExceptKeyword();

  enum class ParsedStmtContext {
    /// This context is at the top level of a GNU statement expression.
    InStmtExpr = 0x1,

    /// The context of a regular substatement.
    SubStmt = 0,
    /// The context of a compound-statement.
    Compound = 0,

    LLVM_MARK_AS_BITMASK_ENUM(InStmtExpr)
  };

  StmtResult finalizeExprStmt(ExprResult E, ParsedStmtContext StmtCtx);

public:
  Parser(PrepEngine &PP, Sema &Actions);
  ~Parser();

  const LangOptions &getLangOpts() const { return PP.getLangOpts(); }
  const TargetInfo &getTargetInfo() const { return PP.getTargetInfo(); }
  PrepEngine &getPrepEngine() const { return PP; }
  Sema &getActions() const { return Actions; }
  AttributeFactory &getAttrFactory() { return AttrFactory; }

  const Token &getCurToken() const { return Tok; }
  Scope *getCurScope() const { return Actions.getCurScope(); }
  void incrementMSManglingNumber() const {
    return Actions.incrementMSManglingNumber();
  }

  // Type forwarding.  All of these are statically 'void*', but they may all be
  // different actual classes based on the actions in place.
  typedef OpaquePtr<DeclGroupRef> DeclGroupPtrTy;
  typedef Sema::FullExprArg FullExprArg;

  using StmtVector = llvm::SmallVector<Stmt *, 32>;

  // Parsing methods.

  void Initialize();

  bool ParseFirstTopLevelDecl(DeclGroupPtrTy &Result);

  bool ParseTopLevelDecl(DeclGroupPtrTy &Result);

  SourceLocation ConsumeToken() {
    assert(!isTokenSpecial() &&
           "Should consume special tokens with Consume*Token");
    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return PrevTokLocation;
  }

  bool TryConsumeToken(tok::TokenKind Expected) {
    if (Tok.isNot(Expected))
      return false;
    assert(!isTokenSpecial() &&
           "Should consume special tokens with Consume*Token");
    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return true;
  }

  bool TryConsumeToken(tok::TokenKind Expected, SourceLocation &Loc) {
    if (!TryConsumeToken(Expected))
      return false;
    Loc = PrevTokLocation;
    return true;
  }

  SourceLocation ConsumeAnyToken() {
    switch (Tok.getKind()) {
    case tok::l_paren:
    case tok::r_paren:
      return ConsumeParen();
    case tok::l_square:
    case tok::r_square:
      return ConsumeBracket();
    case tok::l_brace:
    case tok::r_brace:
      return ConsumeBrace();
    default:
      if (LLVM_UNLIKELY(isTokenStringLiteral()))
        return ConsumeStringToken();
      if (LLVM_UNLIKELY(Tok.isAnnotation()))
        return ConsumeAnnotationToken();
      return ConsumeToken();
    }
  }

  SourceLocation getEndOfPreviousToken() {
    return PP.getLocForEndOfToken(PrevTokLocation);
  }

  IdentifierInfo *getNullabilityKeyword(NullabilityKind nullability) {
    return Actions.getNullabilityKeyword(nullability);
  }

private:
  //===--------------------------------------------------------------------===//
  // Low-Level token peeking and consumption methods.
  //

  bool isTokenParen() const { return Tok.isOneOf(tok::l_paren, tok::r_paren); }
  bool isTokenBracket() const {
    return Tok.isOneOf(tok::l_square, tok::r_square);
  }
  bool isTokenBrace() const { return Tok.isOneOf(tok::l_brace, tok::r_brace); }
  bool isTokenStringLiteral() const {
    return tok::isStringLiteral(Tok.getKind());
  }
  bool isTokenSpecial() const {
    return isTokenStringLiteral() || isTokenParen() || isTokenBracket() ||
           isTokenBrace() || Tok.isAnnotation();
  }

  bool isTokenEqualOrEqualTypo();

  void UnconsumeToken(Token &Consumed) {
    Token Next = Tok;
    PP.InjectToken(Consumed, /*IsReinject*/ true);
    PP.Lex(Tok);
    PP.InjectToken(Next, /*IsReinject*/ true);
  }

  SourceLocation ConsumeAnnotationToken() {
    assert(Tok.isAnnotation() && "wrong consume method");
    SourceLocation Loc = Tok.getLocation();
    PrevTokLocation = Tok.getAnnotationEndLoc();
    PP.Lex(Tok);
    return Loc;
  }

  SourceLocation ConsumeParen() {
    assert(isTokenParen() && "wrong consume method");
    if (Tok.getKind() == tok::l_paren)
      ++ParenCount;
    else if (ParenCount) {
      --ParenCount; // Don't let unbalanced )'s drive the count negative.
    }
    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return PrevTokLocation;
  }

  SourceLocation ConsumeBracket() {
    assert(isTokenBracket() && "wrong consume method");
    if (Tok.getKind() == tok::l_square)
      ++BracketCount;
    else if (BracketCount) {
      --BracketCount; // Don't let unbalanced ]'s drive the count negative.
    }

    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return PrevTokLocation;
  }

  SourceLocation ConsumeBrace() {
    assert(isTokenBrace() && "wrong consume method");
    if (Tok.getKind() == tok::l_brace)
      ++BraceCount;
    else if (BraceCount) {
      --BraceCount; // Don't let unbalanced }'s drive the count negative.
    }

    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return PrevTokLocation;
  }

  SourceLocation ConsumeStringToken() {
    assert(isTokenStringLiteral() &&
           "Should only consume string literals with this method");
    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return PrevTokLocation;
  }

  void cutOffParsing() { Tok.setKind(tok::eof); }

  bool isEofOrEom() { return Tok.getKind() == tok::eof; }

  void initializePragmaHandlers();

  void resetPragmaHandlers();

  void ProcessPragmaUnused();

  void ProcessPragmaVisibility();

  void ProcessPragmaPack();

  void ProcessPragmaMSStruct();

  void ProcessPragmaMSPragma();
  bool ProcessPragmaMSSection(llvm::StringRef PragmaName,
                              SourceLocation PragmaLocation);
  bool ProcessPragmaMSSegment(llvm::StringRef PragmaName,
                              SourceLocation PragmaLocation);
  bool ProcessPragmaMSInitSeg(llvm::StringRef PragmaName,
                              SourceLocation PragmaLocation);
  bool ProcessPragmaMSStrictGuardStackCheck(llvm::StringRef PragmaName,
                                            SourceLocation PragmaLocation);
  bool ProcessPragmaMSFunction(llvm::StringRef PragmaName,
                               SourceLocation PragmaLocation);
  bool ProcessPragmaMSAllocText(llvm::StringRef PragmaName,
                                SourceLocation PragmaLocation);
  bool ProcessPragmaMSOptimize(llvm::StringRef PragmaName,
                               SourceLocation PragmaLocation);

  void ProcessPragmaAlign();

  void ProcessPragmaDump();

  void ProcessPragmaWeak();

  void ProcessPragmaWeakAlias();

  void ProcessPragmaFPContract();

  void ProcessPragmaFEnvAccess();

  void ProcessPragmaFEnvRound();

  void ProcessPragmaCXLimitedRange();

  void ProcessPragmaFloatControl();

  void ProcessPragmaFP();

  bool ParsePragmaAttributeSubjectMatchRuleSet(
      attr::ParsedSubjectMatchRuleSet &SubjectMatchRules,
      SourceLocation &AnyLoc, SourceLocation &LastMatchRuleEndLoc);

  void ProcessPragmaAttribute();

  const Token &GetLookAheadToken(unsigned N) {
    if (N == 0 || Tok.is(tok::eof))
      return Tok;
    return PP.LookAhead(N - 1);
  }

public:
  const Token &NextToken() { return PP.LookAhead(0); }

  static TypeResult getTypeAnnotation(const Token &Tok) {
    if (!Tok.getAnnotationValue())
      return TypeError();
    return ParsedType::getFromOpaquePtr(Tok.getAnnotationValue());
  }

private:
  static void setTypeAnnotation(Token &Tok, TypeResult T) {
    assert((T.isInvalid() || T.get()) &&
           "produced a valid-but-null type annotation?");
    Tok.setAnnotationValue(T.isInvalid() ? nullptr : T.get().getAsOpaquePtr());
  }

public:
  void TryResolveTypeAnnotation();

private:
  enum AnnotatedNameKind {
    /// Annotation has failed and emitted an error.
    ANK_Error,
    /// The identifier is a tentatively-declared name.
    ANK_TentativeDecl,
    /// The identifier can't be resolved.
    ANK_Unresolved,
    /// Annotation was successful.
    ANK_Success
  };

  AnnotatedNameKind TryAnnotateName(CorrectionCandidateCallback *CCC = nullptr);
  AnnotatedNameKind TryAnnotateName(const Token &NextTok,
                                    CorrectionCandidateCallback *CCC);

  bool TryKeywordIdentFallback(bool DisableKeyword);

  class TentativeParsingAction {
    Parser &P;
    Token PrevTok;
    size_t PrevTentativelyDeclaredIdentifierCount;
    unsigned short PrevParenCount, PrevBracketCount, PrevBraceCount;
    bool isActive;

  public:
    explicit TentativeParsingAction(Parser &p) : P(p) {
      PrevTok = P.Tok;
      PrevTentativelyDeclaredIdentifierCount =
          P.TentativelyDeclaredIdentifiers.size();
      PrevParenCount = P.ParenCount;
      PrevBracketCount = P.BracketCount;
      PrevBraceCount = P.BraceCount;
      P.PP.SaveLexState();
      isActive = true;
    }
    void Commit() {
      assert(isActive && "Parsing action was finished!");
      P.TentativelyDeclaredIdentifiers.resize(
          PrevTentativelyDeclaredIdentifierCount);
      P.PP.DropSavedState();
      isActive = false;
    }
    void Revert() {
      assert(isActive && "Parsing action was finished!");
      P.PP.RestoreLexState();
      P.Tok = PrevTok;
      P.TentativelyDeclaredIdentifiers.resize(
          PrevTentativelyDeclaredIdentifierCount);
      P.ParenCount = PrevParenCount;
      P.BracketCount = PrevBracketCount;
      P.BraceCount = PrevBraceCount;
      isActive = false;
    }
    ~TentativeParsingAction() {
      assert(!isActive && "Forgot to call Commit or Revert!");
    }
  };
  class RevertingTentativeParsingAction
      : private Parser::TentativeParsingAction {
  public:
    RevertingTentativeParsingAction(Parser &P)
        : Parser::TentativeParsingAction(P) {}
    ~RevertingTentativeParsingAction() { Revert(); }
  };

  bool RequireToken(tok::TokenKind ExpectedTok,
                    unsigned Diag = diag::err_expected,
                    llvm::StringRef DiagMsg = "");

  bool RequireSemicolon(unsigned DiagID, llvm::StringRef TokenUsed = "");

  enum ExtraSemiKind {
    OutsideFunction = 0,
    InsideStruct = 1,
  };

  void ConsumeRedundantSemicolons(ExtraSemiKind Kind,
                                  DeclSpec::TST T = TST_unspecified);

  enum class CompoundToken {
    /// A '(' '{' beginning a statement-expression.
    StmtExprBegin,
    /// A '}' ')' ending a statement-expression.
    StmtExprEnd,
    /// A '[' '[' beginning a \c [[...]] attribute.
    AttrBegin,
    /// A ']' ']' ending a \c [[...]] attribute.
    AttrEnd,
  };

  void checkCompoundToken(SourceLocation FirstTokLoc,
                          tok::TokenKind FirstTokKind, CompoundToken Op);

public:
  //===--------------------------------------------------------------------===//
  // Scope manipulation

  class ParseScope {
    Parser *Self;
    ParseScope(const ParseScope &) = delete;
    void operator=(const ParseScope &) = delete;

  public:
    // ParseScope - Construct a new object to manage a scope in the
    // parser Self where the new Scope is created with the flags
    // ScopeFlags, but only when we aren't about to enter a compound statement.
    ParseScope(Parser *Self, unsigned ScopeFlags, bool EnteredScope = true,
               bool BeforeCompoundStmt = false)
        : Self(Self) {
      if (EnteredScope && !BeforeCompoundStmt)
        Self->PushScope(ScopeFlags);
      else {
        if (BeforeCompoundStmt)
          Self->incrementMSManglingNumber();

        this->Self = nullptr;
      }
    }

    // Exit - Exit the scope associated with this object now, rather
    // than waiting until the object is destroyed.
    void Exit() {
      if (Self) {
        Self->PopScope();
        Self = nullptr;
      }
    }

    ~ParseScope() { Exit(); }
  };

  class MultiParseScope {
    Parser &Self;
    unsigned NumScopes = 0;

    MultiParseScope(const MultiParseScope &) = delete;

  public:
    MultiParseScope(Parser &Self) : Self(Self) {}
    void Enter(unsigned ScopeFlags) {
      Self.PushScope(ScopeFlags);
      ++NumScopes;
    }
    void Exit() {
      while (NumScopes) {
        Self.PopScope();
        --NumScopes;
      }
    }
    ~MultiParseScope() { Exit(); }
  };

  void PushScope(unsigned ScopeFlags);

  void PopScope();

private:
  class ParseScopeFlags {
    Scope *CurScope;
    unsigned OldFlags = 0;
    ParseScopeFlags(const ParseScopeFlags &) = delete;
    void operator=(const ParseScopeFlags &) = delete;

  public:
    ParseScopeFlags(Parser *Self, unsigned ScopeFlags, bool ManageFlags = true);
    ~ParseScopeFlags();
  };

  //===--------------------------------------------------------------------===//
  // Diagnostic Emission and Error recovery.

public:
  DiagnosticBuilder Diag(SourceLocation Loc, unsigned DiagID);
  DiagnosticBuilder Diag(const Token &Tok, unsigned DiagID);
  DiagnosticBuilder Diag(unsigned DiagID) { return Diag(Tok, DiagID); }

private:
  void SuggestParentheses(SourceLocation Loc, unsigned DK,
                          SourceRange ParenRange);

public:
  enum SkipUntilFlags {
    StopAtSemi = 1 << 0, ///< Stop skipping at semicolon
    /// Stop skipping at specified token, but don't skip the token itself
    StopBeforeMatch = 1 << 1
  };

  friend constexpr SkipUntilFlags operator|(SkipUntilFlags L,
                                            SkipUntilFlags R) {
    return static_cast<SkipUntilFlags>(static_cast<unsigned>(L) |
                                       static_cast<unsigned>(R));
  }

  bool SkipUntil(tok::TokenKind T,
                 SkipUntilFlags Flags = static_cast<SkipUntilFlags>(0)) {
    return SkipUntil(llvm::ArrayRef(T), Flags);
  }
  bool SkipUntil(tok::TokenKind T1, tok::TokenKind T2,
                 SkipUntilFlags Flags = static_cast<SkipUntilFlags>(0)) {
    tok::TokenKind TokArray[] = {T1, T2};
    return SkipUntil(TokArray, Flags);
  }
  bool SkipUntil(tok::TokenKind T1, tok::TokenKind T2, tok::TokenKind T3,
                 SkipUntilFlags Flags = static_cast<SkipUntilFlags>(0)) {
    tok::TokenKind TokArray[] = {T1, T2, T3};
    return SkipUntil(TokArray, Flags);
  }
  bool SkipUntil(llvm::ArrayRef<tok::TokenKind> Toks,
                 SkipUntilFlags Flags = static_cast<SkipUntilFlags>(0));

  void SkipBrokenDecl();

  SourceLocation MisleadingIndentationElseLoc;

private:
  //===--------------------------------------------------------------------===//
  // Late-parsed declarations (attributes, pragmas)

  class LateParsedDeclaration {
  public:
    virtual ~LateParsedDeclaration();

    virtual void ParseLexedAttributes();
    virtual void ParseLexedPragmas();
  };

  struct LateParsedAttribute : public LateParsedDeclaration {
    Parser *Self;
    CachedTokens Toks;
    IdentifierInfo &AttrName;
    IdentifierInfo *MacroII = nullptr;
    SourceLocation AttrNameLoc;
    llvm::SmallVector<Decl *, 2> Decls;

    explicit LateParsedAttribute(Parser *P, IdentifierInfo &Name,
                                 SourceLocation Loc)
        : Self(P), AttrName(Name), AttrNameLoc(Loc) {}

    void ParseLexedAttributes() override;

    void addDecl(Decl *D) { Decls.push_back(D); }
  };

  // A list of late-parsed attributes.  Used by ParseGNUAttributes.
  class LateParsedAttrList
      : public llvm::SmallVector<LateParsedAttribute *, 2> {
  public:
    LateParsedAttrList(bool PSoon = false) : ParseSoon(PSoon) {}

    bool parseSoon() { return ParseSoon; }

  private:
    bool ParseSoon; // Are we planning to parse these shortly after creation?
  };

  using LateParsedDeclarationsContainer =
      llvm::SmallVector<LateParsedDeclaration *, 2>;

  enum CachedInitKind { CIK_DefaultArgument, CIK_DefaultInitializer };

  void ParseLexedAttributeList(LateParsedAttrList &LAs, Decl *D, bool PushScope,
                               bool OnDefinition);
  void ParseLexedAttribute(LateParsedAttribute &LA, bool PushScope,
                           bool OnDefinition);
  bool CacheFunctionPrologue(CachedTokens &Toks);
  bool ConsumeAndStoreInitializer(CachedTokens &Toks, CachedInitKind CIK);
  bool CacheTokensUntil(tok::TokenKind T1, CachedTokens &Toks,
                        bool StopAtSemi = true, bool ConsumeFinalToken = true) {
    return CacheTokensUntil(T1, T1, Toks, StopAtSemi, ConsumeFinalToken);
  }
  bool CacheTokensUntil(tok::TokenKind T1, tok::TokenKind T2,
                        CachedTokens &Toks, bool StopAtSemi = true,
                        bool ConsumeFinalToken = true);

  //===--------------------------------------------------------------------===//
  // C99 6.9: External Definitions.
  DeclGroupPtrTy ParseExternalDeclaration(ParsedAttributes &DeclAttrs,
                                          ParsedAttributes &DeclSpecAttrs,
                                          ParsingDeclSpec *DS = nullptr);
  bool isDeclarationAfterDeclarator();
  bool isStartOfFunctionDefinition(const ParsingDeclarator &Declarator);
  DeclGroupPtrTy ParseDeclarationOrFunctionDefinition(
      ParsedAttributes &DeclAttrs, ParsedAttributes &DeclSpecAttrs,
      ParsingDeclSpec *DS = nullptr, AccessSpecifier AS = AS_none);
  DeclGroupPtrTy ParseDeclOrFunctionDefInternal(ParsedAttributes &Attrs,
                                                ParsedAttributes &DeclSpecAttrs,
                                                ParsingDeclSpec &DS,
                                                AccessSpecifier AS);

  void SkipFunctionBody();
  Decl *ParseFunctionDefinition(ParsingDeclarator &D,
                                LateParsedAttrList *LateParsedAttrs = nullptr);
  void ParseOldStyleParams(Declarator &D);
  // EndLoc is filled with the location of the last token of the simple-asm.
  ExprResult ParseSimpleAsm(bool ForAsmLabel, SourceLocation *EndLoc);
  ExprResult ParseAsmStringLiteral(bool ForAsmLabel);

public:
  //===--------------------------------------------------------------------===//
  // C99 6.5: Expressions.

  enum TypeCastState { NotTypeCast = 0, MaybeTypeCast, IsTypeCast };

  ExprResult ParseExpression(TypeCastState isTypeCast = NotTypeCast);
  ExprResult ParseConstantExpressionInExprEvalContext(
      TypeCastState isTypeCast = NotTypeCast);
  ExprResult ParseConstantExpression();
  ExprResult ParseCaseExpression(SourceLocation CaseLoc);
  // Expr that doesn't include commas.
  ExprResult ParseAssignmentExpression(TypeCastState isTypeCast = NotTypeCast);

  ExprResult ParseMSAsmIdentifier(llvm::SmallVectorImpl<Token> &LineToks,
                                  unsigned &NumLineToksConsumed,
                                  bool IsUnevaluated);

  ExprResult ParseStringLiteralExpression();
  ExprResult ParseUnevaluatedStringLiteralExpression();

private:
  ExprResult ParseStringLiteralExpression(bool Unevaluated);

  ExprResult ParseExpressionWithLeadingExtension(SourceLocation ExtLoc);

  ExprResult ParseRHSOfBinaryExpression(ExprResult LHS, prec::Level MinPrec);
  enum CastParseKind { AnyCastExpr = 0, UnaryExprOnly, PrimaryExprOnly };
  ExprResult ParseCastExpression(CastParseKind ParseKind,
                                 bool isAddressOfOperand, bool &NotCastExpr,
                                 TypeCastState isTypeCast,
                                 bool isVectorLiteral = false,
                                 bool *NotPrimaryExpression = nullptr);
  ExprResult ParseCastExpression(CastParseKind ParseKind,
                                 bool isAddressOfOperand = false,
                                 TypeCastState isTypeCast = NotTypeCast,
                                 bool isVectorLiteral = false,
                                 bool *NotPrimaryExpression = nullptr);

  bool isNotExpressionStart();

  bool isPostfixExpressionSuffixStart() {
    tok::TokenKind K = Tok.getKind();
    return (K == tok::l_square || K == tok::l_paren || K == tok::period ||
            K == tok::arrow || K == tok::plusplus || K == tok::minusminus);
  }

  ExprResult ParsePostfixExpressionSuffix(ExprResult LHS);
  ExprResult ParseUnaryExprOrTypeTraitExpression();
  ExprResult ParseBuiltinPrimaryExpression();

  ExprResult ParseExprAfterUnaryExprOrTypeTrait(const Token &OpTok,
                                                bool &isCastExpr,
                                                ParsedType &CastTy,
                                                SourceRange &CastRange);

  bool ParseExpressionList(llvm::SmallVectorImpl<Expr *> &Exprs,
                           llvm::function_ref<void()> ExpressionStarts =
                               llvm::function_ref<void()>(),
                           bool FailImmediatelyOnInvalidExpr = false,
                           bool EarlyTypoCorrection = false);

  bool ParseSimpleExpressionList(llvm::SmallVectorImpl<Expr *> &Exprs);

  enum ParenParseOption {
    SimpleExpr,      // Only parse '(' expression ')'
    CompoundStmt,    // Also allow '(' compound-statement ')'
    CompoundLiteral, // Also allow '(' type-name ')' '{' ... '}'
    CastExpr         // Also allow '(' type-name ')' <anything>
  };
  ExprResult ParseParenExpression(ParenParseOption &ExprType,
                                  bool stopIfCastExpr, bool isTypeCast,
                                  ParsedType &CastTy,
                                  SourceLocation &RParenLoc);

  ExprResult ParseCompoundLiteralExpression(ParsedType Ty,
                                            SourceLocation LParenLoc,
                                            SourceLocation RParenLoc);

  ExprResult ParseGenericSelectionExpression();

  bool areTokensAdjacent(const Token &A, const Token &B);

  //===--------------------------------------------------------------------===//
  // C99 6.7.8: Initialization.

  ExprResult ParseInitializer() {
    if (Tok.isNot(tok::l_brace))
      return ParseAssignmentExpression();
    return ParseBraceInitializer();
  }
  bool MayBeDesignationStart();
  ExprResult ParseBraceInitializer();
  ExprResult ParseInitializerWithPotentialDesignator();

  //===--------------------------------------------------------------------===//
  // C99 6.8: Statements and Blocks.

  using ExprVector = llvm::SmallVector<Expr *, 12>;

  StmtResult
  ParseStatement(SourceLocation *TrailingElseLoc = nullptr,
                 ParsedStmtContext StmtCtx = ParsedStmtContext::SubStmt);
  StmtResult
  ParseStatementOrDeclaration(StmtVector &Stmts, ParsedStmtContext StmtCtx,
                              SourceLocation *TrailingElseLoc = nullptr);
  StmtResult ParseStatementOrDeclarationAfterAttributes(
      StmtVector &Stmts, ParsedStmtContext StmtCtx,
      SourceLocation *TrailingElseLoc, ParsedAttributes &DeclAttrs,
      ParsedAttributes &DeclSpecAttrs);
  StmtResult ParseExprStatement(ParsedStmtContext StmtCtx);
  StmtResult ParseLabeledStatement(ParsedAttributes &Attrs,
                                   ParsedStmtContext StmtCtx);
  StmtResult ParseCaseStatement(ParsedStmtContext StmtCtx,
                                bool MissingCase = false,
                                ExprResult Expr = ExprResult());
  StmtResult ParseDefaultStatement(ParsedStmtContext StmtCtx);
  StmtResult ParseCompoundStatement(bool isStmtExpr = false);
  StmtResult ParseCompoundStatement(bool isStmtExpr, unsigned ScopeFlags);
  void ParseCompoundStatementLeadingPragmas();
  void WarnTrailingLabel();
  bool SkipEmptyStatement(StmtVector &Stmts);
  StmtResult ParseCompoundStatementBody(bool isStmtExpr = false);
  bool ParseParenExprOrCondition(Sema::ConditionResult &CondResult,
                                 SourceLocation Loc, Sema::ConditionKind CK,
                                 SourceLocation &LParenLoc,
                                 SourceLocation &RParenLoc);
  StmtResult ParseIfStatement(SourceLocation *TrailingElseLoc);
  StmtResult ParseSwitchStatement(SourceLocation *TrailingElseLoc);
  StmtResult ParseWhileStatement(SourceLocation *TrailingElseLoc);
  StmtResult ParseDoStatement();
  StmtResult ParseForStatement(SourceLocation *TrailingElseLoc);
  StmtResult ParseGotoStatement();
  StmtResult ParseContinueStatement();
  StmtResult ParseBreakStatement();
  StmtResult ParseReturnStatement();
  StmtResult ParseAsmStatement(bool &msAsm);
  StmtResult ParseMicrosoftAsmStatement(SourceLocation AsmLoc);

  enum IfExistsBehavior {
    /// Parse the block; this code is always used.
    IEB_Parse,
    /// Skip the block entirely; this code is never used.
    IEB_Skip
  };

  struct IfExistsCondition {
    /// The location of the initial keyword.
    SourceLocation KeywordLoc;
    /// Whether this is an __if_exists block (rather than an
    /// __if_not_exists block).
    bool IsIfExists;

    /// The name we're looking for.
    UnqualifiedId Name;

    /// The behavior of this __if_exists or __if_not_exists block
    /// should.
    IfExistsBehavior Behavior;
  };

  bool ParseMicrosoftIfExistsCondition(IfExistsCondition &Result);
  void ParseMicrosoftIfExistsStatement(StmtVector &Stmts);
  void ParseMicrosoftIfExistsExternalDeclaration();
  bool ParseMicrosoftIfExistsBraceInitializer(ExprVector &InitExprs,
                                              bool &InitExprsOk);
  bool ParseAsmOperandsOpt(llvm::SmallVectorImpl<IdentifierInfo *> &Names,
                           llvm::SmallVectorImpl<Expr *> &Constraints,
                           llvm::SmallVectorImpl<Expr *> &Exprs);

  //===--------------------------------------------------------------------===//
  // MS: SEH Statements and Blocks

  StmtResult ParseSEHTryBlock();
  StmtResult ParseSEHExceptBlock(SourceLocation Loc);
  StmtResult ParseSEHFinallyBlock(SourceLocation Loc);
  StmtResult ParseSEHLeaveStatement();

  //===--------------------------------------------------------------------===//
  // Statements

  //===--------------------------------------------------------------------===//
  // C99 6.7: Declarations.

  enum class DeclSpecContext {
    DSC_normal,         // normal context
    DSC_record,         // struct/union member (tag) context
    DSC_type_specifier, // specifier-qualifier-list (e.g. abstract declarator)
    DSC_top_level,      // translation-unit / block declaration context
    DSC_association,    // _Generic association type
  };

  static bool isTypeSpecifier(DeclSpecContext DSC) {
    switch (DSC) {
    case DeclSpecContext::DSC_normal:
    case DeclSpecContext::DSC_record:
    case DeclSpecContext::DSC_top_level:
      return false;
    case DeclSpecContext::DSC_type_specifier:
    case DeclSpecContext::DSC_association:
      return true;
    }
    llvm_unreachable("unknown DeclSpecContext");
  }

  enum class AllowDefiningTypeSpec {
    /// The grammar doesn't allow a defining-type-specifier here, and we must
    /// not parse one (eg, because a '{' could mean something else).
    No,
    /// The grammar doesn't allow a defining-type-specifier here, but we permit
    /// one for error recovery purposes. Sema will reject.
    NoButErrorRecovery,
    /// The grammar allows a defining-type-specifier here, and one can be valid.
    Yes
  };

  static AllowDefiningTypeSpec
  isDefiningTypeSpecifierContext(DeclSpecContext DSC) {
    switch (DSC) {
    case DeclSpecContext::DSC_normal:
    case DeclSpecContext::DSC_record:
    case DeclSpecContext::DSC_top_level:
    case DeclSpecContext::DSC_association:
      return AllowDefiningTypeSpec::Yes;

    case DeclSpecContext::DSC_type_specifier:
      return AllowDefiningTypeSpec::NoButErrorRecovery;
    }
    llvm_unreachable("unknown DeclSpecContext");
  }

  static bool isOpaqueEnumDeclarationContext(DeclSpecContext DSC) {
    switch (DSC) {
    case DeclSpecContext::DSC_normal:
    case DeclSpecContext::DSC_record:
    case DeclSpecContext::DSC_top_level:
      return true;
    case DeclSpecContext::DSC_type_specifier:
    case DeclSpecContext::DSC_association:
      return false;
    }
    llvm_unreachable("unknown DeclSpecContext");
  }

  DeclGroupPtrTy ParseDeclaration(DeclaratorContext Context,
                                  SourceLocation &DeclEnd,
                                  ParsedAttributes &DeclAttrs,
                                  ParsedAttributes &DeclSpecAttrs,
                                  SourceLocation *DeclSpecStart = nullptr);
  DeclGroupPtrTy
  ParseSimpleDeclaration(DeclaratorContext Context, SourceLocation &DeclEnd,
                         ParsedAttributes &DeclAttrs,
                         ParsedAttributes &DeclSpecAttrs, bool RequireSemi,
                         SourceLocation *DeclSpecStart = nullptr);
  bool CouldBeDeclarator(DeclaratorContext Context);
  DeclGroupPtrTy ParseDeclGroup(ParsingDeclSpec &DS, DeclaratorContext Context,
                                ParsedAttributes &Attrs,
                                SourceLocation *DeclEnd = nullptr);
  Decl *ParseDeclarationAfterDeclarator(Declarator &D);
  bool ParseAsmAttributesAfterDeclarator(Declarator &D);
  Decl *ParseDeclarationAfterDeclaratorAndAttributes(Declarator &D);
  Decl *ParseFunctionStatementBody(Decl *Decl, ParseScope &BodyScope);

  bool ParseImpliedInt(DeclSpec &DS, AccessSpecifier AS, DeclSpecContext DSC,
                       ParsedAttributes &Attrs);
  DeclSpecContext
  getDeclSpecContextFromDeclaratorContext(DeclaratorContext Context);
  void
  ParseDeclarationSpecifiers(DeclSpec &DS, AccessSpecifier AS = AS_none,
                             DeclSpecContext DSC = DeclSpecContext::DSC_normal,
                             LateParsedAttrList *LateAttrs = nullptr);

  bool DiagnoseMissingSemiAfterTagDefinition(
      DeclSpec &DS, AccessSpecifier AS, DeclSpecContext DSContext,
      LateParsedAttrList *LateAttrs = nullptr);

  void ParseSpecifierQualifierList(
      DeclSpec &DS, AccessSpecifier AS = AS_none,
      DeclSpecContext DSC = DeclSpecContext::DSC_normal);

  void ParseEnumSpecifier(SourceLocation TagLoc, DeclSpec &DS,
                          AccessSpecifier AS, DeclSpecContext DSC);
  void ParseEnumBody(SourceLocation StartLoc, Decl *TagDecl);
  void ParseStructUnionBody(SourceLocation StartLoc, DeclSpec::TST TagType,
                            RecordDecl *TagDecl);

  void ParseStructDeclaration(
      ParsingDeclSpec &DS,
      llvm::function_ref<void(ParsingFieldDeclarator &)> FieldsCallback);

  bool isDeclarationSpecifier();
  bool isTypeSpecifierQualifier();

  bool isKnownToBeTypeSpecifier(const Token &Tok) const;

  bool isKnownToBeDeclarationSpecifier() { return isDeclarationSpecifier(); }

  bool isDeclarationStatement() { return isDeclarationSpecifier(); }

  bool isForInitDeclaration() { return isDeclarationSpecifier(); }

  bool isTypeIdInParens(bool &isAmbiguous) {
    isAmbiguous = false;
    return isTypeSpecifierQualifier();
  }
  bool isTypeIdInParens() {
    bool isAmbiguous;
    return isTypeIdInParens(isAmbiguous);
  }

  bool isTypeIdForGenericSelection() { return isTypeSpecifierQualifier(); }

  bool isTypeIdUnambiguously() { return isTypeSpecifierQualifier(); }

  enum class TPResult { True, False, Ambiguous, Error };

  bool isPendingDeclaration(IdentifierInfo *II);

  // "Tentative parsing" functions, used for disambiguation. If a parsing error
  // is encountered they will return TPResult::Error.
  // Returning TPResult::True/False indicates that the ambiguity was
  // resolved and tentative parsing may stop. TPResult::Ambiguous indicates
  // that more tentative parsing is necessary for disambiguation.
  // They all consume tokens, so backtracking should be used after calling them.

  void DiagnoseBitIntUse(const Token &Tok);

public:
  TypeResult
  ParseTypeName(SourceRange *Range = nullptr,
                DeclaratorContext Context = DeclaratorContext::TypeName,
                AccessSpecifier AS = AS_none, Decl **OwnedType = nullptr,
                ParsedAttributes *Attrs = nullptr);

private:
  bool isAllowedBracketAttributeSpecifier(bool Disambiguate = false) {
    return (Tok.isRegularKeywordAttribute() ||
            isBracketAttributeSpecifier(Disambiguate));
  }

  // Check for the start of an attribute-specifier-seq in a context where an
  // attribute is not allowed.
  bool CheckProhibitedBracketAttribute() {
    assert(Tok.is(tok::l_square));
    if (NextToken().isNot(tok::l_square))
      return false;
    return DiagnoseProhibitedBracketAttribute();
  }

  bool DiagnoseProhibitedBracketAttribute();

  void stripTypeAttributesOffDeclSpec(ParsedAttributes &Attrs, DeclSpec &DS,
                                      Sema::TagUseKind TUK);

  // FixItLoc = possible correct location for the attributes
  void ProhibitAttributes(ParsedAttributes &Attrs,
                          SourceLocation FixItLoc = SourceLocation()) {
    if (Attrs.Range.isInvalid())
      return;
    DiagnoseProhibitedAttributes(Attrs, FixItLoc);
    Attrs.clear();
  }

  void ProhibitAttributes(ParsedAttributesView &Attrs,
                          SourceLocation FixItLoc = SourceLocation()) {
    if (Attrs.Range.isInvalid())
      return;
    DiagnoseProhibitedAttributes(Attrs, FixItLoc);
    Attrs.clearListOnly();
  }
  void DiagnoseProhibitedAttributes(const ParsedAttributesView &Attrs,
                                    SourceLocation FixItLoc);

  // Forbid [[...]] attributes that appear on certain syntactic locations which
  // a standard might permit but we do not support yet (e.g. appertaining to
  // decl-specifiers in some modes).
  // For the most cases we don't want to warn on unknown type attributes, but
  // left them to later diagnoses. However, for a few cases like module
  // declarations and module import declarations, we should do it.
  void ProhibitBracketAttributes(ParsedAttributes &Attrs, unsigned AttrDiagID,
                                 unsigned KeywordDiagId,
                                 bool DiagnoseEmptyAttrs = false,
                                 bool WarnOnUnknownAttrs = false);

  SourceLocation SkipBracketAttributes();

  void DiagnoseAndSkipBracketAttributes();

  ExprResult ParseUnevaluatedStringInAttribute(const IdentifierInfo &AttrName);

  bool
  ParseAttributeArgumentList(const neverc::IdentifierInfo &AttrName,
                             llvm::SmallVectorImpl<Expr *> &Exprs,
                             ParsedAttributeArgumentsProperties ArgsProperties);

  unsigned
  ParseAttributeArgsCommon(IdentifierInfo *AttrName, SourceLocation AttrNameLoc,
                           ParsedAttributes &Attrs, SourceLocation *EndLoc,
                           IdentifierInfo *ScopeName, SourceLocation ScopeLoc,
                           ParsedAttr::Form Form);

  enum ParseAttrKindMask {
    PAKM_GNU = 1 << 0,
    PAKM_Declspec = 1 << 1,
    PAKM_Bracket = 1 << 2,
  };

  void ParseAttributes(unsigned WhichAttrKinds, ParsedAttributes &Attrs,
                       LateParsedAttrList *LateAttrs = nullptr);
  bool MaybeParseAttributes(unsigned WhichAttrKinds, ParsedAttributes &Attrs,
                            LateParsedAttrList *LateAttrs = nullptr) {
    if (Tok.isOneOf(tok::kw___attribute, tok::kw___declspec) ||
        isAllowedBracketAttributeSpecifier()) {
      ParseAttributes(WhichAttrKinds, Attrs, LateAttrs);
      return true;
    }
    return false;
  }

  void MaybeParseGNUAttributes(Declarator &D,
                               LateParsedAttrList *LateAttrs = nullptr) {
    if (Tok.is(tok::kw___attribute)) {
      ParsedAttributes Attrs(AttrFactory);
      ParseGNUAttributes(Attrs, LateAttrs, &D);
      D.takeAttributes(Attrs);
    }
  }

  bool MaybeParseGNUAttributes(ParsedAttributes &Attrs,
                               LateParsedAttrList *LateAttrs = nullptr) {
    if (Tok.is(tok::kw___attribute)) {
      ParseGNUAttributes(Attrs, LateAttrs);
      return true;
    }
    return false;
  }

  void ParseGNUAttributes(ParsedAttributes &Attrs,
                          LateParsedAttrList *LateAttrs = nullptr,
                          Declarator *D = nullptr);
  void ParseGNUAttributeArgs(IdentifierInfo *AttrName,
                             SourceLocation AttrNameLoc,
                             ParsedAttributes &Attrs, SourceLocation *EndLoc,
                             IdentifierInfo *ScopeName, SourceLocation ScopeLoc,
                             ParsedAttr::Form Form, Declarator *D);
  IdentifierLoc *ParseIdentifierLoc();

  unsigned
  ParseCustomAttributeArgs(IdentifierInfo *AttrName, SourceLocation AttrNameLoc,
                           ParsedAttributes &Attrs, SourceLocation *EndLoc,
                           IdentifierInfo *ScopeName, SourceLocation ScopeLoc,
                           ParsedAttr::Form Form);

  void MaybeParseBracketAttributes(Declarator &D) {
    if (isAllowedBracketAttributeSpecifier()) {
      ParsedAttributes Attrs(AttrFactory);
      ParseBracketAttributes(Attrs);
      D.takeAttributes(Attrs);
    }
  }

  bool MaybeParseBracketAttributes(ParsedAttributes &Attrs) {
    if (isAllowedBracketAttributeSpecifier(false)) {
      ParseBracketAttributes(Attrs);
      return true;
    }
    return false;
  }

  void ParseBracketAttributeSpecifierInternal(ParsedAttributes &Attrs,
                                              SourceLocation *EndLoc = nullptr);
  void ParseBracketAttributeSpecifier(ParsedAttributes &Attrs,
                                      SourceLocation *EndLoc = nullptr) {
    ParseBracketAttributeSpecifierInternal(Attrs, EndLoc);
  }
  void ParseBracketAttributes(ParsedAttributes &attrs);
  bool ParseBracketAttributeArgs(IdentifierInfo *AttrName,
                                 SourceLocation AttrNameLoc,
                                 ParsedAttributes &Attrs,
                                 SourceLocation *EndLoc,
                                 IdentifierInfo *ScopeName,
                                 SourceLocation ScopeLoc);

  IdentifierInfo *TryParseBracketAttributeIdentifier(SourceLocation &Loc);

  void MaybeParseMicrosoftAttributes(ParsedAttributes &Attrs) {
    if (getLangOpts().MicrosoftExt && Tok.is(tok::l_square)) {
      ParsedAttributes AttrsWithRange(AttrFactory);
      ParseMicrosoftAttributes(AttrsWithRange);
      Attrs.takeAllFrom(AttrsWithRange);
    }
  }
  void ParseMicrosoftAttributes(ParsedAttributes &Attrs);
  bool MaybeParseMicrosoftDeclSpecs(ParsedAttributes &Attrs) {
    if (getLangOpts().DeclSpecKeyword && Tok.is(tok::kw___declspec)) {
      ParseMicrosoftDeclSpecs(Attrs);
      return true;
    }
    return false;
  }
  void ParseMicrosoftDeclSpecs(ParsedAttributes &Attrs);
  bool ParseMicrosoftDeclSpecArgs(IdentifierInfo *AttrName,
                                  SourceLocation AttrNameLoc,
                                  ParsedAttributes &Attrs);
  void ParseMicrosoftTypeAttributes(ParsedAttributes &attrs);
  void DiagnoseAndSkipExtendedMicrosoftTypeAttributes();
  SourceLocation SkipExtendedMicrosoftTypeAttributes();
  void ParseNullabilityTypeSpecifiers(ParsedAttributes &attrs);
  llvm::VersionTuple ParseVersionTuple(SourceRange &Range);
  void ParseAvailabilityAttribute(IdentifierInfo &Availability,
                                  SourceLocation AvailabilityLoc,
                                  ParsedAttributes &attrs,
                                  SourceLocation *endLoc,
                                  IdentifierInfo *ScopeName,
                                  SourceLocation ScopeLoc,
                                  ParsedAttr::Form Form);

  ExprResult ParseAvailabilityCheckExpr(SourceLocation StartLoc);

  void ParseTypeTagForDatatypeAttribute(IdentifierInfo &AttrName,
                                        SourceLocation AttrNameLoc,
                                        ParsedAttributes &Attrs,
                                        SourceLocation *EndLoc,
                                        IdentifierInfo *ScopeName,
                                        SourceLocation ScopeLoc,
                                        ParsedAttr::Form Form);

  void ParseAttributeWithTypeArg(IdentifierInfo &AttrName,
                                 SourceLocation AttrNameLoc,
                                 ParsedAttributes &Attrs,
                                 IdentifierInfo *ScopeName,
                                 SourceLocation ScopeLoc,
                                 ParsedAttr::Form Form);

  void ParseTypeofSpecifier(DeclSpec &DS);
  void ParseAtomicSpecifier(DeclSpec &DS);

  ExprResult ParseAlignArgument(llvm::StringRef KWName, SourceLocation Start,
                                bool &IsType, ParsedType &Ty);
  void ParseAlignmentSpecifier(ParsedAttributes &Attrs,
                               SourceLocation *endLoc = nullptr);
  ExprResult ParseExtIntegerArgument();

  void ParseDeclarator(Declarator &D);
  typedef void (Parser::*DirectDeclParseFunction)(Declarator &);
  void ParseDeclaratorInternal(Declarator &D,
                               DirectDeclParseFunction DirectDeclParser);

  enum AttrRequirements {
    AR_NoAttributesParsed = 0, ///< No attributes are diagnosed.
    AR_GNUAttributesParsedAndRejected = 1 << 0, ///< Diagnose GNU attributes.
    AR_GNUAttributesParsed = 1 << 1,
    AR_BracketAttributesParsed = 1 << 2,
    AR_DeclspecAttributesParsed = 1 << 3,
    AR_AllAttributesParsed = AR_GNUAttributesParsed |
                             AR_BracketAttributesParsed |
                             AR_DeclspecAttributesParsed,
    AR_VendorAttributesParsed =
        AR_GNUAttributesParsed | AR_DeclspecAttributesParsed
  };

  void ParseTypeQualifierListOpt(DeclSpec &DS,
                                 unsigned AttrReqs = AR_AllAttributesParsed,
                                 bool AtomicAllowed = true,
                                 bool IdentifierRequired = false);
  void ParseDirectDeclarator(Declarator &D);
  void ParseParenDeclarator(Declarator &D);
  void ParseFunctionDeclarator(Declarator &D, ParsedAttributes &FirstArgAttrs,
                               BalancedDelimiterTracker &Tracker,
                               bool IsAmbiguous, bool RequiresArg = false);
  bool isFunctionDeclaratorIdentifierList();
  void ParseFunctionDeclaratorIdentifierList(
      Declarator &D,
      llvm::SmallVectorImpl<DeclaratorChunk::ParamInfo> &ParamInfo);
  void ParseParameterDeclarationClause(
      ParsedAttributes &attrs,
      llvm::SmallVectorImpl<DeclaratorChunk::ParamInfo> &ParamInfo,
      SourceLocation &EllipsisLoc);

  void ParseBracketDeclarator(Declarator &D);
  void ParseMisplacedBracketDeclarator(Declarator &D);

  //===--------------------------------------------------------------------===//
  // [[...]] attribute-specifier disambiguation

  enum BracketAttributeKind {
    /// This is not an attribute specifier.
    CAK_NotAttributeSpecifier,
    /// This should be treated as an attribute-specifier.
    CAK_AttributeSpecifier,
    /// The next tokens are '[[', but not a valid attribute-specifier here.
    CAK_InvalidAttributeSpecifier
  };
  BracketAttributeKind isBracketAttributeSpecifier(bool Disambiguate = false);

  Decl *ParseStaticAssertDeclaration(SourceLocation &DeclEnd);

  //===--------------------------------------------------------------------===//
  // struct / union / enum tag parsing (C and shared infrastructure).
  bool isValidAfterTypeSpecifier(bool CouldBeBitfield);
  void ParseStructOrUnionSpecifier(tok::TokenKind TagTokKind,
                                   SourceLocation TagLoc, DeclSpec &DS,
                                   AccessSpecifier AS, DeclSpecContext DSC,
                                   ParsedAttributes &Attributes);
  void SkipBraceEnclosedBody();
  bool ParseUnqualifiedId(UnqualifiedId &Result);

private:
  //===--------------------------------------------------------------------===//
  // Embarcadero: Array and Expression Traits

  class GNUAsmQualifiers {
    unsigned Qualifiers = AQ_unspecified;

  public:
    enum AQ {
      AQ_unspecified = 0,
      AQ_volatile = 1,
      AQ_inline = 2,
      AQ_goto = 4,
    };
    static const char *getQualifierName(AQ Qualifier);
    bool setAsmQualifier(AQ Qualifier);
    inline bool isVolatile() const { return Qualifiers & AQ_volatile; };
    inline bool isInline() const { return Qualifiers & AQ_inline; };
    inline bool isGoto() const { return Qualifiers & AQ_goto; }
  };
  bool isGCCAsmStatement(const Token &TokAfterAsm) const;
  bool isGNUAsmQualifier(const Token &TokAfterAsm) const;
  GNUAsmQualifiers::AQ getGNUAsmQualifier(const Token &Tok) const;
  bool parseGNUAsmQualifierListOpt(GNUAsmQualifiers &AQ);
};

} // end namespace neverc

#endif
