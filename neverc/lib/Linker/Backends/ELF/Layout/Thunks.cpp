#include "Linker/ELF/Thunks.h"
#include "Linker/Core/Runtime/Session.h"
#include "Linker/ELF/Config.h"
#include "Linker/ELF/InputFiles.h"
#include "Linker/ELF/InputSection.h"
#include "Linker/ELF/OutputSections.h"
#include "Linker/ELF/Symbols.h"
#include "Linker/ELF/SyntheticSections.h"
#include "Linker/ELF/Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>
#include <cstring>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;
using namespace linker;
using namespace linker::elf;

// ===----------------------------------------------------------------------===
// AArch64 thunk classes
// ===----------------------------------------------------------------------===

namespace {

// Base class for AArch64 thunks.
//
// An AArch64 thunk may be either short or long. A short thunk is simply a
// branch (B) instruction, and it may be used to call AArch64 functions when the
// distance from the thunk to the target is less than 128MB. Long thunks can
// branch to any virtual address and they are implemented in the derived
// classes. This class tries to create a short thunk if the target is in range,
// otherwise it creates a long thunk.
class AArch64Thunk : public Thunk {
public:
  AArch64Thunk(Symbol &dest, int64_t addend) : Thunk(dest, addend) {}
  bool getMayUseShortThunk();
  void writeTo(uint8_t *buf) override;

private:
  bool mayUseShortThunk = true;
  virtual void writeLong(uint8_t *buf) = 0;
};

// AArch64 long range Thunks.
class AArch64ABSLongThunk final : public AArch64Thunk {
public:
  AArch64ABSLongThunk(Symbol &dest, int64_t addend)
      : AArch64Thunk(dest, addend) {}
  uint32_t size() override { return getMayUseShortThunk() ? 4 : 16; }
  void addSymbols(ThunkSection &isec) override;

private:
  void writeLong(uint8_t *buf) override;
};

class AArch64ADRPThunk final : public AArch64Thunk {
public:
  AArch64ADRPThunk(Symbol &dest, int64_t addend) : AArch64Thunk(dest, addend) {}
  uint32_t size() override { return getMayUseShortThunk() ? 4 : 12; }
  void addSymbols(ThunkSection &isec) override;

private:
  void writeLong(uint8_t *buf) override;
};

} // end anonymous namespace

// ===----------------------------------------------------------------------===
// Thunk base implementation
// ===----------------------------------------------------------------------===

Defined *Thunk::addSymbol(StringRef name, uint8_t type, uint64_t value,
                          InputSectionBase &section) {
  Defined *d = addSyntheticLocal(name, type, value, /*size=*/0, section);
  syms.push_back(d);
  return d;
}

void Thunk::setOffset(uint64_t newOffset) {
  for (Defined *d : syms)
    d->value = d->value - offset + newOffset;
  offset = newOffset;
}

// ===----------------------------------------------------------------------===
// AArch64 thunk implementations
// ===----------------------------------------------------------------------===

namespace {
uint64_t getAArch64ThunkDestVA(const Symbol &s, int64_t a) {
  uint64_t v = s.isInPlt() ? s.getPltVA() : s.getVA(a);
  return v;
}
} // namespace

bool AArch64Thunk::getMayUseShortThunk() {
  if (!mayUseShortThunk)
    return false;
  uint64_t s = getAArch64ThunkDestVA(destination, addend);
  uint64_t p = getThunkTargetSym()->getVA();
  mayUseShortThunk = llvm::isInt<28>(s - p);
  return mayUseShortThunk;
}

void AArch64Thunk::writeTo(uint8_t *buf) {
  if (!getMayUseShortThunk()) {
    writeLong(buf);
    return;
  }
  uint64_t s = getAArch64ThunkDestVA(destination, addend);
  uint64_t p = getThunkTargetSym()->getVA();
  write32(buf, 0x14000000); // b S
  target->relocateNoSym(buf, R_AARCH64_CALL26, s - p);
}

// AArch64 long range Thunks.
void AArch64ABSLongThunk::writeLong(uint8_t *buf) {
  const uint8_t data[] = {
      0x50, 0x00, 0x00, 0x58, //     ldr x16, L0
      0x00, 0x02, 0x1f, 0xd6, //     br  x16
      0x00, 0x00, 0x00, 0x00, // L0: .xword S
      0x00, 0x00, 0x00, 0x00,
  };
  uint64_t s = getAArch64ThunkDestVA(destination, addend);
  memcpy(buf, data, sizeof(data));
  target->relocateNoSym(buf + 8, R_AARCH64_ABS64, s);
}

void AArch64ABSLongThunk::addSymbols(ThunkSection &isec) {
  addSymbol(saver().save("__AArch64AbsLongThunk_" + destination.getName()),
            STT_FUNC, 0, isec);
  addSymbol("$x", STT_NOTYPE, 0, isec);
  if (!getMayUseShortThunk())
    addSymbol("$d", STT_NOTYPE, 8, isec);
}

// This Thunk has a maximum range of 4Gb, this is sufficient for all programs
// using the small code model, including pc-relative ones. At time of writing
// NeverC and gcc do not support the large code model for position independent
// code so it is safe to use this for position independent thunks without
// worrying about the destination being more than 4Gb away.
void AArch64ADRPThunk::writeLong(uint8_t *buf) {
  const uint8_t data[] = {
      0x10, 0x00, 0x00, 0x90, // adrp x16, Dest R_AARCH64_ADR_PREL_PG_HI21(Dest)
      0x10, 0x02, 0x00, 0x91, // add  x16, x16, R_AARCH64_ADD_ABS_LO12_NC(Dest)
      0x00, 0x02, 0x1f, 0xd6, // br   x16
  };
  uint64_t s = getAArch64ThunkDestVA(destination, addend);
  uint64_t p = getThunkTargetSym()->getVA();
  memcpy(buf, data, sizeof(data));
  target->relocateNoSym(buf, R_AARCH64_ADR_PREL_PG_HI21,
                        getAArch64Page(s) - getAArch64Page(p));
  target->relocateNoSym(buf + 4, R_AARCH64_ADD_ABS_LO12_NC, s);
}

void AArch64ADRPThunk::addSymbols(ThunkSection &isec) {
  addSymbol(saver().save("__AArch64ADRPThunk_" + destination.getName()),
            STT_FUNC, 0, isec);
  addSymbol("$x", STT_NOTYPE, 0, isec);
}

// ===----------------------------------------------------------------------===
// Thunk lifetime & dispatch
// ===----------------------------------------------------------------------===

Thunk::Thunk(Symbol &d, int64_t a) : destination(d), addend(a), offset(0) {
  destination.thunkAccessed = true;
}

Thunk::~Thunk() = default;

namespace {
Thunk *addThunkAArch64(RelType type, Symbol &s, int64_t a) {
  if (type != R_AARCH64_CALL26 && type != R_AARCH64_JUMP26 &&
      type != R_AARCH64_PLT32)
    fatal("unrecognized relocation type");
  if (config->picThunk)
    return make<AArch64ADRPThunk>(s, a);
  return make<AArch64ABSLongThunk>(s, a);
}
} // namespace

Thunk *elf::addThunk(const InputSection &isec, Relocation &rel) {
  Symbol &s = *rel.sym;
  int64_t a = rel.addend;

  switch (config->emachine) {
  case EM_AARCH64:
    return addThunkAArch64(rel.type, s, a);
  default:
    llvm_unreachable("thunks not supported for this architecture");
  }
}
