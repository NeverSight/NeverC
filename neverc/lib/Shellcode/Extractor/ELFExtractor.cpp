#include "ExtractorCommon.h"
#include "neverc/Shellcode/Pipeline/Pipeline.h"
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
namespace shellcode {

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
               const ShellcodeOptions &Opts) {
  const TargetDesc &Target = Opts.Target;
  StringRef EntrySymbol = Opts.EntrySymbol;
  bool Verbose = Opts.Verbose;
  auto BufOrErr = MemoryBuffer::getFile(InputObj);
  if (!BufOrErr) {
    errs() << "shellcode-extractor: cannot open '" << InputObj
           << "': " << BufOrErr.getError().message() << "\n";
    return 1;
  }

  auto ObjOrErr = ObjectFile::createObjectFile((*BufOrErr)->getMemBufferRef());
  if (!ObjOrErr) {
    errs() << "shellcode-extractor: '" << InputObj
           << "' is not a valid object file: " << toString(ObjOrErr.takeError())
           << "\n";
    return 1;
  }
  auto &Obj = **ObjOrErr;

  if (!Obj.isELF()) {
    errs() << "shellcode-extractor: expected ELF, got "
           << (Obj.isMachO()  ? "Mach-O"
               : Obj.isCOFF() ? "COFF"
                              : "?")
           << "\n";
    return 1;
  }

  Triple::ArchType ExpectedArch =
      Target.Arch == ShellcodeArch::AArch64 ? Triple::aarch64 : Triple::x86_64;
  if (Obj.getArch() != ExpectedArch) {
    errs() << "shellcode-extractor: object arch mismatch (got "
           << Triple::getArchTypeName(Obj.getArch()) << ", expected "
           << Triple::getArchTypeName(ExpectedArch) << ")\n";
    return 1;
  }

  bool FoundText = false;
  SectionRef TextSec;
  ArrayRef<uint8_t> TextData;
  uint64_t TextAddr = 0;
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
      errs() << "shellcode-extractor: cannot read text section '" << *NameOrErr
             << "': " << toString(DataOrErr.takeError()) << "\n";
      return 1;
    }
    TextData = arrayRefFromStringRef(*DataOrErr);
    TextAddr = Sec.getAddress();
    TextSec = Sec;
    FoundText = true;
    break;
  }
  if (!FoundText || TextData.empty()) {
    errs() << "shellcode-extractor: no .text section found in '" << InputObj
           << "'\n";
    return 1;
  }

  DenseMap<StringRef, uint64_t> DefinedSyms;
  for (const auto &Sym : Obj.symbols()) {
    Expected<uint32_t> FlagsOrErr = Sym.getFlags();
    if (!FlagsOrErr) {
      consumeError(FlagsOrErr.takeError());
      continue;
    }
    if (*FlagsOrErr & BasicSymbolRef::SF_Undefined)
      continue;
    auto AddrOrErr = Sym.getAddress();
    if (!AddrOrErr) {
      consumeError(AddrOrErr.takeError());
      continue;
    }
    auto NameOrErr = Sym.getName();
    if (NameOrErr)
      DefinedSyms[*NameOrErr] = *AddrOrErr;
  }

  SmallVector<uint8_t, 256> TextBytes(TextData.begin(), TextData.end());
  MutableArrayRef<uint8_t> TextView(TextBytes);

  unsigned External = 0;
  unsigned PatchedBranch = 0;
  unsigned PatchedPage21 = 0;
  unsigned PatchedLo12 = 0;
  unsigned PatchedX86Rel32 = 0;

  SmallVector<SectionRef, 2> RelocSectionsForText;
  unsigned TextIdx = TextSec.getIndex();
  for (const SectionRef &Sec : Obj.sections()) {
    auto RelocatedOrErr = Sec.getRelocatedSection();
    if (!RelocatedOrErr) {
      consumeError(RelocatedOrErr.takeError());
      continue;
    }
    if (*RelocatedOrErr == Obj.section_end())
      continue;
    if ((*RelocatedOrErr)->getIndex() == TextIdx)
      RelocSectionsForText.push_back(Sec);
  }
  if (RelocSectionsForText.empty())
    RelocSectionsForText.push_back(TextSec);

  for (const SectionRef &RelocSec : RelocSectionsForText)
    for (const RelocationRef &Reloc : RelocSec.relocations()) {
      uint64_t RelocOff = Reloc.getOffset();
      uint64_t RelocType = Reloc.getType();
      auto SymIt = Reloc.getSymbol();
      int64_t Addend = 0;
      if (auto *ERel = dyn_cast<ELFObjectFileBase>(&Obj)) {
        (void)ERel;
        if (auto AddOrErr = cast<ELFRelocationRef>(Reloc).getAddend())
          Addend = *AddOrErr;
      }

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
        const char *RelName = Target.Arch == ShellcodeArch::AArch64
                                  ? aarch64ElfName(RelocType)
                                  : x86_64ElfName(RelocType);
        errs() << "shellcode-extractor: unresolved relocation referencing '"
               << Name << "' (" << RelName << ")\n";
        printExternHint(errs(), Target, Name);
        continue;
      }

      uint64_t TextEnd = TextAddr + TextBytes.size();
      int64_t FinalAddr = static_cast<int64_t>(SymAddr) + Addend;
      if (static_cast<uint64_t>(FinalAddr) < TextAddr ||
          static_cast<uint64_t>(FinalAddr) >= TextEnd) {
        ++External;
        errs() << "shellcode-extractor: relocation target '" << Name
               << "' at 0x";
        errs().write_hex(FinalAddr);
        errs() << " is outside .text\n";
        continue;
      }

      uint64_t SiteAddr = TextAddr + RelocOff;
      int64_t PCDisp = FinalAddr - static_cast<int64_t>(SiteAddr);

      if (Target.Arch == ShellcodeArch::AArch64) {
        switch (RelocType) {
        case ELF::R_AARCH64_CALL26:
        case ELF::R_AARCH64_JUMP26:
          if (!patchArm64Branch26(TextView, RelocOff, PCDisp)) {
            ++External;
            errs() << "shellcode-extractor: cannot patch branch26 at 0x";
            errs().write_hex(RelocOff);
            errs() << "\n";
            continue;
          }
          ++PatchedBranch;
          break;
        case ELF::R_AARCH64_ADR_PREL_PG_HI21:
        case ELF::R_AARCH64_ADR_PREL_PG_HI21_NC:
          if (!patchArm64Page21(TextView, RelocOff, FinalAddr, SiteAddr)) {
            ++External;
            errs() << "shellcode-extractor: cannot patch adrp at 0x";
            errs().write_hex(RelocOff);
            errs() << "\n";
            continue;
          }
          ++PatchedPage21;
          break;
        case ELF::R_AARCH64_ADD_ABS_LO12_NC:
          if (!patchArm64Lo12WithShift(TextView, RelocOff, FinalAddr, 0)) {
            ++External;
            errs() << "shellcode-extractor: cannot patch add lo12 at 0x";
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
            errs() << "shellcode-extractor: cannot patch ldst lo12 at 0x";
            errs().write_hex(RelocOff);
            errs() << "\n";
            continue;
          }
          ++PatchedLo12;
          break;
        case ELF::R_AARCH64_PREL32:
          if (!patchRel32(TextView, RelocOff, PCDisp)) {
            ++External;
            errs() << "shellcode-extractor: cannot patch prel32 at 0x";
            errs().write_hex(RelocOff);
            errs() << "\n";
            continue;
          }
          ++PatchedLo12;
          break;
        case ELF::R_AARCH64_PREL64:
          if (!patchRel64(TextView, RelocOff, PCDisp)) {
            ++External;
            errs() << "shellcode-extractor: cannot patch prel64 at 0x";
            errs().write_hex(RelocOff);
            errs() << "\n";
            continue;
          }
          ++PatchedLo12;
          break;
        default: {
          ++External;
          errs() << "shellcode-extractor: unsupported intra-.text relocation "
                 << aarch64ElfName(RelocType) << " at 0x";
          errs().write_hex(RelocOff);
          errs() << " referencing '" << Name << "'\n";
          break;
        }
        }
      } else if (Target.Arch == ShellcodeArch::X86_64) {
        switch (RelocType) {
        case ELF::R_X86_64_PC32:
        case ELF::R_X86_64_PLT32:
          if (!patchRel32(TextView, RelocOff, PCDisp)) {
            ++External;
            errs() << "shellcode-extractor: cannot patch rel32 at 0x";
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
          errs() << "shellcode-extractor: GOT-based relocation "
                 << x86_64ElfName(RelocType) << " at 0x";
          errs().write_hex(RelocOff);
          errs() << " referencing '" << Name
                 << "' — shellcode cannot resolve GOT entries; pass "
                    "-fshellcode-all-blr to force indirect calls or "
                    "rewrite the reference as a function-local helper.\n";
          continue;
        default:
          ++External;
          errs() << "shellcode-extractor: unsupported intra-.text relocation "
                 << x86_64ElfName(RelocType) << " at 0x";
          errs().write_hex(RelocOff);
          errs() << " referencing '" << Name << "'\n";
          break;
        }
      } else {
        ++External;
        errs() << "shellcode-extractor: ELF extraction not implemented for "
               << archName(Target.Arch) << "\n";
      }
    }

  if (External > 0) {
    errs() << "shellcode-extractor: " << External
           << " external relocation(s) found — shellcode must be "
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
      errs() << "shellcode-extractor: unexpected data section '" << N << "' ("
             << Size
             << " bytes) — Data2Text pass should have eliminated all "
                "constant data\n";
      return 1;
    }
  }

  bool FoundEntry = false;
  std::string ChosenEntry;
  for (const auto &Sym : Obj.symbols()) {
    auto NameOrErr = Sym.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    StringRef Name = *NameOrErr;
    StringRef Bare = stripLeadingUnderscore(Name);
    bool IsCandidate = false;
    if (!EntrySymbol.empty()) {
      StringRef WantBare = stripLeadingUnderscore(EntrySymbol);
      IsCandidate = (Bare == WantBare || Name == EntrySymbol);
    } else {
      IsCandidate = isDefaultEntryName(Bare);
    }
    if (!IsCandidate)
      continue;
    auto AddrOrErr = Sym.getAddress();
    if (!AddrOrErr) {
      consumeError(AddrOrErr.takeError());
      continue;
    }
    uint64_t Offset = *AddrOrErr - TextAddr;
    if (Offset != 0) {
      errs() << "shellcode-extractor: entry symbol '" << Name
             << "' is at offset " << Offset << " but must be at offset 0\n";
      return 1;
    }
    FoundEntry = true;
    ChosenEntry = Name.str();
    break;
  }
  if (!FoundEntry) {
    errs() << "shellcode-extractor: no entry symbol (";
    if (!EntrySymbol.empty())
      errs() << "'" << EntrySymbol << "'";
    else
      errs() << defaultEntryNameList();
    errs() << ") found in '" << InputObj << "'\n";
    return 1;
  }

  if (int Rc = finalizeShellcodeBytes(TextBytes, Opts))
    return Rc;

  std::error_code EC;
  raw_fd_ostream Out(OutputBin, EC, sys::fs::OF_None);
  if (EC) {
    errs() << "shellcode-extractor: cannot write '" << OutputBin
           << "': " << EC.message() << "\n";
    return 1;
  }
  Out.write(reinterpret_cast<const char *>(TextBytes.data()), TextBytes.size());
  Out.close();
  if (Out.has_error()) {
    errs() << "shellcode-extractor: write error\n";
    return 1;
  }

  if (Verbose) {
    errs() << "shellcode-extractor: wrote " << TextBytes.size() << " bytes to '"
           << OutputBin << "'\n";
    errs() << "shellcode-extractor: target   = " << triplePrettyName(Target)
           << " (ELF)\n";
    errs() << "shellcode-extractor: entry symbol = " << ChosenEntry << "\n";
    if (Target.Arch == ShellcodeArch::AArch64) {
      errs() << "shellcode-extractor: patched " << PatchedBranch
             << " CALL/JUMP26, " << PatchedPage21 << " ADR_PREL_PG_HI21, "
             << PatchedLo12 << " LO12 reloc(s)\n";
    } else {
      errs() << "shellcode-extractor: patched " << PatchedX86Rel32
             << " PC32/PLT32 reloc(s)\n";
    }
  }
  return 0;
}

} // namespace shellcode
} // namespace neverc
