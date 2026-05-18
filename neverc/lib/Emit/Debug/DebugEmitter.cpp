#include "ABI/TargetInfo.h"
#include "Core/ConstantEmitter.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "Core/RecordLayoutInfo.h"
#include "Debug/DebugEmitterInfo.h"
#include "neverc/Compiler/FrontendOptions.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/Version.h"
#include "neverc/Scan/HeaderIndexOptions.h"
#include "neverc/Scan/PrepOptions.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/TimeProfiler.h"
#include <optional>
using namespace neverc;
using namespace neverc::Emit;

namespace {

uint32_t getTypeAlignIfRequired(const Type *Ty, const TreeContext &Ctx) {
  auto TI = Ctx.getTypeInfo(Ty);
  return TI.isAlignRequired() ? TI.Align : 0;
}

uint32_t getTypeAlignIfRequired(QualType Ty, const TreeContext &Ctx) {
  return getTypeAlignIfRequired(Ty.getTypePtr(), Ctx);
}

uint32_t getDeclAlignIfRequired(const Decl *D, const TreeContext &Ctx) {
  return D->hasAttr<AlignedAttr>() ? D->getMaxAlignment() : 0;
}

} // namespace

// ===----------------------------------------------------------------------===
// Construction & location management
// ===----------------------------------------------------------------------===

DebugEmitter::DebugEmitter(ModuleEmitter &ME)
    : ME(ME), DebugKind(ME.getCodeGenOpts().getDebugInfo()),
      DBuilder(ME.getModule()) {
  CreateCompileUnit();
}

DebugEmitter::~DebugEmitter() {
  assert(LexicalBlockStack.empty() &&
         "Region stack mismatch, stack not empty!");
}

ApplyDebugLocation::ApplyDebugLocation(FunctionEmitter &FE,
                                       SourceLocation TemporaryLocation)
    : FE(&FE) {
  init(TemporaryLocation);
}

ApplyDebugLocation::ApplyDebugLocation(FunctionEmitter &FE, bool DefaultToEmpty,
                                       SourceLocation TemporaryLocation)
    : FE(&FE) {
  init(TemporaryLocation, DefaultToEmpty);
}

void ApplyDebugLocation::init(SourceLocation TemporaryLocation,
                              bool DefaultToEmpty) {
  auto *DI = FE->getDebugInfo();
  if (!DI) {
    FE = nullptr;
    return;
  }

  OriginalLocation = FE->Builder.getCurrentDebugLocation();

  if (OriginalLocation && !DI->ME.getExpressionLocationsEnabled())
    return;

  if (TemporaryLocation.isValid()) {
    DI->genLocation(FE->Builder, TemporaryLocation);
    return;
  }

  if (DefaultToEmpty) {
    FE->Builder.SetCurrentDebugLocation(llvm::DebugLoc());
    return;
  }

  // Construct a location that has a valid scope, but no line info.
  assert(!DI->LexicalBlockStack.empty());
  FE->Builder.SetCurrentDebugLocation(
      llvm::DILocation::get(DI->LexicalBlockStack.back()->getContext(), 0, 0,
                            DI->LexicalBlockStack.back(), DI->getInlinedAt()));
}

ApplyDebugLocation::ApplyDebugLocation(FunctionEmitter &FE, const Expr *E)
    : FE(&FE) {
  init(E->getExprLoc());
}

ApplyDebugLocation::ApplyDebugLocation(FunctionEmitter &FE, llvm::DebugLoc Loc)
    : FE(&FE) {
  if (!FE.getDebugInfo()) {
    this->FE = nullptr;
    return;
  }
  OriginalLocation = FE.Builder.getCurrentDebugLocation();
  if (Loc)
    FE.Builder.SetCurrentDebugLocation(std::move(Loc));
}

ApplyDebugLocation::~ApplyDebugLocation() {
  // Query FE so the location isn't overwritten when location updates are
  // temporarily disabled.
  if (FE)
    FE->Builder.SetCurrentDebugLocation(std::move(OriginalLocation));
}

ApplyInlineDebugLocation::ApplyInlineDebugLocation(FunctionEmitter &FE,
                                                   GlobalDecl InlinedFn)
    : FE(&FE) {
  if (!FE.getDebugInfo()) {
    this->FE = nullptr;
    return;
  }
  auto &DI = *FE.getDebugInfo();
  SavedLocation = DI.getLocation();
  assert((DI.getInlinedAt() ==
          FE.Builder.getCurrentDebugLocation()->getInlinedAt()) &&
         "DebugEmitter and IRBuilder are out of sync");

  DI.genInlineFunctionStart(FE.Builder, InlinedFn);
}

ApplyInlineDebugLocation::~ApplyInlineDebugLocation() {
  if (!FE)
    return;
  auto &DI = *FE->getDebugInfo();
  DI.genInlineFunctionEnd(FE->Builder);
  DI.genLocation(FE->Builder, SavedLocation);
}

void DebugEmitter::setLocation(SourceLocation Loc) {
  if (Loc.isInvalid())
    return;

  CurLoc = ME.getContext().getSourceManager().getExpansionLoc(Loc);

  // If we've changed files in the middle of a lexical scope go ahead
  // and create a new lexical scope with file node if it's different
  // from the one in the scope.
  if (LexicalBlockStack.empty())
    return;

  SourceManager &SM = ME.getContext().getSourceManager();
  auto *Scope = cast<llvm::DIScope>(LexicalBlockStack.back());
  PresumedLoc PCLoc = SM.getPresumedLoc(CurLoc);
  if (PCLoc.isInvalid() || Scope->getFile() == getOrCreateFile(CurLoc))
    return;

  if (auto *LBF = dyn_cast<llvm::DILexicalBlockFile>(Scope)) {
    LexicalBlockStack.pop_back();
    LexicalBlockStack.emplace_back(DBuilder.createLexicalBlockFile(
        LBF->getScope(), getOrCreateFile(CurLoc)));
  } else if (isa<llvm::DILexicalBlock>(Scope) ||
             isa<llvm::DISubprogram>(Scope)) {
    LexicalBlockStack.pop_back();
    LexicalBlockStack.emplace_back(
        DBuilder.createLexicalBlockFile(Scope, getOrCreateFile(CurLoc)));
  }
}

llvm::DIScope *DebugEmitter::getDeclContextDescriptor(const Decl *D) {
  return getContextDescriptor(cast<Decl>(D->getDeclContext()), TheCU);
}

llvm::DIScope *DebugEmitter::getContextDescriptor(const Decl *Context,
                                                  llvm::DIScope *Default) {
  if (!Context)
    return Default;

  auto I = RegionMap.find(Context);
  if (I != RegionMap.end()) {
    llvm::Metadata *V = I->second;
    return dyn_cast_or_null<llvm::DIScope>(V);
  }

  if (const auto *RDecl = dyn_cast<RecordDecl>(Context))
    return getOrCreateType(ME.getContext().getTypeDeclType(RDecl),
                           TheCU->getFile());
  return Default;
}

PrintingPolicy DebugEmitter::getPrintingPolicy() const {
  PrintingPolicy PP{ME.getContext().getPrintingPolicy()};

  PP.PrintCanonicalTypes = true;
  PP.UseEnumerators = false;

  // Apply -fdebug-prefix-map.
  PP.Callbacks = &PrintCB;
  return PP;
}

llvm::StringRef DebugEmitter::getFunctionName(const FunctionDecl *FD) {
  return internString(GetName(FD));
}

llvm::StringRef DebugEmitter::getRecordName(const RecordDecl *RD) {

  // quick optimization to avoid having to intern strings that are already
  // stored reliably elsewhere
  if (const IdentifierInfo *II = RD->getIdentifier())
    return II->getName();

  return llvm::StringRef();
}

std::optional<llvm::DIFile::ChecksumKind>
DebugEmitter::computeChecksum(FileID FID,
                              llvm::SmallString<64> &Checksum) const {
  Checksum.clear();

  if (ME.getCodeGenOpts().DwarfVersion < 5)
    return std::nullopt;

  SourceManager &SM = ME.getContext().getSourceManager();
  std::optional<llvm::MemoryBufferRef> MemBuffer = SM.getBufferOrNone(FID);
  if (!MemBuffer)
    return std::nullopt;

  auto Data = llvm::arrayRefFromStringRef(MemBuffer->getBuffer());
  switch (ME.getCodeGenOpts().getDebugSrcHash()) {
  case neverc::CodeGenOptions::DSH_MD5:
    llvm::toHex(llvm::MD5::hash(Data), /*LowerCase=*/true, Checksum);
    return llvm::DIFile::CSK_MD5;
  case neverc::CodeGenOptions::DSH_SHA1:
    llvm::toHex(llvm::SHA1::hash(Data), /*LowerCase=*/true, Checksum);
    return llvm::DIFile::CSK_SHA1;
  case neverc::CodeGenOptions::DSH_SHA256:
    llvm::toHex(llvm::SHA256::hash(Data), /*LowerCase=*/true, Checksum);
    return llvm::DIFile::CSK_SHA256;
  }
  llvm_unreachable("Unhandled DebugSrcHashKind enum");
}

std::optional<llvm::StringRef> DebugEmitter::getSource(const SourceManager &SM,
                                                       FileID FID) {
  if (!ME.getCodeGenOpts().EmbedSource)
    return std::nullopt;

  bool SourceInvalid = false;
  llvm::StringRef Source = SM.getBufferData(FID, &SourceInvalid);

  if (SourceInvalid)
    return std::nullopt;

  return Source;
}

llvm::DIFile *DebugEmitter::getOrCreateFile(SourceLocation Loc) {
  SourceManager &SM = ME.getContext().getSourceManager();
  llvm::StringRef FileName;
  FileID FID;
  std::optional<llvm::DIFile::ChecksumInfo<llvm::StringRef>> CSInfo;

  if (Loc.isInvalid()) {
    // The DIFile used by the CU is distinct from the main source file. Call
    // createFile() below for canonicalization if the source file was specified
    // with an absolute path.
    FileName = TheCU->getFile()->getFilename();
    CSInfo = TheCU->getFile()->getChecksum();
  } else {
    PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    FileName = PLoc.getFilename();

    if (FileName.empty()) {
      FileName = TheCU->getFile()->getFilename();
    } else {
      FileName = PLoc.getFilename();
    }
    FID = PLoc.getFileID();
  }

  // Cache the results.
  auto It = DIFileCache.find(FileName.data());
  if (It != DIFileCache.end()) {
    // Verify that the information still exists.
    if (llvm::Metadata *V = It->second)
      return cast<llvm::DIFile>(V);
  }

  // Put Checksum at a scope where it will persist past the createFile call.
  llvm::SmallString<64> Checksum;
  if (!CSInfo) {
    std::optional<llvm::DIFile::ChecksumKind> CSKind =
        computeChecksum(FID, Checksum);
    if (CSKind)
      CSInfo.emplace(*CSKind, Checksum);
  }
  return createFile(FileName, CSInfo, getSource(SM, SM.getFileID(Loc)));
}

llvm::DIFile *DebugEmitter::createFile(
    llvm::StringRef FileName,
    std::optional<llvm::DIFile::ChecksumInfo<llvm::StringRef>> CSInfo,
    std::optional<llvm::StringRef> Source) {
  llvm::StringRef Dir;
  llvm::StringRef File;
  std::string RemappedFile = remapDIPath(FileName);
  std::string CurDir = remapDIPath(getCurrentDirname());
  llvm::SmallString<128> DirBuf;
  llvm::SmallString<128> FileBuf;
  if (llvm::sys::path::is_absolute(RemappedFile)) {
    // Strip the common prefix (if it is more than just "/" or "C:\") from
    // current directory and FileName for a more space-efficient encoding.
    auto FileIt = llvm::sys::path::begin(RemappedFile);
    auto FileE = llvm::sys::path::end(RemappedFile);
    auto CurDirIt = llvm::sys::path::begin(CurDir);
    auto CurDirE = llvm::sys::path::end(CurDir);
    for (; CurDirIt != CurDirE && *CurDirIt == *FileIt; ++CurDirIt, ++FileIt)
      llvm::sys::path::append(DirBuf, *CurDirIt);
    if (llvm::sys::path::root_path(DirBuf) == DirBuf) {
      // Don't strip the common prefix if it is only the root ("/" or "C:\")
      // since that would make LLVM diagnostic locations confusing.
      Dir = {};
      File = RemappedFile;
    } else {
      for (; FileIt != FileE; ++FileIt)
        llvm::sys::path::append(FileBuf, *FileIt);
      Dir = DirBuf;
      File = FileBuf;
    }
  } else {
    if (!llvm::sys::path::is_absolute(FileName))
      Dir = CurDir;
    File = RemappedFile;
  }
  llvm::DIFile *F = DBuilder.createFile(File, Dir, CSInfo, Source);
  DIFileCache[FileName.data()].reset(F);
  return F;
}

std::string DebugEmitter::remapDIPath(llvm::StringRef Path) const {
  llvm::SmallString<256> P = Path;
  for (auto &[From, To] : llvm::reverse(ME.getCodeGenOpts().DebugPrefixMap))
    if (llvm::sys::path::replace_path_prefix(P, From, To))
      break;
  return P.str().str();
}

unsigned DebugEmitter::getLineNumber(SourceLocation Loc) {
  if (Loc.isInvalid())
    return 0;
  SourceManager &SM = ME.getContext().getSourceManager();
  return SM.getPresumedLoc(Loc).getLine();
}

unsigned DebugEmitter::getColumnNumber(SourceLocation Loc, bool Force) {
  if (!Force && !ME.getCodeGenOpts().DebugColumnInfo)
    return 0;

  if (Loc.isInvalid() && CurLoc.isInvalid())
    return 0;
  SourceManager &SM = ME.getContext().getSourceManager();
  PresumedLoc PLoc = SM.getPresumedLoc(Loc.isValid() ? Loc : CurLoc);
  return PLoc.isValid() ? PLoc.getColumn() : 0;
}

llvm::StringRef DebugEmitter::getCurrentDirname() {
  if (!ME.getCodeGenOpts().DebugCompilationDir.empty())
    return ME.getCodeGenOpts().DebugCompilationDir;

  if (!CWDName.empty())
    return CWDName;
  auto CWD = ME.getFileSystem()->getCurrentWorkingDirectory();
  if (!CWD)
    return llvm::StringRef();
  return CWDName = internString(*CWD);
}

// ===----------------------------------------------------------------------===
// Compile unit & source file creation
// ===----------------------------------------------------------------------===

void DebugEmitter::CreateCompileUnit() {
  llvm::SmallString<64> Checksum;
  std::optional<llvm::DIFile::ChecksumKind> CSKind;
  std::optional<llvm::DIFile::ChecksumInfo<llvm::StringRef>> CSInfo;

  // Should we be asking the SourceManager for the main file name, instead of
  // accepting it as an argument? This just causes the main file name to
  // mismatch with source locations and create extra lexical scopes or
  // mismatched debug info (a CU with a DW_AT_file of "-", because that's what
  // the driver passed, but functions/other things have DW_AT_file of "<stdin>"
  // because that's what the SourceManager says)

  SourceManager &SM = ME.getContext().getSourceManager();
  auto &CGO = ME.getCodeGenOpts();
  const LangOptions &LO = ME.getLangOpts();
  std::string MainFileName = CGO.MainFileName;
  if (MainFileName.empty())
    MainFileName = "<stdin>";

  // The main file name provided via the "-main-file-name" option contains just
  // the file name itself with no path information. This file name may have had
  // a relative path, so we look into the actual file entry for the main
  // file to determine the real absolute path for the file.
  std::string MainFileDir;
  if (OptionalFileEntryRef MainFile =
          SM.getFileEntryRefForID(SM.getMainFileID())) {
    MainFileDir = std::string(MainFile->getDir().getName());
    if (!llvm::sys::path::is_absolute(MainFileName)) {
      llvm::SmallString<1024> MainFileDirSS(MainFileDir);
      llvm::sys::path::Style Style =
          LO.UseTargetPathSeparator
              ? (ME.getTarget().getTriple().isOSWindows()
                     ? llvm::sys::path::Style::windows_backslash
                     : llvm::sys::path::Style::posix)
              : llvm::sys::path::Style::native;
      llvm::sys::path::append(MainFileDirSS, Style, MainFileName);
      MainFileName = std::string(
          llvm::sys::path::remove_leading_dotslash(MainFileDirSS, Style));
    }
    // If the main file name provided is identical to the input file name, and
    // if the input file is a preprocessed source, use the module name for
    // debug info. The module name comes from the name specified in the first
    // linemarker if the input is a preprocessed source. In this case we don't
    // know the content to compute a checksum.
    if (MainFile->getName() == MainFileName &&
        FrontendOptions::getInputKindForExtension(
            MainFile->getName().rsplit('.').second)
            .isPreprocessed()) {
      MainFileName = ME.getModule().getName().str();
    } else {
      CSKind = computeChecksum(SM.getMainFileID(), Checksum);
    }
  }

  llvm::dwarf::SourceLanguage LangTag;
  if (LO.C11 && !(CGO.DebugStrictDwarf && CGO.DwarfVersion < 5)) {
    LangTag = llvm::dwarf::DW_LANG_C11;
  } else if (LO.C99) {
    LangTag = llvm::dwarf::DW_LANG_C99;
  } else {
    LangTag = llvm::dwarf::DW_LANG_C89;
  }

  std::string Producer = getNeverCFullVersion();

  unsigned RuntimeVers = 0;

  llvm::DICompileUnit::DebugEmissionKind EmissionKind;
  switch (DebugKind) {
  case llvm::codegenoptions::NoDebugInfo:
  case llvm::codegenoptions::LocTrackingOnly:
    EmissionKind = llvm::DICompileUnit::NoDebug;
    break;
  case llvm::codegenoptions::DebugLineTablesOnly:
    EmissionKind = llvm::DICompileUnit::LineTablesOnly;
    break;
  case llvm::codegenoptions::DebugDirectivesOnly:
    EmissionKind = llvm::DICompileUnit::DebugDirectivesOnly;
    break;
  case llvm::codegenoptions::DebugInfoConstructor:
  case llvm::codegenoptions::LimitedDebugInfo:
  case llvm::codegenoptions::FullDebugInfo:
  case llvm::codegenoptions::UnusedTypeInfo:
    EmissionKind = llvm::DICompileUnit::FullDebug;
    break;
  }

  uint64_t DwoId = 0;
  auto &CGOpts = ME.getCodeGenOpts();
  // The DIFile used by the CU is distinct from the main source
  // file. Its directory part specifies what becomes the
  // DW_AT_comp_dir (the compilation directory), even if the source
  // file was specified with an absolute path.
  if (CSKind)
    CSInfo.emplace(*CSKind, Checksum);
  llvm::DIFile *CUFile = DBuilder.createFile(
      remapDIPath(MainFileName), remapDIPath(getCurrentDirname()), CSInfo,
      getSource(SM, SM.getMainFileID()));

  llvm::StringRef Sysroot, SDK;
  if (ME.getCodeGenOpts().getDebuggerTuning() == llvm::DebuggerKind::LLDB) {
    Sysroot = ME.getHeaderIdxOpts().Sysroot;
    auto B = llvm::sys::path::rbegin(Sysroot);
    auto E = llvm::sys::path::rend(Sysroot);
    auto It =
        std::find_if(B, E, [](auto SDK) { return SDK.ends_with(".sdk"); });
    if (It != E)
      SDK = *It;
  }

  llvm::DICompileUnit::DebugNameTableKind NameTableKind =
      static_cast<llvm::DICompileUnit::DebugNameTableKind>(
          CGOpts.DebugNameTable);
  if (ME.getTarget().getTriple().getVendor() == llvm::Triple::Apple)
    NameTableKind = llvm::DICompileUnit::DebugNameTableKind::Apple;

  TheCU = DBuilder.createCompileUnit(
      LangTag, CUFile, CGOpts.EmitVersionIdentMetadata ? Producer : "",
      LO.Optimize || CGOpts.PrepareForLTO, CGOpts.DwarfDebugFlags, RuntimeVers,
      CGOpts.SplitDwarfFile, EmissionKind, DwoId, CGOpts.SplitDwarfInlining,
      /*DebugInfoForProfiling=*/false, NameTableKind,
      CGOpts.DebugRangesBaseAddress, remapDIPath(Sysroot), SDK);
}

// ===----------------------------------------------------------------------===
// Type creation — scalars, qualifiers & pointers
// ===----------------------------------------------------------------------===

llvm::DIType *DebugEmitter::CreateType(const BuiltinType *BT) {
  llvm::dwarf::TypeKind Encoding;
  llvm::StringRef BTName;
  switch (BT->getKind()) {
#define BUILTIN_TYPE(Id, SingletonId)
#define PLACEHOLDER_TYPE(Id, SingletonId) case BuiltinType::Id:
#include "neverc/Tree/Type/BuiltinTypes.def"
  case BuiltinType::Dependent:
    llvm_unreachable("Unexpected builtin type");
  case BuiltinType::NullPtr:
    return DBuilder.createNullPtrType();
  case BuiltinType::Void:
    return nullptr;

#define SVE_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
    {
      TreeContext::BuiltinVectorTypeInfo Info =
          // For svcount_t, only the lower 2 bytes are relevant.
          BT->getKind() == BuiltinType::SveCount
              ? TreeContext::BuiltinVectorTypeInfo(
                    ME.getContext().BoolTy, llvm::ElementCount::getFixed(16), 1)
              : ME.getContext().getBuiltinVectorTypeInfo(BT);

      // A single vector of bytes may not suffice as the representation of
      // svcount_t tuples because of the gap between the active 16bits of
      // successive tuple members. Currently no such tuples are defined for
      // svcount_t, so assert that NumVectors is 1.
      assert((BT->getKind() != BuiltinType::SveCount || Info.NumVectors == 1) &&
             "Unsupported number of vectors for svcount_t");

      // Debuggers can't extract 1bit from a vector, so will display a
      // bitpattern for predicates instead.
      unsigned NumElems = Info.EC.getKnownMinValue() * Info.NumVectors;
      if (Info.ElementType == ME.getContext().BoolTy) {
        NumElems /= 8;
        Info.ElementType = ME.getContext().UnsignedCharTy;
      }

      llvm::Metadata *LowerBound, *UpperBound;
      LowerBound = llvm::ConstantAsMetadata::get(llvm::ConstantInt::getSigned(
          llvm::Type::getInt64Ty(ME.getLLVMContext()), 0));
      if (Info.EC.isScalable()) {
        unsigned NumElemsPerVG = NumElems / 2;
        llvm::SmallVector<uint64_t, 9> Expr(
            {llvm::dwarf::DW_OP_constu, NumElemsPerVG, llvm::dwarf::DW_OP_bregx,
             /* AArch64::VG */ 46, 0, llvm::dwarf::DW_OP_mul,
             llvm::dwarf::DW_OP_constu, 1, llvm::dwarf::DW_OP_minus});
        UpperBound = DBuilder.createExpression(Expr);
      } else
        UpperBound = llvm::ConstantAsMetadata::get(llvm::ConstantInt::getSigned(
            llvm::Type::getInt64Ty(ME.getLLVMContext()), NumElems - 1));

      llvm::Metadata *Subscript = DBuilder.getOrCreateSubrange(
          /*count*/ nullptr, LowerBound, UpperBound, /*stride*/ nullptr);
      llvm::DINodeArray SubscriptArray = DBuilder.getOrCreateArray(Subscript);
      llvm::DIType *ElemTy =
          getOrCreateType(Info.ElementType, TheCU->getFile());
      auto Align = getTypeAlignIfRequired(BT, ME.getContext());
      return DBuilder.createVectorType(/*Size*/ 0, Align, ElemTy,
                                       SubscriptArray);
    }

  case BuiltinType::UChar:
  case BuiltinType::Char_U:
    Encoding = llvm::dwarf::DW_ATE_unsigned_char;
    break;
  case BuiltinType::Char_S:
  case BuiltinType::SChar:
    Encoding = llvm::dwarf::DW_ATE_signed_char;
    break;
  case BuiltinType::Char8:
  case BuiltinType::Char16:
  case BuiltinType::Char32:
    Encoding = llvm::dwarf::DW_ATE_UTF;
    break;
  case BuiltinType::UShort:
  case BuiltinType::UInt:
  case BuiltinType::UInt128:
  case BuiltinType::ULong:
  case BuiltinType::WChar_U:
  case BuiltinType::ULongLong:
    Encoding = llvm::dwarf::DW_ATE_unsigned;
    break;
  case BuiltinType::Short:
  case BuiltinType::Int:
  case BuiltinType::Int128:
  case BuiltinType::Long:
  case BuiltinType::WChar_S:
  case BuiltinType::LongLong:
    Encoding = llvm::dwarf::DW_ATE_signed;
    break;
  case BuiltinType::Bool:
    Encoding = llvm::dwarf::DW_ATE_boolean;
    break;
  case BuiltinType::Half:
  case BuiltinType::Float:
  case BuiltinType::LongDouble:
  case BuiltinType::Float16:
  case BuiltinType::BFloat16:
  case BuiltinType::Float128:
  case BuiltinType::Double:
    Encoding = llvm::dwarf::DW_ATE_float;
    break;
  case BuiltinType::ShortAccum:
  case BuiltinType::Accum:
  case BuiltinType::LongAccum:
  case BuiltinType::ShortFract:
  case BuiltinType::Fract:
  case BuiltinType::LongFract:
  case BuiltinType::SatShortFract:
  case BuiltinType::SatFract:
  case BuiltinType::SatLongFract:
  case BuiltinType::SatShortAccum:
  case BuiltinType::SatAccum:
  case BuiltinType::SatLongAccum:
    Encoding = llvm::dwarf::DW_ATE_signed_fixed;
    break;
  case BuiltinType::UShortAccum:
  case BuiltinType::UAccum:
  case BuiltinType::ULongAccum:
  case BuiltinType::UShortFract:
  case BuiltinType::UFract:
  case BuiltinType::ULongFract:
  case BuiltinType::SatUShortAccum:
  case BuiltinType::SatUAccum:
  case BuiltinType::SatULongAccum:
  case BuiltinType::SatUShortFract:
  case BuiltinType::SatUFract:
  case BuiltinType::SatULongFract:
    Encoding = llvm::dwarf::DW_ATE_unsigned_fixed;
    break;
  }

  BTName = BT->getName(ME.getLangOpts());
  // Bit size and offset of the type.
  uint64_t Size = ME.getContext().getTypeSize(BT);
  return DBuilder.createBasicType(BTName, Size, Encoding);
}

llvm::DIType *DebugEmitter::CreateType(const BitIntType *Ty) {

  llvm::StringRef Name = Ty->isUnsigned() ? "unsigned _BitInt" : "_BitInt";
  llvm::dwarf::TypeKind Encoding = Ty->isUnsigned()
                                       ? llvm::dwarf::DW_ATE_unsigned
                                       : llvm::dwarf::DW_ATE_signed;

  return DBuilder.createBasicType(Name, ME.getContext().getTypeSize(Ty),
                                  Encoding);
}

llvm::DIType *DebugEmitter::CreateType(const ComplexType *Ty) {
  // Bit size and offset of the type.
  llvm::dwarf::TypeKind Encoding = llvm::dwarf::DW_ATE_complex_float;
  if (Ty->isComplexIntegerType())
    Encoding = llvm::dwarf::DW_ATE_lo_user;

  uint64_t Size = ME.getContext().getTypeSize(Ty);
  return DBuilder.createBasicType("complex", Size, Encoding);
}

namespace {
void stripUnusedQualifiers(Qualifiers &Q) {
  Q.removeAddressSpace();
  Q.removeUnaligned();
}

llvm::dwarf::Tag getNextQualifier(Qualifiers &Q) {
  if (Q.hasConst()) {
    Q.removeConst();
    return llvm::dwarf::DW_TAG_const_type;
  }
  if (Q.hasVolatile()) {
    Q.removeVolatile();
    return llvm::dwarf::DW_TAG_volatile_type;
  }
  if (Q.hasRestrict()) {
    Q.removeRestrict();
    return llvm::dwarf::DW_TAG_restrict_type;
  }
  return (llvm::dwarf::Tag)0;
}
} // namespace

llvm::DIType *DebugEmitter::CreateQualifiedType(QualType Ty,
                                                llvm::DIFile *Unit) {
  QualifierCollector Qc;
  const Type *T = Qc.strip(Ty);

  stripUnusedQualifiers(Qc);

  // We will create one Derived type for one qualifier and recurse to handle any
  // additional ones.
  llvm::dwarf::Tag Tag = getNextQualifier(Qc);
  if (!Tag) {
    assert(Qc.empty() && "Unknown type qualifier for debug info");
    return getOrCreateType(QualType(T, 0), Unit);
  }

  auto *FromTy = getOrCreateType(Qc.apply(ME.getContext(), T), Unit);

  // No need to fill in the Name, Line, Size, Alignment, Offset in case of
  // CVR derived types.
  return DBuilder.createQualifiedType(Tag, FromTy);
}

llvm::DIType *DebugEmitter::CreateQualifiedType(const FunctionProtoType *F,
                                                llvm::DIFile *Unit) {
  FunctionProtoType::ExtProtoInfo EPI = F->getExtProtoInfo();
  Qualifiers &Q = EPI.TypeQuals;
  stripUnusedQualifiers(Q);

  // We will create one Derived type for one qualifier and recurse to handle any
  // additional ones.
  llvm::dwarf::Tag Tag = getNextQualifier(Q);
  if (!Tag) {
    assert(Q.empty() && "Unknown type qualifier for debug info");
    return nullptr;
  }

  auto *FromTy =
      getOrCreateType(ME.getContext().getFunctionType(F->getReturnType(),
                                                      F->getParamTypes(), EPI),
                      Unit);

  // No need to fill in the Name, Line, Size, Alignment, Offset in case of
  // CVR derived types.
  return DBuilder.createQualifiedType(Tag, FromTy);
}

llvm::DIType *DebugEmitter::CreateType(const PointerType *Ty,
                                       llvm::DIFile *Unit) {
  QualType PointeeTy = Ty->getPointeeType();
  uint64_t Size = ME.getContext().getTypeSize(Ty);
  auto Align = getTypeAlignIfRequired(Ty, ME.getContext());
  std::optional<unsigned> DWARFAddressSpace =
      ME.getTarget().getDWARFAddressSpace(
          ME.getTypes().getTargetAddressSpace(PointeeTy));

  llvm::SmallVector<llvm::Metadata *, 4> Annots;
  auto *BTFAttrTy = dyn_cast<BTFTagAttributedType>(PointeeTy);
  while (BTFAttrTy) {
    llvm::StringRef BTFTag = BTFAttrTy->getAttr()->getBTFTypeTag();
    if (!BTFTag.empty()) {
      llvm::Metadata *Ops[2] = {
          llvm::MDString::get(ME.getLLVMContext(),
                              llvm::StringRef("btf_type_tag")),
          llvm::MDString::get(ME.getLLVMContext(), BTFTag)};
      Annots.insert(Annots.begin(),
                    llvm::MDNode::get(ME.getLLVMContext(), Ops));
    }
    BTFAttrTy = dyn_cast<BTFTagAttributedType>(BTFAttrTy->getWrappedType());
  }

  llvm::DINodeArray Annotations = nullptr;
  if (Annots.size() > 0)
    Annotations = DBuilder.getOrCreateArray(Annots);

  return DBuilder.createPointerType(getOrCreateType(PointeeTy, Unit), Size,
                                    Align, DWARFAddressSpace, llvm::StringRef(),
                                    Annotations);
}

namespace {
llvm::dwarf::Tag getTagForRecord(const RecordDecl *RD) {
  if (RD->isUnion())
    return llvm::dwarf::DW_TAG_union_type;
  return llvm::dwarf::DW_TAG_structure_type;
}
} // namespace

llvm::DICompositeType *
DebugEmitter::getOrCreateRecordFwdDecl(const RecordType *Ty,
                                       llvm::DIScope *Ctx) {
  const RecordDecl *RD = Ty->getDecl();
  if (llvm::DIType *T = getTypeOrNull(ME.getContext().getRecordType(RD)))
    return cast<llvm::DICompositeType>(T);
  llvm::DIFile *DefUnit = getOrCreateFile(RD->getLocation());
  const unsigned Line =
      getLineNumber(RD->getLocation().isValid() ? RD->getLocation() : CurLoc);
  llvm::StringRef RDName = getRecordName(RD);

  uint64_t Size = 0;
  uint32_t Align = 0;

  const RecordDecl *D = RD->getDefinition();
  if (D && D->isCompleteDefinition())
    Size = ME.getContext().getTypeSize(Ty);

  llvm::DINode::DIFlags Flags = llvm::DINode::FlagFwdDecl;

  llvm::DICompositeType *RetTy = DBuilder.createReplaceableCompositeType(
      getTagForRecord(RD), RDName, Ctx, DefUnit, Line, 0, Size, Align, Flags);
  ReplaceMap.emplace_back(
      std::piecewise_construct, std::make_tuple(Ty),
      std::make_tuple(static_cast<llvm::Metadata *>(RetTy)));
  return RetTy;
}

// C struct/union members are always public; no access flags needed.

llvm::DIType *DebugEmitter::CreateType(const TypedefType *Ty,
                                       llvm::DIFile *Unit) {
  llvm::DIType *Underlying =
      getOrCreateType(Ty->getDecl()->getUnderlyingType(), Unit);

  if (Ty->getDecl()->hasAttr<NoDebugAttr>())
    return Underlying;

  // We don't set size information, but do specify where the typedef was
  // declared.
  SourceLocation Loc = Ty->getDecl()->getLocation();

  uint32_t Align = getDeclAlignIfRequired(Ty->getDecl(), ME.getContext());
  // Typedefs are derived from some other type.
  llvm::DINodeArray Annotations = CollectBTFDeclTagAnnotations(Ty->getDecl());

  llvm::DINode::DIFlags Flags = llvm::DINode::FlagZero;

  return DBuilder.createTypedef(Underlying, Ty->getDecl()->getName(),
                                getOrCreateFile(Loc), getLineNumber(Loc),
                                getDeclContextDescriptor(Ty->getDecl()), Align,
                                Flags, Annotations);
}

namespace {
unsigned getDwarfCC(CallingConv CC) {
  switch (CC) {
  case CC_C:
    // Avoid emitting DW_AT_calling_convention if the C convention was used.
    return 0;

  case CC_X86StdCall:
    return llvm::dwarf::DW_CC_BORLAND_stdcall;
  case CC_X86FastCall:
    return llvm::dwarf::DW_CC_BORLAND_msfastcall;
  case CC_X86VectorCall:
    return llvm::dwarf::DW_CC_LLVM_vectorcall;
  case CC_Win64:
    return llvm::dwarf::DW_CC_LLVM_Win64;
  case CC_X86_64SysV:
    return llvm::dwarf::DW_CC_LLVM_X86_64SysV;
  case CC_AArch64VectorCall:
  case CC_AArch64SVEPCS:
    return llvm::dwarf::DW_CC_LLVM_AAPCS;

  case CC_PreserveMost:
    return llvm::dwarf::DW_CC_LLVM_PreserveMost;
  case CC_PreserveAll:
    return llvm::dwarf::DW_CC_LLVM_PreserveAll;
  case CC_X86RegCall:
    return llvm::dwarf::DW_CC_LLVM_X86RegCall;
  }
  return 0;
}

llvm::DINode::DIFlags getRefFlags(const FunctionProtoType *) {
  return llvm::DINode::FlagZero;
}
} // namespace

llvm::DIType *DebugEmitter::CreateType(const FunctionType *Ty,
                                       llvm::DIFile *Unit) {
  const auto *FPT = dyn_cast<FunctionProtoType>(Ty);
  if (FPT) {
    if (llvm::DIType *QTy = CreateQualifiedType(FPT, Unit))
      return QTy;
  }

  llvm::SmallVector<llvm::Metadata *, 16> EltTys;
  EltTys.push_back(getOrCreateType(Ty->getReturnType(), Unit));

  llvm::DINode::DIFlags Flags = llvm::DINode::FlagZero;
  if (!FPT) {
    EltTys.push_back(DBuilder.createUnspecifiedParameter());
  } else {
    Flags = getRefFlags(FPT);
    for (const QualType &ParamType : FPT->param_types())
      EltTys.push_back(getOrCreateType(ParamType, Unit));
    if (FPT->isVariadic())
      EltTys.push_back(DBuilder.createUnspecifiedParameter());
  }

  llvm::DITypeRefArray EltTypeArray = DBuilder.getOrCreateTypeArray(EltTys);
  llvm::DIType *F = DBuilder.createSubroutineType(
      EltTypeArray, Flags, getDwarfCC(Ty->getCallConv()));
  return F;
}

llvm::DIDerivedType *
DebugEmitter::createBitFieldType(const FieldDecl *BitFieldDecl,
                                 llvm::DIScope *RecordTy,
                                 const RecordDecl *RD) {
  llvm::StringRef Name = BitFieldDecl->getName();
  QualType Ty = BitFieldDecl->getType();
  if (BitFieldDecl->hasAttr<PreferredTypeAttr>())
    Ty = BitFieldDecl->getAttr<PreferredTypeAttr>()->getType();
  SourceLocation Loc = BitFieldDecl->getLocation();
  llvm::DIFile *VUnit = getOrCreateFile(Loc);
  llvm::DIType *DebugType = getOrCreateType(Ty, VUnit);

  llvm::DIFile *File = getOrCreateFile(Loc);
  unsigned Line = getLineNumber(Loc);

  const BitFieldInfo &BitFieldInfo =
      ME.getTypes().getRecordLayoutInfo(RD).getBitFieldInfo(BitFieldDecl);
  uint64_t SizeInBits = BitFieldInfo.Size;
  assert(SizeInBits > 0 && "found named 0-width bitfield");
  uint64_t StorageOffsetInBits =
      ME.getContext().toBits(BitFieldInfo.StorageOffset);
  uint64_t Offset = BitFieldInfo.Offset;
  uint64_t OffsetInBits = StorageOffsetInBits + Offset;
  llvm::DINode::DIFlags Flags = llvm::DINode::FlagZero;
  llvm::DINodeArray Annotations = CollectBTFDeclTagAnnotations(BitFieldDecl);
  return DBuilder.createBitFieldMemberType(
      RecordTy, Name, File, Line, SizeInBits, OffsetInBits, StorageOffsetInBits,
      Flags, DebugType, Annotations);
}

llvm::DIDerivedType *DebugEmitter::createBitFieldSeparatorIfNeeded(
    const FieldDecl *BitFieldDecl, const llvm::DIDerivedType *BitFieldDI,
    llvm::ArrayRef<llvm::Metadata *> PreviousFieldsDI, const RecordDecl *RD) {

  if (!ME.getTargetCodeGenInfo().shouldEmitDWARFBitFieldSeparators())
    return nullptr;

  // Add a *single* zero-bitfield separator between two non-zero bitfields
  // separated by one or more zero-bitfields. This distinguishes structures
  // with the same memory layout but different ABI register assignment.
  if (PreviousFieldsDI.empty())
    return nullptr;

  // If we already emitted metadata for a 0-length bitfield, nothing to do here.
  auto *PreviousMDEntry =
      PreviousFieldsDI.empty() ? nullptr : PreviousFieldsDI.back();
  auto *PreviousMDField =
      dyn_cast_or_null<llvm::DIDerivedType>(PreviousMDEntry);
  if (!PreviousMDField || !PreviousMDField->isBitField() ||
      PreviousMDField->getSizeInBits() == 0)
    return nullptr;

  auto PreviousBitfield = RD->field_begin();
  std::advance(PreviousBitfield, BitFieldDecl->getFieldIndex() - 1);

  assert(PreviousBitfield->isBitField());

  TreeContext &Context = ME.getContext();
  if (!PreviousBitfield->isZeroLengthBitField(Context))
    return nullptr;

  QualType Ty = PreviousBitfield->getType();
  SourceLocation Loc = PreviousBitfield->getLocation();
  llvm::DIFile *VUnit = getOrCreateFile(Loc);
  llvm::DIType *DebugType = getOrCreateType(Ty, VUnit);
  llvm::DIScope *RecordTy = BitFieldDI->getScope();

  llvm::DIFile *File = getOrCreateFile(Loc);
  unsigned Line = getLineNumber(Loc);

  uint64_t StorageOffsetInBits =
      cast<llvm::ConstantInt>(BitFieldDI->getStorageOffsetInBits())
          ->getZExtValue();

  llvm::DINode::DIFlags Flags = llvm::DINode::FlagZero;
  llvm::DINodeArray Annotations =
      CollectBTFDeclTagAnnotations(*PreviousBitfield);
  return DBuilder.createBitFieldMemberType(
      RecordTy, "", File, Line, 0, StorageOffsetInBits, StorageOffsetInBits,
      Flags, DebugType, Annotations);
}

llvm::DIType *DebugEmitter::createFieldType(
    llvm::StringRef name, QualType type, SourceLocation loc,
    uint64_t offsetInBits, uint32_t AlignInBits, llvm::DIFile *tunit,
    llvm::DIScope *scope, const RecordDecl *RD, llvm::DINodeArray Annotations) {
  llvm::DIType *debugType = getOrCreateType(type, tunit);

  llvm::DIFile *file = getOrCreateFile(loc);
  const unsigned line = getLineNumber(loc.isValid() ? loc : CurLoc);

  uint64_t SizeInBits = 0;
  auto Align = AlignInBits;
  if (!type->isIncompleteArrayType()) {
    TypeInfo TI = ME.getContext().getTypeInfo(type);
    SizeInBits = TI.Width;
    if (!Align)
      Align = getTypeAlignIfRequired(type, ME.getContext());
  }

  llvm::DINode::DIFlags flags = llvm::DINode::FlagZero;
  return DBuilder.createMemberType(scope, name, file, line, SizeInBits, Align,
                                   offsetInBits, flags, debugType, Annotations);
}

void DebugEmitter::CollectRecordNormalField(
    const FieldDecl *field, uint64_t OffsetInBits, llvm::DIFile *tunit,
    llvm::SmallVectorImpl<llvm::Metadata *> &elements, llvm::DIType *RecordTy,
    const RecordDecl *RD) {
  llvm::StringRef name = field->getName();
  QualType type = field->getType();

  // Ignore unnamed fields unless they're anonymous structs/unions.
  if (name.empty() && !type->isRecordType())
    return;

  llvm::DIType *FieldType;
  if (field->isBitField()) {
    llvm::DIDerivedType *BitFieldType;
    FieldType = BitFieldType = createBitFieldType(field, RecordTy, RD);
    if (llvm::DIType *Separator =
            createBitFieldSeparatorIfNeeded(field, BitFieldType, elements, RD))
      elements.push_back(Separator);
  } else {
    auto Align = getDeclAlignIfRequired(field, ME.getContext());
    llvm::DINodeArray Annotations = CollectBTFDeclTagAnnotations(field);
    FieldType = createFieldType(name, type, field->getLocation(), OffsetInBits,
                                Align, tunit, RecordTy, RD, Annotations);
  }

  elements.push_back(FieldType);
}

void DebugEmitter::CollectRecordNestedType(
    const TypeDecl *TD, llvm::SmallVectorImpl<llvm::Metadata *> &elements) {
  QualType Ty = ME.getContext().getTypeDeclType(TD);
  SourceLocation Loc = TD->getLocation();
  llvm::DIType *nestedType = getOrCreateType(Ty, getOrCreateFile(Loc));
  elements.push_back(nestedType);
}

void DebugEmitter::CollectRecordFields(
    const RecordDecl *record, llvm::DIFile *tunit,
    llvm::SmallVectorImpl<llvm::Metadata *> &elements,
    llvm::DICompositeType *RecordTy) {
  {
    const StructRecordLayout &layout =
        ME.getContext().getStructRecordLayout(record);
    unsigned fieldNo = 0;

    for (const auto *I : record->decls())
      if (const auto *field = dyn_cast<FieldDecl>(I)) {
        CollectRecordNormalField(field, layout.getFieldOffset(fieldNo), tunit,
                                 elements, RecordTy, record);
        ++fieldNo;
      }
  }
}

llvm::DINodeArray DebugEmitter::CollectBTFDeclTagAnnotations(const Decl *D) {
  if (!D->hasAttr<BTFDeclTagAttr>())
    return nullptr;

  llvm::SmallVector<llvm::Metadata *, 4> Annotations;
  for (const auto *I : D->specific_attrs<BTFDeclTagAttr>()) {
    llvm::Metadata *Ops[2] = {
        llvm::MDString::get(ME.getLLVMContext(),
                            llvm::StringRef("btf_decl_tag")),
        llvm::MDString::get(ME.getLLVMContext(), I->getBTFDeclTag())};
    Annotations.push_back(llvm::MDNode::get(ME.getLLVMContext(), Ops));
  }
  return DBuilder.getOrCreateArray(Annotations);
}

llvm::DIType *DebugEmitter::getOrCreateRecordType(QualType RTy,
                                                  SourceLocation Loc) {
  assert(ME.getCodeGenOpts().hasReducedDebugInfo());
  llvm::DIType *T = getOrCreateType(RTy, getOrCreateFile(Loc));
  return T;
}

llvm::DIType *DebugEmitter::getOrCreateStandaloneType(QualType D,
                                                      SourceLocation Loc) {
  assert(ME.getCodeGenOpts().hasReducedDebugInfo());
  assert(!D.isNull() && "null type");
  llvm::DIType *T = getOrCreateType(D, getOrCreateFile(Loc));
  assert(T && "could not create debug info for type");

  RetainedTypes.push_back(D.getAsOpaquePtr());
  return T;
}

void DebugEmitter::addHeapAllocSiteMetadata(llvm::CallBase *CI,
                                            QualType AllocatedTy,
                                            SourceLocation Loc) {
  if (ME.getCodeGenOpts().getDebugInfo() <=
      llvm::codegenoptions::DebugLineTablesOnly)
    return;
  llvm::MDNode *node;
  if (AllocatedTy->isVoidType())
    node = llvm::MDNode::get(ME.getLLVMContext(), std::nullopt);
  else
    node = getOrCreateType(AllocatedTy, getOrCreateFile(Loc));

  CI->setMetadata("heapallocsite", node);
}

void DebugEmitter::completeType(const EnumDecl *ED) {
  if (DebugKind <= llvm::codegenoptions::DebugLineTablesOnly)
    return;
  QualType Ty = ME.getContext().getEnumType(ED);
  void *TyPtr = Ty.getAsOpaquePtr();
  auto I = TypeCache.find(TyPtr);
  if (I == TypeCache.end() || !cast<llvm::DIType>(I->second)->isForwardDecl())
    return;
  llvm::DIType *Res = CreateTypeDefinition(Ty->castAs<EnumType>());
  assert(!Res->isForwardDecl());
  TypeCache[TyPtr].reset(Res);
}

void DebugEmitter::completeType(const RecordDecl *RD) {
  completeRequiredType(RD);
}

void DebugEmitter::completeRecordData(const RecordDecl *RD) {
  completeRecord(RD);
}

void DebugEmitter::completeRecord(const RecordDecl *RD) {
  if (DebugKind <= llvm::codegenoptions::DebugLineTablesOnly)
    return;
  QualType Ty = ME.getContext().getRecordType(RD);
  void *TyPtr = Ty.getAsOpaquePtr();
  auto I = TypeCache.find(TyPtr);
  if (I != TypeCache.end() && !cast<llvm::DIType>(I->second)->isForwardDecl())
    return;

  // We want the canonical definition of the structure to not
  // be the typedef. Since that would lead to circular typedef
  // metadata.
  auto [Res, PrefRes] = CreateTypeDefinition(Ty->castAs<RecordType>());
  assert(!Res->isForwardDecl());
  TypeCache[TyPtr].reset(Res);
}

namespace {
bool shouldOmitDefinition(llvm::codegenoptions::DebugInfoKind DebugKind,
                          const RecordDecl *RD) {
  if (DebugKind == llvm::codegenoptions::DebugLineTablesOnly)
    return true;

  return false;
}
} // namespace

void DebugEmitter::completeRequiredType(const RecordDecl *RD) {
  if (shouldOmitDefinition(DebugKind, RD))
    return;

  QualType Ty = ME.getContext().getRecordType(RD);
  llvm::DIType *T = getTypeOrNull(Ty);
  if (T && T->isForwardDecl())
    completeRecordData(RD);
}

llvm::DIType *DebugEmitter::CreateType(const RecordType *Ty) {
  RecordDecl *RD = Ty->getDecl();
  llvm::DIType *T = cast_or_null<llvm::DIType>(getTypeOrNull(QualType(Ty, 0)));
  if (T || shouldOmitDefinition(DebugKind, RD)) {
    if (!T)
      T = getOrCreateRecordFwdDecl(Ty, getDeclContextDescriptor(RD));
    return T;
  }

  auto [Def, Pref] = CreateTypeDefinition(Ty);

  return Pref ? Pref : Def;
}

std::pair<llvm::DIType *, llvm::DIType *>
DebugEmitter::CreateTypeDefinition(const RecordType *Ty) {
  RecordDecl *RD = Ty->getDecl();

  llvm::DIFile *DefUnit = getOrCreateFile(RD->getLocation());

  // Records (structs and unions) can be recursive.  To handle them, we
  // first generate a debug descriptor for the struct as a forward declaration.
  // Then (if it is a definition) we go through and get debug info for all of
  // its members.  Finally, we create a descriptor for the complete type (which
  // may refer to the forward decl if the struct is recursive) and replace all
  // uses of the forward declaration with the final definition.
  llvm::DICompositeType *FwdDecl = getOrCreateLimitedType(Ty);

  const RecordDecl *D = RD->getDefinition();
  if (!D || !D->isCompleteDefinition())
    return {FwdDecl, nullptr};

  // Push the struct on region stack.
  LexicalBlockStack.emplace_back(&*FwdDecl);
  RegionMap[Ty->getDecl()].reset(FwdDecl);

  llvm::SmallVector<llvm::Metadata *, 16> EltTys;
  CollectRecordFields(RD, DefUnit, EltTys, FwdDecl);

  LexicalBlockStack.pop_back();
  RegionMap.erase(Ty->getDecl());

  llvm::DINodeArray Elements = DBuilder.getOrCreateArray(EltTys);
  DBuilder.replaceArrays(FwdDecl, Elements);

  if (FwdDecl->isTemporary())
    FwdDecl =
        llvm::MDNode::replaceWithPermanent(llvm::TempDICompositeType(FwdDecl));

  RegionMap[Ty->getDecl()].reset(FwdDecl);

  return {FwdDecl, nullptr};
}

llvm::DIType *DebugEmitter::CreateType(const VectorType *Ty,
                                       llvm::DIFile *Unit) {
  if (Ty->isExtVectorBoolType()) {
    // Boolean ext_vector_type(N) are special because their real element type
    // (bits of bit size) is not their NeverC element type (_Bool of size byte).
    // For now, we pretend the boolean vector were actually a vector of bytes
    // (where each byte represents 8 bits of the actual vector).
    auto &Ctx = ME.getContext();
    uint64_t Size = ME.getContext().getTypeSize(Ty);
    uint64_t NumVectorBytes = Size / Ctx.getCharWidth();

    // Construct the vector of 'char' type.
    QualType CharVecTy =
        Ctx.getVectorType(Ctx.CharTy, NumVectorBytes, VectorKind::Generic);
    return CreateType(CharVecTy->getAs<VectorType>(), Unit);
  }

  llvm::DIType *ElementTy = getOrCreateType(Ty->getElementType(), Unit);
  int64_t Count = Ty->getNumElements();

  llvm::Metadata *Subscript;
  QualType QTy(Ty, 0);
  auto SizeExpr = SizeExprCache.find(QTy);
  if (SizeExpr != SizeExprCache.end())
    Subscript = DBuilder.getOrCreateSubrange(
        SizeExpr->getSecond() /*count*/, nullptr /*lowerBound*/,
        nullptr /*upperBound*/, nullptr /*stride*/);
  else {
    auto *CountNode =
        llvm::ConstantAsMetadata::get(llvm::ConstantInt::getSigned(
            llvm::Type::getInt64Ty(ME.getLLVMContext()), Count ? Count : -1));
    Subscript = DBuilder.getOrCreateSubrange(
        CountNode /*count*/, nullptr /*lowerBound*/, nullptr /*upperBound*/,
        nullptr /*stride*/);
  }
  llvm::DINodeArray SubscriptArray = DBuilder.getOrCreateArray(Subscript);

  uint64_t Size = ME.getContext().getTypeSize(Ty);
  auto Align = getTypeAlignIfRequired(Ty, ME.getContext());

  return DBuilder.createVectorType(Size, Align, ElementTy, SubscriptArray);
}

llvm::DIType *DebugEmitter::CreateType(const ConstantMatrixType *Ty,
                                       llvm::DIFile *Unit) {

  llvm::DIType *ElementTy = getOrCreateType(Ty->getElementType(), Unit);
  uint64_t Size = ME.getContext().getTypeSize(Ty);
  uint32_t Align = getTypeAlignIfRequired(Ty, ME.getContext());

  llvm::SmallVector<llvm::Metadata *, 2> Subscripts;
  auto *ColumnCountNode =
      llvm::ConstantAsMetadata::get(llvm::ConstantInt::getSigned(
          llvm::Type::getInt64Ty(ME.getLLVMContext()), Ty->getNumColumns()));
  auto *RowCountNode =
      llvm::ConstantAsMetadata::get(llvm::ConstantInt::getSigned(
          llvm::Type::getInt64Ty(ME.getLLVMContext()), Ty->getNumRows()));
  Subscripts.push_back(DBuilder.getOrCreateSubrange(
      ColumnCountNode /*count*/, nullptr /*lowerBound*/, nullptr /*upperBound*/,
      nullptr /*stride*/));
  Subscripts.push_back(DBuilder.getOrCreateSubrange(
      RowCountNode /*count*/, nullptr /*lowerBound*/, nullptr /*upperBound*/,
      nullptr /*stride*/));
  llvm::DINodeArray SubscriptArray = DBuilder.getOrCreateArray(Subscripts);
  return DBuilder.createArrayType(Size, Align, ElementTy, SubscriptArray);
}

llvm::DIType *DebugEmitter::CreateType(const ArrayType *Ty,
                                       llvm::DIFile *Unit) {
  uint64_t Size;
  uint32_t Align;

  if (const auto *VAT = dyn_cast<VariableArrayType>(Ty)) {
    Size = 0;
    Align = getTypeAlignIfRequired(ME.getContext().getBaseElementType(VAT),
                                   ME.getContext());
  } else if (Ty->isIncompleteArrayType()) {
    Size = 0;
    if (Ty->getElementType()->isIncompleteType())
      Align = 0;
    else
      Align = getTypeAlignIfRequired(Ty->getElementType(), ME.getContext());
  } else if (Ty->isIncompleteType()) {
    Size = 0;
    Align = 0;
  } else {
    // Size and align of the whole array, not the element type.
    Size = ME.getContext().getTypeSize(Ty);
    Align = getTypeAlignIfRequired(Ty, ME.getContext());
  }

  llvm::SmallVector<llvm::Metadata *, 8> Subscripts;
  QualType EltTy(Ty, 0);
  while ((Ty = dyn_cast<ArrayType>(EltTy))) {
    // If the number of elements is known, then count is that number. Otherwise,
    // it's -1. This allows us to represent a subrange with an array of 0
    // elements, like this:
    //
    //   struct foo {
    //     int x[0];
    //   };
    int64_t Count = -1; // Count == -1 is an unbounded array.
    if (const auto *CAT = dyn_cast<ConstantArrayType>(Ty))
      Count = CAT->getSize().getZExtValue();
    else if (const auto *VAT = dyn_cast<VariableArrayType>(Ty)) {
      if (Expr *Size = VAT->getSizeExpr()) {
        Expr::EvalResult Result;
        if (Size->EvaluateAsInt(Result, ME.getContext()))
          Count = Result.Val.getInt().getExtValue();
      }
    }

    auto SizeNode = SizeExprCache.find(EltTy);
    if (SizeNode != SizeExprCache.end())
      Subscripts.push_back(DBuilder.getOrCreateSubrange(
          SizeNode->getSecond() /*count*/, nullptr /*lowerBound*/,
          nullptr /*upperBound*/, nullptr /*stride*/));
    else {
      auto *CountNode =
          llvm::ConstantAsMetadata::get(llvm::ConstantInt::getSigned(
              llvm::Type::getInt64Ty(ME.getLLVMContext()), Count));
      Subscripts.push_back(DBuilder.getOrCreateSubrange(
          CountNode /*count*/, nullptr /*lowerBound*/, nullptr /*upperBound*/,
          nullptr /*stride*/));
    }
    EltTy = Ty->getElementType();
  }

  llvm::DINodeArray SubscriptArray = DBuilder.getOrCreateArray(Subscripts);

  return DBuilder.createArrayType(Size, Align, getOrCreateType(EltTy, Unit),
                                  SubscriptArray);
}

llvm::DIType *DebugEmitter::CreateType(const AtomicType *Ty, llvm::DIFile *U) {
  auto *FromTy = getOrCreateType(Ty->getValueType(), U);
  return DBuilder.createQualifiedType(llvm::dwarf::DW_TAG_atomic_type, FromTy);
}

llvm::DIType *DebugEmitter::CreateEnumType(const EnumType *Ty) {
  const EnumDecl *ED = Ty->getDecl();

  uint64_t Size = 0;
  uint32_t Align = 0;
  if (!ED->getTypeForDecl()->isIncompleteType()) {
    Size = ME.getContext().getTypeSize(ED->getTypeForDecl());
    Align = getDeclAlignIfRequired(ED, ME.getContext());
  }

  if (!ED->getDefinition()) {
    llvm::DIScope *EDContext = getDeclContextDescriptor(ED);
    llvm::DIFile *DefUnit = getOrCreateFile(ED->getLocation());
    llvm::TempDIScope TmpContext(DBuilder.createReplaceableCompositeType(
        llvm::dwarf::DW_TAG_enumeration_type, "", TheCU, DefUnit, 0));

    unsigned Line = getLineNumber(ED->getLocation());
    llvm::StringRef EDName = ED->getName();
    llvm::DIType *RetTy = DBuilder.createReplaceableCompositeType(
        llvm::dwarf::DW_TAG_enumeration_type, EDName, EDContext, DefUnit, Line,
        0, Size, Align, llvm::DINode::FlagFwdDecl);

    ReplaceMap.emplace_back(
        std::piecewise_construct, std::make_tuple(Ty),
        std::make_tuple(static_cast<llvm::Metadata *>(RetTy)));
    return RetTy;
  }

  return CreateTypeDefinition(Ty);
}

llvm::DIType *DebugEmitter::CreateTypeDefinition(const EnumType *Ty) {
  const EnumDecl *ED = Ty->getDecl();
  uint64_t Size = 0;
  uint32_t Align = 0;
  if (!ED->getTypeForDecl()->isIncompleteType()) {
    Size = ME.getContext().getTypeSize(ED->getTypeForDecl());
    Align = getDeclAlignIfRequired(ED, ME.getContext());
  }

  llvm::SmallVector<llvm::Metadata *, 16> Enumerators;
  ED = ED->getDefinition();
  for (const auto *Enum : ED->enumerators()) {
    Enumerators.push_back(
        DBuilder.createEnumerator(Enum->getName(), Enum->getInitVal()));
  }

  llvm::DINodeArray EltArray = DBuilder.getOrCreateArray(Enumerators);

  llvm::DIFile *DefUnit = getOrCreateFile(ED->getLocation());
  unsigned Line = getLineNumber(ED->getLocation());
  llvm::DIScope *EnumContext = getDeclContextDescriptor(ED);
  llvm::DIType *IntTy = getOrCreateType(ED->getIntegerType(), DefUnit);
  return DBuilder.createEnumerationType(
      EnumContext, ED->getName(), DefUnit, Line, Size, Align, EltArray, IntTy,
      /*RunTimeLang=*/0, /*Identifier=*/"", /*IsScoped=*/false);
}

llvm::DIMacro *DebugEmitter::CreateMacro(llvm::DIMacroFile *Parent,
                                         unsigned MType, SourceLocation LineLoc,
                                         llvm::StringRef Name,
                                         llvm::StringRef Value) {
  unsigned Line = LineLoc.isInvalid() ? 0 : getLineNumber(LineLoc);
  return DBuilder.createMacro(Parent, Line, MType, Name, Value);
}

llvm::DIMacroFile *DebugEmitter::CreateTempMacroFile(llvm::DIMacroFile *Parent,
                                                     SourceLocation LineLoc,
                                                     SourceLocation FileLoc) {
  llvm::DIFile *FName = getOrCreateFile(FileLoc);
  unsigned Line = LineLoc.isInvalid() ? 0 : getLineNumber(LineLoc);
  return DBuilder.createTempMacroFile(Parent, Line, FName);
}

namespace {
QualType unwrapTypeForDebugInfo(QualType T, const TreeContext &C) {
  Qualifiers Quals;
  do {
    Qualifiers InnerQuals = T.getLocalQualifiers();
    // Qualifiers::operator+() doesn't like it if you add a Qualifier
    // that is already there.
    Quals += Qualifiers::removeCommonQualifiers(Quals, InnerQuals);
    Quals += InnerQuals;
    QualType LastT = T;
    switch (T->getTypeClass()) {
    default:
      return C.getQualifiedType(T.getTypePtr(), Quals);
    case Type::TypeOfExpr:
      T = cast<TypeOfExprType>(T)->getUnderlyingExpr()->getType();
      break;
    case Type::TypeOf:
      T = cast<TypeOfType>(T)->getUnmodifiedType();
      break;
    case Type::Attributed:
      T = cast<AttributedType>(T)->getEquivalentType();
      break;
    case Type::BTFTagAttributed:
      T = cast<BTFTagAttributedType>(T)->getWrappedType();
      break;
    case Type::Elaborated:
      T = cast<ElaboratedType>(T)->getNamedType();
      break;
    case Type::Paren:
      T = cast<ParenType>(T)->getInnerType();
      break;
    case Type::MacroQualified:
      T = cast<MacroQualifiedType>(T)->getUnderlyingType();
      break;
    case Type::Auto: {
      QualType DT = cast<DeducedType>(T)->getDeducedType();
      assert(!DT.isNull() && "Undeduced types shouldn't reach here.");
      T = DT;
      break;
    }
    case Type::Adjusted:
    case Type::Decayed:
      // Decayed and adjusted types use the adjusted type in LLVM and DWARF.
      T = cast<AdjustedType>(T)->getAdjustedType();
      break;
    }

    assert(T != LastT && "Type unwrapping failed to unwrap!");
    (void)LastT;
  } while (true);
}
} // namespace

// ===----------------------------------------------------------------------===
// Type caching & node dispatch
// ===----------------------------------------------------------------------===

llvm::DIType *DebugEmitter::getTypeOrNull(QualType Ty) {
  assert(Ty == unwrapTypeForDebugInfo(Ty, ME.getContext()));
  auto It = TypeCache.find(Ty.getAsOpaquePtr());
  if (It != TypeCache.end()) {
    // Verify that the debug info still exists.
    if (llvm::Metadata *V = It->second)
      return cast<llvm::DIType>(V);
  }

  return nullptr;
}

llvm::DIType *DebugEmitter::getOrCreateType(QualType Ty, llvm::DIFile *Unit) {
  if (Ty.isNull())
    return nullptr;

  llvm::TimeTraceScope TimeScope("DebugType", [&]() -> llvm::SmallString<64> {
    llvm::SmallString<64> Name;
    llvm::raw_svector_ostream OS(Name);
    Ty.print(OS, getPrintingPolicy());
    return Name;
  });

  // Unwrap the type as needed for debug information.
  Ty = unwrapTypeForDebugInfo(Ty, ME.getContext());

  if (auto *T = getTypeOrNull(Ty))
    return T;

  llvm::DIType *Res = CreateTypeNode(Ty, Unit);
  void *TyPtr = Ty.getAsOpaquePtr();

  // And update the type cache.
  TypeCache[TyPtr].reset(Res);

  return Res;
}

llvm::DIType *DebugEmitter::CreateTypeNode(QualType Ty, llvm::DIFile *Unit) {
  if (Ty.hasLocalQualifiers())
    return CreateQualifiedType(Ty, Unit);

  switch (Ty->getTypeClass()) {
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#include "neverc/Tree/TypeNodes.td.h"
    llvm_unreachable("Dependent types cannot show up in debug information");

  case Type::ExtVector:
  case Type::Vector:
    return CreateType(cast<VectorType>(Ty), Unit);
  case Type::ConstantMatrix:
    return CreateType(cast<ConstantMatrixType>(Ty), Unit);
  case Type::Builtin:
    return CreateType(cast<BuiltinType>(Ty));
  case Type::Complex:
    return CreateType(cast<ComplexType>(Ty));
  case Type::Pointer:
    return CreateType(cast<PointerType>(Ty), Unit);
  case Type::Typedef:
    return CreateType(cast<TypedefType>(Ty), Unit);
  case Type::Record:
    return CreateType(cast<RecordType>(Ty));
  case Type::Enum:
    return CreateEnumType(cast<EnumType>(Ty));
  case Type::FunctionProto:
  case Type::FunctionNoProto:
    return CreateType(cast<FunctionType>(Ty), Unit);
  case Type::ConstantArray:
  case Type::VariableArray:
  case Type::IncompleteArray:
    return CreateType(cast<ArrayType>(Ty), Unit);

  case Type::Atomic:
    return CreateType(cast<AtomicType>(Ty), Unit);

  case Type::BitInt:
    return CreateType(cast<BitIntType>(Ty));

  case Type::Auto:
  case Type::Attributed:
  case Type::BTFTagAttributed:
  case Type::Adjusted:
  case Type::Decayed:
  case Type::Elaborated:
  case Type::Paren:
  case Type::MacroQualified:
  case Type::TypeOfExpr:
  case Type::TypeOf:
    break;
  }

  llvm_unreachable("type should have been unwrapped!");
}

llvm::DICompositeType *
DebugEmitter::getOrCreateLimitedType(const RecordType *Ty) {
  QualType QTy(Ty, 0);

  auto *T = cast_or_null<llvm::DICompositeType>(getTypeOrNull(QTy));

  // We may have cached a forward decl when we could have created
  // a non-forward decl. Go ahead and create a non-forward decl
  // now.
  if (T && !T->isForwardDecl())
    return T;

  // Otherwise create the type.
  llvm::DICompositeType *Res = CreateLimitedType(Ty);

  // Propagate members from the declaration to the definition
  // CreateType(const RecordType*) will overwrite this with the members in the
  // correct order if the full type is needed.
  DBuilder.replaceArrays(Res, T ? T->getElements() : llvm::DINodeArray());

  // And update the type cache.
  TypeCache[QTy.getAsOpaquePtr()].reset(Res);
  return Res;
}

llvm::DICompositeType *DebugEmitter::CreateLimitedType(const RecordType *Ty) {
  RecordDecl *RD = Ty->getDecl();

  llvm::StringRef RDName = getRecordName(RD);
  const SourceLocation Loc = RD->getLocation();
  llvm::DIFile *DefUnit = nullptr;
  unsigned Line = 0;
  if (Loc.isValid()) {
    DefUnit = getOrCreateFile(Loc);
    Line = getLineNumber(Loc);
  }

  llvm::DIScope *RDContext = getDeclContextDescriptor(RD);

  // If we ended up creating the type during the context chain construction,
  // just return that.
  auto *T = cast_or_null<llvm::DICompositeType>(
      getTypeOrNull(ME.getContext().getRecordType(RD)));
  if (T && (!T->isForwardDecl() || !RD->getDefinition()))
    return T;

  // If this is just a forward or incomplete declaration, construct an
  // appropriately marked node and just return it.
  const RecordDecl *D = RD->getDefinition();
  if (!D || !D->isCompleteDefinition())
    return getOrCreateRecordFwdDecl(Ty, RDContext);

  uint64_t Size = ME.getContext().getTypeSize(Ty);
  // __attribute__((aligned)) can increase or decrease alignment *except* on a
  // struct or struct member, where it only increases  alignment unless 'packed'
  // is also specified. To handle this case, the `getTypeAlignIfRequired` needs
  // to be used.
  auto Align = getTypeAlignIfRequired(Ty, ME.getContext());

  auto Flags = llvm::DINode::FlagZero;

  llvm::DINodeArray Annotations = CollectBTFDeclTagAnnotations(D);
  llvm::DICompositeType *RealDecl = DBuilder.createReplaceableCompositeType(
      getTagForRecord(RD), RDName, RDContext, DefUnit, Line, 0, Size, Align,
      Flags, /*Identifier=*/"", Annotations);

  switch (RealDecl->getTag()) {
  default:
    llvm_unreachable("invalid composite type tag");

  case llvm::dwarf::DW_TAG_array_type:
  case llvm::dwarf::DW_TAG_enumeration_type:
    break;

  case llvm::dwarf::DW_TAG_structure_type:
  case llvm::dwarf::DW_TAG_union_type:
    RealDecl =
        llvm::MDNode::replaceWithDistinct(llvm::TempDICompositeType(RealDecl));
    break;
  }

  RegionMap[Ty->getDecl()].reset(RealDecl);
  TypeCache[QualType(Ty, 0).getAsOpaquePtr()].reset(RealDecl);

  return RealDecl;
}

llvm::DIType *DebugEmitter::CreateMemberType(llvm::DIFile *Unit, QualType FType,
                                             llvm::StringRef Name,
                                             uint64_t *Offset) {
  llvm::DIType *FieldTy = DebugEmitter::getOrCreateType(FType, Unit);
  uint64_t FieldSize = ME.getContext().getTypeSize(FType);
  auto FieldAlign = getTypeAlignIfRequired(FType, ME.getContext());
  llvm::DIType *Ty =
      DBuilder.createMemberType(Unit, Name, Unit, 0, FieldSize, FieldAlign,
                                *Offset, llvm::DINode::FlagZero, FieldTy);
  *Offset += FieldSize;
  return Ty;
}

void DebugEmitter::collectFunctionDeclProps(GlobalDecl GD, llvm::DIFile *Unit,
                                            llvm::StringRef &Name,
                                            llvm::StringRef &LinkageName,
                                            llvm::DIScope *&FDContext,
                                            llvm::DINodeArray &TParamsArray,
                                            llvm::DINode::DIFlags &Flags) {
  const auto *FD = cast<FunctionDecl>(GD.getCanonicalDecl().getDecl());
  Name = getFunctionName(FD);
  if (FD->getType()->getAs<FunctionProtoType>())
    LinkageName = ME.getMangledName(GD);
  if (FD->hasPrototype())
    Flags |= llvm::DINode::FlagPrototyped;
  // No need to replicate the linkage name if it isn't different from the
  // subprogram name, no need to have it at all unless debug is set to more
  // than just line tables.
  if (LinkageName == Name ||
      DebugKind <= llvm::codegenoptions::DebugLineTablesOnly)
    LinkageName = llvm::StringRef();

  if (ME.getCodeGenOpts().hasReducedDebugInfo()) {
    if (const RecordDecl *RDecl =
            dyn_cast_or_null<RecordDecl>(FD->getDeclContext())) {
      FDContext = getContextDescriptor(RDecl, TheCU);
    }
  }
  if (ME.getCodeGenOpts().hasReducedDebugInfo()) {
    if (FD->isNoReturn())
      Flags |= llvm::DINode::FlagNoReturn;
  }
}

void DebugEmitter::collectVarDeclProps(const VarDecl *VD, llvm::DIFile *&Unit,
                                       unsigned &LineNo, QualType &T,
                                       llvm::StringRef &Name,
                                       llvm::StringRef &LinkageName,
                                       llvm::DIScope *&VDContext) {
  Unit = getOrCreateFile(VD->getLocation());
  LineNo = getLineNumber(VD->getLocation());

  setLocation(VD->getLocation());

  T = VD->getType();
  if (T->isIncompleteArrayType()) {
    // CodeGen turns int[] into int[1] so we'll do the same here.
    llvm::APInt ConstVal(32, 1);
    QualType ET = ME.getContext().getAsArrayType(T)->getElementType();

    T = ME.getContext().getConstantArrayType(ET, ConstVal, nullptr,
                                             ArraySizeModifier::Normal, 0);
  }

  Name = VD->getName();
  if (VD->getDeclContext() && !isa<FunctionDecl>(VD->getDeclContext()))
    LinkageName = ME.getMangledName(VD);
  if (LinkageName == Name)
    LinkageName = llvm::StringRef();

  const DeclContext *DC = VD->getDeclContext();

  VDContext = getContextDescriptor(cast<Decl>(DC), TheCU);
}

llvm::DISubprogram *DebugEmitter::getFunctionFwdDeclOrStub(GlobalDecl GD,
                                                           bool Stub) {
  llvm::DINodeArray TParamsArray;
  llvm::StringRef Name, LinkageName;
  llvm::DINode::DIFlags Flags = llvm::DINode::FlagZero;
  llvm::DISubprogram::DISPFlags SPFlags = llvm::DISubprogram::SPFlagZero;
  SourceLocation Loc = GD.getDecl()->getLocation();
  llvm::DIFile *Unit = getOrCreateFile(Loc);
  llvm::DIScope *DContext = Unit;
  unsigned Line = getLineNumber(Loc);
  collectFunctionDeclProps(GD, Unit, Name, LinkageName, DContext, TParamsArray,
                           Flags);
  auto *FD = cast<FunctionDecl>(GD.getDecl());

  llvm::SmallVector<QualType, 16> ArgTypes;
  for (const ParmVarDecl *Parm : FD->parameters())
    ArgTypes.push_back(Parm->getType());

  CallingConv CC = FD->getType()->castAs<FunctionType>()->getCallConv();
  QualType FnType = ME.getContext().getFunctionType(
      FD->getReturnType(), ArgTypes, FunctionProtoType::ExtProtoInfo(CC));
  if (!FD->isExternallyVisible())
    SPFlags |= llvm::DISubprogram::SPFlagLocalToUnit;
  if (ME.getLangOpts().Optimize)
    SPFlags |= llvm::DISubprogram::SPFlagOptimized;

  if (Stub) {
    Flags |= getCallSiteRelatedAttrs();
    SPFlags |= llvm::DISubprogram::SPFlagDefinition;
    return DBuilder.createFunction(
        DContext, Name, LinkageName, Unit, Line,
        getOrCreateFunctionType(GD.getDecl(), FnType, Unit), 0, Flags, SPFlags,
        TParamsArray.get(), getFunctionDeclaration(FD));
  }

  llvm::DISubprogram *SP = DBuilder.createTempFunctionFwdDecl(
      DContext, Name, LinkageName, Unit, Line,
      getOrCreateFunctionType(GD.getDecl(), FnType, Unit), 0, Flags, SPFlags,
      TParamsArray.get(), getFunctionDeclaration(FD));
  const FunctionDecl *CanonDecl = FD->getCanonicalDecl();
  FwdDeclReplaceMap.emplace_back(std::piecewise_construct,
                                 std::make_tuple(CanonDecl),
                                 std::make_tuple(SP));
  return SP;
}

llvm::DISubprogram *DebugEmitter::getFunctionForwardDeclaration(GlobalDecl GD) {
  return getFunctionFwdDeclOrStub(GD, /* Stub = */ false);
}

llvm::DISubprogram *DebugEmitter::getFunctionStub(GlobalDecl GD) {
  return getFunctionFwdDeclOrStub(GD, /* Stub = */ true);
}

llvm::DIGlobalVariable *
DebugEmitter::getGlobalVariableForwardDeclaration(const VarDecl *VD) {
  QualType T;
  llvm::StringRef Name, LinkageName;
  SourceLocation Loc = VD->getLocation();
  llvm::DIFile *Unit = getOrCreateFile(Loc);
  llvm::DIScope *DContext = Unit;
  unsigned Line = getLineNumber(Loc);

  collectVarDeclProps(VD, Unit, Line, T, Name, LinkageName, DContext);
  auto Align = getDeclAlignIfRequired(VD, ME.getContext());
  auto *GV = DBuilder.createTempGlobalVariableFwdDecl(
      DContext, Name, LinkageName, Unit, Line, getOrCreateType(T, Unit),
      !VD->isExternallyVisible(), nullptr, nullptr, Align);
  FwdDeclReplaceMap.emplace_back(
      std::piecewise_construct,
      std::make_tuple(cast<VarDecl>(VD->getCanonicalDecl())),
      std::make_tuple(static_cast<llvm::Metadata *>(GV)));
  return GV;
}

llvm::DINode *DebugEmitter::getDeclarationOrDefinition(const Decl *D) {
  // We only need a declaration (not a definition) of the type - so use whatever
  // we would otherwise do to get a type for a pointee. (forward declarations in
  // limited debug info, full definitions (if the type definition is available)
  // in unlimited debug info)
  if (const auto *TD = dyn_cast<TypeDecl>(D))
    return getOrCreateType(ME.getContext().getTypeDeclType(TD),
                           getOrCreateFile(TD->getLocation()));
  auto I = DeclCache.find(D->getCanonicalDecl());

  if (I != DeclCache.end()) {
    auto N = I->second;
    if (auto *GVE = dyn_cast_or_null<llvm::DIGlobalVariableExpression>(N))
      return GVE->getVariable();
    return cast<llvm::DINode>(N);
  }

  auto IE = ImportedDeclCache.find(D->getCanonicalDecl());

  if (IE != ImportedDeclCache.end()) {
    auto N = IE->second;
    if (auto *GVE = dyn_cast_or_null<llvm::DIImportedEntity>(N))
      return cast<llvm::DINode>(GVE);
    return dyn_cast_or_null<llvm::DINode>(N);
  }

  // No definition for now. Emit a forward definition that might be
  // merged with a potential upcoming definition.
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    return getFunctionForwardDeclaration(FD);
  else if (const auto *VD = dyn_cast<VarDecl>(D))
    return getGlobalVariableForwardDeclaration(VD);

  return nullptr;
}

llvm::DISubprogram *DebugEmitter::getFunctionDeclaration(const Decl *D) {
  if (!D || DebugKind <= llvm::codegenoptions::DebugLineTablesOnly)
    return nullptr;

  const auto *FD = dyn_cast<FunctionDecl>(D);
  if (!FD)
    return nullptr;

  // Setup context.
  getDeclContextDescriptor(D);

  auto MI = SPCache.find(FD->getCanonicalDecl());
  if (MI != SPCache.end()) {
    auto *SP = dyn_cast_or_null<llvm::DISubprogram>(MI->second);
    if (SP && !SP->isDefinition())
      return SP;
  }

  for (auto *NextFD : FD->redecls()) {
    auto MI = SPCache.find(NextFD->getCanonicalDecl());
    if (MI != SPCache.end()) {
      auto *SP = dyn_cast_or_null<llvm::DISubprogram>(MI->second);
      if (SP && !SP->isDefinition())
        return SP;
    }
  }
  return nullptr;
}

// Construct a DI subroutine type for \p FnType.
llvm::DISubroutineType *DebugEmitter::getOrCreateFunctionType(const Decl *D,
                                                              QualType FnType,
                                                              llvm::DIFile *F) {
  if (!D || DebugKind <= llvm::codegenoptions::DebugLineTablesOnly)
    // Fake but valid subroutine type — without it, -verify fails and
    // subprogram DIE will miss DW_AT_decl_file and DW_AT_decl_line.
    return DBuilder.createSubroutineType(
        DBuilder.getOrCreateTypeArray(std::nullopt));

  const auto *FTy = FnType->getAs<FunctionType>();
  CallingConv CC = FTy ? FTy->getCallConv() : CallingConv::CC_C;

  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    if (FD->isVariadic()) {
      llvm::SmallVector<llvm::Metadata *, 16> EltTys;
      EltTys.push_back(getOrCreateType(FD->getReturnType(), F));
      if (const auto *FPT = dyn_cast<FunctionProtoType>(FnType))
        for (QualType ParamType : FPT->param_types())
          EltTys.push_back(getOrCreateType(ParamType, F));
      EltTys.push_back(DBuilder.createUnspecifiedParameter());
      llvm::DITypeRefArray EltTypeArray = DBuilder.getOrCreateTypeArray(EltTys);
      return DBuilder.createSubroutineType(EltTypeArray, llvm::DINode::FlagZero,
                                           getDwarfCC(CC));
    }

  return cast<llvm::DISubroutineType>(getOrCreateType(FnType, F));
}

QualType DebugEmitter::getFunctionType(
    const FunctionDecl *FD, QualType RetTy,
    const llvm::SmallVectorImpl<const VarDecl *> &Args) {
  CallingConv CC = CallingConv::CC_C;
  if (FD)
    if (const auto *SrcFnTy = FD->getType()->getAs<FunctionType>())
      CC = SrcFnTy->getCallConv();
  llvm::SmallVector<QualType, 16> ArgTypes;
  for (const VarDecl *VD : Args)
    ArgTypes.push_back(VD->getType());
  return ME.getContext().getFunctionType(RetTy, ArgTypes,
                                         FunctionProtoType::ExtProtoInfo(CC));
}

// ===----------------------------------------------------------------------===
// Function & scope debug emission
// ===----------------------------------------------------------------------===

void DebugEmitter::emitFunctionStart(GlobalDecl GD, SourceLocation Loc,
                                     SourceLocation ScopeLoc, QualType FnType,
                                     llvm::Function *Fn) {
  llvm::StringRef Name;
  llvm::StringRef LinkageName;

  FnBeginRegionCount.push_back(LexicalBlockStack.size());

  const Decl *D = GD.getDecl();
  bool HasDecl = (D != nullptr);

  llvm::DINode::DIFlags Flags = llvm::DINode::FlagZero;
  llvm::DISubprogram::DISPFlags SPFlags = llvm::DISubprogram::SPFlagZero;
  llvm::DIFile *Unit = getOrCreateFile(Loc);
  llvm::DIScope *FDContext = Unit;
  llvm::DINodeArray TParamsArray;
  if (!HasDecl) {
    // Use llvm function name.
    LinkageName = Fn->getName();
  } else if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    // If there is a subprogram for this function available then use it.
    auto FI = SPCache.find(FD->getCanonicalDecl());
    if (FI != SPCache.end()) {
      auto *SP = dyn_cast_or_null<llvm::DISubprogram>(FI->second);
      if (SP && SP->isDefinition()) {
        LexicalBlockStack.emplace_back(SP);
        RegionMap[D].reset(SP);
        return;
      }
    }
    collectFunctionDeclProps(GD, Unit, Name, LinkageName, FDContext,
                             TParamsArray, Flags);
  } else {
    Name = Fn->getName();
    Flags |= llvm::DINode::FlagPrototyped;
  }
  if (Name.starts_with("\01"))
    Name = Name.substr(1);

  if (!HasDecl || D->isImplicit() || D->hasAttr<ArtificialAttr>() ||
      isa<VarDecl>(D)) {
    Flags |= llvm::DINode::FlagArtificial;
    // Artificial functions should not silently reuse CurLoc.
    CurLoc = SourceLocation();
  }

  if (Fn->hasLocalLinkage())
    SPFlags |= llvm::DISubprogram::SPFlagLocalToUnit;
  if (ME.getLangOpts().Optimize)
    SPFlags |= llvm::DISubprogram::SPFlagOptimized;

  llvm::DINode::DIFlags FlagsForDef = Flags | getCallSiteRelatedAttrs();
  llvm::DISubprogram::DISPFlags SPFlagsForDef =
      SPFlags | llvm::DISubprogram::SPFlagDefinition;

  const unsigned LineNo = getLineNumber(Loc.isValid() ? Loc : CurLoc);
  unsigned ScopeLine = getLineNumber(ScopeLoc);
  llvm::DISubroutineType *DIFnType = getOrCreateFunctionType(D, FnType, Unit);
  llvm::DISubprogram *Decl = nullptr;
  llvm::DINodeArray Annotations = nullptr;
  if (D) {
    Decl = getFunctionDeclaration(D);
    Annotations = CollectBTFDeclTagAnnotations(D);
  }

  llvm::DISubprogram *SP =
      DBuilder.createFunction(FDContext, Name, LinkageName, Unit, LineNo,
                              DIFnType, ScopeLine, FlagsForDef, SPFlagsForDef,
                              TParamsArray.get(), Decl, nullptr, Annotations);
  Fn->setSubprogram(SP);
  // We might get here with a VarDecl in the case we're generating
  // code for the initialization of globals. Do not record these decls
  // as they will overwrite the actual VarDecl Decl in the cache.
  if (HasDecl && isa<FunctionDecl>(D))
    DeclCache[D->getCanonicalDecl()].reset(SP);

  LexicalBlockStack.emplace_back(SP);

  if (HasDecl)
    RegionMap[D].reset(SP);
}

void DebugEmitter::genFunctionDecl(GlobalDecl GD, SourceLocation Loc,
                                   QualType FnType, llvm::Function *Fn) {
  llvm::StringRef Name;
  llvm::StringRef LinkageName;

  const Decl *D = GD.getDecl();
  if (!D)
    return;

  llvm::TimeTraceScope TimeScope(
      "DebugFunction", [&]() -> llvm::SmallString<64> {
        return llvm::SmallString<64>(GetName(D, true));
      });

  llvm::DINode::DIFlags Flags = llvm::DINode::FlagZero;
  llvm::DIFile *Unit = getOrCreateFile(Loc);
  bool IsDeclForCallSite = Fn ? true : false;
  llvm::DIScope *FDContext =
      IsDeclForCallSite ? Unit : getDeclContextDescriptor(D);
  llvm::DINodeArray TParamsArray;
  if (isa<FunctionDecl>(D)) {
    collectFunctionDeclProps(GD, Unit, Name, LinkageName, FDContext,
                             TParamsArray, Flags);
  } else {
    llvm_unreachable("not a function");
  }
  if (!Name.empty() && Name[0] == '\01')
    Name = Name.substr(1);

  if (D->isImplicit()) {
    Flags |= llvm::DINode::FlagArtificial;
    // Artificial functions without a location should not silently reuse CurLoc.
    if (Loc.isInvalid())
      CurLoc = SourceLocation();
  }
  unsigned LineNo = getLineNumber(Loc);
  unsigned ScopeLine = 0;
  llvm::DISubprogram::DISPFlags SPFlags = llvm::DISubprogram::SPFlagZero;
  if (ME.getLangOpts().Optimize)
    SPFlags |= llvm::DISubprogram::SPFlagOptimized;

  llvm::DINodeArray Annotations = CollectBTFDeclTagAnnotations(D);
  llvm::DISubroutineType *STy = getOrCreateFunctionType(D, FnType, Unit);
  llvm::DISubprogram *SP = DBuilder.createFunction(
      FDContext, Name, LinkageName, Unit, LineNo, STy, ScopeLine, Flags,
      SPFlags, TParamsArray.get(), nullptr, nullptr, Annotations);

  if (IsDeclForCallSite)
    Fn->setSubprogram(SP);

  DBuilder.finalizeSubprogram(SP);
}

void DebugEmitter::genFuncDeclForCallSite(llvm::CallBase *CallOrInvoke,
                                          QualType CalleeType,
                                          const FunctionDecl *CalleeDecl) {
  if (!CallOrInvoke)
    return;
  auto *Func = CallOrInvoke->getCalledFunction();
  if (!Func)
    return;
  if (Func->getSubprogram())
    return;

  // Do not emit a declaration subprogram for a function with nodebug
  // attribute, or if call site info isn't required.
  if (CalleeDecl->hasAttr<NoDebugAttr>() ||
      getCallSiteRelatedAttrs() == llvm::DINode::FlagZero)
    return;

  if (!CalleeDecl->isStatic() && !CalleeDecl->isInlined())
    genFunctionDecl(CalleeDecl, CalleeDecl->getLocation(), CalleeType, Func);
}

void DebugEmitter::genInlineFunctionStart(CGBuilderTy &Builder, GlobalDecl GD) {
  const auto *FD = cast<FunctionDecl>(GD.getDecl());
  auto FI = SPCache.find(FD->getCanonicalDecl());
  llvm::DISubprogram *SP = nullptr;
  if (FI != SPCache.end())
    SP = dyn_cast_or_null<llvm::DISubprogram>(FI->second);
  if (!SP || !SP->isDefinition())
    SP = getFunctionStub(GD);
  FnBeginRegionCount.push_back(LexicalBlockStack.size());
  LexicalBlockStack.emplace_back(SP);
  setInlinedAt(Builder.getCurrentDebugLocation());
  genLocation(Builder, FD->getLocation());
}

void DebugEmitter::genInlineFunctionEnd(CGBuilderTy &Builder) {
  assert(CurInlinedAt && "unbalanced inline scope stack");
  genFunctionEnd(Builder, nullptr);
  setInlinedAt(llvm::DebugLoc(CurInlinedAt).getInlinedAt());
}

void DebugEmitter::genLocation(CGBuilderTy &Builder, SourceLocation Loc) {
  setLocation(Loc);

  if (CurLoc.isInvalid() || CurLoc.isMacroID() || LexicalBlockStack.empty())
    return;

  llvm::MDNode *Scope = LexicalBlockStack.back();
  Builder.SetCurrentDebugLocation(
      llvm::DILocation::get(ME.getLLVMContext(), getLineNumber(CurLoc),
                            getColumnNumber(CurLoc), Scope, CurInlinedAt));
}

void DebugEmitter::CreateLexicalBlock(SourceLocation Loc) {
  llvm::MDNode *Back = nullptr;
  if (!LexicalBlockStack.empty())
    Back = LexicalBlockStack.back().get();
  LexicalBlockStack.emplace_back(DBuilder.createLexicalBlock(
      cast<llvm::DIScope>(Back), getOrCreateFile(CurLoc), getLineNumber(CurLoc),
      getColumnNumber(CurLoc)));
}

void DebugEmitter::AppendAddressSpaceXDeref(
    unsigned AddressSpace, llvm::SmallVectorImpl<uint64_t> &Expr) const {
  std::optional<unsigned> DWARFAddressSpace =
      ME.getTarget().getDWARFAddressSpace(AddressSpace);
  if (!DWARFAddressSpace)
    return;

  Expr.push_back(llvm::dwarf::DW_OP_constu);
  Expr.push_back(*DWARFAddressSpace);
  Expr.push_back(llvm::dwarf::DW_OP_swap);
  Expr.push_back(llvm::dwarf::DW_OP_xderef);
}

void DebugEmitter::genLexicalBlockStart(CGBuilderTy &Builder,
                                        SourceLocation Loc) {
  setLocation(Loc);
  Builder.SetCurrentDebugLocation(llvm::DILocation::get(
      ME.getLLVMContext(), getLineNumber(Loc), getColumnNumber(Loc),
      LexicalBlockStack.back(), CurInlinedAt));

  if (DebugKind <= llvm::codegenoptions::DebugLineTablesOnly)
    return;

  CreateLexicalBlock(Loc);
}

void DebugEmitter::genLexicalBlockEnd(CGBuilderTy &Builder,
                                      SourceLocation Loc) {
  assert(!LexicalBlockStack.empty() && "Region stack mismatch, stack empty!");

  genLocation(Builder, Loc);

  if (DebugKind <= llvm::codegenoptions::DebugLineTablesOnly)
    return;

  LexicalBlockStack.pop_back();
}

void DebugEmitter::genFunctionEnd(CGBuilderTy &Builder, llvm::Function *Fn) {
  assert(!LexicalBlockStack.empty() && "Region stack mismatch, stack empty!");
  unsigned RCount = FnBeginRegionCount.back();
  assert(RCount <= LexicalBlockStack.size() && "Region stack mismatch");

  while (LexicalBlockStack.size() != RCount) {
    // Provide an entry in the line table for the end of the block.
    genLocation(Builder, CurLoc);
    LexicalBlockStack.pop_back();
  }
  FnBeginRegionCount.pop_back();

  if (Fn && Fn->getSubprogram())
    DBuilder.finalizeSubprogram(Fn->getSubprogram());
}

llvm::DILocalVariable *DebugEmitter::genDeclare(const VarDecl *VD,
                                                llvm::Value *Storage,
                                                std::optional<unsigned> ArgNo,
                                                CGBuilderTy &Builder,
                                                const bool UsePointerValue) {
  assert(ME.getCodeGenOpts().hasReducedDebugInfo());
  assert(!LexicalBlockStack.empty() && "Region stack mismatch, stack empty!");
  if (VD->hasAttr<NoDebugAttr>())
    return nullptr;

  bool Unwritten =
      VD->isImplicit() || (isa<Decl>(VD->getDeclContext()) &&
                           cast<Decl>(VD->getDeclContext())->isImplicit());
  llvm::DIFile *Unit = nullptr;
  if (!Unwritten)
    Unit = getOrCreateFile(VD->getLocation());
  llvm::DIType *Ty = getOrCreateType(VD->getType(), Unit);

  if (!Ty)
    return nullptr;

  unsigned Line = 0;
  unsigned Column = 0;
  if (!Unwritten) {
    Line = getLineNumber(VD->getLocation());
    Column = getColumnNumber(VD->getLocation());
  }
  llvm::SmallVector<uint64_t, 13> Expr;
  llvm::DINode::DIFlags Flags = llvm::DINode::FlagZero;
  if (VD->isImplicit())
    Flags |= llvm::DINode::FlagArtificial;

  auto Align = getDeclAlignIfRequired(VD, ME.getContext());

  unsigned AddressSpace = ME.getTypes().getTargetAddressSpace(VD->getType());
  AppendAddressSpaceXDeref(AddressSpace, Expr);

  // Note: Older versions used to emit byval references with an extra
  // DW_OP_deref, because they referenced the IR arg directly instead of
  // referencing an alloca. Newer versions of LLVM don't treat allocas
  // differently from other function arguments when used in a dbg.declare.
  auto *Scope = cast<llvm::DIScope>(LexicalBlockStack.back());
  llvm::StringRef Name = VD->getName();
  if (Name.empty()) {
    if (const auto *RT = dyn_cast<RecordType>(VD->getType())) {
      const RecordDecl *RD = RT->getDecl();
      if (RD->isUnion() && RD->isAnonymousStructOrUnion()) {
        // GDB has trouble finding local variables in anonymous unions, so we
        // emit artificial local variables for each of the members.
        //
        for (const auto *Field : RD->fields()) {
          llvm::DIType *FieldTy = getOrCreateType(Field->getType(), Unit);
          llvm::StringRef FieldName = Field->getName();

          // Ignore unnamed fields. Do not ignore unnamed records.
          if (FieldName.empty() && !isa<RecordType>(Field->getType()))
            continue;

          auto FieldAlign = getDeclAlignIfRequired(Field, ME.getContext());
          auto *D = DBuilder.createAutoVariable(
              Scope, FieldName, Unit, Line, FieldTy, ME.getLangOpts().Optimize,
              Flags | llvm::DINode::FlagArtificial, FieldAlign);

          DBuilder.insertDeclare(Storage, D, DBuilder.createExpression(Expr),
                                 llvm::DILocation::get(ME.getLLVMContext(),
                                                       Line, Column, Scope,
                                                       CurInlinedAt),
                                 Builder.GetInsertBlock());
        }
      }
    }
  }

  if (UsePointerValue) {
    assert(!llvm::is_contained(Expr, llvm::dwarf::DW_OP_deref) &&
           "Debug info already contains DW_OP_deref.");
    Expr.push_back(llvm::dwarf::DW_OP_deref);
  }

  llvm::DILocalVariable *D = nullptr;
  if (ArgNo) {
    llvm::DINodeArray Annotations = CollectBTFDeclTagAnnotations(VD);
    D = DBuilder.createParameterVariable(Scope, Name, *ArgNo, Unit, Line, Ty,
                                         ME.getLangOpts().Optimize, Flags,
                                         Annotations);
  } else {
    D = DBuilder.createAutoVariable(Scope, Name, Unit, Line, Ty,
                                    ME.getLangOpts().Optimize, Flags, Align);
  }
  DBuilder.insertDeclare(Storage, D, DBuilder.createExpression(Expr),
                         llvm::DILocation::get(ME.getLLVMContext(), Line,
                                               Column, Scope, CurInlinedAt),
                         Builder.GetInsertBlock());
  return D;
}

llvm::DILocalVariable *
DebugEmitter::genDeclareOfAutoVariable(const VarDecl *VD, llvm::Value *Storage,
                                       CGBuilderTy &Builder,
                                       const bool UsePointerValue) {
  assert(ME.getCodeGenOpts().hasReducedDebugInfo());
  return genDeclare(VD, Storage, std::nullopt, Builder, UsePointerValue);
}

void DebugEmitter::genLabel(const LabelDecl *D, CGBuilderTy &Builder) {
  assert(ME.getCodeGenOpts().hasReducedDebugInfo());
  assert(!LexicalBlockStack.empty() && "Region stack mismatch, stack empty!");

  if (D->hasAttr<NoDebugAttr>())
    return;

  auto *Scope = cast<llvm::DIScope>(LexicalBlockStack.back());
  llvm::DIFile *Unit = getOrCreateFile(D->getLocation());

  unsigned Line = getLineNumber(D->getLocation());
  unsigned Column = getColumnNumber(D->getLocation());
  llvm::StringRef Name = D->getName();

  auto *L =
      DBuilder.createLabel(Scope, Name, Unit, Line, ME.getLangOpts().Optimize);
  DBuilder.insertLabel(L,
                       llvm::DILocation::get(ME.getLLVMContext(), Line, Column,
                                             Scope, CurInlinedAt),
                       Builder.GetInsertBlock());
}

llvm::DILocalVariable *
DebugEmitter::genDeclareOfArgVariable(const VarDecl *VD, llvm::Value *AI,
                                      unsigned ArgNo, CGBuilderTy &Builder,
                                      bool UsePointerValue) {
  assert(ME.getCodeGenOpts().hasReducedDebugInfo());
  return genDeclare(VD, AI, ArgNo, Builder, UsePointerValue);
}

llvm::DIGlobalVariableExpression *DebugEmitter::CollectAnonRecordDecls(
    const RecordDecl *RD, llvm::DIFile *Unit, unsigned LineNo,
    llvm::StringRef LinkageName, llvm::GlobalVariable *Var,
    llvm::DIScope *DContext) {
  llvm::DIGlobalVariableExpression *GVE = nullptr;

  for (const auto *Field : RD->fields()) {
    llvm::DIType *FieldTy = getOrCreateType(Field->getType(), Unit);
    llvm::StringRef FieldName = Field->getName();

    // Ignore unnamed fields, but recurse into anonymous records.
    if (FieldName.empty()) {
      if (const auto *RT = dyn_cast<RecordType>(Field->getType()))
        GVE = CollectAnonRecordDecls(RT->getDecl(), Unit, LineNo, LinkageName,
                                     Var, DContext);
      continue;
    }
    GVE = DBuilder.createGlobalVariableExpression(
        DContext, FieldName, LinkageName, Unit, LineNo, FieldTy,
        Var->hasLocalLinkage());
    Var->addDebugInfo(GVE);
  }
  return GVE;
}

std::string DebugEmitter::GetName(const Decl *D, bool Qualified) const {
  std::string Name;
  llvm::raw_string_ostream OS(Name);
  const NamedDecl *ND = dyn_cast<NamedDecl>(D);
  if (!ND)
    return Name;
  PrintingPolicy PP{getPrintingPolicy()};
  ND->getNameForDiagnostic(OS, PP, Qualified);
  return Name;
}

void DebugEmitter::genGlobalVariable(llvm::GlobalVariable *Var,
                                     const VarDecl *D) {
  assert(ME.getCodeGenOpts().hasReducedDebugInfo());
  if (D->hasAttr<NoDebugAttr>())
    return;

  llvm::TimeTraceScope TimeScope(
      "DebugGlobalVariable", [&]() -> llvm::SmallString<64> {
        return llvm::SmallString<64>(GetName(D, true));
      });

  auto Cached = DeclCache.find(D->getCanonicalDecl());
  if (Cached != DeclCache.end())
    return Var->addDebugInfo(
        cast<llvm::DIGlobalVariableExpression>(Cached->second));

  llvm::DIFile *Unit = nullptr;
  llvm::DIScope *DContext = nullptr;
  unsigned LineNo;
  llvm::StringRef DeclName, LinkageName;
  QualType T;
  collectVarDeclProps(D, Unit, LineNo, T, DeclName, LinkageName, DContext);

  // Attempt to store one global variable for the declaration - even if we
  // emit a lot of fields.
  llvm::DIGlobalVariableExpression *GVE = nullptr;

  // If this is an anonymous union then we'll want to emit a global
  // variable for each member of the anonymous union so that it's possible
  // to find the name of any field in the union.
  if (T->isUnionType() && DeclName.empty()) {
    const RecordDecl *RD = T->castAs<RecordType>()->getDecl();
    assert(RD->isAnonymousStructOrUnion() &&
           "unnamed non-anonymous struct or union?");
    GVE = CollectAnonRecordDecls(RD, Unit, LineNo, LinkageName, Var, DContext);
  } else {
    auto Align = getDeclAlignIfRequired(D, ME.getContext());

    llvm::SmallVector<uint64_t, 4> Expr;
    unsigned AddressSpace = ME.getTypes().getTargetAddressSpace(D->getType());
    AppendAddressSpaceXDeref(AddressSpace, Expr);

    llvm::DINodeArray Annotations = CollectBTFDeclTagAnnotations(D);
    GVE = DBuilder.createGlobalVariableExpression(
        DContext, DeclName, LinkageName, Unit, LineNo, getOrCreateType(T, Unit),
        Var->hasLocalLinkage(), true,
        Expr.empty() ? nullptr : DBuilder.createExpression(Expr), nullptr,
        nullptr, Align, Annotations);
    Var->addDebugInfo(GVE);
  }
  DeclCache[D->getCanonicalDecl()].reset(GVE);
}

void DebugEmitter::genGlobalVariable(const ValueDecl *VD, const APValue &Init) {
  assert(ME.getCodeGenOpts().hasReducedDebugInfo());
  if (VD->hasAttr<NoDebugAttr>())
    return;
  llvm::TimeTraceScope TimeScope(
      "DebugConstGlobalVariable", [&]() -> llvm::SmallString<64> {
        return llvm::SmallString<64>(GetName(VD, true));
      });

  auto Align = getDeclAlignIfRequired(VD, ME.getContext());
  llvm::DIFile *Unit = getOrCreateFile(VD->getLocation());
  llvm::StringRef Name = VD->getName();
  llvm::DIType *Ty = getOrCreateType(VD->getType(), Unit);

  if (const auto *ECD = dyn_cast<EnumConstantDecl>(VD)) {
    const auto *ED = cast<EnumDecl>(ECD->getDeclContext());
    assert(isa<EnumType>(ED->getTypeForDecl()) && "Enum without EnumType?");

    llvm::DIType *EDTy =
        getOrCreateType(QualType(ED->getTypeForDecl(), 0), Unit);
    assert(EDTy->getTag() == llvm::dwarf::DW_TAG_enumeration_type);
    (void)EDTy;
    return;
  }

  // Do not emit separate definitions for function local consts.
  if (isa<FunctionDecl>(VD->getDeclContext()))
    return;

  VD = cast<ValueDecl>(VD->getCanonicalDecl());
  llvm::DIScope *DContext = getDeclContextDescriptor(VD);

  auto &GV = DeclCache[VD];
  if (GV)
    return;

  llvm::DIExpression *InitExpr = createConstantValueExpression(VD, Init);

  GV.reset(DBuilder.createGlobalVariableExpression(
      DContext, Name, llvm::StringRef(), Unit, getLineNumber(VD->getLocation()),
      Ty, true, true, InitExpr, nullptr, nullptr, Align));
}

void DebugEmitter::genExternalVariable(llvm::GlobalVariable *Var,
                                       const VarDecl *D) {
  assert(ME.getCodeGenOpts().hasReducedDebugInfo());
  if (D->hasAttr<NoDebugAttr>())
    return;

  auto Align = getDeclAlignIfRequired(D, ME.getContext());
  llvm::DIFile *Unit = getOrCreateFile(D->getLocation());
  llvm::StringRef Name = D->getName();
  llvm::DIType *Ty = getOrCreateType(D->getType(), Unit);

  llvm::DIScope *DContext = getDeclContextDescriptor(D);
  llvm::DIGlobalVariableExpression *GVE =
      DBuilder.createGlobalVariableExpression(
          DContext, Name, llvm::StringRef(), Unit,
          getLineNumber(D->getLocation()), Ty, false, false, nullptr, nullptr,
          nullptr, Align);
  Var->addDebugInfo(GVE);
}

void DebugEmitter::genGlobalAlias(const llvm::GlobalValue *GV,
                                  const GlobalDecl GD) {

  assert(GV);

  if (!ME.getCodeGenOpts().hasReducedDebugInfo())
    return;

  const auto *D = cast<ValueDecl>(GD.getDecl());
  if (D->hasAttr<NoDebugAttr>())
    return;

  auto AliaseeDecl = ME.getMangledNameDecl(GV->getName());
  llvm::DINode *DI;

  if (!AliaseeDecl)
    return;
  if (!(DI = getDeclarationOrDefinition(
            AliaseeDecl.getCanonicalDecl().getDecl())))
    return;

  llvm::DIScope *DContext = getDeclContextDescriptor(D);
  auto Loc = D->getLocation();

  llvm::DIImportedEntity *ImportDI = DBuilder.createImportedDeclaration(
      DContext, DI, getOrCreateFile(Loc), getLineNumber(Loc), D->getName());

  // Record this DIE in the cache for nested declaration reference.
  ImportedDeclCache[GD.getCanonicalDecl().getDecl()].reset(ImportDI);
}

void DebugEmitter::AddStringLiteralDebugInfo(llvm::GlobalVariable *GV,
                                             const StringLiteral *S) {
  SourceLocation Loc = S->getStrTokenLoc(0);
  PresumedLoc PLoc = ME.getContext().getSourceManager().getPresumedLoc(Loc);
  if (!PLoc.isValid())
    return;

  llvm::DIFile *File = getOrCreateFile(Loc);
  llvm::DIGlobalVariableExpression *Debug =
      DBuilder.createGlobalVariableExpression(
          nullptr, llvm::StringRef(), llvm::StringRef(), getOrCreateFile(Loc),
          getLineNumber(Loc), getOrCreateType(S->getType(), File), true);
  GV->addDebugInfo(Debug);
}

llvm::DIScope *DebugEmitter::getCurrentContextDescriptor(const Decl *D) {
  if (!LexicalBlockStack.empty())
    return LexicalBlockStack.back();
  return getContextDescriptor(D, TheCU);
}

void DebugEmitter::setDwoId(uint64_t Signature) {
  assert(TheCU && "no main compile unit");
  TheCU->setDWOId(Signature);
}

void DebugEmitter::finalize() {
  // Creating types might create further types - invalidating the current
  // element and the size(), so don't cache/reference them.

  for (const auto &P : ReplaceMap) {
    assert(P.second);
    auto *Ty = cast<llvm::DIType>(P.second);
    assert(Ty->isForwardDecl());

    auto It = TypeCache.find(P.first);
    assert(It != TypeCache.end());
    assert(It->second);

    DBuilder.replaceTemporary(llvm::TempDIType(Ty),
                              cast<llvm::DIType>(It->second));
  }

  for (const auto &P : FwdDeclReplaceMap) {
    assert(P.second);
    llvm::TempMDNode FwdDecl(cast<llvm::MDNode>(P.second));
    llvm::Metadata *Repl;

    auto It = DeclCache.find(P.first);
    // If there has been no definition for the declaration, call RAUW
    // with ourselves, that will destroy the temporary MDNode and
    // replace it with a standard one, avoiding leaking memory.
    if (It == DeclCache.end())
      Repl = P.second;
    else
      Repl = It->second;

    if (auto *GVE = dyn_cast_or_null<llvm::DIGlobalVariableExpression>(Repl))
      Repl = GVE->getVariable();
    DBuilder.replaceTemporary(std::move(FwdDecl), cast<llvm::MDNode>(Repl));
  }

  // We keep our own list of retained types, because we need to look
  // up the final type in the type cache.
  for (auto &RT : RetainedTypes)
    if (auto MD = TypeCache[RT])
      DBuilder.retainType(cast<llvm::DIType>(MD));

  DBuilder.finalize();
}

// Don't ignore in case of explicit cast where it is referenced indirectly.
void DebugEmitter::genExplicitCastType(QualType Ty) {
  if (ME.getCodeGenOpts().hasReducedDebugInfo())
    if (auto *DieTy = getOrCreateType(Ty, TheCU->getFile()))
      DBuilder.retainType(DieTy);
}

void DebugEmitter::genAndRetainType(QualType Ty) {
  if (ME.getCodeGenOpts().hasMaybeUnusedDebugInfo())
    if (auto *DieTy = getOrCreateType(Ty, TheCU->getFile()))
      DBuilder.retainType(DieTy);
}

llvm::DebugLoc DebugEmitter::sourceLocToDebugLoc(SourceLocation Loc) {
  if (LexicalBlockStack.empty())
    return llvm::DebugLoc();

  llvm::MDNode *Scope = LexicalBlockStack.back();
  return llvm::DILocation::get(ME.getLLVMContext(), getLineNumber(Loc),
                               getColumnNumber(Loc), Scope);
}

llvm::DINode::DIFlags DebugEmitter::getCallSiteRelatedAttrs() const {
  // Call site-related attributes are only useful in optimized programs, and
  // when there's a possibility of debugging backtraces.
  if (!ME.getLangOpts().Optimize ||
      DebugKind == llvm::codegenoptions::NoDebugInfo ||
      DebugKind == llvm::codegenoptions::LocTrackingOnly)
    return llvm::DINode::FlagZero;

  // Call site-related attributes are available in DWARF v5. Some debuggers,
  // while not fully DWARF v5-compliant, may accept these attributes as if they
  // were part of DWARF v4.
  bool SupportsDWARFv4Ext =
      ME.getCodeGenOpts().DwarfVersion == 4 &&
      (ME.getCodeGenOpts().getDebuggerTuning() == llvm::DebuggerKind::LLDB ||
       ME.getCodeGenOpts().getDebuggerTuning() == llvm::DebuggerKind::GDB);

  if (!SupportsDWARFv4Ext && ME.getCodeGenOpts().DwarfVersion < 5)
    return llvm::DINode::FlagZero;

  return llvm::DINode::FlagAllCallsDescribed;
}

llvm::DIExpression *
DebugEmitter::createConstantValueExpression(const neverc::ValueDecl *VD,
                                            const APValue &Val) {
  if (ME.getContext().getTypeSize(VD->getType()) > 64)
    return nullptr;

  if (Val.isFloat())
    return DBuilder.createConstantValueExpression(
        Val.getFloat().bitcastToAPInt().getZExtValue());

  if (!Val.isInt())
    return nullptr;

  llvm::APSInt const &ValInt = Val.getInt();
  std::optional<uint64_t> ValIntOpt;
  if (ValInt.isUnsigned())
    ValIntOpt = ValInt.tryZExtValue();
  else if (auto tmp = ValInt.trySExtValue())
    // Transform a signed optional to unsigned optional. When cpp 23 comes,
    // use std::optional::transform
    ValIntOpt = static_cast<uint64_t>(*tmp);

  if (ValIntOpt)
    return DBuilder.createConstantValueExpression(ValIntOpt.value());

  return nullptr;
}
