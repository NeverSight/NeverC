
static inline void DelimitAttributeArgument(raw_ostream &OS, bool &IsFirst) {
  if (IsFirst) {
    IsFirst = false;
    OS << "(";
  } else
    OS << ", ";
}

// AArch64SVEPcsAttr implementation

AArch64SVEPcsAttr *
AArch64SVEPcsAttr::CreateImplicit(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AArch64SVEPcsAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AArch64SVEPcsAttr *
AArch64SVEPcsAttr::Create(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AArch64SVEPcsAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AArch64SVEPcsAttr *AArch64SVEPcsAttr::CreateImplicit(TreeContext &Ctx,
                                                     SourceRange Range,
                                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_aarch64_sve_pcs:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_aarch64_sve_pcs, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_aarch64_sve_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_aarch64_sve_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_aarch64_sve_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_aarch64_sve_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_aarch64_sve_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_aarch64_sve_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_aarch64_sve_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_aarch64_sve_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

AArch64SVEPcsAttr *AArch64SVEPcsAttr::Create(TreeContext &Ctx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_aarch64_sve_pcs:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_aarch64_sve_pcs, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_aarch64_sve_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_aarch64_sve_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_aarch64_sve_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_aarch64_sve_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_aarch64_sve_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_aarch64_sve_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_aarch64_sve_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_aarch64_sve_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

AArch64SVEPcsAttr::AArch64SVEPcsAttr(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::AArch64SVEPcs, false, false) {}

AArch64SVEPcsAttr *AArch64SVEPcsAttr::clone(TreeContext &C) const {
  auto *A = new (C) AArch64SVEPcsAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AArch64SVEPcsAttr::printPretty(raw_ostream &OS,
                                    const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((aarch64_sve_pcs";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::aarch64_sve_pcs";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::aarch64_sve_pcs";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::aarch64_sve_pcs";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::aarch64_sve_pcs";
    OS << "]]";
    break;
  }
  }
}

const char *AArch64SVEPcsAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "aarch64_sve_pcs";
  case 1:
    return "aarch64_sve_pcs";
  case 2:
    return "aarch64_sve_pcs";
  case 3:
    return "aarch64_sve_pcs";
  case 4:
    return "aarch64_sve_pcs";
  }
}

// AArch64VectorPcsAttr implementation

AArch64VectorPcsAttr *
AArch64VectorPcsAttr::CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AArch64VectorPcsAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AArch64VectorPcsAttr *
AArch64VectorPcsAttr::Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AArch64VectorPcsAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AArch64VectorPcsAttr *AArch64VectorPcsAttr::CreateImplicit(TreeContext &Ctx,
                                                           SourceRange Range,
                                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_aarch64_vector_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_aarch64_vector_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_aarch64_vector_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_aarch64_vector_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_aarch64_vector_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_aarch64_vector_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_aarch64_vector_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_aarch64_vector_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_aarch64_vector_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_aarch64_vector_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

AArch64VectorPcsAttr *
AArch64VectorPcsAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_aarch64_vector_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_aarch64_vector_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_aarch64_vector_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_aarch64_vector_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_aarch64_vector_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_aarch64_vector_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_aarch64_vector_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_aarch64_vector_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_aarch64_vector_pcs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_aarch64_vector_pcs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

AArch64VectorPcsAttr::AArch64VectorPcsAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::AArch64VectorPcs, false, false) {}

AArch64VectorPcsAttr *AArch64VectorPcsAttr::clone(TreeContext &C) const {
  auto *A = new (C) AArch64VectorPcsAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AArch64VectorPcsAttr::printPretty(raw_ostream &OS,
                                       const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((aarch64_vector_pcs";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::aarch64_vector_pcs";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::aarch64_vector_pcs";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::aarch64_vector_pcs";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::aarch64_vector_pcs";
    OS << "]]";
    break;
  }
  }
}

const char *AArch64VectorPcsAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "aarch64_vector_pcs";
  case 1:
    return "aarch64_vector_pcs";
  case 2:
    return "aarch64_vector_pcs";
  case 3:
    return "aarch64_vector_pcs";
  case 4:
    return "aarch64_vector_pcs";
  }
}

// AcquireHandleAttr implementation

AcquireHandleAttr *
AcquireHandleAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef HandleType,
                                  const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AcquireHandleAttr(Ctx, CommonInfo, HandleType);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AcquireHandleAttr *
AcquireHandleAttr::Create(TreeContext &Ctx, llvm::StringRef HandleType,
                          const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AcquireHandleAttr(Ctx, CommonInfo, HandleType);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AcquireHandleAttr *AcquireHandleAttr::CreateImplicit(TreeContext &Ctx,
                                                     llvm::StringRef HandleType,
                                                     SourceRange Range,
                                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_acquire_handle:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_acquire_handle, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_acquire_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_acquire_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_acquire_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_acquire_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_acquire_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_acquire_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_acquire_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_acquire_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, HandleType, I);
}

AcquireHandleAttr *AcquireHandleAttr::Create(TreeContext &Ctx,
                                             llvm::StringRef HandleType,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_acquire_handle:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_acquire_handle, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_acquire_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_acquire_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_acquire_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_acquire_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_acquire_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_acquire_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_acquire_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_acquire_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, HandleType, I);
}

AcquireHandleAttr::AcquireHandleAttr(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo,
                                     llvm::StringRef HandleType)
    : InheritableAttr(Ctx, CommonInfo, attr::AcquireHandle, false, false),
      handleTypeLength(HandleType.size()),
      handleType(new (Ctx, 1) char[handleTypeLength]) {
  if (!HandleType.empty())
    std::memcpy(handleType, HandleType.data(), handleTypeLength);
}

AcquireHandleAttr *AcquireHandleAttr::clone(TreeContext &C) const {
  auto *A = new (C) AcquireHandleAttr(C, *this, getHandleType());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AcquireHandleAttr::printPretty(raw_ostream &OS,
                                    const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((acquire_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::acquire_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::acquire_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::acquire_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::acquire_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *AcquireHandleAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "acquire_handle";
  case 1:
    return "acquire_handle";
  case 2:
    return "acquire_handle";
  case 3:
    return "acquire_handle";
  case 4:
    return "acquire_handle";
  }
}

// AddressSpaceAttr implementation

AddressSpaceAttr *
AddressSpaceAttr::CreateImplicit(TreeContext &Ctx, int AddressSpace,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AddressSpaceAttr(Ctx, CommonInfo, AddressSpace);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AddressSpaceAttr *
AddressSpaceAttr::Create(TreeContext &Ctx, int AddressSpace,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AddressSpaceAttr(Ctx, CommonInfo, AddressSpace);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AddressSpaceAttr *AddressSpaceAttr::CreateImplicit(TreeContext &Ctx,
                                                   int AddressSpace,
                                                   SourceRange Range,
                                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_address_space:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_address_space, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_address_space:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_address_space,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_address_space:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_address_space,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_address_space:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_address_space,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_address_space:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_address_space,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, AddressSpace, I);
}

AddressSpaceAttr *AddressSpaceAttr::Create(TreeContext &Ctx, int AddressSpace,
                                           SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_address_space:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_address_space, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_address_space:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_address_space,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_address_space:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_address_space,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_address_space:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_address_space,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_address_space:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_address_space,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, AddressSpace, I);
}

AddressSpaceAttr::AddressSpaceAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo,
                                   int AddressSpace)
    : TypeAttr(Ctx, CommonInfo, attr::AddressSpace, false),
      addressSpace(AddressSpace) {}

AddressSpaceAttr *AddressSpaceAttr::clone(TreeContext &C) const {
  auto *A = new (C) AddressSpaceAttr(C, *this, addressSpace);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AddressSpaceAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((address_space";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getAddressSpace() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::address_space";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getAddressSpace() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::address_space";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getAddressSpace() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::address_space";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getAddressSpace() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::address_space";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getAddressSpace() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *AddressSpaceAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "address_space";
  case 1:
    return "address_space";
  case 2:
    return "address_space";
  case 3:
    return "address_space";
  case 4:
    return "address_space";
  }
}

// AliasAttr implementation

AliasAttr *AliasAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Aliasee,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AliasAttr(Ctx, CommonInfo, Aliasee);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AliasAttr *AliasAttr::Create(TreeContext &Ctx, llvm::StringRef Aliasee,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AliasAttr(Ctx, CommonInfo, Aliasee);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AliasAttr *AliasAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Aliasee,
                                     SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_alias,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_alias, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_alias, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Aliasee, I);
}

AliasAttr *AliasAttr::Create(TreeContext &Ctx, llvm::StringRef Aliasee,
                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_alias,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_alias, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_alias, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Aliasee, I);
}

AliasAttr::AliasAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                     llvm::StringRef Aliasee)
    : Attr(Ctx, CommonInfo, attr::Alias, false), aliaseeLength(Aliasee.size()),
      aliasee(new (Ctx, 1) char[aliaseeLength]) {
  if (!Aliasee.empty())
    std::memcpy(aliasee, Aliasee.data(), aliaseeLength);
}

AliasAttr *AliasAttr::clone(TreeContext &C) const {
  auto *A = new (C) AliasAttr(C, *this, getAliasee());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AliasAttr::printPretty(raw_ostream &OS,
                            const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((alias";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAliasee() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::alias";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAliasee() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::alias";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAliasee() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *AliasAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "alias";
  case 1:
    return "alias";
  case 2:
    return "alias";
  }
}

// AlignValueAttr implementation

AlignValueAttr *
AlignValueAttr::CreateImplicit(TreeContext &Ctx, Expr *Alignment,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AlignValueAttr(Ctx, CommonInfo, Alignment);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AlignValueAttr *AlignValueAttr::Create(TreeContext &Ctx, Expr *Alignment,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AlignValueAttr(Ctx, CommonInfo, Alignment);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AlignValueAttr *AlignValueAttr::CreateImplicit(TreeContext &Ctx,
                                               Expr *Alignment,
                                               SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_GNU, 0, false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, Alignment, I);
}

AlignValueAttr *AlignValueAttr::Create(TreeContext &Ctx, Expr *Alignment,
                                       SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_GNU, 0, false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, Alignment, I);
}

AlignValueAttr::AlignValueAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo,
                               Expr *Alignment)
    : Attr(Ctx, CommonInfo, attr::AlignValue, false), alignment(Alignment) {}

AlignValueAttr *AlignValueAttr::clone(TreeContext &C) const {
  auto *A = new (C) AlignValueAttr(C, *this, alignment);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AlignValueAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((align_value";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "";
    getAlignment()->printPretty(OS, nullptr, Policy);
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  }
}

const char *AlignValueAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "align_value";
  }
}

// AlignedAttr implementation

AlignedAttr *
AlignedAttr::CreateImplicit(TreeContext &Ctx, bool IsAlignmentExpr,
                            void *Alignment,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AlignedAttr(Ctx, CommonInfo, IsAlignmentExpr, Alignment);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AlignedAttr *AlignedAttr::Create(TreeContext &Ctx, bool IsAlignmentExpr,
                                 void *Alignment,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AlignedAttr(Ctx, CommonInfo, IsAlignmentExpr, Alignment);
  return A;
}

AlignedAttr *AlignedAttr::CreateImplicit(TreeContext &Ctx, bool IsAlignmentExpr,
                                         void *Alignment, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_aligned:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_aligned,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_aligned:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_aligned, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_aligned:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_aligned, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_align:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_align, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Keyword_alignas:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_alignas, true /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Keyword_Alignas:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_Alignas, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, IsAlignmentExpr, Alignment, I);
}

AlignedAttr *AlignedAttr::Create(TreeContext &Ctx, bool IsAlignmentExpr,
                                 void *Alignment, SourceRange Range,
                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_aligned:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_aligned,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_aligned:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_aligned, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_aligned:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_aligned, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_align:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_align, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Keyword_alignas:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_alignas, true /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Keyword_Alignas:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_Alignas, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, IsAlignmentExpr, Alignment, I);
}

AlignedAttr::AlignedAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo,
                         bool IsAlignmentExpr, void *Alignment)
    : InheritableAttr(Ctx, CommonInfo, attr::Aligned, false, false),
      isalignmentExpr(IsAlignmentExpr) {
  if (isalignmentExpr)
    alignmentExpr = reinterpret_cast<Expr *>(Alignment);
  else
    alignmentType = reinterpret_cast<TypeSourceInfo *>(Alignment);
}

AlignedAttr::AlignedAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Aligned, false, false),
      isalignmentExpr(false) {}

AlignedAttr::Spelling AlignedAttr::getSemanticSpelling() const {
  switch (getAttributeSpellingListIndex()) {
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
bool AlignedAttr::isAlignmentDependent() const {
  if (isalignmentExpr)
    return alignmentExpr && (alignmentExpr->isValueDependent() ||
                             alignmentExpr->isTypeDependent());
  else
    return alignmentType->getType()->isDependentType();
}
bool AlignedAttr::isAlignmentErrorDependent() const {
  if (isalignmentExpr)
    return alignmentExpr && alignmentExpr->containsErrors();
  return alignmentType->getType()->containsErrors();
}

AlignedAttr *AlignedAttr::clone(TreeContext &C) const {
  auto *A = new (C) AlignedAttr(
      C, *this, isalignmentExpr,
      isalignmentExpr ? static_cast<void *>(alignmentExpr) : alignmentType);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AlignedAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((aligned";
    if (!((isalignmentExpr && alignmentExpr) ||
          (!isalignmentExpr && alignmentType)))
      ++TrailingOmittedArgs;
    if (!(!((isalignmentExpr && alignmentExpr) ||
            (!isalignmentExpr && alignmentType)))) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "";
      if (isalignmentExpr && alignmentExpr)
        alignmentExpr->printPretty(OS, nullptr, Policy);
      if (!isalignmentExpr && alignmentType)
        alignmentType->getType().print(OS, Policy);
      OS << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::aligned";
    if (!((isalignmentExpr && alignmentExpr) ||
          (!isalignmentExpr && alignmentType)))
      ++TrailingOmittedArgs;
    if (!(!((isalignmentExpr && alignmentExpr) ||
            (!isalignmentExpr && alignmentType)))) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "";
      if (isalignmentExpr && alignmentExpr)
        alignmentExpr->printPretty(OS, nullptr, Policy);
      if (!isalignmentExpr && alignmentType)
        alignmentType->getType().print(OS, Policy);
      OS << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::aligned";
    if (!((isalignmentExpr && alignmentExpr) ||
          (!isalignmentExpr && alignmentType)))
      ++TrailingOmittedArgs;
    if (!(!((isalignmentExpr && alignmentExpr) ||
            (!isalignmentExpr && alignmentType)))) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "";
      if (isalignmentExpr && alignmentExpr)
        alignmentExpr->printPretty(OS, nullptr, Policy);
      if (!isalignmentExpr && alignmentType)
        alignmentType->getType().print(OS, Policy);
      OS << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __declspec(align";
    if (!((isalignmentExpr && alignmentExpr) ||
          (!isalignmentExpr && alignmentType)))
      ++TrailingOmittedArgs;
    if (!(!((isalignmentExpr && alignmentExpr) ||
            (!isalignmentExpr && alignmentType)))) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "";
      if (isalignmentExpr && alignmentExpr)
        alignmentExpr->printPretty(OS, nullptr, Policy);
      if (!isalignmentExpr && alignmentType)
        alignmentType->getType().print(OS, Policy);
      OS << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << ")";
    break;
  }
  case 4: {
    OS << " alignas";
    if (!((isalignmentExpr && alignmentExpr) ||
          (!isalignmentExpr && alignmentType)))
      ++TrailingOmittedArgs;
    if (!(!((isalignmentExpr && alignmentExpr) ||
            (!isalignmentExpr && alignmentType)))) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "";
      if (isalignmentExpr && alignmentExpr)
        alignmentExpr->printPretty(OS, nullptr, Policy);
      if (!isalignmentExpr && alignmentType)
        alignmentType->getType().print(OS, Policy);
      OS << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "";
    break;
  }
  case 5: {
    OS << " _Alignas";
    if (!((isalignmentExpr && alignmentExpr) ||
          (!isalignmentExpr && alignmentType)))
      ++TrailingOmittedArgs;
    if (!(!((isalignmentExpr && alignmentExpr) ||
            (!isalignmentExpr && alignmentType)))) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "";
      if (isalignmentExpr && alignmentExpr)
        alignmentExpr->printPretty(OS, nullptr, Policy);
      if (!isalignmentExpr && alignmentType)
        alignmentType->getType().print(OS, Policy);
      OS << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "";
    break;
  }
  }
}

const char *AlignedAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "aligned";
  case 1:
    return "aligned";
  case 2:
    return "aligned";
  case 3:
    return "align";
  case 4:
    return "alignas";
  case 5:
    return "_Alignas";
  }
}

// AllocAlignAttr implementation

AllocAlignAttr *
AllocAlignAttr::CreateImplicit(TreeContext &Ctx, ParamIdx ParamIndex,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AllocAlignAttr(Ctx, CommonInfo, ParamIndex);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AllocAlignAttr *AllocAlignAttr::Create(TreeContext &Ctx, ParamIdx ParamIndex,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AllocAlignAttr(Ctx, CommonInfo, ParamIndex);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AllocAlignAttr *AllocAlignAttr::CreateImplicit(TreeContext &Ctx,
                                               ParamIdx ParamIndex,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_alloc_align:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_alloc_align, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_alloc_align:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_alloc_align,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_alloc_align:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_alloc_align, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, ParamIndex, I);
}

AllocAlignAttr *AllocAlignAttr::Create(TreeContext &Ctx, ParamIdx ParamIndex,
                                       SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_alloc_align:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_alloc_align, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_alloc_align:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_alloc_align,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_alloc_align:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_alloc_align, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, ParamIndex, I);
}

AllocAlignAttr::AllocAlignAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo,
                               ParamIdx ParamIndex)
    : InheritableAttr(Ctx, CommonInfo, attr::AllocAlign, false, false),
      paramIndex(ParamIndex) {}

AllocAlignAttr *AllocAlignAttr::clone(TreeContext &C) const {
  auto *A = new (C) AllocAlignAttr(C, *this, paramIndex);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AllocAlignAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((alloc_align";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getParamIndex().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::alloc_align";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getParamIndex().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::alloc_align";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getParamIndex().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *AllocAlignAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "alloc_align";
  case 1:
    return "alloc_align";
  case 2:
    return "alloc_align";
  }
}

// AllocSizeAttr implementation

AllocSizeAttr *
AllocSizeAttr::CreateImplicit(TreeContext &Ctx, ParamIdx ElemSizeParam,
                              ParamIdx NumElemsParam,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A =
      new (Ctx) AllocSizeAttr(Ctx, CommonInfo, ElemSizeParam, NumElemsParam);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AllocSizeAttr *AllocSizeAttr::Create(TreeContext &Ctx, ParamIdx ElemSizeParam,
                                     ParamIdx NumElemsParam,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A =
      new (Ctx) AllocSizeAttr(Ctx, CommonInfo, ElemSizeParam, NumElemsParam);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AllocSizeAttr *AllocSizeAttr::CreateImplicit(TreeContext &Ctx,
                                             ParamIdx ElemSizeParam,
                                             ParamIdx NumElemsParam,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_alloc_size:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_alloc_size, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_alloc_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_alloc_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_alloc_size:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_alloc_size, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, ElemSizeParam, NumElemsParam, I);
}

AllocSizeAttr *AllocSizeAttr::Create(TreeContext &Ctx, ParamIdx ElemSizeParam,
                                     ParamIdx NumElemsParam, SourceRange Range,
                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_alloc_size:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_alloc_size, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_alloc_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_alloc_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_alloc_size:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_alloc_size, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, ElemSizeParam, NumElemsParam, I);
}

AllocSizeAttr::AllocSizeAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo,
                             ParamIdx ElemSizeParam, ParamIdx NumElemsParam)
    : InheritableAttr(Ctx, CommonInfo, attr::AllocSize, false, false),
      elemSizeParam(ElemSizeParam), numElemsParam(NumElemsParam) {}

AllocSizeAttr::AllocSizeAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo,
                             ParamIdx ElemSizeParam)
    : InheritableAttr(Ctx, CommonInfo, attr::AllocSize, false, false),
      elemSizeParam(ElemSizeParam), numElemsParam() {}

AllocSizeAttr *AllocSizeAttr::clone(TreeContext &C) const {
  auto *A = new (C) AllocSizeAttr(C, *this, elemSizeParam, numElemsParam);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AllocSizeAttr::printPretty(raw_ostream &OS,
                                const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((alloc_size";
    if (!getNumElemsParam().isValid())
      ++TrailingOmittedArgs;
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getElemSizeParam().getSourceIndex() << "";
    if (!(!getNumElemsParam().isValid())) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "" << getNumElemsParam().getSourceIndex() << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::alloc_size";
    if (!getNumElemsParam().isValid())
      ++TrailingOmittedArgs;
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getElemSizeParam().getSourceIndex() << "";
    if (!(!getNumElemsParam().isValid())) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "" << getNumElemsParam().getSourceIndex() << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::alloc_size";
    if (!getNumElemsParam().isValid())
      ++TrailingOmittedArgs;
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getElemSizeParam().getSourceIndex() << "";
    if (!(!getNumElemsParam().isValid())) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "" << getNumElemsParam().getSourceIndex() << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *AllocSizeAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "alloc_size";
  case 1:
    return "alloc_size";
  case 2:
    return "alloc_size";
  }
}

// AlwaysDestroyAttr implementation

AlwaysDestroyAttr *
AlwaysDestroyAttr::CreateImplicit(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AlwaysDestroyAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AlwaysDestroyAttr *
AlwaysDestroyAttr::Create(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AlwaysDestroyAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AlwaysDestroyAttr *AlwaysDestroyAttr::CreateImplicit(TreeContext &Ctx,
                                                     SourceRange Range,
                                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_always_destroy:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_always_destroy, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_always_destroy:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_always_destroy,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_always_destroy:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_always_destroy,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

AlwaysDestroyAttr *AlwaysDestroyAttr::Create(TreeContext &Ctx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_always_destroy:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_always_destroy, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_always_destroy:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_always_destroy,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_always_destroy:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_always_destroy,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

AlwaysDestroyAttr::AlwaysDestroyAttr(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::AlwaysDestroy, false, false) {}

AlwaysDestroyAttr *AlwaysDestroyAttr::clone(TreeContext &C) const {
  auto *A = new (C) AlwaysDestroyAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AlwaysDestroyAttr::printPretty(raw_ostream &OS,
                                    const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((always_destroy";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::always_destroy";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::always_destroy";
    OS << "]]";
    break;
  }
  }
}

const char *AlwaysDestroyAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "always_destroy";
  case 1:
    return "always_destroy";
  case 2:
    return "always_destroy";
  }
}

// AlwaysInlineAttr implementation

AlwaysInlineAttr *
AlwaysInlineAttr::CreateImplicit(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AlwaysInlineAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AlwaysInlineAttr *
AlwaysInlineAttr::Create(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AlwaysInlineAttr(Ctx, CommonInfo);
  return A;
}

AlwaysInlineAttr *AlwaysInlineAttr::CreateImplicit(TreeContext &Ctx,
                                                   SourceRange Range,
                                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_always_inline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_always_inline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_always_inline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_always_inline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_always_inline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_always_inline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_always_inline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_always_inline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_always_inline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_always_inline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Keyword_forceinline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_forceinline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

AlwaysInlineAttr *AlwaysInlineAttr::Create(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_always_inline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_always_inline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_always_inline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_always_inline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_always_inline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_always_inline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_always_inline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_always_inline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_always_inline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_always_inline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Keyword_forceinline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_forceinline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

AlwaysInlineAttr::AlwaysInlineAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo)
    : DeclOrStmtAttr(Ctx, CommonInfo, attr::AlwaysInline, false, false) {}

AlwaysInlineAttr::Spelling AlwaysInlineAttr::getSemanticSpelling() const {
  switch (getAttributeSpellingListIndex()) {
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
AlwaysInlineAttr *AlwaysInlineAttr::clone(TreeContext &C) const {
  auto *A = new (C) AlwaysInlineAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AlwaysInlineAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((always_inline";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::always_inline";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::always_inline";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[neverc::always_inline";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::always_inline";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " __forceinline";
    OS << "";
    break;
  }
  }
}

const char *AlwaysInlineAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "always_inline";
  case 1:
    return "always_inline";
  case 2:
    return "always_inline";
  case 3:
    return "always_inline";
  case 4:
    return "always_inline";
  case 5:
    return "__forceinline";
  }
}

// AnalyzerNoReturnAttr implementation

AnalyzerNoReturnAttr *
AnalyzerNoReturnAttr::CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AnalyzerNoReturnAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AnalyzerNoReturnAttr *
AnalyzerNoReturnAttr::Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AnalyzerNoReturnAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AnalyzerNoReturnAttr *AnalyzerNoReturnAttr::CreateImplicit(TreeContext &Ctx,
                                                           SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_GNU, 0, false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

AnalyzerNoReturnAttr *AnalyzerNoReturnAttr::Create(TreeContext &Ctx,
                                                   SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_GNU, 0, false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

AnalyzerNoReturnAttr::AnalyzerNoReturnAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::AnalyzerNoReturn, false, false) {}

AnalyzerNoReturnAttr *AnalyzerNoReturnAttr::clone(TreeContext &C) const {
  auto *A = new (C) AnalyzerNoReturnAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AnalyzerNoReturnAttr::printPretty(raw_ostream &OS,
                                       const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((analyzer_noreturn";
    OS << "))";
    break;
  }
  }
}

const char *AnalyzerNoReturnAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "analyzer_noreturn";
  }
}

// AnnotateAttr implementation

AnnotateAttr *
AnnotateAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Annotation,
                             Expr **Args, unsigned ArgsSize,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AnnotateAttr(Ctx, CommonInfo, Annotation, Args, ArgsSize);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AnnotateAttr *AnnotateAttr::Create(TreeContext &Ctx, llvm::StringRef Annotation,
                                   Expr **Args, unsigned ArgsSize,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AnnotateAttr(Ctx, CommonInfo, Annotation, Args, ArgsSize);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AnnotateAttr *AnnotateAttr::CreateImplicit(TreeContext &Ctx,
                                           llvm::StringRef Annotation,
                                           Expr **Args, unsigned ArgsSize,
                                           SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_annotate:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_annotate, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_annotate:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_annotate,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_annotate:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_annotate,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_annotate:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_annotate, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_annotate:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_annotate, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Annotation, Args, ArgsSize, I);
}

AnnotateAttr *AnnotateAttr::Create(TreeContext &Ctx, llvm::StringRef Annotation,
                                   Expr **Args, unsigned ArgsSize,
                                   SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_annotate:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_annotate, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_annotate:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_annotate,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_annotate:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_annotate,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_annotate:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_annotate, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_annotate:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_annotate, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Annotation, Args, ArgsSize, I);
}

AnnotateAttr::AnnotateAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo,
                           llvm::StringRef Annotation, Expr **Args,
                           unsigned ArgsSize)
    : InheritableParamAttr(Ctx, CommonInfo, attr::Annotate, false, false),
      annotationLength(Annotation.size()),
      annotation(new (Ctx, 1) char[annotationLength]), args_Size(ArgsSize),
      args_(new (Ctx, 16) Expr *[args_Size]) {
  if (!Annotation.empty())
    std::memcpy(annotation, Annotation.data(), annotationLength);
  std::copy(Args, Args + args_Size, args_);
}

AnnotateAttr::AnnotateAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo,
                           llvm::StringRef Annotation)
    : InheritableParamAttr(Ctx, CommonInfo, attr::Annotate, false, false),
      annotationLength(Annotation.size()),
      annotation(new (Ctx, 1) char[annotationLength]), args_Size(0),
      args_(nullptr) {
  if (!Annotation.empty())
    std::memcpy(annotation, Annotation.data(), annotationLength);
}

AnnotateAttr *AnnotateAttr::clone(TreeContext &C) const {
  auto *A = new (C) AnnotateAttr(C, *this, getAnnotation(), args_, args_Size);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AnnotateAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((annotate";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAnnotation() << "\"";
    OS << "";
    for (const auto &Val : args()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::annotate";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAnnotation() << "\"";
    OS << "";
    for (const auto &Val : args()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::annotate";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAnnotation() << "\"";
    OS << "";
    for (const auto &Val : args()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::annotate";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAnnotation() << "\"";
    OS << "";
    for (const auto &Val : args()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::annotate";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAnnotation() << "\"";
    OS << "";
    for (const auto &Val : args()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *AnnotateAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "annotate";
  case 1:
    return "annotate";
  case 2:
    return "annotate";
  case 3:
    return "annotate";
  case 4:
    return "annotate";
  }
}

// AnnotateTypeAttr implementation

AnnotateTypeAttr *
AnnotateTypeAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Annotation,
                                 Expr **Args, unsigned ArgsSize,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A =
      new (Ctx) AnnotateTypeAttr(Ctx, CommonInfo, Annotation, Args, ArgsSize);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AnnotateTypeAttr *
AnnotateTypeAttr::Create(TreeContext &Ctx, llvm::StringRef Annotation,
                         Expr **Args, unsigned ArgsSize,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A =
      new (Ctx) AnnotateTypeAttr(Ctx, CommonInfo, Annotation, Args, ArgsSize);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AnnotateTypeAttr *
AnnotateTypeAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Annotation,
                                 Expr **Args, unsigned ArgsSize,
                                 SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_neverc_annotate_type:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_annotate_type,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_annotate_type:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_annotate_type,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Annotation, Args, ArgsSize, I);
}

AnnotateTypeAttr *AnnotateTypeAttr::Create(TreeContext &Ctx,
                                           llvm::StringRef Annotation,
                                           Expr **Args, unsigned ArgsSize,
                                           SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_neverc_annotate_type:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_annotate_type,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_annotate_type:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_annotate_type,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Annotation, Args, ArgsSize, I);
}

AnnotateTypeAttr::AnnotateTypeAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo,
                                   llvm::StringRef Annotation, Expr **Args,
                                   unsigned ArgsSize)
    : TypeAttr(Ctx, CommonInfo, attr::AnnotateType, false),
      annotationLength(Annotation.size()),
      annotation(new (Ctx, 1) char[annotationLength]), args_Size(ArgsSize),
      args_(new (Ctx, 16) Expr *[args_Size]) {
  if (!Annotation.empty())
    std::memcpy(annotation, Annotation.data(), annotationLength);
  std::copy(Args, Args + args_Size, args_);
}

AnnotateTypeAttr::AnnotateTypeAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo,
                                   llvm::StringRef Annotation)
    : TypeAttr(Ctx, CommonInfo, attr::AnnotateType, false),
      annotationLength(Annotation.size()),
      annotation(new (Ctx, 1) char[annotationLength]), args_Size(0),
      args_(nullptr) {
  if (!Annotation.empty())
    std::memcpy(annotation, Annotation.data(), annotationLength);
}

AnnotateTypeAttr *AnnotateTypeAttr::clone(TreeContext &C) const {
  auto *A =
      new (C) AnnotateTypeAttr(C, *this, getAnnotation(), args_, args_Size);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AnnotateTypeAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " [[neverc::annotate_type";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAnnotation() << "\"";
    OS << "";
    for (const auto &Val : args()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 1: {
    OS << " [[neverc::annotate_type";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAnnotation() << "\"";
    OS << "";
    for (const auto &Val : args()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *AnnotateTypeAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "annotate_type";
  case 1:
    return "annotate_type";
  }
}

// AnyX86InterruptAttr implementation

AnyX86InterruptAttr *
AnyX86InterruptAttr::CreateImplicit(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AnyX86InterruptAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AnyX86InterruptAttr *
AnyX86InterruptAttr::Create(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AnyX86InterruptAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AnyX86InterruptAttr *AnyX86InterruptAttr::CreateImplicit(TreeContext &Ctx,
                                                         SourceRange Range,
                                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_interrupt:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_interrupt, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_interrupt:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_interrupt,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_interrupt:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_interrupt, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

AnyX86InterruptAttr *
AnyX86InterruptAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_interrupt:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_interrupt, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_interrupt:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_interrupt,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_interrupt:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_interrupt, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

AnyX86InterruptAttr::AnyX86InterruptAttr(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::AnyX86Interrupt, false, false) {}

AnyX86InterruptAttr *AnyX86InterruptAttr::clone(TreeContext &C) const {
  auto *A = new (C) AnyX86InterruptAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AnyX86InterruptAttr::printPretty(raw_ostream &OS,
                                      const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((interrupt";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::interrupt";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::interrupt";
    OS << "]]";
    break;
  }
  }
}

const char *AnyX86InterruptAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "interrupt";
  case 1:
    return "interrupt";
  case 2:
    return "interrupt";
  }
}

// AnyX86NoCallerSavedRegistersAttr implementation

AnyX86NoCallerSavedRegistersAttr *
AnyX86NoCallerSavedRegistersAttr::CreateImplicit(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AnyX86NoCallerSavedRegistersAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AnyX86NoCallerSavedRegistersAttr *AnyX86NoCallerSavedRegistersAttr::Create(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AnyX86NoCallerSavedRegistersAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AnyX86NoCallerSavedRegistersAttr *
AnyX86NoCallerSavedRegistersAttr::CreateImplicit(TreeContext &Ctx,
                                                 SourceRange Range,
                                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_caller_saved_registers:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_no_caller_saved_registers,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_no_caller_saved_registers:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_no_caller_saved_registers,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_no_caller_saved_registers:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_no_caller_saved_registers,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

AnyX86NoCallerSavedRegistersAttr *
AnyX86NoCallerSavedRegistersAttr::Create(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_caller_saved_registers:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_no_caller_saved_registers,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_no_caller_saved_registers:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_no_caller_saved_registers,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_no_caller_saved_registers:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_no_caller_saved_registers,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

AnyX86NoCallerSavedRegistersAttr::AnyX86NoCallerSavedRegistersAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::AnyX86NoCallerSavedRegisters,
                      false, false) {}

AnyX86NoCallerSavedRegistersAttr *
AnyX86NoCallerSavedRegistersAttr::clone(TreeContext &C) const {
  auto *A = new (C) AnyX86NoCallerSavedRegistersAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AnyX86NoCallerSavedRegistersAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((no_caller_saved_registers";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::no_caller_saved_registers";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::no_caller_saved_registers";
    OS << "]]";
    break;
  }
  }
}

const char *AnyX86NoCallerSavedRegistersAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "no_caller_saved_registers";
  case 1:
    return "no_caller_saved_registers";
  case 2:
    return "no_caller_saved_registers";
  }
}

// AnyX86NoCfCheckAttr implementation

AnyX86NoCfCheckAttr *
AnyX86NoCfCheckAttr::CreateImplicit(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AnyX86NoCfCheckAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AnyX86NoCfCheckAttr *
AnyX86NoCfCheckAttr::Create(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AnyX86NoCfCheckAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AnyX86NoCfCheckAttr *AnyX86NoCfCheckAttr::CreateImplicit(TreeContext &Ctx,
                                                         SourceRange Range,
                                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nocf_check:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_nocf_check, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_nocf_check:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_nocf_check,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_nocf_check:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_nocf_check, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

AnyX86NoCfCheckAttr *
AnyX86NoCfCheckAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nocf_check:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_nocf_check, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_nocf_check:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_nocf_check,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_nocf_check:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_nocf_check, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

AnyX86NoCfCheckAttr::AnyX86NoCfCheckAttr(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::AnyX86NoCfCheck, false, false) {}

AnyX86NoCfCheckAttr *AnyX86NoCfCheckAttr::clone(TreeContext &C) const {
  auto *A = new (C) AnyX86NoCfCheckAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AnyX86NoCfCheckAttr::printPretty(raw_ostream &OS,
                                      const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((nocf_check";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::nocf_check";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::nocf_check";
    OS << "]]";
    break;
  }
  }
}

const char *AnyX86NoCfCheckAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "nocf_check";
  case 1:
    return "nocf_check";
  case 2:
    return "nocf_check";
  }
}

// ArgumentWithTypeTagAttr implementation

ArgumentWithTypeTagAttr *ArgumentWithTypeTagAttr::CreateImplicit(
    TreeContext &Ctx, IdentifierInfo *ArgumentKind, ParamIdx ArgumentIdx,
    ParamIdx TypeTagIdx, bool IsPointer,
    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArgumentWithTypeTagAttr(
      Ctx, CommonInfo, ArgumentKind, ArgumentIdx, TypeTagIdx, IsPointer);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArgumentWithTypeTagAttr *
ArgumentWithTypeTagAttr::Create(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
                                ParamIdx ArgumentIdx, ParamIdx TypeTagIdx,
                                bool IsPointer,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArgumentWithTypeTagAttr(
      Ctx, CommonInfo, ArgumentKind, ArgumentIdx, TypeTagIdx, IsPointer);
  return A;
}

ArgumentWithTypeTagAttr *ArgumentWithTypeTagAttr::CreateImplicit(
    TreeContext &Ctx, IdentifierInfo *ArgumentKind, ParamIdx ArgumentIdx,
    ParamIdx TypeTagIdx, bool IsPointer, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_argument_with_type_tag:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_neverc_argument_with_type_tag,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_clang_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, ArgumentKind, ArgumentIdx, TypeTagIdx, IsPointer,
                        I);
}

ArgumentWithTypeTagAttr *
ArgumentWithTypeTagAttr::Create(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
                                ParamIdx ArgumentIdx, ParamIdx TypeTagIdx,
                                bool IsPointer, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_argument_with_type_tag:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_neverc_argument_with_type_tag,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_clang_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, ArgumentKind, ArgumentIdx, TypeTagIdx, IsPointer, I);
}

ArgumentWithTypeTagAttr *ArgumentWithTypeTagAttr::CreateImplicit(
    TreeContext &Ctx, IdentifierInfo *ArgumentKind, ParamIdx ArgumentIdx,
    ParamIdx TypeTagIdx, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArgumentWithTypeTagAttr(Ctx, CommonInfo, ArgumentKind,
                                              ArgumentIdx, TypeTagIdx);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArgumentWithTypeTagAttr *
ArgumentWithTypeTagAttr::Create(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
                                ParamIdx ArgumentIdx, ParamIdx TypeTagIdx,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArgumentWithTypeTagAttr(Ctx, CommonInfo, ArgumentKind,
                                              ArgumentIdx, TypeTagIdx);
  return A;
}

ArgumentWithTypeTagAttr *ArgumentWithTypeTagAttr::CreateImplicit(
    TreeContext &Ctx, IdentifierInfo *ArgumentKind, ParamIdx ArgumentIdx,
    ParamIdx TypeTagIdx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_argument_with_type_tag:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_neverc_argument_with_type_tag,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_clang_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, ArgumentKind, ArgumentIdx, TypeTagIdx, I);
}

ArgumentWithTypeTagAttr *
ArgumentWithTypeTagAttr::Create(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
                                ParamIdx ArgumentIdx, ParamIdx TypeTagIdx,
                                SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_argument_with_type_tag:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_neverc_argument_with_type_tag,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_clang_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_argument_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_argument_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_pointer_with_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_pointer_with_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, ArgumentKind, ArgumentIdx, TypeTagIdx, I);
}

ArgumentWithTypeTagAttr::ArgumentWithTypeTagAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    IdentifierInfo *ArgumentKind, ParamIdx ArgumentIdx, ParamIdx TypeTagIdx,
    bool IsPointer)
    : InheritableAttr(Ctx, CommonInfo, attr::ArgumentWithTypeTag, false, false),
      argumentKind(ArgumentKind), argumentIdx(ArgumentIdx),
      typeTagIdx(TypeTagIdx), isPointer(IsPointer) {}

ArgumentWithTypeTagAttr::ArgumentWithTypeTagAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    IdentifierInfo *ArgumentKind, ParamIdx ArgumentIdx, ParamIdx TypeTagIdx)
    : InheritableAttr(Ctx, CommonInfo, attr::ArgumentWithTypeTag, false, false),
      argumentKind(ArgumentKind), argumentIdx(ArgumentIdx),
      typeTagIdx(TypeTagIdx), isPointer() {}

ArgumentWithTypeTagAttr::Spelling
ArgumentWithTypeTagAttr::getSemanticSpelling() const {
  switch (getAttributeSpellingListIndex()) {
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

ArgumentWithTypeTagAttr *ArgumentWithTypeTagAttr::clone(TreeContext &C) const {
  auto *A = new (C) ArgumentWithTypeTagAttr(C, *this, argumentKind, argumentIdx,
                                            typeTagIdx, isPointer);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ArgumentWithTypeTagAttr::printPretty(raw_ostream &OS,
                                          const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((argument_with_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getArgumentIdx().getSourceIndex() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getTypeTagIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::argument_with_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getArgumentIdx().getSourceIndex() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getTypeTagIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::argument_with_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getArgumentIdx().getSourceIndex() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getTypeTagIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::argument_with_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getArgumentIdx().getSourceIndex() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getTypeTagIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::argument_with_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getArgumentIdx().getSourceIndex() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getTypeTagIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " __attribute__((pointer_with_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getArgumentIdx().getSourceIndex() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getTypeTagIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 6: {
    OS << " [[clang::pointer_with_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getArgumentIdx().getSourceIndex() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getTypeTagIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 7: {
    OS << " [[neverc::pointer_with_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getArgumentIdx().getSourceIndex() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getTypeTagIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 8: {
    OS << " [[clang::pointer_with_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getArgumentIdx().getSourceIndex() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getTypeTagIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 9: {
    OS << " [[neverc::pointer_with_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getArgumentIdx().getSourceIndex() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getTypeTagIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *ArgumentWithTypeTagAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "argument_with_type_tag";
  case 1:
    return "argument_with_type_tag";
  case 2:
    return "argument_with_type_tag";
  case 3:
    return "argument_with_type_tag";
  case 4:
    return "argument_with_type_tag";
  case 5:
    return "pointer_with_type_tag";
  case 6:
    return "pointer_with_type_tag";
  case 7:
    return "pointer_with_type_tag";
  case 8:
    return "pointer_with_type_tag";
  case 9:
    return "pointer_with_type_tag";
  }
}

// ArmBuiltinAliasAttr implementation

ArmBuiltinAliasAttr *
ArmBuiltinAliasAttr::CreateImplicit(TreeContext &Ctx,
                                    IdentifierInfo *BuiltinName,
                                    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmBuiltinAliasAttr(Ctx, CommonInfo, BuiltinName);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmBuiltinAliasAttr *
ArmBuiltinAliasAttr::Create(TreeContext &Ctx, IdentifierInfo *BuiltinName,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmBuiltinAliasAttr(Ctx, CommonInfo, BuiltinName);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmBuiltinAliasAttr *
ArmBuiltinAliasAttr::CreateImplicit(TreeContext &Ctx,
                                    IdentifierInfo *BuiltinName,
                                    SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_neverc_arm_builtin_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_neverc_arm_builtin_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_neverc_arm_builtin_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_clang_neverc_arm_builtin_alias,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_neverc_arm_builtin_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_neverc_neverc_arm_builtin_alias,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_clang_neverc_arm_builtin_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_neverc_arm_builtin_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_neverc_arm_builtin_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_neverc_arm_builtin_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, BuiltinName, I);
}

ArmBuiltinAliasAttr *ArmBuiltinAliasAttr::Create(TreeContext &Ctx,
                                                 IdentifierInfo *BuiltinName,
                                                 SourceRange Range,
                                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_neverc_arm_builtin_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_neverc_arm_builtin_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_neverc_arm_builtin_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_clang_neverc_arm_builtin_alias,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_neverc_arm_builtin_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_neverc_neverc_arm_builtin_alias,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_clang_neverc_arm_builtin_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_neverc_arm_builtin_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_neverc_arm_builtin_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_neverc_arm_builtin_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, BuiltinName, I);
}

ArmBuiltinAliasAttr::ArmBuiltinAliasAttr(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo,
                                         IdentifierInfo *BuiltinName)
    : InheritableAttr(Ctx, CommonInfo, attr::ArmBuiltinAlias, false, false),
      builtinName(BuiltinName) {}

ArmBuiltinAliasAttr *ArmBuiltinAliasAttr::clone(TreeContext &C) const {
  auto *A = new (C) ArmBuiltinAliasAttr(C, *this, builtinName);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ArmBuiltinAliasAttr::printPretty(raw_ostream &OS,
                                      const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((__neverc_arm_builtin_alias";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getBuiltinName() ? getBuiltinName()->getName() : "") << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::__neverc_arm_builtin_alias";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getBuiltinName() ? getBuiltinName()->getName() : "") << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::__neverc_arm_builtin_alias";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getBuiltinName() ? getBuiltinName()->getName() : "") << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::__neverc_arm_builtin_alias";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getBuiltinName() ? getBuiltinName()->getName() : "") << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::__neverc_arm_builtin_alias";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getBuiltinName() ? getBuiltinName()->getName() : "") << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *ArmBuiltinAliasAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "__neverc_arm_builtin_alias";
  case 1:
    return "__neverc_arm_builtin_alias";
  case 2:
    return "__neverc_arm_builtin_alias";
  case 3:
    return "__neverc_arm_builtin_alias";
  case 4:
    return "__neverc_arm_builtin_alias";
  }
}

// ArmLocallyStreamingAttr implementation

ArmLocallyStreamingAttr *
ArmLocallyStreamingAttr::CreateImplicit(TreeContext &Ctx,
                                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmLocallyStreamingAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmLocallyStreamingAttr *
ArmLocallyStreamingAttr::Create(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmLocallyStreamingAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmLocallyStreamingAttr *
ArmLocallyStreamingAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         true /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

ArmLocallyStreamingAttr *ArmLocallyStreamingAttr::Create(TreeContext &Ctx,
                                                         SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         true /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

ArmLocallyStreamingAttr::ArmLocallyStreamingAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::ArmLocallyStreaming, false,
                      false) {}

ArmLocallyStreamingAttr *ArmLocallyStreamingAttr::clone(TreeContext &C) const {
  auto *A = new (C) ArmLocallyStreamingAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ArmLocallyStreamingAttr::printPretty(raw_ostream &OS,
                                          const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __arm_locally_streaming";
    OS << "";
    break;
  }
  }
}

const char *ArmLocallyStreamingAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "__arm_locally_streaming";
  }
}

// ArmNewZAAttr implementation

ArmNewZAAttr *
ArmNewZAAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmNewZAAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmNewZAAttr *ArmNewZAAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmNewZAAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmNewZAAttr *ArmNewZAAttr::CreateImplicit(TreeContext &Ctx,
                                           SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         true /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

ArmNewZAAttr *ArmNewZAAttr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         true /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

ArmNewZAAttr::ArmNewZAAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::ArmNewZA, false, false) {}

ArmNewZAAttr *ArmNewZAAttr::clone(TreeContext &C) const {
  auto *A = new (C) ArmNewZAAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ArmNewZAAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __arm_new_za";
    OS << "";
    break;
  }
  }
}

const char *ArmNewZAAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "__arm_new_za";
  }
}

// ArmPreservesZAAttr implementation

ArmPreservesZAAttr *
ArmPreservesZAAttr::CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmPreservesZAAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmPreservesZAAttr *
ArmPreservesZAAttr::Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmPreservesZAAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmPreservesZAAttr *ArmPreservesZAAttr::CreateImplicit(TreeContext &Ctx,
                                                       SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         true /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

ArmPreservesZAAttr *ArmPreservesZAAttr::Create(TreeContext &Ctx,
                                               SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         true /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

ArmPreservesZAAttr::ArmPreservesZAAttr(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo)
    : TypeAttr(Ctx, CommonInfo, attr::ArmPreservesZA, false) {}

ArmPreservesZAAttr *ArmPreservesZAAttr::clone(TreeContext &C) const {
  auto *A = new (C) ArmPreservesZAAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ArmPreservesZAAttr::printPretty(raw_ostream &OS,
                                     const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __arm_preserves_za";
    OS << "";
    break;
  }
  }
}

const char *ArmPreservesZAAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "__arm_preserves_za";
  }
}

// ArmSharedZAAttr implementation

ArmSharedZAAttr *
ArmSharedZAAttr::CreateImplicit(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmSharedZAAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmSharedZAAttr *
ArmSharedZAAttr::Create(TreeContext &Ctx,
                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmSharedZAAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmSharedZAAttr *ArmSharedZAAttr::CreateImplicit(TreeContext &Ctx,
                                                 SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         true /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

ArmSharedZAAttr *ArmSharedZAAttr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         true /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

ArmSharedZAAttr::ArmSharedZAAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo)
    : TypeAttr(Ctx, CommonInfo, attr::ArmSharedZA, false) {}

ArmSharedZAAttr *ArmSharedZAAttr::clone(TreeContext &C) const {
  auto *A = new (C) ArmSharedZAAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ArmSharedZAAttr::printPretty(raw_ostream &OS,
                                  const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __arm_shared_za";
    OS << "";
    break;
  }
  }
}

const char *ArmSharedZAAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "__arm_shared_za";
  }
}

// ArmStreamingAttr implementation

ArmStreamingAttr *
ArmStreamingAttr::CreateImplicit(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmStreamingAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmStreamingAttr *
ArmStreamingAttr::Create(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmStreamingAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmStreamingAttr *ArmStreamingAttr::CreateImplicit(TreeContext &Ctx,
                                                   SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         true /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

ArmStreamingAttr *ArmStreamingAttr::Create(TreeContext &Ctx,
                                           SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         true /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

ArmStreamingAttr::ArmStreamingAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo)
    : TypeAttr(Ctx, CommonInfo, attr::ArmStreaming, false) {}

ArmStreamingAttr *ArmStreamingAttr::clone(TreeContext &C) const {
  auto *A = new (C) ArmStreamingAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ArmStreamingAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __arm_streaming";
    OS << "";
    break;
  }
  }
}

const char *ArmStreamingAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "__arm_streaming";
  }
}

// ArmStreamingCompatibleAttr implementation

ArmStreamingCompatibleAttr *ArmStreamingCompatibleAttr::CreateImplicit(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmStreamingCompatibleAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmStreamingCompatibleAttr *
ArmStreamingCompatibleAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArmStreamingCompatibleAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArmStreamingCompatibleAttr *
ArmStreamingCompatibleAttr::CreateImplicit(TreeContext &Ctx,
                                           SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         true /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

ArmStreamingCompatibleAttr *
ArmStreamingCompatibleAttr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         true /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

ArmStreamingCompatibleAttr::ArmStreamingCompatibleAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : TypeAttr(Ctx, CommonInfo, attr::ArmStreamingCompatible, false) {}

ArmStreamingCompatibleAttr *
ArmStreamingCompatibleAttr::clone(TreeContext &C) const {
  auto *A = new (C) ArmStreamingCompatibleAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ArmStreamingCompatibleAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __arm_streaming_compatible";
    OS << "";
    break;
  }
  }
}

const char *ArmStreamingCompatibleAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "__arm_streaming_compatible";
  }
}

// ArtificialAttr implementation

ArtificialAttr *
ArtificialAttr::CreateImplicit(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArtificialAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArtificialAttr *ArtificialAttr::Create(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ArtificialAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ArtificialAttr *ArtificialAttr::CreateImplicit(TreeContext &Ctx,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_artificial:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_artificial, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_artificial:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_artificial,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_artificial:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_artificial, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

ArtificialAttr *ArtificialAttr::Create(TreeContext &Ctx, SourceRange Range,
                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_artificial:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_artificial, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_artificial:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_artificial,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_artificial:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_artificial, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

ArtificialAttr::ArtificialAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Artificial, false, false) {}

ArtificialAttr *ArtificialAttr::clone(TreeContext &C) const {
  auto *A = new (C) ArtificialAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ArtificialAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((artificial";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::artificial";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::artificial";
    OS << "]]";
    break;
  }
  }
}

const char *ArtificialAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "artificial";
  case 1:
    return "artificial";
  case 2:
    return "artificial";
  }
}

// AsmLabelAttr implementation

AsmLabelAttr *
AsmLabelAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Label,
                             bool IsLiteralLabel,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AsmLabelAttr(Ctx, CommonInfo, Label, IsLiteralLabel);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AsmLabelAttr *AsmLabelAttr::Create(TreeContext &Ctx, llvm::StringRef Label,
                                   bool IsLiteralLabel,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AsmLabelAttr(Ctx, CommonInfo, Label, IsLiteralLabel);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AsmLabelAttr *AsmLabelAttr::CreateImplicit(TreeContext &Ctx,
                                           llvm::StringRef Label,
                                           bool IsLiteralLabel,
                                           SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Keyword_asm:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_asm, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Label, IsLiteralLabel, I);
}

AsmLabelAttr *AsmLabelAttr::Create(TreeContext &Ctx, llvm::StringRef Label,
                                   bool IsLiteralLabel, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Keyword_asm:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_asm, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Label, IsLiteralLabel, I);
}

AsmLabelAttr *
AsmLabelAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Label,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AsmLabelAttr(Ctx, CommonInfo, Label);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AsmLabelAttr *AsmLabelAttr::Create(TreeContext &Ctx, llvm::StringRef Label,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AsmLabelAttr(Ctx, CommonInfo, Label);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AsmLabelAttr *AsmLabelAttr::CreateImplicit(TreeContext &Ctx,
                                           llvm::StringRef Label,
                                           SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Keyword_asm:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_asm, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Label, I);
}

AsmLabelAttr *AsmLabelAttr::Create(TreeContext &Ctx, llvm::StringRef Label,
                                   SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Keyword_asm:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_asm, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Label, I);
}

AsmLabelAttr::AsmLabelAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo,
                           llvm::StringRef Label, bool IsLiteralLabel)
    : InheritableAttr(Ctx, CommonInfo, attr::AsmLabel, false, false),
      labelLength(Label.size()), label(new (Ctx, 1) char[labelLength]),
      isLiteralLabel(IsLiteralLabel) {
  if (!Label.empty())
    std::memcpy(label, Label.data(), labelLength);
}

AsmLabelAttr::AsmLabelAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo,
                           llvm::StringRef Label)
    : InheritableAttr(Ctx, CommonInfo, attr::AsmLabel, false, false),
      labelLength(Label.size()), label(new (Ctx, 1) char[labelLength]),
      isLiteralLabel() {
  if (!Label.empty())
    std::memcpy(label, Label.data(), labelLength);
}

AsmLabelAttr *AsmLabelAttr::clone(TreeContext &C) const {
  auto *A = new (C) AsmLabelAttr(C, *this, getLabel(), isLiteralLabel);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AsmLabelAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " asm";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getLabel() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "";
    break;
  }
  case 1: {
    OS << " __asm__";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getLabel() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "";
    break;
  }
  }
}

const char *AsmLabelAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "asm";
  case 1:
    return "__asm__";
  }
}

// AssumeAlignedAttr implementation

AssumeAlignedAttr *
AssumeAlignedAttr::CreateImplicit(TreeContext &Ctx, Expr *Alignment,
                                  Expr *Offset,
                                  const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AssumeAlignedAttr(Ctx, CommonInfo, Alignment, Offset);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AssumeAlignedAttr *
AssumeAlignedAttr::Create(TreeContext &Ctx, Expr *Alignment, Expr *Offset,
                          const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AssumeAlignedAttr(Ctx, CommonInfo, Alignment, Offset);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AssumeAlignedAttr *
AssumeAlignedAttr::CreateImplicit(TreeContext &Ctx, Expr *Alignment,
                                  Expr *Offset, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_assume_aligned:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_assume_aligned, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_assume_aligned:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_assume_aligned,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_assume_aligned:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_assume_aligned,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Alignment, Offset, I);
}

AssumeAlignedAttr *AssumeAlignedAttr::Create(TreeContext &Ctx, Expr *Alignment,
                                             Expr *Offset, SourceRange Range,
                                             Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_assume_aligned:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_assume_aligned, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_assume_aligned:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_assume_aligned,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_assume_aligned:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_assume_aligned,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Alignment, Offset, I);
}

AssumeAlignedAttr::AssumeAlignedAttr(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo,
                                     Expr *Alignment, Expr *Offset)
    : InheritableAttr(Ctx, CommonInfo, attr::AssumeAligned, false, false),
      alignment(Alignment), offset(Offset) {}

AssumeAlignedAttr::AssumeAlignedAttr(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo,
                                     Expr *Alignment)
    : InheritableAttr(Ctx, CommonInfo, attr::AssumeAligned, false, false),
      alignment(Alignment), offset() {}

AssumeAlignedAttr *AssumeAlignedAttr::clone(TreeContext &C) const {
  auto *A = new (C) AssumeAlignedAttr(C, *this, alignment, offset);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AssumeAlignedAttr::printPretty(raw_ostream &OS,
                                    const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((assume_aligned";
    if (!getOffset())
      ++TrailingOmittedArgs;
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "";
    getAlignment()->printPretty(OS, nullptr, Policy);
    OS << "";
    if (!(!getOffset())) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "";
      getOffset()->printPretty(OS, nullptr, Policy);
      OS << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::assume_aligned";
    if (!getOffset())
      ++TrailingOmittedArgs;
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "";
    getAlignment()->printPretty(OS, nullptr, Policy);
    OS << "";
    if (!(!getOffset())) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "";
      getOffset()->printPretty(OS, nullptr, Policy);
      OS << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::assume_aligned";
    if (!getOffset())
      ++TrailingOmittedArgs;
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "";
    getAlignment()->printPretty(OS, nullptr, Policy);
    OS << "";
    if (!(!getOffset())) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "";
      getOffset()->printPretty(OS, nullptr, Policy);
      OS << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *AssumeAlignedAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "assume_aligned";
  case 1:
    return "assume_aligned";
  case 2:
    return "assume_aligned";
  }
}

// AssumptionAttr implementation

AssumptionAttr *
AssumptionAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Assumption,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AssumptionAttr(Ctx, CommonInfo, Assumption);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AssumptionAttr *AssumptionAttr::Create(TreeContext &Ctx,
                                       llvm::StringRef Assumption,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AssumptionAttr(Ctx, CommonInfo, Assumption);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AssumptionAttr *AssumptionAttr::CreateImplicit(TreeContext &Ctx,
                                               llvm::StringRef Assumption,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_assume:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_assume,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_assume:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_assume,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_assume:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_assume,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_assume:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_assume, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_assume:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_assume, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Assumption, I);
}

AssumptionAttr *AssumptionAttr::Create(TreeContext &Ctx,
                                       llvm::StringRef Assumption,
                                       SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_assume:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_assume,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_assume:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_assume,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_assume:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_assume,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_assume:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_assume, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_assume:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_assume, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Assumption, I);
}

AssumptionAttr::AssumptionAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo,
                               llvm::StringRef Assumption)
    : InheritableAttr(Ctx, CommonInfo, attr::Assumption, false, true),
      assumptionLength(Assumption.size()),
      assumption(new (Ctx, 1) char[assumptionLength]) {
  if (!Assumption.empty())
    std::memcpy(assumption, Assumption.data(), assumptionLength);
}

AssumptionAttr *AssumptionAttr::clone(TreeContext &C) const {
  auto *A = new (C) AssumptionAttr(C, *this, getAssumption());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AssumptionAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((assume";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAssumption() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::assume";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAssumption() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::assume";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAssumption() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::assume";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAssumption() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::assume";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAssumption() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *AssumptionAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "assume";
  case 1:
    return "assume";
  case 2:
    return "assume";
  case 3:
    return "assume";
  case 4:
    return "assume";
  }
}

// AvailabilityAttr implementation

AvailabilityAttr *AvailabilityAttr::CreateImplicit(
    TreeContext &Ctx, IdentifierInfo *Platform, VersionTuple Introduced,
    VersionTuple Deprecated, VersionTuple Obsoleted, bool Unavailable,
    llvm::StringRef Message, bool Strict, llvm::StringRef Replacement,
    int Priority, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AvailabilityAttr(Ctx, CommonInfo, Platform, Introduced,
                                       Deprecated, Obsoleted, Unavailable,
                                       Message, Strict, Replacement, Priority);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AvailabilityAttr *AvailabilityAttr::Create(
    TreeContext &Ctx, IdentifierInfo *Platform, VersionTuple Introduced,
    VersionTuple Deprecated, VersionTuple Obsoleted, bool Unavailable,
    llvm::StringRef Message, bool Strict, llvm::StringRef Replacement,
    int Priority, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AvailabilityAttr(Ctx, CommonInfo, Platform, Introduced,
                                       Deprecated, Obsoleted, Unavailable,
                                       Message, Strict, Replacement, Priority);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AvailabilityAttr *AvailabilityAttr::CreateImplicit(
    TreeContext &Ctx, IdentifierInfo *Platform, VersionTuple Introduced,
    VersionTuple Deprecated, VersionTuple Obsoleted, bool Unavailable,
    llvm::StringRef Message, bool Strict, llvm::StringRef Replacement,
    int Priority, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_availability:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_availability, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_availability:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_availability,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_availability:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_availability,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_availability:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_availability,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_availability:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_availability,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Platform, Introduced, Deprecated, Obsoleted,
                        Unavailable, Message, Strict, Replacement, Priority, I);
}

AvailabilityAttr *AvailabilityAttr::Create(
    TreeContext &Ctx, IdentifierInfo *Platform, VersionTuple Introduced,
    VersionTuple Deprecated, VersionTuple Obsoleted, bool Unavailable,
    llvm::StringRef Message, bool Strict, llvm::StringRef Replacement,
    int Priority, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_availability:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_availability, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_availability:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_availability,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_availability:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_availability,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_availability:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_availability,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_availability:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_availability,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Platform, Introduced, Deprecated, Obsoleted, Unavailable,
                Message, Strict, Replacement, Priority, I);
}

AvailabilityAttr::AvailabilityAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    IdentifierInfo *Platform, VersionTuple Introduced, VersionTuple Deprecated,
    VersionTuple Obsoleted, bool Unavailable, llvm::StringRef Message,
    bool Strict, llvm::StringRef Replacement, int Priority)
    : InheritableAttr(Ctx, CommonInfo, attr::Availability, false, true),
      platform(Platform), introduced(Introduced), deprecated(Deprecated),
      obsoleted(Obsoleted), unavailable(Unavailable),
      messageLength(Message.size()), message(new (Ctx, 1) char[messageLength]),
      strict(Strict), replacementLength(Replacement.size()),
      replacement(new (Ctx, 1) char[replacementLength]), priority(Priority) {
  if (!Message.empty())
    std::memcpy(message, Message.data(), messageLength);
  if (!Replacement.empty())
    std::memcpy(replacement, Replacement.data(), replacementLength);
}

AvailabilityAttr *AvailabilityAttr::clone(TreeContext &C) const {
  auto *A = new (C) AvailabilityAttr(
      C, *this, platform, getIntroduced(), getDeprecated(), getObsoleted(),
      unavailable, getMessage(), strict, getReplacement(), priority);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AvailabilityAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((availability";
    OS << "(" << getPlatform()->getName();
    if (getStrict())
      OS << ", strict";
    if (!getIntroduced().empty())
      OS << ", introduced=" << getIntroduced();
    if (!getDeprecated().empty())
      OS << ", deprecated=" << getDeprecated();
    if (!getObsoleted().empty())
      OS << ", obsoleted=" << getObsoleted();
    if (getUnavailable())
      OS << ", unavailable";
    OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::availability";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getPlatform() ? getPlatform()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "introduced=" << getIntroduced() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "deprecated=" << getDeprecated() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "obsoleted=" << getObsoleted() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getUnavailable() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getStrict() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getReplacement() << "\"";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getPriority() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::availability";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getPlatform() ? getPlatform()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "introduced=" << getIntroduced() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "deprecated=" << getDeprecated() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "obsoleted=" << getObsoleted() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getUnavailable() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getStrict() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getReplacement() << "\"";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getPriority() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::availability";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getPlatform() ? getPlatform()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "introduced=" << getIntroduced() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "deprecated=" << getDeprecated() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "obsoleted=" << getObsoleted() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getUnavailable() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getStrict() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getReplacement() << "\"";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getPriority() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::availability";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getPlatform() ? getPlatform()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "introduced=" << getIntroduced() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "deprecated=" << getDeprecated() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "obsoleted=" << getObsoleted() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getUnavailable() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getStrict() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getReplacement() << "\"";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getPriority() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *AvailabilityAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "availability";
  case 1:
    return "availability";
  case 2:
    return "availability";
  case 3:
    return "availability";
  case 4:
    return "availability";
  }
}

// AvailableOnlyInDefaultEvalMethodAttr implementation

AvailableOnlyInDefaultEvalMethodAttr *
AvailableOnlyInDefaultEvalMethodAttr::CreateImplicit(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AvailableOnlyInDefaultEvalMethodAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AvailableOnlyInDefaultEvalMethodAttr *
AvailableOnlyInDefaultEvalMethodAttr::Create(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) AvailableOnlyInDefaultEvalMethodAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

AvailableOnlyInDefaultEvalMethodAttr *
AvailableOnlyInDefaultEvalMethodAttr::CreateImplicit(TreeContext &Ctx,
                                                     SourceRange Range,
                                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_available_only_in_default_eval_method:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU,
          GNU_available_only_in_default_eval_method, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_available_only_in_default_eval_method:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_clang_available_only_in_default_eval_method,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_available_only_in_default_eval_method:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_neverc_available_only_in_default_eval_method,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_available_only_in_default_eval_method:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23,
          C23_clang_available_only_in_default_eval_method, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case C23_neverc_available_only_in_default_eval_method:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23,
          C23_neverc_available_only_in_default_eval_method, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

AvailableOnlyInDefaultEvalMethodAttr *
AvailableOnlyInDefaultEvalMethodAttr::Create(TreeContext &Ctx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_available_only_in_default_eval_method:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU,
          GNU_available_only_in_default_eval_method, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_available_only_in_default_eval_method:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_clang_available_only_in_default_eval_method,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_available_only_in_default_eval_method:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_neverc_available_only_in_default_eval_method,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_available_only_in_default_eval_method:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23,
          C23_clang_available_only_in_default_eval_method, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case C23_neverc_available_only_in_default_eval_method:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23,
          C23_neverc_available_only_in_default_eval_method, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

AvailableOnlyInDefaultEvalMethodAttr::AvailableOnlyInDefaultEvalMethodAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::AvailableOnlyInDefaultEvalMethod,
                      false, false) {}

AvailableOnlyInDefaultEvalMethodAttr *
AvailableOnlyInDefaultEvalMethodAttr::clone(TreeContext &C) const {
  auto *A = new (C) AvailableOnlyInDefaultEvalMethodAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void AvailableOnlyInDefaultEvalMethodAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((available_only_in_default_eval_method";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::available_only_in_default_eval_method";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::available_only_in_default_eval_method";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::available_only_in_default_eval_method";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::available_only_in_default_eval_method";
    OS << "]]";
    break;
  }
  }
}

const char *AvailableOnlyInDefaultEvalMethodAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "available_only_in_default_eval_method";
  case 1:
    return "available_only_in_default_eval_method";
  case 2:
    return "available_only_in_default_eval_method";
  case 3:
    return "available_only_in_default_eval_method";
  case 4:
    return "available_only_in_default_eval_method";
  }
}

// BTFDeclTagAttr implementation

BTFDeclTagAttr *
BTFDeclTagAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef BTFDeclTag,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) BTFDeclTagAttr(Ctx, CommonInfo, BTFDeclTag);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

BTFDeclTagAttr *BTFDeclTagAttr::Create(TreeContext &Ctx,
                                       llvm::StringRef BTFDeclTag,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) BTFDeclTagAttr(Ctx, CommonInfo, BTFDeclTag);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

BTFDeclTagAttr *BTFDeclTagAttr::CreateImplicit(TreeContext &Ctx,
                                               llvm::StringRef BTFDeclTag,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_btf_decl_tag:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_btf_decl_tag, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_btf_decl_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_btf_decl_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_btf_decl_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_btf_decl_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_btf_decl_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_btf_decl_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_btf_decl_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_btf_decl_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, BTFDeclTag, I);
}

BTFDeclTagAttr *BTFDeclTagAttr::Create(TreeContext &Ctx,
                                       llvm::StringRef BTFDeclTag,
                                       SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_btf_decl_tag:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_btf_decl_tag, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_btf_decl_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_btf_decl_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_btf_decl_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_btf_decl_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_btf_decl_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_btf_decl_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_btf_decl_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_btf_decl_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, BTFDeclTag, I);
}

BTFDeclTagAttr::BTFDeclTagAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo,
                               llvm::StringRef BTFDeclTag)
    : InheritableAttr(Ctx, CommonInfo, attr::BTFDeclTag, false, false),
      bTFDeclTagLength(BTFDeclTag.size()),
      bTFDeclTag(new (Ctx, 1) char[bTFDeclTagLength]) {
  if (!BTFDeclTag.empty())
    std::memcpy(bTFDeclTag, BTFDeclTag.data(), bTFDeclTagLength);
}

BTFDeclTagAttr *BTFDeclTagAttr::clone(TreeContext &C) const {
  auto *A = new (C) BTFDeclTagAttr(C, *this, getBTFDeclTag());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void BTFDeclTagAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((btf_decl_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getBTFDeclTag() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::btf_decl_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getBTFDeclTag() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::btf_decl_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getBTFDeclTag() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::btf_decl_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getBTFDeclTag() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::btf_decl_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getBTFDeclTag() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *BTFDeclTagAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "btf_decl_tag";
  case 1:
    return "btf_decl_tag";
  case 2:
    return "btf_decl_tag";
  case 3:
    return "btf_decl_tag";
  case 4:
    return "btf_decl_tag";
  }
}

// BTFTypeTagAttr implementation

BTFTypeTagAttr *
BTFTypeTagAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef BTFTypeTag,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) BTFTypeTagAttr(Ctx, CommonInfo, BTFTypeTag);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

BTFTypeTagAttr *BTFTypeTagAttr::Create(TreeContext &Ctx,
                                       llvm::StringRef BTFTypeTag,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) BTFTypeTagAttr(Ctx, CommonInfo, BTFTypeTag);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

BTFTypeTagAttr *BTFTypeTagAttr::CreateImplicit(TreeContext &Ctx,
                                               llvm::StringRef BTFTypeTag,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_btf_type_tag:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_btf_type_tag, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_btf_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_btf_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_btf_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_btf_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_btf_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_btf_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_btf_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_btf_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, BTFTypeTag, I);
}

BTFTypeTagAttr *BTFTypeTagAttr::Create(TreeContext &Ctx,
                                       llvm::StringRef BTFTypeTag,
                                       SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_btf_type_tag:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_btf_type_tag, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_btf_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_btf_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_btf_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_btf_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_btf_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_btf_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_btf_type_tag:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_btf_type_tag,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, BTFTypeTag, I);
}

BTFTypeTagAttr::BTFTypeTagAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo,
                               llvm::StringRef BTFTypeTag)
    : TypeAttr(Ctx, CommonInfo, attr::BTFTypeTag, false),
      bTFTypeTagLength(BTFTypeTag.size()),
      bTFTypeTag(new (Ctx, 1) char[bTFTypeTagLength]) {
  if (!BTFTypeTag.empty())
    std::memcpy(bTFTypeTag, BTFTypeTag.data(), bTFTypeTagLength);
}

BTFTypeTagAttr *BTFTypeTagAttr::clone(TreeContext &C) const {
  auto *A = new (C) BTFTypeTagAttr(C, *this, getBTFTypeTag());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void BTFTypeTagAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((btf_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getBTFTypeTag() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::btf_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getBTFTypeTag() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::btf_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getBTFTypeTag() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::btf_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getBTFTypeTag() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::btf_type_tag";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getBTFTypeTag() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *BTFTypeTagAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "btf_type_tag";
  case 1:
    return "btf_type_tag";
  case 2:
    return "btf_type_tag";
  case 3:
    return "btf_type_tag";
  case 4:
    return "btf_type_tag";
  }
}

// BuiltinAttr implementation

BuiltinAttr *
BuiltinAttr::CreateImplicit(TreeContext &Ctx, unsigned ID,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) BuiltinAttr(Ctx, CommonInfo, ID);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

BuiltinAttr *BuiltinAttr::Create(TreeContext &Ctx, unsigned ID,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) BuiltinAttr(Ctx, CommonInfo, ID);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

BuiltinAttr *BuiltinAttr::CreateImplicit(TreeContext &Ctx, unsigned ID,
                                         SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return CreateImplicit(Ctx, ID, I);
}

BuiltinAttr *BuiltinAttr::Create(TreeContext &Ctx, unsigned ID,
                                 SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return Create(Ctx, ID, I);
}

BuiltinAttr::BuiltinAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo, unsigned ID)
    : InheritableAttr(Ctx, CommonInfo, attr::Builtin, false, false), iD(ID) {}

BuiltinAttr *BuiltinAttr::clone(TreeContext &C) const {
  auto *A = new (C) BuiltinAttr(C, *this, iD);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void BuiltinAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {}

const char *BuiltinAttr::getSpelling() const { return "(No spelling)"; }

// BuiltinAliasAttr implementation

BuiltinAliasAttr *
BuiltinAliasAttr::CreateImplicit(TreeContext &Ctx, IdentifierInfo *BuiltinName,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) BuiltinAliasAttr(Ctx, CommonInfo, BuiltinName);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

BuiltinAliasAttr *
BuiltinAliasAttr::Create(TreeContext &Ctx, IdentifierInfo *BuiltinName,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) BuiltinAliasAttr(Ctx, CommonInfo, BuiltinName);
  return A;
}

BuiltinAliasAttr *BuiltinAliasAttr::CreateImplicit(TreeContext &Ctx,
                                                   IdentifierInfo *BuiltinName,
                                                   SourceRange Range,
                                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_neverc_builtin_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_builtin_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_builtin_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_builtin_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_neverc_builtin_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_neverc_builtin_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, BuiltinName, I);
}

BuiltinAliasAttr *BuiltinAliasAttr::Create(TreeContext &Ctx,
                                           IdentifierInfo *BuiltinName,
                                           SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_neverc_builtin_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_builtin_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_builtin_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_builtin_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_neverc_builtin_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_neverc_builtin_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, BuiltinName, I);
}

BuiltinAliasAttr::BuiltinAliasAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo,
                                   IdentifierInfo *BuiltinName)
    : Attr(Ctx, CommonInfo, attr::BuiltinAlias, false),
      builtinName(BuiltinName) {}

BuiltinAliasAttr::Spelling BuiltinAliasAttr::getSemanticSpelling() const {
  switch (getAttributeSpellingListIndex()) {
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

BuiltinAliasAttr *BuiltinAliasAttr::clone(TreeContext &C) const {
  auto *A = new (C) BuiltinAliasAttr(C, *this, builtinName);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void BuiltinAliasAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " [[neverc::builtin_alias";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getBuiltinName() ? getBuiltinName()->getName() : "") << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 1: {
    OS << " [[neverc::builtin_alias";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getBuiltinName() ? getBuiltinName()->getName() : "") << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " __attribute__((neverc_builtin_alias";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getBuiltinName() ? getBuiltinName()->getName() : "") << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  }
}

const char *BuiltinAliasAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "builtin_alias";
  case 1:
    return "builtin_alias";
  case 2:
    return "neverc_builtin_alias";
  }
}

// C11NoReturnAttr implementation

C11NoReturnAttr *
C11NoReturnAttr::CreateImplicit(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) C11NoReturnAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

C11NoReturnAttr *
C11NoReturnAttr::Create(TreeContext &Ctx,
                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) C11NoReturnAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

C11NoReturnAttr *C11NoReturnAttr::CreateImplicit(TreeContext &Ctx,
                                                 SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

C11NoReturnAttr *C11NoReturnAttr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

C11NoReturnAttr::C11NoReturnAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::C11NoReturn, false, false) {}

C11NoReturnAttr *C11NoReturnAttr::clone(TreeContext &C) const {
  auto *A = new (C) C11NoReturnAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void C11NoReturnAttr::printPretty(raw_ostream &OS,
                                  const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " _Noreturn";
    OS << "";
    break;
  }
  }
}

const char *C11NoReturnAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "_Noreturn";
  }
}

// CDeclAttr implementation

CDeclAttr *CDeclAttr::CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CDeclAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CDeclAttr *CDeclAttr::Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CDeclAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CDeclAttr *CDeclAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_cdecl:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_cdecl,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_cdecl:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_cdecl, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_cdecl:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_cdecl, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Keyword_cdecl:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_cdecl, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

CDeclAttr *CDeclAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_cdecl:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_cdecl,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_cdecl:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_cdecl, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_cdecl:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_cdecl, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Keyword_cdecl:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_cdecl, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

CDeclAttr::CDeclAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::CDecl, false, false) {}

CDeclAttr *CDeclAttr::clone(TreeContext &C) const {
  auto *A = new (C) CDeclAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void CDeclAttr::printPretty(raw_ostream &OS,
                            const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((cdecl";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::cdecl";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::cdecl";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __cdecl";
    OS << "";
    break;
  }
  case 4: {
    OS << " _cdecl";
    OS << "";
    break;
  }
  case 5: {
    OS << " cdecl";
    OS << "";
    break;
  }
  }
}

const char *CDeclAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "cdecl";
  case 1:
    return "cdecl";
  case 2:
    return "cdecl";
  case 3:
    return "__cdecl";
  case 4:
    return "_cdecl";
  case 5:
    return "cdecl";
  }
}

// CFGuardAttr implementation

CFGuardAttr *
CFGuardAttr::CreateImplicit(TreeContext &Ctx, CFGuardAttr::GuardArg Guard,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CFGuardAttr(Ctx, CommonInfo, Guard);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CFGuardAttr *CFGuardAttr::Create(TreeContext &Ctx, CFGuardAttr::GuardArg Guard,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CFGuardAttr(Ctx, CommonInfo, Guard);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CFGuardAttr *CFGuardAttr::CreateImplicit(TreeContext &Ctx,
                                         CFGuardAttr::GuardArg Guard,
                                         SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Declspec_guard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_guard, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_guard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_guard,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_guard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_clang_guard, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_guard:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_guard,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_guard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_guard, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_guard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_guard, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Guard, I);
}

CFGuardAttr *CFGuardAttr::Create(TreeContext &Ctx, CFGuardAttr::GuardArg Guard,
                                 SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Declspec_guard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_guard, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_guard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_guard,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_guard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_clang_guard, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_guard:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_guard,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_guard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_guard, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_guard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_guard, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Guard, I);
}

CFGuardAttr::CFGuardAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo,
                         CFGuardAttr::GuardArg Guard)
    : InheritableAttr(Ctx, CommonInfo, attr::CFGuard, false, false),
      guard(Guard) {}

bool CFGuardAttr::ConvertStrToGuardArg(StringRef Val,
                                       CFGuardAttr::GuardArg &Out) {
  std::optional<CFGuardAttr::GuardArg> R =
      llvm::StringSwitch<std::optional<CFGuardAttr::GuardArg>>(Val)
          .Case("nocf", CFGuardAttr::GuardArg::nocf)
          .Default(std::optional<CFGuardAttr::GuardArg>());
  if (R) {
    Out = *R;
    return true;
  }
  return false;
}

const char *CFGuardAttr::ConvertGuardArgToStr(CFGuardAttr::GuardArg Val) {
  switch (Val) {
  case CFGuardAttr::GuardArg::nocf:
    return "nocf";
  }
  llvm_unreachable("No enumerator with that value");
}
CFGuardAttr *CFGuardAttr::clone(TreeContext &C) const {
  auto *A = new (C) CFGuardAttr(C, *this, guard);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void CFGuardAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __declspec(guard";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << CFGuardAttr::ConvertGuardArgToStr(getGuard()) << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << ")";
    break;
  }
  case 1: {
    OS << " __attribute__((guard";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << CFGuardAttr::ConvertGuardArgToStr(getGuard()) << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 2: {
    OS << " [[clang::guard";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << CFGuardAttr::ConvertGuardArgToStr(getGuard()) << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[neverc::guard";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << CFGuardAttr::ConvertGuardArgToStr(getGuard()) << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[clang::guard";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << CFGuardAttr::ConvertGuardArgToStr(getGuard()) << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " [[neverc::guard";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << CFGuardAttr::ConvertGuardArgToStr(getGuard()) << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *CFGuardAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "guard";
  case 1:
    return "guard";
  case 2:
    return "guard";
  case 3:
    return "guard";
  case 4:
    return "guard";
  case 5:
    return "guard";
  }
}

// CPUDispatchAttr implementation

CPUDispatchAttr *
CPUDispatchAttr::CreateImplicit(TreeContext &Ctx, IdentifierInfo **Cpus,
                                unsigned CpusSize,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CPUDispatchAttr(Ctx, CommonInfo, Cpus, CpusSize);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CPUDispatchAttr *
CPUDispatchAttr::Create(TreeContext &Ctx, IdentifierInfo **Cpus,
                        unsigned CpusSize,
                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CPUDispatchAttr(Ctx, CommonInfo, Cpus, CpusSize);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CPUDispatchAttr *CPUDispatchAttr::CreateImplicit(TreeContext &Ctx,
                                                 IdentifierInfo **Cpus,
                                                 unsigned CpusSize,
                                                 SourceRange Range,
                                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_cpu_dispatch:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_cpu_dispatch, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_cpu_dispatch:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_cpu_dispatch,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_cpu_dispatch:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_cpu_dispatch,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_cpu_dispatch:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_cpu_dispatch,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_cpu_dispatch:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_cpu_dispatch,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Declspec_cpu_dispatch:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Declspec, Declspec_cpu_dispatch,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Cpus, CpusSize, I);
}

CPUDispatchAttr *CPUDispatchAttr::Create(TreeContext &Ctx,
                                         IdentifierInfo **Cpus,
                                         unsigned CpusSize, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_cpu_dispatch:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_cpu_dispatch, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_cpu_dispatch:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_cpu_dispatch,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_cpu_dispatch:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_cpu_dispatch,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_cpu_dispatch:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_cpu_dispatch,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_cpu_dispatch:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_cpu_dispatch,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Declspec_cpu_dispatch:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Declspec, Declspec_cpu_dispatch,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Cpus, CpusSize, I);
}

CPUDispatchAttr::CPUDispatchAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo,
                                 IdentifierInfo **Cpus, unsigned CpusSize)
    : InheritableAttr(Ctx, CommonInfo, attr::CPUDispatch, false, false),
      cpus_Size(CpusSize), cpus_(new (Ctx, 16) IdentifierInfo *[cpus_Size]) {
  std::copy(Cpus, Cpus + cpus_Size, cpus_);
}

CPUDispatchAttr::CPUDispatchAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::CPUDispatch, false, false),
      cpus_Size(0), cpus_(nullptr) {}

CPUDispatchAttr *CPUDispatchAttr::clone(TreeContext &C) const {
  auto *A = new (C) CPUDispatchAttr(C, *this, cpus_, cpus_Size);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void CPUDispatchAttr::printPretty(raw_ostream &OS,
                                  const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((cpu_dispatch";
    OS << "";
    for (const auto &Val : cpus()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::cpu_dispatch";
    OS << "";
    for (const auto &Val : cpus()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::cpu_dispatch";
    OS << "";
    for (const auto &Val : cpus()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::cpu_dispatch";
    OS << "";
    for (const auto &Val : cpus()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::cpu_dispatch";
    OS << "";
    for (const auto &Val : cpus()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " __declspec(cpu_dispatch";
    OS << "";
    for (const auto &Val : cpus()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << ")";
    break;
  }
  }
}

const char *CPUDispatchAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "cpu_dispatch";
  case 1:
    return "cpu_dispatch";
  case 2:
    return "cpu_dispatch";
  case 3:
    return "cpu_dispatch";
  case 4:
    return "cpu_dispatch";
  case 5:
    return "cpu_dispatch";
  }
}

// CPUSpecificAttr implementation

CPUSpecificAttr *
CPUSpecificAttr::CreateImplicit(TreeContext &Ctx, IdentifierInfo **Cpus,
                                unsigned CpusSize,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CPUSpecificAttr(Ctx, CommonInfo, Cpus, CpusSize);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CPUSpecificAttr *
CPUSpecificAttr::Create(TreeContext &Ctx, IdentifierInfo **Cpus,
                        unsigned CpusSize,
                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CPUSpecificAttr(Ctx, CommonInfo, Cpus, CpusSize);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CPUSpecificAttr *CPUSpecificAttr::CreateImplicit(TreeContext &Ctx,
                                                 IdentifierInfo **Cpus,
                                                 unsigned CpusSize,
                                                 SourceRange Range,
                                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_cpu_specific:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_cpu_specific, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_cpu_specific:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_cpu_specific,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_cpu_specific:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_cpu_specific,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_cpu_specific:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_cpu_specific,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_cpu_specific:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_cpu_specific,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Declspec_cpu_specific:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Declspec, Declspec_cpu_specific,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Cpus, CpusSize, I);
}

CPUSpecificAttr *CPUSpecificAttr::Create(TreeContext &Ctx,
                                         IdentifierInfo **Cpus,
                                         unsigned CpusSize, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_cpu_specific:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_cpu_specific, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_cpu_specific:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_cpu_specific,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_cpu_specific:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_cpu_specific,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_cpu_specific:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_cpu_specific,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_cpu_specific:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_cpu_specific,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Declspec_cpu_specific:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Declspec, Declspec_cpu_specific,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Cpus, CpusSize, I);
}

CPUSpecificAttr::CPUSpecificAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo,
                                 IdentifierInfo **Cpus, unsigned CpusSize)
    : InheritableAttr(Ctx, CommonInfo, attr::CPUSpecific, false, false),
      cpus_Size(CpusSize), cpus_(new (Ctx, 16) IdentifierInfo *[cpus_Size]) {
  std::copy(Cpus, Cpus + cpus_Size, cpus_);
}

CPUSpecificAttr::CPUSpecificAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::CPUSpecific, false, false),
      cpus_Size(0), cpus_(nullptr) {}

CPUSpecificAttr *CPUSpecificAttr::clone(TreeContext &C) const {
  auto *A = new (C) CPUSpecificAttr(C, *this, cpus_, cpus_Size);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void CPUSpecificAttr::printPretty(raw_ostream &OS,
                                  const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((cpu_specific";
    OS << "";
    for (const auto &Val : cpus()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::cpu_specific";
    OS << "";
    for (const auto &Val : cpus()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::cpu_specific";
    OS << "";
    for (const auto &Val : cpus()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::cpu_specific";
    OS << "";
    for (const auto &Val : cpus()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::cpu_specific";
    OS << "";
    for (const auto &Val : cpus()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " __declspec(cpu_specific";
    OS << "";
    for (const auto &Val : cpus()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << ")";
    break;
  }
  }
}

const char *CPUSpecificAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "cpu_specific";
  case 1:
    return "cpu_specific";
  case 2:
    return "cpu_specific";
  case 3:
    return "cpu_specific";
  case 4:
    return "cpu_specific";
  case 5:
    return "cpu_specific";
  }
}

// CallbackAttr implementation

CallbackAttr *
CallbackAttr::CreateImplicit(TreeContext &Ctx, int *Encoding,
                             unsigned EncodingSize,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CallbackAttr(Ctx, CommonInfo, Encoding, EncodingSize);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CallbackAttr *CallbackAttr::Create(TreeContext &Ctx, int *Encoding,
                                   unsigned EncodingSize,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CallbackAttr(Ctx, CommonInfo, Encoding, EncodingSize);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CallbackAttr *CallbackAttr::CreateImplicit(TreeContext &Ctx, int *Encoding,
                                           unsigned EncodingSize,
                                           SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_callback:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_callback, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_callback:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_callback,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_callback:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_callback,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_callback:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_callback, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_callback:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_callback, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Encoding, EncodingSize, I);
}

CallbackAttr *CallbackAttr::Create(TreeContext &Ctx, int *Encoding,
                                   unsigned EncodingSize, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_callback:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_callback, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_callback:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_callback,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_callback:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_callback,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_callback:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_callback, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_callback:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_callback, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Encoding, EncodingSize, I);
}

CallbackAttr::CallbackAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo, int *Encoding,
                           unsigned EncodingSize)
    : InheritableAttr(Ctx, CommonInfo, attr::Callback, false, false),
      encoding_Size(EncodingSize), encoding_(new (Ctx, 16) int[encoding_Size]) {
  std::copy(Encoding, Encoding + encoding_Size, encoding_);
}

CallbackAttr::CallbackAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Callback, false, false),
      encoding_Size(0), encoding_(nullptr) {}

CallbackAttr *CallbackAttr::clone(TreeContext &C) const {
  auto *A = new (C) CallbackAttr(C, *this, encoding_, encoding_Size);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void CallbackAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((callback";
    OS << "";
    for (const auto &Val : encoding()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::callback";
    OS << "";
    for (const auto &Val : encoding()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::callback";
    OS << "";
    for (const auto &Val : encoding()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::callback";
    OS << "";
    for (const auto &Val : encoding()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::callback";
    OS << "";
    for (const auto &Val : encoding()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *CallbackAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "callback";
  case 1:
    return "callback";
  case 2:
    return "callback";
  case 3:
    return "callback";
  case 4:
    return "callback";
  }
}

// CarriesDependencyAttr implementation

CarriesDependencyAttr *
CarriesDependencyAttr::CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CarriesDependencyAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CarriesDependencyAttr *
CarriesDependencyAttr::Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CarriesDependencyAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CarriesDependencyAttr *CarriesDependencyAttr::CreateImplicit(TreeContext &Ctx,
                                                             SourceRange Range,
                                                             Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_carries_dependency:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_carries_dependency,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_carries_dependency:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_carries_dependency,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

CarriesDependencyAttr *
CarriesDependencyAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_carries_dependency:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_carries_dependency,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_carries_dependency:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_carries_dependency,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

CarriesDependencyAttr::CarriesDependencyAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableParamAttr(Ctx, CommonInfo, attr::CarriesDependency, false,
                           false) {}

CarriesDependencyAttr *CarriesDependencyAttr::clone(TreeContext &C) const {
  auto *A = new (C) CarriesDependencyAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void CarriesDependencyAttr::printPretty(raw_ostream &OS,
                                        const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((carries_dependency";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[carries_dependency";
    OS << "]]";
    break;
  }
  }
}

const char *CarriesDependencyAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "carries_dependency";
  case 1:
    return "carries_dependency";
  }
}

// CleanupAttr implementation

CleanupAttr *
CleanupAttr::CreateImplicit(TreeContext &Ctx, FunctionDecl *FunctionDecl,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CleanupAttr(Ctx, CommonInfo, FunctionDecl);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CleanupAttr *CleanupAttr::Create(TreeContext &Ctx, FunctionDecl *FunctionDecl,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CleanupAttr(Ctx, CommonInfo, FunctionDecl);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CleanupAttr *CleanupAttr::CreateImplicit(TreeContext &Ctx,
                                         FunctionDecl *FunctionDecl,
                                         SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_cleanup:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_cleanup,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_cleanup:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_cleanup, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_cleanup:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_cleanup, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, FunctionDecl, I);
}

CleanupAttr *CleanupAttr::Create(TreeContext &Ctx, FunctionDecl *FunctionDecl,
                                 SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_cleanup:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_cleanup,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_cleanup:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_cleanup, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_cleanup:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_cleanup, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, FunctionDecl, I);
}

CleanupAttr::CleanupAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo,
                         FunctionDecl *FunctionDecl)
    : InheritableAttr(Ctx, CommonInfo, attr::Cleanup, false, false),
      functionDecl(FunctionDecl) {}

CleanupAttr *CleanupAttr::clone(TreeContext &C) const {
  auto *A = new (C) CleanupAttr(C, *this, functionDecl);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void CleanupAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((cleanup";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFunctionDecl()->getNameInfo().getAsString() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::cleanup";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFunctionDecl()->getNameInfo().getAsString() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::cleanup";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFunctionDecl()->getNameInfo().getAsString() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *CleanupAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "cleanup";
  case 1:
    return "cleanup";
  case 2:
    return "cleanup";
  }
}

// CodeAlignAttr implementation

CodeAlignAttr *
CodeAlignAttr::CreateImplicit(TreeContext &Ctx, Expr *Alignment,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CodeAlignAttr(Ctx, CommonInfo, Alignment);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CodeAlignAttr *CodeAlignAttr::Create(TreeContext &Ctx, Expr *Alignment,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CodeAlignAttr(Ctx, CommonInfo, Alignment);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CodeAlignAttr *CodeAlignAttr::CreateImplicit(TreeContext &Ctx, Expr *Alignment,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_code_align:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_code_align, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_code_align:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_code_align,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_code_align:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_code_align,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_code_align:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_code_align,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_code_align:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_code_align,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Alignment, I);
}

CodeAlignAttr *CodeAlignAttr::Create(TreeContext &Ctx, Expr *Alignment,
                                     SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_code_align:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_code_align, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_code_align:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_code_align,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_code_align:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_code_align,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_code_align:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_code_align,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_code_align:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_code_align,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Alignment, I);
}

CodeAlignAttr::CodeAlignAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo,
                             Expr *Alignment)
    : StmtAttr(Ctx, CommonInfo, attr::CodeAlign, false), alignment(Alignment) {}

CodeAlignAttr *CodeAlignAttr::clone(TreeContext &C) const {
  auto *A = new (C) CodeAlignAttr(C, *this, alignment);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void CodeAlignAttr::printPretty(raw_ostream &OS,
                                const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((code_align";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "";
    getAlignment()->printPretty(OS, nullptr, Policy);
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::code_align";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "";
    getAlignment()->printPretty(OS, nullptr, Policy);
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::code_align";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "";
    getAlignment()->printPretty(OS, nullptr, Policy);
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::code_align";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "";
    getAlignment()->printPretty(OS, nullptr, Policy);
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::code_align";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "";
    getAlignment()->printPretty(OS, nullptr, Policy);
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *CodeAlignAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "code_align";
  case 1:
    return "code_align";
  case 2:
    return "code_align";
  case 3:
    return "code_align";
  case 4:
    return "code_align";
  }
}

// CodeSegAttr implementation

CodeSegAttr *
CodeSegAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CodeSegAttr(Ctx, CommonInfo, Name);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CodeSegAttr *CodeSegAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CodeSegAttr(Ctx, CommonInfo, Name);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CodeSegAttr *CodeSegAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                                         SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Declspec, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, Name, I);
}

CodeSegAttr *CodeSegAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                 SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Declspec, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, Name, I);
}

CodeSegAttr::CodeSegAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo,
                         llvm::StringRef Name)
    : InheritableAttr(Ctx, CommonInfo, attr::CodeSeg, false, false),
      nameLength(Name.size()), name(new (Ctx, 1) char[nameLength]) {
  if (!Name.empty())
    std::memcpy(name, Name.data(), nameLength);
}

CodeSegAttr *CodeSegAttr::clone(TreeContext &C) const {
  auto *A = new (C) CodeSegAttr(C, *this, getName());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void CodeSegAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __declspec(code_seg";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << ")";
    break;
  }
  }
}

const char *CodeSegAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "code_seg";
  }
}

// ColdAttr implementation

ColdAttr *ColdAttr::CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ColdAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ColdAttr *ColdAttr::Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ColdAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ColdAttr *ColdAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_cold:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_cold,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_cold:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_cold, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_cold:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_cold, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

ColdAttr *ColdAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_cold:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_cold,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_cold:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_cold, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_cold:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_cold, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

ColdAttr::ColdAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Cold, false, false) {}

ColdAttr *ColdAttr::clone(TreeContext &C) const {
  auto *A = new (C) ColdAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ColdAttr::printPretty(raw_ostream &OS,
                           const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((cold";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::cold";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::cold";
    OS << "]]";
    break;
  }
  }
}

const char *ColdAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "cold";
  case 1:
    return "cold";
  case 2:
    return "cold";
  }
}

// CommonAttr implementation

CommonAttr *CommonAttr::CreateImplicit(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CommonAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CommonAttr *CommonAttr::Create(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) CommonAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

CommonAttr *CommonAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_common:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_common,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_common:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_common, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_common:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_common, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

CommonAttr *CommonAttr::Create(TreeContext &Ctx, SourceRange Range,
                               Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_common:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_common,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_common:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_common, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_common:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_common, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

CommonAttr::CommonAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Common, false, false) {}

CommonAttr *CommonAttr::clone(TreeContext &C) const {
  auto *A = new (C) CommonAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void CommonAttr::printPretty(raw_ostream &OS,
                             const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((common";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::common";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::common";
    OS << "]]";
    break;
  }
  }
}

const char *CommonAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "common";
  case 1:
    return "common";
  case 2:
    return "common";
  }
}

// ConstAttr implementation

ConstAttr *ConstAttr::CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ConstAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ConstAttr *ConstAttr::Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ConstAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ConstAttr *ConstAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_const:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_const,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_const:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_const, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_const:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_const, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

ConstAttr *ConstAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_const:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_const,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_const:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_const, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_const:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_const, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

ConstAttr::ConstAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Const, false, false) {}

ConstAttr *ConstAttr::clone(TreeContext &C) const {
  auto *A = new (C) ConstAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ConstAttr::printPretty(raw_ostream &OS,
                            const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((const";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::const";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::const";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __attribute__((__const";
    OS << "))";
    break;
  }
  case 4: {
    OS << " [[gnu::__const";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " [[gnu::__const";
    OS << "]]";
    break;
  }
  }
}

const char *ConstAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "const";
  case 1:
    return "const";
  case 2:
    return "const";
  case 3:
    return "__const";
  case 4:
    return "__const";
  case 5:
    return "__const";
  }
}

// ConstructorAttr implementation

ConstructorAttr *
ConstructorAttr::CreateImplicit(TreeContext &Ctx, int Priority,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ConstructorAttr(Ctx, CommonInfo, Priority);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ConstructorAttr *
ConstructorAttr::Create(TreeContext &Ctx, int Priority,
                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ConstructorAttr(Ctx, CommonInfo, Priority);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ConstructorAttr *ConstructorAttr::CreateImplicit(TreeContext &Ctx, int Priority,
                                                 SourceRange Range,
                                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_constructor:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_constructor, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_constructor:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_constructor,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_constructor:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_constructor, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Priority, I);
}

ConstructorAttr *ConstructorAttr::Create(TreeContext &Ctx, int Priority,
                                         SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_constructor:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_constructor, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_constructor:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_constructor,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_constructor:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_constructor, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Priority, I);
}

ConstructorAttr::ConstructorAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo,
                                 int Priority)
    : InheritableAttr(Ctx, CommonInfo, attr::Constructor, false, false),
      priority(Priority) {}

ConstructorAttr::ConstructorAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Constructor, false, false),
      priority() {}

ConstructorAttr *ConstructorAttr::clone(TreeContext &C) const {
  auto *A = new (C) ConstructorAttr(C, *this, priority);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ConstructorAttr::printPretty(raw_ostream &OS,
                                  const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((constructor";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getPriority() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::constructor";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getPriority() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::constructor";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getPriority() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *ConstructorAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "constructor";
  case 1:
    return "constructor";
  case 2:
    return "constructor";
  }
}

// ConvergentAttr implementation

ConvergentAttr *
ConvergentAttr::CreateImplicit(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ConvergentAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ConvergentAttr *ConvergentAttr::Create(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ConvergentAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ConvergentAttr *ConvergentAttr::CreateImplicit(TreeContext &Ctx,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_convergent:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_convergent, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_convergent:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_convergent,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_convergent:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_convergent,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_convergent:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_convergent,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_convergent:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_convergent,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

ConvergentAttr *ConvergentAttr::Create(TreeContext &Ctx, SourceRange Range,
                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_convergent:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_convergent, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_convergent:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_convergent,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_convergent:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_convergent,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_convergent:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_convergent,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_convergent:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_convergent,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

ConvergentAttr::ConvergentAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Convergent, false, false) {}

ConvergentAttr *ConvergentAttr::clone(TreeContext &C) const {
  auto *A = new (C) ConvergentAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ConvergentAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((convergent";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::convergent";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::convergent";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::convergent";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::convergent";
    OS << "]]";
    break;
  }
  }
}

const char *ConvergentAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "convergent";
  case 1:
    return "convergent";
  case 2:
    return "convergent";
  case 3:
    return "convergent";
  case 4:
    return "convergent";
  }
}

// DLLExportAttr implementation

DLLExportAttr *
DLLExportAttr::CreateImplicit(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DLLExportAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DLLExportAttr *DLLExportAttr::Create(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DLLExportAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DLLExportAttr *DLLExportAttr::CreateImplicit(TreeContext &Ctx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Declspec_dllexport:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_dllexport, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_dllexport:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_dllexport, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_dllexport:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_dllexport,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_dllexport:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_dllexport, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

DLLExportAttr *DLLExportAttr::Create(TreeContext &Ctx, SourceRange Range,
                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Declspec_dllexport:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_dllexport, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_dllexport:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_dllexport, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_dllexport:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_dllexport,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_dllexport:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_dllexport, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

DLLExportAttr::DLLExportAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::DLLExport, false, false) {}

DLLExportAttr *DLLExportAttr::clone(TreeContext &C) const {
  auto *A = new (C) DLLExportAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void DLLExportAttr::printPretty(raw_ostream &OS,
                                const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __declspec(dllexport";
    OS << ")";
    break;
  }
  case 1: {
    OS << " __attribute__((dllexport";
    OS << "))";
    break;
  }
  case 2: {
    OS << " [[gnu::dllexport";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[gnu::dllexport";
    OS << "]]";
    break;
  }
  }
}

const char *DLLExportAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "dllexport";
  case 1:
    return "dllexport";
  case 2:
    return "dllexport";
  case 3:
    return "dllexport";
  }
}

// DLLImportAttr implementation

DLLImportAttr *
DLLImportAttr::CreateImplicit(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DLLImportAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DLLImportAttr *DLLImportAttr::Create(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DLLImportAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DLLImportAttr *DLLImportAttr::CreateImplicit(TreeContext &Ctx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Declspec_dllimport:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_dllimport, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_dllimport:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_dllimport, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_dllimport:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_dllimport,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_dllimport:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_dllimport, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

DLLImportAttr *DLLImportAttr::Create(TreeContext &Ctx, SourceRange Range,
                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Declspec_dllimport:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_dllimport, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_dllimport:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_dllimport, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_dllimport:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_dllimport,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_dllimport:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_dllimport, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

DLLImportAttr::DLLImportAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::DLLImport, false, false) {}

DLLImportAttr *DLLImportAttr::clone(TreeContext &C) const {
  auto *A = new (C) DLLImportAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void DLLImportAttr::printPretty(raw_ostream &OS,
                                const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __declspec(dllimport";
    OS << ")";
    break;
  }
  case 1: {
    OS << " __attribute__((dllimport";
    OS << "))";
    break;
  }
  case 2: {
    OS << " [[gnu::dllimport";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[gnu::dllimport";
    OS << "]]";
    break;
  }
  }
}

const char *DLLImportAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "dllimport";
  case 1:
    return "dllimport";
  case 2:
    return "dllimport";
  case 3:
    return "dllimport";
  }
}

// DeprecatedAttr implementation

DeprecatedAttr *
DeprecatedAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Message,
                               llvm::StringRef Replacement,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DeprecatedAttr(Ctx, CommonInfo, Message, Replacement);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DeprecatedAttr *DeprecatedAttr::Create(TreeContext &Ctx,
                                       llvm::StringRef Message,
                                       llvm::StringRef Replacement,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DeprecatedAttr(Ctx, CommonInfo, Message, Replacement);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DeprecatedAttr *DeprecatedAttr::CreateImplicit(TreeContext &Ctx,
                                               llvm::StringRef Message,
                                               llvm::StringRef Replacement,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_deprecated:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_deprecated, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_deprecated:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_deprecated,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_deprecated:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_deprecated, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_deprecated:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_deprecated, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_deprecated:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_deprecated, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_deprecated:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_deprecated, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Message, Replacement, I);
}

DeprecatedAttr *DeprecatedAttr::Create(TreeContext &Ctx,
                                       llvm::StringRef Message,
                                       llvm::StringRef Replacement,
                                       SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_deprecated:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_deprecated, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_deprecated:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_deprecated,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_deprecated:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_deprecated, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_deprecated:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_deprecated, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_deprecated:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_deprecated, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_deprecated:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_deprecated, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Message, Replacement, I);
}

DeprecatedAttr::DeprecatedAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo,
                               llvm::StringRef Message,
                               llvm::StringRef Replacement)
    : InheritableAttr(Ctx, CommonInfo, attr::Deprecated, false, false),
      messageLength(Message.size()), message(new (Ctx, 1) char[messageLength]),
      replacementLength(Replacement.size()),
      replacement(new (Ctx, 1) char[replacementLength]) {
  if (!Message.empty())
    std::memcpy(message, Message.data(), messageLength);
  if (!Replacement.empty())
    std::memcpy(replacement, Replacement.data(), replacementLength);
}

DeprecatedAttr::DeprecatedAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Deprecated, false, false),
      messageLength(0), message(nullptr), replacementLength(0),
      replacement(nullptr) {}

DeprecatedAttr *DeprecatedAttr::clone(TreeContext &C) const {
  auto *A = new (C) DeprecatedAttr(C, *this, getMessage(), getReplacement());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void DeprecatedAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((deprecated";
    OS << "(\"" << getMessage() << "\"";
    if (!getReplacement().empty())
      OS << ", \"" << getReplacement() << "\"";
    OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::deprecated";
    OS << "(\"" << getMessage() << "\"";
    OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::deprecated";
    OS << "(\"" << getMessage() << "\"";
    OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __declspec(deprecated";
    OS << "(\"" << getMessage() << "\"";
    OS << ")";
    OS << ")";
    break;
  }
  case 4: {
    OS << " [[deprecated";
    OS << "(\"" << getMessage() << "\"";
    OS << ")";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " [[deprecated";
    OS << "(\"" << getMessage() << "\"";
    OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *DeprecatedAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "deprecated";
  case 1:
    return "deprecated";
  case 2:
    return "deprecated";
  case 3:
    return "deprecated";
  case 4:
    return "deprecated";
  case 5:
    return "deprecated";
  }
}

// DestructorAttr implementation

DestructorAttr *
DestructorAttr::CreateImplicit(TreeContext &Ctx, int Priority,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DestructorAttr(Ctx, CommonInfo, Priority);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DestructorAttr *DestructorAttr::Create(TreeContext &Ctx, int Priority,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DestructorAttr(Ctx, CommonInfo, Priority);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DestructorAttr *DestructorAttr::CreateImplicit(TreeContext &Ctx, int Priority,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_destructor:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_destructor, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_destructor:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_destructor,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_destructor:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_destructor, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Priority, I);
}

DestructorAttr *DestructorAttr::Create(TreeContext &Ctx, int Priority,
                                       SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_destructor:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_destructor, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_destructor:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_destructor,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_destructor:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_destructor, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Priority, I);
}

DestructorAttr::DestructorAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo,
                               int Priority)
    : InheritableAttr(Ctx, CommonInfo, attr::Destructor, false, false),
      priority(Priority) {}

DestructorAttr::DestructorAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Destructor, false, false),
      priority() {}

DestructorAttr *DestructorAttr::clone(TreeContext &C) const {
  auto *A = new (C) DestructorAttr(C, *this, priority);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void DestructorAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((destructor";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getPriority() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::destructor";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getPriority() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::destructor";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getPriority() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *DestructorAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "destructor";
  case 1:
    return "destructor";
  case 2:
    return "destructor";
  }
}

// DiagnoseAsBuiltinAttr implementation

DiagnoseAsBuiltinAttr *DiagnoseAsBuiltinAttr::CreateImplicit(
    TreeContext &Ctx, FunctionDecl *Function, unsigned *ArgIndices,
    unsigned ArgIndicesSize, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DiagnoseAsBuiltinAttr(Ctx, CommonInfo, Function,
                                            ArgIndices, ArgIndicesSize);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DiagnoseAsBuiltinAttr *
DiagnoseAsBuiltinAttr::Create(TreeContext &Ctx, FunctionDecl *Function,
                              unsigned *ArgIndices, unsigned ArgIndicesSize,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DiagnoseAsBuiltinAttr(Ctx, CommonInfo, Function,
                                            ArgIndices, ArgIndicesSize);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DiagnoseAsBuiltinAttr *DiagnoseAsBuiltinAttr::CreateImplicit(
    TreeContext &Ctx, FunctionDecl *Function, unsigned *ArgIndices,
    unsigned ArgIndicesSize, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_diagnose_as_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_diagnose_as_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_diagnose_as_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_diagnose_as_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_diagnose_as_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_diagnose_as_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_diagnose_as_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_diagnose_as_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_diagnose_as_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_diagnose_as_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Function, ArgIndices, ArgIndicesSize, I);
}

DiagnoseAsBuiltinAttr *
DiagnoseAsBuiltinAttr::Create(TreeContext &Ctx, FunctionDecl *Function,
                              unsigned *ArgIndices, unsigned ArgIndicesSize,
                              SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_diagnose_as_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_diagnose_as_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_diagnose_as_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_diagnose_as_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_diagnose_as_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_diagnose_as_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_diagnose_as_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_diagnose_as_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_diagnose_as_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_diagnose_as_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Function, ArgIndices, ArgIndicesSize, I);
}

DiagnoseAsBuiltinAttr::DiagnoseAsBuiltinAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    FunctionDecl *Function, unsigned *ArgIndices, unsigned ArgIndicesSize)
    : InheritableAttr(Ctx, CommonInfo, attr::DiagnoseAsBuiltin, false, false),
      function(Function), argIndices_Size(ArgIndicesSize),
      argIndices_(new (Ctx, 16) unsigned[argIndices_Size]) {
  std::copy(ArgIndices, ArgIndices + argIndices_Size, argIndices_);
}

DiagnoseAsBuiltinAttr::DiagnoseAsBuiltinAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    FunctionDecl *Function)
    : InheritableAttr(Ctx, CommonInfo, attr::DiagnoseAsBuiltin, false, false),
      function(Function), argIndices_Size(0), argIndices_(nullptr) {}

DiagnoseAsBuiltinAttr *DiagnoseAsBuiltinAttr::clone(TreeContext &C) const {
  auto *A = new (C)
      DiagnoseAsBuiltinAttr(C, *this, function, argIndices_, argIndices_Size);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void DiagnoseAsBuiltinAttr::printPretty(raw_ostream &OS,
                                        const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((diagnose_as_builtin";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFunction()->getNameInfo().getAsString() << "";
    OS << "";
    for (const auto &Val : argIndices()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::diagnose_as_builtin";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFunction()->getNameInfo().getAsString() << "";
    OS << "";
    for (const auto &Val : argIndices()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::diagnose_as_builtin";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFunction()->getNameInfo().getAsString() << "";
    OS << "";
    for (const auto &Val : argIndices()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::diagnose_as_builtin";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFunction()->getNameInfo().getAsString() << "";
    OS << "";
    for (const auto &Val : argIndices()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::diagnose_as_builtin";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFunction()->getNameInfo().getAsString() << "";
    OS << "";
    for (const auto &Val : argIndices()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val;
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *DiagnoseAsBuiltinAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "diagnose_as_builtin";
  case 1:
    return "diagnose_as_builtin";
  case 2:
    return "diagnose_as_builtin";
  case 3:
    return "diagnose_as_builtin";
  case 4:
    return "diagnose_as_builtin";
  }
}

// DiagnoseIfAttr implementation

DiagnoseIfAttr *DiagnoseIfAttr::CreateImplicit(
    TreeContext &Ctx, Expr *Cond, llvm::StringRef Message,
    DiagnoseIfAttr::DiagnosticType DiagnosticType, bool ArgDependent,
    NamedDecl *Parent, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DiagnoseIfAttr(Ctx, CommonInfo, Cond, Message,
                                     DiagnosticType, ArgDependent, Parent);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DiagnoseIfAttr *
DiagnoseIfAttr::Create(TreeContext &Ctx, Expr *Cond, llvm::StringRef Message,
                       DiagnoseIfAttr::DiagnosticType DiagnosticType,
                       bool ArgDependent, NamedDecl *Parent,
                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DiagnoseIfAttr(Ctx, CommonInfo, Cond, Message,
                                     DiagnosticType, ArgDependent, Parent);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DiagnoseIfAttr *DiagnoseIfAttr::CreateImplicit(
    TreeContext &Ctx, Expr *Cond, llvm::StringRef Message,
    DiagnoseIfAttr::DiagnosticType DiagnosticType, bool ArgDependent,
    NamedDecl *Parent, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_GNU, 0, false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, Cond, Message, DiagnosticType, ArgDependent,
                        Parent, I);
}

DiagnoseIfAttr *
DiagnoseIfAttr::Create(TreeContext &Ctx, Expr *Cond, llvm::StringRef Message,
                       DiagnoseIfAttr::DiagnosticType DiagnosticType,
                       bool ArgDependent, NamedDecl *Parent,
                       SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_GNU, 0, false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, Cond, Message, DiagnosticType, ArgDependent, Parent, I);
}

DiagnoseIfAttr *
DiagnoseIfAttr::CreateImplicit(TreeContext &Ctx, Expr *Cond,
                               llvm::StringRef Message,
                               DiagnoseIfAttr::DiagnosticType DiagnosticType,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A =
      new (Ctx) DiagnoseIfAttr(Ctx, CommonInfo, Cond, Message, DiagnosticType);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DiagnoseIfAttr *
DiagnoseIfAttr::Create(TreeContext &Ctx, Expr *Cond, llvm::StringRef Message,
                       DiagnoseIfAttr::DiagnosticType DiagnosticType,
                       const AttributeCommonInfo &CommonInfo) {
  auto *A =
      new (Ctx) DiagnoseIfAttr(Ctx, CommonInfo, Cond, Message, DiagnosticType);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DiagnoseIfAttr *DiagnoseIfAttr::CreateImplicit(
    TreeContext &Ctx, Expr *Cond, llvm::StringRef Message,
    DiagnoseIfAttr::DiagnosticType DiagnosticType, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_GNU, 0, false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, Cond, Message, DiagnosticType, I);
}

DiagnoseIfAttr *
DiagnoseIfAttr::Create(TreeContext &Ctx, Expr *Cond, llvm::StringRef Message,
                       DiagnoseIfAttr::DiagnosticType DiagnosticType,
                       SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_GNU, 0, false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, Cond, Message, DiagnosticType, I);
}

DiagnoseIfAttr::DiagnoseIfAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo,
                               Expr *Cond, llvm::StringRef Message,
                               DiagnoseIfAttr::DiagnosticType DiagnosticType,
                               bool ArgDependent, NamedDecl *Parent)
    : InheritableAttr(Ctx, CommonInfo, attr::DiagnoseIf, true, true),
      cond(Cond), messageLength(Message.size()),
      message(new (Ctx, 1) char[messageLength]), diagnosticType(DiagnosticType),
      argDependent(ArgDependent), parent(Parent) {
  if (!Message.empty())
    std::memcpy(message, Message.data(), messageLength);
}

DiagnoseIfAttr::DiagnoseIfAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo,
                               Expr *Cond, llvm::StringRef Message,
                               DiagnoseIfAttr::DiagnosticType DiagnosticType)
    : InheritableAttr(Ctx, CommonInfo, attr::DiagnoseIf, true, true),
      cond(Cond), messageLength(Message.size()),
      message(new (Ctx, 1) char[messageLength]), diagnosticType(DiagnosticType),
      argDependent(), parent() {
  if (!Message.empty())
    std::memcpy(message, Message.data(), messageLength);
}

bool DiagnoseIfAttr::ConvertStrToDiagnosticType(
    StringRef Val, DiagnoseIfAttr::DiagnosticType &Out) {
  std::optional<DiagnoseIfAttr::DiagnosticType> R =
      llvm::StringSwitch<std::optional<DiagnoseIfAttr::DiagnosticType>>(Val)
          .Case("error", DiagnoseIfAttr::DiagnosticType::DT_Error)
          .Case("warning", DiagnoseIfAttr::DiagnosticType::DT_Warning)
          .Default(std::optional<DiagnoseIfAttr::DiagnosticType>());
  if (R) {
    Out = *R;
    return true;
  }
  return false;
}

const char *
DiagnoseIfAttr::ConvertDiagnosticTypeToStr(DiagnoseIfAttr::DiagnosticType Val) {
  switch (Val) {
  case DiagnoseIfAttr::DiagnosticType::DT_Error:
    return "error";
  case DiagnoseIfAttr::DiagnosticType::DT_Warning:
    return "warning";
  }
  llvm_unreachable("No enumerator with that value");
}

DiagnoseIfAttr *DiagnoseIfAttr::clone(TreeContext &C) const {
  auto *A = new (C) DiagnoseIfAttr(C, *this, cond, getMessage(), diagnosticType,
                                   argDependent, parent);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void DiagnoseIfAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((diagnose_if";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "";
    getCond()->printPretty(OS, nullptr, Policy);
    OS << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\""
       << DiagnoseIfAttr::ConvertDiagnosticTypeToStr(getDiagnosticType())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  }
}

const char *DiagnoseIfAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "diagnose_if";
  }
}

// DisableTailCallsAttr implementation

DisableTailCallsAttr *
DisableTailCallsAttr::CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DisableTailCallsAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DisableTailCallsAttr *
DisableTailCallsAttr::Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DisableTailCallsAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DisableTailCallsAttr *DisableTailCallsAttr::CreateImplicit(TreeContext &Ctx,
                                                           SourceRange Range,
                                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_disable_tail_calls:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_disable_tail_calls,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_disable_tail_calls:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_disable_tail_calls,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_disable_tail_calls:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_disable_tail_calls,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_disable_tail_calls:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_disable_tail_calls,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_disable_tail_calls:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_disable_tail_calls,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

DisableTailCallsAttr *
DisableTailCallsAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_disable_tail_calls:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_disable_tail_calls,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_disable_tail_calls:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_disable_tail_calls,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_disable_tail_calls:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_disable_tail_calls,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_disable_tail_calls:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_disable_tail_calls,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_disable_tail_calls:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_disable_tail_calls,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

DisableTailCallsAttr::DisableTailCallsAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::DisableTailCalls, false, false) {}

DisableTailCallsAttr *DisableTailCallsAttr::clone(TreeContext &C) const {
  auto *A = new (C) DisableTailCallsAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void DisableTailCallsAttr::printPretty(raw_ostream &OS,
                                       const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((disable_tail_calls";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::disable_tail_calls";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::disable_tail_calls";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::disable_tail_calls";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::disable_tail_calls";
    OS << "]]";
    break;
  }
  }
}

const char *DisableTailCallsAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "disable_tail_calls";
  case 1:
    return "disable_tail_calls";
  case 2:
    return "disable_tail_calls";
  case 3:
    return "disable_tail_calls";
  case 4:
    return "disable_tail_calls";
  }
}

// DisableTryStmtAttr implementation

DisableTryStmtAttr *
DisableTryStmtAttr::CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DisableTryStmtAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DisableTryStmtAttr *
DisableTryStmtAttr::Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) DisableTryStmtAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

DisableTryStmtAttr *DisableTryStmtAttr::CreateImplicit(TreeContext &Ctx,
                                                       SourceRange Range,
                                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_disable_try_stmt:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_disable_try_stmt,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_disable_try_stmt:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_disable_try_stmt,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_disable_try_stmt:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_disable_try_stmt,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Declspec_disable_try_stmt:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Declspec, Declspec_disable_try_stmt,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

DisableTryStmtAttr *DisableTryStmtAttr::Create(TreeContext &Ctx,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_disable_try_stmt:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_disable_try_stmt,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_disable_try_stmt:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_disable_try_stmt,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_disable_try_stmt:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_disable_try_stmt,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Declspec_disable_try_stmt:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Declspec, Declspec_disable_try_stmt,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

DisableTryStmtAttr::DisableTryStmtAttr(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::DisableTryStmt, false, false) {}

DisableTryStmtAttr *DisableTryStmtAttr::clone(TreeContext &C) const {
  auto *A = new (C) DisableTryStmtAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void DisableTryStmtAttr::printPretty(raw_ostream &OS,
                                     const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((disable_try_stmt";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::disable_try_stmt";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::disable_try_stmt";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __declspec(disable_try_stmt";
    OS << ")";
    break;
  }
  }
}

const char *DisableTryStmtAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "disable_try_stmt";
  case 1:
    return "disable_try_stmt";
  case 2:
    return "disable_try_stmt";
  case 3:
    return "disable_try_stmt";
  }
}

// EnableIfAttr implementation

EnableIfAttr *
EnableIfAttr::CreateImplicit(TreeContext &Ctx, Expr *Cond,
                             llvm::StringRef Message,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) EnableIfAttr(Ctx, CommonInfo, Cond, Message);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

EnableIfAttr *EnableIfAttr::Create(TreeContext &Ctx, Expr *Cond,
                                   llvm::StringRef Message,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) EnableIfAttr(Ctx, CommonInfo, Cond, Message);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

EnableIfAttr *EnableIfAttr::CreateImplicit(TreeContext &Ctx, Expr *Cond,
                                           llvm::StringRef Message,
                                           SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_GNU, 0, false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, Cond, Message, I);
}

EnableIfAttr *EnableIfAttr::Create(TreeContext &Ctx, Expr *Cond,
                                   llvm::StringRef Message, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_GNU, 0, false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, Cond, Message, I);
}

EnableIfAttr::EnableIfAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo, Expr *Cond,
                           llvm::StringRef Message)
    : InheritableAttr(Ctx, CommonInfo, attr::EnableIf, false, false),
      cond(Cond), messageLength(Message.size()),
      message(new (Ctx, 1) char[messageLength]) {
  if (!Message.empty())
    std::memcpy(message, Message.data(), messageLength);
}

EnableIfAttr *EnableIfAttr::clone(TreeContext &C) const {
  auto *A = new (C) EnableIfAttr(C, *this, cond, getMessage());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void EnableIfAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((enable_if";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "";
    getCond()->printPretty(OS, nullptr, Policy);
    OS << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  }
}

const char *EnableIfAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "enable_if";
  }
}

// EnforceTCBAttr implementation

EnforceTCBAttr *
EnforceTCBAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef TCBName,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) EnforceTCBAttr(Ctx, CommonInfo, TCBName);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

EnforceTCBAttr *EnforceTCBAttr::Create(TreeContext &Ctx,
                                       llvm::StringRef TCBName,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) EnforceTCBAttr(Ctx, CommonInfo, TCBName);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

EnforceTCBAttr *EnforceTCBAttr::CreateImplicit(TreeContext &Ctx,
                                               llvm::StringRef TCBName,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_enforce_tcb:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_enforce_tcb, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_enforce_tcb:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_enforce_tcb,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_enforce_tcb:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_enforce_tcb,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_enforce_tcb:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_enforce_tcb,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_enforce_tcb:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_enforce_tcb,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, TCBName, I);
}

EnforceTCBAttr *EnforceTCBAttr::Create(TreeContext &Ctx,
                                       llvm::StringRef TCBName,
                                       SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_enforce_tcb:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_enforce_tcb, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_enforce_tcb:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_enforce_tcb,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_enforce_tcb:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_enforce_tcb,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_enforce_tcb:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_enforce_tcb,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_enforce_tcb:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_enforce_tcb,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, TCBName, I);
}

EnforceTCBAttr::EnforceTCBAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo,
                               llvm::StringRef TCBName)
    : InheritableAttr(Ctx, CommonInfo, attr::EnforceTCB, false, true),
      tCBNameLength(TCBName.size()), tCBName(new (Ctx, 1) char[tCBNameLength]) {
  if (!TCBName.empty())
    std::memcpy(tCBName, TCBName.data(), tCBNameLength);
}

EnforceTCBAttr *EnforceTCBAttr::clone(TreeContext &C) const {
  auto *A = new (C) EnforceTCBAttr(C, *this, getTCBName());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void EnforceTCBAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((enforce_tcb";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getTCBName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::enforce_tcb";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getTCBName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::enforce_tcb";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getTCBName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::enforce_tcb";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getTCBName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::enforce_tcb";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getTCBName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *EnforceTCBAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "enforce_tcb";
  case 1:
    return "enforce_tcb";
  case 2:
    return "enforce_tcb";
  case 3:
    return "enforce_tcb";
  case 4:
    return "enforce_tcb";
  }
}

// EnforceTCBLeafAttr implementation

EnforceTCBLeafAttr *
EnforceTCBLeafAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef TCBName,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) EnforceTCBLeafAttr(Ctx, CommonInfo, TCBName);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

EnforceTCBLeafAttr *
EnforceTCBLeafAttr::Create(TreeContext &Ctx, llvm::StringRef TCBName,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) EnforceTCBLeafAttr(Ctx, CommonInfo, TCBName);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

EnforceTCBLeafAttr *EnforceTCBLeafAttr::CreateImplicit(TreeContext &Ctx,
                                                       llvm::StringRef TCBName,
                                                       SourceRange Range,
                                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_enforce_tcb_leaf:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_enforce_tcb_leaf,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_enforce_tcb_leaf:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_enforce_tcb_leaf,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_enforce_tcb_leaf:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_enforce_tcb_leaf,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_enforce_tcb_leaf:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_enforce_tcb_leaf,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_enforce_tcb_leaf:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_enforce_tcb_leaf,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, TCBName, I);
}

EnforceTCBLeafAttr *EnforceTCBLeafAttr::Create(TreeContext &Ctx,
                                               llvm::StringRef TCBName,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_enforce_tcb_leaf:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_enforce_tcb_leaf,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_enforce_tcb_leaf:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_enforce_tcb_leaf,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_enforce_tcb_leaf:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_enforce_tcb_leaf,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_enforce_tcb_leaf:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_enforce_tcb_leaf,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_enforce_tcb_leaf:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_enforce_tcb_leaf,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, TCBName, I);
}

EnforceTCBLeafAttr::EnforceTCBLeafAttr(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo,
                                       llvm::StringRef TCBName)
    : InheritableAttr(Ctx, CommonInfo, attr::EnforceTCBLeaf, false, true),
      tCBNameLength(TCBName.size()), tCBName(new (Ctx, 1) char[tCBNameLength]) {
  if (!TCBName.empty())
    std::memcpy(tCBName, TCBName.data(), tCBNameLength);
}

EnforceTCBLeafAttr *EnforceTCBLeafAttr::clone(TreeContext &C) const {
  auto *A = new (C) EnforceTCBLeafAttr(C, *this, getTCBName());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void EnforceTCBLeafAttr::printPretty(raw_ostream &OS,
                                     const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((enforce_tcb_leaf";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getTCBName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::enforce_tcb_leaf";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getTCBName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::enforce_tcb_leaf";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getTCBName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::enforce_tcb_leaf";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getTCBName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::enforce_tcb_leaf";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getTCBName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *EnforceTCBLeafAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "enforce_tcb_leaf";
  case 1:
    return "enforce_tcb_leaf";
  case 2:
    return "enforce_tcb_leaf";
  case 3:
    return "enforce_tcb_leaf";
  case 4:
    return "enforce_tcb_leaf";
  }
}

// EnumExtensibilityAttr implementation

EnumExtensibilityAttr *
EnumExtensibilityAttr::CreateImplicit(TreeContext &Ctx,
                                      EnumExtensibilityAttr::Kind Extensibility,
                                      const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) EnumExtensibilityAttr(Ctx, CommonInfo, Extensibility);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

EnumExtensibilityAttr *
EnumExtensibilityAttr::Create(TreeContext &Ctx,
                              EnumExtensibilityAttr::Kind Extensibility,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) EnumExtensibilityAttr(Ctx, CommonInfo, Extensibility);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

EnumExtensibilityAttr *
EnumExtensibilityAttr::CreateImplicit(TreeContext &Ctx,
                                      EnumExtensibilityAttr::Kind Extensibility,
                                      SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_enum_extensibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_enum_extensibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_enum_extensibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_enum_extensibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_enum_extensibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_enum_extensibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_enum_extensibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_enum_extensibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_enum_extensibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_enum_extensibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Extensibility, I);
}

EnumExtensibilityAttr *
EnumExtensibilityAttr::Create(TreeContext &Ctx,
                              EnumExtensibilityAttr::Kind Extensibility,
                              SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_enum_extensibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_enum_extensibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_enum_extensibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_enum_extensibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_enum_extensibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_enum_extensibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_enum_extensibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_enum_extensibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_enum_extensibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_enum_extensibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Extensibility, I);
}

EnumExtensibilityAttr::EnumExtensibilityAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    EnumExtensibilityAttr::Kind Extensibility)
    : InheritableAttr(Ctx, CommonInfo, attr::EnumExtensibility, false, false),
      extensibility(Extensibility) {}

bool EnumExtensibilityAttr::ConvertStrToKind(StringRef Val,
                                             EnumExtensibilityAttr::Kind &Out) {
  std::optional<EnumExtensibilityAttr::Kind> R =
      llvm::StringSwitch<std::optional<EnumExtensibilityAttr::Kind>>(Val)
          .Case("closed", EnumExtensibilityAttr::Kind::Closed)
          .Case("open", EnumExtensibilityAttr::Kind::Open)
          .Default(std::optional<EnumExtensibilityAttr::Kind>());
  if (R) {
    Out = *R;
    return true;
  }
  return false;
}

const char *
EnumExtensibilityAttr::ConvertKindToStr(EnumExtensibilityAttr::Kind Val) {
  switch (Val) {
  case EnumExtensibilityAttr::Kind::Closed:
    return "closed";
  case EnumExtensibilityAttr::Kind::Open:
    return "open";
  }
  llvm_unreachable("No enumerator with that value");
}
EnumExtensibilityAttr *EnumExtensibilityAttr::clone(TreeContext &C) const {
  auto *A = new (C) EnumExtensibilityAttr(C, *this, extensibility);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void EnumExtensibilityAttr::printPretty(raw_ostream &OS,
                                        const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((enum_extensibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << EnumExtensibilityAttr::ConvertKindToStr(getExtensibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::enum_extensibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << EnumExtensibilityAttr::ConvertKindToStr(getExtensibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::enum_extensibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << EnumExtensibilityAttr::ConvertKindToStr(getExtensibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::enum_extensibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << EnumExtensibilityAttr::ConvertKindToStr(getExtensibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::enum_extensibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << EnumExtensibilityAttr::ConvertKindToStr(getExtensibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *EnumExtensibilityAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "enum_extensibility";
  case 1:
    return "enum_extensibility";
  case 2:
    return "enum_extensibility";
  case 3:
    return "enum_extensibility";
  case 4:
    return "enum_extensibility";
  }
}

// ErrorAttr implementation

ErrorAttr *ErrorAttr::CreateImplicit(TreeContext &Ctx,
                                     llvm::StringRef UserDiagnostic,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ErrorAttr(Ctx, CommonInfo, UserDiagnostic);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ErrorAttr *ErrorAttr::Create(TreeContext &Ctx, llvm::StringRef UserDiagnostic,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ErrorAttr(Ctx, CommonInfo, UserDiagnostic);
  return A;
}

ErrorAttr *ErrorAttr::CreateImplicit(TreeContext &Ctx,
                                     llvm::StringRef UserDiagnostic,
                                     SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_error:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_error,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_error:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_error, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_error:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_error, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_warning:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_warning,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_warning:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_warning, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_warning:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_warning, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, UserDiagnostic, I);
}

ErrorAttr *ErrorAttr::Create(TreeContext &Ctx, llvm::StringRef UserDiagnostic,
                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_error:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_error,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_error:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_error, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_error:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_error, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_warning:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_warning,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_warning:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_warning, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_warning:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_warning, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, UserDiagnostic, I);
}

ErrorAttr::ErrorAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                     llvm::StringRef UserDiagnostic)
    : InheritableAttr(Ctx, CommonInfo, attr::Error, false, false),
      userDiagnosticLength(UserDiagnostic.size()),
      userDiagnostic(new (Ctx, 1) char[userDiagnosticLength]) {
  if (!UserDiagnostic.empty())
    std::memcpy(userDiagnostic, UserDiagnostic.data(), userDiagnosticLength);
}

ErrorAttr::Spelling ErrorAttr::getSemanticSpelling() const {
  switch (getAttributeSpellingListIndex()) {
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

ErrorAttr *ErrorAttr::clone(TreeContext &C) const {
  auto *A = new (C) ErrorAttr(C, *this, getUserDiagnostic());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ErrorAttr::printPretty(raw_ostream &OS,
                            const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((error";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getUserDiagnostic() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::error";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getUserDiagnostic() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::error";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getUserDiagnostic() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __attribute__((warning";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getUserDiagnostic() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 4: {
    OS << " [[gnu::warning";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getUserDiagnostic() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " [[gnu::warning";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getUserDiagnostic() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *ErrorAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "error";
  case 1:
    return "error";
  case 2:
    return "error";
  case 3:
    return "warning";
  case 4:
    return "warning";
  case 5:
    return "warning";
  }
}

// FallThroughAttr implementation

FallThroughAttr *
FallThroughAttr::CreateImplicit(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FallThroughAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FallThroughAttr *
FallThroughAttr::Create(TreeContext &Ctx,
                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FallThroughAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FallThroughAttr *FallThroughAttr::CreateImplicit(TreeContext &Ctx,
                                                 SourceRange Range,
                                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_fallthrough:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_fallthrough, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_fallthrough:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_fallthrough, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_fallthrough:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_fallthrough,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_fallthrough:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_fallthrough, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_fallthrough:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_fallthrough,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_fallthrough:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_fallthrough, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

FallThroughAttr *FallThroughAttr::Create(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_fallthrough:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_fallthrough, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_fallthrough:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_fallthrough, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_fallthrough:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_fallthrough,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_fallthrough:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_fallthrough, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_fallthrough:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_fallthrough,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_fallthrough:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_fallthrough, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

FallThroughAttr::FallThroughAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo)
    : StmtAttr(Ctx, CommonInfo, attr::FallThrough, false) {}

FallThroughAttr *FallThroughAttr::clone(TreeContext &C) const {
  auto *A = new (C) FallThroughAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void FallThroughAttr::printPretty(raw_ostream &OS,
                                  const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " [[fallthrough";
    OS << "]]";
    break;
  }
  case 1: {
    OS << " [[fallthrough";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::fallthrough";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __attribute__((fallthrough";
    OS << "))";
    break;
  }
  case 4: {
    OS << " [[gnu::fallthrough";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " [[gnu::fallthrough";
    OS << "]]";
    break;
  }
  }
}

const char *FallThroughAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "fallthrough";
  case 1:
    return "fallthrough";
  case 2:
    return "fallthrough";
  case 3:
    return "fallthrough";
  case 4:
    return "fallthrough";
  case 5:
    return "fallthrough";
  }
}

// FastCallAttr implementation

FastCallAttr *
FastCallAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FastCallAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FastCallAttr *FastCallAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FastCallAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FastCallAttr *FastCallAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_fastcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_fastcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_fastcall:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_fastcall,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_fastcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_fastcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Keyword_fastcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_fastcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

FastCallAttr *FastCallAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_fastcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_fastcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_fastcall:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_fastcall,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_fastcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_fastcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Keyword_fastcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_fastcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

FastCallAttr::FastCallAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::FastCall, false, false) {}

FastCallAttr *FastCallAttr::clone(TreeContext &C) const {
  auto *A = new (C) FastCallAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void FastCallAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((fastcall";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::fastcall";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::fastcall";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __fastcall";
    OS << "";
    break;
  }
  case 4: {
    OS << " _fastcall";
    OS << "";
    break;
  }
  }
}

const char *FastCallAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "fastcall";
  case 1:
    return "fastcall";
  case 2:
    return "fastcall";
  case 3:
    return "__fastcall";
  case 4:
    return "_fastcall";
  }
}

// FlagEnumAttr implementation

FlagEnumAttr *
FlagEnumAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FlagEnumAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FlagEnumAttr *FlagEnumAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FlagEnumAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FlagEnumAttr *FlagEnumAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_flag_enum:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_flag_enum, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_flag_enum:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_flag_enum,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_flag_enum:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_flag_enum,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_flag_enum:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_flag_enum, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_flag_enum:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_flag_enum,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

FlagEnumAttr *FlagEnumAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_flag_enum:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_flag_enum, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_flag_enum:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_flag_enum,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_flag_enum:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_flag_enum,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_flag_enum:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_flag_enum, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_flag_enum:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_flag_enum,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

FlagEnumAttr::FlagEnumAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::FlagEnum, false, false) {}

FlagEnumAttr *FlagEnumAttr::clone(TreeContext &C) const {
  auto *A = new (C) FlagEnumAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void FlagEnumAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((flag_enum";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::flag_enum";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::flag_enum";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::flag_enum";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::flag_enum";
    OS << "]]";
    break;
  }
  }
}

const char *FlagEnumAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "flag_enum";
  case 1:
    return "flag_enum";
  case 2:
    return "flag_enum";
  case 3:
    return "flag_enum";
  case 4:
    return "flag_enum";
  }
}

// FlattenAttr implementation

FlattenAttr *
FlattenAttr::CreateImplicit(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FlattenAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FlattenAttr *FlattenAttr::Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FlattenAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FlattenAttr *FlattenAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_flatten:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_flatten,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_flatten:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_flatten, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_flatten:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_flatten, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

FlattenAttr *FlattenAttr::Create(TreeContext &Ctx, SourceRange Range,
                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_flatten:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_flatten,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_flatten:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_flatten, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_flatten:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_flatten, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

FlattenAttr::FlattenAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Flatten, false, false) {}

FlattenAttr *FlattenAttr::clone(TreeContext &C) const {
  auto *A = new (C) FlattenAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void FlattenAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((flatten";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::flatten";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::flatten";
    OS << "]]";
    break;
  }
  }
}

const char *FlattenAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "flatten";
  case 1:
    return "flatten";
  case 2:
    return "flatten";
  }
}

// FormatAttr implementation

FormatAttr *FormatAttr::CreateImplicit(TreeContext &Ctx, IdentifierInfo *Type,
                                       int FormatIdx, int FirstArg,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FormatAttr(Ctx, CommonInfo, Type, FormatIdx, FirstArg);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FormatAttr *FormatAttr::Create(TreeContext &Ctx, IdentifierInfo *Type,
                               int FormatIdx, int FirstArg,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FormatAttr(Ctx, CommonInfo, Type, FormatIdx, FirstArg);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FormatAttr *FormatAttr::CreateImplicit(TreeContext &Ctx, IdentifierInfo *Type,
                                       int FormatIdx, int FirstArg,
                                       SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_format:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_format,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_format:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_format, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_format:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_format, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Type, FormatIdx, FirstArg, I);
}

FormatAttr *FormatAttr::Create(TreeContext &Ctx, IdentifierInfo *Type,
                               int FormatIdx, int FirstArg, SourceRange Range,
                               Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_format:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_format,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_format:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_format, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_format:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_format, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Type, FormatIdx, FirstArg, I);
}

FormatAttr::FormatAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                       IdentifierInfo *Type, int FormatIdx, int FirstArg)
    : InheritableAttr(Ctx, CommonInfo, attr::Format, false, false), type(Type),
      formatIdx(FormatIdx), firstArg(FirstArg) {}

FormatAttr *FormatAttr::clone(TreeContext &C) const {
  auto *A = new (C) FormatAttr(C, *this, type, formatIdx, firstArg);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void FormatAttr::printPretty(raw_ostream &OS,
                             const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((format";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getType() ? getType()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFormatIdx() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFirstArg() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::format";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getType() ? getType()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFormatIdx() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFirstArg() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::format";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getType() ? getType()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFormatIdx() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFirstArg() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *FormatAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "format";
  case 1:
    return "format";
  case 2:
    return "format";
  }
}

// FormatArgAttr implementation

FormatArgAttr *
FormatArgAttr::CreateImplicit(TreeContext &Ctx, ParamIdx FormatIdx,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FormatArgAttr(Ctx, CommonInfo, FormatIdx);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FormatArgAttr *FormatArgAttr::Create(TreeContext &Ctx, ParamIdx FormatIdx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FormatArgAttr(Ctx, CommonInfo, FormatIdx);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FormatArgAttr *FormatArgAttr::CreateImplicit(TreeContext &Ctx,
                                             ParamIdx FormatIdx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_format_arg:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_format_arg, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_format_arg:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_format_arg,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_format_arg:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_format_arg, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, FormatIdx, I);
}

FormatArgAttr *FormatArgAttr::Create(TreeContext &Ctx, ParamIdx FormatIdx,
                                     SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_format_arg:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_format_arg, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_format_arg:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_format_arg,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_format_arg:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_format_arg, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, FormatIdx, I);
}

FormatArgAttr::FormatArgAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo,
                             ParamIdx FormatIdx)
    : InheritableAttr(Ctx, CommonInfo, attr::FormatArg, false, false),
      formatIdx(FormatIdx) {}

FormatArgAttr *FormatArgAttr::clone(TreeContext &C) const {
  auto *A = new (C) FormatArgAttr(C, *this, formatIdx);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void FormatArgAttr::printPretty(raw_ostream &OS,
                                const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((format_arg";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFormatIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::format_arg";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFormatIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::format_arg";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getFormatIdx().getSourceIndex() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *FormatArgAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "format_arg";
  case 1:
    return "format_arg";
  case 2:
    return "format_arg";
  }
}

// FunctionReturnThunksAttr implementation

FunctionReturnThunksAttr *FunctionReturnThunksAttr::CreateImplicit(
    TreeContext &Ctx, FunctionReturnThunksAttr::Kind ThunkType,
    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FunctionReturnThunksAttr(Ctx, CommonInfo, ThunkType);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FunctionReturnThunksAttr *
FunctionReturnThunksAttr::Create(TreeContext &Ctx,
                                 FunctionReturnThunksAttr::Kind ThunkType,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) FunctionReturnThunksAttr(Ctx, CommonInfo, ThunkType);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

FunctionReturnThunksAttr *FunctionReturnThunksAttr::CreateImplicit(
    TreeContext &Ctx, FunctionReturnThunksAttr::Kind ThunkType,
    SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_function_return:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_function_return, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_function_return:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_function_return,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_function_return:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_function_return,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, ThunkType, I);
}

FunctionReturnThunksAttr *
FunctionReturnThunksAttr::Create(TreeContext &Ctx,
                                 FunctionReturnThunksAttr::Kind ThunkType,
                                 SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_function_return:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_function_return, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_function_return:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_function_return,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_function_return:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_function_return,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, ThunkType, I);
}

FunctionReturnThunksAttr::FunctionReturnThunksAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    FunctionReturnThunksAttr::Kind ThunkType)
    : InheritableAttr(Ctx, CommonInfo, attr::FunctionReturnThunks, false,
                      false),
      thunkType(ThunkType) {}

bool FunctionReturnThunksAttr::ConvertStrToKind(
    StringRef Val, FunctionReturnThunksAttr::Kind &Out) {
  std::optional<FunctionReturnThunksAttr::Kind> R =
      llvm::StringSwitch<std::optional<FunctionReturnThunksAttr::Kind>>(Val)
          .Case("keep", FunctionReturnThunksAttr::Kind::Keep)
          .Case("thunk-extern", FunctionReturnThunksAttr::Kind::Extern)
          .Default(std::optional<FunctionReturnThunksAttr::Kind>());
  if (R) {
    Out = *R;
    return true;
  }
  return false;
}

const char *
FunctionReturnThunksAttr::ConvertKindToStr(FunctionReturnThunksAttr::Kind Val) {
  switch (Val) {
  case FunctionReturnThunksAttr::Kind::Keep:
    return "keep";
  case FunctionReturnThunksAttr::Kind::Extern:
    return "thunk-extern";
  }
  llvm_unreachable("No enumerator with that value");
}
FunctionReturnThunksAttr *
FunctionReturnThunksAttr::clone(TreeContext &C) const {
  auto *A = new (C) FunctionReturnThunksAttr(C, *this, thunkType);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void FunctionReturnThunksAttr::printPretty(raw_ostream &OS,
                                           const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((function_return";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << FunctionReturnThunksAttr::ConvertKindToStr(getThunkType())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::function_return";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << FunctionReturnThunksAttr::ConvertKindToStr(getThunkType())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::function_return";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << FunctionReturnThunksAttr::ConvertKindToStr(getThunkType())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *FunctionReturnThunksAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "function_return";
  case 1:
    return "function_return";
  case 2:
    return "function_return";
  }
}

// GNUInlineAttr implementation

GNUInlineAttr *
GNUInlineAttr::CreateImplicit(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) GNUInlineAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

GNUInlineAttr *GNUInlineAttr::Create(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) GNUInlineAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

GNUInlineAttr *GNUInlineAttr::CreateImplicit(TreeContext &Ctx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_gnu_inline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_gnu_inline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_gnu_inline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_gnu_inline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_gnu_inline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_gnu_inline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

GNUInlineAttr *GNUInlineAttr::Create(TreeContext &Ctx, SourceRange Range,
                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_gnu_inline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_gnu_inline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_gnu_inline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_gnu_inline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_gnu_inline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_gnu_inline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

GNUInlineAttr::GNUInlineAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::GNUInline, false, false) {}

GNUInlineAttr *GNUInlineAttr::clone(TreeContext &C) const {
  auto *A = new (C) GNUInlineAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void GNUInlineAttr::printPretty(raw_ostream &OS,
                                const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((gnu_inline";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::gnu_inline";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::gnu_inline";
    OS << "]]";
    break;
  }
  }
}

const char *GNUInlineAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "gnu_inline";
  case 1:
    return "gnu_inline";
  case 2:
    return "gnu_inline";
  }
}

// HotAttr implementation

HotAttr *HotAttr::CreateImplicit(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) HotAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

HotAttr *HotAttr::Create(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) HotAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

HotAttr *HotAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_hot:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_hot,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_hot:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_hot, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_hot:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23, C23_gnu_hot,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

HotAttr *HotAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_hot:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_hot,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_hot:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_hot, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_hot:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23, C23_gnu_hot,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

HotAttr::HotAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Hot, false, false) {}

HotAttr *HotAttr::clone(TreeContext &C) const {
  auto *A = new (C) HotAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void HotAttr::printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((hot";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::hot";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::hot";
    OS << "]]";
    break;
  }
  }
}

const char *HotAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "hot";
  case 1:
    return "hot";
  case 2:
    return "hot";
  }
}

// IFuncAttr implementation

IFuncAttr *IFuncAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Resolver,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) IFuncAttr(Ctx, CommonInfo, Resolver);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

IFuncAttr *IFuncAttr::Create(TreeContext &Ctx, llvm::StringRef Resolver,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) IFuncAttr(Ctx, CommonInfo, Resolver);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

IFuncAttr *IFuncAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Resolver,
                                     SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_ifunc:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_ifunc,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_ifunc:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_ifunc, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_ifunc:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_ifunc, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Resolver, I);
}

IFuncAttr *IFuncAttr::Create(TreeContext &Ctx, llvm::StringRef Resolver,
                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_ifunc:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_ifunc,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_ifunc:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_ifunc, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_ifunc:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_ifunc, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Resolver, I);
}

IFuncAttr::IFuncAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                     llvm::StringRef Resolver)
    : Attr(Ctx, CommonInfo, attr::IFunc, false),
      resolverLength(Resolver.size()),
      resolver(new (Ctx, 1) char[resolverLength]) {
  if (!Resolver.empty())
    std::memcpy(resolver, Resolver.data(), resolverLength);
}

IFuncAttr *IFuncAttr::clone(TreeContext &C) const {
  auto *A = new (C) IFuncAttr(C, *this, getResolver());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void IFuncAttr::printPretty(raw_ostream &OS,
                            const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((ifunc";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getResolver() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::ifunc";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getResolver() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::ifunc";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getResolver() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *IFuncAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "ifunc";
  case 1:
    return "ifunc";
  case 2:
    return "ifunc";
  }
}

// InitSegAttr implementation

InitSegAttr *
InitSegAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Section,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) InitSegAttr(Ctx, CommonInfo, Section);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

InitSegAttr *InitSegAttr::Create(TreeContext &Ctx, llvm::StringRef Section,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) InitSegAttr(Ctx, CommonInfo, Section);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

InitSegAttr *InitSegAttr::CreateImplicit(TreeContext &Ctx,
                                         llvm::StringRef Section,
                                         SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Pragma, 0, false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, Section, I);
}

InitSegAttr *InitSegAttr::Create(TreeContext &Ctx, llvm::StringRef Section,
                                 SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Pragma, 0, false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, Section, I);
}

InitSegAttr::InitSegAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo,
                         llvm::StringRef Section)
    : Attr(Ctx, CommonInfo, attr::InitSeg, false),
      sectionLength(Section.size()), section(new (Ctx, 1) char[sectionLength]) {
  if (!Section.empty())
    std::memcpy(section, Section.data(), sectionLength);
}

InitSegAttr *InitSegAttr::clone(TreeContext &C) const {
  auto *A = new (C) InitSegAttr(C, *this, getSection());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void InitSegAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << "#pragma init_seg";
    printPrettyPragma(OS, Policy);
    OS << "\n";
    break;
  }
  }
}

const char *InitSegAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "init_seg";
  }
}

// InternalLinkageAttr implementation

InternalLinkageAttr *
InternalLinkageAttr::CreateImplicit(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) InternalLinkageAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

InternalLinkageAttr *
InternalLinkageAttr::Create(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) InternalLinkageAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

InternalLinkageAttr *InternalLinkageAttr::CreateImplicit(TreeContext &Ctx,
                                                         SourceRange Range,
                                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_internal_linkage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_internal_linkage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_internal_linkage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_internal_linkage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_internal_linkage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_internal_linkage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_internal_linkage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_internal_linkage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_internal_linkage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_internal_linkage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

InternalLinkageAttr *
InternalLinkageAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_internal_linkage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_internal_linkage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_internal_linkage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_internal_linkage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_internal_linkage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_internal_linkage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_internal_linkage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_internal_linkage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_internal_linkage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_internal_linkage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

InternalLinkageAttr::InternalLinkageAttr(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::InternalLinkage, false, false) {}

InternalLinkageAttr *InternalLinkageAttr::clone(TreeContext &C) const {
  auto *A = new (C) InternalLinkageAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void InternalLinkageAttr::printPretty(raw_ostream &OS,
                                      const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((internal_linkage";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::internal_linkage";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::internal_linkage";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::internal_linkage";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::internal_linkage";
    OS << "]]";
    break;
  }
  }
}

const char *InternalLinkageAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "internal_linkage";
  case 1:
    return "internal_linkage";
  case 2:
    return "internal_linkage";
  case 3:
    return "internal_linkage";
  case 4:
    return "internal_linkage";
  }
}

// LTOVisibilityPublicAttr implementation

LTOVisibilityPublicAttr *
LTOVisibilityPublicAttr::CreateImplicit(TreeContext &Ctx,
                                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) LTOVisibilityPublicAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

LTOVisibilityPublicAttr *
LTOVisibilityPublicAttr::Create(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) LTOVisibilityPublicAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

LTOVisibilityPublicAttr *
LTOVisibilityPublicAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                        Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_lto_visibility_public:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_lto_visibility_public,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_lto_visibility_public:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_lto_visibility_public,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_lto_visibility_public:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_lto_visibility_public,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_lto_visibility_public:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_lto_visibility_public,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_lto_visibility_public:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_lto_visibility_public,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

LTOVisibilityPublicAttr *LTOVisibilityPublicAttr::Create(TreeContext &Ctx,
                                                         SourceRange Range,
                                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_lto_visibility_public:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_lto_visibility_public,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_lto_visibility_public:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_lto_visibility_public,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_lto_visibility_public:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_lto_visibility_public,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_lto_visibility_public:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_lto_visibility_public,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_lto_visibility_public:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_lto_visibility_public,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

LTOVisibilityPublicAttr::LTOVisibilityPublicAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::LTOVisibilityPublic, false,
                      false) {}

LTOVisibilityPublicAttr *LTOVisibilityPublicAttr::clone(TreeContext &C) const {
  auto *A = new (C) LTOVisibilityPublicAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void LTOVisibilityPublicAttr::printPretty(raw_ostream &OS,
                                          const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((lto_visibility_public";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::lto_visibility_public";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::lto_visibility_public";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::lto_visibility_public";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::lto_visibility_public";
    OS << "]]";
    break;
  }
  }
}

const char *LTOVisibilityPublicAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "lto_visibility_public";
  case 1:
    return "lto_visibility_public";
  case 2:
    return "lto_visibility_public";
  case 3:
    return "lto_visibility_public";
  case 4:
    return "lto_visibility_public";
  }
}

// LeafAttr implementation

LeafAttr *LeafAttr::CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) LeafAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

LeafAttr *LeafAttr::Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) LeafAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

LeafAttr *LeafAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_leaf:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_leaf,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_leaf:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_leaf, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_leaf:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_leaf, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

LeafAttr *LeafAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_leaf:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_leaf,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_leaf:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_leaf, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_leaf:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_leaf, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

LeafAttr::LeafAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Leaf, false, false) {}

LeafAttr *LeafAttr::clone(TreeContext &C) const {
  auto *A = new (C) LeafAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void LeafAttr::printPretty(raw_ostream &OS,
                           const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((leaf";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::leaf";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::leaf";
    OS << "]]";
    break;
  }
  }
}

const char *LeafAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "leaf";
  case 1:
    return "leaf";
  case 2:
    return "leaf";
  }
}

// LikelyAttr implementation

LikelyAttr *LikelyAttr::CreateImplicit(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) LikelyAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

LikelyAttr *LikelyAttr::Create(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) LikelyAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

LikelyAttr *LikelyAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_likely:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_likely, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_likely:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_likely, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

LikelyAttr *LikelyAttr::Create(TreeContext &Ctx, SourceRange Range,
                               Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_likely:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_likely, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_likely:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_likely, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

LikelyAttr::LikelyAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : StmtAttr(Ctx, CommonInfo, attr::Likely, false) {}

LikelyAttr *LikelyAttr::clone(TreeContext &C) const {
  auto *A = new (C) LikelyAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void LikelyAttr::printPretty(raw_ostream &OS,
                             const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " [[likely";
    OS << "]]";
    break;
  }
  case 1: {
    OS << " [[neverc::likely";
    OS << "]]";
    break;
  }
  }
}

const char *LikelyAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "likely";
  case 1:
    return "likely";
  }
}

// LoaderUninitializedAttr implementation

LoaderUninitializedAttr *
LoaderUninitializedAttr::CreateImplicit(TreeContext &Ctx,
                                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) LoaderUninitializedAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

LoaderUninitializedAttr *
LoaderUninitializedAttr::Create(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) LoaderUninitializedAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

LoaderUninitializedAttr *
LoaderUninitializedAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                        Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_loader_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_loader_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_loader_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_loader_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_loader_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_loader_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_loader_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_loader_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_loader_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_loader_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

LoaderUninitializedAttr *LoaderUninitializedAttr::Create(TreeContext &Ctx,
                                                         SourceRange Range,
                                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_loader_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_loader_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_loader_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_loader_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_loader_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_loader_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_loader_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_loader_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_loader_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_loader_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

LoaderUninitializedAttr::LoaderUninitializedAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : Attr(Ctx, CommonInfo, attr::LoaderUninitialized, false) {}

LoaderUninitializedAttr *LoaderUninitializedAttr::clone(TreeContext &C) const {
  auto *A = new (C) LoaderUninitializedAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void LoaderUninitializedAttr::printPretty(raw_ostream &OS,
                                          const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((loader_uninitialized";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::loader_uninitialized";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::loader_uninitialized";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::loader_uninitialized";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::loader_uninitialized";
    OS << "]]";
    break;
  }
  }
}

const char *LoaderUninitializedAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "loader_uninitialized";
  case 1:
    return "loader_uninitialized";
  case 2:
    return "loader_uninitialized";
  case 3:
    return "loader_uninitialized";
  case 4:
    return "loader_uninitialized";
  }
}

// MSABIAttr implementation

MSABIAttr *MSABIAttr::CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MSABIAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MSABIAttr *MSABIAttr::Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MSABIAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MSABIAttr *MSABIAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_ms_abi:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_ms_abi,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_ms_abi:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_ms_abi, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_ms_abi:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_ms_abi, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

MSABIAttr *MSABIAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_ms_abi:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_ms_abi,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_ms_abi:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_ms_abi, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_ms_abi:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_ms_abi, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

MSABIAttr::MSABIAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::MSABI, false, false) {}

MSABIAttr *MSABIAttr::clone(TreeContext &C) const {
  auto *A = new (C) MSABIAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void MSABIAttr::printPretty(raw_ostream &OS,
                            const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((ms_abi";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::ms_abi";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::ms_abi";
    OS << "]]";
    break;
  }
  }
}

const char *MSABIAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "ms_abi";
  case 1:
    return "ms_abi";
  case 2:
    return "ms_abi";
  }
}

// MSAllocatorAttr implementation

MSAllocatorAttr *
MSAllocatorAttr::CreateImplicit(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MSAllocatorAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MSAllocatorAttr *
MSAllocatorAttr::Create(TreeContext &Ctx,
                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MSAllocatorAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MSAllocatorAttr *MSAllocatorAttr::CreateImplicit(TreeContext &Ctx,
                                                 SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Declspec, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

MSAllocatorAttr *MSAllocatorAttr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Declspec, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

MSAllocatorAttr::MSAllocatorAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::MSAllocator, false, false) {}

MSAllocatorAttr *MSAllocatorAttr::clone(TreeContext &C) const {
  auto *A = new (C) MSAllocatorAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void MSAllocatorAttr::printPretty(raw_ostream &OS,
                                  const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __declspec(allocator";
    OS << ")";
    break;
  }
  }
}

const char *MSAllocatorAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "allocator";
  }
}

// MSStructAttr implementation

MSStructAttr *
MSStructAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MSStructAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MSStructAttr *MSStructAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MSStructAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MSStructAttr *MSStructAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_ms_struct:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_ms_struct, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_ms_struct:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_ms_struct,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_ms_struct:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_ms_struct, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

MSStructAttr *MSStructAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_ms_struct:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_ms_struct, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_ms_struct:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_ms_struct,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_ms_struct:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_ms_struct, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

MSStructAttr::MSStructAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::MSStruct, false, false) {}

MSStructAttr *MSStructAttr::clone(TreeContext &C) const {
  auto *A = new (C) MSStructAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void MSStructAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((ms_struct";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::ms_struct";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::ms_struct";
    OS << "]]";
    break;
  }
  }
}

const char *MSStructAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "ms_struct";
  case 1:
    return "ms_struct";
  case 2:
    return "ms_struct";
  }
}

// MaxFieldAlignmentAttr implementation

MaxFieldAlignmentAttr *
MaxFieldAlignmentAttr::CreateImplicit(TreeContext &Ctx, unsigned Alignment,
                                      const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MaxFieldAlignmentAttr(Ctx, CommonInfo, Alignment);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MaxFieldAlignmentAttr *
MaxFieldAlignmentAttr::Create(TreeContext &Ctx, unsigned Alignment,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MaxFieldAlignmentAttr(Ctx, CommonInfo, Alignment);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MaxFieldAlignmentAttr *
MaxFieldAlignmentAttr::CreateImplicit(TreeContext &Ctx, unsigned Alignment,
                                      SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return CreateImplicit(Ctx, Alignment, I);
}

MaxFieldAlignmentAttr *MaxFieldAlignmentAttr::Create(TreeContext &Ctx,
                                                     unsigned Alignment,
                                                     SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return Create(Ctx, Alignment, I);
}

MaxFieldAlignmentAttr::MaxFieldAlignmentAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo, unsigned Alignment)
    : InheritableAttr(Ctx, CommonInfo, attr::MaxFieldAlignment, false, false),
      alignment(Alignment) {}

MaxFieldAlignmentAttr *MaxFieldAlignmentAttr::clone(TreeContext &C) const {
  auto *A = new (C) MaxFieldAlignmentAttr(C, *this, alignment);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void MaxFieldAlignmentAttr::printPretty(raw_ostream &OS,
                                        const PrintingPolicy &Policy) const {}

const char *MaxFieldAlignmentAttr::getSpelling() const {
  return "(No spelling)";
}

// MayAliasAttr implementation

MayAliasAttr *
MayAliasAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MayAliasAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MayAliasAttr *MayAliasAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MayAliasAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MayAliasAttr *MayAliasAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_may_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_may_alias, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_may_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_may_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_may_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_may_alias, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

MayAliasAttr *MayAliasAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_may_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_may_alias, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_may_alias:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_may_alias,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_may_alias:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_may_alias, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

MayAliasAttr::MayAliasAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::MayAlias, false, false) {}

MayAliasAttr *MayAliasAttr::clone(TreeContext &C) const {
  auto *A = new (C) MayAliasAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void MayAliasAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((may_alias";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::may_alias";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::may_alias";
    OS << "]]";
    break;
  }
  }
}

const char *MayAliasAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "may_alias";
  case 1:
    return "may_alias";
  case 2:
    return "may_alias";
  }
}

// MaybeUndefAttr implementation

MaybeUndefAttr *
MaybeUndefAttr::CreateImplicit(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MaybeUndefAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MaybeUndefAttr *MaybeUndefAttr::Create(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MaybeUndefAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MaybeUndefAttr *MaybeUndefAttr::CreateImplicit(TreeContext &Ctx,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_maybe_undef:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_maybe_undef, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_maybe_undef:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_maybe_undef,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_maybe_undef:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_maybe_undef,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_maybe_undef:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_maybe_undef,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_maybe_undef:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_maybe_undef,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

MaybeUndefAttr *MaybeUndefAttr::Create(TreeContext &Ctx, SourceRange Range,
                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_maybe_undef:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_maybe_undef, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_maybe_undef:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_maybe_undef,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_maybe_undef:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_maybe_undef,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_maybe_undef:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_maybe_undef,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_maybe_undef:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_maybe_undef,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

MaybeUndefAttr::MaybeUndefAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::MaybeUndef, false, false) {}

MaybeUndefAttr *MaybeUndefAttr::clone(TreeContext &C) const {
  auto *A = new (C) MaybeUndefAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void MaybeUndefAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((maybe_undef";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::maybe_undef";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::maybe_undef";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::maybe_undef";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::maybe_undef";
    OS << "]]";
    break;
  }
  }
}

const char *MaybeUndefAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "maybe_undef";
  case 1:
    return "maybe_undef";
  case 2:
    return "maybe_undef";
  case 3:
    return "maybe_undef";
  case 4:
    return "maybe_undef";
  }
}

// MinSizeAttr implementation

MinSizeAttr *
MinSizeAttr::CreateImplicit(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MinSizeAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MinSizeAttr *MinSizeAttr::Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MinSizeAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MinSizeAttr *MinSizeAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_minsize:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_minsize,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_minsize:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_minsize,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_minsize:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_minsize,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_minsize:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_minsize, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_minsize:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_minsize, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

MinSizeAttr *MinSizeAttr::Create(TreeContext &Ctx, SourceRange Range,
                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_minsize:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_minsize,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_minsize:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_minsize,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_minsize:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_minsize,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_minsize:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_minsize, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_minsize:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_minsize, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

MinSizeAttr::MinSizeAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::MinSize, false, false) {}

MinSizeAttr *MinSizeAttr::clone(TreeContext &C) const {
  auto *A = new (C) MinSizeAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void MinSizeAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((minsize";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::minsize";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::minsize";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::minsize";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::minsize";
    OS << "]]";
    break;
  }
  }
}

const char *MinSizeAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "minsize";
  case 1:
    return "minsize";
  case 2:
    return "minsize";
  case 3:
    return "minsize";
  case 4:
    return "minsize";
  }
}

// MinVectorWidthAttr implementation

MinVectorWidthAttr *
MinVectorWidthAttr::CreateImplicit(TreeContext &Ctx, unsigned VectorWidth,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MinVectorWidthAttr(Ctx, CommonInfo, VectorWidth);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MinVectorWidthAttr *
MinVectorWidthAttr::Create(TreeContext &Ctx, unsigned VectorWidth,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MinVectorWidthAttr(Ctx, CommonInfo, VectorWidth);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MinVectorWidthAttr *MinVectorWidthAttr::CreateImplicit(TreeContext &Ctx,
                                                       unsigned VectorWidth,
                                                       SourceRange Range,
                                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_min_vector_width:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_min_vector_width,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_min_vector_width:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_min_vector_width,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_min_vector_width:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_min_vector_width,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_min_vector_width:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_min_vector_width,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_min_vector_width:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_min_vector_width,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, VectorWidth, I);
}

MinVectorWidthAttr *MinVectorWidthAttr::Create(TreeContext &Ctx,
                                               unsigned VectorWidth,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_min_vector_width:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_min_vector_width,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_min_vector_width:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_min_vector_width,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_min_vector_width:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_min_vector_width,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_min_vector_width:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_min_vector_width,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_min_vector_width:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_min_vector_width,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, VectorWidth, I);
}

MinVectorWidthAttr::MinVectorWidthAttr(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo,
                                       unsigned VectorWidth)
    : InheritableAttr(Ctx, CommonInfo, attr::MinVectorWidth, false, false),
      vectorWidth(VectorWidth) {}

MinVectorWidthAttr *MinVectorWidthAttr::clone(TreeContext &C) const {
  auto *A = new (C) MinVectorWidthAttr(C, *this, vectorWidth);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void MinVectorWidthAttr::printPretty(raw_ostream &OS,
                                     const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((min_vector_width";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getVectorWidth() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::min_vector_width";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getVectorWidth() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::min_vector_width";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getVectorWidth() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::min_vector_width";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getVectorWidth() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::min_vector_width";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getVectorWidth() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *MinVectorWidthAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "min_vector_width";
  case 1:
    return "min_vector_width";
  case 2:
    return "min_vector_width";
  case 3:
    return "min_vector_width";
  case 4:
    return "min_vector_width";
  }
}

// ModeAttr implementation

ModeAttr *ModeAttr::CreateImplicit(TreeContext &Ctx, IdentifierInfo *Mode,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ModeAttr(Ctx, CommonInfo, Mode);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ModeAttr *ModeAttr::Create(TreeContext &Ctx, IdentifierInfo *Mode,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ModeAttr(Ctx, CommonInfo, Mode);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ModeAttr *ModeAttr::CreateImplicit(TreeContext &Ctx, IdentifierInfo *Mode,
                                   SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_mode:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_mode,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_mode:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_mode, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_mode:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_mode, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Mode, I);
}

ModeAttr *ModeAttr::Create(TreeContext &Ctx, IdentifierInfo *Mode,
                           SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_mode:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_mode,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_mode:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_mode, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_mode:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_mode, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Mode, I);
}

ModeAttr::ModeAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                   IdentifierInfo *Mode)
    : Attr(Ctx, CommonInfo, attr::Mode, false), mode(Mode) {}

ModeAttr *ModeAttr::clone(TreeContext &C) const {
  auto *A = new (C) ModeAttr(C, *this, mode);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ModeAttr::printPretty(raw_ostream &OS,
                           const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((mode";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getMode() ? getMode()->getName() : "") << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::mode";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getMode() ? getMode()->getName() : "") << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::mode";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getMode() ? getMode()->getName() : "") << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *ModeAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "mode";
  case 1:
    return "mode";
  case 2:
    return "mode";
  }
}

// MustTailAttr implementation

MustTailAttr *
MustTailAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MustTailAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MustTailAttr *MustTailAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) MustTailAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

MustTailAttr *MustTailAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_musttail:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_musttail, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_musttail:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_musttail,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_musttail:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_musttail,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_musttail:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_musttail, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_musttail:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_musttail, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

MustTailAttr *MustTailAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_musttail:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_musttail, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_musttail:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_musttail,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_musttail:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_musttail,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_musttail:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_musttail, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_musttail:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_musttail, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

MustTailAttr::MustTailAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : StmtAttr(Ctx, CommonInfo, attr::MustTail, false) {}

MustTailAttr *MustTailAttr::clone(TreeContext &C) const {
  auto *A = new (C) MustTailAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void MustTailAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((musttail";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::musttail";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::musttail";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::musttail";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::musttail";
    OS << "]]";
    break;
  }
  }
}

const char *MustTailAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "musttail";
  case 1:
    return "musttail";
  case 2:
    return "musttail";
  case 3:
    return "musttail";
  case 4:
    return "musttail";
  }
}

// NakedAttr implementation

NakedAttr *NakedAttr::CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NakedAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NakedAttr *NakedAttr::Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NakedAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NakedAttr *NakedAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_naked:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_naked,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_naked:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_naked, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_naked:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_naked, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_naked:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_naked, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NakedAttr *NakedAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_naked:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_naked,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_naked:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_naked, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_naked:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_naked, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_naked:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_naked, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NakedAttr::NakedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Naked, false, false) {}

NakedAttr *NakedAttr::clone(TreeContext &C) const {
  auto *A = new (C) NakedAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NakedAttr::printPretty(raw_ostream &OS,
                            const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((naked";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::naked";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::naked";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __declspec(naked";
    OS << ")";
    break;
  }
  }
}

const char *NakedAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "naked";
  case 1:
    return "naked";
  case 2:
    return "naked";
  case 3:
    return "naked";
  }
}

// NoAliasAttr implementation

NoAliasAttr *
NoAliasAttr::CreateImplicit(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoAliasAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoAliasAttr *NoAliasAttr::Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoAliasAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoAliasAttr *NoAliasAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Declspec, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

NoAliasAttr *NoAliasAttr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Declspec, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

NoAliasAttr::NoAliasAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NoAlias, false, false) {}

NoAliasAttr *NoAliasAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoAliasAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoAliasAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __declspec(noalias";
    OS << ")";
    break;
  }
  }
}

const char *NoAliasAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "noalias";
  }
}

// NoBuiltinAttr implementation

NoBuiltinAttr *
NoBuiltinAttr::CreateImplicit(TreeContext &Ctx, StringRef *BuiltinNames,
                              unsigned BuiltinNamesSize,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A =
      new (Ctx) NoBuiltinAttr(Ctx, CommonInfo, BuiltinNames, BuiltinNamesSize);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoBuiltinAttr *NoBuiltinAttr::Create(TreeContext &Ctx, StringRef *BuiltinNames,
                                     unsigned BuiltinNamesSize,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A =
      new (Ctx) NoBuiltinAttr(Ctx, CommonInfo, BuiltinNames, BuiltinNamesSize);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoBuiltinAttr *NoBuiltinAttr::CreateImplicit(TreeContext &Ctx,
                                             StringRef *BuiltinNames,
                                             unsigned BuiltinNamesSize,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_builtin:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_no_builtin, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_no_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_no_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_no_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_no_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_no_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_no_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_no_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_no_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, BuiltinNames, BuiltinNamesSize, I);
}

NoBuiltinAttr *NoBuiltinAttr::Create(TreeContext &Ctx, StringRef *BuiltinNames,
                                     unsigned BuiltinNamesSize,
                                     SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_builtin:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_no_builtin, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_no_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_no_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_no_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_no_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_no_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_no_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_no_builtin:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_no_builtin,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, BuiltinNames, BuiltinNamesSize, I);
}

NoBuiltinAttr::NoBuiltinAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo,
                             StringRef *BuiltinNames, unsigned BuiltinNamesSize)
    : Attr(Ctx, CommonInfo, attr::NoBuiltin, false),
      builtinNames_Size(BuiltinNamesSize),
      builtinNames_(new (Ctx, 16) StringRef[builtinNames_Size]) {
  for (size_t I = 0, E = builtinNames_Size; I != E; ++I) {
    StringRef Ref = BuiltinNames[I];
    if (!Ref.empty()) {
      char *Mem = new (Ctx, 1) char[Ref.size()];
      std::memcpy(Mem, Ref.data(), Ref.size());
      builtinNames_[I] = StringRef(Mem, Ref.size());
    }
  }
}

NoBuiltinAttr::NoBuiltinAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo)
    : Attr(Ctx, CommonInfo, attr::NoBuiltin, false), builtinNames_Size(0),
      builtinNames_(nullptr) {}

NoBuiltinAttr *NoBuiltinAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoBuiltinAttr(C, *this, builtinNames_, builtinNames_Size);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoBuiltinAttr::printPretty(raw_ostream &OS,
                                const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((no_builtin";
    OS << "";
    for (const auto &Val : builtinNames()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::no_builtin";
    OS << "";
    for (const auto &Val : builtinNames()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::no_builtin";
    OS << "";
    for (const auto &Val : builtinNames()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::no_builtin";
    OS << "";
    for (const auto &Val : builtinNames()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::no_builtin";
    OS << "";
    for (const auto &Val : builtinNames()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *NoBuiltinAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "no_builtin";
  case 1:
    return "no_builtin";
  case 2:
    return "no_builtin";
  case 3:
    return "no_builtin";
  case 4:
    return "no_builtin";
  }
}

// NoCommonAttr implementation

NoCommonAttr *
NoCommonAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoCommonAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoCommonAttr *NoCommonAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoCommonAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoCommonAttr *NoCommonAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nocommon:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_nocommon, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_nocommon:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_nocommon,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_nocommon:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_nocommon, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoCommonAttr *NoCommonAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nocommon:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_nocommon, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_nocommon:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_nocommon,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_nocommon:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_nocommon, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoCommonAttr::NoCommonAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NoCommon, false, false) {}

NoCommonAttr *NoCommonAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoCommonAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoCommonAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((nocommon";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::nocommon";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::nocommon";
    OS << "]]";
    break;
  }
  }
}

const char *NoCommonAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "nocommon";
  case 1:
    return "nocommon";
  case 2:
    return "nocommon";
  }
}

// NoDebugAttr implementation

NoDebugAttr *
NoDebugAttr::CreateImplicit(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoDebugAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoDebugAttr *NoDebugAttr::Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoDebugAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoDebugAttr *NoDebugAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nodebug:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_nodebug,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_nodebug:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_nodebug, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_nodebug:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_nodebug, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoDebugAttr *NoDebugAttr::Create(TreeContext &Ctx, SourceRange Range,
                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nodebug:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_nodebug,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_nodebug:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_nodebug, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_nodebug:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_nodebug, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoDebugAttr::NoDebugAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NoDebug, false, false) {}

NoDebugAttr *NoDebugAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoDebugAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoDebugAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((nodebug";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::nodebug";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::nodebug";
    OS << "]]";
    break;
  }
  }
}

const char *NoDebugAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "nodebug";
  case 1:
    return "nodebug";
  case 2:
    return "nodebug";
  }
}

// NoDerefAttr implementation

NoDerefAttr *
NoDerefAttr::CreateImplicit(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoDerefAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoDerefAttr *NoDerefAttr::Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoDerefAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoDerefAttr *NoDerefAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_noderef:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_noderef,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_noderef:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_noderef,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_noderef:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_noderef,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_noderef:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_noderef, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_noderef:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_noderef, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoDerefAttr *NoDerefAttr::Create(TreeContext &Ctx, SourceRange Range,
                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_noderef:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_noderef,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_noderef:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_noderef,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_noderef:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_noderef,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_noderef:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_noderef, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_noderef:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_noderef, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoDerefAttr::NoDerefAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : TypeAttr(Ctx, CommonInfo, attr::NoDeref, false) {}

NoDerefAttr *NoDerefAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoDerefAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoDerefAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((noderef";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::noderef";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::noderef";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::noderef";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::noderef";
    OS << "]]";
    break;
  }
  }
}

const char *NoDerefAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "noderef";
  case 1:
    return "noderef";
  case 2:
    return "noderef";
  case 3:
    return "noderef";
  case 4:
    return "noderef";
  }
}

// NoDestroyAttr implementation

NoDestroyAttr *
NoDestroyAttr::CreateImplicit(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoDestroyAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoDestroyAttr *NoDestroyAttr::Create(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoDestroyAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoDestroyAttr *NoDestroyAttr::CreateImplicit(TreeContext &Ctx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_destroy:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_no_destroy, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_no_destroy:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_no_destroy,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_no_destroy:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_no_destroy,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoDestroyAttr *NoDestroyAttr::Create(TreeContext &Ctx, SourceRange Range,
                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_destroy:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_no_destroy, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_no_destroy:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_no_destroy,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_no_destroy:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_no_destroy,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoDestroyAttr::NoDestroyAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NoDestroy, false, false) {}

NoDestroyAttr *NoDestroyAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoDestroyAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoDestroyAttr::printPretty(raw_ostream &OS,
                                const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((no_destroy";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::no_destroy";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::no_destroy";
    OS << "]]";
    break;
  }
  }
}

const char *NoDestroyAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "no_destroy";
  case 1:
    return "no_destroy";
  case 2:
    return "no_destroy";
  }
}

// NoDuplicateAttr implementation

NoDuplicateAttr *
NoDuplicateAttr::CreateImplicit(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoDuplicateAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoDuplicateAttr *
NoDuplicateAttr::Create(TreeContext &Ctx,
                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoDuplicateAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoDuplicateAttr *NoDuplicateAttr::CreateImplicit(TreeContext &Ctx,
                                                 SourceRange Range,
                                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_noduplicate:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_noduplicate, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_noduplicate:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_noduplicate,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_noduplicate:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_noduplicate,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_noduplicate:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_noduplicate,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_noduplicate:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_noduplicate,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoDuplicateAttr *NoDuplicateAttr::Create(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_noduplicate:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_noduplicate, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_noduplicate:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_noduplicate,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_noduplicate:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_noduplicate,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_noduplicate:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_noduplicate,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_noduplicate:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_noduplicate,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoDuplicateAttr::NoDuplicateAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NoDuplicate, false, false) {}

NoDuplicateAttr *NoDuplicateAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoDuplicateAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoDuplicateAttr::printPretty(raw_ostream &OS,
                                  const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((noduplicate";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::noduplicate";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::noduplicate";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::noduplicate";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::noduplicate";
    OS << "]]";
    break;
  }
  }
}

const char *NoDuplicateAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "noduplicate";
  case 1:
    return "noduplicate";
  case 2:
    return "noduplicate";
  case 3:
    return "noduplicate";
  case 4:
    return "noduplicate";
  }
}

// NoEscapeAttr implementation

NoEscapeAttr *
NoEscapeAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoEscapeAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoEscapeAttr *NoEscapeAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoEscapeAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoEscapeAttr *NoEscapeAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_noescape:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_noescape, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_noescape:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_noescape,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_noescape:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_noescape,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_noescape:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_noescape, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_noescape:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_noescape, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoEscapeAttr *NoEscapeAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_noescape:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_noescape, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_noescape:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_noescape,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_noescape:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_noescape,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_noescape:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_noescape, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_noescape:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_noescape, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoEscapeAttr::NoEscapeAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : Attr(Ctx, CommonInfo, attr::NoEscape, false) {}

NoEscapeAttr *NoEscapeAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoEscapeAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoEscapeAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((noescape";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::noescape";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::noescape";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::noescape";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::noescape";
    OS << "]]";
    break;
  }
  }
}

const char *NoEscapeAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "noescape";
  case 1:
    return "noescape";
  case 2:
    return "noescape";
  case 3:
    return "noescape";
  case 4:
    return "noescape";
  }
}

// NoInlineAttr implementation

NoInlineAttr *
NoInlineAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoInlineAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoInlineAttr *NoInlineAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoInlineAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoInlineAttr *NoInlineAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Keyword_noinline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_noinline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_noinline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_noinline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_noinline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_noinline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_noinline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_noinline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_noinline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_noinline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_noinline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_noinline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_noinline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_noinline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoInlineAttr *NoInlineAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Keyword_noinline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_noinline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_noinline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_noinline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_noinline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_noinline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_noinline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_noinline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_noinline:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_noinline,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_noinline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_noinline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_noinline:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_noinline, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoInlineAttr::NoInlineAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : DeclOrStmtAttr(Ctx, CommonInfo, attr::NoInline, false, false) {}

NoInlineAttr *NoInlineAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoInlineAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoInlineAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __noinline__";
    OS << "";
    break;
  }
  case 1: {
    OS << " __attribute__((noinline";
    OS << "))";
    break;
  }
  case 2: {
    OS << " [[gnu::noinline";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[gnu::noinline";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::noinline";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " [[neverc::noinline";
    OS << "]]";
    break;
  }
  case 6: {
    OS << " __declspec(noinline";
    OS << ")";
    break;
  }
  }
}

const char *NoInlineAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "__noinline__";
  case 1:
    return "noinline";
  case 2:
    return "noinline";
  case 3:
    return "noinline";
  case 4:
    return "noinline";
  case 5:
    return "noinline";
  case 6:
    return "noinline";
  }
}

// NoMergeAttr implementation

NoMergeAttr *
NoMergeAttr::CreateImplicit(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoMergeAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoMergeAttr *NoMergeAttr::Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoMergeAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoMergeAttr *NoMergeAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nomerge:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_nomerge,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_nomerge:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_nomerge,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_nomerge:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_nomerge,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_nomerge:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_nomerge, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_nomerge:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_nomerge, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoMergeAttr *NoMergeAttr::Create(TreeContext &Ctx, SourceRange Range,
                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nomerge:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_nomerge,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_nomerge:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_nomerge,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_nomerge:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_nomerge,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_nomerge:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_nomerge, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_nomerge:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_nomerge, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoMergeAttr::NoMergeAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : DeclOrStmtAttr(Ctx, CommonInfo, attr::NoMerge, false, false) {}

NoMergeAttr *NoMergeAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoMergeAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoMergeAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((nomerge";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::nomerge";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::nomerge";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::nomerge";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::nomerge";
    OS << "]]";
    break;
  }
  }
}

const char *NoMergeAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "nomerge";
  case 1:
    return "nomerge";
  case 2:
    return "nomerge";
  case 3:
    return "nomerge";
  case 4:
    return "nomerge";
  }
}

// NoRandomizeLayoutAttr implementation

NoRandomizeLayoutAttr *
NoRandomizeLayoutAttr::CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoRandomizeLayoutAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoRandomizeLayoutAttr *
NoRandomizeLayoutAttr::Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoRandomizeLayoutAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoRandomizeLayoutAttr *NoRandomizeLayoutAttr::CreateImplicit(TreeContext &Ctx,
                                                             SourceRange Range,
                                                             Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_randomize_layout:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_no_randomize_layout,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_no_randomize_layout:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_no_randomize_layout,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_no_randomize_layout:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_no_randomize_layout,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoRandomizeLayoutAttr *
NoRandomizeLayoutAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_randomize_layout:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_no_randomize_layout,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_no_randomize_layout:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_no_randomize_layout,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_no_randomize_layout:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_no_randomize_layout,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoRandomizeLayoutAttr::NoRandomizeLayoutAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NoRandomizeLayout, false, false) {}

NoRandomizeLayoutAttr *NoRandomizeLayoutAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoRandomizeLayoutAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoRandomizeLayoutAttr::printPretty(raw_ostream &OS,
                                        const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((no_randomize_layout";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::no_randomize_layout";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::no_randomize_layout";
    OS << "]]";
    break;
  }
  }
}

const char *NoRandomizeLayoutAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "no_randomize_layout";
  case 1:
    return "no_randomize_layout";
  case 2:
    return "no_randomize_layout";
  }
}

// NoReturnAttr implementation

NoReturnAttr *
NoReturnAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoReturnAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoReturnAttr *NoReturnAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoReturnAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoReturnAttr *NoReturnAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_noreturn:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_noreturn, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_noreturn:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_noreturn,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_noreturn:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_noreturn, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_noreturn:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_noreturn, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoReturnAttr *NoReturnAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_noreturn:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_noreturn, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_noreturn:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_noreturn,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_noreturn:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_noreturn, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_noreturn:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_noreturn, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoReturnAttr::NoReturnAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NoReturn, false, false) {}

NoReturnAttr *NoReturnAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoReturnAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoReturnAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((noreturn";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::noreturn";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::noreturn";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __declspec(noreturn";
    OS << ")";
    break;
  }
  }
}

const char *NoReturnAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "noreturn";
  case 1:
    return "noreturn";
  case 2:
    return "noreturn";
  case 3:
    return "noreturn";
  }
}

// NoSpeculativeLoadHardeningAttr implementation

NoSpeculativeLoadHardeningAttr *NoSpeculativeLoadHardeningAttr::CreateImplicit(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoSpeculativeLoadHardeningAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoSpeculativeLoadHardeningAttr *
NoSpeculativeLoadHardeningAttr::Create(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoSpeculativeLoadHardeningAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoSpeculativeLoadHardeningAttr *
NoSpeculativeLoadHardeningAttr::CreateImplicit(TreeContext &Ctx,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_no_speculative_load_hardening,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_no_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_clang_no_speculative_load_hardening, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_no_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_neverc_no_speculative_load_hardening, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case C23_clang_no_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_no_speculative_load_hardening,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_no_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_no_speculative_load_hardening,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoSpeculativeLoadHardeningAttr *
NoSpeculativeLoadHardeningAttr::Create(TreeContext &Ctx, SourceRange Range,
                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_no_speculative_load_hardening,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_no_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_clang_no_speculative_load_hardening, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_no_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_neverc_no_speculative_load_hardening, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case C23_clang_no_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_no_speculative_load_hardening,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_no_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_no_speculative_load_hardening,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoSpeculativeLoadHardeningAttr::NoSpeculativeLoadHardeningAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NoSpeculativeLoadHardening, false,
                      false) {}

NoSpeculativeLoadHardeningAttr *
NoSpeculativeLoadHardeningAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoSpeculativeLoadHardeningAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoSpeculativeLoadHardeningAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((no_speculative_load_hardening";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::no_speculative_load_hardening";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::no_speculative_load_hardening";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::no_speculative_load_hardening";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::no_speculative_load_hardening";
    OS << "]]";
    break;
  }
  }
}

const char *NoSpeculativeLoadHardeningAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "no_speculative_load_hardening";
  case 1:
    return "no_speculative_load_hardening";
  case 2:
    return "no_speculative_load_hardening";
  case 3:
    return "no_speculative_load_hardening";
  case 4:
    return "no_speculative_load_hardening";
  }
}

// NoSplitStackAttr implementation

NoSplitStackAttr *
NoSplitStackAttr::CreateImplicit(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoSplitStackAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoSplitStackAttr *
NoSplitStackAttr::Create(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoSplitStackAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoSplitStackAttr *NoSplitStackAttr::CreateImplicit(TreeContext &Ctx,
                                                   SourceRange Range,
                                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_split_stack:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_no_split_stack, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_no_split_stack:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_no_split_stack,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_no_split_stack:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_no_split_stack,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoSplitStackAttr *NoSplitStackAttr::Create(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_split_stack:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_no_split_stack, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_no_split_stack:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_no_split_stack,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_no_split_stack:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_no_split_stack,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoSplitStackAttr::NoSplitStackAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NoSplitStack, false, false) {}

NoSplitStackAttr *NoSplitStackAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoSplitStackAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoSplitStackAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((no_split_stack";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::no_split_stack";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::no_split_stack";
    OS << "]]";
    break;
  }
  }
}

const char *NoSplitStackAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "no_split_stack";
  case 1:
    return "no_split_stack";
  case 2:
    return "no_split_stack";
  }
}

// NoStackProtectorAttr implementation

NoStackProtectorAttr *
NoStackProtectorAttr::CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoStackProtectorAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoStackProtectorAttr *
NoStackProtectorAttr::Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoStackProtectorAttr(Ctx, CommonInfo);
  return A;
}

NoStackProtectorAttr *NoStackProtectorAttr::CreateImplicit(TreeContext &Ctx,
                                                           SourceRange Range,
                                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Declspec_safebuffers:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Declspec, Declspec_safebuffers,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoStackProtectorAttr *
NoStackProtectorAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_no_stack_protector:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_no_stack_protector,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Declspec_safebuffers:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Declspec, Declspec_safebuffers,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoStackProtectorAttr::NoStackProtectorAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NoStackProtector, false, false) {}

NoStackProtectorAttr::Spelling
NoStackProtectorAttr::getSemanticSpelling() const {
  switch (getAttributeSpellingListIndex()) {
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
NoStackProtectorAttr *NoStackProtectorAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoStackProtectorAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoStackProtectorAttr::printPretty(raw_ostream &OS,
                                       const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((no_stack_protector";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::no_stack_protector";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::no_stack_protector";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::no_stack_protector";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::no_stack_protector";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " [[gnu::no_stack_protector";
    OS << "]]";
    break;
  }
  case 6: {
    OS << " [[gnu::no_stack_protector";
    OS << "]]";
    break;
  }
  case 7: {
    OS << " __declspec(safebuffers";
    OS << ")";
    break;
  }
  }
}

const char *NoStackProtectorAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "no_stack_protector";
  case 1:
    return "no_stack_protector";
  case 2:
    return "no_stack_protector";
  case 3:
    return "no_stack_protector";
  case 4:
    return "no_stack_protector";
  case 5:
    return "no_stack_protector";
  case 6:
    return "no_stack_protector";
  case 7:
    return "safebuffers";
  }
}

// NoThrowAttr implementation

NoThrowAttr *
NoThrowAttr::CreateImplicit(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoThrowAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoThrowAttr *NoThrowAttr::Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoThrowAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoThrowAttr *NoThrowAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nothrow:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_nothrow,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_nothrow:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_nothrow, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_nothrow:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_nothrow, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_nothrow:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_nothrow, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoThrowAttr *NoThrowAttr::Create(TreeContext &Ctx, SourceRange Range,
                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nothrow:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_nothrow,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_nothrow:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_nothrow, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_nothrow:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_nothrow, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_nothrow:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_nothrow, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoThrowAttr::NoThrowAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NoThrow, false, false) {}

NoThrowAttr *NoThrowAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoThrowAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoThrowAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((nothrow";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::nothrow";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::nothrow";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __declspec(nothrow";
    OS << ")";
    break;
  }
  }
}

const char *NoThrowAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "nothrow";
  case 1:
    return "nothrow";
  case 2:
    return "nothrow";
  case 3:
    return "nothrow";
  }
}

// NoUwtableAttr implementation

NoUwtableAttr *
NoUwtableAttr::CreateImplicit(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoUwtableAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoUwtableAttr *NoUwtableAttr::Create(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NoUwtableAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NoUwtableAttr *NoUwtableAttr::CreateImplicit(TreeContext &Ctx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nouwtable:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_nouwtable, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_nouwtable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_nouwtable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_nouwtable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_nouwtable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_nouwtable:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_nouwtable, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_nouwtable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_nouwtable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NoUwtableAttr *NoUwtableAttr::Create(TreeContext &Ctx, SourceRange Range,
                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nouwtable:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_nouwtable, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_nouwtable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_nouwtable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_nouwtable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_nouwtable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_nouwtable:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_nouwtable, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_nouwtable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_nouwtable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NoUwtableAttr::NoUwtableAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NoUwtable, false, false) {}

NoUwtableAttr *NoUwtableAttr::clone(TreeContext &C) const {
  auto *A = new (C) NoUwtableAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NoUwtableAttr::printPretty(raw_ostream &OS,
                                const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((nouwtable";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::nouwtable";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::nouwtable";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::nouwtable";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::nouwtable";
    OS << "]]";
    break;
  }
  }
}

const char *NoUwtableAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "nouwtable";
  case 1:
    return "nouwtable";
  case 2:
    return "nouwtable";
  case 3:
    return "nouwtable";
  case 4:
    return "nouwtable";
  }
}

// NonNullAttr implementation

NonNullAttr *
NonNullAttr::CreateImplicit(TreeContext &Ctx, ParamIdx *Args, unsigned ArgsSize,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NonNullAttr(Ctx, CommonInfo, Args, ArgsSize);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NonNullAttr *NonNullAttr::Create(TreeContext &Ctx, ParamIdx *Args,
                                 unsigned ArgsSize,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NonNullAttr(Ctx, CommonInfo, Args, ArgsSize);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NonNullAttr *NonNullAttr::CreateImplicit(TreeContext &Ctx, ParamIdx *Args,
                                         unsigned ArgsSize, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nonnull:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_nonnull,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_nonnull:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_nonnull, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_nonnull:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_nonnull, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Args, ArgsSize, I);
}

NonNullAttr *NonNullAttr::Create(TreeContext &Ctx, ParamIdx *Args,
                                 unsigned ArgsSize, SourceRange Range,
                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_nonnull:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_nonnull,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_nonnull:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_nonnull, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_nonnull:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_nonnull, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Args, ArgsSize, I);
}

NonNullAttr::NonNullAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo, ParamIdx *Args,
                         unsigned ArgsSize)
    : InheritableParamAttr(Ctx, CommonInfo, attr::NonNull, false, true),
      args_Size(ArgsSize), args_(new (Ctx, 16) ParamIdx[args_Size]) {
  std::copy(Args, Args + args_Size, args_);
}

NonNullAttr::NonNullAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : InheritableParamAttr(Ctx, CommonInfo, attr::NonNull, false, true),
      args_Size(0), args_(nullptr) {}

NonNullAttr *NonNullAttr::clone(TreeContext &C) const {
  auto *A = new (C) NonNullAttr(C, *this, args_, args_Size);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NonNullAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((nonnull";
    OS << "";
    for (const auto &Val : args()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val.getSourceIndex();
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::nonnull";
    OS << "";
    for (const auto &Val : args()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val.getSourceIndex();
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::nonnull";
    OS << "";
    for (const auto &Val : args()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << Val.getSourceIndex();
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *NonNullAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "nonnull";
  case 1:
    return "nonnull";
  case 2:
    return "nonnull";
  }
}

// NotTailCalledAttr implementation

NotTailCalledAttr *
NotTailCalledAttr::CreateImplicit(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NotTailCalledAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NotTailCalledAttr *
NotTailCalledAttr::Create(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) NotTailCalledAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

NotTailCalledAttr *NotTailCalledAttr::CreateImplicit(TreeContext &Ctx,
                                                     SourceRange Range,
                                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_not_tail_called:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_not_tail_called, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_not_tail_called:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_not_tail_called,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_not_tail_called:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_not_tail_called,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_not_tail_called:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_not_tail_called,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_not_tail_called:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_not_tail_called,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

NotTailCalledAttr *NotTailCalledAttr::Create(TreeContext &Ctx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_not_tail_called:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_not_tail_called, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_not_tail_called:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_not_tail_called,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_not_tail_called:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_not_tail_called,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_not_tail_called:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_not_tail_called,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_not_tail_called:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_not_tail_called,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

NotTailCalledAttr::NotTailCalledAttr(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::NotTailCalled, false, false) {}

NotTailCalledAttr *NotTailCalledAttr::clone(TreeContext &C) const {
  auto *A = new (C) NotTailCalledAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void NotTailCalledAttr::printPretty(raw_ostream &OS,
                                    const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((not_tail_called";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::not_tail_called";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::not_tail_called";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::not_tail_called";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::not_tail_called";
    OS << "]]";
    break;
  }
  }
}

const char *NotTailCalledAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "not_tail_called";
  case 1:
    return "not_tail_called";
  case 2:
    return "not_tail_called";
  case 3:
    return "not_tail_called";
  case 4:
    return "not_tail_called";
  }
}

// OptimizeNoneAttr implementation

OptimizeNoneAttr *
OptimizeNoneAttr::CreateImplicit(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) OptimizeNoneAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

OptimizeNoneAttr *
OptimizeNoneAttr::Create(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) OptimizeNoneAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

OptimizeNoneAttr *OptimizeNoneAttr::CreateImplicit(TreeContext &Ctx,
                                                   SourceRange Range,
                                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_optnone:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_optnone,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_optnone:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_optnone,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_optnone:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_optnone,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_optnone:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_optnone, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_optnone:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_optnone, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

OptimizeNoneAttr *OptimizeNoneAttr::Create(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_optnone:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_optnone,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_optnone:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_optnone,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_optnone:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_optnone,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_optnone:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_optnone, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_optnone:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_optnone, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

OptimizeNoneAttr::OptimizeNoneAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::OptimizeNone, false, false) {}

OptimizeNoneAttr *OptimizeNoneAttr::clone(TreeContext &C) const {
  auto *A = new (C) OptimizeNoneAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void OptimizeNoneAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((optnone";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::optnone";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::optnone";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::optnone";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::optnone";
    OS << "]]";
    break;
  }
  }
}

const char *OptimizeNoneAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "optnone";
  case 1:
    return "optnone";
  case 2:
    return "optnone";
  case 3:
    return "optnone";
  case 4:
    return "optnone";
  }
}

// OverloadableAttr implementation

OverloadableAttr *
OverloadableAttr::CreateImplicit(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) OverloadableAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

OverloadableAttr *
OverloadableAttr::Create(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) OverloadableAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

OverloadableAttr *OverloadableAttr::CreateImplicit(TreeContext &Ctx,
                                                   SourceRange Range,
                                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_overloadable:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_overloadable, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_overloadable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_overloadable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_overloadable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_overloadable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_overloadable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_overloadable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_overloadable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_overloadable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

OverloadableAttr *OverloadableAttr::Create(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_overloadable:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_overloadable, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_overloadable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_overloadable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_overloadable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_overloadable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_overloadable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_overloadable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_overloadable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_overloadable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

OverloadableAttr::OverloadableAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo)
    : Attr(Ctx, CommonInfo, attr::Overloadable, false) {}

OverloadableAttr *OverloadableAttr::clone(TreeContext &C) const {
  auto *A = new (C) OverloadableAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void OverloadableAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((overloadable";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::overloadable";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::overloadable";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::overloadable";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::overloadable";
    OS << "]]";
    break;
  }
  }
}

const char *OverloadableAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "overloadable";
  case 1:
    return "overloadable";
  case 2:
    return "overloadable";
  case 3:
    return "overloadable";
  case 4:
    return "overloadable";
  }
}

// OverrideAttr implementation

OverrideAttr *
OverrideAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) OverrideAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

OverrideAttr *OverrideAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) OverrideAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

OverrideAttr *OverrideAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_override:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_override, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_override:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_override,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_override:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_override, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_override:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_override, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

OverrideAttr *OverrideAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_override:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_override, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_override:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_override,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_override:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_override, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_override:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_override, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

OverrideAttr::OverrideAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Override, false, false) {}

OverrideAttr *OverrideAttr::clone(TreeContext &C) const {
  auto *A = new (C) OverrideAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void OverrideAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((override";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::override";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::override";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __declspec(override";
    OS << ")";
    break;
  }
  }
}

const char *OverrideAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "override";
  case 1:
    return "override";
  case 2:
    return "override";
  case 3:
    return "override";
  }
}

// PackedAttr implementation

PackedAttr *PackedAttr::CreateImplicit(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PackedAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PackedAttr *PackedAttr::Create(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PackedAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PackedAttr *PackedAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_packed:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_packed,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_packed:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_packed, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_packed:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_packed, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

PackedAttr *PackedAttr::Create(TreeContext &Ctx, SourceRange Range,
                               Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_packed:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_packed,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_packed:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_packed, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_packed:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_packed, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

PackedAttr::PackedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Packed, false, false) {}

PackedAttr *PackedAttr::clone(TreeContext &C) const {
  auto *A = new (C) PackedAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void PackedAttr::printPretty(raw_ostream &OS,
                             const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((packed";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::packed";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::packed";
    OS << "]]";
    break;
  }
  }
}

const char *PackedAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "packed";
  case 1:
    return "packed";
  case 2:
    return "packed";
  }
}

// PassObjectSizeAttr implementation

PassObjectSizeAttr *
PassObjectSizeAttr::CreateImplicit(TreeContext &Ctx, int Type,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PassObjectSizeAttr(Ctx, CommonInfo, Type);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PassObjectSizeAttr *
PassObjectSizeAttr::Create(TreeContext &Ctx, int Type,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PassObjectSizeAttr(Ctx, CommonInfo, Type);
  return A;
}

PassObjectSizeAttr *PassObjectSizeAttr::CreateImplicit(TreeContext &Ctx,
                                                       int Type,
                                                       SourceRange Range,
                                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_pass_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_pass_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_pass_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_pass_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_pass_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_pass_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_pass_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_pass_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_pass_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_pass_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_pass_dynamic_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_pass_dynamic_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_pass_dynamic_object_size:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_clang_pass_dynamic_object_size,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_pass_dynamic_object_size:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_neverc_pass_dynamic_object_size,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_clang_pass_dynamic_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_pass_dynamic_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_pass_dynamic_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_pass_dynamic_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Type, I);
}

PassObjectSizeAttr *PassObjectSizeAttr::Create(TreeContext &Ctx, int Type,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_pass_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_pass_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_pass_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_pass_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_pass_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_pass_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_pass_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_pass_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_pass_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_pass_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_pass_dynamic_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_pass_dynamic_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_pass_dynamic_object_size:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_clang_pass_dynamic_object_size,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_pass_dynamic_object_size:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_neverc_pass_dynamic_object_size,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_clang_pass_dynamic_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_pass_dynamic_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_pass_dynamic_object_size:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_pass_dynamic_object_size,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Type, I);
}

PassObjectSizeAttr::PassObjectSizeAttr(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo,
                                       int Type)
    : InheritableParamAttr(Ctx, CommonInfo, attr::PassObjectSize, false, false),
      type(Type) {}

PassObjectSizeAttr::Spelling PassObjectSizeAttr::getSemanticSpelling() const {
  switch (getAttributeSpellingListIndex()) {
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

PassObjectSizeAttr *PassObjectSizeAttr::clone(TreeContext &C) const {
  auto *A = new (C) PassObjectSizeAttr(C, *this, type);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void PassObjectSizeAttr::printPretty(raw_ostream &OS,
                                     const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((pass_object_size";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getType() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::pass_object_size";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getType() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::pass_object_size";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getType() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::pass_object_size";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getType() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::pass_object_size";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getType() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " __attribute__((pass_dynamic_object_size";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getType() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 6: {
    OS << " [[clang::pass_dynamic_object_size";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getType() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 7: {
    OS << " [[neverc::pass_dynamic_object_size";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getType() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 8: {
    OS << " [[clang::pass_dynamic_object_size";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getType() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 9: {
    OS << " [[neverc::pass_dynamic_object_size";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getType() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *PassObjectSizeAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "pass_object_size";
  case 1:
    return "pass_object_size";
  case 2:
    return "pass_object_size";
  case 3:
    return "pass_object_size";
  case 4:
    return "pass_object_size";
  case 5:
    return "pass_dynamic_object_size";
  case 6:
    return "pass_dynamic_object_size";
  case 7:
    return "pass_dynamic_object_size";
  case 8:
    return "pass_dynamic_object_size";
  case 9:
    return "pass_dynamic_object_size";
  }
}

// PatchableFunctionEntryAttr implementation

PatchableFunctionEntryAttr *PatchableFunctionEntryAttr::CreateImplicit(
    TreeContext &Ctx, unsigned Count, int Offset,
    const AttributeCommonInfo &CommonInfo) {
  auto *A =
      new (Ctx) PatchableFunctionEntryAttr(Ctx, CommonInfo, Count, Offset);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PatchableFunctionEntryAttr *
PatchableFunctionEntryAttr::Create(TreeContext &Ctx, unsigned Count, int Offset,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A =
      new (Ctx) PatchableFunctionEntryAttr(Ctx, CommonInfo, Count, Offset);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PatchableFunctionEntryAttr *
PatchableFunctionEntryAttr::CreateImplicit(TreeContext &Ctx, unsigned Count,
                                           int Offset, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_patchable_function_entry:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_patchable_function_entry,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_patchable_function_entry:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_patchable_function_entry,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_patchable_function_entry:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_patchable_function_entry,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Count, Offset, I);
}

PatchableFunctionEntryAttr *
PatchableFunctionEntryAttr::Create(TreeContext &Ctx, unsigned Count, int Offset,
                                   SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_patchable_function_entry:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_patchable_function_entry,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_patchable_function_entry:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_patchable_function_entry,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_patchable_function_entry:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_patchable_function_entry,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Count, Offset, I);
}

PatchableFunctionEntryAttr::PatchableFunctionEntryAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo, unsigned Count,
    int Offset)
    : InheritableAttr(Ctx, CommonInfo, attr::PatchableFunctionEntry, false,
                      false),
      count(Count), offset(Offset) {}

PatchableFunctionEntryAttr::PatchableFunctionEntryAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo, unsigned Count)
    : InheritableAttr(Ctx, CommonInfo, attr::PatchableFunctionEntry, false,
                      false),
      count(Count), offset() {}

PatchableFunctionEntryAttr *
PatchableFunctionEntryAttr::clone(TreeContext &C) const {
  auto *A = new (C) PatchableFunctionEntryAttr(C, *this, count, offset);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void PatchableFunctionEntryAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((patchable_function_entry";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getCount() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getOffset() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::patchable_function_entry";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getCount() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getOffset() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::patchable_function_entry";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getCount() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getOffset() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *PatchableFunctionEntryAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "patchable_function_entry";
  case 1:
    return "patchable_function_entry";
  case 2:
    return "patchable_function_entry";
  }
}

// PragmaNeverCBSSSectionAttr implementation

PragmaNeverCBSSSectionAttr *PragmaNeverCBSSSectionAttr::CreateImplicit(
    TreeContext &Ctx, llvm::StringRef Name,
    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PragmaNeverCBSSSectionAttr(Ctx, CommonInfo, Name);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PragmaNeverCBSSSectionAttr *
PragmaNeverCBSSSectionAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PragmaNeverCBSSSectionAttr(Ctx, CommonInfo, Name);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PragmaNeverCBSSSectionAttr *PragmaNeverCBSSSectionAttr::CreateImplicit(
    TreeContext &Ctx, llvm::StringRef Name, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return CreateImplicit(Ctx, Name, I);
}

PragmaNeverCBSSSectionAttr *
PragmaNeverCBSSSectionAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                   SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return Create(Ctx, Name, I);
}

PragmaNeverCBSSSectionAttr::PragmaNeverCBSSSectionAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    llvm::StringRef Name)
    : InheritableAttr(Ctx, CommonInfo, attr::PragmaNeverCBSSSection, false,
                      false),
      nameLength(Name.size()), name(new (Ctx, 1) char[nameLength]) {
  if (!Name.empty())
    std::memcpy(name, Name.data(), nameLength);
}

PragmaNeverCBSSSectionAttr *
PragmaNeverCBSSSectionAttr::clone(TreeContext &C) const {
  auto *A = new (C) PragmaNeverCBSSSectionAttr(C, *this, getName());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void PragmaNeverCBSSSectionAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {}

const char *PragmaNeverCBSSSectionAttr::getSpelling() const {
  return "(No spelling)";
}

// PragmaNeverCDataSectionAttr implementation

PragmaNeverCDataSectionAttr *PragmaNeverCDataSectionAttr::CreateImplicit(
    TreeContext &Ctx, llvm::StringRef Name,
    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PragmaNeverCDataSectionAttr(Ctx, CommonInfo, Name);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PragmaNeverCDataSectionAttr *
PragmaNeverCDataSectionAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PragmaNeverCDataSectionAttr(Ctx, CommonInfo, Name);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PragmaNeverCDataSectionAttr *PragmaNeverCDataSectionAttr::CreateImplicit(
    TreeContext &Ctx, llvm::StringRef Name, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return CreateImplicit(Ctx, Name, I);
}

PragmaNeverCDataSectionAttr *
PragmaNeverCDataSectionAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                    SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return Create(Ctx, Name, I);
}

PragmaNeverCDataSectionAttr::PragmaNeverCDataSectionAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    llvm::StringRef Name)
    : InheritableAttr(Ctx, CommonInfo, attr::PragmaNeverCDataSection, false,
                      false),
      nameLength(Name.size()), name(new (Ctx, 1) char[nameLength]) {
  if (!Name.empty())
    std::memcpy(name, Name.data(), nameLength);
}

PragmaNeverCDataSectionAttr *
PragmaNeverCDataSectionAttr::clone(TreeContext &C) const {
  auto *A = new (C) PragmaNeverCDataSectionAttr(C, *this, getName());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void PragmaNeverCDataSectionAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {}

const char *PragmaNeverCDataSectionAttr::getSpelling() const {
  return "(No spelling)";
}

// PragmaNeverCRelroSectionAttr implementation

PragmaNeverCRelroSectionAttr *PragmaNeverCRelroSectionAttr::CreateImplicit(
    TreeContext &Ctx, llvm::StringRef Name,
    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PragmaNeverCRelroSectionAttr(Ctx, CommonInfo, Name);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PragmaNeverCRelroSectionAttr *
PragmaNeverCRelroSectionAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PragmaNeverCRelroSectionAttr(Ctx, CommonInfo, Name);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PragmaNeverCRelroSectionAttr *PragmaNeverCRelroSectionAttr::CreateImplicit(
    TreeContext &Ctx, llvm::StringRef Name, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return CreateImplicit(Ctx, Name, I);
}

PragmaNeverCRelroSectionAttr *
PragmaNeverCRelroSectionAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                     SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return Create(Ctx, Name, I);
}

PragmaNeverCRelroSectionAttr::PragmaNeverCRelroSectionAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    llvm::StringRef Name)
    : InheritableAttr(Ctx, CommonInfo, attr::PragmaNeverCRelroSection, false,
                      false),
      nameLength(Name.size()), name(new (Ctx, 1) char[nameLength]) {
  if (!Name.empty())
    std::memcpy(name, Name.data(), nameLength);
}

PragmaNeverCRelroSectionAttr *
PragmaNeverCRelroSectionAttr::clone(TreeContext &C) const {
  auto *A = new (C) PragmaNeverCRelroSectionAttr(C, *this, getName());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void PragmaNeverCRelroSectionAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {}

const char *PragmaNeverCRelroSectionAttr::getSpelling() const {
  return "(No spelling)";
}

// PragmaNeverCRodataSectionAttr implementation

PragmaNeverCRodataSectionAttr *PragmaNeverCRodataSectionAttr::CreateImplicit(
    TreeContext &Ctx, llvm::StringRef Name,
    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PragmaNeverCRodataSectionAttr(Ctx, CommonInfo, Name);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PragmaNeverCRodataSectionAttr *
PragmaNeverCRodataSectionAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                      const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PragmaNeverCRodataSectionAttr(Ctx, CommonInfo, Name);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PragmaNeverCRodataSectionAttr *PragmaNeverCRodataSectionAttr::CreateImplicit(
    TreeContext &Ctx, llvm::StringRef Name, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return CreateImplicit(Ctx, Name, I);
}

PragmaNeverCRodataSectionAttr *
PragmaNeverCRodataSectionAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                      SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return Create(Ctx, Name, I);
}

PragmaNeverCRodataSectionAttr::PragmaNeverCRodataSectionAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    llvm::StringRef Name)
    : InheritableAttr(Ctx, CommonInfo, attr::PragmaNeverCRodataSection, false,
                      false),
      nameLength(Name.size()), name(new (Ctx, 1) char[nameLength]) {
  if (!Name.empty())
    std::memcpy(name, Name.data(), nameLength);
}

PragmaNeverCRodataSectionAttr *
PragmaNeverCRodataSectionAttr::clone(TreeContext &C) const {
  auto *A = new (C) PragmaNeverCRodataSectionAttr(C, *this, getName());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void PragmaNeverCRodataSectionAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {}

const char *PragmaNeverCRodataSectionAttr::getSpelling() const {
  return "(No spelling)";
}

// PragmaNeverCTextSectionAttr implementation

PragmaNeverCTextSectionAttr *PragmaNeverCTextSectionAttr::CreateImplicit(
    TreeContext &Ctx, llvm::StringRef Name,
    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PragmaNeverCTextSectionAttr(Ctx, CommonInfo, Name);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PragmaNeverCTextSectionAttr *
PragmaNeverCTextSectionAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PragmaNeverCTextSectionAttr(Ctx, CommonInfo, Name);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PragmaNeverCTextSectionAttr *PragmaNeverCTextSectionAttr::CreateImplicit(
    TreeContext &Ctx, llvm::StringRef Name, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return CreateImplicit(Ctx, Name, I);
}

PragmaNeverCTextSectionAttr *
PragmaNeverCTextSectionAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                    SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return Create(Ctx, Name, I);
}

PragmaNeverCTextSectionAttr::PragmaNeverCTextSectionAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    llvm::StringRef Name)
    : InheritableAttr(Ctx, CommonInfo, attr::PragmaNeverCTextSection, false,
                      false),
      nameLength(Name.size()), name(new (Ctx, 1) char[nameLength]) {
  if (!Name.empty())
    std::memcpy(name, Name.data(), nameLength);
}

PragmaNeverCTextSectionAttr *
PragmaNeverCTextSectionAttr::clone(TreeContext &C) const {
  auto *A = new (C) PragmaNeverCTextSectionAttr(C, *this, getName());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void PragmaNeverCTextSectionAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {}

const char *PragmaNeverCTextSectionAttr::getSpelling() const {
  return "(No spelling)";
}

// PreferredTypeAttr implementation

PreferredTypeAttr *
PreferredTypeAttr::CreateImplicit(TreeContext &Ctx, TypeSourceInfo *Type,
                                  const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PreferredTypeAttr(Ctx, CommonInfo, Type);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PreferredTypeAttr *
PreferredTypeAttr::Create(TreeContext &Ctx, TypeSourceInfo *Type,
                          const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PreferredTypeAttr(Ctx, CommonInfo, Type);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PreferredTypeAttr *PreferredTypeAttr::CreateImplicit(TreeContext &Ctx,
                                                     TypeSourceInfo *Type,
                                                     SourceRange Range,
                                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_preferred_type:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_preferred_type, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_preferred_type:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_preferred_type,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_preferred_type:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_preferred_type,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_preferred_type:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_preferred_type,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_preferred_type:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_preferred_type,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Type, I);
}

PreferredTypeAttr *PreferredTypeAttr::Create(TreeContext &Ctx,
                                             TypeSourceInfo *Type,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_preferred_type:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_preferred_type, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_preferred_type:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_preferred_type,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_preferred_type:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_preferred_type,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_preferred_type:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_preferred_type,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_preferred_type:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_preferred_type,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Type, I);
}

PreferredTypeAttr::PreferredTypeAttr(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo,
                                     TypeSourceInfo *Type)
    : InheritableAttr(Ctx, CommonInfo, attr::PreferredType, false, false),
      type(Type) {}

PreferredTypeAttr::PreferredTypeAttr(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::PreferredType, false, false),
      type() {}

PreferredTypeAttr *PreferredTypeAttr::clone(TreeContext &C) const {
  auto *A = new (C) PreferredTypeAttr(C, *this, type);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void PreferredTypeAttr::printPretty(raw_ostream &OS,
                                    const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((preferred_type";
    if (!getTypeLoc())
      ++TrailingOmittedArgs;
    if (!(!getTypeLoc())) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "" << getType().getAsString() << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::preferred_type";
    if (!getTypeLoc())
      ++TrailingOmittedArgs;
    if (!(!getTypeLoc())) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "" << getType().getAsString() << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::preferred_type";
    if (!getTypeLoc())
      ++TrailingOmittedArgs;
    if (!(!getTypeLoc())) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "" << getType().getAsString() << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::preferred_type";
    if (!getTypeLoc())
      ++TrailingOmittedArgs;
    if (!(!getTypeLoc())) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "" << getType().getAsString() << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::preferred_type";
    if (!getTypeLoc())
      ++TrailingOmittedArgs;
    if (!(!getTypeLoc())) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "" << getType().getAsString() << "";
    }
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *PreferredTypeAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "preferred_type";
  case 1:
    return "preferred_type";
  case 2:
    return "preferred_type";
  case 3:
    return "preferred_type";
  case 4:
    return "preferred_type";
  }
}

// PreserveAllAttr implementation

PreserveAllAttr *
PreserveAllAttr::CreateImplicit(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PreserveAllAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PreserveAllAttr *
PreserveAllAttr::Create(TreeContext &Ctx,
                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PreserveAllAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PreserveAllAttr *PreserveAllAttr::CreateImplicit(TreeContext &Ctx,
                                                 SourceRange Range,
                                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_preserve_all:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_preserve_all, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_preserve_all:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_preserve_all,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_preserve_all:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_preserve_all,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_preserve_all:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_preserve_all,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_preserve_all:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_preserve_all,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

PreserveAllAttr *PreserveAllAttr::Create(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_preserve_all:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_preserve_all, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_preserve_all:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_preserve_all,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_preserve_all:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_preserve_all,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_preserve_all:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_preserve_all,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_preserve_all:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_preserve_all,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

PreserveAllAttr::PreserveAllAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::PreserveAll, false, false) {}

PreserveAllAttr *PreserveAllAttr::clone(TreeContext &C) const {
  auto *A = new (C) PreserveAllAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void PreserveAllAttr::printPretty(raw_ostream &OS,
                                  const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((preserve_all";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::preserve_all";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::preserve_all";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::preserve_all";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::preserve_all";
    OS << "]]";
    break;
  }
  }
}

const char *PreserveAllAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "preserve_all";
  case 1:
    return "preserve_all";
  case 2:
    return "preserve_all";
  case 3:
    return "preserve_all";
  case 4:
    return "preserve_all";
  }
}

// PreserveMostAttr implementation

PreserveMostAttr *
PreserveMostAttr::CreateImplicit(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PreserveMostAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PreserveMostAttr *
PreserveMostAttr::Create(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PreserveMostAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PreserveMostAttr *PreserveMostAttr::CreateImplicit(TreeContext &Ctx,
                                                   SourceRange Range,
                                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_preserve_most:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_preserve_most, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_preserve_most:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_preserve_most,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_preserve_most:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_preserve_most,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_preserve_most:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_preserve_most,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_preserve_most:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_preserve_most,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

PreserveMostAttr *PreserveMostAttr::Create(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_preserve_most:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_preserve_most, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_preserve_most:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_preserve_most,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_preserve_most:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_preserve_most,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_preserve_most:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_preserve_most,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_preserve_most:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_preserve_most,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

PreserveMostAttr::PreserveMostAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::PreserveMost, false, false) {}

PreserveMostAttr *PreserveMostAttr::clone(TreeContext &C) const {
  auto *A = new (C) PreserveMostAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void PreserveMostAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((preserve_most";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::preserve_most";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::preserve_most";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::preserve_most";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::preserve_most";
    OS << "]]";
    break;
  }
  }
}

const char *PreserveMostAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "preserve_most";
  case 1:
    return "preserve_most";
  case 2:
    return "preserve_most";
  case 3:
    return "preserve_most";
  case 4:
    return "preserve_most";
  }
}

// Ptr32Attr implementation

Ptr32Attr *Ptr32Attr::CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) Ptr32Attr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

Ptr32Attr *Ptr32Attr::Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) Ptr32Attr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

Ptr32Attr *Ptr32Attr::CreateImplicit(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

Ptr32Attr *Ptr32Attr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

Ptr32Attr::Ptr32Attr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : TypeAttr(Ctx, CommonInfo, attr::Ptr32, false) {}

Ptr32Attr *Ptr32Attr::clone(TreeContext &C) const {
  auto *A = new (C) Ptr32Attr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void Ptr32Attr::printPretty(raw_ostream &OS,
                            const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __ptr32";
    OS << "";
    break;
  }
  }
}

const char *Ptr32Attr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "__ptr32";
  }
}

// Ptr64Attr implementation

Ptr64Attr *Ptr64Attr::CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) Ptr64Attr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

Ptr64Attr *Ptr64Attr::Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) Ptr64Attr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

Ptr64Attr *Ptr64Attr::CreateImplicit(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

Ptr64Attr *Ptr64Attr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

Ptr64Attr::Ptr64Attr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : TypeAttr(Ctx, CommonInfo, attr::Ptr64, false) {}

Ptr64Attr *Ptr64Attr::clone(TreeContext &C) const {
  auto *A = new (C) Ptr64Attr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void Ptr64Attr::printPretty(raw_ostream &OS,
                            const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __ptr64";
    OS << "";
    break;
  }
  }
}

const char *Ptr64Attr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "__ptr64";
  }
}

// PureAttr implementation

PureAttr *PureAttr::CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PureAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PureAttr *PureAttr::Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) PureAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

PureAttr *PureAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_pure:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_pure,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_pure:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_pure, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_pure:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_pure, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

PureAttr *PureAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_pure:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_pure,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_pure:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_pure, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_pure:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_pure, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

PureAttr::PureAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Pure, false, false) {}

PureAttr *PureAttr::clone(TreeContext &C) const {
  auto *A = new (C) PureAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void PureAttr::printPretty(raw_ostream &OS,
                           const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((pure";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::pure";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::pure";
    OS << "]]";
    break;
  }
  }
}

const char *PureAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "pure";
  case 1:
    return "pure";
  case 2:
    return "pure";
  }
}

// RandomizeLayoutAttr implementation

RandomizeLayoutAttr *
RandomizeLayoutAttr::CreateImplicit(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) RandomizeLayoutAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

RandomizeLayoutAttr *
RandomizeLayoutAttr::Create(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) RandomizeLayoutAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

RandomizeLayoutAttr *RandomizeLayoutAttr::CreateImplicit(TreeContext &Ctx,
                                                         SourceRange Range,
                                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_randomize_layout:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_randomize_layout,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_randomize_layout:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_randomize_layout,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_randomize_layout:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_randomize_layout,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

RandomizeLayoutAttr *
RandomizeLayoutAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_randomize_layout:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_randomize_layout,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_randomize_layout:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_randomize_layout,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_randomize_layout:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_randomize_layout,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

RandomizeLayoutAttr::RandomizeLayoutAttr(TreeContext &Ctx,
                                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::RandomizeLayout, false, false) {}

RandomizeLayoutAttr *RandomizeLayoutAttr::clone(TreeContext &C) const {
  auto *A = new (C) RandomizeLayoutAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void RandomizeLayoutAttr::printPretty(raw_ostream &OS,
                                      const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((randomize_layout";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::randomize_layout";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::randomize_layout";
    OS << "]]";
    break;
  }
  }
}

const char *RandomizeLayoutAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "randomize_layout";
  case 1:
    return "randomize_layout";
  case 2:
    return "randomize_layout";
  }
}

// ReadOnlyPlacementAttr implementation

ReadOnlyPlacementAttr *
ReadOnlyPlacementAttr::CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ReadOnlyPlacementAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ReadOnlyPlacementAttr *
ReadOnlyPlacementAttr::Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ReadOnlyPlacementAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ReadOnlyPlacementAttr *ReadOnlyPlacementAttr::CreateImplicit(TreeContext &Ctx,
                                                             SourceRange Range,
                                                             Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_enforce_read_only_placement:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_enforce_read_only_placement,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_enforce_read_only_placement:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_clang_enforce_read_only_placement, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_enforce_read_only_placement:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_neverc_enforce_read_only_placement, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case C23_clang_enforce_read_only_placement:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_enforce_read_only_placement,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_enforce_read_only_placement:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_enforce_read_only_placement,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

ReadOnlyPlacementAttr *
ReadOnlyPlacementAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_enforce_read_only_placement:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_enforce_read_only_placement,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_enforce_read_only_placement:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_clang_enforce_read_only_placement, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_enforce_read_only_placement:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_neverc_enforce_read_only_placement, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case C23_clang_enforce_read_only_placement:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_enforce_read_only_placement,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_enforce_read_only_placement:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_enforce_read_only_placement,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

ReadOnlyPlacementAttr::ReadOnlyPlacementAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::ReadOnlyPlacement, false, false) {}

ReadOnlyPlacementAttr *ReadOnlyPlacementAttr::clone(TreeContext &C) const {
  auto *A = new (C) ReadOnlyPlacementAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ReadOnlyPlacementAttr::printPretty(raw_ostream &OS,
                                        const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((enforce_read_only_placement";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::enforce_read_only_placement";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::enforce_read_only_placement";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::enforce_read_only_placement";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::enforce_read_only_placement";
    OS << "]]";
    break;
  }
  }
}

const char *ReadOnlyPlacementAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "enforce_read_only_placement";
  case 1:
    return "enforce_read_only_placement";
  case 2:
    return "enforce_read_only_placement";
  case 3:
    return "enforce_read_only_placement";
  case 4:
    return "enforce_read_only_placement";
  }
}

// RegCallAttr implementation

RegCallAttr *
RegCallAttr::CreateImplicit(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) RegCallAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

RegCallAttr *RegCallAttr::Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) RegCallAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

RegCallAttr *RegCallAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_regcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_regcall,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_regcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_regcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_regcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_regcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Keyword_regcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_regcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

RegCallAttr *RegCallAttr::Create(TreeContext &Ctx, SourceRange Range,
                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_regcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_regcall,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_regcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_regcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_regcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_regcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Keyword_regcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_regcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

RegCallAttr::RegCallAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::RegCall, false, false) {}

RegCallAttr *RegCallAttr::clone(TreeContext &C) const {
  auto *A = new (C) RegCallAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void RegCallAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((regcall";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::regcall";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::regcall";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __regcall";
    OS << "";
    break;
  }
  }
}

const char *RegCallAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "regcall";
  case 1:
    return "regcall";
  case 2:
    return "regcall";
  case 3:
    return "__regcall";
  }
}

// ReleaseHandleAttr implementation

ReleaseHandleAttr *
ReleaseHandleAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef HandleType,
                                  const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ReleaseHandleAttr(Ctx, CommonInfo, HandleType);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ReleaseHandleAttr *
ReleaseHandleAttr::Create(TreeContext &Ctx, llvm::StringRef HandleType,
                          const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ReleaseHandleAttr(Ctx, CommonInfo, HandleType);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ReleaseHandleAttr *ReleaseHandleAttr::CreateImplicit(TreeContext &Ctx,
                                                     llvm::StringRef HandleType,
                                                     SourceRange Range,
                                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_release_handle:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_release_handle, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_release_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_release_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_release_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_release_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_release_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_release_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_release_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_release_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, HandleType, I);
}

ReleaseHandleAttr *ReleaseHandleAttr::Create(TreeContext &Ctx,
                                             llvm::StringRef HandleType,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_release_handle:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_release_handle, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_release_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_release_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_release_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_release_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_release_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_release_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_release_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_release_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, HandleType, I);
}

ReleaseHandleAttr::ReleaseHandleAttr(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo,
                                     llvm::StringRef HandleType)
    : InheritableParamAttr(Ctx, CommonInfo, attr::ReleaseHandle, false, false),
      handleTypeLength(HandleType.size()),
      handleType(new (Ctx, 1) char[handleTypeLength]) {
  if (!HandleType.empty())
    std::memcpy(handleType, HandleType.data(), handleTypeLength);
}

ReleaseHandleAttr *ReleaseHandleAttr::clone(TreeContext &C) const {
  auto *A = new (C) ReleaseHandleAttr(C, *this, getHandleType());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ReleaseHandleAttr::printPretty(raw_ostream &OS,
                                    const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((release_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::release_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::release_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::release_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::release_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *ReleaseHandleAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "release_handle";
  case 1:
    return "release_handle";
  case 2:
    return "release_handle";
  case 3:
    return "release_handle";
  case 4:
    return "release_handle";
  }
}

// RestrictAttr implementation

RestrictAttr *
RestrictAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) RestrictAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

RestrictAttr *RestrictAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) RestrictAttr(Ctx, CommonInfo);
  return A;
}

RestrictAttr *RestrictAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Declspec_restrict:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_restrict, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_malloc:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_malloc,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_malloc:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_malloc, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_malloc:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_malloc, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

RestrictAttr *RestrictAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Declspec_restrict:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_restrict, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_malloc:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_malloc,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_malloc:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_malloc, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_malloc:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_malloc, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

RestrictAttr::RestrictAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Restrict, false, false) {}

RestrictAttr::Spelling RestrictAttr::getSemanticSpelling() const {
  switch (getAttributeSpellingListIndex()) {
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
RestrictAttr *RestrictAttr::clone(TreeContext &C) const {
  auto *A = new (C) RestrictAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void RestrictAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __declspec(restrict";
    OS << ")";
    break;
  }
  case 1: {
    OS << " __attribute__((malloc";
    OS << "))";
    break;
  }
  case 2: {
    OS << " [[gnu::malloc";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[gnu::malloc";
    OS << "]]";
    break;
  }
  }
}

const char *RestrictAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "restrict";
  case 1:
    return "malloc";
  case 2:
    return "malloc";
  case 3:
    return "malloc";
  }
}

// RetainAttr implementation

RetainAttr *RetainAttr::CreateImplicit(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) RetainAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

RetainAttr *RetainAttr::Create(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) RetainAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

RetainAttr *RetainAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_retain:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_retain,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_retain:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_retain, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_retain:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_retain, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

RetainAttr *RetainAttr::Create(TreeContext &Ctx, SourceRange Range,
                               Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_retain:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_retain,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_retain:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_retain, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_retain:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_retain, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

RetainAttr::RetainAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Retain, false, false) {}

RetainAttr *RetainAttr::clone(TreeContext &C) const {
  auto *A = new (C) RetainAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void RetainAttr::printPretty(raw_ostream &OS,
                             const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((retain";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::retain";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::retain";
    OS << "]]";
    break;
  }
  }
}

const char *RetainAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "retain";
  case 1:
    return "retain";
  case 2:
    return "retain";
  }
}

// ReturnsNonNullAttr implementation

ReturnsNonNullAttr *
ReturnsNonNullAttr::CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ReturnsNonNullAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ReturnsNonNullAttr *
ReturnsNonNullAttr::Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ReturnsNonNullAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ReturnsNonNullAttr *ReturnsNonNullAttr::CreateImplicit(TreeContext &Ctx,
                                                       SourceRange Range,
                                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_returns_nonnull:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_returns_nonnull, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_returns_nonnull:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_returns_nonnull,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_returns_nonnull:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_returns_nonnull,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

ReturnsNonNullAttr *ReturnsNonNullAttr::Create(TreeContext &Ctx,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_returns_nonnull:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_returns_nonnull, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_returns_nonnull:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_returns_nonnull,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_returns_nonnull:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_returns_nonnull,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

ReturnsNonNullAttr::ReturnsNonNullAttr(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::ReturnsNonNull, false, false) {}

ReturnsNonNullAttr *ReturnsNonNullAttr::clone(TreeContext &C) const {
  auto *A = new (C) ReturnsNonNullAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ReturnsNonNullAttr::printPretty(raw_ostream &OS,
                                     const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((returns_nonnull";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::returns_nonnull";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::returns_nonnull";
    OS << "]]";
    break;
  }
  }
}

const char *ReturnsNonNullAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "returns_nonnull";
  case 1:
    return "returns_nonnull";
  case 2:
    return "returns_nonnull";
  }
}

// ReturnsTwiceAttr implementation

ReturnsTwiceAttr *
ReturnsTwiceAttr::CreateImplicit(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ReturnsTwiceAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ReturnsTwiceAttr *
ReturnsTwiceAttr::Create(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ReturnsTwiceAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ReturnsTwiceAttr *ReturnsTwiceAttr::CreateImplicit(TreeContext &Ctx,
                                                   SourceRange Range,
                                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_returns_twice:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_returns_twice, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_returns_twice:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_returns_twice,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_returns_twice:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_returns_twice,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

ReturnsTwiceAttr *ReturnsTwiceAttr::Create(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_returns_twice:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_returns_twice, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_returns_twice:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_returns_twice,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_returns_twice:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_returns_twice,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

ReturnsTwiceAttr::ReturnsTwiceAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::ReturnsTwice, false, false) {}

ReturnsTwiceAttr *ReturnsTwiceAttr::clone(TreeContext &C) const {
  auto *A = new (C) ReturnsTwiceAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ReturnsTwiceAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((returns_twice";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::returns_twice";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::returns_twice";
    OS << "]]";
    break;
  }
  }
}

const char *ReturnsTwiceAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "returns_twice";
  case 1:
    return "returns_twice";
  case 2:
    return "returns_twice";
  }
}

// SPtrAttr implementation

SPtrAttr *SPtrAttr::CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SPtrAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SPtrAttr *SPtrAttr::Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SPtrAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SPtrAttr *SPtrAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

SPtrAttr *SPtrAttr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

SPtrAttr::SPtrAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : TypeAttr(Ctx, CommonInfo, attr::SPtr, false) {}

SPtrAttr *SPtrAttr::clone(TreeContext &C) const {
  auto *A = new (C) SPtrAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void SPtrAttr::printPretty(raw_ostream &OS,
                           const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __sptr";
    OS << "";
    break;
  }
  }
}

const char *SPtrAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "__sptr";
  }
}

// SectionAttr implementation

SectionAttr *
SectionAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SectionAttr(Ctx, CommonInfo, Name);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SectionAttr *SectionAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SectionAttr(Ctx, CommonInfo, Name);
  return A;
}

SectionAttr *SectionAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Name,
                                         SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_section:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_section,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_section:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_section, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_section:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_section, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_allocate:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_allocate, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Name, I);
}

SectionAttr *SectionAttr::Create(TreeContext &Ctx, llvm::StringRef Name,
                                 SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_section:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_section,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_section:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_section, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_section:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_section, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_allocate:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_allocate, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Name, I);
}

SectionAttr::SectionAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo,
                         llvm::StringRef Name)
    : InheritableAttr(Ctx, CommonInfo, attr::Section, false, false),
      nameLength(Name.size()), name(new (Ctx, 1) char[nameLength]) {
  if (!Name.empty())
    std::memcpy(name, Name.data(), nameLength);
}

SectionAttr::Spelling SectionAttr::getSemanticSpelling() const {
  switch (getAttributeSpellingListIndex()) {
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

SectionAttr *SectionAttr::clone(TreeContext &C) const {
  auto *A = new (C) SectionAttr(C, *this, getName());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void SectionAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((section";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::section";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::section";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __declspec(allocate";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getName() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << ")";
    break;
  }
  }
}

const char *SectionAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "section";
  case 1:
    return "section";
  case 2:
    return "section";
  case 3:
    return "allocate";
  }
}

// SelectAnyAttr implementation

SelectAnyAttr *
SelectAnyAttr::CreateImplicit(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SelectAnyAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SelectAnyAttr *SelectAnyAttr::Create(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SelectAnyAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SelectAnyAttr *SelectAnyAttr::CreateImplicit(TreeContext &Ctx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Declspec_selectany:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_selectany, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_selectany:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_selectany, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_selectany:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_selectany,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_selectany:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_selectany, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

SelectAnyAttr *SelectAnyAttr::Create(TreeContext &Ctx, SourceRange Range,
                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Declspec_selectany:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_selectany, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case GNU_selectany:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_selectany, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_selectany:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_selectany,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_selectany:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_selectany, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

SelectAnyAttr::SelectAnyAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::SelectAny, false, false) {}

SelectAnyAttr *SelectAnyAttr::clone(TreeContext &C) const {
  auto *A = new (C) SelectAnyAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void SelectAnyAttr::printPretty(raw_ostream &OS,
                                const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __declspec(selectany";
    OS << ")";
    break;
  }
  case 1: {
    OS << " __attribute__((selectany";
    OS << "))";
    break;
  }
  case 2: {
    OS << " [[gnu::selectany";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[gnu::selectany";
    OS << "]]";
    break;
  }
  }
}

const char *SelectAnyAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "selectany";
  case 1:
    return "selectany";
  case 2:
    return "selectany";
  case 3:
    return "selectany";
  }
}

// SentinelAttr implementation

SentinelAttr *
SentinelAttr::CreateImplicit(TreeContext &Ctx, int Sentinel, int NullPos,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SentinelAttr(Ctx, CommonInfo, Sentinel, NullPos);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SentinelAttr *SentinelAttr::Create(TreeContext &Ctx, int Sentinel, int NullPos,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SentinelAttr(Ctx, CommonInfo, Sentinel, NullPos);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SentinelAttr *SentinelAttr::CreateImplicit(TreeContext &Ctx, int Sentinel,
                                           int NullPos, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_sentinel:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_sentinel, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_sentinel:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_sentinel,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_sentinel:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_sentinel, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Sentinel, NullPos, I);
}

SentinelAttr *SentinelAttr::Create(TreeContext &Ctx, int Sentinel, int NullPos,
                                   SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_sentinel:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_sentinel, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_sentinel:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_sentinel,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_sentinel:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_sentinel, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Sentinel, NullPos, I);
}

SentinelAttr::SentinelAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo, int Sentinel,
                           int NullPos)
    : InheritableAttr(Ctx, CommonInfo, attr::Sentinel, false, false),
      sentinel(Sentinel), nullPos(NullPos) {}

SentinelAttr::SentinelAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Sentinel, false, false),
      sentinel(), nullPos() {}

SentinelAttr *SentinelAttr::clone(TreeContext &C) const {
  auto *A = new (C) SentinelAttr(C, *this, sentinel, nullPos);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void SentinelAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((sentinel";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getSentinel() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getNullPos() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::sentinel";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getSentinel() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getNullPos() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::sentinel";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getSentinel() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getNullPos() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *SentinelAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "sentinel";
  case 1:
    return "sentinel";
  case 2:
    return "sentinel";
  }
}

// SpeculativeLoadHardeningAttr implementation

SpeculativeLoadHardeningAttr *SpeculativeLoadHardeningAttr::CreateImplicit(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SpeculativeLoadHardeningAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SpeculativeLoadHardeningAttr *
SpeculativeLoadHardeningAttr::Create(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SpeculativeLoadHardeningAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SpeculativeLoadHardeningAttr *
SpeculativeLoadHardeningAttr::CreateImplicit(TreeContext &Ctx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_speculative_load_hardening,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_speculative_load_hardening:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_clang_speculative_load_hardening,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_neverc_speculative_load_hardening, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case C23_clang_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_speculative_load_hardening,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_speculative_load_hardening,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

SpeculativeLoadHardeningAttr *
SpeculativeLoadHardeningAttr::Create(TreeContext &Ctx, SourceRange Range,
                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_speculative_load_hardening,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_speculative_load_hardening:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_clang_speculative_load_hardening,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket,
          Bracket_neverc_speculative_load_hardening, false /*IsAlignas*/,
          false /*IsRegularKeywordAttribute*/};
    case C23_clang_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_speculative_load_hardening,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_speculative_load_hardening:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_speculative_load_hardening,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

SpeculativeLoadHardeningAttr::SpeculativeLoadHardeningAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::SpeculativeLoadHardening, false,
                      false) {}

SpeculativeLoadHardeningAttr *
SpeculativeLoadHardeningAttr::clone(TreeContext &C) const {
  auto *A = new (C) SpeculativeLoadHardeningAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void SpeculativeLoadHardeningAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((speculative_load_hardening";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::speculative_load_hardening";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::speculative_load_hardening";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::speculative_load_hardening";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::speculative_load_hardening";
    OS << "]]";
    break;
  }
  }
}

const char *SpeculativeLoadHardeningAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "speculative_load_hardening";
  case 1:
    return "speculative_load_hardening";
  case 2:
    return "speculative_load_hardening";
  case 3:
    return "speculative_load_hardening";
  case 4:
    return "speculative_load_hardening";
  }
}

// StandardNoReturnAttr implementation

StandardNoReturnAttr *
StandardNoReturnAttr::CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) StandardNoReturnAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

StandardNoReturnAttr *
StandardNoReturnAttr::Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) StandardNoReturnAttr(Ctx, CommonInfo);
  return A;
}

StandardNoReturnAttr *StandardNoReturnAttr::CreateImplicit(TreeContext &Ctx,
                                                           SourceRange Range,
                                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_noreturn:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_noreturn, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_noreturn:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_noreturn, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_Noreturn:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_Noreturn, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

StandardNoReturnAttr *
StandardNoReturnAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_noreturn:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_noreturn, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_noreturn:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_noreturn, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_Noreturn:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_Noreturn, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

StandardNoReturnAttr::StandardNoReturnAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::StandardNoReturn, false, false) {}

StandardNoReturnAttr::Spelling
StandardNoReturnAttr::getSemanticSpelling() const {
  switch (getAttributeSpellingListIndex()) {
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
StandardNoReturnAttr *StandardNoReturnAttr::clone(TreeContext &C) const {
  auto *A = new (C) StandardNoReturnAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void StandardNoReturnAttr::printPretty(raw_ostream &OS,
                                       const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " [[noreturn";
    OS << "]]";
    break;
  }
  case 1: {
    OS << " [[noreturn";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[_Noreturn";
    OS << "]]";
    break;
  }
  }
}

const char *StandardNoReturnAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "noreturn";
  case 1:
    return "noreturn";
  case 2:
    return "_Noreturn";
  }
}

// StdCallAttr implementation

StdCallAttr *
StdCallAttr::CreateImplicit(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) StdCallAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

StdCallAttr *StdCallAttr::Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) StdCallAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

StdCallAttr *StdCallAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_stdcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_stdcall,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_stdcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_stdcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_stdcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_stdcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Keyword_stdcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_stdcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

StdCallAttr *StdCallAttr::Create(TreeContext &Ctx, SourceRange Range,
                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_stdcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_stdcall,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_stdcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_stdcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_stdcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_stdcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Keyword_stdcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_stdcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

StdCallAttr::StdCallAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::StdCall, false, false) {}

StdCallAttr *StdCallAttr::clone(TreeContext &C) const {
  auto *A = new (C) StdCallAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void StdCallAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((stdcall";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::stdcall";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::stdcall";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __stdcall";
    OS << "";
    break;
  }
  case 4: {
    OS << " _stdcall";
    OS << "";
    break;
  }
  }
}

const char *StdCallAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "stdcall";
  case 1:
    return "stdcall";
  case 2:
    return "stdcall";
  case 3:
    return "__stdcall";
  case 4:
    return "_stdcall";
  }
}

// StrictFPAttr implementation

StrictFPAttr *
StrictFPAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) StrictFPAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

StrictFPAttr *StrictFPAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) StrictFPAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

StrictFPAttr *StrictFPAttr::CreateImplicit(TreeContext &Ctx,
                                           SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return CreateImplicit(Ctx, I);
}

StrictFPAttr *StrictFPAttr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        AttributeCommonInfo::Form::Implicit());
  return Create(Ctx, I);
}

StrictFPAttr::StrictFPAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::StrictFP, false, false) {}

StrictFPAttr *StrictFPAttr::clone(TreeContext &C) const {
  auto *A = new (C) StrictFPAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void StrictFPAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {}

const char *StrictFPAttr::getSpelling() const { return "(No spelling)"; }

// StrictGuardStackCheckAttr implementation

StrictGuardStackCheckAttr *StrictGuardStackCheckAttr::CreateImplicit(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) StrictGuardStackCheckAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

StrictGuardStackCheckAttr *
StrictGuardStackCheckAttr::Create(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) StrictGuardStackCheckAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

StrictGuardStackCheckAttr *
StrictGuardStackCheckAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Declspec, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

StrictGuardStackCheckAttr *
StrictGuardStackCheckAttr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Declspec, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

StrictGuardStackCheckAttr::StrictGuardStackCheckAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::StrictGuardStackCheck, false,
                      false) {}

StrictGuardStackCheckAttr *
StrictGuardStackCheckAttr::clone(TreeContext &C) const {
  auto *A = new (C) StrictGuardStackCheckAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void StrictGuardStackCheckAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __declspec(strict_gs_check";
    OS << ")";
    break;
  }
  }
}

const char *StrictGuardStackCheckAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "strict_gs_check";
  }
}

// SuppressAttr implementation

SuppressAttr *
SuppressAttr::CreateImplicit(TreeContext &Ctx, StringRef *DiagnosticIdentifiers,
                             unsigned DiagnosticIdentifiersSize,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SuppressAttr(Ctx, CommonInfo, DiagnosticIdentifiers,
                                   DiagnosticIdentifiersSize);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SuppressAttr *SuppressAttr::Create(TreeContext &Ctx,
                                   StringRef *DiagnosticIdentifiers,
                                   unsigned DiagnosticIdentifiersSize,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SuppressAttr(Ctx, CommonInfo, DiagnosticIdentifiers,
                                   DiagnosticIdentifiersSize);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SuppressAttr *SuppressAttr::CreateImplicit(TreeContext &Ctx,
                                           StringRef *DiagnosticIdentifiers,
                                           unsigned DiagnosticIdentifiersSize,
                                           SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_gsl_suppress:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gsl_suppress,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_suppress:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_suppress, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_suppress:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_suppress,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_suppress:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_suppress,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_suppress:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_suppress, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_suppress:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_suppress, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, DiagnosticIdentifiers, DiagnosticIdentifiersSize,
                        I);
}

SuppressAttr *SuppressAttr::Create(TreeContext &Ctx,
                                   StringRef *DiagnosticIdentifiers,
                                   unsigned DiagnosticIdentifiersSize,
                                   SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_gsl_suppress:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gsl_suppress,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_suppress:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_suppress, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_suppress:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_suppress,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_suppress:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_suppress,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_suppress:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_clang_suppress, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_suppress:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_suppress, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, DiagnosticIdentifiers, DiagnosticIdentifiersSize, I);
}

SuppressAttr::SuppressAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo,
                           StringRef *DiagnosticIdentifiers,
                           unsigned DiagnosticIdentifiersSize)
    : DeclOrStmtAttr(Ctx, CommonInfo, attr::Suppress, false, false),
      diagnosticIdentifiers_Size(DiagnosticIdentifiersSize),
      diagnosticIdentifiers_(new (Ctx, 16)
                                 StringRef[diagnosticIdentifiers_Size]) {
  for (size_t I = 0, E = diagnosticIdentifiers_Size; I != E; ++I) {
    StringRef Ref = DiagnosticIdentifiers[I];
    if (!Ref.empty()) {
      char *Mem = new (Ctx, 1) char[Ref.size()];
      std::memcpy(Mem, Ref.data(), Ref.size());
      diagnosticIdentifiers_[I] = StringRef(Mem, Ref.size());
    }
  }
}

SuppressAttr::SuppressAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : DeclOrStmtAttr(Ctx, CommonInfo, attr::Suppress, false, false),
      diagnosticIdentifiers_Size(0), diagnosticIdentifiers_(nullptr) {}

SuppressAttr *SuppressAttr::clone(TreeContext &C) const {
  auto *A = new (C) SuppressAttr(C, *this, diagnosticIdentifiers_,
                                 diagnosticIdentifiers_Size);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void SuppressAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " [[gsl::suppress";
    OS << "";
    for (const auto &Val : diagnosticIdentifiers()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 1: {
    OS << " __attribute__((suppress";
    OS << "";
    for (const auto &Val : diagnosticIdentifiers()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 2: {
    OS << " [[clang::suppress";
    OS << "";
    for (const auto &Val : diagnosticIdentifiers()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[neverc::suppress";
    OS << "";
    for (const auto &Val : diagnosticIdentifiers()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[clang::suppress";
    OS << "";
    for (const auto &Val : diagnosticIdentifiers()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " [[neverc::suppress";
    OS << "";
    for (const auto &Val : diagnosticIdentifiers()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *SuppressAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "suppress";
  case 1:
    return "suppress";
  case 2:
    return "suppress";
  case 3:
    return "suppress";
  case 4:
    return "suppress";
  case 5:
    return "suppress";
  }
}

// SysVABIAttr implementation

SysVABIAttr *
SysVABIAttr::CreateImplicit(TreeContext &Ctx,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SysVABIAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SysVABIAttr *SysVABIAttr::Create(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) SysVABIAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

SysVABIAttr *SysVABIAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                         Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_sysv_abi:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_sysv_abi, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_sysv_abi:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_sysv_abi,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_sysv_abi:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_sysv_abi, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

SysVABIAttr *SysVABIAttr::Create(TreeContext &Ctx, SourceRange Range,
                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_sysv_abi:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_sysv_abi, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_sysv_abi:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_sysv_abi,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_sysv_abi:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_sysv_abi, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

SysVABIAttr::SysVABIAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::SysVABI, false, false) {}

SysVABIAttr *SysVABIAttr::clone(TreeContext &C) const {
  auto *A = new (C) SysVABIAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void SysVABIAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((sysv_abi";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::sysv_abi";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::sysv_abi";
    OS << "]]";
    break;
  }
  }
}

const char *SysVABIAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "sysv_abi";
  case 1:
    return "sysv_abi";
  case 2:
    return "sysv_abi";
  }
}

// TLSModelAttr implementation

TLSModelAttr *
TLSModelAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Model,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TLSModelAttr(Ctx, CommonInfo, Model);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TLSModelAttr *TLSModelAttr::Create(TreeContext &Ctx, llvm::StringRef Model,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TLSModelAttr(Ctx, CommonInfo, Model);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TLSModelAttr *TLSModelAttr::CreateImplicit(TreeContext &Ctx,
                                           llvm::StringRef Model,
                                           SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_tls_model:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_tls_model, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_tls_model:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_tls_model,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_tls_model:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_tls_model, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Model, I);
}

TLSModelAttr *TLSModelAttr::Create(TreeContext &Ctx, llvm::StringRef Model,
                                   SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_tls_model:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_tls_model, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_tls_model:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_tls_model,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_tls_model:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_tls_model, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Model, I);
}

TLSModelAttr::TLSModelAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo,
                           llvm::StringRef Model)
    : InheritableAttr(Ctx, CommonInfo, attr::TLSModel, false, false),
      modelLength(Model.size()), model(new (Ctx, 1) char[modelLength]) {
  if (!Model.empty())
    std::memcpy(model, Model.data(), modelLength);
}

TLSModelAttr *TLSModelAttr::clone(TreeContext &C) const {
  auto *A = new (C) TLSModelAttr(C, *this, getModel());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void TLSModelAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((tls_model";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getModel() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::tls_model";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getModel() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::tls_model";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getModel() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *TLSModelAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "tls_model";
  case 1:
    return "tls_model";
  case 2:
    return "tls_model";
  }
}

// TargetAttr implementation

TargetAttr *TargetAttr::CreateImplicit(TreeContext &Ctx,
                                       llvm::StringRef FeaturesStr,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TargetAttr(Ctx, CommonInfo, FeaturesStr);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TargetAttr *TargetAttr::Create(TreeContext &Ctx, llvm::StringRef FeaturesStr,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TargetAttr(Ctx, CommonInfo, FeaturesStr);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TargetAttr *TargetAttr::CreateImplicit(TreeContext &Ctx,
                                       llvm::StringRef FeaturesStr,
                                       SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_target:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_target,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_target:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_target, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_target:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_target, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, FeaturesStr, I);
}

TargetAttr *TargetAttr::Create(TreeContext &Ctx, llvm::StringRef FeaturesStr,
                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_target:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_target,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_target:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_target, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_target:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_target, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, FeaturesStr, I);
}

TargetAttr::TargetAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
                       llvm::StringRef FeaturesStr)
    : InheritableAttr(Ctx, CommonInfo, attr::Target, false, false),
      featuresStrLength(FeaturesStr.size()),
      featuresStr(new (Ctx, 1) char[featuresStrLength]) {
  if (!FeaturesStr.empty())
    std::memcpy(featuresStr, FeaturesStr.data(), featuresStrLength);
}

TargetAttr *TargetAttr::clone(TreeContext &C) const {
  auto *A = new (C) TargetAttr(C, *this, getFeaturesStr());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void TargetAttr::printPretty(raw_ostream &OS,
                             const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((target";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getFeaturesStr() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::target";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getFeaturesStr() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::target";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getFeaturesStr() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *TargetAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "target";
  case 1:
    return "target";
  case 2:
    return "target";
  }
}

// TargetClonesAttr implementation

TargetClonesAttr *
TargetClonesAttr::CreateImplicit(TreeContext &Ctx, StringRef *FeaturesStrs,
                                 unsigned FeaturesStrsSize,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx)
      TargetClonesAttr(Ctx, CommonInfo, FeaturesStrs, FeaturesStrsSize);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TargetClonesAttr *
TargetClonesAttr::Create(TreeContext &Ctx, StringRef *FeaturesStrs,
                         unsigned FeaturesStrsSize,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx)
      TargetClonesAttr(Ctx, CommonInfo, FeaturesStrs, FeaturesStrsSize);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TargetClonesAttr *TargetClonesAttr::CreateImplicit(TreeContext &Ctx,
                                                   StringRef *FeaturesStrs,
                                                   unsigned FeaturesStrsSize,
                                                   SourceRange Range,
                                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_target_clones:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_target_clones, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_target_clones:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_target_clones,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_target_clones:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_target_clones,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, FeaturesStrs, FeaturesStrsSize, I);
}

TargetClonesAttr *TargetClonesAttr::Create(TreeContext &Ctx,
                                           StringRef *FeaturesStrs,
                                           unsigned FeaturesStrsSize,
                                           SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_target_clones:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_target_clones, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_target_clones:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_target_clones,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_target_clones:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_target_clones,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, FeaturesStrs, FeaturesStrsSize, I);
}

TargetClonesAttr::TargetClonesAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo,
                                   StringRef *FeaturesStrs,
                                   unsigned FeaturesStrsSize)
    : InheritableAttr(Ctx, CommonInfo, attr::TargetClones, false, false),
      featuresStrs_Size(FeaturesStrsSize),
      featuresStrs_(new (Ctx, 16) StringRef[featuresStrs_Size]) {
  for (size_t I = 0, E = featuresStrs_Size; I != E; ++I) {
    StringRef Ref = FeaturesStrs[I];
    if (!Ref.empty()) {
      char *Mem = new (Ctx, 1) char[Ref.size()];
      std::memcpy(Mem, Ref.data(), Ref.size());
      featuresStrs_[I] = StringRef(Mem, Ref.size());
    }
  }
}

TargetClonesAttr::TargetClonesAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::TargetClones, false, false),
      featuresStrs_Size(0), featuresStrs_(nullptr) {}

TargetClonesAttr *TargetClonesAttr::clone(TreeContext &C) const {
  auto *A =
      new (C) TargetClonesAttr(C, *this, featuresStrs_, featuresStrs_Size);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void TargetClonesAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((target_clones";
    OS << "";
    for (const auto &Val : featuresStrs()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::target_clones";
    OS << "";
    for (const auto &Val : featuresStrs()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::target_clones";
    OS << "";
    for (const auto &Val : featuresStrs()) {
      DelimitAttributeArgument(OS, IsFirstArgument);
      OS << "\"" << Val << "\"";
    }
    OS << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *TargetClonesAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "target_clones";
  case 1:
    return "target_clones";
  case 2:
    return "target_clones";
  }
}

// TargetVersionAttr implementation

TargetVersionAttr *
TargetVersionAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef NamesStr,
                                  const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TargetVersionAttr(Ctx, CommonInfo, NamesStr);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TargetVersionAttr *
TargetVersionAttr::Create(TreeContext &Ctx, llvm::StringRef NamesStr,
                          const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TargetVersionAttr(Ctx, CommonInfo, NamesStr);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TargetVersionAttr *TargetVersionAttr::CreateImplicit(TreeContext &Ctx,
                                                     llvm::StringRef NamesStr,
                                                     SourceRange Range,
                                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_target_version:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_target_version, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_target_version:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_target_version,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_target_version:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_target_version,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, NamesStr, I);
}

TargetVersionAttr *TargetVersionAttr::Create(TreeContext &Ctx,
                                             llvm::StringRef NamesStr,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_target_version:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_target_version, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_target_version:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_target_version,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_target_version:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_target_version,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, NamesStr, I);
}

TargetVersionAttr::TargetVersionAttr(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo,
                                     llvm::StringRef NamesStr)
    : InheritableAttr(Ctx, CommonInfo, attr::TargetVersion, false, false),
      namesStrLength(NamesStr.size()),
      namesStr(new (Ctx, 1) char[namesStrLength]) {
  if (!NamesStr.empty())
    std::memcpy(namesStr, NamesStr.data(), namesStrLength);
}

TargetVersionAttr *TargetVersionAttr::clone(TreeContext &C) const {
  auto *A = new (C) TargetVersionAttr(C, *this, getNamesStr());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void TargetVersionAttr::printPretty(raw_ostream &OS,
                                    const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((target_version";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getNamesStr() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::target_version";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getNamesStr() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::target_version";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getNamesStr() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *TargetVersionAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "target_version";
  case 1:
    return "target_version";
  case 2:
    return "target_version";
  }
}

// ThreadAttr implementation

ThreadAttr *ThreadAttr::CreateImplicit(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ThreadAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ThreadAttr *ThreadAttr::Create(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ThreadAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ThreadAttr *ThreadAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Declspec, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

ThreadAttr *ThreadAttr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Declspec, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

ThreadAttr::ThreadAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : Attr(Ctx, CommonInfo, attr::Thread, false) {}

ThreadAttr *ThreadAttr::clone(TreeContext &C) const {
  auto *A = new (C) ThreadAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ThreadAttr::printPretty(raw_ostream &OS,
                             const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __declspec(thread";
    OS << ")";
    break;
  }
  }
}

const char *ThreadAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "thread";
  }
}

// TransparentUnionAttr implementation

TransparentUnionAttr *
TransparentUnionAttr::CreateImplicit(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TransparentUnionAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TransparentUnionAttr *
TransparentUnionAttr::Create(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TransparentUnionAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TransparentUnionAttr *TransparentUnionAttr::CreateImplicit(TreeContext &Ctx,
                                                           SourceRange Range,
                                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_transparent_union:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_transparent_union,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_transparent_union:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_transparent_union,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_transparent_union:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_transparent_union,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

TransparentUnionAttr *
TransparentUnionAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_transparent_union:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_transparent_union,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_transparent_union:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_transparent_union,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_transparent_union:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_transparent_union,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

TransparentUnionAttr::TransparentUnionAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::TransparentUnion, false, false) {}

TransparentUnionAttr *TransparentUnionAttr::clone(TreeContext &C) const {
  auto *A = new (C) TransparentUnionAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void TransparentUnionAttr::printPretty(raw_ostream &OS,
                                       const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((transparent_union";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::transparent_union";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::transparent_union";
    OS << "]]";
    break;
  }
  }
}

const char *TransparentUnionAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "transparent_union";
  case 1:
    return "transparent_union";
  case 2:
    return "transparent_union";
  }
}

// TypeNonNullAttr implementation

TypeNonNullAttr *
TypeNonNullAttr::CreateImplicit(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TypeNonNullAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TypeNonNullAttr *
TypeNonNullAttr::Create(TreeContext &Ctx,
                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TypeNonNullAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TypeNonNullAttr *TypeNonNullAttr::CreateImplicit(TreeContext &Ctx,
                                                 SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

TypeNonNullAttr *TypeNonNullAttr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

TypeNonNullAttr::TypeNonNullAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo)
    : TypeAttr(Ctx, CommonInfo, attr::TypeNonNull, false) {}

TypeNonNullAttr *TypeNonNullAttr::clone(TreeContext &C) const {
  auto *A = new (C) TypeNonNullAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void TypeNonNullAttr::printPretty(raw_ostream &OS,
                                  const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " _Nonnull";
    OS << "";
    break;
  }
  }
}

const char *TypeNonNullAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "_Nonnull";
  }
}

// TypeNullUnspecifiedAttr implementation

TypeNullUnspecifiedAttr *
TypeNullUnspecifiedAttr::CreateImplicit(TreeContext &Ctx,
                                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TypeNullUnspecifiedAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TypeNullUnspecifiedAttr *
TypeNullUnspecifiedAttr::Create(TreeContext &Ctx,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TypeNullUnspecifiedAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TypeNullUnspecifiedAttr *
TypeNullUnspecifiedAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

TypeNullUnspecifiedAttr *TypeNullUnspecifiedAttr::Create(TreeContext &Ctx,
                                                         SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

TypeNullUnspecifiedAttr::TypeNullUnspecifiedAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : TypeAttr(Ctx, CommonInfo, attr::TypeNullUnspecified, false) {}

TypeNullUnspecifiedAttr *TypeNullUnspecifiedAttr::clone(TreeContext &C) const {
  auto *A = new (C) TypeNullUnspecifiedAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void TypeNullUnspecifiedAttr::printPretty(raw_ostream &OS,
                                          const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " _Null_unspecified";
    OS << "";
    break;
  }
  }
}

const char *TypeNullUnspecifiedAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "_Null_unspecified";
  }
}

// TypeNullableAttr implementation

TypeNullableAttr *
TypeNullableAttr::CreateImplicit(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TypeNullableAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TypeNullableAttr *
TypeNullableAttr::Create(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TypeNullableAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TypeNullableAttr *TypeNullableAttr::CreateImplicit(TreeContext &Ctx,
                                                   SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

TypeNullableAttr *TypeNullableAttr::Create(TreeContext &Ctx,
                                           SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

TypeNullableAttr::TypeNullableAttr(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo)
    : TypeAttr(Ctx, CommonInfo, attr::TypeNullable, false) {}

TypeNullableAttr *TypeNullableAttr::clone(TreeContext &C) const {
  auto *A = new (C) TypeNullableAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void TypeNullableAttr::printPretty(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " _Nullable";
    OS << "";
    break;
  }
  }
}

const char *TypeNullableAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "_Nullable";
  }
}

// TypeTagForDatatypeAttr implementation

TypeTagForDatatypeAttr *TypeTagForDatatypeAttr::CreateImplicit(
    TreeContext &Ctx, IdentifierInfo *ArgumentKind,
    TypeSourceInfo *MatchingCType, bool LayoutCompatible, bool MustBeNull,
    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx)
      TypeTagForDatatypeAttr(Ctx, CommonInfo, ArgumentKind, MatchingCType,
                             LayoutCompatible, MustBeNull);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TypeTagForDatatypeAttr *
TypeTagForDatatypeAttr::Create(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
                               TypeSourceInfo *MatchingCType,
                               bool LayoutCompatible, bool MustBeNull,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx)
      TypeTagForDatatypeAttr(Ctx, CommonInfo, ArgumentKind, MatchingCType,
                             LayoutCompatible, MustBeNull);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TypeTagForDatatypeAttr *TypeTagForDatatypeAttr::CreateImplicit(
    TreeContext &Ctx, IdentifierInfo *ArgumentKind,
    TypeSourceInfo *MatchingCType, bool LayoutCompatible, bool MustBeNull,
    SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_type_tag_for_datatype:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_type_tag_for_datatype,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_type_tag_for_datatype:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_type_tag_for_datatype,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_type_tag_for_datatype:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_type_tag_for_datatype,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_type_tag_for_datatype:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_type_tag_for_datatype,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_type_tag_for_datatype:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_type_tag_for_datatype,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, ArgumentKind, MatchingCType, LayoutCompatible,
                        MustBeNull, I);
}

TypeTagForDatatypeAttr *
TypeTagForDatatypeAttr::Create(TreeContext &Ctx, IdentifierInfo *ArgumentKind,
                               TypeSourceInfo *MatchingCType,
                               bool LayoutCompatible, bool MustBeNull,
                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_type_tag_for_datatype:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_type_tag_for_datatype,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_type_tag_for_datatype:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_type_tag_for_datatype,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_type_tag_for_datatype:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_type_tag_for_datatype,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_type_tag_for_datatype:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_type_tag_for_datatype,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_type_tag_for_datatype:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_type_tag_for_datatype,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, ArgumentKind, MatchingCType, LayoutCompatible, MustBeNull,
                I);
}

TypeTagForDatatypeAttr::TypeTagForDatatypeAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    IdentifierInfo *ArgumentKind, TypeSourceInfo *MatchingCType,
    bool LayoutCompatible, bool MustBeNull)
    : InheritableAttr(Ctx, CommonInfo, attr::TypeTagForDatatype, false, false),
      argumentKind(ArgumentKind), matchingCType(MatchingCType),
      layoutCompatible(LayoutCompatible), mustBeNull(MustBeNull) {}

TypeTagForDatatypeAttr *TypeTagForDatatypeAttr::clone(TreeContext &C) const {
  auto *A = new (C) TypeTagForDatatypeAttr(
      C, *this, argumentKind, matchingCType, layoutCompatible, mustBeNull);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void TypeTagForDatatypeAttr::printPretty(raw_ostream &OS,
                                         const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((type_tag_for_datatype";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getMatchingCType().getAsString() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getLayoutCompatible() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getMustBeNull() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::type_tag_for_datatype";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getMatchingCType().getAsString() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getLayoutCompatible() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getMustBeNull() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::type_tag_for_datatype";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getMatchingCType().getAsString() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getLayoutCompatible() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getMustBeNull() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::type_tag_for_datatype";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getMatchingCType().getAsString() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getLayoutCompatible() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getMustBeNull() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::type_tag_for_datatype";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << (getArgumentKind() ? getArgumentKind()->getName() : "") << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getMatchingCType().getAsString() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getLayoutCompatible() << "";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "" << getMustBeNull() << "";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *TypeTagForDatatypeAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "type_tag_for_datatype";
  case 1:
    return "type_tag_for_datatype";
  case 2:
    return "type_tag_for_datatype";
  case 3:
    return "type_tag_for_datatype";
  case 4:
    return "type_tag_for_datatype";
  }
}

// TypeVisibilityAttr implementation

TypeVisibilityAttr *TypeVisibilityAttr::CreateImplicit(
    TreeContext &Ctx, TypeVisibilityAttr::VisibilityType Visibility,
    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TypeVisibilityAttr(Ctx, CommonInfo, Visibility);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TypeVisibilityAttr *
TypeVisibilityAttr::Create(TreeContext &Ctx,
                           TypeVisibilityAttr::VisibilityType Visibility,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) TypeVisibilityAttr(Ctx, CommonInfo, Visibility);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

TypeVisibilityAttr *TypeVisibilityAttr::CreateImplicit(
    TreeContext &Ctx, TypeVisibilityAttr::VisibilityType Visibility,
    SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_type_visibility:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_type_visibility, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_type_visibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_type_visibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_type_visibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_type_visibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_type_visibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_type_visibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_type_visibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_type_visibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Visibility, I);
}

TypeVisibilityAttr *
TypeVisibilityAttr::Create(TreeContext &Ctx,
                           TypeVisibilityAttr::VisibilityType Visibility,
                           SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_type_visibility:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_type_visibility, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_type_visibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_type_visibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_type_visibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_type_visibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_type_visibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_type_visibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_type_visibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_type_visibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Visibility, I);
}

TypeVisibilityAttr::TypeVisibilityAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    TypeVisibilityAttr::VisibilityType Visibility)
    : InheritableAttr(Ctx, CommonInfo, attr::TypeVisibility, false, false),
      visibility(Visibility) {}

bool TypeVisibilityAttr::ConvertStrToVisibilityType(
    StringRef Val, TypeVisibilityAttr::VisibilityType &Out) {
  std::optional<TypeVisibilityAttr::VisibilityType> R =
      llvm::StringSwitch<std::optional<TypeVisibilityAttr::VisibilityType>>(Val)
          .Case("default", TypeVisibilityAttr::VisibilityType::Default)
          .Case("hidden", TypeVisibilityAttr::VisibilityType::Hidden)
          .Case("internal", TypeVisibilityAttr::VisibilityType::Hidden)
          .Case("protected", TypeVisibilityAttr::VisibilityType::Protected)
          .Default(std::optional<TypeVisibilityAttr::VisibilityType>());
  if (R) {
    Out = *R;
    return true;
  }
  return false;
}

const char *TypeVisibilityAttr::ConvertVisibilityTypeToStr(
    TypeVisibilityAttr::VisibilityType Val) {
  switch (Val) {
  case TypeVisibilityAttr::VisibilityType::Default:
    return "default";
  case TypeVisibilityAttr::VisibilityType::Hidden:
    return "hidden";
  case TypeVisibilityAttr::VisibilityType::Protected:
    return "protected";
  }
  llvm_unreachable("No enumerator with that value");
}
TypeVisibilityAttr *TypeVisibilityAttr::clone(TreeContext &C) const {
  auto *A = new (C) TypeVisibilityAttr(C, *this, visibility);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void TypeVisibilityAttr::printPretty(raw_ostream &OS,
                                     const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((type_visibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\""
       << TypeVisibilityAttr::ConvertVisibilityTypeToStr(getVisibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::type_visibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\""
       << TypeVisibilityAttr::ConvertVisibilityTypeToStr(getVisibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::type_visibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\""
       << TypeVisibilityAttr::ConvertVisibilityTypeToStr(getVisibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::type_visibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\""
       << TypeVisibilityAttr::ConvertVisibilityTypeToStr(getVisibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::type_visibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\""
       << TypeVisibilityAttr::ConvertVisibilityTypeToStr(getVisibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *TypeVisibilityAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "type_visibility";
  case 1:
    return "type_visibility";
  case 2:
    return "type_visibility";
  case 3:
    return "type_visibility";
  case 4:
    return "type_visibility";
  }
}

// UPtrAttr implementation

UPtrAttr *UPtrAttr::CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UPtrAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UPtrAttr *UPtrAttr::Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UPtrAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UPtrAttr *UPtrAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return CreateImplicit(Ctx, I);
}

UPtrAttr *UPtrAttr::Create(TreeContext &Ctx, SourceRange Range) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute,
                        {AttributeCommonInfo::AS_Keyword, 0,
                         false /*IsAlignas*/,
                         false /*IsRegularKeywordAttribute*/});
  return Create(Ctx, I);
}

UPtrAttr::UPtrAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : TypeAttr(Ctx, CommonInfo, attr::UPtr, false) {}

UPtrAttr *UPtrAttr::clone(TreeContext &C) const {
  auto *A = new (C) UPtrAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void UPtrAttr::printPretty(raw_ostream &OS,
                           const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __uptr";
    OS << "";
    break;
  }
  }
}

const char *UPtrAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "__uptr";
  }
}

// UnavailableAttr implementation

UnavailableAttr *
UnavailableAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Message,
                                UnavailableAttr::ImplicitReason ImplicitReason,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UnavailableAttr(Ctx, CommonInfo, Message, ImplicitReason);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UnavailableAttr *
UnavailableAttr::Create(TreeContext &Ctx, llvm::StringRef Message,
                        UnavailableAttr::ImplicitReason ImplicitReason,
                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UnavailableAttr(Ctx, CommonInfo, Message, ImplicitReason);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UnavailableAttr *
UnavailableAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Message,
                                UnavailableAttr::ImplicitReason ImplicitReason,
                                SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_unavailable:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_unavailable, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Message, ImplicitReason, I);
}

UnavailableAttr *
UnavailableAttr::Create(TreeContext &Ctx, llvm::StringRef Message,
                        UnavailableAttr::ImplicitReason ImplicitReason,
                        SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_unavailable:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_unavailable, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Message, ImplicitReason, I);
}

UnavailableAttr *
UnavailableAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Message,
                                const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UnavailableAttr(Ctx, CommonInfo, Message);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UnavailableAttr *
UnavailableAttr::Create(TreeContext &Ctx, llvm::StringRef Message,
                        const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UnavailableAttr(Ctx, CommonInfo, Message);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UnavailableAttr *UnavailableAttr::CreateImplicit(TreeContext &Ctx,
                                                 llvm::StringRef Message,
                                                 SourceRange Range,
                                                 Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_unavailable:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_unavailable, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Message, I);
}

UnavailableAttr *UnavailableAttr::Create(TreeContext &Ctx,
                                         llvm::StringRef Message,
                                         SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_unavailable:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_unavailable, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_unavailable:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_unavailable,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Message, I);
}

UnavailableAttr::UnavailableAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo,
                                 llvm::StringRef Message,
                                 UnavailableAttr::ImplicitReason ImplicitReason)
    : InheritableAttr(Ctx, CommonInfo, attr::Unavailable, false, false),
      messageLength(Message.size()), message(new (Ctx, 1) char[messageLength]),
      implicitReason(ImplicitReason) {
  if (!Message.empty())
    std::memcpy(message, Message.data(), messageLength);
}

UnavailableAttr::UnavailableAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo,
                                 llvm::StringRef Message)
    : InheritableAttr(Ctx, CommonInfo, attr::Unavailable, false, false),
      messageLength(Message.size()), message(new (Ctx, 1) char[messageLength]),
      implicitReason(UnavailableAttr::ImplicitReason(0)) {
  if (!Message.empty())
    std::memcpy(message, Message.data(), messageLength);
}

UnavailableAttr::UnavailableAttr(TreeContext &Ctx,
                                 const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Unavailable, false, false),
      messageLength(0), message(nullptr),
      implicitReason(UnavailableAttr::ImplicitReason(0)) {}

UnavailableAttr *UnavailableAttr::clone(TreeContext &C) const {
  auto *A = new (C) UnavailableAttr(C, *this, getMessage(), implicitReason);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void UnavailableAttr::printPretty(raw_ostream &OS,
                                  const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((unavailable";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::unavailable";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::unavailable";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::unavailable";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::unavailable";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *UnavailableAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "unavailable";
  case 1:
    return "unavailable";
  case 2:
    return "unavailable";
  case 3:
    return "unavailable";
  case 4:
    return "unavailable";
  }
}

// UninitializedAttr implementation

UninitializedAttr *
UninitializedAttr::CreateImplicit(TreeContext &Ctx,
                                  const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UninitializedAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UninitializedAttr *
UninitializedAttr::Create(TreeContext &Ctx,
                          const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UninitializedAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UninitializedAttr *UninitializedAttr::CreateImplicit(TreeContext &Ctx,
                                                     SourceRange Range,
                                                     Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_uninitialized:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_uninitialized, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

UninitializedAttr *UninitializedAttr::Create(TreeContext &Ctx,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_uninitialized:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_uninitialized, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_uninitialized:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_uninitialized,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

UninitializedAttr::UninitializedAttr(TreeContext &Ctx,
                                     const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Uninitialized, false, false) {}

UninitializedAttr *UninitializedAttr::clone(TreeContext &C) const {
  auto *A = new (C) UninitializedAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void UninitializedAttr::printPretty(raw_ostream &OS,
                                    const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((uninitialized";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::uninitialized";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::uninitialized";
    OS << "]]";
    break;
  }
  }
}

const char *UninitializedAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "uninitialized";
  case 1:
    return "uninitialized";
  case 2:
    return "uninitialized";
  }
}

// UnlikelyAttr implementation

UnlikelyAttr *
UnlikelyAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UnlikelyAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UnlikelyAttr *UnlikelyAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UnlikelyAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UnlikelyAttr *UnlikelyAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_unlikely:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_unlikely, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_unlikely:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_unlikely, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

UnlikelyAttr *UnlikelyAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_unlikely:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_unlikely, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_neverc_unlikely:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_neverc_unlikely, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

UnlikelyAttr::UnlikelyAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : StmtAttr(Ctx, CommonInfo, attr::Unlikely, false) {}

UnlikelyAttr *UnlikelyAttr::clone(TreeContext &C) const {
  auto *A = new (C) UnlikelyAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void UnlikelyAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " [[unlikely";
    OS << "]]";
    break;
  }
  case 1: {
    OS << " [[neverc::unlikely";
    OS << "]]";
    break;
  }
  }
}

const char *UnlikelyAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "unlikely";
  case 1:
    return "unlikely";
  }
}

// UnsafeBufferUsageAttr implementation

UnsafeBufferUsageAttr *
UnsafeBufferUsageAttr::CreateImplicit(TreeContext &Ctx,
                                      const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UnsafeBufferUsageAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UnsafeBufferUsageAttr *
UnsafeBufferUsageAttr::Create(TreeContext &Ctx,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UnsafeBufferUsageAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UnsafeBufferUsageAttr *UnsafeBufferUsageAttr::CreateImplicit(TreeContext &Ctx,
                                                             SourceRange Range,
                                                             Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_unsafe_buffer_usage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_unsafe_buffer_usage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_unsafe_buffer_usage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_unsafe_buffer_usage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_unsafe_buffer_usage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_unsafe_buffer_usage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_unsafe_buffer_usage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_unsafe_buffer_usage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_unsafe_buffer_usage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_unsafe_buffer_usage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

UnsafeBufferUsageAttr *
UnsafeBufferUsageAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_unsafe_buffer_usage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_unsafe_buffer_usage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_unsafe_buffer_usage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_unsafe_buffer_usage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_unsafe_buffer_usage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_unsafe_buffer_usage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_unsafe_buffer_usage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_unsafe_buffer_usage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_unsafe_buffer_usage:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_unsafe_buffer_usage,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

UnsafeBufferUsageAttr::UnsafeBufferUsageAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::UnsafeBufferUsage, false, false) {}

UnsafeBufferUsageAttr *UnsafeBufferUsageAttr::clone(TreeContext &C) const {
  auto *A = new (C) UnsafeBufferUsageAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void UnsafeBufferUsageAttr::printPretty(raw_ostream &OS,
                                        const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((unsafe_buffer_usage";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::unsafe_buffer_usage";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::unsafe_buffer_usage";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::unsafe_buffer_usage";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::unsafe_buffer_usage";
    OS << "]]";
    break;
  }
  }
}

const char *UnsafeBufferUsageAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "unsafe_buffer_usage";
  case 1:
    return "unsafe_buffer_usage";
  case 2:
    return "unsafe_buffer_usage";
  case 3:
    return "unsafe_buffer_usage";
  case 4:
    return "unsafe_buffer_usage";
  }
}

// UnusedAttr implementation

UnusedAttr *UnusedAttr::CreateImplicit(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UnusedAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UnusedAttr *UnusedAttr::Create(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UnusedAttr(Ctx, CommonInfo);
  return A;
}

UnusedAttr *UnusedAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_maybe_unused:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_maybe_unused,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_unused:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_unused,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_unused:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_unused, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_unused:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_unused, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_maybe_unused:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_maybe_unused, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

UnusedAttr *UnusedAttr::Create(TreeContext &Ctx, SourceRange Range,
                               Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_maybe_unused:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_maybe_unused,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_unused:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_unused,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_unused:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_unused, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_unused:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_unused, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_maybe_unused:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_maybe_unused, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

UnusedAttr::UnusedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Unused, false, false) {}

UnusedAttr::Spelling UnusedAttr::getSemanticSpelling() const {
  switch (getAttributeSpellingListIndex()) {
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
UnusedAttr *UnusedAttr::clone(TreeContext &C) const {
  auto *A = new (C) UnusedAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void UnusedAttr::printPretty(raw_ostream &OS,
                             const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " [[maybe_unused";
    OS << "]]";
    break;
  }
  case 1: {
    OS << " __attribute__((unused";
    OS << "))";
    break;
  }
  case 2: {
    OS << " [[gnu::unused";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[gnu::unused";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[maybe_unused";
    OS << "]]";
    break;
  }
  }
}

const char *UnusedAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "maybe_unused";
  case 1:
    return "unused";
  case 2:
    return "unused";
  case 3:
    return "unused";
  case 4:
    return "maybe_unused";
  }
}

// UseHandleAttr implementation

UseHandleAttr *
UseHandleAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef HandleType,
                              const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UseHandleAttr(Ctx, CommonInfo, HandleType);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UseHandleAttr *UseHandleAttr::Create(TreeContext &Ctx,
                                     llvm::StringRef HandleType,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UseHandleAttr(Ctx, CommonInfo, HandleType);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UseHandleAttr *UseHandleAttr::CreateImplicit(TreeContext &Ctx,
                                             llvm::StringRef HandleType,
                                             SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_use_handle:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_use_handle, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_use_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_use_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_use_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_use_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_use_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_use_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_use_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_use_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, HandleType, I);
}

UseHandleAttr *UseHandleAttr::Create(TreeContext &Ctx,
                                     llvm::StringRef HandleType,
                                     SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_use_handle:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_use_handle, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_use_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_use_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_use_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_use_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_use_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_use_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_use_handle:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_use_handle,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, HandleType, I);
}

UseHandleAttr::UseHandleAttr(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo,
                             llvm::StringRef HandleType)
    : InheritableParamAttr(Ctx, CommonInfo, attr::UseHandle, false, false),
      handleTypeLength(HandleType.size()),
      handleType(new (Ctx, 1) char[handleTypeLength]) {
  if (!HandleType.empty())
    std::memcpy(handleType, HandleType.data(), handleTypeLength);
}

UseHandleAttr *UseHandleAttr::clone(TreeContext &C) const {
  auto *A = new (C) UseHandleAttr(C, *this, getHandleType());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void UseHandleAttr::printPretty(raw_ostream &OS,
                                const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((use_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::use_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::use_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::use_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::use_handle";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getHandleType() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *UseHandleAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "use_handle";
  case 1:
    return "use_handle";
  case 2:
    return "use_handle";
  case 3:
    return "use_handle";
  case 4:
    return "use_handle";
  }
}

// UsedAttr implementation

UsedAttr *UsedAttr::CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UsedAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UsedAttr *UsedAttr::Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) UsedAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

UsedAttr *UsedAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_used:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_used,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_used:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_used, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_used:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_used, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

UsedAttr *UsedAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_used:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_used,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_used:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_used, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_used:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_used, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

UsedAttr::UsedAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Used, false, false) {}

UsedAttr *UsedAttr::clone(TreeContext &C) const {
  auto *A = new (C) UsedAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void UsedAttr::printPretty(raw_ostream &OS,
                           const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((used";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::used";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::used";
    OS << "]]";
    break;
  }
  }
}

const char *UsedAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "used";
  case 1:
    return "used";
  case 2:
    return "used";
  }
}

// VectorCallAttr implementation

VectorCallAttr *
VectorCallAttr::CreateImplicit(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) VectorCallAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

VectorCallAttr *VectorCallAttr::Create(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) VectorCallAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

VectorCallAttr *VectorCallAttr::CreateImplicit(TreeContext &Ctx,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_vectorcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_vectorcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_vectorcall:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_vectorcall,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_vectorcall:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_vectorcall,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_vectorcall:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_vectorcall,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_vectorcall:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_vectorcall,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Keyword_vectorcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_vectorcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

VectorCallAttr *VectorCallAttr::Create(TreeContext &Ctx, SourceRange Range,
                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_vectorcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_vectorcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_vectorcall:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_vectorcall,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_vectorcall:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_vectorcall,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_vectorcall:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_vectorcall,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_vectorcall:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_vectorcall,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Keyword_vectorcall:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Keyword,
                                       Keyword_vectorcall, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

VectorCallAttr::VectorCallAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::VectorCall, false, false) {}

VectorCallAttr *VectorCallAttr::clone(TreeContext &C) const {
  auto *A = new (C) VectorCallAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void VectorCallAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((vectorcall";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::vectorcall";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::vectorcall";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::vectorcall";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::vectorcall";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " __vectorcall";
    OS << "";
    break;
  }
  case 6: {
    OS << " _vectorcall";
    OS << "";
    break;
  }
  }
}

const char *VectorCallAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "vectorcall";
  case 1:
    return "vectorcall";
  case 2:
    return "vectorcall";
  case 3:
    return "vectorcall";
  case 4:
    return "vectorcall";
  case 5:
    return "__vectorcall";
  case 6:
    return "_vectorcall";
  }
}

// VisibilityAttr implementation

VisibilityAttr *
VisibilityAttr::CreateImplicit(TreeContext &Ctx,
                               VisibilityAttr::VisibilityType Visibility,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) VisibilityAttr(Ctx, CommonInfo, Visibility);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

VisibilityAttr *
VisibilityAttr::Create(TreeContext &Ctx,
                       VisibilityAttr::VisibilityType Visibility,
                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) VisibilityAttr(Ctx, CommonInfo, Visibility);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

VisibilityAttr *
VisibilityAttr::CreateImplicit(TreeContext &Ctx,
                               VisibilityAttr::VisibilityType Visibility,
                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_visibility:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_visibility, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_visibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_visibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_visibility:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_visibility, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Visibility, I);
}

VisibilityAttr *
VisibilityAttr::Create(TreeContext &Ctx,
                       VisibilityAttr::VisibilityType Visibility,
                       SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_visibility:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_visibility, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_visibility:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_visibility,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_visibility:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_visibility, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Visibility, I);
}

VisibilityAttr::VisibilityAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo,
                               VisibilityAttr::VisibilityType Visibility)
    : InheritableAttr(Ctx, CommonInfo, attr::Visibility, false, false),
      visibility(Visibility) {}

bool VisibilityAttr::ConvertStrToVisibilityType(
    StringRef Val, VisibilityAttr::VisibilityType &Out) {
  std::optional<VisibilityAttr::VisibilityType> R =
      llvm::StringSwitch<std::optional<VisibilityAttr::VisibilityType>>(Val)
          .Case("default", VisibilityAttr::VisibilityType::Default)
          .Case("hidden", VisibilityAttr::VisibilityType::Hidden)
          .Case("internal", VisibilityAttr::VisibilityType::Hidden)
          .Case("protected", VisibilityAttr::VisibilityType::Protected)
          .Default(std::optional<VisibilityAttr::VisibilityType>());
  if (R) {
    Out = *R;
    return true;
  }
  return false;
}

const char *
VisibilityAttr::ConvertVisibilityTypeToStr(VisibilityAttr::VisibilityType Val) {
  switch (Val) {
  case VisibilityAttr::VisibilityType::Default:
    return "default";
  case VisibilityAttr::VisibilityType::Hidden:
    return "hidden";
  case VisibilityAttr::VisibilityType::Protected:
    return "protected";
  }
  llvm_unreachable("No enumerator with that value");
}
VisibilityAttr *VisibilityAttr::clone(TreeContext &C) const {
  auto *A = new (C) VisibilityAttr(C, *this, visibility);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void VisibilityAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((visibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << VisibilityAttr::ConvertVisibilityTypeToStr(getVisibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::visibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << VisibilityAttr::ConvertVisibilityTypeToStr(getVisibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::visibility";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << VisibilityAttr::ConvertVisibilityTypeToStr(getVisibility())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *VisibilityAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "visibility";
  case 1:
    return "visibility";
  case 2:
    return "visibility";
  }
}

// VolatileAttr implementation

VolatileAttr *
VolatileAttr::CreateImplicit(TreeContext &Ctx,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) VolatileAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

VolatileAttr *VolatileAttr::Create(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) VolatileAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

VolatileAttr *VolatileAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                           Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_volatile:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_volatile, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_volatile:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_volatile,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_volatile:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_volatile, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_volatile:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_volatile, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

VolatileAttr *VolatileAttr::Create(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_volatile:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_volatile, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_volatile:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_volatile,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_volatile:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_volatile, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Declspec_volatile:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Declspec,
                                       Declspec_volatile, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

VolatileAttr::VolatileAttr(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Volatile, false, false) {}

VolatileAttr *VolatileAttr::clone(TreeContext &C) const {
  auto *A = new (C) VolatileAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void VolatileAttr::printPretty(raw_ostream &OS,
                               const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((volatile";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::volatile";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::volatile";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __declspec(volatile";
    OS << ")";
    break;
  }
  }
}

const char *VolatileAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "volatile";
  case 1:
    return "volatile";
  case 2:
    return "volatile";
  case 3:
    return "volatile";
  }
}

// WarnUnusedAttr implementation

WarnUnusedAttr *
WarnUnusedAttr::CreateImplicit(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) WarnUnusedAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

WarnUnusedAttr *WarnUnusedAttr::Create(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) WarnUnusedAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

WarnUnusedAttr *WarnUnusedAttr::CreateImplicit(TreeContext &Ctx,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_warn_unused:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_warn_unused, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_warn_unused:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_warn_unused,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_warn_unused:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_warn_unused, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

WarnUnusedAttr *WarnUnusedAttr::Create(TreeContext &Ctx, SourceRange Range,
                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_warn_unused:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_warn_unused, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_warn_unused:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_warn_unused,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_warn_unused:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_warn_unused, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

WarnUnusedAttr::WarnUnusedAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::WarnUnused, false, false) {}

WarnUnusedAttr *WarnUnusedAttr::clone(TreeContext &C) const {
  auto *A = new (C) WarnUnusedAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void WarnUnusedAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((warn_unused";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::warn_unused";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::warn_unused";
    OS << "]]";
    break;
  }
  }
}

const char *WarnUnusedAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "warn_unused";
  case 1:
    return "warn_unused";
  case 2:
    return "warn_unused";
  }
}

// WarnUnusedResultAttr implementation

WarnUnusedResultAttr *
WarnUnusedResultAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Message,
                                     const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) WarnUnusedResultAttr(Ctx, CommonInfo, Message);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

WarnUnusedResultAttr *
WarnUnusedResultAttr::Create(TreeContext &Ctx, llvm::StringRef Message,
                             const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) WarnUnusedResultAttr(Ctx, CommonInfo, Message);
  return A;
}

WarnUnusedResultAttr *
WarnUnusedResultAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Message,
                                     SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_nodiscard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_nodiscard, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_nodiscard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_nodiscard, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_warn_unused_result:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_warn_unused_result,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_warn_unused_result:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_warn_unused_result,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_warn_unused_result:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_warn_unused_result,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_warn_unused_result:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_warn_unused_result,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Message, I);
}

WarnUnusedResultAttr *WarnUnusedResultAttr::Create(TreeContext &Ctx,
                                                   llvm::StringRef Message,
                                                   SourceRange Range,
                                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case Bracket_nodiscard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_nodiscard, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_nodiscard:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_nodiscard, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_warn_unused_result:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_warn_unused_result,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case GNU_warn_unused_result:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_warn_unused_result,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_warn_unused_result:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_warn_unused_result,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_warn_unused_result:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_warn_unused_result,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Message, I);
}

WarnUnusedResultAttr::WarnUnusedResultAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    llvm::StringRef Message)
    : InheritableAttr(Ctx, CommonInfo, attr::WarnUnusedResult, false, false),
      messageLength(Message.size()), message(new (Ctx, 1) char[messageLength]) {
  if (!Message.empty())
    std::memcpy(message, Message.data(), messageLength);
}

WarnUnusedResultAttr::WarnUnusedResultAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::WarnUnusedResult, false, false),
      messageLength(0), message(nullptr) {}

WarnUnusedResultAttr::Spelling
WarnUnusedResultAttr::getSemanticSpelling() const {
  switch (getAttributeSpellingListIndex()) {
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

WarnUnusedResultAttr *WarnUnusedResultAttr::clone(TreeContext &C) const {
  auto *A = new (C) WarnUnusedResultAttr(C, *this, getMessage());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void WarnUnusedResultAttr::printPretty(raw_ostream &OS,
                                       const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " [[nodiscard";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 1: {
    OS << " [[nodiscard";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::warn_unused_result";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " __attribute__((warn_unused_result";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 4: {
    OS << " [[gnu::warn_unused_result";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 5: {
    OS << " [[gnu::warn_unused_result";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getMessage() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *WarnUnusedResultAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "nodiscard";
  case 1:
    return "nodiscard";
  case 2:
    return "warn_unused_result";
  case 3:
    return "warn_unused_result";
  case 4:
    return "warn_unused_result";
  case 5:
    return "warn_unused_result";
  }
}

// WeakAttr implementation

WeakAttr *WeakAttr::CreateImplicit(TreeContext &Ctx,
                                   const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) WeakAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

WeakAttr *WeakAttr::Create(TreeContext &Ctx,
                           const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) WeakAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

WeakAttr *WeakAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                   Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_weak:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_weak,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_weak:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_weak, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_weak:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_weak, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

WeakAttr *WeakAttr::Create(TreeContext &Ctx, SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_weak:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_weak,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_weak:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_weak, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_weak:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_weak, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

WeakAttr::WeakAttr(TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::Weak, false, false) {}

WeakAttr *WeakAttr::clone(TreeContext &C) const {
  auto *A = new (C) WeakAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void WeakAttr::printPretty(raw_ostream &OS,
                           const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((weak";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::weak";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::weak";
    OS << "]]";
    break;
  }
  }
}

const char *WeakAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "weak";
  case 1:
    return "weak";
  case 2:
    return "weak";
  }
}

// WeakImportAttr implementation

WeakImportAttr *
WeakImportAttr::CreateImplicit(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) WeakImportAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

WeakImportAttr *WeakImportAttr::Create(TreeContext &Ctx,
                                       const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) WeakImportAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

WeakImportAttr *WeakImportAttr::CreateImplicit(TreeContext &Ctx,
                                               SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_weak_import:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_weak_import, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_weak_import:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_weak_import,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_weak_import:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_weak_import,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_weak_import:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_weak_import,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_weak_import:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_weak_import,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

WeakImportAttr *WeakImportAttr::Create(TreeContext &Ctx, SourceRange Range,
                                       Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_weak_import:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU,
                                       GNU_weak_import, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_clang_weak_import:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_clang_weak_import,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_neverc_weak_import:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_neverc_weak_import,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_clang_weak_import:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_clang_weak_import,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_neverc_weak_import:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_neverc_weak_import,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

WeakImportAttr::WeakImportAttr(TreeContext &Ctx,
                               const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::WeakImport, false, false) {}

WeakImportAttr *WeakImportAttr::clone(TreeContext &C) const {
  auto *A = new (C) WeakImportAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void WeakImportAttr::printPretty(raw_ostream &OS,
                                 const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((weak_import";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[clang::weak_import";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[neverc::weak_import";
    OS << "]]";
    break;
  }
  case 3: {
    OS << " [[clang::weak_import";
    OS << "]]";
    break;
  }
  case 4: {
    OS << " [[neverc::weak_import";
    OS << "]]";
    break;
  }
  }
}

const char *WeakImportAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "weak_import";
  case 1:
    return "weak_import";
  case 2:
    return "weak_import";
  case 3:
    return "weak_import";
  case 4:
    return "weak_import";
  }
}

// WeakRefAttr implementation

WeakRefAttr *
WeakRefAttr::CreateImplicit(TreeContext &Ctx, llvm::StringRef Aliasee,
                            const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) WeakRefAttr(Ctx, CommonInfo, Aliasee);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

WeakRefAttr *WeakRefAttr::Create(TreeContext &Ctx, llvm::StringRef Aliasee,
                                 const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) WeakRefAttr(Ctx, CommonInfo, Aliasee);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

WeakRefAttr *WeakRefAttr::CreateImplicit(TreeContext &Ctx,
                                         llvm::StringRef Aliasee,
                                         SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_weakref:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_weakref,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_weakref:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_weakref, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_weakref:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_weakref, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, Aliasee, I);
}

WeakRefAttr *WeakRefAttr::Create(TreeContext &Ctx, llvm::StringRef Aliasee,
                                 SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_weakref:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, GNU_weakref,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_weakref:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_Bracket,
                                       Bracket_gnu_weakref, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    case C23_gnu_weakref:
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_C23,
                                       C23_gnu_weakref, false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, Aliasee, I);
}

WeakRefAttr::WeakRefAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo,
                         llvm::StringRef Aliasee)
    : InheritableAttr(Ctx, CommonInfo, attr::WeakRef, false, false),
      aliaseeLength(Aliasee.size()), aliasee(new (Ctx, 1) char[aliaseeLength]) {
  if (!Aliasee.empty())
    std::memcpy(aliasee, Aliasee.data(), aliaseeLength);
}

WeakRefAttr::WeakRefAttr(TreeContext &Ctx,
                         const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::WeakRef, false, false),
      aliaseeLength(0), aliasee(nullptr) {}

WeakRefAttr *WeakRefAttr::clone(TreeContext &C) const {
  auto *A = new (C) WeakRefAttr(C, *this, getAliasee());
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void WeakRefAttr::printPretty(raw_ostream &OS,
                              const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((weakref";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAliasee() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::weakref";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAliasee() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::weakref";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\"" << getAliasee() << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *WeakRefAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "weakref";
  case 1:
    return "weakref";
  case 2:
    return "weakref";
  }
}

// X86ForceAlignArgPointerAttr implementation

X86ForceAlignArgPointerAttr *X86ForceAlignArgPointerAttr::CreateImplicit(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) X86ForceAlignArgPointerAttr(Ctx, CommonInfo);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

X86ForceAlignArgPointerAttr *
X86ForceAlignArgPointerAttr::Create(TreeContext &Ctx,
                                    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) X86ForceAlignArgPointerAttr(Ctx, CommonInfo);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

X86ForceAlignArgPointerAttr *
X86ForceAlignArgPointerAttr::CreateImplicit(TreeContext &Ctx, SourceRange Range,
                                            Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_force_align_arg_pointer:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_force_align_arg_pointer,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_force_align_arg_pointer:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_force_align_arg_pointer,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_force_align_arg_pointer:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_force_align_arg_pointer,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, I);
}

X86ForceAlignArgPointerAttr *
X86ForceAlignArgPointerAttr::Create(TreeContext &Ctx, SourceRange Range,
                                    Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_force_align_arg_pointer:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_force_align_arg_pointer,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_force_align_arg_pointer:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_force_align_arg_pointer,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_force_align_arg_pointer:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_force_align_arg_pointer,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, I);
}

X86ForceAlignArgPointerAttr::X86ForceAlignArgPointerAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo)
    : InheritableAttr(Ctx, CommonInfo, attr::X86ForceAlignArgPointer, false,
                      false) {}

X86ForceAlignArgPointerAttr *
X86ForceAlignArgPointerAttr::clone(TreeContext &C) const {
  auto *A = new (C) X86ForceAlignArgPointerAttr(C, *this);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void X86ForceAlignArgPointerAttr::printPretty(
    raw_ostream &OS, const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((force_align_arg_pointer";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::force_align_arg_pointer";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::force_align_arg_pointer";
    OS << "]]";
    break;
  }
  }
}

const char *X86ForceAlignArgPointerAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "force_align_arg_pointer";
  case 1:
    return "force_align_arg_pointer";
  case 2:
    return "force_align_arg_pointer";
  }
}

// ZeroCallUsedRegsAttr implementation

ZeroCallUsedRegsAttr *ZeroCallUsedRegsAttr::CreateImplicit(
    TreeContext &Ctx,
    ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind ZeroCallUsedRegs,
    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ZeroCallUsedRegsAttr(Ctx, CommonInfo, ZeroCallUsedRegs);
  A->setImplicit(true);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ZeroCallUsedRegsAttr *ZeroCallUsedRegsAttr::Create(
    TreeContext &Ctx,
    ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind ZeroCallUsedRegs,
    const AttributeCommonInfo &CommonInfo) {
  auto *A = new (Ctx) ZeroCallUsedRegsAttr(Ctx, CommonInfo, ZeroCallUsedRegs);
  if (!A->isAttributeSpellingListCalculated() && !A->getAttrName())
    A->setAttributeSpellingListIndex(0);
  return A;
}

ZeroCallUsedRegsAttr *ZeroCallUsedRegsAttr::CreateImplicit(
    TreeContext &Ctx,
    ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind ZeroCallUsedRegs,
    SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_zero_call_used_regs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_zero_call_used_regs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_zero_call_used_regs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_zero_call_used_regs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_zero_call_used_regs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_zero_call_used_regs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return CreateImplicit(Ctx, ZeroCallUsedRegs, I);
}

ZeroCallUsedRegsAttr *ZeroCallUsedRegsAttr::Create(
    TreeContext &Ctx,
    ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind ZeroCallUsedRegs,
    SourceRange Range, Spelling S) {
  AttributeCommonInfo I(Range, NoSemaHandlerAttribute, [&]() {
    switch (S) {
    case GNU_zero_call_used_regs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_GNU, GNU_zero_call_used_regs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case Bracket_gnu_zero_call_used_regs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_Bracket, Bracket_gnu_zero_call_used_regs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    case C23_gnu_zero_call_used_regs:
      return AttributeCommonInfo::Form{
          AttributeCommonInfo::AS_C23, C23_gnu_zero_call_used_regs,
          false /*IsAlignas*/, false /*IsRegularKeywordAttribute*/};
    default:
      llvm_unreachable("Unknown attribute spelling!");
      return AttributeCommonInfo::Form{AttributeCommonInfo::AS_GNU, 0,
                                       false /*IsAlignas*/,
                                       false /*IsRegularKeywordAttribute*/};
    }
  }());
  return Create(Ctx, ZeroCallUsedRegs, I);
}

ZeroCallUsedRegsAttr::ZeroCallUsedRegsAttr(
    TreeContext &Ctx, const AttributeCommonInfo &CommonInfo,
    ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind ZeroCallUsedRegs)
    : InheritableAttr(Ctx, CommonInfo, attr::ZeroCallUsedRegs, false, false),
      zeroCallUsedRegs(ZeroCallUsedRegs) {}

bool ZeroCallUsedRegsAttr::ConvertStrToZeroCallUsedRegsKind(
    StringRef Val, ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind &Out) {
  std::optional<ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind> R =
      llvm::StringSwitch<
          std::optional<ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind>>(Val)
          .Case("skip", ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::Skip)
          .Case("used-gpr-arg",
                ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::UsedGPRArg)
          .Case("used-gpr", ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::UsedGPR)
          .Case("used-arg", ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::UsedArg)
          .Case("used", ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::Used)
          .Case("all-gpr-arg",
                ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::AllGPRArg)
          .Case("all-gpr", ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::AllGPR)
          .Case("all-arg", ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::AllArg)
          .Case("all", ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::All)
          .Default(std::optional<ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind>());
  if (R) {
    Out = *R;
    return true;
  }
  return false;
}

const char *ZeroCallUsedRegsAttr::ConvertZeroCallUsedRegsKindToStr(
    ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind Val) {
  switch (Val) {
  case ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::Skip:
    return "skip";
  case ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::UsedGPRArg:
    return "used-gpr-arg";
  case ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::UsedGPR:
    return "used-gpr";
  case ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::UsedArg:
    return "used-arg";
  case ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::Used:
    return "used";
  case ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::AllGPRArg:
    return "all-gpr-arg";
  case ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::AllGPR:
    return "all-gpr";
  case ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::AllArg:
    return "all-arg";
  case ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind::All:
    return "all";
  }
  llvm_unreachable("No enumerator with that value");
}
ZeroCallUsedRegsAttr *ZeroCallUsedRegsAttr::clone(TreeContext &C) const {
  auto *A = new (C) ZeroCallUsedRegsAttr(C, *this, zeroCallUsedRegs);
  A->Inherited = Inherited;
  A->setImplicit(Implicit);
  return A;
}

void ZeroCallUsedRegsAttr::printPretty(raw_ostream &OS,
                                       const PrintingPolicy &Policy) const {
  bool IsFirstArgument = true;
  (void)IsFirstArgument;
  unsigned TrailingOmittedArgs = 0;
  (void)TrailingOmittedArgs;
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    break;
  case 0: {
    OS << " __attribute__((zero_call_used_regs";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\""
       << ZeroCallUsedRegsAttr::ConvertZeroCallUsedRegsKindToStr(
              getZeroCallUsedRegs())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "))";
    break;
  }
  case 1: {
    OS << " [[gnu::zero_call_used_regs";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\""
       << ZeroCallUsedRegsAttr::ConvertZeroCallUsedRegsKindToStr(
              getZeroCallUsedRegs())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  case 2: {
    OS << " [[gnu::zero_call_used_regs";
    DelimitAttributeArgument(OS, IsFirstArgument);
    OS << "\""
       << ZeroCallUsedRegsAttr::ConvertZeroCallUsedRegsKindToStr(
              getZeroCallUsedRegs())
       << "\"";
    if (!IsFirstArgument)
      OS << ")";
    OS << "]]";
    break;
  }
  }
}

const char *ZeroCallUsedRegsAttr::getSpelling() const {
  switch (getAttributeSpellingListIndex()) {
  default:
    llvm_unreachable("Unknown attribute spelling!");
    return "(No spelling)";
  case 0:
    return "zero_call_used_regs";
  case 1:
    return "zero_call_used_regs";
  case 2:
    return "zero_call_used_regs";
  }
}

const char *Attr::getSpelling() const {
  switch (getKind()) {
  case attr::AArch64SVEPcs:
    return cast<AArch64SVEPcsAttr>(this)->getSpelling();
  case attr::AArch64VectorPcs:
    return cast<AArch64VectorPcsAttr>(this)->getSpelling();
  case attr::AcquireHandle:
    return cast<AcquireHandleAttr>(this)->getSpelling();
  case attr::AddressSpace:
    return cast<AddressSpaceAttr>(this)->getSpelling();
  case attr::Alias:
    return cast<AliasAttr>(this)->getSpelling();
  case attr::AlignValue:
    return cast<AlignValueAttr>(this)->getSpelling();
  case attr::Aligned:
    return cast<AlignedAttr>(this)->getSpelling();
  case attr::AllocAlign:
    return cast<AllocAlignAttr>(this)->getSpelling();
  case attr::AllocSize:
    return cast<AllocSizeAttr>(this)->getSpelling();
  case attr::AlwaysDestroy:
    return cast<AlwaysDestroyAttr>(this)->getSpelling();
  case attr::AlwaysInline:
    return cast<AlwaysInlineAttr>(this)->getSpelling();
  case attr::AnalyzerNoReturn:
    return cast<AnalyzerNoReturnAttr>(this)->getSpelling();
  case attr::Annotate:
    return cast<AnnotateAttr>(this)->getSpelling();
  case attr::AnnotateType:
    return cast<AnnotateTypeAttr>(this)->getSpelling();
  case attr::AnyX86Interrupt:
    return cast<AnyX86InterruptAttr>(this)->getSpelling();
  case attr::AnyX86NoCallerSavedRegisters:
    return cast<AnyX86NoCallerSavedRegistersAttr>(this)->getSpelling();
  case attr::AnyX86NoCfCheck:
    return cast<AnyX86NoCfCheckAttr>(this)->getSpelling();
  case attr::ArgumentWithTypeTag:
    return cast<ArgumentWithTypeTagAttr>(this)->getSpelling();
  case attr::ArmBuiltinAlias:
    return cast<ArmBuiltinAliasAttr>(this)->getSpelling();
  case attr::ArmLocallyStreaming:
    return cast<ArmLocallyStreamingAttr>(this)->getSpelling();
  case attr::ArmNewZA:
    return cast<ArmNewZAAttr>(this)->getSpelling();
  case attr::ArmPreservesZA:
    return cast<ArmPreservesZAAttr>(this)->getSpelling();
  case attr::ArmSharedZA:
    return cast<ArmSharedZAAttr>(this)->getSpelling();
  case attr::ArmStreaming:
    return cast<ArmStreamingAttr>(this)->getSpelling();
  case attr::ArmStreamingCompatible:
    return cast<ArmStreamingCompatibleAttr>(this)->getSpelling();
  case attr::Artificial:
    return cast<ArtificialAttr>(this)->getSpelling();
  case attr::AsmLabel:
    return cast<AsmLabelAttr>(this)->getSpelling();
  case attr::AssumeAligned:
    return cast<AssumeAlignedAttr>(this)->getSpelling();
  case attr::Assumption:
    return cast<AssumptionAttr>(this)->getSpelling();
  case attr::Availability:
    return cast<AvailabilityAttr>(this)->getSpelling();
  case attr::AvailableOnlyInDefaultEvalMethod:
    return cast<AvailableOnlyInDefaultEvalMethodAttr>(this)->getSpelling();
  case attr::BTFDeclTag:
    return cast<BTFDeclTagAttr>(this)->getSpelling();
  case attr::BTFTypeTag:
    return cast<BTFTypeTagAttr>(this)->getSpelling();
  case attr::Builtin:
    return cast<BuiltinAttr>(this)->getSpelling();
  case attr::BuiltinAlias:
    return cast<BuiltinAliasAttr>(this)->getSpelling();
  case attr::C11NoReturn:
    return cast<C11NoReturnAttr>(this)->getSpelling();
  case attr::CDecl:
    return cast<CDeclAttr>(this)->getSpelling();
  case attr::CFGuard:
    return cast<CFGuardAttr>(this)->getSpelling();
  case attr::CPUDispatch:
    return cast<CPUDispatchAttr>(this)->getSpelling();
  case attr::CPUSpecific:
    return cast<CPUSpecificAttr>(this)->getSpelling();
  case attr::Callback:
    return cast<CallbackAttr>(this)->getSpelling();
  case attr::CarriesDependency:
    return cast<CarriesDependencyAttr>(this)->getSpelling();
  case attr::Cleanup:
    return cast<CleanupAttr>(this)->getSpelling();
  case attr::CodeAlign:
    return cast<CodeAlignAttr>(this)->getSpelling();
  case attr::CodeSeg:
    return cast<CodeSegAttr>(this)->getSpelling();
  case attr::Cold:
    return cast<ColdAttr>(this)->getSpelling();
  case attr::Common:
    return cast<CommonAttr>(this)->getSpelling();
  case attr::Const:
    return cast<ConstAttr>(this)->getSpelling();
  case attr::Constructor:
    return cast<ConstructorAttr>(this)->getSpelling();
  case attr::Convergent:
    return cast<ConvergentAttr>(this)->getSpelling();
  case attr::DLLExport:
    return cast<DLLExportAttr>(this)->getSpelling();
  case attr::DLLImport:
    return cast<DLLImportAttr>(this)->getSpelling();
  case attr::Deprecated:
    return cast<DeprecatedAttr>(this)->getSpelling();
  case attr::Destructor:
    return cast<DestructorAttr>(this)->getSpelling();
  case attr::DiagnoseAsBuiltin:
    return cast<DiagnoseAsBuiltinAttr>(this)->getSpelling();
  case attr::DiagnoseIf:
    return cast<DiagnoseIfAttr>(this)->getSpelling();
  case attr::DisableTailCalls:
    return cast<DisableTailCallsAttr>(this)->getSpelling();
  case attr::DisableTryStmt:
    return cast<DisableTryStmtAttr>(this)->getSpelling();
  case attr::EnableIf:
    return cast<EnableIfAttr>(this)->getSpelling();
  case attr::EnforceTCB:
    return cast<EnforceTCBAttr>(this)->getSpelling();
  case attr::EnforceTCBLeaf:
    return cast<EnforceTCBLeafAttr>(this)->getSpelling();
  case attr::EnumExtensibility:
    return cast<EnumExtensibilityAttr>(this)->getSpelling();
  case attr::Error:
    return cast<ErrorAttr>(this)->getSpelling();
  case attr::FallThrough:
    return cast<FallThroughAttr>(this)->getSpelling();
  case attr::FastCall:
    return cast<FastCallAttr>(this)->getSpelling();
  case attr::FlagEnum:
    return cast<FlagEnumAttr>(this)->getSpelling();
  case attr::Flatten:
    return cast<FlattenAttr>(this)->getSpelling();
  case attr::Format:
    return cast<FormatAttr>(this)->getSpelling();
  case attr::FormatArg:
    return cast<FormatArgAttr>(this)->getSpelling();
  case attr::FunctionReturnThunks:
    return cast<FunctionReturnThunksAttr>(this)->getSpelling();
  case attr::GNUInline:
    return cast<GNUInlineAttr>(this)->getSpelling();
  case attr::Hot:
    return cast<HotAttr>(this)->getSpelling();
  case attr::IFunc:
    return cast<IFuncAttr>(this)->getSpelling();
  case attr::InitSeg:
    return cast<InitSegAttr>(this)->getSpelling();
  case attr::InternalLinkage:
    return cast<InternalLinkageAttr>(this)->getSpelling();
  case attr::LTOVisibilityPublic:
    return cast<LTOVisibilityPublicAttr>(this)->getSpelling();
  case attr::Leaf:
    return cast<LeafAttr>(this)->getSpelling();
  case attr::Likely:
    return cast<LikelyAttr>(this)->getSpelling();
  case attr::LoaderUninitialized:
    return cast<LoaderUninitializedAttr>(this)->getSpelling();
  case attr::MSABI:
    return cast<MSABIAttr>(this)->getSpelling();
  case attr::MSAllocator:
    return cast<MSAllocatorAttr>(this)->getSpelling();
  case attr::MSStruct:
    return cast<MSStructAttr>(this)->getSpelling();
  case attr::MaxFieldAlignment:
    return cast<MaxFieldAlignmentAttr>(this)->getSpelling();
  case attr::MayAlias:
    return cast<MayAliasAttr>(this)->getSpelling();
  case attr::MaybeUndef:
    return cast<MaybeUndefAttr>(this)->getSpelling();
  case attr::MinSize:
    return cast<MinSizeAttr>(this)->getSpelling();
  case attr::MinVectorWidth:
    return cast<MinVectorWidthAttr>(this)->getSpelling();
  case attr::Mode:
    return cast<ModeAttr>(this)->getSpelling();
  case attr::MustTail:
    return cast<MustTailAttr>(this)->getSpelling();
  case attr::Naked:
    return cast<NakedAttr>(this)->getSpelling();
  case attr::NoAlias:
    return cast<NoAliasAttr>(this)->getSpelling();
  case attr::NoBuiltin:
    return cast<NoBuiltinAttr>(this)->getSpelling();
  case attr::NoCommon:
    return cast<NoCommonAttr>(this)->getSpelling();
  case attr::NoDebug:
    return cast<NoDebugAttr>(this)->getSpelling();
  case attr::NoDeref:
    return cast<NoDerefAttr>(this)->getSpelling();
  case attr::NoDestroy:
    return cast<NoDestroyAttr>(this)->getSpelling();
  case attr::NoDuplicate:
    return cast<NoDuplicateAttr>(this)->getSpelling();
  case attr::NoEscape:
    return cast<NoEscapeAttr>(this)->getSpelling();
  case attr::NoInline:
    return cast<NoInlineAttr>(this)->getSpelling();
  case attr::NoMerge:
    return cast<NoMergeAttr>(this)->getSpelling();
  case attr::NoRandomizeLayout:
    return cast<NoRandomizeLayoutAttr>(this)->getSpelling();
  case attr::NoReturn:
    return cast<NoReturnAttr>(this)->getSpelling();
  case attr::NoSpeculativeLoadHardening:
    return cast<NoSpeculativeLoadHardeningAttr>(this)->getSpelling();
  case attr::NoSplitStack:
    return cast<NoSplitStackAttr>(this)->getSpelling();
  case attr::NoStackProtector:
    return cast<NoStackProtectorAttr>(this)->getSpelling();
  case attr::NoThrow:
    return cast<NoThrowAttr>(this)->getSpelling();
  case attr::NoUwtable:
    return cast<NoUwtableAttr>(this)->getSpelling();
  case attr::NonNull:
    return cast<NonNullAttr>(this)->getSpelling();
  case attr::NotTailCalled:
    return cast<NotTailCalledAttr>(this)->getSpelling();
  case attr::OptimizeNone:
    return cast<OptimizeNoneAttr>(this)->getSpelling();
  case attr::Overloadable:
    return cast<OverloadableAttr>(this)->getSpelling();
  case attr::Override:
    return cast<OverrideAttr>(this)->getSpelling();
  case attr::Packed:
    return cast<PackedAttr>(this)->getSpelling();
  case attr::PassObjectSize:
    return cast<PassObjectSizeAttr>(this)->getSpelling();
  case attr::PatchableFunctionEntry:
    return cast<PatchableFunctionEntryAttr>(this)->getSpelling();
  case attr::PragmaNeverCBSSSection:
    return cast<PragmaNeverCBSSSectionAttr>(this)->getSpelling();
  case attr::PragmaNeverCDataSection:
    return cast<PragmaNeverCDataSectionAttr>(this)->getSpelling();
  case attr::PragmaNeverCRelroSection:
    return cast<PragmaNeverCRelroSectionAttr>(this)->getSpelling();
  case attr::PragmaNeverCRodataSection:
    return cast<PragmaNeverCRodataSectionAttr>(this)->getSpelling();
  case attr::PragmaNeverCTextSection:
    return cast<PragmaNeverCTextSectionAttr>(this)->getSpelling();
  case attr::PreferredType:
    return cast<PreferredTypeAttr>(this)->getSpelling();
  case attr::PreserveAll:
    return cast<PreserveAllAttr>(this)->getSpelling();
  case attr::PreserveMost:
    return cast<PreserveMostAttr>(this)->getSpelling();
  case attr::Ptr32:
    return cast<Ptr32Attr>(this)->getSpelling();
  case attr::Ptr64:
    return cast<Ptr64Attr>(this)->getSpelling();
  case attr::Pure:
    return cast<PureAttr>(this)->getSpelling();
  case attr::RandomizeLayout:
    return cast<RandomizeLayoutAttr>(this)->getSpelling();
  case attr::ReadOnlyPlacement:
    return cast<ReadOnlyPlacementAttr>(this)->getSpelling();
  case attr::RegCall:
    return cast<RegCallAttr>(this)->getSpelling();
  case attr::ReleaseHandle:
    return cast<ReleaseHandleAttr>(this)->getSpelling();
  case attr::Restrict:
    return cast<RestrictAttr>(this)->getSpelling();
  case attr::Retain:
    return cast<RetainAttr>(this)->getSpelling();
  case attr::ReturnsNonNull:
    return cast<ReturnsNonNullAttr>(this)->getSpelling();
  case attr::ReturnsTwice:
    return cast<ReturnsTwiceAttr>(this)->getSpelling();
  case attr::SPtr:
    return cast<SPtrAttr>(this)->getSpelling();
  case attr::Section:
    return cast<SectionAttr>(this)->getSpelling();
  case attr::SelectAny:
    return cast<SelectAnyAttr>(this)->getSpelling();
  case attr::Sentinel:
    return cast<SentinelAttr>(this)->getSpelling();
  case attr::SpeculativeLoadHardening:
    return cast<SpeculativeLoadHardeningAttr>(this)->getSpelling();
  case attr::StandardNoReturn:
    return cast<StandardNoReturnAttr>(this)->getSpelling();
  case attr::StdCall:
    return cast<StdCallAttr>(this)->getSpelling();
  case attr::StrictFP:
    return cast<StrictFPAttr>(this)->getSpelling();
  case attr::StrictGuardStackCheck:
    return cast<StrictGuardStackCheckAttr>(this)->getSpelling();
  case attr::Suppress:
    return cast<SuppressAttr>(this)->getSpelling();
  case attr::SysVABI:
    return cast<SysVABIAttr>(this)->getSpelling();
  case attr::TLSModel:
    return cast<TLSModelAttr>(this)->getSpelling();
  case attr::Target:
    return cast<TargetAttr>(this)->getSpelling();
  case attr::TargetClones:
    return cast<TargetClonesAttr>(this)->getSpelling();
  case attr::TargetVersion:
    return cast<TargetVersionAttr>(this)->getSpelling();
  case attr::Thread:
    return cast<ThreadAttr>(this)->getSpelling();
  case attr::TransparentUnion:
    return cast<TransparentUnionAttr>(this)->getSpelling();
  case attr::TypeNonNull:
    return cast<TypeNonNullAttr>(this)->getSpelling();
  case attr::TypeNullUnspecified:
    return cast<TypeNullUnspecifiedAttr>(this)->getSpelling();
  case attr::TypeNullable:
    return cast<TypeNullableAttr>(this)->getSpelling();
  case attr::TypeTagForDatatype:
    return cast<TypeTagForDatatypeAttr>(this)->getSpelling();
  case attr::TypeVisibility:
    return cast<TypeVisibilityAttr>(this)->getSpelling();
  case attr::UPtr:
    return cast<UPtrAttr>(this)->getSpelling();
  case attr::Unavailable:
    return cast<UnavailableAttr>(this)->getSpelling();
  case attr::Uninitialized:
    return cast<UninitializedAttr>(this)->getSpelling();
  case attr::Unlikely:
    return cast<UnlikelyAttr>(this)->getSpelling();
  case attr::UnsafeBufferUsage:
    return cast<UnsafeBufferUsageAttr>(this)->getSpelling();
  case attr::Unused:
    return cast<UnusedAttr>(this)->getSpelling();
  case attr::UseHandle:
    return cast<UseHandleAttr>(this)->getSpelling();
  case attr::Used:
    return cast<UsedAttr>(this)->getSpelling();
  case attr::VectorCall:
    return cast<VectorCallAttr>(this)->getSpelling();
  case attr::Visibility:
    return cast<VisibilityAttr>(this)->getSpelling();
  case attr::Volatile:
    return cast<VolatileAttr>(this)->getSpelling();
  case attr::WarnUnused:
    return cast<WarnUnusedAttr>(this)->getSpelling();
  case attr::WarnUnusedResult:
    return cast<WarnUnusedResultAttr>(this)->getSpelling();
  case attr::Weak:
    return cast<WeakAttr>(this)->getSpelling();
  case attr::WeakImport:
    return cast<WeakImportAttr>(this)->getSpelling();
  case attr::WeakRef:
    return cast<WeakRefAttr>(this)->getSpelling();
  case attr::X86ForceAlignArgPointer:
    return cast<X86ForceAlignArgPointerAttr>(this)->getSpelling();
  case attr::ZeroCallUsedRegs:
    return cast<ZeroCallUsedRegsAttr>(this)->getSpelling();
  }
  llvm_unreachable("Unexpected attribute kind!");
}

Attr *Attr::clone(TreeContext &C) const {
  switch (getKind()) {
  case attr::AArch64SVEPcs:
    return cast<AArch64SVEPcsAttr>(this)->clone(C);
  case attr::AArch64VectorPcs:
    return cast<AArch64VectorPcsAttr>(this)->clone(C);
  case attr::AcquireHandle:
    return cast<AcquireHandleAttr>(this)->clone(C);
  case attr::AddressSpace:
    return cast<AddressSpaceAttr>(this)->clone(C);
  case attr::Alias:
    return cast<AliasAttr>(this)->clone(C);
  case attr::AlignValue:
    return cast<AlignValueAttr>(this)->clone(C);
  case attr::Aligned:
    return cast<AlignedAttr>(this)->clone(C);
  case attr::AllocAlign:
    return cast<AllocAlignAttr>(this)->clone(C);
  case attr::AllocSize:
    return cast<AllocSizeAttr>(this)->clone(C);
  case attr::AlwaysDestroy:
    return cast<AlwaysDestroyAttr>(this)->clone(C);
  case attr::AlwaysInline:
    return cast<AlwaysInlineAttr>(this)->clone(C);
  case attr::AnalyzerNoReturn:
    return cast<AnalyzerNoReturnAttr>(this)->clone(C);
  case attr::Annotate:
    return cast<AnnotateAttr>(this)->clone(C);
  case attr::AnnotateType:
    return cast<AnnotateTypeAttr>(this)->clone(C);
  case attr::AnyX86Interrupt:
    return cast<AnyX86InterruptAttr>(this)->clone(C);
  case attr::AnyX86NoCallerSavedRegisters:
    return cast<AnyX86NoCallerSavedRegistersAttr>(this)->clone(C);
  case attr::AnyX86NoCfCheck:
    return cast<AnyX86NoCfCheckAttr>(this)->clone(C);
  case attr::ArgumentWithTypeTag:
    return cast<ArgumentWithTypeTagAttr>(this)->clone(C);
  case attr::ArmBuiltinAlias:
    return cast<ArmBuiltinAliasAttr>(this)->clone(C);
  case attr::ArmLocallyStreaming:
    return cast<ArmLocallyStreamingAttr>(this)->clone(C);
  case attr::ArmNewZA:
    return cast<ArmNewZAAttr>(this)->clone(C);
  case attr::ArmPreservesZA:
    return cast<ArmPreservesZAAttr>(this)->clone(C);
  case attr::ArmSharedZA:
    return cast<ArmSharedZAAttr>(this)->clone(C);
  case attr::ArmStreaming:
    return cast<ArmStreamingAttr>(this)->clone(C);
  case attr::ArmStreamingCompatible:
    return cast<ArmStreamingCompatibleAttr>(this)->clone(C);
  case attr::Artificial:
    return cast<ArtificialAttr>(this)->clone(C);
  case attr::AsmLabel:
    return cast<AsmLabelAttr>(this)->clone(C);
  case attr::AssumeAligned:
    return cast<AssumeAlignedAttr>(this)->clone(C);
  case attr::Assumption:
    return cast<AssumptionAttr>(this)->clone(C);
  case attr::Availability:
    return cast<AvailabilityAttr>(this)->clone(C);
  case attr::AvailableOnlyInDefaultEvalMethod:
    return cast<AvailableOnlyInDefaultEvalMethodAttr>(this)->clone(C);
  case attr::BTFDeclTag:
    return cast<BTFDeclTagAttr>(this)->clone(C);
  case attr::BTFTypeTag:
    return cast<BTFTypeTagAttr>(this)->clone(C);
  case attr::Builtin:
    return cast<BuiltinAttr>(this)->clone(C);
  case attr::BuiltinAlias:
    return cast<BuiltinAliasAttr>(this)->clone(C);
  case attr::C11NoReturn:
    return cast<C11NoReturnAttr>(this)->clone(C);
  case attr::CDecl:
    return cast<CDeclAttr>(this)->clone(C);
  case attr::CFGuard:
    return cast<CFGuardAttr>(this)->clone(C);
  case attr::CPUDispatch:
    return cast<CPUDispatchAttr>(this)->clone(C);
  case attr::CPUSpecific:
    return cast<CPUSpecificAttr>(this)->clone(C);
  case attr::Callback:
    return cast<CallbackAttr>(this)->clone(C);
  case attr::CarriesDependency:
    return cast<CarriesDependencyAttr>(this)->clone(C);
  case attr::Cleanup:
    return cast<CleanupAttr>(this)->clone(C);
  case attr::CodeAlign:
    return cast<CodeAlignAttr>(this)->clone(C);
  case attr::CodeSeg:
    return cast<CodeSegAttr>(this)->clone(C);
  case attr::Cold:
    return cast<ColdAttr>(this)->clone(C);
  case attr::Common:
    return cast<CommonAttr>(this)->clone(C);
  case attr::Const:
    return cast<ConstAttr>(this)->clone(C);
  case attr::Constructor:
    return cast<ConstructorAttr>(this)->clone(C);
  case attr::Convergent:
    return cast<ConvergentAttr>(this)->clone(C);
  case attr::DLLExport:
    return cast<DLLExportAttr>(this)->clone(C);
  case attr::DLLImport:
    return cast<DLLImportAttr>(this)->clone(C);
  case attr::Deprecated:
    return cast<DeprecatedAttr>(this)->clone(C);
  case attr::Destructor:
    return cast<DestructorAttr>(this)->clone(C);
  case attr::DiagnoseAsBuiltin:
    return cast<DiagnoseAsBuiltinAttr>(this)->clone(C);
  case attr::DiagnoseIf:
    return cast<DiagnoseIfAttr>(this)->clone(C);
  case attr::DisableTailCalls:
    return cast<DisableTailCallsAttr>(this)->clone(C);
  case attr::DisableTryStmt:
    return cast<DisableTryStmtAttr>(this)->clone(C);
  case attr::EnableIf:
    return cast<EnableIfAttr>(this)->clone(C);
  case attr::EnforceTCB:
    return cast<EnforceTCBAttr>(this)->clone(C);
  case attr::EnforceTCBLeaf:
    return cast<EnforceTCBLeafAttr>(this)->clone(C);
  case attr::EnumExtensibility:
    return cast<EnumExtensibilityAttr>(this)->clone(C);
  case attr::Error:
    return cast<ErrorAttr>(this)->clone(C);
  case attr::FallThrough:
    return cast<FallThroughAttr>(this)->clone(C);
  case attr::FastCall:
    return cast<FastCallAttr>(this)->clone(C);
  case attr::FlagEnum:
    return cast<FlagEnumAttr>(this)->clone(C);
  case attr::Flatten:
    return cast<FlattenAttr>(this)->clone(C);
  case attr::Format:
    return cast<FormatAttr>(this)->clone(C);
  case attr::FormatArg:
    return cast<FormatArgAttr>(this)->clone(C);
  case attr::FunctionReturnThunks:
    return cast<FunctionReturnThunksAttr>(this)->clone(C);
  case attr::GNUInline:
    return cast<GNUInlineAttr>(this)->clone(C);
  case attr::Hot:
    return cast<HotAttr>(this)->clone(C);
  case attr::IFunc:
    return cast<IFuncAttr>(this)->clone(C);
  case attr::InitSeg:
    return cast<InitSegAttr>(this)->clone(C);
  case attr::InternalLinkage:
    return cast<InternalLinkageAttr>(this)->clone(C);
  case attr::LTOVisibilityPublic:
    return cast<LTOVisibilityPublicAttr>(this)->clone(C);
  case attr::Leaf:
    return cast<LeafAttr>(this)->clone(C);
  case attr::Likely:
    return cast<LikelyAttr>(this)->clone(C);
  case attr::LoaderUninitialized:
    return cast<LoaderUninitializedAttr>(this)->clone(C);
  case attr::MSABI:
    return cast<MSABIAttr>(this)->clone(C);
  case attr::MSAllocator:
    return cast<MSAllocatorAttr>(this)->clone(C);
  case attr::MSStruct:
    return cast<MSStructAttr>(this)->clone(C);
  case attr::MaxFieldAlignment:
    return cast<MaxFieldAlignmentAttr>(this)->clone(C);
  case attr::MayAlias:
    return cast<MayAliasAttr>(this)->clone(C);
  case attr::MaybeUndef:
    return cast<MaybeUndefAttr>(this)->clone(C);
  case attr::MinSize:
    return cast<MinSizeAttr>(this)->clone(C);
  case attr::MinVectorWidth:
    return cast<MinVectorWidthAttr>(this)->clone(C);
  case attr::Mode:
    return cast<ModeAttr>(this)->clone(C);
  case attr::MustTail:
    return cast<MustTailAttr>(this)->clone(C);
  case attr::Naked:
    return cast<NakedAttr>(this)->clone(C);
  case attr::NoAlias:
    return cast<NoAliasAttr>(this)->clone(C);
  case attr::NoBuiltin:
    return cast<NoBuiltinAttr>(this)->clone(C);
  case attr::NoCommon:
    return cast<NoCommonAttr>(this)->clone(C);
  case attr::NoDebug:
    return cast<NoDebugAttr>(this)->clone(C);
  case attr::NoDeref:
    return cast<NoDerefAttr>(this)->clone(C);
  case attr::NoDestroy:
    return cast<NoDestroyAttr>(this)->clone(C);
  case attr::NoDuplicate:
    return cast<NoDuplicateAttr>(this)->clone(C);
  case attr::NoEscape:
    return cast<NoEscapeAttr>(this)->clone(C);
  case attr::NoInline:
    return cast<NoInlineAttr>(this)->clone(C);
  case attr::NoMerge:
    return cast<NoMergeAttr>(this)->clone(C);
  case attr::NoRandomizeLayout:
    return cast<NoRandomizeLayoutAttr>(this)->clone(C);
  case attr::NoReturn:
    return cast<NoReturnAttr>(this)->clone(C);
  case attr::NoSpeculativeLoadHardening:
    return cast<NoSpeculativeLoadHardeningAttr>(this)->clone(C);
  case attr::NoSplitStack:
    return cast<NoSplitStackAttr>(this)->clone(C);
  case attr::NoStackProtector:
    return cast<NoStackProtectorAttr>(this)->clone(C);
  case attr::NoThrow:
    return cast<NoThrowAttr>(this)->clone(C);
  case attr::NoUwtable:
    return cast<NoUwtableAttr>(this)->clone(C);
  case attr::NonNull:
    return cast<NonNullAttr>(this)->clone(C);
  case attr::NotTailCalled:
    return cast<NotTailCalledAttr>(this)->clone(C);
  case attr::OptimizeNone:
    return cast<OptimizeNoneAttr>(this)->clone(C);
  case attr::Overloadable:
    return cast<OverloadableAttr>(this)->clone(C);
  case attr::Override:
    return cast<OverrideAttr>(this)->clone(C);
  case attr::Packed:
    return cast<PackedAttr>(this)->clone(C);
  case attr::PassObjectSize:
    return cast<PassObjectSizeAttr>(this)->clone(C);
  case attr::PatchableFunctionEntry:
    return cast<PatchableFunctionEntryAttr>(this)->clone(C);
  case attr::PragmaNeverCBSSSection:
    return cast<PragmaNeverCBSSSectionAttr>(this)->clone(C);
  case attr::PragmaNeverCDataSection:
    return cast<PragmaNeverCDataSectionAttr>(this)->clone(C);
  case attr::PragmaNeverCRelroSection:
    return cast<PragmaNeverCRelroSectionAttr>(this)->clone(C);
  case attr::PragmaNeverCRodataSection:
    return cast<PragmaNeverCRodataSectionAttr>(this)->clone(C);
  case attr::PragmaNeverCTextSection:
    return cast<PragmaNeverCTextSectionAttr>(this)->clone(C);
  case attr::PreferredType:
    return cast<PreferredTypeAttr>(this)->clone(C);
  case attr::PreserveAll:
    return cast<PreserveAllAttr>(this)->clone(C);
  case attr::PreserveMost:
    return cast<PreserveMostAttr>(this)->clone(C);
  case attr::Ptr32:
    return cast<Ptr32Attr>(this)->clone(C);
  case attr::Ptr64:
    return cast<Ptr64Attr>(this)->clone(C);
  case attr::Pure:
    return cast<PureAttr>(this)->clone(C);
  case attr::RandomizeLayout:
    return cast<RandomizeLayoutAttr>(this)->clone(C);
  case attr::ReadOnlyPlacement:
    return cast<ReadOnlyPlacementAttr>(this)->clone(C);
  case attr::RegCall:
    return cast<RegCallAttr>(this)->clone(C);
  case attr::ReleaseHandle:
    return cast<ReleaseHandleAttr>(this)->clone(C);
  case attr::Restrict:
    return cast<RestrictAttr>(this)->clone(C);
  case attr::Retain:
    return cast<RetainAttr>(this)->clone(C);
  case attr::ReturnsNonNull:
    return cast<ReturnsNonNullAttr>(this)->clone(C);
  case attr::ReturnsTwice:
    return cast<ReturnsTwiceAttr>(this)->clone(C);
  case attr::SPtr:
    return cast<SPtrAttr>(this)->clone(C);
  case attr::Section:
    return cast<SectionAttr>(this)->clone(C);
  case attr::SelectAny:
    return cast<SelectAnyAttr>(this)->clone(C);
  case attr::Sentinel:
    return cast<SentinelAttr>(this)->clone(C);
  case attr::SpeculativeLoadHardening:
    return cast<SpeculativeLoadHardeningAttr>(this)->clone(C);
  case attr::StandardNoReturn:
    return cast<StandardNoReturnAttr>(this)->clone(C);
  case attr::StdCall:
    return cast<StdCallAttr>(this)->clone(C);
  case attr::StrictFP:
    return cast<StrictFPAttr>(this)->clone(C);
  case attr::StrictGuardStackCheck:
    return cast<StrictGuardStackCheckAttr>(this)->clone(C);
  case attr::Suppress:
    return cast<SuppressAttr>(this)->clone(C);
  case attr::SysVABI:
    return cast<SysVABIAttr>(this)->clone(C);
  case attr::TLSModel:
    return cast<TLSModelAttr>(this)->clone(C);
  case attr::Target:
    return cast<TargetAttr>(this)->clone(C);
  case attr::TargetClones:
    return cast<TargetClonesAttr>(this)->clone(C);
  case attr::TargetVersion:
    return cast<TargetVersionAttr>(this)->clone(C);
  case attr::Thread:
    return cast<ThreadAttr>(this)->clone(C);
  case attr::TransparentUnion:
    return cast<TransparentUnionAttr>(this)->clone(C);
  case attr::TypeNonNull:
    return cast<TypeNonNullAttr>(this)->clone(C);
  case attr::TypeNullUnspecified:
    return cast<TypeNullUnspecifiedAttr>(this)->clone(C);
  case attr::TypeNullable:
    return cast<TypeNullableAttr>(this)->clone(C);
  case attr::TypeTagForDatatype:
    return cast<TypeTagForDatatypeAttr>(this)->clone(C);
  case attr::TypeVisibility:
    return cast<TypeVisibilityAttr>(this)->clone(C);
  case attr::UPtr:
    return cast<UPtrAttr>(this)->clone(C);
  case attr::Unavailable:
    return cast<UnavailableAttr>(this)->clone(C);
  case attr::Uninitialized:
    return cast<UninitializedAttr>(this)->clone(C);
  case attr::Unlikely:
    return cast<UnlikelyAttr>(this)->clone(C);
  case attr::UnsafeBufferUsage:
    return cast<UnsafeBufferUsageAttr>(this)->clone(C);
  case attr::Unused:
    return cast<UnusedAttr>(this)->clone(C);
  case attr::UseHandle:
    return cast<UseHandleAttr>(this)->clone(C);
  case attr::Used:
    return cast<UsedAttr>(this)->clone(C);
  case attr::VectorCall:
    return cast<VectorCallAttr>(this)->clone(C);
  case attr::Visibility:
    return cast<VisibilityAttr>(this)->clone(C);
  case attr::Volatile:
    return cast<VolatileAttr>(this)->clone(C);
  case attr::WarnUnused:
    return cast<WarnUnusedAttr>(this)->clone(C);
  case attr::WarnUnusedResult:
    return cast<WarnUnusedResultAttr>(this)->clone(C);
  case attr::Weak:
    return cast<WeakAttr>(this)->clone(C);
  case attr::WeakImport:
    return cast<WeakImportAttr>(this)->clone(C);
  case attr::WeakRef:
    return cast<WeakRefAttr>(this)->clone(C);
  case attr::X86ForceAlignArgPointer:
    return cast<X86ForceAlignArgPointerAttr>(this)->clone(C);
  case attr::ZeroCallUsedRegs:
    return cast<ZeroCallUsedRegsAttr>(this)->clone(C);
  }
  llvm_unreachable("Unexpected attribute kind!");
}

void Attr::printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const {
  switch (getKind()) {
  case attr::AArch64SVEPcs:
    return cast<AArch64SVEPcsAttr>(this)->printPretty(OS, Policy);
  case attr::AArch64VectorPcs:
    return cast<AArch64VectorPcsAttr>(this)->printPretty(OS, Policy);
  case attr::AcquireHandle:
    return cast<AcquireHandleAttr>(this)->printPretty(OS, Policy);
  case attr::AddressSpace:
    return cast<AddressSpaceAttr>(this)->printPretty(OS, Policy);
  case attr::Alias:
    return cast<AliasAttr>(this)->printPretty(OS, Policy);
  case attr::AlignValue:
    return cast<AlignValueAttr>(this)->printPretty(OS, Policy);
  case attr::Aligned:
    return cast<AlignedAttr>(this)->printPretty(OS, Policy);
  case attr::AllocAlign:
    return cast<AllocAlignAttr>(this)->printPretty(OS, Policy);
  case attr::AllocSize:
    return cast<AllocSizeAttr>(this)->printPretty(OS, Policy);
  case attr::AlwaysDestroy:
    return cast<AlwaysDestroyAttr>(this)->printPretty(OS, Policy);
  case attr::AlwaysInline:
    return cast<AlwaysInlineAttr>(this)->printPretty(OS, Policy);
  case attr::AnalyzerNoReturn:
    return cast<AnalyzerNoReturnAttr>(this)->printPretty(OS, Policy);
  case attr::Annotate:
    return cast<AnnotateAttr>(this)->printPretty(OS, Policy);
  case attr::AnnotateType:
    return cast<AnnotateTypeAttr>(this)->printPretty(OS, Policy);
  case attr::AnyX86Interrupt:
    return cast<AnyX86InterruptAttr>(this)->printPretty(OS, Policy);
  case attr::AnyX86NoCallerSavedRegisters:
    return cast<AnyX86NoCallerSavedRegistersAttr>(this)->printPretty(OS,
                                                                     Policy);
  case attr::AnyX86NoCfCheck:
    return cast<AnyX86NoCfCheckAttr>(this)->printPretty(OS, Policy);
  case attr::ArgumentWithTypeTag:
    return cast<ArgumentWithTypeTagAttr>(this)->printPretty(OS, Policy);
  case attr::ArmBuiltinAlias:
    return cast<ArmBuiltinAliasAttr>(this)->printPretty(OS, Policy);
  case attr::ArmLocallyStreaming:
    return cast<ArmLocallyStreamingAttr>(this)->printPretty(OS, Policy);
  case attr::ArmNewZA:
    return cast<ArmNewZAAttr>(this)->printPretty(OS, Policy);
  case attr::ArmPreservesZA:
    return cast<ArmPreservesZAAttr>(this)->printPretty(OS, Policy);
  case attr::ArmSharedZA:
    return cast<ArmSharedZAAttr>(this)->printPretty(OS, Policy);
  case attr::ArmStreaming:
    return cast<ArmStreamingAttr>(this)->printPretty(OS, Policy);
  case attr::ArmStreamingCompatible:
    return cast<ArmStreamingCompatibleAttr>(this)->printPretty(OS, Policy);
  case attr::Artificial:
    return cast<ArtificialAttr>(this)->printPretty(OS, Policy);
  case attr::AsmLabel:
    return cast<AsmLabelAttr>(this)->printPretty(OS, Policy);
  case attr::AssumeAligned:
    return cast<AssumeAlignedAttr>(this)->printPretty(OS, Policy);
  case attr::Assumption:
    return cast<AssumptionAttr>(this)->printPretty(OS, Policy);
  case attr::Availability:
    return cast<AvailabilityAttr>(this)->printPretty(OS, Policy);
  case attr::AvailableOnlyInDefaultEvalMethod:
    return cast<AvailableOnlyInDefaultEvalMethodAttr>(this)->printPretty(
        OS, Policy);
  case attr::BTFDeclTag:
    return cast<BTFDeclTagAttr>(this)->printPretty(OS, Policy);
  case attr::BTFTypeTag:
    return cast<BTFTypeTagAttr>(this)->printPretty(OS, Policy);
  case attr::Builtin:
    return cast<BuiltinAttr>(this)->printPretty(OS, Policy);
  case attr::BuiltinAlias:
    return cast<BuiltinAliasAttr>(this)->printPretty(OS, Policy);
  case attr::C11NoReturn:
    return cast<C11NoReturnAttr>(this)->printPretty(OS, Policy);
  case attr::CDecl:
    return cast<CDeclAttr>(this)->printPretty(OS, Policy);
  case attr::CFGuard:
    return cast<CFGuardAttr>(this)->printPretty(OS, Policy);
  case attr::CPUDispatch:
    return cast<CPUDispatchAttr>(this)->printPretty(OS, Policy);
  case attr::CPUSpecific:
    return cast<CPUSpecificAttr>(this)->printPretty(OS, Policy);
  case attr::Callback:
    return cast<CallbackAttr>(this)->printPretty(OS, Policy);
  case attr::CarriesDependency:
    return cast<CarriesDependencyAttr>(this)->printPretty(OS, Policy);
  case attr::Cleanup:
    return cast<CleanupAttr>(this)->printPretty(OS, Policy);
  case attr::CodeAlign:
    return cast<CodeAlignAttr>(this)->printPretty(OS, Policy);
  case attr::CodeSeg:
    return cast<CodeSegAttr>(this)->printPretty(OS, Policy);
  case attr::Cold:
    return cast<ColdAttr>(this)->printPretty(OS, Policy);
  case attr::Common:
    return cast<CommonAttr>(this)->printPretty(OS, Policy);
  case attr::Const:
    return cast<ConstAttr>(this)->printPretty(OS, Policy);
  case attr::Constructor:
    return cast<ConstructorAttr>(this)->printPretty(OS, Policy);
  case attr::Convergent:
    return cast<ConvergentAttr>(this)->printPretty(OS, Policy);
  case attr::DLLExport:
    return cast<DLLExportAttr>(this)->printPretty(OS, Policy);
  case attr::DLLImport:
    return cast<DLLImportAttr>(this)->printPretty(OS, Policy);
  case attr::Deprecated:
    return cast<DeprecatedAttr>(this)->printPretty(OS, Policy);
  case attr::Destructor:
    return cast<DestructorAttr>(this)->printPretty(OS, Policy);
  case attr::DiagnoseAsBuiltin:
    return cast<DiagnoseAsBuiltinAttr>(this)->printPretty(OS, Policy);
  case attr::DiagnoseIf:
    return cast<DiagnoseIfAttr>(this)->printPretty(OS, Policy);
  case attr::DisableTailCalls:
    return cast<DisableTailCallsAttr>(this)->printPretty(OS, Policy);
  case attr::DisableTryStmt:
    return cast<DisableTryStmtAttr>(this)->printPretty(OS, Policy);
  case attr::EnableIf:
    return cast<EnableIfAttr>(this)->printPretty(OS, Policy);
  case attr::EnforceTCB:
    return cast<EnforceTCBAttr>(this)->printPretty(OS, Policy);
  case attr::EnforceTCBLeaf:
    return cast<EnforceTCBLeafAttr>(this)->printPretty(OS, Policy);
  case attr::EnumExtensibility:
    return cast<EnumExtensibilityAttr>(this)->printPretty(OS, Policy);
  case attr::Error:
    return cast<ErrorAttr>(this)->printPretty(OS, Policy);
  case attr::FallThrough:
    return cast<FallThroughAttr>(this)->printPretty(OS, Policy);
  case attr::FastCall:
    return cast<FastCallAttr>(this)->printPretty(OS, Policy);
  case attr::FlagEnum:
    return cast<FlagEnumAttr>(this)->printPretty(OS, Policy);
  case attr::Flatten:
    return cast<FlattenAttr>(this)->printPretty(OS, Policy);
  case attr::Format:
    return cast<FormatAttr>(this)->printPretty(OS, Policy);
  case attr::FormatArg:
    return cast<FormatArgAttr>(this)->printPretty(OS, Policy);
  case attr::FunctionReturnThunks:
    return cast<FunctionReturnThunksAttr>(this)->printPretty(OS, Policy);
  case attr::GNUInline:
    return cast<GNUInlineAttr>(this)->printPretty(OS, Policy);
  case attr::Hot:
    return cast<HotAttr>(this)->printPretty(OS, Policy);
  case attr::IFunc:
    return cast<IFuncAttr>(this)->printPretty(OS, Policy);
  case attr::InitSeg:
    return cast<InitSegAttr>(this)->printPretty(OS, Policy);
  case attr::InternalLinkage:
    return cast<InternalLinkageAttr>(this)->printPretty(OS, Policy);
  case attr::LTOVisibilityPublic:
    return cast<LTOVisibilityPublicAttr>(this)->printPretty(OS, Policy);
  case attr::Leaf:
    return cast<LeafAttr>(this)->printPretty(OS, Policy);
  case attr::Likely:
    return cast<LikelyAttr>(this)->printPretty(OS, Policy);
  case attr::LoaderUninitialized:
    return cast<LoaderUninitializedAttr>(this)->printPretty(OS, Policy);
  case attr::MSABI:
    return cast<MSABIAttr>(this)->printPretty(OS, Policy);
  case attr::MSAllocator:
    return cast<MSAllocatorAttr>(this)->printPretty(OS, Policy);
  case attr::MSStruct:
    return cast<MSStructAttr>(this)->printPretty(OS, Policy);
  case attr::MaxFieldAlignment:
    return cast<MaxFieldAlignmentAttr>(this)->printPretty(OS, Policy);
  case attr::MayAlias:
    return cast<MayAliasAttr>(this)->printPretty(OS, Policy);
  case attr::MaybeUndef:
    return cast<MaybeUndefAttr>(this)->printPretty(OS, Policy);
  case attr::MinSize:
    return cast<MinSizeAttr>(this)->printPretty(OS, Policy);
  case attr::MinVectorWidth:
    return cast<MinVectorWidthAttr>(this)->printPretty(OS, Policy);
  case attr::Mode:
    return cast<ModeAttr>(this)->printPretty(OS, Policy);
  case attr::MustTail:
    return cast<MustTailAttr>(this)->printPretty(OS, Policy);
  case attr::Naked:
    return cast<NakedAttr>(this)->printPretty(OS, Policy);
  case attr::NoAlias:
    return cast<NoAliasAttr>(this)->printPretty(OS, Policy);
  case attr::NoBuiltin:
    return cast<NoBuiltinAttr>(this)->printPretty(OS, Policy);
  case attr::NoCommon:
    return cast<NoCommonAttr>(this)->printPretty(OS, Policy);
  case attr::NoDebug:
    return cast<NoDebugAttr>(this)->printPretty(OS, Policy);
  case attr::NoDeref:
    return cast<NoDerefAttr>(this)->printPretty(OS, Policy);
  case attr::NoDestroy:
    return cast<NoDestroyAttr>(this)->printPretty(OS, Policy);
  case attr::NoDuplicate:
    return cast<NoDuplicateAttr>(this)->printPretty(OS, Policy);
  case attr::NoEscape:
    return cast<NoEscapeAttr>(this)->printPretty(OS, Policy);
  case attr::NoInline:
    return cast<NoInlineAttr>(this)->printPretty(OS, Policy);
  case attr::NoMerge:
    return cast<NoMergeAttr>(this)->printPretty(OS, Policy);
  case attr::NoRandomizeLayout:
    return cast<NoRandomizeLayoutAttr>(this)->printPretty(OS, Policy);
  case attr::NoReturn:
    return cast<NoReturnAttr>(this)->printPretty(OS, Policy);
  case attr::NoSpeculativeLoadHardening:
    return cast<NoSpeculativeLoadHardeningAttr>(this)->printPretty(OS, Policy);
  case attr::NoSplitStack:
    return cast<NoSplitStackAttr>(this)->printPretty(OS, Policy);
  case attr::NoStackProtector:
    return cast<NoStackProtectorAttr>(this)->printPretty(OS, Policy);
  case attr::NoThrow:
    return cast<NoThrowAttr>(this)->printPretty(OS, Policy);
  case attr::NoUwtable:
    return cast<NoUwtableAttr>(this)->printPretty(OS, Policy);
  case attr::NonNull:
    return cast<NonNullAttr>(this)->printPretty(OS, Policy);
  case attr::NotTailCalled:
    return cast<NotTailCalledAttr>(this)->printPretty(OS, Policy);
  case attr::OptimizeNone:
    return cast<OptimizeNoneAttr>(this)->printPretty(OS, Policy);
  case attr::Overloadable:
    return cast<OverloadableAttr>(this)->printPretty(OS, Policy);
  case attr::Override:
    return cast<OverrideAttr>(this)->printPretty(OS, Policy);
  case attr::Packed:
    return cast<PackedAttr>(this)->printPretty(OS, Policy);
  case attr::PassObjectSize:
    return cast<PassObjectSizeAttr>(this)->printPretty(OS, Policy);
  case attr::PatchableFunctionEntry:
    return cast<PatchableFunctionEntryAttr>(this)->printPretty(OS, Policy);
  case attr::PragmaNeverCBSSSection:
    return cast<PragmaNeverCBSSSectionAttr>(this)->printPretty(OS, Policy);
  case attr::PragmaNeverCDataSection:
    return cast<PragmaNeverCDataSectionAttr>(this)->printPretty(OS, Policy);
  case attr::PragmaNeverCRelroSection:
    return cast<PragmaNeverCRelroSectionAttr>(this)->printPretty(OS, Policy);
  case attr::PragmaNeverCRodataSection:
    return cast<PragmaNeverCRodataSectionAttr>(this)->printPretty(OS, Policy);
  case attr::PragmaNeverCTextSection:
    return cast<PragmaNeverCTextSectionAttr>(this)->printPretty(OS, Policy);
  case attr::PreferredType:
    return cast<PreferredTypeAttr>(this)->printPretty(OS, Policy);
  case attr::PreserveAll:
    return cast<PreserveAllAttr>(this)->printPretty(OS, Policy);
  case attr::PreserveMost:
    return cast<PreserveMostAttr>(this)->printPretty(OS, Policy);
  case attr::Ptr32:
    return cast<Ptr32Attr>(this)->printPretty(OS, Policy);
  case attr::Ptr64:
    return cast<Ptr64Attr>(this)->printPretty(OS, Policy);
  case attr::Pure:
    return cast<PureAttr>(this)->printPretty(OS, Policy);
  case attr::RandomizeLayout:
    return cast<RandomizeLayoutAttr>(this)->printPretty(OS, Policy);
  case attr::ReadOnlyPlacement:
    return cast<ReadOnlyPlacementAttr>(this)->printPretty(OS, Policy);
  case attr::RegCall:
    return cast<RegCallAttr>(this)->printPretty(OS, Policy);
  case attr::ReleaseHandle:
    return cast<ReleaseHandleAttr>(this)->printPretty(OS, Policy);
  case attr::Restrict:
    return cast<RestrictAttr>(this)->printPretty(OS, Policy);
  case attr::Retain:
    return cast<RetainAttr>(this)->printPretty(OS, Policy);
  case attr::ReturnsNonNull:
    return cast<ReturnsNonNullAttr>(this)->printPretty(OS, Policy);
  case attr::ReturnsTwice:
    return cast<ReturnsTwiceAttr>(this)->printPretty(OS, Policy);
  case attr::SPtr:
    return cast<SPtrAttr>(this)->printPretty(OS, Policy);
  case attr::Section:
    return cast<SectionAttr>(this)->printPretty(OS, Policy);
  case attr::SelectAny:
    return cast<SelectAnyAttr>(this)->printPretty(OS, Policy);
  case attr::Sentinel:
    return cast<SentinelAttr>(this)->printPretty(OS, Policy);
  case attr::SpeculativeLoadHardening:
    return cast<SpeculativeLoadHardeningAttr>(this)->printPretty(OS, Policy);
  case attr::StandardNoReturn:
    return cast<StandardNoReturnAttr>(this)->printPretty(OS, Policy);
  case attr::StdCall:
    return cast<StdCallAttr>(this)->printPretty(OS, Policy);
  case attr::StrictFP:
    return cast<StrictFPAttr>(this)->printPretty(OS, Policy);
  case attr::StrictGuardStackCheck:
    return cast<StrictGuardStackCheckAttr>(this)->printPretty(OS, Policy);
  case attr::Suppress:
    return cast<SuppressAttr>(this)->printPretty(OS, Policy);
  case attr::SysVABI:
    return cast<SysVABIAttr>(this)->printPretty(OS, Policy);
  case attr::TLSModel:
    return cast<TLSModelAttr>(this)->printPretty(OS, Policy);
  case attr::Target:
    return cast<TargetAttr>(this)->printPretty(OS, Policy);
  case attr::TargetClones:
    return cast<TargetClonesAttr>(this)->printPretty(OS, Policy);
  case attr::TargetVersion:
    return cast<TargetVersionAttr>(this)->printPretty(OS, Policy);
  case attr::Thread:
    return cast<ThreadAttr>(this)->printPretty(OS, Policy);
  case attr::TransparentUnion:
    return cast<TransparentUnionAttr>(this)->printPretty(OS, Policy);
  case attr::TypeNonNull:
    return cast<TypeNonNullAttr>(this)->printPretty(OS, Policy);
  case attr::TypeNullUnspecified:
    return cast<TypeNullUnspecifiedAttr>(this)->printPretty(OS, Policy);
  case attr::TypeNullable:
    return cast<TypeNullableAttr>(this)->printPretty(OS, Policy);
  case attr::TypeTagForDatatype:
    return cast<TypeTagForDatatypeAttr>(this)->printPretty(OS, Policy);
  case attr::TypeVisibility:
    return cast<TypeVisibilityAttr>(this)->printPretty(OS, Policy);
  case attr::UPtr:
    return cast<UPtrAttr>(this)->printPretty(OS, Policy);
  case attr::Unavailable:
    return cast<UnavailableAttr>(this)->printPretty(OS, Policy);
  case attr::Uninitialized:
    return cast<UninitializedAttr>(this)->printPretty(OS, Policy);
  case attr::Unlikely:
    return cast<UnlikelyAttr>(this)->printPretty(OS, Policy);
  case attr::UnsafeBufferUsage:
    return cast<UnsafeBufferUsageAttr>(this)->printPretty(OS, Policy);
  case attr::Unused:
    return cast<UnusedAttr>(this)->printPretty(OS, Policy);
  case attr::UseHandle:
    return cast<UseHandleAttr>(this)->printPretty(OS, Policy);
  case attr::Used:
    return cast<UsedAttr>(this)->printPretty(OS, Policy);
  case attr::VectorCall:
    return cast<VectorCallAttr>(this)->printPretty(OS, Policy);
  case attr::Visibility:
    return cast<VisibilityAttr>(this)->printPretty(OS, Policy);
  case attr::Volatile:
    return cast<VolatileAttr>(this)->printPretty(OS, Policy);
  case attr::WarnUnused:
    return cast<WarnUnusedAttr>(this)->printPretty(OS, Policy);
  case attr::WarnUnusedResult:
    return cast<WarnUnusedResultAttr>(this)->printPretty(OS, Policy);
  case attr::Weak:
    return cast<WeakAttr>(this)->printPretty(OS, Policy);
  case attr::WeakImport:
    return cast<WeakImportAttr>(this)->printPretty(OS, Policy);
  case attr::WeakRef:
    return cast<WeakRefAttr>(this)->printPretty(OS, Policy);
  case attr::X86ForceAlignArgPointer:
    return cast<X86ForceAlignArgPointerAttr>(this)->printPretty(OS, Policy);
  case attr::ZeroCallUsedRegs:
    return cast<ZeroCallUsedRegsAttr>(this)->printPretty(OS, Policy);
  }
  llvm_unreachable("Unexpected attribute kind!");
}
