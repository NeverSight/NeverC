
#ifndef NEVERC_ATTR_CLASSES_INC
#define NEVERC_ATTR_CLASSES_INC

static inline void DelimitAttributeArgument(raw_ostream &OS, bool &IsFirst) {
  if (IsFirst) {
    IsFirst = false;
    OS << "(";
  } else
    OS << ", ";
}
class AArch64SVEPcsAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_aarch64_sve_pcs = 0,
    Bracket_clang_aarch64_sve_pcs = 1,
    Bracket_neverc_aarch64_sve_pcs = 2,
    C23_clang_aarch64_sve_pcs = 3,
    C23_neverc_aarch64_sve_pcs = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AArch64SVEPcsAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static AArch64SVEPcsAttr *Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo);
  static AArch64SVEPcsAttr *CreateImplicit(TreeContext &Ctx,
                                           SourceRange Range = {},
                                           Spelling S = GNU_aarch64_sve_pcs);
  static AArch64SVEPcsAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                   Spelling S = GNU_aarch64_sve_pcs);

  // Constructors
  AArch64SVEPcsAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  AArch64SVEPcsAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AArch64SVEPcs;
  }
};

class AArch64VectorPcsAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_aarch64_vector_pcs = 0,
    Bracket_clang_aarch64_vector_pcs = 1,
    Bracket_neverc_aarch64_vector_pcs = 2,
    C23_clang_aarch64_vector_pcs = 3,
    C23_neverc_aarch64_vector_pcs = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AArch64VectorPcsAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static AArch64VectorPcsAttr *Create(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static AArch64VectorPcsAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_aarch64_vector_pcs);
  static AArch64VectorPcsAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_aarch64_vector_pcs);

  // Constructors
  AArch64VectorPcsAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  AArch64VectorPcsAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AArch64VectorPcs;
  }
};

class AcquireHandleAttr : public InheritableAttr {
  unsigned handleTypeLength;
  char *handleType;

public:
  enum Spelling {
    GNU_acquire_handle = 0,
    Bracket_clang_acquire_handle = 1,
    Bracket_neverc_acquire_handle = 2,
    C23_clang_acquire_handle = 3,
    C23_neverc_acquire_handle = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AcquireHandleAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef HandleType,
                 const AttributeCommonInfo &CommonInfo);
  static AcquireHandleAttr *Create(TreeContext &Ctx, llvm::StringRef HandleType,
                                   const AttributeCommonInfo &CommonInfo);
  static AcquireHandleAttr *CreateImplicit(TreeContext &Ctx,
                                           llvm::StringRef HandleType,
                                           SourceRange Range = {},
                                           Spelling S = GNU_acquire_handle);
  static AcquireHandleAttr *Create(TreeContext &Ctx, llvm::StringRef HandleType,
                                   SourceRange Range = {},
                                   Spelling S = GNU_acquire_handle);

  // Constructors
  AcquireHandleAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                    llvm::StringRef HandleType);

  AcquireHandleAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getHandleType() const {
    return llvm::StringRef(handleType, handleTypeLength);
  }
  unsigned getHandleTypeLength() const { return handleTypeLength; }
  void setHandleType(TreeContext &C, llvm::StringRef S) {
    handleTypeLength = S.size();
    this->handleType = new (C, 1) char[handleTypeLength];
    if (!S.empty())
      std::memcpy(this->handleType, S.data(), handleTypeLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AcquireHandle;
  }
};

class AddressSpaceAttr : public TypeAttr {
  int addressSpace;

public:
  enum Spelling {
    GNU_address_space = 0,
    Bracket_clang_address_space = 1,
    Bracket_neverc_address_space = 2,
    C23_clang_address_space = 3,
    C23_neverc_address_space = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AddressSpaceAttr *
  CreateImplicit(TreeContext &Ctx, int AddressSpace,
                 const AttributeCommonInfo &CommonInfo);
  static AddressSpaceAttr *Create(TreeContext &Ctx, int AddressSpace,
                                  const AttributeCommonInfo &CommonInfo);
  static AddressSpaceAttr *CreateImplicit(TreeContext &Ctx, int AddressSpace,
                                          SourceRange Range = {},
                                          Spelling S = GNU_address_space);
  static AddressSpaceAttr *Create(TreeContext &Ctx, int AddressSpace,
                                  SourceRange Range = {},
                                  Spelling S = GNU_address_space);

  // Constructors
  AddressSpaceAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                   int AddressSpace);

  AddressSpaceAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  int getAddressSpace() const { return addressSpace; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AddressSpace;
  }
};

class AliasAttr : public Attr {
  unsigned aliaseeLength;
  char *aliasee;

public:
  enum Spelling {
    GNU_alias = 0,
    Bracket_gnu_alias = 1,
    C23_gnu_alias = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AliasAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Aliasee,
                                   const AttributeCommonInfo &CommonInfo);
  static AliasAttr *Create(TreeContext &Ctx, llvm::StringRef Aliasee,
                           const AttributeCommonInfo &CommonInfo);
  static AliasAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Aliasee,
                                   SourceRange Range = {},
                                   Spelling S = GNU_alias);
  static AliasAttr *Create(TreeContext &Ctx, llvm::StringRef Aliasee,
                           SourceRange Range = {}, Spelling S = GNU_alias);

  // Constructors
  AliasAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
            llvm::StringRef Aliasee);

  AliasAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getAliasee() const {
    return llvm::StringRef(aliasee, aliaseeLength);
  }
  unsigned getAliaseeLength() const { return aliaseeLength; }
  void setAliasee(TreeContext &C, llvm::StringRef S) {
    aliaseeLength = S.size();
    this->aliasee = new (C, 1) char[aliaseeLength];
    if (!S.empty())
      std::memcpy(this->aliasee, S.data(), aliaseeLength);
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::Alias; }
};

class AlignValueAttr : public Attr {
  Expr *alignment;

public:
  // Factory methods
  static AlignValueAttr *CreateImplicit(TreeContext &Ctx, Expr *Alignment,
                                        const AttributeCommonInfo &CommonInfo);
  static AlignValueAttr *Create(TreeContext &Ctx, Expr *Alignment,
                                const AttributeCommonInfo &CommonInfo);
  static AlignValueAttr *CreateImplicit(TreeContext &Ctx, Expr *Alignment,
                                        SourceRange Range = {});
  static AlignValueAttr *Create(TreeContext &Ctx, Expr *Alignment,
                                SourceRange Range = {});

  // Constructors
  AlignValueAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                 Expr *Alignment);

  AlignValueAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Expr *getAlignment() const { return alignment; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AlignValue;
  }
};

class AlignedAttr : public InheritableAttr {
  bool isalignmentExpr;
  union {
    Expr *alignmentExpr;
    TypeSourceInfo *alignmentType;
  };
  std::optional<unsigned> alignmentCache;

public:
  enum Spelling {
    GNU_aligned = 0,
    Bracket_gnu_aligned = 1,
    C23_gnu_aligned = 2,
    Declspec_align = 3,
    Keyword_alignas = 4,
    Keyword_Alignas = 5,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AlignedAttr *CreateImplicit(TreeContext &Ctx, bool IsAlignmentExpr,
                                     void *Alignment,
                                     const AttributeCommonInfo &CommonInfo);
  static AlignedAttr *Create(TreeContext &Ctx, bool IsAlignmentExpr,
                             void *Alignment,
                             const AttributeCommonInfo &CommonInfo);
  static AlignedAttr *CreateImplicit(TreeContext &Ctx, bool IsAlignmentExpr,
                                     void *Alignment, SourceRange Range = {},
                                     Spelling S = GNU_aligned);
  static AlignedAttr *Create(TreeContext &Ctx, bool IsAlignmentExpr,
                             void *Alignment, SourceRange Range = {},
                             Spelling S = GNU_aligned);

  // Constructors
  AlignedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
              bool IsAlignmentExpr, void *Alignment);
  AlignedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  AlignedAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Spelling getSemanticSpelling() const;
  bool isGNU() const {
    return getAttributeSpellingListIndex() == 0 ||
           getAttributeSpellingListIndex() == 1 ||
           getAttributeSpellingListIndex() == 2;
  }
  bool isC11() const { return getAttributeSpellingListIndex() == 5; }
  bool isAlignas() const {
    return getAttributeSpellingListIndex() == 4 ||
           getAttributeSpellingListIndex() == 5;
  }
  bool isDeclspec() const { return getAttributeSpellingListIndex() == 3; }
  bool isAlignmentDependent() const;
  bool isAlignmentErrorDependent() const;
  unsigned getAlignment(TreeContext &Ctx) const;
  bool isAlignmentExpr() const { return isalignmentExpr; }
  Expr *getAlignmentExpr() const {
    assert(isalignmentExpr);
    return alignmentExpr;
  }
  TypeSourceInfo *getAlignmentType() const {
    assert(!isalignmentExpr);
    return alignmentType;
  }
  std::optional<unsigned> getCachedAlignmentValue() const {
    return alignmentCache;
  }
  void setCachedAlignmentValue(unsigned AlignVal) { alignmentCache = AlignVal; }

  static bool classof(const Attr *A) { return A->getKind() == attr::Aligned; }
};

class AllocAlignAttr : public InheritableAttr {
  ParamIdx paramIndex;

public:
  enum Spelling {
    GNU_alloc_align = 0,
    Bracket_gnu_alloc_align = 1,
    C23_gnu_alloc_align = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AllocAlignAttr *CreateImplicit(TreeContext &Ctx, ParamIdx ParamIndex,
                                        const AttributeCommonInfo &CommonInfo);
  static AllocAlignAttr *Create(TreeContext &Ctx, ParamIdx ParamIndex,
                                const AttributeCommonInfo &CommonInfo);
  static AllocAlignAttr *CreateImplicit(TreeContext &Ctx, ParamIdx ParamIndex,
                                        SourceRange Range = {},
                                        Spelling S = GNU_alloc_align);
  static AllocAlignAttr *Create(TreeContext &Ctx, ParamIdx ParamIndex,
                                SourceRange Range = {},
                                Spelling S = GNU_alloc_align);

  // Constructors
  AllocAlignAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                 ParamIdx ParamIndex);

  AllocAlignAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  ParamIdx getParamIndex() const { return paramIndex; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AllocAlign;
  }
};

class AllocSizeAttr : public InheritableAttr {
  ParamIdx elemSizeParam;

  ParamIdx numElemsParam;

public:
  enum Spelling {
    GNU_alloc_size = 0,
    Bracket_gnu_alloc_size = 1,
    C23_gnu_alloc_size = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AllocSizeAttr *CreateImplicit(TreeContext &Ctx, ParamIdx ElemSizeParam,
                                       ParamIdx NumElemsParam,
                                       const AttributeCommonInfo &CommonInfo);
  static AllocSizeAttr *Create(TreeContext &Ctx, ParamIdx ElemSizeParam,
                               ParamIdx NumElemsParam,
                               const AttributeCommonInfo &CommonInfo);
  static AllocSizeAttr *CreateImplicit(TreeContext &Ctx, ParamIdx ElemSizeParam,
                                       ParamIdx NumElemsParam,
                                       SourceRange Range = {},
                                       Spelling S = GNU_alloc_size);
  static AllocSizeAttr *Create(TreeContext &Ctx, ParamIdx ElemSizeParam,
                               ParamIdx NumElemsParam, SourceRange Range = {},
                               Spelling S = GNU_alloc_size);

  // Constructors
  AllocSizeAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                ParamIdx ElemSizeParam, ParamIdx NumElemsParam);
  AllocSizeAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                ParamIdx ElemSizeParam);

  AllocSizeAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  ParamIdx getElemSizeParam() const { return elemSizeParam; }

  ParamIdx getNumElemsParam() const { return numElemsParam; }

  static bool classof(const Attr *A) { return A->getKind() == attr::AllocSize; }
};

class AlwaysDestroyAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_always_destroy = 0,
    Bracket_clang_always_destroy = 1,
    Bracket_neverc_always_destroy = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AlwaysDestroyAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static AlwaysDestroyAttr *Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo);
  static AlwaysDestroyAttr *CreateImplicit(TreeContext &Ctx,
                                           SourceRange Range = {},
                                           Spelling S = GNU_always_destroy);
  static AlwaysDestroyAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                   Spelling S = GNU_always_destroy);

  // Constructors
  AlwaysDestroyAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  AlwaysDestroyAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AlwaysDestroy;
  }
};

class AlwaysInlineAttr : public DeclOrStmtAttr {
public:
  enum Spelling {
    GNU_always_inline = 0,
    Bracket_gnu_always_inline = 1,
    C23_gnu_always_inline = 2,
    Bracket_neverc_always_inline = 3,
    C23_neverc_always_inline = 4,
    Keyword_forceinline = 5,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AlwaysInlineAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static AlwaysInlineAttr *Create(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static AlwaysInlineAttr *CreateImplicit(TreeContext &Ctx,
                                          SourceRange Range = {},
                                          Spelling S = GNU_always_inline);
  static AlwaysInlineAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                  Spelling S = GNU_always_inline);

  // Constructors
  AlwaysInlineAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  AlwaysInlineAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Spelling getSemanticSpelling() const;
  bool isNeverCAlwaysInline() const {
    return getAttributeSpellingListIndex() == 3 ||
           getAttributeSpellingListIndex() == 4;
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AlwaysInline;
  }
};

class AnalyzerNoReturnAttr : public InheritableAttr {
public:
  // Factory methods
  static AnalyzerNoReturnAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static AnalyzerNoReturnAttr *Create(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static AnalyzerNoReturnAttr *CreateImplicit(TreeContext &Ctx,
                                              SourceRange Range = {});
  static AnalyzerNoReturnAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  AnalyzerNoReturnAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  AnalyzerNoReturnAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AnalyzerNoReturn;
  }
};

class AnnotateAttr : public InheritableParamAttr {
  unsigned annotationLength;
  char *annotation;

  unsigned args_Size;
  Expr **args_;

public:
  enum Spelling {
    GNU_annotate = 0,
    Bracket_clang_annotate = 1,
    Bracket_neverc_annotate = 2,
    C23_clang_annotate = 3,
    C23_neverc_annotate = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AnnotateAttr *CreateImplicit(TreeContext &Ctx,
                                      llvm::StringRef Annotation, Expr **Args,
                                      unsigned ArgsSize,
                                      const AttributeCommonInfo &CommonInfo);
  static AnnotateAttr *Create(TreeContext &Ctx, llvm::StringRef Annotation,
                              Expr **Args, unsigned ArgsSize,
                              const AttributeCommonInfo &CommonInfo);
  static AnnotateAttr *CreateImplicit(TreeContext &Ctx,
                                      llvm::StringRef Annotation, Expr **Args,
                                      unsigned ArgsSize, SourceRange Range = {},
                                      Spelling S = GNU_annotate);
  static AnnotateAttr *Create(TreeContext &Ctx, llvm::StringRef Annotation,
                              Expr **Args, unsigned ArgsSize,
                              SourceRange Range = {},
                              Spelling S = GNU_annotate);

  // Constructors
  AnnotateAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
               llvm::StringRef Annotation, Expr **Args, unsigned ArgsSize);
  AnnotateAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
               llvm::StringRef Annotation);

  AnnotateAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getAnnotation() const {
    return llvm::StringRef(annotation, annotationLength);
  }
  unsigned getAnnotationLength() const { return annotationLength; }
  void setAnnotation(TreeContext &C, llvm::StringRef S) {
    annotationLength = S.size();
    this->annotation = new (C, 1) char[annotationLength];
    if (!S.empty())
      std::memcpy(this->annotation, S.data(), annotationLength);
  }

  typedef Expr **args_iterator;
  args_iterator args_begin() const { return args_; }
  args_iterator args_end() const { return args_ + args_Size; }
  unsigned args_size() const { return args_Size; }
  llvm::iterator_range<args_iterator> args() const {
    return llvm::make_range(args_begin(), args_end());
  }

  static AnnotateAttr *Create(TreeContext &Ctx, llvm::StringRef Annotation,
                              const AttributeCommonInfo &CommonInfo) {
    return AnnotateAttr::Create(Ctx, Annotation, nullptr, 0, CommonInfo);
  }
  static AnnotateAttr *CreateImplicit(TreeContext &Ctx,
                                      llvm::StringRef Annotation,
                                      const AttributeCommonInfo &CommonInfo) {
    return AnnotateAttr::CreateImplicit(Ctx, Annotation, nullptr, 0,
                                        CommonInfo);
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::Annotate; }
};

class AnnotateTypeAttr : public TypeAttr {
  unsigned annotationLength;
  char *annotation;

  unsigned args_Size;
  Expr **args_;

public:
  enum Spelling {
    Bracket_neverc_annotate_type = 0,
    C23_neverc_annotate_type = 1,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AnnotateTypeAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef Annotation, Expr **Args,
                 unsigned ArgsSize, const AttributeCommonInfo &CommonInfo);
  static AnnotateTypeAttr *Create(TreeContext &Ctx, llvm::StringRef Annotation,
                                  Expr **Args, unsigned ArgsSize,
                                  const AttributeCommonInfo &CommonInfo);
  static AnnotateTypeAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef Annotation, Expr **Args,
                 unsigned ArgsSize, SourceRange Range = {},
                 Spelling S = Bracket_neverc_annotate_type);
  static AnnotateTypeAttr *Create(TreeContext &Ctx, llvm::StringRef Annotation,
                                  Expr **Args, unsigned ArgsSize,
                                  SourceRange Range = {},
                                  Spelling S = Bracket_neverc_annotate_type);

  // Constructors
  AnnotateTypeAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                   llvm::StringRef Annotation, Expr **Args, unsigned ArgsSize);
  AnnotateTypeAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                   llvm::StringRef Annotation);

  AnnotateTypeAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getAnnotation() const {
    return llvm::StringRef(annotation, annotationLength);
  }
  unsigned getAnnotationLength() const { return annotationLength; }
  void setAnnotation(TreeContext &C, llvm::StringRef S) {
    annotationLength = S.size();
    this->annotation = new (C, 1) char[annotationLength];
    if (!S.empty())
      std::memcpy(this->annotation, S.data(), annotationLength);
  }

  typedef Expr **args_iterator;
  args_iterator args_begin() const { return args_; }
  args_iterator args_end() const { return args_ + args_Size; }
  unsigned args_size() const { return args_Size; }
  llvm::iterator_range<args_iterator> args() const {
    return llvm::make_range(args_begin(), args_end());
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AnnotateType;
  }
};

class AnyX86InterruptAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_interrupt = 0,
    Bracket_gnu_interrupt = 1,
    C23_gnu_interrupt = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AnyX86InterruptAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static AnyX86InterruptAttr *Create(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static AnyX86InterruptAttr *CreateImplicit(TreeContext &Ctx,
                                             SourceRange Range = {},
                                             Spelling S = GNU_interrupt);
  static AnyX86InterruptAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_interrupt);

  // Constructors
  AnyX86InterruptAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  AnyX86InterruptAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AnyX86Interrupt;
  }
};

class AnyX86NoCallerSavedRegistersAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_no_caller_saved_registers = 0,
    Bracket_gnu_no_caller_saved_registers = 1,
    C23_gnu_no_caller_saved_registers = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AnyX86NoCallerSavedRegistersAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static AnyX86NoCallerSavedRegistersAttr *
  Create(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static AnyX86NoCallerSavedRegistersAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_no_caller_saved_registers);
  static AnyX86NoCallerSavedRegistersAttr *
  Create(TreeContext &Ctx, SourceRange Range = {},
         Spelling S = GNU_no_caller_saved_registers);

  // Constructors
  AnyX86NoCallerSavedRegistersAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo);

  AnyX86NoCallerSavedRegistersAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AnyX86NoCallerSavedRegisters;
  }
};

class AnyX86NoCfCheckAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_nocf_check = 0,
    Bracket_gnu_nocf_check = 1,
    C23_gnu_nocf_check = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AnyX86NoCfCheckAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static AnyX86NoCfCheckAttr *Create(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static AnyX86NoCfCheckAttr *CreateImplicit(TreeContext &Ctx,
                                             SourceRange Range = {},
                                             Spelling S = GNU_nocf_check);
  static AnyX86NoCfCheckAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_nocf_check);

  // Constructors
  AnyX86NoCfCheckAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  AnyX86NoCfCheckAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AnyX86NoCfCheck;
  }
};

class ArgumentWithTypeTagAttr : public InheritableAttr {
  IdentifierInfo *argumentKind;

  ParamIdx argumentIdx;

  ParamIdx typeTagIdx;

  bool isPointer;

public:
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

  // Factory methods
  static ArgumentWithTypeTagAttr *
  CreateImplicit(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
                 ParamIdx ArgumentIdx, ParamIdx TypeTagIdx, bool IsPointer,
                 const AttributeCommonInfo &CommonInfo);
  static ArgumentWithTypeTagAttr *Create(TreeContext &Ctx,
                                         IdentifierInfo *ArgumentKind,
                                         ParamIdx ArgumentIdx,
                                         ParamIdx TypeTagIdx, bool IsPointer,
                                         const AttributeCommonInfo &CommonInfo);
  static ArgumentWithTypeTagAttr *
  CreateImplicit(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
                 ParamIdx ArgumentIdx, ParamIdx TypeTagIdx, bool IsPointer,
                 SourceRange Range = {},
                 Spelling S = GNU_argument_with_type_tag);
  static ArgumentWithTypeTagAttr *
  Create(TreeContext &Ctx, IdentifierInfo *ArgumentKind, ParamIdx ArgumentIdx,
         ParamIdx TypeTagIdx, bool IsPointer, SourceRange Range = {},
         Spelling S = GNU_argument_with_type_tag);
  static ArgumentWithTypeTagAttr *
  CreateImplicit(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
                 ParamIdx ArgumentIdx, ParamIdx TypeTagIdx,
                 const AttributeCommonInfo &CommonInfo);
  static ArgumentWithTypeTagAttr *
  Create(TreeContext &Ctx, IdentifierInfo *ArgumentKind, ParamIdx ArgumentIdx,
         ParamIdx TypeTagIdx, const AttributeCommonInfo &CommonInfo);
  static ArgumentWithTypeTagAttr *
  CreateImplicit(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
                 ParamIdx ArgumentIdx, ParamIdx TypeTagIdx,
                 SourceRange Range = {},
                 Spelling S = GNU_argument_with_type_tag);
  static ArgumentWithTypeTagAttr *
  Create(TreeContext &Ctx, IdentifierInfo *ArgumentKind, ParamIdx ArgumentIdx,
         ParamIdx TypeTagIdx, SourceRange Range = {},
         Spelling S = GNU_argument_with_type_tag);

  // Constructors
  ArgumentWithTypeTagAttr(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo,
                          IdentifierInfo *ArgumentKind, ParamIdx ArgumentIdx,
                          ParamIdx TypeTagIdx, bool IsPointer);
  ArgumentWithTypeTagAttr(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo,
                          IdentifierInfo *ArgumentKind, ParamIdx ArgumentIdx,
                          ParamIdx TypeTagIdx);

  ArgumentWithTypeTagAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Spelling getSemanticSpelling() const;
  IdentifierInfo *getArgumentKind() const { return argumentKind; }

  ParamIdx getArgumentIdx() const { return argumentIdx; }

  ParamIdx getTypeTagIdx() const { return typeTagIdx; }

  bool getIsPointer() const { return isPointer; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::ArgumentWithTypeTag;
  }
};

class ArmBuiltinAliasAttr : public InheritableAttr {
  IdentifierInfo *builtinName;

public:
  enum Spelling {
    GNU_neverc_arm_builtin_alias = 0,
    Bracket_clang_neverc_arm_builtin_alias = 1,
    Bracket_neverc_neverc_arm_builtin_alias = 2,
    C23_clang_neverc_arm_builtin_alias = 3,
    C23_neverc_neverc_arm_builtin_alias = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ArmBuiltinAliasAttr *
  CreateImplicit(TreeContext &Ctx, IdentifierInfo *BuiltinName,
                 const AttributeCommonInfo &CommonInfo);
  static ArmBuiltinAliasAttr *Create(TreeContext &Ctx,
                                     IdentifierInfo *BuiltinName,
                                     const AttributeCommonInfo &CommonInfo);
  static ArmBuiltinAliasAttr *
  CreateImplicit(TreeContext &Ctx, IdentifierInfo *BuiltinName,
                 SourceRange Range = {},
                 Spelling S = GNU_neverc_arm_builtin_alias);
  static ArmBuiltinAliasAttr *Create(TreeContext &Ctx,
                                     IdentifierInfo *BuiltinName,
                                     SourceRange Range = {},
                                     Spelling S = GNU_neverc_arm_builtin_alias);

  // Constructors
  ArmBuiltinAliasAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                      IdentifierInfo *BuiltinName);

  ArmBuiltinAliasAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  IdentifierInfo *getBuiltinName() const { return builtinName; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::ArmBuiltinAlias;
  }
};

class ArmLocallyStreamingAttr : public InheritableAttr {
public:
  // Factory methods
  static ArmLocallyStreamingAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static ArmLocallyStreamingAttr *Create(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo);
  static ArmLocallyStreamingAttr *CreateImplicit(TreeContext &Ctx,
                                                 SourceRange Range = {});
  static ArmLocallyStreamingAttr *Create(TreeContext &Ctx,
                                         SourceRange Range = {});

  // Constructors
  ArmLocallyStreamingAttr(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo);

  ArmLocallyStreamingAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::ArmLocallyStreaming;
  }
};

class ArmNewZAAttr : public InheritableAttr {
public:
  // Factory methods
  static ArmNewZAAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static ArmNewZAAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static ArmNewZAAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {});
  static ArmNewZAAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  ArmNewZAAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  ArmNewZAAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::ArmNewZA; }
};

class ArmPreservesZAAttr : public TypeAttr {
public:
  // Factory methods
  static ArmPreservesZAAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static ArmPreservesZAAttr *Create(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo);
  static ArmPreservesZAAttr *CreateImplicit(TreeContext &Ctx,
                                            SourceRange Range = {});
  static ArmPreservesZAAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  ArmPreservesZAAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  ArmPreservesZAAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::ArmPreservesZA;
  }
};

class ArmSharedZAAttr : public TypeAttr {
public:
  // Factory methods
  static ArmSharedZAAttr *CreateImplicit(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo);
  static ArmSharedZAAttr *Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo);
  static ArmSharedZAAttr *CreateImplicit(TreeContext &Ctx,
                                         SourceRange Range = {});
  static ArmSharedZAAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  ArmSharedZAAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  ArmSharedZAAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::ArmSharedZA;
  }
};

class ArmStreamingAttr : public TypeAttr {
public:
  // Factory methods
  static ArmStreamingAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static ArmStreamingAttr *Create(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static ArmStreamingAttr *CreateImplicit(TreeContext &Ctx,
                                          SourceRange Range = {});
  static ArmStreamingAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  ArmStreamingAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  ArmStreamingAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::ArmStreaming;
  }
};

class ArmStreamingCompatibleAttr : public TypeAttr {
public:
  // Factory methods
  static ArmStreamingCompatibleAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static ArmStreamingCompatibleAttr *
  Create(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static ArmStreamingCompatibleAttr *CreateImplicit(TreeContext &Ctx,
                                                    SourceRange Range = {});
  static ArmStreamingCompatibleAttr *Create(TreeContext &Ctx,
                                            SourceRange Range = {});

  // Constructors
  ArmStreamingCompatibleAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo);

  ArmStreamingCompatibleAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::ArmStreamingCompatible;
  }
};

class ArtificialAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_artificial = 0,
    Bracket_gnu_artificial = 1,
    C23_gnu_artificial = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ArtificialAttr *CreateImplicit(TreeContext &Ctx,
                                        const AttributeCommonInfo &CommonInfo);
  static ArtificialAttr *Create(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo);
  static ArtificialAttr *CreateImplicit(TreeContext &Ctx,
                                        SourceRange Range = {},
                                        Spelling S = GNU_artificial);
  static ArtificialAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                Spelling S = GNU_artificial);

  // Constructors
  ArtificialAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  ArtificialAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::Artificial;
  }
};

class AsmLabelAttr : public InheritableAttr {
  unsigned labelLength;
  char *label;

  bool isLiteralLabel;

public:
  enum Spelling {
    Keyword_asm = 0,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AsmLabelAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Label,
                                      bool IsLiteralLabel,
                                      const AttributeCommonInfo &CommonInfo);
  static AsmLabelAttr *Create(TreeContext &Ctx, llvm::StringRef Label,
                              bool IsLiteralLabel,
                              const AttributeCommonInfo &CommonInfo);
  static AsmLabelAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Label,
                                      bool IsLiteralLabel,
                                      SourceRange Range = {},
                                      Spelling S = Keyword_asm);
  static AsmLabelAttr *Create(TreeContext &Ctx, llvm::StringRef Label,
                              bool IsLiteralLabel, SourceRange Range = {},
                              Spelling S = Keyword_asm);
  static AsmLabelAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Label,
                                      const AttributeCommonInfo &CommonInfo);
  static AsmLabelAttr *Create(TreeContext &Ctx, llvm::StringRef Label,
                              const AttributeCommonInfo &CommonInfo);
  static AsmLabelAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Label,
                                      SourceRange Range = {},
                                      Spelling S = Keyword_asm);
  static AsmLabelAttr *Create(TreeContext &Ctx, llvm::StringRef Label,
                              SourceRange Range = {}, Spelling S = Keyword_asm);

  // Constructors
  AsmLabelAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
               llvm::StringRef Label, bool IsLiteralLabel);
  AsmLabelAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
               llvm::StringRef Label);

  AsmLabelAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getLabel() const {
    return llvm::StringRef(label, labelLength);
  }
  unsigned getLabelLength() const { return labelLength; }
  void setLabel(TreeContext &C, llvm::StringRef S) {
    labelLength = S.size();
    this->label = new (C, 1) char[labelLength];
    if (!S.empty())
      std::memcpy(this->label, S.data(), labelLength);
  }

  bool getIsLiteralLabel() const { return isLiteralLabel; }

  bool isEquivalent(AsmLabelAttr *Other) const {
    return getLabel() == Other->getLabel() &&
           getIsLiteralLabel() == Other->getIsLiteralLabel();
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::AsmLabel; }
};

class AssumeAlignedAttr : public InheritableAttr {
  Expr *alignment;

  Expr *offset;

public:
  enum Spelling {
    GNU_assume_aligned = 0,
    Bracket_gnu_assume_aligned = 1,
    C23_gnu_assume_aligned = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AssumeAlignedAttr *
  CreateImplicit(TreeContext &Ctx, Expr *Alignment, Expr *Offset,
                 const AttributeCommonInfo &CommonInfo);
  static AssumeAlignedAttr *Create(TreeContext &Ctx, Expr *Alignment,
                                   Expr *Offset,
                                   const AttributeCommonInfo &CommonInfo);
  static AssumeAlignedAttr *CreateImplicit(TreeContext &Ctx, Expr *Alignment,
                                           Expr *Offset, SourceRange Range = {},
                                           Spelling S = GNU_assume_aligned);
  static AssumeAlignedAttr *Create(TreeContext &Ctx, Expr *Alignment,
                                   Expr *Offset, SourceRange Range = {},
                                   Spelling S = GNU_assume_aligned);

  // Constructors
  AssumeAlignedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                    Expr *Alignment, Expr *Offset);
  AssumeAlignedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                    Expr *Alignment);

  AssumeAlignedAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Expr *getAlignment() const { return alignment; }

  Expr *getOffset() const { return offset; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AssumeAligned;
  }
};

class AssumptionAttr : public InheritableAttr {
  unsigned assumptionLength;
  char *assumption;

public:
  enum Spelling {
    GNU_assume = 0,
    Bracket_clang_assume = 1,
    Bracket_neverc_assume = 2,
    C23_clang_assume = 3,
    C23_neverc_assume = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AssumptionAttr *CreateImplicit(TreeContext &Ctx,
                                        llvm::StringRef Assumption,
                                        const AttributeCommonInfo &CommonInfo);
  static AssumptionAttr *Create(TreeContext &Ctx, llvm::StringRef Assumption,
                                const AttributeCommonInfo &CommonInfo);
  static AssumptionAttr *CreateImplicit(TreeContext &Ctx,
                                        llvm::StringRef Assumption,
                                        SourceRange Range = {},
                                        Spelling S = GNU_assume);
  static AssumptionAttr *Create(TreeContext &Ctx, llvm::StringRef Assumption,
                                SourceRange Range = {},
                                Spelling S = GNU_assume);

  // Constructors
  AssumptionAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                 llvm::StringRef Assumption);

  AssumptionAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getAssumption() const {
    return llvm::StringRef(assumption, assumptionLength);
  }
  unsigned getAssumptionLength() const { return assumptionLength; }
  void setAssumption(TreeContext &C, llvm::StringRef S) {
    assumptionLength = S.size();
    this->assumption = new (C, 1) char[assumptionLength];
    if (!S.empty())
      std::memcpy(this->assumption, S.data(), assumptionLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::Assumption;
  }
};

class AvailabilityAttr : public InheritableAttr {
  IdentifierInfo *platform;

  VersionTuple introduced;

  VersionTuple deprecated;

  VersionTuple obsoleted;

  bool unavailable;

  unsigned messageLength;
  char *message;

  bool strict;

  unsigned replacementLength;
  char *replacement;

  int priority;

public:
  enum Spelling {
    GNU_availability = 0,
    Bracket_clang_availability = 1,
    Bracket_neverc_availability = 2,
    C23_clang_availability = 3,
    C23_neverc_availability = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AvailabilityAttr *CreateImplicit(
      TreeContext &Ctx, IdentifierInfo *Platform, VersionTuple Introduced,
      VersionTuple Deprecated, VersionTuple Obsoleted, bool Unavailable,
      llvm::StringRef Message, bool Strict, llvm::StringRef Replacement,
      int Priority, const AttributeCommonInfo &CommonInfo);
  static AvailabilityAttr *
  Create(TreeContext &Ctx, IdentifierInfo *Platform, VersionTuple Introduced,
         VersionTuple Deprecated, VersionTuple Obsoleted, bool Unavailable,
         llvm::StringRef Message, bool Strict, llvm::StringRef Replacement,
         int Priority, const AttributeCommonInfo &CommonInfo);
  static AvailabilityAttr *CreateImplicit(
      TreeContext &Ctx, IdentifierInfo *Platform, VersionTuple Introduced,
      VersionTuple Deprecated, VersionTuple Obsoleted, bool Unavailable,
      llvm::StringRef Message, bool Strict, llvm::StringRef Replacement,
      int Priority, SourceRange Range = {}, Spelling S = GNU_availability);
  static AvailabilityAttr *
  Create(TreeContext &Ctx, IdentifierInfo *Platform, VersionTuple Introduced,
         VersionTuple Deprecated, VersionTuple Obsoleted, bool Unavailable,
         llvm::StringRef Message, bool Strict, llvm::StringRef Replacement,
         int Priority, SourceRange Range = {}, Spelling S = GNU_availability);

  // Constructors
  AvailabilityAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                   IdentifierInfo *Platform, VersionTuple Introduced,
                   VersionTuple Deprecated, VersionTuple Obsoleted,
                   bool Unavailable, llvm::StringRef Message, bool Strict,
                   llvm::StringRef Replacement, int Priority);

  AvailabilityAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  IdentifierInfo *getPlatform() const { return platform; }

  VersionTuple getIntroduced() const { return introduced; }
  void setIntroduced(TreeContext &C, VersionTuple V) { introduced = V; }

  VersionTuple getDeprecated() const { return deprecated; }
  void setDeprecated(TreeContext &C, VersionTuple V) { deprecated = V; }

  VersionTuple getObsoleted() const { return obsoleted; }
  void setObsoleted(TreeContext &C, VersionTuple V) { obsoleted = V; }

  bool getUnavailable() const { return unavailable; }

  llvm::StringRef getMessage() const {
    return llvm::StringRef(message, messageLength);
  }
  unsigned getMessageLength() const { return messageLength; }
  void setMessage(TreeContext &C, llvm::StringRef S) {
    messageLength = S.size();
    this->message = new (C, 1) char[messageLength];
    if (!S.empty())
      std::memcpy(this->message, S.data(), messageLength);
  }

  bool getStrict() const { return strict; }

  llvm::StringRef getReplacement() const {
    return llvm::StringRef(replacement, replacementLength);
  }
  unsigned getReplacementLength() const { return replacementLength; }
  void setReplacement(TreeContext &C, llvm::StringRef S) {
    replacementLength = S.size();
    this->replacement = new (C, 1) char[replacementLength];
    if (!S.empty())
      std::memcpy(this->replacement, S.data(), replacementLength);
  }

  int getPriority() const { return priority; }

  static llvm::StringRef getPrettyPlatformName(llvm::StringRef Platform) {
    return llvm::StringSwitch<llvm::StringRef>(Platform)
        .Case("android", "Android")
        .Case("ios", "iOS")
        .Case("macos", "macOS")
        .Case("ios_app_extension", "iOS (App Extension)")
        .Case("macos_app_extension", "macOS (App Extension)")
        .Default(llvm::StringRef());
  }
  static llvm::StringRef
  getPlatformNameSourceSpelling(llvm::StringRef Platform) {
    return llvm::StringSwitch<llvm::StringRef>(Platform)
        .Case("ios", "iOS")
        .Case("macos", "macOS")
        .Case("ios_app_extension", "iOSApplicationExtension")
        .Case("macos_app_extension", "macOSApplicationExtension")
        .Default(Platform);
  }
  static llvm::StringRef canonicalizePlatformName(llvm::StringRef Platform) {
    return llvm::StringSwitch<llvm::StringRef>(Platform)
        .Case("iOS", "ios")
        .Case("macOS", "macos")
        .Case("iOSApplicationExtension", "ios_app_extension")
        .Case("macOSApplicationExtension", "macos_app_extension")
        .Default(Platform);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::Availability;
  }
};

class AvailableOnlyInDefaultEvalMethodAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_available_only_in_default_eval_method = 0,
    Bracket_clang_available_only_in_default_eval_method = 1,
    Bracket_neverc_available_only_in_default_eval_method = 2,
    C23_clang_available_only_in_default_eval_method = 3,
    C23_neverc_available_only_in_default_eval_method = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static AvailableOnlyInDefaultEvalMethodAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static AvailableOnlyInDefaultEvalMethodAttr *
  Create(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static AvailableOnlyInDefaultEvalMethodAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_available_only_in_default_eval_method);
  static AvailableOnlyInDefaultEvalMethodAttr *
  Create(TreeContext &Ctx, SourceRange Range = {},
         Spelling S = GNU_available_only_in_default_eval_method);

  // Constructors
  AvailableOnlyInDefaultEvalMethodAttr(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo);

  AvailableOnlyInDefaultEvalMethodAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::AvailableOnlyInDefaultEvalMethod;
  }
};

class BTFDeclTagAttr : public InheritableAttr {
  unsigned bTFDeclTagLength;
  char *bTFDeclTag;

public:
  enum Spelling {
    GNU_btf_decl_tag = 0,
    Bracket_clang_btf_decl_tag = 1,
    Bracket_neverc_btf_decl_tag = 2,
    C23_clang_btf_decl_tag = 3,
    C23_neverc_btf_decl_tag = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static BTFDeclTagAttr *CreateImplicit(TreeContext &Ctx,
                                        llvm::StringRef BTFDeclTag,
                                        const AttributeCommonInfo &CommonInfo);
  static BTFDeclTagAttr *Create(TreeContext &Ctx, llvm::StringRef BTFDeclTag,
                                const AttributeCommonInfo &CommonInfo);
  static BTFDeclTagAttr *CreateImplicit(TreeContext &Ctx,
                                        llvm::StringRef BTFDeclTag,
                                        SourceRange Range = {},
                                        Spelling S = GNU_btf_decl_tag);
  static BTFDeclTagAttr *Create(TreeContext &Ctx, llvm::StringRef BTFDeclTag,
                                SourceRange Range = {},
                                Spelling S = GNU_btf_decl_tag);

  // Constructors
  BTFDeclTagAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                 llvm::StringRef BTFDeclTag);

  BTFDeclTagAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getBTFDeclTag() const {
    return llvm::StringRef(bTFDeclTag, bTFDeclTagLength);
  }
  unsigned getBTFDeclTagLength() const { return bTFDeclTagLength; }
  void setBTFDeclTag(TreeContext &C, llvm::StringRef S) {
    bTFDeclTagLength = S.size();
    this->bTFDeclTag = new (C, 1) char[bTFDeclTagLength];
    if (!S.empty())
      std::memcpy(this->bTFDeclTag, S.data(), bTFDeclTagLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::BTFDeclTag;
  }
};

class BTFTypeTagAttr : public TypeAttr {
  unsigned bTFTypeTagLength;
  char *bTFTypeTag;

public:
  enum Spelling {
    GNU_btf_type_tag = 0,
    Bracket_clang_btf_type_tag = 1,
    Bracket_neverc_btf_type_tag = 2,
    C23_clang_btf_type_tag = 3,
    C23_neverc_btf_type_tag = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static BTFTypeTagAttr *CreateImplicit(TreeContext &Ctx,
                                        llvm::StringRef BTFTypeTag,
                                        const AttributeCommonInfo &CommonInfo);
  static BTFTypeTagAttr *Create(TreeContext &Ctx, llvm::StringRef BTFTypeTag,
                                const AttributeCommonInfo &CommonInfo);
  static BTFTypeTagAttr *CreateImplicit(TreeContext &Ctx,
                                        llvm::StringRef BTFTypeTag,
                                        SourceRange Range = {},
                                        Spelling S = GNU_btf_type_tag);
  static BTFTypeTagAttr *Create(TreeContext &Ctx, llvm::StringRef BTFTypeTag,
                                SourceRange Range = {},
                                Spelling S = GNU_btf_type_tag);

  // Constructors
  BTFTypeTagAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                 llvm::StringRef BTFTypeTag);

  BTFTypeTagAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getBTFTypeTag() const {
    return llvm::StringRef(bTFTypeTag, bTFTypeTagLength);
  }
  unsigned getBTFTypeTagLength() const { return bTFTypeTagLength; }
  void setBTFTypeTag(TreeContext &C, llvm::StringRef S) {
    bTFTypeTagLength = S.size();
    this->bTFTypeTag = new (C, 1) char[bTFTypeTagLength];
    if (!S.empty())
      std::memcpy(this->bTFTypeTag, S.data(), bTFTypeTagLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::BTFTypeTag;
  }
};

class BuiltinAttr : public InheritableAttr {
  unsigned iD;

public:
  // Factory methods
  static BuiltinAttr *CreateImplicit(TreeContext &Ctx, unsigned ID,
                                     const AttributeCommonInfo &CommonInfo);
  static BuiltinAttr *Create(TreeContext &Ctx, unsigned ID,
                             const AttributeCommonInfo &CommonInfo);
  static BuiltinAttr *CreateImplicit(TreeContext &Ctx, unsigned ID,
                                     SourceRange Range = {});
  static BuiltinAttr *Create(TreeContext &Ctx, unsigned ID,
                             SourceRange Range = {});

  // Constructors
  BuiltinAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
              unsigned ID);

  BuiltinAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  unsigned getID() const { return iD; }

  static bool classof(const Attr *A) { return A->getKind() == attr::Builtin; }
};

class BuiltinAliasAttr : public Attr {
  IdentifierInfo *builtinName;

public:
  enum Spelling {
    Bracket_neverc_builtin_alias = 0,
    C23_neverc_builtin_alias = 1,
    GNU_neverc_builtin_alias = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static BuiltinAliasAttr *
  CreateImplicit(TreeContext &Ctx, IdentifierInfo *BuiltinName,
                 const AttributeCommonInfo &CommonInfo);
  static BuiltinAliasAttr *Create(TreeContext &Ctx, IdentifierInfo *BuiltinName,
                                  const AttributeCommonInfo &CommonInfo);
  static BuiltinAliasAttr *
  CreateImplicit(TreeContext &Ctx, IdentifierInfo *BuiltinName,
                 SourceRange Range = {},
                 Spelling S = Bracket_neverc_builtin_alias);
  static BuiltinAliasAttr *Create(TreeContext &Ctx, IdentifierInfo *BuiltinName,
                                  SourceRange Range = {},
                                  Spelling S = Bracket_neverc_builtin_alias);

  // Constructors
  BuiltinAliasAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                   IdentifierInfo *BuiltinName);

  BuiltinAliasAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Spelling getSemanticSpelling() const;
  IdentifierInfo *getBuiltinName() const { return builtinName; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::BuiltinAlias;
  }
};

class C11NoReturnAttr : public InheritableAttr {
public:
  // Factory methods
  static C11NoReturnAttr *CreateImplicit(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo);
  static C11NoReturnAttr *Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo);
  static C11NoReturnAttr *CreateImplicit(TreeContext &Ctx,
                                         SourceRange Range = {});
  static C11NoReturnAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  C11NoReturnAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  C11NoReturnAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::C11NoReturn;
  }
};

class CDeclAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_cdecl = 0,
    Bracket_gnu_cdecl = 1,
    C23_gnu_cdecl = 2,
    Keyword_cdecl = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static CDeclAttr *CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo);
  static CDeclAttr *Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo);
  static CDeclAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                   Spelling S = GNU_cdecl);
  static CDeclAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                           Spelling S = GNU_cdecl);

  // Constructors
  CDeclAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  CDeclAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::CDecl; }
};

class CFGuardAttr : public InheritableAttr {
public:
  enum GuardArg { nocf };

private:
  CFGuardAttr::GuardArg guard;

public:
  enum Spelling {
    Declspec_guard = 0,
    GNU_guard = 1,
    Bracket_clang_guard = 2,
    Bracket_neverc_guard = 3,
    C23_clang_guard = 4,
    C23_neverc_guard = 5,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static CFGuardAttr *CreateImplicit(TreeContext &Ctx,
                                     CFGuardAttr::GuardArg Guard,
                                     const AttributeCommonInfo &CommonInfo);
  static CFGuardAttr *Create(TreeContext &Ctx, CFGuardAttr::GuardArg Guard,
                             const AttributeCommonInfo &CommonInfo);
  static CFGuardAttr *CreateImplicit(TreeContext &Ctx,
                                     CFGuardAttr::GuardArg Guard,
                                     SourceRange Range = {},
                                     Spelling S = Declspec_guard);
  static CFGuardAttr *Create(TreeContext &Ctx, CFGuardAttr::GuardArg Guard,
                             SourceRange Range = {},
                             Spelling S = Declspec_guard);

  // Constructors
  CFGuardAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
              CFGuardAttr::GuardArg Guard);

  CFGuardAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  CFGuardAttr::GuardArg getGuard() const { return guard; }

  static bool ConvertStrToGuardArg(StringRef Val, CFGuardAttr::GuardArg &Out);
  static const char *ConvertGuardArgToStr(CFGuardAttr::GuardArg Val);

  static bool classof(const Attr *A) { return A->getKind() == attr::CFGuard; }
};

class CPUDispatchAttr : public InheritableAttr {
  unsigned cpus_Size;
  IdentifierInfo **cpus_;

public:
  enum Spelling {
    GNU_cpu_dispatch = 0,
    Bracket_clang_cpu_dispatch = 1,
    Bracket_neverc_cpu_dispatch = 2,
    C23_clang_cpu_dispatch = 3,
    C23_neverc_cpu_dispatch = 4,
    Declspec_cpu_dispatch = 5,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static CPUDispatchAttr *CreateImplicit(TreeContext &Ctx,
                                         IdentifierInfo **Cpus,
                                         unsigned CpusSize,
                                         const AttributeCommonInfo &CommonInfo);
  static CPUDispatchAttr *Create(TreeContext &Ctx, IdentifierInfo **Cpus,
                                 unsigned CpusSize,
                                 const AttributeCommonInfo &CommonInfo);
  static CPUDispatchAttr *
  CreateImplicit(TreeContext &Ctx, IdentifierInfo **Cpus, unsigned CpusSize,
                 SourceRange Range = {}, Spelling S = GNU_cpu_dispatch);
  static CPUDispatchAttr *Create(TreeContext &Ctx, IdentifierInfo **Cpus,
                                 unsigned CpusSize, SourceRange Range = {},
                                 Spelling S = GNU_cpu_dispatch);

  // Constructors
  CPUDispatchAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                  IdentifierInfo **Cpus, unsigned CpusSize);
  CPUDispatchAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  CPUDispatchAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  typedef IdentifierInfo **cpus_iterator;
  cpus_iterator cpus_begin() const { return cpus_; }
  cpus_iterator cpus_end() const { return cpus_ + cpus_Size; }
  unsigned cpus_size() const { return cpus_Size; }
  llvm::iterator_range<cpus_iterator> cpus() const {
    return llvm::make_range(cpus_begin(), cpus_end());
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::CPUDispatch;
  }
};

class CPUSpecificAttr : public InheritableAttr {
  unsigned cpus_Size;
  IdentifierInfo **cpus_;

public:
  enum Spelling {
    GNU_cpu_specific = 0,
    Bracket_clang_cpu_specific = 1,
    Bracket_neverc_cpu_specific = 2,
    C23_clang_cpu_specific = 3,
    C23_neverc_cpu_specific = 4,
    Declspec_cpu_specific = 5,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static CPUSpecificAttr *CreateImplicit(TreeContext &Ctx,
                                         IdentifierInfo **Cpus,
                                         unsigned CpusSize,
                                         const AttributeCommonInfo &CommonInfo);
  static CPUSpecificAttr *Create(TreeContext &Ctx, IdentifierInfo **Cpus,
                                 unsigned CpusSize,
                                 const AttributeCommonInfo &CommonInfo);
  static CPUSpecificAttr *
  CreateImplicit(TreeContext &Ctx, IdentifierInfo **Cpus, unsigned CpusSize,
                 SourceRange Range = {}, Spelling S = GNU_cpu_specific);
  static CPUSpecificAttr *Create(TreeContext &Ctx, IdentifierInfo **Cpus,
                                 unsigned CpusSize, SourceRange Range = {},
                                 Spelling S = GNU_cpu_specific);

  // Constructors
  CPUSpecificAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                  IdentifierInfo **Cpus, unsigned CpusSize);
  CPUSpecificAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  CPUSpecificAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  typedef IdentifierInfo **cpus_iterator;
  cpus_iterator cpus_begin() const { return cpus_; }
  cpus_iterator cpus_end() const { return cpus_ + cpus_Size; }
  unsigned cpus_size() const { return cpus_Size; }
  llvm::iterator_range<cpus_iterator> cpus() const {
    return llvm::make_range(cpus_begin(), cpus_end());
  }

  IdentifierInfo *getCPUName(unsigned Index) const {
    return *(cpus_begin() + Index);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::CPUSpecific;
  }
};

class CallbackAttr : public InheritableAttr {
  unsigned encoding_Size;
  int *encoding_;

public:
  enum Spelling {
    GNU_callback = 0,
    Bracket_clang_callback = 1,
    Bracket_neverc_callback = 2,
    C23_clang_callback = 3,
    C23_neverc_callback = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static CallbackAttr *CreateImplicit(TreeContext &Ctx, int *Encoding,
                                      unsigned EncodingSize,
                                      const AttributeCommonInfo &CommonInfo);
  static CallbackAttr *Create(TreeContext &Ctx, int *Encoding,
                              unsigned EncodingSize,
                              const AttributeCommonInfo &CommonInfo);
  static CallbackAttr *CreateImplicit(TreeContext &Ctx, int *Encoding,
                                      unsigned EncodingSize,
                                      SourceRange Range = {},
                                      Spelling S = GNU_callback);
  static CallbackAttr *Create(TreeContext &Ctx, int *Encoding,
                              unsigned EncodingSize, SourceRange Range = {},
                              Spelling S = GNU_callback);

  // Constructors
  CallbackAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
               int *Encoding, unsigned EncodingSize);
  CallbackAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  CallbackAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  typedef int *encoding_iterator;
  encoding_iterator encoding_begin() const { return encoding_; }
  encoding_iterator encoding_end() const { return encoding_ + encoding_Size; }
  unsigned encoding_size() const { return encoding_Size; }
  llvm::iterator_range<encoding_iterator> encoding() const {
    return llvm::make_range(encoding_begin(), encoding_end());
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::Callback; }
};

class CarriesDependencyAttr : public InheritableParamAttr {
public:
  enum Spelling {
    GNU_carries_dependency = 0,
    Bracket_carries_dependency = 1,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static CarriesDependencyAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static CarriesDependencyAttr *Create(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo);
  static CarriesDependencyAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_carries_dependency);
  static CarriesDependencyAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                       Spelling S = GNU_carries_dependency);

  // Constructors
  CarriesDependencyAttr(TreeContext &Ctx,
                        const AttributeCommonInfo &CommonInfo);

  CarriesDependencyAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::CarriesDependency;
  }
};

class CleanupAttr : public InheritableAttr {
  FunctionDecl *functionDecl;

public:
  enum Spelling {
    GNU_cleanup = 0,
    Bracket_gnu_cleanup = 1,
    C23_gnu_cleanup = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static CleanupAttr *CreateImplicit(TreeContext &Ctx,
                                     FunctionDecl *FunctionDecl,
                                     const AttributeCommonInfo &CommonInfo);
  static CleanupAttr *Create(TreeContext &Ctx, FunctionDecl *FunctionDecl,
                             const AttributeCommonInfo &CommonInfo);
  static CleanupAttr *CreateImplicit(TreeContext &Ctx,
                                     FunctionDecl *FunctionDecl,
                                     SourceRange Range = {},
                                     Spelling S = GNU_cleanup);
  static CleanupAttr *Create(TreeContext &Ctx, FunctionDecl *FunctionDecl,
                             SourceRange Range = {}, Spelling S = GNU_cleanup);

  // Constructors
  CleanupAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
              FunctionDecl *FunctionDecl);

  CleanupAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  FunctionDecl *getFunctionDecl() const { return functionDecl; }

  static bool classof(const Attr *A) { return A->getKind() == attr::Cleanup; }
};

class CodeAlignAttr : public StmtAttr {
  Expr *alignment;

public:
  enum Spelling {
    GNU_code_align = 0,
    Bracket_clang_code_align = 1,
    Bracket_neverc_code_align = 2,
    C23_clang_code_align = 3,
    C23_neverc_code_align = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static CodeAlignAttr *CreateImplicit(TreeContext &Ctx, Expr *Alignment,
                                       const AttributeCommonInfo &CommonInfo);
  static CodeAlignAttr *Create(TreeContext &Ctx, Expr *Alignment,
                               const AttributeCommonInfo &CommonInfo);
  static CodeAlignAttr *CreateImplicit(TreeContext &Ctx, Expr *Alignment,
                                       SourceRange Range = {},
                                       Spelling S = GNU_code_align);
  static CodeAlignAttr *Create(TreeContext &Ctx, Expr *Alignment,
                               SourceRange Range = {},
                               Spelling S = GNU_code_align);

  // Constructors
  CodeAlignAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                Expr *Alignment);

  CodeAlignAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Expr *getAlignment() const { return alignment; }

  static constexpr int MinimumAlignment = 1;
  static constexpr int MaximumAlignment = 4096;

  static bool classof(const Attr *A) { return A->getKind() == attr::CodeAlign; }
};

class CodeSegAttr : public InheritableAttr {
  unsigned nameLength;
  char *name;

public:
  // Factory methods
  static CodeSegAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                                     const AttributeCommonInfo &CommonInfo);
  static CodeSegAttr *Create(TreeContext &Ctx, llvm::StringRef Name,
                             const AttributeCommonInfo &CommonInfo);
  static CodeSegAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                                     SourceRange Range = {});
  static CodeSegAttr *Create(TreeContext &Ctx, llvm::StringRef Name,
                             SourceRange Range = {});

  // Constructors
  CodeSegAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
              llvm::StringRef Name);

  CodeSegAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getName() const { return llvm::StringRef(name, nameLength); }
  unsigned getNameLength() const { return nameLength; }
  void setName(TreeContext &C, llvm::StringRef S) {
    nameLength = S.size();
    this->name = new (C, 1) char[nameLength];
    if (!S.empty())
      std::memcpy(this->name, S.data(), nameLength);
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::CodeSeg; }
};

class ColdAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_cold = 0,
    Bracket_gnu_cold = 1,
    C23_gnu_cold = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ColdAttr *CreateImplicit(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static ColdAttr *Create(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo);
  static ColdAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                  Spelling S = GNU_cold);
  static ColdAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                          Spelling S = GNU_cold);

  // Constructors
  ColdAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  ColdAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Cold; }
};

class CommonAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_common = 0,
    Bracket_gnu_common = 1,
    C23_gnu_common = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static CommonAttr *CreateImplicit(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo);
  static CommonAttr *Create(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo);
  static CommonAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                    Spelling S = GNU_common);
  static CommonAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                            Spelling S = GNU_common);

  // Constructors
  CommonAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  CommonAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Common; }
};

class ConstAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_const = 0,
    Bracket_gnu_const = 1,
    C23_gnu_const = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ConstAttr *CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo);
  static ConstAttr *Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo);
  static ConstAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                   Spelling S = GNU_const);
  static ConstAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                           Spelling S = GNU_const);

  // Constructors
  ConstAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  ConstAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Const; }
};

class ConstructorAttr : public InheritableAttr {
  int priority;

public:
  enum Spelling {
    GNU_constructor = 0,
    Bracket_gnu_constructor = 1,
    C23_gnu_constructor = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ConstructorAttr *CreateImplicit(TreeContext &Ctx, int Priority,
                                         const AttributeCommonInfo &CommonInfo);
  static ConstructorAttr *Create(TreeContext &Ctx, int Priority,
                                 const AttributeCommonInfo &CommonInfo);
  static ConstructorAttr *CreateImplicit(TreeContext &Ctx, int Priority,
                                         SourceRange Range = {},
                                         Spelling S = GNU_constructor);
  static ConstructorAttr *Create(TreeContext &Ctx, int Priority,
                                 SourceRange Range = {},
                                 Spelling S = GNU_constructor);

  // Constructors
  ConstructorAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                  int Priority);
  ConstructorAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  ConstructorAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  int getPriority() const { return priority; }

  static const int DefaultPriority = 65535;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::Constructor;
  }
};

class ConvergentAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_convergent = 0,
    Bracket_clang_convergent = 1,
    Bracket_neverc_convergent = 2,
    C23_clang_convergent = 3,
    C23_neverc_convergent = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ConvergentAttr *CreateImplicit(TreeContext &Ctx,
                                        const AttributeCommonInfo &CommonInfo);
  static ConvergentAttr *Create(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo);
  static ConvergentAttr *CreateImplicit(TreeContext &Ctx,
                                        SourceRange Range = {},
                                        Spelling S = GNU_convergent);
  static ConvergentAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                Spelling S = GNU_convergent);

  // Constructors
  ConvergentAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  ConvergentAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::Convergent;
  }
};

class DLLExportAttr : public InheritableAttr {
public:
  enum Spelling {
    Declspec_dllexport = 0,
    GNU_dllexport = 1,
    Bracket_gnu_dllexport = 2,
    C23_gnu_dllexport = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static DLLExportAttr *CreateImplicit(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo);
  static DLLExportAttr *Create(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo);
  static DLLExportAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                       Spelling S = Declspec_dllexport);
  static DLLExportAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                               Spelling S = Declspec_dllexport);

  // Constructors
  DLLExportAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  DLLExportAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::DLLExport; }
};

class DLLImportAttr : public InheritableAttr {
public:
  enum Spelling {
    Declspec_dllimport = 0,
    GNU_dllimport = 1,
    Bracket_gnu_dllimport = 2,
    C23_gnu_dllimport = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static DLLImportAttr *CreateImplicit(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo);
  static DLLImportAttr *Create(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo);
  static DLLImportAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                       Spelling S = Declspec_dllimport);
  static DLLImportAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                               Spelling S = Declspec_dllimport);

  // Constructors
  DLLImportAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  DLLImportAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::DLLImport; }
};

class DeprecatedAttr : public InheritableAttr {
  unsigned messageLength;
  char *message;

  unsigned replacementLength;
  char *replacement;

public:
  enum Spelling {
    GNU_deprecated = 0,
    Bracket_gnu_deprecated = 1,
    C23_gnu_deprecated = 2,
    Declspec_deprecated = 3,
    Bracket_deprecated = 4,
    C23_deprecated = 5,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static DeprecatedAttr *CreateImplicit(TreeContext &Ctx,
                                        llvm::StringRef Message,
                                        llvm::StringRef Replacement,
                                        const AttributeCommonInfo &CommonInfo);
  static DeprecatedAttr *Create(TreeContext &Ctx, llvm::StringRef Message,
                                llvm::StringRef Replacement,
                                const AttributeCommonInfo &CommonInfo);
  static DeprecatedAttr *CreateImplicit(TreeContext &Ctx,
                                        llvm::StringRef Message,
                                        llvm::StringRef Replacement,
                                        SourceRange Range = {},
                                        Spelling S = GNU_deprecated);
  static DeprecatedAttr *Create(TreeContext &Ctx, llvm::StringRef Message,
                                llvm::StringRef Replacement,
                                SourceRange Range = {},
                                Spelling S = GNU_deprecated);

  // Constructors
  DeprecatedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                 llvm::StringRef Message, llvm::StringRef Replacement);
  DeprecatedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  DeprecatedAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getMessage() const {
    return llvm::StringRef(message, messageLength);
  }
  unsigned getMessageLength() const { return messageLength; }
  void setMessage(TreeContext &C, llvm::StringRef S) {
    messageLength = S.size();
    this->message = new (C, 1) char[messageLength];
    if (!S.empty())
      std::memcpy(this->message, S.data(), messageLength);
  }

  llvm::StringRef getReplacement() const {
    return llvm::StringRef(replacement, replacementLength);
  }
  unsigned getReplacementLength() const { return replacementLength; }
  void setReplacement(TreeContext &C, llvm::StringRef S) {
    replacementLength = S.size();
    this->replacement = new (C, 1) char[replacementLength];
    if (!S.empty())
      std::memcpy(this->replacement, S.data(), replacementLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::Deprecated;
  }
};

class DestructorAttr : public InheritableAttr {
  int priority;

public:
  enum Spelling {
    GNU_destructor = 0,
    Bracket_gnu_destructor = 1,
    C23_gnu_destructor = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static DestructorAttr *CreateImplicit(TreeContext &Ctx, int Priority,
                                        const AttributeCommonInfo &CommonInfo);
  static DestructorAttr *Create(TreeContext &Ctx, int Priority,
                                const AttributeCommonInfo &CommonInfo);
  static DestructorAttr *CreateImplicit(TreeContext &Ctx, int Priority,
                                        SourceRange Range = {},
                                        Spelling S = GNU_destructor);
  static DestructorAttr *Create(TreeContext &Ctx, int Priority,
                                SourceRange Range = {},
                                Spelling S = GNU_destructor);

  // Constructors
  DestructorAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                 int Priority);
  DestructorAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  DestructorAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  int getPriority() const { return priority; }

  static const int DefaultPriority = 65535;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::Destructor;
  }
};

class DiagnoseAsBuiltinAttr : public InheritableAttr {
  FunctionDecl *function;

  unsigned argIndices_Size;
  unsigned *argIndices_;

public:
  enum Spelling {
    GNU_diagnose_as_builtin = 0,
    Bracket_clang_diagnose_as_builtin = 1,
    Bracket_neverc_diagnose_as_builtin = 2,
    C23_clang_diagnose_as_builtin = 3,
    C23_neverc_diagnose_as_builtin = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static DiagnoseAsBuiltinAttr *
  CreateImplicit(TreeContext &Ctx, FunctionDecl *Function, unsigned *ArgIndices,
                 unsigned ArgIndicesSize,
                 const AttributeCommonInfo &CommonInfo);
  static DiagnoseAsBuiltinAttr *Create(TreeContext &Ctx, FunctionDecl *Function,
                                       unsigned *ArgIndices,
                                       unsigned ArgIndicesSize,
                                       const AttributeCommonInfo &CommonInfo);
  static DiagnoseAsBuiltinAttr *
  CreateImplicit(TreeContext &Ctx, FunctionDecl *Function, unsigned *ArgIndices,
                 unsigned ArgIndicesSize, SourceRange Range = {},
                 Spelling S = GNU_diagnose_as_builtin);
  static DiagnoseAsBuiltinAttr *Create(TreeContext &Ctx, FunctionDecl *Function,
                                       unsigned *ArgIndices,
                                       unsigned ArgIndicesSize,
                                       SourceRange Range = {},
                                       Spelling S = GNU_diagnose_as_builtin);

  // Constructors
  DiagnoseAsBuiltinAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                        FunctionDecl *Function, unsigned *ArgIndices,
                        unsigned ArgIndicesSize);
  DiagnoseAsBuiltinAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                        FunctionDecl *Function);

  DiagnoseAsBuiltinAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  FunctionDecl *getFunction() const { return function; }

  typedef unsigned *argIndices_iterator;
  argIndices_iterator argIndices_begin() const { return argIndices_; }
  argIndices_iterator argIndices_end() const {
    return argIndices_ + argIndices_Size;
  }
  unsigned argIndices_size() const { return argIndices_Size; }
  llvm::iterator_range<argIndices_iterator> argIndices() const {
    return llvm::make_range(argIndices_begin(), argIndices_end());
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::DiagnoseAsBuiltin;
  }
};

class DiagnoseIfAttr : public InheritableAttr {
  Expr *cond;

  unsigned messageLength;
  char *message;

public:
  enum DiagnosticType { DT_Error, DT_Warning };

private:
  DiagnoseIfAttr::DiagnosticType diagnosticType;

  bool argDependent;

  NamedDecl *parent;

public:
  // Factory methods
  static DiagnoseIfAttr *
  CreateImplicit(TreeContext &Ctx, Expr *Cond, llvm::StringRef Message,
                 DiagnoseIfAttr::DiagnosticType DiagnosticType,
                 bool ArgDependent, NamedDecl *Parent,
                 const AttributeCommonInfo &CommonInfo);
  static DiagnoseIfAttr *Create(TreeContext &Ctx, Expr *Cond,
                                llvm::StringRef Message,
                                DiagnoseIfAttr::DiagnosticType DiagnosticType,
                                bool ArgDependent, NamedDecl *Parent,
                                const AttributeCommonInfo &CommonInfo);
  static DiagnoseIfAttr *
  CreateImplicit(TreeContext &Ctx, Expr *Cond, llvm::StringRef Message,
                 DiagnoseIfAttr::DiagnosticType DiagnosticType,
                 bool ArgDependent, NamedDecl *Parent, SourceRange Range = {});
  static DiagnoseIfAttr *Create(TreeContext &Ctx, Expr *Cond,
                                llvm::StringRef Message,
                                DiagnoseIfAttr::DiagnosticType DiagnosticType,
                                bool ArgDependent, NamedDecl *Parent,
                                SourceRange Range = {});
  static DiagnoseIfAttr *
  CreateImplicit(TreeContext &Ctx, Expr *Cond, llvm::StringRef Message,
                 DiagnoseIfAttr::DiagnosticType DiagnosticType,
                 const AttributeCommonInfo &CommonInfo);
  static DiagnoseIfAttr *Create(TreeContext &Ctx, Expr *Cond,
                                llvm::StringRef Message,
                                DiagnoseIfAttr::DiagnosticType DiagnosticType,
                                const AttributeCommonInfo &CommonInfo);
  static DiagnoseIfAttr *
  CreateImplicit(TreeContext &Ctx, Expr *Cond, llvm::StringRef Message,
                 DiagnoseIfAttr::DiagnosticType DiagnosticType,
                 SourceRange Range = {});
  static DiagnoseIfAttr *Create(TreeContext &Ctx, Expr *Cond,
                                llvm::StringRef Message,
                                DiagnoseIfAttr::DiagnosticType DiagnosticType,
                                SourceRange Range = {});

  // Constructors
  DiagnoseIfAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                 Expr *Cond, llvm::StringRef Message,
                 DiagnoseIfAttr::DiagnosticType DiagnosticType,
                 bool ArgDependent, NamedDecl *Parent);
  DiagnoseIfAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                 Expr *Cond, llvm::StringRef Message,
                 DiagnoseIfAttr::DiagnosticType DiagnosticType);

  DiagnoseIfAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Expr *getCond() const { return cond; }

  llvm::StringRef getMessage() const {
    return llvm::StringRef(message, messageLength);
  }
  unsigned getMessageLength() const { return messageLength; }
  void setMessage(TreeContext &C, llvm::StringRef S) {
    messageLength = S.size();
    this->message = new (C, 1) char[messageLength];
    if (!S.empty())
      std::memcpy(this->message, S.data(), messageLength);
  }

  DiagnoseIfAttr::DiagnosticType getDiagnosticType() const {
    return diagnosticType;
  }

  static bool ConvertStrToDiagnosticType(StringRef Val,
                                         DiagnoseIfAttr::DiagnosticType &Out);
  static const char *
  ConvertDiagnosticTypeToStr(DiagnoseIfAttr::DiagnosticType Val);
  bool getArgDependent() const { return argDependent; }

  NamedDecl *getParent() const { return parent; }

  bool isError() const { return diagnosticType == DT_Error; }
  bool isWarning() const { return diagnosticType == DT_Warning; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::DiagnoseIf;
  }
};

class DisableTailCallsAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_disable_tail_calls = 0,
    Bracket_clang_disable_tail_calls = 1,
    Bracket_neverc_disable_tail_calls = 2,
    C23_clang_disable_tail_calls = 3,
    C23_neverc_disable_tail_calls = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static DisableTailCallsAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static DisableTailCallsAttr *Create(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static DisableTailCallsAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_disable_tail_calls);
  static DisableTailCallsAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_disable_tail_calls);

  // Constructors
  DisableTailCallsAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  DisableTailCallsAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::DisableTailCalls;
  }
};

class DisableTryStmtAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_disable_try_stmt = 0,
    Bracket_gnu_disable_try_stmt = 1,
    C23_gnu_disable_try_stmt = 2,
    Declspec_disable_try_stmt = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static DisableTryStmtAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static DisableTryStmtAttr *Create(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo);
  static DisableTryStmtAttr *CreateImplicit(TreeContext &Ctx,
                                            SourceRange Range = {},
                                            Spelling S = GNU_disable_try_stmt);
  static DisableTryStmtAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                    Spelling S = GNU_disable_try_stmt);

  // Constructors
  DisableTryStmtAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  DisableTryStmtAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::DisableTryStmt;
  }
};

class EnableIfAttr : public InheritableAttr {
  Expr *cond;

  unsigned messageLength;
  char *message;

public:
  // Factory methods
  static EnableIfAttr *CreateImplicit(TreeContext &Ctx, Expr *Cond,
                                      llvm::StringRef Message,
                                      const AttributeCommonInfo &CommonInfo);
  static EnableIfAttr *Create(TreeContext &Ctx, Expr *Cond,
                              llvm::StringRef Message,
                              const AttributeCommonInfo &CommonInfo);
  static EnableIfAttr *CreateImplicit(TreeContext &Ctx, Expr *Cond,
                                      llvm::StringRef Message,
                                      SourceRange Range = {});
  static EnableIfAttr *Create(TreeContext &Ctx, Expr *Cond,
                              llvm::StringRef Message, SourceRange Range = {});

  // Constructors
  EnableIfAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
               Expr *Cond, llvm::StringRef Message);

  EnableIfAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Expr *getCond() const { return cond; }

  llvm::StringRef getMessage() const {
    return llvm::StringRef(message, messageLength);
  }
  unsigned getMessageLength() const { return messageLength; }
  void setMessage(TreeContext &C, llvm::StringRef S) {
    messageLength = S.size();
    this->message = new (C, 1) char[messageLength];
    if (!S.empty())
      std::memcpy(this->message, S.data(), messageLength);
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::EnableIf; }
};

class EnforceTCBAttr : public InheritableAttr {
  unsigned tCBNameLength;
  char *tCBName;

public:
  enum Spelling {
    GNU_enforce_tcb = 0,
    Bracket_clang_enforce_tcb = 1,
    Bracket_neverc_enforce_tcb = 2,
    C23_clang_enforce_tcb = 3,
    C23_neverc_enforce_tcb = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static EnforceTCBAttr *CreateImplicit(TreeContext &Ctx,
                                        llvm::StringRef TCBName,
                                        const AttributeCommonInfo &CommonInfo);
  static EnforceTCBAttr *Create(TreeContext &Ctx, llvm::StringRef TCBName,
                                const AttributeCommonInfo &CommonInfo);
  static EnforceTCBAttr *CreateImplicit(TreeContext &Ctx,
                                        llvm::StringRef TCBName,
                                        SourceRange Range = {},
                                        Spelling S = GNU_enforce_tcb);
  static EnforceTCBAttr *Create(TreeContext &Ctx, llvm::StringRef TCBName,
                                SourceRange Range = {},
                                Spelling S = GNU_enforce_tcb);

  // Constructors
  EnforceTCBAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                 llvm::StringRef TCBName);

  EnforceTCBAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getTCBName() const {
    return llvm::StringRef(tCBName, tCBNameLength);
  }
  unsigned getTCBNameLength() const { return tCBNameLength; }
  void setTCBName(TreeContext &C, llvm::StringRef S) {
    tCBNameLength = S.size();
    this->tCBName = new (C, 1) char[tCBNameLength];
    if (!S.empty())
      std::memcpy(this->tCBName, S.data(), tCBNameLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::EnforceTCB;
  }
};

class EnforceTCBLeafAttr : public InheritableAttr {
  unsigned tCBNameLength;
  char *tCBName;

public:
  enum Spelling {
    GNU_enforce_tcb_leaf = 0,
    Bracket_clang_enforce_tcb_leaf = 1,
    Bracket_neverc_enforce_tcb_leaf = 2,
    C23_clang_enforce_tcb_leaf = 3,
    C23_neverc_enforce_tcb_leaf = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static EnforceTCBLeafAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef TCBName,
                 const AttributeCommonInfo &CommonInfo);
  static EnforceTCBLeafAttr *Create(TreeContext &Ctx, llvm::StringRef TCBName,
                                    const AttributeCommonInfo &CommonInfo);
  static EnforceTCBLeafAttr *CreateImplicit(TreeContext &Ctx,
                                            llvm::StringRef TCBName,
                                            SourceRange Range = {},
                                            Spelling S = GNU_enforce_tcb_leaf);
  static EnforceTCBLeafAttr *Create(TreeContext &Ctx, llvm::StringRef TCBName,
                                    SourceRange Range = {},
                                    Spelling S = GNU_enforce_tcb_leaf);

  // Constructors
  EnforceTCBLeafAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                     llvm::StringRef TCBName);

  EnforceTCBLeafAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getTCBName() const {
    return llvm::StringRef(tCBName, tCBNameLength);
  }
  unsigned getTCBNameLength() const { return tCBNameLength; }
  void setTCBName(TreeContext &C, llvm::StringRef S) {
    tCBNameLength = S.size();
    this->tCBName = new (C, 1) char[tCBNameLength];
    if (!S.empty())
      std::memcpy(this->tCBName, S.data(), tCBNameLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::EnforceTCBLeaf;
  }
};

class EnumExtensibilityAttr : public InheritableAttr {
public:
  enum Kind { Closed, Open };

private:
  EnumExtensibilityAttr::Kind extensibility;

public:
  enum Spelling {
    GNU_enum_extensibility = 0,
    Bracket_clang_enum_extensibility = 1,
    Bracket_neverc_enum_extensibility = 2,
    C23_clang_enum_extensibility = 3,
    C23_neverc_enum_extensibility = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static EnumExtensibilityAttr *
  CreateImplicit(TreeContext &Ctx, EnumExtensibilityAttr::Kind Extensibility,
                 const AttributeCommonInfo &CommonInfo);
  static EnumExtensibilityAttr *
  Create(TreeContext &Ctx, EnumExtensibilityAttr::Kind Extensibility,
         const AttributeCommonInfo &CommonInfo);
  static EnumExtensibilityAttr *
  CreateImplicit(TreeContext &Ctx, EnumExtensibilityAttr::Kind Extensibility,
                 SourceRange Range = {}, Spelling S = GNU_enum_extensibility);
  static EnumExtensibilityAttr *
  Create(TreeContext &Ctx, EnumExtensibilityAttr::Kind Extensibility,
         SourceRange Range = {}, Spelling S = GNU_enum_extensibility);

  // Constructors
  EnumExtensibilityAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                        EnumExtensibilityAttr::Kind Extensibility);

  EnumExtensibilityAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  EnumExtensibilityAttr::Kind getExtensibility() const { return extensibility; }

  static bool ConvertStrToKind(StringRef Val, EnumExtensibilityAttr::Kind &Out);
  static const char *ConvertKindToStr(EnumExtensibilityAttr::Kind Val);

  static bool classof(const Attr *A) {
    return A->getKind() == attr::EnumExtensibility;
  }
};

class ErrorAttr : public InheritableAttr {
  unsigned userDiagnosticLength;
  char *userDiagnostic;

public:
  enum Spelling {
    GNU_error = 0,
    Bracket_gnu_error = 1,
    C23_gnu_error = 2,
    GNU_warning = 3,
    Bracket_gnu_warning = 4,
    C23_gnu_warning = 5,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ErrorAttr *CreateImplicit(TreeContext &Ctx,
                                   llvm::StringRef UserDiagnostic,
                                   const AttributeCommonInfo &CommonInfo);
  static ErrorAttr *Create(TreeContext &Ctx, llvm::StringRef UserDiagnostic,
                           const AttributeCommonInfo &CommonInfo);
  static ErrorAttr *CreateImplicit(TreeContext &Ctx,
                                   llvm::StringRef UserDiagnostic,
                                   SourceRange Range = {},
                                   Spelling S = GNU_error);
  static ErrorAttr *Create(TreeContext &Ctx, llvm::StringRef UserDiagnostic,
                           SourceRange Range = {}, Spelling S = GNU_error);

  // Constructors
  ErrorAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
            llvm::StringRef UserDiagnostic);

  ErrorAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Spelling getSemanticSpelling() const;
  bool isError() const {
    return getAttributeSpellingListIndex() == 0 ||
           getAttributeSpellingListIndex() == 1 ||
           getAttributeSpellingListIndex() == 2;
  }
  bool isWarning() const {
    return getAttributeSpellingListIndex() == 3 ||
           getAttributeSpellingListIndex() == 4 ||
           getAttributeSpellingListIndex() == 5;
  }
  llvm::StringRef getUserDiagnostic() const {
    return llvm::StringRef(userDiagnostic, userDiagnosticLength);
  }
  unsigned getUserDiagnosticLength() const { return userDiagnosticLength; }
  void setUserDiagnostic(TreeContext &C, llvm::StringRef S) {
    userDiagnosticLength = S.size();
    this->userDiagnostic = new (C, 1) char[userDiagnosticLength];
    if (!S.empty())
      std::memcpy(this->userDiagnostic, S.data(), userDiagnosticLength);
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::Error; }
};

class FallThroughAttr : public StmtAttr {
public:
  enum Spelling {
    Bracket_fallthrough = 0,
    C23_fallthrough = 1,
    Bracket_neverc_fallthrough = 2,
    GNU_fallthrough = 3,
    Bracket_gnu_fallthrough = 4,
    C23_gnu_fallthrough = 5,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static FallThroughAttr *CreateImplicit(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo);
  static FallThroughAttr *Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo);
  static FallThroughAttr *CreateImplicit(TreeContext &Ctx,
                                         SourceRange Range = {},
                                         Spelling S = Bracket_fallthrough);
  static FallThroughAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                 Spelling S = Bracket_fallthrough);

  // Constructors
  FallThroughAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  FallThroughAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::FallThrough;
  }
};

class FastCallAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_fastcall = 0,
    Bracket_gnu_fastcall = 1,
    C23_gnu_fastcall = 2,
    Keyword_fastcall = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static FastCallAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static FastCallAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static FastCallAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_fastcall);
  static FastCallAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = GNU_fastcall);

  // Constructors
  FastCallAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  FastCallAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::FastCall; }
};

class FlagEnumAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_flag_enum = 0,
    Bracket_clang_flag_enum = 1,
    Bracket_neverc_flag_enum = 2,
    C23_clang_flag_enum = 3,
    C23_neverc_flag_enum = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static FlagEnumAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static FlagEnumAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static FlagEnumAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_flag_enum);
  static FlagEnumAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = GNU_flag_enum);

  // Constructors
  FlagEnumAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  FlagEnumAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::FlagEnum; }
};

class FlattenAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_flatten = 0,
    Bracket_gnu_flatten = 1,
    C23_gnu_flatten = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static FlattenAttr *CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static FlattenAttr *Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo);
  static FlattenAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_flatten);
  static FlattenAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                             Spelling S = GNU_flatten);

  // Constructors
  FlattenAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  FlattenAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Flatten; }
};

class FormatAttr : public InheritableAttr {
  IdentifierInfo *type;

  int formatIdx;

  int firstArg;

public:
  enum Spelling {
    GNU_format = 0,
    Bracket_gnu_format = 1,
    C23_gnu_format = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static FormatAttr *CreateImplicit(TreeContext &Ctx, IdentifierInfo *Type,
                                    int FormatIdx, int FirstArg,
                                    const AttributeCommonInfo &CommonInfo);
  static FormatAttr *Create(TreeContext &Ctx, IdentifierInfo *Type,
                            int FormatIdx, int FirstArg,
                            const AttributeCommonInfo &CommonInfo);
  static FormatAttr *CreateImplicit(TreeContext &Ctx, IdentifierInfo *Type,
                                    int FormatIdx, int FirstArg,
                                    SourceRange Range = {},
                                    Spelling S = GNU_format);
  static FormatAttr *Create(TreeContext &Ctx, IdentifierInfo *Type,
                            int FormatIdx, int FirstArg, SourceRange Range = {},
                            Spelling S = GNU_format);

  // Constructors
  FormatAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
             IdentifierInfo *Type, int FormatIdx, int FirstArg);

  FormatAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  IdentifierInfo *getType() const { return type; }

  int getFormatIdx() const { return formatIdx; }

  int getFirstArg() const { return firstArg; }

  static bool classof(const Attr *A) { return A->getKind() == attr::Format; }
};

class FormatArgAttr : public InheritableAttr {
  ParamIdx formatIdx;

public:
  enum Spelling {
    GNU_format_arg = 0,
    Bracket_gnu_format_arg = 1,
    C23_gnu_format_arg = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static FormatArgAttr *CreateImplicit(TreeContext &Ctx, ParamIdx FormatIdx,
                                       const AttributeCommonInfo &CommonInfo);
  static FormatArgAttr *Create(TreeContext &Ctx, ParamIdx FormatIdx,
                               const AttributeCommonInfo &CommonInfo);
  static FormatArgAttr *CreateImplicit(TreeContext &Ctx, ParamIdx FormatIdx,
                                       SourceRange Range = {},
                                       Spelling S = GNU_format_arg);
  static FormatArgAttr *Create(TreeContext &Ctx, ParamIdx FormatIdx,
                               SourceRange Range = {},
                               Spelling S = GNU_format_arg);

  // Constructors
  FormatArgAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                ParamIdx FormatIdx);

  FormatArgAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  ParamIdx getFormatIdx() const { return formatIdx; }

  static bool classof(const Attr *A) { return A->getKind() == attr::FormatArg; }
};

class FunctionReturnThunksAttr : public InheritableAttr {
public:
  enum Kind { Keep, Extern };

private:
  FunctionReturnThunksAttr::Kind thunkType;

public:
  enum Spelling {
    GNU_function_return = 0,
    Bracket_gnu_function_return = 1,
    C23_gnu_function_return = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static FunctionReturnThunksAttr *
  CreateImplicit(TreeContext &Ctx, FunctionReturnThunksAttr::Kind ThunkType,
                 const AttributeCommonInfo &CommonInfo);
  static FunctionReturnThunksAttr *
  Create(TreeContext &Ctx, FunctionReturnThunksAttr::Kind ThunkType,
         const AttributeCommonInfo &CommonInfo);
  static FunctionReturnThunksAttr *
  CreateImplicit(TreeContext &Ctx, FunctionReturnThunksAttr::Kind ThunkType,
                 SourceRange Range = {}, Spelling S = GNU_function_return);
  static FunctionReturnThunksAttr *
  Create(TreeContext &Ctx, FunctionReturnThunksAttr::Kind ThunkType,
         SourceRange Range = {}, Spelling S = GNU_function_return);

  // Constructors
  FunctionReturnThunksAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo,
                           FunctionReturnThunksAttr::Kind ThunkType);

  FunctionReturnThunksAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  FunctionReturnThunksAttr::Kind getThunkType() const { return thunkType; }

  static bool ConvertStrToKind(StringRef Val,
                               FunctionReturnThunksAttr::Kind &Out);
  static const char *ConvertKindToStr(FunctionReturnThunksAttr::Kind Val);

  static bool classof(const Attr *A) {
    return A->getKind() == attr::FunctionReturnThunks;
  }
};

class GNUInlineAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_gnu_inline = 0,
    Bracket_gnu_gnu_inline = 1,
    C23_gnu_gnu_inline = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static GNUInlineAttr *CreateImplicit(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo);
  static GNUInlineAttr *Create(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo);
  static GNUInlineAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                       Spelling S = GNU_gnu_inline);
  static GNUInlineAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                               Spelling S = GNU_gnu_inline);

  // Constructors
  GNUInlineAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  GNUInlineAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::GNUInline; }
};

class HotAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_hot = 0,
    Bracket_gnu_hot = 1,
    C23_gnu_hot = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static HotAttr *CreateImplicit(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo);
  static HotAttr *Create(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo);
  static HotAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                 Spelling S = GNU_hot);
  static HotAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                         Spelling S = GNU_hot);

  // Constructors
  HotAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  HotAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Hot; }
};

class IFuncAttr : public Attr {
  unsigned resolverLength;
  char *resolver;

public:
  enum Spelling {
    GNU_ifunc = 0,
    Bracket_gnu_ifunc = 1,
    C23_gnu_ifunc = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static IFuncAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Resolver,
                                   const AttributeCommonInfo &CommonInfo);
  static IFuncAttr *Create(TreeContext &Ctx, llvm::StringRef Resolver,
                           const AttributeCommonInfo &CommonInfo);
  static IFuncAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Resolver,
                                   SourceRange Range = {},
                                   Spelling S = GNU_ifunc);
  static IFuncAttr *Create(TreeContext &Ctx, llvm::StringRef Resolver,
                           SourceRange Range = {}, Spelling S = GNU_ifunc);

  // Constructors
  IFuncAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
            llvm::StringRef Resolver);

  IFuncAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getResolver() const {
    return llvm::StringRef(resolver, resolverLength);
  }
  unsigned getResolverLength() const { return resolverLength; }
  void setResolver(TreeContext &C, llvm::StringRef S) {
    resolverLength = S.size();
    this->resolver = new (C, 1) char[resolverLength];
    if (!S.empty())
      std::memcpy(this->resolver, S.data(), resolverLength);
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::IFunc; }
};

class InitSegAttr : public Attr {
  unsigned sectionLength;
  char *section;

public:
  // Factory methods
  static InitSegAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Section,
                                     const AttributeCommonInfo &CommonInfo);
  static InitSegAttr *Create(TreeContext &Ctx, llvm::StringRef Section,
                             const AttributeCommonInfo &CommonInfo);
  static InitSegAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Section,
                                     SourceRange Range = {});
  static InitSegAttr *Create(TreeContext &Ctx, llvm::StringRef Section,
                             SourceRange Range = {});

  // Constructors
  InitSegAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
              llvm::StringRef Section);

  InitSegAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getSection() const {
    return llvm::StringRef(section, sectionLength);
  }
  unsigned getSectionLength() const { return sectionLength; }
  void setSection(TreeContext &C, llvm::StringRef S) {
    sectionLength = S.size();
    this->section = new (C, 1) char[sectionLength];
    if (!S.empty())
      std::memcpy(this->section, S.data(), sectionLength);
  }

  void printPrettyPragma(raw_ostream &OS, const PrintingPolicy &Policy) const {
    OS << " (" << getSection() << ')';
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::InitSeg; }
};

class InternalLinkageAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_internal_linkage = 0,
    Bracket_clang_internal_linkage = 1,
    Bracket_neverc_internal_linkage = 2,
    C23_clang_internal_linkage = 3,
    C23_neverc_internal_linkage = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static InternalLinkageAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static InternalLinkageAttr *Create(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static InternalLinkageAttr *CreateImplicit(TreeContext &Ctx,
                                             SourceRange Range = {},
                                             Spelling S = GNU_internal_linkage);
  static InternalLinkageAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_internal_linkage);

  // Constructors
  InternalLinkageAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  InternalLinkageAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::InternalLinkage;
  }
};

class LTOVisibilityPublicAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_lto_visibility_public = 0,
    Bracket_clang_lto_visibility_public = 1,
    Bracket_neverc_lto_visibility_public = 2,
    C23_clang_lto_visibility_public = 3,
    C23_neverc_lto_visibility_public = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static LTOVisibilityPublicAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static LTOVisibilityPublicAttr *Create(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo);
  static LTOVisibilityPublicAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_lto_visibility_public);
  static LTOVisibilityPublicAttr *
  Create(TreeContext &Ctx, SourceRange Range = {},
         Spelling S = GNU_lto_visibility_public);

  // Constructors
  LTOVisibilityPublicAttr(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo);

  LTOVisibilityPublicAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::LTOVisibilityPublic;
  }
};

class LeafAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_leaf = 0,
    Bracket_gnu_leaf = 1,
    C23_gnu_leaf = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static LeafAttr *CreateImplicit(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static LeafAttr *Create(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo);
  static LeafAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                  Spelling S = GNU_leaf);
  static LeafAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                          Spelling S = GNU_leaf);

  // Constructors
  LeafAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  LeafAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Leaf; }
};

class LikelyAttr : public StmtAttr {
public:
  enum Spelling {
    Bracket_likely = 0,
    C23_neverc_likely = 1,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static LikelyAttr *CreateImplicit(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo);
  static LikelyAttr *Create(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo);
  static LikelyAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                    Spelling S = Bracket_likely);
  static LikelyAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                            Spelling S = Bracket_likely);

  // Constructors
  LikelyAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  LikelyAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Likely; }
};

class LoaderUninitializedAttr : public Attr {
public:
  enum Spelling {
    GNU_loader_uninitialized = 0,
    Bracket_clang_loader_uninitialized = 1,
    Bracket_neverc_loader_uninitialized = 2,
    C23_clang_loader_uninitialized = 3,
    C23_neverc_loader_uninitialized = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static LoaderUninitializedAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static LoaderUninitializedAttr *Create(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo);
  static LoaderUninitializedAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_loader_uninitialized);
  static LoaderUninitializedAttr *Create(TreeContext &Ctx,
                                         SourceRange Range = {},
                                         Spelling S = GNU_loader_uninitialized);

  // Constructors
  LoaderUninitializedAttr(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo);

  LoaderUninitializedAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::LoaderUninitialized;
  }
};

class MSABIAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_ms_abi = 0,
    Bracket_gnu_ms_abi = 1,
    C23_gnu_ms_abi = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static MSABIAttr *CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo);
  static MSABIAttr *Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo);
  static MSABIAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                   Spelling S = GNU_ms_abi);
  static MSABIAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                           Spelling S = GNU_ms_abi);

  // Constructors
  MSABIAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  MSABIAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::MSABI; }
};

class MSAllocatorAttr : public InheritableAttr {
public:
  // Factory methods
  static MSAllocatorAttr *CreateImplicit(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo);
  static MSAllocatorAttr *Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo);
  static MSAllocatorAttr *CreateImplicit(TreeContext &Ctx,
                                         SourceRange Range = {});
  static MSAllocatorAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  MSAllocatorAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  MSAllocatorAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::MSAllocator;
  }
};

class MSStructAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_ms_struct = 0,
    Bracket_gnu_ms_struct = 1,
    C23_gnu_ms_struct = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static MSStructAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static MSStructAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static MSStructAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_ms_struct);
  static MSStructAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = GNU_ms_struct);

  // Constructors
  MSStructAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  MSStructAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::MSStruct; }
};

class MaxFieldAlignmentAttr : public InheritableAttr {
  unsigned alignment;

public:
  // Factory methods
  static MaxFieldAlignmentAttr *
  CreateImplicit(TreeContext &Ctx, unsigned Alignment,
                 const AttributeCommonInfo &CommonInfo);
  static MaxFieldAlignmentAttr *Create(TreeContext &Ctx, unsigned Alignment,
                                       const AttributeCommonInfo &CommonInfo);
  static MaxFieldAlignmentAttr *
  CreateImplicit(TreeContext &Ctx, unsigned Alignment, SourceRange Range = {});
  static MaxFieldAlignmentAttr *Create(TreeContext &Ctx, unsigned Alignment,
                                       SourceRange Range = {});

  // Constructors
  MaxFieldAlignmentAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                        unsigned Alignment);

  MaxFieldAlignmentAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  unsigned getAlignment() const { return alignment; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::MaxFieldAlignment;
  }
};

class MayAliasAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_may_alias = 0,
    Bracket_gnu_may_alias = 1,
    C23_gnu_may_alias = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static MayAliasAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static MayAliasAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static MayAliasAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_may_alias);
  static MayAliasAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = GNU_may_alias);

  // Constructors
  MayAliasAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  MayAliasAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::MayAlias; }
};

class MaybeUndefAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_maybe_undef = 0,
    Bracket_clang_maybe_undef = 1,
    Bracket_neverc_maybe_undef = 2,
    C23_clang_maybe_undef = 3,
    C23_neverc_maybe_undef = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static MaybeUndefAttr *CreateImplicit(TreeContext &Ctx,
                                        const AttributeCommonInfo &CommonInfo);
  static MaybeUndefAttr *Create(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo);
  static MaybeUndefAttr *CreateImplicit(TreeContext &Ctx,
                                        SourceRange Range = {},
                                        Spelling S = GNU_maybe_undef);
  static MaybeUndefAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                Spelling S = GNU_maybe_undef);

  // Constructors
  MaybeUndefAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  MaybeUndefAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::MaybeUndef;
  }
};

class MinSizeAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_minsize = 0,
    Bracket_clang_minsize = 1,
    Bracket_neverc_minsize = 2,
    C23_clang_minsize = 3,
    C23_neverc_minsize = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static MinSizeAttr *CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static MinSizeAttr *Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo);
  static MinSizeAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_minsize);
  static MinSizeAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                             Spelling S = GNU_minsize);

  // Constructors
  MinSizeAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  MinSizeAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::MinSize; }
};

class MinVectorWidthAttr : public InheritableAttr {
  unsigned vectorWidth;

public:
  enum Spelling {
    GNU_min_vector_width = 0,
    Bracket_clang_min_vector_width = 1,
    Bracket_neverc_min_vector_width = 2,
    C23_clang_min_vector_width = 3,
    C23_neverc_min_vector_width = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static MinVectorWidthAttr *
  CreateImplicit(TreeContext &Ctx, unsigned VectorWidth,
                 const AttributeCommonInfo &CommonInfo);
  static MinVectorWidthAttr *Create(TreeContext &Ctx, unsigned VectorWidth,
                                    const AttributeCommonInfo &CommonInfo);
  static MinVectorWidthAttr *CreateImplicit(TreeContext &Ctx,
                                            unsigned VectorWidth,
                                            SourceRange Range = {},
                                            Spelling S = GNU_min_vector_width);
  static MinVectorWidthAttr *Create(TreeContext &Ctx, unsigned VectorWidth,
                                    SourceRange Range = {},
                                    Spelling S = GNU_min_vector_width);

  // Constructors
  MinVectorWidthAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                     unsigned VectorWidth);

  MinVectorWidthAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  unsigned getVectorWidth() const { return vectorWidth; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::MinVectorWidth;
  }
};

class ModeAttr : public Attr {
  IdentifierInfo *mode;

public:
  enum Spelling {
    GNU_mode = 0,
    Bracket_gnu_mode = 1,
    C23_gnu_mode = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ModeAttr *CreateImplicit(TreeContext &Ctx, IdentifierInfo *Mode,
                                  const AttributeCommonInfo &CommonInfo);
  static ModeAttr *Create(TreeContext &Ctx, IdentifierInfo *Mode,
                          const AttributeCommonInfo &CommonInfo);
  static ModeAttr *CreateImplicit(TreeContext &Ctx, IdentifierInfo *Mode,
                                  SourceRange Range = {},
                                  Spelling S = GNU_mode);
  static ModeAttr *Create(TreeContext &Ctx, IdentifierInfo *Mode,
                          SourceRange Range = {}, Spelling S = GNU_mode);

  // Constructors
  ModeAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
           IdentifierInfo *Mode);

  ModeAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  IdentifierInfo *getMode() const { return mode; }

  static bool classof(const Attr *A) { return A->getKind() == attr::Mode; }
};

class MustTailAttr : public StmtAttr {
public:
  enum Spelling {
    GNU_musttail = 0,
    Bracket_clang_musttail = 1,
    Bracket_neverc_musttail = 2,
    C23_clang_musttail = 3,
    C23_neverc_musttail = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static MustTailAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static MustTailAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static MustTailAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_musttail);
  static MustTailAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = GNU_musttail);

  // Constructors
  MustTailAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  MustTailAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::MustTail; }
};

class NakedAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_naked = 0,
    Bracket_gnu_naked = 1,
    C23_gnu_naked = 2,
    Declspec_naked = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NakedAttr *CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo);
  static NakedAttr *Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo);
  static NakedAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                   Spelling S = GNU_naked);
  static NakedAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                           Spelling S = GNU_naked);

  // Constructors
  NakedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NakedAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Naked; }
};

class NoAliasAttr : public InheritableAttr {
public:
  // Factory methods
  static NoAliasAttr *CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static NoAliasAttr *Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo);
  static NoAliasAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {});
  static NoAliasAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  NoAliasAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoAliasAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::NoAlias; }
};

class NoBuiltinAttr : public Attr {
  unsigned builtinNames_Size;
  StringRef *builtinNames_;

public:
  enum Spelling {
    GNU_no_builtin = 0,
    Bracket_clang_no_builtin = 1,
    Bracket_neverc_no_builtin = 2,
    C23_clang_no_builtin = 3,
    C23_neverc_no_builtin = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoBuiltinAttr *CreateImplicit(TreeContext &Ctx,
                                       StringRef *BuiltinNames,
                                       unsigned BuiltinNamesSize,
                                       const AttributeCommonInfo &CommonInfo);
  static NoBuiltinAttr *Create(TreeContext &Ctx, StringRef *BuiltinNames,
                               unsigned BuiltinNamesSize,
                               const AttributeCommonInfo &CommonInfo);
  static NoBuiltinAttr *CreateImplicit(TreeContext &Ctx,
                                       StringRef *BuiltinNames,
                                       unsigned BuiltinNamesSize,
                                       SourceRange Range = {},
                                       Spelling S = GNU_no_builtin);
  static NoBuiltinAttr *Create(TreeContext &Ctx, StringRef *BuiltinNames,
                               unsigned BuiltinNamesSize,
                               SourceRange Range = {},
                               Spelling S = GNU_no_builtin);

  // Constructors
  NoBuiltinAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                StringRef *BuiltinNames, unsigned BuiltinNamesSize);
  NoBuiltinAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoBuiltinAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  typedef StringRef *builtinNames_iterator;
  builtinNames_iterator builtinNames_begin() const { return builtinNames_; }
  builtinNames_iterator builtinNames_end() const {
    return builtinNames_ + builtinNames_Size;
  }
  unsigned builtinNames_size() const { return builtinNames_Size; }
  llvm::iterator_range<builtinNames_iterator> builtinNames() const {
    return llvm::make_range(builtinNames_begin(), builtinNames_end());
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::NoBuiltin; }
};

class NoCommonAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_nocommon = 0,
    Bracket_gnu_nocommon = 1,
    C23_gnu_nocommon = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoCommonAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static NoCommonAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static NoCommonAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_nocommon);
  static NoCommonAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = GNU_nocommon);

  // Constructors
  NoCommonAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoCommonAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::NoCommon; }
};

class NoDebugAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_nodebug = 0,
    Bracket_gnu_nodebug = 1,
    C23_gnu_nodebug = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoDebugAttr *CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static NoDebugAttr *Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo);
  static NoDebugAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_nodebug);
  static NoDebugAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                             Spelling S = GNU_nodebug);

  // Constructors
  NoDebugAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoDebugAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::NoDebug; }
};

class NoDerefAttr : public TypeAttr {
public:
  enum Spelling {
    GNU_noderef = 0,
    Bracket_clang_noderef = 1,
    Bracket_neverc_noderef = 2,
    C23_clang_noderef = 3,
    C23_neverc_noderef = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoDerefAttr *CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static NoDerefAttr *Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo);
  static NoDerefAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_noderef);
  static NoDerefAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                             Spelling S = GNU_noderef);

  // Constructors
  NoDerefAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoDerefAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::NoDeref; }
};

class NoDestroyAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_no_destroy = 0,
    Bracket_clang_no_destroy = 1,
    Bracket_neverc_no_destroy = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoDestroyAttr *CreateImplicit(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo);
  static NoDestroyAttr *Create(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo);
  static NoDestroyAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                       Spelling S = GNU_no_destroy);
  static NoDestroyAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                               Spelling S = GNU_no_destroy);

  // Constructors
  NoDestroyAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoDestroyAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::NoDestroy; }
};

class NoDuplicateAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_noduplicate = 0,
    Bracket_clang_noduplicate = 1,
    Bracket_neverc_noduplicate = 2,
    C23_clang_noduplicate = 3,
    C23_neverc_noduplicate = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoDuplicateAttr *CreateImplicit(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo);
  static NoDuplicateAttr *Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo);
  static NoDuplicateAttr *CreateImplicit(TreeContext &Ctx,
                                         SourceRange Range = {},
                                         Spelling S = GNU_noduplicate);
  static NoDuplicateAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                 Spelling S = GNU_noduplicate);

  // Constructors
  NoDuplicateAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoDuplicateAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::NoDuplicate;
  }
};

class NoEscapeAttr : public Attr {
public:
  enum Spelling {
    GNU_noescape = 0,
    Bracket_clang_noescape = 1,
    Bracket_neverc_noescape = 2,
    C23_clang_noescape = 3,
    C23_neverc_noescape = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoEscapeAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static NoEscapeAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static NoEscapeAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_noescape);
  static NoEscapeAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = GNU_noescape);

  // Constructors
  NoEscapeAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoEscapeAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::NoEscape; }
};

class NoInlineAttr : public DeclOrStmtAttr {
public:
  enum Spelling {
    Keyword_noinline = 0,
    GNU_noinline = 1,
    Bracket_gnu_noinline = 2,
    C23_gnu_noinline = 3,
    Bracket_neverc_noinline = 4,
    C23_neverc_noinline = 5,
    Declspec_noinline = 6,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoInlineAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static NoInlineAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static NoInlineAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = Keyword_noinline);
  static NoInlineAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = Keyword_noinline);

  // Constructors
  NoInlineAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoInlineAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  bool isNeverCNoInline() const {
    return getAttributeSpellingListIndex() == 4 ||
           getAttributeSpellingListIndex() == 5;
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::NoInline; }
};

class NoMergeAttr : public DeclOrStmtAttr {
public:
  enum Spelling {
    GNU_nomerge = 0,
    Bracket_clang_nomerge = 1,
    Bracket_neverc_nomerge = 2,
    C23_clang_nomerge = 3,
    C23_neverc_nomerge = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoMergeAttr *CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static NoMergeAttr *Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo);
  static NoMergeAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_nomerge);
  static NoMergeAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                             Spelling S = GNU_nomerge);

  // Constructors
  NoMergeAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoMergeAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::NoMerge; }
};

class NoRandomizeLayoutAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_no_randomize_layout = 0,
    Bracket_gnu_no_randomize_layout = 1,
    C23_gnu_no_randomize_layout = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoRandomizeLayoutAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static NoRandomizeLayoutAttr *Create(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo);
  static NoRandomizeLayoutAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_no_randomize_layout);
  static NoRandomizeLayoutAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                       Spelling S = GNU_no_randomize_layout);

  // Constructors
  NoRandomizeLayoutAttr(TreeContext &Ctx,
                        const AttributeCommonInfo &CommonInfo);

  NoRandomizeLayoutAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::NoRandomizeLayout;
  }
};

class NoReturnAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_noreturn = 0,
    Bracket_gnu_noreturn = 1,
    C23_gnu_noreturn = 2,
    Declspec_noreturn = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoReturnAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static NoReturnAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static NoReturnAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_noreturn);
  static NoReturnAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = GNU_noreturn);

  // Constructors
  NoReturnAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoReturnAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::NoReturn; }
};

class NoSpeculativeLoadHardeningAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_no_speculative_load_hardening = 0,
    Bracket_clang_no_speculative_load_hardening = 1,
    Bracket_neverc_no_speculative_load_hardening = 2,
    C23_clang_no_speculative_load_hardening = 3,
    C23_neverc_no_speculative_load_hardening = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoSpeculativeLoadHardeningAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static NoSpeculativeLoadHardeningAttr *
  Create(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static NoSpeculativeLoadHardeningAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_no_speculative_load_hardening);
  static NoSpeculativeLoadHardeningAttr *
  Create(TreeContext &Ctx, SourceRange Range = {},
         Spelling S = GNU_no_speculative_load_hardening);

  // Constructors
  NoSpeculativeLoadHardeningAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo);

  NoSpeculativeLoadHardeningAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::NoSpeculativeLoadHardening;
  }
};

class NoSplitStackAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_no_split_stack = 0,
    Bracket_gnu_no_split_stack = 1,
    C23_gnu_no_split_stack = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoSplitStackAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static NoSplitStackAttr *Create(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static NoSplitStackAttr *CreateImplicit(TreeContext &Ctx,
                                          SourceRange Range = {},
                                          Spelling S = GNU_no_split_stack);
  static NoSplitStackAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                  Spelling S = GNU_no_split_stack);

  // Constructors
  NoSplitStackAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoSplitStackAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::NoSplitStack;
  }
};

class NoStackProtectorAttr : public InheritableAttr {
public:
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

  // Factory methods
  static NoStackProtectorAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static NoStackProtectorAttr *Create(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static NoStackProtectorAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_no_stack_protector);
  static NoStackProtectorAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_no_stack_protector);

  // Constructors
  NoStackProtectorAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoStackProtectorAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Spelling getSemanticSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::NoStackProtector;
  }
};

class NoThrowAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_nothrow = 0,
    Bracket_gnu_nothrow = 1,
    C23_gnu_nothrow = 2,
    Declspec_nothrow = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoThrowAttr *CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static NoThrowAttr *Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo);
  static NoThrowAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_nothrow);
  static NoThrowAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                             Spelling S = GNU_nothrow);

  // Constructors
  NoThrowAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoThrowAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::NoThrow; }
};

class NoUwtableAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_nouwtable = 0,
    Bracket_clang_nouwtable = 1,
    Bracket_neverc_nouwtable = 2,
    C23_clang_nouwtable = 3,
    C23_neverc_nouwtable = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NoUwtableAttr *CreateImplicit(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo);
  static NoUwtableAttr *Create(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo);
  static NoUwtableAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                       Spelling S = GNU_nouwtable);
  static NoUwtableAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                               Spelling S = GNU_nouwtable);

  // Constructors
  NoUwtableAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NoUwtableAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::NoUwtable; }
};

class NonNullAttr : public InheritableParamAttr {
  unsigned args_Size;
  ParamIdx *args_;

public:
  enum Spelling {
    GNU_nonnull = 0,
    Bracket_gnu_nonnull = 1,
    C23_gnu_nonnull = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NonNullAttr *CreateImplicit(TreeContext &Ctx, ParamIdx *Args,
                                     unsigned ArgsSize,
                                     const AttributeCommonInfo &CommonInfo);
  static NonNullAttr *Create(TreeContext &Ctx, ParamIdx *Args,
                             unsigned ArgsSize,
                             const AttributeCommonInfo &CommonInfo);
  static NonNullAttr *CreateImplicit(TreeContext &Ctx, ParamIdx *Args,
                                     unsigned ArgsSize, SourceRange Range = {},
                                     Spelling S = GNU_nonnull);
  static NonNullAttr *Create(TreeContext &Ctx, ParamIdx *Args,
                             unsigned ArgsSize, SourceRange Range = {},
                             Spelling S = GNU_nonnull);

  // Constructors
  NonNullAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
              ParamIdx *Args, unsigned ArgsSize);
  NonNullAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NonNullAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  typedef ParamIdx *args_iterator;
  args_iterator args_begin() const { return args_; }
  args_iterator args_end() const { return args_ + args_Size; }
  unsigned args_size() const { return args_Size; }
  llvm::iterator_range<args_iterator> args() const {
    return llvm::make_range(args_begin(), args_end());
  }

  bool isNonNull(unsigned IdxAST) const {
    if (!args_size())
      return true;
    return llvm::any_of(args(), [=](const ParamIdx &Idx) {
      return Idx.getASTIndex() == IdxAST;
    });
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::NonNull; }
};

class NotTailCalledAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_not_tail_called = 0,
    Bracket_clang_not_tail_called = 1,
    Bracket_neverc_not_tail_called = 2,
    C23_clang_not_tail_called = 3,
    C23_neverc_not_tail_called = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static NotTailCalledAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static NotTailCalledAttr *Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo);
  static NotTailCalledAttr *CreateImplicit(TreeContext &Ctx,
                                           SourceRange Range = {},
                                           Spelling S = GNU_not_tail_called);
  static NotTailCalledAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                   Spelling S = GNU_not_tail_called);

  // Constructors
  NotTailCalledAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  NotTailCalledAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::NotTailCalled;
  }
};

class OptimizeNoneAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_optnone = 0,
    Bracket_clang_optnone = 1,
    Bracket_neverc_optnone = 2,
    C23_clang_optnone = 3,
    C23_neverc_optnone = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static OptimizeNoneAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static OptimizeNoneAttr *Create(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static OptimizeNoneAttr *CreateImplicit(TreeContext &Ctx,
                                          SourceRange Range = {},
                                          Spelling S = GNU_optnone);
  static OptimizeNoneAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                  Spelling S = GNU_optnone);

  // Constructors
  OptimizeNoneAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  OptimizeNoneAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::OptimizeNone;
  }
};

class OverloadableAttr : public Attr {
public:
  enum Spelling {
    GNU_overloadable = 0,
    Bracket_clang_overloadable = 1,
    Bracket_neverc_overloadable = 2,
    C23_clang_overloadable = 3,
    C23_neverc_overloadable = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static OverloadableAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static OverloadableAttr *Create(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static OverloadableAttr *CreateImplicit(TreeContext &Ctx,
                                          SourceRange Range = {},
                                          Spelling S = GNU_overloadable);
  static OverloadableAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                  Spelling S = GNU_overloadable);

  // Constructors
  OverloadableAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  OverloadableAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::Overloadable;
  }
};

class OverrideAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_override = 0,
    Bracket_gnu_override = 1,
    C23_gnu_override = 2,
    Declspec_override = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static OverrideAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static OverrideAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static OverrideAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_override);
  static OverrideAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = GNU_override);

  // Constructors
  OverrideAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  OverrideAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Override; }
};

class PackedAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_packed = 0,
    Bracket_gnu_packed = 1,
    C23_gnu_packed = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static PackedAttr *CreateImplicit(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo);
  static PackedAttr *Create(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo);
  static PackedAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                    Spelling S = GNU_packed);
  static PackedAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                            Spelling S = GNU_packed);

  // Constructors
  PackedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  PackedAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Packed; }
};

class PassObjectSizeAttr : public InheritableParamAttr {
  int type;

public:
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

  // Factory methods
  static PassObjectSizeAttr *
  CreateImplicit(TreeContext &Ctx, int Type,
                 const AttributeCommonInfo &CommonInfo);
  static PassObjectSizeAttr *Create(TreeContext &Ctx, int Type,
                                    const AttributeCommonInfo &CommonInfo);
  static PassObjectSizeAttr *CreateImplicit(TreeContext &Ctx, int Type,
                                            SourceRange Range = {},
                                            Spelling S = GNU_pass_object_size);
  static PassObjectSizeAttr *Create(TreeContext &Ctx, int Type,
                                    SourceRange Range = {},
                                    Spelling S = GNU_pass_object_size);

  // Constructors
  PassObjectSizeAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                     int Type);

  PassObjectSizeAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Spelling getSemanticSpelling() const;
  bool isDynamic() const {
    return getAttributeSpellingListIndex() == 5 ||
           getAttributeSpellingListIndex() == 6 ||
           getAttributeSpellingListIndex() == 7 ||
           getAttributeSpellingListIndex() == 8 ||
           getAttributeSpellingListIndex() == 9;
  }
  int getType() const { return type; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::PassObjectSize;
  }
};

class PatchableFunctionEntryAttr : public InheritableAttr {
  unsigned count;

  int offset;

public:
  enum Spelling {
    GNU_patchable_function_entry = 0,
    Bracket_gnu_patchable_function_entry = 1,
    C23_gnu_patchable_function_entry = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static PatchableFunctionEntryAttr *
  CreateImplicit(TreeContext &Ctx, unsigned Count, int Offset,
                 const AttributeCommonInfo &CommonInfo);
  static PatchableFunctionEntryAttr *
  Create(TreeContext &Ctx, unsigned Count, int Offset,
         const AttributeCommonInfo &CommonInfo);
  static PatchableFunctionEntryAttr *
  CreateImplicit(TreeContext &Ctx, unsigned Count, int Offset,
                 SourceRange Range = {},
                 Spelling S = GNU_patchable_function_entry);
  static PatchableFunctionEntryAttr *
  Create(TreeContext &Ctx, unsigned Count, int Offset, SourceRange Range = {},
         Spelling S = GNU_patchable_function_entry);

  // Constructors
  PatchableFunctionEntryAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo,
                             unsigned Count, int Offset);
  PatchableFunctionEntryAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo,
                             unsigned Count);

  PatchableFunctionEntryAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  unsigned getCount() const { return count; }

  int getOffset() const { return offset; }

  static const int DefaultOffset = 0;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::PatchableFunctionEntry;
  }
};

class PragmaNeverCBSSSectionAttr : public InheritableAttr {
  unsigned nameLength;
  char *name;

public:
  // Factory methods
  static PragmaNeverCBSSSectionAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                 const AttributeCommonInfo &CommonInfo);
  static PragmaNeverCBSSSectionAttr *
  Create(TreeContext &Ctx, llvm::StringRef Name,
         const AttributeCommonInfo &CommonInfo);
  static PragmaNeverCBSSSectionAttr *CreateImplicit(TreeContext &Ctx,
                                                    llvm::StringRef Name,
                                                    SourceRange Range = {});
  static PragmaNeverCBSSSectionAttr *
  Create(TreeContext &Ctx, llvm::StringRef Name, SourceRange Range = {});

  // Constructors
  PragmaNeverCBSSSectionAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo,
                             llvm::StringRef Name);

  PragmaNeverCBSSSectionAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getName() const { return llvm::StringRef(name, nameLength); }
  unsigned getNameLength() const { return nameLength; }
  void setName(TreeContext &C, llvm::StringRef S) {
    nameLength = S.size();
    this->name = new (C, 1) char[nameLength];
    if (!S.empty())
      std::memcpy(this->name, S.data(), nameLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::PragmaNeverCBSSSection;
  }
};

class PragmaNeverCDataSectionAttr : public InheritableAttr {
  unsigned nameLength;
  char *name;

public:
  // Factory methods
  static PragmaNeverCDataSectionAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                 const AttributeCommonInfo &CommonInfo);
  static PragmaNeverCDataSectionAttr *
  Create(TreeContext &Ctx, llvm::StringRef Name,
         const AttributeCommonInfo &CommonInfo);
  static PragmaNeverCDataSectionAttr *CreateImplicit(TreeContext &Ctx,
                                                     llvm::StringRef Name,
                                                     SourceRange Range = {});
  static PragmaNeverCDataSectionAttr *
  Create(TreeContext &Ctx, llvm::StringRef Name, SourceRange Range = {});

  // Constructors
  PragmaNeverCDataSectionAttr(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo,
                              llvm::StringRef Name);

  PragmaNeverCDataSectionAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getName() const { return llvm::StringRef(name, nameLength); }
  unsigned getNameLength() const { return nameLength; }
  void setName(TreeContext &C, llvm::StringRef S) {
    nameLength = S.size();
    this->name = new (C, 1) char[nameLength];
    if (!S.empty())
      std::memcpy(this->name, S.data(), nameLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::PragmaNeverCDataSection;
  }
};

class PragmaNeverCRelroSectionAttr : public InheritableAttr {
  unsigned nameLength;
  char *name;

public:
  // Factory methods
  static PragmaNeverCRelroSectionAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                 const AttributeCommonInfo &CommonInfo);
  static PragmaNeverCRelroSectionAttr *
  Create(TreeContext &Ctx, llvm::StringRef Name,
         const AttributeCommonInfo &CommonInfo);
  static PragmaNeverCRelroSectionAttr *CreateImplicit(TreeContext &Ctx,
                                                      llvm::StringRef Name,
                                                      SourceRange Range = {});
  static PragmaNeverCRelroSectionAttr *
  Create(TreeContext &Ctx, llvm::StringRef Name, SourceRange Range = {});

  // Constructors
  PragmaNeverCRelroSectionAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo,
                               llvm::StringRef Name);

  PragmaNeverCRelroSectionAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getName() const { return llvm::StringRef(name, nameLength); }
  unsigned getNameLength() const { return nameLength; }
  void setName(TreeContext &C, llvm::StringRef S) {
    nameLength = S.size();
    this->name = new (C, 1) char[nameLength];
    if (!S.empty())
      std::memcpy(this->name, S.data(), nameLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::PragmaNeverCRelroSection;
  }
};

class PragmaNeverCRodataSectionAttr : public InheritableAttr {
  unsigned nameLength;
  char *name;

public:
  // Factory methods
  static PragmaNeverCRodataSectionAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                 const AttributeCommonInfo &CommonInfo);
  static PragmaNeverCRodataSectionAttr *
  Create(TreeContext &Ctx, llvm::StringRef Name,
         const AttributeCommonInfo &CommonInfo);
  static PragmaNeverCRodataSectionAttr *CreateImplicit(TreeContext &Ctx,
                                                       llvm::StringRef Name,
                                                       SourceRange Range = {});
  static PragmaNeverCRodataSectionAttr *
  Create(TreeContext &Ctx, llvm::StringRef Name, SourceRange Range = {});

  // Constructors
  PragmaNeverCRodataSectionAttr(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo,
                                llvm::StringRef Name);

  PragmaNeverCRodataSectionAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getName() const { return llvm::StringRef(name, nameLength); }
  unsigned getNameLength() const { return nameLength; }
  void setName(TreeContext &C, llvm::StringRef S) {
    nameLength = S.size();
    this->name = new (C, 1) char[nameLength];
    if (!S.empty())
      std::memcpy(this->name, S.data(), nameLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::PragmaNeverCRodataSection;
  }
};

class PragmaNeverCTextSectionAttr : public InheritableAttr {
  unsigned nameLength;
  char *name;

public:
  // Factory methods
  static PragmaNeverCTextSectionAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                 const AttributeCommonInfo &CommonInfo);
  static PragmaNeverCTextSectionAttr *
  Create(TreeContext &Ctx, llvm::StringRef Name,
         const AttributeCommonInfo &CommonInfo);
  static PragmaNeverCTextSectionAttr *CreateImplicit(TreeContext &Ctx,
                                                     llvm::StringRef Name,
                                                     SourceRange Range = {});
  static PragmaNeverCTextSectionAttr *
  Create(TreeContext &Ctx, llvm::StringRef Name, SourceRange Range = {});

  // Constructors
  PragmaNeverCTextSectionAttr(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo,
                              llvm::StringRef Name);

  PragmaNeverCTextSectionAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getName() const { return llvm::StringRef(name, nameLength); }
  unsigned getNameLength() const { return nameLength; }
  void setName(TreeContext &C, llvm::StringRef S) {
    nameLength = S.size();
    this->name = new (C, 1) char[nameLength];
    if (!S.empty())
      std::memcpy(this->name, S.data(), nameLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::PragmaNeverCTextSection;
  }
};

class PreferredTypeAttr : public InheritableAttr {
  TypeSourceInfo *type;

public:
  enum Spelling {
    GNU_preferred_type = 0,
    Bracket_clang_preferred_type = 1,
    Bracket_neverc_preferred_type = 2,
    C23_clang_preferred_type = 3,
    C23_neverc_preferred_type = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static PreferredTypeAttr *
  CreateImplicit(TreeContext &Ctx, TypeSourceInfo *Type,
                 const AttributeCommonInfo &CommonInfo);
  static PreferredTypeAttr *Create(TreeContext &Ctx, TypeSourceInfo *Type,
                                   const AttributeCommonInfo &CommonInfo);
  static PreferredTypeAttr *CreateImplicit(TreeContext &Ctx,
                                           TypeSourceInfo *Type,
                                           SourceRange Range = {},
                                           Spelling S = GNU_preferred_type);
  static PreferredTypeAttr *Create(TreeContext &Ctx, TypeSourceInfo *Type,
                                   SourceRange Range = {},
                                   Spelling S = GNU_preferred_type);

  // Constructors
  PreferredTypeAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                    TypeSourceInfo *Type);
  PreferredTypeAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  PreferredTypeAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  QualType getType() const { return type->getType(); }
  TypeSourceInfo *getTypeLoc() const { return type; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::PreferredType;
  }
};

class PreserveAllAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_preserve_all = 0,
    Bracket_clang_preserve_all = 1,
    Bracket_neverc_preserve_all = 2,
    C23_clang_preserve_all = 3,
    C23_neverc_preserve_all = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static PreserveAllAttr *CreateImplicit(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo);
  static PreserveAllAttr *Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo);
  static PreserveAllAttr *CreateImplicit(TreeContext &Ctx,
                                         SourceRange Range = {},
                                         Spelling S = GNU_preserve_all);
  static PreserveAllAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                 Spelling S = GNU_preserve_all);

  // Constructors
  PreserveAllAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  PreserveAllAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::PreserveAll;
  }
};

class PreserveMostAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_preserve_most = 0,
    Bracket_clang_preserve_most = 1,
    Bracket_neverc_preserve_most = 2,
    C23_clang_preserve_most = 3,
    C23_neverc_preserve_most = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static PreserveMostAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static PreserveMostAttr *Create(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static PreserveMostAttr *CreateImplicit(TreeContext &Ctx,
                                          SourceRange Range = {},
                                          Spelling S = GNU_preserve_most);
  static PreserveMostAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                  Spelling S = GNU_preserve_most);

  // Constructors
  PreserveMostAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  PreserveMostAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::PreserveMost;
  }
};

class Ptr32Attr : public TypeAttr {
public:
  // Factory methods
  static Ptr32Attr *CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo);
  static Ptr32Attr *Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo);
  static Ptr32Attr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {});
  static Ptr32Attr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  Ptr32Attr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  Ptr32Attr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Ptr32; }
};

class Ptr64Attr : public TypeAttr {
public:
  // Factory methods
  static Ptr64Attr *CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo);
  static Ptr64Attr *Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo);
  static Ptr64Attr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {});
  static Ptr64Attr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  Ptr64Attr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  Ptr64Attr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Ptr64; }
};

class PureAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_pure = 0,
    Bracket_gnu_pure = 1,
    C23_gnu_pure = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static PureAttr *CreateImplicit(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static PureAttr *Create(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo);
  static PureAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                  Spelling S = GNU_pure);
  static PureAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                          Spelling S = GNU_pure);

  // Constructors
  PureAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  PureAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Pure; }
};

class RandomizeLayoutAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_randomize_layout = 0,
    Bracket_gnu_randomize_layout = 1,
    C23_gnu_randomize_layout = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static RandomizeLayoutAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static RandomizeLayoutAttr *Create(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static RandomizeLayoutAttr *CreateImplicit(TreeContext &Ctx,
                                             SourceRange Range = {},
                                             Spelling S = GNU_randomize_layout);
  static RandomizeLayoutAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_randomize_layout);

  // Constructors
  RandomizeLayoutAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  RandomizeLayoutAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::RandomizeLayout;
  }
};

class ReadOnlyPlacementAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_enforce_read_only_placement = 0,
    Bracket_clang_enforce_read_only_placement = 1,
    Bracket_neverc_enforce_read_only_placement = 2,
    C23_clang_enforce_read_only_placement = 3,
    C23_neverc_enforce_read_only_placement = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ReadOnlyPlacementAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static ReadOnlyPlacementAttr *Create(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo);
  static ReadOnlyPlacementAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_enforce_read_only_placement);
  static ReadOnlyPlacementAttr *
  Create(TreeContext &Ctx, SourceRange Range = {},
         Spelling S = GNU_enforce_read_only_placement);

  // Constructors
  ReadOnlyPlacementAttr(TreeContext &Ctx,
                        const AttributeCommonInfo &CommonInfo);

  ReadOnlyPlacementAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::ReadOnlyPlacement;
  }
};

class RegCallAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_regcall = 0,
    Bracket_gnu_regcall = 1,
    C23_gnu_regcall = 2,
    Keyword_regcall = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static RegCallAttr *CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static RegCallAttr *Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo);
  static RegCallAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_regcall);
  static RegCallAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                             Spelling S = GNU_regcall);

  // Constructors
  RegCallAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  RegCallAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::RegCall; }
};

class ReleaseHandleAttr : public InheritableParamAttr {
  unsigned handleTypeLength;
  char *handleType;

public:
  enum Spelling {
    GNU_release_handle = 0,
    Bracket_clang_release_handle = 1,
    Bracket_neverc_release_handle = 2,
    C23_clang_release_handle = 3,
    C23_neverc_release_handle = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ReleaseHandleAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef HandleType,
                 const AttributeCommonInfo &CommonInfo);
  static ReleaseHandleAttr *Create(TreeContext &Ctx, llvm::StringRef HandleType,
                                   const AttributeCommonInfo &CommonInfo);
  static ReleaseHandleAttr *CreateImplicit(TreeContext &Ctx,
                                           llvm::StringRef HandleType,
                                           SourceRange Range = {},
                                           Spelling S = GNU_release_handle);
  static ReleaseHandleAttr *Create(TreeContext &Ctx, llvm::StringRef HandleType,
                                   SourceRange Range = {},
                                   Spelling S = GNU_release_handle);

  // Constructors
  ReleaseHandleAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                    llvm::StringRef HandleType);

  ReleaseHandleAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getHandleType() const {
    return llvm::StringRef(handleType, handleTypeLength);
  }
  unsigned getHandleTypeLength() const { return handleTypeLength; }
  void setHandleType(TreeContext &C, llvm::StringRef S) {
    handleTypeLength = S.size();
    this->handleType = new (C, 1) char[handleTypeLength];
    if (!S.empty())
      std::memcpy(this->handleType, S.data(), handleTypeLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::ReleaseHandle;
  }
};

class RestrictAttr : public InheritableAttr {
public:
  enum Spelling {
    Declspec_restrict = 0,
    GNU_malloc = 1,
    Bracket_gnu_malloc = 2,
    C23_gnu_malloc = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static RestrictAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static RestrictAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static RestrictAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = Declspec_restrict);
  static RestrictAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = Declspec_restrict);

  // Constructors
  RestrictAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  RestrictAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Spelling getSemanticSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Restrict; }
};

class RetainAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_retain = 0,
    Bracket_gnu_retain = 1,
    C23_gnu_retain = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static RetainAttr *CreateImplicit(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo);
  static RetainAttr *Create(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo);
  static RetainAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                    Spelling S = GNU_retain);
  static RetainAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                            Spelling S = GNU_retain);

  // Constructors
  RetainAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  RetainAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Retain; }
};

class ReturnsNonNullAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_returns_nonnull = 0,
    Bracket_gnu_returns_nonnull = 1,
    C23_gnu_returns_nonnull = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ReturnsNonNullAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static ReturnsNonNullAttr *Create(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo);
  static ReturnsNonNullAttr *CreateImplicit(TreeContext &Ctx,
                                            SourceRange Range = {},
                                            Spelling S = GNU_returns_nonnull);
  static ReturnsNonNullAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                    Spelling S = GNU_returns_nonnull);

  // Constructors
  ReturnsNonNullAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  ReturnsNonNullAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::ReturnsNonNull;
  }
};

class ReturnsTwiceAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_returns_twice = 0,
    Bracket_gnu_returns_twice = 1,
    C23_gnu_returns_twice = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ReturnsTwiceAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static ReturnsTwiceAttr *Create(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static ReturnsTwiceAttr *CreateImplicit(TreeContext &Ctx,
                                          SourceRange Range = {},
                                          Spelling S = GNU_returns_twice);
  static ReturnsTwiceAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                  Spelling S = GNU_returns_twice);

  // Constructors
  ReturnsTwiceAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  ReturnsTwiceAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::ReturnsTwice;
  }
};

class SPtrAttr : public TypeAttr {
public:
  // Factory methods
  static SPtrAttr *CreateImplicit(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static SPtrAttr *Create(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo);
  static SPtrAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {});
  static SPtrAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  SPtrAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  SPtrAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::SPtr; }
};

class SectionAttr : public InheritableAttr {
  unsigned nameLength;
  char *name;

public:
  enum Spelling {
    GNU_section = 0,
    Bracket_gnu_section = 1,
    C23_gnu_section = 2,
    Declspec_allocate = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static SectionAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                                     const AttributeCommonInfo &CommonInfo);
  static SectionAttr *Create(TreeContext &Ctx, llvm::StringRef Name,
                             const AttributeCommonInfo &CommonInfo);
  static SectionAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                                     SourceRange Range = {},
                                     Spelling S = GNU_section);
  static SectionAttr *Create(TreeContext &Ctx, llvm::StringRef Name,
                             SourceRange Range = {}, Spelling S = GNU_section);

  // Constructors
  SectionAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
              llvm::StringRef Name);

  SectionAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Spelling getSemanticSpelling() const;
  llvm::StringRef getName() const { return llvm::StringRef(name, nameLength); }
  unsigned getNameLength() const { return nameLength; }
  void setName(TreeContext &C, llvm::StringRef S) {
    nameLength = S.size();
    this->name = new (C, 1) char[nameLength];
    if (!S.empty())
      std::memcpy(this->name, S.data(), nameLength);
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::Section; }
};

class SelectAnyAttr : public InheritableAttr {
public:
  enum Spelling {
    Declspec_selectany = 0,
    GNU_selectany = 1,
    Bracket_gnu_selectany = 2,
    C23_gnu_selectany = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static SelectAnyAttr *CreateImplicit(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo);
  static SelectAnyAttr *Create(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo);
  static SelectAnyAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                       Spelling S = Declspec_selectany);
  static SelectAnyAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                               Spelling S = Declspec_selectany);

  // Constructors
  SelectAnyAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  SelectAnyAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::SelectAny; }
};

class SentinelAttr : public InheritableAttr {
  int sentinel;

  int nullPos;

public:
  enum Spelling {
    GNU_sentinel = 0,
    Bracket_gnu_sentinel = 1,
    C23_gnu_sentinel = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static SentinelAttr *CreateImplicit(TreeContext &Ctx, int Sentinel,
                                      int NullPos,
                                      const AttributeCommonInfo &CommonInfo);
  static SentinelAttr *Create(TreeContext &Ctx, int Sentinel, int NullPos,
                              const AttributeCommonInfo &CommonInfo);
  static SentinelAttr *CreateImplicit(TreeContext &Ctx, int Sentinel,
                                      int NullPos, SourceRange Range = {},
                                      Spelling S = GNU_sentinel);
  static SentinelAttr *Create(TreeContext &Ctx, int Sentinel, int NullPos,
                              SourceRange Range = {},
                              Spelling S = GNU_sentinel);

  // Constructors
  SentinelAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
               int Sentinel, int NullPos);
  SentinelAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  SentinelAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  int getSentinel() const { return sentinel; }

  static const int DefaultSentinel = 0;

  int getNullPos() const { return nullPos; }

  static const int DefaultNullPos = 0;

  static bool classof(const Attr *A) { return A->getKind() == attr::Sentinel; }
};

class SpeculativeLoadHardeningAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_speculative_load_hardening = 0,
    Bracket_clang_speculative_load_hardening = 1,
    Bracket_neverc_speculative_load_hardening = 2,
    C23_clang_speculative_load_hardening = 3,
    C23_neverc_speculative_load_hardening = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static SpeculativeLoadHardeningAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static SpeculativeLoadHardeningAttr *
  Create(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static SpeculativeLoadHardeningAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_speculative_load_hardening);
  static SpeculativeLoadHardeningAttr *
  Create(TreeContext &Ctx, SourceRange Range = {},
         Spelling S = GNU_speculative_load_hardening);

  // Constructors
  SpeculativeLoadHardeningAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo);

  SpeculativeLoadHardeningAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::SpeculativeLoadHardening;
  }
};

class StandardNoReturnAttr : public InheritableAttr {
public:
  enum Spelling {
    Bracket_noreturn = 0,
    C23_noreturn = 1,
    C23_Noreturn = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static StandardNoReturnAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static StandardNoReturnAttr *Create(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static StandardNoReturnAttr *CreateImplicit(TreeContext &Ctx,
                                              SourceRange Range = {},
                                              Spelling S = Bracket_noreturn);
  static StandardNoReturnAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = Bracket_noreturn);

  // Constructors
  StandardNoReturnAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  StandardNoReturnAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Spelling getSemanticSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::StandardNoReturn;
  }
};

class StdCallAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_stdcall = 0,
    Bracket_gnu_stdcall = 1,
    C23_gnu_stdcall = 2,
    Keyword_stdcall = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static StdCallAttr *CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static StdCallAttr *Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo);
  static StdCallAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_stdcall);
  static StdCallAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                             Spelling S = GNU_stdcall);

  // Constructors
  StdCallAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  StdCallAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::StdCall; }
};

class StrictFPAttr : public InheritableAttr {
public:
  // Factory methods
  static StrictFPAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static StrictFPAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static StrictFPAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {});
  static StrictFPAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  StrictFPAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  StrictFPAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::StrictFP; }
};

class StrictGuardStackCheckAttr : public InheritableAttr {
public:
  // Factory methods
  static StrictGuardStackCheckAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static StrictGuardStackCheckAttr *
  Create(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static StrictGuardStackCheckAttr *CreateImplicit(TreeContext &Ctx,
                                                   SourceRange Range = {});
  static StrictGuardStackCheckAttr *Create(TreeContext &Ctx,
                                           SourceRange Range = {});

  // Constructors
  StrictGuardStackCheckAttr(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo);

  StrictGuardStackCheckAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::StrictGuardStackCheck;
  }
};

class SuppressAttr : public DeclOrStmtAttr {
  unsigned diagnosticIdentifiers_Size;
  StringRef *diagnosticIdentifiers_;

public:
  enum Spelling {
    Bracket_gsl_suppress = 0,
    GNU_suppress = 1,
    Bracket_clang_suppress = 2,
    Bracket_neverc_suppress = 3,
    C23_clang_suppress = 4,
    C23_neverc_suppress = 5,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static SuppressAttr *CreateImplicit(TreeContext &Ctx,
                                      StringRef *DiagnosticIdentifiers,
                                      unsigned DiagnosticIdentifiersSize,
                                      const AttributeCommonInfo &CommonInfo);
  static SuppressAttr *Create(TreeContext &Ctx,
                              StringRef *DiagnosticIdentifiers,
                              unsigned DiagnosticIdentifiersSize,
                              const AttributeCommonInfo &CommonInfo);
  static SuppressAttr *CreateImplicit(TreeContext &Ctx,
                                      StringRef *DiagnosticIdentifiers,
                                      unsigned DiagnosticIdentifiersSize,
                                      SourceRange Range = {},
                                      Spelling S = Bracket_gsl_suppress);
  static SuppressAttr *Create(TreeContext &Ctx,
                              StringRef *DiagnosticIdentifiers,
                              unsigned DiagnosticIdentifiersSize,
                              SourceRange Range = {},
                              Spelling S = Bracket_gsl_suppress);

  // Constructors
  SuppressAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
               StringRef *DiagnosticIdentifiers,
               unsigned DiagnosticIdentifiersSize);
  SuppressAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  SuppressAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  bool isGSL() const { return getAttributeSpellingListIndex() == 0; }
  typedef StringRef *diagnosticIdentifiers_iterator;
  diagnosticIdentifiers_iterator diagnosticIdentifiers_begin() const {
    return diagnosticIdentifiers_;
  }
  diagnosticIdentifiers_iterator diagnosticIdentifiers_end() const {
    return diagnosticIdentifiers_ + diagnosticIdentifiers_Size;
  }
  unsigned diagnosticIdentifiers_size() const {
    return diagnosticIdentifiers_Size;
  }
  llvm::iterator_range<diagnosticIdentifiers_iterator>
  diagnosticIdentifiers() const {
    return llvm::make_range(diagnosticIdentifiers_begin(),
                            diagnosticIdentifiers_end());
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::Suppress; }
};

class SysVABIAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_sysv_abi = 0,
    Bracket_gnu_sysv_abi = 1,
    C23_gnu_sysv_abi = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static SysVABIAttr *CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo);
  static SysVABIAttr *Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo);
  static SysVABIAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                     Spelling S = GNU_sysv_abi);
  static SysVABIAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                             Spelling S = GNU_sysv_abi);

  // Constructors
  SysVABIAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  SysVABIAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::SysVABI; }
};

class TLSModelAttr : public InheritableAttr {
  unsigned modelLength;
  char *model;

public:
  enum Spelling {
    GNU_tls_model = 0,
    Bracket_gnu_tls_model = 1,
    C23_gnu_tls_model = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static TLSModelAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Model,
                                      const AttributeCommonInfo &CommonInfo);
  static TLSModelAttr *Create(TreeContext &Ctx, llvm::StringRef Model,
                              const AttributeCommonInfo &CommonInfo);
  static TLSModelAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Model,
                                      SourceRange Range = {},
                                      Spelling S = GNU_tls_model);
  static TLSModelAttr *Create(TreeContext &Ctx, llvm::StringRef Model,
                              SourceRange Range = {},
                              Spelling S = GNU_tls_model);

  // Constructors
  TLSModelAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
               llvm::StringRef Model);

  TLSModelAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getModel() const {
    return llvm::StringRef(model, modelLength);
  }
  unsigned getModelLength() const { return modelLength; }
  void setModel(TreeContext &C, llvm::StringRef S) {
    modelLength = S.size();
    this->model = new (C, 1) char[modelLength];
    if (!S.empty())
      std::memcpy(this->model, S.data(), modelLength);
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::TLSModel; }
};

class TargetAttr : public InheritableAttr {
  unsigned featuresStrLength;
  char *featuresStr;

public:
  enum Spelling {
    GNU_target = 0,
    Bracket_gnu_target = 1,
    C23_gnu_target = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static TargetAttr *CreateImplicit(TreeContext &Ctx,
                                    llvm::StringRef FeaturesStr,
                                    const AttributeCommonInfo &CommonInfo);
  static TargetAttr *Create(TreeContext &Ctx, llvm::StringRef FeaturesStr,
                            const AttributeCommonInfo &CommonInfo);
  static TargetAttr *CreateImplicit(TreeContext &Ctx,
                                    llvm::StringRef FeaturesStr,
                                    SourceRange Range = {},
                                    Spelling S = GNU_target);
  static TargetAttr *Create(TreeContext &Ctx, llvm::StringRef FeaturesStr,
                            SourceRange Range = {}, Spelling S = GNU_target);

  // Constructors
  TargetAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
             llvm::StringRef FeaturesStr);

  TargetAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getFeaturesStr() const {
    return llvm::StringRef(featuresStr, featuresStrLength);
  }
  unsigned getFeaturesStrLength() const { return featuresStrLength; }
  void setFeaturesStr(TreeContext &C, llvm::StringRef S) {
    featuresStrLength = S.size();
    this->featuresStr = new (C, 1) char[featuresStrLength];
    if (!S.empty())
      std::memcpy(this->featuresStr, S.data(), featuresStrLength);
  }

  StringRef getArchitecture() const {
    StringRef Features = getFeaturesStr();
    if (Features == "default")
      return {};

    SmallVector<StringRef, 1> AttrFeatures;
    Features.split(AttrFeatures, ",");

    for (auto &Feature : AttrFeatures) {
      Feature = Feature.trim();
      if (Feature.starts_with("arch="))
        return Feature.drop_front(sizeof("arch=") - 1);
    }
    return "";
  }

  // Gets the list of features as simple string-refs with no +/- or 'no-'.
  // Only adds the items to 'Out' that are additions.
  void getAddedFeatures(llvm::SmallVectorImpl<StringRef> &Out) const {
    StringRef Features = getFeaturesStr();
    if (Features == "default")
      return;

    SmallVector<StringRef, 1> AttrFeatures;
    Features.split(AttrFeatures, ",");

    for (auto &Feature : AttrFeatures) {
      Feature = Feature.trim();

      if (!Feature.starts_with("no-") && !Feature.starts_with("arch=") &&
          !Feature.starts_with("fpmath=") && !Feature.starts_with("tune="))
        Out.push_back(Feature);
    }
  }

  bool isDefaultVersion() const { return getFeaturesStr() == "default"; }

  static bool classof(const Attr *A) { return A->getKind() == attr::Target; }
};

class TargetClonesAttr : public InheritableAttr {
  unsigned featuresStrs_Size;
  StringRef *featuresStrs_;

public:
  enum Spelling {
    GNU_target_clones = 0,
    Bracket_gnu_target_clones = 1,
    C23_gnu_target_clones = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static TargetClonesAttr *
  CreateImplicit(TreeContext &Ctx, StringRef *FeaturesStrs,
                 unsigned FeaturesStrsSize,
                 const AttributeCommonInfo &CommonInfo);
  static TargetClonesAttr *Create(TreeContext &Ctx, StringRef *FeaturesStrs,
                                  unsigned FeaturesStrsSize,
                                  const AttributeCommonInfo &CommonInfo);
  static TargetClonesAttr *CreateImplicit(TreeContext &Ctx,
                                          StringRef *FeaturesStrs,
                                          unsigned FeaturesStrsSize,
                                          SourceRange Range = {},
                                          Spelling S = GNU_target_clones);
  static TargetClonesAttr *Create(TreeContext &Ctx, StringRef *FeaturesStrs,
                                  unsigned FeaturesStrsSize,
                                  SourceRange Range = {},
                                  Spelling S = GNU_target_clones);

  // Constructors
  TargetClonesAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                   StringRef *FeaturesStrs, unsigned FeaturesStrsSize);
  TargetClonesAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  TargetClonesAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  typedef StringRef *featuresStrs_iterator;
  featuresStrs_iterator featuresStrs_begin() const { return featuresStrs_; }
  featuresStrs_iterator featuresStrs_end() const {
    return featuresStrs_ + featuresStrs_Size;
  }
  unsigned featuresStrs_size() const { return featuresStrs_Size; }
  llvm::iterator_range<featuresStrs_iterator> featuresStrs() const {
    return llvm::make_range(featuresStrs_begin(), featuresStrs_end());
  }

  StringRef getFeatureStr(unsigned Index) const {
    return *(featuresStrs_begin() + Index);
  }
  // Given an index into the 'featuresStrs' sequence, compute a unique
  // ID to be used with function name mangling for the associated variant.
  // This mapping is necessary due to a requirement that the mangling ID
  // used for the "default" variant be the largest mangling ID in the
  // variant set. Duplicate variants present in 'featuresStrs' are also
  // assigned their own unique ID (the mapping is bijective).
  unsigned getMangledIndex(unsigned Index) const {
    if (getFeatureStr(Index) == "default")
      return std::count_if(featuresStrs_begin(), featuresStrs_end(),
                           [](StringRef S) { return S != "default"; });

    return std::count_if(featuresStrs_begin(), featuresStrs_begin() + Index,
                         [](StringRef S) { return S != "default"; });
  }

  // Given an index into the 'featuresStrs' sequence, determine if the
  // index corresponds to the first instance of the named variant. This
  // is used to skip over duplicate variant instances when iterating over
  // 'featuresStrs'.
  bool isFirstOfVersion(unsigned Index) const {
    StringRef FeatureStr(getFeatureStr(Index));
    return 0 ==
           std::count_if(featuresStrs_begin(), featuresStrs_begin() + Index,
                         [FeatureStr](StringRef S) { return S == FeatureStr; });
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::TargetClones;
  }
};

class TargetVersionAttr : public InheritableAttr {
  unsigned namesStrLength;
  char *namesStr;

public:
  enum Spelling {
    GNU_target_version = 0,
    Bracket_gnu_target_version = 1,
    C23_gnu_target_version = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static TargetVersionAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef NamesStr,
                 const AttributeCommonInfo &CommonInfo);
  static TargetVersionAttr *Create(TreeContext &Ctx, llvm::StringRef NamesStr,
                                   const AttributeCommonInfo &CommonInfo);
  static TargetVersionAttr *CreateImplicit(TreeContext &Ctx,
                                           llvm::StringRef NamesStr,
                                           SourceRange Range = {},
                                           Spelling S = GNU_target_version);
  static TargetVersionAttr *Create(TreeContext &Ctx, llvm::StringRef NamesStr,
                                   SourceRange Range = {},
                                   Spelling S = GNU_target_version);

  // Constructors
  TargetVersionAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                    llvm::StringRef NamesStr);

  TargetVersionAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getNamesStr() const {
    return llvm::StringRef(namesStr, namesStrLength);
  }
  unsigned getNamesStrLength() const { return namesStrLength; }
  void setNamesStr(TreeContext &C, llvm::StringRef S) {
    namesStrLength = S.size();
    this->namesStr = new (C, 1) char[namesStrLength];
    if (!S.empty())
      std::memcpy(this->namesStr, S.data(), namesStrLength);
  }

  StringRef getName() const { return getNamesStr().trim(); }
  bool isDefaultVersion() const { return getName() == "default"; }
  void getFeatures(llvm::SmallVectorImpl<StringRef> &Out) const {
    if (isDefaultVersion())
      return;
    StringRef Features = getName();

    SmallVector<StringRef, 8> AttrFeatures;
    Features.split(AttrFeatures, "+");

    for (auto &Feature : AttrFeatures) {
      Feature = Feature.trim();
      Out.push_back(Feature);
    }
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::TargetVersion;
  }
};

class ThreadAttr : public Attr {
public:
  // Factory methods
  static ThreadAttr *CreateImplicit(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo);
  static ThreadAttr *Create(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo);
  static ThreadAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {});
  static ThreadAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  ThreadAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  ThreadAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Thread; }
};

class TransparentUnionAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_transparent_union = 0,
    Bracket_gnu_transparent_union = 1,
    C23_gnu_transparent_union = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static TransparentUnionAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static TransparentUnionAttr *Create(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static TransparentUnionAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_transparent_union);
  static TransparentUnionAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_transparent_union);

  // Constructors
  TransparentUnionAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  TransparentUnionAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::TransparentUnion;
  }
};

class TypeNonNullAttr : public TypeAttr {
public:
  // Factory methods
  static TypeNonNullAttr *CreateImplicit(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo);
  static TypeNonNullAttr *Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo);
  static TypeNonNullAttr *CreateImplicit(TreeContext &Ctx,
                                         SourceRange Range = {});
  static TypeNonNullAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  TypeNonNullAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  TypeNonNullAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::TypeNonNull;
  }
};

class TypeNullUnspecifiedAttr : public TypeAttr {
public:
  // Factory methods
  static TypeNullUnspecifiedAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static TypeNullUnspecifiedAttr *Create(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo);
  static TypeNullUnspecifiedAttr *CreateImplicit(TreeContext &Ctx,
                                                 SourceRange Range = {});
  static TypeNullUnspecifiedAttr *Create(TreeContext &Ctx,
                                         SourceRange Range = {});

  // Constructors
  TypeNullUnspecifiedAttr(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo);

  TypeNullUnspecifiedAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::TypeNullUnspecified;
  }
};

class TypeNullableAttr : public TypeAttr {
public:
  // Factory methods
  static TypeNullableAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static TypeNullableAttr *Create(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static TypeNullableAttr *CreateImplicit(TreeContext &Ctx,
                                          SourceRange Range = {});
  static TypeNullableAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  TypeNullableAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  TypeNullableAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::TypeNullable;
  }
};

class TypeTagForDatatypeAttr : public InheritableAttr {
  IdentifierInfo *argumentKind;

  TypeSourceInfo *matchingCType;

  bool layoutCompatible;

  bool mustBeNull;

public:
  enum Spelling {
    GNU_type_tag_for_datatype = 0,
    Bracket_clang_type_tag_for_datatype = 1,
    Bracket_neverc_type_tag_for_datatype = 2,
    C23_clang_type_tag_for_datatype = 3,
    C23_neverc_type_tag_for_datatype = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static TypeTagForDatatypeAttr *
  CreateImplicit(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
                 TypeSourceInfo *MatchingCType, bool LayoutCompatible,
                 bool MustBeNull, const AttributeCommonInfo &CommonInfo);
  static TypeTagForDatatypeAttr *Create(TreeContext &Ctx,
                                        IdentifierInfo *ArgumentKind,
                                        TypeSourceInfo *MatchingCType,
                                        bool LayoutCompatible, bool MustBeNull,
                                        const AttributeCommonInfo &CommonInfo);
  static TypeTagForDatatypeAttr *
  CreateImplicit(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
                 TypeSourceInfo *MatchingCType, bool LayoutCompatible,
                 bool MustBeNull, SourceRange Range = {},
                 Spelling S = GNU_type_tag_for_datatype);
  static TypeTagForDatatypeAttr *
  Create(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
         TypeSourceInfo *MatchingCType, bool LayoutCompatible, bool MustBeNull,
         SourceRange Range = {}, Spelling S = GNU_type_tag_for_datatype);

  // Constructors
  TypeTagForDatatypeAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo,
                         IdentifierInfo *ArgumentKind,
                         TypeSourceInfo *MatchingCType, bool LayoutCompatible,
                         bool MustBeNull);

  TypeTagForDatatypeAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  IdentifierInfo *getArgumentKind() const { return argumentKind; }

  QualType getMatchingCType() const { return matchingCType->getType(); }
  TypeSourceInfo *getMatchingCTypeLoc() const { return matchingCType; }

  bool getLayoutCompatible() const { return layoutCompatible; }

  bool getMustBeNull() const { return mustBeNull; }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::TypeTagForDatatype;
  }
};

class TypeVisibilityAttr : public InheritableAttr {
public:
  enum VisibilityType { Default, Hidden, Protected };

private:
  TypeVisibilityAttr::VisibilityType visibility;

public:
  enum Spelling {
    GNU_type_visibility = 0,
    Bracket_clang_type_visibility = 1,
    Bracket_neverc_type_visibility = 2,
    C23_clang_type_visibility = 3,
    C23_neverc_type_visibility = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static TypeVisibilityAttr *
  CreateImplicit(TreeContext &Ctx,
                 TypeVisibilityAttr::VisibilityType Visibility,
                 const AttributeCommonInfo &CommonInfo);
  static TypeVisibilityAttr *
  Create(TreeContext &Ctx, TypeVisibilityAttr::VisibilityType Visibility,
         const AttributeCommonInfo &CommonInfo);
  static TypeVisibilityAttr *
  CreateImplicit(TreeContext &Ctx,
                 TypeVisibilityAttr::VisibilityType Visibility,
                 SourceRange Range = {}, Spelling S = GNU_type_visibility);
  static TypeVisibilityAttr *
  Create(TreeContext &Ctx, TypeVisibilityAttr::VisibilityType Visibility,
         SourceRange Range = {}, Spelling S = GNU_type_visibility);

  // Constructors
  TypeVisibilityAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                     TypeVisibilityAttr::VisibilityType Visibility);

  TypeVisibilityAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  TypeVisibilityAttr::VisibilityType getVisibility() const {
    return visibility;
  }

  static bool
  ConvertStrToVisibilityType(StringRef Val,
                             TypeVisibilityAttr::VisibilityType &Out);
  static const char *
  ConvertVisibilityTypeToStr(TypeVisibilityAttr::VisibilityType Val);

  static bool classof(const Attr *A) {
    return A->getKind() == attr::TypeVisibility;
  }
};

class UPtrAttr : public TypeAttr {
public:
  // Factory methods
  static UPtrAttr *CreateImplicit(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static UPtrAttr *Create(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo);
  static UPtrAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {});
  static UPtrAttr *Create(TreeContext &Ctx, SourceRange Range = {});

  // Constructors
  UPtrAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  UPtrAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::UPtr; }
};

class UnavailableAttr : public InheritableAttr {
  unsigned messageLength;
  char *message;

public:
  enum ImplicitReason { IR_None };

private:
  UnavailableAttr::ImplicitReason implicitReason;

public:
  enum Spelling {
    GNU_unavailable = 0,
    Bracket_clang_unavailable = 1,
    Bracket_neverc_unavailable = 2,
    C23_clang_unavailable = 3,
    C23_neverc_unavailable = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static UnavailableAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef Message,
                 UnavailableAttr::ImplicitReason ImplicitReason,
                 const AttributeCommonInfo &CommonInfo);
  static UnavailableAttr *Create(TreeContext &Ctx, llvm::StringRef Message,
                                 UnavailableAttr::ImplicitReason ImplicitReason,
                                 const AttributeCommonInfo &CommonInfo);
  static UnavailableAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef Message,
                 UnavailableAttr::ImplicitReason ImplicitReason,
                 SourceRange Range = {}, Spelling S = GNU_unavailable);
  static UnavailableAttr *Create(TreeContext &Ctx, llvm::StringRef Message,
                                 UnavailableAttr::ImplicitReason ImplicitReason,
                                 SourceRange Range = {},
                                 Spelling S = GNU_unavailable);
  static UnavailableAttr *CreateImplicit(TreeContext &Ctx,
                                         llvm::StringRef Message,
                                         const AttributeCommonInfo &CommonInfo);
  static UnavailableAttr *Create(TreeContext &Ctx, llvm::StringRef Message,
                                 const AttributeCommonInfo &CommonInfo);
  static UnavailableAttr *CreateImplicit(TreeContext &Ctx,
                                         llvm::StringRef Message,
                                         SourceRange Range = {},
                                         Spelling S = GNU_unavailable);
  static UnavailableAttr *Create(TreeContext &Ctx, llvm::StringRef Message,
                                 SourceRange Range = {},
                                 Spelling S = GNU_unavailable);

  // Constructors
  UnavailableAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                  llvm::StringRef Message,
                  UnavailableAttr::ImplicitReason ImplicitReason);
  UnavailableAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                  llvm::StringRef Message);
  UnavailableAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  UnavailableAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getMessage() const {
    return llvm::StringRef(message, messageLength);
  }
  unsigned getMessageLength() const { return messageLength; }
  void setMessage(TreeContext &C, llvm::StringRef S) {
    messageLength = S.size();
    this->message = new (C, 1) char[messageLength];
    if (!S.empty())
      std::memcpy(this->message, S.data(), messageLength);
  }

  UnavailableAttr::ImplicitReason getImplicitReason() const {
    return implicitReason;
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::Unavailable;
  }
};

class UninitializedAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_uninitialized = 0,
    Bracket_clang_uninitialized = 1,
    Bracket_neverc_uninitialized = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static UninitializedAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static UninitializedAttr *Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo);
  static UninitializedAttr *CreateImplicit(TreeContext &Ctx,
                                           SourceRange Range = {},
                                           Spelling S = GNU_uninitialized);
  static UninitializedAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                   Spelling S = GNU_uninitialized);

  // Constructors
  UninitializedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  UninitializedAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::Uninitialized;
  }
};

class UnlikelyAttr : public StmtAttr {
public:
  enum Spelling {
    Bracket_unlikely = 0,
    C23_neverc_unlikely = 1,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static UnlikelyAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static UnlikelyAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static UnlikelyAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = Bracket_unlikely);
  static UnlikelyAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = Bracket_unlikely);

  // Constructors
  UnlikelyAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  UnlikelyAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Unlikely; }
};

class UnsafeBufferUsageAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_unsafe_buffer_usage = 0,
    Bracket_clang_unsafe_buffer_usage = 1,
    Bracket_neverc_unsafe_buffer_usage = 2,
    C23_clang_unsafe_buffer_usage = 3,
    C23_neverc_unsafe_buffer_usage = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static UnsafeBufferUsageAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static UnsafeBufferUsageAttr *Create(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo);
  static UnsafeBufferUsageAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_unsafe_buffer_usage);
  static UnsafeBufferUsageAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                       Spelling S = GNU_unsafe_buffer_usage);

  // Constructors
  UnsafeBufferUsageAttr(TreeContext &Ctx,
                        const AttributeCommonInfo &CommonInfo);

  UnsafeBufferUsageAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::UnsafeBufferUsage;
  }
};

class UnusedAttr : public InheritableAttr {
public:
  enum Spelling {
    Bracket_maybe_unused = 0,
    GNU_unused = 1,
    Bracket_gnu_unused = 2,
    C23_gnu_unused = 3,
    C23_maybe_unused = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static UnusedAttr *CreateImplicit(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo);
  static UnusedAttr *Create(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo);
  static UnusedAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                    Spelling S = Bracket_maybe_unused);
  static UnusedAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                            Spelling S = Bracket_maybe_unused);

  // Constructors
  UnusedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  UnusedAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Spelling getSemanticSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Unused; }
};

class UseHandleAttr : public InheritableParamAttr {
  unsigned handleTypeLength;
  char *handleType;

public:
  enum Spelling {
    GNU_use_handle = 0,
    Bracket_clang_use_handle = 1,
    Bracket_neverc_use_handle = 2,
    C23_clang_use_handle = 3,
    C23_neverc_use_handle = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static UseHandleAttr *CreateImplicit(TreeContext &Ctx,
                                       llvm::StringRef HandleType,
                                       const AttributeCommonInfo &CommonInfo);
  static UseHandleAttr *Create(TreeContext &Ctx, llvm::StringRef HandleType,
                               const AttributeCommonInfo &CommonInfo);
  static UseHandleAttr *CreateImplicit(TreeContext &Ctx,
                                       llvm::StringRef HandleType,
                                       SourceRange Range = {},
                                       Spelling S = GNU_use_handle);
  static UseHandleAttr *Create(TreeContext &Ctx, llvm::StringRef HandleType,
                               SourceRange Range = {},
                               Spelling S = GNU_use_handle);

  // Constructors
  UseHandleAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                llvm::StringRef HandleType);

  UseHandleAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getHandleType() const {
    return llvm::StringRef(handleType, handleTypeLength);
  }
  unsigned getHandleTypeLength() const { return handleTypeLength; }
  void setHandleType(TreeContext &C, llvm::StringRef S) {
    handleTypeLength = S.size();
    this->handleType = new (C, 1) char[handleTypeLength];
    if (!S.empty())
      std::memcpy(this->handleType, S.data(), handleTypeLength);
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::UseHandle; }
};

class UsedAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_used = 0,
    Bracket_gnu_used = 1,
    C23_gnu_used = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static UsedAttr *CreateImplicit(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static UsedAttr *Create(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo);
  static UsedAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                  Spelling S = GNU_used);
  static UsedAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                          Spelling S = GNU_used);

  // Constructors
  UsedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  UsedAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Used; }
};

class VectorCallAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_vectorcall = 0,
    Bracket_clang_vectorcall = 1,
    Bracket_neverc_vectorcall = 2,
    C23_clang_vectorcall = 3,
    C23_neverc_vectorcall = 4,
    Keyword_vectorcall = 5,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static VectorCallAttr *CreateImplicit(TreeContext &Ctx,
                                        const AttributeCommonInfo &CommonInfo);
  static VectorCallAttr *Create(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo);
  static VectorCallAttr *CreateImplicit(TreeContext &Ctx,
                                        SourceRange Range = {},
                                        Spelling S = GNU_vectorcall);
  static VectorCallAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                Spelling S = GNU_vectorcall);

  // Constructors
  VectorCallAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  VectorCallAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::VectorCall;
  }
};

class VisibilityAttr : public InheritableAttr {
public:
  enum VisibilityType { Default, Hidden, Protected };

private:
  VisibilityAttr::VisibilityType visibility;

public:
  enum Spelling {
    GNU_visibility = 0,
    Bracket_gnu_visibility = 1,
    C23_gnu_visibility = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static VisibilityAttr *
  CreateImplicit(TreeContext &Ctx, VisibilityAttr::VisibilityType Visibility,
                 const AttributeCommonInfo &CommonInfo);
  static VisibilityAttr *Create(TreeContext &Ctx,
                                VisibilityAttr::VisibilityType Visibility,
                                const AttributeCommonInfo &CommonInfo);
  static VisibilityAttr *
  CreateImplicit(TreeContext &Ctx, VisibilityAttr::VisibilityType Visibility,
                 SourceRange Range = {}, Spelling S = GNU_visibility);
  static VisibilityAttr *Create(TreeContext &Ctx,
                                VisibilityAttr::VisibilityType Visibility,
                                SourceRange Range = {},
                                Spelling S = GNU_visibility);

  // Constructors
  VisibilityAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                 VisibilityAttr::VisibilityType Visibility);

  VisibilityAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  VisibilityAttr::VisibilityType getVisibility() const { return visibility; }

  static bool ConvertStrToVisibilityType(StringRef Val,
                                         VisibilityAttr::VisibilityType &Out);
  static const char *
  ConvertVisibilityTypeToStr(VisibilityAttr::VisibilityType Val);

  static bool classof(const Attr *A) {
    return A->getKind() == attr::Visibility;
  }
};

class VolatileAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_volatile = 0,
    Bracket_gnu_volatile = 1,
    C23_gnu_volatile = 2,
    Declspec_volatile = 3,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static VolatileAttr *CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo);
  static VolatileAttr *Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);
  static VolatileAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                      Spelling S = GNU_volatile);
  static VolatileAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                              Spelling S = GNU_volatile);

  // Constructors
  VolatileAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  VolatileAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Volatile; }
};

class WarnUnusedAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_warn_unused = 0,
    Bracket_gnu_warn_unused = 1,
    C23_gnu_warn_unused = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static WarnUnusedAttr *CreateImplicit(TreeContext &Ctx,
                                        const AttributeCommonInfo &CommonInfo);
  static WarnUnusedAttr *Create(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo);
  static WarnUnusedAttr *CreateImplicit(TreeContext &Ctx,
                                        SourceRange Range = {},
                                        Spelling S = GNU_warn_unused);
  static WarnUnusedAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                Spelling S = GNU_warn_unused);

  // Constructors
  WarnUnusedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  WarnUnusedAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::WarnUnused;
  }
};

class WarnUnusedResultAttr : public InheritableAttr {
  unsigned messageLength;
  char *message;

public:
  enum Spelling {
    Bracket_nodiscard = 0,
    C23_nodiscard = 1,
    Bracket_neverc_warn_unused_result = 2,
    GNU_warn_unused_result = 3,
    Bracket_gnu_warn_unused_result = 4,
    C23_gnu_warn_unused_result = 5,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static WarnUnusedResultAttr *
  CreateImplicit(TreeContext &Ctx, llvm::StringRef Message,
                 const AttributeCommonInfo &CommonInfo);
  static WarnUnusedResultAttr *Create(TreeContext &Ctx, llvm::StringRef Message,
                                      const AttributeCommonInfo &CommonInfo);
  static WarnUnusedResultAttr *CreateImplicit(TreeContext &Ctx,
                                              llvm::StringRef Message,
                                              SourceRange Range = {},
                                              Spelling S = Bracket_nodiscard);
  static WarnUnusedResultAttr *Create(TreeContext &Ctx, llvm::StringRef Message,
                                      SourceRange Range = {},
                                      Spelling S = Bracket_nodiscard);

  // Constructors
  WarnUnusedResultAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                       llvm::StringRef Message);
  WarnUnusedResultAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  WarnUnusedResultAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  Spelling getSemanticSpelling() const;
  llvm::StringRef getMessage() const {
    return llvm::StringRef(message, messageLength);
  }
  unsigned getMessageLength() const { return messageLength; }
  void setMessage(TreeContext &C, llvm::StringRef S) {
    messageLength = S.size();
    this->message = new (C, 1) char[messageLength];
    if (!S.empty())
      std::memcpy(this->message, S.data(), messageLength);
  }

  static bool classof(const Attr *A) {
    return A->getKind() == attr::WarnUnusedResult;
  }
};

class WeakAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_weak = 0,
    Bracket_gnu_weak = 1,
    C23_gnu_weak = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static WeakAttr *CreateImplicit(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo);
  static WeakAttr *Create(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo);
  static WeakAttr *CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                                  Spelling S = GNU_weak);
  static WeakAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                          Spelling S = GNU_weak);

  // Constructors
  WeakAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  WeakAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) { return A->getKind() == attr::Weak; }
};

class WeakImportAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_weak_import = 0,
    Bracket_clang_weak_import = 1,
    Bracket_neverc_weak_import = 2,
    C23_clang_weak_import = 3,
    C23_neverc_weak_import = 4,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static WeakImportAttr *CreateImplicit(TreeContext &Ctx,
                                        const AttributeCommonInfo &CommonInfo);
  static WeakImportAttr *Create(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo);
  static WeakImportAttr *CreateImplicit(TreeContext &Ctx,
                                        SourceRange Range = {},
                                        Spelling S = GNU_weak_import);
  static WeakImportAttr *Create(TreeContext &Ctx, SourceRange Range = {},
                                Spelling S = GNU_weak_import);

  // Constructors
  WeakImportAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  WeakImportAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::WeakImport;
  }
};

class WeakRefAttr : public InheritableAttr {
  unsigned aliaseeLength;
  char *aliasee;

public:
  enum Spelling {
    GNU_weakref = 0,
    Bracket_gnu_weakref = 1,
    C23_gnu_weakref = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static WeakRefAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Aliasee,
                                     const AttributeCommonInfo &CommonInfo);
  static WeakRefAttr *Create(TreeContext &Ctx, llvm::StringRef Aliasee,
                             const AttributeCommonInfo &CommonInfo);
  static WeakRefAttr *CreateImplicit(TreeContext &Ctx, llvm::StringRef Aliasee,
                                     SourceRange Range = {},
                                     Spelling S = GNU_weakref);
  static WeakRefAttr *Create(TreeContext &Ctx, llvm::StringRef Aliasee,
                             SourceRange Range = {}, Spelling S = GNU_weakref);

  // Constructors
  WeakRefAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
              llvm::StringRef Aliasee);
  WeakRefAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);

  WeakRefAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  llvm::StringRef getAliasee() const {
    return llvm::StringRef(aliasee, aliaseeLength);
  }
  unsigned getAliaseeLength() const { return aliaseeLength; }
  void setAliasee(TreeContext &C, llvm::StringRef S) {
    aliaseeLength = S.size();
    this->aliasee = new (C, 1) char[aliaseeLength];
    if (!S.empty())
      std::memcpy(this->aliasee, S.data(), aliaseeLength);
  }

  static bool classof(const Attr *A) { return A->getKind() == attr::WeakRef; }
};

class X86ForceAlignArgPointerAttr : public InheritableAttr {
public:
  enum Spelling {
    GNU_force_align_arg_pointer = 0,
    Bracket_gnu_force_align_arg_pointer = 1,
    C23_gnu_force_align_arg_pointer = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static X86ForceAlignArgPointerAttr *
  CreateImplicit(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static X86ForceAlignArgPointerAttr *
  Create(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo);
  static X86ForceAlignArgPointerAttr *
  CreateImplicit(TreeContext &Ctx, SourceRange Range = {},
                 Spelling S = GNU_force_align_arg_pointer);
  static X86ForceAlignArgPointerAttr *
  Create(TreeContext &Ctx, SourceRange Range = {},
         Spelling S = GNU_force_align_arg_pointer);

  // Constructors
  X86ForceAlignArgPointerAttr(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo);

  X86ForceAlignArgPointerAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;

  static bool classof(const Attr *A) {
    return A->getKind() == attr::X86ForceAlignArgPointer;
  }
};

class ZeroCallUsedRegsAttr : public InheritableAttr {
public:
  enum ZeroCallUsedRegsKind {
    Skip,
    UsedGPRArg,
    UsedGPR,
    UsedArg,
    Used,
    AllGPRArg,
    AllGPR,
    AllArg,
    All
  };

private:
  ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind zeroCallUsedRegs;

public:
  enum Spelling {
    GNU_zero_call_used_regs = 0,
    Bracket_gnu_zero_call_used_regs = 1,
    C23_gnu_zero_call_used_regs = 2,
    SpellingNotCalculated = 15

  };

  // Factory methods
  static ZeroCallUsedRegsAttr *
  CreateImplicit(TreeContext &Ctx,
                 ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind ZeroCallUsedRegs,
                 const AttributeCommonInfo &CommonInfo);
  static ZeroCallUsedRegsAttr *
  Create(TreeContext &Ctx,
         ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind ZeroCallUsedRegs,
         const AttributeCommonInfo &CommonInfo);
  static ZeroCallUsedRegsAttr *
  CreateImplicit(TreeContext &Ctx,
                 ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind ZeroCallUsedRegs,
                 SourceRange Range = {}, Spelling S = GNU_zero_call_used_regs);
  static ZeroCallUsedRegsAttr *
  Create(TreeContext &Ctx,
         ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind ZeroCallUsedRegs,
         SourceRange Range = {}, Spelling S = GNU_zero_call_used_regs);

  // Constructors
  ZeroCallUsedRegsAttr(
      TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
      ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind ZeroCallUsedRegs);

  ZeroCallUsedRegsAttr *clone(TreeContext &C) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
  const char *getSpelling() const;
  ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind getZeroCallUsedRegs() const {
    return zeroCallUsedRegs;
  }

  static bool ConvertStrToZeroCallUsedRegsKind(
      StringRef Val, ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind &Out);
  static const char *ConvertZeroCallUsedRegsKindToStr(
      ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind Val);

  static bool classof(const Attr *A) {
    return A->getKind() == attr::ZeroCallUsedRegs;
  }
};

#endif // NEVERC_ATTR_CLASSES_INC
