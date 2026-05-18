#include "Linker/MachO/ConcatOutputSection.h"
#include "Linker/MachO/Config.h"
#include "Linker/MachO/Emit.h"
#include "Linker/MachO/InputFiles.h"
#include "Linker/MachO/InputSection.h"
#include "Linker/MachO/MapFile.h"
#include "Linker/MachO/OutputSection.h"
#include "Linker/MachO/OutputSegment.h"
#include "Linker/MachO/SectionPriorities.h"
#include "Linker/MachO/SymbolTable.h"
#include "Linker/MachO/Symbols.h"
#include "Linker/MachO/SyntheticSections.h"
#include "Linker/MachO/Target.h"
#include "Linker/MachO/UnwindInfoSection.h"

#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/Session.h"
#include "Linker/Core/Support/Chunks.h"
#include "Linker/Core/Support/FileIO.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/xxhash.h"

#include <algorithm>
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

using namespace llvm;
namespace llvm_macho = llvm::MachO;

namespace {
constexpr uint32_t TOOL_NEVERC_LINKER = 4;
using namespace llvm::MachO;
using namespace llvm::sys;
using namespace linker;
using namespace linker::macho;

class LCUuid;

class OutputWriter {
public:
  OutputWriter() : buffer(errorHandler().outputBuffer) {}

  void resolveSpecialSymbols();
  void scanRelocations();
  void computeSymbolLayout();
  template <class LP> void buildOutputLayout();
  template <class LP> void assembleLoadCommands();
  void assignSegmentAddresses();
  void finalizeLinkEdit();
  void layoutSegment(OutputSegment *);

  void allocateOutputBuffer();
  void writeSections();
  void applyARM64Hints();
  void patchFixupChains();
  void computeContentHash();
  void signOutput();
  void writeImage();

  template <class LP> void run();

  ThreadPool workers;
  std::unique_ptr<FileOutputBuffer> &buffer;
  uint64_t addr = 0;
  uint64_t fileOff = 0;
  MachHeaderSection *header = nullptr;
  StringTableSection *stringSec = nullptr;
  SymtabSection *symtabSec = nullptr;
  IndirectSymtabSection *indirectSec = nullptr;
  CodeSignatureSection *codesig = nullptr;
  DataInCodeSection *dataInCode = nullptr;
  FunctionStartsSection *funcStarts = nullptr;

  LCUuid *uuidCmd = nullptr;
  OutputSegment *linkeditSeg = nullptr;
};

class LCDyldInfo final : public LoadCommand {
public:
  LCDyldInfo(RebaseSection *rebaseSection, BindingSection *bindingSection,
             WeakBindingSection *weakBindingSection,
             LazyBindingSection *lazyBindingSection,
             ExportSection *exportSection)
      : rebaseSection(rebaseSection), bindingSection(bindingSection),
        weakBindingSection(weakBindingSection),
        lazyBindingSection(lazyBindingSection), exportSection(exportSection) {}

  uint32_t getSize() const override {
    return sizeof(llvm_macho::dyld_info_command);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::dyld_info_command *>(buf);
    c->cmd = LC_DYLD_INFO_ONLY;
    c->cmdsize = getSize();
    if (rebaseSection->isNeeded()) {
      c->rebase_off = rebaseSection->fileOff;
      c->rebase_size = rebaseSection->getFileSize();
    }
    if (bindingSection->isNeeded()) {
      c->bind_off = bindingSection->fileOff;
      c->bind_size = bindingSection->getFileSize();
    }
    if (weakBindingSection->isNeeded()) {
      c->weak_bind_off = weakBindingSection->fileOff;
      c->weak_bind_size = weakBindingSection->getFileSize();
    }
    if (lazyBindingSection->isNeeded()) {
      c->lazy_bind_off = lazyBindingSection->fileOff;
      c->lazy_bind_size = lazyBindingSection->getFileSize();
    }
    if (exportSection->isNeeded()) {
      c->export_off = exportSection->fileOff;
      c->export_size = exportSection->getFileSize();
    }
  }

  RebaseSection *rebaseSection;
  BindingSection *bindingSection;
  WeakBindingSection *weakBindingSection;
  LazyBindingSection *lazyBindingSection;
  ExportSection *exportSection;
};

class LCSubFramework final : public LoadCommand {
public:
  LCSubFramework(StringRef umbrella) : umbrella(umbrella) {}

  uint32_t getSize() const override {
    return alignToPowerOf2(sizeof(llvm_macho::sub_framework_command) +
                               umbrella.size() + 1,
                           target->wordSize);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::sub_framework_command *>(buf);
    buf += sizeof(llvm_macho::sub_framework_command);

    c->cmd = LC_SUB_FRAMEWORK;
    c->cmdsize = getSize();
    c->umbrella = sizeof(llvm_macho::sub_framework_command);

    memcpy(buf, umbrella.data(), umbrella.size());
    buf[umbrella.size()] = '\0';
  }

private:
  const StringRef umbrella;
};

class LCFunctionStarts final : public LoadCommand {
public:
  explicit LCFunctionStarts(FunctionStartsSection *functionStartsSection)
      : functionStartsSection(functionStartsSection) {}

  uint32_t getSize() const override {
    return sizeof(llvm_macho::linkedit_data_command);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::linkedit_data_command *>(buf);
    c->cmd = LC_FUNCTION_STARTS;
    c->cmdsize = getSize();
    c->dataoff = functionStartsSection->fileOff;
    c->datasize = functionStartsSection->getFileSize();
  }

private:
  FunctionStartsSection *functionStartsSection;
};

class LCDataInCode final : public LoadCommand {
public:
  explicit LCDataInCode(DataInCodeSection *dataInCodeSection)
      : dataInCodeSection(dataInCodeSection) {}

  uint32_t getSize() const override {
    return sizeof(llvm_macho::linkedit_data_command);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::linkedit_data_command *>(buf);
    c->cmd = LC_DATA_IN_CODE;
    c->cmdsize = getSize();
    c->dataoff = dataInCodeSection->fileOff;
    c->datasize = dataInCodeSection->getFileSize();
  }

private:
  DataInCodeSection *dataInCodeSection;
};

class LCDysymtab final : public LoadCommand {
public:
  LCDysymtab(SymtabSection *symtabSection,
             IndirectSymtabSection *indirectSymtabSection)
      : symtabSection(symtabSection),
        indirectSymtabSection(indirectSymtabSection) {}

  uint32_t getSize() const override {
    return sizeof(llvm_macho::dysymtab_command);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::dysymtab_command *>(buf);
    c->cmd = LC_DYSYMTAB;
    c->cmdsize = getSize();

    c->ilocalsym = 0;
    c->iextdefsym = c->nlocalsym = symtabSection->getNumLocalSymbols();
    c->nextdefsym = symtabSection->getNumExternalSymbols();
    c->iundefsym = c->iextdefsym + c->nextdefsym;
    c->nundefsym = symtabSection->getNumUndefinedSymbols();

    c->indirectsymoff = indirectSymtabSection->fileOff;
    c->nindirectsyms = indirectSymtabSection->getNumSymbols();
  }

  SymtabSection *symtabSection;
  IndirectSymtabSection *indirectSymtabSection;
};

template <class LP> class LCSegment final : public LoadCommand {
public:
  LCSegment(StringRef name, OutputSegment *seg) : name(name), seg(seg) {}

  uint32_t getSize() const override {
    return sizeof(typename LP::segment_command) +
           seg->numNonHiddenSections() * sizeof(typename LP::section);
  }

  void writeTo(uint8_t *buf) const override {
    using SegmentCommand = typename LP::segment_command;
    using SectionHeader = typename LP::section;

    auto *c = reinterpret_cast<SegmentCommand *>(buf);
    buf += sizeof(SegmentCommand);

    c->cmd = LP::segmentLCType;
    c->cmdsize = getSize();
    memcpy(c->segname, name.data(), name.size());
    c->fileoff = seg->fileOff;
    c->maxprot = seg->maxProt;
    c->initprot = seg->initProt;

    c->vmaddr = seg->addr;
    c->vmsize = seg->vmSize;
    c->filesize = seg->fileSize;
    c->nsects = seg->numNonHiddenSections();
    c->flags = seg->flags;

    for (const OutputSection *osec : seg->getSections()) {
      if (osec->isHidden())
        continue;

      auto *sectHdr = reinterpret_cast<SectionHeader *>(buf);
      buf += sizeof(SectionHeader);

      memcpy(sectHdr->sectname, osec->name.data(), osec->name.size());
      memcpy(sectHdr->segname, name.data(), name.size());

      sectHdr->addr = osec->addr;
      sectHdr->offset = osec->fileOff;
      sectHdr->align = Log2_32(osec->align);
      sectHdr->flags = osec->flags;
      sectHdr->size = osec->getSize();
      sectHdr->reserved1 = osec->reserved1;
      sectHdr->reserved2 = osec->reserved2;
    }
  }

private:
  StringRef name;
  OutputSegment *seg;
};

class LCMain final : public LoadCommand {
  uint32_t getSize() const override {
    return sizeof(structs::entry_point_command);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<structs::entry_point_command *>(buf);
    c->cmd = LC_MAIN;
    c->cmdsize = getSize();

    if (config->entry->isInStubs())
      c->entryoff =
          in.stubs->fileOff + config->entry->stubsIndex * target->stubSize;
    else
      c->entryoff = config->entry->getVA() - in.header->addr;

    c->stacksize = 0;
  }
};

class LCSymtab final : public LoadCommand {
public:
  LCSymtab(SymtabSection *symtabSection, StringTableSection *stringTableSection)
      : symtabSection(symtabSection), stringTableSection(stringTableSection) {}

  uint32_t getSize() const override {
    return sizeof(llvm_macho::symtab_command);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::symtab_command *>(buf);
    c->cmd = LC_SYMTAB;
    c->cmdsize = getSize();
    c->symoff = symtabSection->fileOff;
    c->nsyms = symtabSection->getNumSymbols();
    c->stroff = stringTableSection->fileOff;
    c->strsize = stringTableSection->getFileSize();
  }

  SymtabSection *symtabSection = nullptr;
  StringTableSection *stringTableSection = nullptr;
};

class LCDylib final : public LoadCommand {
public:
  LCDylib(LoadCommandType type, StringRef path,
          uint32_t compatibilityVersion = 0, uint32_t currentVersion = 0)
      : type(type), path(path), compatibilityVersion(compatibilityVersion),
        currentVersion(currentVersion) {
    instanceCount++;
  }

  uint32_t getSize() const override {
    return alignToPowerOf2(sizeof(llvm_macho::dylib_command) + path.size() + 1,
                           target->wordSize);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::dylib_command *>(buf);
    buf += sizeof(llvm_macho::dylib_command);

    c->cmd = type;
    c->cmdsize = getSize();
    c->dylib.name = sizeof(llvm_macho::dylib_command);
    c->dylib.timestamp = 0;
    c->dylib.compatibility_version = compatibilityVersion;
    c->dylib.current_version = currentVersion;

    memcpy(buf, path.data(), path.size());
    buf[path.size()] = '\0';
  }

  static uint32_t getInstanceCount() { return instanceCount; }
  static void resetInstanceCount() { instanceCount = 0; }

private:
  LoadCommandType type;
  StringRef path;
  uint32_t compatibilityVersion;
  uint32_t currentVersion;
  static uint32_t instanceCount;
};

uint32_t LCDylib::instanceCount = 0;

class LCLoadDylinker final : public LoadCommand {
public:
  uint32_t getSize() const override {
    return alignToPowerOf2(sizeof(llvm_macho::dylinker_command) + path.size() +
                               1,
                           target->wordSize);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::dylinker_command *>(buf);
    buf += sizeof(llvm_macho::dylinker_command);

    c->cmd = LC_LOAD_DYLINKER;
    c->cmdsize = getSize();
    c->name = sizeof(llvm_macho::dylinker_command);

    memcpy(buf, path.data(), path.size());
    buf[path.size()] = '\0';
  }

private:
  // Recent versions of Darwin won't run any binary that has dyld at a
  // different location.
  const StringRef path = "/usr/lib/dyld";
};

class LCRPath final : public LoadCommand {
public:
  explicit LCRPath(StringRef path) : path(path) {}

  uint32_t getSize() const override {
    return alignToPowerOf2(sizeof(llvm_macho::rpath_command) + path.size() + 1,
                           target->wordSize);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::rpath_command *>(buf);
    buf += sizeof(llvm_macho::rpath_command);

    c->cmd = LC_RPATH;
    c->cmdsize = getSize();
    c->path = sizeof(llvm_macho::rpath_command);

    memcpy(buf, path.data(), path.size());
    buf[path.size()] = '\0';
  }

private:
  StringRef path;
};

class LCDyldEnv final : public LoadCommand {
public:
  explicit LCDyldEnv(StringRef name) : name(name) {}

  uint32_t getSize() const override {
    return alignToPowerOf2(sizeof(llvm_macho::dyld_env_command) + name.size() +
                               1,
                           target->wordSize);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::dyld_env_command *>(buf);
    buf += sizeof(llvm_macho::dyld_env_command);

    c->cmd = LC_DYLD_ENVIRONMENT;
    c->cmdsize = getSize();
    c->name = sizeof(llvm_macho::dyld_env_command);

    memcpy(buf, name.data(), name.size());
    buf[name.size()] = '\0';
  }

private:
  StringRef name;
};

class LCMinVersion final : public LoadCommand {
public:
  explicit LCMinVersion(const PlatformInfo &platformInfo)
      : platformInfo(platformInfo) {}

  uint32_t getSize() const override {
    return sizeof(llvm_macho::version_min_command);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::version_min_command *>(buf);
    if (platformInfo.target.Platform != PLATFORM_MACOS)
      llvm_unreachable("invalid platform");
    c->cmd = LC_VERSION_MIN_MACOSX;
    c->cmdsize = getSize();
    c->version = encodeVersion(platformInfo.target.MinDeployment);
    c->sdk = encodeVersion(platformInfo.sdk);
  }

private:
  const PlatformInfo &platformInfo;
};

class LCBuildVersion final : public LoadCommand {
public:
  explicit LCBuildVersion(const PlatformInfo &platformInfo)
      : platformInfo(platformInfo) {}

  const int ntools = 1;

  uint32_t getSize() const override {
    return sizeof(llvm_macho::build_version_command) +
           ntools * sizeof(llvm_macho::build_tool_version);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::build_version_command *>(buf);
    c->cmd = LC_BUILD_VERSION;
    c->cmdsize = getSize();

    c->platform = static_cast<uint32_t>(platformInfo.target.Platform);
    c->minos = encodeVersion(platformInfo.target.MinDeployment);
    c->sdk = encodeVersion(platformInfo.sdk);

    c->ntools = ntools;
    auto *t = reinterpret_cast<llvm_macho::build_tool_version *>(&c[1]);
    t->tool = TOOL_NEVERC_LINKER;
    t->version = encodeVersion(VersionTuple(
        LLVM_VERSION_MAJOR, LLVM_VERSION_MINOR, LLVM_VERSION_PATCH));
  }

private:
  const PlatformInfo &platformInfo;
};

class LCUuid final : public LoadCommand {
public:
  uint32_t getSize() const override { return sizeof(llvm_macho::uuid_command); }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::uuid_command *>(buf);
    c->cmd = LC_UUID;
    c->cmdsize = getSize();
    uuidBuf = c->uuid;
  }

  void writeUuid(uint64_t digest) const {
    static_assert(sizeof(llvm_macho::uuid_command::uuid) == 16);
    memcpy(uuidBuf, "NCC\xa1UU1D", 8);
    memcpy(uuidBuf + 8, &digest, 8);
    // RFC 4122 v3 conformance: fix version/variant bits.
    std::swap(uuidBuf[3], uuidBuf[8]);
    assert((uuidBuf[6] & 0xf0) == 0x30);
    assert((uuidBuf[8] & 0xc0) == 0x80);
  }

  mutable uint8_t *uuidBuf;
};

template <class LP> class LCEncryptionInfo final : public LoadCommand {
public:
  uint32_t getSize() const override {
    return sizeof(typename LP::encryption_info_command);
  }

  void writeTo(uint8_t *buf) const override {
    using EncryptionInfo = typename LP::encryption_info_command;
    auto *c = reinterpret_cast<EncryptionInfo *>(buf);
    buf += sizeof(EncryptionInfo);
    c->cmd = LP::encryptionInfoLCType;
    c->cmdsize = getSize();
    c->cryptoff = in.header->getSize();
    auto it = find_if(outputSegments, [](const OutputSegment *seg) {
      return seg->name == segment_names::text;
    });
    assert(it != outputSegments.end());
    c->cryptsize = (*it)->fileSize - c->cryptoff;
  }
};

class LCCodeSignature final : public LoadCommand {
public:
  LCCodeSignature(CodeSignatureSection *section) : section(section) {}

  uint32_t getSize() const override {
    return sizeof(llvm_macho::linkedit_data_command);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::linkedit_data_command *>(buf);
    c->cmd = LC_CODE_SIGNATURE;
    c->cmdsize = getSize();
    c->dataoff = static_cast<uint32_t>(section->fileOff);
    c->datasize = section->getSize();
  }

  CodeSignatureSection *section;
};

class LCExportsTrie final : public LoadCommand {
public:
  LCExportsTrie(ExportSection *section) : section(section) {}

  uint32_t getSize() const override {
    return sizeof(llvm_macho::linkedit_data_command);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::linkedit_data_command *>(buf);
    c->cmd = LC_DYLD_EXPORTS_TRIE;
    c->cmdsize = getSize();
    c->dataoff = section->fileOff;
    c->datasize = section->getSize();
  }

  ExportSection *section;
};

class LCChainedFixups final : public LoadCommand {
public:
  LCChainedFixups(ChainedFixupsSection *section) : section(section) {}

  uint32_t getSize() const override {
    return sizeof(llvm_macho::linkedit_data_command);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<llvm_macho::linkedit_data_command *>(buf);
    c->cmd = LC_DYLD_CHAINED_FIXUPS;
    c->cmdsize = getSize();
    c->dataoff = section->fileOff;
    c->datasize = section->getSize();
  }

  ChainedFixupsSection *section;
};

} // namespace

void OutputWriter::resolveSpecialSymbols() {
  if (config->entry)
    if (auto *undefined = dyn_cast<Undefined>(config->entry))
      treatUndefinedSymbol(*undefined, "the entry point");

  for (const Symbol *sym : config->explicitUndefineds) {
    if (const auto *undefined = dyn_cast<Undefined>(sym))
      treatUndefinedSymbol(*undefined, "-u");
  }
  // Literal exported-symbol names must be defined, but glob
  // patterns need not match.
  for (const CachedHashStringRef &cachedName :
       config->exportedSymbols.literals) {
    if (const Symbol *sym = symtab->find(cachedName))
      if (const auto *undefined = dyn_cast<Undefined>(sym))
        treatUndefinedSymbol(*undefined, "-exported_symbol(s_list)");
  }
}

namespace {
void prepareSymbolRelocation(Symbol *sym, const InputSection *isec,
                             const linker::macho::Reloc &r) {
  assert(sym->isLive());
  const RelocAttrs &relocAttrs = target->getRelocAttrs(r.type);

  if (relocAttrs.hasAttr(RelocAttrBits::BRANCH)) {
    if (needsBinding(sym))
      in.stubs->addEntry(sym);
  } else if (relocAttrs.hasAttr(RelocAttrBits::GOT)) {
    if (relocAttrs.hasAttr(RelocAttrBits::POINTER) || needsBinding(sym))
      in.got->addEntry(sym);
  } else if (relocAttrs.hasAttr(RelocAttrBits::TLV)) {
    if (needsBinding(sym))
      in.tlvPointers->addEntry(sym);
  } else if (relocAttrs.hasAttr(RelocAttrBits::UNSIGNED)) {
    // TLV section refs are section-relative offsets, no rebase needed.
    if (!(isThreadLocalVariables(isec->getFlags()) && isa<Defined>(sym)))
      addNonLazyBindingEntries(sym, isec, r.offset, r.addend);
  }
}
} // namespace

void OutputWriter::scanRelocations() {
  TimeTraceScope timeScope("Scan relocations");

  const size_t phase1Size = inputSections.size();
  SmallVector<ConcatInputSection *> phase1RelocSections;
  phase1RelocSections.reserve(inputSections.size());
  for (ConcatInputSection *isec : inputSections) {
    if (isec->shouldOmitFromOutput() || isec->relocs.empty())
      continue;
    phase1RelocSections.push_back(isec);
  }
  parallelForEach(phase1RelocSections, [](ConcatInputSection *isec) {
    if (isec->shouldOmitFromOutput())
      return;
    for (auto it = isec->relocs.begin(); it != isec->relocs.end(); ++it) {
      linker::macho::Reloc &r = *it;
      if (auto *referentIsec = r.referent.dyn_cast<InputSection *>())
        r.referent = referentIsec->canonical();
      if (target->hasAttr(r.type, RelocAttrBits::SUBTRAHEND)) {
        ++it;
        if (auto *referentIsec = it->referent.dyn_cast<InputSection *>())
          it->referent = referentIsec->canonical();
      }
    }
  });

  // Phase 2 (serial): resolve undefs and prepare symbol relocs.
  for (size_t i = 0; i < inputSections.size(); ++i) {
    ConcatInputSection *isec = inputSections[i];

    if (isec->shouldOmitFromOutput())
      continue;

    for (auto it = isec->relocs.begin(); it != isec->relocs.end(); ++it) {
      linker::macho::Reloc &r = *it;

      // Late sections need canonicalization.
      if (i >= phase1Size) {
        if (auto *referentIsec = r.referent.dyn_cast<InputSection *>())
          r.referent = referentIsec->canonical();
      }

      if (target->hasAttr(r.type, RelocAttrBits::SUBTRAHEND)) {
        ++it;
        if (i >= phase1Size) {
          if (auto *referentIsec = it->referent.dyn_cast<InputSection *>())
            it->referent = referentIsec->canonical();
        }
        continue;
      }
      if (auto *sym = r.referent.dyn_cast<Symbol *>()) {
        if (auto *undefined = dyn_cast<Undefined>(sym))
          treatUndefinedSymbol(*undefined, isec, r.offset);
        // treatUndefinedSymbol() can replace sym with a DylibSymbol; re-check.
        if (!isa<Undefined>(sym) && validateSymbolRelocation(sym, isec, r))
          prepareSymbolRelocation(sym, isec, r);
      } else {
        if (!r.pcrel) {
          if (config->emitChainedFixups)
            in.chainedFixups->addRebase(isec, r.offset);
          else
            in.rebase->addEntry(isec, r.offset);
        }
      }
    }
  }

  in.unwindInfo->prepare();
}

namespace {
void addNonWeakDefinition(const Defined *defined) {
  if (config->emitChainedFixups)
    in.chainedFixups->setHasNonWeakDefinition();
  else
    in.weakBinding->addNonWeakDefinition(defined);
}
} // namespace

void OutputWriter::computeSymbolLayout() {
  TimeTraceScope timeScope("Scan symbols");
  SmallVector<Defined *> globalDefineds;
  globalDefineds.reserve(symtab->getSymbols().size());
  for (Symbol *sym : symtab->getSymbols()) {
    if (auto *defined = dyn_cast<Defined>(sym)) {
      if (!defined->isLive())
        continue;
      globalDefineds.push_back(defined);
    } else if (const auto *dysym = dyn_cast<DylibSymbol>(sym)) {
      // This branch intentionally doesn't check isLive().
      if (dysym->isDynamicLookup())
        continue;
      dysym->getFile()->refState =
          std::max(dysym->getFile()->refState, dysym->getRefState());
    }
  }

  parallelForEach(globalDefineds,
                  [](Defined *defined) { defined->canonicalize(); });

  for (Defined *defined : globalDefineds) {
    if (defined->overridesWeakDef)
      addNonWeakDefinition(defined);
    if (!defined->isAbsolute() && isCodeSection(defined->isec))
      in.unwindInfo->addSymbol(defined);
  }

  // Canonicalize locals per-file in parallel; merge unwind syms serially.
  SmallVector<const ObjFile *> objFiles;
  size_t totalObjSymbolCount = 0;
  for (const InputFile *file : inputFiles)
    if (auto *objFile = dyn_cast<ObjFile>(file)) {
      objFiles.push_back(objFile);
      totalObjSymbolCount += objFile->symbols.size();
    }

  std::vector<SmallVector<Defined *, 0>> localUnwindSymbols(objFiles.size());
  auto canonicalizeObjLocalSymbols = [&](size_t idx) {
    const ObjFile *objFile = objFiles[idx];
    auto &unwindSymbols = localUnwindSymbols[idx];
    for (Symbol *sym : objFile->symbols)
      if (auto *defined = dyn_cast_or_null<Defined>(sym))
        if (defined->isLive() && !defined->isExternal()) {
          defined->canonicalize();
          if (!defined->isAbsolute() && isCodeSection(defined->isec))
            unwindSymbols.push_back(defined);
        }
  };
  static constexpr size_t kParallelObjFileThreshold = 32;
  static constexpr size_t kParallelSymbolCountThreshold = 50000;
  bool shouldParallelCanonicalize =
      parallel::strategy.ThreadsRequested != 1 &&
      (objFiles.size() >= kParallelObjFileThreshold ||
       totalObjSymbolCount >= kParallelSymbolCountThreshold);
  if (!shouldParallelCanonicalize) {
    for (size_t i = 0; i < objFiles.size(); ++i)
      canonicalizeObjLocalSymbols(i);
  } else {
    parallelFor(0, objFiles.size(), canonicalizeObjLocalSymbols);
  }

  for (const SmallVector<Defined *, 0> &symbols : localUnwindSymbols)
    for (Defined *defined : symbols)
      in.unwindInfo->addSymbol(defined);
}

namespace {
bool useLCBuildVersion(const PlatformInfo &platformInfo) {
  if (platformInfo.target.Platform != PLATFORM_MACOS)
    return true;
  return platformInfo.target.MinDeployment >= VersionTuple(10, 14);
}
} // namespace

template <class LP> void OutputWriter::assembleLoadCommands() {
  uint8_t segIndex = 0;
  for (OutputSegment *seg : outputSegments) {
    in.header->addLoadCommand(make<LCSegment<LP>>(seg->name, seg));
    seg->index = segIndex++;
  }

  if (config->emitChainedFixups) {
    in.header->addLoadCommand(make<LCChainedFixups>(in.chainedFixups));
    in.header->addLoadCommand(make<LCExportsTrie>(in.exports));
  } else {
    in.header->addLoadCommand(make<LCDyldInfo>(
        in.rebase, in.binding, in.weakBinding, in.lazyBinding, in.exports));
  }
  in.header->addLoadCommand(make<LCSymtab>(symtabSec, stringSec));
  in.header->addLoadCommand(make<LCDysymtab>(symtabSec, indirectSec));
  if (!config->umbrella.empty())
    in.header->addLoadCommand(make<LCSubFramework>(config->umbrella));
  if (config->emitEncryptionInfo)
    in.header->addLoadCommand(make<LCEncryptionInfo<LP>>());
  for (StringRef path : config->runtimePaths)
    in.header->addLoadCommand(make<LCRPath>(path));

  switch (config->outputType) {
  case MH_EXECUTE:
    in.header->addLoadCommand(make<LCLoadDylinker>());
    break;
  case MH_DYLIB:
    in.header->addLoadCommand(make<LCDylib>(LC_ID_DYLIB, config->installName,
                                            config->dylibCompatibilityVersion,
                                            config->dylibCurrentVersion));
    break;
  case MH_BUNDLE:
    break;
  default:
    llvm_unreachable("unhandled output file type");
  }

  if (config->generateUuid) {
    uuidCmd = make<LCUuid>();
    in.header->addLoadCommand(uuidCmd);
  }

  if (useLCBuildVersion(config->platformInfo))
    in.header->addLoadCommand(make<LCBuildVersion>(config->platformInfo));
  else
    in.header->addLoadCommand(make<LCMinVersion>(config->platformInfo));

  if (config->outputType == MH_EXECUTE)
    in.header->addLoadCommand(make<LCMain>());

  int64_t dylibOrdinal = 1;
  DenseMap<StringRef, int64_t> ordinalForInstallName;

  std::vector<DylibFile *> dylibFiles;
  for (InputFile *file : inputFiles) {
    if (auto *dylibFile = dyn_cast<DylibFile>(file))
      dylibFiles.push_back(dylibFile);
  }
  for (size_t i = 0; i < dylibFiles.size(); ++i)
    dylibFiles.insert(dylibFiles.end(), dylibFiles[i]->extraDylibs.begin(),
                      dylibFiles[i]->extraDylibs.end());

  for (DylibFile *dylibFile : dylibFiles) {
    if (dylibFile->isBundleLoader) {
      dylibFile->ordinal = BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE;
      // Shortcut since bundle-loader does not re-export the symbols.

      dylibFile->reexport = false;
      continue;
    }

    if (!dylibFile->isReferenced() && !dylibFile->forceNeeded &&
        (!dylibFile->isExplicitlyLinked() || dylibFile->deadStrippable ||
         config->deadStripDylibs))
      continue;

    // Dedup by installName: symlinks and reexports can produce duplicates.
    int64_t &ordinal = ordinalForInstallName[dylibFile->installName];
    if (ordinal) {
      dylibFile->ordinal = ordinal;
      continue;
    }

    ordinal = dylibFile->ordinal = dylibOrdinal++;
    LoadCommandType lcType =
        dylibFile->forceWeakImport || dylibFile->refState == RefState::Weak
            ? LC_LOAD_WEAK_DYLIB
            : LC_LOAD_DYLIB;
    in.header->addLoadCommand(make<LCDylib>(lcType, dylibFile->installName,
                                            dylibFile->compatibilityVersion,
                                            dylibFile->currentVersion));

    if (dylibFile->reexport)
      in.header->addLoadCommand(
          make<LCDylib>(LC_REEXPORT_DYLIB, dylibFile->installName));
  }

  for (const auto &dyldEnv : config->dyldEnvs)
    in.header->addLoadCommand(make<LCDyldEnv>(dyldEnv));

  if (funcStarts)
    in.header->addLoadCommand(make<LCFunctionStarts>(funcStarts));
  if (dataInCode)
    in.header->addLoadCommand(make<LCDataInCode>(dataInCode));
  if (codesig)
    in.header->addLoadCommand(make<LCCodeSignature>(codesig));

  const uint32_t MACOS_MAXPATHLEN = 1024;
  config->headerPad = std::max(
      config->headerPad, (config->headerPadMaxInstallNames
                              ? LCDylib::getInstanceCount() * MACOS_MAXPATHLEN
                              : 0));
}

namespace {
void sortSegmentsAndSections() {
  TimeTraceScope timeScope("Sort layout");
  sortOutputSegments();

  DenseMap<const InputSection *, size_t> isecPriorities =
      priorityBuilder.buildInputSectionPriorities();

  static constexpr size_t kParallelInputSortSectionThreshold = 8;
  static constexpr size_t kParallelInputSortCountThreshold = 8192;
  uint32_t sectionIndex = 0;
  for (OutputSegment *seg : outputSegments) {
    seg->sortOutputSections();

    if (!isecPriorities.empty()) {
      SmallVector<ConcatOutputSection *> mergedSections;
      size_t mergedInputCount = 0;
      for (OutputSection *osec : seg->getSections())
        if (auto *merged = dyn_cast<ConcatOutputSection>(osec))
          if (merged->inputs.size() > 1) {
            mergedSections.push_back(merged);
            mergedInputCount += merged->inputs.size();
          }

      auto sortInputs = [&](ConcatOutputSection *merged) {
        llvm::stable_sort(
            merged->inputs, [&](InputSection *a, InputSection *b) {
              return isecPriorities.lookup(a) > isecPriorities.lookup(b);
            });
      };
      bool shouldParallelSort =
          parallel::strategy.ThreadsRequested != 1 &&
          mergedSections.size() >= kParallelInputSortSectionThreshold &&
          mergedInputCount >= kParallelInputSortCountThreshold;
      if (shouldParallelSort)
        parallelForEach(mergedSections, sortInputs);
      else
        for (ConcatOutputSection *merged : mergedSections)
          sortInputs(merged);
    }

    // Normalize TLV data alignment to the maximum across all TLV sections.
    uint32_t tlvAlign = 0;
    for (const OutputSection *osec : seg->getSections())
      if (isThreadLocalData(osec->flags) && osec->align > tlvAlign)
        tlvAlign = osec->align;

    for (OutputSection *osec : seg->getSections()) {
      if (!osec->isHidden())
        osec->index = ++sectionIndex;
      if (isThreadLocalData(osec->flags)) {
        if (!firstTLVDataSection)
          firstTLVDataSection = osec;
        osec->align = tlvAlign;
      }
    }
  }
}
} // namespace

template <class LP> void OutputWriter::buildOutputLayout() {
  TimeTraceScope timeScope("Build output layout");
  stringSec = make<StringTableSection>();
  symtabSec = makeSymtabSection<LP>(*stringSec);
  indirectSec = make<IndirectSymtabSection>();
  if (config->adhocCodesign)
    codesig = make<CodeSignatureSection>();
  if (config->emitDataInCodeInfo)
    dataInCode = make<DataInCodeSection>();
  if (config->emitFunctionStarts)
    funcStarts = make<FunctionStartsSection>();

  switch (config->outputType) {
  case MH_EXECUTE:
    make<PageZeroSection>();
    break;
  case MH_DYLIB:
  case MH_BUNDLE:
    break;
  default:
    llvm_unreachable("unhandled output file type");
  }

  for (ConcatInputSection *isec : inputSections) {
    if (isec->shouldOmitFromOutput())
      continue;
    ConcatOutputSection *osec = cast<ConcatOutputSection>(isec->parent);
    osec->addInput(isec);
    osec->inputOrder =
        std::min(osec->inputOrder, static_cast<int>(isec->outSecOff));
  }

  for (const auto &it : concatOutputSections) {
    StringRef segname = it.first.first;
    ConcatOutputSection *osec = it.second;
    assert(segname != segment_names::ld);
    if (osec->isNeeded()) {
      if (osec->name == section_names::ehFrame &&
          segname == segment_names::text)
        osec->align = target->wordSize;

      if (isThreadLocalVariables(osec->flags))
        osec->align = std::max<uint32_t>(osec->align, target->wordSize);

      getOrCreateOutputSegment(segname)->addOutputSection(osec);
    }
  }

  for (SyntheticSection *ssec : syntheticSections) {
    auto it = concatOutputSections.find({ssec->segname, ssec->name});
    if (ssec->isNeeded() || ssec->segname == segment_names::linkEdit) {
      if (it == concatOutputSections.end()) {
        getOrCreateOutputSegment(ssec->segname)->addOutputSection(ssec);
      } else {
        fatal("section from " +
              toString(it->second->firstSection()->getFile()) +
              " conflicts with synthetic section " + ssec->segname + "," +
              ssec->name);
      }
    }
  }

  linkeditSeg = getOrCreateOutputSegment(segment_names::linkEdit);
}

void OutputWriter::assignSegmentAddresses() {
  TimeTraceScope timeScope("Assign segment addresses");
  const uint64_t pageSize = target->getPageSize();

  // Finalize concat sections. Parallelize when there are enough to justify
  // the scheduling overhead.
  SmallVector<ConcatOutputSection *, 32> concatSections;
  for (OutputSegment *seg : outputSegments) {
    if (seg == linkeditSeg)
      continue;
    for (OutputSection *osec : seg->getSections())
      if (osec->isNeeded())
        if (auto *cs = dyn_cast<ConcatOutputSection>(osec))
          concatSections.push_back(cs);
  }

  if (concatSections.size() >= 8 && parallel::strategy.ThreadsRequested != 1)
    parallelForEach(concatSections,
                    [](ConcatOutputSection *cs) { cs->finalizeContents(); });
  else
    for (auto *cs : concatSections)
      cs->finalizeContents();

  // Assign monotonically increasing addresses to each segment.
  for (OutputSegment *seg : outputSegments) {
    if (seg == linkeditSeg)
      continue;
    seg->addr = addr;
    layoutSegment(seg);
    fileOff = alignToPowerOf2(fileOff, pageSize);
    addr = alignToPowerOf2(addr, pageSize);
    seg->vmSize = addr - seg->addr;
    seg->fileSize = fileOff - seg->fileOff;
    seg->assignAddressesToStartEndSymbols();
  }
}

void OutputWriter::finalizeLinkEdit() {
  TimeTraceScope timeScope("Finalize LINKEDIT");
  SmallVector<LinkEditSection *, 10> active;
  for (LinkEditSection *s :
       {(LinkEditSection *)in.rebase, (LinkEditSection *)in.binding,
        (LinkEditSection *)in.weakBinding, (LinkEditSection *)in.lazyBinding,
        (LinkEditSection *)in.exports, (LinkEditSection *)in.chainedFixups,
        (LinkEditSection *)symtabSec, (LinkEditSection *)indirectSec,
        (LinkEditSection *)dataInCode, (LinkEditSection *)funcStarts})
    if (s)
      active.push_back(s);

  parallelForEach(active, [](LinkEditSection *s) { s->finalizeContents(); });

  linkeditSeg->addr = addr;
  layoutSegment(linkeditSeg);
  linkeditSeg->vmSize = addr - linkeditSeg->addr;
  linkeditSeg->fileSize = fileOff - linkeditSeg->fileOff;
}

void OutputWriter::layoutSegment(OutputSegment *seg) {
  seg->fileOff = fileOff;

  for (OutputSection *osec : seg->getSections()) {
    if (!osec->isNeeded())
      continue;
    addr = alignToPowerOf2(addr, osec->align);
    fileOff = alignToPowerOf2(fileOff, osec->align);
    osec->addr = addr;
    osec->fileOff = isZeroFill(osec->flags) ? 0 : fileOff;
    osec->finalize();
    osec->assignAddressesToStartEndSymbols();

    addr += osec->getSize();
    fileOff += osec->getFileSize();
  }
}

void OutputWriter::allocateOutputBuffer() {
  Expected<std::unique_ptr<FileOutputBuffer>> bufferOrErr =
      FileOutputBuffer::create(config->outputFile, fileOff,
                               FileOutputBuffer::F_executable);

  if (!bufferOrErr) {
    unlinkAsync(config->outputFile);
    bufferOrErr = FileOutputBuffer::create(config->outputFile, fileOff,
                                           FileOutputBuffer::F_executable);
  }

  if (!bufferOrErr)
    fatal("failed to open " + config->outputFile + ": " +
          llvm::toString(bufferOrErr.takeError()));
  buffer = std::move(*bufferOrErr);
  in.bufferStart = buffer->getBufferStart();

  prefaultBuffer(buffer->getBufferStart(), fileOff);
}

void OutputWriter::writeSections() {
  uint8_t *buf = buffer->getBufferStart();
  std::vector<const OutputSection *> osecs;
  for (const OutputSegment *seg : outputSegments)
    append_range(osecs, seg->getSections());

  parallelForEach(osecs.begin(), osecs.end(), [&](const OutputSection *osec) {
    osec->writeTo(buf + osec->fileOff);
  });
}

void OutputWriter::applyARM64Hints() {
  if (config->arch() != AK_arm64 || config->ignoreOptimizationHints)
    return;

  uint8_t *buf = buffer->getBufferStart();
  TimeTraceScope timeScope("Apply linker optimization hints");
  parallelForEach(inputFiles, [buf](const InputFile *file) {
    if (const auto *objFile = dyn_cast<ObjFile>(file))
      target->applyOptimizationHints(buf, *objFile);
  });
}

void OutputWriter::computeContentHash() {
  TimeTraceScope timeScope("Content hash");

  ArrayRef<uint8_t> data{buffer->getBufferStart(), buffer->getBufferEnd()};
  static constexpr size_t kChunkSize = 512 * 1024;
  std::vector<ArrayRef<uint8_t>> chunks = splitIntoChunks(data, kChunkSize);
  std::vector<uint64_t> hashes(chunks.size() + 1);

  parallelFor(0, chunks.size(),
              [&](size_t i) { hashes[i] = xxh3_64bits(chunks[i]); });

  hashes[chunks.size()] = xxh3_64bits(sys::path::filename(config->finalOutput));
  uint64_t digest = xxh3_64bits({reinterpret_cast<uint8_t *>(hashes.data()),
                                 hashes.size() * sizeof(uint64_t)});
  uuidCmd->writeUuid(digest);
}

void OutputWriter::patchFixupChains() {
  if (!config->emitChainedFixups)
    return;

  const std::vector<Location> &loc = in.chainedFixups->getLocations();
  if (loc.empty())
    return;

  TimeTraceScope timeScope("Build fixup chains");

  const uint64_t pageSize = target->getPageSize();
  constexpr uint32_t stride = 4;

  // Split locations into per-page ranges for parallel processing.
  struct PageRange {
    size_t begin, end;
    const OutputSegment *seg;
  };
  SmallVector<PageRange> ranges;
  for (size_t i = 0, count = loc.size(); i < count;) {
    size_t rangeStart = i;
    const OutputSegment *oseg = loc[i].isec->parent->parent;
    uint64_t pageIdx = loc[i].offset / pageSize;
    ++i;
    while (i < count && loc[i].isec->parent->parent == oseg &&
           (loc[i].offset / pageSize) == pageIdx)
      ++i;
    ranges.push_back({rangeStart, i, oseg});
  }

  parallelForEach(ranges, [&](const PageRange &range) {
    uint8_t *buf = buffer->getBufferStart() + range.seg->fileOff;
    for (size_t i = range.begin + 1; i < range.end; ++i) {
      uint64_t offset = loc[i].offset - loc[i - 1].offset;
      if (offset < target->wordSize || offset % stride != 0) {
        error(loc[i].isec->getSegName() + "," + loc[i].isec->getName() +
              ": fixup chain stride violation");
        return;
      }
      reinterpret_cast<dyld_chained_ptr_64_bind *>(buf + loc[i - 1].offset)
          ->next = offset / stride;
    }
  });
}

void OutputWriter::signOutput() {
  if (codesig) {
    TimeTraceScope timeScope("Sign output");
    codesig->writeHashes(buffer->getBufferStart());
  }
}

void OutputWriter::writeImage() {
  TimeTraceScope timeScope("Write image");
  allocateOutputBuffer();
  reportPendingUndefinedSymbols();
  if (errorCount())
    return;
  writeSections();

  // ARM64 hints (__TEXT) and fixup chains (__DATA) touch disjoint
  // regions; always overlap when both are needed.
  bool hasHints =
      config->arch() == AK_arm64 && !config->ignoreOptimizationHints;
  if (hasHints && config->emitChainedFixups) {
    workers.async([this] { applyARM64Hints(); });
    patchFixupChains();
    workers.wait();
  } else {
    applyARM64Hints();
    patchFixupChains();
  }

  if (config->generateUuid)
    computeContentHash();
  signOutput();

  if (auto e = buffer->commit())
    fatal("failed to write output '" + buffer->getPath() +
          "': " + toString(std::move(e)));
}

template <class LP> void OutputWriter::run() {
  // Phase 1: symbol and relocation resolution.
  resolveSpecialSymbols();
  if (config->entry && needsBinding(config->entry))
    in.stubs->addEntry(config->entry);

  computeSymbolLayout();
  scanRelocations();
  if (in.initOffsets->isNeeded())
    in.initOffsets->setUp();

  reportPendingUndefinedSymbols();
  reportPendingDuplicateSymbols();
  if (errorCount())
    return;

  if (in.stubHelper && in.stubHelper->isNeeded())
    in.stubHelper->setUp();

  // Phase 2: layout computation.
  buildOutputLayout<LP>();
  sortSegmentsAndSections();
  assembleLoadCommands<LP>();
  assignSegmentAddresses();

  // Phase 3: finalize and emit. Map file runs concurrently with LINKEDIT.
  if (!config->mapFile.empty()) {
    workers.async([&] {
      if (LLVM_ENABLE_THREADS && config->driverCfg->timeTraceEnabled)
        timeTraceProfilerInitialize(config->driverCfg->timeTraceGranularity,
                                    "mapFile");
      writeMapFile();
      if (LLVM_ENABLE_THREADS && config->driverCfg->timeTraceEnabled)
        timeTraceProfilerFinishThread();
    });
  }
  finalizeLinkEdit();
  writeImage();
}

template <class LP> void macho::writeOutput() { OutputWriter().run<LP>(); }

void macho::resetEmitState() { LCDylib::resetInstanceCount(); }

void macho::createSyntheticSections() {
  in.header = make<MachHeaderSection>();
  if (config->dedupStrings)
    in.cStringSection =
        make<DeduplicatedCStringSection>(section_names::cString);
  else
    in.cStringSection = make<CStringSection>(section_names::cString);
  in.wordLiteralSection = make<WordLiteralSection>();
  if (config->emitChainedFixups) {
    in.chainedFixups = make<ChainedFixupsSection>();
  } else {
    in.rebase = make<RebaseSection>();
    in.binding = make<BindingSection>();
    in.weakBinding = make<WeakBindingSection>();
    in.lazyBinding = make<LazyBindingSection>();
    in.lazyPointers = make<LazyPointerSection>();
    in.stubHelper = make<StubHelperSection>();
  }
  in.exports = make<ExportSection>();
  in.got = make<GotSection>();
  in.tlvPointers = make<TlvPointerSection>();
  in.stubs = make<StubsSection>();
  in.unwindInfo = makeUnwindInfoSection();
  in.initOffsets = make<InitOffsetsSection>();

  uint8_t *arr = bAlloc().Allocate<uint8_t>(target->wordSize);
  memset(arr, 0, target->wordSize);
  in.imageLoaderCache = makeSyntheticInputSection(
      segment_names::data, section_names::data, S_REGULAR,
      ArrayRef<uint8_t>{arr, target->wordSize},
      /*align=*/target->wordSize);
  in.imageLoaderCache->live = true;
}

OutputSection *macho::firstTLVDataSection = nullptr;

template void macho::writeOutput<LP64>();
