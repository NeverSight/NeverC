#include "NeverCTestFixture.h"

#include <iterator>
#include <regex>
#include <tuple>

class X86PrivilegedIntrinTest : public NeverCTest {
protected:
  std::pair<int, std::string> compileToAsm(const char *src, const char *stem,
                                           const char *opt = "-O2") {
    auto srcFile = tmpFile(std::string(stem) + ".c");
    writeFile(srcFile, src);
    auto outFile = tmpFile(std::string(stem) + ".s");
    // -fno-builtin-mimalloc: these tests scan the whole assembly listing for
    // the presence or absence of specific instructions and of "#APP", and the
    // allocator that is otherwise injected by default contributes hundreds of
    // functions with inline asm of their own.
    auto r =
        ncc({"--target=x86_64-pc-windows-msvc", "-fno-builtin-mimalloc",
             "-ffreestanding", "-fno-lto", "-mllvm", "-verify-machineinstrs",
             opt, "-S", srcFile.string(), "-o", outFile.string()});
    if (r.exitCode != 0) return {r.exitCode, r.err};
    return {0, readFile(outFile)};
  }
};

TEST_F(X86PrivilegedIntrinTest,
       IntrinsicRenumberingPreservesChainlessPatterns) {
  constexpr const char *Source = R"(
    typedef long long Pair __attribute__((vector_size(16)));
    __attribute__((target("pclmul")))
    Pair multiply(Pair x, Pair y) {
      return __builtin_ia32_pclmulqdq128(x, y, 0);
    }
    __attribute__((target("crc32")))
    unsigned int checksum(unsigned int x, unsigned int y) {
      return __builtin_ia32_crc32si(x, y);
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto [RC, Assembly] = compileToAsm(Source, "chainless_intrinsics", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    EXPECT_NE(Assembly.find("\tpclmulqdq"), std::string::npos) << Assembly;
    EXPECT_NE(Assembly.find("\tcrc32"), std::string::npos) << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest,
       APXNativeAtomicInterfacesKeepEncodingAndEffects) {
  const auto Source = tmpFile("apx_native.c");
  writeFile(Source, R"(
    #if !defined(__NEVERC_X86_APX_ATOMIC__)
    #error Missing APX native atomic capability
    #endif
    unsigned long long probe(unsigned long long address, unsigned long long a,
                              unsigned long long b) {
      __builtin_ia32_aadd32_address(address, (unsigned)a, 32, 0x64);
      __builtin_ia32_aadd64_address(address, a, 64, 0);
      __builtin_ia32_aand32_address(address, (unsigned)a, 64, 0);
      __builtin_ia32_aand64_address(address, a, 64, 0);
      __builtin_ia32_aor32_address(address, (unsigned)a, 64, 0);
      __builtin_ia32_aor64_address(address, a, 64, 0);
      __builtin_ia32_axor32_address(address, (unsigned)a, 64, 0);
      __builtin_ia32_axor64_address(address, a, 64, 0);
      (void)__builtin_ia32_cmpccxadd32_address(address, a, b, 4, 32, 0x65);
      (void)__builtin_ia32_cmpccxadd32_address(address, a, b, 4, 32, 0x65);
      return __builtin_ia32_cmpccxadd64_address(address, a, b, 15, 64, 0);
    }
  )");
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (bool IR : {true, false}) {
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        const auto Output = tmpFile(IR ? "apx_native.ll" : "apx_native.s");
        std::vector<std::string> Args = {std::string("--target=") + Target,
                                         Opt,
                                         "-ffreestanding",
                                         "-fno-builtin-mimalloc",
                                         "-fno-lto",
                                         "-masm=att",
                                         "-mllvm",
                                         "-verify-machineinstrs",
                                         "-S",
                                         Source.string(),
                                         "-o",
                                         Output.string()};
        if (IR)
          Args.push_back("-emit-llvm");
        const auto R = ncc(Args);
        ASSERT_EQ(R.exitCode, 0) << R.err;
        const auto Text = readFile(Output);
        EXPECT_EQ(Text.find(IR ? " asm " : "#APP"), std::string::npos);
        for (const char *Op : {"aadd", "aand", "aor", "axor", "cmpccxadd"})
          for (unsigned Bits : {32u, 64u})
            if (IR)
              EXPECT_NE(Text.find("@llvm.x86." + std::string(Op) +
                                  std::to_string(Bits) + ".address("),
                        std::string::npos)
                  << Text;
        if (!IR) {
          EXPECT_NE(Text.find("{evex}"), std::string::npos) << Text;
          EXPECT_NE(Text.find("%fs:(%ebx)"), std::string::npos) << Text;
          EXPECT_NE(Text.find("%gs:(%ebx)"), std::string::npos) << Text;
          const std::regex Query("cmpexadd");
          EXPECT_EQ(std::distance(
                        std::sregex_iterator(Text.begin(), Text.end(), Query),
                        std::sregex_iterator()),
                    2)
              << Text;
        }
      }
}

TEST_F(X86PrivilegedIntrinTest, APXNativeAtomicRejectsInvalidControls) {
  for (const char *Call :
       {"__builtin_ia32_aadd32_address(a, b, 16, 0)",
        "__builtin_ia32_aadd64_address(a, b, 64, 99)",
        "__builtin_ia32_axor64_address(a, b, n, 0)",
        "__builtin_ia32_cmpccxadd32_address(a, b, b, 16, 64, 0)",
        "__builtin_ia32_cmpccxadd64_address(a, b, b, n, 64, 0)",
        "__builtin_ia32_cmpccxadd64_address(a, b, b, 0, 64, n)"}) {
    const auto Source = "void f(unsigned long long a, unsigned long long b, "
                        "unsigned n) { (void)" +
                        std::string(Call) + "; }";
    const auto [Status, Error] = compileToAsm(Source.c_str(), "invalid_apx");
    EXPECT_NE(Status, 0) << Call << "\n" << Error;
    EXPECT_TRUE(Error.find("constant integer") != std::string::npos ||
                Error.find("outside the valid range") != std::string::npos ||
                Error.find("memory address") != std::string::npos)
        << Error;
  }
}

TEST_F(X86PrivilegedIntrinTest, PortableScalarSystemBuiltinsKeepNativeEffects) {
  const auto Source = tmpFile("scalar_system.c");
  writeFile(Source, R"(
    unsigned long long io(unsigned short port, unsigned index,
                          unsigned long long value) {
      __builtin_ia32_wrmsr(index, value);
      __builtin_ia32_outbyte(port, value);
      __builtin_ia32_outword(port, value);
      __builtin_ia32_outdword(port, value);
      return __builtin_ia32_rdmsr(index) ^ __builtin_ia32_inbyte(port) ^
             __builtin_ia32_inword(port) ^ __builtin_ia32_indword(port);
    }
    void unused(void) {
      __builtin_ia32_rdmsr(0xc0000080);
      __builtin_ia32_inbyte(0x1234);
      __builtin_ia32_inbyte(0x1234);
    }
  )");
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      for (bool EmitIR : {true, false}) {
        auto Output = tmpFile(EmitIR ? "scalar_system.ll" : "scalar_system.s");
        std::vector<std::string> Args = {std::string("--target=") + Target,
                                         "-std=c23",
                                         "-ffreestanding",
                                         "-fno-builtin-mimalloc",
                                         "-fno-lto",
                                         "-mllvm",
                                         "-verify-machineinstrs",
                                         Opt,
                                         "-S",
                                         Source.string(),
                                         "-o",
                                         Output.string()};
        if (EmitIR)
          Args.push_back("-emit-llvm");
        auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        if (EmitIR) {
          for (const char *Name : {"rdmsr", "wrmsr", "inbyte", "inword",
                                   "indword", "outbyte", "outword", "outdword"})
            EXPECT_NE(Text.find(std::string("@llvm.x86.") + Name + "("),
                      std::string::npos)
                << Text;
          const std::regex Read("call i8 @llvm\\.x86\\.inbyte\\(i16 4660\\)");
          EXPECT_EQ(std::distance(
                        std::sregex_iterator(Text.begin(), Text.end(), Read),
                        std::sregex_iterator()),
                    2)
              << Text;
          EXPECT_EQ(Text.find(" asm "), std::string::npos) << Text;
        } else {
          EXPECT_NE(Text.find("\trdmsr"), std::string::npos) << Text;
          EXPECT_NE(Text.find("\twrmsr"), std::string::npos) << Text;
          EXPECT_EQ(Text.find("#APP"), std::string::npos) << Text;
        }
      }
    }
}

TEST_F(X86PrivilegedIntrinTest, MpxUsesNativeBoundsBankAndPreservesBranches) {
  constexpr const char *Source = R"(
    void bounds(unsigned long long base, unsigned long long value,
                volatile unsigned *predicate) {
      __builtin_ia32_bndmk(0, base, value, 32, 0x64);
      __builtin_ia32_bndmov_load(1, base, 64, 0x65);
      __builtin_ia32_bndmov_reg(2, 1);
      __builtin_ia32_bndcl_reg(2, value);
      __builtin_ia32_bndcu_reg(2, value);
      __builtin_ia32_bndcn_reg(2, value);
      __builtin_ia32_bndcl_address(2, base, 32, 0x26);
      __builtin_ia32_bndcu_address(2, base, 64, 0x36);
      __builtin_ia32_bndcn_address(2, base, 64, 0x3e);
      __builtin_ia32_bndldx(3, base, value, 64, 0);
      __builtin_ia32_bndstx(3, base, value, 64, 0);
      if (*predicate) __builtin_ia32_bndmov_reg(2, 0);
      __builtin_ia32_bndmov_store(2, base, 64, 0);
      __builtin_ia32_bndmov_store_lock(2, base, 64, 0);
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto [RC, Assembly] = compileToAsm(Source, "mpx_native", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    for (const char *Name :
         {"bndmk", "bndmov", "bndcl", "bndcu", "bndcn", "bndldx", "bndstx"})
      EXPECT_NE(Assembly.find(std::string("\t") + Name + "\t"),
                std::string::npos)
          << Assembly;
    EXPECT_NE(Assembly.find("lock"), std::string::npos) << Assembly;
    EXPECT_TRUE(std::regex_search(Assembly, std::regex("repne[ \\t]+j")))
        << Assembly;
    EXPECT_TRUE(std::regex_search(Assembly, std::regex("repne[ \\t]+ret")))
        << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
    EXPECT_FALSE(std::regex_search(Assembly, std::regex("\\bxmm[0-9]+\\b")))
        << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest, MpxRejectsInvalidBankAndAddressControls) {
  for (const char *Call :
       {"bnd_branch(2)", "bnd_branch(-1)", "bnd_branch(index)",
        "bndmk(4, base, base, 64, 0)", "bndmov_reg(0, 4)",
        "bndmov_reg(index, 0)", "bndcl_reg(256, base)", "bndcu_reg(-1, base)",
        "bndcn_reg(index, base)", "bndmov_load(0, base, 16, 0)",
        "bndmov_store(0, base, 0, 0)", "bndmov_store_lock(0, base, 64, 0xf0)",
        "bndcl_address(0, base, 64, 0x67)", "bndcu_address(0, base, 64, index)",
        "bndcn_address(0, base, index, 0)", "bndldx(0, base, base, 128, 0)",
        "bndstx(0, base, base, 64, -1)"}) {
    SCOPED_TRACE(Call);
    const std::string Source =
        "void bad(unsigned index, unsigned long long base) { __builtin_ia32_" +
        std::string(Call) + "; }";
    auto [RC, Error] = compileToAsm(Source.c_str(), "mpx_invalid");
    EXPECT_NE(RC, 0) << Error;
    EXPECT_TRUE(Error.find("outside the valid range") != std::string::npos ||
                Error.find("constant integer") != std::string::npos ||
                Error.find("native memory addressing") != std::string::npos)
        << Error;
  }
}

TEST_F(X86PrivilegedIntrinTest, MpxOriginalBranchEffectsSurviveOptimization) {
  constexpr const char *Source = R"(
    void preserve(void) { __builtin_ia32_bnd_branch(1); }
    void initialize(volatile unsigned *memory) {
      *memory = 1;
      __builtin_ia32_bnd_branch(0);
      __builtin_ia32_bnd_branch(0);
      *memory = 2;
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    const auto [RC, Assembly] = compileToAsm(Source, "mpx_branch", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    const auto First = Assembly.find("{disp32}");
    const auto Last = Assembly.rfind("{disp32}");
    ASSERT_NE(First, std::string::npos) << Assembly;
    EXPECT_NE(First, Last) << Assembly;
    EXPECT_LT(Assembly.find("], 1"), First) << Assembly;
    EXPECT_GT(Assembly.find("], 2"), Last) << Assembly;
    EXPECT_TRUE(
        std::regex_search(Assembly.substr(Assembly.find("preserve:"),
                                          Assembly.find("initialize:") -
                                              Assembly.find("preserve:")),
                          std::regex("repne[ \\t]+ret")))
        << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest, MpxChecksCannotBeEliminatedOrReordered) {
  constexpr const char *Source = R"(
    void checks(unsigned long long value, unsigned *memory) {
      *memory = 1;
      __builtin_ia32_bndcl_reg(3, value);
      __builtin_ia32_bndcl_reg(3, value);
      *memory = 2;
    }
  )";
  auto [RC, Assembly] = compileToAsm(Source, "mpx_order");
  ASSERT_EQ(RC, 0) << Assembly;
  const std::regex Instruction("\\tbndcl\\t");
  EXPECT_EQ(std::distance(std::sregex_iterator(Assembly.begin(), Assembly.end(),
                                               Instruction),
                          std::sregex_iterator()),
            2)
      << Assembly;
  const auto First = Assembly.find("\tbndcl\t");
  const auto Last = Assembly.rfind("\tbndcl\t");
  EXPECT_LT(Assembly.find("], 1"), First) << Assembly;
  EXPECT_GT(Assembly.find("], 2"), Last) << Assembly;
}

TEST_F(X86PrivilegedIntrinTest, AmxPublicHeaderExposesPhysicalTileInterfaces) {
  auto Source = tmpFile("amx_header.c");
  auto Assembly = tmpFile("amx_header.s");
  writeFile(Source, R"(
    #include <immintrin.h>
    void f(void *out, const void *config, unsigned long long stride,
           unsigned row) {
      _tile_loadconfig(config);
      _tile_storeconfig(out);
      _tile_zero(0);
      _tile_loadd(1, config, stride);
      _tile_stream_loadd(2, config, stride);
      _tile_loaddrs(3, config, stride);
      _tile_stream_loaddrs(4, config, stride);
      _tile_dpbssd(0, 1, 2);
      _tile_dpbsud(0, 1, 2);
      _tile_dpbusd(0, 1, 2);
      _tile_dpbuud(0, 1, 2);
      _tile_dpbf16ps(0, 1, 2);
      _tile_dpfp16ps(0, 1, 2);
      _tile_cmmimfp16ps(0, 1, 2);
      _tile_cmmrlfp16ps(0, 1, 2);
      _tile_dpbf8ps(0, 1, 2);
      _tile_dpbhf8ps(0, 1, 2);
      _tile_dphbf8ps(0, 1, 2);
      _tile_dphf8ps(0, 1, 2);
      _tile_mmultf32ps(0, 1, 2);
      ((__m512 *)out)[0] = _tile_cvtrowd2ps(0, row);
      ((__m512 *)out)[1] = _tile_cvtrowd2psi(1, 255);
      ((__m512bh *)out)[2] = _tile_cvtrowps2bf16h(2, row);
      ((__m512bh *)out)[3] = _tile_cvtrowps2bf16hi(3, 255);
      ((__m512bh *)out)[4] = _tile_cvtrowps2bf16l(4, row);
      ((__m512bh *)out)[5] = _tile_cvtrowps2bf16li(5, 255);
      ((__m512h *)out)[6] = _tile_cvtrowps2phh(6, row);
      ((__m512h *)out)[7] = _tile_cvtrowps2phhi(7, 255);
      ((__m512h *)out)[8] = _tile_cvtrowps2phl(0, row);
      ((__m512h *)out)[9] = _tile_cvtrowps2phli(1, 255);
      ((__m512i *)out)[10] = _tile_movrow(2, row);
      ((__m512i *)out)[11] = _tile_movrowi(3, 255);
      _tile_stored(0, out, stride);
      _tile_loadconfig_address(config, 32, 100, 0);
      _tile_storeconfig_address(out, 64, 101, 1);
      _tile_loadd_address(0, config, stride, 32, 54, 1);
      _tile_stream_loadd_address(1, config, stride, 64, 62, 0);
      _tile_loaddrs_address(2, config, stride, 32, 0, 0);
      _tile_stream_loaddrs_address(3, config, stride, 64, 0, 1);
      _tile_stored_address(4, out, stride, 32, 100, 0);
      ((__m512i *)out)[0] = _tile_movrow_reg(5, row, 1);
      ((__m512 *)out)[1] = _tile_cvtrowd2ps_reg(6, row, 0);
      ((__m512bh *)out)[2] = _tile_cvtrowps2bf16h_reg(7, row, 1);
      ((__m512bh *)out)[3] = _tile_cvtrowps2bf16l_reg(0, row, 0);
      ((__m512h *)out)[4] = _tile_cvtrowps2phh_reg(1, row, 1);
      ((__m512h *)out)[5] = _tile_cvtrowps2phl_reg(2, row, 0);
      _tile_release();
    }
  )");
  for (const char *Triple :
       {"x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu",
        "x86_64-apple-darwin"}) {
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Triple);
      SCOPED_TRACE(Opt);
      auto Result = ncc({std::string("--target=") + Triple,
                         "-ffreestanding",
                         "-fno-builtin-mimalloc",
                         "-fno-lto",
                         "-Werror",
                         "-mamx-avx512",
                         "-mavx10.2",
                         "-mamx-int8",
                         "-mamx-bf16",
                         "-mamx-fp16",
                         "-mamx-complex",
                         "-mamx-fp8",
                         "-mamx-movrs",
                         "-mamx-tf32",
                         "-mapx-features=egpr",
                         "-mllvm",
                         "-verify-machineinstrs",
                         Opt,
                         "-S",
                         Source.string(),
                         "-o",
                         Assembly.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Text = readFile(Assembly);
      EXPECT_EQ(Text.find("#APP"), std::string::npos) << Text;
      EXPECT_NE(Text.find("\ttdpbf8ps\t"), std::string::npos) << Text;
      EXPECT_NE(Text.find("\ttcvtrowps2phl\t"), std::string::npos) << Text;
      EXPECT_NE(Text.find("\ttileloaddrst1\t"), std::string::npos) << Text;
    }
  }
}

TEST_F(X86PrivilegedIntrinTest, AmxComputeAndMovrsUseNativeInstructions) {
  constexpr const char *Source = R"(
    __attribute__((target("amx-fp8,amx-movrs,amx-tf32")))
    void compute(const void *source, unsigned long long stride) {
      __builtin_ia32_tileloaddrs64(3, source, stride);
      __builtin_ia32_tileloaddrst164(4, source, stride);
      __builtin_ia32_tdpbf8ps(0, 1, 2);
      __builtin_ia32_tdpbhf8ps(3, 4, 5);
      __builtin_ia32_tdphbf8ps(6, 7, 0);
      __builtin_ia32_tdphf8ps(7, 2, 1);
      __builtin_ia32_tmmultf32ps(5, 6, 4);
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto [RC, Assembly] = compileToAsm(Source, "amx_compute", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    for (const char *Name :
         {"tileloaddrs", "tileloaddrst1", "tdpbf8ps", "tdpbhf8ps", "tdphbf8ps",
          "tdphf8ps", "tmmultf32ps"})
      EXPECT_NE(Assembly.find(std::string("\t") + Name + "\t"),
                std::string::npos)
          << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest, AmxExplicitAddressesAndRowsRemainNative) {
  constexpr const char *Source = R"(
    #if !defined(__NEVERC_X86_AMX_CONTROLS__) || __NEVERC_X86_AMX_CONTROLS__ != 1
    #error missing compiler AMX control capability
    #endif
    #if defined(__AMX_TILE__) || defined(__AMX_AVX512__)
    #error compiler capability must not change baseline CPU features
    #endif
    __attribute__((target("amx-tile,amx-movrs,amx-avx512,avx10.2,egpr")))
    void controls(unsigned long long base, unsigned long long stride, unsigned row) {
      __builtin_ia32_ldtilecfg_address(base, 32, 100, 0);
      __builtin_ia32_sttilecfg_address(base, 64, 101, 1);
      __builtin_ia32_tileloadd64_address(7, base, stride, 32, 54, 1);
      __builtin_ia32_tileloaddt164_address(6, base, stride, 64, 62, 0);
      __builtin_ia32_tileloaddrs64_address(5, base, stride, 32, 0, 0);
      __builtin_ia32_tileloaddrst164_address(4, base, stride, 64, 0, 1);
      __builtin_ia32_tilestored64_address(3, base, stride, 32, 100, 0);
      __builtin_ia32_tilemovrow_reg(7, row, 1);
      __builtin_ia32_tcvtrowd2ps_reg(6, row, 0);
      __builtin_ia32_tcvtrowps2bf16h_reg(5, row, 1);
      __builtin_ia32_tcvtrowps2bf16l_reg(4, row, 0);
      __builtin_ia32_tcvtrowps2phh_reg(3, row, 1);
      __builtin_ia32_tcvtrowps2phl_reg(2, row, 0);
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    auto [RC, Assembly] = compileToAsm(Source, "amx_controls", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    EXPECT_NE(Assembly.find("{evex}"), std::string::npos) << Assembly;
    EXPECT_NE(Assembly.find("{vex}"), std::string::npos) << Assembly;
    EXPECT_NE(Assembly.find("r16d"), std::string::npos) << Assembly;
    for (const char *Name :
         {"ldtilecfg", "sttilecfg", "tileloadd", "tileloaddt1", "tileloaddrs",
          "tileloaddrst1", "tilestored", "tilemovrow", "tcvtrowd2ps",
          "tcvtrowps2bf16h", "tcvtrowps2bf16l", "tcvtrowps2phh",
          "tcvtrowps2phl"})
      EXPECT_NE(Assembly.find(Name), std::string::npos) << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest, AmxExtendedConstantRowsSurviveOptimization) {
  const auto Source = tmpFile("amx_constant_rows.c");
  std::string Text;
  for (const char *Name : {"tilemovrow", "tcvtrowd2ps", "tcvtrowps2bf16h",
                           "tcvtrowps2bf16l", "tcvtrowps2phh", "tcvtrowps2phl"})
    for (const char *Row : {"0", "1", "0x80000000", "0xffffffff"}) {
      Text += "__attribute__((target(\"amx-avx512,avx10.2,egpr\"))) void f_";
      Text += std::string(Name) + "_" + Row + "(void) { __builtin_ia32_";
      Text += std::string(Name) + "_reg(0, " + Row + ", 1); }\n";
    }
  writeFile(Source, Text);
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto Object = tmpFile("amx_constant_rows.o");
      const auto Result =
          ncc({std::string("--target=") + Target, "-ffreestanding", "-fno-lto",
               "-fno-builtin-mimalloc", "-mllvm", "-verify-machineinstrs", Opt,
               "-c", Source.string(), "-o", Object.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      EXPECT_FALSE(readFile(Object).empty());
    }
}

TEST_F(X86PrivilegedIntrinTest, AmxExplicitControlsRejectInvalidContracts) {
  for (const char *Call :
       {"__builtin_ia32_ldtilecfg_address(base, 16, 0, 0)",
        "__builtin_ia32_ldtilecfg_address(base, 64, 1, 0)",
        "__builtin_ia32_sttilecfg_address(base, 64, 0, 2)",
        "__builtin_ia32_ldtilecfg_address(base, row, 0, 0)",
        "__builtin_ia32_tileloadd64_address(8, base, base, 64, 0, 0)",
        "__builtin_ia32_tileloadd64_address(row, base, base, 64, 0, 0)",
        "__builtin_ia32_tilemovrow_reg(8, row, 0)",
        "__builtin_ia32_tilemovrow_reg(0, row, 2)",
        "__builtin_ia32_tilemovrow_reg(0, row, row)"}) {
    SCOPED_TRACE(Call);
    const auto Source =
        std::string(
            "__attribute__((target(\"amx-tile,amx-avx512,avx10.2,egpr\"))) ") +
        "void bad(unsigned long long base, unsigned row) { " + Call + "; }";
    auto [RC, Error] = compileToAsm(Source.c_str(), "invalid_amx_controls");
    EXPECT_NE(RC, 0) << Error;
    EXPECT_NE(Error.find("error:"), std::string::npos) << Error;
  }
  for (const char *Call : {"__builtin_ia32_ldtilecfg_address(base, 64, 0, 1)",
                           "__builtin_ia32_tilemovrow_reg(0, row, 1)"}) {
    const auto Source =
        std::string(
            "__attribute__((target(\"amx-tile,amx-avx512,avx10.2\"))) ") +
        "void bad(unsigned long long base, unsigned row) { " + Call + "; }";
    auto [RC, Error] = compileToAsm(Source.c_str(), "missing_amx_egpr");
    EXPECT_NE(RC, 0) << Error;
    EXPECT_NE(Error.find("requires egpr"), std::string::npos) << Error;
  }
}

TEST_F(X86PrivilegedIntrinTest, AmxRowsReturnNative512BitVectors) {
  constexpr const char *Source = R"(
    typedef float FloatRow __attribute__((vector_size(64)));
    typedef __bf16 BfloatRow __attribute__((vector_size(64)));
    typedef _Float16 HalfRow __attribute__((vector_size(64)));
    typedef int IntRow __attribute__((vector_size(64)));
    __attribute__((target("amx-avx512,avx10.2")))
    void rows(unsigned row, FloatRow *f, BfloatRow *b, HalfRow *h, IntRow *i) {
      f[0] = __builtin_ia32_tcvtrowd2ps(0, row);
      f[1] = __builtin_ia32_tcvtrowd2psi(7, 255);
      b[0] = __builtin_ia32_tcvtrowps2bf16h(1, row);
      b[1] = __builtin_ia32_tcvtrowps2bf16hi(6, 0);
      b[2] = __builtin_ia32_tcvtrowps2bf16l(2, row);
      b[3] = __builtin_ia32_tcvtrowps2bf16li(5, 15);
      h[0] = __builtin_ia32_tcvtrowps2phh(3, row);
      h[1] = __builtin_ia32_tcvtrowps2phhi(4, 16);
      h[2] = __builtin_ia32_tcvtrowps2phl(4, row);
      h[3] = __builtin_ia32_tcvtrowps2phli(3, 31);
      i[0] = __builtin_ia32_tilemovrow(5, row);
      i[1] = __builtin_ia32_tilemovrowi(2, 1);
      (void)__builtin_ia32_tilemovrow(5, row);
      (void)__builtin_ia32_tilemovrow(5, row);
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto [RC, Assembly] = compileToAsm(Source, "amx_rows", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    for (const char *Name :
         {"tcvtrowd2ps", "tcvtrowps2bf16h", "tcvtrowps2bf16l", "tcvtrowps2phh",
          "tcvtrowps2phl", "tilemovrow"}) {
      const std::regex Instruction(std::string("\\t") + Name + "\\t");
      const auto Count = std::distance(
          std::sregex_iterator(Assembly.begin(), Assembly.end(), Instruction),
          std::sregex_iterator());
      EXPECT_EQ(Count, std::string(Name) == "tilemovrow" ? 4 : 2) << Assembly;
    }
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest, AmxRowFeaturesAndImmediateOperandsAreChecked) {
  for (const char *Call : {"__builtin_ia32_tilemovrow(8, row)",
                           "__builtin_ia32_tcvtrowd2ps(row, row)",
                           "__builtin_ia32_tcvtrowps2bf16hi(0, row)",
                           "__builtin_ia32_tcvtrowps2phli(0, 256)",
                           "__builtin_ia32_tilemovrowi(0, -1)"}) {
    const std::string Source =
        "__attribute__((target(\"amx-avx512,avx10.2\"))) "
        "void f(unsigned row) { (void)" +
        std::string(Call) + "; }";
    auto [RC, Error] = compileToAsm(Source.c_str(), "amx_invalid_row");
    EXPECT_NE(RC, 0) << Call << '\n' << Error;
    EXPECT_TRUE(Error.find("outside the valid range") != std::string::npos ||
                Error.find("constant integer") != std::string::npos)
        << Error;
  }
  for (const char *Features : {"amx-avx512", "avx10.2"}) {
    const std::string Source = "__attribute__((target(\"" +
                               std::string(Features) +
                               "\"))) void f(unsigned row) { "
                               "(void)__builtin_ia32_tilemovrow(0, row); }";
    auto [RC, Error] = compileToAsm(Source.c_str(), "amx_missing_row_feature");
    EXPECT_NE(RC, 0) << Error;
    EXPECT_NE(Error.find("needs target feature"), std::string::npos) << Error;
  }
}

TEST_F(X86PrivilegedIntrinTest, AmxRowDriverFlagsControlFeaturesAndMacros) {
  auto Source = tmpFile("amx_row_features.c");
  auto Object = tmpFile("amx_row_features.obj");
  writeFile(Source, R"(
    #if !defined(__AMX_AVX512__) || !defined(__AMX_TILE__) || \
        !defined(__AVX10_2__) || !defined(__AVX10_2_512__)
    #error missing row features
    #endif
    void f(unsigned row) { (void)__builtin_ia32_tilemovrow(0, row); }
  )");
  for (const char *Feature : {"amx-avx512", "avx10.2"}) {
    for (bool Enabled : {false, true}) {
      const std::string Enable = std::string("-m") + Feature;
      const std::string Disable = std::string("-mno-") + Feature;
      auto Result = ncc({"--target=x86_64-pc-windows-msvc", "-ffreestanding",
                         "-fno-builtin-mimalloc", "-fno-lto", "-c",
                         "-mamx-avx512", "-mavx10.2",
                         Enabled ? Disable : Enable, Enabled ? Enable : Disable,
                         Source.string(), "-o", Object.string()});
      EXPECT_EQ(Result.exitCode == 0, Enabled) << Result.err;
      if (!Enabled)
        EXPECT_NE(Result.err.find("missing row features"), std::string::npos)
            << Result.err;
    }
  }
}

TEST_F(X86PrivilegedIntrinTest, AmxFeaturesAndTileIndicesAreChecked) {
  for (const char *Call :
       {"__builtin_ia32_tdpbf8ps(0, 0, 1)", "__builtin_ia32_tdpbhf8ps(0, 1, 1)",
        "__builtin_ia32_tdphbf8ps(8, 1, 2)", "__builtin_ia32_tdphf8ps(n, 1, 2)",
        "__builtin_ia32_tmmultf32ps(0, 1, 8)",
        "__builtin_ia32_tileloaddrs64(8, p, 64)",
        "__builtin_ia32_tileloaddrst164(n, p, 64)"}) {
    const std::string Source =
        "__attribute__((target(\"amx-fp8,amx-movrs,amx-tf32\"))) "
        "void f(unsigned n, const void *p) { " +
        std::string(Call) + "; }";
    auto [RC, Error] = compileToAsm(Source.c_str(), "invalid_amx_operand");
    EXPECT_NE(RC, 0) << Call << '\n' << Error;
    EXPECT_TRUE(Error.find("different tiles") != std::string::npos ||
                Error.find("outside the valid range") != std::string::npos ||
                Error.find("constant integer") != std::string::npos)
        << Error;
  }
  for (const char *Call : {"__builtin_ia32_tdpbf8ps(0, 1, 2)",
                           "__builtin_ia32_tmmultf32ps(0, 1, 2)",
                           "__builtin_ia32_tileloaddrs64(0, p, 64)"}) {
    const std::string Source =
        "void f(const void *p) { " + std::string(Call) + "; }";
    auto [RC, Error] = compileToAsm(Source.c_str(), "missing_amx_feature");
    EXPECT_NE(RC, 0) << Error;
    EXPECT_NE(Error.find("needs target feature"), std::string::npos) << Error;
  }
}

TEST_F(X86PrivilegedIntrinTest, AmxPhysicalAndManagedTilesCannotBeMixed) {
  for (const char *Call : {"__builtin_ia32_tdpbf8ps(0, 1, 2)",
                           "__builtin_ia32_tilemovrow(0, 1)"}) {
    const std::string Source =
        "typedef int Tile __attribute__((vector_size(1024))); "
        "__attribute__((target(\"amx-fp8,amx-avx512,avx10.2\"))) "
        "void f(const void *p, void *q) { "
        "Tile t = __builtin_ia32_tileloadd64_internal(2, 16, p, 64); "
        "(void)" +
        std::string(Call) +
        "; "
        "__builtin_ia32_tilestored64_internal(2, 16, q, 64, t); }";
    for (const char *Opt : {"-O0", "-O2"}) {
      auto [RC, Error] = compileToAsm(Source.c_str(), "mixed_amx_models", Opt);
      EXPECT_NE(RC, 0) << Error;
      EXPECT_NE(Error.find("cannot mix physical AMX tile intrinsics"),
                std::string::npos)
          << Error;
    }
  }
}

TEST_F(X86PrivilegedIntrinTest,
       AmxCommandLineFeaturesControlBuiltinsAndMacros) {
  for (const auto &[Feature, Macro, Call] :
       {std::tuple{"amx-fp8", "__AMX_FP8__",
                   "__builtin_ia32_tdpbf8ps(0, 1, 2)"},
        std::tuple{"amx-movrs", "__AMX_MOVRS__",
                   "__builtin_ia32_tileloaddrs64(0, p, 64)"},
        std::tuple{"amx-tf32", "__AMX_TF32__",
                   "__builtin_ia32_tmmultf32ps(0, 1, 2)"}}) {
    SCOPED_TRACE(Feature);
    auto Source = tmpFile("amx_feature.c");
    auto Object = tmpFile("amx_feature.obj");
    writeFile(Source, std::string("#if !defined(") + Macro +
                          ") || !defined(__AMX_TILE__)\n#error missing "
                          "requested feature\n#endif\n"
                          "void f(const void *p) { " +
                          Call + "; }\n");
    const std::string Enable = std::string("-m") + Feature;
    const std::string Disable = std::string("-mno-") + Feature;
    for (bool Enabled : {true, false}) {
      auto Result = ncc({"--target=x86_64-pc-windows-msvc", "-ffreestanding",
                         "-fno-builtin-mimalloc", "-fno-lto", "-c",
                         Enabled ? Disable : Enable, Enabled ? Enable : Disable,
                         Source.string(), "-o", Object.string()});
      EXPECT_EQ(Result.exitCode == 0, Enabled) << Result.err;
      if (!Enabled)
        EXPECT_NE(Result.err.find("missing requested feature"),
                  std::string::npos)
            << Result.err;
    }
  }
}

// ---------------------------------------------------------------------------
// MSR: rdmsr / wrmsr — true intrinsic lowering (no InlineAsm markers)
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, MsrIntrinsicsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned __int64 read_msr(unsigned int addr) { return __readmsr(addr); }
    void write_msr(unsigned int addr, unsigned __int64 val) { __writemsr(addr, val); }
    void write_nonserializing(unsigned int addr, unsigned __int64 val) {
      __builtin_ia32_wrmsrns(addr, val);
    }
    void clear_task_switched(void) { __builtin_ia32_clts(); }
  )",
                              "msr");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("rdmsr"), std::string::npos) << "expected rdmsr\n" << s;
  EXPECT_NE(s.find("wrmsr"), std::string::npos) << "expected wrmsr\n" << s;
  EXPECT_NE(s.find("\twrmsrns"), std::string::npos) << s;
  EXPECT_NE(s.find("\tclts"), std::string::npos) << s;
  EXPECT_EQ(s.find("#APP"), std::string::npos) << "intrinsic path must not use InlineAsm\n" << s;
}

// ---------------------------------------------------------------------------
// CR registers: read + write — write must NOT be DCE'd
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, CrRegisterReadsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned __int64 read_cr0(void) { return __readcr0(); }
    unsigned __int64 read_cr3(void) { return __readcr3(); }
    unsigned __int64 read_cr4(void) { return __readcr4(); }
    unsigned __int64 read_cr8(void) { return __readcr8(); }
  )", "cr_read");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("mov\trax, cr0"), std::string::npos) << "expected mov rax, cr0\n" << s;
  EXPECT_NE(s.find("mov\trax, cr3"), std::string::npos) << "expected mov rax, cr3\n" << s;
  EXPECT_NE(s.find("mov\trax, cr4"), std::string::npos) << "expected mov rax, cr4\n" << s;
  EXPECT_NE(s.find("mov\trax, cr8"), std::string::npos) << "expected mov rax, cr8\n" << s;
}

TEST_F(X86PrivilegedIntrinTest, CrRegisterWritesNotDCEd) {
  auto [rc, s] = compileToAsm(R"(
    void write_cr0(unsigned __int64 v) { __writecr0(v); }
    void write_cr3(unsigned __int64 v) { __writecr3(v); }
    void write_cr4(unsigned __int64 v) { __writecr4(v); }
    void write_cr8(unsigned __int64 v) { __writecr8(v); }
  )", "cr_write");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("mov\tcr0, rcx"), std::string::npos) << "write_cr0 DCE'd or wrong\n" << s;
  EXPECT_NE(s.find("mov\tcr3, rcx"), std::string::npos) << "write_cr3 DCE'd or wrong\n" << s;
  EXPECT_NE(s.find("mov\tcr4, rcx"), std::string::npos) << "write_cr4 DCE'd or wrong\n" << s;
  EXPECT_NE(s.find("mov\tcr8, rcx"), std::string::npos) << "write_cr8 DCE'd or wrong\n" << s;
}

// ---------------------------------------------------------------------------
// DR registers: read + write — write must NOT be DCE'd
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, DrRegisterOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned __int64 read_dr0(void) { return __readdr(0); }
    unsigned __int64 read_dr7(void) { return __readdr(7); }
    void write_dr0(unsigned __int64 v) { __writedr(0, v); }
    void write_dr7(unsigned __int64 v) { __writedr(7, v); }
  )", "dr");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("mov\trax, dr0"), std::string::npos) << "expected mov rax, dr0\n" << s;
  EXPECT_NE(s.find("mov\trax, dr7"), std::string::npos) << "expected mov rax, dr7\n" << s;
  EXPECT_NE(s.find("mov\tdr0, rcx"), std::string::npos) << "write_dr0 DCE'd or wrong\n" << s;
  EXPECT_NE(s.find("mov\tdr7, rcx"), std::string::npos) << "write_dr7 DCE'd or wrong\n" << s;
}

TEST_F(X86PrivilegedIntrinTest, SystemRegisterReadsRemainObservable) {
  constexpr const char *Source = R"(
    void discard(void) {
      __readcr3();
      __readcr3();
      __readdr(4);
      __readdr(4);
    }
    unsigned long long repeated(void) {
      unsigned long long first = __builtin_ia32_readcr(8);
      return first ^ __builtin_ia32_readcr(8);
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto [RC, Assembly] = compileToAsm(Source, "system_reads", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    for (const char *Register : {"cr3", "cr8", "dr4"}) {
      const std::regex Read(std::string("\\tmov\\t[^,\\n]+, ") + Register);
      EXPECT_EQ(std::distance(std::sregex_iterator(Assembly.begin(),
                                                   Assembly.end(), Read),
                              std::sregex_iterator()),
                2)
          << Assembly;
    }
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest, SystemRegisterIndicesAreChecked) {
  for (const char *Source : {"void f(void) { __builtin_ia32_readcr(1); }",
                             "void f(void) { __builtin_ia32_writecr(7, 0); }",
                             "void f(void) { __builtin_ia32_readdr(8); }",
                             "void f(void) { __builtin_ia32_writedr(9, 0); }",
                             "void f(unsigned n) { __readdr(n); }",
                             "void f(void) { __writedr(8, 0); }"}) {
    SCOPED_TRACE(Source);
    auto [RC, Error] = compileToAsm(Source, "invalid_system_register");
    EXPECT_NE(RC, 0) << Error;
    EXPECT_TRUE(Error.find("control register index must be") !=
                    std::string::npos ||
                Error.find("outside the valid range") != std::string::npos ||
                Error.find("constant integer") != std::string::npos)
        << Error;
  }
}

// ---------------------------------------------------------------------------
// Port I/O: in/out 8/16/32 — true intrinsic lowering
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, PortIoIntrinsicsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned char  in8 (unsigned short p) { return __inbyte(p); }
    unsigned short in16(unsigned short p) { return __inword(p); }
    unsigned int   in32(unsigned short p) { return __indword(p); }
    void out8 (unsigned short p, unsigned char  v) { __outbyte(p, v); }
    void out16(unsigned short p, unsigned short v) { __outword(p, v); }
    void out32(unsigned short p, unsigned int   v) { __outdword(p, v); }
  )", "portio");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("in\tal, dx"), std::string::npos) << "expected in al, dx\n" << s;
  EXPECT_NE(s.find("in\tax, dx"), std::string::npos) << "expected in ax, dx\n" << s;
  EXPECT_NE(s.find("in\teax, dx"), std::string::npos) << "expected in eax, dx\n" << s;
  EXPECT_NE(s.find("out\tdx, al"), std::string::npos) << "expected out dx, al\n" << s;
  EXPECT_NE(s.find("out\tdx, ax"), std::string::npos) << "expected out dx, ax\n" << s;
  EXPECT_NE(s.find("out\tdx, eax"), std::string::npos) << "expected out dx, eax\n" << s;
  EXPECT_EQ(s.find("#APP"), std::string::npos) << "intrinsic path must not use InlineAsm\n" << s;
}

// ---------------------------------------------------------------------------
// TLB flush: invlpg
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, InvlpgLowersCorrectly) {
  auto [rc, s] = compileToAsm(
      "void flush_page(void *addr) { __invlpg(addr); }\n", "invlpg");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("invlpg"), std::string::npos) << "expected invlpg\n" << s;
}

// ---------------------------------------------------------------------------
// CLI / STI
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, CliStiLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_cli(void) { _disable(); }
    void do_sti(void) { _enable(); }
  )", "clisti");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("cli"), std::string::npos) << "expected cli\n" << s;
  EXPECT_NE(s.find("sti"), std::string::npos) << "expected sti\n" << s;
}

// ---------------------------------------------------------------------------
// Descriptor table ops: sidt/lidt/sgdt/lgdt
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest,
       DescriptorAddressesKeepNativeEffectsAndControls) {
  std::string Source = "void probe(unsigned long long offset) {\n";
  const std::vector<std::string> Names = {
      "verr_mem_address", "verw_mem_address", "sldt_mem_address",
      "str_mem_address",  "smsw_mem_address", "lldt_mem_address",
      "ltr_mem_address",  "lmsw_mem_address", "sgdt_address",
      "sidt_address",     "lgdt_address",     "lidt_address",
      "invlpg_address"};
  for (const auto &Name : Names)
    for (const auto &Controls : {"64, 0", "32, 100", "32, 101"})
      Source +=
          "  (void)__builtin_ia32_" + Name + "(offset, " + Controls + ");\n";
  Source += "}\n";
  const auto File = tmpFile("descriptor_addresses.c");
  writeFile(File, Source);
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      for (bool EmitIR : {true, false}) {
        const auto Output = tmpFile(EmitIR ? "descriptor_addresses.ll"
                                           : "descriptor_addresses.s");
        std::vector<std::string> Args = {std::string("--target=") + Target,
                                         "-std=c23",
                                         "-ffreestanding",
                                         "-fno-builtin-mimalloc",
                                         "-fno-lto",
                                         "-mllvm",
                                         "-verify-machineinstrs",
                                         Opt,
                                         "-S",
                                         File.string(),
                                         "-o",
                                         Output.string()};
        if (EmitIR)
          Args.push_back("-emit-llvm");
        const auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        for (auto Name : Names) {
          if (EmitIR) {
            std::replace(Name.begin(), Name.end(), '_', '.');
            EXPECT_NE(Text.find("@llvm.x86." + Name + "("), std::string::npos)
                << Text;
          } else {
            Name = Name.substr(0, Name.find('_'));
            const std::regex Pattern("\\t" + Name + "[a-z]*[\\t ]+");
            EXPECT_EQ(std::distance(std::sregex_iterator(Text.begin(),
                                                         Text.end(), Pattern),
                                    std::sregex_iterator()),
                      3)
                << Text;
          }
        }
        EXPECT_EQ(Text.find(EmitIR ? " asm " : "#APP"), std::string::npos)
            << Text;
        if (!EmitIR) {
          const bool Intel = Text.find(".intel_syntax") != std::string::npos;
          EXPECT_NE(Text.find(Intel ? "fs:[ebx]" : "%fs:(%ebx)"),
                    std::string::npos)
              << Text;
          EXPECT_NE(Text.find(Intel ? "gs:[ebx]" : "%gs:(%ebx)"),
                    std::string::npos)
              << Text;
        }
      }
    }
  for (const auto &Name : Names)
    for (const auto &Controls : {"16, 0", "64, 1", "offset, 0", "64, offset"}) {
      writeFile(File,
                "void invalid(unsigned long long offset) { __builtin_ia32_" +
                    Name + "(offset, " + Controls + "); }");
      const auto Result =
          ncc({"--target=x86_64-unknown-linux-gnu", "-ffreestanding",
               "-fno-builtin-mimalloc", "-fsyntax-only", File.string()});
      EXPECT_NE(Result.exitCode, 0) << Name << " " << Controls;
    }
}

TEST_F(X86PrivilegedIntrinTest, DescriptorTableOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_sidt(void *p) { __sidt(p); }
    void do_lidt(void *p) { __lidt(p); }
    void do_sgdt(void *p) { _sgdt(p); }
    void do_lgdt(void *p) { _lgdt(p); }
  )", "desctable");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("sidt"), std::string::npos) << "expected sidt\n" << s;
  EXPECT_NE(s.find("lidt"), std::string::npos) << "expected lidt\n" << s;
  EXPECT_NE(s.find("sgdt"), std::string::npos) << "expected sgdt\n" << s;
  EXPECT_NE(s.find("lgdt"), std::string::npos) << "expected lgdt\n" << s;
}

// ---------------------------------------------------------------------------
// WBINVD
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, WbinvdLowersCorrectly) {
  auto [rc, s] = compileToAsm(
      "void do_wbinvd(void) { __wbinvd(); }\n", "wbinvd");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("wbinvd"), std::string::npos) << "expected wbinvd\n" << s;
}

// ---------------------------------------------------------------------------
// Segment limit (LSL)
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest,
       DescriptorQueriesUseNativeResultAndStatusPairs) {
  std::string Source;
  for (const char *Operation : {"lar", "lsl"}) {
    for (const auto &[Bits, Type] :
         {std::pair{16, "unsigned short"}, std::pair{32, "unsigned int"},
          std::pair{64, "unsigned long long"}}) {
      for (bool Memory : {false, true}) {
        const std::string Name = std::string(Operation) +
                                 (Memory ? "_mem" : "_reg") +
                                 std::to_string(Bits);
        Source += std::string(Type) + " " + Name + "(" +
                  (Memory ? "const void *" : "unsigned short ") + "selector, " +
                  Type + " prior, unsigned char *zf) { " + Type +
                  " first = __builtin_ia32_" + Name +
                  "(selector, prior, zf); return first ^ __builtin_ia32_" +
                  Name + "(selector, prior, zf + 1); }\n";
      }
    }
  }
  for (const char *Opt : {"-O0", "-O2"}) {
    auto [RC, Assembly] =
        compileToAsm(Source.c_str(), "descriptor_queries", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    for (const char *Operation : {"lar", "lsl"}) {
      const std::regex Instruction(std::string("\\t") + Operation + "\\t");
      EXPECT_EQ(std::distance(std::sregex_iterator(Assembly.begin(),
                                                   Assembly.end(), Instruction),
                              std::sregex_iterator()),
                12)
          << Assembly;
    }
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest, DeadAndRepeatedSegmentLimitReadsRemainOrdered) {
  constexpr const char *Source = R"(
    void query(unsigned selector) {
      (void)__segmentlimit(selector);
      (void)__segmentlimit(selector);
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    auto [RC, Assembly] = compileToAsm(Source, "dead_segment_limits", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    const std::regex Instruction("\\tlsl\\t");
    EXPECT_EQ(std::distance(std::sregex_iterator(Assembly.begin(),
                                                 Assembly.end(), Instruction),
                            std::sregex_iterator()),
              2)
        << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest, BoundedSystemEffectsUseNativeInstructions) {
  constexpr const char *Source = R"(
    #include <x86gprintrin.h>
    __attribute__((target("hreset")))
    void query(unsigned control, unsigned long long address, unsigned ids,
               unsigned count, unsigned short selector, const void *memory) {
      _hreset(control);
      __builtin_ia32_lkgs_reg(selector);
      __builtin_ia32_lkgs_mem(memory);
      __builtin_ia32_invlpgb(address, ids, count);
      __builtin_ia32_tlbsync();
      _hreset(control);
      __builtin_ia32_lkgs_reg(selector);
      __builtin_ia32_lkgs_mem(memory);
      __builtin_ia32_invlpgb(address, ids, count);
      __builtin_ia32_tlbsync();
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    auto [RC, Assembly] = compileToAsm(Source, "bounded_system", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    for (const auto &[Instruction, Count] :
         {std::pair{"hreset", 2}, std::pair{"lkgs", 4}, std::pair{"invlpgb", 2},
          std::pair{"tlbsync", 2}}) {
      const std::regex Pattern(std::string("\\t") + Instruction +
                               "[\\t\\r\\n]");
      EXPECT_EQ(std::distance(std::sregex_iterator(Assembly.begin(),
                                                   Assembly.end(), Pattern),
                              std::sregex_iterator()),
                Count)
          << Assembly;
    }
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest, DescriptorStateEffectsRemainNativeWhenUnused) {
  constexpr const char *Source = R"(
    void probe(unsigned short selector, void *memory) {
      (void)__builtin_ia32_verr_reg(selector);
      (void)__builtin_ia32_verr_reg(selector);
      (void)__builtin_ia32_verr_mem(memory);
      (void)__builtin_ia32_verr_mem(memory);
      (void)__builtin_ia32_verw_reg(selector);
      (void)__builtin_ia32_verw_reg(selector);
      (void)__builtin_ia32_verw_mem(memory);
      (void)__builtin_ia32_verw_mem(memory);
      (void)__builtin_ia32_sldt_reg16();
      (void)__builtin_ia32_sldt_reg16();
      (void)__builtin_ia32_sldt_reg32();
      (void)__builtin_ia32_sldt_reg32();
      (void)__builtin_ia32_sldt_reg64();
      (void)__builtin_ia32_sldt_reg64();
      (void)__builtin_ia32_sldt_mem(memory);
      (void)__builtin_ia32_sldt_mem(memory);
      (void)__builtin_ia32_str_reg16();
      (void)__builtin_ia32_str_reg16();
      (void)__builtin_ia32_str_reg32();
      (void)__builtin_ia32_str_reg32();
      (void)__builtin_ia32_str_reg64();
      (void)__builtin_ia32_str_reg64();
      (void)__builtin_ia32_str_mem(memory);
      (void)__builtin_ia32_str_mem(memory);
      (void)__builtin_ia32_smsw_reg16();
      (void)__builtin_ia32_smsw_reg16();
      (void)__builtin_ia32_smsw_reg32();
      (void)__builtin_ia32_smsw_reg32();
      (void)__builtin_ia32_smsw_reg64();
      (void)__builtin_ia32_smsw_reg64();
      (void)__builtin_ia32_smsw_mem(memory);
      (void)__builtin_ia32_smsw_mem(memory);
      (void)__builtin_ia32_lldt_reg(selector);
      (void)__builtin_ia32_lldt_reg(selector);
      (void)__builtin_ia32_lldt_mem(memory);
      (void)__builtin_ia32_lldt_mem(memory);
      (void)__builtin_ia32_ltr_reg(selector);
      (void)__builtin_ia32_ltr_reg(selector);
      (void)__builtin_ia32_ltr_mem(memory);
      (void)__builtin_ia32_ltr_mem(memory);
      (void)__builtin_ia32_lmsw_reg(selector);
      (void)__builtin_ia32_lmsw_reg(selector);
      (void)__builtin_ia32_lmsw_mem(memory);
      (void)__builtin_ia32_lmsw_mem(memory);
      (void)__builtin_ia32_invd();
      (void)__builtin_ia32_invd();
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    auto [RC, Assembly] = compileToAsm(Source, "descriptor_state", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    for (const auto &[Instruction, Count] :
         {std::pair{"verr", 4}, std::pair{"verw", 4}, std::pair{"sldt", 8},
          std::pair{"str", 8}, std::pair{"smsw", 8}, std::pair{"lldt", 4},
          std::pair{"ltr", 4}, std::pair{"lmsw", 4}, std::pair{"invd", 2}}) {
      const std::regex Pattern(std::string("\\t") + Instruction +
                               "[\\t\\r\\n]");
      EXPECT_EQ(std::distance(std::sregex_iterator(Assembly.begin(),
                                                   Assembly.end(), Pattern),
                              std::sregex_iterator()),
                Count)
          << Assembly;
    }
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest, SgxSupervisorBuiltinsAndHeadersAreNative) {
  constexpr const char *Source = R"(
    #if !defined(__NEVERC_X86_SGX_COMPLETION__) || __NEVERC_X86_SGX_COMPLETION__ != 1
    #error missing compiler SGX completion capability
    #endif
    #ifdef __SGX__
    #error compiler capability must not change baseline CPU features
    #endif
    #include <immintrin.h>
    __attribute__((target("sgx")))
    unsigned long long query(unsigned long long a, unsigned long long b,
                             unsigned long long c, unsigned long long d,
                             unsigned long long *out, unsigned char *flags,
                             __SIZE_TYPE__ *data) {
      unsigned long long result = __builtin_ia32_encls(a, b, c, d, out, flags);
      result ^= __builtin_ia32_enclv(a, b, c, d, out, flags);
      result ^= _encls_u32((unsigned)a, data);
      result ^= _enclv_u32((unsigned)a, data);
      return result;
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    auto [RC, Assembly] = compileToAsm(Source, "sgx_supervisor", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    for (const char *Instruction : {"encls", "enclv"}) {
      const std::regex Pattern(std::string("\\t") + Instruction +
                               "[\\t\\r\\n]");
      EXPECT_EQ(std::distance(std::sregex_iterator(Assembly.begin(),
                                                   Assembly.end(), Pattern),
                              std::sregex_iterator()),
                2)
          << Assembly;
    }
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest,
       SgxSupervisorBuiltinsRequireTheirTargetFeature) {
  for (const char *Operation : {"encls", "enclv", "enclu_bounded"}) {
    const std::string Source = "unsigned long long query(unsigned long long "
                               "*out, unsigned char *flags) {"
                               "return __builtin_ia32_" +
                               std::string(Operation) +
                               "(0, 0, 0, 0, out, flags); }";
    auto [RC, Error] = compileToAsm(Source.c_str(), "sgx_without_feature");
    EXPECT_NE(RC, 0);
    EXPECT_NE(Error.find("sgx"), std::string::npos) << Error;
  }
}

TEST_F(X86PrivilegedIntrinTest, SgxBoundedUserLeavesAndHeaderAreNative) {
  for (unsigned Leaf : {0, 1, 5, 6, 7, 8, 9, 10, 11}) {
    SCOPED_TRACE(Leaf);
    const std::string Source =
        "#include <immintrin.h>\n"
        "__attribute__((target(\"sgx\"))) unsigned long long query("
        "unsigned long long b, unsigned long long c, unsigned long long d, "
        "unsigned long long *out, unsigned char *flags, __SIZE_TYPE__ *data) {"
        "unsigned long long r = __builtin_ia32_enclu_bounded(" +
        std::to_string(0x8123456700000000ULL | Leaf) +
        "ULL,b,c,d,out,flags);"
        "return r ^ _enclu_bounded_u32(" +
        std::to_string(Leaf) + ",data); }";
    for (const char *Opt : {"-O0", "-O2"}) {
      auto [RC, Assembly] = compileToAsm(Source.c_str(), "enclu_bounded", Opt);
      ASSERT_EQ(RC, 0) << Assembly;
      const std::regex Pattern("\\tenclu[\\t\\r\\n]");
      EXPECT_EQ(std::distance(std::sregex_iterator(Assembly.begin(),
                                                   Assembly.end(), Pattern),
                              std::sregex_iterator()),
                2)
          << Assembly;
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
    }
  }
}

TEST_F(X86PrivilegedIntrinTest, SgxBoundedUserRejectsUnownedContinuations) {
  for (const char *Leaf :
       {"a", "2", "3", "4", "12", "0xffffffffULL", "0x8123456700000002ULL"}) {
    SCOPED_TRACE(Leaf);
    const std::string Source =
        "__attribute__((target(\"sgx\"))) unsigned long long query("
        "unsigned long long a, unsigned long long *out, unsigned char *flags) {"
        "return __builtin_ia32_enclu_bounded(" +
        std::string(Leaf) + ",0,0,0,out,flags); }";
    auto [RC, Error] = compileToAsm(Source.c_str(), "enclu_unowned");
    EXPECT_NE(RC, 0) << Error;
    EXPECT_NE(Error.find(std::string(Leaf) == "a" ? "constant" : "fallthrough"),
              std::string::npos)
        << Error;
  }
}

TEST_F(X86PrivilegedIntrinTest, FlagBuiltinsNeedNoPlatformHeaders) {
  auto Source = tmpFile("portable-flags.c");
  writeFile(Source, R"c(
unsigned long long flags(void) { return __readeflags(); }
void restore(unsigned long long flags) { __writeeflags(flags); }
)c");
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      auto IR = tmpFile("portable-flags.ll");
      const auto Compile =
          ncc({std::string("--target=") + Target, Opt, "-S", "-emit-llvm",
               "-ffreestanding", "-Werror", "-fno-builtin-mimalloc", "-fno-lto",
               Source.string(), "-o", IR.string()});
      ASSERT_EQ(Compile.exitCode, 0) << Compile.err;
      const auto Text = readFile(IR);
      EXPECT_NE(Text.find("call i64 @llvm.x86.flags.read.u64()"),
                std::string::npos)
          << Text;
      EXPECT_NE(Text.find("call void @llvm.x86.flags.write.u64(i64"),
                std::string::npos)
          << Text;
      EXPECT_EQ(Text.find(" asm "), std::string::npos) << Text;
    }
}

TEST_F(X86PrivilegedIntrinTest, DescriptorAddressesKeepWidthSegmentAndResults) {
  for (unsigned Width : {32, 64})
    for (const auto &[Prefix, Segment] : {std::pair{0, ""},
                                          {0x26, "es:"},
                                          {0x2e, "cs:"},
                                          {0x36, "ss:"},
                                          {0x3e, "ds:"},
                                          {0x64, "fs:"},
                                          {0x65, "gs:"}}) {
      SCOPED_TRACE(Width);
      SCOPED_TRACE(Prefix);
      const auto Controls =
          std::to_string(Width) + "," + std::to_string(Prefix);
      std::string Source = "void lkgs(unsigned long long offset) {"
                           "__builtin_ia32_lkgs_address(offset," +
                           Controls + ");}\n";
      for (const char *Operation : {"lar", "lsl"})
        for (unsigned Bits : {16, 32, 64}) {
          const auto Name =
              std::string(Operation) + "_mem" + std::to_string(Bits);
          Source += "unsigned long long " + Name +
                    "(unsigned long long offset, unsigned long long prior, "
                    "unsigned char *zf) { return __builtin_ia32_" +
                    Name + "_address(offset," + Controls + ",prior,zf);}\n";
        }
      for (const char *Opt : {"-O0", "-O2"}) {
        auto [RC, Assembly] =
            compileToAsm(Source.c_str(), "descriptor_address", Opt);
        ASSERT_EQ(RC, 0) << Assembly;
        for (const char *Mnemonic : {"lkgs", "lar", "lsl"})
          EXPECT_TRUE(std::regex_search(
              Assembly,
              std::regex(std::string("\\t") + Mnemonic + "[wlq]?\\t")))
              << Assembly;
        EXPECT_NE(Assembly.find(std::string(Segment) +
                                (Width == 64 ? "[rbx]" : "[ebx]")),
                  std::string::npos)
            << Assembly;
        EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
      }
    }
}

TEST_F(X86PrivilegedIntrinTest, DescriptorAddressesRejectInvalidControls) {
  for (const auto &[Width, Segment] : {std::pair{"width", "0"},
                                       {"64", "segment"},
                                       {"0", "0"},
                                       {"16", "0"},
                                       {"128", "0"},
                                       {"-1", "0"},
                                       {"64", "1"},
                                       {"64", "0x67"},
                                       {"32", "0xf0"},
                                       {"32", "-1"}})
    for (const char *Operation : {"lkgs", "lar_mem16", "lar_mem32", "lar_mem64",
                                  "lsl_mem16", "lsl_mem32", "lsl_mem64"}) {
      SCOPED_TRACE(Width);
      SCOPED_TRACE(Segment);
      SCOPED_TRACE(Operation);
      const std::string Source =
          "void probe(unsigned width, unsigned segment, unsigned char *zf) {"
          "__builtin_ia32_" +
          std::string(Operation) + "_address(0," + Width + "," + Segment +
          (std::string(Operation) == "lkgs" ? "" : ",0,zf") + ");}";
      auto [RC, Error] = compileToAsm(Source.c_str(), "descriptor_bad_address");
      EXPECT_NE(RC, 0) << Error;
      EXPECT_NE(Error.find(std::string(Width) == "width" ||
                                   std::string(Segment) == "segment"
                               ? "constant"
                               : "native memory addressing"),
                std::string::npos)
          << Error;
    }
}

TEST_F(X86PrivilegedIntrinTest, VmxExplicitAddressesKeepWidthAndSegment) {
  for (unsigned Width : {32, 64})
    for (const auto &[Prefix, Segment] : {std::pair{0, ""},
                                          {0x26, "es:"},
                                          {0x2e, "cs:"},
                                          {0x36, "ss:"},
                                          {0x3e, "ds:"},
                                          {0x64, "fs:"},
                                          {0x65, "gs:"}}) {
      SCOPED_TRACE(Width);
      SCOPED_TRACE(Prefix);
      std::string Source;
      for (const char *Name : {"vmread", "vmwrite_mem", "invept", "invvpid",
                               "vmclear", "vmptrld", "vmptrst", "vmxon"}) {
        const bool Selector = std::string(Name) == "vmread" ||
                              std::string(Name) == "vmwrite_mem" ||
                              std::string(Name) == "invept" ||
                              std::string(Name) == "invvpid";
        const bool Void = std::string(Name) == "vmptrst";
        Source += std::string(Void ? "void " : "unsigned char ") + Name +
                  "(unsigned long long field, unsigned long long offset) {" +
                  (Void ? "" : "return ") + "__builtin_ia32_" + Name +
                  "_address(" + (Selector ? "field," : "") + "offset," +
                  std::to_string(Width) + "," + std::to_string(Prefix) +
                  ");}\n";
      }
      for (const char *Opt : {"-O0", "-O2"}) {
        auto [RC, Assembly] = compileToAsm(Source.c_str(), "vmx_address", Opt);
        ASSERT_EQ(RC, 0) << Assembly;
        for (const char *Mnemonic : {"vmread", "vmwrite", "invept", "invvpid",
                                     "vmclear", "vmptrld", "vmptrst", "vmxon"})
          EXPECT_TRUE(std::regex_search(
              Assembly, std::regex(std::string("\\t") + Mnemonic + "[ql]?\\t")))
              << Assembly;
        EXPECT_NE(Assembly.find(std::string(Segment) +
                                (Width == 64 ? "[rbx]" : "[ebx]")),
                  std::string::npos)
            << Assembly;
        EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
      }
    }
}

TEST_F(X86PrivilegedIntrinTest, VmxExplicitAddressesRejectInvalidControls) {
  for (const auto &[Width, Segment] : {std::pair{"width", "0"},
                                       {"64", "segment"},
                                       {"0", "0"},
                                       {"16", "0"},
                                       {"128", "0"},
                                       {"-1", "0"},
                                       {"64", "1"},
                                       {"64", "0x67"},
                                       {"32", "0xf0"},
                                       {"32", "-1"}})
    for (bool Selector : {false, true}) {
      SCOPED_TRACE(Width);
      SCOPED_TRACE(Segment);
      SCOPED_TRACE(Selector);
      const std::string Source =
          "void probe(unsigned width, unsigned segment) {" +
          std::string(Selector ? "__builtin_ia32_vmread_address(0,0,"
                               : "__builtin_ia32_vmptrst_address(0,") +
          Width + "," + Segment + ");}";
      auto [RC, Error] = compileToAsm(Source.c_str(), "vmx_bad_address");
      EXPECT_NE(RC, 0) << Error;
      EXPECT_NE(Error.find(std::string(Width) == "width" ||
                                   std::string(Segment) == "segment"
                               ? "constant"
                               : "native memory addressing"),
                std::string::npos)
          << Error;
    }
}

TEST_F(X86PrivilegedIntrinTest, SmxBoundedLeavesUseNativeRegisterTransport) {
  for (unsigned Leaf : {0, 6, 7, 8}) {
    SCOPED_TRACE(Leaf);
    const std::string Source =
        "unsigned long long query(unsigned long long b, unsigned long long c, "
        "unsigned long long d, unsigned long long *out) {"
        "__builtin_ia32_getsec_bounded(" +
        std::to_string(0x8000000100000000ULL | Leaf) +
        "ULL,b,c,d,out);"
        "return __builtin_ia32_getsec_bounded(" +
        std::to_string(Leaf) + "ULL,b,c,d,out); }";
    for (const char *Opt : {"-O0", "-O2"}) {
      auto [RC, Assembly] = compileToAsm(Source.c_str(), "getsec_bounded", Opt);
      ASSERT_EQ(RC, 0) << Assembly;
      const std::regex Pattern("\\tgetsec[\\t\\r\\n]");
      EXPECT_EQ(std::distance(std::sregex_iterator(Assembly.begin(),
                                                   Assembly.end(), Pattern),
                              std::sregex_iterator()),
                2)
          << Assembly;
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
    }
  }
}

TEST_F(X86PrivilegedIntrinTest, SmxBoundedLeavesRejectUnownedTransitions) {
  for (const char *Leaf : {"a", "1", "2", "3", "4", "5", "9", "0xffffffffULL",
                           "0x8000000100000002ULL"}) {
    SCOPED_TRACE(Leaf);
    const std::string Source = "unsigned long long query(unsigned long long a, "
                               "unsigned long long *out) {"
                               "return __builtin_ia32_getsec_bounded(" +
                               std::string(Leaf) + ",0,0,0,out); }";
    auto [RC, Error] = compileToAsm(Source.c_str(), "getsec_unowned");
    EXPECT_NE(RC, 0) << Error;
    EXPECT_NE(Error.find(std::string(Leaf) == "a" ? "constant" : "fallthrough"),
              std::string::npos)
        << Error;
  }
}

TEST_F(X86PrivilegedIntrinTest, HresetBuiltinRequiresItsTargetFeature) {
  auto [RC, Error] = compileToAsm(
      "void query(unsigned control) { __builtin_ia32_hreset(control); }",
      "hreset_without_feature");
  EXPECT_NE(RC, 0);
  EXPECT_NE(Error.find("hreset"), std::string::npos) << Error;
}

TEST_F(X86PrivilegedIntrinTest, SegmentLimitLowersCorrectly) {
  auto [rc, s] = compileToAsm(
      "unsigned int do_lsl(unsigned int sel) { return __segmentlimit(sel); }\n",
      "seglimit");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("lsl"), std::string::npos) << "expected lsl instruction\n" << s;
}

TEST_F(X86PrivilegedIntrinTest, PortableVmxBuiltinsNeedNoPlatformHeaders) {
  auto Source = tmpFile("portable_vmx.c");
  auto Output = tmpFile("portable_vmx.out");
  writeFile(Source, R"(
    unsigned char read(unsigned long long field, unsigned long long *out) {
      return __builtin_ia32_vmread(field, out);
    }
    unsigned char write(unsigned long long field, unsigned long long value) {
      return __builtin_ia32_vmwrite(field, value);
    }
    unsigned char clear(const void *p) { return __builtin_ia32_vmclear(p); }
    unsigned char load(const void *p) { return __builtin_ia32_vmptrld(p); }
    void store(void *p) { __builtin_ia32_vmptrst(p); }
    unsigned char on(const void *p) { return __builtin_ia32_vmxon(p); }
    void off(void) { __builtin_ia32_vmxoff(); }
    unsigned char launch(void) { return __builtin_ia32_vmlaunch(); }
    unsigned char resume(void) { return __builtin_ia32_vmresume(); }
  )");
  for (const char *Triple : {"x86_64-pc-windows-msvc",
                             "x86_64-unknown-linux-gnu", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (bool IR : {false, true}) {
        SCOPED_TRACE(Triple);
        SCOPED_TRACE(Opt);
        SCOPED_TRACE(IR);
        std::vector<std::string> Arguments = {std::string("--target=") + Triple,
                                              "-ffreestanding",
                                              "-fno-builtin-mimalloc",
                                              "-fno-lto",
                                              "-Werror",
                                              "-mllvm",
                                              "-verify-machineinstrs",
                                              Opt,
                                              "-S",
                                              Source.string(),
                                              "-o",
                                              Output.string()};
        if (IR)
          Arguments.push_back("-emit-llvm");
        const auto Result = ncc(Arguments);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        for (const char *Name :
             {"vmread", "vmwrite", "vmclear", "vmptrld", "vmptrst", "vmxon",
              "vmxoff", "vmlaunch", "vmresume"})
          EXPECT_NE(Text.find((IR ? "@llvm.x86." : "\t") + std::string(Name) +
                              (IR ? "(" : "")),
                    std::string::npos)
              << Text;
        EXPECT_EQ(Text.find(IR ? " asm " : "#APP"), std::string::npos) << Text;
        EXPECT_EQ(Text.find("__builtin_ia32_"), std::string::npos) << Text;
      }
}

// ---------------------------------------------------------------------------
// VMX: vmxoff, vmlaunch, vmresume, vmptrst (simple ops)
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, VmxSimpleOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_vmxoff(void) { __vmx_off(); }
    unsigned char do_vmlaunch(void) { return __vmx_vmlaunch(); }
    unsigned char do_vmresume(void) { return __vmx_vmresume(); }
    void do_vmptrst(void *p) { __vmx_vmptrst((__int64 *)p); }
  )", "vmx_simple");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("vmxoff"), std::string::npos) << "expected vmxoff\n" << s;
  EXPECT_NE(s.find("vmlaunch"), std::string::npos) << "expected vmlaunch\n" << s;
  EXPECT_NE(s.find("vmresume"), std::string::npos) << "expected vmresume\n" << s;
  EXPECT_NE(s.find("vmptrst"), std::string::npos) << "expected vmptrst\n" << s;
}

// ---------------------------------------------------------------------------
// VMX: vmwrite, vmread, vmclear, vmptrld, vmxon (parameterized ops)
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, VmxParameterizedOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned char do_vmwrite(unsigned __int64 f, unsigned __int64 v) { return __vmx_vmwrite(f, v); }
    unsigned char do_vmread(unsigned __int64 f, unsigned __int64 *v) { return __vmx_vmread(f, v); }
    unsigned char do_vmclear(unsigned __int64 *p) { return __vmx_vmclear(p); }
    unsigned char do_vmptrld(unsigned __int64 *p) { return __vmx_vmptrld(p); }
    unsigned char do_vmxon(unsigned __int64 *p) { return __vmx_on(p); }
  )", "vmx_param");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("vmwrite"), std::string::npos) << "expected vmwrite\n" << s;
  EXPECT_NE(s.find("vmread"), std::string::npos) << "expected vmread\n" << s;
  EXPECT_NE(s.find("vmclear"), std::string::npos) << "expected vmclear\n" << s;
  EXPECT_NE(s.find("vmptrld"), std::string::npos) << "expected vmptrld\n" << s;
  EXPECT_NE(s.find("vmxon"), std::string::npos) << "expected vmxon\n" << s;
}

TEST_F(X86PrivilegedIntrinTest, VmreadOwnsItsConditionalMemoryWrite) {
  // A register VMREAD followed by a C/IR store writes the destination even on
  // VMfail. Require the native memory form, including when status is unused.
  const std::regex memoryRead(R"(vmread\s+qword ptr \[[^\]]+\], [a-z0-9]+)");
  for (const char *opt : {"-O0", "-O2"}) {
    for (bool dead : {false, true}) {
      SCOPED_TRACE(opt);
      SCOPED_TRACE(dead);
      const char *source = dead ? R"(
        #include <intrin.h>
        void access(unsigned __int64 field, unsigned __int64 *destination) {
          __vmx_vmread(field, destination);
        }
      )"
                                : R"(
        #include <intrin.h>
        unsigned char access(unsigned __int64 field,
                             unsigned __int64 *destination) {
          return __vmx_vmread(field, destination);
        }
      )";
      auto [rc, assembly] = compileToAsm(source, "vmread_memory", opt);
      ASSERT_EQ(rc, 0) << assembly;
      EXPECT_TRUE(std::regex_search(assembly, memoryRead)) << assembly;
      EXPECT_EQ(assembly.find("#APP"), std::string::npos) << assembly;
    }
  }
}

TEST_F(X86PrivilegedIntrinTest, VmcsRegisterReadAndMemoryWriteUseNativeForms) {
  constexpr const char *Source = R"(
    unsigned long long read_register(unsigned long long field,
                                     unsigned long long prior,
                                     unsigned char *status) {
      return __builtin_ia32_vmread_reg(field, prior, status);
    }
    unsigned char write_memory(unsigned long long field, const void *source) {
      return __builtin_ia32_vmwrite_mem(field, source);
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto [RC, Assembly] = compileToAsm(Source, "vmcs_native_forms", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    EXPECT_TRUE(std::regex_search(
        Assembly, std::regex("\\tvmread\\t[a-z0-9]+, [a-z0-9]+")))
        << Assembly;
    EXPECT_TRUE(std::regex_search(
        Assembly, std::regex("\\tvmwrite\\t[a-z0-9]+, qword ptr \\[")))
        << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

// ---------------------------------------------------------------------------
// VMX status flag encoding: vmlaunch returns 0/1/2 via sete+setb
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, VmxStatusFlagEncoding) {
  auto [rc, s] = compileToAsm(R"(
    unsigned char do_vmlaunch(void) { return __vmx_vmlaunch(); }
  )", "vmx_flags");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("sete"), std::string::npos) << "expected sete for ZF\n" << s;
  EXPECT_NE(s.find("setb"), std::string::npos) << "expected setb for CF\n" << s;
}

// ---------------------------------------------------------------------------
// GS segment: read/write/inc/add for all sizes
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, GsSegmentOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned char     read_gs8 (unsigned long o) { return __readgsbyte(o); }
    unsigned short    read_gs16(unsigned long o) { return __readgsword(o); }
    unsigned long     read_gs32(unsigned long o) { return __readgsdword(o); }
    unsigned __int64  read_gs64(unsigned long o) { return __readgsqword(o); }
    void write_gs8 (unsigned long o, unsigned char     v) { __writegsbyte(o, v); }
    void write_gs16(unsigned long o, unsigned short    v) { __writegsword(o, v); }
    void write_gs32(unsigned long o, unsigned long     v) { __writegsdword(o, v); }
    void write_gs64(unsigned long o, unsigned __int64  v) { __writegsqword(o, v); }
    void inc_gs8 (unsigned long o) { __incgsbyte(o); }
    void inc_gs16(unsigned long o) { __incgsword(o); }
    void inc_gs32(unsigned long o) { __incgsdword(o); }
    void inc_gs64(unsigned long o) { __incgsqword(o); }
    void add_gs8 (unsigned long o, unsigned char     v) { __addgsbyte(o, v); }
    void add_gs16(unsigned long o, unsigned short    v) { __addgsword(o, v); }
    void add_gs32(unsigned long o, unsigned long     v) { __addgsdword(o, v); }
    void add_gs64(unsigned long o, unsigned __int64  v) { __addgsqword(o, v); }
  )", "gsops");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("gs:"), std::string::npos) << "expected gs: segment prefix\n" << s;
}

// ---------------------------------------------------------------------------
// FS segment: read for all sizes
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, FsSegmentReadsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned char     read_fs8 (unsigned long o) { return __readfsbyte(o); }
    unsigned short    read_fs16(unsigned long o) { return __readfsword(o); }
    unsigned long     read_fs32(unsigned long o) { return __readfsdword(o); }
    unsigned __int64  read_fs64(unsigned long o) { return __readfsqword(o); }
  )", "fsread");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("fs:"), std::string::npos) << "expected fs: segment prefix\n" << s;
}

// ---------------------------------------------------------------------------
// Bounded SGX/SMX completion with explicit arithmetic flags.
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, BoundedLeafStateRemainsNativeAcrossFormats) {
  const auto Source = tmpFile("bounded_leaf_state.c");
  std::string Program =
      "#if !defined(__NEVERC_X86_BOUNDED_LEAF_STATE__) || "
      "__NEVERC_X86_BOUNDED_LEAF_STATE__ != 1\n"
      "#error missing bounded leaf state capability\n#endif\n";
  for (const std::string Operation : {"enclu", "getsec"})
    for (unsigned Leaf : {0, 1, 5, 6, 7, 8, 9, 10, 11}) {
      if (Operation == "getsec" && Leaf != 0 && Leaf != 6 && Leaf != 7 &&
          Leaf != 8)
        continue;
      Program += "__attribute__((target(\"sgx,sahf\"))) void " + Operation +
                 std::to_string(Leaf) +
                 "(unsigned long long b, unsigned long long c, unsigned long "
                 "long d, unsigned short flags, unsigned long long *out, "
                 "unsigned short *cc) { __builtin_ia32_" +
                 Operation + "_bounded_state((0xabcdefffULL << 32) | " +
                 std::to_string(Leaf) + ", b, c, d, flags, out, cc); }\n";
    }
  Program += R"(
    __attribute__((target("sahf"))) void unused(void) {
      unsigned long long out[4];
      unsigned short flags;
      __builtin_ia32_getsec_bounded_state(8, 0, 0, 0, 0, out, &flags);
      __builtin_ia32_getsec_bounded_state(8, 0, 0, 0, 0, out, &flags);
    }
  )";
  writeFile(Source, Program);
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (const char *Mode : {"ir", "asm", "object"}) {
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        SCOPED_TRACE(Mode);
        const auto Output = tmpFile(std::string("bounded_leaf_state.") + Mode);
        std::vector<std::string> Args = {std::string("--target=") + Target,
                                         "-std=c23",
                                         "-ffreestanding",
                                         "-fno-builtin-mimalloc",
                                         "-fno-lto",
                                         "-mllvm",
                                         "-verify-machineinstrs",
                                         Opt,
                                         Source.string(),
                                         "-o",
                                         Output.string()};
        Args.push_back(std::string(Mode) == "object" ? "-c" : "-S");
        if (std::string(Mode) == "ir")
          Args.push_back("-emit-llvm");
        const auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        if (std::string(Mode) == "ir") {
          EXPECT_NE(Text.find("{ i64, i64, i64, i64, i16 }"), std::string::npos)
              << Text;
          EXPECT_EQ(Text.find(" asm "), std::string::npos) << Text;
          const std::regex Calls(
              "call[^@\\n]*@llvm\\.x86\\.(enclu|getsec)\\.bounded\\.state\\(");
          EXPECT_EQ(std::distance(
                        std::sregex_iterator(Text.begin(), Text.end(), Calls),
                        std::sregex_iterator()),
                    15)
              << Text;
        } else if (std::string(Mode) == "asm") {
          for (const char *Name : {"enclu", "getsec", "sahf", "lahf", "seto"})
            EXPECT_NE(Text.find(Name), std::string::npos) << Text;
          EXPECT_EQ(Text.find("#APP"), std::string::npos) << Text;
        } else
          EXPECT_FALSE(Text.empty());
      }
}

TEST_F(X86PrivilegedIntrinTest,
       BoundedLeafStateRejectsTransitionsAndUnknownLeaves) {
  for (const std::string Operation : {"enclu", "getsec"})
    for (const std::string Leaf : {"2", "3", "4", "12", "0xffffffff", "leaf"}) {
      const auto Source =
          "__attribute__((target(\"sgx,sahf\"))) void bad(unsigned long long "
          "leaf, unsigned long long *out, unsigned short *flags) { "
          "__builtin_ia32_" +
          Operation + "_bounded_state(" + Leaf + ",0,0,0,0,out,flags); }";
      auto [RC, Error] = compileToAsm(Source.c_str(), "invalid_leaf_state");
      EXPECT_NE(RC, 0) << Operation << ' ' << Leaf << '\n' << Error;
      EXPECT_NE(Error.find("error:"), std::string::npos) << Error;
    }
}

TEST_F(X86PrivilegedIntrinTest, BoundedLeafStateRequiresItsOwnFeatures) {
  for (const std::string Operation : {"enclu", "getsec"}) {
    const auto Source = tmpFile("leaf_state_feature.c");
    writeFile(Source,
              "void state(unsigned long long *out, unsigned short *flags) { "
              "__builtin_ia32_" +
                  Operation + "_bounded_state(0,0,0,0,0,out,flags); }");
    auto Result = ncc({"-std=c23", "-ffreestanding", "-fno-builtin-mimalloc",
                       "-msgx", "-mno-sahf", "-S", Source.string(), "-o",
                       tmpFile("missing_flags.s").string()});
    EXPECT_NE(Result.exitCode, 0) << Result.err;
    EXPECT_NE(Result.err.find("sahf"), std::string::npos) << Result.err;
    Result = ncc({"-std=c23", "-ffreestanding", "-fno-builtin-mimalloc",
                  "-mno-sgx", "-msahf", "-S", Source.string(), "-o",
                  tmpFile("missing_sgx.s").string()});
    if (Operation == "enclu") {
      EXPECT_NE(Result.exitCode, 0) << Result.err;
      EXPECT_NE(Result.err.find("sgx"), std::string::npos) << Result.err;
    } else
      EXPECT_EQ(Result.exitCode, 0) << Result.err;
  }
}

// ---------------------------------------------------------------------------
// Rep string ops: movsb/w/d/q, stosb/w/d/q
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, ExplicitRepStateRemainsNativeAcrossFormats) {
  const auto Source = tmpFile("rep_state.c");
  writeFile(Source, R"(
    #if !defined(__NEVERC_X86_REP_STATE__) || __NEVERC_X86_REP_STATE__ != 1
    #error missing compiler REP state capability
    #endif
    __attribute__((target("sahf")))
    void strings(unsigned long long a, unsigned long long b,
                 unsigned long long c, unsigned char df, unsigned short flags,
                 unsigned long long *out, unsigned short *cc) {
      __builtin_ia32_rep_movs_state(a, b, c, df, 1, 32, 0x64, out);
      __builtin_ia32_rep_stos_state(a, b, c, df, 8, 64, out);
      __builtin_ia32_rep_lods_state(a, b, c, df, 2, 32, 0x65, out);
      __builtin_ia32_rep_cmps_state(a, b, c, df, flags, 4, 64, 0, 0, out, cc);
      __builtin_ia32_rep_scas_state(a, b, c, df, flags, 8, 32, 1, out, cc);
      __builtin_ia32_rep_ins_state(a, b, c, df, 2, 32, out);
      __builtin_ia32_rep_outs_state(a, b, c, df, 4, 64, 0x64, out);
    }
    void unused(unsigned long long a, unsigned long long b,
                unsigned long long c) {
      unsigned long long out[3];
      __builtin_ia32_rep_movs_state(a, b, c, 0, 1, 64, 0, out);
      __builtin_ia32_rep_movs_state(a, b, c, 1, 1, 64, 0, out);
    }
  )");
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (const char *Mode : {"ir", "asm", "object"}) {
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        SCOPED_TRACE(Mode);
        const auto Output = tmpFile(std::string("rep_state.") + Mode);
        std::vector<std::string> Args = {std::string("--target=") + Target,
                                         "-std=c23",
                                         "-ffreestanding",
                                         "-fno-builtin-mimalloc",
                                         "-fno-lto",
                                         "-mllvm",
                                         "-verify-machineinstrs",
                                         Opt,
                                         Source.string(),
                                         "-o",
                                         Output.string()};
        Args.push_back(std::string(Mode) == "object" ? "-c" : "-S");
        if (std::string(Mode) == "ir")
          Args.push_back("-emit-llvm");
        const auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        if (std::string(Mode) == "ir") {
          for (const char *Name :
               {"movs", "stos", "lods", "cmps", "scas", "ins", "outs"})
            EXPECT_NE(
                Text.find(std::string("@llvm.x86.rep.") + Name + ".state("),
                std::string::npos)
                << Text;
          EXPECT_EQ(Text.find(" asm "), std::string::npos) << Text;
          const std::regex Moves(
              "call[^@\\n]*@llvm\\.x86\\.rep\\.movs\\.state\\(");
          EXPECT_EQ(std::distance(
                        std::sregex_iterator(Text.begin(), Text.end(), Moves),
                        std::sregex_iterator()),
                    3)
              << Text;
        } else if (std::string(Mode) == "asm") {
          for (const char *Name :
               {"rep", "movsb", "stosq", "lodsw", "cmps", "scasq", "insw",
                "outs", "sahf", "lahf", "seto", "std", "cld", "fs:", "gs:"})
            EXPECT_NE(Text.find(Name), std::string::npos) << Text;
          EXPECT_EQ(Text.find("#APP"), std::string::npos) << Text;
        } else {
          EXPECT_FALSE(Text.empty());
        }
      }
}

TEST_F(X86PrivilegedIntrinTest, ExplicitRepStateRejectsInvalidControls) {
  for (const char *Call :
       {"__builtin_ia32_rep_movs_state(a,b,c,df,3,64,0,out)",
        "__builtin_ia32_rep_movs_state(a,b,c,df,1,16,0,out)",
        "__builtin_ia32_rep_movs_state(a,b,c,df,1,64,1,out)",
        "__builtin_ia32_rep_movs_state(a,b,c,df,1,64,0x100,out)",
        "__builtin_ia32_rep_movs_state(a,b,c,df,element,64,0,out)",
        "__builtin_ia32_rep_stos_state(a,b,c,df,0,64,out)",
        "__builtin_ia32_rep_lods_state(a,b,c,df,1,0,0,out)",
        "__builtin_ia32_rep_cmps_state(a,b,c,df,0,1,64,0,2,out,cc)",
        "__builtin_ia32_rep_scas_state(a,b,c,df,0,1,64,element,out,cc)",
        "__builtin_ia32_rep_ins_state(a,b,c,df,8,64,out)",
        "__builtin_ia32_rep_outs_state(a,b,c,df,8,64,0,out)"}) {
    SCOPED_TRACE(Call);
    const std::string Source =
        "__attribute__((target(\"sahf\"))) void bad(unsigned long long a, "
        "unsigned long long b, unsigned long long c, unsigned char df, "
        "unsigned element, unsigned long long *out, unsigned short *cc) { " +
        std::string(Call) + "; }";
    auto [RC, Error] = compileToAsm(Source.c_str(), "invalid_rep_state");
    EXPECT_NE(RC, 0) << Error;
    EXPECT_NE(Error.find("error:"), std::string::npos) << Error;
  }
}

TEST_F(X86PrivilegedIntrinTest, ExplicitRepComparisonsRequireSahf) {
  for (const char *Call :
       {"__builtin_ia32_rep_cmps_state(a,b,c,0,0,1,64,0,0,out,cc)",
        "__builtin_ia32_rep_scas_state(a,b,c,0,0,1,64,0,out,cc)"}) {
    const auto Source = tmpFile("rep_missing_sahf.c");
    writeFile(Source, "void bad(unsigned long long a, unsigned long long b, "
                      "unsigned long long c, unsigned long long *out, "
                      "unsigned short *cc) { " +
                          std::string(Call) + "; }");
    const auto Result = ncc({"--target=x86_64-unknown-linux-gnu", "-mno-sahf",
                             "-ffreestanding", "-fno-builtin-mimalloc", "-S",
                             Source.string(), "-o", tmpFile("bad.s").string()});
    EXPECT_NE(Result.exitCode, 0) << Result.err;
    EXPECT_NE(Result.err.find("needs target feature sahf"),
              std::string::npos)
        << Result.err;
  }
}

TEST_F(X86PrivilegedIntrinTest, RepStringOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_movsb(void *d, const void *s, unsigned __int64 n) { __movsb((unsigned char*)d, (const unsigned char*)s, n); }
    void do_movsw(void *d, const void *s, unsigned __int64 n) { __movsw((unsigned short*)d, (const unsigned short*)s, n); }
    void do_movsd(void *d, const void *s, unsigned __int64 n) { __movsd((unsigned long*)d, (const unsigned long*)s, n); }
    void do_movsq(void *d, const void *s, unsigned __int64 n) { __movsq((unsigned __int64*)d, (const unsigned __int64*)s, n); }
    void do_stosb(unsigned char     *d, unsigned char     v, unsigned __int64 n) { __stosb(d, v, n); }
    void do_stosw(unsigned short    *d, unsigned short    v, unsigned __int64 n) { __stosw(d, v, n); }
    void do_stosd(unsigned long     *d, unsigned long     v, unsigned __int64 n) { __stosd(d, v, n); }
    void do_stosq(unsigned __int64  *d, unsigned __int64  v, unsigned __int64 n) { __stosq(d, v, n); }
  )", "repstring");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("movsb"), std::string::npos) << "expected movsb\n" << s;
  EXPECT_NE(s.find("movsw"), std::string::npos) << "expected movsw\n" << s;
  EXPECT_NE(s.find("movsd"), std::string::npos) << "expected movsd\n" << s;
  EXPECT_NE(s.find("movsq"), std::string::npos) << "expected movsq\n" << s;
  EXPECT_NE(s.find("stosb"), std::string::npos) << "expected stosb\n" << s;
  EXPECT_NE(s.find("stosw"), std::string::npos) << "expected stosw\n" << s;
  EXPECT_NE(s.find("stosd"), std::string::npos) << "expected stosd\n" << s;
  EXPECT_NE(s.find("stosq"), std::string::npos) << "expected stosq\n" << s;
}

TEST_F(X86PrivilegedIntrinTest, NativeStringInterfacesAcceptConstSources) {
  constexpr const char *Source = R"(
    #include <intrin.h>
    #pragma clang diagnostic error "-Wincompatible-pointer-types-discards-qualifiers"
    void strings(void *d, const void *s, unsigned long long n,
                 unsigned long long value, unsigned short port) {
      __movsb(d, s, n);
      __movsw(d, s, n);
      __movsd(d, s, n);
      __movsq(d, s, n);
      __builtin_ia32_movsb(d, s, n);
      __builtin_ia32_movsw(d, s, n);
      __builtin_ia32_movsd_rep(d, s, n);
      __builtin_ia32_movsq(d, s, n);
      __builtin_ia32_stosb(d, value, n);
      __builtin_ia32_stosw(d, value, n);
      __builtin_ia32_stosd(d, value, n);
      __builtin_ia32_stosq(d, value, n);
      __builtin_ia32_inbytestring(port, d, n);
      __builtin_ia32_inwordstring(port, d, n);
      __builtin_ia32_indwordstring(port, d, n);
      __builtin_ia32_outbytestring(port, s, n);
      __builtin_ia32_outwordstring(port, s, n);
      __builtin_ia32_outdwordstring(port, s, n);
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto [RC, Assembly] = compileToAsm(Source, "native_strings", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    for (const char *Mnemonic :
         {"movsb", "movsw", "movsd", "movsq", "stosb", "stosw", "stosd",
          "stosq", "insb", "insw", "insd", "outsb", "outsw", "outsd"})
      EXPECT_NE(Assembly.find(Mnemonic), std::string::npos) << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}

// ---------------------------------------------------------------------------
// Port I/O string ops: inbytestring, outbytestring
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, PortIoStringOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_inbytes(unsigned short p, unsigned char *b, unsigned long n) { __inbytestring(p, b, n); }
    void do_outbytes(unsigned short p, unsigned char *b, unsigned long n) { __outbytestring(p, b, n); }
  )", "portiostr");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("rep"), std::string::npos) << "expected rep prefix\n" << s;
  EXPECT_NE(s.find("insb"), std::string::npos) << "expected insb\n" << s;
  EXPECT_NE(s.find("outsb"), std::string::npos) << "expected outsb\n" << s;
}

// ---------------------------------------------------------------------------
// CPUID
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, CpuidLowersCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_cpuid(int info[4], int func, int subfunc) { __cpuidex(info, func, subfunc); }
  )", "cpuid");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("cpuid"), std::string::npos) << "expected cpuid\n" << s;
}

// ---------------------------------------------------------------------------
// INT 2C
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, Int2cLowersCorrectly) {
  auto [rc, s] = compileToAsm(
      "void do_int2c(void) { __int2c(); }\n", "int2c");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("int\t44"), std::string::npos) << "expected int 44 (0x2c)\n" << s;
}

TEST_F(X86PrivilegedIntrinTest, Int2cRetainsMicrosoftAssertionContract) {
  const auto Source = tmpFile("int2c_assert.c");
  const auto IR = tmpFile("int2c_assert.ll");
  writeFile(Source, "#include <intrin.h>\n"
                    "int assertion(void) { __int2c(); return 7; }\n");
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto Result = ncc({"--target=x86_64-pc-windows-msvc", "-ffreestanding",
                       "-fno-builtin-mimalloc", "-fno-lto", Opt, "-S",
                       "-emit-llvm", Source.string(), "-o", IR.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    const auto Text = readFile(IR);
    EXPECT_NE(Text.find("@llvm.x86.int(i8 44)"), std::string::npos) << Text;
    EXPECT_NE(Text.find("noreturn"), std::string::npos) << Text;
    if (std::string(Opt) == "-O2")
      EXPECT_NE(Text.find("unreachable"), std::string::npos) << Text;
    EXPECT_EQ(Text.find("llvm.x86.int2c"), std::string::npos) << Text;
    EXPECT_EQ(Text.find(" asm "), std::string::npos) << Text;
  }
}

TEST_F(X86PrivilegedIntrinTest, RawInterruptRequiresAnImmediateByte) {
  for (unsigned Vector : {0, 1, 3, 4, 44, 128, 255}) {
    SCOPED_TRACE(Vector);
    const auto Source = "void interrupt(void) { __builtin_ia32_int(" +
                        std::to_string(Vector) + "); }";
    auto [RC, Assembly] = compileToAsm(Source.c_str(), "int_vector");
    ASSERT_EQ(RC, 0) << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
  for (const char *Vector : {"v", "-1", "256", "0xffffffffU"}) {
    SCOPED_TRACE(Vector);
    const auto Source = "void interrupt(unsigned v) { __builtin_ia32_int(" +
                        std::string(Vector) + "); }";
    auto [RC, Error] = compileToAsm(Source.c_str(), "int_invalid");
    EXPECT_NE(RC, 0) << Error;
    EXPECT_NE(Error.find(std::string(Vector) == "v" ? "constant" : "range"),
              std::string::npos)
        << Error;
  }
}

TEST_F(X86PrivilegedIntrinTest,
       RawInterruptRetainsContinuationAndMemoryEffects) {
  const auto Source = tmpFile("int2c_resume.c");
  const auto IR = tmpFile("int2c_resume.ll");
  const auto Object = tmpFile("int2c_resume.o");
  writeFile(Source, R"(
    unsigned resume(unsigned *memory, unsigned input) {
      *memory = input;
      __builtin_ia32_int(44);
      unsigned first = *memory;
      __builtin_ia32_int(44);
      return first ^ *memory ^ input;
    }
  )");
  for (const char *Triple :
       {"x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu",
        "x86_64-apple-darwin"}) {
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Triple);
      SCOPED_TRACE(Opt);
      std::vector<std::string> Args = {std::string("--target=") + Triple,
                                       "-std=c23",
                                       "-fms-extensions",
                                       "-ffreestanding",
                                       "-fno-builtin-mimalloc",
                                       "-fno-lto",
                                       "-Werror",
                                       "-mllvm",
                                       "-verify-machineinstrs",
                                       Opt,
                                       Source.string()};
      auto EmitIR = Args;
      EmitIR.insert(EmitIR.end(), {"-S", "-emit-llvm", "-o", IR.string()});
      auto Result = ncc(EmitIR);
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Text = readFile(IR);
      const std::regex NativeCall("@llvm\\.x86\\.int\\(i8 44\\)");
      EXPECT_EQ(std::distance(
                    std::sregex_iterator(Text.begin(), Text.end(), NativeCall),
                    std::sregex_iterator()),
                2)
          << Text;
      for (const char *LostContinuation :
           {"noreturn", "unreachable", "llvm.x86.int2c", " asm "})
        EXPECT_EQ(Text.find(LostContinuation), std::string::npos) << Text;
      EXPECT_NE(Text.find("ret i32"), std::string::npos) << Text;
      Args.insert(Args.end(), {"-c", "-o", Object.string()});
      Result = ncc(Args);
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
    }
  }
}

// ---------------------------------------------------------------------------
// EFLAGS read/write
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, EflagsReadWriteLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned __int64 read_flags(void) { return __readeflags(); }
    void write_flags(unsigned __int64 v) { __writeeflags(v); }
  )", "eflags");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("pushfq"), std::string::npos) << "expected pushfq\n" << s;
  EXPECT_NE(s.find("popfq"), std::string::npos) << "expected popfq\n" << s;
}

// ---------------------------------------------------------------------------
// INVPCID
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, InvpcidLowersCorrectly) {
  auto [rc, s] = compileToAsm(
      "void do_invpcid(unsigned int t, void *d) { _invpcid(t, d); }\n",
      "invpcid");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("invpcid"), std::string::npos) << "expected invpcid\n" << s;
}

// ---------------------------------------------------------------------------
// XSETBV
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, XsetbvLowersCorrectly) {
  auto [rc, s] = compileToAsm(
      "void do_xsetbv(unsigned int xcr, unsigned __int64 val) { _xsetbv(xcr, val); }\n",
      "xsetbv");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("xsetbv"), std::string::npos) << "expected xsetbv\n" << s;
}

// ---------------------------------------------------------------------------
// TSX: xbegin, xabort, xend, xtest
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, TsxOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_xbegin(void) { _xbegin(); }
    void do_xend(void)   { _xend(); }
    void do_xabort(void) { _xabort(0x42); }
    unsigned char do_xtest(void) { return _xtest(); }
  )", "tsx");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("xbegin"), std::string::npos) << "expected xbegin\n" << s;
  EXPECT_NE(s.find("xend"), std::string::npos) << "expected xend\n" << s;
  EXPECT_NE(s.find("xabort"), std::string::npos) << "expected xabort\n" << s;
  EXPECT_NE(s.find("xtest"), std::string::npos) << "expected xtest\n" << s;
}

TEST_F(X86PrivilegedIntrinTest,
       TransactionAbortRequiresAConstantInEveryInterface) {
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"}) {
    SCOPED_TRACE(Target);
    for (unsigned Interface = 0; Interface != 3; ++Interface) {
      // The undeclared Microsoft builtin is active only in Microsoft mode.
      if (Interface == 2 &&
          std::string(Target).find("windows") == std::string::npos)
        continue;
      SCOPED_TRACE(Interface);
      const auto Source = tmpFile("transaction_nonconstant.c");
      const std::string Header =
          Interface == 0 ? "#include <immintrin.h>\n" : "";
      const std::string Name =
          Interface == 1 ? "__builtin_ia32_xabort" : "_xabort";
      writeFile(Source,
                Header +
                    "__attribute__((target(\"rtm\"))) void f(unsigned x) { " +
                    Name + "(x); }\n");
      // This must fail in the frontend, before LLVM can select an instruction
      // from an invalid nonconstant immediate or invent an encoded byte.
      const auto Result =
          ncc({Target, "-fno-builtin-mimalloc", "-ffreestanding",
               "-fsyntax-only", Source.string()});
      EXPECT_GT(Result.exitCode, 0) << Result.err;
      EXPECT_NE(Result.err.find("must be a constant integer"),
                std::string::npos)
          << Result.err;
    }
  }
}

TEST_F(X86PrivilegedIntrinTest, TransactionHeadersExposeNativeInterfaces) {
  for (const char *Header : {"immintrin.h", "x86intrin.h"}) {
    SCOPED_TRACE(Header);
    const auto Source = tmpFile("transaction_headers.c");
    writeFile(Source, std::string("#include <") + Header + R"(>
      #ifdef _MSC_VER
      #include <intrin.h>
      #endif
      _Static_assert(_XBEGIN_STARTED == ~0u, "transaction success status");
      _Static_assert(_XABORT_EXPLICIT == 1 && _XABORT_RETRY == 2 &&
                     _XABORT_CONFLICT == 4 && _XABORT_CAPACITY == 8 &&
                     _XABORT_DEBUG == 16 && _XABORT_NESTED == 32,
                     "abort status bits");
      _Static_assert(_XABORT_CODE(0xff000001u) == 255, "abort code");
      __attribute__((target("rtm")))
      unsigned begin(void) { return _xbegin(); }
      __attribute__((target("rtm")))
      void end(void) { _xend(); }
      __attribute__((target("rtm")))
      void abort_transaction(void) { _xabort(255); }
      __attribute__((target("rtm")))
      int query(void) { return _xtest(); }
    )");
    for (const char *Target :
         {"--target=x86_64-unknown-linux-gnu",
          "--target=x86_64-pc-windows-msvc", "--target=x86_64-apple-darwin"}) {
      SCOPED_TRACE(Target);
      for (const char *Opt : {"-O0", "-O2"}) {
        SCOPED_TRACE(Opt);
        const auto AssemblyFile = tmpFile("transaction_headers.s");
        auto Result =
            ncc({Target, Opt, "-fno-lto", "-fno-builtin-mimalloc",
                 "-ffreestanding", "-Werror=implicit-function-declaration",
                 "-mllvm", "-verify-machineinstrs", "-S", Source.string(), "-o",
                 AssemblyFile.string()});
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Assembly = readFile(AssemblyFile);
        for (const char *Mnemonic : {"xbegin", "xend", "xabort", "xtest"})
          EXPECT_NE(Assembly.find(std::string("\t") + Mnemonic),
                    std::string::npos)
              << Assembly;
        EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
        EXPECT_EQ(Assembly.find("\tcall"), std::string::npos) << Assembly;
        const auto IRFile = tmpFile("transaction_headers.ll");
        Result = ncc({Target, Opt, "-fno-lto", "-fno-builtin-mimalloc",
                      "-ffreestanding", "-S", "-emit-llvm", Source.string(),
                      "-o", IRFile.string()});
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto IR = readFile(IRFile);
        for (const char *Name : {"xbegin", "xend", "xabort", "xtest"})
          EXPECT_NE(IR.find(std::string("@llvm.x86.") + Name),
                    std::string::npos)
              << IR;
        EXPECT_EQ(IR.find("asm "), std::string::npos) << IR;
      }
    }
  }
}

TEST_F(X86PrivilegedIntrinTest, BoundedVirtualizationUsesNativeInstructions) {
  constexpr const char *Source = R"(
    #include <intrin.h>
    unsigned long long probe(unsigned long long address, unsigned asid,
                             unsigned long long *ordinary) {
      *ordinary = address;
      __builtin_ia32_vmload(address);
      __builtin_ia32_vmsave(address);
      __builtin_ia32_invlpga(address, asid);
      __builtin_ia32_stgi();
      __builtin_ia32_clgi();
      __builtin_ia32_swapgs();
      __builtin_ia32_stac();
      __builtin_ia32_clac();
      __builtin_ia32_hlt();
      __builtin_ia32_vmfunc_eptp_switch(asid);
      __svm_vmload(address);
      __svm_vmsave(address);
      __svm_invlpga((void *)address, (int)asid);
      __svm_stgi();
      __svm_clgi();
      __halt();
      return *ordinary ^ address ^ asid;
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto [RC, Assembly] = compileToAsm(Source, "bounded_virtualization", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    for (const char *Mnemonic : {"vmload", "vmsave", "invlpga", "stgi", "clgi",
                                 "swapgs", "stac", "clac", "hlt", "vmfunc"})
      EXPECT_NE(Assembly.find(std::string("\t") + Mnemonic), std::string::npos)
          << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
    EXPECT_EQ(Assembly.find("call"), std::string::npos) << Assembly;
  }
}

TEST_F(X86PrivilegedIntrinTest, SvmAddressOverrideRetainsItsNativePrefix) {
  constexpr const char *Source = R"(
    unsigned probe(unsigned address, unsigned asid) {
      __builtin_ia32_vmload_addr32(address);
      __builtin_ia32_vmsave_addr32(address);
      __builtin_ia32_invlpga_addr32(address, asid);
      __builtin_ia32_invlpgb_addr32(address, asid, 0x80000001u);
      return address ^ asid;
    }
  )";
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto [RC, Assembly] = compileToAsm(Source, "svm_address_override", Opt);
    ASSERT_EQ(RC, 0) << Assembly;
    for (const char *Mnemonic : {"vmload", "vmsave", "invlpga", "invlpgb"})
      EXPECT_TRUE(std::regex_search(
          Assembly, std::regex(std::string("addr32[ \t]+") + Mnemonic)))
          << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
  }
}
