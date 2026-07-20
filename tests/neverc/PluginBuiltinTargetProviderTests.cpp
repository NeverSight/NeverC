#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/TextDiagnosticBuffer.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "gtest/gtest.h"
#include <array>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>

using namespace llvm;
using namespace neverc;
using namespace neverc::plugin;

namespace {

void initializeBuiltinTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmPrinters();
  });
}

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

std::unique_ptr<Module> makeModule(LLVMContext &Context,
                                   const BuiltinTargetRoute &Route,
                                   StringRef DataLayout) {
  auto Result = std::make_unique<Module>("builtin-route-test", Context);
  Result->setTargetTriple(Route.CanonicalTriple);
  Result->setDataLayout(DataLayout);
  Function *F = Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt32Ty(Context), false),
      GlobalValue::ExternalLinkage, "builtin_route_probe", *Result);
  BasicBlock *Entry = BasicBlock::Create(Context, "entry", F);
  IRBuilder<> Builder(Entry);
  Builder.CreateRet(
      ConstantInt::get(llvm::Type::getInt32Ty(Context), 0));
  return Result;
}

file_magic::Impl expectedMagic(BuiltinObjectFormat Format) {
  switch (Format) {
  case BuiltinObjectFormat::ELF:
    return file_magic::elf_relocatable;
  case BuiltinObjectFormat::COFF:
    return file_magic::coff_object;
  case BuiltinObjectFormat::MachO:
    return file_magic::macho_object;
  }
  llvm_unreachable("unknown builtin object format");
}

TEST(PluginBuiltinTargetProviderTest, InventoryIsCompleteAndStable) {
  using ExpectedRoute =
      std::tuple<const char *, BuiltinTargetABIKind, BuiltinObjectFormat>;
  const std::array<ExpectedRoute, 9> Expected = {{
      {"x86_64-apple-macosx", BuiltinTargetABIKind::X86_64SysV,
       BuiltinObjectFormat::MachO},
      {"aarch64-apple-macosx", BuiltinTargetABIKind::AArch64DarwinPCS,
       BuiltinObjectFormat::MachO},
      {"x86_64-unknown-linux-gnu", BuiltinTargetABIKind::X86_64SysV,
       BuiltinObjectFormat::ELF},
      {"aarch64-unknown-linux-gnu", BuiltinTargetABIKind::AArch64AAPCS,
       BuiltinObjectFormat::ELF},
      {"x86_64-unknown-linux-android29",
       BuiltinTargetABIKind::X86_64SysV, BuiltinObjectFormat::ELF},
      {"aarch64-unknown-linux-android29",
       BuiltinTargetABIKind::AArch64AAPCS, BuiltinObjectFormat::ELF},
      {"x86_64-pc-windows-msvc", BuiltinTargetABIKind::X86_64Win64,
       BuiltinObjectFormat::COFF},
      {"aarch64-pc-windows-msvc", BuiltinTargetABIKind::AArch64Win64,
       BuiltinObjectFormat::COFF},
      {"aarch64-apple-ios", BuiltinTargetABIKind::AArch64DarwinPCS,
       BuiltinObjectFormat::MachO},
  }};

  ArrayRef<BuiltinTargetRoute> Routes = builtinTargetRoutes();
  ASSERT_EQ(Routes.size(), Expected.size());

  std::set<std::pair<uint64_t, uint64_t>> TargetIDs;
  std::set<std::pair<uint64_t, uint64_t>> ABIIDs;
  for (size_t I = 0; I != Routes.size(); ++I) {
    const auto &[Triple, ABI, Format] = Expected[I];
    EXPECT_EQ(Routes[I].CanonicalTriple.str(), Triple);
    EXPECT_EQ(Routes[I].ABI, ABI);
    EXPECT_EQ(Routes[I].ObjectFormat, Format);
    EXPECT_TRUE(Routes[I].SupportsSource);
    EXPECT_TRUE(Routes[I].SupportsAssembly);
    EXPECT_TRUE(Routes[I].SupportsObject);
    EXPECT_FALSE(Routes[I].SupportsMCDecode);
    EXPECT_TRUE(TargetIDs.emplace(Routes[I].TargetID.High,
                                  Routes[I].TargetID.Low)
                    .second);
    EXPECT_TRUE(
        ABIIDs.emplace(Routes[I].ABIID.High, Routes[I].ABIID.Low).second);
    EXPECT_NE(Routes[I].MCSchemaID.High | Routes[I].MCSchemaID.Low, 0U);
    EXPECT_NE(Routes[I].ObjectFormatID.High |
                  Routes[I].ObjectFormatID.Low,
              0U);
  }
}

TEST(PluginBuiltinTargetProviderTest,
     ResolvesAliasesWithoutMergingPlatformABIs) {
  const BuiltinTargetRoute *Mac =
      findBuiltinTargetRoute("arm64-apple-macosx14.0");
  const BuiltinTargetRoute *IOS =
      findBuiltinTargetRoute("arm64-apple-ios17.0");
  const BuiltinTargetRoute *Android =
      findBuiltinTargetRoute("aarch64-linux-android35");
  const BuiltinTargetRoute *Linux =
      findBuiltinTargetRoute("aarch64-unknown-linux-gnu");

  ASSERT_NE(Mac, nullptr);
  ASSERT_NE(IOS, nullptr);
  ASSERT_NE(Android, nullptr);
  ASSERT_NE(Linux, nullptr);
  EXPECT_NE(Mac->TargetID.Low, IOS->TargetID.Low);
  EXPECT_NE(Android->ABIID.Low, Linux->ABIID.Low);
  EXPECT_EQ(Mac->ObjectFormat, BuiltinObjectFormat::MachO);
  EXPECT_EQ(IOS->ObjectFormat, BuiltinObjectFormat::MachO);
  EXPECT_EQ(Android->ObjectFormat, BuiltinObjectFormat::ELF);
  EXPECT_EQ(Linux->ObjectFormat, BuiltinObjectFormat::ELF);
  EXPECT_EQ(findBuiltinTargetRoute("riscv64-unknown-linux-gnu"), nullptr);
}

TEST(PluginBuiltinTargetProviderTest,
     FrontendLLVMAndObjectWriterAgreeForEveryRoute) {
  initializeBuiltinTargets();

  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    SCOPED_TRACE(Route.CanonicalTriple.str());

    CompilerInstance Compiler;
    TextDiagnosticBuffer Diagnostics;
    Compiler.createDiagnostics(&Diagnostics, /*ShouldOwnClient=*/false);
    Compiler.getInvocation().getTargetOpts().Triple =
        Route.CanonicalTriple.str();
    ASSERT_TRUE(Compiler.createTarget());
    const std::string FrontendLayout =
        Compiler.getTarget().getDataLayoutString();

    auto LLVMTarget = lookupBuiltinLLVMTarget(Route);
    ASSERT_TRUE(static_cast<bool>(LLVMTarget))
        << errorText(LLVMTarget.takeError());
    llvm::TargetOptions Options;
    std::unique_ptr<TargetMachine> TM((*LLVMTarget)->createTargetMachine(
        Route.CanonicalTriple, Route.DefaultCPU, "", Options,
        std::nullopt, CodeGenOptLevel::None));
    ASSERT_NE(TM, nullptr);

    LLVMContext Context;
    std::unique_ptr<Module> ObjectModule =
        makeModule(Context, Route, FrontendLayout);
    ASSERT_FALSE(validateBuiltinTargetPipeline(
        Route, *ObjectModule, *TM, Route.DefaultCPU, ""));

    SmallVector<char, 0> ObjectBytes;
    raw_svector_ostream ObjectStream(ObjectBytes);
    legacy::PassManager ObjectPasses;
    ASSERT_FALSE(TM->addPassesToEmitFile(
        ObjectPasses, ObjectStream, nullptr, CodeGenFileType::ObjectFile));
    ObjectPasses.run(*ObjectModule);
    EXPECT_EQ(identify_magic(StringRef(ObjectBytes.data(), ObjectBytes.size())),
              expectedMagic(Route.ObjectFormat));

    LLVMContext AssemblyContext;
    std::unique_ptr<Module> AssemblyModule =
        makeModule(AssemblyContext, Route, FrontendLayout);
    SmallVector<char, 0> AssemblyBytes;
    raw_svector_ostream AssemblyStream(AssemblyBytes);
    legacy::PassManager AssemblyPasses;
    ASSERT_FALSE(TM->addPassesToEmitFile(
        AssemblyPasses, AssemblyStream, nullptr,
        CodeGenFileType::AssemblyFile));
    AssemblyPasses.run(*AssemblyModule);
    EXPECT_FALSE(AssemblyBytes.empty());
  }
}

} // namespace
