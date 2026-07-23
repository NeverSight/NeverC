// Intra-image relocation executor: see DynCodeRelocationExecutor.h.
//
// It reuses the same checked encoders (patchArm64Branch26 / patchArm64Page21 /
// patchArm64Lo12WithShift / patchRel32 / patchRel64) the per-format extractors
// used, so migrating a format reader onto this executor keeps the emitted bytes
// identical while removing the duplicated patch dispatch.

#include "Extractor/DynCodeRelocationExecutor.h"
#include "Extractor/ExtractorCommon.h"
#include "llvm/Support/Error.h"
#include <cstring>
#include <vector>

using namespace llvm;

namespace neverc {
namespace dyncode {

llvm::Error applyDynCodeRelocation(MutableArrayRef<uint8_t> Bytes,
                                   const DynCodeRelocationWork &Work) {
  int64_t FinalAddr =
      static_cast<int64_t>(Work.TargetOffset) + Work.Addend;
  if (FinalAddr < 0 || static_cast<uint64_t>(FinalAddr) >= Bytes.size())
    return createStringError(
        inconvertibleErrorCode(),
        "dyncode relocation: target address %lld is outside the image",
        (long long)FinalAddr);
  int64_t PCDisp = FinalAddr - static_cast<int64_t>(Work.SiteOffset);

  bool Ok = false;
  switch (Work.Kind) {
  case DynCodeRelocApplyKind::AArch64Branch26:
    Ok = patchArm64Branch26(Bytes, Work.SiteOffset, PCDisp);
    break;
  case DynCodeRelocApplyKind::AArch64Page21:
    Ok = patchArm64Page21(Bytes, Work.SiteOffset,
                          static_cast<int64_t>(FinalAddr), Work.SiteOffset);
    break;
  case DynCodeRelocApplyKind::AArch64AddLo12:
    Ok = patchArm64Lo12WithShift(Bytes, Work.SiteOffset,
                                 static_cast<uint64_t>(FinalAddr), 0);
    break;
  case DynCodeRelocApplyKind::AArch64LdstLo12:
    Ok = patchArm64Lo12WithShift(Bytes, Work.SiteOffset,
                                 static_cast<uint64_t>(FinalAddr),
                                 Work.LdstShift);
    break;
  case DynCodeRelocApplyKind::AArch64Lo12Auto: {
    // Mach-O PAGEOFF12 and COFF PAGEOFFSET_12L do not carry the access size in
    // the relocation type, so decode the instruction: an ADD (imm12) takes
    // shift 0, everything else is a scaled load/store whose shift comes from
    // the encoding.  This mirrors the old per-format extractors exactly.
    if (Work.SiteOffset + 4 > Bytes.size()) {
      Ok = false;
      break;
    }
    uint32_t Inst;
    std::memcpy(&Inst, &Bytes[Work.SiteOffset], 4);
    bool IsAdd = (Inst & 0xFF800000) == 0x91000000;
    if (IsAdd)
      Ok = patchArm64Lo12WithShift(Bytes, Work.SiteOffset,
                                   static_cast<uint64_t>(FinalAddr), 0);
    else
      Ok = patchArm64Lo12AutoShift(Bytes, Work.SiteOffset,
                                   static_cast<uint64_t>(FinalAddr),
                                   /*IsLdSt=*/true);
    break;
  }
  case DynCodeRelocApplyKind::AArch64Prel32:
  case DynCodeRelocApplyKind::X86Rel32:
    Ok = patchRel32(Bytes, Work.SiteOffset, PCDisp);
    break;
  case DynCodeRelocApplyKind::AArch64Prel64:
    Ok = patchRel64(Bytes, Work.SiteOffset, PCDisp);
    break;
  case DynCodeRelocApplyKind::None:
    return createStringError(inconvertibleErrorCode(),
                             "dyncode relocation: no apply kind at site 0x%llx",
                             (unsigned long long)Work.SiteOffset);
  }

  if (!Ok)
    return createStringError(
        inconvertibleErrorCode(),
        "dyncode relocation: fixup at site 0x%llx runs past the image end",
        (unsigned long long)Work.SiteOffset);
  return Error::success();
}

llvm::Error executeDynCodeRelocations(DynCodeImage &Image,
                                      ArrayRef<DynCodeRelocationWork> Work) {
  if (Work.empty())
    return Error::success();

  // Patch a private copy, then write it back through the checked builder so the
  // image's state machine and budget still gate the mutation.
  std::vector<uint8_t> Buf(Image.bytes().begin(), Image.bytes().end());
  for (const DynCodeRelocationWork &W : Work)
    if (llvm::Error E = applyDynCodeRelocation(Buf, W))
      return E;
  return Image.write(0, Buf);
}

} // namespace dyncode
} // namespace neverc
