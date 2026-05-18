#include "neverc/Shellcode/Pipeline/DriverIntegration.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Shellcode/Extractor/ShellcodeExtractor.h"
#include "neverc/Shellcode/Pipeline/Pipeline.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

using llvm::ArrayRef;
using llvm::SmallString;
using llvm::SmallVector;
using llvm::SmallVectorImpl;
using llvm::StringRef;

namespace opts = neverc::driver::options;

namespace neverc {
namespace shellcode {

namespace {

constexpr unsigned DriverOnlyOpts[] = {
    opts::OPT_fshellcode,
    opts::OPT_fno_shellcode,
    opts::OPT_fshellcode_all_blr,
    opts::OPT_mshellcode_syscall,
    opts::OPT_mshellcode_libsystem,
    opts::OPT_mshellcode_win_peb_import,
    opts::OPT_mshellcode_context_EQ,
    opts::OPT_fshellcode_keep_obj_EQ,
    opts::OPT_fshellcode_entry_EQ,
    opts::OPT_fshellcode_bad_bytes_EQ,
    opts::OPT_fshellcode_bad_byte_profile_EQ,
    opts::OPT_fshellcode_bad_byte_rewrite,
    opts::OPT_fno_shellcode_bad_byte_rewrite,
    opts::OPT_fshellcode_charset_EQ,
    opts::OPT_fshellcode_max_length_EQ,
    opts::OPT_fshellcode_align_EQ,
    opts::OPT_fshellcode_pad_EQ,
    opts::OPT_fshellcode_obfuscate_EQ,
    opts::OPT_fshellcode_mir_obfuscate_EQ,
};

struct Incompat {
  llvm::opt::OptSpecifier ID;
  const char *Reason;
};
const Incompat Incompats[] = {
    {opts::OPT_flto, "LTO emits bitcode, not an object file; "
                     "ShellcodeExtractor needs real object files"},
    {opts::OPT_flto_EQ, "LTO emits bitcode, not an object file; "
                        "ShellcodeExtractor needs real object files"},
    {opts::OPT_fsanitize_EQ,
     "sanitizers require a runtime library that shellcode cannot link to"},
    {opts::OPT_fstack_protector,
     "stack-protector emits references to __stack_chk_guard / "
     "__stack_chk_fail"},
    {opts::OPT_fstack_protector_all,
     "stack-protector emits references to __stack_chk_guard / "
     "__stack_chk_fail"},
    {opts::OPT_fstack_protector_strong,
     "stack-protector emits references to __stack_chk_guard / "
     "__stack_chk_fail"},
};

int hexDigitValue(char C) {
  if (C >= '0' && C <= '9')
    return C - '0';
  if (C >= 'a' && C <= 'f')
    return 10 + C - 'a';
  if (C >= 'A' && C <= 'F')
    return 10 + C - 'A';
  return -1;
}

void appendBadByte(ShellcodeOptions &Out, uint8_t Byte) {
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
    int N = hexDigitValue(C);
    if (N < 0)
      return false;
    Value = (Value << 4) | static_cast<unsigned>(N);
  }
  if (Value > 0xFF)
    return false;
  Byte = static_cast<uint8_t>(Value);
  return true;
}

bool parseBadByteList(const llvm::opt::Arg *A, ShellcodeOptions &Out) {
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
  unsigned Radix = 10;
  if (Spec.consume_front("0x") || Spec.consume_front("0X"))
    Radix = 16;
  if (Spec.empty())
    return false;
  uint64_t Value = 0;
  for (char C : Spec) {
    int Digit = -1;
    if (C >= '0' && C <= '9')
      Digit = C - '0';
    else if (Radix == 16 && C >= 'a' && C <= 'f')
      Digit = 10 + (C - 'a');
    else if (Radix == 16 && C >= 'A' && C <= 'F')
      Digit = 10 + (C - 'A');
    if (Digit < 0 || static_cast<unsigned>(Digit) >= Radix)
      return false;
    if (Value > (UINT64_MAX - static_cast<uint64_t>(Digit)) / Radix)
      return false; // Overflow.
    Value = Value * Radix + static_cast<uint64_t>(Digit);
  }
  Out = Value;
  return true;
}

bool parseMaxLengthArg(const llvm::opt::Arg *A, ShellcodeOptions &Out) {
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

bool parseAlignArg(const llvm::opt::Arg *A, ShellcodeOptions &Out) {
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

bool parsePadArg(const llvm::opt::Arg *A, ShellcodeOptions &Out) {
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

bool applyBadByteProfile(const llvm::opt::Arg *A, ShellcodeOptions &Out) {
  static constexpr uint8_t NullBytes[] = {0x00};
  static constexpr uint8_t HttpNewlineBytes[] = {0x00, 0x0A, 0x0D};
  static constexpr uint8_t WhitespaceBytes[] = {0x00, 0x09, 0x0A, 0x0B,
                                                0x0C, 0x0D, 0x20};
  static constexpr uint8_t AsciiControlBytes[] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
      0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
      0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x7F};

  static const BadByteProfile Profiles[] = {
      {"null", NullBytes},
      {"c-string", NullBytes},
      {"http-newline", HttpNewlineBytes},
      {"line", HttpNewlineBytes},
      {"whitespace", WhitespaceBytes},
      {"ascii-control", AsciiControlBytes},
  };

  StringRef Name = A->getValue();
  for (const BadByteProfile &P : Profiles) {
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

bool &passBuilderCallbackInstalled() {
  static bool Installed = false;
  return Installed;
}

void ensurePassBuilderCallbackInstalled() {
  if (passBuilderCallbackInstalled())
    return;
  passBuilderCallbackInstalled() = true;
  llvm::ListRegisterPassBuilderCallbacks.push_back([](llvm::PassBuilder &PB) {
    registerShellcodePasses(PB, getCurrentShellcodeOptions());
  });
}

const char *const CommonInjectFlags[] = {
    "-ffreestanding",       "-fno-builtin",
    "-fno-stack-protector", "-fomit-frame-pointer",
    "-fno-unwind-tables",   "-fno-asynchronous-unwind-tables",
    "-fno-jump-tables",     "-fno-vectorize",
    "-fno-slp-vectorize",   "-fno-lto",
    "-D__NEVERC_SHELLCODE__=1",
    "-fshellcode-mode",     "-fbuiltin-string",
    "-Oz",
};

SmallVector<const char *, 8> perTargetInjectFlags(const TargetDesc &T) {
  SmallVector<const char *, 8> Out;
  if (T.DriverInjectFlags) {
    for (const char *const *P = T.DriverInjectFlags; *P != nullptr; ++P)
      Out.push_back(*P);
  }
  if (T.Level == ExecutionLevel::Kernel) {
    Out.push_back("-D__NEVERC_SHELLCODE_KERNEL__=1");
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

bool isDroppedFromArgv(unsigned ID, bool ShellcodeEnabled) {
  if (llvm::is_contained(DriverOnlyOpts, ID))
    return true;
  switch (ID) {
  case opts::OPT_o:
  case opts::OPT_c:
  case opts::OPT_S:
  case opts::OPT_E:
    return true;
  default:
    return false;
  }
}

bool collectOptions(const llvm::opt::InputArgList &Args,
                    ShellcodeOptions &Out) {
  Out.Enabled = Args.hasFlag(opts::OPT_fshellcode, opts::OPT_fno_shellcode,
                             /*Default=*/false);
  Out.AllBlr = Args.hasArg(opts::OPT_fshellcode_all_blr);
  Out.SyscallInlining = Args.hasArg(opts::OPT_mshellcode_syscall) ||
                        Args.hasArg(opts::OPT_mshellcode_libsystem);
  Out.WindowsPEBImport = Args.hasArg(opts::OPT_mshellcode_win_peb_import);
  Out.Level = ExecutionLevel::User;
  if (auto *A = Args.getLastArg(opts::OPT_mshellcode_context_EQ)) {
    StringRef V = A->getValue();
    if (V == "kernel")
      Out.Level = ExecutionLevel::Kernel;
    else if (V != "user") {
      llvm::errs() << "neverc: error: " << A->getSpelling()
                   << " expects 'user' or 'kernel', got '" << V << "'\n";
      return false;
    }
  }
  if (auto *A = Args.getLastArg(opts::OPT_fshellcode_keep_obj_EQ))
    Out.KeepObjPath = A->getValue();
  if (auto *A = Args.getLastArg(opts::OPT_fshellcode_entry_EQ))
    Out.EntrySymbol = A->getValue();
  Out.BadByteProfile.clear();
  Out.BadBytes.clear();
  if (auto *A = Args.getLastArg(opts::OPT_fshellcode_bad_byte_profile_EQ))
    if (!applyBadByteProfile(A, Out))
      return false;
  if (auto *A = Args.getLastArg(opts::OPT_fshellcode_bad_bytes_EQ))
    if (!parseBadByteList(A, Out))
      return false;
  Out.BadByteRewrite = Args.hasFlag(opts::OPT_fshellcode_bad_byte_rewrite,
                                    opts::OPT_fno_shellcode_bad_byte_rewrite,
                                    /*Default=*/true);
  Out.Charset.clear();
  if (auto *A = Args.getLastArg(opts::OPT_fshellcode_charset_EQ)) {
    StringRef Name = StringRef(A->getValue()).trim();
    if (Name.empty()) {
      llvm::errs() << "neverc: error: " << A->getSpelling()
                   << " expects a non-empty charset name registered via "
                      "Plugin.h::registerCharsetEncoder\n";
      return false;
    }
    Out.Charset = Name.str();
  }
  Out.MaxLength.reset();
  if (auto *A = Args.getLastArg(opts::OPT_fshellcode_max_length_EQ))
    if (!parseMaxLengthArg(A, Out))
      return false;
  Out.Align = 1;
  if (auto *A = Args.getLastArg(opts::OPT_fshellcode_align_EQ))
    if (!parseAlignArg(A, Out))
      return false;
  Out.PadByte.reset();
  if (auto *A = Args.getLastArg(opts::OPT_fshellcode_pad_EQ)) {
    if (!parsePadArg(A, Out))
      return false;
    if (Out.Align <= 1 && !Out.MaxLength) {
      llvm::errs() << "neverc: error: " << A->getSpelling()
                   << " requires at least one of -fshellcode-align= or "
                      "-fshellcode-max-length= to be set\n";
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
  if (auto *A = Args.getLastArg(opts::OPT_fshellcode_obfuscate_EQ))
    Out.ObfuscateSpec = A->getValue();
  if (auto *A = Args.getLastArg(opts::OPT_fshellcode_mir_obfuscate_EQ))
    Out.MirObfuscateSpec = A->getValue();
  else
    Out.MirObfuscateSpec = Out.ObfuscateSpec;
  Out.Verbose = Args.hasArg(opts::OPT_v);
  return true;
}

void applyImplicitShellcodeLowering(ShellcodeOptions &Out) {
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

void rewriteArgs(const llvm::opt::InputArgList &Parsed, StringRef Argv0,
                 const TargetDesc &Target, CompilationState &State,
                 SmallVectorImpl<const char *> &Out) {
  auto &Pool = State.StringPool;
  SmallVector<const char *, 256> NewArgs;
  NewArgs.push_back(saveCStr(Pool, Argv0));

  llvm::opt::ArgStringList Rendered;
  for (const llvm::opt::Arg *A : Parsed) {
    if (isDroppedFromArgv(A->getOption().getID(), State.Opts.Enabled))
      continue;
    Rendered.clear();
    A->render(Parsed, Rendered);
    for (const char *S : Rendered)
      NewArgs.push_back(saveCStr(Pool, S));
  }

  for (const char *F : CommonInjectFlags)
    NewArgs.push_back(saveCStr(Pool, F));
  for (const char *F : perTargetInjectFlags(Target))
    NewArgs.push_back(saveCStr(Pool, F));
  NewArgs.push_back(saveCStr(Pool, "-c"));
  NewArgs.push_back(saveCStr(Pool, "-o"));
  NewArgs.push_back(saveCStr(Pool, State.TmpObj));

  Out = std::move(NewArgs);
}

} // namespace

int configureCompilation(SmallVectorImpl<const char *> &Args,
                         CompilationState &State) {
  const llvm::opt::OptTable &OptTbl = neverc::driver::getDriverOptTable();
  llvm::opt::Visibility VisMask(opts::NeverCOption);
  unsigned MissingIdx = 0, MissingCnt = 0;
  SmallVector<const char *, 256> PreParse;
  PreParse.reserve(Args.size());
  StringRef Argv0 = Args.empty() ? StringRef() : StringRef(Args[0]);
  for (size_t I = 1, E = Args.size(); I < E; ++I)
    if (Args[I] != nullptr)
      PreParse.push_back(Args[I]);
  llvm::opt::InputArgList Parsed =
      OptTbl.ParseArgs(PreParse, MissingIdx, MissingCnt, VisMask);

  State.PrintOnly = Parsed.hasArg(opts::OPT__HASH_HASH_HASH) ||
                    Parsed.hasArg(opts::OPT_fdriver_only) ||
                    Parsed.hasArg(opts::OPT_ccc_print_phases) ||
                    Parsed.hasArg(opts::OPT_ccc_print_bindings);

  if (!collectOptions(Parsed, State.Opts))
    return 1;
  if (!State.Opts.Enabled) {
    registerShellcodeMachinePasses(State.Opts);
    return 0;
  }

  llvm::Triple TT = resolveTriple(Parsed);
  State.Opts.Target = describeTriple(TT, State.Opts.Level);
  if (State.Opts.Target.OS == ShellcodeOS::Unknown ||
      State.Opts.Target.Arch == ShellcodeArch::Unknown) {
    llvm::errs() << "neverc: error: -fshellcode does not support triple '"
                 << TT.str()
                 << "'. Supported: arm64-apple-macos, x86_64-apple-macos, "
                    "aarch64-linux-gnu, x86_64-linux-gnu, "
                    "aarch64-linux-android, x86_64-linux-android, "
                    "aarch64-pc-windows-msvc, x86_64-pc-windows-msvc.\n";
    return 1;
  }

  applyImplicitShellcodeLowering(State.Opts);

  for (const Incompat &IC : Incompats) {
    if (auto *A = Parsed.getLastArg(IC.ID)) {
      llvm::errs() << "neverc: error: " << A->getSpelling()
                   << " is not compatible with -fshellcode: " << IC.Reason
                   << "\n";
      return 1;
    }
  }

  State.OutputBin = "a.bin";
  if (auto *A = Parsed.getLastArg(opts::OPT_o))
    State.OutputBin = A->getValue();

  const char *Ext =
      State.Opts.Target.Format == ObjectFormat::COFF ? "obj" : "o";
  SmallString<128> TmpPath;
  if (auto EC = llvm::sys::fs::createTemporaryFile("shellcode", Ext, TmpPath)) {
    llvm::errs() << "neverc: error: cannot create temp file: " << EC.message()
                 << "\n";
    return 1;
  }
  State.TmpObj = std::string(TmpPath);
  llvm::sys::RemoveFileOnSignal(State.TmpObj);

  rewriteArgs(Parsed, Argv0, State.Opts.Target, State, Args);

  ensurePassBuilderCallbackInstalled();
  registerShellcodeMachinePasses(State.Opts);

  return 0;
}

int finalizeCompilation(const CompilationState &State, int CompilationRes) {
  int Res = CompilationRes;
  auto CleanupTmp = [&]() {
    if (State.TmpObj.empty())
      return;
    llvm::sys::fs::remove(State.TmpObj);
    llvm::sys::DontRemoveFileOnSignal(State.TmpObj);
  };
  if (State.PrintOnly) {
    CleanupTmp();
    return Res;
  }
  if (Res == 0) {
    if (!State.Opts.KeepObjPath.empty()) {
      if (std::error_code EC =
              llvm::sys::fs::copy_file(State.TmpObj, State.Opts.KeepObjPath)) {
        llvm::errs() << "neverc: warning: cannot save shellcode object to '"
                     << State.Opts.KeepObjPath << "': " << EC.message() << "\n";
      }
    }
    Res = extractShellcode(State.TmpObj, State.OutputBin, State.Opts);
  }
  CleanupTmp();
  return Res;
}

} // namespace shellcode
} // namespace neverc
