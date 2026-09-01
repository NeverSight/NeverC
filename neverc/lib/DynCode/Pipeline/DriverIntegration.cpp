#include "neverc/DynCode/Pipeline/DriverIntegration.h"
#include "neverc/DynCode/Pipeline/Pipeline.h"
#include "neverc/Invoke/Options.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <cstring>

using llvm::ArrayRef;
using llvm::SmallString;
using llvm::SmallVector;
using llvm::SmallVectorImpl;
using llvm::StringRef;

namespace opts = neverc::driver::options;

namespace neverc {
namespace dyncode {

namespace {

struct Incompat {
  // Store the raw option enum rather than llvm::opt::OptSpecifier: the latter
  // has no constexpr constructor, which would force this table into writable
  // storage with a dynamic initializer.
  opts::ID ID;
  const char *Reason;
};
constexpr Incompat Incompats[] = {
#define NEVERC_SC_INCOMPAT(optId, reason) {opts::optId, reason},
#include "neverc/DynCode/Tables/DynCodeIncompats.def"
#include "neverc/DynCode/Tables/UserExtra_DynCodeIncompats.def"
#undef NEVERC_SC_INCOMPAT
};

void appendBadByte(DynCodeOptions &Out, uint8_t Byte) {
  if (!llvm::is_contained(Out.BadBytes, Byte))
    Out.BadBytes.push_back(Byte);
}

bool parseHexByte(StringRef Token, uint8_t &Byte) {
  Token = Token.trim();
  if (!Token.consume_front("0x"))
    Token.consume_front("0X");
  if (Token.empty() || Token.size() > 2)
    return false;
  unsigned Value = 0;
  for (char C : Token) {
    unsigned N = llvm::hexDigitValue(C);
    if (N == ~0U)
      return false;
    Value = (Value << 4) | N;
  }
  Byte = static_cast<uint8_t>(Value);
  return true;
}

bool parseBadByteList(const llvm::opt::Arg *A, DynCodeOptions &Out) {
  StringRef Spec = A->getValue();
  if (Spec.trim().empty()) {
    llvm::errs() << "neverc: error: " << A->getSpelling()
                 << " expects a comma-separated hex byte list like "
                    "'00,0a,0d'\n";
    return false;
  }

  SmallVector<StringRef, 16> Tokens;
  Spec.split(Tokens, ',');
  for (StringRef Tok : Tokens) {
    uint8_t Byte = 0;
    if (!parseHexByte(Tok, Byte)) {
      llvm::errs() << "neverc: error: " << A->getSpelling()
                   << " expects comma-separated hex bytes in range 00-ff; "
                      "got '"
                   << Tok.trim() << "' in '" << Spec << "'\n";
      return false;
    }
    appendBadByte(Out, Byte);
  }
  return true;
}

bool parseUnsignedDriverInt(StringRef Spec, uint64_t &Out) {
  Spec = Spec.trim();
  if (Spec.empty())
    return false;
  return !Spec.getAsInteger(0, Out);
}

bool parseMaxLengthArg(const llvm::opt::Arg *A, DynCodeOptions &Out) {
  uint64_t Value = 0;
  if (!parseUnsignedDriverInt(A->getValue(), Value) || Value == 0) {
    llvm::errs() << "neverc: error: " << A->getSpelling()
                 << " expects a positive byte count (decimal or 0x-prefixed "
                    "hex), got '"
                 << A->getValue() << "'\n";
    return false;
  }
  Out.MaxLength = Value;
  return true;
}

bool parseAlignArg(const llvm::opt::Arg *A, DynCodeOptions &Out) {
  uint64_t Value = 0;
  if (!parseUnsignedDriverInt(A->getValue(), Value) || Value == 0) {
    llvm::errs() << "neverc: error: " << A->getSpelling()
                 << " expects a positive byte count (decimal or 0x-prefixed "
                    "hex), got '"
                 << A->getValue() << "'\n";
    return false;
  }
  if ((Value & (Value - 1)) != 0) {
    llvm::errs() << "neverc: error: " << A->getSpelling()
                 << " must be a power of two; got " << Value << "\n";
    return false;
  }
  if (Value > UINT32_MAX) {
    llvm::errs() << "neverc: error: " << A->getSpelling() << " value " << Value
                 << " exceeds 32-bit alignment range\n";
    return false;
  }
  Out.Align = static_cast<uint32_t>(Value);
  return true;
}

bool parsePadArg(const llvm::opt::Arg *A, DynCodeOptions &Out) {
  uint8_t Byte = 0;
  if (!parseHexByte(A->getValue(), Byte)) {
    llvm::errs() << "neverc: error: " << A->getSpelling()
                 << " expects a single hex byte in range 00-ff, got '"
                 << A->getValue() << "'\n";
    return false;
  }
  Out.PadByte = Byte;
  return true;
}

struct BadByteProfile {
  StringRef Name;
  ArrayRef<uint8_t> Bytes;
};

bool applyBadByteProfile(const llvm::opt::Arg *A, DynCodeOptions &Out) {
  static constexpr uint8_t NullBytes[] = {0x00};
  static constexpr uint8_t HttpNewlineBytes[] = {0x00, 0x0A, 0x0D};
  static constexpr uint8_t WhitespaceBytes[] = {0x00, 0x09, 0x0A, 0x0B,
                                                0x0C, 0x0D, 0x20};
  static constexpr uint8_t AsciiControlBytes[] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
      0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
      0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x7F};

  static const BadByteProfile kBadByteProfiles[] = {
      {"null", NullBytes},
      {"c-string", NullBytes},
      {"http-newline", HttpNewlineBytes},
      {"line", HttpNewlineBytes},
      {"whitespace", WhitespaceBytes},
      {"ascii-control", AsciiControlBytes},
  };

  StringRef Name = A->getValue();
  for (const BadByteProfile &P : kBadByteProfiles) {
    if (Name == P.Name) {
      Out.BadByteProfile = P.Name.str();
      for (uint8_t Byte : P.Bytes)
        appendBadByte(Out, Byte);
      return true;
    }
  }

  llvm::errs() << "neverc: error: unknown " << A->getSpelling() << " value '"
               << Name
               << "'. Supported profiles: null, c-string, http-newline, "
                  "line, whitespace, ascii-control\n";
  return false;
}

const char *const CommonInjectFlags[] = {
#define NEVERC_SC_FLAG(flag) flag,
#include "neverc/DynCode/Tables/DynCodeInjectFlags.def"
#include "neverc/DynCode/Tables/UserExtra_DynCodeInjectFlags.def"
#undef NEVERC_SC_FLAG
};

SmallVector<const char *, 8> perTargetInjectFlags(const TargetDesc &T) {
  SmallVector<const char *, 8> Out;
  if (T.DriverInjectFlags) {
    for (const char *const *P = T.DriverInjectFlags; *P != nullptr; ++P)
      Out.push_back(*P);
  }
  if (T.Level == ExecutionLevel::Kernel) {
    Out.push_back("-D__NEVERC_DYNCODE_KERNEL__=1");
    if (T.KernelInjectFlags) {
      for (const char *const *P = T.KernelInjectFlags; *P != nullptr; ++P)
        Out.push_back(*P);
    }
  }
  return Out;
}

const char *saveCStr(std::set<std::string> &Pool, StringRef S) {
  return Pool.insert(std::string(S)).first->c_str();
}

bool collectOptions(const llvm::opt::InputArgList &Args, DynCodeOptions &Out) {
  Out.Enabled = Args.hasFlag(opts::OPT_fdyncode, opts::OPT_fno_dyncode,
                             /*Default=*/false);
  Out.AllBlr = Args.hasArg(opts::OPT_fdyncode_all_blr);
  Out.SyscallInlining = Args.hasArg(opts::OPT_mdyncode_syscall) ||
                        Args.hasArg(opts::OPT_mdyncode_libsystem);
  Out.WindowsPEBImport = Args.hasArg(opts::OPT_mdyncode_win_peb_import);
  Out.Level = ExecutionLevel::User;
  if (auto *A = Args.getLastArg(opts::OPT_mdyncode_context_EQ)) {
    StringRef V = A->getValue();
    if (V == "kernel")
      Out.Level = ExecutionLevel::Kernel;
    else if (V != "user") {
      llvm::errs() << "neverc: error: " << A->getSpelling()
                   << " expects 'user' or 'kernel', got '" << V << "'\n";
      return false;
    }
  }
  if (auto *A = Args.getLastArg(opts::OPT_fdyncode_keep_obj_EQ))
    Out.KeepObjPath = A->getValue();
  if (auto *A = Args.getLastArg(opts::OPT_fdyncode_report_EQ))
    Out.ReportPath = A->getValue();
  if (auto *A = Args.getLastArg(opts::OPT_fdyncode_entry_EQ))
    Out.EntrySymbol = A->getValue();
  Out.BadByteProfile.clear();
  Out.BadBytes.clear();
  if (auto *A = Args.getLastArg(opts::OPT_fdyncode_bad_byte_profile_EQ))
    if (!applyBadByteProfile(A, Out))
      return false;
  if (auto *A = Args.getLastArg(opts::OPT_fdyncode_bad_bytes_EQ))
    if (!parseBadByteList(A, Out))
      return false;
  Out.BadByteRewrite = Args.hasFlag(opts::OPT_fdyncode_bad_byte_rewrite,
                                    opts::OPT_fno_dyncode_bad_byte_rewrite,
                                    /*Default=*/true);
  Out.HeapArena = Args.hasFlag(opts::OPT_fdyncode_heap_arena,
                               opts::OPT_fno_dyncode_heap_arena,
                               /*Default=*/true);
  Out.InlineAll = Args.hasFlag(opts::OPT_fdyncode_inline_all,
                               opts::OPT_fno_dyncode_inline_all,
                               /*Default=*/false);
  Out.Charset.clear();
  if (auto *A = Args.getLastArg(opts::OPT_fdyncode_charset_EQ)) {
    StringRef Name = StringRef(A->getValue()).trim();
    if (Name.empty()) {
      llvm::errs() << "neverc: error: " << A->getSpelling()
                   << " expects a non-empty charset name registered via "
                      "registerCharsetEncoder\n";
      return false;
    }
    Out.Charset = Name.str();
  }
  Out.MaxLength.reset();
  if (auto *A = Args.getLastArg(opts::OPT_fdyncode_max_length_EQ))
    if (!parseMaxLengthArg(A, Out))
      return false;
  Out.Align = 1;
  if (auto *A = Args.getLastArg(opts::OPT_fdyncode_align_EQ))
    if (!parseAlignArg(A, Out))
      return false;
  Out.PadByte.reset();
  if (auto *A = Args.getLastArg(opts::OPT_fdyncode_pad_EQ)) {
    if (!parsePadArg(A, Out))
      return false;
    if (Out.Align <= 1 && !Out.MaxLength) {
      llvm::errs() << "neverc: error: " << A->getSpelling()
                   << " requires at least one of -fdyncode-align= or "
                      "-fdyncode-max-length= to be set\n";
      return false;
    }
    if (llvm::is_contained(Out.BadBytes, *Out.PadByte)) {
      llvm::errs() << "neverc: error: " << A->getSpelling() << " value 0x"
                   << llvm::format_hex_no_prefix(*Out.PadByte, 2)
                   << " is also listed as a bad byte; pick a pad byte that the "
                      "bad-byte audit will accept\n";
      return false;
    }
  }
  if (auto *A = Args.getLastArg(opts::OPT_fdyncode_obfuscate_EQ))
    Out.ObfuscateSpec = A->getValue();
  if (auto *A = Args.getLastArg(opts::OPT_fdyncode_mir_obfuscate_EQ))
    Out.MirObfuscateSpec = A->getValue();
  else
    Out.MirObfuscateSpec = Out.ObfuscateSpec;
  Out.Verbose = Args.hasArg(opts::OPT_v);
  return true;
}

void applyImplicitDynCodeLowering(DynCodeOptions &Out) {
  if (!Out.Enabled)
    return;
  if (Out.Target.Level == ExecutionLevel::Kernel) {
    Out.SyscallInlining = false;
    Out.WindowsPEBImport = false;
    return;
  }
  switch (Out.Target.Syscall) {
  case SyscallABI::WindowsPEB:
    Out.WindowsPEBImport = true;
    Out.SyscallInlining = false;
    break;
  case SyscallABI::DarwinSvc80:
  case SyscallABI::DarwinSyscall:
  case SyscallABI::LinuxSvc0:
  case SyscallABI::LinuxSyscall:
    Out.SyscallInlining = true;
    Out.WindowsPEBImport = false;
    break;
  case SyscallABI::None:
    break;
  }
}

llvm::Triple resolveTriple(const llvm::opt::InputArgList &Args) {
  std::string Str;
  if (auto *A =
          Args.getLastArg(opts::OPT_target, opts::OPT_target_legacy_spelling))
    Str = A->getValue();
  else
    Str = llvm::sys::getDefaultTargetTriple();
  return llvm::Triple(llvm::Triple::normalize(Str));
}

// Append the dyncode codegen inject flags (driver-level flags that flow into
// the cc1 compile) to the existing driver argv in place.  Unlike the prototype
// this does not drop -o/-c/-S/-E or add a private temp object: the driver's
// DAG produces the intermediate object and the DynCodeJobAction consumes it.
void appendInjectArgs(DynCodeDriverSetup &Setup,
                      SmallVectorImpl<const char *> &Args) {
  auto &Pool = Setup.StringPool;
  for (const char *F : CommonInjectFlags)
    Args.push_back(saveCStr(Pool, F));
  for (const char *F : perTargetInjectFlags(Setup.Opts.Target))
    Args.push_back(saveCStr(Pool, F));
}

bool hasDriverDynCodeOptionToken(ArrayRef<const char *> Args) {
  for (const char *Arg : Args) {
    if (!Arg)
      continue;
    if (std::strcmp(Arg, "-fdyncode") == 0 ||
        std::strncmp(Arg, "-fdyncode-", 10) == 0 ||
        std::strcmp(Arg, "-fno-dyncode") == 0 ||
        std::strncmp(Arg, "-fno-dyncode-", 13) == 0 ||
        std::strncmp(Arg, "-mdyncode-", 10) == 0)
      return true;
  }
  return false;
}

} // namespace

int prepareDriverDynCode(SmallVectorImpl<const char *> &Args,
                         DynCodeDriverSetup &Setup) {
  if (!hasDriverDynCodeOptionToken(Args))
    return 0;

  const llvm::opt::OptTable &OptTbl = neverc::driver::getDriverOptTable();
  llvm::opt::Visibility VisMask(opts::NeverCOption);
  unsigned MissingIdx = 0, MissingCnt = 0;
  SmallVector<const char *, 256> PreParse;
  PreParse.reserve(Args.size());
  for (size_t I = 1, E = Args.size(); I < E; ++I)
    if (Args[I] != nullptr)
      PreParse.push_back(Args[I]);
  llvm::opt::InputArgList Parsed =
      OptTbl.ParseArgs(PreParse, MissingIdx, MissingCnt, VisMask);

  if (!collectOptions(Parsed, Setup.Opts))
    return 1;
  if (!Setup.Opts.Enabled)
    return 0;

  llvm::Triple TT = resolveTriple(Parsed);
  Setup.Opts.Target = describeTriple(TT, Setup.Opts.Level);
  Setup.Opts.TargetTriple = TT.str();
  if (auto *A = Parsed.getLastArg(opts::OPT_mcpu_EQ))
    Setup.Opts.CPU = A->getValue();
  if (Setup.Opts.Target.OS == DynCodeOS::Unknown ||
      Setup.Opts.Target.Arch == DynCodeArch::Unknown) {
    llvm::errs() << "neverc: error: -fdyncode does not support triple '"
                 << TT.str()
                 << "'. Supported: arm64-apple-macos, x86_64-apple-macos, "
                    "aarch64-linux-gnu, x86_64-linux-gnu, "
                    "aarch64-linux-android, x86_64-linux-android, "
                    "aarch64-pc-windows-msvc, x86_64-pc-windows-msvc.\n";
    return 1;
  }

  applyImplicitDynCodeLowering(Setup.Opts);

  for (const Incompat &IC : Incompats) {
    if (auto *A = Parsed.getLastArg(IC.ID)) {
      llvm::errs() << "neverc: error: " << A->getSpelling()
                   << " is not compatible with -fdyncode: " << IC.Reason
                   << "\n";
      return 1;
    }
  }

  appendInjectArgs(Setup, Args);

  // The frozen options now flow to the in-process cc1 as a
  // task-local DynCodeExecutionContext threaded through the driver and
  // DirectInvocationOpts, not through a mutable process-global singleton.
  Setup.Enabled = true;
  return 0;
}

} // namespace dyncode
} // namespace neverc
