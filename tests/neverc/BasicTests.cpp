#include "NeverCTestFixture.h"

#include <optional>

class BasicTest : public NeverCTest {};

TEST_F(BasicTest, TestBasic) {
  compileRunAndCheck("test_basic",
                     (testDir() / "test_basic.c").string(),
                     "-std=c11", 0, "test_basic: ALL PASSED");
}

TEST_F(BasicTest, TestC11) {
  compileRunAndCheck("test_c11",
                     (testDir() / "standards" / "test_c11.c").string(),
                     "-std=c11", 0, "test_c11: ALL PASSED");
}

TEST_F(BasicTest, TestC23) {
  compileRunAndCheck("test_c23",
                     (testDir() / "standards" / "test_c23.c").string(),
                     "-std=c23", 0, "test_c23: ALL PASSED");
}

TEST_F(BasicTest, TestC23Default) {
  auto src = (testDir() / "standards" / "test_c23_default.c").string();
  compileRunAndCheck("test_c23_default", src, "", 0,
                     "test_c23_default: ALL PASSED");
}

TEST_F(BasicTest, TestPreprocessor) {
  compileRunAndCheck("test_preprocessor",
                     (testDir() / "test_preprocessor.c").string(),
                     "-std=c23", 0, "test_preprocessor: ALL PASSED");
}

// Regression: SWAR per-byte zero-detector borrow-propagation bug in the
// no-SIMD lexer scanners (mis-tokenized "a  != b", "x_^y", "1./2").
TEST_F(BasicTest, SwarTokenBoundaries) {
  compileRunAndCheck("test_swar_token_boundaries",
                     (testDir() / "test_swar_token_boundaries.c").string(),
                     "-std=gnu11", 0,
                     "test_swar_token_boundaries: ALL PASSED");
}

// NeverC builtin string tests
static const char *kStrFlags = "-std=c23 -fbuiltin-string";
static const char *kStrGFlags = "-std=c23 -fbuiltin-string -g";

TEST_F(BasicTest, BuiltinStringFunctionOnlyConsumer) {
  auto src = tmpFile("neverc_string_function_only.c");
  writeFile(src,
            "int main(void) {\n"
            "  string s = \"runtime\";\n"
            "  return neverc_string_len(s) != 7;\n"
            "}\n");
  compileRunAndCheck("neverc_string_function_only", src.string(), kStrFlags, 0);
}

TEST_F(BasicTest, BuiltinStringRuntimeWorksAcrossTranslationUnits) {
  auto owner = tmpFile("neverc_string_owner.c");
  auto consumer = tmpFile("neverc_string_consumer.c");
  writeFile(owner,
            "extern __SIZE_TYPE__ consume_length(string);\n"
            "int main(void) {\n"
            "  string first = \"runtime\";\n"
            "  string second = \"owner\";\n"
            "  return consume_length(first) != 7 ||\n"
            "         neverc_string_len(second) != 5;\n"
            "}\n");
  writeFile(consumer,
            "__SIZE_TYPE__ consume_length(string value) {\n"
            "  return neverc_string_len(value);\n"
            "}\n");

  for (const char *mode : {"", "-flto=full", "-fno-lto"}) {
    SCOPED_TRACE(mode[0] ? mode : "auto-lto");
    auto exe = tmpFile(std::string("neverc_string_multitu_") +
                       (mode[0] ? mode + 1 : "auto"));
    std::vector<std::string> args = {
        "-std=c23", "-fbuiltin-string", owner.string(), consumer.string(),
        "-o", exe.string(),
    };
    if (mode[0])
      args.insert(args.begin() + 2, mode);
    for (auto &flag : sysrootFlags())
      args.push_back(flag);
    for (auto &flag : archFlags())
      args.push_back(flag);

    auto compile = ncc(args);
    ASSERT_EQ(compile.exitCode, 0) << compile.err;
    auto run = exec(exe.string(), {});
    EXPECT_EQ(run.exitCode, 0) << run.out << run.err;

    if (!isWindows() && std::string(mode) == "-fno-lto") {
      auto nm = exec("nm", {"-a", exe.string()});
      ASSERT_EQ(nm.exitCode, 0) << nm.err;

      unsigned copies = 0;
      std::istringstream lines(nm.out);
      for (std::string line; std::getline(lines, line);) {
        auto pos = line.find_last_of(" \t");
        std::string name =
            pos == std::string::npos ? line : line.substr(pos + 1);
        if (isDarwin() && !name.empty() && name.front() == '_')
          name.erase(name.begin());
        if (name == "neverc_string_len")
          ++copies;
      }
      EXPECT_EQ(copies, 1u)
          << "non-LTO multi-TU output must coalesce string runtime code";
    }
  }
}

TEST_F(BasicTest, BuiltinStringRuntimeRemainsOptimizableAfterEmbedding) {
  auto src = tmpFile("neverc_string_optimizable.c");
  auto ir = tmpFile("neverc_string_optimizable.ll");
  writeFile(src,
            "__attribute__((noinline))\n"
            "__SIZE_TYPE__ measured_length(string value) {\n"
            "  return neverc_string_len(value);\n"
            "}\n"
            "int main(void) { return measured_length(\"runtime\") != 7; }\n");

  std::vector<std::string> args = {
      "-std=c23", "-fbuiltin-string", "-fno-builtin-mimalloc", "-O2",
      "-fno-lto", "-S", "-emit-llvm", src.string(), "-o", ir.string(),
  };
  for (auto &flag : sysrootFlags())
    args.push_back(flag);
  for (auto &flag : archFlags())
    args.push_back(flag);

  auto compile = ncc(args);
  ASSERT_EQ(compile.exitCode, 0) << compile.err;

  const std::string text = readFile(ir);
  const size_t definition = text.find("@neverc_string_len(");
  ASSERT_NE(definition, std::string::npos);
  const size_t hash = text.find('#', definition);
  const size_t body = text.find('{', definition);
  ASSERT_NE(hash, std::string::npos);
  ASSERT_NE(body, std::string::npos);
  ASSERT_LT(hash, body);

  size_t groupEnd = hash + 1;
  while (groupEnd < body && text[groupEnd] >= '0' && text[groupEnd] <= '9')
    ++groupEnd;
  const std::string group = text.substr(hash + 1, groupEnd - hash - 1);
  const std::string marker = "attributes #" + group + " = {";
  const size_t attributes = text.find(marker);
  ASSERT_NE(attributes, std::string::npos);
  const size_t lineEnd = text.find('\n', attributes);
  const std::string attributeLine =
      text.substr(attributes, lineEnd - attributes);
  EXPECT_EQ(attributeLine.find("optnone"), std::string::npos);
  EXPECT_EQ(attributeLine.find("noinline"), std::string::npos);
  EXPECT_EQ(text.find("@llvm.used"), std::string::npos)
      << "synthetic used roots must not pin embedded runtime definitions";
  EXPECT_EQ(text.find("@llvm.compiler.used"), std::string::npos)
      << "synthetic compiler.used roots must not pin runtime definitions";
}

TEST_F(BasicTest, BuiltinString) {
  compileRunAndCheck("test_neverc_string",
                     (testDir() / "string" / "test_neverc_string.c").string(),
                     kStrFlags, 0, "test_neverc_string: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringEncoding) {
  compileRunAndCheck(
      "test_neverc_string_encoding",
      (testDir() / "string" / "test_neverc_string_encoding.c").string(),
      kStrFlags, 0, "test_neverc_string_encoding: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringWideLiteral) {
  auto src =
      (testDir() / "string" / "test_neverc_string_wide_literal.c").string();
  compileRunAndCheck("test_neverc_string_wide_literal", src, kStrFlags, 0,
                     "test_neverc_string_wide_literal: ALL PASSED");
  compileRunAndCheck("test_neverc_string_wide_literal_g", src, kStrGFlags, 0,
                     "test_neverc_string_wide_literal: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringNamespace) {
  auto src =
      (testDir() / "string" / "test_neverc_string_namespace.c").string();
  compileRunAndCheck("test_neverc_string_namespace", src, kStrFlags, 0,
                     "test_neverc_string_namespace: ALL PASSED");
  compileRunAndCheck("test_neverc_string_namespace_g", src, kStrGFlags, 0,
                     "test_neverc_string_namespace: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringEncodingDebug) {
  compileRunAndCheck(
      "test_neverc_string_encoding_g",
      (testDir() / "string" / "test_neverc_string_encoding.c").string(),
      kStrGFlags, 0, "test_neverc_string_encoding: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringSplit) {
  auto src = (testDir() / "string" / "test_neverc_string_split.c").string();
  compileRunAndCheck("test_neverc_string_split", src, kStrFlags, 0,
                     "test_neverc_string_split: ALL PASSED");
  compileRunAndCheck("test_neverc_string_split_g", src, kStrGFlags, 0,
                     "test_neverc_string_split: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringFuzz) {
  auto src = (testDir() / "string" / "test_neverc_string_fuzz.c").string();
  compileRunAndCheck("test_neverc_string_fuzz", src, kStrFlags, 0,
                     "test_neverc_string_fuzz: ALL PASSED");
  compileRunAndCheck("test_neverc_string_fuzz_g", src, kStrGFlags, 0,
                     "test_neverc_string_fuzz: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringCrossCompilesWithPcgOnCOFF) {
  const auto src =
      (testDir() / "string" / "test_neverc_string_fuzz.c").string();

  for (const char *triple :
       {"x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc"}) {
    SCOPED_TRACE(triple);
    auto obj = tmpFile(std::string("string_pcg_") + triple + ".obj");
    auto r = ncc({
        std::string("--target=") + triple,
        "-O0",
        "-std=c23",
        "-fbuiltin-string",
        "-fno-lto",
        "-c",
        "-mllvm",
        "-neverc-pcg-min-funcs=2",
        "-mllvm",
        "-neverc-pcg-weight-floor=1",
        "-mllvm",
        "-neverc-pcg-cg-weight-div=1",
        src,
        "-o",
        obj.string(),
    });
    ASSERT_EQ(r.exitCode, 0) << r.err;
    EXPECT_TRUE(fs::exists(obj) && fileSize(obj) > 0);
    EXPECT_NE(readFile(obj).find(".__pcg"), std::string::npos)
        << "the regression must exercise merged parallel codegen";
  }
}

// A retained definition holds against --gc-sections only because codegen
// flags the section its body lands in, and it only knows to do that from
// llvm.used.  That list is an appending global, so parallel codegen keeps it
// in one partition while binning bodies across all of them: unless each
// partition inherits the entries for the definitions it actually emits, every
// retained function binned elsewhere quietly loses its hold.  ELF puts each
// flagged definition in its own section, so the section names are the
// observable form of the marker.
TEST_F(BasicTest, RetainedDefinitionsSurviveParallelCodegen) {
  constexpr unsigned Retained = 12;
  std::string source;
  for (unsigned Index = 0; Index != Retained; ++Index) {
    const std::string N = std::to_string(Index);
    source += "__attribute__((used, retain)) static int keep" + N +
              "(void) { return " + N + "; }\n";
  }
  source += "int main(void) { return 0; }\n";
  auto src = tmpFile("pcg_retain.c");
  writeFile(src, source);

  for (const char *triple :
       {"x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu"}) {
    SCOPED_TRACE(triple);
    auto obj = tmpFile(std::string("pcg_retain_") + triple + ".o");
    auto r =
        ncc({std::string("--target=") + triple, "-O0", "-std=c11", "-fno-lto",
             "-c", "-mllvm", "-neverc-pcg-min-funcs=2", "-mllvm",
             "-neverc-pcg-weight-floor=1", "-mllvm",
             "-neverc-pcg-cg-weight-div=1", src.string(), "-o", obj.string()});
    ASSERT_EQ(r.exitCode, 0) << r.err;

    const std::string bytes = readFile(obj);
    EXPECT_NE(bytes.find(".__pcg"), std::string::npos)
        << "the regression must exercise merged parallel codegen";
    for (unsigned Index = 0; Index != Retained; ++Index)
      EXPECT_NE(bytes.find(".text.keep" + std::to_string(Index)),
                std::string::npos)
          << "retained definition keep" << Index
          << " lost its section after the partition merge";
  }
}

TEST_F(BasicTest, BuiltinStringFormat) {
  auto src = (testDir() / "string" / "test_neverc_string_format.c").string();
  compileRunAndCheck("test_neverc_string_format", src, kStrFlags, 0,
                     "test_neverc_string_format: ALL PASSED");
  compileRunAndCheck("test_neverc_string_format_g", src, kStrGFlags, 0,
                     "test_neverc_string_format: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringFormatOOM) {
  auto src =
      (testDir() / "string" / "test_neverc_string_format_oom.c").string();
  std::string flags =
      std::string(kStrFlags) +
      " -DNEVERC_STRING_ALLOC=__neverc_oom_alloc"
      " -DNEVERC_STRING_FREE=__neverc_oom_free";
  compileRunAndCheck("test_neverc_string_format_oom", src, flags, 0,
                     "test_neverc_string_format_oom: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringWebcodec) {
  auto src =
      (testDir() / "string" / "test_neverc_string_webcodec.c").string();
  compileRunAndCheck("test_neverc_string_webcodec", src, kStrFlags, 0,
                     "test_neverc_string_webcodec: ALL PASSED");
  compileRunAndCheck("test_neverc_string_webcodec_g", src, kStrGFlags, 0,
                     "test_neverc_string_webcodec: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringIC) {
  auto src = (testDir() / "string" / "test_neverc_string_ic.c").string();
  compileRunAndCheck("test_neverc_string_ic", src, kStrFlags, 0,
                     "test_neverc_string_ic: ALL PASSED");
  compileRunAndCheck("test_neverc_string_ic_g", src, kStrGFlags, 0,
                     "test_neverc_string_ic: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringAlias) {
  auto src = (testDir() / "string" / "test_neverc_string_alias.c").string();
  compileRunAndCheck("test_neverc_string_alias", src, kStrFlags, 0,
                     "test_neverc_string_alias: ALL PASSED");
  compileRunAndCheck("test_neverc_string_alias_g", src, kStrGFlags, 0,
                     "test_neverc_string_alias: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringQStringParity) {
  auto src =
      (testDir() / "string" / "test_neverc_string_qstring_parity.c").string();
  compileRunAndCheck("test_neverc_string_qstring_parity", src, kStrFlags, 0,
                     "test_neverc_string_qstring_parity: ALL PASSED");
  compileRunAndCheck("test_neverc_string_qstring_parity_g", src, kStrGFlags, 0,
                     "test_neverc_string_qstring_parity: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringOps) {
  auto src = (testDir() / "string" / "test_neverc_string_ops.c").string();
  compileRunAndCheck("test_neverc_string_ops", src, kStrFlags, 0,
                     "test_neverc_string_ops: ALL PASSED");
  compileRunAndCheck("test_neverc_string_ops_g", src, kStrGFlags, 0,
                     "test_neverc_string_ops: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringEncrypt) {
  auto src =
      (testDir() / "string" / "test_neverc_string_encrypt.c").string();
  compileRunAndCheck("test_neverc_string_encrypt", src, kStrFlags, 0,
                     "test_neverc_string_encrypt: ALL PASSED");
  compileRunAndCheck("test_neverc_string_encrypt_g", src, kStrGFlags, 0,
                     "test_neverc_string_encrypt: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringEncryptUnicode) {
  auto src =
      (testDir() / "string" / "test_neverc_string_encrypt_unicode.c").string();
  compileRunAndCheck("test_neverc_string_encrypt_unicode", src, kStrFlags, 0,
                     "test_neverc_string_encrypt_unicode: ALL PASSED");
  compileRunAndCheck("test_neverc_string_encrypt_unicode_g", src, kStrGFlags, 0,
                     "test_neverc_string_encrypt_unicode: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringEncryptNonLiteral) {
  auto src = tmpFile("test_encrypt_non_literal.c");
  writeFile(src, "int main(void) { string s = \"hi\"; string e = s.encrypt(); "
                 "return 0; }");
  auto args = splitFlags(std::string(kStrFlags));
  args.push_back("-fsyntax-only");
  for (auto &f : sysrootFlags()) args.push_back(f);
  for (auto &f : archFlags()) args.push_back(f);
  args.push_back(src.string());
  expectCommandFail("test_encrypt_non_literal",
                    ".encrypt() can only be applied to string literals", args);
}

TEST_F(BasicTest, BuiltinStringEncryptArgs) {
  auto src = tmpFile("test_encrypt_args.c");
  writeFile(src,
            "int main(void) { string e = \"hi\".encrypt(42); return 0; }");
  auto args = splitFlags(std::string(kStrFlags));
  args.push_back("-fsyntax-only");
  for (auto &f : sysrootFlags()) args.push_back(f);
  for (auto &f : archFlags()) args.push_back(f);
  args.push_back(src.string());
  expectCommandFail("test_encrypt_args", ".encrypt() takes no arguments",
                    args);
}

TEST_F(BasicTest, BuiltinStringEncryptDoubleEncrypt) {
  auto src = tmpFile("test_encrypt_double.c");
  writeFile(src,
            "int main(void) { string e = \"hi\".encrypt().encrypt(); "
            "return 0; }");
  auto args = splitFlags(std::string(kStrFlags));
  args.push_back("-fsyntax-only");
  for (auto &f : sysrootFlags()) args.push_back(f);
  for (auto &f : archFlags()) args.push_back(f);
  args.push_back(src.string());
  expectCommandFail("test_encrypt_double",
                    ".encrypt() can only be applied to string literals", args);
}

TEST_F(BasicTest, BuiltinStringEncryptCliKey) {
  auto src =
      (testDir() / "string" / "test_neverc_string_encrypt.c").string();
  std::string keyFlags =
      std::string(kStrFlags) + " -fstring-encrypt-key=0xDEADBEEF";
  compileRunAndCheck("test_neverc_string_encrypt_key", src, keyFlags, 0,
                     "test_neverc_string_encrypt: ALL PASSED");
}

TEST_F(BasicTest, BuiltinStringKernel) {
  std::string flags =
      std::string(kStrFlags) +
      " -DNEVERC_STRING_ALLOC=kernel_alloc -DNEVERC_STRING_FREE=kernel_free";
  compileRunAndCheck("test_neverc_string_kernel",
                     (testDir() / "string" / "test_neverc_string_kernel.c")
                         .string(),
                     flags, 0, "");
}

TEST_F(BasicTest, BuiltinStringInvalid) {
  auto src =
      (testDir() / "string" / "test_neverc_string_invalid.c").string();
  auto args = splitFlags(std::string(kStrFlags));
  args.push_back("-fsyntax-only");
  for (auto &f : sysrootFlags()) args.push_back(f);
  for (auto &f : archFlags()) args.push_back(f);
  args.push_back(src);
  expectCommandFail("test_neverc_string_invalid",
                    "cannot take c_str/data from a temporary string", args);
}

TEST_F(BasicTest, BuiltinStringDCE) {
  if (isWindows()) {
    GTEST_SKIP() << "DCE symbol check relies on nm, which is not a reliable "
                    "standard tool on Windows";
    return;
  }
  auto dceSrc = tmpFile("neverc_string_dce.c");
  writeFile(dceSrc, R"(int main(void) {
    string s = "123";
    if (s == "123") return 0;
    return 1;
})");

  std::vector<std::string> forbidden = {
      "neverc_string_to_base64",   "neverc_string_from_base64",
      "neverc_string_to_hex",      "neverc_string_from_hex",
      "neverc_string_to_utf16",    "neverc_string_to_utf32",
      "neverc_string_to_latin1",   "neverc_string_html_escape",
      "neverc_string_json_escape", "neverc_string_csv_escape",
      "neverc_string_url_encode",  "neverc_string_form_encode",
      "neverc_string_format",
  };

  for (auto opt : {"-O0", "-O1", "-O2", "-Os"}) {
    SCOPED_TRACE(opt);
    auto exe = tmpFile(std::string("neverc_string_dce_") + opt);
    std::vector<std::string> args = {"-std=c23", "-fbuiltin-string"};
    for (auto &f : sysrootFlags()) args.push_back(f);
    for (auto &f : archFlags()) args.push_back(f);
    args.push_back(opt);
    args.push_back(dceSrc.string());
    args.push_back("-o");
    args.push_back(exe.string());
    auto r = ncc(args);
    ASSERT_EQ(r.exitCode, 0) << "DCE " << opt << ": compile\n" << r.err;

    auto nm = exec("nm", {exe.string()});
    for (auto &sym : forbidden) {
      EXPECT_EQ(nm.out.find(sym), std::string::npos)
          << "DCE " << opt << ": leaked symbol " << sym;
    }
  }
}

// ---- .nc extension auto-detection ----

TEST_F(BasicTest, NcExtStringAutoEnabled) {
  auto src = (testDir() / "string" / "test_nc_ext_string.nc").string();
  compileRunAndCheck("test_nc_ext_string", src, "-std=c23", 0,
                     "test_nc_ext_string: ALL PASSED");
}

TEST_F(BasicTest, NcExtStringAutoEnabledDebug) {
  auto src = (testDir() / "string" / "test_nc_ext_string.nc").string();
  compileRunAndCheck("test_nc_ext_string_g", src, "-std=c23 -g", 0,
                     "test_nc_ext_string: ALL PASSED");
}

TEST_F(BasicTest, NcExtNoExplicitFlag) {
  auto ncSrc = tmpFile("no_flag.nc");
  writeFile(ncSrc, R"(#include <stdio.h>
int main(void) {
    string s = "works";
    printf("nc_no_flag: %s\n", s.len == 5 ? "ALL PASSED" : "FAILED");
    return s.len != 5;
})");
  compileRunAndCheck("nc_no_flag", ncSrc.string(), "-std=c23", 0,
                     "nc_no_flag: ALL PASSED");
}

TEST_F(BasicTest, CExtStringRequiresFlag) {
  auto cSrc = tmpFile("c_needs_flag.c");
  writeFile(cSrc, R"(int main(void) {
    string s = "fail";
    return 0;
})");
  auto args = splitFlags("-std=c23 -fsyntax-only");
  for (auto &f : sysrootFlags()) args.push_back(f);
  for (auto &f : archFlags()) args.push_back(f);
  args.push_back(cSrc.string());
  expectCommandFail("c_needs_flag", "undeclared identifier 'string'", args);
}

TEST_F(BasicTest, NcExtDynCodeString) {
  auto ncSrc = tmpFile("nc_dyncode_str.nc");
  writeFile(ncSrc, R"(int dyncode_entry(void) {
    string s = "dyncode";
    if (s.len != 9) return 1;
    if (s != "dyncode") return 1;
    string upper = s.to_upper();
    if (upper != "DYNCODE") return 1;
    return 0;
})");
  auto bin = tmpFile("nc_dyncode_str.bin");
  std::vector<std::string> args = {"-fdyncode", "-std=c23",
                                   ncSrc.string(), "-o", bin.string()};
  auto r = ncc(args);
  EXPECT_EQ(r.exitCode, 0) << "nc+dyncode compile failed\n" << r.err;
  EXPECT_TRUE(fs::exists(bin) && fileSize(bin) > 0)
      << "nc+dyncode binary missing or empty";
}

TEST_F(BasicTest, NcExtDynCodeCrossCompile) {
  auto ncSrc = tmpFile("nc_sc_cross.nc");
  writeFile(ncSrc, R"(int dyncode_entry(void) {
    string s = "cross";
    return s.len != 5;
})");
  static const char *triples[] = {
      "arm64-apple-macos",       "x86_64-apple-macos",
      "aarch64-linux-gnu",       "x86_64-linux-gnu",
      "aarch64-linux-android29", "x86_64-linux-android29",
      "aarch64-pc-windows-msvc", "x86_64-pc-windows-msvc",
  };
  for (auto *triple : triples) {
    SCOPED_TRACE(triple);
    auto bin = tmpFile(std::string("nc_sc_") + triple + ".bin");
    auto r = ncc({"-fdyncode", "-target", triple, ncSrc.string(), "-o",
                  bin.string()});
    EXPECT_TRUE(r.exitCode == 0 && fs::exists(bin) && fileSize(bin) > 0)
        << ".nc dyncode cross-compile " << triple << " failed\n" << r.err;
  }
}

TEST_F(BasicTest, AdvancedC) {
  compileRunAndCheck("test_advanced_c",
                     (testDir() / "standards" / "test_advanced_c.c").string(),
                     "-std=c11", 0, "test_advanced_c: ALL PASSED");
}

TEST_F(BasicTest, LTO) {
  SCOPED_TRACE("test_lto");
  auto ltoDir = testDir() / "lto";
  auto obj1 = tmpFile("lto_main.o");
  auto obj2 = tmpFile("lto_helper.o");
  auto exe = tmpFile("lto_test");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto args1 = base;
  args1.insert(args1.end(), {"-flto", "-c",
                             (ltoDir / "test_lto.c").string(), "-o",
                             obj1.string()});
  ASSERT_EQ(ncc(args1).exitCode, 0) << "LTO: compile main";

  auto args2 = base;
  args2.insert(args2.end(), {"-flto", "-c",
                             (ltoDir / "test_lto_helper.c").string(), "-o",
                             obj2.string()});
  ASSERT_EQ(ncc(args2).exitCode, 0) << "LTO: compile helper";

  auto link = base;
  link.insert(link.end(), {"-flto", obj1.string(), obj2.string(), "-o",
                           exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0) << "LTO: link";

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0) << "LTO: run";
  EXPECT_TRUE(r.contains("test_lto: ALL PASSED")) << "LTO: stdout\n" << r.out;
}

TEST_F(BasicTest, LTOC23) {
  SCOPED_TRACE("test_lto_c23");
  auto ltoDir = testDir() / "lto";
  auto objA = tmpFile("lto_c23_main.o");
  auto objB = tmpFile("lto_c23_lib.o");
  auto exe = tmpFile("lto_c23");

  std::vector<std::string> base = {"-std=c23"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto a1 = base;
  a1.insert(a1.end(), {"-flto", "-c",
                       (ltoDir / "test_lto_c23_main.c").string(), "-o",
                       objA.string()});
  ASSERT_EQ(ncc(a1).exitCode, 0);

  auto a2 = base;
  a2.insert(a2.end(), {"-flto", "-c",
                       (ltoDir / "test_lto_c23_lib.c").string(), "-o",
                       objB.string()});
  ASSERT_EQ(ncc(a2).exitCode, 0);

  auto link = base;
  link.insert(link.end(), {"-flto", objA.string(), objB.string(), "-o",
                           exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("test_lto_c23: ALL PASSED"));
}

TEST_F(BasicTest, BasicLTO) {
  SCOPED_TRACE("test_basic LTO");
  auto src = (testDir() / "test_basic.c").string();
  auto obj = tmpFile("test_basic_lto.o");
  auto exe = tmpFile("test_basic_lto");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto c = base;
  c.insert(c.end(), {"-flto", "-c", src, "-o", obj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0);

  auto l = base;
  l.insert(l.end(), {"-flto", obj.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(l).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("test_basic: ALL PASSED"));
}

TEST_F(BasicTest, CompileOnly) {
  auto src = tmpFile("hello.c");
  writeFile(src, "int main(void) { return 0; }");
  compileOnly("compile_only", src.string());
}

// __builtin_verbose_trap: valid usage should pass syntax check.
TEST_F(BasicTest, VerboseTrapBasic) {
  auto src = tmpFile("verbose_trap.c");
  writeFile(src,
            "void f(void) { __builtin_verbose_trap(\"bounds\", \"index out of range\"); }\n");
  syntaxCheck("verbose_trap_basic", src.string(), "c11", hostTriple());
}

// __builtin_verbose_trap: argument containing '$' should be rejected.
TEST_F(BasicTest, VerboseTrapRejectsDollar) {
  auto src = tmpFile("verbose_trap_dollar.c");
  writeFile(src,
            "void f(void) { __builtin_verbose_trap(\"bad$cat\", \"msg\"); }\n");
  auto args = splitFlags("-std=c11 -fsyntax-only");
  for (auto &f : sysrootFlags()) args.push_back(f);
  for (auto &f : archFlags()) args.push_back(f);
  args.push_back(src.string());
  expectCommandFail("verbose_trap_dollar",
                    "must not contain $", args);
}

// __builtin_verbose_trap: debug info contains the synthetic inline frame.
TEST_F(BasicTest, VerboseTrapDebugInfo) {
  auto src = tmpFile("verbose_trap_dbg.c");
  writeFile(src,
            "void f(void) { __builtin_verbose_trap(\"cat\", \"msg\"); }\n");
  auto ll = tmpFile("verbose_trap_dbg.ll");
  auto r = ncc({"-std=c11", "-S", "-emit-llvm", "-g", "-o", ll.string(),
                src.string()});
  ASSERT_EQ(r.exitCode, 0) << r.err;
  std::string ir = readFile(ll);
  EXPECT_TRUE(ir.find("__neverc_trap_msg$cat$msg") != std::string::npos)
      << "expected synthetic trap subprogram in IR\n" << ir.substr(0, 500);
}

class LeaksGateTest : public NeverCTest {
protected:
  // leaks(1) ships with every macOS but can only attach when the host allows
  // debugging, and when it cannot it blocks forever instead of failing. Bound
  // the wait so an unusable leaks costs seconds instead of the suite timeout.
  //
  // leaks stops the target while it inspects it, so killing leaks alone would
  // strand the target stopped and holding its inherited stdout open, blocking
  // the caller anyway. The report therefore goes to a file rather than down
  // the pipe, and the timeout takes the target down with leaks.
  //
  // Returns the report, or nothing if the run had to be killed.
  std::optional<std::string> runLeaks(const fs::path &exe, int seconds) const {
    auto report = tmpFile("_leaks_report.txt");
    // Job control puts leaks and the target it spawns in their own process
    // group, so the timeout can take down both by group id. Matching on the
    // target's path instead would also match this script, which carries that
    // path in its own command line.
    const std::string script =
        "set -m\n"
        "leaks --atExit -- '" + exe.string() + "'"
        " > '" + report.string() + "' 2>&1 & p=$!\n"
        "( sleep " + std::to_string(seconds) +
        "; kill -9 -$p 2>/dev/null ) >/dev/null 2>&1 & w=$!\n"
        "wait $p; s=$?\n"
        "kill $w 2>/dev/null\n"
        "exit $s\n";
    auto r = exec("/bin/sh", {"-c", script});
    if (r.exitCode == 128 + 9 || r.exitCode < 0)
      return std::nullopt;
    return readFile(report);
  }
};

TEST_F(LeaksGateTest, BuiltinStringRuntimeIsLeakFree) {
  if (!isDarwin())
    GTEST_SKIP() << "leaks gate requires macOS";
  if (exec("which", {"leaks"}).exitCode != 0)
    GTEST_SKIP() << "leaks(1) not available";

  // Whether leaks can attach at all is a property of the host, so establish it
  // once on a trivial program rather than discovering it target by target.
  auto probeSrc = tmpFile("leaks_probe.c");
  writeFile(probeSrc, "int main(void) { return 0; }\n");
  auto probeExe = tmpFile("leaks_probe");
  std::vector<std::string> probeArgs;
  for (auto &f : sysrootFlags()) probeArgs.push_back(f);
  for (auto &f : archFlags()) probeArgs.push_back(f);
  probeArgs.push_back(probeSrc.string());
  probeArgs.push_back("-o");
  probeArgs.push_back(probeExe.string());
  auto probeBuild = ncc(probeArgs);
  ASSERT_EQ(probeBuild.exitCode, 0) << probeBuild.err;
  if (!runLeaks(probeExe, 30))
    GTEST_SKIP() << "leaks(1) cannot attach on this host";

  auto stringDir = testDir() / "string";
  std::vector<std::string> targets = {
      "test_neverc_string",        "test_neverc_string_encoding",
      "test_neverc_string_namespace", "test_neverc_string_split",
      "test_neverc_string_fuzz",   "test_neverc_string_format",
      "test_neverc_string_webcodec", "test_neverc_string_ic",
      "test_neverc_string_alias",  "test_neverc_string_qstring_parity",
      "test_neverc_string_ops",    "test_neverc_string_wide_literal",
      "test_neverc_string_encrypt", "test_neverc_string_encrypt_unicode",
      "test_neverc_string_composite_cleanup",
  };

  for (auto &t : targets) {
    SCOPED_TRACE("leaks_" + t);
    auto src = (stringDir / (t + ".c")).string();
    auto exe = tmpFile(t);
    std::vector<std::string> args = splitFlags(kStrFlags);
    for (auto &f : sysrootFlags()) args.push_back(f);
    for (auto &f : archFlags()) args.push_back(f);
    args.push_back(src);
    args.push_back("-o");
    args.push_back(exe.string());
    // A target that stops building must fail here; continuing would retire its
    // leak coverage without saying so.
    auto cr = ncc(args);
    ASSERT_EQ(cr.exitCode, 0) << cr.err;

    // The probe proved leaks works here, so a stall now is a real defect.
    auto report = runLeaks(exe, 120);
    ASSERT_TRUE(report.has_value()) << "leaks_" << t << ": leaks(1) never returned";
    EXPECT_TRUE(report->find("0 leaks for 0 total leaked bytes") != std::string::npos ||
                report->find("0 leaks") != std::string::npos)
        << "leaks_" << t << ": leaks reported\n" << *report;
  }
}
