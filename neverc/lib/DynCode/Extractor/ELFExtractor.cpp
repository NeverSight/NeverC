#include "ExtractorCommon.h"
#include "neverc/DynCode/Pipeline/Pipeline.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <cstring>

using namespace llvm;
using namespace llvm::object;

namespace neverc {
namespace dyncode {

namespace {

unsigned shiftForLdstReloc(uint32_t Type) {
  switch (Type) {
  case ELF::R_AARCH64_LDST8_ABS_LO12_NC:
    return 0;
  case ELF::R_AARCH64_LDST16_ABS_LO12_NC:
    return 1;
  case ELF::R_AARCH64_LDST32_ABS_LO12_NC:
    return 2;
  case ELF::R_AARCH64_LDST64_ABS_LO12_NC:
    return 3;
  case ELF::R_AARCH64_LDST128_ABS_LO12_NC:
    return 4;
  default:
    return 0;
  }
}

const char *aarch64ElfName(uint32_t T) {
  switch (T) {
#define ELF_RELOC(N, V)                                                        \
  case V:                                                                      \
    return #N;
#include "llvm/BinaryFormat/ELFRelocs/AArch64.def"
#undef ELF_RELOC
  default:
    return "R_AARCH64_UNKNOWN";
  }
}

const char *x86_64ElfName(uint32_t T) {
  switch (T) {
#define ELF_RELOC(N, V)                                                        \
  case V:                                                                      \
    return #N;
#include "llvm/BinaryFormat/ELFRelocs/x86_64.def"
#undef ELF_RELOC
  default:
    return "R_X86_64_UNKNOWN";
  }
}

} // namespace

int extractELF(StringRef InputObj, StringRef OutputBin,
               const DynCodeOptions &Opts) {
  const TargetDesc &Target = Opts.Target;
  StringRef EntrySymbol = Opts.EntrySymbol;
  bool Verbose = Opts.Verbose;
  auto BufOrErr = MemoryBuffer::getFile(InputObj);
  if (!BufOrErr) {
    errs() << "dyncode-extractor: cannot open '" << InputObj
           << "': " << BufOrErr.getError().message() << "\n";
    return 1;
  }

  auto ObjOrErr = ObjectFile::createObjectFile((*BufOrErr)->getMemBufferRef());
  if (!ObjOrErr) {
    errs() << "dyncode-extractor: '" << InputObj
           << "' is not a valid object file: " << toString(ObjOrErr.takeError())
           << "\n";
    return 1;
  }
  auto &Obj = **ObjOrErr;

  if (!Obj.isELF()) {
    errs() << "dyncode-extractor: expected ELF, got "
           << (Obj.isMachO()  ? "Mach-O"
               : Obj.isCOFF() ? "COFF"
                              : "?")
           << "\n";
    return 1;
  }

  Triple::ArchType ExpectedArch =
      Target.Arch == DynCodeArch::AArch64 ? Triple::aarch64 : Triple::x86_64;
  if (Obj.getArch() != ExpectedArch) {
    errs() << "dyncode-extractor: object arch mismatch (got "
           << Triple::getArchTypeName(Obj.getArch()) << ", expected "
           << Triple::getArchTypeName(ExpectedArch) << ")\n";
    return 1;
  }

  struct TextFragment {
    SectionRef Section;
    ArrayRef<uint8_t> Data;
    uint64_t MergedOffset = 0;
  };
  SmallVector<TextFragment, 8> Fragments;
  for (const SectionRef &Sec : Obj.sections()) {
    auto NameOrErr = Sec.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    if (!isTextSection(Target, *NameOrErr))
      continue;
    auto DataOrErr = Sec.getContents();
    if (!DataOrErr) {
      errs() << "dyncode-extractor: cannot read text section '" << *NameOrErr
             << "': " << toString(DataOrErr.takeError()) << "\n";
      return 1;
    }
    if (DataOrErr->empty())
      continue;
    Fragments.push_back({Sec, arrayRefFromStringRef(*DataOrErr), 0});
  }
  if (Fragments.empty()) {
    errs() << "dyncode-extractor: no .text section found in '" << InputObj
           << "'\n";
    return 1;
  }

  DenseMap<unsigned, unsigned> SecIdxToFrag;
  for (unsigned I = 0; I < Fragments.size(); ++I)
    SecIdxToFrag[Fragments[I].Section.getIndex()] = I;

  bool FoundEntry = false;
  std::string ChosenEntry;
  for (const auto &Sym : Obj.symbols()) {
    auto NameOrErr = Sym.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    if (!isDynCodeEntryCandidate(*NameOrErr, EntrySymbol))
      continue;
    auto SecOrErr = Sym.getSection();
    if (!SecOrErr) {
      consumeError(SecOrErr.takeError());
      continue;
    }
    if (*SecOrErr == Obj.section_end())
      continue;
    unsigned SecIdx = (*SecOrErr)->getIndex();
    auto FIt = SecIdxToFrag.find(SecIdx);
    if (FIt == SecIdxToFrag.end())
      continue;
    auto AddrOrErr = Sym.getAddress();
    if (!AddrOrErr) {
      consumeError(AddrOrErr.takeError());
      continue;
    }
    uint64_t SecRel = *AddrOrErr - (*SecOrErr)->getAddress();
    if (SecRel != 0) {
      errs() << "dyncode-extractor: entry symbol '" << *NameOrErr
             << "' is at offset " << SecRel
             << " within its section but must be at offset 0\n";
      return 1;
    }
    if (FIt->second != 0) {
      std::swap(Fragments[0], Fragments[FIt->second]);
      SecIdxToFrag.clear();
      for (unsigned I = 0; I < Fragments.size(); ++I)
        SecIdxToFrag[Fragments[I].Section.getIndex()] = I;
    }
    FoundEntry = true;
    ChosenEntry = NameOrErr->str();
    break;
  }
  if (!FoundEntry) {
    errs() << "dyncode-extractor: no entry symbol (";
    if (!EntrySymbol.empty())
      errs() << "'" << EntrySymbol << "'";
    else
      errs() << defaultEntryNameList();
    errs() << ") found in '" << InputObj << "'\n";
    SmallVector<StringRef, 8> Candidates;
    for (const auto &Sym : Obj.symbols()) {
      auto NOrErr = Sym.getName();
      if (!NOrErr)
        continue;
      StringRef N = *NOrErr;
      if (N.empty() || N.starts_with(".L") ||
          isDynCodeInternalRuntimeName(N))
        continue;
      Candidates.push_back(N);
    }
    if (!Candidates.empty()) {
      errs() << "dyncode-extractor: defined symbols in this object:";
      for (StringRef N : Candidates)
        errs() << " " << N;
      errs() << "\n";
      errs() << "dyncode-extractor: hint: pass -fdyncode-entry=<name> "
                "to pick one\n";
    }
    return 1;
  }

  uint64_t TotalSize = 0;
  for (unsigned I = 0; I < Fragments.size(); ++I) {
    if (I > 0)
      TotalSize = (TotalSize + 15) & ~15ULL;
    Fragments[I].MergedOffset = TotalSize;
    TotalSize += Fragments[I].Data.size();
  }
  uint8_t PadByte = (Target.Arch == DynCodeArch::X86_64) ? 0xCC : 0x00;
  SmallVector<uint8_t, 256> TextBytes(TotalSize, PadByte);
  for (auto &F : Fragments)
    std::copy(F.Data.begin(), F.Data.end(),
              TextBytes.begin() + F.MergedOffset);

  DenseMap<StringRef, uint64_t> DefinedSyms;
  for (const auto &Sym : Obj.symbols()) {
    Expected<uint32_t> FlagsOrErr = Sym.getFlags();
    if (!FlagsOrErr) {
      consumeError(FlagsOrErr.takeError());
      continue;
    }
    if (*FlagsOrErr & BasicSymbolRef::SF_Undefined)
      continue;
    auto NameOrErr = Sym.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    auto SecOrErr = Sym.getSection();
    if (!SecOrErr) {
      consumeError(SecOrErr.takeError());
      continue;
    }
    if (*SecOrErr == Obj.section_end())
      continue;
    unsigned SecIdx = (*SecOrErr)->getIndex();
    auto FIt = SecIdxToFrag.find(SecIdx);
    if (FIt == SecIdxToFrag.end())
      continue;
    auto AddrOrErr = Sym.getAddress();
    if (!AddrOrErr) {
      consumeError(AddrOrErr.takeError());
      continue;
    }
    uint64_t SecRel = *AddrOrErr - (*SecOrErr)->getAddress();
    DefinedSyms[*NameOrErr] =
        Fragments[FIt->second].MergedOffset + SecRel;
  }

  MutableArrayRef<uint8_t> TextView(TextBytes);

  unsigned External = 0;
  unsigned PatchedBranch = 0;
  unsigned PatchedPage21 = 0;
  unsigned PatchedLo12 = 0;
  unsigned PatchedX86Rel32 = 0;

  struct RelocSecInfo {
    SectionRef RelocSec;
    unsigned FragIdx;
  };
  SmallVector<RelocSecInfo, 4> RelocSections;
  for (const SectionRef &Sec : Obj.sections()) {
    auto RelocatedOrErr = Sec.getRelocatedSection();
    if (!RelocatedOrErr) {
      consumeError(RelocatedOrErr.takeError());
      continue;
    }
    if (*RelocatedOrErr == Obj.section_end())
      continue;
    unsigned RelocatedIdx = (*RelocatedOrErr)->getIndex();
    auto FIt = SecIdxToFrag.find(RelocatedIdx);
    if (FIt != SecIdxToFrag.end())
      RelocSections.push_back({Sec, FIt->second});
  }
  if (RelocSections.empty()) {
    for (unsigned I = 0; I < Fragments.size(); ++I)
      RelocSections.push_back({Fragments[I].Section, I});
  }

  for (const auto &RS : RelocSections) {
    uint64_t FragOff = Fragments[RS.FragIdx].MergedOffset;
    for (const RelocationRef &Reloc : RS.RelocSec.relocations()) {
      uint64_t RelocOff = Reloc.getOffset() + FragOff;
      uint64_t RelocType = Reloc.getType();
      auto SymIt = Reloc.getSymbol();
      int64_t Addend = 0;
      if (auto AddOrErr = ELFRelocationRef(Reloc).getAddend())
        Addend = *AddOrErr;

      StringRef Name = "<unknown>";
      bool IsDefined = false;
      uint64_t SymAddr = 0;
      if (SymIt != Obj.symbol_end()) {
        if (auto N = SymIt->getName()) {
          Name = *N;
          auto It = DefinedSyms.find(Name);
          if (It != DefinedSyms.end()) {
            IsDefined = true;
            SymAddr = It->second;
          }
        }
      }

      if (!IsDefined) {
        ++External;
        const char *RelName = Target.Arch == DynCodeArch::AArch64
                                  ? aarch64ElfName(RelocType)
                                  : x86_64ElfName(RelocType);
        errs() << "dyncode-extractor: unresolved relocation referencing '"
               << Name << "' (" << RelName << ")\n";
        printExternHint(errs(), Target, Name);
        continue;
      }

      int64_t FinalAddr = static_cast<int64_t>(SymAddr) + Addend;
      if (FinalAddr < 0 ||
          static_cast<uint64_t>(FinalAddr) >= TotalSize) {
        ++External;
        errs() << "dyncode-extractor: relocation target '" << Name
               << "' at 0x";
        errs().write_hex(FinalAddr);
        errs() << " is outside merged .text\n";
        continue;
      }

      int64_t PCDisp = FinalAddr - static_cast<int64_t>(RelocOff);

      if (Target.Arch == DynCodeArch::AArch64) {
        switch (RelocType) {
        case ELF::R_AARCH64_CALL26:
        case ELF::R_AARCH64_JUMP26:
          if (!patchArm64Branch26(TextView, RelocOff, PCDisp)) {
            ++External;
            errs() << "dyncode-extractor: cannot patch branch26 at 0x";
            errs().write_hex(RelocOff);
            errs() << "\n";
            continue;
          }
          ++PatchedBranch;
          break;
        case ELF::R_AARCH64_ADR_PREL_PG_HI21:
        case ELF::R_AARCH64_ADR_PREL_PG_HI21_NC:
          if (!patchArm64Page21(TextView, RelocOff, FinalAddr, RelocOff)) {
            ++External;
            errs() << "dyncode-extractor: cannot patch adrp at 0x";
            errs().write_hex(RelocOff);
            errs() << "\n";
            continue;
          }
          ++PatchedPage21;
          break;
        case ELF::R_AARCH64_ADD_ABS_LO12_NC:
          if (!patchArm64Lo12WithShift(TextView, RelocOff, FinalAddr, 0)) {
            ++External;
            errs() << "dyncode-extractor: cannot patch add lo12 at 0x";
            errs().write_hex(RelocOff);
            errs() << "\n";
            continue;
          }
          ++PatchedLo12;
          break;
        case ELF::R_AARCH64_LDST8_ABS_LO12_NC:
        case ELF::R_AARCH64_LDST16_ABS_LO12_NC:
        case ELF::R_AARCH64_LDST32_ABS_LO12_NC:
        case ELF::R_AARCH64_LDST64_ABS_LO12_NC:
        case ELF::R_AARCH64_LDST128_ABS_LO12_NC:
          if (!patchArm64Lo12WithShift(TextView, RelocOff, FinalAddr,
                                       shiftForLdstReloc(RelocType))) {
            ++External;
            errs() << "dyncode-extractor: cannot patch ldst lo12 at 0x";
            errs().write_hex(RelocOff);
            errs() << "\n";
            continue;
          }
          ++PatchedLo12;
          break;
        case ELF::R_AARCH64_PREL32:
          if (!patchRel32(TextView, RelocOff, PCDisp)) {
            ++External;
            errs() << "dyncode-extractor: cannot patch prel32 at 0x";
            errs().write_hex(RelocOff);
            errs() << "\n";
            continue;
          }
          ++PatchedLo12;
          break;
        case ELF::R_AARCH64_PREL64:
          if (!patchRel64(TextView, RelocOff, PCDisp)) {
            ++External;
            errs() << "dyncode-extractor: cannot patch prel64 at 0x";
            errs().write_hex(RelocOff);
            errs() << "\n";
            continue;
          }
          ++PatchedLo12;
          break;
        default: {
          ++External;
          errs() << "dyncode-extractor: unsupported intra-.text relocation "
                 << aarch64ElfName(RelocType) << " at 0x";
          errs().write_hex(RelocOff);
          errs() << " referencing '" << Name << "'\n";
          break;
        }
        }
      } else if (Target.Arch == DynCodeArch::X86_64) {
        switch (RelocType) {
        case ELF::R_X86_64_PC32:
        case ELF::R_X86_64_PLT32:
          if (!patchRel32(TextView, RelocOff, PCDisp)) {
            ++External;
            errs() << "dyncode-extractor: cannot patch rel32 at 0x";
            errs().write_hex(RelocOff);
            errs() << "\n";
            continue;
          }
          ++PatchedX86Rel32;
          break;
        case ELF::R_X86_64_GOTPCREL:
        case ELF::R_X86_64_GOTPCRELX:
        case ELF::R_X86_64_REX_GOTPCRELX:
          ++External;
          errs() << "dyncode-extractor: GOT-based relocation "
                 << x86_64ElfName(RelocType) << " at 0x";
          errs().write_hex(RelocOff);
          errs() << " referencing '" << Name
                 << "' — dyncode cannot resolve GOT entries; pass "
                    "-fdyncode-all-blr to force indirect calls or "
                    "rewrite the reference as a function-local helper.\n";
          continue;
        default:
          ++External;
          errs() << "dyncode-extractor: unsupported intra-.text relocation "
                 << x86_64ElfName(RelocType) << " at 0x";
          errs().write_hex(RelocOff);
          errs() << " referencing '" << Name << "'\n";
          break;
        }
      } else {
        ++External;
        errs() << "dyncode-extractor: ELF extraction not implemented for "
               << archName(Target.Arch) << "\n";
      }
    }
  }

  if (External > 0) {
    errs() << "dyncode-extractor: " << External
           << " external relocation(s) found — dyncode must be "
              "fully resolved\n";
    return 1;
  }

  for (const auto &Sec : Obj.sections()) {
    auto NameOrErr = Sec.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    StringRef N = *NameOrErr;
    if (!isForbiddenDataSection(Target, N))
      continue;
    auto DataOrErr = Sec.getContents();
    uint64_t Size = 0;
    if (DataOrErr)
      Size = DataOrErr->size();
    if (Size > 0) {
      errs() << "dyncode-extractor: unexpected data section '" << N << "' ("
             << Size
             << " bytes) — Data2Text pass should have eliminated all "
                "constant data\n";
      return 1;
    }
  }

  if (int Rc = finalizeDynCodeBytes(TextBytes, Opts))
    return Rc;

  std::error_code EC;
  raw_fd_ostream Out(OutputBin, EC, sys::fs::OF_None);
  if (EC) {
    errs() << "dyncode-extractor: cannot write '" << OutputBin
           << "': " << EC.message() << "\n";
    return 1;
  }
  Out.write(reinterpret_cast<const char *>(TextBytes.data()), TextBytes.size());
  Out.close();
  if (Out.has_error()) {
    errs() << "dyncode-extractor: write error\n";
    return 1;
  }

  if (Verbose) {
    errs() << "dyncode-extractor: wrote " << TextBytes.size() << " bytes to '"
           << OutputBin << "'\n";
    errs() << "dyncode-extractor: target   = " << triplePrettyName(Target)
           << " (ELF)\n";
    if (Fragments.size() > 1)
      errs() << "dyncode-extractor: merged " << Fragments.size()
             << " .text sections\n";
    errs() << "dyncode-extractor: entry symbol = " << ChosenEntry << "\n";
    if (Target.Arch == DynCodeArch::AArch64) {
      errs() << "dyncode-extractor: patched " << PatchedBranch
             << " CALL/JUMP26, " << PatchedPage21 << " ADR_PREL_PG_HI21, "
             << PatchedLo12 << " LO12 reloc(s)\n";
    } else {
      errs() << "dyncode-extractor: patched " << PatchedX86Rel32
             << " PC32/PLT32 reloc(s)\n";
    }
  }
  return 0;
}

} // namespace dyncode
} // namespace neverc
