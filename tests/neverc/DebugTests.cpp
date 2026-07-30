#include "NeverCTestFixture.h"

#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/xxhash.h"

#include <cstring>
#include <regex>

class DebugTest : public NeverCTest {
protected:
  struct DwarfDumpTool {
    std::string path;
    std::string infoFlag;
    std::string lineFlag;
    bool isLibdwarf = false;
  };

  DwarfDumpTool findDwarfDump() const {
    if (exec("which", {"llvm-dwarfdump"}).exitCode == 0)
      return {"llvm-dwarfdump", "--debug-info", "--debug-line", false};

    if (exec("which", {"dwarfdump"}).exitCode == 0) {
      auto ver = exec("dwarfdump", {"--version"});
      if (ver.contains("libdwarf") || ver.contains("dwarfdump 2."))
        return {"dwarfdump", "-v -i", "-l", true};
      return {"dwarfdump", "--debug-info", "--debug-line", false};
    }

    return {};
  }

  CmdResult dwarfDumpInfo(const DwarfDumpTool &tool,
                          const std::string &obj) const {
    auto flags = splitFlags(tool.infoFlag);
    flags.push_back(obj);
    return exec(tool.path, flags);
  }

  CmdResult dwarfDumpLine(const DwarfDumpTool &tool,
                          const std::string &obj) const {
    auto flags = splitFlags(tool.lineFlag);
    flags.push_back(obj);
    return exec(tool.path, flags);
  }
};

static bool
inspectELFDebugCompression(llvm::StringRef Bytes,
                           llvm::DebugCompressionType CompressionType,
                           unsigned &CompressedSections, std::string &Reason) {
  using namespace llvm;
  using namespace llvm::object;

  CompressedSections = 0;
  auto ObjectOrErr = ObjectFile::createObjectFile(
      MemoryBufferRef(Bytes, "split-dwarf-compression-test"));
  if (!ObjectOrErr) {
    consumeError(ObjectOrErr.takeError());
    Reason = "cannot parse ELF object";
    return false;
  }
  if (!(*ObjectOrErr)->isELF() || (*ObjectOrErr)->getBytesInAddress() != 8 ||
      !(*ObjectOrErr)->isLittleEndian()) {
    Reason = "expected an ELF64LE object";
    return false;
  }

  const uint32_t ExpectedType = CompressionType == DebugCompressionType::Zlib
                                    ? ELF::ELFCOMPRESS_ZLIB
                                    : ELF::ELFCOMPRESS_ZSTD;
  for (const SectionRef &Section : (*ObjectOrErr)->sections()) {
    Expected<StringRef> NameOrErr = Section.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      Reason = "cannot read an ELF section name";
      return false;
    }
    const ELFSectionRef ELFSection(Section);
    if (!NameOrErr->starts_with(".debug_") ||
        !(ELFSection.getFlags() & ELF::SHF_COMPRESSED))
      continue;

    const uint64_t Offset = ELFSection.getOffset();
    const uint64_t Size = Section.getSize();
    if (Offset > Bytes.size() || Size > Bytes.size() - Offset ||
        Size < sizeof(ELF::Elf64_Chdr)) {
      Reason = "compressed debug section is truncated";
      return false;
    }
    ELF::Elf64_Chdr Header{};
    memcpy(&Header, Bytes.data() + Offset, sizeof(Header));
    if (Header.ch_type != ExpectedType || Header.ch_reserved != 0 ||
        Header.ch_size == 0 || Header.ch_addralign == 0 ||
        (Header.ch_addralign & (Header.ch_addralign - 1)) != 0) {
      Reason = "compressed debug section has an invalid ELF compression header";
      return false;
    }
    ++CompressedSections;
  }
  if (CompressedSections == 0) {
    Reason = "object contains no compressed debug sections";
    return false;
  }
  return true;
}

static bool inspectELFSplitDwarfSectionFlags(llvm::StringRef Bytes,
                                             std::string &Reason) {
  using namespace llvm;
  using namespace llvm::object;

  auto ObjectOrErr = ObjectFile::createObjectFile(
      MemoryBufferRef(Bytes, "split-dwarf-elf-flags-test"));
  if (!ObjectOrErr) {
    consumeError(ObjectOrErr.takeError());
    Reason = "cannot parse ELF object";
    return false;
  }
  if (!(*ObjectOrErr)->isELF()) {
    Reason = "expected an ELF object";
    return false;
  }

  unsigned SplitDwarfSections = 0;
  for (const SectionRef &Section : (*ObjectOrErr)->sections()) {
    Expected<StringRef> NameOrErr = Section.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      Reason = "cannot read an ELF section name";
      return false;
    }
    const bool IsDwoPayload = NameOrErr->ends_with(".dwo");
    const bool IsPackageIndex =
        *NameOrErr == ".debug_cu_index" || *NameOrErr == ".debug_tu_index";
    if (!IsDwoPayload && !IsPackageIndex)
      continue;
    ++SplitDwarfSections;
    if (!(ELFSectionRef(Section).getFlags() & ELF::SHF_EXCLUDE)) {
      Reason =
          ("Split-DWARF section '" + *NameOrErr + "' lacks SHF_EXCLUDE").str();
      return false;
    }
  }
  if (SplitDwarfSections == 0) {
    Reason = "ELF split-DWARF object contains no split-debug sections";
    return false;
  }
  return true;
}

static bool inspectCOFFSplitDwarfSectionFlags(llvm::StringRef Bytes,
                                              std::string &Reason) {
  using namespace llvm;
  using namespace llvm::object;

  auto ObjectOrErr = ObjectFile::createObjectFile(
      MemoryBufferRef(Bytes, "split-dwarf-coff-flags-test"));
  if (!ObjectOrErr) {
    consumeError(ObjectOrErr.takeError());
    Reason = "cannot parse COFF object";
    return false;
  }
  const auto *COFFObject = dyn_cast<COFFObjectFile>(ObjectOrErr->get());
  if (!COFFObject) {
    Reason = "expected a COFF object";
    return false;
  }

  unsigned SplitDwarfSections = 0;
  for (const SectionRef &Section : COFFObject->sections()) {
    Expected<StringRef> NameOrErr = Section.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      Reason = "cannot read a COFF section name";
      return false;
    }
    const bool IsDwoPayload = NameOrErr->ends_with(".dwo");
    const bool IsPackageIndex =
        *NameOrErr == ".debug_cu_index" || *NameOrErr == ".debug_tu_index";
    if (!IsDwoPayload && !IsPackageIndex)
      continue;
    ++SplitDwarfSections;
    if (!(COFFObject->getCOFFSection(Section)->Characteristics &
          COFF::IMAGE_SCN_LNK_REMOVE)) {
      Reason = ("Split-DWARF section '" + *NameOrErr +
                "' is not marked IMAGE_SCN_LNK_REMOVE")
                   .str();
      return false;
    }
  }
  if (SplitDwarfSections == 0) {
    Reason = "COFF split-DWARF object contains no split-debug sections";
    return false;
  }
  return true;
}

TEST_F(DebugTest, HostDWARF) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("host_dwarf.o");
  auto exe = tmpFile("host_dwarf");

  std::vector<std::string> base;
  for (auto &f : sysrootFlags())
    base.push_back(f);
  for (auto &f : archFlags())
    base.push_back(f);

  auto c = base;
  c.insert(c.end(),
           {"-std=c11", "-g", "-fno-lto", "-c", src, "-o", obj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0) << "host-dwarf compile";

  auto l = base;
  l.insert(l.end(), {"-std=c11", "-g", "-fno-lto", src, "-o", exe.string()});
  ASSERT_EQ(ncc(l).exitCode, 0) << "host-dwarf link";

  // dwarfdump verification
  auto tool = findDwarfDump();
  if (tool.path.empty()) {
    GTEST_SKIP() << "dwarfdump not available";
    return;
  }

  auto dump = dwarfDumpInfo(tool, obj.string());
  EXPECT_TRUE(dump.contains("DW_TAG_compile_unit"));
  EXPECT_TRUE(dump.contains("neverc")) << "producer not neverc";

  for (auto *fn : {"compute_area", "shape_score", "main"}) {
    SCOPED_TRACE(fn);
    EXPECT_TRUE(dump.contains(fn)) << fn << " subprogram missing";
  }

  EXPECT_TRUE(dump.contains("argc")) << "argc parameter missing";

  for (auto *ty : {"Point", "Rect", "Shape", "Color"}) {
    SCOPED_TRACE(ty);
    EXPECT_TRUE(dump.contains(ty)) << ty << " type missing";
  }

  auto lineDump = dwarfDumpLine(tool, obj.string());
  EXPECT_TRUE(lineDump.contains("test_dwarf_debug.c"));
}

TEST_F(DebugTest, WindowsCOFFDWARF) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("win_dwarf.obj");

  auto r = ncc({"--target=x86_64-pc-windows-msvc", "-gdwarf-5", "-fno-lto",
                "-c", src, "-o", obj.string()});
  ASSERT_EQ(r.exitCode, 0) << "windows-dwarf compile\n" << r.err;

  std::string objdumpTool;
  if (exec("which", {"llvm-objdump"}).exitCode == 0)
    objdumpTool = "llvm-objdump";
  else if (exec("which", {"objdump"}).exitCode == 0)
    objdumpTool = "objdump";
  else {
    GTEST_SKIP() << "objdump not available";
    return;
  }
  auto sections = exec(objdumpTool, {"-h", obj.string()});
  if (!sections.contains(".text")) {
    GTEST_SKIP() << objdumpTool << " cannot parse COFF format on this host";
    return;
  }
  for (auto *sect : {".debug_info", ".debug_abbrev", ".debug_line",
                     ".debug_str"}) {
    SCOPED_TRACE(sect);
    EXPECT_TRUE(sections.contains(sect)) << sect << " section missing";
  }

  auto tool = findDwarfDump();
  if (!tool.path.empty()) {
    auto dump = dwarfDumpInfo(tool, obj.string());
    if (dump.contains("DW_TAG_compile_unit")) {
      EXPECT_TRUE(dump.contains("0x0005") || dump.contains("DWARF version 5")
                  || dump.contains("version_stamp    = 0x0005"))
          << "DWARF version 5 expected";
    }
  }
}

TEST_F(DebugTest, LinuxELFDWARF) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("linux_dwarf.o");

  auto r = ncc({"--target=x86_64-linux-gnu", "-gdwarf-5", "-fno-lto", "-c",
                src, "-o", obj.string()});
  ASSERT_EQ(r.exitCode, 0) << "linux-dwarf compile\n" << r.err;

  auto tool = findDwarfDump();
  if (!tool.path.empty()) {
    auto dump = dwarfDumpInfo(tool, obj.string());
    if (dump.contains("DW_TAG_compile_unit")) {
      EXPECT_TRUE(dump.contains("0x0005") || dump.contains("DWARF version 5")
                  || dump.contains("version_stamp    = 0x0005"));
      EXPECT_TRUE(dump.contains("main"));
    }
  }
}

TEST_F(DebugTest, AArch64DWARF) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("aarch64_dwarf.o");

  auto r = ncc({"--target=aarch64-linux-gnu", "-gdwarf-5", "-fno-lto", "-c",
                src, "-o", obj.string()});
  ASSERT_EQ(r.exitCode, 0) << "aarch64-dwarf compile\n" << r.err;

  auto tool = findDwarfDump();
  if (!tool.path.empty()) {
    auto dump = dwarfDumpInfo(tool, obj.string());
    if (dump.contains("DW_TAG_compile_unit")) {
      EXPECT_TRUE(dump.contains("0x0005") || dump.contains("DWARF version 5")
                  || dump.contains("version_stamp    = 0x0005"));
      EXPECT_TRUE(dump.contains("main"));
    }
  }
}

// Flags that make parallel codegen split a small module, so the partition
// paths below are exercised without needing hundreds of functions.
static std::vector<std::string> forcePartitions(unsigned N) {
  return {"-fparallel-codegen=" + std::to_string(N), "-mllvm",
          "-neverc-pcg-min-funcs=2", "-mllvm", "-neverc-pcg-weight-floor=0"};
}

static std::string makePartitionedDebugSource(const std::string &NamePrefix,
                                              int NumFns) {
  std::string Code;
  for (int I = 0; I < NumFns; ++I)
    Code += "int " + NamePrefix + std::to_string(I) +
            "(int a){int s=0;for(int k=0;k<a;k++)s+=k*" + std::to_string(I) +
            ";return s;}\n";
  Code += "int main(void){int t=0;\n";
  for (int I = 0; I < NumFns; ++I)
    Code += "  t += " + NamePrefix + std::to_string(I) + "(3);\n";
  Code += "  return t;}\n";
  return Code;
}

static std::string multiCUFunctionName(unsigned CU, unsigned Partition) {
  for (unsigned Suffix = 0;; ++Suffix) {
    std::string Name = "parallel_cu" + std::to_string(CU) + "_partition" +
                       std::to_string(Partition) + "_" + std::to_string(Suffix);
    if (llvm::xxh3_64bits(Name) % 4 == Partition)
      return Name;
  }
}

static std::string makeMultiCompileUnitBitcode() {
  using namespace llvm;

  LLVMContext Context;
  Module M("parallel-split-dwarf-multi-cu", Context);
  M.setTargetTriple("x86_64-unknown-linux-gnu");
  M.addModuleFlag(Module::Warning, "Dwarf Version", 5);
  M.addModuleFlag(Module::Warning, "Debug Info Version",
                  DEBUG_METADATA_VERSION);

  DIBuilder DIB0(M);
  DIBuilder DIB1(M);
  DIBuilder *DIBs[] = {&DIB0, &DIB1};
  DIFile *Files[] = {DIB0.createFile("parallel-cu0.c", "/"),
                     DIB1.createFile("parallel-cu1.c", "/")};
  DIB0.createCompileUnit(
      dwarf::DW_LANG_C11, Files[0], "neverc multi-CU test",
      /*isOptimized=*/false, /*Flags=*/"", /*RV=*/0,
      /*SplitName=*/"parallel-multi-cu.dwo",
      DICompileUnit::DebugEmissionKind::FullDebug, /*DWOId=*/0,
      /*SplitDebugInlining=*/false, /*DebugInfoForProfiling=*/false,
      DICompileUnit::DebugNameTableKind::GNU);
  DIB1.createCompileUnit(
      dwarf::DW_LANG_C11, Files[1], "neverc multi-CU test",
      /*isOptimized=*/false, /*Flags=*/"", /*RV=*/0,
      /*SplitName=*/"parallel-multi-cu.dwo",
      DICompileUnit::DebugEmissionKind::FullDebug, /*DWOId=*/0,
      /*SplitDebugInlining=*/false, /*DebugInfoForProfiling=*/false,
      DICompileUnit::DebugNameTableKind::GNU);
  DISubroutineType *DebugFnTypes[2];
  for (unsigned CU = 0; CU != 2; ++CU) {
    DIType *IntDebugTy =
        DIBs[CU]->createBasicType("int", 32, dwarf::DW_ATE_signed);
    DITypeRefArray DebugParamTypes =
        DIBs[CU]->getOrCreateTypeArray({IntDebugTy, IntDebugTy});
    DebugFnTypes[CU] = DIBs[CU]->createSubroutineType(DebugParamTypes);
  }

  Type *IntTy = Type::getInt32Ty(Context);
  FunctionType *FnTy = FunctionType::get(IntTy, {IntTy}, false);
  for (unsigned I = 0; I != 8; ++I) {
    const unsigned CU = I / 4;
    const unsigned Partition = I % 4;
    const std::string Name = multiCUFunctionName(CU, Partition);
    Function *F = Function::Create(FnTy, GlobalValue::ExternalLinkage, Name, M);
    F->getArg(0)->setName("value");
    DISubprogram *SP = DIBs[CU]->createFunction(
        Files[CU], Name, Name, Files[CU], I + 1, DebugFnTypes[CU], I + 1,
        DINode::FlagPrototyped, DISubprogram::SPFlagDefinition);
    F->setSubprogram(SP);

    BasicBlock *Entry = BasicBlock::Create(Context, "entry", F);
    IRBuilder<> Builder(Entry);
    Builder.SetCurrentDebugLocation(DILocation::get(Context, I + 1, 1, SP));
    Value *Result =
        Builder.CreateAdd(F->getArg(0), ConstantInt::get(IntTy, I + 1));
    Builder.CreateRet(Result);
  }
  DIB0.finalize();
  DIB1.finalize();

  SmallVector<char, 0> Bitcode;
  raw_svector_ostream OS(Bitcode);
  WriteBitcodeToFile(M, OS);
  return std::string(Bitcode.begin(), Bitcode.end());
}

class ScopedEnvironmentVariable {
public:
  ScopedEnvironmentVariable(const char *Name, const char *Value) : Name(Name) {
    if (const char *Current = std::getenv(Name)) {
      HadPrevious = true;
      Previous = Current;
    }
#ifdef _WIN32
    _putenv_s(Name, Value ? Value : "");
#else
    if (Value)
      setenv(Name, Value, 1);
    else
      unsetenv(Name);
#endif
  }

  ~ScopedEnvironmentVariable() {
#ifdef _WIN32
    _putenv_s(Name.c_str(), HadPrevious ? Previous.c_str() : "");
#else
    if (HadPrevious)
      setenv(Name.c_str(), Previous.c_str(), 1);
    else
      unsetenv(Name.c_str());
#endif
  }

  ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;
  ScopedEnvironmentVariable &
  operator=(const ScopedEnvironmentVariable &) = delete;

private:
  std::string Name;
  std::string Previous;
  bool HadPrevious = false;
};

// Turns the merger's silent serial-codegen fallback into a hard error, so a
// partitioning regression fails the test instead of quietly compiling slower.
class StrictParallelCodegen {
public:
  StrictParallelCodegen() : Strict("NEVERC_PCG_STRICT", "1") {}
  StrictParallelCodegen(const StrictParallelCodegen &) = delete;
  StrictParallelCodegen &operator=(const StrictParallelCodegen &) = delete;

private:
  ScopedEnvironmentVariable Strict;
};

// Split DWARF is a true dual-output parallel operation: every partition emits a
// skeleton object and a relocation-free `.dwo`; both are merged in memory,
// cross-checked by DWO ID, then committed together. Strict/debug mode proves
// this test entered that path instead of passing through the serial fallback.
TEST_F(DebugTest, SplitDwarfIsMergedByParallelCodegen) {
  StrictParallelCodegen Strict;
  ScopedEnvironmentVariable Debug("NEVERC_PCG_DEBUG", "1");
  auto src = tmpFile("pcg_split_dwarf.c");
  auto obj = tmpFile("pcg_split_dwarf.o");
  auto dwo = obj;
  dwo.replace_extension(".dwo");
  writeFile(src, makePartitionedDebugSource("split_dwarf_", 24));

  std::vector<std::string> args = {
      "--target=x86_64-unknown-linux-gnu", "-gdwarf-5", "-gsplit-dwarf",
      "-fno-lto", "-fno-builtin-mimalloc",
  };
  for (auto &flag : forcePartitions(4))
    args.push_back(flag);
  args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});

  auto result = ncc(args);
  ASSERT_EQ(result.exitCode, 0) << result.err;
  EXPECT_TRUE(result.stderrContains("[pcg] SUCCESS: merged")) << result.err;
  EXPECT_TRUE(result.stderrContains("split-DWARF contributions")) << result.err;
  EXPECT_FALSE(readFile(dwo).empty())
      << "successful split-DWARF compilation discarded its .dwo output";

  auto tool = findDwarfDump();
  if (tool.path.empty())
    GTEST_SKIP() << "dwarfdump not available";
  auto dump = dwarfDumpInfo(tool, dwo.string());
  ASSERT_EQ(dump.exitCode, 0) << dump.err;
  EXPECT_TRUE(dump.contains("DW_UT_split_compile"));
  for (int I = 0; I < 24; ++I)
    EXPECT_TRUE(dump.contains("split_dwarf_" + std::to_string(I)))
        << "partition " << I << " debug name is missing";
}

TEST_F(DebugTest, MultiCompileUnitSplitDwarfStaysParallel) {
  StrictParallelCodegen Strict;
  ScopedEnvironmentVariable Debug("NEVERC_PCG_DEBUG", "1");
  auto bitcode = tmpFile("pcg_split_dwarf_multi_cu.bc");
  auto obj = tmpFile("pcg_split_dwarf_multi_cu.o");
  auto dwo = obj;
  dwo.replace_extension(".dwo");
  writeFile(bitcode, makeMultiCompileUnitBitcode());

  std::vector<std::string> args = {
      "--target=x86_64-unknown-linux-gnu",
      "-gdwarf-5",
      "-gsplit-dwarf",
      "-fno-lto",
      "-fno-builtin-mimalloc",
  };
  for (auto &Flag : forcePartitions(4))
    args.push_back(Flag);
  args.insert(args.end(), {"-mllvm", "-neverc-pcg-cg-weight-div=1"});
  args.insert(args.end(), {"-c", bitcode.string(), "-o", obj.string()});

  const auto Result = ncc(args);
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  ASSERT_TRUE(Result.stderrContains("[pcg] SUCCESS: merged")) << Result.err;
  ASSERT_FALSE(readFile(dwo).empty());

  const auto Tool = findDwarfDump();
  if (Tool.path.empty())
    GTEST_SKIP() << "dwarfdump not available";
  const auto Dump = dwarfDumpInfo(Tool, dwo.string());
  ASSERT_EQ(Dump.exitCode, 0) << Dump.err;
  for (unsigned CU = 0; CU != 2; ++CU)
    for (unsigned Partition = 0; Partition != 4; ++Partition)
      EXPECT_TRUE(Dump.contains(multiCUFunctionName(CU, Partition)));
}

TEST_F(DebugTest, SplitDwarfMergeFailureFallsBackTransactionally) {
  // CI may enable strict mode globally; this test deliberately exercises the
  // production fallback instead.
  ScopedEnvironmentVariable NoStrict("NEVERC_PCG_STRICT", nullptr);
  ScopedEnvironmentVariable Debug("NEVERC_PCG_DEBUG", "1");
  auto src = tmpFile("pcg_split_fallback.c");
  auto obj = tmpFile("pcg_split_fallback.o");
  auto dwo = obj;
  dwo.replace_extension(".dwo");
  writeFile(src, makePartitionedDebugSource("split_fallback_", 16));

  const std::vector<std::string> Common = {
      "--target=x86_64-unknown-linux-gnu",
      "-gdwarf-5",
      "-gsplit-dwarf",
      "-fno-lto",
      "-fno-builtin-mimalloc",
  };
  auto FinishArgs = [&](std::vector<std::string> Args) {
    Args.insert(Args.end(), {"-c", src.string(), "-o", obj.string()});
    return Args;
  };

  auto SerialArgs = Common;
  SerialArgs.push_back("-fno-parallel-codegen");
  const auto SerialResult = ncc(FinishArgs(std::move(SerialArgs)));
  ASSERT_EQ(SerialResult.exitCode, 0) << SerialResult.err;
  const std::string SerialObject = readFile(obj);
  const std::string SerialDwo = readFile(dwo);
  ASSERT_FALSE(SerialObject.empty());
  ASSERT_FALSE(SerialDwo.empty());

  auto ParallelArgs = Common;
  for (auto &Flag : forcePartitions(4))
    ParallelArgs.push_back(Flag);
  CmdResult FallbackResult;
  {
    ScopedEnvironmentVariable ForceFailure("NEVERC_PCG_FORCE_MERGE_FAIL", "1");
    FallbackResult = ncc(FinishArgs(std::move(ParallelArgs)));
  }
  ASSERT_EQ(FallbackResult.exitCode, 0) << FallbackResult.err;
  EXPECT_TRUE(FallbackResult.stderrContains("[pcg] FALLBACK:"))
      << FallbackResult.err;
  EXPECT_EQ(readFile(obj), SerialObject)
      << "parallel failure left bytes ahead of the serial object";
  EXPECT_EQ(readFile(dwo), SerialDwo)
      << "parallel failure left bytes ahead of the serial DWO";
}

TEST_F(DebugTest, SplitDwarfParallelCodegenCoversELFAndCOFFTargets) {
  StrictParallelCodegen Strict;
  ScopedEnvironmentVariable Debug("NEVERC_PCG_DEBUG", "1");
  auto src = tmpFile("pcg_split_dwarf_targets.c");
  writeFile(src, makePartitionedDebugSource("split_target_", 16));

  struct TargetCase {
    const char *Name;
    const char *Triple;
    const char *ObjectSuffix;
  };
  constexpr TargetCase Targets[] = {
      {"x64_linux", "x86_64-unknown-linux-gnu", ".o"},
      {"arm64_linux", "aarch64-unknown-linux-gnu", ".o"},
      {"x64_windows", "x86_64-pc-windows-msvc", ".obj"},
      {"arm64_windows", "aarch64-pc-windows-msvc", ".obj"},
  };

  for (const TargetCase &Target : Targets) {
    SCOPED_TRACE(Target.Triple);
    auto obj = tmpFile(std::string("pcg_split_target_") + Target.Name +
                       Target.ObjectSuffix);
    auto dwo = obj;
    dwo.replace_extension(".dwo");
    std::vector<std::string> args = {
        std::string("--target=") + Target.Triple,
        "-gdwarf-5",
        "-gsplit-dwarf",
        "-fno-lto",
        "-fno-builtin-mimalloc",
    };
    for (auto &Flag : forcePartitions(4))
      args.push_back(Flag);
    args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});

    const auto Result = ncc(args);
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    EXPECT_TRUE(Result.stderrContains("[pcg] SUCCESS: merged")) << Result.err;
    EXPECT_TRUE(Result.stderrContains("split-DWARF contributions"))
        << Result.err;
    EXPECT_FALSE(readFile(obj).empty());
    const std::string DwoBytes = readFile(dwo);
    EXPECT_FALSE(DwoBytes.empty());
    if (llvm::StringRef(Target.Triple).contains("windows")) {
      std::string Reason;
      EXPECT_TRUE(inspectCOFFSplitDwarfSectionFlags(DwoBytes, Reason))
          << Reason;
    } else {
      std::string Reason;
      EXPECT_TRUE(inspectELFSplitDwarfSectionFlags(DwoBytes, Reason)) << Reason;
    }
  }
}

TEST_F(DebugTest, CompressedSplitDwarfIsDeterministicAcrossWorkerCounts) {
  StrictParallelCodegen Strict;
  ScopedEnvironmentVariable Debug("NEVERC_PCG_DEBUG", "1");
  auto src = tmpFile("pcg_compressed_split_dwarf.c");
  writeFile(src, makePartitionedDebugSource("compressed_split_", 24));

  struct Target {
    const char *Name;
    const char *Triple;
  };
  constexpr Target Targets[] = {
      {"x64", "x86_64-unknown-linux-gnu"},
      {"arm64", "aarch64-unknown-linux-gnu"},
  };
  struct Codec {
    llvm::DebugCompressionType Type;
    const char *Name;
  };
  constexpr Codec Codecs[] = {
      {llvm::DebugCompressionType::Zlib, "zlib"},
      {llvm::DebugCompressionType::Zstd, "zstd"},
  };

  unsigned TestedConfigurations = 0;
  for (const Target &Target : Targets) {
    for (const Codec &Codec : Codecs) {
      SCOPED_TRACE(std::string(Target.Triple) + " / " + Codec.Name);
      const llvm::compression::Format Format =
          llvm::compression::formatFor(Codec.Type);
      if (llvm::compression::getReasonIfUnsupported(Format))
        continue;
      ++TestedConfigurations;

      auto obj = tmpFile(std::string("pcg_compressed_") + Target.Name + "_" +
                         Codec.Name + ".o");
      auto dwo = obj;
      dwo.replace_extension(".dwo");
      std::vector<std::string> args = {
          std::string("--target=") + Target.Triple,
          "-gdwarf-5",
          "-gsplit-dwarf",
          std::string("-gz=") + Codec.Name,
          "-fno-lto",
          "-fno-builtin-mimalloc",
      };
      for (auto &Flag : forcePartitions(4))
        args.push_back(Flag);
      args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});

      CmdResult OneThreadResult;
      {
        ScopedEnvironmentVariable Workers("NEVERC_PCG_THREADS", "1");
        OneThreadResult = ncc(args);
      }
      ASSERT_EQ(OneThreadResult.exitCode, 0) << OneThreadResult.err;
      ASSERT_TRUE(OneThreadResult.stderrContains("[pcg] SUCCESS: merged"))
          << OneThreadResult.err;

      const std::string OneThreadObject = readFile(obj);
      const std::string OneThreadDwo = readFile(dwo);
      ASSERT_FALSE(OneThreadObject.empty());
      ASSERT_FALSE(OneThreadDwo.empty());
      unsigned ObjectCompressedSections = 0;
      unsigned DwoCompressedSections = 0;
      std::string Reason;
      ASSERT_TRUE(inspectELFDebugCompression(OneThreadObject, Codec.Type,
                                             ObjectCompressedSections, Reason))
          << "main object: " << Reason;
      ASSERT_TRUE(inspectELFDebugCompression(OneThreadDwo, Codec.Type,
                                             DwoCompressedSections, Reason))
          << "DWO: " << Reason;

      {
        ScopedEnvironmentVariable Workers("NEVERC_PCG_THREADS", "4");
        const auto FourThreadResult = ncc(args);
        ASSERT_EQ(FourThreadResult.exitCode, 0) << FourThreadResult.err;
        ASSERT_TRUE(FourThreadResult.stderrContains("[pcg] SUCCESS: merged"))
            << FourThreadResult.err;
      }
      EXPECT_EQ(readFile(obj), OneThreadObject)
          << "main object depends on worker scheduling";
      EXPECT_EQ(readFile(dwo), OneThreadDwo)
          << "DWO depends on worker scheduling";

      const auto Tool = findDwarfDump();
      if (!Tool.path.empty()) {
        const auto Dump = dwarfDumpInfo(Tool, dwo.string());
        ASSERT_EQ(Dump.exitCode, 0) << Dump.err;
        // Some platform dwarfdump builds exit successfully but do not
        // implement SHF_COMPRESSED DWP sections. When the tool does expose
        // the units, cross-check their names; the compiler's in-process pair
        // verifier has already parsed every compressed unit before commit.
        if (Dump.contains("DW_UT_split_compile"))
          for (int I = 0; I < 24; ++I)
            EXPECT_TRUE(Dump.contains("compressed_split_" + std::to_string(I)))
                << "partition " << I
                << " debug name is missing after decompression";
      }
    }
  }

  if (TestedConfigurations == 0)
    GTEST_SKIP() << "this build has neither zlib nor zstd support";
}

// Ordinary `-g` must keep using parallel codegen. The Split-DWARF dual-output
// path is gated on an auxiliary `.dwo` stream, not on the presence of DWARF.
TEST_F(DebugTest, OrdinaryDebugStillUsesParallelCodegen) {
  StrictParallelCodegen Strict;
  ScopedEnvironmentVariable Debug("NEVERC_PCG_DEBUG", "1");
  auto src = tmpFile("pcg_ordinary_debug.c");
  auto obj = tmpFile("pcg_ordinary_debug.o");
  writeFile(src, makePartitionedDebugSource("ordinary_debug_", 24));

  std::vector<std::string> args = {
      "--target=x86_64-unknown-linux-gnu",
      "-g",
      "-fno-lto",
      "-fno-builtin-mimalloc",
  };
  for (auto &Flag : forcePartitions(4))
    args.push_back(Flag);
  args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});

  const auto Result = ncc(args);
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(Result.stderrContains("[pcg] SUCCESS: merged")) << Result.err;
  EXPECT_FALSE(Result.stderrContains("split-DWARF contributions"))
      << Result.err;
  EXPECT_FALSE(fs::exists(obj.parent_path() / (obj.stem().string() + ".dwo")));
}

// `=single` keeps fission layout inside one object and must stay on the
// parallel path. Its partition DWO payloads are packaged on private streams
// and embedded only after the same cross-artifact verification as external
// split DWARF.
TEST_F(DebugTest, SplitDwarfSingleModeStaysOnParallelCodegen) {
  StrictParallelCodegen Strict;
  ScopedEnvironmentVariable Debug("NEVERC_PCG_DEBUG", "1");
  auto src = tmpFile("pcg_split_single.c");
  auto obj = tmpFile("pcg_split_single.o");
  writeFile(src, makePartitionedDebugSource("split_single_", 24));

  std::vector<std::string> args = {
      "--target=x86_64-unknown-linux-gnu",
      "-gdwarf-5",
      "-gsplit-dwarf=single",
      "-fno-lto",
      "-fno-builtin-mimalloc",
  };
  for (auto &Flag : forcePartitions(4))
    args.push_back(Flag);
  args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});

  const auto Result = ncc(args);
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(Result.stderrContains("[pcg] SUCCESS: merged")) << Result.err;
  EXPECT_TRUE(Result.stderrContains("embedded split-DWARF contributions"))
      << Result.err;
  EXPECT_FALSE(fs::exists(obj.parent_path() / (obj.stem().string() + ".dwo")));
  std::string Reason;
  EXPECT_TRUE(inspectELFSplitDwarfSectionFlags(readFile(obj), Reason))
      << Reason;
}

// NeverC's C-only frontend has no C++ ODR identifiers, so
// -fdebug-types-section cannot itself manufacture a DW_UT_split_type here.
// DwarfPackageTest covers real DWARF 5 type-unit rows; this integration test
// ensures both the DWARF64 encoding and the type-unit option keep the dual
// output path enabled.
TEST_F(DebugTest, SplitDwarf64AndTypeUnitOptionRemainParallel) {
  StrictParallelCodegen Strict;
  ScopedEnvironmentVariable Debug("NEVERC_PCG_DEBUG", "1");
  auto src = tmpFile("pcg_split_dwarf64_types.c");
  writeFile(src, makePartitionedDebugSource("split_dwarf64_", 16));

  struct Case {
    const char *Name;
    std::vector<std::string> Extra;
  };
  const Case Cases[] = {
      {"dwarf64", {"-gdwarf64"}},
      {"debug_types", {"-fdebug-types-section"}},
  };

  for (const Case &C : Cases) {
    SCOPED_TRACE(C.Name);
    auto obj = tmpFile(std::string("pcg_split_") + C.Name + ".o");
    auto dwo = obj;
    dwo.replace_extension(".dwo");
    std::vector<std::string> args = {
        "--target=x86_64-unknown-linux-gnu",
        "-gdwarf-5",
        "-gsplit-dwarf",
        "-fno-lto",
        "-fno-builtin-mimalloc",
    };
    args.insert(args.end(), C.Extra.begin(), C.Extra.end());
    for (auto &Flag : forcePartitions(4))
      args.push_back(Flag);
    args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});

    const auto Result = ncc(args);
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    EXPECT_TRUE(Result.stderrContains("[pcg] SUCCESS: merged")) << Result.err;
    EXPECT_TRUE(Result.stderrContains("split-DWARF contributions"))
        << Result.err;
    EXPECT_FALSE(readFile(obj).empty());
    EXPECT_FALSE(readFile(dwo).empty());
  }
}

// Whatever an alias resolves to has to keep its definition in every partition.
// The rewrite that drops aliases from partitions other than 0 cannot run until
// materializeAll(), which is itself what runs the verifier, so demoting the
// target to a declaration first leaves the module malformed and the compile
// aborts with "Alias must point to a definition".  Only -g reaches it, because
// UpgradeDebugInfo is what calls verifyModule.
//
// Function targets were already handled; global variables, thread-locals and
// alias chains reach the same verifier and were not.
//
// The target has to survive only that long, though: once the aliases are gone
// it must stop being a definition, or every partition emits one and the merge
// refuses the duplicates.  Strict mode is what makes that second half fail the
// test rather than fall back to serial codegen unnoticed.
TEST_F(DebugTest, ParallelCodegenKeepsAliasTargetsDefined) {
  StrictParallelCodegen Strict;
  auto src = tmpFile("pcg_alias.c");
  std::string code = R"(
int plain_var = 42;
extern int plain_alias __attribute__((alias("plain_var")));

__thread int tls_var = 7;
extern __thread int tls_alias __attribute__((alias("tls_var")));

int chain_var = 5;
extern int chain_mid __attribute__((alias("chain_var")));
extern int chain_top __attribute__((alias("chain_mid")));

int base_fn(int x) { return x + 1; }
extern int fn_alias(int) __attribute__((alias("base_fn")));
)";
  // Enough binnable functions that the targets above cannot all land in
  // partition 0 by chance.
  for (int I = 0; I < 24; ++I)
    code += "int spread_" + std::to_string(I) + "(int a){int s=0;for(int k=0;"
            "k<a;k++)s+=k*" + std::to_string(I) + ";return s;}\n";
  code += "int main(void){int t=plain_alias+tls_alias+chain_top+fn_alias(1);\n";
  for (int I = 0; I < 24; ++I)
    code += "  t += spread_" + std::to_string(I) + "(3);\n";
  code += "  return t;}\n";
  writeFile(src, code);

  auto obj = tmpFile("pcg_alias.o");
  // ELF: the only format of the three whose C frontend accepts alias
  // attributes on variables.
  std::vector<std::string> args = {"--target=x86_64-unknown-linux-gnu",
                                   "-gdwarf-5", "-fno-lto"};
  for (auto &f : forcePartitions(4))
    args.push_back(f);
  args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});

  auto r = ncc(args);
  EXPECT_EQ(r.exitCode, 0)
      << "alias targets lost their definition during partitioning\n"
      << r.err;
}

// Mach-O writes cross-section DWARF references as plain offsets rather than
// relocations, so merging partitions has to re-point them.  Under DWARF 5 the
// names in .debug_info are indices through .debug_str_offsets, and leaving
// that table alone makes every partition after the first resolve its names
// against partition 0's strings -- silently, with nothing malformed for a
// debugger to report.  The symptom is duplicated and empty function names.
TEST_F(DebugTest, MachOParallelCodegenResolvesDwarf5Names) {
  if (!isDarwin())
    GTEST_SKIP() << "Mach-O is the only format that offsets DWARF by value";

  StrictParallelCodegen Strict;
  constexpr int NumFns = 24;
  auto src = tmpFile("pcg_names.c");
  writeFile(src, makePartitionedDebugSource("uniquely_named_", NumFns));

  auto obj = tmpFile("pcg_names.o");
  std::vector<std::string> args = {"-gdwarf-5", "-fno-lto"};
  for (auto &f : forcePartitions(4))
    args.push_back(f);
  args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});
  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << r.err;

  auto tool = findDwarfDump();
  if (tool.path.empty())
    GTEST_SKIP() << "dwarfdump not available";

  auto dump = dwarfDumpInfo(tool, obj.string());
  ASSERT_TRUE(dump.contains("DW_TAG_subprogram"));
  for (int I = 0; I < NumFns; ++I) {
    std::string Name = "uniquely_named_" + std::to_string(I);
    // Anchored at the end so uniquely_named_1 cannot match uniquely_named_12.
    // The two dumpers differ in how they close the value: llvm/Apple print
    // DW_AT_name ("x"), libdwarf prints (indexed string: 0x..)x at end of line.
    EXPECT_TRUE(dump.contains(Name + "\"") || dump.contains(Name + "\n"))
        << Name << " missing: a partition's names resolved against another "
                   "partition's string table";
  }
}

TEST_F(DebugTest, MachOParallelCodegenRebasesLegacyLineOffsets) {
  if (!isDarwin())
    GTEST_SKIP() << "Mach-O is the only format that offsets DWARF by value";

  auto tool = findDwarfDump();
  if (tool.path.empty() || tool.isLibdwarf)
    GTEST_SKIP() << "an LLVM-compatible dwarfdump is required";

  StrictParallelCodegen Strict;
  auto src = tmpFile("pcg_legacy_dwarf.c");
  writeFile(src, makePartitionedDebugSource("legacy_partition_", 24));

  for (int Version : {2, 3}) {
    auto obj =
        tmpFile("pcg_dwarf" + std::to_string(Version) + "_line_offsets.o");
    std::vector<std::string> args = {
        "-gdwarf-" + std::to_string(Version), "-fno-lto"};
    for (auto &f : forcePartitions(4))
      args.push_back(f);
    args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});
    auto compile = ncc(args);
    ASSERT_EQ(compile.exitCode, 0) << compile.err;

    auto verify = exec(tool.path, {"--verify", obj.string()});
    EXPECT_EQ(verify.exitCode, 0)
        << "DWARF " << Version
        << " encoded DW_AT_stmt_list as an unrebased DW_FORM_data4\n"
        << verify.out << verify.err;
  }
}

// .debug_aranges stores a plain .debug_info unit offset in each contribution.
// Without rebasing, every partition's address ranges claim to describe CU 0:
// the section remains structurally valid, but address-to-source lookup selects
// the wrong compile unit for every partition after the first.
TEST_F(DebugTest, MachOParallelCodegenRebasesArangesUnitOffsets) {
  if (!isDarwin())
    GTEST_SKIP() << "Mach-O is the only format that offsets DWARF by value";

  auto tool = findDwarfDump();
  if (tool.path.empty())
    GTEST_SKIP() << "dwarfdump not available";

  StrictParallelCodegen Strict;
  auto src = tmpFile("pcg_aranges.c");
  writeFile(src, makePartitionedDebugSource("arange_partition_", 24));

  auto obj = tmpFile("pcg_aranges.o");
  std::vector<std::string> args = {"-gdwarf-5", "-gdwarf-aranges", "-fno-lto"};
  for (auto &f : forcePartitions(4))
    args.push_back(f);
  args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});
  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << r.err;

  auto dump = tool.isLibdwarf
                  ? exec(tool.path, {"-r", "-v", obj.string()})
                  : exec(tool.path, {"--debug-aranges", obj.string()});
  ASSERT_EQ(dump.exitCode, 0) << dump.err;
  const std::regex NonZeroCUOffset(
      R"((cu_offset\s*=\s*|overall offset\s*=\s*)0x0*[1-9a-fA-F][0-9a-fA-F]*)");
  EXPECT_TRUE(std::regex_search(dump.out, NonZeroCUOffset))
      << "all merged address ranges still point at the first compile unit\n"
      << dump.out;
}

TEST_F(DebugTest, MachOParallelCodegenDropsIncompleteAppleAccelerators) {
  if (!isDarwin())
    GTEST_SKIP() << "Apple accelerator tables are Mach-O-specific";

  StrictParallelCodegen Strict;
  constexpr int NumFns = 24;
  auto src = tmpFile("pcg_apple_names.c");
  writeFile(src, makePartitionedDebugSource("apple_partition_", NumFns));

  auto obj = tmpFile("pcg_apple_names.o");
  std::vector<std::string> args = {"-gdwarf-4", "-gpubnames", "-fno-lto"};
  for (auto &f : forcePartitions(4))
    args.push_back(f);
  args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});
  auto compile = ncc(args);
  ASSERT_EQ(compile.exitCode, 0) << compile.err;

  auto sections = exec("otool", {"-l", obj.string()});
  ASSERT_EQ(sections.exitCode, 0) << sections.err;
  EXPECT_FALSE(sections.contains("__apple_"))
      << "a concatenated Apple hash table indexes only partition zero";

  auto tool = findDwarfDump();
  if (tool.path.empty())
    GTEST_SKIP() << "dwarfdump not available";
  auto dump = dwarfDumpInfo(tool, obj.string());
  ASSERT_EQ(dump.exitCode, 0) << dump.err;
  for (int I = 0; I < NumFns; ++I)
    EXPECT_TRUE(dump.contains("apple_partition_" + std::to_string(I)))
        << "dropping the derived index must not drop its underlying DIE";
}

TEST_F(DebugTest, WindowsDefaultDebug) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("win_default_debug.obj");

  auto r = ncc({"--target=x86_64-pc-windows-msvc", "-g", "-fno-lto", "-c",
                src, "-o", obj.string()});
  ASSERT_EQ(r.exitCode, 0);

  std::string objdumpTool;
  if (exec("which", {"llvm-objdump"}).exitCode == 0)
    objdumpTool = "llvm-objdump";
  else if (exec("which", {"objdump"}).exitCode == 0)
    objdumpTool = "objdump";
  else {
    GTEST_SKIP() << "objdump not available";
    return;
  }
  auto sections = exec(objdumpTool, {"-h", obj.string()});
  if (!sections.contains(".text")) {
    GTEST_SKIP() << objdumpTool << " cannot parse COFF format on this host";
    return;
  }
  EXPECT_TRUE(sections.contains(".debug_info"))
      << "Windows -g should produce DWARF, not CodeView";
}
