#include "ExtractorCommon.h"
#include "neverc/Shellcode/Pipeline/Pipeline.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/MachO.h"
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

const char *arm64RelocTypeName(uint64_t T) {
  switch (T) {
  case 0:
    return "ARM64_RELOC_UNSIGNED";
  case 1:
    return "ARM64_RELOC_SUBTRACTOR";
  case 2:
    return "ARM64_RELOC_BRANCH26";
  case 3:
    return "ARM64_RELOC_PAGE21";
  case 4:
    return "ARM64_RELOC_PAGEOFF12";
  case 5:
    return "ARM64_RELOC_GOT_LOAD_PAGE21";
  case 6:
    return "ARM64_RELOC_GOT_LOAD_PAGEOFF12";
  case 7:
    return "ARM64_RELOC_POINTER_TO_GOT";
  case 8:
    return "ARM64_RELOC_TLVP_LOAD_PAGE21";
  case 9:
    return "ARM64_RELOC_TLVP_LOAD_PAGEOFF12";
  case 10:
    return "ARM64_RELOC_ADDEND";
  default:
    return "UNKNOWN";
  }
}

const char *x86_64RelocTypeName(uint64_t T) {
  switch (T) {
  case 0:
    return "X86_64_RELOC_UNSIGNED";
  case 1:
    return "X86_64_RELOC_SIGNED";
  case 2:
    return "X86_64_RELOC_BRANCH";
  case 3:
    return "X86_64_RELOC_GOT_LOAD";
  case 4:
    return "X86_64_RELOC_GOT";
  case 5:
    return "X86_64_RELOC_SUBTRACTOR";
  case 6:
    return "X86_64_RELOC_SIGNED_1";
  case 7:
    return "X86_64_RELOC_SIGNED_2";
  case 8:
    return "X86_64_RELOC_SIGNED_4";
  case 9:
    return "X86_64_RELOC_TLV";
  default:
    return "UNKNOWN";
  }
}

const char *machoRelocTypeName(const TargetDesc &Target, uint64_t T) {
  return Target.Arch == ShellcodeArch::AArch64 ? arm64RelocTypeName(T)
                                               : x86_64RelocTypeName(T);
}

} // namespace

int extractMachO(StringRef InputObj, StringRef OutputBin,
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

  if (!Obj.isMachO()) {
    errs() << "shellcode-extractor: '" << InputObj
           << "' is not a Mach-O object (got "
           << Triple::getArchTypeName(Obj.getArch()) << ")\n";
    return 1;
  }
  Triple::ArchType ExpectedArch =
      Target.Arch == ShellcodeArch::AArch64 ? Triple::aarch64 : Triple::x86_64;
  if (Obj.getArch() != ExpectedArch) {
    errs() << "shellcode-extractor: Mach-O arch mismatch (got "
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
    errs() << "shellcode-extractor: no __TEXT,__text section found in '"
           << InputObj << "'\n";
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
  MutableArrayRef<uint8_t> TextViewAll(TextBytes);

  unsigned ExternalRelocCount = 0;
  unsigned PatchedBranch26 = 0;
  unsigned PatchedPage21 = 0;
  unsigned PatchedPageOff12 = 0;
  unsigned PatchedX86Pcrel32 = 0;
  const bool IsArm64 = Target.Arch == ShellcodeArch::AArch64;

  for (const auto &Reloc : TextSec.relocations()) {
    auto SymOrErr = Reloc.getSymbol();
    StringRef SymName = "<unknown>";
    bool IsDefined = false;
    uint64_t TargetAddr = 0;
    if (SymOrErr != Obj.symbol_end()) {
      auto NameOrErr = SymOrErr->getName();
      if (NameOrErr) {
        SymName = *NameOrErr;
        auto It = DefinedSyms.find(SymName);
        if (It != DefinedSyms.end()) {
          IsDefined = true;
          TargetAddr = It->second;
        }
      }
    }

    if (!IsDefined) {
      ExternalRelocCount++;
      errs() << "shellcode-extractor: unresolved relocation referencing '"
             << SymName << "' (" << machoRelocTypeName(Target, Reloc.getType())
             << ")\n";
      printExternHint(errs(), Target, SymName);
      continue;
    }

    uint64_t RelocOffset = Reloc.getOffset();
    uint64_t RelocType = Reloc.getType();
    uint64_t TextEnd = TextAddr + TextBytes.size();

    if (!IsArm64) {
      auto rejectUnsupported = [&](const char *Reason) {
        ExternalRelocCount++;
        errs() << "shellcode-extractor: unsupported x86_64 Mach-O reloc "
               << machoRelocTypeName(Target, RelocType) << " at offset 0x";
        errs().write_hex(RelocOffset);
        errs() << " referencing '" << SymName << "' -- " << Reason << "\n";
      };
      switch (RelocType) {
      case 1:   // X86_64_RELOC_SIGNED
      case 2:   // X86_64_RELOC_BRANCH
      case 6:   // X86_64_RELOC_SIGNED_1
      case 7:   // X86_64_RELOC_SIGNED_2
      case 8: { // X86_64_RELOC_SIGNED_4
        if (TargetAddr < TextAddr || TargetAddr >= TextEnd) {
          ExternalRelocCount++;
          errs() << "shellcode-extractor: pcrel32 target '" << SymName
                 << "' at 0x";
          errs().write_hex(TargetAddr);
          errs() << " is outside __text (expected [0x";
          errs().write_hex(TextAddr);
          errs() << ", 0x";
          errs().write_hex(TextEnd);
          errs() << "))\n";
          continue;
        }
        if (RelocOffset + 4 > TextBytes.size()) {
          ExternalRelocCount++;
          errs() << "shellcode-extractor: pcrel32 reloc at offset 0x";
          errs().write_hex(RelocOffset);
          errs() << " extends past __text end\n";
          continue;
        }
        unsigned Extra = 0;
        switch (RelocType) {
        case 6:
          Extra = 1;
          break;
        case 7:
          Extra = 2;
          break;
        case 8:
          Extra = 4;
          break;
        default:
          Extra = 0;
          break;
        }
        int64_t TargetOff64 = static_cast<int64_t>(TargetAddr - TextAddr);
        int64_t Expected = TargetOff64 - static_cast<int64_t>(RelocOffset) -
                           static_cast<int64_t>(4 + Extra);
        if (Expected < INT32_MIN || Expected > INT32_MAX) {
          ExternalRelocCount++;
          errs() << "shellcode-extractor: pcrel32 out of range at offset 0x";
          errs().write_hex(RelocOffset);
          errs() << " (delta=" << Expected << ")\n";
          continue;
        }
        int32_t Stored;
        memcpy(&Stored, &TextBytes[RelocOffset], 4);
        if (static_cast<int64_t>(Stored) != Expected) {
          int32_t E32 = static_cast<int32_t>(Expected);
          memcpy(&TextBytes[RelocOffset], &E32, 4);
        }
        ++PatchedX86Pcrel32;
        continue;
      }
      case 0: // X86_64_RELOC_UNSIGNED -- absolute address, unsupported.
        rejectUnsupported("absolute 64/32-bit addresses require a loader; "
                          "shellcode must route globals through the stack "
                          "(Data2TextPass) or via resolver parameters");
        continue;
      case 3: // X86_64_RELOC_GOT_LOAD
      case 4: // X86_64_RELOC_GOT
        rejectUnsupported(
            "GOT indirection is not available in shellcode; keep "
            "references to symbols defined inside the same __text "
            "section or route externs through the resolver shim");
        continue;
      case 5: // X86_64_RELOC_SUBTRACTOR
        rejectUnsupported(
            "section-relative subtractor relocations pair with an "
            "UNSIGNED entry and require loader fix-ups");
        continue;
      case 9: // X86_64_RELOC_TLV
        rejectUnsupported(
            "thread-local variables have no loader-provided TLS "
            "slot in shellcode; shellcode mode already downgrades "
            "_Thread_local to plain static");
        continue;
      default:
        rejectUnsupported("unknown / unsupported relocation type");
        continue;
      }
    }

    if (TargetAddr < TextAddr || TargetAddr >= TextEnd) {
      ExternalRelocCount++;
      errs() << "shellcode-extractor: relocation target '" << SymName
             << "' at 0x";
      errs().write_hex(TargetAddr);
      errs() << " is outside __text (expected [0x";
      errs().write_hex(TextAddr);
      errs() << ", 0x";
      errs().write_hex(TextEnd);
      errs() << "))\n";
      if (SymName.starts_with("lCPI") || SymName.starts_with("LCPI"))
        errs() << "shellcode-extractor: hint: the backend spilled a "
                  "constant into a literal pool; check for large "
                  "integer / FP / SIMD constants that aren't being "
                  "inlined (Data2TextPass only inlines scalar values "
                  "it recognises).\n";
      continue;
    }
    uint64_t TargetOff = TargetAddr - TextAddr;
    (void)TextEnd;

    if (RelocOffset + 4 > TextBytes.size()) {
      ExternalRelocCount++;
      errs() << "shellcode-extractor: relocation at offset 0x";
      errs().write_hex(RelocOffset);
      errs() << " extends past __text end\n";
      continue;
    }

    switch (RelocType) {
    case 2: {
      int64_t PCRelOffset =
          static_cast<int64_t>(TargetOff) - static_cast<int64_t>(RelocOffset);
      if (!patchArm64Branch26(TextViewAll, RelocOffset, PCRelOffset)) {
        ExternalRelocCount++;
        errs() << "shellcode-extractor: cannot patch BRANCH26 at offset 0x";
        errs().write_hex(RelocOffset);
        errs() << "\n";
        continue;
      }
      ++PatchedBranch26;
      break;
    }
    case 3:
      if (!patchArm64Page21(TextViewAll, RelocOffset,
                            static_cast<int64_t>(TargetOff), RelocOffset)) {
        ExternalRelocCount++;
        errs() << "shellcode-extractor: cannot patch PAGE21 at offset 0x";
        errs().write_hex(RelocOffset);
        errs() << "\n";
        continue;
      }
      ++PatchedPage21;
      break;
    case 4: {
      uint32_t Inst;
      memcpy(&Inst, &TextBytes[RelocOffset], 4);
      bool IsAdd = (Inst & 0xFF800000) == 0x91000000;
      bool IsIntLdSt = (Inst & 0x3F000000) == 0x39000000;
      bool IsFpLdSt = (Inst & 0x3F000000) == 0x3D000000;
      if (!IsAdd && !IsIntLdSt && !IsFpLdSt) {
        ExternalRelocCount++;
        errs() << "shellcode-extractor: cannot patch PAGEOFF12 at offset 0x";
        errs().write_hex(RelocOffset);
        errs() << " (unknown opcode 0x";
        errs().write_hex(Inst);
        errs() << ")\n";
        continue;
      }
      bool OK =
          IsAdd
              ? patchArm64Lo12WithShift(TextViewAll, RelocOffset, TargetOff, 0)
              : patchArm64Lo12AutoShift(TextViewAll, RelocOffset, TargetOff,
                                        /*IsLdSt=*/true);
      if (!OK) {
        ExternalRelocCount++;
        errs() << "shellcode-extractor: cannot patch PAGEOFF12 at offset 0x";
        errs().write_hex(RelocOffset);
        errs() << "\n";
        continue;
      }
      ++PatchedPageOff12;
      break;
    }
    default: {
      ExternalRelocCount++;
      errs() << "shellcode-extractor: unsupported intra-section "
                "relocation type "
             << RelocType << " at offset 0x";
      errs().write_hex(RelocOffset);
      errs() << " referencing '" << SymName << "'\n";
      break;
    }
    }
  }

  if (ExternalRelocCount > 0) {
    errs() << "shellcode-extractor: " << ExternalRelocCount
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
    StringRef Name = *NameOrErr;
    if (!isForbiddenDataSection(Target, Name))
      continue;
    auto DataOrErr = Sec.getContents();
    uint64_t Size = 0;
    if (DataOrErr)
      Size = DataOrErr->size();
    if (Size > 0) {
      errs() << "shellcode-extractor: unexpected data section '" << Name
             << "' (" << Size
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
    if (!EntrySymbol.empty())
      errs() << "shellcode-extractor: entry symbol '" << EntrySymbol
             << "' not found in '" << InputObj << "'\n";
    else
      errs() << "shellcode-extractor: no entry symbol ("
             << defaultEntryNameList() << ") found in '" << InputObj << "'\n";

    SmallVector<StringRef, 8> Candidates;
    for (const auto &Sym : Obj.symbols()) {
      auto NameOrErr = Sym.getName();
      if (!NameOrErr)
        continue;
      StringRef N = *NameOrErr;
      if (N.empty() || N.starts_with("ltmp") || N.starts_with("l_.") ||
          isShellcodeInternalRuntimeName(N))
        continue;
      Candidates.push_back(N);
    }
    if (!Candidates.empty()) {
      errs() << "shellcode-extractor: defined symbols in this object:";
      for (StringRef N : Candidates)
        errs() << " " << N;
      errs() << "\n";
      errs() << "shellcode-extractor: hint: pass -fshellcode-entry=<name> "
                "to pick one\n";
    }
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
           << " (Mach-O)\n";
    errs() << "shellcode-extractor: entry symbol = " << ChosenEntry << "\n";
    if (IsArm64)
      errs() << "shellcode-extractor: patched " << PatchedBranch26
             << " BRANCH26, " << PatchedPage21 << " PAGE21, "
             << PatchedPageOff12 << " PAGEOFF12 intra-section reloc(s)\n";
    else
      errs() << "shellcode-extractor: patched " << PatchedX86Pcrel32
             << " x86_64 pcrel32 intra-section reloc(s)\n";
  }
  return 0;
}

} // namespace shellcode
} // namespace neverc
