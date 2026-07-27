#include "Extractor/ExtractorCommon.h"
#include "neverc/DynCode/IR/StringRuntimeABI.h"
#include "neverc/DynCode/Import/KernelImportABI.h"
#include "neverc/DynCode/Import/SyscallTables.h"
#include "neverc/DynCode/Import/WinImportTables.h"
#include "neverc/DynCode/Pipeline/Pipeline.h"
#include "neverc/DynCode/Pipeline/SymbolNames.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

using namespace llvm;

namespace neverc {
namespace dyncode {

StringRef stripLeadingUnderscore(StringRef S) {
  return SymbolNames::stripObjectLeadingUnderscore(S);
}

bool isDynCodeInternalRuntimeName(StringRef Name) {
#define NEVERC_DYNCODE_INTERNAL_RUNTIME_PREFIX(prefix)                       \
  static_assert(sizeof(prefix) > 1 &&                                          \
                    ((prefix)[0] == '_' || (prefix)[0] == 'l'),                \
                "every DynCodeInternalRuntimePrefixes row must start with "  \
                "'_' or 'l' so the fast-path in "                              \
                "isDynCodeInternalRuntimeName stays sound -- add the new "   \
                "leading character to the whitelist below before extending "   \
                "the .def table");
#include "neverc/DynCode/Tables/DynCodeInternalRuntimePrefixes.def"
#include "neverc/DynCode/Tables/UserExtra_DynCodeInternalRuntimePrefixes.def"
#undef NEVERC_DYNCODE_INTERNAL_RUNTIME_PREFIX

  if (Name.empty())
    return false;
  char Front = Name.front();
  if (Front != '_' && Front != 'l')
    return false;
#define NEVERC_DYNCODE_INTERNAL_RUNTIME_PREFIX(prefix)                       \
  if (Name.starts_with(prefix))                                                \
    return true;
#include "neverc/DynCode/Tables/DynCodeInternalRuntimePrefixes.def"
#include "neverc/DynCode/Tables/UserExtra_DynCodeInternalRuntimePrefixes.def"
#undef NEVERC_DYNCODE_INTERNAL_RUNTIME_PREFIX
  return false;
}

bool isDefaultEntryName(StringRef Bare) {
  static constexpr StringRef kNames[] = {
#define NEVERC_DEFAULT_ENTRY(name) StringRef(#name),
#include "neverc/DynCode/Tables/DefaultEntryNames.def"
#include "neverc/DynCode/Tables/UserExtra_DefaultEntryNames.def"
#undef NEVERC_DEFAULT_ENTRY
  };
  for (StringRef N : kNames)
    if (Bare == N)
      return true;
  return false;
}

bool isDynCodeEntryCandidate(StringRef Name, StringRef UserEntry) {
  StringRef Bare = stripLeadingUnderscore(Name);
  if (!UserEntry.empty()) {
    StringRef WantBare = stripLeadingUnderscore(UserEntry);
    return Bare == WantBare || Name == UserEntry;
  }
  return isDefaultEntryName(Bare);
}

std::string defaultEntryNameList() {
  std::string Out;
  bool First = true;
#define NEVERC_DEFAULT_ENTRY(name)                                             \
  do {                                                                         \
    if (!First)                                                                \
      Out.append(" / ");                                                       \
    Out.append(#name);                                                         \
    First = false;                                                             \
  } while (0);
#include "neverc/DynCode/Tables/DefaultEntryNames.def"
#include "neverc/DynCode/Tables/UserExtra_DefaultEntryNames.def"
#undef NEVERC_DEFAULT_ENTRY
  return Out;
}

bool patchArm64Branch26(MutableArrayRef<uint8_t> Text, uint64_t Off,
                        int64_t PCDisp) {
  if (Off + 4 > Text.size())
    return false;
  uint32_t Inst;
  std::memcpy(&Inst, &Text[Off], 4);
  int32_t Imm26 = static_cast<int32_t>(PCDisp >> 2) & 0x03FFFFFF;
  Inst = (Inst & 0xFC000000) | static_cast<uint32_t>(Imm26);
  std::memcpy(&Text[Off], &Inst, 4);
  return true;
}

bool patchArm64Page21(MutableArrayRef<uint8_t> Text, uint64_t Off,
                      int64_t SymAddr, uint64_t SiteAddr) {
  if (Off + 4 > Text.size())
    return false;
  int64_t SymPage = SymAddr >> 12;
  int64_t PcPage = static_cast<int64_t>(SiteAddr) >> 12;
  int64_t PageDiff = SymPage - PcPage;
  uint32_t Imm21 = static_cast<uint32_t>(PageDiff & 0x1FFFFF);
  uint32_t ImmLo = Imm21 & 0x3;
  uint32_t ImmHi = (Imm21 >> 2) & 0x7FFFF;
  uint32_t Inst;
  std::memcpy(&Inst, &Text[Off], 4);
  Inst &= ~static_cast<uint32_t>(0x60FFFFE0);
  Inst |= (ImmLo << 29) | (ImmHi << 5);
  std::memcpy(&Text[Off], &Inst, 4);
  return true;
}

bool patchArm64Lo12WithShift(MutableArrayRef<uint8_t> Text, uint64_t Off,
                             uint64_t TargetAddr, unsigned Shift) {
  if (Off + 4 > Text.size())
    return false;
  uint32_t Inst;
  std::memcpy(&Inst, &Text[Off], 4);
  uint64_t Off12 = TargetAddr & 0xFFF;
  uint32_t Scaled = static_cast<uint32_t>((Off12 >> Shift) & 0xFFF);
  Inst &= ~static_cast<uint32_t>(0x003FFC00);
  Inst |= (Scaled << 10);
  std::memcpy(&Text[Off], &Inst, 4);
  return true;
}

bool patchArm64Lo12AutoShift(MutableArrayRef<uint8_t> Text, uint64_t Off,
                             uint64_t TargetAddr, bool IsLdSt) {
  if (Off + 4 > Text.size())
    return false;
  uint32_t Inst;
  std::memcpy(&Inst, &Text[Off], 4);
  unsigned Shift = 0;
  if (IsLdSt) {
    bool IsInt = (Inst & 0x3F000000) == 0x39000000;
    bool IsFp = (Inst & 0x3F000000) == 0x3D000000;
    if (!IsInt && !IsFp)
      return false;
    Shift = (Inst >> 30) & 0x3;
    if (IsFp && Shift == 0 && ((Inst >> 23) & 0x1) == 1)
      Shift = 4;
  }
  uint64_t Off12 = TargetAddr & 0xFFF;
  uint32_t Scaled = static_cast<uint32_t>((Off12 >> Shift) & 0xFFF);
  Inst &= ~static_cast<uint32_t>(0x003FFC00);
  Inst |= (Scaled << 10);
  std::memcpy(&Text[Off], &Inst, 4);
  return true;
}

bool patchRel32(MutableArrayRef<uint8_t> Text, uint64_t Off, int64_t PCDisp) {
  if (Off + 4 > Text.size())
    return false;
  int32_t Disp = static_cast<int32_t>(PCDisp);
  std::memcpy(&Text[Off], &Disp, 4);
  return true;
}

bool patchRel64(MutableArrayRef<uint8_t> Text, uint64_t Off, int64_t PCDisp) {
  if (Off + 8 > Text.size())
    return false;
  int64_t Disp = PCDisp;
  std::memcpy(&Text[Off], &Disp, 8);
  return true;
}

namespace {

struct TextRule {
  ArrayRef<StringRef> Exact;
  ArrayRef<StringRef> Prefix;
};

struct ForbiddenRule {
  ArrayRef<StringRef> Exact;
  ArrayRef<StringRef> Prefix;
};

constexpr StringRef kMachOTextExact[] = {"__text"};

constexpr StringRef kMachOForbiddenExact[] = {
    "__data",     "__bss",      "__common",    "__cstring",        "__const",
    "__literal4", "__literal8", "__literal16", "__compact_unwind", "__eh_frame",
};

constexpr StringRef kElfTextExact[] = {".text"};
constexpr StringRef kElfTextPrefix[] = {".text."};

constexpr StringRef kElfForbiddenExact[] = {
    ".data",       ".bss",   ".rodata", ".data.rel.ro", ".init_array",
    ".fini_array", ".ctors", ".dtors",  ".eh_frame",    ".eh_frame_hdr",
};
constexpr StringRef kElfForbiddenPrefix[] = {".rodata."};

constexpr StringRef kCoffTextExact[] = {".text"};
constexpr StringRef kCoffTextPrefix[] = {".text$", ".text."};

constexpr StringRef kCoffForbiddenExact[] = {
    ".data",  ".bss",   ".rdata", ".pdata", ".xdata",
    ".gfids", ".didat", ".edata", ".idata",
};
constexpr StringRef kCoffForbiddenPrefix[] = {".rdata$", ".data$", ".CRT$",
                                              ".tls$"};

TextRule textRuleFor(ObjectFormat Fmt) {
  switch (Fmt) {
  case ObjectFormat::MachO:
    return {kMachOTextExact, {}};
  case ObjectFormat::ELF:
    return {kElfTextExact, kElfTextPrefix};
  case ObjectFormat::COFF:
    return {kCoffTextExact, kCoffTextPrefix};
  default:
    return {{}, {}};
  }
}

ForbiddenRule forbiddenRuleFor(ObjectFormat Fmt) {
  switch (Fmt) {
  case ObjectFormat::MachO:
    return {kMachOForbiddenExact, {}};
  case ObjectFormat::ELF:
    return {kElfForbiddenExact, kElfForbiddenPrefix};
  case ObjectFormat::COFF:
    return {kCoffForbiddenExact, kCoffForbiddenPrefix};
  default:
    return {{}, {}};
  }
}

bool matchesRule(ArrayRef<StringRef> Exact, ArrayRef<StringRef> Prefix,
                 StringRef Name) {
  for (StringRef E : Exact)
    if (Name == E)
      return true;
  for (StringRef P : Prefix)
    if (Name.starts_with(P))
      return true;
  return false;
}

} // namespace

bool isTextSection(const TargetDesc &Target, StringRef Name) {
  TextRule R = textRuleFor(Target.Format);
  return matchesRule(R.Exact, R.Prefix, Name);
}

bool isForbiddenDataSection(const TargetDesc &Target, StringRef Name) {
  ForbiddenRule R = forbiddenRuleFor(Target.Format);
  return matchesRule(R.Exact, R.Prefix, Name);
}

namespace {

void printHexByte(raw_ostream &Os, uint8_t Byte) {
  static constexpr char Hex[] = "0123456789abcdef";
  Os << "0x" << Hex[(Byte >> 4) & 0xF] << Hex[Byte & 0xF];
}

void printContext(raw_ostream &Os, ArrayRef<uint8_t> Bytes, size_t Off) {
  size_t Begin = Off > 4 ? Off - 4 : 0;
  size_t End = std::min(Bytes.size(), Off + 5);
  Os << "context:";
  for (size_t I = Begin; I < End; ++I) {
    Os << (I == Off ? " [" : " ");
    printHexByte(Os, Bytes[I]);
    if (I == Off)
      Os << "]";
  }
}

} // namespace

bool auditFinalBadBytes(ArrayRef<uint8_t> Bytes, const DynCodeOptions &Opts) {
  if (Opts.BadBytes.empty())
    return true;

  bool Forbidden[256] = {};
  for (uint8_t Byte : Opts.BadBytes)
    Forbidden[Byte] = true;

  unsigned Hits = 0;
  constexpr unsigned MaxReported = 8;
  for (size_t I = 0, E = Bytes.size(); I < E; ++I) {
    uint8_t Byte = Bytes[I];
    if (!Forbidden[Byte])
      continue;
    ++Hits;
    if (Hits <= MaxReported) {
      errs() << "dyncode-extractor: forbidden output byte ";
      printHexByte(errs(), Byte);
      errs() << " at offset 0x";
      errs().write_hex(I);
      errs() << " (";
      printContext(errs(), Bytes, I);
      errs() << ")\n";
    }
  }

  if (Hits == 0)
    return true;

  errs() << "dyncode-extractor: bad-byte audit failed: " << Hits
         << " forbidden byte occurrence(s) in final";
  if (!Opts.BadByteProfile.empty())
    errs() << " profile '" << Opts.BadByteProfile << "'";
  errs() << " dyncode bytes; output was not written\n";
  errs() << "dyncode-extractor: hint: adjust the source/codegen so the "
            "emitted instructions avoid these bytes, relax the bad-byte "
            "policy, or register a post-extract encoder interpose that rewrites "
            "the payload before this final audit.\n";
  return false;
}

namespace {

bool applyDynCodeSizing(SmallVectorImpl<uint8_t> &Bytes,
                          const DynCodeOptions &Opts) {
  uint8_t Pad = Opts.PadByte.value_or(0x00);

  bool WantsPadding = (Opts.Align > 1 || Opts.MaxLength.has_value());

  if (WantsPadding && !Opts.BadBytes.empty()) {
    bool BadHit = false;
    for (uint8_t B : Opts.BadBytes) {
      if (B == Pad) {
        BadHit = true;
        break;
      }
    }
    if (BadHit) {
      errs() << "dyncode-extractor: implicit pad byte ";
      printHexByte(errs(), Pad);
      errs() << " is in the bad-byte set; pass -fdyncode-pad=<byte> with a "
                "byte that the bad-byte audit accepts\n";
      return false;
    }
  }

  if (Opts.Align > 1) {
    uint64_t Mask = static_cast<uint64_t>(Opts.Align) - 1;
    uint64_t CurrentLen = Bytes.size();
    uint64_t Aligned = (CurrentLen + Mask) & ~Mask;
    if (Aligned > CurrentLen)
      Bytes.append(static_cast<size_t>(Aligned - CurrentLen), Pad);
  }

  if (Opts.MaxLength) {
    uint64_t Limit = *Opts.MaxLength;
    if (Bytes.size() > Limit) {
      errs() << "dyncode-extractor: dyncode size " << Bytes.size()
             << " bytes exceeds -fdyncode-max-length=" << Limit << "\n";
      return false;
    }
    if (Bytes.size() < Limit)
      Bytes.append(static_cast<size_t>(Limit - Bytes.size()), Pad);
  }

  return true;
}

} // namespace

int finalizeDynCodeBytes(SmallVectorImpl<uint8_t> &Bytes,
                           const DynCodeOptions &Opts) {
  applyPostExtractObfuscationInterpose(Bytes, Opts);
  if (!auditFinalBadBytes(Bytes, Opts))
    return 1;
  if (!applyDynCodeSizing(Bytes, Opts))
    return 1;
  applyPostFinalizeObfuscationInterpose(Bytes, Opts);
  return 0;
}

namespace {

template <size_t N>
bool nameInTable(StringRef Bare, const StringRef (&Table)[N]) {
  for (StringRef E : Table)
    if (Bare == E)
      return true;
  return false;
}

} // namespace

bool isReservedMemStdlibName(StringRef Bare) {
  static constexpr StringRef kNames[] = {
#define NEVERC_NAME(name) StringRef(#name),
#include "neverc/DynCode/Tables/ReservedMemStdlibNames.def"
#include "neverc/DynCode/Tables/UserExtra_ReservedMemStdlibNames.def"
#undef NEVERC_NAME
  };
  return nameInTable(Bare, kNames);
}

namespace {

bool isComplexLibcall(StringRef Bare) {
  static constexpr StringRef kNames[] = {
#define NEVERC_NAME(name) StringRef(#name),
#include "neverc/DynCode/Tables/ComplexLibcallNames.def"
#include "neverc/DynCode/Tables/UserExtra_ComplexLibcallNames.def"
#undef NEVERC_NAME
  };
  return nameInTable(Bare, kNames);
}

} // namespace

bool isLibmTranscendentalName(StringRef Bare) {
  static constexpr StringRef kNames[] = {
#define NEVERC_NAME(name) StringRef(#name),
#include "neverc/DynCode/Tables/LibmTranscendentalNames.def"
#include "neverc/DynCode/Tables/UserExtra_LibmTranscendentalNames.def"
#undef NEVERC_NAME
  };
  return nameInTable(Bare, kNames);
}

bool isStdioCallName(StringRef Bare) {
  static constexpr StringRef kNames[] = {
#define NEVERC_NAME(name) StringRef(#name),
#include "neverc/DynCode/Tables/StdioCallNames.def"
#include "neverc/DynCode/Tables/UserExtra_StdioCallNames.def"
#undef NEVERC_NAME
  };
  return nameInTable(Bare, kNames);
}

bool isScalarSoftFloatHelperName(StringRef Bare) {
  static constexpr StringRef kNames[] = {
#define NEVERC_NAME(name) StringRef(#name),
#include "neverc/DynCode/Tables/ScalarSoftFloatNames.def"
#include "neverc/DynCode/Tables/UserExtra_ScalarSoftFloatNames.def"
#undef NEVERC_NAME
  };
  return nameInTable(Bare, kNames);
}

bool isLongIntegerCompilerRtHelperName(StringRef Bare) {
  static constexpr StringRef kPrefixes[] = {
#define NEVERC_PREFIX(prefix) StringRef(#prefix),
#include "neverc/DynCode/Tables/LongIntegerHelperPrefixes.def"
#include "neverc/DynCode/Tables/UserExtra_LongIntegerHelperPrefixes.def"
#undef NEVERC_PREFIX
  };
  for (StringRef P : kPrefixes)
    if (Bare.starts_with(P))
      return true;
  return false;
}

bool isBinary128HelperName(StringRef Bare) {
  static constexpr StringRef kNames[] = {
#define NEVERC_NAME(name) StringRef(#name),
#include "neverc/DynCode/Tables/Binary128HelperNames.def"
#include "neverc/DynCode/Tables/UserExtra_Binary128HelperNames.def"
#undef NEVERC_NAME
  };
  return nameInTable(Bare, kNames);
}

bool isHeapAllocatorName(StringRef Bare) {
  static constexpr StringRef kNames[] = {
#define NEVERC_NAME(name) StringRef(#name),
#include "neverc/DynCode/Tables/HeapAllocatorNames.def"
#include "neverc/DynCode/Tables/UserExtra_HeapAllocatorNames.def"
#undef NEVERC_NAME
  };
  return nameInTable(Bare, kNames);
}

bool isBuiltinStringRuntimeName(StringRef Bare) {
  return StringRuntimeABI::isRuntimeSymbolName(Bare);
}

bool isSetjmpName(StringRef Bare) {
  static constexpr StringRef kNames[] = {
#define NEVERC_NAME(name) StringRef(#name),
#include "neverc/DynCode/Tables/SetjmpNames.def"
#include "neverc/DynCode/Tables/UserExtra_SetjmpNames.def"
#undef NEVERC_NAME
  };
  return nameInTable(Bare, kNames);
}

bool isCompilerRtRuntimeHelperName(StringRef Bare) {
  if (!Bare.starts_with("__"))
    return false;
  if (Bare.size() < 4)
    return false;
  char Last = Bare.back();
  if (Last < '0' || Last > '9')
    return false;
  StringRef Middle = Bare.drop_front(2).drop_back(1);
  for (char C : Middle)
    if (!std::isalnum(static_cast<unsigned char>(C)) && C != '_')
      return false;
  return !Middle.empty();
}

namespace {

struct KernelHelperRow {
  StringRef Name;
  StringRef OSTag;
};
constexpr KernelHelperRow kKernelHelperTable[] = {
#define NEVERC_KERNEL_HELPER(name, os) {#name, os},
#include "neverc/DynCode/Tables/KernelHelperNames.def"
#include "neverc/DynCode/Tables/UserExtra_KernelHelperNames.def"
#undef NEVERC_KERNEL_HELPER
};

bool isKernelHelperFor(StringRef Bare, StringRef OSTag) {
  for (const KernelHelperRow &R : kKernelHelperTable)
    if (R.Name == Bare && R.OSTag == OSTag)
      return true;
  return false;
}

} // namespace

StringRef lookupKernelHelperOS(StringRef Bare) {
  for (const KernelHelperRow &R : kKernelHelperTable)
    if (R.Name == Bare)
      return R.OSTag;
  return {};
}

namespace {

StringRef osTag(const TargetDesc &T) {
  switch (T.OS) {
  case DynCodeOS::Linux:
  case DynCodeOS::Android:
    return "linux";
  case DynCodeOS::Windows:
    return "windows";
  case DynCodeOS::Darwin:
    return "darwin";
  default:
    return "";
  }
}

StringRef kernelImportLabel(KernelImportABI K) {
  switch (K) {
  case KernelImportABI::WindowsKernelResolverShim:
    return "the Windows kernel resolver shim";
  case KernelImportABI::LinuxKallsymsShim:
    return "the Linux kallsyms-backed loader shim";
  case KernelImportABI::DarwinXNUKextShim:
    return "the Darwin XNU kext loader shim";
  case KernelImportABI::None:
    break;
  }
  return KernelResolverABI::LoaderResolverShimLabel;
}

bool looksLikeCurrentTargetKernelHelper(const TargetDesc &T, StringRef Bare) {
  StringRef Tag = osTag(T);
  if (Tag.empty())
    return false;
  return isKernelHelperFor(Bare, Tag);
}

std::string exactCompilerGeneratedExternHint(StringRef Name) {
  struct Row {
    StringRef Name;
    StringRef Hint;
  };
  static constexpr Row kRows[] = {
#define NEVERC_EXTERN_HINT(name, message) {#name, message},
#include "neverc/DynCode/Tables/CompilerGeneratedExternHints.def"
#include "neverc/DynCode/Tables/UserExtra_CompilerGeneratedExternHints.def"
#undef NEVERC_EXTERN_HINT
  };
  for (const Row &R : kRows)
    if (Name == R.Name)
      return R.Hint.str();
  return {};
}

} // namespace

std::string getExternalSymbolHint(StringRef Name, const TargetDesc &Target,
                                  ExternalSymbolHintContext Context) {
  StringRef Bare = stripLeadingUnderscore(Name);

  if (Target.Level == ExecutionLevel::Kernel) {
    StringRef HelperOS = lookupKernelHelperOS(Bare);
    StringRef Tag = osTag(Target);
    if (!HelperOS.empty() && (Tag.empty() || HelperOS == Tag))
      return "ring-0 kernel helper survived KernelImportPass; this normally "
             "only happens when the call is inside an inline asm template or "
             "a custom MIR pass reinserts it after codegen -- move the helper "
             "call back into ordinary C so KernelImportPass can rewrite it "
             "into a resolver-backed call";
  } else {
    StringRef HelperOS = lookupKernelHelperOS(Bare);
    if (!HelperOS.empty())
      return std::string("extern is a ") + HelperOS.str() +
             " kernel-only helper and cannot be called from ring-3 dyncode; "
             "pass -mdyncode-context=kernel if this is actually a ring-0 "
             "payload, or drop the call";
  }

  if (std::string Hint = exactCompilerGeneratedExternHint(Name); !Hint.empty())
    return Hint;
  if (Name != Bare) {
    if (std::string Hint = exactCompilerGeneratedExternHint(Bare);
        !Hint.empty())
      return Hint;
  }

  if (isReservedMemStdlibName(Bare))
    return "libc-style string/memory/stdlib helper survived codegen; "
           "MemIntrinPass inlines these at IR level -- either re-enable the "
           "pass or split the operation into a bounded manual loop so the "
           "backend cannot re-outline it";

  if (isComplexLibcall(Bare))
    return "compiler-rt helper for _Complex arithmetic emitted; dyncode has "
           "no compiler-rt runtime. Replace _Complex types with "
           "`struct { double re, im; }` and expand multiplication/division "
           "manually";

  if (isScalarSoftFloatHelperName(Bare)) {
    if ((Target.Level == ExecutionLevel::Kernel &&
         Target.Arch == DynCodeArch::AArch64) ||
        Context.FunctionHasGeneralRegsOnly)
      return "scalar soft-float helper emitted because dyncode kernel mode "
             "injects -mgeneral-regs-only on AArch64 (FPU/NEON is off-limits "
             "in EL1 without explicit save/restore). Either move the FP work "
             "into a ring-3 helper invoked from your loader, or wrap the FP "
             "block in the platform's kernel-FPU begin/end primitive "
             "(`kernel_fpu_begin/end` on Linux, "
             "`KeSaveExtendedProcessorState` on Windows) and rebuild with "
             "-mdyncode-context=user for that translation unit";
    return "scalar soft-float helper emitted; dyncode mode has no "
           "compiler-rt to link. Switch the operation to native FP widths "
           "supported by the hardware (binary32/binary64) or fold the "
           "constants at compile time";
  }

  if (isLongIntegerCompilerRtHelperName(Bare))
    return "compiler-rt helper emitted; keep arithmetic within native 64-bit "
           "widths or expand the operation manually";

  if (isBinary128HelperName(Bare))
    return "long double on this target is IEEE binary128 and needs "
           "compiler-rt soft-float helpers; dyncode mode has no runtime to "
           "link. Prefer double (64-bit) or provide your own soft-float "
           "runtime";

  if (Bare == "dyld_stub_binder" || Bare == "dyld_stub_binding_helper")
    return "dyld stub emitted; pass -mdyncode-syscall so libSystem externs "
           "are rewritten to inline svc #0x80";

  if (isLibmTranscendentalName(Bare))
    return "libm transcendental call emitted; dyncode does not link libm. "
           "Either replace the call with a hardware FP intrinsic the backend "
           "can lower in-place (`__builtin_sqrtf` for sqrt, `__builtin_fabs` "
           "for absolute value, etc.), or precompute / approximate the value "
           "manually (e.g. polynomial / lookup table) so no runtime symbol is "
           "needed";

  if (isStdioCallName(Bare))
    return "stdio call emitted; dyncode has no FILE / vararg backend. For "
           "terminal output drop the format string and call the platform write "
           "primitive directly (POSIX `write(fd, buf, n)` rewritten by "
           "SyscallStubPass on Linux / macOS / Android, `WriteFile` resolved "
           "through WinPEBImportPass on Windows). For string formatting build "
           "the buffer yourself with `__builtin_memcpy` / manual digit "
           "conversion";

  if (Bare == "alloca" || Bare == "_alloca")
    return "alloca() emitted; dyncode mode has no runtime helper to adjust "
           "the stack pointer at call sites. C99 variable-length arrays "
           "(`int buf[n];`) compile cleanly via inline `sub sp, sp, x?` / "
           "`sub rsp, ?` sequences and are the recommended replacement; for "
           "unbounded sizes pass a heap pointer in from the loader instead";

  if (isHeapAllocatorName(Bare))
    return "heap allocator call emitted; HeapArenaPass should have rewritten "
           "this call. If -fno-dyncode-heap-arena was passed, re-enable it "
           "(default) or: (a) size buffers with stack arrays / VLAs and thread "
           "the base pointer down, (b) carve a fixed-size scratch arena out of "
           "`static char arena[N]`, or (c) ask the loader to hand a heap "
           "pointer in via the entry function's argument";

  if (isBuiltinStringRuntimeName(Bare))
    return "NeverC builtin string runtime helper survived codegen; dyncode "
           "mode expects the BuiltinString prelude to emit these helpers and "
           "StringRuntimePass to rewrite their allocator path before import "
           "resolution. Check that builtin string predefines are enabled and "
           "the dyncode pipeline includes StringRuntimePass";

  if (isSetjmpName(Bare))
    return "setjmp/longjmp emitted; dyncode does not link libc and cannot "
           "capture callee-saved state portably. Replace the non-local exit "
           "with a return code propagated up the call chain, or hand-roll a "
           "tiny inline-asm save/restore for the specific registers your "
           "control flow touches";

  if (isCompilerRtRuntimeHelperName(Bare))
    return "compiler-rt helper (wide-integer / soft-float / bit-count) "
           "emitted; dyncode has no runtime to link it in. Rewrite the "
           "expression to use widths the hardware supports (fits in 64 bits "
           "on arm64 / x86_64)";

  return {};
}

void printExternHint(raw_ostream &Os, const TargetDesc &Target,
                     StringRef Name) {
  StringRef Bare = stripLeadingUnderscore(Name);

  if (Target.Level == ExecutionLevel::Kernel &&
      looksLikeCurrentTargetKernelHelper(Target, Bare)) {
    Os << "dyncode-extractor: hint: '" << Bare
       << "' is a ring-0 kernel helper but the call survived codegen; "
          "KernelImportPass usually rewrites direct extern calls into "
          "resolver-backed indirect calls via "
       << kernelImportLabel(Target.KernelImport)
       << ". Inspect the call site (inline asm / custom pipeline?) or "
          "keep the reference out of the dyncode module.\n";
    return;
  }

  if (Target.OS == DynCodeOS::Windows) {
    if (isLikelyWin32ApiName(Name)) {
      Os << "dyncode-extractor: hint: '" << Name
         << "' is a known Win32 API; pass -mdyncode-win-peb-import so "
            "the dyncode pipeline resolves it at runtime via a PEB walk "
            "(kernel32 / ntdll / user32 / ws2_32 / advapi32 / shell32 "
            "are covered).\n";
      return;
    }
    if (isLikelySyscallName(Bare)) {
      const char *Win32Alt = nullptr;
#define NEVERC_POSIX_WIN32(bareName, win32Name)                                \
  if (Bare == #bareName)                                                       \
    Win32Alt = win32Name;                                                      \
  else
#include "neverc/DynCode/Tables/PosixToWin32Alt.def"
#include "neverc/DynCode/Tables/UserExtra_PosixToWin32Alt.def"
#undef NEVERC_POSIX_WIN32
      {
      }
      Os << "dyncode-extractor: hint: '" << Bare
         << "' is a POSIX libc name and has no Windows kernel-syscall "
            "mapping; rewrite the call to use the Win32 API";
      if (Win32Alt)
        Os << " (" << Win32Alt << ")";
      Os << " and pass -mdyncode-win-peb-import so the pipeline "
            "resolves it at runtime via a PEB walk.\n";
      return;
    }
  }
  if (isLikelySyscallName(Bare)) {
    if (Target.Syscall == SyscallABI::None) {
      Os << "dyncode-extractor: hint: '" << Bare
         << "' looks like a libc primitive, but this (OS, arch) pair "
            "has no ring-3 syscall auto-lowering wired up in the "
            "dyncode pipeline yet. Either thread the helper in via "
            "a function pointer parameter from the loader (same shape "
            "as `-mdyncode-context=kernel`'s resolver), or retarget "
            "this payload at a triple whose TargetDesc fills in a "
            "concrete SyscallABI.\n";
      return;
    }
    Os << "dyncode-extractor: hint: '" << Bare
       << "' looks like a libc primitive mapped to a kernel syscall; "
          "pass -mdyncode-syscall (alias -mdyncode-libsystem) to "
          "inline it as the target's native kernel trap (svc / syscall).\n";
    return;
  }
  std::string Hint = getExternalSymbolHint(Bare, Target);
  if (!Hint.empty())
    Os << "dyncode-extractor: hint: '" << Bare << "' " << Hint << ".\n";
}

} // namespace dyncode
} // namespace neverc
