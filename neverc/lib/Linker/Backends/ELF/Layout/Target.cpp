#include "Linker/ELF/Target.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "Linker/ELF/InputFiles.h"
#include "Linker/ELF/OutputSections.h"
#include "Linker/ELF/SymbolTable.h"
#include "Linker/ELF/Symbols.h"
#include "Linker/ELF/SyntheticSections.h"
#include "llvm/Object/ELF.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;
using namespace linker;
using namespace linker::elf;

const TargetInfo *elf::target;

std::string linker::toString(RelType type) {
  StringRef s = getELFRelocationTypeName(elf::config->emachine, type);
  if (s == "Unknown")
    return ("Unknown (" + Twine(type) + ")").str();
  return std::string(s);
}

TargetInfo *elf::getTarget() {
  switch (config->emachine) {
  case EM_AARCH64:
    return getAArch64TargetInfo();
  case EM_X86_64:
    return getX86_64TargetInfo();
  default:
    error("unsupported target architecture");
    return nullptr;
  }
}

ErrorPlace elf::getErrorPlace(const uint8_t *loc) {
  assert(loc != nullptr);
  for (InputSectionBase *d : ctx.inputSections) {
    auto *isec = dyn_cast<InputSection>(d);
    if (!isec || !isec->getParent() || (isec->type & SHT_NOBITS))
      continue;

    const uint8_t *isecLoc =
        Out::bufferStart
            ? (Out::bufferStart + isec->getParent()->offset + isec->outSecOff)
            : isec->contentMaybeDecompress().data();
    if (isecLoc == nullptr) {
      assert(isa<SyntheticSection>(isec) && "No data but not synthetic?");
      continue;
    }
    if (isecLoc <= loc && loc < isecLoc + isec->getSize()) {
      std::string objLoc = isec->getLocation(loc - isecLoc);
      // Return object file location and source file location.
      Undefined dummy(nullptr, "", STB_LOCAL, 0, 0);
      return {isec, objLoc + ": ",
              isec->file ? isec->getSrcMsg(dummy, loc - isecLoc) : ""};
    }
  }
  return {};
}

TargetInfo::~TargetInfo() {}

int64_t TargetInfo::getImplicitAddend(const uint8_t *buf, RelType type) const {
  internalLinkerError(getErrorLocation(buf),
                      "cannot read addend for relocation " + toString(type));
  return 0;
}

bool TargetInfo::usesOnlyLowPageBits(RelType type) const { return false; }

bool TargetInfo::needsThunk(RelExpr expr, RelType type, const InputFile *file,
                            uint64_t branchAddr, const Symbol &s,
                            int64_t a) const {
  return false;
}

bool TargetInfo::adjustPrologueForCrossSplitStack(uint8_t *loc, uint8_t *end,
                                                  uint8_t stOther) const {
  llvm_unreachable("Target doesn't support split stacks.");
}

bool TargetInfo::inBranchRange(RelType type, uint64_t src, uint64_t dst) const {
  return true;
}

RelExpr TargetInfo::adjustTlsExpr(RelType type, RelExpr expr) const {
  return expr;
}

RelExpr TargetInfo::adjustGotPcExpr(RelType type, int64_t addend,
                                    const uint8_t *data) const {
  return R_GOT_PC;
}

void TargetInfo::relocateAlloc(InputSectionBase &sec, uint8_t *buf) const {
  const unsigned bits = 64;
  uint64_t secAddr = sec.getOutputSection()->addr;
  if (auto *s = dyn_cast<InputSection>(&sec))
    secAddr += s->outSecOff;
  else if (auto *ehIn = dyn_cast<EhInputSection>(&sec))
    secAddr += ehIn->getParent()->outSecOff;
  for (const Relocation &rel : sec.relocs()) {
    uint8_t *loc = buf + rel.offset;
    const uint64_t val = SignExtend64(
        sec.getRelocTargetVA(sec.file, rel.type, rel.addend,
                             secAddr + rel.offset, *rel.sym, rel.expr),
        bits);
    if (rel.expr != R_RELAX_HINT)
      relocate(loc, rel, val);
  }
}

uint64_t TargetInfo::getImageBase() const {
  // Use --image-base if set. Fall back to the target default if not.
  if (config->imageBase)
    return *config->imageBase;
  return config->isPic ? 0 : defaultImageBase;
}
