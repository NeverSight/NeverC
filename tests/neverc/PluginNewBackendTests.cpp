#include "NeverCTestFixture.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <future>
#include <string>
#include <vector>

namespace {

uint32_t readU32LE(const std::string &Bytes, size_t Offset) {
  return static_cast<uint32_t>(
             static_cast<uint8_t>(Bytes[Offset])) |
         (static_cast<uint32_t>(
              static_cast<uint8_t>(Bytes[Offset + 1]))
          << 8) |
         (static_cast<uint32_t>(
              static_cast<uint8_t>(Bytes[Offset + 2]))
          << 16) |
         (static_cast<uint32_t>(
              static_cast<uint8_t>(Bytes[Offset + 3]))
          << 24);
}

class PluginNewBackendTest : public NeverCTest {};
class PluginTargetMCFailureTest : public NeverCTest {};
class PluginTargetMCConcurrencyTest : public NeverCTest {};
class PluginTargetMCDeterminismTest : public NeverCTest {};

TEST_F(PluginNewBackendTest,
       CompilesCThroughCoarseProviderToCustomNObj) {
  const fs::path Source = tmpFile("answer.c");
  const fs::path Object = tmpFile("answer.nobj");
  writeFile(Source, "int answer(void) { return 42; }\n");

  const std::string Plugin =
      std::string("-fplugin=") + NEVERC_TEST_NEW_BACKEND_PLUGIN;
  CmdResult Result =
      ncc({"--no-default-config", Plugin,
           "--target=neverc-test-none", "-O0", "-fno-lto", "-c",
           Source.string(), "-o", Object.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string Bytes = readFile(Object);
  ASSERT_EQ(Bytes.size(), 21U);
  EXPECT_EQ(Bytes.substr(0, 4), "NOBJ");
  EXPECT_EQ(readU32LE(Bytes, 4), 1U);
  EXPECT_EQ(readU32LE(Bytes, 8), 2U);
  EXPECT_EQ(static_cast<uint8_t>(Bytes[12]), UINT8_C(0x2a));
  EXPECT_EQ(static_cast<uint8_t>(Bytes[13]), UINT8_C(0xc3));
  EXPECT_EQ(Bytes.substr(14), std::string("answer\0", 7));
}

TEST_F(PluginNewBackendTest,
       RejectsUnloweredTypeMetadataBeforeCoarseProviderCodegen) {
  const fs::path Source = tmpFile("unlowered-type-test.bc");
  const fs::path Object = tmpFile("unlowered-type-test.nobj");
  llvm::LLVMContext Context;
  llvm::Module Module("unlowered_type_test", Context);
  Module.setTargetTriple("neverc-test");
  Module.setDataLayout("e-p:64:64-i64:64-n32:64-S128");
  llvm::Type *PointerType = llvm::PointerType::getUnqual(Context);
  llvm::Function *Accepts = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt1Ty(Context), {PointerType},
                              false),
      llvm::GlobalValue::ExternalLinkage, "accepts", Module);
  llvm::IRBuilder<> Builder(
      llvm::BasicBlock::Create(Context, "entry", Accepts));
  llvm::Function *TypeTest =
      llvm::Intrinsic::getDeclaration(&Module, llvm::Intrinsic::type_test);
  llvm::Value *TypeID = llvm::MetadataAsValue::get(
      Context, llvm::MDString::get(Context, "type.id"));
  Builder.CreateRet(Builder.CreateCall(TypeTest, {Accepts->getArg(0), TypeID}));
  llvm::SmallVector<char, 0> Bitcode;
  llvm::raw_svector_ostream BitcodeStream(Bitcode);
  llvm::WriteBitcodeToFile(Module, BitcodeStream);
  writeFile(Source, std::string(Bitcode.begin(), Bitcode.end()));

  const std::string Plugin =
      std::string("-fplugin=") + NEVERC_TEST_NEW_BACKEND_PLUGIN;
  const std::string OptimizationPlugin =
      std::string("-fplugin=") + NEVERC_TEST_IR_OPTIMIZATION_PASSTHROUGH_PLUGIN;
  CmdResult Result = ncc({"--no-default-config", Plugin, OptimizationPlugin,
                          "--target=neverc-test-none", "-O0", "-fno-lto", "-c",
                          Source.string(), "-o", Object.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("llvm.type.test"), std::string::npos) << Result.err;
  EXPECT_NE(
      Result.err.find("CFI requires whole-program type metadata lowering"),
      std::string::npos)
      << Result.err;
  EXPECT_FALSE(fs::exists(Object));
}

TEST_F(PluginNewBackendTest,
       AssemblesSourceThroughReplacementParserToCustomNObj) {
  const fs::path Source = tmpFile("answer.s");
  const fs::path Object = tmpFile("answer-from-assembly.nobj");
  writeFile(Source, ".nobj_answer\n");

  const std::string Plugin =
      std::string("-fplugin=") + NEVERC_TEST_NEW_BACKEND_PLUGIN;
  CmdResult Result =
      ncc({"--no-default-config", Plugin,
           "--target=neverc-test-none", "-c", Source.string(),
           "-o", Object.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string Bytes = readFile(Object);
  ASSERT_EQ(Bytes.size(), 21U);
  EXPECT_EQ(Bytes.substr(0, 4), "NOBJ");
  EXPECT_EQ(readU32LE(Bytes, 4), 1U);
  EXPECT_EQ(readU32LE(Bytes, 8), 2U);
  EXPECT_EQ(static_cast<uint8_t>(Bytes[12]), UINT8_C(0x2a));
  EXPECT_EQ(static_cast<uint8_t>(Bytes[13]), UINT8_C(0xc3));
  EXPECT_EQ(Bytes.substr(14), std::string("answer\0", 7));
}

TEST_F(PluginTargetMCFailureTest,
       ParserFailureLeavesNoPartialObject) {
  const fs::path Source = tmpFile("invalid.s");
  const fs::path Object = tmpFile("invalid.nobj");
  writeFile(Source, ".not_a_valid_nobj_instruction\n");

  const std::string Plugin =
      std::string("-fplugin=") + NEVERC_TEST_NEW_BACKEND_PLUGIN;
  CmdResult Result =
      ncc({"--no-default-config", Plugin,
           "--target=neverc-test-none", "-c", Source.string(),
           "-o", Object.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("org.neverc.test.new-backend"),
            std::string::npos)
      << Result.err;
  EXPECT_NE(Result.err.find("assembly.parse"), std::string::npos)
      << Result.err;
  EXPECT_FALSE(fs::exists(Object));
}

TEST_F(PluginTargetMCConcurrencyTest,
       ParallelCAndAssemblyTasksRemainIsolated) {
  constexpr size_t TaskCount = 8;
  const std::string Plugin =
      std::string("-fplugin=") + NEVERC_TEST_NEW_BACKEND_PLUGIN;
  std::vector<fs::path> Sources;
  std::vector<fs::path> Objects;
  Sources.reserve(TaskCount);
  Objects.reserve(TaskCount);
  for (size_t I = 0; I != TaskCount; ++I) {
    const bool Assembly = (I % 2) != 0;
    Sources.push_back(
        tmpFile("parallel-" + std::to_string(I) +
                (Assembly ? ".s" : ".c")));
    Objects.push_back(
        tmpFile("parallel-" + std::to_string(I) + ".nobj"));
    writeFile(Sources.back(),
              Assembly ? ".nobj_answer\n"
                       : "int answer(void) { return 42; }\n");
  }

  std::vector<std::future<CmdResult>> Results;
  Results.reserve(TaskCount);
  for (size_t I = 0; I != TaskCount; ++I)
    Results.push_back(std::async(
        std::launch::async, [this, Plugin, &Sources, &Objects, I] {
          return ncc({"--no-default-config", Plugin,
                      "--target=neverc-test-none", "-O0", "-fno-lto",
                      "-c", Sources[I].string(), "-o",
                      Objects[I].string()});
        }));

  for (size_t I = 0; I != TaskCount; ++I) {
    CmdResult Result = Results[I].get();
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    const std::string Bytes = readFile(Objects[I]);
    ASSERT_EQ(Bytes.size(), 21U);
    EXPECT_EQ(Bytes.substr(0, 14),
              std::string("NOBJ\1\0\0\0\2\0\0\0\x2a\xc3", 14));
  }
}

TEST_F(PluginTargetMCDeterminismTest,
       RepeatedCompilationProducesIdenticalObjectBytes) {
  constexpr size_t IterationCount = 20;
  const fs::path Source = tmpFile("deterministic-answer.c");
  writeFile(Source, "int answer(void) { return 42; }\n");
  const std::string Plugin =
      std::string("-fplugin=") + NEVERC_TEST_NEW_BACKEND_PLUGIN;
  std::string Baseline;
  for (size_t I = 0; I != IterationCount; ++I) {
    const fs::path Object =
        tmpFile("deterministic-" + std::to_string(I) + ".nobj");
    CmdResult Result =
        ncc({"--no-default-config", Plugin,
             "--target=neverc-test-none", "-O0", "-fno-lto", "-c",
             Source.string(), "-o", Object.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    const std::string Bytes = readFile(Object);
    if (I == 0)
      Baseline = Bytes;
    else
      EXPECT_EQ(Bytes, Baseline) << "iteration " << I;
  }
  ASSERT_EQ(Baseline.size(), 21U);
}

} // namespace
