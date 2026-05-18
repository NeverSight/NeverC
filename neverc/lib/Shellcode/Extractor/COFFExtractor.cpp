#include "ExtractorCommon.h"
#include "neverc/Shellcode/Pipeline/Pipeline.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
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

const char *coffArm64RelName(uint16_t T) {
  switch (T) {
  case COFF::IMAGE_REL_ARM64_ABSOLUTE:
    return "IMAGE_REL_ARM64_ABSOLUTE";
  case COFF::IMAGE_REL_ARM64_ADDR32:
    return "IMAGE_REL_ARM64_ADDR32";
  case COFF::IMAGE_REL_ARM64_ADDR32NB:
    return "IMAGE_REL_ARM64_ADDR32NB";
  case COFF::IMAGE_REL_ARM64_BRANCH26:
    return "IMAGE_REL_ARM64_BRANCH26";
  case COFF::IMAGE_REL_ARM64_PAGEBASE_REL21:
    return "IMAGE_REL_ARM64_PAGEBASE_REL21";
  case COFF::IMAGE_REL_ARM64_REL21:
    return "IMAGE_REL_ARM64_REL21";
  case COFF::IMAGE_REL_ARM64_PAGEOFFSET_12A:
    return "IMAGE_REL_ARM64_PAGEOFFSET_12A";
  case COFF::IMAGE_REL_ARM64_PAGEOFFSET_12L:
    return "IMAGE_REL_ARM64_PAGEOFFSET_12L";
  case COFF::IMAGE_REL_ARM64_SECREL:
    return "IMAGE_REL_ARM64_SECREL";
  case COFF::IMAGE_REL_ARM64_ADDR64:
    return "IMAGE_REL_ARM64_ADDR64";
  case COFF::IMAGE_REL_ARM64_BRANCH19:
    return "IMAGE_REL_ARM64_BRANCH19";
  case COFF::IMAGE_REL_ARM64_BRANCH14:
    return "IMAGE_REL_ARM64_BRANCH14";
  case COFF::IMAGE_REL_ARM64_REL32:
    return "IMAGE_REL_ARM64_REL32";
  default:
    return "IMAGE_REL_ARM64_UNKNOWN";
  }
}

const char *coffAmd64RelName(uint16_t T) {
  switch (T) {
  case COFF::IMAGE_REL_AMD64_ABSOLUTE:
    return "IMAGE_REL_AMD64_ABSOLUTE";
  case COFF::IMAGE_REL_AMD64_ADDR64:
    return "IMAGE_REL_AMD64_ADDR64";
  case COFF::IMAGE_REL_AMD64_ADDR32:
    return "IMAGE_REL_AMD64_ADDR32";
  case COFF::IMAGE_REL_AMD64_ADDR32NB:
    return "IMAGE_REL_AMD64_ADDR32NB";
  case COFF::IMAGE_REL_AMD64_REL32:
    return "IMAGE_REL_AMD64_REL32";
  case COFF::IMAGE_REL_AMD64_REL32_1:
    return "IMAGE_REL_AMD64_REL32_1";
  case COFF::IMAGE_REL_AMD64_REL32_2:
    return "IMAGE_REL_AMD64_REL32_2";
  case COFF::IMAGE_REL_AMD64_REL32_3:
    return "IMAGE_REL_AMD64_REL32_3";
  case COFF::IMAGE_REL_AMD64_REL32_4:
    return "IMAGE_REL_AMD64_REL32_4";
  case COFF::IMAGE_REL_AMD64_REL32_5:
    return "IMAGE_REL_AMD64_REL32_5";
  default:
    return "IMAGE_REL_AMD64_UNKNOWN";
  }
}

} // namespace

int extractCOFF(StringRef InputObj, StringRef OutputBin,
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
  auto *Obj = dyn_cast<COFFObjectFile>(&**ObjOrErr);
  if (!Obj) {
    errs() << "shellcode-extractor: expected COFF object\n";
    return 1;
  }

  Triple::ArchType ExpectedArch =
      Target.Arch == ShellcodeArch::AArch64 ? Triple::aarch64 : Triple::x86_64;
  if (Obj->getArch() != ExpectedArch) {
    errs() << "shellcode-extractor: object arch mismatch (got "
           << Triple::getArchTypeName(Obj->getArch()) << ", expected "
           << Triple::getArchTypeName(ExpectedArch) << ")\n";
    return 1;
  }

  bool FoundText = false;
  SectionRef TextSec;
  ArrayRef<uint8_t> TextData;
  uint64_t TextAddr = 0;
  for (const SectionRef &Sec : Obj->sections()) {
    auto NameOrErr = Sec.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    StringRef N = *NameOrErr;
    if (!isTextSection(Target, N))
      continue;
    auto DataOrErr = Sec.getContents();
    if (!DataOrErr) {
      errs() << "shellcode-extractor: cannot read text section '" << N
             << "': " << toString(DataOrErr.takeError()) << "\n";
      return 1;
    }
    if (DataOrErr->empty())
      continue;
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
  for (const auto &Sym : Obj->symbols()) {
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
  unsigned PatchedRel32 = 0;

  for (const RelocationRef &Reloc : TextSec.relocations()) {
    uint16_t Type = static_cast<uint16_t>(Reloc.getType());
    uint64_t RelocOff = Reloc.getOffset();
    auto SymIt = Reloc.getSymbol();

    StringRef Name = "<unknown>";
    bool IsDefined = false;
    uint64_t SymAddr = 0;
    if (SymIt != Obj->symbol_end()) {
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
                                ? coffArm64RelName(Type)
                                : coffAmd64RelName(Type);
      errs() << "shellcode-extractor: unresolved relocation referencing '"
             << Name << "' (" << RelName << ")\n";
      printExternHint(errs(), Target, Name);
      continue;
    }

    uint64_t TextEnd = TextAddr + TextBytes.size();
    if (SymAddr < TextAddr || SymAddr >= TextEnd) {
      ++External;
      errs() << "shellcode-extractor: relocation target '" << Name << "' at 0x";
      errs().write_hex(SymAddr);
      errs() << " is outside .text\n";
      continue;
    }
    uint64_t SiteAddr = TextAddr + RelocOff;
    int64_t PCDisp =
        static_cast<int64_t>(SymAddr) - static_cast<int64_t>(SiteAddr);

    if (Target.Arch == ShellcodeArch::X86_64) {
      switch (Type) {
      case COFF::IMAGE_REL_AMD64_REL32:
      case COFF::IMAGE_REL_AMD64_REL32_1:
      case COFF::IMAGE_REL_AMD64_REL32_2:
      case COFF::IMAGE_REL_AMD64_REL32_3:
      case COFF::IMAGE_REL_AMD64_REL32_4:
      case COFF::IMAGE_REL_AMD64_REL32_5: {
        int Extra = static_cast<int>(Type - COFF::IMAGE_REL_AMD64_REL32);
        int64_t Disp = PCDisp - 4 - Extra;
        if (!patchRel32(TextView, RelocOff, Disp)) {
          ++External;
          continue;
        }
        ++PatchedRel32;
        break;
      }
      default: {
        ++External;
        errs() << "shellcode-extractor: unsupported intra-.text relocation "
               << coffAmd64RelName(Type) << " at 0x";
        errs().write_hex(RelocOff);
        errs() << " referencing '" << Name << "'\n";
        break;
      }
      }
    } else if (Target.Arch == ShellcodeArch::AArch64) {
      switch (Type) {
      case COFF::IMAGE_REL_ARM64_BRANCH26:
        if (!patchArm64Branch26(TextView, RelocOff, PCDisp)) {
          ++External;
          continue;
        }
        ++PatchedBranch;
        break;
      case COFF::IMAGE_REL_ARM64_PAGEBASE_REL21:
        if (!patchArm64Page21(TextView, RelocOff, static_cast<int64_t>(SymAddr),
                              SiteAddr)) {
          ++External;
          continue;
        }
        ++PatchedPage21;
        break;
      case COFF::IMAGE_REL_ARM64_PAGEOFFSET_12A:
        if (!patchArm64Lo12AutoShift(TextView, RelocOff, SymAddr,
                                     /*IsLdSt=*/false)) {
          ++External;
          continue;
        }
        ++PatchedLo12;
        break;
      case COFF::IMAGE_REL_ARM64_PAGEOFFSET_12L:
        if (!patchArm64Lo12AutoShift(TextView, RelocOff, SymAddr,
                                     /*IsLdSt=*/true)) {
          ++External;
          continue;
        }
        ++PatchedLo12;
        break;
      case COFF::IMAGE_REL_ARM64_REL32: {
        if (!patchRel32(TextView, RelocOff, PCDisp - 4)) {
          ++External;
          continue;
        }
        ++PatchedRel32;
        break;
      }
      default: {
        ++External;
        errs() << "shellcode-extractor: unsupported intra-.text relocation "
               << coffArm64RelName(Type) << " at 0x";
        errs().write_hex(RelocOff);
        errs() << " referencing '" << Name << "'\n";
        break;
      }
      }
    } else {
      ++External;
      errs() << "shellcode-extractor: COFF extraction not implemented for "
             << archName(Target.Arch) << "\n";
    }
  }

  if (External > 0) {
    errs() << "shellcode-extractor: " << External
           << " external relocation(s) found — shellcode must be "
              "fully resolved\n";
    return 1;
  }

  for (const auto &Sec : Obj->sections()) {
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
  for (const auto &Sym : Obj->symbols()) {
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
           << " (COFF)\n";
    errs() << "shellcode-extractor: entry symbol = " << ChosenEntry << "\n";
    if (Target.Arch == ShellcodeArch::AArch64) {
      errs() << "shellcode-extractor: patched " << PatchedBranch
             << " BRANCH26, " << PatchedPage21 << " PAGEBASE_REL21, "
             << PatchedLo12 << " PAGEOFFSET_12, " << PatchedRel32
             << " REL32 reloc(s)\n";
    } else {
      errs() << "shellcode-extractor: patched " << PatchedRel32
             << " REL32 reloc(s)\n";
    }
  }
  return 0;
}

} // namespace shellcode
} // namespace neverc
