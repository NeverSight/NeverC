#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/Lookup.h"
#include "neverc/Foundation/Attr/AttributeCommonInfo.h"
#include "neverc/Foundation/Attr/Attributes.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/PrettyDeclStackTrace.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include <optional>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Declaration specifiers
// ===----------------------------------------------------------------------===

NEVERC_HOT void
Parser::ParseDeclarationSpecifiers(DeclSpec &DS, AccessSpecifier AS,
                                   DeclSpecContext DSContext,
                                   LateParsedAttrList *LateAttrs) {
  if (DS.getSourceRange().isInvalid()) {
    DS.SetRangeStart(Tok.getLocation());
    DS.SetRangeEnd(SourceLocation());
  }

  bool AttrsLastTime = false;
  ParsedAttributes attrs(AttrFactory);
  const PrintingPolicy &Policy = Actions.getCachedPrintingPolicy();
  while (true) {
    bool isInvalid = false;
    const char *PrevSpec = nullptr;
    unsigned DiagID = 0;

    SourceLocation ConsumedEnd;

    // MSVC compat: _Atomic<T> inside typedef.
    if (getLangOpts().MSVCCompat && Tok.is(tok::kw__Atomic) &&
        DS.getStorageClassSpec() == neverc::DeclSpec::SCS_typedef &&
        !DS.hasTypeSpecifier() && GetLookAheadToken(1).is(tok::less))
      Tok.setKind(tok::identifier);

    SourceLocation Loc = Tok.getLocation();

    switch (Tok.getKind()) {
    default:
      if (Tok.isRegularKeywordAttribute())
        goto Attribute;

    DoneWithDeclSpec:
      if (!AttrsLastTime)
        ProhibitAttributes(attrs);
      else {
        for (const ParsedAttr &PA : attrs) {
          if (!PA.isStandardAttributeSyntax() &&
              !PA.isRegularKeywordAttribute())
            continue;
          if (PA.getKind() == ParsedAttr::UnknownAttribute)
            continue;
          if (PA.getKind() == ParsedAttr::AT_VectorSize) {
            Diag(PA.getLoc(), diag::warn_attribute_ignored) << PA;
            PA.setInvalid();
            continue;
          }
          if (PA.isTypeAttr() && PA.getKind() != ParsedAttr::AT_AnyX86NoCfCheck)
            continue;
          Diag(PA.getLoc(), diag::err_attribute_not_type_attr)
              << PA << PA.isRegularKeywordAttribute();
          PA.setInvalid();
        }

        DS.takeAttributesFrom(attrs);
      }

      DS.Finish(Actions, Policy);
      return;

    case tok::l_square:
    case tok::kw_alignas:
      if (!isAllowedBracketAttributeSpecifier())
        goto DoneWithDeclSpec;

    Attribute:
      ProhibitAttributes(attrs);
      attrs.clear();
      attrs.Range = SourceRange();

      ParseBracketAttributes(attrs);
      AttrsLastTime = true;
      continue;

    case tok::coloncolon:
      goto DoneWithDeclSpec;

    case tok::annot_typename: {
      if (DS.hasTypeSpecifier() && DS.hasTagDefinition())
        goto DoneWithDeclSpec;

      TypeResult T = getTypeAnnotation(Tok);
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_typename, Loc, PrevSpec,
                                     DiagID, T, Policy);
      if (isInvalid)
        break;

      DS.SetRangeEnd(Tok.getAnnotationEndLoc());
      ConsumeAnnotationToken();
      // MSVC calling-convention in parens after typename.
      if (Tok.is(tok::l_paren) &&
          NextToken().isOneOf(tok::kw___stdcall, tok::kw___cdecl, tok::kw_cdecl,
                              tok::kw___fastcall, tok::kw___regcall,
                              tok::kw___vectorcall)) {
        const Token &NextNextToken = PP.LookAhead(1);
        if (NextNextToken.is(tok::r_paren)) {
          const Token &NextToken2 = PP.LookAhead(2);
          if (NextToken2.is(tok::l_paren)) {
            ConsumeParen();
          }
        }
      }
      continue;
    }

      goto DoneWithDeclSpec;

    case tok::identifier: {
      if (DS.hasTypeSpecifier())
        goto DoneWithDeclSpec;

      if (!getLangOpts().DeclSpecKeyword && Tok.is(tok::identifier) &&
          Tok.getIdentifierInfo()->getName().equals(
              tok::getKeywordSpelling(tok::kw___declspec))) {
        Diag(Loc, diag::err_ms_attributes_not_enabled);

        // The next token should be an open paren. If it is, eat the entire
        // attribute declaration and continue.
        if (NextToken().is(tok::l_paren)) {
          // Consume the __declspec identifier.
          ConsumeToken();

          // Eat the parens and everything between them.
          BalancedDelimiterTracker T(*this, tok::l_paren);
          if (T.consumeOpen()) {
            assert(false && "Not a left paren?");
            return;
          }
          T.skipToEnd();
          continue;
        }
      }

      ParsedType TypeRep =
          Actions.getTypeName(*Tok.getIdentifierInfo(), Tok.getLocation(),
                              getCurScope(), false, false);

      // If this is not a typedef name, don't parse it as part of the declspec,
      // it must be an implicit int or an error.
      if (!TypeRep) {
        if (Tok.isNot(tok::identifier))
          continue;
        ParsedAttributes Attrs(AttrFactory);
        if (ParseImpliedInt(DS, AS, DSContext, Attrs)) {
          if (!Attrs.empty()) {
            AttrsLastTime = true;
            attrs.takeAllFrom(Attrs);
          }
          continue;
        }
        goto DoneWithDeclSpec;
      }

      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_typename, Loc, PrevSpec,
                                     DiagID, TypeRep, Policy);
      if (isInvalid)
        break;

      DS.SetRangeEnd(Tok.getLocation());
      ConsumeToken(); // The identifier

      // Trailing qualifiers (e.g. typedef-name + const) are handled on later
      // iterations; conflicting type specifiers are diagnosed elsewhere.
      continue;
    }

    // Attributes support.
    case tok::kw___attribute:
    case tok::kw___declspec:
      ParseAttributes(PAKM_GNU | PAKM_Declspec, DS.getAttributes(), LateAttrs);
      continue;

    // Microsoft single token adornments.
    case tok::kw___forceinline: {
      isInvalid = DS.setFunctionSpecForceInline(Loc, PrevSpec, DiagID);
      IdentifierInfo *AttrName = Tok.getIdentifierInfo();
      SourceLocation AttrNameLoc = Tok.getLocation();
      DS.getAttributes().addNew(AttrName, AttrNameLoc, nullptr, AttrNameLoc,
                                nullptr, 0, tok::kw___forceinline);
      break;
    }

    case tok::kw___unaligned:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_unaligned, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;

    case tok::kw___sptr:
    case tok::kw___uptr:
    case tok::kw___ptr64:
    case tok::kw___ptr32:
    case tok::kw___w64:
    case tok::kw___cdecl:
    case tok::kw_cdecl:
    case tok::kw___stdcall:
    case tok::kw___fastcall:
    case tok::kw___regcall:
    case tok::kw___vectorcall:
      ParseMicrosoftTypeAttributes(DS.getAttributes());
      continue;

    // Nullability type specifiers.
    case tok::kw__Nonnull:
    case tok::kw__Nullable:
    case tok::kw__Null_unspecified:
      ParseNullabilityTypeSpecifiers(DS.getAttributes());
      continue;

    // storage-class-specifier
    case tok::kw_typedef:
      isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_typedef, Loc,
                                         PrevSpec, DiagID, Policy);

      break;
    case tok::kw_extern:
      if (DS.getThreadStorageClassSpec() == DeclSpec::TSCS___thread)
        Diag(Tok, diag::ext_thread_before)
            << tok::getKeywordSpelling(tok::kw_extern);
      isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_extern, Loc,
                                         PrevSpec, DiagID, Policy);

      break;
    case tok::kw___private_extern__:
      isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_private_extern,
                                         Loc, PrevSpec, DiagID, Policy);

      break;
    case tok::kw_static:
      if (DS.getThreadStorageClassSpec() == DeclSpec::TSCS___thread)
        Diag(Tok, diag::ext_thread_before)
            << tok::getKeywordSpelling(tok::kw_static);
      isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_static, Loc,
                                         PrevSpec, DiagID, Policy);

      break;
    case tok::kw_auto:
      if (getLangOpts().C23) {
        if (isKnownToBeTypeSpecifier(GetLookAheadToken(1))) {
          isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_auto, Loc,
                                             PrevSpec, DiagID, Policy);
          Diag(Tok, diag::err_c_auto_storage_class);
        } else
          isInvalid = DS.SetTypeSpecType(DeclSpec::TST_auto, Loc, PrevSpec,
                                         DiagID, Policy);
      } else {
        isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_auto, Loc,
                                           PrevSpec, DiagID, Policy);
        Diag(Tok, diag::err_c_auto_storage_class);
      }

      break;
    case tok::kw___auto_type:
      Diag(Tok, diag::ext_auto_type);
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_auto_type, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw_register:
      isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_register, Loc,
                                         PrevSpec, DiagID, Policy);

      break;
    case tok::kw___thread:
      isInvalid = DS.SetStorageClassSpecThread(DeclSpec::TSCS___thread, Loc,
                                               PrevSpec, DiagID);

      break;
    case tok::kw_thread_local:
      if (getLangOpts().C23)
        Diag(Tok, diag::warn_c23_compat_keyword) << Tok.getName();
      // Map \c thread_local to \c _Thread_local in C23 so storage class matches
      // C11 thread-local rules.
      // Diagnostics will show _Thread_local when the user wrote
      // thread_local in source in C23 mode; we need some general way to
      // identify which way the user spelled the keyword in source.
      isInvalid = DS.SetStorageClassSpecThread(
          getLangOpts().C23 ? DeclSpec::TSCS__Thread_local
                            : DeclSpec::TSCS_thread_local,
          Loc, PrevSpec, DiagID);

      break;
    case tok::kw__Thread_local:
      if (!getLangOpts().C11)
        Diag(Tok, diag::ext_c11_feature) << Tok.getName();
      isInvalid = DS.SetStorageClassSpecThread(DeclSpec::TSCS__Thread_local,
                                               Loc, PrevSpec, DiagID);

      break;

    // function-specifier
    case tok::kw_inline:
      isInvalid = DS.setFunctionSpecInline(Loc, PrevSpec, DiagID);
      break;
    case tok::kw__Noreturn:
      if (!getLangOpts().C11)
        Diag(Tok, diag::ext_c11_feature) << Tok.getName();
      isInvalid = DS.setFunctionSpecNoreturn(Loc, PrevSpec, DiagID);
      break;

    // alignment-specifier
    case tok::kw__Alignas:
      if (!getLangOpts().C11)
        Diag(Tok, diag::ext_c11_feature) << Tok.getName();
      ParseAlignmentSpecifier(DS.getAttributes());
      continue;

    // C23 \c constexpr
    case tok::kw_constexpr:
      isInvalid = DS.SetConstexprSpec(ConstexprSpecKind::Constexpr, Loc,
                                      PrevSpec, DiagID);
      break;

    // type-specifier
    case tok::kw_short:
      isInvalid = DS.SetTypeSpecWidth(TypeSpecifierWidth::Short, Loc, PrevSpec,
                                      DiagID, Policy);
      break;
    case tok::kw_long:
      if (DS.getTypeSpecWidth() != TypeSpecifierWidth::Long)
        isInvalid = DS.SetTypeSpecWidth(TypeSpecifierWidth::Long, Loc, PrevSpec,
                                        DiagID, Policy);
      else
        isInvalid = DS.SetTypeSpecWidth(TypeSpecifierWidth::LongLong, Loc,
                                        PrevSpec, DiagID, Policy);
      break;
    case tok::kw___int64:
      isInvalid = DS.SetTypeSpecWidth(TypeSpecifierWidth::LongLong, Loc,
                                      PrevSpec, DiagID, Policy);
      break;
    case tok::kw_signed:
      isInvalid =
          DS.SetTypeSpecSign(TypeSpecifierSign::Signed, Loc, PrevSpec, DiagID);
      break;
    case tok::kw_unsigned:
      isInvalid = DS.SetTypeSpecSign(TypeSpecifierSign::Unsigned, Loc, PrevSpec,
                                     DiagID);
      break;
    case tok::kw__Complex:
      if (!getLangOpts().C99)
        Diag(Tok, diag::ext_c99_feature) << Tok.getName();
      isInvalid =
          DS.SetTypeSpecComplex(DeclSpec::TSC_complex, Loc, PrevSpec, DiagID);
      break;
    case tok::kw__Imaginary:
      if (!getLangOpts().C99)
        Diag(Tok, diag::ext_c99_feature) << Tok.getName();
      isInvalid =
          DS.SetTypeSpecComplex(DeclSpec::TSC_imaginary, Loc, PrevSpec, DiagID);
      break;
    case tok::kw_void:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_void, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_char:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_char, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_int:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_int, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw__ExtInt:
    case tok::kw__BitInt: {
      DiagnoseBitIntUse(Tok);
      ExprResult ER = ParseExtIntegerArgument();
      if (ER.isInvalid())
        continue;
      isInvalid = DS.SetBitIntType(Loc, ER.get(), PrevSpec, DiagID, Policy);
      ConsumedEnd = PrevTokLocation;
      break;
    }
    case tok::kw___int128:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_int128, Loc, PrevSpec,
                                     DiagID, Policy);
      break;

    // NeverC Rust-style fixed-width integer types
    case tok::kw_i8:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_i8, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_i16:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_i16, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_i32:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_i32, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_i64:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_i64, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_i128:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_i128, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_u8:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_u8, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_u16:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_u16, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_u32:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_u32, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_u64:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_u64, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_u128:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_u128, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_isize:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_isize, Loc, PrevSpec, DiagID,
                                     Policy);
      break;
    case tok::kw_usize:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_usize, Loc, PrevSpec, DiagID,
                                     Policy);
      break;

    case tok::kw___bf16:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_BFloat16, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw_float:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_float, Loc, PrevSpec, DiagID,
                                     Policy);
      break;
    case tok::kw_double:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_double, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw__Float16:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_float16, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw__Accum:
      assert(getLangOpts().FixedPoint &&
             "This keyword is only used when fixed point types are enabled "
             "with `-ffixed-point`");
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_accum, Loc, PrevSpec, DiagID,
                                     Policy);
      break;
    case tok::kw__Fract:
      assert(getLangOpts().FixedPoint &&
             "This keyword is only used when fixed point types are enabled "
             "with `-ffixed-point`");
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_fract, Loc, PrevSpec, DiagID,
                                     Policy);
      break;
    case tok::kw__Sat:
      assert(getLangOpts().FixedPoint &&
             "This keyword is only used when fixed point types are enabled "
             "with `-ffixed-point`");
      isInvalid = DS.SetTypeSpecSat(Loc, PrevSpec, DiagID);
      break;
    case tok::kw___float128:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_float128, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw___fp16:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_half, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_bool:
      if (getLangOpts().C23)
        Diag(Tok, diag::warn_c23_compat_keyword) << Tok.getName();
      [[fallthrough]];
    case tok::kw__Bool:
      if (Tok.is(tok::kw__Bool) && !getLangOpts().C99)
        Diag(Tok, diag::ext_c99_feature) << Tok.getName();

      if (Tok.is(tok::kw_bool) &&
          DS.getTypeSpecType() != DeclSpec::TST_unspecified &&
          DS.getStorageClassSpec() == DeclSpec::SCS_typedef) {
        PrevSpec = ""; // Not used by the diagnostic.
        DiagID = diag::err_bool_redeclaration;
        // For better error recovery.
        Tok.setKind(tok::identifier);
        isInvalid = true;
      } else {
        isInvalid = DS.SetTypeSpecType(DeclSpec::TST_bool, Loc, PrevSpec,
                                       DiagID, Policy);
      }
      break;
    case tok::kw__Decimal32:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_decimal32, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw__Decimal64:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_decimal64, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw__Decimal128:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_decimal128, Loc, PrevSpec,
                                     DiagID, Policy);
      break;

    // record-specifier:
    case tok::kw_struct:
    case tok::kw_union: {
      tok::TokenKind Kind = Tok.getKind();
      ConsumeToken();

      // These are attributes following record specifiers.
      // To produce better diagnostic, we parse them when
      // parsing the record specifier.
      ParsedAttributes Attributes(AttrFactory);
      ParseStructOrUnionSpecifier(Kind, Loc, DS, AS, DSContext, Attributes);

      // If there are attributes following the record specifier,
      // take them over and handle them here.
      if (!Attributes.empty()) {
        AttrsLastTime = true;
        attrs.takeAllFrom(Attributes);
      }
      continue;
    }

    // enum-specifier:
    case tok::kw_enum:
      ConsumeToken();
      ParseEnumSpecifier(Loc, DS, AS, DSContext);
      continue;

    // cv-qualifier:
    case tok::kw_const:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_const, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;
    case tok::kw_volatile:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_volatile, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;
    case tok::kw_restrict:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_restrict, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;

    // C23/GNU typeof support.
    case tok::kw_typeof:
    case tok::kw_typeof_unqual:
      ParseTypeofSpecifier(DS);
      continue;

    case tok::annot_pragma_pack:
      ProcessPragmaPack();
      continue;

    case tok::annot_pragma_ms_pragma:
      ProcessPragmaMSPragma();
      continue;

    case tok::kw__Atomic:
      // C11 6.7.2.4/4:
      //   If the _Atomic keyword is immediately followed by a left parenthesis,
      //   it is interpreted as a type specifier (with a type name), not as a
      //   type qualifier.
      if (!getLangOpts().C11)
        Diag(Tok, diag::ext_c11_feature) << Tok.getName();

      if (NextToken().is(tok::l_paren)) {
        ParseAtomicSpecifier(DS);
        continue;
      }
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_atomic, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;

    case tok::less:
      goto DoneWithDeclSpec;
    }

    DS.SetRangeEnd(ConsumedEnd.isValid() ? ConsumedEnd : Tok.getLocation());

    // If the specifier wasn't legal, issue a diagnostic.
    if (isInvalid) {
      assert(PrevSpec && "Method did not return previous specifier!");
      assert(DiagID);

      if (DiagID == diag::ext_duplicate_declspec ||
          DiagID == diag::ext_warn_duplicate_declspec ||
          DiagID == diag::err_duplicate_declspec)
        Diag(Loc, DiagID) << PrevSpec
                          << FixItHint::CreateRemoval(
                                 SourceRange(Loc, DS.getEndLoc()));
      else
        Diag(Loc, DiagID) << PrevSpec;
    }

    if (DiagID != diag::err_bool_redeclaration && ConsumedEnd.isInvalid())
      // After an error the next token can be an annotation token.
      ConsumeAnyToken();

    AttrsLastTime = false;
  }
}

void Parser::ParseStructDeclaration(
    ParsingDeclSpec &DS,
    llvm::function_ref<void(ParsingFieldDeclarator &)> FieldsCallback) {

  if (Tok.is(tok::kw___extension__)) {
    // __extension__ silences extension warnings in the subexpression.
    ExtensionRAIIObject O(Diags); // Use RAII to do this.
    ConsumeToken();
    return ParseStructDeclaration(DS, FieldsCallback);
  }

  ParsedAttributes Attrs(AttrFactory);
  MaybeParseBracketAttributes(Attrs);

  ParseSpecifierQualifierList(DS);

  // If there are no declarators, this is a free-standing declaration
  // specifier. Let the actions module cope with it.
  if (Tok.is(tok::semi)) {
    // C23 6.7.2.1p9 : "The optional attribute specifier sequence in a
    // member declaration appertains to each of the members declared by the
    // member declarator list; it shall not appear if the optional member
    // declarator list is omitted."
    ProhibitAttributes(Attrs);
    RecordDecl *AnonRecord = nullptr;
    Decl *TheDecl = Actions.ParsedFreeStandingDeclSpec(
        getCurScope(), AS_none, DS, ParsedAttributesView::none(), AnonRecord);
    assert(!AnonRecord && "Did not expect anonymous struct or union here");
    DS.complete(TheDecl);
    return;
  }

  // Read struct-declarators until we find the semicolon.
  bool FirstDeclarator = true;
  SourceLocation CommaLoc;
  while (true) {
    ParsingFieldDeclarator DeclaratorInfo(*this, DS, Attrs);

    // Attributes are only allowed here on successive declarators.
    if (!FirstDeclarator) {
      // However, this does not apply for [[]] attributes (which could show up
      // before or after the __attribute__ attributes).
      DiagnoseAndSkipBracketAttributes();
      MaybeParseGNUAttributes(DeclaratorInfo.D);
      DiagnoseAndSkipBracketAttributes();
    }

    /// struct-declarator: declarator
    /// struct-declarator: declarator[opt] ':' constant-expression
    if (Tok.isNot(tok::colon)) {
      // Don't parse FOO:BAR as if it were a typo for FOO::BAR.
      ColonProtectionRAIIObject X(*this);
      ParseDeclarator(DeclaratorInfo.D);
    } else
      DeclaratorInfo.D.SetIdentifier(nullptr, Tok.getLocation());

    if (TryConsumeToken(tok::colon)) {
      ExprResult Res(ParseConstantExpression());
      if (Res.isInvalid())
        SkipUntil(tok::semi, StopBeforeMatch);
      else
        DeclaratorInfo.BitfieldSize = Res.get();
    }

    // If attributes exist after the declarator, parse them.
    MaybeParseGNUAttributes(DeclaratorInfo.D);

    // We're done with this declarator;  invoke the callback.
    FieldsCallback(DeclaratorInfo);

    // If we don't have a comma, it is either the end of the list (a ';')
    // or an error, bail out.
    if (!TryConsumeToken(tok::comma, CommaLoc))
      return;

    FirstDeclarator = false;
  }
}

void Parser::ParseStructUnionBody(SourceLocation RecordLoc,
                                  DeclSpec::TST TagType, RecordDecl *TagDecl) {
  PrettyDeclStackTraceEntry CrashInfo(Actions.Context, TagDecl, RecordLoc,
                                      "parsing struct/union body");

  BalancedDelimiterTracker T(*this, tok::l_brace);
  if (T.consumeOpen())
    return;

  ParseScope StructScope(this, Scope::RecordScope | Scope::DeclScope);
  Actions.OnTagStartDefinition(getCurScope(), TagDecl);

  // While we still have something to read, read the declarations in the struct.
  while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
    // Each iteration of this loop reads one struct-declaration.
    if (Tok.is(tok::semi)) {
      ConsumeRedundantSemicolons(InsideStruct, TagType);
      continue;
    }

    if (Tok.isOneOf(tok::kw__Static_assert, tok::kw_static_assert)) {
      SourceLocation DeclEnd;
      ParseStaticAssertDeclaration(DeclEnd);
      continue;
    }

    if (Tok.is(tok::annot_pragma_pack)) {
      ProcessPragmaPack();
      continue;
    }

    if (Tok.is(tok::annot_pragma_align)) {
      ProcessPragmaAlign();
      continue;
    }

    if (tok::isPragmaAnnotation(Tok.getKind())) {
      Diag(Tok.getLocation(), diag::err_pragma_misplaced_in_decl)
          << DeclSpec::getSpecifierName(
                 TagType, Actions.getTreeContext().getPrintingPolicy());
      ConsumeAnnotationToken();
      continue;
    }

    auto CFieldCallback = [&](ParsingFieldDeclarator &FD) {
      Decl *Field =
          Actions.OnField(getCurScope(), TagDecl,
                          FD.D.getDeclSpec().getSourceRange().getBegin(), FD.D,
                          FD.BitfieldSize);
      FD.complete(Field);
    };

    ParsingDeclSpec DS(*this);
    ParseStructDeclaration(DS, CFieldCallback);

    if (TryConsumeToken(tok::semi))
      continue;

    if (Tok.is(tok::r_brace)) {
      RequireToken(tok::semi, diag::ext_expected_semi_decl_list);
      break;
    }

    RequireToken(tok::semi, diag::err_expected_semi_decl_list);
    // Skip to end of block or statement to avoid ext-warning on extra ';'.
    SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
    // If we stopped at a ';', eat it.
    TryConsumeToken(tok::semi);
  }

  T.consumeClose();

  ParsedAttributes attrs(AttrFactory);
  // If attributes exist after struct contents, parse them.
  MaybeParseGNUAttributes(attrs);

  llvm::SmallVector<Decl *, 32> FieldDecls(TagDecl->fields());

  Actions.OnFields(getCurScope(), RecordLoc, TagDecl, FieldDecls,
                   T.getOpenLocation(), T.getCloseLocation(), attrs);
  StructScope.Exit();
  Actions.OnTagFinishDefinition(getCurScope(), TagDecl, T.getRange());
}

void Parser::ParseEnumSpecifier(SourceLocation StartLoc, DeclSpec &DS,
                                AccessSpecifier AS, DeclSpecContext DSC) {
  // If attributes exist after tag, parse them.
  ParsedAttributes attrs(AttrFactory);
  MaybeParseAttributes(PAKM_GNU | PAKM_Declspec | PAKM_Bracket, attrs);

  // Determine whether this declaration is permitted to have an enum-base.
  AllowDefiningTypeSpec AllowEnumSpecifier =
      isDefiningTypeSpecifierContext(DSC);
  bool CanBeOpaqueEnumDeclaration =
      DS.isEmpty() && isOpaqueEnumDeclarationContext(DSC);
  bool CanHaveEnumBase = getLangOpts().MicrosoftExt &&
                         (AllowEnumSpecifier == AllowDefiningTypeSpec::Yes ||
                          CanBeOpaqueEnumDeclaration);

  // Must have either 'enum name' or 'enum {...}' or (rarely) 'enum : T { ...
  // }'.
  if (Tok.isNot(tok::identifier) && Tok.isNot(tok::l_brace) &&
      Tok.isNot(tok::colon)) {
    Diag(Tok, diag::err_expected_either) << tok::identifier << tok::l_brace;

    DS.SetTypeSpecError();
    // Skip the rest of this declarator, up until the comma or semicolon.
    SkipUntil(tok::comma, StopAtSemi);
    return;
  }

  // If an identifier is present, consume and remember it.
  IdentifierInfo *Name = nullptr;
  SourceLocation NameLoc;
  if (Tok.is(tok::identifier)) {
    Name = Tok.getIdentifierInfo();
    NameLoc = ConsumeToken();
  }

  TypeResult BaseType;
  SourceRange BaseRange;

  bool CanBeBitfield = getCurScope()->isRecordScope() && Name;

  if (Tok.is(tok::colon)) {
    // This might be an enum-base or part of some unrelated enclosing context.
    //
    // 'enum E : base' is permitted in two circumstances:
    //
    // 1) As a defining-type-specifier, when followed by '{'.
    // 2) As the sole constituent of a complete declaration -- when DS is empty
    //    and the next token is ';'.
    //
    // The restriction to defining-type-specifiers matters so _Generic(a, enum E
    // : int{}) can disambiguate ':' after the tag.
    //
    // Disambiguate ':' after the tag: MSVC-style fixed underlying type vs.
    // struct bit-field width.

    if (!CanBeBitfield && (CanHaveEnumBase || !ColonIsSacred)) {
      SourceLocation ColonLoc = ConsumeToken();

      // Parse a type-specifier-seq as a type. We can't just ParseTypeName here,
      // because under -fms-extensions,
      //   enum E : int *p;
      // declares 'enum E : int; E *p;' not 'enum E : int*; E p;'.
      DeclSpec DS(AttrFactory);
      // Parse enum-base as a specifier-qualifier-list (fixed underlying type).
      ParseSpecifierQualifierList(DS, AS, DeclSpecContext::DSC_type_specifier);
      Declarator DeclaratorInfo(DS, ParsedAttributesView::none(),
                                DeclaratorContext::TypeName);
      BaseType = Actions.OnTypeName(getCurScope(), DeclaratorInfo);

      BaseRange =
          SourceRange(ColonLoc, DeclaratorInfo.getSourceRange().getEnd());

      if (!getLangOpts().C23) {
        if (getLangOpts().MicrosoftExt)
          Diag(ColonLoc, diag::ext_ms_c_enum_fixed_underlying_type)
              << BaseRange;
        else
          Diag(ColonLoc, diag::ext_c_enum_fixed_underlying_type) << BaseRange;
      }
    }
  }

  // C99 6.7.2.3p11:
  // enum foo {..};  void bar() { enum foo; }    <- new foo in bar.
  // enum foo {..};  void bar() { enum foo x; }  <- use of old foo.
  Sema::TagUseKind TUK;
  if (AllowEnumSpecifier == AllowDefiningTypeSpec::No)
    TUK = Sema::TUK_Reference;
  else if (Tok.is(tok::l_brace)) {
    TUK = Sema::TUK_Definition;
  } else if (!isTypeSpecifier(DSC) &&
             (Tok.is(tok::semi) ||
              (Tok.isAtStartOfLine() &&
               !isValidAfterTypeSpecifier(CanBeBitfield)))) {
    TUK = Sema::TUK_Declaration;
    if (Tok.isNot(tok::semi)) {
      RequireToken(tok::semi, diag::err_expected_after,
                   tok::getKeywordSpelling(tok::kw_enum));
      PP.InjectToken(Tok, /*IsReinject=*/true);
      Tok.setKind(tok::semi);
    }
  } else {
    TUK = Sema::TUK_Reference;
  }

  bool IsElaboratedTypeSpecifier = TUK == Sema::TUK_Reference;

  if (!Name && TUK != Sema::TUK_Definition) {
    Diag(Tok, diag::err_enumerator_unnamed_no_def);

    DS.SetTypeSpecError();
    // Skip the rest of this declarator, up until the comma or semicolon.
    SkipUntil(tok::comma, StopAtSemi);
    return;
  }

  // An elaborated-type-specifier has a much more constrained grammar:
  //
  //   'enum' nested-name-specifier[opt] identifier
  //
  // If we parsed any other bits, reject them now.
  //
  // MSVC permits a full enum-specifier or opaque-enum-declaration anywhere.
  if (IsElaboratedTypeSpecifier && !getLangOpts().MicrosoftExt) {
    ProhibitBracketAttributes(attrs, diag::err_attributes_not_allowed,
                              diag::err_keyword_not_allowed,
                              /*DiagnoseEmptyAttrs=*/true);
    if (BaseType.isUsable())
      Diag(BaseRange.getBegin(), diag::ext_enum_base_in_type_specifier)
          << (AllowEnumSpecifier == AllowDefiningTypeSpec::Yes) << BaseRange;
  }

  stripTypeAttributesOffDeclSpec(attrs, DS, TUK);

  Sema::SkipBodyInfo SkipBody;

  bool Owned = false;
  const char *PrevSpec = nullptr;
  unsigned DiagID;
  Decl *TagDecl =
      Actions
          .OnTag(getCurScope(), DeclSpec::TST_enum, TUK, StartLoc, Name,
                 NameLoc, attrs, AS, Owned, BaseType, OffsetOfState, &SkipBody)
          .get();

  if (SkipBody.ShouldSkip) {
    assert(TUK == Sema::TUK_Definition && "can only skip a definition");

    BalancedDelimiterTracker T(*this, tok::l_brace);
    T.consumeOpen();
    T.skipToEnd();

    if (DS.SetTypeSpecType(DeclSpec::TST_enum, StartLoc,
                           NameLoc.isValid() ? NameLoc : StartLoc, PrevSpec,
                           DiagID, TagDecl, Owned,
                           Actions.getTreeContext().getPrintingPolicy()))
      Diag(StartLoc, DiagID) << PrevSpec;
    return;
  }

  if (!TagDecl) {
    // The action failed to produce an enumeration tag. If this is a
    // definition, consume the entire definition.
    if (Tok.is(tok::l_brace) && TUK != Sema::TUK_Reference) {
      ConsumeBrace();
      SkipUntil(tok::r_brace, StopAtSemi);
    }

    DS.SetTypeSpecError();
    return;
  }

  if (Tok.is(tok::l_brace) && TUK == Sema::TUK_Definition) {
    Decl *D = SkipBody.CheckSameAsPrevious ? SkipBody.New : TagDecl;
    ParseEnumBody(StartLoc, D);
    if (SkipBody.CheckSameAsPrevious) {
      DS.SetTypeSpecError();
      return;
    }
  }

  if (DS.SetTypeSpecType(DeclSpec::TST_enum, StartLoc,
                         NameLoc.isValid() ? NameLoc : StartLoc, PrevSpec,
                         DiagID, TagDecl, Owned,
                         Actions.getTreeContext().getPrintingPolicy()))
    Diag(StartLoc, DiagID) << PrevSpec;
}

void Parser::ParseEnumBody(SourceLocation StartLoc, Decl *EnumDecl) {
  // Enter the scope of the enum body and start the definition.
  ParseScope EnumScope(this, Scope::DeclScope | Scope::EnumScope);
  Actions.OnTagStartDefinition(getCurScope(), EnumDecl);

  BalancedDelimiterTracker T(*this, tok::l_brace);
  T.consumeOpen();

  if (Tok.is(tok::r_brace))
    Diag(Tok, diag::err_empty_enum);

  llvm::SmallVector<Decl *, 32> EnumConstantDecls;
  llvm::SmallVector<ParsingDeclDiagnosticScope, 32> EnumConstParsingDeclScopes;

  Decl *LastEnumConstDecl = nullptr;

  while (Tok.isNot(tok::r_brace)) {
    // Parse enumerator. If failed, try skipping till the start of the next
    // enumerator definition.
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
      if (SkipUntil(tok::comma, tok::r_brace, StopBeforeMatch) &&
          TryConsumeToken(tok::comma))
        continue;
      break;
    }
    IdentifierInfo *Ident = Tok.getIdentifierInfo();
    SourceLocation IdentLoc = ConsumeToken();

    // If attributes exist after the enumerator, parse them.
    ParsedAttributes attrs(AttrFactory);
    MaybeParseGNUAttributes(attrs);
    if (isAllowedBracketAttributeSpecifier()) {
      ParseBracketAttributes(attrs);
    }

    SourceLocation EqualLoc;
    ExprResult AssignedVal;
    EnumConstParsingDeclScopes.emplace_back(*this);

    EnterExpressionEvaluationContext ConstantEvaluated(
        Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);
    if (TryConsumeToken(tok::equal, EqualLoc)) {
      AssignedVal = ParseConstantExpressionInExprEvalContext();
      if (AssignedVal.isInvalid())
        SkipUntil(tok::comma, tok::r_brace, StopBeforeMatch);
    }

    // Install the enumerator constant into EnumDecl.
    Decl *EnumConstDecl = Actions.OnEnumConstant(
        getCurScope(), EnumDecl, LastEnumConstDecl, IdentLoc, Ident, attrs,
        EqualLoc, AssignedVal.get());
    EnumConstParsingDeclScopes.back().done();

    EnumConstantDecls.push_back(EnumConstDecl);
    LastEnumConstDecl = EnumConstDecl;

    if (Tok.is(tok::identifier)) {
      // We're missing a comma between enumerators.
      SourceLocation Loc = getEndOfPreviousToken();
      Diag(Loc, diag::err_enumerator_list_missing_comma)
          << FixItHint::CreateInsertion(Loc, ", ");
      continue;
    }

    // Emumerator definition must be finished, only comma or r_brace are
    // allowed here.
    SourceLocation CommaLoc;
    if (Tok.isNot(tok::r_brace) && !TryConsumeToken(tok::comma, CommaLoc)) {
      if (EqualLoc.isValid())
        Diag(Tok.getLocation(), diag::err_expected_either)
            << tok::r_brace << tok::comma;
      else
        Diag(Tok.getLocation(), diag::err_expected_end_of_enumerator);
      if (SkipUntil(tok::comma, tok::r_brace, StopBeforeMatch)) {
        if (TryConsumeToken(tok::comma, CommaLoc))
          continue;
      } else {
        break;
      }
    }

    if (Tok.is(tok::r_brace) && CommaLoc.isValid()) {
      if (!getLangOpts().C99)
        Diag(CommaLoc, diag::ext_enumerator_list_comma_c)
            << FixItHint::CreateRemoval(CommaLoc);
      break;
    }
  }

  // Eat the }.
  T.consumeClose();

  // If attributes exist after the identifier list, parse them.
  ParsedAttributes attrs(AttrFactory);
  MaybeParseGNUAttributes(attrs);

  Actions.OnEnumBody(StartLoc, T.getRange(), EnumDecl, EnumConstantDecls,
                     getCurScope(), attrs);

  // Now attach delayed diagnostics to each enumerator declaration.
  assert(EnumConstantDecls.size() == EnumConstParsingDeclScopes.size());
  for (size_t i = 0, e = EnumConstantDecls.size(); i != e; ++i) {
    ParsingDeclRAIIObject PD(*this, ParsingDeclRAIIObject::NoParent);
    EnumConstParsingDeclScopes[i].redelay();
    PD.complete(EnumConstantDecls[i]);
  }

  EnumScope.Exit();
  Actions.OnTagFinishDefinition(getCurScope(), EnumDecl, T.getRange());

  // The next token must be valid after an enum definition. If not, a ';'
  // was probably forgotten.
  bool CanBeBitfield = getCurScope()->isRecordScope();
  if (!isValidAfterTypeSpecifier(CanBeBitfield)) {
    RequireToken(tok::semi, diag::err_expected_after,
                 tok::getKeywordSpelling(tok::kw_enum));
    // Push this token back into the preprocessor and change our current token
    // to ';' so that the rest of the code recovers as though there were an
    // ';' after the definition.
    PP.InjectToken(Tok, /*IsReinject=*/true);
    Tok.setKind(tok::semi);
  }
}

bool Parser::isKnownToBeTypeSpecifier(const Token &Tok) const {
  switch (Tok.getKind()) {
  default:
    return false;
    // type-specifiers
  case tok::kw_short:
  case tok::kw_long:
  case tok::kw___int64:
  case tok::kw___int128:
  case tok::kw_signed:
  case tok::kw_unsigned:
  case tok::kw__Complex:
  case tok::kw__Imaginary:
  case tok::kw_void:
  case tok::kw_char:
  case tok::kw_int:
  case tok::kw__ExtInt:
  case tok::kw__BitInt:
  case tok::kw___bf16:
  case tok::kw_float:
  case tok::kw_double:
  case tok::kw__Accum:
  case tok::kw__Fract:
  case tok::kw__Float16:
  case tok::kw___fp16:
  case tok::kw___float128:
  case tok::kw_bool:
  case tok::kw__Bool:
  case tok::kw__Decimal32:
  case tok::kw__Decimal64:
  case tok::kw__Decimal128:
    // NeverC Rust-style integer types
  case tok::kw_i8:
  case tok::kw_i16:
  case tok::kw_i32:
  case tok::kw_i64:
  case tok::kw_i128:
  case tok::kw_u8:
  case tok::kw_u16:
  case tok::kw_u32:
  case tok::kw_u64:
  case tok::kw_u128:
  case tok::kw_isize:
  case tok::kw_usize:

    // struct-or-union-specifier (C99)
  case tok::kw_struct:
  case tok::kw_union:
    // enum-specifier
  case tok::kw_enum:

    // typedef-name
  case tok::annot_typename:
    return true;
  }
}

bool Parser::isTypeSpecifierQualifier() {
  switch (Tok.getKind()) {
  default:
    return false;

  case tok::identifier: // foo::bar
    // Annotate typenames / scope; if that changes the token stream, recurse.
    TryResolveTypeAnnotation();
    if (Tok.is(tok::identifier))
      return false;
    return isTypeSpecifierQualifier();

    // GNU attributes support.
  case tok::kw___attribute:
    // C23/GNU typeof support.
  case tok::kw_typeof:
  case tok::kw_typeof_unqual:

    // type-specifiers
  case tok::kw_short:
  case tok::kw_long:
  case tok::kw___int64:
  case tok::kw___int128:
  case tok::kw_signed:
  case tok::kw_unsigned:
  case tok::kw__Complex:
  case tok::kw__Imaginary:
  case tok::kw_void:
  case tok::kw_char:
  case tok::kw_int:
  case tok::kw__ExtInt:
  case tok::kw__BitInt:
  case tok::kw___bf16:
  case tok::kw_float:
  case tok::kw_double:
  case tok::kw__Accum:
  case tok::kw__Fract:
  case tok::kw__Float16:
  case tok::kw___fp16:
  case tok::kw___float128:
  case tok::kw_bool:
  case tok::kw__Bool:
  case tok::kw__Decimal32:
  case tok::kw__Decimal64:
  case tok::kw__Decimal128:
    // NeverC Rust-style integer types
  case tok::kw_i8:
  case tok::kw_i16:
  case tok::kw_i32:
  case tok::kw_i64:
  case tok::kw_i128:
  case tok::kw_u8:
  case tok::kw_u16:
  case tok::kw_u32:
  case tok::kw_u64:
  case tok::kw_u128:
  case tok::kw_isize:
  case tok::kw_usize:

    // struct-or-union-specifier (C99)
  case tok::kw_struct:
  case tok::kw_union:
    // enum-specifier
  case tok::kw_enum:

    // type-qualifier
  case tok::kw_const:
  case tok::kw_volatile:
  case tok::kw_restrict:
  case tok::kw__Sat:

    // typedef-name
  case tok::annot_typename:
    return true;

  case tok::less:
    return false;

  case tok::kw___cdecl:
  case tok::kw_cdecl:
  case tok::kw___stdcall:
  case tok::kw___fastcall:
  case tok::kw___regcall:
  case tok::kw___vectorcall:
  case tok::kw___w64:
  case tok::kw___ptr64:
  case tok::kw___ptr32:
  case tok::kw___unaligned:

  case tok::kw__Nonnull:
  case tok::kw__Nullable:
  case tok::kw__Null_unspecified:

  // C11 _Atomic
  case tok::kw__Atomic:
    return true;
  }
}

bool Parser::isDeclarationSpecifier() {
  switch (Tok.getKind()) {
  default:
    return false;

  case tok::identifier: // foo::bar
    // Annotate typenames / scope; if that changes the token stream, recurse.
    TryResolveTypeAnnotation();
    if (Tok.is(tok::identifier))
      return false;

    return isDeclarationSpecifier();

    // storage-class-specifier
  case tok::kw_typedef:
  case tok::kw_extern:
  case tok::kw___private_extern__:
  case tok::kw_static:
  case tok::kw_auto:
  case tok::kw___auto_type:
  case tok::kw_register:
  case tok::kw___thread:
  case tok::kw_thread_local:
  case tok::kw__Thread_local:

    // type-specifiers
  case tok::kw_short:
  case tok::kw_long:
  case tok::kw___int64:
  case tok::kw___int128:
  case tok::kw_signed:
  case tok::kw_unsigned:
  case tok::kw__Complex:
  case tok::kw__Imaginary:
  case tok::kw_void:
  case tok::kw_char:

  case tok::kw_int:
  case tok::kw__ExtInt:
  case tok::kw__BitInt:
  case tok::kw___bf16:
  case tok::kw_float:
  case tok::kw_double:
  case tok::kw__Accum:
  case tok::kw__Fract:
  case tok::kw__Float16:
  case tok::kw___fp16:
  case tok::kw___float128:
  case tok::kw_bool:
  case tok::kw__Bool:
  case tok::kw__Decimal32:
  case tok::kw__Decimal64:
  case tok::kw__Decimal128:
    // NeverC Rust-style integer types
  case tok::kw_i8:
  case tok::kw_i16:
  case tok::kw_i32:
  case tok::kw_i64:
  case tok::kw_i128:
  case tok::kw_u8:
  case tok::kw_u16:
  case tok::kw_u32:
  case tok::kw_u64:
  case tok::kw_u128:
  case tok::kw_isize:
  case tok::kw_usize:

    // struct-or-union-specifier (C99)
  case tok::kw_struct:
  case tok::kw_union:
    // enum-specifier
  case tok::kw_enum:

    // type-qualifier
  case tok::kw_const:
  case tok::kw_volatile:
  case tok::kw_restrict:
  case tok::kw__Sat:

    // function-specifier
  case tok::kw_inline:
  case tok::kw__Noreturn:

    // alignment-specifier
  case tok::kw__Alignas:

    // static_assert-declaration
  case tok::kw_static_assert:
  case tok::kw__Static_assert:

    // C23/GNU typeof support.
  case tok::kw_typeof:
  case tok::kw_typeof_unqual:

    // GNU attributes.
  case tok::kw___attribute:

    // C23 \c constexpr
  case tok::kw_constexpr:

    // C11 _Atomic
  case tok::kw__Atomic:
    return true;

  case tok::less:
    return false;

    // typedef-name
  case tok::annot_typename:
    return true;

  case tok::kw___declspec:
  case tok::kw___cdecl:
  case tok::kw_cdecl:
  case tok::kw___stdcall:
  case tok::kw___fastcall:
  case tok::kw___regcall:
  case tok::kw___vectorcall:
  case tok::kw___w64:
  case tok::kw___sptr:
  case tok::kw___uptr:
  case tok::kw___ptr64:
  case tok::kw___ptr32:
  case tok::kw___forceinline:
  case tok::kw___unaligned:

  case tok::kw__Nonnull:
  case tok::kw__Nullable:
  case tok::kw__Null_unspecified:
    return true;
  }
  return false;
}

void Parser::ParseTypeQualifierListOpt(DeclSpec &DS, unsigned AttrReqs,
                                       bool AtomicAllowed,
                                       bool IdentifierRequired) {
  if ((AttrReqs & AR_BracketAttributesParsed) &&
      isAllowedBracketAttributeSpecifier()) {
    ParsedAttributes Attrs(AttrFactory);
    ParseBracketAttributes(Attrs);
    DS.takeAttributesFrom(Attrs);
  }

  SourceLocation EndLoc;

  while (true) {
    bool isInvalid = false;
    const char *PrevSpec = nullptr;
    unsigned DiagID = 0;
    SourceLocation Loc = Tok.getLocation();

    switch (Tok.getKind()) {
    case tok::kw_const:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_const, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;
    case tok::kw_volatile:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_volatile, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;
    case tok::kw_restrict:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_restrict, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;
    case tok::kw__Atomic:
      if (!AtomicAllowed)
        goto DoneWithTypeQuals;
      if (!getLangOpts().C11)
        Diag(Tok, diag::ext_c11_feature) << Tok.getName();
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_atomic, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;

    case tok::kw___unaligned:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_unaligned, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;
    case tok::kw___uptr:
      if ((AttrReqs & AR_DeclspecAttributesParsed) && IdentifierRequired &&
          DS.isEmpty() && NextToken().is(tok::semi)) {
        if (TryKeywordIdentFallback(false))
          continue;
      }
      [[fallthrough]];
    case tok::kw___sptr:
    case tok::kw___w64:
    case tok::kw___ptr64:
    case tok::kw___ptr32:
    case tok::kw___cdecl:
    case tok::kw_cdecl:
    case tok::kw___stdcall:
    case tok::kw___fastcall:
    case tok::kw___regcall:
    case tok::kw___vectorcall:
      if (AttrReqs & AR_DeclspecAttributesParsed) {
        ParseMicrosoftTypeAttributes(DS.getAttributes());
        continue;
      }
      goto DoneWithTypeQuals;

    // Nullability type specifiers.
    case tok::kw__Nonnull:
    case tok::kw__Nullable:
    case tok::kw__Null_unspecified:
      ParseNullabilityTypeSpecifiers(DS.getAttributes());
      continue;

    case tok::kw___attribute:
      if (AttrReqs & AR_GNUAttributesParsedAndRejected)
        // When GNU attributes are expressly forbidden, diagnose their usage.
        Diag(Tok, diag::err_attributes_not_allowed);

      // Parse the attributes even if they are rejected to ensure that error
      // recovery is graceful.
      if (AttrReqs & AR_GNUAttributesParsed ||
          AttrReqs & AR_GNUAttributesParsedAndRejected) {
        ParseGNUAttributes(DS.getAttributes());
        continue; // do *not* consume the next token!
      }
      // otherwise, FALL THROUGH!
      [[fallthrough]];
    default:
    DoneWithTypeQuals:
      // If this is not a type-qualifier token, we're done reading type
      // qualifiers.  First verify that DeclSpec's are consistent.
      DS.Finish(Actions, Actions.getTreeContext().getPrintingPolicy());
      if (EndLoc.isValid())
        DS.SetRangeEnd(EndLoc);
      return;
    }

    // If the specifier combination wasn't legal, issue a diagnostic.
    if (isInvalid) {
      assert(PrevSpec && "Method did not return previous specifier!");
      Diag(Tok, DiagID) << PrevSpec;
    }
    EndLoc = ConsumeToken();
  }
}
