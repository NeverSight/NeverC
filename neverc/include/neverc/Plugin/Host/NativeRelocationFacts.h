#ifndef NEVERC_PLUGIN_HOST_NATIVERELOCATIONFACTS_H
#define NEVERC_PLUGIN_HOST_NATIVERELOCATIONFACTS_H

#include "neverc/Plugin/PluginObject.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>
#include <optional>

namespace neverc::plugin {

// What a relocation's type number says about the field it patches: how wide
// that field is, how it is addressed, and which linker-level form it belongs
// to.
//
// All of it comes from the type number. It used to be read out of the type
// *name* -- scanning it for digits to get a width, for "pc" to decide
// PC-relativeness -- and that misreads a large share of real relocations. The
// architecture token in "R_X86_64" and "R_AARCH64" scans as a width, so an
// AArch64 CALL26 came out 64 bits wide; "R_PPC64_..." contains "pc" and would
// be called PC-relative wholesale; R_X86_64_REX_GOTPCRELX has no digits at
// all. A type number, unlike a name, means exactly one thing.
//
// This lives here rather than beside the reader that first needed it because
// a type number is not self-describing: the same 2 is AMD64's ADDR32 and
// ARM64's ADDR32NB, and reading one through the wrong table silently answers
// about a different relocation. Everything that has to interpret a native type
// -- the reader that records it, the writer that restates it, the linker that
// patches the bytes it covers -- has to agree on what it means, so they read
// it from one table instead of each keeping a copy that drifts.
//
// A type not listed is reported as unknown and the caller refuses the input.
// That is a recoverable outcome, whereas a guessed width silently truncates a
// field or overruns a section end.
struct NativeRelocationFacts {
  uint32_t Width = 0;
  bool IsPCRelative = false;
  bool IsSigned = false;
  // Set when the patched field lives inside an instruction instead of being a
  // data word of its own. Such a field cannot be read or written as an integer
  // -- its bits are interleaved with the opcode -- so anything that would
  // replace the bytes it covers with a computed value destroys the
  // instruction, and anything that would lift an addend out of them reads
  // opcode bits as a number.
  bool IsInstructionField = false;
  NevercObjectRelocationKind Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  // Set for R_*_NONE, which holds a slot in the relocation table without
  // asking the linker for anything. Such a relocation has no width to give.
  bool IsNoOp = false;
};

namespace nativereloc {

constexpr NativeRelocationFacts absoluteFacts(uint32_t Width,
                                              bool Signed = false) {
  return {Width, false, Signed, false, NEVERC_OBJECT_RELOCATION_ABSOLUTE};
}

constexpr NativeRelocationFacts pcRelativeFacts(uint32_t Width) {
  return {Width, true, true, false, NEVERC_OBJECT_RELOCATION_PC_RELATIVE};
}

constexpr NativeRelocationFacts kindFacts(NevercObjectRelocationKind Kind,
                                          uint32_t Width, bool PCRelative) {
  return {Width, PCRelative, PCRelative, false, Kind};
}

// A field inside an instruction, whose width is the instruction's own size.
// AArch64 instructions are a fixed four bytes; the x86 forms this describes
// name one specific encoding, so they state their size.
constexpr NativeRelocationFacts
instructionFacts(NevercObjectRelocationKind Kind, bool PCRelative,
                 uint32_t Width = 32) {
  return {Width, PCRelative, PCRelative, true, Kind};
}

// R_*_NONE holds a slot in the relocation table without asking for anything:
// the linker steps over it. They reach an object through `ld -r` and through
// passes that neutralise a relocation in place rather than renumbering the
// table around it. Refusing the whole object over one is the wrong answer, and
// so is inventing a width for a relocation that patches nothing.
constexpr NativeRelocationFacts noOpFacts() {
  NativeRelocationFacts Facts;
  Facts.IsNoOp = true;
  return Facts;
}

inline std::optional<NativeRelocationFacts> elfX86_64Facts(uint64_t Type) {
  switch (Type) {
  case llvm::ELF::R_X86_64_NONE:
    return noOpFacts();
  // Marks the `call *(%rax)` that completes a TLS descriptor sequence so the
  // linker knows which two bytes it may relax. Nothing in them is a field.
  case llvm::ELF::R_X86_64_TLSDESC_CALL:
    return instructionFacts(NEVERC_OBJECT_RELOCATION_TLS, /*PCRelative=*/false,
                            /*Width=*/16);
  case llvm::ELF::R_X86_64_64:
    return absoluteFacts(64);
  case llvm::ELF::R_X86_64_32:
    return absoluteFacts(32);
  case llvm::ELF::R_X86_64_32S:
    return absoluteFacts(32, /*Signed=*/true);
  case llvm::ELF::R_X86_64_16:
    return absoluteFacts(16);
  case llvm::ELF::R_X86_64_8:
    return absoluteFacts(8);
  case llvm::ELF::R_X86_64_SIZE32:
    return absoluteFacts(32);
  case llvm::ELF::R_X86_64_SIZE64:
    return absoluteFacts(64);
  case llvm::ELF::R_X86_64_GLOB_DAT:
  case llvm::ELF::R_X86_64_JUMP_SLOT:
  case llvm::ELF::R_X86_64_RELATIVE:
  case llvm::ELF::R_X86_64_IRELATIVE:
    return absoluteFacts(64);
  case llvm::ELF::R_X86_64_PC64:
    return pcRelativeFacts(64);
  case llvm::ELF::R_X86_64_PC32:
    return pcRelativeFacts(32);
  case llvm::ELF::R_X86_64_PC16:
    return pcRelativeFacts(16);
  case llvm::ELF::R_X86_64_PC8:
    return pcRelativeFacts(8);
  case llvm::ELF::R_X86_64_GOT32:
    return kindFacts(NEVERC_OBJECT_RELOCATION_GOT_RELATIVE, 32, false);
  case llvm::ELF::R_X86_64_GOT64:
  case llvm::ELF::R_X86_64_GOTOFF64:
  case llvm::ELF::R_X86_64_GOTPLT64:
    return kindFacts(NEVERC_OBJECT_RELOCATION_GOT_RELATIVE, 64, false);
  case llvm::ELF::R_X86_64_GOTPC32:
  case llvm::ELF::R_X86_64_GOTPCREL:
  case llvm::ELF::R_X86_64_GOTPCRELX:
  case llvm::ELF::R_X86_64_REX_GOTPCRELX:
    return kindFacts(NEVERC_OBJECT_RELOCATION_GOT_RELATIVE, 32, true);
  case llvm::ELF::R_X86_64_GOTPC64:
  case llvm::ELF::R_X86_64_GOTPCREL64:
    return kindFacts(NEVERC_OBJECT_RELOCATION_GOT_RELATIVE, 64, true);
  case llvm::ELF::R_X86_64_PLT32:
    return kindFacts(NEVERC_OBJECT_RELOCATION_PLT_RELATIVE, 32, true);
  case llvm::ELF::R_X86_64_PLTOFF64:
    return kindFacts(NEVERC_OBJECT_RELOCATION_PLT_RELATIVE, 64, false);
  case llvm::ELF::R_X86_64_DTPMOD64:
  case llvm::ELF::R_X86_64_DTPOFF64:
  case llvm::ELF::R_X86_64_TPOFF64:
  case llvm::ELF::R_X86_64_TLSDESC:
    return kindFacts(NEVERC_OBJECT_RELOCATION_TLS, 64, false);
  case llvm::ELF::R_X86_64_DTPOFF32:
  case llvm::ELF::R_X86_64_TPOFF32:
    return kindFacts(NEVERC_OBJECT_RELOCATION_TLS, 32, false);
  case llvm::ELF::R_X86_64_TLSGD:
  case llvm::ELF::R_X86_64_TLSLD:
  case llvm::ELF::R_X86_64_GOTTPOFF:
  case llvm::ELF::R_X86_64_GOTPC32_TLSDESC:
    return kindFacts(NEVERC_OBJECT_RELOCATION_TLS, 32, true);
  default:
    return std::nullopt;
  }
}

// The AArch64 relocations that address their target from the program counter.
// Listed rather than derived: "PREL", "PAGE" and the branch forms are
// PC-relative while the "ABS" and "LO12" forms are not, and the distinction
// does not follow from any part of the encoding.
inline bool aarch64IsPCRelative(uint64_t Type) {
  switch (Type) {
  case llvm::ELF::R_AARCH64_LD_PREL_LO19:
  case llvm::ELF::R_AARCH64_ADR_PREL_LO21:
  case llvm::ELF::R_AARCH64_ADR_PREL_PG_HI21:
  case llvm::ELF::R_AARCH64_ADR_PREL_PG_HI21_NC:
  case llvm::ELF::R_AARCH64_TSTBR14:
  case llvm::ELF::R_AARCH64_CONDBR19:
  case llvm::ELF::R_AARCH64_JUMP26:
  case llvm::ELF::R_AARCH64_CALL26:
  case llvm::ELF::R_AARCH64_MOVW_PREL_G0:
  case llvm::ELF::R_AARCH64_MOVW_PREL_G0_NC:
  case llvm::ELF::R_AARCH64_MOVW_PREL_G1:
  case llvm::ELF::R_AARCH64_MOVW_PREL_G1_NC:
  case llvm::ELF::R_AARCH64_MOVW_PREL_G2:
  case llvm::ELF::R_AARCH64_MOVW_PREL_G2_NC:
  case llvm::ELF::R_AARCH64_MOVW_PREL_G3:
  case llvm::ELF::R_AARCH64_GOT_LD_PREL19:
  case llvm::ELF::R_AARCH64_ADR_GOT_PAGE:
  case llvm::ELF::R_AARCH64_PLT32:
  case llvm::ELF::R_AARCH64_TLSGD_ADR_PREL21:
  case llvm::ELF::R_AARCH64_TLSGD_ADR_PAGE21:
  case llvm::ELF::R_AARCH64_TLSLD_ADR_PREL21:
  case llvm::ELF::R_AARCH64_TLSLD_ADR_PAGE21:
  case llvm::ELF::R_AARCH64_TLSLD_LD_PREL19:
  case llvm::ELF::R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
  case llvm::ELF::R_AARCH64_TLSIE_LD_GOTTPREL_PREL19:
  case llvm::ELF::R_AARCH64_TLSDESC_LD_PREL19:
  case llvm::ELF::R_AARCH64_TLSDESC_ADR_PREL21:
  case llvm::ELF::R_AARCH64_TLSDESC_ADR_PAGE21:
    return true;
  default:
    return false;
  }
}

inline std::optional<NativeRelocationFacts> elfAArch64Facts(uint64_t Type) {
  switch (Type) {
  case llvm::ELF::R_AARCH64_NONE:
    return noOpFacts();
  // Plain data words -- the only AArch64 relocations whose field is not an
  // instruction.
  case llvm::ELF::R_AARCH64_ABS64:
    return absoluteFacts(64);
  case llvm::ELF::R_AARCH64_ABS32:
    return absoluteFacts(32);
  case llvm::ELF::R_AARCH64_ABS16:
    return absoluteFacts(16);
  case llvm::ELF::R_AARCH64_PREL64:
    return pcRelativeFacts(64);
  case llvm::ELF::R_AARCH64_PREL32:
    return pcRelativeFacts(32);
  case llvm::ELF::R_AARCH64_PREL16:
    return pcRelativeFacts(16);
  // A signed pointer: the field is an ordinary 64-bit word, with the signing
  // schema the linker applies held beside it rather than in the field.
  case llvm::ELF::R_AARCH64_AUTH_ABS64:
    return absoluteFacts(64);
  case llvm::ELF::R_AARCH64_GOTREL64:
    return kindFacts(NEVERC_OBJECT_RELOCATION_GOT_RELATIVE, 64, false);
  case llvm::ELF::R_AARCH64_GOTREL32:
    return kindFacts(NEVERC_OBJECT_RELOCATION_GOT_RELATIVE, 32, false);
  case llvm::ELF::R_AARCH64_TLS_DTPMOD64:
  case llvm::ELF::R_AARCH64_TLS_DTPREL64:
  case llvm::ELF::R_AARCH64_TLS_TPREL64:
  case llvm::ELF::R_AARCH64_TLSDESC:
    return kindFacts(NEVERC_OBJECT_RELOCATION_TLS, 64, false);
  case llvm::ELF::R_AARCH64_GLOB_DAT:
  case llvm::ELF::R_AARCH64_JUMP_SLOT:
  case llvm::ELF::R_AARCH64_RELATIVE:
  case llvm::ELF::R_AARCH64_IRELATIVE:
    return absoluteFacts(64);
  default:
    break;
  }

  // Everything else AArch64 defines patches a field inside a fixed 32-bit
  // instruction. The operand size in the mnemonic -- LDST64, MOVW_G3, CALL26 --
  // describes what the field feeds, not how wide the field is.
  const bool StaticForm =
      Type >= llvm::ELF::R_AARCH64_MOVW_UABS_G0 && Type <= llvm::ELF::R_AARCH64_PLT32;
  const bool ThreadLocalForm =
      Type >= llvm::ELF::R_AARCH64_TLSGD_ADR_PREL21 &&
      Type <= llvm::ELF::R_AARCH64_TLSLD_LDST128_DTPREL_LO12_NC;
  if (!StaticForm && !ThreadLocalForm)
    return std::nullopt;

  const bool PCRelative = aarch64IsPCRelative(Type);
  const bool GOTForm = Type >= llvm::ELF::R_AARCH64_MOVW_GOTOFF_G0 &&
                       Type <= llvm::ELF::R_AARCH64_LD64_GOTPAGE_LO15;
  NevercObjectRelocationKind Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  if (ThreadLocalForm)
    Kind = NEVERC_OBJECT_RELOCATION_TLS;
  else if (Type == llvm::ELF::R_AARCH64_PLT32)
    Kind = NEVERC_OBJECT_RELOCATION_PLT_RELATIVE;
  else if (GOTForm)
    Kind = NEVERC_OBJECT_RELOCATION_GOT_RELATIVE;
  else if (PCRelative)
    Kind = NEVERC_OBJECT_RELOCATION_PC_RELATIVE;
  return instructionFacts(Kind, PCRelative);
}

inline std::optional<NativeRelocationFacts> coffX86_64Facts(uint64_t Type) {
  switch (Type) {
  case llvm::COFF::IMAGE_REL_AMD64_ADDR64:
    return absoluteFacts(64);
  case llvm::COFF::IMAGE_REL_AMD64_ADDR32:
    return absoluteFacts(32);
  case llvm::COFF::IMAGE_REL_AMD64_ADDR32NB:
    return kindFacts(NEVERC_OBJECT_RELOCATION_IMAGE_RELATIVE, 32, false);
  case llvm::COFF::IMAGE_REL_AMD64_REL32:
  case llvm::COFF::IMAGE_REL_AMD64_REL32_1:
  case llvm::COFF::IMAGE_REL_AMD64_REL32_2:
  case llvm::COFF::IMAGE_REL_AMD64_REL32_3:
  case llvm::COFF::IMAGE_REL_AMD64_REL32_4:
  case llvm::COFF::IMAGE_REL_AMD64_REL32_5:
    return pcRelativeFacts(32);
  case llvm::COFF::IMAGE_REL_AMD64_SECTION:
    return kindFacts(NEVERC_OBJECT_RELOCATION_SECTION_RELATIVE, 16, false);
  case llvm::COFF::IMAGE_REL_AMD64_SECREL:
    return kindFacts(NEVERC_OBJECT_RELOCATION_SECTION_RELATIVE, 32, false);
  case llvm::COFF::IMAGE_REL_AMD64_SECREL7:
    return kindFacts(NEVERC_OBJECT_RELOCATION_SECTION_RELATIVE, 8, false);
  case llvm::COFF::IMAGE_REL_AMD64_TOKEN:
  case llvm::COFF::IMAGE_REL_AMD64_SREL32:
  case llvm::COFF::IMAGE_REL_AMD64_SSPAN32:
    return kindFacts(NEVERC_OBJECT_RELOCATION_TARGET_EXTENSION, 32, false);
  default:
    return std::nullopt;
  }
}

inline std::optional<NativeRelocationFacts> coffAArch64Facts(uint64_t Type) {
  switch (Type) {
  case llvm::COFF::IMAGE_REL_ARM64_ADDR64:
    return absoluteFacts(64);
  case llvm::COFF::IMAGE_REL_ARM64_ADDR32:
    return absoluteFacts(32);
  case llvm::COFF::IMAGE_REL_ARM64_ADDR32NB:
    return kindFacts(NEVERC_OBJECT_RELOCATION_IMAGE_RELATIVE, 32, false);
  // Instruction forms: a 32-bit instruction holds the patched field.
  case llvm::COFF::IMAGE_REL_ARM64_BRANCH26:
  case llvm::COFF::IMAGE_REL_ARM64_PAGEBASE_REL21:
  case llvm::COFF::IMAGE_REL_ARM64_REL21:
  case llvm::COFF::IMAGE_REL_ARM64_BRANCH19:
  case llvm::COFF::IMAGE_REL_ARM64_BRANCH14:
    return instructionFacts(NEVERC_OBJECT_RELOCATION_PC_RELATIVE, true);
  // REL32 is a data word, unlike the branch forms above.
  case llvm::COFF::IMAGE_REL_ARM64_REL32:
    return pcRelativeFacts(32);
  // The page-offset forms address within a page, not from the program counter.
  case llvm::COFF::IMAGE_REL_ARM64_PAGEOFFSET_12A:
  case llvm::COFF::IMAGE_REL_ARM64_PAGEOFFSET_12L:
    return instructionFacts(NEVERC_OBJECT_RELOCATION_ABSOLUTE, false);
  case llvm::COFF::IMAGE_REL_ARM64_SECREL:
    return kindFacts(NEVERC_OBJECT_RELOCATION_SECTION_RELATIVE, 32, false);
  case llvm::COFF::IMAGE_REL_ARM64_SECREL_LOW12A:
  case llvm::COFF::IMAGE_REL_ARM64_SECREL_HIGH12A:
  case llvm::COFF::IMAGE_REL_ARM64_SECREL_LOW12L:
    return instructionFacts(NEVERC_OBJECT_RELOCATION_SECTION_RELATIVE, false);
  case llvm::COFF::IMAGE_REL_ARM64_SECTION:
    return kindFacts(NEVERC_OBJECT_RELOCATION_SECTION_RELATIVE, 16, false);
  case llvm::COFF::IMAGE_REL_ARM64_TOKEN:
    return kindFacts(NEVERC_OBJECT_RELOCATION_TARGET_EXTENSION, 32, false);
  default:
    return std::nullopt;
  }
}

inline NevercObjectRelocationKind machOX86_64Kind(uint64_t Type) {
  switch (Type) {
  case llvm::MachO::X86_64_RELOC_BRANCH:
  case llvm::MachO::X86_64_RELOC_SIGNED:
  case llvm::MachO::X86_64_RELOC_SIGNED_1:
  case llvm::MachO::X86_64_RELOC_SIGNED_2:
  case llvm::MachO::X86_64_RELOC_SIGNED_4:
    return NEVERC_OBJECT_RELOCATION_PC_RELATIVE;
  case llvm::MachO::X86_64_RELOC_GOT:
  case llvm::MachO::X86_64_RELOC_GOT_LOAD:
    return NEVERC_OBJECT_RELOCATION_GOT_RELATIVE;
  case llvm::MachO::X86_64_RELOC_TLV:
    return NEVERC_OBJECT_RELOCATION_TLS;
  case llvm::MachO::X86_64_RELOC_UNSIGNED:
    return NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  default:
    return NEVERC_OBJECT_RELOCATION_TARGET_EXTENSION;
  }
}

inline NevercObjectRelocationKind machOAArch64Kind(uint64_t Type) {
  switch (Type) {
  case llvm::MachO::ARM64_RELOC_BRANCH26:
  case llvm::MachO::ARM64_RELOC_PAGE21:
    return NEVERC_OBJECT_RELOCATION_PC_RELATIVE;
  case llvm::MachO::ARM64_RELOC_GOT_LOAD_PAGE21:
  case llvm::MachO::ARM64_RELOC_GOT_LOAD_PAGEOFF12:
  case llvm::MachO::ARM64_RELOC_POINTER_TO_GOT:
    return NEVERC_OBJECT_RELOCATION_GOT_RELATIVE;
  case llvm::MachO::ARM64_RELOC_TLVP_LOAD_PAGE21:
  case llvm::MachO::ARM64_RELOC_TLVP_LOAD_PAGEOFF12:
    return NEVERC_OBJECT_RELOCATION_TLS;
  case llvm::MachO::ARM64_RELOC_UNSIGNED:
  case llvm::MachO::ARM64_RELOC_PAGEOFF12:
    return NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  default:
    return NEVERC_OBJECT_RELOCATION_TARGET_EXTENSION;
  }
}

// On AArch64 every Mach-O relocation except the plain pointer forms patches a
// field inside an instruction.
inline bool machOAArch64IsInstructionField(uint64_t Type) {
  switch (Type) {
  case llvm::MachO::ARM64_RELOC_UNSIGNED:
  case llvm::MachO::ARM64_RELOC_SUBTRACTOR:
  case llvm::MachO::ARM64_RELOC_POINTER_TO_GOT:
  case llvm::MachO::ARM64_RELOC_ADDEND:
    return false;
  default:
    return true;
  }
}

} // namespace nativereloc

// Whether there are tables here for \p Target at all. A caller that refuses
// what it cannot recognise needs this to tell "this type is not a relocation
// of that shape" from "nothing here knows this target", which are different
// answers even though both arrive as an absent one.
inline bool haveNativeRelocationTable(const llvm::Triple &Target) {
  if (Target.getArch() != llvm::Triple::x86_64 &&
      Target.getArch() != llvm::Triple::aarch64)
    return false;
  return Target.isOSBinFormatELF() || Target.isOSBinFormatCOFF() ||
         Target.isOSBinFormatMachO();
}

// The facts a relocation type carries on \p Target, or nothing when the target
// or the type is one this does not know.
//
// Mach-O is missing here on purpose: it records the field size and the
// addressing mode in the relocation entry rather than implying them from the
// type, so those two have to be read from the entry and only the remainder --
// the kind, and whether the field sits inside an instruction -- is answered by
// machOFacts below.
inline std::optional<NativeRelocationFacts>
nativeRelocationFacts(const llvm::Triple &Target, uint64_t Type) {
  if (Target.isOSBinFormatELF()) {
    if (Target.getArch() == llvm::Triple::x86_64)
      return nativereloc::elfX86_64Facts(Type);
    if (Target.getArch() == llvm::Triple::aarch64)
      return nativereloc::elfAArch64Facts(Type);
    return std::nullopt;
  }
  if (Target.isOSBinFormatCOFF()) {
    if (Target.getArch() == llvm::Triple::x86_64)
      return nativereloc::coffX86_64Facts(Type);
    if (Target.getArch() == llvm::Triple::aarch64)
      return nativereloc::coffAArch64Facts(Type);
    return std::nullopt;
  }
  return std::nullopt;
}

// The part of a Mach-O relocation that follows from its type, given the width
// and addressing mode already read out of the entry itself.
inline std::optional<NativeRelocationFacts>
machOFacts(llvm::Triple::ArchType Arch, uint64_t Type, uint32_t Width,
           bool PCRelative) {
  if (Arch == llvm::Triple::aarch64)
    return NativeRelocationFacts{
        Width, PCRelative, PCRelative,
        nativereloc::machOAArch64IsInstructionField(Type),
        nativereloc::machOAArch64Kind(Type)};
  if (Arch == llvm::Triple::x86_64)
    // x86 relocations always cover a displacement or immediate field that
    // occupies whole bytes of its own, even inside an instruction.
    return NativeRelocationFacts{Width, PCRelative, PCRelative, false,
                                 nativereloc::machOX86_64Kind(Type)};
  return std::nullopt;
}

// Whether the bytes a relocation of this type covers are a field in their own
// right, so that replacing them with a computed value leaves the rest of the
// program alone.
//
// Nothing is the answer for a type this does not recognise, which the caller
// has to tell apart from a "no": a relocation whose form is unknown cannot be
// shown to be safe to overwrite either.
inline std::optional<bool>
nativeRelocationFieldIsWholeBytes(const llvm::Triple &Target, uint64_t Type) {
  if (Target.isOSBinFormatMachO()) {
    // Whether the field stands on its own follows from the type alone, so the
    // width and addressing mode -- which a Mach-O entry states and a type
    // number does not -- have no bearing on the answer.
    const std::optional<NativeRelocationFacts> Facts =
        machOFacts(Target.getArch(), Type, /*Width=*/32, /*PCRelative=*/false);
    if (!Facts)
      return std::nullopt;
    return !Facts->IsInstructionField;
  }
  const std::optional<NativeRelocationFacts> Facts =
      nativeRelocationFacts(Target, Type);
  if (!Facts || Facts->IsNoOp)
    return std::nullopt;
  return !Facts->IsInstructionField;
}

} // namespace neverc::plugin

#endif
