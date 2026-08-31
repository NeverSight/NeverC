#include "NeverCTestFixture.h"

#include "llvm/Object/COFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"

#include <algorithm>
#include <set>

namespace {

// `-###` renders quoted arguments and escapes each separator, so a Windows
// path comes back doubled. Compare paths on a separator-insensitive form.
std::string collapseSeparators(std::string Text) {
  std::replace(Text.begin(), Text.end(), '\\', '/');
  Text.erase(std::unique(Text.begin(), Text.end(),
                         [](char Left, char Right) {
                           return Left == '/' && Right == '/';
                         }),
             Text.end());
  return Text;
}

struct ReleaseMetadata {
  bool HasDebugSections = false;
  bool HasPrivateSymbol = false;
  bool HasPublicSymbol = false;
  bool HasPrivateSymbolNameBytes = false;
  bool HasPublicSymbolNameBytes = false;
  bool HasReleaseSymbolNameBytes = false;
};

struct ReleaseTargetCase {
  const char *Name;
  const char *TraceFormat;
  const char *Triple;
  const char *ObjectSuffix;
  const char *ImageSuffix;
  std::vector<std::string> ExecutableLinkerArgs;
  const char *SharedImageSuffix;
  std::vector<std::string> SharedLinkerArgs;
};

const std::vector<ReleaseTargetCase> &releaseTargets() {
  static const std::vector<ReleaseTargetCase> Targets = {
      {"elf",
       "ELF",
       "x86_64-linux-gnu",
       ".o",
       ".elf",
       {"-Xlinker", "--entry=main"},
       ".so",
       {}},
      {"macho",
       "Mach-O",
       "arm64-apple-macos",
       ".o",
       ".macho",
       {},
       ".dylib",
       {}},
      {"coff",
       "COFF",
       "x86_64-pc-windows-msvc",
       ".obj",
       ".exe",
       {"-Xlinker", "--entry=main", "-Xlinker", "--subsystem=console"},
       ".dll",
       {"-Xlinker", "--noentry"}},
  };
  return Targets;
}

llvm::Expected<ReleaseMetadata> inspectReleaseMetadata(llvm::StringRef Bytes) {
  auto Object = llvm::object::ObjectFile::createObjectFile(
      llvm::MemoryBufferRef(Bytes, "neverc-strip-test"));
  if (!Object)
    return Object.takeError();

  ReleaseMetadata Metadata;
  Metadata.HasPrivateSymbolNameBytes =
      Bytes.contains("neverc_private_release_symbol");
  Metadata.HasPublicSymbolNameBytes =
      Bytes.contains("neverc_public_release_symbol") ||
      Bytes.contains("neverc_public_release_api");
  Metadata.HasReleaseSymbolNameBytes =
      Metadata.HasPrivateSymbolNameBytes || Metadata.HasPublicSymbolNameBytes;
  for (const llvm::object::SectionRef &Section : (*Object)->sections()) {
    llvm::Expected<llvm::StringRef> Name = Section.getName();
    if (!Name)
      return Name.takeError();
    Metadata.HasDebugSections |=
        Name->starts_with(".debug") || Name->starts_with(".zdebug") ||
        Name->starts_with("__debug") || Name->starts_with("__zdebug");
  }

  for (const llvm::object::SymbolRef &Symbol : (*Object)->symbols()) {
    llvm::Expected<llvm::StringRef> Name = Symbol.getName();
    if (!Name)
      return Name.takeError();
    Metadata.HasPrivateSymbol |=
        Name->contains("neverc_private_release_symbol");
    Metadata.HasPublicSymbol |=
        Name->contains("neverc_public_release_symbol") ||
        Name->contains("neverc_public_release_api");
  }
  return Metadata;
}

} // namespace

class DriverTest : public NeverCTest {
protected:
  ReleaseMetadata inspectMetadata(const fs::path &Path) const {
    auto Metadata = inspectReleaseMetadata(readFile(Path));
    if (!Metadata) {
      const auto Message = llvm::toString(Metadata.takeError());
      ADD_FAILURE() << std::string(Message.begin(), Message.end());
      return {};
    }
    return *Metadata;
  }
};

TEST_F(DriverTest, PrintArgumentsEchoesTheDriverInvocation) {
  auto Result = ncc({"-fprint-arguments", "-fsyntax-only",
                     (testDir() / "test_basic.c").string()});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(Result.out.find("compiler arguments:\n"), std::string::npos)
      << Result.out;
  EXPECT_NE(Result.out.find("\"-fprint-arguments\",\n"), std::string::npos)
      << Result.out;
}

TEST_F(DriverTest, LinkTimeTraceUsesCanonicalCrossFormatPhases) {
  const auto Source = tmpFile("link-phase-trace.c");
  writeFile(Source, "int main(void) { return 0; }\n");

  for (const ReleaseTargetCase &Target : releaseTargets()) {
    SCOPED_TRACE(Target.Name);
    const std::string TargetArg = std::string("--target=") + Target.Triple;
    const auto Object = tmpFile(std::string("link-phase-trace-") + Target.Name +
                                Target.ObjectSuffix);
    const auto Image = tmpFile(std::string("link-phase-trace-") + Target.Name +
                               Target.ImageSuffix);
    const auto UntracedImage = tmpFile(std::string("link-phase-no-trace-") +
                                       Target.Name + Target.ImageSuffix);

    auto Compile =
        ncc({TargetArg, "-fno-lto", "-fno-stack-protector", "-nostdlib", "-c",
             Source.string(), "-o", Object.string()});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

    std::vector<std::string> UntracedLinkArgs = {
        TargetArg,       "-fno-lto", "-nostdlib",
        Object.string(), "-o",       UntracedImage.string()};
    UntracedLinkArgs.insert(UntracedLinkArgs.end(),
                            Target.ExecutableLinkerArgs.begin(),
                            Target.ExecutableLinkerArgs.end());
    auto UntracedLink = ncc(UntracedLinkArgs);
    ASSERT_EQ(UntracedLink.exitCode, 0) << UntracedLink.err;
    EXPECT_FALSE(fs::exists(UntracedImage.string() + ".time-trace"));

    std::vector<std::string> LinkArgs = {
        TargetArg,
        "-fno-lto",
        "-nostdlib",
        "-ftime-trace",
        "-ftime-trace-granularity=1",
        Object.string(),
        "-o",
        Image.string(),
    };
    LinkArgs.insert(LinkArgs.end(), Target.ExecutableLinkerArgs.begin(),
                    Target.ExecutableLinkerArgs.end());
    auto Link = ncc(LinkArgs);
    ASSERT_EQ(Link.exitCode, 0) << Link.err;

    const fs::path TracePath(Image.string() + ".time-trace");
    ASSERT_TRUE(fs::is_regular_file(TracePath));
    auto Parsed = llvm::json::parse(readFile(TracePath));
    ASSERT_TRUE(static_cast<bool>(Parsed));
    const llvm::json::Object *Root = Parsed->getAsObject();
    ASSERT_NE(Root, nullptr);
    const llvm::json::Array *Events = Root->getArray("traceEvents");
    ASSERT_NE(Events, nullptr);

    const std::string Format = Target.TraceFormat;
    bool SawDispatch = false;
    bool SawBackend = false;
    for (const llvm::json::Value &Value : *Events) {
      const llvm::json::Object *Event = Value.getAsObject();
      if (!Event || Event->getString("ph") != "X")
        continue;
      const llvm::StringRef Name = Event->getString("name");
      const llvm::json::Object *Args = Event->getObject("args");
      const llvm::StringRef Detail =
          Args ? Args->getString("detail") : llvm::StringRef();
      const bool IsThisFormat = Detail == Format;
      SawDispatch |= Name == "neverc.link.dispatch" && IsThisFormat;
      SawBackend |= Name == "neverc.link.backend" && IsThisFormat;
    }

    EXPECT_TRUE(SawDispatch)
        << "missing canonical dispatch phase for " << Format;
    EXPECT_TRUE(SawBackend) << "missing canonical backend phase for " << Format;
  }
}

TEST_F(DriverTest, FailedLinkTimeTraceIsWrittenAcrossFormats) {
  const auto Source = tmpFile("failed-link-phase-trace.c");
  writeFile(Source,
            "extern int neverc_phase_trace_missing(void);\n"
            "int main(void) { return neverc_phase_trace_missing(); }\n");

  for (const ReleaseTargetCase &Target : releaseTargets()) {
    SCOPED_TRACE(Target.Name);
    const std::string TargetArg = std::string("--target=") + Target.Triple;
    const auto Object = tmpFile(std::string("failed-link-phase-trace-") +
                                Target.Name + Target.ObjectSuffix);
    const auto Image = tmpFile(std::string("failed-link-phase-trace-") +
                               Target.Name + Target.ImageSuffix);
    auto Compile =
        ncc({TargetArg, "-fno-lto", "-fno-stack-protector", "-nostdlib", "-c",
             Source.string(), "-o", Object.string()});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

    std::vector<std::string> LinkArgs = {
        TargetArg,
        "-fno-lto",
        "-nostdlib",
        "-ftime-trace",
        "-ftime-trace-granularity=1",
        Object.string(),
        "-o",
        Image.string(),
    };
    LinkArgs.insert(LinkArgs.end(), Target.ExecutableLinkerArgs.begin(),
                    Target.ExecutableLinkerArgs.end());
    auto Link = ncc(LinkArgs);
    EXPECT_NE(Link.exitCode, 0);
    EXPECT_FALSE(fs::exists(Image));

    const fs::path TracePath(Image.string() + ".time-trace");
    ASSERT_TRUE(fs::is_regular_file(TracePath));
    auto Parsed = llvm::json::parse(readFile(TracePath));
    ASSERT_TRUE(static_cast<bool>(Parsed));
    const llvm::json::Object *Root = Parsed->getAsObject();
    ASSERT_NE(Root, nullptr);
    const llvm::json::Array *Events = Root->getArray("traceEvents");
    ASSERT_NE(Events, nullptr);

    const std::string Format = Target.TraceFormat;
    bool SawDispatch = false;
    bool SawBackend = false;
    for (const llvm::json::Value &Value : *Events) {
      const llvm::json::Object *Event = Value.getAsObject();
      if (!Event || Event->getString("ph") != "X")
        continue;
      const llvm::json::Object *Args = Event->getObject("args");
      if (!Args || Args->getString("detail") != Format)
        continue;
      SawDispatch |= Event->getString("name") == "neverc.link.dispatch";
      SawBackend |= Event->getString("name") == "neverc.link.backend";
    }
    EXPECT_TRUE(SawDispatch) << "missing failed dispatch phase for " << Format;
    EXPECT_TRUE(SawBackend) << "missing failed backend phase for " << Format;
  }
}

TEST_F(DriverTest, MachOLinkTimeTraceOwnsInlineMapProfiler) {
  const auto Source = tmpFile("macho-inline-map-time-trace.c");
  const auto Object = tmpFile("macho-inline-map-time-trace.o");
  const auto Image = tmpFile("macho-inline-map-time-trace.macho");
  const auto Map = tmpFile("macho-inline-map-time-trace.map");
  const fs::path Trace(Image.string() + ".time-trace");
  writeFile(Source, "int main(void) { return 0; }\n");

  auto Compile =
      ncc({"--target=arm64-apple-macos", "-fno-lto", "-fno-stack-protector",
           "-nostdlib", "-c", Source.string(), "-o", Object.string()});
  ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

  auto Link = ncc({"--target=arm64-apple-macos", "-fno-lto", "-nostdlib",
                   "-ftime-trace", "-ftime-trace-granularity=1",
                   "-flinker-map=" + Map.string(), Object.string(), "-o",
                   Image.string()});
  ASSERT_EQ(Link.exitCode, 0) << Link.err;
  ASSERT_TRUE(fs::is_regular_file(Image));
  ASSERT_TRUE(fs::is_regular_file(Map));
  ASSERT_TRUE(fs::is_regular_file(Trace));
  EXPECT_GT(fileSize(Image), 0U);
  EXPECT_GT(fileSize(Map), 0U);
  ASSERT_GT(fileSize(Trace), 0U);
  auto Parsed = llvm::json::parse(readFile(Trace));
  ASSERT_TRUE(static_cast<bool>(Parsed));
  ASSERT_NE(Parsed->getAsObject(), nullptr);
}

TEST_F(DriverTest, PreprocessedOutputRoundTripsAcrossIncludeCallbacks) {
  const auto FirstHeader = tmpFile("deferred-first.h");
  const auto SecondHeader = tmpFile("deferred-second.h");
  const auto ThirdHeader = tmpFile("deferred-third.h");
  const auto Source = tmpFile("deferred-main.c");
  const auto Preprocessed = tmpFile("deferred-main.i");
  const auto Object = tmpFile("deferred-main.o");

  // Builtin macro expansions interrupt the raw-token fast path.  The tokens
  // that resume afterwards used to remain buffered while Lex() synchronously
  // emitted ExitFile/EnterFile line markers, moving declaration suffixes into
  // the following include and making -save-temps output unparsable.
  writeFile(FirstHeader,
            "#ifndef DEFERRED_FIRST_H\n"
            "#define DEFERRED_FIRST_H\n"
            "typedef __SIZE_TYPE__ deferred_size_t;\n"
            "#endif\n");
  writeFile(SecondHeader,
            "#ifndef DEFERRED_SECOND_H\n"
            "#define DEFERRED_SECOND_H\n"
            "typedef typeof(nullptr) deferred_null_t;\n"
            "#endif\n");
  writeFile(ThirdHeader,
            "#ifndef DEFERRED_THIRD_H\n"
            "#define DEFERRED_THIRD_H\n"
            "extern int deferred_external;\n"
            "#endif\n");
  writeFile(Source,
            "#include \"deferred-first.h\"\n"
            "#include \"deferred-second.h\"\n"
            "#include \"deferred-third.h\"\n"
            "deferred_size_t deferred_value;\n"
            "deferred_null_t deferred_pointer;\n"
            "int main(void) { return 0; }\n");

  auto Preprocess =
      ncc({"-std=c23", "-E", "-I", tmp().string(), Source.string(), "-o",
           Preprocessed.string()});
  ASSERT_EQ(Preprocess.exitCode, 0) << Preprocess.err;

  const std::string Text = readFile(Preprocessed);
  EXPECT_EQ(Text.find(";#"), std::string::npos) << Text;

  auto Reparse = ncc({"-std=c23", "-fsyntax-only", "-x", "cpp-output",
                      Preprocessed.string()});
  ASSERT_EQ(Reparse.exitCode, 0) << Reparse.err;

  auto SaveTemps = ncc({"-std=c23", "-fno-lto", "-save-temps=obj", "-I",
                        tmp().string(), "-c", Source.string(), "-o",
                        Object.string()});
  ASSERT_EQ(SaveTemps.exitCode, 0) << SaveTemps.err;
  EXPECT_TRUE(fs::is_regular_file(Object));

  const auto Image = tmpFile(isWindows() ? "deferred-main.exe"
                                         : "deferred-main");
  auto AutoLTOSaveTemps =
      ncc({"-std=c23", "-save-temps=obj", "-I", tmp().string(),
           Source.string(), "-o", Image.string()});
  ASSERT_EQ(AutoLTOSaveTemps.exitCode, 0) << AutoLTOSaveTemps.err;
  EXPECT_TRUE(fs::is_regular_file(Image));
}

TEST_F(DriverTest, PluginCapabilityQueryPrintsJsonWithoutCompiling) {
  for (const char *Option : {"--print-plugin-capabilities",
                             "--print-plugin-capabilities=json"}) {
    SCOPED_TRACE(Option);
    auto Result = ncc({Option});

    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    EXPECT_NE(Result.out.find("\"abi\""), std::string::npos) << Result.out;
    EXPECT_NE(Result.out.find("\"modules\""), std::string::npos) << Result.out;
    EXPECT_TRUE(Result.err.empty()) << Result.err;
  }
}

TEST_F(DriverTest, PluginCapabilityQueryRejectsUnsupportedFormat) {
  auto Result = ncc({"--print-plugin-capabilities=yaml"});

  EXPECT_EQ(Result.exitCode, 1) << Result.out << Result.err;
  EXPECT_NE(Result.err.find("only 'json' is supported"), std::string::npos)
      << Result.err;
}

TEST_F(DriverTest, TestSignCertificateQueryWritesRedirectedDer) {
  auto Result = ncc({"--print-test-sign-cert"});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  ASSERT_GE(Result.out.size(), 2u) << "certificate output was truncated";
  EXPECT_EQ(static_cast<unsigned char>(Result.out[0]), 0x30u)
      << "certificate must start with an ASN.1 SEQUENCE";
}

TEST_F(DriverTest, KernelStyleC) {
  syntaxCheck("kernel_style_c_test",
              (testDir() / "kernel/kernel_style_c_test.c").string(), "gnu11",
              "x86_64-linux-gnu");
}

TEST_F(DriverTest, CrossAArch64) {
  auto sysroot = neverc().parent_path().parent_path() / "runtime/linux/arm64";
  if (!fs::is_directory(sysroot))
    GTEST_SKIP() << "AArch64 Linux cross-compile needs bundled sysroot";
  syntaxCheck("test_cross_aarch64",
              (testDir() / "platform/test_cross_aarch64.c").string(), "c11",
              "aarch64-linux-gnu", "-fneverc-types");
}

TEST_F(DriverTest, AArch64FixedFP16BuiltinsEmitInstructions) {
  const auto Source = tmpFile("aarch64-fixed-fp16-builtins.c");
  const auto Assembly = tmpFile("aarch64-fixed-fp16-builtins.s");
  writeFile(Source, R"(
unsigned short scvtf_w(unsigned long long x) {
  return __builtin_arm_scvtf_fixed(x, 16, 0);
}
unsigned short scvtf_x(unsigned long long x) {
  return __builtin_arm_scvtf_fixed(x, 64, 1);
}
unsigned short ucvtf_w(unsigned long long x) {
  return __builtin_arm_ucvtf_fixed(x, 16, 0);
}
unsigned short ucvtf_x(unsigned long long x) {
  return __builtin_arm_ucvtf_fixed(x, 64, 1);
}
unsigned long long fcvtzs_w(unsigned short x) {
  return __builtin_arm_fcvtzs_fixed(x, 16, 0);
}
unsigned long long fcvtzs_x(unsigned short x) {
  return __builtin_arm_fcvtzs_fixed(x, 64, 1);
}
unsigned long long fcvtzu_w(unsigned short x) {
  return __builtin_arm_fcvtzu_fixed(x, 16, 0);
}
unsigned long long fcvtzu_x(unsigned short x) {
  return __builtin_arm_fcvtzu_fixed(x, 64, 1);
}
)");

  auto Result = ncc({"-target", "aarch64-linux-gnu",
                     "-march=armv8.2-a+fp16", "-ffreestanding", "-O2", "-S",
                     Source.string(), "-o", Assembly.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string Text = readFile(Assembly);
  for (const char *Instruction : {
           "scvtf\th0, w0, #16", "scvtf\th0, x0, #64",
           "ucvtf\th0, w0, #16", "ucvtf\th0, x0, #64",
           "fcvtzs\tw8, h0, #16", "fcvtzs\tx0, h0, #64",
           "fcvtzu\tw8, h0, #16", "fcvtzu\tx0, h0, #64",
       })
    EXPECT_NE(Text.find(Instruction), std::string::npos) << Instruction << '\n'
                                                         << Text;
}

TEST_F(DriverTest, AArch64SubgBuiltinEmitsInstruction) {
  const auto Source = tmpFile("aarch64-subg-builtin.c");
  const auto Assembly = tmpFile("aarch64-subg-builtin.s");
  writeFile(Source, R"(
void *subg(void *pointer) {
  return __builtin_arm_subg(pointer, 112, 9);
}
)");

  auto Result = ncc({"-target", "aarch64-linux-gnu",
                     "-march=armv8.5-a+memtag", "-ffreestanding", "-O2", "-S",
                     Source.string(), "-o", Assembly.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string Text = readFile(Assembly);
  EXPECT_NE(Text.find("subg\t"), std::string::npos) << Text;
  EXPECT_NE(Text.find("#112, #9"), std::string::npos) << Text;
}

TEST_F(DriverTest, AArch64PacgaBuiltinEmitsInstruction) {
  const auto Source = tmpFile("aarch64-pacga-builtin.c");
  const auto Assembly = tmpFile("aarch64-pacga-builtin.s");
  writeFile(Source, R"(
unsigned long long pacga(unsigned long long value,
                         unsigned long long discriminator) {
  return __builtin_arm_pacga(value, discriminator);
}
)");

  auto Result = ncc({"-target", "aarch64-linux-gnu",
                     "-march=armv8.3-a+pauth", "-ffreestanding", "-O2", "-S",
                     Source.string(), "-o", Assembly.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string Text = readFile(Assembly);
  EXPECT_NE(Text.find("pacga\tx0, x0, x1"), std::string::npos) << Text;
}

TEST_F(DriverTest, AArch64WspWriteBuiltinEmitsInstruction) {
  const auto Source = tmpFile("aarch64-wsp-write-builtin.c");
  const auto Assembly = tmpFile("aarch64-wsp-write-builtin.s");
  writeFile(Source, R"(
unsigned long long write_wsp(unsigned value) {
  return __builtin_arm_wsp_write(value);
}
unsigned long long zero_extend_wsp(void) {
  return __builtin_arm_wsp_zero_extend();
}
)");

  auto Result = ncc({"-target", "aarch64-linux-gnu", "-ffreestanding", "-O2",
                     "-S", Source.string(), "-o", Assembly.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string Text = readFile(Assembly);
  EXPECT_NE(Text.find("mov\twsp, w"), std::string::npos) << Text;
  EXPECT_NE(Text.find(", wsp"), std::string::npos) << Text;
}

TEST_F(DriverTest, AArch64LseRmwBuiltinsEmitInstructions) {
  const auto Source = tmpFile("aarch64-lse-rmw-builtins.c");
  const auto Assembly = tmpFile("aarch64-lse-rmw-builtins.s");
  writeFile(Source, R"(
unsigned long long ldclral(unsigned long long value, void *address) {
  return __builtin_arm_ldclr(value, address, 8, __ATOMIC_ACQ_REL);
}
unsigned long long ldeorb(unsigned long long value, void *address) {
  return __builtin_arm_ldeor(value, address, 1, __ATOMIC_RELAXED);
}
unsigned long long ldseth(unsigned long long value, void *address) {
  return __builtin_arm_ldset(value, address, 2, __ATOMIC_CONSUME);
}
unsigned long long ldsmaxa(unsigned long long value, void *address) {
  return __builtin_arm_ldsmax(value, address, 4, __ATOMIC_ACQUIRE);
}
unsigned long long ldsminl(unsigned long long value, void *address) {
  return __builtin_arm_ldsmin(value, address, 8, __ATOMIC_RELEASE);
}
unsigned long long ldumaxalb(unsigned long long value, void *address) {
  return __builtin_arm_ldumax(value, address, 1, __ATOMIC_ACQ_REL);
}
unsigned long long lduminseqh(unsigned long long value, void *address) {
  return __builtin_arm_ldumin(value, address, 2, __ATOMIC_SEQ_CST);
}
)");

  auto Result = ncc({"-target", "aarch64-linux-gnu", "-march=armv8.1-a+lse",
                     "-ffreestanding", "-O2", "-S", Source.string(), "-o",
                     Assembly.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string Text = readFile(Assembly);
  EXPECT_NE(Text.find("ldclral\tx0, x0, [x1]"), std::string::npos) << Text;
  EXPECT_NE(Text.find("ldeorb\t"), std::string::npos) << Text;
  EXPECT_NE(Text.find("ldsetah\t"), std::string::npos) << Text;
  EXPECT_NE(Text.find("ldsmaxa\t"), std::string::npos) << Text;
  EXPECT_NE(Text.find("ldsminl\t"), std::string::npos) << Text;
  EXPECT_NE(Text.find("ldumaxalb\t"), std::string::npos) << Text;
  EXPECT_NE(Text.find("lduminalh\t"), std::string::npos) << Text;
  EXPECT_EQ(Text.find("\tmvn\t"), std::string::npos) << Text;
}

TEST_F(DriverTest, AArch64LseRmwBuiltinsRejectInvalidContracts) {
  const struct {
    const char *Name;
    const char *Call;
    const char *Parameters;
    const char *Error;
    bool EnableLse;
  } Cases[] = {
      {"dynamic_width", "__builtin_arm_ldclr(v, p, width, 0)",
       ", unsigned width", "must be a constant integer", true},
      {"dynamic_order", "__builtin_arm_ldclr(v, p, 8, order)",
       ", unsigned order", "must be a constant integer", true},
      {"width_3", "__builtin_arm_ldclr(v, p, 3, 0)", "",
       "outside the valid range [1, 2, 4, 8]", true},
      {"width_16", "__builtin_arm_ldclr(v, p, 16, 0)", "",
       "outside the valid range [1, 2, 4, 8]", true},
      {"order_6", "__builtin_arm_ldclr(v, p, 8, 6)", "",
       "outside the valid range [0, 5]", true},
      {"missing_lse", "__builtin_arm_ldumin(v, p, 2, 5)", "",
       "needs target feature lse", false},
  };

  for (const auto &Case : Cases) {
    SCOPED_TRACE(Case.Name);
    const auto Source = tmpFile(std::string(Case.Name) + ".c");
    const auto Assembly = tmpFile(std::string(Case.Name) + ".s");
    writeFile(Source, "unsigned long long f(unsigned long long v, void *p" +
                          std::string(Case.Parameters) + ") { return " +
                          Case.Call + "; }\n");
    std::vector<std::string> Args = {"-target", "aarch64-linux-gnu"};
    if (Case.EnableLse)
      Args.insert(Args.end(), {"-march=armv8.1-a+lse"});
    Args.insert(Args.end(), {"-ffreestanding", "-S", Source.string(), "-o",
                             Assembly.string()});
    auto Result = ncc(Args);
    EXPECT_NE(Result.exitCode, 0) << Case.Name;
    EXPECT_NE(Result.err.find(Case.Error), std::string::npos) << Result.err;
  }
}

TEST_F(DriverTest, AArch64SVECountBuiltinEmitsInstruction) {
  const auto Source = tmpFile("aarch64-sve-count-builtin.c");
  const auto Assembly = tmpFile("aarch64-sve-count-builtin.s");
  writeFile(Source, R"(
#include <arm_sve.h>
unsigned long long count_bytes(void) {
  return svcntb();
}
)");

  auto Result = ncc({"-target", "aarch64-linux-gnu",
                     "-march=armv8.2-a+sve", "-ffreestanding", "-O2", "-S",
                     Source.string(), "-o", Assembly.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string Text = readFile(Assembly);
  EXPECT_NE(Text.find("rdvl\tx0, #1"), std::string::npos) << Text;
}

TEST_F(DriverTest, CrossAppleIOS) {
  // test_basic.c pulls in <stdio.h>; resolving libc headers for an Apple
  // target requires the host Apple SDK (-isysroot), which only exists on a
  // Darwin host.  On Windows/Linux there is no Apple SDK to search.
  if (!isDarwin())
    GTEST_SKIP() << "iOS cross-compile needs host Apple SDK headers";
  syntaxCheck("test_cross_apple_ios",
              (testDir() / "test_basic.c").string(), "c11",
              "aarch64-apple-ios");
}

TEST_F(DriverTest, DarwinX64UsesUniversalMacOSSysroot) {
  const auto UniversalSysroot =
      neverc().parent_path().parent_path() / "runtime/macos/arm64";
  if (!fs::is_directory(UniversalSysroot))
    GTEST_SKIP() << "Darwin driver test needs bundled universal macOS SDK";

  const auto Source = tmpFile("darwin-x64-universal-sysroot.c");
  writeFile(Source, "int neverc_darwin_x64_sysroot(void) { return 0; }\n");

  auto Result = ncc({"-###", "-c", "--target=x86_64-apple-macosx11.0",
                     Source.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string Commands = Result.err + Result.out;
  const std::string NormalizedCommands = collapseSeparators(Commands);
  EXPECT_NE(NormalizedCommands.find(collapseSeparators(UniversalSysroot.string())),
            std::string::npos)
      << "x86_64 Darwin must use the bundled universal macOS SDK\n"
      << Commands;

  const auto LegacyX64Sysroot =
      neverc().parent_path().parent_path() / "runtime/macos/x64";
  EXPECT_EQ(NormalizedCommands.find(collapseSeparators(LegacyX64Sysroot.string())),
            std::string::npos)
      << "x86_64 Darwin must not use the removed x64-only macOS SDK\n"
      << Commands;
}

TEST_F(DriverTest, DarwinWarningPolicyUsesStructuredLinkerConfig) {
  const auto Source = tmpFile("darwin-warning-policy.c");
  const auto Object = tmpFile("darwin-warning-policy.o");
  const auto Sysroot = tmpFile("darwin-warning-policy-sysroot");
  fs::create_directories(Sysroot / "usr/lib");
  fs::create_directories(Sysroot / "usr/local/lib");
  writeFile(Source, "int main(void) { return 0; }\n");

  const CmdResult Compile = ncc(
      {"--no-default-config", "--target=arm64-apple-macosx13.0",
       "--sysroot=" + Sysroot.string(), "-O0", "-fno-lto", "-nostdlib",
       "-fno-stack-protector", "-c", Source.string(), "-o", Object.string()});
  ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

  auto LinkWithPolicies = [&](const std::vector<std::string> &Policies,
                              const fs::path &Output) {
    std::vector<std::string> Args = {"--no-default-config",
                                     "--target=arm64-apple-macosx13.0",
                                     "--sysroot=" + Sysroot.string(),
                                     "-O0",
                                     "-fno-lto",
                                     "-nostdlib",
                                     "-Wl,--no-uuid",
                                     "-Wl,--no-adhoc-codesign",
                                     "-Wl,-e,_main",
                                     "-Wl,-pagezero-size,1"};
    Args.insert(Args.end(), Policies.begin(), Policies.end());
    Args.insert(Args.end(), {Object.string(), "-o", Output.string()});
    return ncc(Args);
  };

  const auto FatalOutput = tmpFile("darwin-warning-policy-fatal.macho");
  const CmdResult Fatal = LinkWithPolicies({"-Werror"}, FatalOutput);
  EXPECT_NE(Fatal.exitCode, 0) << Fatal.err;
  EXPECT_TRUE(Fatal.stderrContains("__PAGEZERO size is not page aligned"))
      << Fatal.err;
  EXPECT_FALSE(fs::exists(FatalOutput));

  for (const std::vector<std::string> &Policies :
       {std::vector<std::string>{"-w", "-Werror"},
        std::vector<std::string>{"-Werror", "-w"}}) {
    const auto Output =
        tmpFile(Policies.front() == "-w"
                    ? "darwin-warning-policy-suppress-then-fatal.macho"
                    : "darwin-warning-policy-fatal-then-suppress.macho");
    const CmdResult Suppressed = LinkWithPolicies(Policies, Output);
    ASSERT_EQ(Suppressed.exitCode, 0) << Suppressed.err;
    EXPECT_FALSE(
        Suppressed.stderrContains("__PAGEZERO size is not page aligned"));
    EXPECT_FALSE(Suppressed.stderrContains("warning:"));
    EXPECT_FALSE(Suppressed.stderrContains("unknown argument '-w'"));
    EXPECT_TRUE(fs::is_regular_file(Output));
  }

  const auto ErrorOutput = tmpFile("darwin-warning-policy-error.macho");
  const CmdResult Error =
      LinkWithPolicies({"-w", "-Wl,-segprot,__TEXT,rxq,rx"}, ErrorOutput);
  EXPECT_NE(Error.exitCode, 0) << Error.err;
  EXPECT_TRUE(Error.stderrContains("unknown -segprot letter 'q' in rxq"))
      << Error.err;
  EXPECT_FALSE(Error.stderrContains("__PAGEZERO size is not page aligned"));
  EXPECT_FALSE(Error.stderrContains("warning:"));
  EXPECT_FALSE(fs::exists(ErrorOutput));
}

TEST_F(DriverTest, CrossAndroid) {
  auto sysroot = neverc().parent_path().parent_path() / "runtime/android/arm64";
  if (!fs::is_directory(sysroot))
    GTEST_SKIP() << "Android cross-compile needs bundled sysroot";
  syntaxCheck("test_cross_android",
              (testDir() / "platform/test_android_target_min.c").string(),
              "c11", "aarch64-linux-android29");
}

// Builtin string in kernel-style code on multiple targets
TEST_F(DriverTest, StringKernelMultiTarget) {
  auto src = (testDir() / "string/test_neverc_string_kernel.c").string();
  std::string alloc =
      "-fbuiltin-string -DNEVERC_STRING_ALLOC=kernel_alloc "
      "-DNEVERC_STRING_FREE=kernel_free";
  syntaxCheck("neverc_string_kernel_linux", src, "c23", "x86_64-linux-gnu",
              alloc);
  syntaxCheck("neverc_string_kernel_android", src, "c23",
              "aarch64-linux-android29", alloc);
  syntaxCheck("neverc_string_kernel_ios", src, "c23", "aarch64-apple-ios",
              alloc);
  syntaxCheck("neverc_string_kernel_macos", src, "c23", hostTriple(), alloc);
  syntaxCheck("neverc_string_kernel_windows", src, "c23",
              "x86_64-windows-msvc", alloc);
}

TEST_F(DriverTest, InlineAsmMS) {
  syntaxCheck("test_inline_asm_ms",
              (testDir() / "asm/test_inline_asm_ms.c").string(), "c11",
              "x86_64-windows-msvc", "-fms-extensions");
}

TEST_F(DriverTest, InlineAsmMSCompile) {
  auto obj = tmpFile("inline_asm_ms.obj");
  auto r = ncc({"-std=c11", "-target", "x86_64-windows-msvc", "-fms-extensions",
                "-c", (testDir() / "asm/test_inline_asm_ms.c").string(), "-o",
                obj.string()});
  EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST_F(DriverTest, SEHTryC) {
  syntaxCheck("seh_try_c",
              (testDir() / "platform/seh_try_c.c").string(), "c11",
              "x86_64-windows-msvc", "-fms-extensions");
}

TEST_F(DriverTest, RejectDynCodeSEH) {
  auto src = (testDir() / "platform/seh_try_c.c").string();
  auto grep =
      "SEH '__try'/'__except'/'__finally' is not supported under -fdyncode";
  expectCommandFail("reject_dyncode_seh_x64", grep,
                    {"-fdyncode", "--target=x86_64-pc-windows-msvc",
                     "-fms-extensions", src, "-o", tmpFile("seh.bin").string()});
  expectCommandFail("reject_dyncode_seh_arm64", grep,
                    {"-fdyncode", "--target=aarch64-pc-windows-msvc",
                     "-fms-extensions", src, "-o", tmpFile("seh.bin").string()});
}

TEST_F(DriverTest, AdvancedFlagsRejected) {
  syntaxCheck("advanced_flags_rejected",
              (testDir() / "advanced_flags_rejected.c").string(), "c11",
              hostTriple());
}

// Reject unsupported architectures
TEST_F(DriverTest, RejectUnsupportedTargets) {
  auto src = (testDir() / "test_basic.c").string();
  struct Case { const char *name; const char *triple; };
  Case cases[] = {
      {"armv7", "armv7-unknown-linux-gnueabi"},
      {"riscv64", "riscv64-unknown-linux-gnu"},
      {"mips64", "mips64-unknown-linux-gnu"},
      {"i386", "i386-pc-linux-gnu"},
      {"arm64ec", "arm64ec-windows-msvc"},
      {"arm64e", "arm64e-apple-darwin"},
      {"aarch64_be", "aarch64_be-linux-gnu"},
      {"wasm32", "wasm32-unknown-unknown"},
      {"wasm64", "wasm64-unknown-unknown"},
  };
  for (auto &c : cases) {
    SCOPED_TRACE(c.name);
    expectCommandFail(std::string("reject_target_") + c.name,
                      "unsupported target architecture",
                      {"-target", c.triple, "-c", src, "-o", "/dev/null"});
  }
}

TEST_F(DriverTest, RejectDarwinArch) {
  if (!isDarwin()) {
    GTEST_SKIP() << "Darwin -arch tests";
    return;
  }
  auto src = (testDir() / "test_basic.c").string();
  auto sf = sysrootFlags();
  for (auto *arch : {"armv7", "arm64e", "arm64_32"}) {
    SCOPED_TRACE(arch);
    std::vector<std::string> args = {"-arch", arch, "-c", src, "-o",
                                     "/dev/null"};
    for (auto &f : sf) args.push_back(f);
    expectCommandFail(std::string("reject_arch_") + arch, "invalid arch name",
                      args);
  }
}

TEST_F(DriverTest, RejectCxxMode) {
  auto src = (testDir() / "test_basic.c").string();
  auto sf = sysrootFlags();
  auto af = archFlags();
  {
    std::vector<std::string> args = {"-x", "c++", "-fsyntax-only", src};
    for (auto &f : sf) args.push_back(f);
    for (auto &f : af) args.push_back(f);
    expectCommandFail("reject_x_cxx", "c++", args);
  }
  {
    std::vector<std::string> args = {"-x", "objective-c", "-fsyntax-only", src};
    for (auto &f : sf) args.push_back(f);
    for (auto &f : af) args.push_back(f);
    expectCommandFail("reject_x_objc", "objective-c", args);
  }
}

TEST_F(DriverTest, SingleDriverEntrypoint) {
  auto buildDir = neverc().parent_path();
  std::vector<std::string> forbidden = {
      "neverc++",  "neverc-cl",  "neverc-cpp",  "neverc-dxc",
      "clang",     "clang++",    "clang-cl",    "clang-cpp",
      "clang-dxc", "cl",         "gcc",         "g++",
      "cpp",       "flang",      "dxc",         "lld",
      "ld.lld",    "lld-link",   "ld64.lld",    "ld-lld",
      "lld-gnu",   "lld-darwin",
  };
  for (auto &name : forbidden)
    EXPECT_FALSE(fs::exists(buildDir / name))
        << "unexpected driver: " << name;
}

TEST_F(DriverTest, DebugInfoRequiresGAcrossObjectFormats) {
  const auto Source = tmpFile("debug-policy.c");
  writeFile(Source, "__attribute__((noinline, used)) static int "
                    "neverc_private_release_symbol(void) { return 42; }\n"
                    "__attribute__((noinline, used)) int "
                    "neverc_public_release_symbol(void) {\n"
                    "  return neverc_private_release_symbol();\n"
                    "}\n");

  for (const ReleaseTargetCase &Target : releaseTargets()) {
    SCOPED_TRACE(Target.Name);
    for (bool EmitDebugInfo : {false, true}) {
      SCOPED_TRACE(EmitDebugInfo ? "-g" : "default");
      const auto ObjectPath =
          tmpFile(std::string("debug-policy-") + Target.Name +
                  (EmitDebugInfo ? "-g" : "-default") + Target.ObjectSuffix);
      std::vector<std::string> Args = {
          std::string("--target=") + Target.Triple,
          "-fno-lto",
          "-fno-stack-protector",
      };
      if (EmitDebugInfo)
        Args.push_back("-g");
      Args.insert(Args.end(), {"-c", Source.string(), "-o",
                               ObjectPath.string()});

      auto Compile = ncc(Args);
      ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

      const ReleaseMetadata Metadata = inspectMetadata(ObjectPath);
      EXPECT_EQ(Metadata.HasDebugSections, EmitDebugInfo);
      EXPECT_TRUE(Metadata.HasPrivateSymbol);
      EXPECT_TRUE(Metadata.HasPublicSymbol);
      EXPECT_TRUE(Metadata.HasReleaseSymbolNameBytes);
    }
  }
}

TEST_F(DriverTest, StripOptionRejectsNonFinalOutputs) {
  const auto Source = tmpFile("strip-scope.c");
  const auto Object = tmpFile("strip-scope.o");
  writeFile(Source, "int neverc_strip_scope(void) { return 42; }\n");

  expectCommandFail("strip_compile_only", "only applies to final linked",
                    {"--target=x86_64-linux-gnu", "-c", "--strip",
                     Source.string(), "-o", Object.string()});

  auto Compile = ncc({"--target=x86_64-linux-gnu", "-fno-lto", "-c",
                      Source.string(), "-o", Object.string()});
  ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

  expectCommandFail("strip_relocatable", "only applies to final linked",
                    {"--target=x86_64-linux-gnu", "-nostdlib", "-r", "-s",
                     Object.string(), "-o",
                     tmpFile("strip-scope-relocatable.o").string()});

  expectCommandFail(
      "strip_android_kernel_intermediate", "only applies to final linked",
      {"--target=aarch64-linux-android", "-fandroid-kernel-driver-mode",
       "-DNVK_KERNEL=510", "-nostdlib", "-r", "--strip", Source.string(),
       "-o", tmpFile("strip-scope-android-intermediate.o").string()});

  expectCommandFail("strip_static_library", "only applies to final linked",
                    {"--emit-static-lib", "--strip", Object.string(), "-o",
                     tmpFile("strip-scope.a").string()});

  expectCommandFail("strip_dyncode", "only applies to final linked",
                    {"--target=x86_64-linux-gnu", "-fdyncode", "-s",
                     Source.string(), "-o",
                     tmpFile("strip-scope.bin").string()});
}

TEST_F(DriverTest, StripOptionAcceptsFinalAndroidKernelModule) {
  const auto Source = tmpFile("strip-android-module-scope.c");
  const auto Module = tmpFile("strip-android-module-scope.ko");
  writeFile(Source,
            "__attribute__((used, noinline)) static int "
            "neverc_android_module_private(void) { return 42; }\n"
            "int neverc_android_module_public(void) {\n"
            "  return neverc_android_module_private();\n"
            "}\n");

  auto Result = ncc({"--target=aarch64-linux-android",
                     "-fandroid-kernel-driver-mode", "-DNVK_KERNEL=510",
                     "-nostdlib", "-r", "--strip", Source.string(), "-o",
                     Module.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::is_regular_file(Module));
}

TEST_F(DriverTest, AndroidKernelModeUsesGeneralRegistersOnly) {
  const auto Source = tmpFile("android-kernel-general-registers.c");
  writeFile(Source, "int neverc_kernel_scalar_only(void) { return 0; }\n");

  const auto Result = ncc(
      {"-###", "--target=aarch64-linux-android", "-fandroid-kernel-driver-mode",
       "-DNVK_KERNEL=510", "-c", Source.string(), "-o",
       tmpFile("android-kernel-general-registers.o").string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  for (const char *Feature :
       {"+reserve-x18", "+v8a", "-fp-armv8", "-crypto", "-neon", "-sve",
        "-sve2", "-sme", "-sme2"}) {
    const std::string Expected =
        std::string("\"-target-feature\" \"") + Feature + "\"";
    EXPECT_NE(Result.err.find(Expected), std::string::npos)
        << "missing " << Expected << " in:\n"
        << Result.err;
  }
}

TEST_F(DriverTest, StripOptionRemovesNamesAndDwarfAcrossFormats) {
  const auto Source = tmpFile("strip_release.c");
  writeFile(Source,
            "__attribute__((noinline, used)) static int "
            "neverc_private_release_symbol(void) { return 42; }\n"
            "__attribute__((noinline, used)) int "
            "neverc_public_release_symbol(void) {\n"
            "  return neverc_private_release_symbol();\n"
            "}\n"
            "int main(void) { return neverc_public_release_symbol(); }\n");

  for (const ReleaseTargetCase &Target : releaseTargets()) {
    SCOPED_TRACE(Target.Name);
    const std::string TargetArg = std::string("--target=") + Target.Triple;
    const auto ObjectPath = tmpFile(std::string("strip-input-") + Target.Name +
                                    Target.ObjectSuffix);
    auto Compile = ncc({TargetArg, "-fno-lto", "-nostdlib", "-g", "-c",
                        Source.string(), "-o", ObjectPath.string()});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

    const ReleaseMetadata InputMetadata = inspectMetadata(ObjectPath);
    EXPECT_TRUE(InputMetadata.HasDebugSections)
        << "test input must contain DWARF";
    EXPECT_TRUE(InputMetadata.HasPrivateSymbol)
        << "test input must contain a private symbol name";
    EXPECT_TRUE(InputMetadata.HasReleaseSymbolNameBytes)
        << "test input must contain release symbol strings";

    for (const std::string StripOption : {"--strip", "-s"}) {
      SCOPED_TRACE(StripOption);
      const auto ImagePath = tmpFile(
          std::string("strip-output-") + Target.Name +
          (StripOption == "--strip" ? "-long" : "-short") + Target.ImageSuffix);
      std::vector<std::string> LinkArgs = {
          TargetArg,   "-fno-lto",          "-nostdlib", "-g",
          StripOption, ObjectPath.string(), "-o",        ImagePath.string(),
      };
      LinkArgs.insert(LinkArgs.end(), Target.ExecutableLinkerArgs.begin(),
                      Target.ExecutableLinkerArgs.end());
      if (llvm::StringRef(Target.Name) == "coff")
        LinkArgs.insert(LinkArgs.end(), {"-Xlinker", "--debug=dwarf"});
      auto Link = ncc(LinkArgs);
      ASSERT_EQ(Link.exitCode, 0) << Link.err;

      const ReleaseMetadata OutputMetadata = inspectMetadata(ImagePath);
      EXPECT_FALSE(OutputMetadata.HasDebugSections);
      EXPECT_FALSE(OutputMetadata.HasPrivateSymbol);
      EXPECT_FALSE(OutputMetadata.HasReleaseSymbolNameBytes);
      EXPECT_FALSE(fs::exists(ImagePath.string() + ".dSYM"));
    }
  }
}

TEST_F(DriverTest, StripOptionRemovesMetadataWithDefaultLtoAcrossFormats) {
  const auto Source = tmpFile("strip-lto-release.c");
  writeFile(Source,
            "__attribute__((noinline, used)) static int "
            "neverc_private_release_symbol(void) { return 42; }\n"
            "__attribute__((noinline, used)) int "
            "neverc_public_release_symbol(void) {\n"
            "  return neverc_private_release_symbol();\n"
            "}\n"
            "int main(void) { return neverc_public_release_symbol(); }\n");

  for (const ReleaseTargetCase &Target : releaseTargets()) {
    SCOPED_TRACE(Target.Name);
    const auto Image =
        tmpFile(std::string("strip-lto-") + Target.Name + Target.ImageSuffix);
    std::vector<std::string> Args = {
        std::string("--target=") + Target.Triple,
        "-fno-stack-protector",
        "-nostdlib",
        "-g",
        "--strip",
        Source.string(),
        "-o",
        Image.string(),
    };
    Args.insert(Args.end(), Target.ExecutableLinkerArgs.begin(),
                Target.ExecutableLinkerArgs.end());

    auto Link = ncc(Args);
    ASSERT_EQ(Link.exitCode, 0) << Link.err;

    const ReleaseMetadata Metadata = inspectMetadata(Image);
    EXPECT_FALSE(Metadata.HasDebugSections);
    EXPECT_FALSE(Metadata.HasPrivateSymbol);
    EXPECT_FALSE(Metadata.HasReleaseSymbolNameBytes);
    EXPECT_FALSE(fs::exists(Image.string() + ".dSYM"));
  }
}

TEST_F(DriverTest, StripOptionSuppressesDarwinDsymBundle) {
  const auto Source = tmpFile("strip_dsym.c");
  const auto Image = tmpFile("strip-dsym.macho");
  writeFile(Source, "int main(void) { return 0; }\n");

  auto Result = ncc({"--target=arm64-apple-macos", "-fno-lto", "-nostdlib",
                     "-g", "--strip", Source.string(), "-o", Image.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::exists(Image));
  EXPECT_FALSE(fs::exists(Image.string() + ".dSYM"));
}

TEST_F(DriverTest, StripOptionPreservesDynamicAbiWithoutStaticSymbolLeak) {
  const auto Source = tmpFile("strip_shared.c");
  writeFile(Source, "__attribute__((noinline, used)) static int "
                    "neverc_private_release_symbol(void) { return 42; }\n"
                    "#if defined(_WIN32)\n"
                    "__declspec(dllexport)\n"
                    "#endif\n"
                    "__attribute__((noinline, used)) int "
                    "neverc_public_release_api(void) {\n"
                    "  return neverc_private_release_symbol();\n"
                    "}\n");

  for (const ReleaseTargetCase &Target : releaseTargets()) {
    SCOPED_TRACE(Target.Name);
    const auto Image = tmpFile(std::string("strip-shared-") + Target.Name +
                               Target.SharedImageSuffix);
    std::vector<std::string> Args = {
        std::string("--target=") + Target.Triple,
        "-fno-lto",
        "-fno-stack-protector",
        "-nostdlib",
        "-shared",
        "--strip",
        Source.string(),
        "-o",
        Image.string(),
    };
    Args.insert(Args.end(), Target.SharedLinkerArgs.begin(),
                Target.SharedLinkerArgs.end());

    auto Link = ncc(Args);
    ASSERT_EQ(Link.exitCode, 0) << Link.err;

    const ReleaseMetadata Metadata = inspectMetadata(Image);
    EXPECT_TRUE(Metadata.HasPublicSymbolNameBytes)
        << "the dynamic ABI still requires the exported name";
    EXPECT_FALSE(Metadata.HasPrivateSymbolNameBytes);
    EXPECT_FALSE(Metadata.HasPublicSymbol)
        << "the export must not be duplicated in the static symbol table";
    EXPECT_FALSE(Metadata.HasPrivateSymbol);
  }
}

TEST_F(DriverTest, DarwinLtoDebugProducesDsymUnlessStripped) {
  if (!isDarwin())
    GTEST_SKIP() << "dsymutil is a Darwin-host packaging tool";

  const auto Source = tmpFile("lto_dsym.c");
  const auto DebugImage = tmpFile("lto-debug.macho");
  const auto StrippedImage = tmpFile("lto-stripped.macho");
  writeFile(Source,
            "__attribute__((noinline)) int lto_debug_marker(void) { return "
            "42; }\n"
            "int main(void) { return lto_debug_marker(); }\n");

  const std::vector<std::string> CommonArgs = {
      "--target=arm64-apple-macos", "-nostdlib", "-fno-stack-protector", "-g",
      Source.string()};

  auto DebugArgs = CommonArgs;
  DebugArgs.insert(DebugArgs.end(), {"-o", DebugImage.string()});
  auto Debug = ncc(DebugArgs);
  ASSERT_EQ(Debug.exitCode, 0) << Debug.err;
  const auto DsymBundle = fs::path(DebugImage.string() + ".dSYM");
  const auto DwarfImage =
      DsymBundle / "Contents/Resources/DWARF" / DebugImage.filename();
  ASSERT_TRUE(fs::is_directory(DsymBundle));
  ASSERT_TRUE(fs::is_regular_file(DwarfImage));
  EXPECT_TRUE(inspectMetadata(DwarfImage).HasDebugSections);

  auto StrippedArgs = CommonArgs;
  StrippedArgs.insert(StrippedArgs.end(),
                      {"--strip", "-o", StrippedImage.string()});
  auto Stripped = ncc(StrippedArgs);
  ASSERT_EQ(Stripped.exitCode, 0) << Stripped.err;
  EXPECT_FALSE(fs::exists(StrippedImage.string() + ".dSYM"));
}

// Reject C++ / ObjC / OpenMP flags
TEST_F(DriverTest, RejectUnsupportedFlags) {
  auto src = (testDir() / "test_basic.c").string();
  auto sf = sysrootFlags();
  auto af = archFlags();

  struct Case { const char *flag; const char *grep; };
  Case cases[] = {
      {"-fcoroutines", "unknown argument: '-fcoroutines'"},
      {"-fcxx-exceptions", "unknown argument: '-fcxx-exceptions'"},
      {"-emit-module-interface", "unknown argument"},
      {"-emit-header-unit", "unknown argument"},
      {"-fobjc-arc", "unknown argument"},
      {"-fblocks", "unknown argument"},
      {"-fapinotes", "unknown argument"},
      {"-fopenmp", "unknown argument"},
      {"-fopenacc", "unknown argument"},
  };
  for (auto &c : cases) {
    SCOPED_TRACE(c.flag);
    std::vector<std::string> args = {c.flag, "-fsyntax-only", src};
    for (auto &f : sf) args.push_back(f);
    for (auto &f : af) args.push_back(f);
    expectCommandFail(std::string("reject_") + c.flag, c.grep, args);
  }
}

// Reject driver-mode overrides
TEST_F(DriverTest, RejectDriverModes) {
  auto src = (testDir() / "test_basic.c").string();
  for (auto *mode : {"neverc", "cl", "g++", "cpp", "flang", "dxc"}) {
    SCOPED_TRACE(mode);
    std::string flag = std::string("--driver-mode=") + mode;
    expectCommandFail(std::string("reject_driver_mode_") + mode,
                      "unknown argument", {flag, "-###", src});
  }
}

TEST_F(DriverTest, O0RuntimeBitcodeCanRemainOptimizable) {
  auto src = (testDir() / "test_basic.c").string();
  auto r = ncc({"-###", "-c", "-O0", "-disable-O0-optnone",
                "-finline-functions", src});
  ASSERT_EQ(r.exitCode, 0) << r.err;

  const std::string all = r.err + r.out;
  EXPECT_NE(all.find("\"-disable-O0-optnone\""), std::string::npos)
      << "the driver must forward the cc1 optimization attribute control\n"
      << all;
  EXPECT_NE(all.find("\"-finline-functions\""), std::string::npos)
      << "runtime bitcode must not gain implicit noinline attributes\n"
      << all;
}

// Windows MSVC linker option forwarding
TEST_F(DriverTest, WindowsVbsEnclaveLinkOptionsXmslink) {
  auto src = (testDir() / "test_basic.c").string();
  auto image = tmpFile("vbs-enclave-xmslink.dll");
  auto r = ncc({"-###", "--target=x86_64-pc-windows-msvc", "-nostdlib",
                "-shared", "-Xmslink", "/ENCLAVE", "-Xmslink", "/GUARD:MIXED",
                "-Xmslink", "/INTEGRITYCHECK", "-Xmslink", "/INCREMENTAL:NO",
                src, "-o", image.string()});
  ASSERT_EQ(r.exitCode, 0) << r.err;

  const std::string all = r.err + r.out;
  for (const char *option :
       {"--enclave", "--guard=mixed", "--integritycheck", "--no-incremental"}) {
    EXPECT_NE(all.find(std::string("\"") + option + "\""), std::string::npos)
        << "missing normalized linker option " << option << '\n'
        << all;
  }
  for (const char *option :
       {"/ENCLAVE", "/GUARD:MIXED", "/INTEGRITYCHECK", "/INCREMENTAL:NO"}) {
    EXPECT_EQ(all.find(std::string("\"") + option + "\""), std::string::npos)
        << "raw MSVC option was rendered as a linker input: " << option << '\n'
        << all;
  }
}

TEST_F(DriverTest, WindowsVbsEnclaveUsesBundledRuntimeByDefault) {
  auto src = (testDir() / "test_basic.c").string();

  for (const char *target :
       {"x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc"}) {
    SCOPED_TRACE(target);
    auto image = tmpFile(std::string("vbs-enclave-runtime-") + target + ".dll");
    auto r = ncc({"-###", std::string("--target=") + target, "-nostdlib",
                  "-shared", "-Xmslink", "/NODEFAULTLIB", "-Xmslink",
                  "/ENCLAVE", src, "-o", image.string()});
    ASSERT_EQ(r.exitCode, 0) << r.err;

    const std::string all = collapseSeparators(r.err + r.out);
    EXPECT_NE(all.find("/runtime/windows/"), std::string::npos)
        << "VBS enclave links must use NeverC's bundled runtime\n"
        << all;
    EXPECT_NE(all.find("/msvc/crt/lib/enclave"), std::string::npos)
        << "missing bundled enclave CRT search path\n"
        << all;
    EXPECT_NE(all.find("/msvc/sdk/lib/ucrt_enclave"), std::string::npos)
        << "missing bundled enclave UCRT search path\n"
        << all;
  }
}

TEST_F(DriverTest, WindowsVbsEnclaveHonorsExplicitVCToolsDir) {
  auto src = (testDir() / "test_basic.c").string();
  const fs::path foreignToolchain = tmp() / "foreign-vc";
  fs::create_directories(foreignToolchain / "lib" / "x64" / "enclave");

  auto image = tmpFile("vbs-enclave-explicit-vctools.dll");
  auto r =
      ncc({"-###", "--target=x86_64-pc-windows-msvc", "-vctoolsdir",
           foreignToolchain.string(), "-nostdlib", "-shared", "-Xmslink",
           "/NODEFAULTLIB", "-Xmslink", "/ENCLAVE", src, "-o", image.string()});
  ASSERT_EQ(r.exitCode, 0) << r.err;

  const std::string all = collapseSeparators(r.err + r.out);
  const std::string explicitRoot =
      collapseSeparators(foreignToolchain.string());
  EXPECT_NE(all.find(explicitRoot + "/lib/x64/enclave"), std::string::npos)
      << "the explicit MSVC enclave runtime was not selected\n"
      << all;
  EXPECT_EQ(all.find("\"--libpath=" + explicitRoot + "/lib/x64\""),
            std::string::npos)
      << "the explicit enclave link fell back to the ordinary MSVC CRT\n"
      << all;
  EXPECT_EQ(all.find("/runtime/windows/x64/msvc/"), std::string::npos)
      << "the bundled runtime overrode an explicit -vctoolsdir\n"
      << all;
}

TEST_F(DriverTest, WindowsVbsEnclaveHonorsExplicitWinSysRoot) {
  auto src = (testDir() / "test_basic.c").string();
  const fs::path winSysRoot = tmp() / "foreign-winsysroot";
  const fs::path vcRoot = winSysRoot / "VC" / "Tools" / "MSVC" / "14.99.99999";
  const fs::path sdkRoot = winSysRoot / "Windows Kits" / "10";
  const fs::path sdkVersion = sdkRoot / "Include" / "10.0.99999.0";
  const fs::path enclaveUcrt =
      sdkRoot / "Lib" / "10.0.99999.0" / "ucrt_enclave" / "x64";
  const fs::path userMode = sdkRoot / "Lib" / "10.0.99999.0" / "um" / "x64";
  fs::create_directories(vcRoot / "lib" / "x64" / "enclave");
  fs::create_directories(sdkVersion);
  fs::create_directories(enclaveUcrt);
  fs::create_directories(userMode);

  auto image = tmpFile("vbs-enclave-explicit-winsysroot.dll");
  auto r =
      ncc({"-###", "--target=x86_64-pc-windows-msvc", "-winsysroot",
           winSysRoot.string(), "-nostdlib", "-shared", "-Xmslink",
           "/NODEFAULTLIB", "-Xmslink", "/ENCLAVE", src, "-o", image.string()});
  ASSERT_EQ(r.exitCode, 0) << r.err;

  const std::string all = collapseSeparators(r.err + r.out);
  for (const fs::path &expected :
       {vcRoot / "lib" / "x64" / "enclave", enclaveUcrt, userMode}) {
    EXPECT_NE(all.find(collapseSeparators(expected.string())),
              std::string::npos)
        << "the explicit Windows sysroot path was not selected\n"
        << all;
  }
  const std::string ordinaryUcrt = collapseSeparators(
      (sdkRoot / "Lib" / "10.0.99999.0" / "ucrt" / "x64").string());
  EXPECT_EQ(all.find("\"--libpath=" + ordinaryUcrt + "\""), std::string::npos)
      << "the explicit enclave link fell back to the ordinary UCRT\n"
      << all;
  EXPECT_EQ(all.find("/runtime/windows/x64/msvc/"), std::string::npos)
      << "the bundled runtime overrode an explicit -winsysroot\n"
      << all;
}

TEST_F(DriverTest, WindowsVbsEnclaveDoesNotSelectEnclaveLibrariesImplicitly) {
  auto src = (testDir() / "test_basic.c").string();
  for (const char *target :
       {"x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc"}) {
    SCOPED_TRACE(target);
    auto image =
        tmpFile(std::string("vbs-enclave-explicit-runtime-") + target + ".dll");
    auto r =
        ncc({"-###", std::string("--target=") + target, "-nostdlib", "-shared",
             "-Xmslink", "/ENCLAVE", src, "-o", image.string()});
    ASSERT_EQ(r.exitCode, 0) << r.err;

    const std::string all = collapseSeparators(r.err + r.out);
    EXPECT_NE(all.find("/runtime/windows/"), std::string::npos)
        << "VBS enclave links must use NeverC's bundled runtime\n"
        << all;
    EXPECT_NE(all.find("/msvc/crt/lib"), std::string::npos)
        << "/ENCLAVE alone must select the bundled ordinary CRT\n"
        << all;
    EXPECT_NE(all.find("/msvc/sdk/lib/ucrt"), std::string::npos)
        << "/ENCLAVE alone must select the bundled ordinary UCRT\n"
        << all;
    EXPECT_EQ(all.find("/msvc/crt/lib/enclave"), std::string::npos)
        << "/ENCLAVE alone must not select the enclave CRT\n"
        << all;
    EXPECT_EQ(all.find("/msvc/sdk/lib/ucrt_enclave"), std::string::npos)
        << "/ENCLAVE alone must not select the enclave UCRT\n"
        << all;
  }
}

TEST_F(DriverTest, WindowsVbsEnclaveLinksWithBundledRuntime) {
  struct TargetCase {
    const char *Triple;
    const char *Machine;
  };

  const auto fixture = testDir() / "Inputs" / "VBSEnclave";
  for (const TargetCase &target :
       {TargetCase{"x86_64-pc-windows-msvc", "/MACHINE:X64"},
        TargetCase{"aarch64-pc-windows-msvc", "/MACHINE:ARM64"}}) {
    SCOPED_TRACE(target.Triple);
    const std::string targetArg = std::string("--target=") + target.Triple;
    std::vector<fs::path> objects;

    for (const char *sourceName : {"enclave", "guarded", "legacy"}) {
      const fs::path object =
          tmpFile(std::string("vbs-cross-") + target.Triple + "-" + sourceName +
                  ".obj");
      std::vector<std::string> args = {
          targetArg, "-x", "c", "-fno-lto", "-fno-builtin-mimalloc", "-c"};
      if (std::string(sourceName) != "legacy")
        args.push_back("-fms-guard=cf");
      args.push_back((fixture / (std::string(sourceName) + ".cpp")).string());
      args.insert(args.end(), {"-o", object.string()});

      auto compile = ncc(args);
      ASSERT_EQ(compile.exitCode, 0) << compile.err;
      objects.push_back(object);
    }

    const fs::path image =
        tmpFile(std::string("vbs-cross-") + target.Triple + ".dll");
    std::vector<std::string> linkArgs = {
        targetArg,           "-fno-lto",          "-shared",
        "-nostdlib",         objects[0].string(), objects[1].string(),
        objects[2].string(), "-lvertdll",         "-lbcrypt",
        "-llibcmt",          "-llibvcruntime",    "-lucrt",
        "-Xmslink",          "/INCREMENTAL:NO",   "-Xmslink",
        "/NODEFAULTLIB",     "-Xmslink",          "/ENCLAVE",
        "-Xmslink",          "/INTEGRITYCHECK",   "-Xmslink",
        "/GUARD:MIXED",      "-Xmslink",          "/DYNAMICBASE",
        "-Xmslink",          target.Machine,      "-o",
        image.string(),
    };

    auto link = ncc(linkArgs);
    ASSERT_EQ(link.exitCode, 0) << link.err;
    ASSERT_TRUE(fs::is_regular_file(image));

    auto object = llvm::object::ObjectFile::createObjectFile(image.string());
    if (!object) {
      ADD_FAILURE() << llvm::toString(object.takeError()).str().str();
      continue;
    }
    const auto *coff =
        llvm::dyn_cast<llvm::object::COFFObjectFile>(object->getBinary());
    ASSERT_NE(coff, nullptr);

    std::set<std::string> imports;
    for (const llvm::object::ImportDirectoryEntryRef &entry :
         coff->import_directories()) {
      llvm::StringRef name;
      if (llvm::Error error = entry.getName(name)) {
        ADD_FAILURE() << llvm::toString(std::move(error)).str().str();
        continue;
      }
      imports.insert(name.lower().str().str());
    }
    EXPECT_EQ(imports.count("ucrtbase_enclave.dll"), 1u);
    EXPECT_EQ(imports.count("vertdll.dll"), 1u);
    EXPECT_EQ(imports.count("kernel32.dll"), 0u)
        << "ordinary Windows CRT leaked into an enclave image";
  }
}

TEST_F(DriverTest, WindowsVbsEnclaveLinkOptionsWl) {
  auto src = (testDir() / "test_basic.c").string();
  auto image = tmpFile("vbs-enclave-wl.dll");
  auto r =
      ncc({"-###", "--target=x86_64-pc-windows-msvc", "-nostdlib", "-shared",
           "-Wl,/ENCLAVE,/GUARD:MIXED,/INTEGRITYCHECK,/INCREMENTAL:NO", src,
           "-o", image.string()});
  ASSERT_EQ(r.exitCode, 0) << r.err;

  const std::string all = r.err + r.out;
  for (const char *option :
       {"--enclave", "--guard=mixed", "--integritycheck", "--no-incremental"}) {
    EXPECT_NE(all.find(std::string("\"") + option + "\""), std::string::npos)
        << "missing normalized linker option " << option << '\n'
        << all;
  }
  for (const char *option :
       {"/ENCLAVE", "/GUARD:MIXED", "/INTEGRITYCHECK", "/INCREMENTAL:NO"}) {
    EXPECT_EQ(all.find(std::string("\"") + option + "\""), std::string::npos)
        << "raw MSVC option was rendered as a linker input: " << option << '\n'
        << all;
  }
}

TEST_F(DriverTest, WindowsMSVCForwardedBooleanNoOptions) {
  auto src = (testDir() / "test_basic.c").string();
  auto image = tmpFile("msvc-forwarded-no-options.dll");
  auto r =
      ncc({"-###", "--target=x86_64-pc-windows-msvc", "-nostdlib", "-shared",
           "-Xmslink", "/DYNAMICBASE:NO", "-Xmslink", "/INTEGRITYCHECK:NO",
           "-Xmslink", "/DRIVER:UPONLY", src, "-o", image.string()});
  ASSERT_EQ(r.exitCode, 0) << r.err;

  const std::string all = r.err + r.out;
  for (const char *option : {"--no-dynamicbase", "--no-integritycheck"})
    EXPECT_NE(all.find(std::string("\"") + option + "\""), std::string::npos)
        << "missing normalized negative linker option " << option << '\n'
        << all;
  EXPECT_EQ(all.find("--dynamicbase=NO"), std::string::npos) << all;
  EXPECT_EQ(all.find("--integritycheck=NO"), std::string::npos) << all;
  EXPECT_NE(all.find("\"--driver=uponly\""), std::string::npos) << all;
  EXPECT_EQ(all.find("--driver=UPONLY"), std::string::npos) << all;
  EXPECT_EQ(all.find("unused argument"), std::string::npos) << all;
}

TEST_F(DriverTest, WindowsVbsForwardingPreservesCrossChannelOrder) {
  auto src = (testDir() / "test_basic.c").string();
  auto image = tmpFile("vbs-forwarding-order.dll");
  auto r = ncc({"-###", "--target=x86_64-pc-windows-msvc", "-nostdlib",
                "-shared", "-Wl,/INCREMENTAL:NO,/GUARD:CF", "-Xmslink",
                "/INCREMENTAL", "-Xmslink", "/GUARD:MIXED", "-Xmslink",
                "/ENCLAVE", src, "-o", image.string()});
  ASSERT_EQ(r.exitCode, 0) << r.err;

  const std::string all = r.err + r.out;
  const size_t noIncremental = all.find("\"--no-incremental\"");
  const size_t incremental = all.find("\"--incremental\"");
  const size_t guardCF = all.find("\"--guard=cf\"");
  const size_t guardMixed = all.find("\"--guard=mixed\"");
  ASSERT_NE(noIncremental, std::string::npos) << all;
  ASSERT_NE(incremental, std::string::npos) << all;
  ASSERT_NE(guardCF, std::string::npos) << all;
  ASSERT_NE(guardMixed, std::string::npos) << all;
  EXPECT_LT(noIncremental, incremental) << all;
  EXPECT_LT(guardCF, guardMixed) << all;
  EXPECT_EQ(all.find("unused argument"), std::string::npos) << all;
}

TEST_F(DriverTest, WindowsVbsEnclaveAbsoluteInputPathIsPreserved) {
  auto object = tmpFile("absolute-link-input.obj");
  auto image = tmpFile("absolute-link-input.dll");
  writeFile(object, "");

  auto r = ncc({"-###", "--target=x86_64-pc-windows-msvc", "-nostdlib",
                "-shared", object.string(), "-o", image.string()});
  ASSERT_EQ(r.exitCode, 0) << r.err;

  const std::string all = collapseSeparators(r.err + r.out);
  const std::string expectedPath = collapseSeparators(object.string());
  EXPECT_NE(all.find(std::string("\"") + expectedPath + "\""),
            std::string::npos)
      << "ordinary absolute linker input path was rewritten\n"
      << all;
}

// Windows MSVC default runtime
TEST_F(DriverTest, WindowsMSVCDefaultRuntime) {
  auto src = (testDir() / "test_basic.c").string();
  auto r = ncc({"-###", "--target=x86_64-pc-windows-msvc", "-c", src});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("--dependent-lib=libcmt") ||
              r.contains("--dependent-lib=libcmt"))
      << "MSVC default runtime missing libcmt\n"
      << r.err << r.out;
}

// Windows MSVC LTO compatibility
TEST_F(DriverTest, WindowsMSVCLTO) {
  auto src = (testDir() / "test_basic.c").string();
  auto r = ncc(
      {"-###", "--target=x86_64-pc-windows-msvc", "-flto", "-c", "--", src});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("-flto=full") || r.contains("-flto=full"));
}

// Windows GNU no MSVC runtime
TEST_F(DriverTest, WindowsGNUNoMSVCRuntime) {
  auto src = (testDir() / "test_basic.c").string();
  auto r = ncc({"-###", "--target=x86_64-w64-windows-gnu", "-c", src});
  EXPECT_EQ(r.exitCode, 0);
  auto all = r.err + r.out;
  EXPECT_EQ(all.find("--dependent-lib=libcmt"), std::string::npos);
}

// ---- .nc extension auto-detection ----

TEST_F(DriverTest, NcExtAutoFlags) {
  auto ncSrc = tmpFile("nc_auto.nc");
  writeFile(ncSrc, "int main(void) { return 0; }");
  auto r = ncc({"-###", "-c", ncSrc.string()});
  EXPECT_EQ(r.exitCode, 0) << r.err;
  auto all = r.err + r.out;
  EXPECT_NE(all.find("-fneverc-types"), std::string::npos)
      << ".nc input should inject -fneverc-types\n" << all;
  EXPECT_NE(all.find("-fbuiltin-string"), std::string::npos)
      << ".nc input should inject -fbuiltin-string\n" << all;
}

TEST_F(DriverTest, CExtNoAutoFlags) {
  auto cSrc = tmpFile("no_auto.c");
  writeFile(cSrc, "int main(void) { return 0; }");
  auto r = ncc({"-###", "-c", cSrc.string()});
  EXPECT_EQ(r.exitCode, 0) << r.err;
  auto all = r.err + r.out;
  EXPECT_EQ(all.find("-fneverc-types"), std::string::npos)
      << ".c input should not inject -fneverc-types\n" << all;
  EXPECT_EQ(all.find("-fbuiltin-string"), std::string::npos)
      << ".c input should not inject -fbuiltin-string\n" << all;
}

TEST_F(DriverTest, NcExtCrossTarget) {
  auto ncSrc = tmpFile("nc_cross.nc");
  writeFile(ncSrc, "int main(void) { return 0; }");
  static const char *triples[] = {
      "x86_64-linux-gnu",        "aarch64-linux-gnu",
      "x86_64-pc-windows-msvc",  "aarch64-pc-windows-msvc",
      "x86_64-apple-macos",      "arm64-apple-macos",
      "aarch64-linux-android29",
  };
  for (auto *triple : triples) {
    SCOPED_TRACE(triple);
    auto r = ncc({"-###", "-target", triple, "-c", ncSrc.string()});
    EXPECT_EQ(r.exitCode, 0) << r.err;
    auto all = r.err + r.out;
    EXPECT_NE(all.find("-fneverc-types"), std::string::npos)
        << ".nc + " << triple << " should inject -fneverc-types\n" << all;
    EXPECT_NE(all.find("-fbuiltin-string"), std::string::npos)
        << ".nc + " << triple << " should inject -fbuiltin-string\n" << all;
  }
}

TEST_F(DriverTest, NcExtWithDynCode) {
  auto ncSrc = tmpFile("nc_dyncode.nc");
  writeFile(ncSrc, "int entry(void) { return 0; }");
  auto r = ncc({"-###", "-fdyncode", "--target=x86_64-linux-gnu",
                ncSrc.string(), "-o", tmpFile("nc_dyncode.bin").string()});
  EXPECT_EQ(r.exitCode, 0) << r.err;
  auto all = r.err + r.out;
  EXPECT_NE(all.find("-fneverc-types"), std::string::npos)
      << ".nc + dyncode should inject -fneverc-types\n" << all;
  EXPECT_NE(all.find("-fbuiltin-string"), std::string::npos)
      << ".nc + dyncode should inject -fbuiltin-string\n" << all;
}

TEST_F(DriverTest, CExtExplicitFlagsStillWork) {
  auto cSrc = tmpFile("explicit_flags.c");
  writeFile(cSrc, "int main(void) { return 0; }");
  auto r = ncc({"-###", "-fneverc-types", "-fbuiltin-string", "-c",
                cSrc.string()});
  EXPECT_EQ(r.exitCode, 0) << r.err;
  auto all = r.err + r.out;
  EXPECT_NE(all.find("-fneverc-types"), std::string::npos)
      << "explicit -fneverc-types should pass through\n" << all;
  EXPECT_NE(all.find("-fbuiltin-string"), std::string::npos)
      << "explicit -fbuiltin-string should pass through\n" << all;
}
