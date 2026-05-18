
#if !defined(WANT_DECL_MERGE_LOGIC) && !defined(WANT_STMT_MERGE_LOGIC)
static bool isGlobalVar(const Decl *D) {
  if (const auto *S = dyn_cast<VarDecl>(D))
    return S->hasGlobalStorage();
  return false;
}

static bool isHasFunctionProto(const Decl *D) {
  if (const auto *S = dyn_cast<Decl>(D))
    return S->getFunctionType() != nullptr &&
           isa<FunctionProtoType>(S->getFunctionType());
  return false;
}

static bool isFunctionLike(const Decl *D) {
  if (const auto *S = dyn_cast<Decl>(D))
    return S->getFunctionType() != nullptr;
  return false;
}

static bool isInlineFunction(const Decl *D) {
  if (const auto *S = dyn_cast<FunctionDecl>(D))
    return S->isInlineSpecified();
  return false;
}

static bool isLocalVar(const Decl *D) {
  if (const auto *S = dyn_cast<VarDecl>(D))
    return S->hasLocalStorage() && !isa<ParmVarDecl>(S);
  return false;
}

static bool isNonParmVar(const Decl *D) {
  if (const auto *S = dyn_cast<VarDecl>(D))
    return S->getKind() != Decl::ParmVar;
  return false;
}

static bool isBitField(const Decl *D) {
  if (const auto *S = dyn_cast<FieldDecl>(D))
    return S->isBitField();
  return false;
}

static bool isNonLocalVar(const Decl *D) {
  if (const auto *S = dyn_cast<VarDecl>(D))
    return !S->hasLocalStorage();
  return false;
}

static bool isTLSVar(const Decl *D) {
  if (const auto *S = dyn_cast<VarDecl>(D))
    return S->getTLSKind() != 0;
  return false;
}

static constexpr ParsedAttrInfo::Spelling AArch64SVEPcsSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "aarch64_sve_pcs"},
    {AttributeCommonInfo::AS_Bracket, "clang::aarch64_sve_pcs"},
    {AttributeCommonInfo::AS_Bracket, "neverc::aarch64_sve_pcs"},
    {AttributeCommonInfo::AS_C23, "clang::aarch64_sve_pcs"},
    {AttributeCommonInfo::AS_C23, "neverc::aarch64_sve_pcs"},
};
struct ParsedAttrInfoAArch64SVEPcs final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAArch64SVEPcs()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AArch64SVEPcs,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/AArch64SVEPcsSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAArch64SVEPcs Instance;
};
const ParsedAttrInfoAArch64SVEPcs ParsedAttrInfoAArch64SVEPcs::Instance;
static constexpr ParsedAttrInfo::Spelling AArch64VectorPcsSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "aarch64_vector_pcs"},
    {AttributeCommonInfo::AS_Bracket, "clang::aarch64_vector_pcs"},
    {AttributeCommonInfo::AS_Bracket, "neverc::aarch64_vector_pcs"},
    {AttributeCommonInfo::AS_C23, "clang::aarch64_vector_pcs"},
    {AttributeCommonInfo::AS_C23, "neverc::aarch64_vector_pcs"},
};
struct ParsedAttrInfoAArch64VectorPcs final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAArch64VectorPcs()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AArch64VectorPcs,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/AArch64VectorPcsSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAArch64VectorPcs Instance;
};
const ParsedAttrInfoAArch64VectorPcs ParsedAttrInfoAArch64VectorPcs::Instance;
static constexpr ParsedAttrInfo::Spelling AcquireHandleSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "acquire_handle"},
    {AttributeCommonInfo::AS_Bracket, "clang::acquire_handle"},
    {AttributeCommonInfo::AS_Bracket, "neverc::acquire_handle"},
    {AttributeCommonInfo::AS_C23, "clang::acquire_handle"},
    {AttributeCommonInfo::AS_C23, "neverc::acquire_handle"},
};
static constexpr const char *AcquireHandleArgNames[] = {
    "HandleType",
};
struct ParsedAttrInfoAcquireHandle final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAcquireHandle()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AcquireHandle,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/AcquireHandleSpellings,
            /*ArgNames=*/AcquireHandleArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D) && !isa<TypedefNameDecl>(D) &&
        !isa<ParmVarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions, typedefs, and parameters";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
    MatchRules.push_back(std::make_pair(attr::SubjectMatchRule_type_alias,
                                        /*IsSupported=*/true));
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_parameter, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAcquireHandle Instance;
};
const ParsedAttrInfoAcquireHandle ParsedAttrInfoAcquireHandle::Instance;
static constexpr ParsedAttrInfo::Spelling AddressSpaceSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "address_space"},
    {AttributeCommonInfo::AS_Bracket, "clang::address_space"},
    {AttributeCommonInfo::AS_Bracket, "neverc::address_space"},
    {AttributeCommonInfo::AS_C23, "clang::address_space"},
    {AttributeCommonInfo::AS_C23, "neverc::address_space"},
};
static constexpr const char *AddressSpaceArgNames[] = {
    "AddressSpace",
};
struct ParsedAttrInfoAddressSpace final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAddressSpace()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AddressSpace,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/AddressSpaceSpellings,
            /*ArgNames=*/AddressSpaceArgNames) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAddressSpace Instance;
};
const ParsedAttrInfoAddressSpace ParsedAttrInfoAddressSpace::Instance;
static constexpr ParsedAttrInfo::Spelling AliasSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "alias"},
    {AttributeCommonInfo::AS_Bracket, "gnu::alias"},
    {AttributeCommonInfo::AS_C23, "gnu::alias"},
};
static constexpr const char *AliasArgNames[] = {
    "Aliasee",
};
struct ParsedAttrInfoAlias final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAlias()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Alias,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/AliasSpellings,
            /*ArgNames=*/AliasArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D) && !isGlobalVar(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions and global variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_global, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAlias Instance;
};
const ParsedAttrInfoAlias ParsedAttrInfoAlias::Instance;
static constexpr ParsedAttrInfo::Spelling AlignValueSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "align_value"},
};
static constexpr const char *AlignValueArgNames[] = {
    "Alignment",
};
struct ParsedAttrInfoAlignValue final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAlignValue()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AlignValue,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/AlignValueSpellings,
            /*ArgNames=*/AlignValueArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<VarDecl>(D) && !isa<TypedefNameDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "variables and typedefs";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
    MatchRules.push_back(std::make_pair(attr::SubjectMatchRule_type_alias,
                                        /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return (N == 0) || false; }

  static const ParsedAttrInfoAlignValue Instance;
};
const ParsedAttrInfoAlignValue ParsedAttrInfoAlignValue::Instance;
static constexpr ParsedAttrInfo::Spelling AlignedSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "aligned"},
    {AttributeCommonInfo::AS_Bracket, "gnu::aligned"},
    {AttributeCommonInfo::AS_C23, "gnu::aligned"},
    {AttributeCommonInfo::AS_Declspec, "align"},
    {AttributeCommonInfo::AS_Keyword, "alignas"},
    {AttributeCommonInfo::AS_Keyword, "_Alignas"},
};
static constexpr const char *AlignedArgNames[] = {
    "Alignment",
};
struct ParsedAttrInfoAligned final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAligned()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Aligned,
            /*NumArgs=*/0,
            /*OptArgs=*/1,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/AlignedSpellings,
            /*ArgNames=*/AlignedArgNames) {}
  unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const override {
    enum Spelling {
      GNU_aligned = 0,
      Bracket_gnu_aligned = 1,
      C23_gnu_aligned = 2,
      Declspec_align = 3,
      Keyword_alignas = 4,
      Keyword_Alignas = 5,
      SpellingNotCalculated = 15

    };

    unsigned Idx = Attr.getAttributeSpellingListIndex();
    switch (Idx) {
    default:
      llvm_unreachable("Unknown spelling list index");
    case 0:
      return GNU_aligned;
    case 1:
      return Bracket_gnu_aligned;
    case 2:
      return C23_gnu_aligned;
    case 3:
      return Declspec_align;
    case 4:
      return Keyword_alignas;
    case 5:
      return Keyword_Alignas;
    }
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAligned Instance;
};
const ParsedAttrInfoAligned ParsedAttrInfoAligned::Instance;
static constexpr ParsedAttrInfo::Spelling AllocAlignSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "alloc_align"},
    {AttributeCommonInfo::AS_Bracket, "gnu::alloc_align"},
    {AttributeCommonInfo::AS_C23, "gnu::alloc_align"},
};
static constexpr const char *AllocAlignArgNames[] = {
    "ParamIndex",
};
struct ParsedAttrInfoAllocAlign final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAllocAlign()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AllocAlign,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/AllocAlignSpellings,
            /*ArgNames=*/AllocAlignArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isHasFunctionProto(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "non-K&R-style functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAllocAlign Instance;
};
const ParsedAttrInfoAllocAlign ParsedAttrInfoAllocAlign::Instance;
static constexpr ParsedAttrInfo::Spelling AllocSizeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "alloc_size"},
    {AttributeCommonInfo::AS_Bracket, "gnu::alloc_size"},
    {AttributeCommonInfo::AS_C23, "gnu::alloc_size"},
};
static constexpr const char *AllocSizeArgNames[] = {
    "ElemSizeParam",
    "NumElemsParam",
};
struct ParsedAttrInfoAllocSize final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAllocSize()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AllocSize,
            /*NumArgs=*/1,
            /*OptArgs=*/1,
            /*NumArgMembers=*/2,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/AllocSizeSpellings,
            /*ArgNames=*/AllocSizeArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isHasFunctionProto(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "non-K&R-style functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAllocSize Instance;
};
const ParsedAttrInfoAllocSize ParsedAttrInfoAllocSize::Instance;
static constexpr ParsedAttrInfo::Spelling AlwaysDestroySpellings[] = {
    {AttributeCommonInfo::AS_GNU, "always_destroy"},
    {AttributeCommonInfo::AS_Bracket, "clang::always_destroy"},
    {AttributeCommonInfo::AS_Bracket, "neverc::always_destroy"},
};
struct ParsedAttrInfoAlwaysDestroy final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAlwaysDestroy()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AlwaysDestroy,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/AlwaysDestroySpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<VarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<NoDestroyAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAlwaysDestroy Instance;
};
const ParsedAttrInfoAlwaysDestroy ParsedAttrInfoAlwaysDestroy::Instance;
static constexpr ParsedAttrInfo::Spelling AlwaysInlineSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "always_inline"},
    {AttributeCommonInfo::AS_Bracket, "gnu::always_inline"},
    {AttributeCommonInfo::AS_C23, "gnu::always_inline"},
    {AttributeCommonInfo::AS_Bracket, "neverc::always_inline"},
    {AttributeCommonInfo::AS_C23, "neverc::always_inline"},
    {AttributeCommonInfo::AS_Keyword, "__forceinline"},
};
struct ParsedAttrInfoAlwaysInline final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAlwaysInline()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AlwaysInline,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/1,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/AlwaysInlineSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions and statements";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &Attr,
                            const Stmt *St) const override {
    if (!isa<Stmt>(St)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions and statements";
      return false;
    }
    return true;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<NotTailCalledAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const override {
    enum Spelling {
      GNU_always_inline = 0,
      Bracket_gnu_always_inline = 1,
      C23_gnu_always_inline = 2,
      Bracket_neverc_always_inline = 3,
      C23_neverc_always_inline = 4,
      Keyword_forceinline = 5,
      SpellingNotCalculated = 15

    };

    unsigned Idx = Attr.getAttributeSpellingListIndex();
    switch (Idx) {
    default:
      llvm_unreachable("Unknown spelling list index");
    case 0:
      return GNU_always_inline;
    case 1:
      return Bracket_gnu_always_inline;
    case 2:
      return C23_gnu_always_inline;
    case 3:
      return Bracket_neverc_always_inline;
    case 4:
      return C23_neverc_always_inline;
    case 5:
      return Keyword_forceinline;
    }
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAlwaysInline Instance;
};
const ParsedAttrInfoAlwaysInline ParsedAttrInfoAlwaysInline::Instance;
static constexpr ParsedAttrInfo::Spelling AnalyzerNoReturnSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "analyzer_noreturn"},
};
struct ParsedAttrInfoAnalyzerNoReturn final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAnalyzerNoReturn()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AnalyzerNoReturn,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/AnalyzerNoReturnSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAnalyzerNoReturn Instance;
};
const ParsedAttrInfoAnalyzerNoReturn ParsedAttrInfoAnalyzerNoReturn::Instance;
static constexpr ParsedAttrInfo::Spelling AnnotateSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "annotate"},
    {AttributeCommonInfo::AS_Bracket, "clang::annotate"},
    {AttributeCommonInfo::AS_Bracket, "neverc::annotate"},
    {AttributeCommonInfo::AS_C23, "clang::annotate"},
    {AttributeCommonInfo::AS_C23, "neverc::annotate"},
};
static constexpr const char *AnnotateArgNames[] = {
    "Annotation",
    "Args...",
};
struct ParsedAttrInfoAnnotate final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAnnotate()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Annotate,
            /*NumArgs=*/1,
            /*OptArgs=*/15,
            /*NumArgMembers=*/2,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/AnnotateSpellings,
            /*ArgNames=*/AnnotateArgNames) {}
  bool isParamExpr(size_t N) const override { return (N == 1) || false; }

  static const ParsedAttrInfoAnnotate Instance;
};
const ParsedAttrInfoAnnotate ParsedAttrInfoAnnotate::Instance;
static constexpr ParsedAttrInfo::Spelling AnnotateTypeSpellings[] = {
    {AttributeCommonInfo::AS_Bracket, "neverc::annotate_type"},
    {AttributeCommonInfo::AS_C23, "neverc::annotate_type"},
};
static constexpr const char *AnnotateTypeArgNames[] = {
    "Annotation",
    "Args...",
};
struct ParsedAttrInfoAnnotateType final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAnnotateType()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AnnotateType,
            /*NumArgs=*/1,
            /*OptArgs=*/15,
            /*NumArgMembers=*/2,
            /*HasCustomParsing=*/1,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/AnnotateTypeSpellings,
            /*ArgNames=*/AnnotateTypeArgNames) {}
  bool isParamExpr(size_t N) const override { return (N == 1) || false; }

  static const ParsedAttrInfoAnnotateType Instance;
};
const ParsedAttrInfoAnnotateType ParsedAttrInfoAnnotateType::Instance;
static constexpr ParsedAttrInfo::Spelling InterruptSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "interrupt"},
    {AttributeCommonInfo::AS_Bracket, "gnu::interrupt"},
    {AttributeCommonInfo::AS_C23, "gnu::interrupt"},
};
struct ParsedAttrInfoInterrupt final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoInterrupt()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Interrupt,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/1,
            /*IsTargetSpecific=*/1,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/InterruptSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isHasFunctionProto(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "non-K&R-style functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::x86_64);
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoInterrupt Instance;
};
const ParsedAttrInfoInterrupt ParsedAttrInfoInterrupt::Instance;
static constexpr ParsedAttrInfo::Spelling
    AnyX86NoCallerSavedRegistersSpellings[] = {
        {AttributeCommonInfo::AS_GNU, "no_caller_saved_registers"},
        {AttributeCommonInfo::AS_Bracket, "gnu::no_caller_saved_registers"},
        {AttributeCommonInfo::AS_C23, "gnu::no_caller_saved_registers"},
};
struct ParsedAttrInfoAnyX86NoCallerSavedRegisters final
    : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAnyX86NoCallerSavedRegisters()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AnyX86NoCallerSavedRegisters,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/AnyX86NoCallerSavedRegistersSpellings,
            /*ArgNames=*/{}) {}
  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::x86_64);
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context)
                   AnyX86NoCallerSavedRegistersAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAnyX86NoCallerSavedRegisters Instance;
};
const ParsedAttrInfoAnyX86NoCallerSavedRegisters
    ParsedAttrInfoAnyX86NoCallerSavedRegisters::Instance;
static constexpr ParsedAttrInfo::Spelling AnyX86NoCfCheckSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "nocf_check"},
    {AttributeCommonInfo::AS_Bracket, "gnu::nocf_check"},
    {AttributeCommonInfo::AS_C23, "gnu::nocf_check"},
};
struct ParsedAttrInfoAnyX86NoCfCheck final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAnyX86NoCfCheck()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AnyX86NoCfCheck,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/AnyX86NoCfCheckSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isFunctionLike(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions and function pointers";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::x86_64);
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_hasType_functionType, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAnyX86NoCfCheck Instance;
};
const ParsedAttrInfoAnyX86NoCfCheck ParsedAttrInfoAnyX86NoCfCheck::Instance;
static constexpr ParsedAttrInfo::Spelling ArgumentWithTypeTagSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "argument_with_type_tag"},
    {AttributeCommonInfo::AS_Bracket, "clang::argument_with_type_tag"},
    {AttributeCommonInfo::AS_Bracket, "neverc::argument_with_type_tag"},
    {AttributeCommonInfo::AS_C23, "clang::argument_with_type_tag"},
    {AttributeCommonInfo::AS_C23, "neverc::argument_with_type_tag"},
    {AttributeCommonInfo::AS_GNU, "pointer_with_type_tag"},
    {AttributeCommonInfo::AS_Bracket, "clang::pointer_with_type_tag"},
    {AttributeCommonInfo::AS_Bracket, "neverc::pointer_with_type_tag"},
    {AttributeCommonInfo::AS_C23, "clang::pointer_with_type_tag"},
    {AttributeCommonInfo::AS_C23, "neverc::pointer_with_type_tag"},
};
static constexpr const char *ArgumentWithTypeTagArgNames[] = {
    "ArgumentKind",
    "ArgumentIdx",
    "TypeTagIdx",
};
struct ParsedAttrInfoArgumentWithTypeTag final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoArgumentWithTypeTag()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ArgumentWithTypeTag,
            /*NumArgs=*/3,
            /*OptArgs=*/0,
            /*NumArgMembers=*/3,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ArgumentWithTypeTagSpellings,
            /*ArgNames=*/ArgumentWithTypeTagArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isHasFunctionProto(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "non-K&R-style functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const override {
    enum Spelling {
      GNU_argument_with_type_tag = 0,
      Bracket_clang_argument_with_type_tag = 1,
      Bracket_neverc_argument_with_type_tag = 2,
      C23_clang_argument_with_type_tag = 3,
      C23_neverc_argument_with_type_tag = 4,
      GNU_pointer_with_type_tag = 5,
      Bracket_clang_pointer_with_type_tag = 6,
      Bracket_neverc_pointer_with_type_tag = 7,
      C23_clang_pointer_with_type_tag = 8,
      C23_neverc_pointer_with_type_tag = 9,
      SpellingNotCalculated = 15

    };

    unsigned Idx = Attr.getAttributeSpellingListIndex();
    switch (Idx) {
    default:
      llvm_unreachable("Unknown spelling list index");
    case 0:
      return GNU_argument_with_type_tag;
    case 1:
      return Bracket_clang_argument_with_type_tag;
    case 2:
      return Bracket_neverc_argument_with_type_tag;
    case 3:
      return C23_clang_argument_with_type_tag;
    case 4:
      return C23_neverc_argument_with_type_tag;
    case 5:
      return GNU_pointer_with_type_tag;
    case 6:
      return Bracket_clang_pointer_with_type_tag;
    case 7:
      return Bracket_neverc_pointer_with_type_tag;
    case 8:
      return C23_clang_pointer_with_type_tag;
    case 9:
      return C23_neverc_pointer_with_type_tag;
    }
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoArgumentWithTypeTag Instance;
};
const ParsedAttrInfoArgumentWithTypeTag
    ParsedAttrInfoArgumentWithTypeTag::Instance;
static constexpr ParsedAttrInfo::Spelling ArmBuiltinAliasSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "__neverc_arm_builtin_alias"},
    {AttributeCommonInfo::AS_Bracket, "clang::__neverc_arm_builtin_alias"},
    {AttributeCommonInfo::AS_Bracket, "neverc::__neverc_arm_builtin_alias"},
    {AttributeCommonInfo::AS_C23, "clang::__neverc_arm_builtin_alias"},
    {AttributeCommonInfo::AS_C23, "neverc::__neverc_arm_builtin_alias"},
};
static constexpr const char *ArmBuiltinAliasArgNames[] = {
    "BuiltinName",
};
struct ParsedAttrInfoArmBuiltinAlias final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoArmBuiltinAlias()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ArmBuiltinAlias,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/ArmBuiltinAliasSpellings,
            /*ArgNames=*/ArmBuiltinAliasArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::aarch64);
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoArmBuiltinAlias Instance;
};
const ParsedAttrInfoArmBuiltinAlias ParsedAttrInfoArmBuiltinAlias::Instance;
static constexpr ParsedAttrInfo::Spelling ArmLocallyStreamingSpellings[] = {
    {AttributeCommonInfo::AS_Keyword, "__arm_locally_streaming"},
};
struct ParsedAttrInfoArmLocallyStreaming final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoArmLocallyStreaming()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ArmLocallyStreaming,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ArmLocallyStreamingSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::aarch64);
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoArmLocallyStreaming Instance;
};
const ParsedAttrInfoArmLocallyStreaming
    ParsedAttrInfoArmLocallyStreaming::Instance;
static constexpr ParsedAttrInfo::Spelling ArmNewZASpellings[] = {
    {AttributeCommonInfo::AS_Keyword, "__arm_new_za"},
};
struct ParsedAttrInfoArmNewZA final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoArmNewZA()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ArmNewZA,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ArmNewZASpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<ArmSharedZAAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<ArmPreservesZAAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::aarch64);
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoArmNewZA Instance;
};
const ParsedAttrInfoArmNewZA ParsedAttrInfoArmNewZA::Instance;
static constexpr ParsedAttrInfo::Spelling ArmPreservesZASpellings[] = {
    {AttributeCommonInfo::AS_Keyword, "__arm_preserves_za"},
};
struct ParsedAttrInfoArmPreservesZA final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoArmPreservesZA()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ArmPreservesZA,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ArmPreservesZASpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isHasFunctionProto(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "non-K&R-style functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::aarch64);
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoArmPreservesZA Instance;
};
const ParsedAttrInfoArmPreservesZA ParsedAttrInfoArmPreservesZA::Instance;
static constexpr ParsedAttrInfo::Spelling ArmSharedZASpellings[] = {
    {AttributeCommonInfo::AS_Keyword, "__arm_shared_za"},
};
struct ParsedAttrInfoArmSharedZA final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoArmSharedZA()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ArmSharedZA,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ArmSharedZASpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isHasFunctionProto(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "non-K&R-style functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::aarch64);
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoArmSharedZA Instance;
};
const ParsedAttrInfoArmSharedZA ParsedAttrInfoArmSharedZA::Instance;
static constexpr ParsedAttrInfo::Spelling ArmStreamingSpellings[] = {
    {AttributeCommonInfo::AS_Keyword, "__arm_streaming"},
};
struct ParsedAttrInfoArmStreaming final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoArmStreaming()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ArmStreaming,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ArmStreamingSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isHasFunctionProto(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "non-K&R-style functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::aarch64);
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoArmStreaming Instance;
};
const ParsedAttrInfoArmStreaming ParsedAttrInfoArmStreaming::Instance;
static constexpr ParsedAttrInfo::Spelling ArmStreamingCompatibleSpellings[] = {
    {AttributeCommonInfo::AS_Keyword, "__arm_streaming_compatible"},
};
struct ParsedAttrInfoArmStreamingCompatible final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoArmStreamingCompatible()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ArmStreamingCompatible,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ArmStreamingCompatibleSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isHasFunctionProto(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "non-K&R-style functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::aarch64);
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoArmStreamingCompatible Instance;
};
const ParsedAttrInfoArmStreamingCompatible
    ParsedAttrInfoArmStreamingCompatible::Instance;
static constexpr ParsedAttrInfo::Spelling ArmSveVectorBitsSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "arm_sve_vector_bits"},
};
static constexpr const char *ArmSveVectorBitsArgNames[] = {
    "NumBits",
};
struct ParsedAttrInfoArmSveVectorBits final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoArmSveVectorBits()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ArmSveVectorBits,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ArmSveVectorBitsSpellings,
            /*ArgNames=*/ArmSveVectorBitsArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<TypedefNameDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "typedefs";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoArmSveVectorBits Instance;
};
const ParsedAttrInfoArmSveVectorBits ParsedAttrInfoArmSveVectorBits::Instance;
static constexpr ParsedAttrInfo::Spelling ArtificialSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "artificial"},
    {AttributeCommonInfo::AS_Bracket, "gnu::artificial"},
    {AttributeCommonInfo::AS_C23, "gnu::artificial"},
};
struct ParsedAttrInfoArtificial final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoArtificial()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Artificial,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ArtificialSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isInlineFunction(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "inline functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) ArtificialAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoArtificial Instance;
};
const ParsedAttrInfoArtificial ParsedAttrInfoArtificial::Instance;
static constexpr ParsedAttrInfo::Spelling AssumeAlignedSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "assume_aligned"},
    {AttributeCommonInfo::AS_Bracket, "gnu::assume_aligned"},
    {AttributeCommonInfo::AS_C23, "gnu::assume_aligned"},
};
static constexpr const char *AssumeAlignedArgNames[] = {
    "Alignment",
    "Offset",
};
struct ParsedAttrInfoAssumeAligned final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAssumeAligned()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AssumeAligned,
            /*NumArgs=*/1,
            /*OptArgs=*/1,
            /*NumArgMembers=*/2,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/AssumeAlignedSpellings,
            /*ArgNames=*/AssumeAlignedArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override {
    return (N == 0) || (N == 1) || false;
  }

  static const ParsedAttrInfoAssumeAligned Instance;
};
const ParsedAttrInfoAssumeAligned ParsedAttrInfoAssumeAligned::Instance;
static constexpr ParsedAttrInfo::Spelling AssumptionSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "assume"},
    {AttributeCommonInfo::AS_Bracket, "clang::assume"},
    {AttributeCommonInfo::AS_Bracket, "neverc::assume"},
    {AttributeCommonInfo::AS_C23, "clang::assume"},
    {AttributeCommonInfo::AS_C23, "neverc::assume"},
};
static constexpr const char *AssumptionArgNames[] = {
    "Assumption",
};
struct ParsedAttrInfoAssumption final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAssumption()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Assumption,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/AssumptionSpellings,
            /*ArgNames=*/AssumptionArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAssumption Instance;
};
const ParsedAttrInfoAssumption ParsedAttrInfoAssumption::Instance;
static constexpr ParsedAttrInfo::Spelling AvailabilitySpellings[] = {
    {AttributeCommonInfo::AS_GNU, "availability"},
    {AttributeCommonInfo::AS_Bracket, "clang::availability"},
    {AttributeCommonInfo::AS_Bracket, "neverc::availability"},
    {AttributeCommonInfo::AS_C23, "clang::availability"},
    {AttributeCommonInfo::AS_C23, "neverc::availability"},
};
static constexpr const char *AvailabilityArgNames[] = {
    "platform", "introduced", "deprecated",  "obsoleted", "unavailable",
    "message",  "strict",     "replacement", "priority",
};
struct ParsedAttrInfoAvailability final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAvailability()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Availability,
            /*NumArgs=*/9,
            /*OptArgs=*/0,
            /*NumArgMembers=*/9,
            /*HasCustomParsing=*/1,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/AvailabilitySpellings,
            /*ArgNames=*/AvailabilityArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<NamedDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "named declarations";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_enum, /*IsSupported=*/true));
    MatchRules.push_back(std::make_pair(attr::SubjectMatchRule_enum_constant,
                                        /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_field, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_record, /*IsSupported=*/true));
    MatchRules.push_back(std::make_pair(attr::SubjectMatchRule_type_alias,
                                        /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAvailability Instance;
};
const ParsedAttrInfoAvailability ParsedAttrInfoAvailability::Instance;
static constexpr ParsedAttrInfo::Spelling
    AvailableOnlyInDefaultEvalMethodSpellings[] = {
        {AttributeCommonInfo::AS_GNU, "available_only_in_default_eval_method"},
        {AttributeCommonInfo::AS_Bracket,
         "clang::available_only_in_default_eval_method"},
        {AttributeCommonInfo::AS_Bracket,
         "neverc::available_only_in_default_eval_method"},
        {AttributeCommonInfo::AS_C23,
         "clang::available_only_in_default_eval_method"},
        {AttributeCommonInfo::AS_C23,
         "neverc::available_only_in_default_eval_method"},
};
struct ParsedAttrInfoAvailableOnlyInDefaultEvalMethod final
    : public ParsedAttrInfo {
  constexpr ParsedAttrInfoAvailableOnlyInDefaultEvalMethod()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_AvailableOnlyInDefaultEvalMethod,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/AvailableOnlyInDefaultEvalMethodSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<TypedefNameDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "typedefs";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(attr::SubjectMatchRule_type_alias,
                                        /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoAvailableOnlyInDefaultEvalMethod Instance;
};
const ParsedAttrInfoAvailableOnlyInDefaultEvalMethod
    ParsedAttrInfoAvailableOnlyInDefaultEvalMethod::Instance;
static constexpr ParsedAttrInfo::Spelling BTFDeclTagSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "btf_decl_tag"},
    {AttributeCommonInfo::AS_Bracket, "clang::btf_decl_tag"},
    {AttributeCommonInfo::AS_Bracket, "neverc::btf_decl_tag"},
    {AttributeCommonInfo::AS_C23, "clang::btf_decl_tag"},
    {AttributeCommonInfo::AS_C23, "neverc::btf_decl_tag"},
};
static constexpr const char *BTFDeclTagArgNames[] = {
    "BTFDeclTag",
};
struct ParsedAttrInfoBTFDeclTag final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoBTFDeclTag()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_BTFDeclTag,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/BTFDeclTagSpellings,
            /*ArgNames=*/BTFDeclTagArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<VarDecl>(D) && !isa<FunctionDecl>(D) && !isa<RecordDecl>(D) &&
        !isa<FieldDecl>(D) && !isa<TypedefNameDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "variables, functions, structs, unions, fields, and typedefs";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_record, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_field, /*IsSupported=*/true));
    MatchRules.push_back(std::make_pair(attr::SubjectMatchRule_type_alias,
                                        /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoBTFDeclTag Instance;
};
const ParsedAttrInfoBTFDeclTag ParsedAttrInfoBTFDeclTag::Instance;
static constexpr ParsedAttrInfo::Spelling BTFTypeTagSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "btf_type_tag"},
    {AttributeCommonInfo::AS_Bracket, "clang::btf_type_tag"},
    {AttributeCommonInfo::AS_Bracket, "neverc::btf_type_tag"},
    {AttributeCommonInfo::AS_C23, "clang::btf_type_tag"},
    {AttributeCommonInfo::AS_C23, "neverc::btf_type_tag"},
};
static constexpr const char *BTFTypeTagArgNames[] = {
    "BTFTypeTag",
};
struct ParsedAttrInfoBTFTypeTag final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoBTFTypeTag()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_BTFTypeTag,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/BTFTypeTagSpellings,
            /*ArgNames=*/BTFTypeTagArgNames) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoBTFTypeTag Instance;
};
const ParsedAttrInfoBTFTypeTag ParsedAttrInfoBTFTypeTag::Instance;
static constexpr ParsedAttrInfo::Spelling BuiltinAliasSpellings[] = {
    {AttributeCommonInfo::AS_Bracket, "neverc::builtin_alias"},
    {AttributeCommonInfo::AS_C23, "neverc::builtin_alias"},
    {AttributeCommonInfo::AS_GNU, "neverc_builtin_alias"},
};
static constexpr const char *BuiltinAliasArgNames[] = {
    "BuiltinName",
};
struct ParsedAttrInfoBuiltinAlias final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoBuiltinAlias()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_BuiltinAlias,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/BuiltinAliasSpellings,
            /*ArgNames=*/BuiltinAliasArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const override {
    enum Spelling {
      Bracket_neverc_builtin_alias = 0,
      C23_neverc_builtin_alias = 1,
      GNU_neverc_builtin_alias = 2,
      SpellingNotCalculated = 15

    };

    unsigned Idx = Attr.getAttributeSpellingListIndex();
    switch (Idx) {
    default:
      llvm_unreachable("Unknown spelling list index");
    case 0:
      return Bracket_neverc_builtin_alias;
    case 1:
      return C23_neverc_builtin_alias;
    case 2:
      return GNU_neverc_builtin_alias;
    }
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoBuiltinAlias Instance;
};
const ParsedAttrInfoBuiltinAlias ParsedAttrInfoBuiltinAlias::Instance;
static constexpr ParsedAttrInfo::Spelling CDeclSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "cdecl"},
    {AttributeCommonInfo::AS_Bracket, "gnu::cdecl"},
    {AttributeCommonInfo::AS_C23, "gnu::cdecl"},
    {AttributeCommonInfo::AS_Keyword, "__cdecl"},
    {AttributeCommonInfo::AS_Keyword, "_cdecl"},
    {AttributeCommonInfo::AS_Keyword, "cdecl"},
};
struct ParsedAttrInfoCDecl final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoCDecl()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_CDecl,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/CDeclSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoCDecl Instance;
};
const ParsedAttrInfoCDecl ParsedAttrInfoCDecl::Instance;
static constexpr ParsedAttrInfo::Spelling CFGuardSpellings[] = {
    {AttributeCommonInfo::AS_Declspec, "guard"},
    {AttributeCommonInfo::AS_GNU, "guard"},
    {AttributeCommonInfo::AS_Bracket, "clang::guard"},
    {AttributeCommonInfo::AS_Bracket, "neverc::guard"},
    {AttributeCommonInfo::AS_C23, "clang::guard"},
    {AttributeCommonInfo::AS_C23, "neverc::guard"},
};
static constexpr const char *CFGuardArgNames[] = {
    "Guard",
};
struct ParsedAttrInfoCFGuard final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoCFGuard()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_CFGuard,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/CFGuardSpellings,
            /*ArgNames=*/CFGuardArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getOS() == llvm::Triple::Win32);
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoCFGuard Instance;
};
const ParsedAttrInfoCFGuard ParsedAttrInfoCFGuard::Instance;
static constexpr ParsedAttrInfo::Spelling CPUDispatchSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "cpu_dispatch"},
    {AttributeCommonInfo::AS_Bracket, "clang::cpu_dispatch"},
    {AttributeCommonInfo::AS_Bracket, "neverc::cpu_dispatch"},
    {AttributeCommonInfo::AS_C23, "clang::cpu_dispatch"},
    {AttributeCommonInfo::AS_C23, "neverc::cpu_dispatch"},
    {AttributeCommonInfo::AS_Declspec, "cpu_dispatch"},
};
static constexpr const char *CPUDispatchArgNames[] = {
    "Cpus...",
};
struct ParsedAttrInfoCPUDispatch final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoCPUDispatch()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_CPUDispatch,
            /*NumArgs=*/0,
            /*OptArgs=*/15,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/CPUDispatchSpellings,
            /*ArgNames=*/CPUDispatchArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<TargetClonesAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<TargetVersionAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<TargetAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<CPUSpecificAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoCPUDispatch Instance;
};
const ParsedAttrInfoCPUDispatch ParsedAttrInfoCPUDispatch::Instance;
static constexpr ParsedAttrInfo::Spelling CPUSpecificSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "cpu_specific"},
    {AttributeCommonInfo::AS_Bracket, "clang::cpu_specific"},
    {AttributeCommonInfo::AS_Bracket, "neverc::cpu_specific"},
    {AttributeCommonInfo::AS_C23, "clang::cpu_specific"},
    {AttributeCommonInfo::AS_C23, "neverc::cpu_specific"},
    {AttributeCommonInfo::AS_Declspec, "cpu_specific"},
};
static constexpr const char *CPUSpecificArgNames[] = {
    "Cpus...",
};
struct ParsedAttrInfoCPUSpecific final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoCPUSpecific()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_CPUSpecific,
            /*NumArgs=*/0,
            /*OptArgs=*/15,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/CPUSpecificSpellings,
            /*ArgNames=*/CPUSpecificArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<TargetClonesAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<TargetVersionAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<TargetAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<CPUDispatchAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoCPUSpecific Instance;
};
const ParsedAttrInfoCPUSpecific ParsedAttrInfoCPUSpecific::Instance;
static constexpr ParsedAttrInfo::Spelling CallbackSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "callback"},
    {AttributeCommonInfo::AS_Bracket, "clang::callback"},
    {AttributeCommonInfo::AS_Bracket, "neverc::callback"},
    {AttributeCommonInfo::AS_C23, "clang::callback"},
    {AttributeCommonInfo::AS_C23, "neverc::callback"},
};
static constexpr const char *CallbackArgNames[] = {
    "Encoding...",
};
struct ParsedAttrInfoCallback final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoCallback()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Callback,
            /*NumArgs=*/0,
            /*OptArgs=*/15,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/CallbackSpellings,
            /*ArgNames=*/CallbackArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoCallback Instance;
};
const ParsedAttrInfoCallback ParsedAttrInfoCallback::Instance;
static constexpr ParsedAttrInfo::Spelling CarriesDependencySpellings[] = {
    {AttributeCommonInfo::AS_GNU, "carries_dependency"},
    {AttributeCommonInfo::AS_Bracket, "carries_dependency"},
};
struct ParsedAttrInfoCarriesDependency final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoCarriesDependency()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_CarriesDependency,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/CarriesDependencySpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<ParmVarDecl>(D) && !isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "parameters and functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_parameter, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoCarriesDependency Instance;
};
const ParsedAttrInfoCarriesDependency ParsedAttrInfoCarriesDependency::Instance;
static constexpr ParsedAttrInfo::Spelling CleanupSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "cleanup"},
    {AttributeCommonInfo::AS_Bracket, "gnu::cleanup"},
    {AttributeCommonInfo::AS_C23, "gnu::cleanup"},
};
static constexpr const char *CleanupArgNames[] = {
    "FunctionDecl",
};
struct ParsedAttrInfoCleanup final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoCleanup()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Cleanup,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/CleanupSpellings,
            /*ArgNames=*/CleanupArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isLocalVar(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "local variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_local, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoCleanup Instance;
};
const ParsedAttrInfoCleanup ParsedAttrInfoCleanup::Instance;
static constexpr ParsedAttrInfo::Spelling CodeAlignSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "code_align"},
    {AttributeCommonInfo::AS_Bracket, "clang::code_align"},
    {AttributeCommonInfo::AS_Bracket, "neverc::code_align"},
    {AttributeCommonInfo::AS_C23, "clang::code_align"},
    {AttributeCommonInfo::AS_C23, "neverc::code_align"},
};
static constexpr const char *CodeAlignArgNames[] = {
    "Alignment",
};
struct ParsedAttrInfoCodeAlign final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoCodeAlign()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_CodeAlign,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/1,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/CodeAlignSpellings,
            /*ArgNames=*/CodeAlignArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &AL,
                            const Decl *D) const override {
    S.Diag(AL.getLoc(), diag::err_attribute_invalid_on_decl)
        << AL << AL.isRegularKeywordAttribute() << D->getLocation();
    return false;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &Attr,
                            const Stmt *St) const override {
    if (!isa<ForStmt>(St) && !isa<WhileStmt>(St) && !isa<DoStmt>(St)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "'for', 'while', and 'do' statements";
      return false;
    }
    return true;
  }

  bool isParamExpr(size_t N) const override { return (N == 0) || false; }

  static const ParsedAttrInfoCodeAlign Instance;
};
const ParsedAttrInfoCodeAlign ParsedAttrInfoCodeAlign::Instance;
static constexpr ParsedAttrInfo::Spelling CodeSegSpellings[] = {
    {AttributeCommonInfo::AS_Declspec, "code_seg"},
};
static constexpr const char *CodeSegArgNames[] = {
    "Name",
};
struct ParsedAttrInfoCodeSeg final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoCodeSeg()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_CodeSeg,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/CodeSegSpellings,
            /*ArgNames=*/CodeSegArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoCodeSeg Instance;
};
const ParsedAttrInfoCodeSeg ParsedAttrInfoCodeSeg::Instance;
static constexpr ParsedAttrInfo::Spelling ColdSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "cold"},
    {AttributeCommonInfo::AS_Bracket, "gnu::cold"},
    {AttributeCommonInfo::AS_C23, "gnu::cold"},
};
struct ParsedAttrInfoCold final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoCold()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Cold,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/ColdSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<HotAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) ColdAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoCold Instance;
};
const ParsedAttrInfoCold ParsedAttrInfoCold::Instance;
static constexpr ParsedAttrInfo::Spelling CommonSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "common"},
    {AttributeCommonInfo::AS_Bracket, "gnu::common"},
    {AttributeCommonInfo::AS_C23, "gnu::common"},
};
struct ParsedAttrInfoCommon final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoCommon()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Common,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/CommonSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<VarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<InternalLinkageAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoCommon Instance;
};
const ParsedAttrInfoCommon ParsedAttrInfoCommon::Instance;
static constexpr ParsedAttrInfo::Spelling ConstSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "const"},
    {AttributeCommonInfo::AS_Bracket, "gnu::const"},
    {AttributeCommonInfo::AS_C23, "gnu::const"},
    {AttributeCommonInfo::AS_GNU, "__const"},
    {AttributeCommonInfo::AS_Bracket, "gnu::__const"},
    {AttributeCommonInfo::AS_C23, "gnu::__const"},
};
struct ParsedAttrInfoConst final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoConst()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Const,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ConstSpellings,
            /*ArgNames=*/{}) {}
  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) ConstAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoConst Instance;
};
const ParsedAttrInfoConst ParsedAttrInfoConst::Instance;
static constexpr ParsedAttrInfo::Spelling ConstructorSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "constructor"},
    {AttributeCommonInfo::AS_Bracket, "gnu::constructor"},
    {AttributeCommonInfo::AS_C23, "gnu::constructor"},
};
static constexpr const char *ConstructorArgNames[] = {
    "Priority",
};
struct ParsedAttrInfoConstructor final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoConstructor()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Constructor,
            /*NumArgs=*/0,
            /*OptArgs=*/1,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/ConstructorSpellings,
            /*ArgNames=*/ConstructorArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoConstructor Instance;
};
const ParsedAttrInfoConstructor ParsedAttrInfoConstructor::Instance;
static constexpr ParsedAttrInfo::Spelling ConvergentSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "convergent"},
    {AttributeCommonInfo::AS_Bracket, "clang::convergent"},
    {AttributeCommonInfo::AS_Bracket, "neverc::convergent"},
    {AttributeCommonInfo::AS_C23, "clang::convergent"},
    {AttributeCommonInfo::AS_C23, "neverc::convergent"},
};
struct ParsedAttrInfoConvergent final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoConvergent()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Convergent,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/ConvergentSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) ConvergentAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoConvergent Instance;
};
const ParsedAttrInfoConvergent ParsedAttrInfoConvergent::Instance;
static constexpr ParsedAttrInfo::Spelling DLLExportSpellings[] = {
    {AttributeCommonInfo::AS_Declspec, "dllexport"},
    {AttributeCommonInfo::AS_GNU, "dllexport"},
    {AttributeCommonInfo::AS_Bracket, "gnu::dllexport"},
    {AttributeCommonInfo::AS_C23, "gnu::dllexport"},
};
struct ParsedAttrInfoDLLExport final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoDLLExport()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_DLLExport,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/DLLExportSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D) && !isa<VarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions and variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (Target.getTriple().hasDLLImportExport());
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoDLLExport Instance;
};
const ParsedAttrInfoDLLExport ParsedAttrInfoDLLExport::Instance;
static constexpr ParsedAttrInfo::Spelling DLLImportSpellings[] = {
    {AttributeCommonInfo::AS_Declspec, "dllimport"},
    {AttributeCommonInfo::AS_GNU, "dllimport"},
    {AttributeCommonInfo::AS_Bracket, "gnu::dllimport"},
    {AttributeCommonInfo::AS_C23, "gnu::dllimport"},
};
struct ParsedAttrInfoDLLImport final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoDLLImport()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_DLLImport,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/DLLImportSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D) && !isa<VarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions and variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (Target.getTriple().hasDLLImportExport());
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoDLLImport Instance;
};
const ParsedAttrInfoDLLImport ParsedAttrInfoDLLImport::Instance;
static constexpr ParsedAttrInfo::Spelling DeprecatedSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "deprecated"},
    {AttributeCommonInfo::AS_Bracket, "gnu::deprecated"},
    {AttributeCommonInfo::AS_C23, "gnu::deprecated"},
    {AttributeCommonInfo::AS_Declspec, "deprecated"},
    {AttributeCommonInfo::AS_Bracket, "deprecated"},
    {AttributeCommonInfo::AS_C23, "deprecated"},
};
static constexpr const char *DeprecatedArgNames[] = {
    "Message",
    "Replacement",
};
struct ParsedAttrInfoDeprecated final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoDeprecated()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Deprecated,
            /*NumArgs=*/0,
            /*OptArgs=*/2,
            /*NumArgMembers=*/2,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/DeprecatedSpellings,
            /*ArgNames=*/DeprecatedArgNames) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoDeprecated Instance;
};
const ParsedAttrInfoDeprecated ParsedAttrInfoDeprecated::Instance;
static constexpr ParsedAttrInfo::Spelling DestructorSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "destructor"},
    {AttributeCommonInfo::AS_Bracket, "gnu::destructor"},
    {AttributeCommonInfo::AS_C23, "gnu::destructor"},
};
static constexpr const char *DestructorArgNames[] = {
    "Priority",
};
struct ParsedAttrInfoDestructor final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoDestructor()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Destructor,
            /*NumArgs=*/0,
            /*OptArgs=*/1,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/DestructorSpellings,
            /*ArgNames=*/DestructorArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoDestructor Instance;
};
const ParsedAttrInfoDestructor ParsedAttrInfoDestructor::Instance;
static constexpr ParsedAttrInfo::Spelling DiagnoseAsBuiltinSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "diagnose_as_builtin"},
    {AttributeCommonInfo::AS_Bracket, "clang::diagnose_as_builtin"},
    {AttributeCommonInfo::AS_Bracket, "neverc::diagnose_as_builtin"},
    {AttributeCommonInfo::AS_C23, "clang::diagnose_as_builtin"},
    {AttributeCommonInfo::AS_C23, "neverc::diagnose_as_builtin"},
};
static constexpr const char *DiagnoseAsBuiltinArgNames[] = {
    "Function",
    "ArgIndices...",
};
struct ParsedAttrInfoDiagnoseAsBuiltin final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoDiagnoseAsBuiltin()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_DiagnoseAsBuiltin,
            /*NumArgs=*/1,
            /*OptArgs=*/15,
            /*NumArgMembers=*/2,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/DiagnoseAsBuiltinSpellings,
            /*ArgNames=*/DiagnoseAsBuiltinArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoDiagnoseAsBuiltin Instance;
};
const ParsedAttrInfoDiagnoseAsBuiltin ParsedAttrInfoDiagnoseAsBuiltin::Instance;
static constexpr ParsedAttrInfo::Spelling DiagnoseIfSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "diagnose_if"},
};
static constexpr const char *DiagnoseIfArgNames[] = {
    "Cond",
    "Message",
    "DiagnosticType",
};
struct ParsedAttrInfoDiagnoseIf final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoDiagnoseIf()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_DiagnoseIf,
            /*NumArgs=*/3,
            /*OptArgs=*/0,
            /*NumArgMembers=*/3,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/DiagnoseIfSpellings,
            /*ArgNames=*/DiagnoseIfArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return (N == 0) || false; }

  static const ParsedAttrInfoDiagnoseIf Instance;
};
const ParsedAttrInfoDiagnoseIf ParsedAttrInfoDiagnoseIf::Instance;
static constexpr ParsedAttrInfo::Spelling DisableTailCallsSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "disable_tail_calls"},
    {AttributeCommonInfo::AS_Bracket, "clang::disable_tail_calls"},
    {AttributeCommonInfo::AS_Bracket, "neverc::disable_tail_calls"},
    {AttributeCommonInfo::AS_C23, "clang::disable_tail_calls"},
    {AttributeCommonInfo::AS_C23, "neverc::disable_tail_calls"},
};
struct ParsedAttrInfoDisableTailCalls final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoDisableTailCalls()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_DisableTailCalls,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/DisableTailCallsSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<NakedAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) DisableTailCallsAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoDisableTailCalls Instance;
};
const ParsedAttrInfoDisableTailCalls ParsedAttrInfoDisableTailCalls::Instance;
static constexpr ParsedAttrInfo::Spelling DisableTryStmtSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "disable_try_stmt"},
    {AttributeCommonInfo::AS_Bracket, "gnu::disable_try_stmt"},
    {AttributeCommonInfo::AS_C23, "gnu::disable_try_stmt"},
    {AttributeCommonInfo::AS_Declspec, "disable_try_stmt"},
};
struct ParsedAttrInfoDisableTryStmt final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoDisableTryStmt()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_DisableTryStmt,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/DisableTryStmtSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoDisableTryStmt Instance;
};
const ParsedAttrInfoDisableTryStmt ParsedAttrInfoDisableTryStmt::Instance;
static constexpr ParsedAttrInfo::Spelling EnableIfSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "enable_if"},
};
static constexpr const char *EnableIfArgNames[] = {
    "Cond",
    "Message",
};
struct ParsedAttrInfoEnableIf final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoEnableIf()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_EnableIf,
            /*NumArgs=*/2,
            /*OptArgs=*/0,
            /*NumArgMembers=*/2,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/EnableIfSpellings,
            /*ArgNames=*/EnableIfArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return (N == 0) || false; }

  static const ParsedAttrInfoEnableIf Instance;
};
const ParsedAttrInfoEnableIf ParsedAttrInfoEnableIf::Instance;
static constexpr ParsedAttrInfo::Spelling EnforceTCBSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "enforce_tcb"},
    {AttributeCommonInfo::AS_Bracket, "clang::enforce_tcb"},
    {AttributeCommonInfo::AS_Bracket, "neverc::enforce_tcb"},
    {AttributeCommonInfo::AS_C23, "clang::enforce_tcb"},
    {AttributeCommonInfo::AS_C23, "neverc::enforce_tcb"},
};
static constexpr const char *EnforceTCBArgNames[] = {
    "TCBName",
};
struct ParsedAttrInfoEnforceTCB final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoEnforceTCB()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_EnforceTCB,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/EnforceTCBSpellings,
            /*ArgNames=*/EnforceTCBArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoEnforceTCB Instance;
};
const ParsedAttrInfoEnforceTCB ParsedAttrInfoEnforceTCB::Instance;
static constexpr ParsedAttrInfo::Spelling EnforceTCBLeafSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "enforce_tcb_leaf"},
    {AttributeCommonInfo::AS_Bracket, "clang::enforce_tcb_leaf"},
    {AttributeCommonInfo::AS_Bracket, "neverc::enforce_tcb_leaf"},
    {AttributeCommonInfo::AS_C23, "clang::enforce_tcb_leaf"},
    {AttributeCommonInfo::AS_C23, "neverc::enforce_tcb_leaf"},
};
static constexpr const char *EnforceTCBLeafArgNames[] = {
    "TCBName",
};
struct ParsedAttrInfoEnforceTCBLeaf final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoEnforceTCBLeaf()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_EnforceTCBLeaf,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/EnforceTCBLeafSpellings,
            /*ArgNames=*/EnforceTCBLeafArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoEnforceTCBLeaf Instance;
};
const ParsedAttrInfoEnforceTCBLeaf ParsedAttrInfoEnforceTCBLeaf::Instance;
static constexpr ParsedAttrInfo::Spelling EnumExtensibilitySpellings[] = {
    {AttributeCommonInfo::AS_GNU, "enum_extensibility"},
    {AttributeCommonInfo::AS_Bracket, "clang::enum_extensibility"},
    {AttributeCommonInfo::AS_Bracket, "neverc::enum_extensibility"},
    {AttributeCommonInfo::AS_C23, "clang::enum_extensibility"},
    {AttributeCommonInfo::AS_C23, "neverc::enum_extensibility"},
};
static constexpr const char *EnumExtensibilityArgNames[] = {
    "Extensibility",
};
struct ParsedAttrInfoEnumExtensibility final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoEnumExtensibility()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_EnumExtensibility,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/EnumExtensibilitySpellings,
            /*ArgNames=*/EnumExtensibilityArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<EnumDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "enums";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_enum, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoEnumExtensibility Instance;
};
const ParsedAttrInfoEnumExtensibility ParsedAttrInfoEnumExtensibility::Instance;
static constexpr ParsedAttrInfo::Spelling ErrorSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "error"},
    {AttributeCommonInfo::AS_Bracket, "gnu::error"},
    {AttributeCommonInfo::AS_C23, "gnu::error"},
    {AttributeCommonInfo::AS_GNU, "warning"},
    {AttributeCommonInfo::AS_Bracket, "gnu::warning"},
    {AttributeCommonInfo::AS_C23, "gnu::warning"},
};
static constexpr const char *ErrorArgNames[] = {
    "UserDiagnostic",
};
struct ParsedAttrInfoError final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoError()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Error,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/ErrorSpellings,
            /*ArgNames=*/ErrorArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const override {
    enum Spelling {
      GNU_error = 0,
      Bracket_gnu_error = 1,
      C23_gnu_error = 2,
      GNU_warning = 3,
      Bracket_gnu_warning = 4,
      C23_gnu_warning = 5,
      SpellingNotCalculated = 15

    };

    unsigned Idx = Attr.getAttributeSpellingListIndex();
    switch (Idx) {
    default:
      llvm_unreachable("Unknown spelling list index");
    case 0:
      return GNU_error;
    case 1:
      return Bracket_gnu_error;
    case 2:
      return C23_gnu_error;
    case 3:
      return GNU_warning;
    case 4:
      return Bracket_gnu_warning;
    case 5:
      return C23_gnu_warning;
    }
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoError Instance;
};
const ParsedAttrInfoError ParsedAttrInfoError::Instance;
static constexpr ParsedAttrInfo::Spelling ExtVectorTypeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "ext_vector_type"},
};
static constexpr const char *ExtVectorTypeArgNames[] = {
    "NumElements",
};
struct ParsedAttrInfoExtVectorType final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoExtVectorType()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ExtVectorType,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ExtVectorTypeSpellings,
            /*ArgNames=*/ExtVectorTypeArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<TypedefNameDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "typedefs";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return (N == 0) || false; }

  static const ParsedAttrInfoExtVectorType Instance;
};
const ParsedAttrInfoExtVectorType ParsedAttrInfoExtVectorType::Instance;
static constexpr ParsedAttrInfo::Spelling FallThroughSpellings[] = {
    {AttributeCommonInfo::AS_Bracket, "fallthrough"},
    {AttributeCommonInfo::AS_C23, "fallthrough"},
    {AttributeCommonInfo::AS_Bracket, "neverc::fallthrough"},
    {AttributeCommonInfo::AS_GNU, "fallthrough"},
    {AttributeCommonInfo::AS_Bracket, "gnu::fallthrough"},
    {AttributeCommonInfo::AS_C23, "gnu::fallthrough"},
};
struct ParsedAttrInfoFallThrough final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoFallThrough()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_FallThrough,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/1,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/FallThroughSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &AL,
                            const Decl *D) const override {
    S.Diag(AL.getLoc(), diag::err_attribute_invalid_on_decl)
        << AL << AL.isRegularKeywordAttribute() << D->getLocation();
    return false;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &Attr,
                            const Stmt *St) const override {
    if (!isa<NullStmt>(St) && !isa<SwitchCase>(St)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "empty statements";
      return false;
    }
    return true;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoFallThrough Instance;
};
const ParsedAttrInfoFallThrough ParsedAttrInfoFallThrough::Instance;
static constexpr ParsedAttrInfo::Spelling FastCallSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "fastcall"},
    {AttributeCommonInfo::AS_Bracket, "gnu::fastcall"},
    {AttributeCommonInfo::AS_C23, "gnu::fastcall"},
    {AttributeCommonInfo::AS_Keyword, "__fastcall"},
    {AttributeCommonInfo::AS_Keyword, "_fastcall"},
};
struct ParsedAttrInfoFastCall final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoFastCall()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_FastCall,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/FastCallSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoFastCall Instance;
};
const ParsedAttrInfoFastCall ParsedAttrInfoFastCall::Instance;
static constexpr ParsedAttrInfo::Spelling FlagEnumSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "flag_enum"},
    {AttributeCommonInfo::AS_Bracket, "clang::flag_enum"},
    {AttributeCommonInfo::AS_Bracket, "neverc::flag_enum"},
    {AttributeCommonInfo::AS_C23, "clang::flag_enum"},
    {AttributeCommonInfo::AS_C23, "neverc::flag_enum"},
};
struct ParsedAttrInfoFlagEnum final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoFlagEnum()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_FlagEnum,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/FlagEnumSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<EnumDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "enums";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_enum, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) FlagEnumAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoFlagEnum Instance;
};
const ParsedAttrInfoFlagEnum ParsedAttrInfoFlagEnum::Instance;
static constexpr ParsedAttrInfo::Spelling FlattenSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "flatten"},
    {AttributeCommonInfo::AS_Bracket, "gnu::flatten"},
    {AttributeCommonInfo::AS_C23, "gnu::flatten"},
};
struct ParsedAttrInfoFlatten final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoFlatten()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Flatten,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/FlattenSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) FlattenAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoFlatten Instance;
};
const ParsedAttrInfoFlatten ParsedAttrInfoFlatten::Instance;
static constexpr ParsedAttrInfo::Spelling FormatSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "format"},
    {AttributeCommonInfo::AS_Bracket, "gnu::format"},
    {AttributeCommonInfo::AS_C23, "gnu::format"},
};
static constexpr const char *FormatArgNames[] = {
    "Type",
    "FormatIdx",
    "FirstArg",
};
struct ParsedAttrInfoFormat final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoFormat()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Format,
            /*NumArgs=*/3,
            /*OptArgs=*/0,
            /*NumArgMembers=*/3,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/FormatSpellings,
            /*ArgNames=*/FormatArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isHasFunctionProto(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "non-K&R-style functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoFormat Instance;
};
const ParsedAttrInfoFormat ParsedAttrInfoFormat::Instance;
static constexpr ParsedAttrInfo::Spelling FormatArgSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "format_arg"},
    {AttributeCommonInfo::AS_Bracket, "gnu::format_arg"},
    {AttributeCommonInfo::AS_C23, "gnu::format_arg"},
};
static constexpr const char *FormatArgArgNames[] = {
    "FormatIdx",
};
struct ParsedAttrInfoFormatArg final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoFormatArg()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_FormatArg,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/FormatArgSpellings,
            /*ArgNames=*/FormatArgArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isHasFunctionProto(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "non-K&R-style functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoFormatArg Instance;
};
const ParsedAttrInfoFormatArg ParsedAttrInfoFormatArg::Instance;
static constexpr ParsedAttrInfo::Spelling FunctionReturnThunksSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "function_return"},
    {AttributeCommonInfo::AS_Bracket, "gnu::function_return"},
    {AttributeCommonInfo::AS_C23, "gnu::function_return"},
};
static constexpr const char *FunctionReturnThunksArgNames[] = {
    "ThunkType",
};
struct ParsedAttrInfoFunctionReturnThunks final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoFunctionReturnThunks()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_FunctionReturnThunks,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/FunctionReturnThunksSpellings,
            /*ArgNames=*/FunctionReturnThunksArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::x86_64);
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoFunctionReturnThunks Instance;
};
const ParsedAttrInfoFunctionReturnThunks
    ParsedAttrInfoFunctionReturnThunks::Instance;
static constexpr ParsedAttrInfo::Spelling GNUInlineSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "gnu_inline"},
    {AttributeCommonInfo::AS_Bracket, "gnu::gnu_inline"},
    {AttributeCommonInfo::AS_C23, "gnu::gnu_inline"},
};
struct ParsedAttrInfoGNUInline final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoGNUInline()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_GNUInline,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/GNUInlineSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoGNUInline Instance;
};
const ParsedAttrInfoGNUInline ParsedAttrInfoGNUInline::Instance;
static constexpr ParsedAttrInfo::Spelling HotSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "hot"},
    {AttributeCommonInfo::AS_Bracket, "gnu::hot"},
    {AttributeCommonInfo::AS_C23, "gnu::hot"},
};
struct ParsedAttrInfoHot final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoHot()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Hot,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/HotSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<ColdAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) HotAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoHot Instance;
};
const ParsedAttrInfoHot ParsedAttrInfoHot::Instance;
static constexpr ParsedAttrInfo::Spelling IFuncSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "ifunc"},
    {AttributeCommonInfo::AS_Bracket, "gnu::ifunc"},
    {AttributeCommonInfo::AS_C23, "gnu::ifunc"},
};
static constexpr const char *IFuncArgNames[] = {
    "Resolver",
};
struct ParsedAttrInfoIFunc final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoIFunc()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_IFunc,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/IFuncSpellings,
            /*ArgNames=*/IFuncArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getObjectFormat() == llvm::Triple::ELF ||
                    T.getObjectFormat() == llvm::Triple::MachO);
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoIFunc Instance;
};
const ParsedAttrInfoIFunc ParsedAttrInfoIFunc::Instance;
static constexpr ParsedAttrInfo::Spelling InternalLinkageSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "internal_linkage"},
    {AttributeCommonInfo::AS_Bracket, "clang::internal_linkage"},
    {AttributeCommonInfo::AS_Bracket, "neverc::internal_linkage"},
    {AttributeCommonInfo::AS_C23, "clang::internal_linkage"},
    {AttributeCommonInfo::AS_C23, "neverc::internal_linkage"},
};
struct ParsedAttrInfoInternalLinkage final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoInternalLinkage()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_InternalLinkage,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/InternalLinkageSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<VarDecl>(D) && !isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "variables and functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<CommonAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoInternalLinkage Instance;
};
const ParsedAttrInfoInternalLinkage ParsedAttrInfoInternalLinkage::Instance;
static constexpr ParsedAttrInfo::Spelling LTOVisibilityPublicSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "lto_visibility_public"},
    {AttributeCommonInfo::AS_Bracket, "clang::lto_visibility_public"},
    {AttributeCommonInfo::AS_Bracket, "neverc::lto_visibility_public"},
    {AttributeCommonInfo::AS_C23, "clang::lto_visibility_public"},
    {AttributeCommonInfo::AS_C23, "neverc::lto_visibility_public"},
};
struct ParsedAttrInfoLTOVisibilityPublic final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoLTOVisibilityPublic()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_LTOVisibilityPublic,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/LTOVisibilityPublicSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<RecordDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "structs and unions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_record, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) LTOVisibilityPublicAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoLTOVisibilityPublic Instance;
};
const ParsedAttrInfoLTOVisibilityPublic
    ParsedAttrInfoLTOVisibilityPublic::Instance;
static constexpr ParsedAttrInfo::Spelling LeafSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "leaf"},
    {AttributeCommonInfo::AS_Bracket, "gnu::leaf"},
    {AttributeCommonInfo::AS_C23, "gnu::leaf"},
};
struct ParsedAttrInfoLeaf final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoLeaf()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Leaf,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/LeafSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) LeafAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoLeaf Instance;
};
const ParsedAttrInfoLeaf ParsedAttrInfoLeaf::Instance;
static constexpr ParsedAttrInfo::Spelling LikelySpellings[] = {
    {AttributeCommonInfo::AS_Bracket, "likely"},
    {AttributeCommonInfo::AS_C23, "neverc::likely"},
};
struct ParsedAttrInfoLikely final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoLikely()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Likely,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/1,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/LikelySpellings,
            /*ArgNames=*/{}) {}
  using ParsedAttrInfo::diagMutualExclusion;

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoLikely Instance;
};
const ParsedAttrInfoLikely ParsedAttrInfoLikely::Instance;
static constexpr ParsedAttrInfo::Spelling LoaderUninitializedSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "loader_uninitialized"},
    {AttributeCommonInfo::AS_Bracket, "clang::loader_uninitialized"},
    {AttributeCommonInfo::AS_Bracket, "neverc::loader_uninitialized"},
    {AttributeCommonInfo::AS_C23, "clang::loader_uninitialized"},
    {AttributeCommonInfo::AS_C23, "neverc::loader_uninitialized"},
};
struct ParsedAttrInfoLoaderUninitialized final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoLoaderUninitialized()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_LoaderUninitialized,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/LoaderUninitializedSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isGlobalVar(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "global variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_global, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) LoaderUninitializedAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoLoaderUninitialized Instance;
};
const ParsedAttrInfoLoaderUninitialized
    ParsedAttrInfoLoaderUninitialized::Instance;
static constexpr ParsedAttrInfo::Spelling MSABISpellings[] = {
    {AttributeCommonInfo::AS_GNU, "ms_abi"},
    {AttributeCommonInfo::AS_Bracket, "gnu::ms_abi"},
    {AttributeCommonInfo::AS_C23, "gnu::ms_abi"},
};
struct ParsedAttrInfoMSABI final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoMSABI()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_MSABI,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/MSABISpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoMSABI Instance;
};
const ParsedAttrInfoMSABI ParsedAttrInfoMSABI::Instance;
static constexpr ParsedAttrInfo::Spelling MSAllocatorSpellings[] = {
    {AttributeCommonInfo::AS_Declspec, "allocator"},
};
struct ParsedAttrInfoMSAllocator final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoMSAllocator()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_MSAllocator,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/MSAllocatorSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoMSAllocator Instance;
};
const ParsedAttrInfoMSAllocator ParsedAttrInfoMSAllocator::Instance;
static constexpr ParsedAttrInfo::Spelling MSStructSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "ms_struct"},
    {AttributeCommonInfo::AS_Bracket, "gnu::ms_struct"},
    {AttributeCommonInfo::AS_C23, "gnu::ms_struct"},
};
struct ParsedAttrInfoMSStruct final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoMSStruct()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_MSStruct,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/MSStructSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<RecordDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "structs and unions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_record, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) MSStructAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoMSStruct Instance;
};
const ParsedAttrInfoMSStruct ParsedAttrInfoMSStruct::Instance;
static constexpr ParsedAttrInfo::Spelling MatrixTypeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "matrix_type"},
    {AttributeCommonInfo::AS_Bracket, "clang::matrix_type"},
    {AttributeCommonInfo::AS_Bracket, "neverc::matrix_type"},
    {AttributeCommonInfo::AS_C23, "clang::matrix_type"},
    {AttributeCommonInfo::AS_C23, "neverc::matrix_type"},
};
static constexpr const char *MatrixTypeArgNames[] = {
    "NumRows",
    "NumColumns",
};
struct ParsedAttrInfoMatrixType final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoMatrixType()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_MatrixType,
            /*NumArgs=*/2,
            /*OptArgs=*/0,
            /*NumArgMembers=*/2,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/MatrixTypeSpellings,
            /*ArgNames=*/MatrixTypeArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<TypedefNameDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "typedefs";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override {
    return (N == 0) || (N == 1) || false;
  }

  static const ParsedAttrInfoMatrixType Instance;
};
const ParsedAttrInfoMatrixType ParsedAttrInfoMatrixType::Instance;
static constexpr ParsedAttrInfo::Spelling MayAliasSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "may_alias"},
    {AttributeCommonInfo::AS_Bracket, "gnu::may_alias"},
    {AttributeCommonInfo::AS_C23, "gnu::may_alias"},
};
struct ParsedAttrInfoMayAlias final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoMayAlias()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_MayAlias,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/MayAliasSpellings,
            /*ArgNames=*/{}) {}
  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) MayAliasAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoMayAlias Instance;
};
const ParsedAttrInfoMayAlias ParsedAttrInfoMayAlias::Instance;
static constexpr ParsedAttrInfo::Spelling MaybeUndefSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "maybe_undef"},
    {AttributeCommonInfo::AS_Bracket, "clang::maybe_undef"},
    {AttributeCommonInfo::AS_Bracket, "neverc::maybe_undef"},
    {AttributeCommonInfo::AS_C23, "clang::maybe_undef"},
    {AttributeCommonInfo::AS_C23, "neverc::maybe_undef"},
};
struct ParsedAttrInfoMaybeUndef final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoMaybeUndef()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_MaybeUndef,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/MaybeUndefSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<ParmVarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "parameters";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_parameter, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) MaybeUndefAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoMaybeUndef Instance;
};
const ParsedAttrInfoMaybeUndef ParsedAttrInfoMaybeUndef::Instance;
static constexpr ParsedAttrInfo::Spelling MinSizeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "minsize"},
    {AttributeCommonInfo::AS_Bracket, "clang::minsize"},
    {AttributeCommonInfo::AS_Bracket, "neverc::minsize"},
    {AttributeCommonInfo::AS_C23, "clang::minsize"},
    {AttributeCommonInfo::AS_C23, "neverc::minsize"},
};
struct ParsedAttrInfoMinSize final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoMinSize()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_MinSize,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/MinSizeSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoMinSize Instance;
};
const ParsedAttrInfoMinSize ParsedAttrInfoMinSize::Instance;
static constexpr ParsedAttrInfo::Spelling MinVectorWidthSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "min_vector_width"},
    {AttributeCommonInfo::AS_Bracket, "clang::min_vector_width"},
    {AttributeCommonInfo::AS_Bracket, "neverc::min_vector_width"},
    {AttributeCommonInfo::AS_C23, "clang::min_vector_width"},
    {AttributeCommonInfo::AS_C23, "neverc::min_vector_width"},
};
static constexpr const char *MinVectorWidthArgNames[] = {
    "VectorWidth",
};
struct ParsedAttrInfoMinVectorWidth final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoMinVectorWidth()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_MinVectorWidth,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/MinVectorWidthSpellings,
            /*ArgNames=*/MinVectorWidthArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoMinVectorWidth Instance;
};
const ParsedAttrInfoMinVectorWidth ParsedAttrInfoMinVectorWidth::Instance;
static constexpr ParsedAttrInfo::Spelling ModeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "mode"},
    {AttributeCommonInfo::AS_Bracket, "gnu::mode"},
    {AttributeCommonInfo::AS_C23, "gnu::mode"},
};
static constexpr const char *ModeArgNames[] = {
    "Mode",
};
struct ParsedAttrInfoMode final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoMode()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Mode,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ModeSpellings,
            /*ArgNames=*/ModeArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<VarDecl>(D) && !isa<EnumDecl>(D) && !isa<TypedefNameDecl>(D) &&
        !isa<FieldDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "variables, enums, typedefs, and fields";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoMode Instance;
};
const ParsedAttrInfoMode ParsedAttrInfoMode::Instance;
static constexpr ParsedAttrInfo::Spelling MustTailSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "musttail"},
    {AttributeCommonInfo::AS_Bracket, "clang::musttail"},
    {AttributeCommonInfo::AS_Bracket, "neverc::musttail"},
    {AttributeCommonInfo::AS_C23, "clang::musttail"},
    {AttributeCommonInfo::AS_C23, "neverc::musttail"},
};
struct ParsedAttrInfoMustTail final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoMustTail()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_MustTail,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/1,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/MustTailSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &AL,
                            const Decl *D) const override {
    S.Diag(AL.getLoc(), diag::err_attribute_invalid_on_decl)
        << AL << AL.isRegularKeywordAttribute() << D->getLocation();
    return false;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &Attr,
                            const Stmt *St) const override {
    if (!isa<ReturnStmt>(St)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "return statements";
      return false;
    }
    return true;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoMustTail Instance;
};
const ParsedAttrInfoMustTail ParsedAttrInfoMustTail::Instance;
static constexpr ParsedAttrInfo::Spelling NakedSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "naked"},
    {AttributeCommonInfo::AS_Bracket, "gnu::naked"},
    {AttributeCommonInfo::AS_C23, "gnu::naked"},
    {AttributeCommonInfo::AS_Declspec, "naked"},
};
struct ParsedAttrInfoNaked final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNaked()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Naked,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NakedSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<DisableTailCallsAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNaked Instance;
};
const ParsedAttrInfoNaked ParsedAttrInfoNaked::Instance;
static constexpr ParsedAttrInfo::Spelling NeonPolyVectorTypeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "neon_polyvector_type"},
    {AttributeCommonInfo::AS_Bracket, "clang::neon_polyvector_type"},
    {AttributeCommonInfo::AS_Bracket, "neverc::neon_polyvector_type"},
    {AttributeCommonInfo::AS_C23, "clang::neon_polyvector_type"},
    {AttributeCommonInfo::AS_C23, "neverc::neon_polyvector_type"},
};
static constexpr const char *NeonPolyVectorTypeArgNames[] = {
    "NumElements",
};
struct ParsedAttrInfoNeonPolyVectorType final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNeonPolyVectorType()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NeonPolyVectorType,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/NeonPolyVectorTypeSpellings,
            /*ArgNames=*/NeonPolyVectorTypeArgNames) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNeonPolyVectorType Instance;
};
const ParsedAttrInfoNeonPolyVectorType
    ParsedAttrInfoNeonPolyVectorType::Instance;
static constexpr ParsedAttrInfo::Spelling NeonVectorTypeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "neon_vector_type"},
    {AttributeCommonInfo::AS_Bracket, "clang::neon_vector_type"},
    {AttributeCommonInfo::AS_Bracket, "neverc::neon_vector_type"},
    {AttributeCommonInfo::AS_C23, "clang::neon_vector_type"},
    {AttributeCommonInfo::AS_C23, "neverc::neon_vector_type"},
};
static constexpr const char *NeonVectorTypeArgNames[] = {
    "NumElements",
};
struct ParsedAttrInfoNeonVectorType final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNeonVectorType()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NeonVectorType,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/NeonVectorTypeSpellings,
            /*ArgNames=*/NeonVectorTypeArgNames) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNeonVectorType Instance;
};
const ParsedAttrInfoNeonVectorType ParsedAttrInfoNeonVectorType::Instance;
static constexpr ParsedAttrInfo::Spelling NoAliasSpellings[] = {
    {AttributeCommonInfo::AS_Declspec, "noalias"},
};
struct ParsedAttrInfoNoAlias final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoAlias()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoAlias,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/NoAliasSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) NoAliasAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoAlias Instance;
};
const ParsedAttrInfoNoAlias ParsedAttrInfoNoAlias::Instance;
static constexpr ParsedAttrInfo::Spelling NoBuiltinSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "no_builtin"},
    {AttributeCommonInfo::AS_Bracket, "clang::no_builtin"},
    {AttributeCommonInfo::AS_Bracket, "neverc::no_builtin"},
    {AttributeCommonInfo::AS_C23, "clang::no_builtin"},
    {AttributeCommonInfo::AS_C23, "neverc::no_builtin"},
};
static constexpr const char *NoBuiltinArgNames[] = {
    "BuiltinNames...",
};
struct ParsedAttrInfoNoBuiltin final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoBuiltin()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoBuiltin,
            /*NumArgs=*/0,
            /*OptArgs=*/15,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoBuiltinSpellings,
            /*ArgNames=*/NoBuiltinArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoBuiltin Instance;
};
const ParsedAttrInfoNoBuiltin ParsedAttrInfoNoBuiltin::Instance;
static constexpr ParsedAttrInfo::Spelling NoCommonSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "nocommon"},
    {AttributeCommonInfo::AS_Bracket, "gnu::nocommon"},
    {AttributeCommonInfo::AS_C23, "gnu::nocommon"},
};
struct ParsedAttrInfoNoCommon final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoCommon()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoCommon,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoCommonSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<VarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) NoCommonAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoCommon Instance;
};
const ParsedAttrInfoNoCommon ParsedAttrInfoNoCommon::Instance;
static constexpr ParsedAttrInfo::Spelling NoDebugSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "nodebug"},
    {AttributeCommonInfo::AS_Bracket, "gnu::nodebug"},
    {AttributeCommonInfo::AS_C23, "gnu::nodebug"},
};
struct ParsedAttrInfoNoDebug final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoDebug()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoDebug,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoDebugSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<TypedefNameDecl>(D) && !isFunctionLike(D) && !isNonParmVar(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "typedefs, functions, function pointers, and variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(attr::SubjectMatchRule_type_alias,
                                        /*IsSupported=*/true));
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_hasType_functionType, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable_not_is_parameter,
                       /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoDebug Instance;
};
const ParsedAttrInfoNoDebug ParsedAttrInfoNoDebug::Instance;
static constexpr ParsedAttrInfo::Spelling NoDerefSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "noderef"},
    {AttributeCommonInfo::AS_Bracket, "clang::noderef"},
    {AttributeCommonInfo::AS_Bracket, "neverc::noderef"},
    {AttributeCommonInfo::AS_C23, "clang::noderef"},
    {AttributeCommonInfo::AS_C23, "neverc::noderef"},
};
struct ParsedAttrInfoNoDeref final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoDeref()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoDeref,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/NoDerefSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoDeref Instance;
};
const ParsedAttrInfoNoDeref ParsedAttrInfoNoDeref::Instance;
static constexpr ParsedAttrInfo::Spelling NoDestroySpellings[] = {
    {AttributeCommonInfo::AS_GNU, "no_destroy"},
    {AttributeCommonInfo::AS_Bracket, "clang::no_destroy"},
    {AttributeCommonInfo::AS_Bracket, "neverc::no_destroy"},
};
struct ParsedAttrInfoNoDestroy final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoDestroy()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoDestroy,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoDestroySpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<VarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<AlwaysDestroyAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoDestroy Instance;
};
const ParsedAttrInfoNoDestroy ParsedAttrInfoNoDestroy::Instance;
static constexpr ParsedAttrInfo::Spelling NoDuplicateSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "noduplicate"},
    {AttributeCommonInfo::AS_Bracket, "clang::noduplicate"},
    {AttributeCommonInfo::AS_Bracket, "neverc::noduplicate"},
    {AttributeCommonInfo::AS_C23, "clang::noduplicate"},
    {AttributeCommonInfo::AS_C23, "neverc::noduplicate"},
};
struct ParsedAttrInfoNoDuplicate final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoDuplicate()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoDuplicate,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoDuplicateSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) NoDuplicateAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoDuplicate Instance;
};
const ParsedAttrInfoNoDuplicate ParsedAttrInfoNoDuplicate::Instance;
static constexpr ParsedAttrInfo::Spelling NoEscapeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "noescape"},
    {AttributeCommonInfo::AS_Bracket, "clang::noescape"},
    {AttributeCommonInfo::AS_Bracket, "neverc::noescape"},
    {AttributeCommonInfo::AS_C23, "clang::noescape"},
    {AttributeCommonInfo::AS_C23, "neverc::noescape"},
};
struct ParsedAttrInfoNoEscape final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoEscape()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoEscape,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoEscapeSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<ParmVarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "parameters";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_parameter, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoEscape Instance;
};
const ParsedAttrInfoNoEscape ParsedAttrInfoNoEscape::Instance;
static constexpr ParsedAttrInfo::Spelling NoInlineSpellings[] = {
    {AttributeCommonInfo::AS_Keyword, "__noinline__"},
    {AttributeCommonInfo::AS_GNU, "noinline"},
    {AttributeCommonInfo::AS_Bracket, "gnu::noinline"},
    {AttributeCommonInfo::AS_C23, "gnu::noinline"},
    {AttributeCommonInfo::AS_Bracket, "neverc::noinline"},
    {AttributeCommonInfo::AS_C23, "neverc::noinline"},
    {AttributeCommonInfo::AS_Declspec, "noinline"},
};
struct ParsedAttrInfoNoInline final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoInline()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoInline,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/1,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoInlineSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions and statements";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &Attr,
                            const Stmt *St) const override {
    if (!isa<Stmt>(St)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions and statements";
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) NoInlineAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoInline Instance;
};
const ParsedAttrInfoNoInline ParsedAttrInfoNoInline::Instance;
static constexpr ParsedAttrInfo::Spelling NoMergeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "nomerge"},
    {AttributeCommonInfo::AS_Bracket, "clang::nomerge"},
    {AttributeCommonInfo::AS_Bracket, "neverc::nomerge"},
    {AttributeCommonInfo::AS_C23, "clang::nomerge"},
    {AttributeCommonInfo::AS_C23, "neverc::nomerge"},
};
struct ParsedAttrInfoNoMerge final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoMerge()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoMerge,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/1,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoMergeSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D) && !isa<VarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions, statements and variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &Attr,
                            const Stmt *St) const override {
    if (!isa<Stmt>(St)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions, statements and variables";
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoMerge Instance;
};
const ParsedAttrInfoNoMerge ParsedAttrInfoNoMerge::Instance;
static constexpr ParsedAttrInfo::Spelling NoRandomizeLayoutSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "no_randomize_layout"},
    {AttributeCommonInfo::AS_Bracket, "gnu::no_randomize_layout"},
    {AttributeCommonInfo::AS_C23, "gnu::no_randomize_layout"},
};
struct ParsedAttrInfoNoRandomizeLayout final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoRandomizeLayout()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoRandomizeLayout,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoRandomizeLayoutSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<RecordDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "structs and unions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<RandomizeLayoutAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_record, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoRandomizeLayout Instance;
};
const ParsedAttrInfoNoRandomizeLayout ParsedAttrInfoNoRandomizeLayout::Instance;
static constexpr ParsedAttrInfo::Spelling NoReturnSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "noreturn"},
    {AttributeCommonInfo::AS_Bracket, "gnu::noreturn"},
    {AttributeCommonInfo::AS_C23, "gnu::noreturn"},
    {AttributeCommonInfo::AS_Declspec, "noreturn"},
};
struct ParsedAttrInfoNoReturn final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoReturn()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoReturn,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/NoReturnSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoReturn Instance;
};
const ParsedAttrInfoNoReturn ParsedAttrInfoNoReturn::Instance;
static constexpr ParsedAttrInfo::Spelling
    NoSpeculativeLoadHardeningSpellings[] = {
        {AttributeCommonInfo::AS_GNU, "no_speculative_load_hardening"},
        {AttributeCommonInfo::AS_Bracket,
         "clang::no_speculative_load_hardening"},
        {AttributeCommonInfo::AS_Bracket,
         "neverc::no_speculative_load_hardening"},
        {AttributeCommonInfo::AS_C23, "clang::no_speculative_load_hardening"},
        {AttributeCommonInfo::AS_C23, "neverc::no_speculative_load_hardening"},
};
struct ParsedAttrInfoNoSpeculativeLoadHardening final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoSpeculativeLoadHardening()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoSpeculativeLoadHardening,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoSpeculativeLoadHardeningSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<SpeculativeLoadHardeningAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context)
                   NoSpeculativeLoadHardeningAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoSpeculativeLoadHardening Instance;
};
const ParsedAttrInfoNoSpeculativeLoadHardening
    ParsedAttrInfoNoSpeculativeLoadHardening::Instance;
static constexpr ParsedAttrInfo::Spelling NoSplitStackSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "no_split_stack"},
    {AttributeCommonInfo::AS_Bracket, "gnu::no_split_stack"},
    {AttributeCommonInfo::AS_C23, "gnu::no_split_stack"},
};
struct ParsedAttrInfoNoSplitStack final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoSplitStack()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoSplitStack,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoSplitStackSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) NoSplitStackAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoSplitStack Instance;
};
const ParsedAttrInfoNoSplitStack ParsedAttrInfoNoSplitStack::Instance;
static constexpr ParsedAttrInfo::Spelling NoStackProtectorSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "no_stack_protector"},
    {AttributeCommonInfo::AS_Bracket, "clang::no_stack_protector"},
    {AttributeCommonInfo::AS_Bracket, "neverc::no_stack_protector"},
    {AttributeCommonInfo::AS_C23, "clang::no_stack_protector"},
    {AttributeCommonInfo::AS_C23, "neverc::no_stack_protector"},
    {AttributeCommonInfo::AS_Bracket, "gnu::no_stack_protector"},
    {AttributeCommonInfo::AS_C23, "gnu::no_stack_protector"},
    {AttributeCommonInfo::AS_Declspec, "safebuffers"},
};
struct ParsedAttrInfoNoStackProtector final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoStackProtector()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoStackProtector,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoStackProtectorSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const override {
    enum Spelling {
      GNU_no_stack_protector = 0,
      Bracket_clang_no_stack_protector = 1,
      Bracket_neverc_no_stack_protector = 2,
      C23_clang_no_stack_protector = 3,
      C23_neverc_no_stack_protector = 4,
      Bracket_gnu_no_stack_protector = 5,
      C23_gnu_no_stack_protector = 6,
      Declspec_safebuffers = 7,
      SpellingNotCalculated = 15

    };

    unsigned Idx = Attr.getAttributeSpellingListIndex();
    switch (Idx) {
    default:
      llvm_unreachable("Unknown spelling list index");
    case 0:
      return GNU_no_stack_protector;
    case 1:
      return Bracket_clang_no_stack_protector;
    case 2:
      return Bracket_neverc_no_stack_protector;
    case 3:
      return C23_clang_no_stack_protector;
    case 4:
      return C23_neverc_no_stack_protector;
    case 5:
      return Bracket_gnu_no_stack_protector;
    case 6:
      return C23_gnu_no_stack_protector;
    case 7:
      return Declspec_safebuffers;
    }
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) NoStackProtectorAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoStackProtector Instance;
};
const ParsedAttrInfoNoStackProtector ParsedAttrInfoNoStackProtector::Instance;
static constexpr ParsedAttrInfo::Spelling NoThrowSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "nothrow"},
    {AttributeCommonInfo::AS_Bracket, "gnu::nothrow"},
    {AttributeCommonInfo::AS_C23, "gnu::nothrow"},
    {AttributeCommonInfo::AS_Declspec, "nothrow"},
};
struct ParsedAttrInfoNoThrow final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoThrow()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoThrow,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoThrowSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isFunctionLike(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions and function pointers";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_hasType_functionType, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoThrow Instance;
};
const ParsedAttrInfoNoThrow ParsedAttrInfoNoThrow::Instance;
static constexpr ParsedAttrInfo::Spelling NoUwtableSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "nouwtable"},
    {AttributeCommonInfo::AS_Bracket, "clang::nouwtable"},
    {AttributeCommonInfo::AS_Bracket, "neverc::nouwtable"},
    {AttributeCommonInfo::AS_C23, "clang::nouwtable"},
    {AttributeCommonInfo::AS_C23, "neverc::nouwtable"},
};
struct ParsedAttrInfoNoUwtable final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNoUwtable()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NoUwtable,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NoUwtableSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isFunctionLike(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions and function pointers";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_hasType_functionType, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) NoUwtableAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNoUwtable Instance;
};
const ParsedAttrInfoNoUwtable ParsedAttrInfoNoUwtable::Instance;
static constexpr ParsedAttrInfo::Spelling NonNullSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "nonnull"},
    {AttributeCommonInfo::AS_Bracket, "gnu::nonnull"},
    {AttributeCommonInfo::AS_C23, "gnu::nonnull"},
};
static constexpr const char *NonNullArgNames[] = {
    "Args...",
};
struct ParsedAttrInfoNonNull final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNonNull()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NonNull,
            /*NumArgs=*/0,
            /*OptArgs=*/15,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/NonNullSpellings,
            /*ArgNames=*/NonNullArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isHasFunctionProto(D) && !isa<ParmVarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions, methods, and parameters";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNonNull Instance;
};
const ParsedAttrInfoNonNull ParsedAttrInfoNonNull::Instance;
static constexpr ParsedAttrInfo::Spelling NotTailCalledSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "not_tail_called"},
    {AttributeCommonInfo::AS_Bracket, "clang::not_tail_called"},
    {AttributeCommonInfo::AS_Bracket, "neverc::not_tail_called"},
    {AttributeCommonInfo::AS_C23, "clang::not_tail_called"},
    {AttributeCommonInfo::AS_C23, "neverc::not_tail_called"},
};
struct ParsedAttrInfoNotTailCalled final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoNotTailCalled()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_NotTailCalled,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/NotTailCalledSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<AlwaysInlineAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) NotTailCalledAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoNotTailCalled Instance;
};
const ParsedAttrInfoNotTailCalled ParsedAttrInfoNotTailCalled::Instance;
static constexpr ParsedAttrInfo::Spelling OptimizeNoneSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "optnone"},
    {AttributeCommonInfo::AS_Bracket, "clang::optnone"},
    {AttributeCommonInfo::AS_Bracket, "neverc::optnone"},
    {AttributeCommonInfo::AS_C23, "clang::optnone"},
    {AttributeCommonInfo::AS_C23, "neverc::optnone"},
};
struct ParsedAttrInfoOptimizeNone final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoOptimizeNone()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_OptimizeNone,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/OptimizeNoneSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoOptimizeNone Instance;
};
const ParsedAttrInfoOptimizeNone ParsedAttrInfoOptimizeNone::Instance;
static constexpr ParsedAttrInfo::Spelling OverloadableSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "overloadable"},
    {AttributeCommonInfo::AS_Bracket, "clang::overloadable"},
    {AttributeCommonInfo::AS_Bracket, "neverc::overloadable"},
    {AttributeCommonInfo::AS_C23, "clang::overloadable"},
    {AttributeCommonInfo::AS_C23, "neverc::overloadable"},
};
struct ParsedAttrInfoOverloadable final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoOverloadable()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Overloadable,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/OverloadableSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) OverloadableAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoOverloadable Instance;
};
const ParsedAttrInfoOverloadable ParsedAttrInfoOverloadable::Instance;
static constexpr ParsedAttrInfo::Spelling OverrideSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "override"},
    {AttributeCommonInfo::AS_Bracket, "gnu::override"},
    {AttributeCommonInfo::AS_C23, "gnu::override"},
    {AttributeCommonInfo::AS_Declspec, "override"},
};
struct ParsedAttrInfoOverride final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoOverride()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Override,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/OverrideSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D) && !isGlobalVar(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions and global variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_global, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) OverrideAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoOverride Instance;
};
const ParsedAttrInfoOverride ParsedAttrInfoOverride::Instance;
static constexpr ParsedAttrInfo::Spelling PackedSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "packed"},
    {AttributeCommonInfo::AS_Bracket, "gnu::packed"},
    {AttributeCommonInfo::AS_C23, "gnu::packed"},
};
struct ParsedAttrInfoPacked final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPacked()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Packed,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/PackedSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPacked Instance;
};
const ParsedAttrInfoPacked ParsedAttrInfoPacked::Instance;
static constexpr ParsedAttrInfo::Spelling PassObjectSizeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "pass_object_size"},
    {AttributeCommonInfo::AS_Bracket, "clang::pass_object_size"},
    {AttributeCommonInfo::AS_Bracket, "neverc::pass_object_size"},
    {AttributeCommonInfo::AS_C23, "clang::pass_object_size"},
    {AttributeCommonInfo::AS_C23, "neverc::pass_object_size"},
    {AttributeCommonInfo::AS_GNU, "pass_dynamic_object_size"},
    {AttributeCommonInfo::AS_Bracket, "clang::pass_dynamic_object_size"},
    {AttributeCommonInfo::AS_Bracket, "neverc::pass_dynamic_object_size"},
    {AttributeCommonInfo::AS_C23, "clang::pass_dynamic_object_size"},
    {AttributeCommonInfo::AS_C23, "neverc::pass_dynamic_object_size"},
};
static constexpr const char *PassObjectSizeArgNames[] = {
    "Type",
};
struct ParsedAttrInfoPassObjectSize final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPassObjectSize()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_PassObjectSize,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/PassObjectSizeSpellings,
            /*ArgNames=*/PassObjectSizeArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<ParmVarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "parameters";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const override {
    enum Spelling {
      GNU_pass_object_size = 0,
      Bracket_clang_pass_object_size = 1,
      Bracket_neverc_pass_object_size = 2,
      C23_clang_pass_object_size = 3,
      C23_neverc_pass_object_size = 4,
      GNU_pass_dynamic_object_size = 5,
      Bracket_clang_pass_dynamic_object_size = 6,
      Bracket_neverc_pass_dynamic_object_size = 7,
      C23_clang_pass_dynamic_object_size = 8,
      C23_neverc_pass_dynamic_object_size = 9,
      SpellingNotCalculated = 15

    };

    unsigned Idx = Attr.getAttributeSpellingListIndex();
    switch (Idx) {
    default:
      llvm_unreachable("Unknown spelling list index");
    case 0:
      return GNU_pass_object_size;
    case 1:
      return Bracket_clang_pass_object_size;
    case 2:
      return Bracket_neverc_pass_object_size;
    case 3:
      return C23_clang_pass_object_size;
    case 4:
      return C23_neverc_pass_object_size;
    case 5:
      return GNU_pass_dynamic_object_size;
    case 6:
      return Bracket_clang_pass_dynamic_object_size;
    case 7:
      return Bracket_neverc_pass_dynamic_object_size;
    case 8:
      return C23_clang_pass_dynamic_object_size;
    case 9:
      return C23_neverc_pass_dynamic_object_size;
    }
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_parameter, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPassObjectSize Instance;
};
const ParsedAttrInfoPassObjectSize ParsedAttrInfoPassObjectSize::Instance;
static constexpr ParsedAttrInfo::Spelling PatchableFunctionEntrySpellings[] = {
    {AttributeCommonInfo::AS_GNU, "patchable_function_entry"},
    {AttributeCommonInfo::AS_Bracket, "gnu::patchable_function_entry"},
    {AttributeCommonInfo::AS_C23, "gnu::patchable_function_entry"},
};
static constexpr const char *PatchableFunctionEntryArgNames[] = {
    "Count",
    "Offset",
};
struct ParsedAttrInfoPatchableFunctionEntry final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPatchableFunctionEntry()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_PatchableFunctionEntry,
            /*NumArgs=*/1,
            /*OptArgs=*/1,
            /*NumArgMembers=*/2,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/PatchableFunctionEntrySpellings,
            /*ArgNames=*/PatchableFunctionEntryArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::aarch64 ||
                    T.getArch() == llvm::Triple::x86 ||
                    T.getArch() == llvm::Triple::x86_64);
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPatchableFunctionEntry Instance;
};
const ParsedAttrInfoPatchableFunctionEntry
    ParsedAttrInfoPatchableFunctionEntry::Instance;
static constexpr const char *PragmaNeverCBSSSectionArgNames[] = {
    "Name",
};
struct ParsedAttrInfoPragmaNeverCBSSSection final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPragmaNeverCBSSSection()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_PragmaNeverCBSSSection,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/{},
            /*ArgNames=*/PragmaNeverCBSSSectionArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isGlobalVar(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "global variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPragmaNeverCBSSSection Instance;
};
const ParsedAttrInfoPragmaNeverCBSSSection
    ParsedAttrInfoPragmaNeverCBSSSection::Instance;
static constexpr const char *PragmaNeverCDataSectionArgNames[] = {
    "Name",
};
struct ParsedAttrInfoPragmaNeverCDataSection final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPragmaNeverCDataSection()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_PragmaNeverCDataSection,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/{},
            /*ArgNames=*/PragmaNeverCDataSectionArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isGlobalVar(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "global variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPragmaNeverCDataSection Instance;
};
const ParsedAttrInfoPragmaNeverCDataSection
    ParsedAttrInfoPragmaNeverCDataSection::Instance;
static constexpr const char *PragmaNeverCRelroSectionArgNames[] = {
    "Name",
};
struct ParsedAttrInfoPragmaNeverCRelroSection final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPragmaNeverCRelroSection()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_PragmaNeverCRelroSection,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/{},
            /*ArgNames=*/PragmaNeverCRelroSectionArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isGlobalVar(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "global variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPragmaNeverCRelroSection Instance;
};
const ParsedAttrInfoPragmaNeverCRelroSection
    ParsedAttrInfoPragmaNeverCRelroSection::Instance;
static constexpr const char *PragmaNeverCRodataSectionArgNames[] = {
    "Name",
};
struct ParsedAttrInfoPragmaNeverCRodataSection final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPragmaNeverCRodataSection()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_PragmaNeverCRodataSection,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/{},
            /*ArgNames=*/PragmaNeverCRodataSectionArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isGlobalVar(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "global variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPragmaNeverCRodataSection Instance;
};
const ParsedAttrInfoPragmaNeverCRodataSection
    ParsedAttrInfoPragmaNeverCRodataSection::Instance;
static constexpr const char *PragmaNeverCTextSectionArgNames[] = {
    "Name",
};
struct ParsedAttrInfoPragmaNeverCTextSection final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPragmaNeverCTextSection()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_PragmaNeverCTextSection,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/{},
            /*ArgNames=*/PragmaNeverCTextSectionArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPragmaNeverCTextSection Instance;
};
const ParsedAttrInfoPragmaNeverCTextSection
    ParsedAttrInfoPragmaNeverCTextSection::Instance;
static constexpr ParsedAttrInfo::Spelling PreferredTypeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "preferred_type"},
    {AttributeCommonInfo::AS_Bracket, "clang::preferred_type"},
    {AttributeCommonInfo::AS_Bracket, "neverc::preferred_type"},
    {AttributeCommonInfo::AS_C23, "clang::preferred_type"},
    {AttributeCommonInfo::AS_C23, "neverc::preferred_type"},
};
static constexpr const char *PreferredTypeArgNames[] = {
    "Type",
};
struct ParsedAttrInfoPreferredType final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPreferredType()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_PreferredType,
            /*NumArgs=*/0,
            /*OptArgs=*/1,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/PreferredTypeSpellings,
            /*ArgNames=*/PreferredTypeArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isBitField(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "bit-field data members";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPreferredType Instance;
};
const ParsedAttrInfoPreferredType ParsedAttrInfoPreferredType::Instance;
static constexpr ParsedAttrInfo::Spelling PreserveAllSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "preserve_all"},
    {AttributeCommonInfo::AS_Bracket, "clang::preserve_all"},
    {AttributeCommonInfo::AS_Bracket, "neverc::preserve_all"},
    {AttributeCommonInfo::AS_C23, "clang::preserve_all"},
    {AttributeCommonInfo::AS_C23, "neverc::preserve_all"},
};
struct ParsedAttrInfoPreserveAll final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPreserveAll()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_PreserveAll,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/PreserveAllSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPreserveAll Instance;
};
const ParsedAttrInfoPreserveAll ParsedAttrInfoPreserveAll::Instance;
static constexpr ParsedAttrInfo::Spelling PreserveMostSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "preserve_most"},
    {AttributeCommonInfo::AS_Bracket, "clang::preserve_most"},
    {AttributeCommonInfo::AS_Bracket, "neverc::preserve_most"},
    {AttributeCommonInfo::AS_C23, "clang::preserve_most"},
    {AttributeCommonInfo::AS_C23, "neverc::preserve_most"},
};
struct ParsedAttrInfoPreserveMost final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPreserveMost()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_PreserveMost,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/PreserveMostSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPreserveMost Instance;
};
const ParsedAttrInfoPreserveMost ParsedAttrInfoPreserveMost::Instance;
static constexpr ParsedAttrInfo::Spelling Ptr32Spellings[] = {
    {AttributeCommonInfo::AS_Keyword, "__ptr32"},
};
struct ParsedAttrInfoPtr32 final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPtr32()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Ptr32,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/Ptr32Spellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPtr32 Instance;
};
const ParsedAttrInfoPtr32 ParsedAttrInfoPtr32::Instance;
static constexpr ParsedAttrInfo::Spelling Ptr64Spellings[] = {
    {AttributeCommonInfo::AS_Keyword, "__ptr64"},
};
struct ParsedAttrInfoPtr64 final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPtr64()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Ptr64,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/Ptr64Spellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPtr64 Instance;
};
const ParsedAttrInfoPtr64 ParsedAttrInfoPtr64::Instance;
static constexpr ParsedAttrInfo::Spelling PureSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "pure"},
    {AttributeCommonInfo::AS_Bracket, "gnu::pure"},
    {AttributeCommonInfo::AS_C23, "gnu::pure"},
};
struct ParsedAttrInfoPure final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoPure()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Pure,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/PureSpellings,
            /*ArgNames=*/{}) {}
  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) PureAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoPure Instance;
};
const ParsedAttrInfoPure ParsedAttrInfoPure::Instance;
static constexpr ParsedAttrInfo::Spelling RandomizeLayoutSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "randomize_layout"},
    {AttributeCommonInfo::AS_Bracket, "gnu::randomize_layout"},
    {AttributeCommonInfo::AS_C23, "gnu::randomize_layout"},
};
struct ParsedAttrInfoRandomizeLayout final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoRandomizeLayout()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_RandomizeLayout,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/RandomizeLayoutSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<RecordDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "structs and unions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<NoRandomizeLayoutAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_record, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoRandomizeLayout Instance;
};
const ParsedAttrInfoRandomizeLayout ParsedAttrInfoRandomizeLayout::Instance;
static constexpr ParsedAttrInfo::Spelling ReadOnlyPlacementSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "enforce_read_only_placement"},
    {AttributeCommonInfo::AS_Bracket, "clang::enforce_read_only_placement"},
    {AttributeCommonInfo::AS_Bracket, "neverc::enforce_read_only_placement"},
    {AttributeCommonInfo::AS_C23, "clang::enforce_read_only_placement"},
    {AttributeCommonInfo::AS_C23, "neverc::enforce_read_only_placement"},
};
struct ParsedAttrInfoReadOnlyPlacement final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoReadOnlyPlacement()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ReadOnlyPlacement,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/ReadOnlyPlacementSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<RecordDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "structs and unions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_record, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoReadOnlyPlacement Instance;
};
const ParsedAttrInfoReadOnlyPlacement ParsedAttrInfoReadOnlyPlacement::Instance;
static constexpr ParsedAttrInfo::Spelling RegCallSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "regcall"},
    {AttributeCommonInfo::AS_Bracket, "gnu::regcall"},
    {AttributeCommonInfo::AS_C23, "gnu::regcall"},
    {AttributeCommonInfo::AS_Keyword, "__regcall"},
};
struct ParsedAttrInfoRegCall final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoRegCall()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_RegCall,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/RegCallSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoRegCall Instance;
};
const ParsedAttrInfoRegCall ParsedAttrInfoRegCall::Instance;
static constexpr ParsedAttrInfo::Spelling RegparmSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "regparm"},
    {AttributeCommonInfo::AS_Bracket, "gnu::regparm"},
    {AttributeCommonInfo::AS_C23, "gnu::regparm"},
};
static constexpr const char *RegparmArgNames[] = {
    "NumParams",
};
struct ParsedAttrInfoRegparm final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoRegparm()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Regparm,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/RegparmSpellings,
            /*ArgNames=*/RegparmArgNames) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoRegparm Instance;
};
const ParsedAttrInfoRegparm ParsedAttrInfoRegparm::Instance;
static constexpr ParsedAttrInfo::Spelling ReleaseHandleSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "release_handle"},
    {AttributeCommonInfo::AS_Bracket, "clang::release_handle"},
    {AttributeCommonInfo::AS_Bracket, "neverc::release_handle"},
    {AttributeCommonInfo::AS_C23, "clang::release_handle"},
    {AttributeCommonInfo::AS_C23, "neverc::release_handle"},
};
static constexpr const char *ReleaseHandleArgNames[] = {
    "HandleType",
};
struct ParsedAttrInfoReleaseHandle final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoReleaseHandle()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ReleaseHandle,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/ReleaseHandleSpellings,
            /*ArgNames=*/ReleaseHandleArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<ParmVarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "parameters";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_parameter, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoReleaseHandle Instance;
};
const ParsedAttrInfoReleaseHandle ParsedAttrInfoReleaseHandle::Instance;
static constexpr ParsedAttrInfo::Spelling RestrictSpellings[] = {
    {AttributeCommonInfo::AS_Declspec, "restrict"},
    {AttributeCommonInfo::AS_GNU, "malloc"},
    {AttributeCommonInfo::AS_Bracket, "gnu::malloc"},
    {AttributeCommonInfo::AS_C23, "gnu::malloc"},
};
struct ParsedAttrInfoRestrict final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoRestrict()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Restrict,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/RestrictSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const override {
    enum Spelling {
      Declspec_restrict = 0,
      GNU_malloc = 1,
      Bracket_gnu_malloc = 2,
      C23_gnu_malloc = 3,
      SpellingNotCalculated = 15

    };

    unsigned Idx = Attr.getAttributeSpellingListIndex();
    switch (Idx) {
    default:
      llvm_unreachable("Unknown spelling list index");
    case 0:
      return Declspec_restrict;
    case 1:
      return GNU_malloc;
    case 2:
      return Bracket_gnu_malloc;
    case 3:
      return C23_gnu_malloc;
    }
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoRestrict Instance;
};
const ParsedAttrInfoRestrict ParsedAttrInfoRestrict::Instance;
static constexpr ParsedAttrInfo::Spelling RetainSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "retain"},
    {AttributeCommonInfo::AS_Bracket, "gnu::retain"},
    {AttributeCommonInfo::AS_C23, "gnu::retain"},
};
struct ParsedAttrInfoRetain final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoRetain()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Retain,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/RetainSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isNonLocalVar(D) && !isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "variables with non-local storage and functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) RetainAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoRetain Instance;
};
const ParsedAttrInfoRetain ParsedAttrInfoRetain::Instance;
static constexpr ParsedAttrInfo::Spelling ReturnsNonNullSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "returns_nonnull"},
    {AttributeCommonInfo::AS_Bracket, "gnu::returns_nonnull"},
    {AttributeCommonInfo::AS_C23, "gnu::returns_nonnull"},
};
struct ParsedAttrInfoReturnsNonNull final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoReturnsNonNull()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ReturnsNonNull,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/ReturnsNonNullSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoReturnsNonNull Instance;
};
const ParsedAttrInfoReturnsNonNull ParsedAttrInfoReturnsNonNull::Instance;
static constexpr ParsedAttrInfo::Spelling ReturnsTwiceSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "returns_twice"},
    {AttributeCommonInfo::AS_Bracket, "gnu::returns_twice"},
    {AttributeCommonInfo::AS_C23, "gnu::returns_twice"},
};
struct ParsedAttrInfoReturnsTwice final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoReturnsTwice()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ReturnsTwice,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/ReturnsTwiceSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) ReturnsTwiceAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoReturnsTwice Instance;
};
const ParsedAttrInfoReturnsTwice ParsedAttrInfoReturnsTwice::Instance;
static constexpr ParsedAttrInfo::Spelling SPtrSpellings[] = {
    {AttributeCommonInfo::AS_Keyword, "__sptr"},
};
struct ParsedAttrInfoSPtr final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoSPtr()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_SPtr,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/SPtrSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoSPtr Instance;
};
const ParsedAttrInfoSPtr ParsedAttrInfoSPtr::Instance;
static constexpr ParsedAttrInfo::Spelling SectionSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "section"},
    {AttributeCommonInfo::AS_Bracket, "gnu::section"},
    {AttributeCommonInfo::AS_C23, "gnu::section"},
    {AttributeCommonInfo::AS_Declspec, "allocate"},
};
static constexpr const char *SectionArgNames[] = {
    "Name",
};
struct ParsedAttrInfoSection final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoSection()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Section,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/SectionSpellings,
            /*ArgNames=*/SectionArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D) && !isGlobalVar(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "functions and global variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const override {
    enum Spelling {
      GNU_section = 0,
      Bracket_gnu_section = 1,
      C23_gnu_section = 2,
      Declspec_allocate = 3,
      SpellingNotCalculated = 15

    };

    unsigned Idx = Attr.getAttributeSpellingListIndex();
    switch (Idx) {
    default:
      llvm_unreachable("Unknown spelling list index");
    case 0:
      return GNU_section;
    case 1:
      return Bracket_gnu_section;
    case 2:
      return C23_gnu_section;
    case 3:
      return Declspec_allocate;
    }
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_global, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoSection Instance;
};
const ParsedAttrInfoSection ParsedAttrInfoSection::Instance;
static constexpr ParsedAttrInfo::Spelling SelectAnySpellings[] = {
    {AttributeCommonInfo::AS_Declspec, "selectany"},
    {AttributeCommonInfo::AS_GNU, "selectany"},
    {AttributeCommonInfo::AS_Bracket, "gnu::selectany"},
    {AttributeCommonInfo::AS_C23, "gnu::selectany"},
};
struct ParsedAttrInfoSelectAny final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoSelectAny()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_SelectAny,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/SelectAnySpellings,
            /*ArgNames=*/{}) {}
  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) SelectAnyAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoSelectAny Instance;
};
const ParsedAttrInfoSelectAny ParsedAttrInfoSelectAny::Instance;
static constexpr ParsedAttrInfo::Spelling SentinelSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "sentinel"},
    {AttributeCommonInfo::AS_Bracket, "gnu::sentinel"},
    {AttributeCommonInfo::AS_C23, "gnu::sentinel"},
};
static constexpr const char *SentinelArgNames[] = {
    "Sentinel",
    "NullPos",
};
struct ParsedAttrInfoSentinel final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoSentinel()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Sentinel,
            /*NumArgs=*/0,
            /*OptArgs=*/2,
            /*NumArgMembers=*/2,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/SentinelSpellings,
            /*ArgNames=*/SentinelArgNames) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoSentinel Instance;
};
const ParsedAttrInfoSentinel ParsedAttrInfoSentinel::Instance;
static constexpr ParsedAttrInfo::Spelling SpeculativeLoadHardeningSpellings[] =
    {
        {AttributeCommonInfo::AS_GNU, "speculative_load_hardening"},
        {AttributeCommonInfo::AS_Bracket, "clang::speculative_load_hardening"},
        {AttributeCommonInfo::AS_Bracket, "neverc::speculative_load_hardening"},
        {AttributeCommonInfo::AS_C23, "clang::speculative_load_hardening"},
        {AttributeCommonInfo::AS_C23, "neverc::speculative_load_hardening"},
};
struct ParsedAttrInfoSpeculativeLoadHardening final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoSpeculativeLoadHardening()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_SpeculativeLoadHardening,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/SpeculativeLoadHardeningSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<NoSpeculativeLoadHardeningAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) SpeculativeLoadHardeningAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoSpeculativeLoadHardening Instance;
};
const ParsedAttrInfoSpeculativeLoadHardening
    ParsedAttrInfoSpeculativeLoadHardening::Instance;
static constexpr ParsedAttrInfo::Spelling StandardNoReturnSpellings[] = {
    {AttributeCommonInfo::AS_Bracket, "noreturn"},
    {AttributeCommonInfo::AS_C23, "noreturn"},
    {AttributeCommonInfo::AS_C23, "_Noreturn"},
};
struct ParsedAttrInfoStandardNoReturn final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoStandardNoReturn()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_StandardNoReturn,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/StandardNoReturnSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const override {
    enum Spelling {
      Bracket_noreturn = 0,
      C23_noreturn = 1,
      C23_Noreturn = 2,
      SpellingNotCalculated = 15

    };

    unsigned Idx = Attr.getAttributeSpellingListIndex();
    switch (Idx) {
    default:
      llvm_unreachable("Unknown spelling list index");
    case 0:
      return Bracket_noreturn;
    case 1:
      return C23_noreturn;
    case 2:
      return C23_Noreturn;
    }
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoStandardNoReturn Instance;
};
const ParsedAttrInfoStandardNoReturn ParsedAttrInfoStandardNoReturn::Instance;
static constexpr ParsedAttrInfo::Spelling StdCallSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "stdcall"},
    {AttributeCommonInfo::AS_Bracket, "gnu::stdcall"},
    {AttributeCommonInfo::AS_C23, "gnu::stdcall"},
    {AttributeCommonInfo::AS_Keyword, "__stdcall"},
    {AttributeCommonInfo::AS_Keyword, "_stdcall"},
};
struct ParsedAttrInfoStdCall final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoStdCall()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_StdCall,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/StdCallSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoStdCall Instance;
};
const ParsedAttrInfoStdCall ParsedAttrInfoStdCall::Instance;
struct ParsedAttrInfoStrictFP final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoStrictFP()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_StrictFP,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/{},
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoStrictFP Instance;
};
const ParsedAttrInfoStrictFP ParsedAttrInfoStrictFP::Instance;
static constexpr ParsedAttrInfo::Spelling StrictGuardStackCheckSpellings[] = {
    {AttributeCommonInfo::AS_Declspec, "strict_gs_check"},
};
struct ParsedAttrInfoStrictGuardStackCheck final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoStrictGuardStackCheck()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_StrictGuardStackCheck,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/StrictGuardStackCheckSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) StrictGuardStackCheckAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoStrictGuardStackCheck Instance;
};
const ParsedAttrInfoStrictGuardStackCheck
    ParsedAttrInfoStrictGuardStackCheck::Instance;
static constexpr ParsedAttrInfo::Spelling SuppressSpellings[] = {
    {AttributeCommonInfo::AS_Bracket, "gsl::suppress"},
    {AttributeCommonInfo::AS_GNU, "suppress"},
    {AttributeCommonInfo::AS_Bracket, "clang::suppress"},
    {AttributeCommonInfo::AS_Bracket, "neverc::suppress"},
    {AttributeCommonInfo::AS_C23, "clang::suppress"},
    {AttributeCommonInfo::AS_C23, "neverc::suppress"},
};
static constexpr const char *SuppressArgNames[] = {
    "DiagnosticIdentifiers...",
};
struct ParsedAttrInfoSuppress final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoSuppress()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Suppress,
            /*NumArgs=*/0,
            /*OptArgs=*/15,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/1,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/SuppressSpellings,
            /*ArgNames=*/SuppressArgNames) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoSuppress Instance;
};
const ParsedAttrInfoSuppress ParsedAttrInfoSuppress::Instance;
static constexpr ParsedAttrInfo::Spelling SysVABISpellings[] = {
    {AttributeCommonInfo::AS_GNU, "sysv_abi"},
    {AttributeCommonInfo::AS_Bracket, "gnu::sysv_abi"},
    {AttributeCommonInfo::AS_C23, "gnu::sysv_abi"},
};
struct ParsedAttrInfoSysVABI final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoSysVABI()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_SysVABI,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/SysVABISpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoSysVABI Instance;
};
const ParsedAttrInfoSysVABI ParsedAttrInfoSysVABI::Instance;
static constexpr ParsedAttrInfo::Spelling TLSModelSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "tls_model"},
    {AttributeCommonInfo::AS_Bracket, "gnu::tls_model"},
    {AttributeCommonInfo::AS_C23, "gnu::tls_model"},
};
static constexpr const char *TLSModelArgNames[] = {
    "Model",
};
struct ParsedAttrInfoTLSModel final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoTLSModel()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_TLSModel,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/TLSModelSpellings,
            /*ArgNames=*/TLSModelArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isTLSVar(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "thread-local variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_thread_local, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoTLSModel Instance;
};
const ParsedAttrInfoTLSModel ParsedAttrInfoTLSModel::Instance;
static constexpr ParsedAttrInfo::Spelling TargetSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "target"},
    {AttributeCommonInfo::AS_Bracket, "gnu::target"},
    {AttributeCommonInfo::AS_C23, "gnu::target"},
};
static constexpr const char *TargetArgNames[] = {
    "featuresStr",
};
struct ParsedAttrInfoTarget final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoTarget()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Target,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/TargetSpellings,
            /*ArgNames=*/TargetArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<TargetClonesAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<TargetVersionAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<CPUDispatchAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<CPUSpecificAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoTarget Instance;
};
const ParsedAttrInfoTarget ParsedAttrInfoTarget::Instance;
static constexpr ParsedAttrInfo::Spelling TargetClonesSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "target_clones"},
    {AttributeCommonInfo::AS_Bracket, "gnu::target_clones"},
    {AttributeCommonInfo::AS_C23, "gnu::target_clones"},
};
static constexpr const char *TargetClonesArgNames[] = {
    "featuresStrs...",
};
struct ParsedAttrInfoTargetClones final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoTargetClones()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_TargetClones,
            /*NumArgs=*/0,
            /*OptArgs=*/15,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/TargetClonesSpellings,
            /*ArgNames=*/TargetClonesArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<TargetVersionAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<TargetAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<CPUDispatchAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<CPUSpecificAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoTargetClones Instance;
};
const ParsedAttrInfoTargetClones ParsedAttrInfoTargetClones::Instance;
static constexpr ParsedAttrInfo::Spelling TargetVersionSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "target_version"},
    {AttributeCommonInfo::AS_Bracket, "gnu::target_version"},
    {AttributeCommonInfo::AS_C23, "gnu::target_version"},
};
static constexpr const char *TargetVersionArgNames[] = {
    "NamesStr",
};
struct ParsedAttrInfoTargetVersion final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoTargetVersion()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_TargetVersion,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/TargetVersionSpellings,
            /*ArgNames=*/TargetVersionArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  using ParsedAttrInfo::diagMutualExclusion;

  bool diagMutualExclusion(Sema &S, const ParsedAttr &AL,
                           const Decl *D) const override {
    if (const auto *A = D->getAttr<TargetClonesAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<TargetAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<CPUDispatchAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *A = D->getAttr<CPUSpecificAttr>()) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << A
          << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
      S.Diag(A->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoTargetVersion Instance;
};
const ParsedAttrInfoTargetVersion ParsedAttrInfoTargetVersion::Instance;
static constexpr ParsedAttrInfo::Spelling ThreadSpellings[] = {
    {AttributeCommonInfo::AS_Declspec, "thread"},
};
struct ParsedAttrInfoThread final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoThread()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Thread,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/ThreadSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<VarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  bool acceptsLangOpts(const LangOptions &LangOpts) const override {
    return LangOpts.MicrosoftExt;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoThread Instance;
};
const ParsedAttrInfoThread ParsedAttrInfoThread::Instance;
static constexpr ParsedAttrInfo::Spelling TransparentUnionSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "transparent_union"},
    {AttributeCommonInfo::AS_Bracket, "gnu::transparent_union"},
    {AttributeCommonInfo::AS_C23, "gnu::transparent_union"},
};
struct ParsedAttrInfoTransparentUnion final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoTransparentUnion()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_TransparentUnion,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/TransparentUnionSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoTransparentUnion Instance;
};
const ParsedAttrInfoTransparentUnion ParsedAttrInfoTransparentUnion::Instance;
static constexpr ParsedAttrInfo::Spelling TypeNonNullSpellings[] = {
    {AttributeCommonInfo::AS_Keyword, "_Nonnull"},
};
struct ParsedAttrInfoTypeNonNull final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoTypeNonNull()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_TypeNonNull,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/TypeNonNullSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoTypeNonNull Instance;
};
const ParsedAttrInfoTypeNonNull ParsedAttrInfoTypeNonNull::Instance;
static constexpr ParsedAttrInfo::Spelling TypeNullUnspecifiedSpellings[] = {
    {AttributeCommonInfo::AS_Keyword, "_Null_unspecified"},
};
struct ParsedAttrInfoTypeNullUnspecified final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoTypeNullUnspecified()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_TypeNullUnspecified,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/TypeNullUnspecifiedSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoTypeNullUnspecified Instance;
};
const ParsedAttrInfoTypeNullUnspecified
    ParsedAttrInfoTypeNullUnspecified::Instance;
static constexpr ParsedAttrInfo::Spelling TypeNullableSpellings[] = {
    {AttributeCommonInfo::AS_Keyword, "_Nullable"},
};
struct ParsedAttrInfoTypeNullable final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoTypeNullable()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_TypeNullable,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/TypeNullableSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoTypeNullable Instance;
};
const ParsedAttrInfoTypeNullable ParsedAttrInfoTypeNullable::Instance;
static constexpr ParsedAttrInfo::Spelling TypeTagForDatatypeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "type_tag_for_datatype"},
    {AttributeCommonInfo::AS_Bracket, "clang::type_tag_for_datatype"},
    {AttributeCommonInfo::AS_Bracket, "neverc::type_tag_for_datatype"},
    {AttributeCommonInfo::AS_C23, "clang::type_tag_for_datatype"},
    {AttributeCommonInfo::AS_C23, "neverc::type_tag_for_datatype"},
};
static constexpr const char *TypeTagForDatatypeArgNames[] = {
    "ArgumentKind",
    "MatchingCType",
    "LayoutCompatible",
    "MustBeNull",
};
struct ParsedAttrInfoTypeTagForDatatype final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoTypeTagForDatatype()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_TypeTagForDatatype,
            /*NumArgs=*/4,
            /*OptArgs=*/0,
            /*NumArgMembers=*/4,
            /*HasCustomParsing=*/1,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/TypeTagForDatatypeSpellings,
            /*ArgNames=*/TypeTagForDatatypeArgNames) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoTypeTagForDatatype Instance;
};
const ParsedAttrInfoTypeTagForDatatype
    ParsedAttrInfoTypeTagForDatatype::Instance;
static constexpr ParsedAttrInfo::Spelling TypeVisibilitySpellings[] = {
    {AttributeCommonInfo::AS_GNU, "type_visibility"},
    {AttributeCommonInfo::AS_Bracket, "clang::type_visibility"},
    {AttributeCommonInfo::AS_Bracket, "neverc::type_visibility"},
    {AttributeCommonInfo::AS_C23, "clang::type_visibility"},
    {AttributeCommonInfo::AS_C23, "neverc::type_visibility"},
};
static constexpr const char *TypeVisibilityArgNames[] = {
    "Visibility",
};
struct ParsedAttrInfoTypeVisibility final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoTypeVisibility()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_TypeVisibility,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/TypeVisibilitySpellings,
            /*ArgNames=*/TypeVisibilityArgNames) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoTypeVisibility Instance;
};
const ParsedAttrInfoTypeVisibility ParsedAttrInfoTypeVisibility::Instance;
static constexpr ParsedAttrInfo::Spelling UPtrSpellings[] = {
    {AttributeCommonInfo::AS_Keyword, "__uptr"},
};
struct ParsedAttrInfoUPtr final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoUPtr()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_UPtr,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/UPtrSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoUPtr Instance;
};
const ParsedAttrInfoUPtr ParsedAttrInfoUPtr::Instance;
static constexpr ParsedAttrInfo::Spelling UnavailableSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "unavailable"},
    {AttributeCommonInfo::AS_Bracket, "clang::unavailable"},
    {AttributeCommonInfo::AS_Bracket, "neverc::unavailable"},
    {AttributeCommonInfo::AS_C23, "clang::unavailable"},
    {AttributeCommonInfo::AS_C23, "neverc::unavailable"},
};
static constexpr const char *UnavailableArgNames[] = {
    "Message",
};
struct ParsedAttrInfoUnavailable final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoUnavailable()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Unavailable,
            /*NumArgs=*/0,
            /*OptArgs=*/1,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/UnavailableSpellings,
            /*ArgNames=*/UnavailableArgNames) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoUnavailable Instance;
};
const ParsedAttrInfoUnavailable ParsedAttrInfoUnavailable::Instance;
static constexpr ParsedAttrInfo::Spelling UninitializedSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "uninitialized"},
    {AttributeCommonInfo::AS_Bracket, "clang::uninitialized"},
    {AttributeCommonInfo::AS_Bracket, "neverc::uninitialized"},
};
struct ParsedAttrInfoUninitialized final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoUninitialized()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Uninitialized,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/UninitializedSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isLocalVar(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "local variables";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_local, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoUninitialized Instance;
};
const ParsedAttrInfoUninitialized ParsedAttrInfoUninitialized::Instance;
static constexpr ParsedAttrInfo::Spelling UnlikelySpellings[] = {
    {AttributeCommonInfo::AS_Bracket, "unlikely"},
    {AttributeCommonInfo::AS_C23, "neverc::unlikely"},
};
struct ParsedAttrInfoUnlikely final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoUnlikely()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Unlikely,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/1,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/UnlikelySpellings,
            /*ArgNames=*/{}) {}
  using ParsedAttrInfo::diagMutualExclusion;

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoUnlikely Instance;
};
const ParsedAttrInfoUnlikely ParsedAttrInfoUnlikely::Instance;
static constexpr ParsedAttrInfo::Spelling UnsafeBufferUsageSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "unsafe_buffer_usage"},
    {AttributeCommonInfo::AS_Bracket, "clang::unsafe_buffer_usage"},
    {AttributeCommonInfo::AS_Bracket, "neverc::unsafe_buffer_usage"},
    {AttributeCommonInfo::AS_C23, "clang::unsafe_buffer_usage"},
    {AttributeCommonInfo::AS_C23, "neverc::unsafe_buffer_usage"},
};
struct ParsedAttrInfoUnsafeBufferUsage final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoUnsafeBufferUsage()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_UnsafeBufferUsage,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/UnsafeBufferUsageSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoUnsafeBufferUsage Instance;
};
const ParsedAttrInfoUnsafeBufferUsage ParsedAttrInfoUnsafeBufferUsage::Instance;
static constexpr ParsedAttrInfo::Spelling UnusedSpellings[] = {
    {AttributeCommonInfo::AS_Bracket, "maybe_unused"},
    {AttributeCommonInfo::AS_GNU, "unused"},
    {AttributeCommonInfo::AS_Bracket, "gnu::unused"},
    {AttributeCommonInfo::AS_C23, "gnu::unused"},
    {AttributeCommonInfo::AS_C23, "maybe_unused"},
};
struct ParsedAttrInfoUnused final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoUnused()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Unused,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/UnusedSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<VarDecl>(D) && !isa<TypeDecl>(D) && !isa<EnumDecl>(D) &&
        !isa<EnumConstantDecl>(D) && !isa<LabelDecl>(D) && !isa<FieldDecl>(D) &&
        !isFunctionLike(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "variables, types, enums, enumerators, labels, fields, functions, "
             "and function pointers";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const override {
    enum Spelling {
      Bracket_maybe_unused = 0,
      GNU_unused = 1,
      Bracket_gnu_unused = 2,
      C23_gnu_unused = 3,
      C23_maybe_unused = 4,
      SpellingNotCalculated = 15

    };

    unsigned Idx = Attr.getAttributeSpellingListIndex();
    switch (Idx) {
    default:
      llvm_unreachable("Unknown spelling list index");
    case 0:
      return Bracket_maybe_unused;
    case 1:
      return GNU_unused;
    case 2:
      return Bracket_gnu_unused;
    case 3:
      return C23_gnu_unused;
    case 4:
      return C23_maybe_unused;
    }
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoUnused Instance;
};
const ParsedAttrInfoUnused ParsedAttrInfoUnused::Instance;
static constexpr ParsedAttrInfo::Spelling UseHandleSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "use_handle"},
    {AttributeCommonInfo::AS_Bracket, "clang::use_handle"},
    {AttributeCommonInfo::AS_Bracket, "neverc::use_handle"},
    {AttributeCommonInfo::AS_C23, "clang::use_handle"},
    {AttributeCommonInfo::AS_C23, "neverc::use_handle"},
};
static constexpr const char *UseHandleArgNames[] = {
    "HandleType",
};
struct ParsedAttrInfoUseHandle final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoUseHandle()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_UseHandle,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/UseHandleSpellings,
            /*ArgNames=*/UseHandleArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<ParmVarDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "parameters";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_variable_is_parameter, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoUseHandle Instance;
};
const ParsedAttrInfoUseHandle ParsedAttrInfoUseHandle::Instance;
static constexpr ParsedAttrInfo::Spelling UsedSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "used"},
    {AttributeCommonInfo::AS_Bracket, "gnu::used"},
    {AttributeCommonInfo::AS_C23, "gnu::used"},
};
struct ParsedAttrInfoUsed final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoUsed()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Used,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/UsedSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isNonLocalVar(D) && !isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "variables with non-local storage and functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) UsedAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoUsed Instance;
};
const ParsedAttrInfoUsed ParsedAttrInfoUsed::Instance;
static constexpr ParsedAttrInfo::Spelling VectorCallSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "vectorcall"},
    {AttributeCommonInfo::AS_Bracket, "clang::vectorcall"},
    {AttributeCommonInfo::AS_Bracket, "neverc::vectorcall"},
    {AttributeCommonInfo::AS_C23, "clang::vectorcall"},
    {AttributeCommonInfo::AS_C23, "neverc::vectorcall"},
    {AttributeCommonInfo::AS_Keyword, "__vectorcall"},
    {AttributeCommonInfo::AS_Keyword, "_vectorcall"},
};
struct ParsedAttrInfoVectorCall final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoVectorCall()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_VectorCall,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/VectorCallSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoVectorCall Instance;
};
const ParsedAttrInfoVectorCall ParsedAttrInfoVectorCall::Instance;
static constexpr ParsedAttrInfo::Spelling VectorSizeSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "vector_size"},
    {AttributeCommonInfo::AS_Bracket, "gnu::vector_size"},
    {AttributeCommonInfo::AS_C23, "gnu::vector_size"},
};
static constexpr const char *VectorSizeArgNames[] = {
    "NumBytes",
};
struct ParsedAttrInfoVectorSize final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoVectorSize()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_VectorSize,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/1,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/VectorSizeSpellings,
            /*ArgNames=*/VectorSizeArgNames) {}
  bool isParamExpr(size_t N) const override { return (N == 0) || false; }

  static const ParsedAttrInfoVectorSize Instance;
};
const ParsedAttrInfoVectorSize ParsedAttrInfoVectorSize::Instance;
static constexpr ParsedAttrInfo::Spelling VisibilitySpellings[] = {
    {AttributeCommonInfo::AS_GNU, "visibility"},
    {AttributeCommonInfo::AS_Bracket, "gnu::visibility"},
    {AttributeCommonInfo::AS_C23, "gnu::visibility"},
};
static constexpr const char *VisibilityArgNames[] = {
    "Visibility",
};
struct ParsedAttrInfoVisibility final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoVisibility()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Visibility,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/VisibilitySpellings,
            /*ArgNames=*/VisibilityArgNames) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoVisibility Instance;
};
const ParsedAttrInfoVisibility ParsedAttrInfoVisibility::Instance;
static constexpr ParsedAttrInfo::Spelling VolatileSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "volatile"},
    {AttributeCommonInfo::AS_Bracket, "gnu::volatile"},
    {AttributeCommonInfo::AS_C23, "gnu::volatile"},
    {AttributeCommonInfo::AS_Declspec, "volatile"},
};
struct ParsedAttrInfoVolatile final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoVolatile()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Volatile,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/VolatileSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoVolatile Instance;
};
const ParsedAttrInfoVolatile ParsedAttrInfoVolatile::Instance;
static constexpr ParsedAttrInfo::Spelling WarnUnusedSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "warn_unused"},
    {AttributeCommonInfo::AS_Bracket, "gnu::warn_unused"},
    {AttributeCommonInfo::AS_C23, "gnu::warn_unused"},
};
struct ParsedAttrInfoWarnUnused final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoWarnUnused()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_WarnUnused,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/WarnUnusedSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<RecordDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "structs and unions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_record, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) WarnUnusedAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoWarnUnused Instance;
};
const ParsedAttrInfoWarnUnused ParsedAttrInfoWarnUnused::Instance;
static constexpr ParsedAttrInfo::Spelling WarnUnusedResultSpellings[] = {
    {AttributeCommonInfo::AS_Bracket, "nodiscard"},
    {AttributeCommonInfo::AS_C23, "nodiscard"},
    {AttributeCommonInfo::AS_Bracket, "neverc::warn_unused_result"},
    {AttributeCommonInfo::AS_GNU, "warn_unused_result"},
    {AttributeCommonInfo::AS_Bracket, "gnu::warn_unused_result"},
    {AttributeCommonInfo::AS_C23, "gnu::warn_unused_result"},
};
static constexpr const char *WarnUnusedResultArgNames[] = {
    "Message",
};
struct ParsedAttrInfoWarnUnusedResult final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoWarnUnusedResult()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_WarnUnusedResult,
            /*NumArgs=*/0,
            /*OptArgs=*/1,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/WarnUnusedResultSpellings,
            /*ArgNames=*/WarnUnusedResultArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<EnumDecl>(D) && !isa<RecordDecl>(D) && !isFunctionLike(D) &&
        !isa<TypedefNameDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "enums, structs, unions, functions, function pointers, and "
             "typedefs";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const override {
    enum Spelling {
      Bracket_nodiscard = 0,
      C23_nodiscard = 1,
      Bracket_neverc_warn_unused_result = 2,
      GNU_warn_unused_result = 3,
      Bracket_gnu_warn_unused_result = 4,
      C23_gnu_warn_unused_result = 5,
      SpellingNotCalculated = 15

    };

    unsigned Idx = Attr.getAttributeSpellingListIndex();
    switch (Idx) {
    default:
      llvm_unreachable("Unknown spelling list index");
    case 0:
      return Bracket_nodiscard;
    case 1:
      return C23_nodiscard;
    case 2:
      return Bracket_neverc_warn_unused_result;
    case 3:
      return GNU_warn_unused_result;
    case 4:
      return Bracket_gnu_warn_unused_result;
    case 5:
      return C23_gnu_warn_unused_result;
    }
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_enum, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_record, /*IsSupported=*/true));
    MatchRules.push_back(std::make_pair(
        attr::SubjectMatchRule_hasType_functionType, /*IsSupported=*/true));
    MatchRules.push_back(std::make_pair(attr::SubjectMatchRule_type_alias,
                                        /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoWarnUnusedResult Instance;
};
const ParsedAttrInfoWarnUnusedResult ParsedAttrInfoWarnUnusedResult::Instance;
static constexpr ParsedAttrInfo::Spelling WeakSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "weak"},
    {AttributeCommonInfo::AS_Bracket, "gnu::weak"},
    {AttributeCommonInfo::AS_C23, "gnu::weak"},
};
struct ParsedAttrInfoWeak final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoWeak()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_Weak,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/WeakSpellings,
            /*ArgNames=*/{}) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<VarDecl>(D) && !isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "variables and functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    D->addAttr(::new (S.Context) WeakAttr(S.Context, Attr));
    return AttributeApplied;
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoWeak Instance;
};
const ParsedAttrInfoWeak ParsedAttrInfoWeak::Instance;
static constexpr ParsedAttrInfo::Spelling WeakImportSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "weak_import"},
    {AttributeCommonInfo::AS_Bracket, "clang::weak_import"},
    {AttributeCommonInfo::AS_Bracket, "neverc::weak_import"},
    {AttributeCommonInfo::AS_C23, "clang::weak_import"},
    {AttributeCommonInfo::AS_C23, "neverc::weak_import"},
};
struct ParsedAttrInfoWeakImport final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoWeakImport()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_WeakImport,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/0,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/WeakImportSpellings,
            /*ArgNames=*/{}) {}
  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoWeakImport Instance;
};
const ParsedAttrInfoWeakImport ParsedAttrInfoWeakImport::Instance;
static constexpr ParsedAttrInfo::Spelling WeakRefSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "weakref"},
    {AttributeCommonInfo::AS_Bracket, "gnu::weakref"},
    {AttributeCommonInfo::AS_C23, "gnu::weakref"},
};
static constexpr const char *WeakRefArgNames[] = {
    "Aliasee",
};
struct ParsedAttrInfoWeakRef final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoWeakRef()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_WeakRef,
            /*NumArgs=*/0,
            /*OptArgs=*/1,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/WeakRefSpellings,
            /*ArgNames=*/WeakRefArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<VarDecl>(D) && !isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute()
          << "variables and functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_variable, /*IsSupported=*/true));
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoWeakRef Instance;
};
const ParsedAttrInfoWeakRef ParsedAttrInfoWeakRef::Instance;
static constexpr ParsedAttrInfo::Spelling X86ForceAlignArgPointerSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "force_align_arg_pointer"},
    {AttributeCommonInfo::AS_Bracket, "gnu::force_align_arg_pointer"},
    {AttributeCommonInfo::AS_C23, "gnu::force_align_arg_pointer"},
};
struct ParsedAttrInfoX86ForceAlignArgPointer final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoX86ForceAlignArgPointer()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_X86ForceAlignArgPointer,
            /*NumArgs=*/0,
            /*OptArgs=*/0,
            /*NumArgMembers=*/0,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/1,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/0,
            /*Spellings=*/X86ForceAlignArgPointerSpellings,
            /*ArgNames=*/{}) {}
  bool existsInTarget(const TargetInfo &Target) const override {
    const llvm::Triple &T = Target.getTriple();
    (void)T;
    return true && (T.getArch() == llvm::Triple::x86_64);
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoX86ForceAlignArgPointer Instance;
};
const ParsedAttrInfoX86ForceAlignArgPointer
    ParsedAttrInfoX86ForceAlignArgPointer::Instance;
static constexpr ParsedAttrInfo::Spelling ZeroCallUsedRegsSpellings[] = {
    {AttributeCommonInfo::AS_GNU, "zero_call_used_regs"},
    {AttributeCommonInfo::AS_Bracket, "gnu::zero_call_used_regs"},
    {AttributeCommonInfo::AS_C23, "gnu::zero_call_used_regs"},
};
static constexpr const char *ZeroCallUsedRegsArgNames[] = {
    "ZeroCallUsedRegs",
};
struct ParsedAttrInfoZeroCallUsedRegs final : public ParsedAttrInfo {
  constexpr ParsedAttrInfoZeroCallUsedRegs()
      : ParsedAttrInfo(
            /*AttrKind=*/ParsedAttr::AT_ZeroCallUsedRegs,
            /*NumArgs=*/1,
            /*OptArgs=*/0,
            /*NumArgMembers=*/1,
            /*HasCustomParsing=*/0,
            /*IsTargetSpecific=*/0,
            /*IsType=*/0,
            /*IsStmt=*/0,
            /*IsKnownToGCC=*/1,
            /*IsSupportedByPragmaAttribute=*/1,
            /*Spellings=*/ZeroCallUsedRegsSpellings,
            /*ArgNames=*/ZeroCallUsedRegsArgNames) {}
  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "functions";
      return false;
    }
    return true;
  }

  bool diagAppertainsToStmt(Sema &S, const ParsedAttr &AL,
                            const Stmt *St) const override {
    S.Diag(AL.getLoc(), diag::err_decl_attribute_invalid_on_stmt)
        << AL << AL.isRegularKeywordAttribute() << St->getBeginLoc();
    return false;
  }

  void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
          &MatchRules,
      const LangOptions &LangOpts) const override {
    MatchRules.push_back(
        std::make_pair(attr::SubjectMatchRule_function, /*IsSupported=*/true));
  }

  bool isParamExpr(size_t N) const override { return false; }

  static const ParsedAttrInfoZeroCallUsedRegs Instance;
};
const ParsedAttrInfoZeroCallUsedRegs ParsedAttrInfoZeroCallUsedRegs::Instance;
static const ParsedAttrInfo *AttrInfoMap[] = {
    &ParsedAttrInfoAArch64SVEPcs::Instance,
    &ParsedAttrInfoAArch64VectorPcs::Instance,
    &ParsedAttrInfoAcquireHandle::Instance,
    &ParsedAttrInfoAddressSpace::Instance,
    &ParsedAttrInfoAlias::Instance,
    &ParsedAttrInfoAlignValue::Instance,
    &ParsedAttrInfoAligned::Instance,
    &ParsedAttrInfoAllocAlign::Instance,
    &ParsedAttrInfoAllocSize::Instance,
    &ParsedAttrInfoAlwaysDestroy::Instance,
    &ParsedAttrInfoAlwaysInline::Instance,
    &ParsedAttrInfoAnalyzerNoReturn::Instance,
    &ParsedAttrInfoAnnotate::Instance,
    &ParsedAttrInfoAnnotateType::Instance,
    &ParsedAttrInfoInterrupt::Instance,
    &ParsedAttrInfoAnyX86NoCallerSavedRegisters::Instance,
    &ParsedAttrInfoAnyX86NoCfCheck::Instance,
    &ParsedAttrInfoArgumentWithTypeTag::Instance,
    &ParsedAttrInfoArmBuiltinAlias::Instance,
    &ParsedAttrInfoArmLocallyStreaming::Instance,
    &ParsedAttrInfoArmNewZA::Instance,
    &ParsedAttrInfoArmPreservesZA::Instance,
    &ParsedAttrInfoArmSharedZA::Instance,
    &ParsedAttrInfoArmStreaming::Instance,
    &ParsedAttrInfoArmStreamingCompatible::Instance,
    &ParsedAttrInfoArmSveVectorBits::Instance,
    &ParsedAttrInfoArtificial::Instance,
    &ParsedAttrInfoAssumeAligned::Instance,
    &ParsedAttrInfoAssumption::Instance,
    &ParsedAttrInfoAvailability::Instance,
    &ParsedAttrInfoAvailableOnlyInDefaultEvalMethod::Instance,
    &ParsedAttrInfoBTFDeclTag::Instance,
    &ParsedAttrInfoBTFTypeTag::Instance,
    &ParsedAttrInfoBuiltinAlias::Instance,
    &ParsedAttrInfoCDecl::Instance,
    &ParsedAttrInfoCFGuard::Instance,
    &ParsedAttrInfoCPUDispatch::Instance,
    &ParsedAttrInfoCPUSpecific::Instance,
    &ParsedAttrInfoCallback::Instance,
    &ParsedAttrInfoCarriesDependency::Instance,
    &ParsedAttrInfoCleanup::Instance,
    &ParsedAttrInfoCodeAlign::Instance,
    &ParsedAttrInfoCodeSeg::Instance,
    &ParsedAttrInfoCold::Instance,
    &ParsedAttrInfoCommon::Instance,
    &ParsedAttrInfoConst::Instance,
    &ParsedAttrInfoConstructor::Instance,
    &ParsedAttrInfoConvergent::Instance,
    &ParsedAttrInfoDLLExport::Instance,
    &ParsedAttrInfoDLLImport::Instance,
    &ParsedAttrInfoDeprecated::Instance,
    &ParsedAttrInfoDestructor::Instance,
    &ParsedAttrInfoDiagnoseAsBuiltin::Instance,
    &ParsedAttrInfoDiagnoseIf::Instance,
    &ParsedAttrInfoDisableTailCalls::Instance,
    &ParsedAttrInfoDisableTryStmt::Instance,
    &ParsedAttrInfoEnableIf::Instance,
    &ParsedAttrInfoEnforceTCB::Instance,
    &ParsedAttrInfoEnforceTCBLeaf::Instance,
    &ParsedAttrInfoEnumExtensibility::Instance,
    &ParsedAttrInfoError::Instance,
    &ParsedAttrInfoExtVectorType::Instance,
    &ParsedAttrInfoFallThrough::Instance,
    &ParsedAttrInfoFastCall::Instance,
    &ParsedAttrInfoFlagEnum::Instance,
    &ParsedAttrInfoFlatten::Instance,
    &ParsedAttrInfoFormat::Instance,
    &ParsedAttrInfoFormatArg::Instance,
    &ParsedAttrInfoFunctionReturnThunks::Instance,
    &ParsedAttrInfoGNUInline::Instance,
    &ParsedAttrInfoHot::Instance,
    &ParsedAttrInfoIFunc::Instance,
    &ParsedAttrInfoInternalLinkage::Instance,
    &ParsedAttrInfoLTOVisibilityPublic::Instance,
    &ParsedAttrInfoLeaf::Instance,
    &ParsedAttrInfoLikely::Instance,
    &ParsedAttrInfoLoaderUninitialized::Instance,
    &ParsedAttrInfoMSABI::Instance,
    &ParsedAttrInfoMSAllocator::Instance,
    &ParsedAttrInfoMSStruct::Instance,
    &ParsedAttrInfoMatrixType::Instance,
    &ParsedAttrInfoMayAlias::Instance,
    &ParsedAttrInfoMaybeUndef::Instance,
    &ParsedAttrInfoMinSize::Instance,
    &ParsedAttrInfoMinVectorWidth::Instance,
    &ParsedAttrInfoMode::Instance,
    &ParsedAttrInfoMustTail::Instance,
    &ParsedAttrInfoNaked::Instance,
    &ParsedAttrInfoNeonPolyVectorType::Instance,
    &ParsedAttrInfoNeonVectorType::Instance,
    &ParsedAttrInfoNoAlias::Instance,
    &ParsedAttrInfoNoBuiltin::Instance,
    &ParsedAttrInfoNoCommon::Instance,
    &ParsedAttrInfoNoDebug::Instance,
    &ParsedAttrInfoNoDeref::Instance,
    &ParsedAttrInfoNoDestroy::Instance,
    &ParsedAttrInfoNoDuplicate::Instance,
    &ParsedAttrInfoNoEscape::Instance,
    &ParsedAttrInfoNoInline::Instance,
    &ParsedAttrInfoNoMerge::Instance,
    &ParsedAttrInfoNoRandomizeLayout::Instance,
    &ParsedAttrInfoNoReturn::Instance,
    &ParsedAttrInfoNoSpeculativeLoadHardening::Instance,
    &ParsedAttrInfoNoSplitStack::Instance,
    &ParsedAttrInfoNoStackProtector::Instance,
    &ParsedAttrInfoNoThrow::Instance,
    &ParsedAttrInfoNoUwtable::Instance,
    &ParsedAttrInfoNonNull::Instance,
    &ParsedAttrInfoNotTailCalled::Instance,
    &ParsedAttrInfoOptimizeNone::Instance,
    &ParsedAttrInfoOverloadable::Instance,
    &ParsedAttrInfoOverride::Instance,
    &ParsedAttrInfoPacked::Instance,
    &ParsedAttrInfoPassObjectSize::Instance,
    &ParsedAttrInfoPatchableFunctionEntry::Instance,
    &ParsedAttrInfoPragmaNeverCBSSSection::Instance,
    &ParsedAttrInfoPragmaNeverCDataSection::Instance,
    &ParsedAttrInfoPragmaNeverCRelroSection::Instance,
    &ParsedAttrInfoPragmaNeverCRodataSection::Instance,
    &ParsedAttrInfoPragmaNeverCTextSection::Instance,
    &ParsedAttrInfoPreferredType::Instance,
    &ParsedAttrInfoPreserveAll::Instance,
    &ParsedAttrInfoPreserveMost::Instance,
    &ParsedAttrInfoPtr32::Instance,
    &ParsedAttrInfoPtr64::Instance,
    &ParsedAttrInfoPure::Instance,
    &ParsedAttrInfoRandomizeLayout::Instance,
    &ParsedAttrInfoReadOnlyPlacement::Instance,
    &ParsedAttrInfoRegCall::Instance,
    &ParsedAttrInfoRegparm::Instance,
    &ParsedAttrInfoReleaseHandle::Instance,
    &ParsedAttrInfoRestrict::Instance,
    &ParsedAttrInfoRetain::Instance,
    &ParsedAttrInfoReturnsNonNull::Instance,
    &ParsedAttrInfoReturnsTwice::Instance,
    &ParsedAttrInfoSPtr::Instance,
    &ParsedAttrInfoSection::Instance,
    &ParsedAttrInfoSelectAny::Instance,
    &ParsedAttrInfoSentinel::Instance,
    &ParsedAttrInfoSpeculativeLoadHardening::Instance,
    &ParsedAttrInfoStandardNoReturn::Instance,
    &ParsedAttrInfoStdCall::Instance,
    &ParsedAttrInfoStrictFP::Instance,
    &ParsedAttrInfoStrictGuardStackCheck::Instance,
    &ParsedAttrInfoSuppress::Instance,
    &ParsedAttrInfoSysVABI::Instance,
    &ParsedAttrInfoTLSModel::Instance,
    &ParsedAttrInfoTarget::Instance,
    &ParsedAttrInfoTargetClones::Instance,
    &ParsedAttrInfoTargetVersion::Instance,
    &ParsedAttrInfoThread::Instance,
    &ParsedAttrInfoTransparentUnion::Instance,
    &ParsedAttrInfoTypeNonNull::Instance,
    &ParsedAttrInfoTypeNullUnspecified::Instance,
    &ParsedAttrInfoTypeNullable::Instance,
    &ParsedAttrInfoTypeTagForDatatype::Instance,
    &ParsedAttrInfoTypeVisibility::Instance,
    &ParsedAttrInfoUPtr::Instance,
    &ParsedAttrInfoUnavailable::Instance,
    &ParsedAttrInfoUninitialized::Instance,
    &ParsedAttrInfoUnlikely::Instance,
    &ParsedAttrInfoUnsafeBufferUsage::Instance,
    &ParsedAttrInfoUnused::Instance,
    &ParsedAttrInfoUseHandle::Instance,
    &ParsedAttrInfoUsed::Instance,
    &ParsedAttrInfoVectorCall::Instance,
    &ParsedAttrInfoVectorSize::Instance,
    &ParsedAttrInfoVisibility::Instance,
    &ParsedAttrInfoVolatile::Instance,
    &ParsedAttrInfoWarnUnused::Instance,
    &ParsedAttrInfoWarnUnusedResult::Instance,
    &ParsedAttrInfoWeak::Instance,
    &ParsedAttrInfoWeakImport::Instance,
    &ParsedAttrInfoWeakRef::Instance,
    &ParsedAttrInfoX86ForceAlignArgPointer::Instance,
    &ParsedAttrInfoZeroCallUsedRegs::Instance,
};

static bool checkAttributeMatchRuleAppliesTo(const Decl *D,
                                             attr::SubjectMatchRule rule) {
  switch (rule) {
  case attr::SubjectMatchRule_enum:
    return isa<EnumDecl>(D);
  case attr::SubjectMatchRule_enum_constant:
    return isa<EnumConstantDecl>(D);
  case attr::SubjectMatchRule_field:
    return isa<FieldDecl>(D);
  case attr::SubjectMatchRule_function:
    return isa<FunctionDecl>(D);
  case attr::SubjectMatchRule_record:
    return isa<RecordDecl>(D);
  case attr::SubjectMatchRule_record_not_is_union:
    return isStruct(D);
  case attr::SubjectMatchRule_hasType_abstract:
    assert(false && "Abstract matcher rule isn't allowed");
    return false;
  case attr::SubjectMatchRule_hasType_functionType:
    return isFunctionLike(D);
  case attr::SubjectMatchRule_type_alias:
    return isa<TypedefNameDecl>(D);
  case attr::SubjectMatchRule_variable:
    return isa<VarDecl>(D);
  case attr::SubjectMatchRule_variable_is_thread_local:
    return isTLSVar(D);
  case attr::SubjectMatchRule_variable_is_global:
    return isGlobalVar(D);
  case attr::SubjectMatchRule_variable_is_local:
    return isLocalVar(D);
  case attr::SubjectMatchRule_variable_is_parameter:
    return isa<ParmVarDecl>(D);
  case attr::SubjectMatchRule_variable_not_is_parameter:
    return isNonParmVar(D);
  }
  llvm_unreachable("Invalid match rule");
  return false;
}

#elif defined(WANT_DECL_MERGE_LOGIC)

static bool DiagnoseMutualExclusions(Sema &S, const NamedDecl *D,
                                     const Attr *A) {
  if (const auto *Second = dyn_cast<AlwaysDestroyAttr>(A)) {
    if (const auto *First = D->getAttr<NoDestroyAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<AlwaysInlineAttr>(A)) {
    if (const auto *First = D->getAttr<NotTailCalledAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<ArmNewZAAttr>(A)) {
    if (const auto *First = D->getAttr<ArmSharedZAAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<ArmPreservesZAAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<CPUDispatchAttr>(A)) {
    if (const auto *First = D->getAttr<TargetClonesAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<TargetVersionAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<TargetAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<CPUSpecificAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<CPUSpecificAttr>(A)) {
    if (const auto *First = D->getAttr<TargetClonesAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<TargetVersionAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<TargetAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<CPUDispatchAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<ColdAttr>(A)) {
    if (const auto *First = D->getAttr<HotAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<CommonAttr>(A)) {
    if (const auto *First = D->getAttr<InternalLinkageAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<DisableTailCallsAttr>(A)) {
    if (const auto *First = D->getAttr<NakedAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<HotAttr>(A)) {
    if (const auto *First = D->getAttr<ColdAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<InternalLinkageAttr>(A)) {
    if (const auto *First = D->getAttr<CommonAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<NakedAttr>(A)) {
    if (const auto *First = D->getAttr<DisableTailCallsAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<NoDestroyAttr>(A)) {
    if (const auto *First = D->getAttr<AlwaysDestroyAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<NoRandomizeLayoutAttr>(A)) {
    if (const auto *First = D->getAttr<RandomizeLayoutAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<NoSpeculativeLoadHardeningAttr>(A)) {
    if (const auto *First = D->getAttr<SpeculativeLoadHardeningAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<NotTailCalledAttr>(A)) {
    if (const auto *First = D->getAttr<AlwaysInlineAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<RandomizeLayoutAttr>(A)) {
    if (const auto *First = D->getAttr<NoRandomizeLayoutAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<SpeculativeLoadHardeningAttr>(A)) {
    if (const auto *First = D->getAttr<NoSpeculativeLoadHardeningAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<TargetAttr>(A)) {
    if (const auto *First = D->getAttr<TargetClonesAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<TargetVersionAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<CPUDispatchAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<CPUSpecificAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<TargetClonesAttr>(A)) {
    if (const auto *First = D->getAttr<TargetVersionAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<TargetAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<CPUDispatchAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<CPUSpecificAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  if (const auto *Second = dyn_cast<TargetVersionAttr>(A)) {
    if (const auto *First = D->getAttr<TargetClonesAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<TargetAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<CPUDispatchAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    if (const auto *First = D->getAttr<CPUSpecificAttr>()) {
      S.Diag(First->getLocation(), diag::err_attributes_are_not_compatible)
          << First << Second
          << (First->isRegularKeywordAttribute() ||
              Second->isRegularKeywordAttribute());
      S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
      return false;
    }
    return true;
  }
  return true;
}

#elif defined(WANT_STMT_MERGE_LOGIC)

static bool DiagnoseMutualExclusions(Sema &S,
                                     const SmallVectorImpl<const Attr *> &C) {
  for (const Attr *A : C) {
    if (const auto *Second = dyn_cast<AlwaysInlineAttr>(A)) {
      auto Iter = llvm::find_if(
          C, [](const Attr *Check) { return isa<NotTailCalledAttr>(Check); });
      if (Iter != C.end()) {
        S.Diag((*Iter)->getLocation(), diag::err_attributes_are_not_compatible)
            << *Iter << Second
            << ((*Iter)->isRegularKeywordAttribute() ||
                Second->isRegularKeywordAttribute());
        S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
        return false;
      }
    }
    if (const auto *Second = dyn_cast<LikelyAttr>(A)) {
      auto Iter = llvm::find_if(
          C, [](const Attr *Check) { return isa<UnlikelyAttr>(Check); });
      if (Iter != C.end()) {
        S.Diag((*Iter)->getLocation(), diag::err_attributes_are_not_compatible)
            << *Iter << Second
            << ((*Iter)->isRegularKeywordAttribute() ||
                Second->isRegularKeywordAttribute());
        S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
        return false;
      }
    }
    if (const auto *Second = dyn_cast<UnlikelyAttr>(A)) {
      auto Iter = llvm::find_if(
          C, [](const Attr *Check) { return isa<LikelyAttr>(Check); });
      if (Iter != C.end()) {
        S.Diag((*Iter)->getLocation(), diag::err_attributes_are_not_compatible)
            << *Iter << Second
            << ((*Iter)->isRegularKeywordAttribute() ||
                Second->isRegularKeywordAttribute());
        S.Diag(Second->getLocation(), diag::note_conflicting_attribute);
        return false;
      }
    }
  }
  return true;
}

#endif
