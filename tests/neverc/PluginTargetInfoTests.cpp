#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/TextDiagnosticBuffer.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/MacroBuilder.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetInfo.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <memory>
#include <string>

using namespace neverc;
using namespace neverc::plugin;

namespace {

std::string errorText(llvm::Error ErrorValue) {
  return llvm::toString(std::move(ErrorValue)).str().str();
}

PluginTargetSnapshot::TargetRecord makeTargetRecord() {
  PluginTargetSnapshot::TargetRecord Record;
  Record.PluginID = "org.neverc.test.target-info";
  Record.ID = {UINT64_C(0x5100), UINT64_C(0x100)};
  Record.CanonicalName = "test.target-info";
  Record.Machine.RawTriple = "novel-acme-neveros-none";
  Record.Machine.Architecture = "novel";
  Record.Machine.Vendor = "acme";
  Record.Machine.OperatingSystem = "neveros";
  Record.Machine.Environment = "none";
  Record.Machine.DataLayout =
      "E-p:32:32-p1:64:64-i32:32-i64:64-n32-S64";
  Record.Machine.DefaultCPU = "generic";
  Record.Machine.CPUs = {"generic", "tuned"};
  Record.Machine.Endianness = NEVERC_TARGET_ENDIAN_BIG;
  Record.Machine.PointerWidth = 32;
  Record.Machine.IntWidth = 32;
  Record.Machine.LongWidth = 32;
  Record.Machine.LongLongWidth = 64;
  Record.Machine.StackAlignment = 64;
  Record.Machine.MaximumAtomicWidth = 32;
  Record.Machine.MaximumVectorAlignment = 256;
  Record.Machine.BuiltinVaListKind =
      NEVERC_TARGET_VA_LIST_VOID_POINTER;
  Record.Machine.TLSSupported = false;
  Record.Machine.AddressSpaces.push_back({1, 64, 64, 64, 0});
  Record.Macros.push_back({"__NOVEL_TARGET__", "7", false});
  Record.Macros.push_back({"__REMOVE_ME__", "", true});
  Record.Builtins.push_back(
      {"__builtin_novel_add", "iii", "nc", "simd", "", 2, nullptr});
  Record.Registers.push_back({"r0", {"zero"}, {}, 0});
  Record.Constraints.push_back(
      {"r", NEVERC_TARGET_CONSTRAINT_ALLOWS_REGISTER, 0, 0, {}, 0, -1,
       "r"});
  Record.Clobbers = "~{flags}";
  Record.Machine.Features.push_back({"simd", {}, {}, true});
  return Record;
}

TEST(PluginTargetInfoTest, MaterializesPrimitiveTargetProperties) {
  PluginTargetInfo Target(makeTargetRecord());

  EXPECT_EQ(Target.getTriple().str(), "novel-acme-neveros-none");
  EXPECT_STREQ(Target.getDataLayoutString(),
               "E-p:32:32-p1:64:64-i32:32-i64:64-n32-S64");
  EXPECT_EQ(Target.getPointerWidth(LangAS::Default), 32U);
  EXPECT_EQ(Target.getPointerAlign(LangAS::Default), 32U);
  EXPECT_EQ(Target.getIntWidth(), 32U);
  EXPECT_EQ(Target.getLongWidth(), 32U);
  EXPECT_EQ(Target.getLongLongWidth(), 64U);
  EXPECT_EQ(Target.getMaxPointerWidth(), 64U);
  EXPECT_EQ(Target.getMaxVectorAlign(), 256U);
  EXPECT_EQ(Target.getMaxAtomicInlineWidth(), 32U);
  EXPECT_EQ(Target.getBuiltinVaListKind(),
            TargetInfo::VoidPtrBuiltinVaList);
  EXPECT_FALSE(Target.isTLSSupported());
  EXPECT_TRUE(Target.isValidCPUName("generic"));
  EXPECT_TRUE(Target.setCPU("tuned"));
  EXPECT_FALSE(Target.setCPU("missing"));
}

TEST(PluginTargetInfoTest, MaterializesLanguageAndAsmExtensions) {
  PluginTargetInfo Target(makeTargetRecord());
  std::string Defines;
  llvm::raw_string_ostream Stream(Defines);
  MacroBuilder Builder(Stream);
  LangOptions Options;

  Target.getTargetDefines(Options, Builder);
  Stream.flush();
  EXPECT_NE(Defines.find("#define __NOVEL_TARGET__ 7"),
            std::string::npos);
  EXPECT_NE(Defines.find("#undef __REMOVE_ME__"), std::string::npos);

  llvm::ArrayRef<Builtin::Info> Builtins =
      Target.getTargetBuiltins();
  ASSERT_EQ(Builtins.size(), 1U);
  EXPECT_EQ(Builtins[0].Name, "__builtin_novel_add");
  EXPECT_STREQ(Builtins[0].Type, "iii");
  EXPECT_STREQ(Builtins[0].Attributes, "nc");
  EXPECT_STREQ(Builtins[0].Features, "simd");

  EXPECT_TRUE(Target.isValidGCCRegisterName("r0"));
  EXPECT_TRUE(Target.isValidGCCRegisterName("zero"));
  const char *Constraint = "r";
  TargetInfo::ConstraintInfo Info("r", "");
  EXPECT_TRUE(Target.validateAsmConstraint(Constraint, Info));
  EXPECT_TRUE(Info.allowsRegister());
  EXPECT_EQ(Target.convertConstraint(Constraint), "r");
  EXPECT_EQ(Target.getClobbers(), "~{flags}");
  EXPECT_TRUE(Target.hasFeature("simd"));
}

TEST(PluginTargetInfoTest, CompilerInstanceSelectsSessionTargetBeforeBuiltins) {
  PluginProcessServices Services{"neverc-plugin-target-info-tests",
                                 LLVM_VERSION_MAJOR};
  ASSERT_FALSE(registerPluginTargetInterfaces(Services));
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Loaded =
      Services.registry().load(NEVERC_TEST_TARGET_VALID_PLUGIN);
  ASSERT_TRUE(static_cast<bool>(Loaded))
      << errorText(Loaded.takeError());
  {
    auto Plan = makePluginActivationPlan(
        Services.registry(), {"org.neverc.test.target-valid"});
    ASSERT_TRUE(static_cast<bool>(Plan)) << errorText(Plan.takeError());
    ASSERT_FALSE(activatePluginPlan(Services, *Plan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    ASSERT_TRUE(static_cast<bool>(CreatedSession))
        << errorText(CreatedSession.takeError());
    std::unique_ptr<PluginSession> Session = std::move(*CreatedSession);
    auto CreatedTask =
        Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    ASSERT_TRUE(static_cast<bool>(CreatedTask))
        << errorText(CreatedTask.takeError());

    {
      CompilerInstance Compiler;
      TextDiagnosticBuffer Diagnostics;
      Compiler.createDiagnostics(&Diagnostics,
                                 /*ShouldOwnClient=*/false);
      Compiler.getInvocation().getTargetOpts().Triple =
          "test.fixture-target";
      Compiler.setPluginTaskContext(std::move(*CreatedTask));

      ASSERT_TRUE(Compiler.createTarget());
      EXPECT_EQ(Compiler.getTarget().getTriple().str(),
                "test-unknown-none-none");
      EXPECT_EQ(
          Compiler.getTarget().getPointerWidth(LangAS::Default), 64U);
      const auto *PluginTarget =
          Compiler.getTarget().getPluginTargetInfo();
      ASSERT_NE(PluginTarget, nullptr);
      ASSERT_NE(PluginTarget->abi(), nullptr);
      EXPECT_NE(PluginTarget->abi()->ClassifyFunction, nullptr);
      EXPECT_EQ(PluginTarget->task(),
                Compiler.getPluginTaskContext());

      std::unique_ptr<PluginTaskContext> ReturnedTask =
          Compiler.takePluginTaskContext();
      EXPECT_FALSE(ReturnedTask->end());
    }

    EXPECT_FALSE(Session->end());
  }

  EXPECT_FALSE(Services.shutdown());
}

} // namespace
