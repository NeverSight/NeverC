#include "ExtractorCommon.h"
#include "neverc/DynCode/Pipeline/Pipeline.h"
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
namespace dyncode {

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
  auto *Obj = dyn_cast<COFFObjectFile>(&**ObjOrErr);
  if (!Obj) {
    errs() << "dyncode-extractor: expected COFF object\n";
    return 1;
  }

  Triple::ArchType ExpectedArch =
      Target.Arch == DynCodeArch::AArch64 ? Triple::aarch64 : Triple::x86_64;
  if (Obj->getArch() != ExpectedArch) {
    errs() << "dyncode-extractor: object arch mismatch (got "
           << Triple::getArchTypeName(Obj->getArch()) << ", expected "
           << Triple::getArchTypeName(ExpectedArch) << ")\n";
    return 1;
  }

  struct TextFragment {
    SectionRef Section;
    ArrayRef<uint8_t> Data;
    uint64_t MergedOffset = 0;
  };
  SmallVector<TextFragment, 8> Fragments;
  for (const SectionRef &Sec : Obj->sections()) {
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
  for (const auto &Sym : Obj->symbols()) {
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
    if (*SecOrErr == Obj->section_end())
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
    for (const auto &Sym : Obj->symbols()) {
      auto NOrErr = Sym.getName();
      if (!NOrErr)
        continue;
      StringRef N = *NOrErr;
      if (N.empty() || N.starts_with(".") ||
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
  for (const auto &Sym : Obj->symbols()) {
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
    if (*SecOrErr == Obj->section_end())
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
  unsigned PatchedRel32 = 0;

  for (auto &Frag : Fragments) {
    for (const RelocationRef &Reloc : Frag.Section.relocations()) {
      uint16_t Type = static_cast<uint16_t>(Reloc.getType());
      uint64_t RelocOff = Reloc.getOffset() + Frag.MergedOffset;
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
        const char *RelName = Target.Arch == DynCodeArch::AArch64
                                  ? coffArm64RelName(Type)
                                  : coffAmd64RelName(Type);
        errs() << "dyncode-extractor: unresolved relocation referencing '"
               << Name << "' (" << RelName << ")\n";
        printExternHint(errs(), Target, Name);
        continue;
      }

      if (SymAddr >= TotalSize) {
        ++External;
        errs() << "dyncode-extractor: relocation target '" << Name
               << "' at 0x";
        errs().write_hex(SymAddr);
        errs() << " is outside merged .text\n";
        continue;
      }
      int64_t PCDisp =
          static_cast<int64_t>(SymAddr) - static_cast<int64_t>(RelocOff);

      if (Target.Arch == DynCodeArch::X86_64) {
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
          errs() << "dyncode-extractor: unsupported intra-.text relocation "
                 << coffAmd64RelName(Type) << " at 0x";
          errs().write_hex(RelocOff);
          errs() << " referencing '" << Name << "'\n";
          break;
        }
        }
      } else if (Target.Arch == DynCodeArch::AArch64) {
        switch (Type) {
        case COFF::IMAGE_REL_ARM64_BRANCH26:
          if (!patchArm64Branch26(TextView, RelocOff, PCDisp)) {
            ++External;
            continue;
          }
          ++PatchedBranch;
          break;
        case COFF::IMAGE_REL_ARM64_PAGEBASE_REL21:
          if (!patchArm64Page21(TextView, RelocOff,
                                static_cast<int64_t>(SymAddr), RelocOff)) {
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
          errs() << "dyncode-extractor: unsupported intra-.text relocation "
                 << coffArm64RelName(Type) << " at 0x";
          errs().write_hex(RelocOff);
          errs() << " referencing '" << Name << "'\n";
          break;
        }
        }
      } else {
        ++External;
        errs() << "dyncode-extractor: COFF extraction not implemented for "
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
           << " (COFF)\n";
    if (Fragments.size() > 1)
      errs() << "dyncode-extractor: merged " << Fragments.size()
             << " .text sections\n";
    errs() << "dyncode-extractor: entry symbol = " << ChosenEntry << "\n";
    if (Target.Arch == DynCodeArch::AArch64) {
      errs() << "dyncode-extractor: patched " << PatchedBranch
             << " BRANCH26, " << PatchedPage21 << " PAGEBASE_REL21, "
             << PatchedLo12 << " PAGEOFFSET_12, " << PatchedRel32
             << " REL32 reloc(s)\n";
    } else {
      errs() << "dyncode-extractor: patched " << PatchedRel32
             << " REL32 reloc(s)\n";
    }
  }
  return 0;
}

} // namespace dyncode
} // namespace neverc
