// The builtin dyncode relocation providers: native relocation type -> intra-
// image fixup form.  See DynCodeRelocationProvider.h.
//
// The three per-format switches here reproduce exactly what the old ELF, COFF
// and Mach-O extractors open-coded.  The only structural difference is that the
// per-format end-relative field bias (the "-4" x86 correction and the COFF/
// Mach-O SIGNED_N extra) is folded into AddendAdjust so the single shared
// relocation executor emits identical bytes regardless of object format.  ELF
// already carries that bias in the relocation addend, so ELF forms use
// AddendAdjust 0.

#include "Extractor/DynCodeRelocationProvider.h"
#include "neverc/Plugin/Host/BuiltinObjectExtension.h"
#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include <string>

using namespace llvm;

namespace neverc {
namespace dyncode {

namespace {

DynCodeRelocationMapping intra(DynCodeRelocApplyKind Kind, unsigned Shift = 0,
                              int64_t AddendAdjust = 0) {
  DynCodeRelocationMapping M;
  M.Class = DynCodeRelocationClass::IntraImage;
  M.Kind = Kind;
  M.LdstShift = Shift;
  M.AddendAdjust = AddendAdjust;
  return M;
}

DynCodeRelocationMapping classified(DynCodeRelocationClass Class) {
  DynCodeRelocationMapping M;
  M.Class = Class;
  return M;
}

// Mach-O relocation type numbers (llvm::MachO::RelocationInfoType), inlined so
// this file matches the numeric switch the old Mach-O extractor used.
enum : uint64_t {
  MachOArm64Unsigned = 0,
  MachOArm64Subtractor = 1,
  MachOArm64Branch26 = 2,
  MachOArm64Page21 = 3,
  MachOArm64PageOff12 = 4,
  MachOArm64GotLoadPage21 = 5,
  MachOArm64GotLoadPageOff12 = 6,
  MachOArm64PointerToGot = 7,
  MachOArm64TlvpLoadPage21 = 8,
  MachOArm64TlvpLoadPageOff12 = 9,
  MachOArm64Addend = 10,
};

enum : uint64_t {
  MachOX86Unsigned = 0,
  MachOX86Signed = 1,
  MachOX86Branch = 2,
  MachOX86GotLoad = 3,
  MachOX86Got = 4,
  MachOX86Subtractor = 5,
  MachOX86Signed1 = 6,
  MachOX86Signed2 = 7,
  MachOX86Signed4 = 8,
  MachOX86Tlv = 9,
};

DynCodeRelocationMapping mapElfAArch64(uint64_t T) {
  switch (T) {
  case ELF::R_AARCH64_CALL26:
  case ELF::R_AARCH64_JUMP26:
    return intra(DynCodeRelocApplyKind::AArch64Branch26);
  case ELF::R_AARCH64_ADR_PREL_PG_HI21:
  case ELF::R_AARCH64_ADR_PREL_PG_HI21_NC:
    return intra(DynCodeRelocApplyKind::AArch64Page21);
  case ELF::R_AARCH64_ADD_ABS_LO12_NC:
    return intra(DynCodeRelocApplyKind::AArch64AddLo12, 0);
  case ELF::R_AARCH64_LDST8_ABS_LO12_NC:
    return intra(DynCodeRelocApplyKind::AArch64LdstLo12, 0);
  case ELF::R_AARCH64_LDST16_ABS_LO12_NC:
    return intra(DynCodeRelocApplyKind::AArch64LdstLo12, 1);
  case ELF::R_AARCH64_LDST32_ABS_LO12_NC:
    return intra(DynCodeRelocApplyKind::AArch64LdstLo12, 2);
  case ELF::R_AARCH64_LDST64_ABS_LO12_NC:
    return intra(DynCodeRelocApplyKind::AArch64LdstLo12, 3);
  case ELF::R_AARCH64_LDST128_ABS_LO12_NC:
    return intra(DynCodeRelocApplyKind::AArch64LdstLo12, 4);
  case ELF::R_AARCH64_PREL32:
    return intra(DynCodeRelocApplyKind::AArch64Prel32);
  case ELF::R_AARCH64_PREL64:
    return intra(DynCodeRelocApplyKind::AArch64Prel64);
  default:
    return classified(DynCodeRelocationClass::Unsupported);
  }
}

DynCodeRelocationMapping mapElfX86(uint64_t T) {
  switch (T) {
  case ELF::R_X86_64_PC32:
  case ELF::R_X86_64_PLT32:
    return intra(DynCodeRelocApplyKind::X86Rel32);
  case ELF::R_X86_64_GOTPCREL:
  case ELF::R_X86_64_GOTPCRELX:
  case ELF::R_X86_64_REX_GOTPCRELX:
    return classified(DynCodeRelocationClass::ExternalGOT);
  default:
    return classified(DynCodeRelocationClass::Unsupported);
  }
}

DynCodeRelocationMapping mapCoffAArch64(uint64_t T) {
  switch (T) {
  case COFF::IMAGE_REL_ARM64_BRANCH26:
    return intra(DynCodeRelocApplyKind::AArch64Branch26);
  case COFF::IMAGE_REL_ARM64_PAGEBASE_REL21:
    return intra(DynCodeRelocApplyKind::AArch64Page21);
  case COFF::IMAGE_REL_ARM64_PAGEOFFSET_12A:
    return intra(DynCodeRelocApplyKind::AArch64AddLo12, 0);
  case COFF::IMAGE_REL_ARM64_PAGEOFFSET_12L:
    return intra(DynCodeRelocApplyKind::AArch64Lo12Auto);
  case COFF::IMAGE_REL_ARM64_REL32:
    // COFF stores no addend; the -4 end-relative bias lived in the extractor.
    return intra(DynCodeRelocApplyKind::X86Rel32, 0, /*AddendAdjust=*/-4);
  default:
    return classified(DynCodeRelocationClass::Unsupported);
  }
}

DynCodeRelocationMapping mapCoffX86(uint64_t T) {
  switch (T) {
  case COFF::IMAGE_REL_AMD64_REL32:
  case COFF::IMAGE_REL_AMD64_REL32_1:
  case COFF::IMAGE_REL_AMD64_REL32_2:
  case COFF::IMAGE_REL_AMD64_REL32_3:
  case COFF::IMAGE_REL_AMD64_REL32_4:
  case COFF::IMAGE_REL_AMD64_REL32_5: {
    int64_t Extra = static_cast<int64_t>(T) -
                    static_cast<int64_t>(COFF::IMAGE_REL_AMD64_REL32);
    return intra(DynCodeRelocApplyKind::X86Rel32, 0,
                 /*AddendAdjust=*/-(4 + Extra));
  }
  default:
    return classified(DynCodeRelocationClass::Unsupported);
  }
}

DynCodeRelocationMapping mapMachOAArch64(uint64_t T) {
  switch (T) {
  case MachOArm64Branch26:
    return intra(DynCodeRelocApplyKind::AArch64Branch26);
  case MachOArm64Page21:
    return intra(DynCodeRelocApplyKind::AArch64Page21);
  case MachOArm64PageOff12:
    return intra(DynCodeRelocApplyKind::AArch64Lo12Auto);
  case MachOArm64Unsigned:
    return classified(DynCodeRelocationClass::ExternalAbsolute);
  case MachOArm64GotLoadPage21:
  case MachOArm64GotLoadPageOff12:
  case MachOArm64PointerToGot:
    return classified(DynCodeRelocationClass::ExternalGOT);
  default:
    return classified(DynCodeRelocationClass::Unsupported);
  }
}

DynCodeRelocationMapping mapMachOX86(uint64_t T) {
  switch (T) {
  case MachOX86Signed:
  case MachOX86Branch:
    return intra(DynCodeRelocApplyKind::X86Rel32, 0, /*AddendAdjust=*/-4);
  case MachOX86Signed1:
    return intra(DynCodeRelocApplyKind::X86Rel32, 0, /*AddendAdjust=*/-5);
  case MachOX86Signed2:
    return intra(DynCodeRelocApplyKind::X86Rel32, 0, /*AddendAdjust=*/-6);
  case MachOX86Signed4:
    return intra(DynCodeRelocApplyKind::X86Rel32, 0, /*AddendAdjust=*/-8);
  case MachOX86Unsigned:
    return classified(DynCodeRelocationClass::ExternalAbsolute);
  case MachOX86GotLoad:
  case MachOX86Got:
    return classified(DynCodeRelocationClass::ExternalGOT);
  default:
    return classified(DynCodeRelocationClass::Unsupported);
  }
}

} // namespace

std::optional<uint64_t>
decodeNativeRelocationType(const plugin::PluginObjectExtension &Ext) {
  namespace ext = plugin::builtinext;
  ArrayRef<uint8_t> Bytes(Ext.Bytes);
  if (!ext::hasTag(Bytes, ext::RelocationTag) || ext::version(Bytes) < 1)
    return std::nullopt;
  // A version added after this was written still carries the fields it knows,
  // in the same places -- that is what a fixed-width layout buys. Refusing
  // anything but the version current at the time would turn the next field
  // appended to the blob into a decode failure here.
  return ext::field(Bytes, ext::RelocationNativeType);
}

DynCodeRelocationMapping mapDynCodeRelocation(const TargetDesc &Target,
                                              uint64_t NativeType) {
  switch (Target.Format) {
  case ObjectFormat::ELF:
    return Target.Arch == DynCodeArch::AArch64 ? mapElfAArch64(NativeType)
                                               : mapElfX86(NativeType);
  case ObjectFormat::COFF:
    return Target.Arch == DynCodeArch::AArch64 ? mapCoffAArch64(NativeType)
                                               : mapCoffX86(NativeType);
  case ObjectFormat::MachO:
    return Target.Arch == DynCodeArch::AArch64 ? mapMachOAArch64(NativeType)
                                               : mapMachOX86(NativeType);
  default:
    return classified(DynCodeRelocationClass::Unsupported);
  }
}

llvm::Error resolveAndApplyDynCodeRelocations(const DynCodeExtractionPlan &Plan,
                                              const TargetDesc &Target,
                                              DynCodeImage &Image,
                                              DynCodeReport &Report) {
  std::vector<DynCodeRelocationWork> Work;
  Work.reserve(Plan.relocations().size());

  for (const DynCodeRelocationEntry &E : Plan.relocations()) {
    if (!E.Resolved)
      return createStringError(
          errc::invalid_argument,
          "dyncode relocation: site 0x%llx references an unresolved external "
          "target; dyncode must be fully resolved before extraction",
          (unsigned long long)E.SiteOffset);

    // Applying needs the precise native type, and its absence has to be named
    // as such rather than standing in as a value: 0 is the plain pointer form
    // on Mach-O, so a relocation with no recoverable type used to come back
    // diagnosed as an absolute-address one the input never held.
    if (!E.NativeType)
      return createStringError(
          errc::invalid_argument,
          "dyncode relocation: site 0x%llx carries no readable native "
          "relocation type; the object graph did not come from the builtin "
          "reader, or its extension is malformed",
          (unsigned long long)E.SiteOffset);

    DynCodeRelocationMapping M = mapDynCodeRelocation(Target, *E.NativeType);
    switch (M.Class) {
    case DynCodeRelocationClass::IntraImage:
      break;
    case DynCodeRelocationClass::ExternalGOT:
      return createStringError(
          errc::invalid_argument,
          "dyncode relocation: GOT-based relocation (native type %llu) at site "
          "0x%llx -- dyncode has no GOT; keep references inside the extracted "
          "code or route externs through a resolver shim",
          (unsigned long long)*E.NativeType, (unsigned long long)E.SiteOffset);
    case DynCodeRelocationClass::ExternalAbsolute:
      return createStringError(
          errc::invalid_argument,
          "dyncode relocation: absolute-address relocation (native type %llu) "
          "at site 0x%llx -- dyncode has no load address; route globals "
          "through the stack (Data2TextPass) or resolver parameters",
          (unsigned long long)*E.NativeType, (unsigned long long)E.SiteOffset);
    case DynCodeRelocationClass::Unsupported:
      return createStringError(
          errc::invalid_argument,
          "dyncode relocation: unsupported relocation (native type %llu) at "
          "site 0x%llx for this target",
          (unsigned long long)*E.NativeType, (unsigned long long)E.SiteOffset);
    }

    DynCodeRelocationWork W;
    W.SiteOffset = E.SiteOffset;
    W.TargetOffset = E.TargetOffset;
    W.Addend = E.Addend + M.AddendAdjust;
    W.Kind = M.Kind;
    W.LdstShift = M.LdstShift;
    Work.push_back(W);
  }

  if (llvm::Error E = executeDynCodeRelocations(Image, Work))
    return E;

  if (llvm::Error E = Report.addRecord(
          {26, "builtin.relocation_executor", "relocations.applied",
           std::to_string(Work.size())}))
    return E;
  return Error::success();
}

} // namespace dyncode
} // namespace neverc
