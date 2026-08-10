#include "NeverCTestFixture.h"

#include "neverc/Foundation/Builtin/XorStrCipher.h"
#include "neverc/Linker/Core/Driver/LTOCacheContract.h"
#include "neverc/Transforms/XorStr/EncryptCallStringsPass.h"
#include "neverc/Transforms/XorStr/XorStrCleanupPass.h"

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <cstdlib>
#include <optional>

namespace {

void setEnvironmentVariable(const char *Name, const char *Value) {
#ifdef _WIN32
  _putenv_s(Name, Value);
#else
  setenv(Name, Value, 1);
#endif
}

void unsetEnvironmentVariable(const char *Name) {
#ifdef _WIN32
  _putenv_s(Name, "");
#else
  unsetenv(Name);
#endif
}

class ScopedEnvironmentVariable {
  std::string Name;
  std::optional<std::string> OldValue;

public:
  ScopedEnvironmentVariable(const char *Name, const std::string &Value)
      : Name(Name) {
    if (const char *Old = std::getenv(Name))
      OldValue = Old;
    setEnvironmentVariable(Name, Value.c_str());
  }

  ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;
  ScopedEnvironmentVariable &
  operator=(const ScopedEnvironmentVariable &) = delete;

  ~ScopedEnvironmentVariable() {
    if (OldValue)
      setEnvironmentVariable(Name.c_str(), OldValue->c_str());
    else
      unsetEnvironmentVariable(Name.c_str());
  }
};

llvm::Expected<std::string> readTextSections(llvm::StringRef Bytes) {
  auto Object = llvm::object::ObjectFile::createObjectFile(
      llvm::MemoryBufferRef(Bytes, "xorstr-object"));
  if (!Object)
    return Object.takeError();

  std::string Text;
  for (const llvm::object::SectionRef &Section : (*Object)->sections()) {
    if (!Section.isText())
      continue;
    llvm::Expected<llvm::StringRef> Contents = Section.getContents();
    if (!Contents)
      return Contents.takeError();
    Text.append(Contents->data(), Contents->size());
  }
  if (Text.empty())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "object has no text section");
  return Text;
}

void captureErrorDiagnostic(const llvm::DiagnosticInfo &Diagnostic,
                            void *Context) {
  if (Diagnostic.getSeverity() == llvm::DS_Error)
    *static_cast<bool *>(Context) = true;
}

} // namespace

class XorStrTest : public NeverCTest {
protected:
  fs::path xorStrDir() { return testDir() / "xorstr"; }

  CmdResult syntaxOnly(const std::string &src,
                       const std::string &extraFlags = "") {
    std::vector<std::string> args = {"-fsyntax-only", "-include",
                                     "neverc/xorstr/xorstr.h"};
    for (auto &f : splitFlags(extraFlags))
      args.push_back(f);
    for (auto &f : sysrootFlags())
      args.push_back(f);
    for (auto &f : archFlags())
      args.push_back(f);
    args.push_back(src);
    return ncc(args);
  }

  std::string emitIR(const std::string &src, const std::string &name,
                     const std::string &extraFlags = "") {
    auto ir = tmpFile(name + ".ll");
    std::vector<std::string> args = {"-S", "-emit-llvm", "-include",
                                     "neverc/xorstr/xorstr.h"};
    for (auto &f : splitFlags(extraFlags))
      args.push_back(f);
    for (auto &f : sysrootFlags())
      args.push_back(f);
    for (auto &f : archFlags())
      args.push_back(f);
    args.push_back(src);
    args.push_back("-o");
    args.push_back(ir.string());
    auto r = ncc(args);
    EXPECT_EQ(r.exitCode, 0) << name << ": emit-llvm failed\n" << r.err;
    if (r.exitCode != 0)
      return "";
    return readFile(ir);
  }
};

// ---- Basic tests ----

TEST_F(XorStrTest, Basic_CompileClean) {
  auto src = xorStrDir() / "nc_xorstr_basic.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string());
  EXPECT_EQ(r.exitCode, 0) << "basic xorstr failed\n" << r.err;
}

TEST_F(XorStrTest, Basic_NonLiteralError) {
  auto src = xorStrDir() / "nc_xorstr_basic.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string(), "-DTEST_BAD_ARG");
  EXPECT_NE(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("not a string literal"))
      << "expected 'not a string literal' error\n" << r.err;
}

TEST_F(XorStrTest, LinkOnlyRejectsInvalidConfigurationValues) {
  auto source = tmpFile("xorstr-link-config.c");
  auto input = tmpFile("xorstr-link-config.o");
  writeFile(source, "int neverc_xorstr_link_config_probe;\n");
  auto compile = ncc({"-c", "-fno-lto", "-fno-builtin-mimalloc",
                      source.string(), "-o", input.string()});
  ASSERT_EQ(compile.exitCode, 0) << compile.err;

  const char *InvalidOptions[] = {
      "-fstring-encrypt-key=not-a-number",
      "-fencrypt-call-strings-max-len=not-a-number",
  };

  for (const char *InvalidOption : InvalidOptions) {
    SCOPED_TRACE(InvalidOption);
    auto output = tmpFile("invalid-xorstr-link-value.o");
    auto r = ncc({"-###", "-r", "-nostdlib", InvalidOption,
                  input.string(), "-o", output.string()});
    EXPECT_NE(r.exitCode, 0);
    EXPECT_TRUE(r.stderrContains("invalid value")) << r.err;
    EXPECT_TRUE(r.stderrContains("not-a-number")) << r.err;
  }
}

// ---- Wide / u8 / u16 / u32 strings ----

TEST_F(XorStrTest, Wide_AllEncodings) {
  auto src = xorStrDir() / "nc_xorstr_wide.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string());
  EXPECT_EQ(r.exitCode, 0)
      << "wide/u8 xorstr failed\n" << r.err;
}

// ---- Intermediate IR: encrypted, opaque until final codegen/LTO ----

TEST_F(XorStrTest, Codegen_IntermediateKeepsOpaqueDecoder) {
  auto src = xorStrDir() / "nc_xorstr_codegen.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  // This assertion covers the xorstr transformation's input module.  The
  // hosted allocator is a separate embedded module and legitimately contains
  // Windows API names, including the plaintext used by this fixture.
  auto ir = emitIR(src.string(), "xorstr_codegen", "-O2 -fno-builtin-mimalloc");
  if (ir.empty())
    return;

  EXPECT_NE(ir.find("private"), std::string::npos)
      << "expected encrypted constant in IR (private global)";

  EXPECT_EQ(ir.find("GetProcAddress"), std::string::npos)
      << "plaintext 'GetProcAddress' should not appear in IR";

  const size_t CallerBegin = ir.find("@test_xorstr(");
  ASSERT_NE(CallerBegin, std::string::npos);
  const size_t CallerEnd = ir.find("\n}", CallerBegin);
  ASSERT_NE(CallerEnd, std::string::npos);
  const std::string Caller = ir.substr(CallerBegin, CallerEnd - CallerBegin);
  EXPECT_NE(Caller.find("@__neverc_xorstr_decrypt("), std::string::npos)
      << "intermediate IR must retain the call for final-link rekeying\n"
      << Caller;
  const size_t DecoderName = ir.find("@__neverc_xorstr_decrypt(");
  ASSERT_NE(DecoderName, std::string::npos);
  const size_t DecoderEnd = ir.find("\n}", DecoderName);
  ASSERT_NE(DecoderEnd, std::string::npos);
  const std::string Decoder = ir.substr(DecoderName, DecoderEnd - DecoderName);
  const bool HasVolatileLoad =
      Decoder.find("load volatile") != std::string::npos ||
      Decoder.find("load atomic volatile") != std::string::npos;
  EXPECT_TRUE(HasVolatileLoad)
      << "ordinary optimization must not specialize decoder inputs\n"
      << Decoder;
  EXPECT_NE(ir.find("!neverc.xorstr.cleanup"), std::string::npos)
      << "explicit NC_XORSTR buffers must be wiped even when automatic call "
         "encryption is disabled\n"
      << ir;
}

TEST_F(XorStrTest, Codegen_NativeObjectHasNoSharedDecoder) {
  auto src = xorStrDir() / "nc_xorstr_codegen.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto object = tmpFile("xorstr_no_shared_decoder.o");
  std::vector<std::string> args = {"-c",       "-O2",
                                   "-fno-lto", "-fno-builtin-mimalloc",
                                   "-include", "neverc/xorstr/xorstr.h"};
  for (auto &f : sysrootFlags())
    args.push_back(f);
  for (auto &f : archFlags())
    args.push_back(f);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(object.string());

  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << "native object compilation failed\n" << r.err;
  const std::string Bytes = readFile(object);
  EXPECT_EQ(Bytes.find("__neverc_xorstr_"), std::string::npos)
      << "machine-code output must not retain a shared decoder identity";
  EXPECT_EQ(Bytes.find(".rekey"), std::string::npos)
      << "machine-code output must not identify re-keyed ciphertext globals";
  EXPECT_EQ(Bytes.find("GetProcAddress"), std::string::npos)
      << "machine-code output must not contain plaintext";
}

TEST_F(XorStrTest, Codegen_MachineOutlinerCannotRecreateSharedDecoder) {
  const fs::path Source = tmpFile("xorstr_machine_outliner.c");
  const fs::path Assembly = tmpFile("xorstr_machine_outliner.s");
  writeFile(
      Source,
      "const char *outline_a(void) { return NC_XORSTR(\"outline-aa\"); }\n"
      "const char *outline_b(void) { return NC_XORSTR(\"outline-bb\"); }\n"
      "const char *outline_c(void) { return NC_XORSTR(\"outline-cc\"); }\n");

  std::vector<std::string> Args = {
      "-S",
      "-O2",
      "-fno-lto",
      "-fno-builtin-mimalloc",
      "-fstring-encrypt-key=1",
      "-include",
      "neverc/xorstr/xorstr.h",
      "-mllvm",
      "-enable-machine-outliner=always",
  };
  for (auto &Flag : sysrootFlags())
    Args.push_back(Flag);
  for (auto &Flag : archFlags())
    Args.push_back(Flag);
  Args.push_back(Source.string());
  Args.push_back("-o");
  Args.push_back(Assembly.string());

  CmdResult Build = ncc(Args);
  ASSERT_EQ(Build.exitCode, 0) << Build.err;
  const std::string Text = readFile(Assembly);
  EXPECT_EQ(Text.find("OUTLINED_FUNCTION"), std::string::npos)
      << "machine outlining must not extract inline decrypt code";
  EXPECT_EQ(Text.find("__neverc_xorstr_"), std::string::npos)
      << "assembly must not retain a shared decoder identity";
  for (const char *Plaintext : {"outline-aa", "outline-bb", "outline-cc"})
    EXPECT_EQ(Text.find(Plaintext), std::string::npos) << Plaintext;
}

TEST_F(XorStrTest, Codegen_CleanupTracksSelectDecoderOutput) {
  const fs::path Source = tmpFile("xorstr_select_output_cleanup.c");
  writeFile(Source,
            "static const char encoded[4] = {1, 2, 3, 4};\n"
            "int cleanup_select_output(int choose) {\n"
            "  char left[5] = {0};\n"
            "  char right[5] = {0};\n"
            "  char *output = choose ? left : right;\n"
            "  return __neverc_xorstr_decrypt(encoded, 4, 1, output)[0];\n"
            "}\n");

  for (const char *Opt : {"-O0", "-O1"}) {
    SCOPED_TRACE(Opt);
    const std::string IR =
        emitIR(Source.string(),
               std::string("xorstr_select_output_cleanup_") + (Opt + 1),
               std::string(Opt) + " -fno-lto -fno-builtin-mimalloc");
    ASSERT_FALSE(IR.empty());
    EXPECT_NE(IR.find("\"nooutline\""), std::string::npos)
        << "a decoder whose output is selected between stack buffers must "
           "still disable machine outlining\n"
        << IR;

    const std::string CleanupMarker = "!neverc.xorstr.cleanup";
    size_t CleanupCount = 0;
    for (size_t Position = 0;
         (Position = IR.find(CleanupMarker, Position)) != std::string::npos;
         Position += CleanupMarker.size())
      ++CleanupCount;
    EXPECT_GE(CleanupCount, 2u)
        << "every possible decoder output buffer must receive a volatile "
           "wipe, including through an -O0 pointer slot\n"
        << IR;

    if (std::string(Opt) == "-O1") {
      const size_t CallerBegin = IR.find("@cleanup_select_output(");
      ASSERT_NE(CallerBegin, std::string::npos);
      const size_t CallerEnd = IR.find("\n}", CallerBegin);
      ASSERT_NE(CallerEnd, std::string::npos);
      const std::string Caller =
          IR.substr(CallerBegin, CallerEnd - CallerBegin);
      const size_t Cleanup = Caller.find(CleanupMarker);
      ASSERT_NE(Cleanup, std::string::npos);
      EXPECT_EQ(Caller.find("call void @llvm.lifetime."), std::string::npos)
          << "protected buffers must remain live through the volatile wipe\n"
          << Caller;
    }
  }
}

TEST_F(XorStrTest, CleanupRemovesConflictingLifetimeMarkersAcrossCFG) {
  llvm::LLVMContext Context;
  llvm::Module Module("xorstr-cleanup-lifetime-cfg", Context);
  Module.setDataLayout("e-p:64:64-i64:64-n8:16:32:64-S128");
  llvm::Function *Function = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(Context),
                              {llvm::Type::getInt1Ty(Context)}, false),
      llvm::GlobalValue::ExternalLinkage, "cleanup_lifetime_cfg", Module);
  llvm::BasicBlock *Entry =
      llvm::BasicBlock::Create(Context, "entry", Function);
  llvm::BasicBlock *Ended =
      llvm::BasicBlock::Create(Context, "ended", Function);
  llvm::BasicBlock *Live =
      llvm::BasicBlock::Create(Context, "live", Function);
  llvm::BasicBlock *Exit =
      llvm::BasicBlock::Create(Context, "exit", Function);

  llvm::IRBuilder<> Builder(Entry);
  llvm::AllocaInst *Buffer = Builder.CreateAlloca(
      llvm::ArrayType::get(Builder.getInt8Ty(), 16), nullptr, "xorstr.buf");
  Buffer->setMetadata("neverc.xorstr", llvm::MDNode::get(Context, {}));
  Builder.CreateLifetimeStart(Buffer);
  Builder.CreateCondBr(Function->getArg(0), Ended, Live);
  Builder.SetInsertPoint(Ended);
  Builder.CreateLifetimeEnd(Buffer);
  Builder.CreateBr(Exit);
  Builder.SetInsertPoint(Live);
  Builder.CreateBr(Exit);
  Builder.SetInsertPoint(Exit);
  Builder.CreateRetVoid();

  llvm::FunctionAnalysisManager FAM;
  (void)neverc::xorstr::XorStrCleanupPass().run(*Function, FAM);
  ASSERT_FALSE(llvm::verifyModule(Module, &llvm::errs()));
  std::string IR;
  llvm::raw_string_ostream IRStream(IR);
  Module.print(IRStream, nullptr);
  IRStream.flush();
  EXPECT_EQ(IR.find("call void @llvm.lifetime.start"), std::string::npos)
      << "protected stack storage must stay live until every cleanup exit\n"
      << IR;
  EXPECT_EQ(IR.find("call void @llvm.lifetime.end"), std::string::npos)
      << "a predecessor lifetime.end must not make the exit wipe invalid\n"
      << IR;
  EXPECT_NE(IR.find("!neverc.xorstr.cleanup"), std::string::npos) << IR;
}

TEST_F(XorStrTest, CleanupUsesCompleteConstantAllocaSize) {
  llvm::LLVMContext Context;
  llvm::Module Module("xorstr-cleanup-alloca-count", Context);
  Module.setDataLayout("e-p:64:64-i64:64-n8:16:32:64-S128");
  llvm::Function *Function = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(Context), false),
      llvm::GlobalValue::ExternalLinkage, "cleanup_alloca_count", Module);
  llvm::BasicBlock *Entry =
      llvm::BasicBlock::Create(Context, "entry", Function);
  llvm::IRBuilder<> Builder(Entry);
  llvm::AllocaInst *Buffer =
      Builder.CreateAlloca(Builder.getInt8Ty(), Builder.getInt64(16),
                           "xorstr.byte.buffer");
  Buffer->setMetadata("neverc.xorstr", llvm::MDNode::get(Context, {}));
  Builder.CreateRetVoid();

  llvm::FunctionAnalysisManager FAM;
  (void)neverc::xorstr::XorStrCleanupPass().run(*Function, FAM);
  ASSERT_FALSE(llvm::verifyModule(Module, &llvm::errs()));

  llvm::MemSetInst *Cleanup = nullptr;
  for (llvm::Instruction &Instruction : Entry->instructionsWithoutDebug())
    if (auto *Memset = llvm::dyn_cast<llvm::MemSetInst>(&Instruction))
      if (Memset->hasMetadata("neverc.xorstr.cleanup"))
        Cleanup = Memset;
  ASSERT_NE(Cleanup, nullptr);
  const auto *Length =
      llvm::dyn_cast<llvm::ConstantInt>(Cleanup->getLength());
  ASSERT_NE(Length, nullptr);
  EXPECT_EQ(Length->getZExtValue(), 16u)
      << "alloca element counts must be included in the volatile wipe";
}

TEST_F(XorStrTest, CleanupRejectsDynamicOrNonDominatingStorage) {
  auto RunAndExpectError = [](bool DynamicSize) {
    llvm::LLVMContext Context;
    bool SawError = false;
    Context.setDiagnosticHandlerCallBack(captureErrorDiagnostic, &SawError);
    llvm::Module Module(DynamicSize ? "xorstr-cleanup-dynamic"
                                    : "xorstr-cleanup-nondominating",
                        Context);
    Module.setDataLayout("e-p:64:64-i64:64-n8:16:32:64-S128");
    llvm::Type *ArgumentType = DynamicSize ? llvm::Type::getInt64Ty(Context)
                                           : llvm::Type::getInt1Ty(Context);
    llvm::Function *Function = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(Context),
                                {ArgumentType}, false),
        llvm::GlobalValue::ExternalLinkage, "cleanup_unsupported", Module);
    llvm::BasicBlock *Entry =
        llvm::BasicBlock::Create(Context, "entry", Function);
    llvm::IRBuilder<> Builder(Entry);
    if (DynamicSize) {
      llvm::AllocaInst *Buffer = Builder.CreateAlloca(
          Builder.getInt8Ty(), Function->getArg(0), "xorstr.dynamic");
      Buffer->setMetadata("neverc.xorstr", llvm::MDNode::get(Context, {}));
      Builder.CreateRetVoid();
    } else {
      llvm::BasicBlock *Allocate =
          llvm::BasicBlock::Create(Context, "allocate", Function);
      llvm::BasicBlock *Bypass =
          llvm::BasicBlock::Create(Context, "bypass", Function);
      llvm::BasicBlock *Exit =
          llvm::BasicBlock::Create(Context, "exit", Function);
      Builder.CreateCondBr(Function->getArg(0), Allocate, Bypass);
      Builder.SetInsertPoint(Allocate);
      llvm::AllocaInst *Buffer = Builder.CreateAlloca(
          llvm::ArrayType::get(Builder.getInt8Ty(), 16), nullptr,
          "xorstr.conditional");
      Buffer->setMetadata("neverc.xorstr", llvm::MDNode::get(Context, {}));
      Builder.CreateBr(Exit);
      Builder.SetInsertPoint(Bypass);
      Builder.CreateBr(Exit);
      Builder.SetInsertPoint(Exit);
      Builder.CreateRetVoid();
    }

    llvm::FunctionAnalysisManager FAM;
    (void)neverc::xorstr::XorStrCleanupPass().run(*Function, FAM);
    EXPECT_TRUE(SawError)
        << "unsupported protected storage must fail instead of leaking or "
           "creating invalid IR";
    EXPECT_FALSE(llvm::verifyModule(Module, &llvm::errs()));
  };

  RunAndExpectError(/*DynamicSize=*/true);
  RunAndExpectError(/*DynamicSize=*/false);
}

TEST_F(XorStrTest, CleanupSupportsConditionalStorageWithSeparateExit) {
  llvm::LLVMContext Context;
  bool SawError = false;
  Context.setDiagnosticHandlerCallBack(captureErrorDiagnostic, &SawError);
  llvm::Module Module("xorstr-cleanup-conditional-separate-exit", Context);
  Module.setDataLayout("e-p:64:64-i64:64-n8:16:32:64-S128");
  llvm::Function *Function = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(Context),
                              {llvm::Type::getInt1Ty(Context)}, false),
      llvm::GlobalValue::ExternalLinkage, "cleanup_conditional_separate_exit",
      Module);
  llvm::BasicBlock *Entry =
      llvm::BasicBlock::Create(Context, "entry", Function);
  llvm::BasicBlock *Allocate =
      llvm::BasicBlock::Create(Context, "allocate", Function);
  llvm::BasicBlock *Bypass =
      llvm::BasicBlock::Create(Context, "bypass", Function);

  llvm::IRBuilder<> Builder(Entry);
  Builder.CreateCondBr(Function->getArg(0), Allocate, Bypass);
  Builder.SetInsertPoint(Allocate);
  llvm::AllocaInst *Buffer =
      Builder.CreateAlloca(llvm::ArrayType::get(Builder.getInt8Ty(), 16),
                           nullptr, "xorstr.conditional");
  Buffer->setMetadata("neverc.xorstr", llvm::MDNode::get(Context, {}));
  Builder.CreateRetVoid();
  Builder.SetInsertPoint(Bypass);
  Builder.CreateRetVoid();

  llvm::FunctionAnalysisManager FAM;
  (void)neverc::xorstr::XorStrCleanupPass().run(*Function, FAM);
  EXPECT_FALSE(SawError)
      << "an exit that cannot be reached after the protected allocation does "
         "not require a wipe";
  ASSERT_FALSE(llvm::verifyModule(Module, &llvm::errs()));

  size_t AllocateWipes = 0;
  for (llvm::Instruction &Instruction : Allocate->instructionsWithoutDebug())
    if (auto *Memset = llvm::dyn_cast<llvm::MemSetInst>(&Instruction))
      if (Memset->hasMetadata("neverc.xorstr.cleanup"))
        ++AllocateWipes;
  size_t BypassWipes = 0;
  for (llvm::Instruction &Instruction : Bypass->instructionsWithoutDebug())
    if (auto *Memset = llvm::dyn_cast<llvm::MemSetInst>(&Instruction))
      if (Memset->hasMetadata("neverc.xorstr.cleanup"))
        ++BypassWipes;
  EXPECT_EQ(AllocateWipes, 1u);
  EXPECT_EQ(BypassWipes, 0u);
}

TEST_F(XorStrTest, CleanupRejectsDecoderOutputOutsideStackStorage) {
  llvm::LLVMContext Context;
  bool SawError = false;
  Context.setDiagnosticHandlerCallBack(captureErrorDiagnostic, &SawError);
  llvm::Module Module("xorstr-cleanup-global-output", Context);
  Module.setDataLayout("e-p:64:64-i64:64-n8:16:32:64-S128");
  llvm::Type *PointerTy = llvm::PointerType::get(Context, 0);
  llvm::Type *SizeTy = llvm::Type::getInt64Ty(Context);
  llvm::Function *Decoder = llvm::Function::Create(
      llvm::FunctionType::get(PointerTy, {PointerTy, SizeTy, SizeTy, PointerTy},
                              false),
      llvm::GlobalValue::ExternalLinkage, "__neverc_xorstr_decrypt", Module);
  llvm::Constant *Ciphertext =
      llvm::ConstantDataArray::getString(Context, "ciphertext", false);
  auto *CiphertextGlobal = new llvm::GlobalVariable(
      Module, Ciphertext->getType(), true, llvm::GlobalValue::PrivateLinkage,
      Ciphertext, "ciphertext");
  llvm::Constant *Output = llvm::ConstantAggregateZero::get(
      llvm::ArrayType::get(llvm::Type::getInt8Ty(Context), 16));
  auto *OutputGlobal = new llvm::GlobalVariable(
      Module, Output->getType(), false, llvm::GlobalValue::PrivateLinkage,
      Output, "plaintext.output");
  llvm::Function *Function = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(Context), false),
      llvm::GlobalValue::ExternalLinkage, "cleanup_global_output", Module);
  llvm::IRBuilder<> Builder(
      llvm::BasicBlock::Create(Context, "entry", Function));
  Builder.CreateCall(Decoder, {CiphertextGlobal, Builder.getInt64(1),
                               Builder.getInt64(1), OutputGlobal});
  Builder.CreateRetVoid();

  llvm::FunctionAnalysisManager FAM;
  (void)neverc::xorstr::XorStrCleanupPass().run(*Function, FAM);
  EXPECT_TRUE(SawError)
      << "a provider must not redirect decoder plaintext outside wipeable "
         "stack storage";
  EXPECT_FALSE(llvm::verifyModule(Module, &llvm::errs()));
}

TEST_F(XorStrTest, CleanupRejectsOpaqueDecoderOutput) {
  llvm::LLVMContext Context;
  bool SawError = false;
  Context.setDiagnosticHandlerCallBack(captureErrorDiagnostic, &SawError);
  llvm::Module Module("xorstr-cleanup-opaque-output", Context);
  Module.setDataLayout("e-p:64:64-i64:64-n8:16:32:64-S128");
  llvm::Type *PointerTy = llvm::PointerType::get(Context, 0);
  llvm::Type *SizeTy = llvm::Type::getInt64Ty(Context);
  llvm::Function *Decoder = llvm::Function::Create(
      llvm::FunctionType::get(PointerTy, {PointerTy, SizeTy, SizeTy, PointerTy},
                              false),
      llvm::GlobalValue::ExternalLinkage, "__neverc_xorstr_decrypt", Module);
  llvm::Function *OpaqueOutput = llvm::Function::Create(
      llvm::FunctionType::get(PointerTy, false),
      llvm::GlobalValue::ExternalLinkage, "opaque_output", Module);
  llvm::Constant *Ciphertext =
      llvm::ConstantDataArray::getString(Context, "ciphertext", false);
  auto *CiphertextGlobal = new llvm::GlobalVariable(
      Module, Ciphertext->getType(), true, llvm::GlobalValue::PrivateLinkage,
      Ciphertext, "ciphertext");
  llvm::Function *Function = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(Context), false),
      llvm::GlobalValue::ExternalLinkage, "cleanup_opaque_output", Module);
  llvm::IRBuilder<> Builder(
      llvm::BasicBlock::Create(Context, "entry", Function));
  llvm::Value *Output = Builder.CreateCall(OpaqueOutput);
  Builder.CreateCall(Decoder, {CiphertextGlobal, Builder.getInt64(1),
                               Builder.getInt64(1), Output});
  Builder.CreateRetVoid();

  llvm::FunctionAnalysisManager FAM;
  (void)neverc::xorstr::XorStrCleanupPass().run(*Function, FAM);
  EXPECT_TRUE(SawError)
      << "an opaque call result cannot prove that plaintext remains in "
         "wipeable stack storage";
  EXPECT_FALSE(llvm::verifyModule(Module, &llvm::errs()));
}

TEST_F(XorStrTest, CleanupCoversReturnAndExceptionalResumeExits) {
  llvm::LLVMContext Context;
  llvm::Module Module("xorstr-cleanup-resume", Context);
  Module.setDataLayout("e-p:64:64-i64:64-n8:16:32:64-S128");
  llvm::Type *VoidTy = llvm::Type::getVoidTy(Context);
  llvm::Function *MayThrow = llvm::Function::Create(
      llvm::FunctionType::get(VoidTy, false),
      llvm::GlobalValue::ExternalLinkage, "may_throw", Module);
  llvm::Function *Personality = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt32Ty(Context), {}, true),
      llvm::GlobalValue::ExternalLinkage, "__gxx_personality_v0", Module);
  llvm::Function *Function = llvm::Function::Create(
      llvm::FunctionType::get(VoidTy, false),
      llvm::GlobalValue::ExternalLinkage, "cleanup_resume", Module);
  Function->setPersonalityFn(Personality);
  llvm::BasicBlock *Entry =
      llvm::BasicBlock::Create(Context, "entry", Function);
  llvm::BasicBlock *Normal =
      llvm::BasicBlock::Create(Context, "normal", Function);
  llvm::BasicBlock *Unwind =
      llvm::BasicBlock::Create(Context, "unwind", Function);
  llvm::IRBuilder<> Builder(Entry);
  llvm::AllocaInst *Buffer = Builder.CreateAlloca(
      llvm::ArrayType::get(Builder.getInt8Ty(), 16), nullptr, "xorstr.buf");
  Buffer->setMetadata("neverc.xorstr", llvm::MDNode::get(Context, {}));
  Builder.CreateInvoke(MayThrow, Normal, Unwind);
  Builder.SetInsertPoint(Normal);
  Builder.CreateRetVoid();
  Builder.SetInsertPoint(Unwind);
  llvm::LandingPadInst *Landing = Builder.CreateLandingPad(
      llvm::StructType::get(llvm::PointerType::get(Context, 0),
                            llvm::Type::getInt32Ty(Context)),
      0, "landing");
  Landing->setCleanup(true);
  Builder.CreateResume(Landing);

  llvm::FunctionAnalysisManager FAM;
  (void)neverc::xorstr::XorStrCleanupPass().run(*Function, FAM);
  ASSERT_FALSE(llvm::verifyModule(Module, &llvm::errs()));
  std::string IR;
  llvm::raw_string_ostream IRStream(IR);
  Module.print(IRStream, nullptr);
  IRStream.flush();
  const std::string Marker = "!neverc.xorstr.cleanup";
  size_t Count = 0;
  for (size_t Position = 0;
       (Position = IR.find(Marker, Position)) != std::string::npos;
       Position += Marker.size())
    ++Count;
  EXPECT_GE(Count, 2u)
      << "both the return and resume paths must wipe protected storage\n"
      << IR;
}

TEST_F(XorStrTest, CleanupCoversCatchSwitchUnwindToCaller) {
  llvm::LLVMContext Context;
  llvm::Module Module("xorstr-cleanup-catchswitch", Context);
  Module.setTargetTriple("x86_64-pc-windows-msvc");
  Module.setDataLayout("e-p:64:64-i64:64-n8:16:32:64-S128");
  llvm::Type *VoidTy = llvm::Type::getVoidTy(Context);
  llvm::Function *MayThrow = llvm::Function::Create(
      llvm::FunctionType::get(VoidTy, false),
      llvm::GlobalValue::ExternalLinkage, "may_throw", Module);
  llvm::Function *Personality = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt32Ty(Context), {}, true),
      llvm::GlobalValue::ExternalLinkage, "__CxxFrameHandler3", Module);
  llvm::Function *Function = llvm::Function::Create(
      llvm::FunctionType::get(VoidTy, false),
      llvm::GlobalValue::ExternalLinkage, "cleanup_catchswitch", Module);
  Function->setPersonalityFn(Personality);

  llvm::BasicBlock *Entry =
      llvm::BasicBlock::Create(Context, "entry", Function);
  llvm::BasicBlock *Done =
      llvm::BasicBlock::Create(Context, "done", Function);
  llvm::BasicBlock *Dispatch =
      llvm::BasicBlock::Create(Context, "catch.dispatch", Function);
  llvm::BasicBlock *Handler =
      llvm::BasicBlock::Create(Context, "catch", Function);

  llvm::IRBuilder<> Builder(Entry);
  llvm::AllocaInst *Buffer = Builder.CreateAlloca(
      llvm::ArrayType::get(Builder.getInt8Ty(), 16), nullptr, "xorstr.buf");
  Buffer->setMetadata("neverc.xorstr", llvm::MDNode::get(Context, {}));
  Builder.CreateInvoke(MayThrow, Done, Dispatch);
  Builder.SetInsertPoint(Done);
  Builder.CreateRetVoid();
  Builder.SetInsertPoint(Dispatch);
  llvm::CatchSwitchInst *CatchSwitch = Builder.CreateCatchSwitch(
      llvm::ConstantTokenNone::get(Context), /*UnwindBB=*/nullptr,
      /*NumHandlers=*/1, "catchswitch");
  CatchSwitch->addHandler(Handler);
  Builder.SetInsertPoint(Handler);
  llvm::CatchPadInst *CatchPad =
      Builder.CreateCatchPad(CatchSwitch, {}, "catchpad");
  Builder.CreateCatchRet(CatchPad, Done);

  ASSERT_FALSE(llvm::verifyModule(Module, &llvm::errs()));
  llvm::FunctionAnalysisManager FAM;
  (void)neverc::xorstr::XorStrCleanupPass().run(*Function, FAM);
  ASSERT_FALSE(llvm::verifyModule(Module, &llvm::errs()));

  std::string IR;
  llvm::raw_string_ostream IRStream(IR);
  Module.print(IRStream, nullptr);
  IRStream.flush();
  const std::string Marker = "!neverc.xorstr.cleanup";
  size_t Count = 0;
  for (size_t Position = 0;
       (Position = IR.find(Marker, Position)) != std::string::npos;
       Position += Marker.size())
    ++Count;
  EXPECT_GE(Count, 2u)
      << "both normal return and unmatched catch unwind must wipe protected "
         "storage\n"
      << IR;
  EXPECT_EQ(IR.find("catchswitch within none [label %catch] unwind to caller"),
            std::string::npos)
      << "the direct unwind-to-caller edge must be routed through a cleanup "
         "funclet\n"
      << IR;
}

TEST_F(XorStrTest, FinalizeWithoutXorStrIgnoresUnsupportedPointerWidth) {
  llvm::LLVMContext Context;
  bool SawError = false;
  Context.setDiagnosticHandlerCallBack(captureErrorDiagnostic, &SawError);
  llvm::Module Module("xorstr-finalize-noop", Context);
  Module.setDataLayout("e-p:128:128");

  llvm::ModuleAnalysisManager MAM;
  const llvm::PreservedAnalyses Result =
      neverc::xorstr::FinalizeXorStrPass(/*KeySeed=*/1).run(Module, MAM);

  EXPECT_FALSE(SawError)
      << "a mandatory finalization pass must be a no-op for modules with no "
         "xorstr decoder";
  EXPECT_TRUE(Result.areAllPreserved());
}

TEST_F(XorStrTest, FinalizeRejectsUndersizedDecoderOutputBuffer) {
  auto Run = [](uint64_t BufferBytes, bool ExpectError) {
    llvm::LLVMContext Context;
    bool SawError = false;
    Context.setDiagnosticHandlerCallBack(captureErrorDiagnostic, &SawError);
    llvm::Module Module("xorstr-finalize-output-size", Context);
    Module.setDataLayout("e-p:64:64-i64:64-n8:16:32:64-S128");

    llvm::Type *PointerTy = llvm::PointerType::get(Context, 0);
    llvm::Type *SizeTy = llvm::Type::getInt64Ty(Context);
    llvm::Function *Decoder = llvm::Function::Create(
        llvm::FunctionType::get(PointerTy,
                                {PointerTy, SizeTy, SizeTy, PointerTy}, false),
        llvm::GlobalValue::ExternalLinkage, "__neverc_xorstr_decrypt", Module);

    constexpr uint64_t Length = 4;
    constexpr uint64_t Key = 1;
    const neverc::xorstr::CipherSchedule Schedule =
        neverc::xorstr::makeSchedule(Key, Length, /*WordBits=*/64);
    const std::array<uint8_t, Length> Plaintext = {'t', 'e', 's', 't'};
    std::array<uint8_t, Length> Ciphertext;
    uint64_t State = Schedule.InitialState;
    for (uint64_t I = 0; I != Length; ++I) {
      State = neverc::xorstr::advanceState(State, I, Schedule,
                                           /*WordBits=*/64);
      Ciphertext[I] =
          Plaintext[I] ^ neverc::xorstr::streamByte(State, Schedule,
                                                    /*WordBits=*/64);
    }
    llvm::Constant *CiphertextInit = llvm::ConstantDataArray::get(
        Context, llvm::ArrayRef<uint8_t>(Ciphertext));
    auto *CiphertextGlobal = new llvm::GlobalVariable(
        Module, CiphertextInit->getType(), true,
        llvm::GlobalValue::PrivateLinkage, CiphertextInit, "ciphertext");

    llvm::Function *Caller = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(Context), false),
        llvm::GlobalValue::ExternalLinkage, "caller", Module);
    llvm::IRBuilder<> Builder(
        llvm::BasicBlock::Create(Context, "entry", Caller));
    llvm::AllocaInst *Output = Builder.CreateAlloca(
        llvm::ArrayType::get(Builder.getInt8Ty(), BufferBytes), nullptr,
        "output");
    Builder.CreateCall(Decoder, {CiphertextGlobal,
                                 Builder.getInt64(neverc::xorstr::sealLength(
                                     Length, Key, /*WordBits=*/64)),
                                 Builder.getInt64(Key), Output});
    Builder.CreateRetVoid();

    llvm::ModuleAnalysisManager MAM;
    (void)neverc::xorstr::FinalizeXorStrPass(/*KeySeed=*/1).run(Module, MAM);
    EXPECT_EQ(SawError, ExpectError);
    EXPECT_FALSE(llvm::verifyModule(Module, &llvm::errs()));
    if (ExpectError)
      EXPECT_NE(Module.getFunction("__neverc_xorstr_decrypt"), nullptr)
          << "an unsafe decoder call must remain visible after fail-closed "
             "finalization";
    else
      EXPECT_EQ(Module.getFunction("__neverc_xorstr_decrypt"), nullptr)
          << "a correctly sized output must still finalize completely";
  };

  Run(/*BufferBytes=*/4, /*ExpectError=*/true);
  Run(/*BufferBytes=*/5, /*ExpectError=*/false);
}

TEST_F(XorStrTest, GeneratedMixingUsesValidShiftsForSupportedNarrowWords) {
  for (unsigned WordBits : {8u, 16u}) {
    SCOPED_TRACE(WordBits);
    llvm::LLVMContext Context;
    llvm::Module Module("xorstr-narrow-word", Context);
    Module.setDataLayout(WordBits == 8 ? "e-p:8:8-i8:8-n8-S8"
                                       : "e-p:16:16-i16:16-n8:16-S16");
    llvm::Type *PointerTy = llvm::PointerType::get(Context, 0);
    llvm::Function *Consume = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(Context), {PointerTy},
                                false),
        llvm::GlobalValue::ExternalLinkage, "consume", Module);
    llvm::Constant *Literal =
        llvm::ConstantDataArray::getString(Context, "abc", true);
    auto *LiteralGV = new llvm::GlobalVariable(
        Module, Literal->getType(), true, llvm::GlobalValue::PrivateLinkage,
        Literal, ".str");
    LiteralGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    llvm::Function *Caller = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(Context), false),
        llvm::GlobalValue::ExternalLinkage, "caller", Module);
    llvm::IRBuilder<> Builder(
        llvm::BasicBlock::Create(Context, "entry", Caller));
    Builder.CreateCall(Consume, {LiteralGV});
    Builder.CreateRetVoid();

    llvm::ModuleAnalysisManager MAM;
    (void)neverc::xorstr::EncryptCallStringsPass(/*MaxLen=*/1024,
                                                 /*KeySeed=*/1)
        .run(Module, MAM);
    ASSERT_FALSE(llvm::verifyModule(Module, &llvm::errs()));

    for (llvm::Function &Function : Module) {
      for (llvm::BasicBlock &Block : Function) {
        for (llvm::Instruction &Instruction : Block) {
          auto *Shift = llvm::dyn_cast<llvm::BinaryOperator>(&Instruction);
          if (!Shift || (Shift->getOpcode() != llvm::Instruction::Shl &&
                         Shift->getOpcode() != llvm::Instruction::LShr))
            continue;
          auto *Amount =
              llvm::dyn_cast<llvm::ConstantInt>(Shift->getOperand(1));
          ASSERT_NE(Amount, nullptr);
          EXPECT_LT(Amount->getZExtValue(),
                    Shift->getType()->getIntegerBitWidth())
              << "a shift by the scalar width produces poison";
        }
      }
    }
  }
}

TEST_F(XorStrTest, AutomaticEncryptionProtectsCallsInExceptionPads) {
  llvm::LLVMContext Context;
  llvm::Module Module("xorstr-exception-pad", Context);
  Module.setTargetTriple("aarch64-unknown-linux-android");
  Module.setDataLayout("e-p:64:64-i64:64-n32:64-S128");
  {
    llvm::Type *VoidTy = llvm::Type::getVoidTy(Context);
    llvm::FunctionType *VoidFnTy = llvm::FunctionType::get(VoidTy, false);
    llvm::Function *MayThrow = llvm::Function::Create(
        VoidFnTy, llvm::GlobalValue::ExternalLinkage, "may_throw", Module);
    llvm::Function *Consume = llvm::Function::Create(
        llvm::FunctionType::get(VoidTy, {llvm::PointerType::get(Context, 0)},
                                false),
        llvm::GlobalValue::ExternalLinkage, "consume_exception_text", Module);
    llvm::Function *Personality = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getInt32Ty(Context), {}, true),
        llvm::GlobalValue::ExternalLinkage, "__gxx_personality_v0", Module);
    llvm::Function *Caller = llvm::Function::Create(
        VoidFnTy, llvm::GlobalValue::ExternalLinkage,
        "call_from_exception_pad", Module);
    Caller->setPersonalityFn(Personality);

    llvm::Constant *Literal =
        llvm::ConstantDataArray::getString(Context, "eh-pad-secret", true);
    auto *LiteralGV = new llvm::GlobalVariable(
        Module, Literal->getType(), true, llvm::GlobalValue::PrivateLinkage,
        Literal, ".str");
    LiteralGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

    llvm::BasicBlock *Entry =
        llvm::BasicBlock::Create(Context, "entry", Caller);
    llvm::BasicBlock *Catch =
        llvm::BasicBlock::Create(Context, "catch", Caller);
    llvm::BasicBlock *Done =
        llvm::BasicBlock::Create(Context, "done", Caller);
    llvm::IRBuilder<> Builder(Entry);
    Builder.CreateInvoke(MayThrow, Done, Catch);
    Builder.SetInsertPoint(Catch);
    auto *Landing = Builder.CreateLandingPad(
        llvm::StructType::get(llvm::PointerType::get(Context, 0),
                              llvm::Type::getInt32Ty(Context)),
        0, "landing");
    Landing->setCleanup(true);
    Builder.CreateCall(Consume, LiteralGV);
    Builder.CreateBr(Done);
    Builder.SetInsertPoint(Done);
    Builder.CreateRetVoid();

  }

  llvm::ModuleAnalysisManager MAM;
  (void)neverc::xorstr::EncryptCallStringsPass(/*MaxLen=*/1024,
                                                /*KeySeed=*/1)
      .run(Module, MAM);
  llvm::FunctionAnalysisManager FAM;
  for (llvm::Function &F : Module)
    if (!F.isDeclaration())
      (void)neverc::xorstr::XorStrCleanupPass().run(F, FAM);
  ASSERT_FALSE(llvm::verifyModule(Module, &llvm::errs()));
  std::string IR;
  llvm::raw_string_ostream IRStream(IR);
  Module.print(IRStream, nullptr);
  IRStream.flush();
  ASSERT_FALSE(IR.empty());
  EXPECT_EQ(IR.find("eh-pad-secret"), std::string::npos)
      << "a direct call literal in an exception pad must be encrypted\n"
      << IR;
  EXPECT_NE(IR.find("!neverc.xorstr.cleanup"), std::string::npos)
      << "the exception-pad plaintext buffer must be wiped\n"
      << IR;
}

TEST_F(XorStrTest, IRInputHonorsAutomaticEncryptionAndFullWidthSeed) {
  const fs::path Input = tmpFile("xorstr_ir_input.bc");
  {
    llvm::LLVMContext Context;
    llvm::Module Module("xorstr-ir-input", Context);
    Module.setTargetTriple("aarch64-unknown-linux-android");
    Module.setDataLayout("e-p:64:64-i64:64-n32:64-S128");
    llvm::Type *VoidTy = llvm::Type::getVoidTy(Context);
    llvm::Function *Consume = llvm::Function::Create(
        llvm::FunctionType::get(VoidTy, {llvm::PointerType::get(Context, 0)},
                                false),
        llvm::GlobalValue::ExternalLinkage, "consume_ir_text", Module);
    llvm::Function *Caller = llvm::Function::Create(
        llvm::FunctionType::get(VoidTy, false),
        llvm::GlobalValue::ExternalLinkage, "call_ir_text", Module);
    llvm::Constant *Literal =
        llvm::ConstantDataArray::getString(Context, "ir-input-secret", true);
    auto *LiteralGV = new llvm::GlobalVariable(
        Module, Literal->getType(), true, llvm::GlobalValue::PrivateLinkage,
        Literal, ".str");
    LiteralGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    llvm::IRBuilder<> Builder(
        llvm::BasicBlock::Create(Context, "entry", Caller));
    Builder.CreateCall(Consume, {LiteralGV});
    Builder.CreateRetVoid();

    std::error_code Error;
    llvm::raw_fd_ostream Stream(Input.string(), Error);
    ASSERT_FALSE(Error) << Error.message();
    llvm::WriteBitcodeToFile(Module, Stream);
  }

  auto CompileIR = [&](llvm::StringRef Name,
                       llvm::StringRef Seed) -> std::string {
    const fs::path Output = tmpFile(Name.str() + ".ll");
    std::vector<std::string> Args = {
        "-S", "-emit-llvm", "-O1", "-fno-lto",
        "-fno-builtin-mimalloc", "-fencrypt-call-strings",
    };
    if (!Seed.empty())
      Args.push_back("-fstring-encrypt-key=" + Seed.str());
    Args.insert(Args.end(), {Input.string(), "-o", Output.string()});
    const CmdResult Build = ncc(Args);
    EXPECT_EQ(Build.exitCode, 0) << Build.err;
    return Build.exitCode == 0 ? readFile(Output) : std::string();
  };

  const std::string FixedA =
      CompileIR("xorstr_ir_fixed_a", "0xFEDCBA9876543210");
  const std::string FixedB =
      CompileIR("xorstr_ir_fixed_b", "0xFEDCBA9876543210");
  ASSERT_FALSE(FixedA.empty());
  ASSERT_FALSE(FixedB.empty());
  EXPECT_EQ(FixedA.find("ir-input-secret"), std::string::npos);
  EXPECT_NE(FixedA.find("!neverc.xorstr.cleanup"), std::string::npos);
  EXPECT_EQ(FixedA, FixedB)
      << "a fixed 64-bit seed must reproduce imported-IR protection";

  const std::string RandomA = CompileIR("xorstr_ir_random_a", "");
  const std::string RandomB = CompileIR("xorstr_ir_random_b", "");
  ASSERT_FALSE(RandomA.empty());
  ASSERT_FALSE(RandomB.empty());
  EXPECT_NE(RandomA, RandomB)
      << "the default seed must use fresh entropy for imported IR too";
}

TEST_F(XorStrTest, Codegen_ProviderCannotBypassFinalSealing) {
  auto src = xorStrDir() / "nc_xorstr_codegen.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto object = tmpFile("xorstr_provider_sealed.o");
  std::vector<std::string> args = {
      "-c",
      "-O2",
      "-fno-lto",
      "-fno-builtin-mimalloc",
      "-include",
      "neverc/xorstr/xorstr.h",
      std::string("-fplugin=") + NEVERC_TEST_IR_OPTIMIZATION_PASSTHROUGH_PLUGIN,
  };
  for (auto &f : sysrootFlags())
    args.push_back(f);
  for (auto &f : archFlags())
    args.push_back(f);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(object.string());

  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << "provider object compilation failed\n" << r.err;
  const std::string Bytes = readFile(object);
  EXPECT_EQ(Bytes.find("__neverc_xorstr_"), std::string::npos)
      << "an IR optimization provider must not bypass final xorstr sealing";
  EXPECT_EQ(Bytes.find(".rekey"), std::string::npos)
      << "provider machine-code output must not identify ciphertext globals";
  EXPECT_EQ(Bytes.find("GetProcAddress"), std::string::npos)
      << "provider machine-code output must not contain plaintext";
}

TEST_F(XorStrTest, Codegen_FixedSeedIsStableButDomainSeparated) {
  const fs::path Source = tmpFile("xorstr_fixed_seed_domain.c");
  const fs::path AlphaObject = tmpFile("xorstr_fixed_seed_alpha.o");
  const fs::path OmegaObject = tmpFile("xorstr_fixed_seed_omega.o");
  const fs::path AlphaRepeatObject =
      tmpFile("xorstr_fixed_seed_alpha_repeat.o");

  auto Compile = [&](const char *Literal, const fs::path &Output) {
    writeFile(Source, std::string("const char *test_xorstr(void) { return ") +
                          "NC_XORSTR(\"" + Literal + "\"); }\n");
    std::vector<std::string> Args = {"-c",
                                     "-O2",
                                     "-fno-lto",
                                     "-fno-builtin-mimalloc",
                                     "-fstring-encrypt-key=1",
                                     "-include",
                                     "neverc/xorstr/xorstr.h"};
    for (auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.push_back(Source.string());
    Args.push_back("-o");
    Args.push_back(Output.string());
    return ncc(Args);
  };

  CmdResult Alpha = Compile("fixed-seed-alpha", AlphaObject);
  ASSERT_EQ(Alpha.exitCode, 0) << Alpha.err;
  CmdResult Omega = Compile("fixed-seed-omega", OmegaObject);
  ASSERT_EQ(Omega.exitCode, 0) << Omega.err;
  CmdResult AlphaRepeat = Compile("fixed-seed-alpha", AlphaRepeatObject);
  ASSERT_EQ(AlphaRepeat.exitCode, 0) << AlphaRepeat.err;

  auto AlphaText = readTextSections(readFile(AlphaObject));
  ASSERT_TRUE(static_cast<bool>(AlphaText))
      << llvm::toString(AlphaText.takeError()).str().str();
  auto OmegaText = readTextSections(readFile(OmegaObject));
  ASSERT_TRUE(static_cast<bool>(OmegaText))
      << llvm::toString(OmegaText.takeError()).str().str();
  auto AlphaRepeatText = readTextSections(readFile(AlphaRepeatObject));
  ASSERT_TRUE(static_cast<bool>(AlphaRepeatText))
      << llvm::toString(AlphaRepeatText.takeError()).str().str();

  EXPECT_EQ(*AlphaText, *AlphaRepeatText)
      << "a fixed seed must keep identical inputs reproducible";
  EXPECT_NE(*AlphaText, *OmegaText)
      << "same-shape translation units must not reuse a fixed-seed key stream";
}

TEST_F(XorStrTest, Codegen_FixedSeedIsStableAcrossParallelFrontends) {
  std::vector<fs::path> sources;
  std::string mainSource;
  for (unsigned i = 0; i != 16; ++i) {
    fs::path source = tmpFile("xorstr_parallel_" + std::to_string(i) + ".c");
    writeFile(source, "int xorstr_parallel_" + std::to_string(i) +
                          "(void) { return NC_XORSTR(\"parallel-secret-" +
                          std::to_string(i) + "\")[0] == 'p' ? 0 : 1; }\n");
    sources.push_back(source);
    mainSource += "int xorstr_parallel_" + std::to_string(i) + "(void);\n";
  }
  mainSource += "int main(void) { return ";
  for (unsigned i = 0; i != sources.size(); ++i) {
    if (i != 0)
      mainSource += " + ";
    mainSource += "xorstr_parallel_" + std::to_string(i) + "()";
  }
  mainSource += "; }\n";
  fs::path main = tmpFile("xorstr_parallel_main.c");
  writeFile(main, mainSource);
  sources.push_back(main);

  auto compile = [&](const fs::path &output) {
    std::vector<std::string> args = {"-O2", "-fstring-encrypt-key=1",
                                     "-fno-builtin-mimalloc", "-include",
                                     "neverc/xorstr/xorstr.h"};
    for (auto &flag : sysrootFlags())
      args.push_back(flag);
    for (auto &flag : archFlags())
      args.push_back(flag);
    for (const fs::path &source : sources)
      args.push_back(source.string());
    args.push_back("-o");
    args.push_back(output.string());
    return ncc(args);
  };

  fs::path first = tmpFile("xorstr_parallel_first");
  fs::path second = tmpFile("xorstr_parallel_second");
  auto firstBuild = compile(first);
  ASSERT_EQ(firstBuild.exitCode, 0) << firstBuild.err;
  auto secondBuild = compile(second);
  ASSERT_EQ(secondBuild.exitCode, 0) << secondBuild.err;

  auto firstText = readTextSections(readFile(first));
  ASSERT_TRUE(static_cast<bool>(firstText))
      << llvm::toString(firstText.takeError()).str().str();
  auto secondText = readTextSections(readFile(second));
  ASSERT_TRUE(static_cast<bool>(secondText))
      << llvm::toString(secondText.takeError()).str().str();
  EXPECT_EQ(*firstText, *secondText)
      << "a fixed seed must not depend on parallel frontend scheduling";
}

TEST_F(XorStrTest, EncryptCallStrings_FixedSeedIsStableButDomainSeparated) {
  const fs::path Source = tmpFile("auto_xorstr_fixed_seed_domain.c");
  const fs::path AlphaObject = tmpFile("auto_xorstr_fixed_seed_alpha.o");
  const fs::path OmegaObject = tmpFile("auto_xorstr_fixed_seed_omega.o");
  const fs::path AlphaRepeatObject =
      tmpFile("auto_xorstr_fixed_seed_alpha_repeat.o");

  auto Compile = [&](const char *Literal, const fs::path &Output) {
    writeFile(Source,
              "extern void consume(const char *);\n" +
                  std::string("void test_auto_xorstr(void) { consume(\"") +
                  Literal + "\"); }\n");
    std::vector<std::string> Args = {"-c",
                                     "-O2",
                                     "-fno-lto",
                                     "-fno-builtin-mimalloc",
                                     "-fencrypt-call-strings",
                                     "-fstring-encrypt-key=1"};
    for (auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.push_back(Source.string());
    Args.push_back("-o");
    Args.push_back(Output.string());
    return ncc(Args);
  };

  CmdResult Alpha = Compile("fixed-seed-alpha", AlphaObject);
  ASSERT_EQ(Alpha.exitCode, 0) << Alpha.err;
  CmdResult Omega = Compile("fixed-seed-omega", OmegaObject);
  ASSERT_EQ(Omega.exitCode, 0) << Omega.err;
  CmdResult AlphaRepeat = Compile("fixed-seed-alpha", AlphaRepeatObject);
  ASSERT_EQ(AlphaRepeat.exitCode, 0) << AlphaRepeat.err;

  auto AlphaText = readTextSections(readFile(AlphaObject));
  ASSERT_TRUE(static_cast<bool>(AlphaText))
      << llvm::toString(AlphaText.takeError()).str().str();
  auto OmegaText = readTextSections(readFile(OmegaObject));
  ASSERT_TRUE(static_cast<bool>(OmegaText))
      << llvm::toString(OmegaText.takeError()).str().str();
  auto AlphaRepeatText = readTextSections(readFile(AlphaRepeatObject));
  ASSERT_TRUE(static_cast<bool>(AlphaRepeatText))
      << llvm::toString(AlphaRepeatText.takeError()).str().str();

  EXPECT_EQ(*AlphaText, *AlphaRepeatText)
      << "automatic encryption must honor a fixed reproducible seed";
  EXPECT_NE(*AlphaText, *OmegaText)
      << "automatic encryption must domain-separate same-shape inputs";
}

TEST_F(XorStrTest, StringEncryptKey_AcceptsFullWidthHexSeed) {
  const fs::path Source = tmpFile("xorstr_full_width_seed.c");
  const fs::path LowObject = tmpFile("xorstr_full_width_seed_low.o");
  const fs::path HighObject = tmpFile("xorstr_full_width_seed_high.o");
  const fs::path HighRepeatObject =
      tmpFile("xorstr_full_width_seed_high_repeat.o");
  writeFile(Source,
            "extern void consume(const char *);\n"
            "void test_seed(void) { consume(\"full-width-seed-secret\"); }\n");

  auto Compile = [&](const char *Seed, const fs::path &Output) {
    std::vector<std::string> Args = {"-c",
                                     "-O2",
                                     "-fno-lto",
                                     "-fno-builtin-mimalloc",
                                     "-fencrypt-call-strings",
                                     std::string("-fstring-encrypt-key=") +
                                         Seed};
    for (auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.push_back(Source.string());
    Args.push_back("-o");
    Args.push_back(Output.string());
    return ncc(Args);
  };

  CmdResult Low = Compile("0x00000001DEADBEEF", LowObject);
  ASSERT_EQ(Low.exitCode, 0) << Low.err;
  CmdResult High = Compile("0x12345678DEADBEEF", HighObject);
  ASSERT_EQ(High.exitCode, 0) << High.err;
  CmdResult HighRepeat = Compile("0x12345678DEADBEEF", HighRepeatObject);
  ASSERT_EQ(HighRepeat.exitCode, 0) << HighRepeat.err;

  auto LowText = readTextSections(readFile(LowObject));
  ASSERT_TRUE(static_cast<bool>(LowText))
      << llvm::toString(LowText.takeError()).str().str();
  auto HighText = readTextSections(readFile(HighObject));
  ASSERT_TRUE(static_cast<bool>(HighText))
      << llvm::toString(HighText.takeError()).str().str();
  auto HighRepeatText = readTextSections(readFile(HighRepeatObject));
  ASSERT_TRUE(static_cast<bool>(HighRepeatText))
      << llvm::toString(HighRepeatText.takeError()).str().str();

  EXPECT_NE(*LowText, *HighText)
      << "the upper 32 seed bits must affect automatic encryption";
  EXPECT_EQ(*HighText, *HighRepeatText)
      << "a fixed 64-bit seed must remain reproducible";
}

TEST_F(XorStrTest, Codegen_DefaultFinalLinkSeedBypassesLTOCache) {
  ScopedEnvironmentVariable CacheEnabled(linker::ltoCacheEnvVar, "1");

  const fs::path Source = tmpFile("xorstr_cached_link_source.c");
  const fs::path Main = tmpFile("xorstr_cached_link_main.c");
  const fs::path LTOObject = tmpFile("xorstr_cached_link_source.o");
  const fs::path MainObject = tmpFile("xorstr_cached_link_main.o");
  writeFile(Source, "int cached_xorstr(void) {\n"
                    "  return NC_XORSTR(\"cached-link-secret\")[0] != 'c';\n"
                    "}\n");
  writeFile(Main, "extern int cached_xorstr(void);\n"
                  "int main(void) { return cached_xorstr(); }\n");

  std::vector<std::string> CompileArgs = {"-c",
                                          "-O2",
                                          "-flto=full",
                                          "-fno-builtin-mimalloc",
                                          "-fstring-encrypt-key=1",
                                          "-include",
                                          "neverc/xorstr/xorstr.h"};
  for (auto &Flag : sysrootFlags())
    CompileArgs.push_back(Flag);
  for (auto &Flag : archFlags())
    CompileArgs.push_back(Flag);
  CompileArgs.push_back(Source.string());
  CompileArgs.push_back("-o");
  CompileArgs.push_back(LTOObject.string());
  CmdResult SourceBuild = ncc(CompileArgs);
  ASSERT_EQ(SourceBuild.exitCode, 0) << SourceBuild.err;

  std::vector<std::string> MainArgs = {"-c", "-O2", "-fno-lto",
                                       "-fno-builtin-mimalloc"};
  for (auto &Flag : sysrootFlags())
    MainArgs.push_back(Flag);
  for (auto &Flag : archFlags())
    MainArgs.push_back(Flag);
  MainArgs.push_back(Main.string());
  MainArgs.push_back("-o");
  MainArgs.push_back(MainObject.string());
  CmdResult MainBuild = ncc(MainArgs);
  ASSERT_EQ(MainBuild.exitCode, 0) << MainBuild.err;

  for (const char *Mode : {"", "-flto=full"}) {
    const std::string ModeName = Mode[0] ? "full" : "auto";
    SCOPED_TRACE(ModeName);
    ScopedEnvironmentVariable CacheDirectory(
        linker::ltoCacheDirEnvVar, tmpFile("lto-cache-" + ModeName).string());
    const fs::path First = tmpFile("xorstr_cached_link_first_" + ModeName);
    const fs::path Second = tmpFile("xorstr_cached_link_second_" + ModeName);

    auto Link = [&](const fs::path &Output) {
      std::vector<std::string> Args = {"-O2",    "-fno-builtin-mimalloc",
                                       "-mllvm", "-neverc-pcg-min-funcs=1",
                                       "-mllvm", "-neverc-pcg-weight-floor=0"};
      if (Mode[0])
        Args.push_back(Mode);
      for (auto &Flag : sysrootFlags())
        Args.push_back(Flag);
      for (auto &Flag : archFlags())
        Args.push_back(Flag);
      Args.push_back(LTOObject.string());
      Args.push_back(MainObject.string());
      for (auto &Flag : linkFlags())
        Args.push_back(Flag);
      Args.push_back("-o");
      Args.push_back(Output.string());
      return ncc(Args);
    };

    CmdResult FirstLink = Link(First);
    ASSERT_EQ(FirstLink.exitCode, 0) << FirstLink.err;
    CmdResult SecondLink = Link(Second);
    ASSERT_EQ(SecondLink.exitCode, 0) << SecondLink.err;

    auto FirstText = readTextSections(readFile(First));
    ASSERT_TRUE(static_cast<bool>(FirstText))
        << llvm::toString(FirstText.takeError()).str().str();
    auto SecondText = readTextSections(readFile(Second));
    ASSERT_TRUE(static_cast<bool>(SecondText))
        << llvm::toString(SecondText.takeError()).str().str();
    EXPECT_NE(*FirstText, *SecondText)
        << "neither the full-link nor partition cache may replay a "
           "default-seed xorstr artifact";

    EXPECT_EQ(exec(First.string(), {}).exitCode, 0);
    EXPECT_EQ(exec(Second.string(), {}).exitCode, 0);
  }
}

TEST_F(XorStrTest,
       EncryptCallStrings_DefaultFinalLinkSeedBypassesLTOCacheForLTOExposedLiteral) {
  ScopedEnvironmentVariable CacheEnabled(linker::ltoCacheEnvVar, "1");

  const fs::path SourceA =
      xorStrDir() / "encrypt_call_strings_lto_late_a.c";
  const fs::path SourceB =
      xorStrDir() / "encrypt_call_strings_lto_late_b.c";
  const fs::path ConsumerSource =
      xorStrDir() / "encrypt_call_strings_lto_late_consume.c";
  if (!fs::exists(SourceA) || !fs::exists(SourceB) ||
      !fs::exists(ConsumerSource))
    GTEST_SKIP() << "late-LTO xorstr fixtures not found";

  const fs::path ObjectA = tmpFile("xorstr_cached_late_a.o");
  const fs::path ObjectB = tmpFile("xorstr_cached_late_b.o");
  const fs::path ConsumerObject = tmpFile("xorstr_cached_late_consume.o");

  auto Compile = [&](const fs::path &Source, const fs::path &Output,
                     bool IsLTO) {
    std::vector<std::string> Args = {"-c", "-std=c11", "-O2",
                                     IsLTO ? "-flto=full" : "-fno-lto",
                                     "-fno-builtin-mimalloc"};
    for (auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.push_back(Source.string());
    Args.push_back("-o");
    Args.push_back(Output.string());
    return ncc(Args);
  };

  ASSERT_EQ(Compile(SourceA, ObjectA, true).exitCode, 0);
  ASSERT_EQ(Compile(SourceB, ObjectB, true).exitCode, 0);
  ASSERT_EQ(Compile(ConsumerSource, ConsumerObject, false).exitCode, 0);
  EXPECT_EQ(readFile(ObjectA).find("__neverc_xorstr_"), std::string::npos);
  EXPECT_EQ(readFile(ObjectB).find("__neverc_xorstr_"), std::string::npos)
      << "the regression requires an input whose literal becomes a call "
         "argument only during LTO";

  for (const char *Mode : {"", "-flto=full"}) {
    const std::string ModeName = Mode[0] ? "full" : "auto";
    SCOPED_TRACE(ModeName);
    ScopedEnvironmentVariable CacheDirectory(
        linker::ltoCacheDirEnvVar,
        tmpFile("late-auto-lto-cache-" + ModeName).string());
    const fs::path First =
        tmpFile("xorstr_cached_late_first_" + ModeName);
    const fs::path Second =
        tmpFile("xorstr_cached_late_second_" + ModeName);

    auto Link = [&](const fs::path &Output) {
      std::vector<std::string> Args = {
          "-O2", "-fencrypt-call-strings", "-fno-builtin-mimalloc",
          "-mllvm", "-neverc-pcg-min-funcs=1", "-mllvm",
          "-neverc-pcg-weight-floor=0"};
      if (Mode[0])
        Args.push_back(Mode);
      for (auto &Flag : sysrootFlags())
        Args.push_back(Flag);
      for (auto &Flag : archFlags())
        Args.push_back(Flag);
      Args.insert(Args.end(), {ObjectA.string(), ObjectB.string(),
                               ConsumerObject.string()});
      for (auto &Flag : linkFlags())
        Args.push_back(Flag);
      Args.push_back("-o");
      Args.push_back(Output.string());
      return ncc(Args);
    };

    CmdResult FirstLink = Link(First);
    ASSERT_EQ(FirstLink.exitCode, 0) << FirstLink.err;
    CmdResult SecondLink = Link(Second);
    ASSERT_EQ(SecondLink.exitCode, 0) << SecondLink.err;

    auto FirstText = readTextSections(readFile(First));
    ASSERT_TRUE(static_cast<bool>(FirstText))
        << llvm::toString(FirstText.takeError()).str().str();
    auto SecondText = readTextSections(readFile(Second));
    ASSERT_TRUE(static_cast<bool>(SecondText))
        << llvm::toString(SecondText.takeError()).str().str();
    EXPECT_NE(*FirstText, *SecondText)
        << "automatic encryption introduced only after LTO must still bypass "
           "both cache layers when the final seed is fresh";

    for (const fs::path &Executable : {First, Second}) {
      const std::string Bytes = readFile(Executable);
      EXPECT_EQ(Bytes.find("lto-late-secret-alpha"), std::string::npos);
      EXPECT_EQ(Bytes.find("__neverc_xorstr_"), std::string::npos);
      EXPECT_EQ(exec(Executable.string(), {}).exitCode, 0);
    }
  }
}

TEST_F(XorStrTest, Codegen_FinalLTORekeyUsesAll64SeedBits) {
  ScopedEnvironmentVariable CacheDirectory(linker::ltoCacheDirEnvVar,
                                           tmpFile("lto-seed-cache").string());
  ScopedEnvironmentVariable CacheEnabled(linker::ltoCacheEnvVar, "1");

  const fs::path Source = tmpFile("xorstr_lto_seed_source.c");
  const fs::path Main = tmpFile("xorstr_lto_seed_main.c");
  const fs::path LTOObject = tmpFile("xorstr_lto_seed_source.o");
  const fs::path MainObject = tmpFile("xorstr_lto_seed_main.o");
  writeFile(Source, "int lto_seed_xorstr(void) {\n"
                    "  return NC_XORSTR(\"full-width-link-seed\")[0] != 'f';\n"
                    "}\n");
  writeFile(Main, "extern int lto_seed_xorstr(void);\n"
                  "int main(void) { return lto_seed_xorstr(); }\n");

  auto Compile = [&](const fs::path &Input, const fs::path &Output,
                     bool IsLTO) {
    std::vector<std::string> Args = {"-c", "-O2",
                                     IsLTO ? "-flto=full" : "-fno-lto",
                                     "-fno-builtin-mimalloc"};
    if (IsLTO)
      Args.insert(Args.end(), {"-fstring-encrypt-key=1", "-include",
                               "neverc/xorstr/xorstr.h"});
    for (auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.push_back(Input.string());
    Args.push_back("-o");
    Args.push_back(Output.string());
    return ncc(Args);
  };
  CmdResult SourceBuild = Compile(Source, LTOObject, true);
  ASSERT_EQ(SourceBuild.exitCode, 0) << SourceBuild.err;
  CmdResult MainBuild = Compile(Main, MainObject, false);
  ASSERT_EQ(MainBuild.exitCode, 0) << MainBuild.err;

  auto Link = [&](const char *Seed, const fs::path &Output) {
    std::vector<std::string> Args = {
        "-O2", "-flto=full", "-fno-builtin-mimalloc",
        std::string("-fstring-encrypt-key=") + Seed};
    for (auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.push_back(LTOObject.string());
    Args.push_back(MainObject.string());
    for (auto &Flag : linkFlags())
      Args.push_back(Flag);
    Args.push_back("-o");
    Args.push_back(Output.string());
    return ncc(Args);
  };

  const fs::path Low = tmpFile("xorstr_lto_seed_low");
  const fs::path High = tmpFile("xorstr_lto_seed_high");
  const fs::path HighRepeat = tmpFile("xorstr_lto_seed_high_repeat");
  CmdResult LowLink = Link("0x00000001DEADBEEF", Low);
  ASSERT_EQ(LowLink.exitCode, 0) << LowLink.err;
  CmdResult HighLink = Link("0x12345678DEADBEEF", High);
  ASSERT_EQ(HighLink.exitCode, 0) << HighLink.err;
  CmdResult HighRepeatLink = Link("0x12345678DEADBEEF", HighRepeat);
  ASSERT_EQ(HighRepeatLink.exitCode, 0) << HighRepeatLink.err;

  auto LowText = readTextSections(readFile(Low));
  ASSERT_TRUE(static_cast<bool>(LowText))
      << llvm::toString(LowText.takeError()).str().str();
  auto HighText = readTextSections(readFile(High));
  ASSERT_TRUE(static_cast<bool>(HighText))
      << llvm::toString(HighText.takeError()).str().str();
  auto HighRepeatText = readTextSections(readFile(HighRepeat));
  ASSERT_TRUE(static_cast<bool>(HighRepeatText))
      << llvm::toString(HighRepeatText.takeError()).str().str();
  EXPECT_NE(*LowText, *HighText)
      << "the final LTO rekey and its cache key must use the upper 32 bits";
  EXPECT_EQ(*HighText, *HighRepeatText)
      << "a fixed full-width final-link seed must remain reproducible";
  EXPECT_EQ(exec(Low.string(), {}).exitCode, 0);
  EXPECT_EQ(exec(High.string(), {}).exitCode, 0);
  EXPECT_EQ(exec(HighRepeat.string(), {}).exitCode, 0);
}

TEST_F(XorStrTest, Runtime_StatefulDecryptRoundTrip) {
  auto src = xorStrDir() / "nc_xorstr_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  compileRunAndCheck("xorstr_runtime", src.string(),
                     "-std=c11 -O2 -fno-builtin-mimalloc");
}

TEST_F(XorStrTest, Runtime_PreservesCiphertextPointerOffset) {
  auto src = xorStrDir() / "nc_xorstr_ciphertext_offset_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  compileRunAndCheck("xorstr_ciphertext_offset_runtime", src.string(),
                     "-std=c11 -O2 -fno-lto -fno-builtin-mimalloc");
}

TEST_F(XorStrTest, Runtime_CleanupPreservesMustTailIRValidity) {
  auto src = xorStrDir() / "nc_xorstr_musttail_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  compileRunAndCheck("xorstr_musttail_runtime", src.string(),
                     "-std=c23 -O2 -fno-lto -fno-builtin-mimalloc");
}

TEST_F(XorStrTest, Runtime_RejectsXorStrBufferPassedByMustTail) {
  auto src = xorStrDir() / "nc_xorstr_musttail_argument.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto object = tmpFile("xorstr_musttail_argument.o");
  std::vector<std::string> args = {"-c", "-std=c23", "-O2", "-fno-lto",
                                   "-fno-builtin-mimalloc"};
  for (auto &flag : sysrootFlags())
    args.push_back(flag);
  for (auto &flag : archFlags())
    args.push_back(flag);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(object.string());

  auto result = ncc(args);
  EXPECT_NE(result.exitCode, 0);
  EXPECT_TRUE(result.stderrContains(
      "xorstr stack buffer cannot be passed to a musttail call"))
      << result.err;
}

// ---- -fencrypt-call-strings auto-encryption ----

TEST_F(XorStrTest, EncryptCallStrings_AutoEncrypt) {
  auto src = xorStrDir() / "encrypt_call_strings.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto ir = tmpFile("encrypt_call_strings.ll");
  std::vector<std::string> args = {"-S", "-emit-llvm", "-O1",
                                   "-fencrypt-call-strings"};
  for (auto &f : sysrootFlags())
    args.push_back(f);
  for (auto &f : archFlags())
    args.push_back(f);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(ir.string());
  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << "encrypt-call-strings failed\n" << r.err;

  auto irContent = readFile(ir);

  EXPECT_NE(irContent.find("xorstr"), std::string::npos)
      << "expected xorstr pattern in auto-encrypted IR";

  EXPECT_NE(irContent.find("load volatile i8"), std::string::npos)
      << "auto-encrypted ciphertext loads must resist later folding";

  EXPECT_EQ(irContent.find("\"hello auto\""), std::string::npos)
      << "plaintext 'hello auto' should not appear in IR";
}

TEST_F(XorStrTest, EncryptCallStrings_RejectsMustTailLiteral) {
  auto src = xorStrDir() / "encrypt_call_strings_musttail_argument.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto object = tmpFile("encrypt_call_strings_musttail_argument.o");
  std::vector<std::string> args = {"-c",
                                   "-std=c23",
                                   "-O0",
                                   "-fno-lto",
                                   "-fencrypt-call-strings",
                                   "-fno-builtin-mimalloc"};
  for (auto &flag : sysrootFlags())
    args.push_back(flag);
  for (auto &flag : archFlags())
    args.push_back(flag);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(object.string());

  auto result = ncc(args);
  EXPECT_NE(result.exitCode, 0);
  EXPECT_TRUE(result.stderrContains(
      "automatic string encryption cannot protect a musttail argument"))
      << result.err;
}

TEST_F(XorStrTest, EncryptCallStrings_PluginTailDoesNotDuplicateCleanup) {
  auto src = xorStrDir() / "encrypt_call_strings.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto ir = tmpFile("encrypt_call_strings_plugin_cleanup.ll");
  std::vector<std::string> args = {
      "-S", "-emit-llvm", "-O1", "-fencrypt-call-strings",
      std::string("-fplugin=") + NEVERC_TEST_EMPTY_PLUGIN};
  for (auto &f : sysrootFlags())
    args.push_back(f);
  for (auto &f : archFlags())
    args.push_back(f);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(ir.string());

  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << "plugin IR compilation failed\n" << r.err;
  const std::string IR = readFile(ir);
  const std::string Needle = "call void @llvm.memset";
  size_t Count = 0;
  for (size_t Position = 0;
       (Position = IR.find(Needle, Position)) != std::string::npos;
       Position += Needle.size())
    ++Count;
  EXPECT_EQ(Count, 1u)
      << "the mandatory plugin tail must not duplicate cleanup wipes\n"
      << IR;
}

TEST_F(XorStrTest, EncryptCallStrings_SealsLiteralIntroducedByLatePlugin) {
  const fs::path Source = tmpFile("xorstr_late_plugin_literal.c");
  const fs::path Object = tmpFile("xorstr_late_plugin_literal.o");
  const fs::path LTOObject = tmpFile("xorstr_late_plugin_literal_lto.o");
  const fs::path Consumer = tmpFile("xorstr_late_plugin_consumer.c");
  const fs::path ConsumerObject = tmpFile("xorstr_late_plugin_consumer.o");
  writeFile(Source,
            "extern void consume(const char *);\n"
            "__attribute__((noinline))\n"
            "const char *plugin_tail_source(void) {\n"
            "  return \"late-plugin-secret\";\n"
            "}\n"
            "void plugin_tail_target(void) { consume(\"early-secret\"); }\n");

  std::vector<std::string> Args = {
      "-c",
      "-std=c11",
      "-O1",
      "-fno-lto",
      "-fno-builtin-mimalloc",
      "-fencrypt-call-strings",
      "-fstring-encrypt-key=1",
      std::string("-fplugin=") + NEVERC_TEST_IR_PASS_LATE_LITERAL_PLUGIN,
  };
  for (auto &Flag : sysrootFlags())
    Args.push_back(Flag);
  for (auto &Flag : archFlags())
    Args.push_back(Flag);
  Args.push_back(Source.string());
  Args.push_back("-o");
  Args.push_back(Object.string());

  CmdResult Build = ncc(Args);
  ASSERT_EQ(Build.exitCode, 0) << Build.err;
  const std::string Bytes = readFile(Object);
  EXPECT_EQ(Bytes.find("late-plugin-secret"), std::string::npos)
      << "the final automatic-string seal must run after pre-codegen plugins";
  EXPECT_EQ(Bytes.find("__neverc_xorstr_"), std::string::npos)
      << "late plugin literals must still finish with no shared decoder";

  std::vector<std::string> LTOCompileArgs = {"-c",
                                             "-std=c11",
                                             "-O2",
                                             "-flto=full",
                                             "-fno-builtin-mimalloc",
                                             "-fencrypt-call-strings",
                                             "-fstring-encrypt-key=1"};
  for (auto &Flag : sysrootFlags())
    LTOCompileArgs.push_back(Flag);
  for (auto &Flag : archFlags())
    LTOCompileArgs.push_back(Flag);
  LTOCompileArgs.push_back(Source.string());
  LTOCompileArgs.push_back("-o");
  LTOCompileArgs.push_back(LTOObject.string());
  CmdResult LTOCompile = ncc(LTOCompileArgs);
  ASSERT_EQ(LTOCompile.exitCode, 0) << LTOCompile.err;

  writeFile(Consumer, "extern void plugin_tail_target(void);\n"
                      "extern const char *plugin_tail_source(void);\n"
                      "void consume(const char *p) { (void)p; }\n"
                      "int main(void) {\n"
                      "  (void)plugin_tail_source();\n"
                      "  plugin_tail_target();\n"
                      "  return 0;\n"
                      "}\n");
  std::vector<std::string> ConsumerArgs = {"-c", "-std=c11", "-O2", "-fno-lto",
                                           "-fno-builtin-mimalloc"};
  for (auto &Flag : sysrootFlags())
    ConsumerArgs.push_back(Flag);
  for (auto &Flag : archFlags())
    ConsumerArgs.push_back(Flag);
  ConsumerArgs.push_back(Consumer.string());
  ConsumerArgs.push_back("-o");
  ConsumerArgs.push_back(ConsumerObject.string());
  CmdResult ConsumerBuild = ncc(ConsumerArgs);
  ASSERT_EQ(ConsumerBuild.exitCode, 0) << ConsumerBuild.err;

  for (const char *Mode : {"", "-flto=full"}) {
    SCOPED_TRACE(Mode[0] ? Mode : "auto-lto");
    const fs::path Executable =
        tmpFile(Mode[0] ? "xorstr_late_plugin_full_lto"
                        : "xorstr_late_plugin_auto_lto");
    std::vector<std::string> LinkArgs = {
        "-O2",
        "-fno-builtin-mimalloc",
        "-fencrypt-call-strings",
        "-fstring-encrypt-key=1",
        std::string("-fplugin=") + NEVERC_TEST_IR_PASS_LATE_LITERAL_PLUGIN,
    };
    if (Mode[0])
      LinkArgs.push_back(Mode);
    for (auto &Flag : sysrootFlags())
      LinkArgs.push_back(Flag);
    for (auto &Flag : archFlags())
      LinkArgs.push_back(Flag);
    LinkArgs.push_back(LTOObject.string());
    LinkArgs.push_back(ConsumerObject.string());
    LinkArgs.push_back("-o");
    LinkArgs.push_back(Executable.string());
    CmdResult Link = ncc(LinkArgs);
    ASSERT_EQ(Link.exitCode, 0) << Link.err;
    const std::string LinkedBytes = readFile(Executable);
    EXPECT_EQ(LinkedBytes.find("late-plugin-secret"), std::string::npos);
    EXPECT_EQ(LinkedBytes.find("__neverc_xorstr_"), std::string::npos);
    CmdResult Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(XorStrTest, EncryptCallStrings_NativeObjectHasNoSemanticMarkers) {
  auto src = xorStrDir() / "encrypt_call_strings.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto object = tmpFile("encrypt_call_strings_anonymous.o");
  std::vector<std::string> args = {"-c", "-O2", "-fno-lto",
                                   "-fencrypt-call-strings"};
  for (auto &f : sysrootFlags())
    args.push_back(f);
  for (auto &f : archFlags())
    args.push_back(f);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(object.string());

  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << "native object compilation failed\n" << r.err;
  const std::string Bytes = readFile(object);
  EXPECT_EQ(Bytes.find(".xorstr.enc"), std::string::npos)
      << "ciphertext globals must not expose an xorstr marker";
  EXPECT_EQ(Bytes.find("hello auto"), std::string::npos)
      << "machine-code output must not contain plaintext";
}

TEST_F(XorStrTest, EncryptCallStrings_ProviderCannotBypassEncryption) {
  auto src = xorStrDir() / "encrypt_call_strings_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  for (const char *Mode : {"", "-flto=full", "-fno-lto"}) {
    SCOPED_TRACE(Mode[0] ? Mode : "auto-lto");
    const std::string ModeName =
        Mode[0] ? (std::string(Mode) == "-fno-lto" ? "no_lto" : "full_lto")
                : "auto_lto";
    auto Executable = tmpFile("encrypt_call_strings_provider_" + ModeName);
    std::vector<std::string> Args = {
        "-std=c11",
        "-O2",
        "-fencrypt-call-strings",
        "-fstring-encrypt-key=1",
        "-fno-builtin-mimalloc",
        std::string("-fplugin=") +
            NEVERC_TEST_IR_OPTIMIZATION_PASSTHROUGH_PLUGIN,
    };
    if (Mode[0])
      Args.push_back(Mode);
    for (auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.push_back(src.string());
    Args.push_back("-o");
    Args.push_back(Executable.string());

    CmdResult Build = ncc(Args);
    ASSERT_EQ(Build.exitCode, 0) << Build.err;
    const std::string Bytes = readFile(Executable);
    EXPECT_EQ(Bytes.find("hello auto"), std::string::npos)
        << "an IR optimization provider must not bypass automatic string "
           "encryption";

    CmdResult Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(XorStrTest, EncryptCallStrings_RuntimeRoundTrip) {
  auto src = xorStrDir() / "encrypt_call_strings_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  compileRunAndCheck("encrypt_call_strings_runtime", src.string(),
                     "-std=c11 -O1 -fencrypt-call-strings "
                     "-fno-builtin-mimalloc");
}
TEST_F(XorStrTest, EncryptCallStrings_PreservesInteriorStringPointer) {
  auto src = xorStrDir() / "encrypt_call_strings_offset_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  compileRunAndCheck("encrypt_call_strings_offset_runtime", src.string(),
                     "-std=c11 -O1 -fno-lto -fencrypt-call-strings "
                     "-fno-builtin-mimalloc");
}

TEST_F(XorStrTest, EncryptCallStrings_RemovesInteriorPointerPlaintext) {
  auto src = xorStrDir() / "encrypt_call_strings_offset_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto object = tmpFile("encrypt_call_strings_offset.o");
  std::vector<std::string> args = {"-c",
                                   "-std=c11",
                                   "-O1",
                                   "-fno-lto",
                                   "-fencrypt-call-strings",
                                   "-fno-builtin-mimalloc"};
  for (auto &flag : sysrootFlags())
    args.push_back(flag);
  for (auto &flag : archFlags())
    args.push_back(flag);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(object.string());

  auto result = ncc(args);
  ASSERT_EQ(result.exitCode, 0) << result.err;
  EXPECT_EQ(readFile(object).find("prefix"), std::string::npos)
      << "encrypting an interior pointer must remove its backing plaintext";
}

TEST_F(XorStrTest, EncryptCallStrings_HandlesMergedLiteralGlobal) {
  auto src = xorStrDir() / "encrypt_call_strings_shared_global_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  compileRunAndCheck("encrypt_call_strings_shared_global_runtime", src.string(),
                     "-std=c11 -O1 -fno-lto -fencrypt-call-strings "
                     "-fno-builtin-mimalloc");
}

TEST_F(XorStrTest, EncryptCallStrings_ProtectsIndirectCallArgument) {
  auto src = xorStrDir() / "encrypt_call_strings_indirect_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto executable = tmpFile("encrypt_call_strings_indirect_runtime");
  std::vector<std::string> args = {"-std=c11", "-O1", "-fno-lto",
                                   "-fencrypt-call-strings",
                                   "-fno-builtin-mimalloc"};
  for (auto &flag : sysrootFlags())
    args.push_back(flag);
  for (auto &flag : archFlags())
    args.push_back(flag);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(executable.string());

  auto build = ncc(args);
  ASSERT_EQ(build.exitCode, 0) << build.err;
  EXPECT_EQ(readFile(executable).find("indirect-secret"), std::string::npos)
      << "indirect call arguments must not bypass automatic encryption";
  auto run = exec(executable.string(), {});
  EXPECT_EQ(run.exitCode, 0) << run.out << run.err;
}

TEST_F(XorStrTest, EncryptCallStrings_ProtectsSelectArguments) {
  auto src = xorStrDir() / "encrypt_call_strings_select_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto executable = tmpFile("encrypt_call_strings_select_runtime");
  std::vector<std::string> args = {"-std=c11", "-O1", "-fno-lto",
                                   "-fencrypt-call-strings",
                                   "-fno-builtin-mimalloc"};
  for (auto &flag : sysrootFlags())
    args.push_back(flag);
  for (auto &flag : archFlags())
    args.push_back(flag);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(executable.string());

  auto build = ncc(args);
  ASSERT_EQ(build.exitCode, 0) << build.err;
  const std::string bytes = readFile(executable);
  EXPECT_EQ(bytes.find("select-secret-alpha"), std::string::npos);
  EXPECT_EQ(bytes.find("select-secret-omega"), std::string::npos);
  auto run = exec(executable.string(), {});
  EXPECT_EQ(run.exitCode, 0) << run.out << run.err;
}

TEST_F(XorStrTest, EncryptCallStrings_ProtectsO0LocalPointerFlow) {
  auto src = xorStrDir() / "encrypt_call_strings_local_phi_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto executable = tmpFile("encrypt_call_strings_local_phi_runtime");
  std::vector<std::string> args = {"-std=c11", "-O0", "-fno-lto",
                                   "-fencrypt-call-strings",
                                   "-fno-builtin-mimalloc"};
  for (auto &flag : sysrootFlags())
    args.push_back(flag);
  for (auto &flag : archFlags())
    args.push_back(flag);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(executable.string());

  auto build = ncc(args);
  ASSERT_EQ(build.exitCode, 0) << build.err;
  const std::string bytes = readFile(executable);
  EXPECT_EQ(bytes.find("phi-secret-alpha"), std::string::npos);
  EXPECT_EQ(bytes.find("phi-secret-omega"), std::string::npos);
  auto run = exec(executable.string(), {});
  EXPECT_EQ(run.exitCode, 0) << run.out << run.err;
}

TEST_F(XorStrTest, EncryptCallStrings_O0IntermediateKeepsVolatileCleanup) {
  auto src = xorStrDir() / "encrypt_call_strings_local_phi_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  const std::string ir = emitIR(src.string(), "encrypt_call_strings_o0_cleanup",
                                "-std=c11 -O0 -fno-lto -fencrypt-call-strings "
                                "-fno-builtin-mimalloc");
  ASSERT_FALSE(ir.empty());
  EXPECT_NE(ir.find("llvm.memset"), std::string::npos);
  EXPECT_NE(ir.find("i1 true), !neverc.xorstr.cleanup"), std::string::npos)
      << "optnone must not suppress the volatile plaintext wipe";
  EXPECT_NE(ir.find("\"nooutline\""), std::string::npos)
      << "machine outlining must not recreate a shared decrypt helper";
  EXPECT_EQ(ir.find("phi-secret-alpha"), std::string::npos);
  EXPECT_EQ(ir.find("phi-secret-omega"), std::string::npos);
}

TEST_F(XorStrTest, EncryptCallStrings_PreservesPointerIdentityAndOffsets) {
  auto src = xorStrDir() / "encrypt_call_strings_pointer_identity_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto executable = tmpFile("encrypt_call_strings_pointer_identity_runtime");
  std::vector<std::string> args = {"-std=c11", "-O0", "-fno-lto",
                                   "-fencrypt-call-strings",
                                   "-fno-builtin-mimalloc"};
  for (auto &flag : sysrootFlags())
    args.push_back(flag);
  for (auto &flag : archFlags())
    args.push_back(flag);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(executable.string());

  auto build = ncc(args);
  ASSERT_EQ(build.exitCode, 0) << build.err;
  EXPECT_EQ(readFile(executable).find("pointer-identity-secret"),
            std::string::npos);
  auto run = exec(executable.string(), {});
  EXPECT_EQ(run.exitCode, 0) << run.out << run.err;
}

TEST_F(XorStrTest, EncryptCallStrings_PreservesExportedConstantArrays) {
  const fs::path Producer = tmpFile("xorstr_exported_array_producer.c");
  const fs::path Consumer = tmpFile("xorstr_exported_array_consumer.c");
  const fs::path ProducerObject = tmpFile("xorstr_exported_array_producer.o");
  const fs::path Executable = tmpFile("xorstr_exported_array_runtime");

  writeFile(Producer,
            "extern void consume(const char *);\n"
            "const char exported_message[] = \"exported-array-secret\";\n"
            "void producer(void) { consume(exported_message); }\n");
  writeFile(Consumer,
            "extern const char exported_message[];\n"
            "extern void producer(void);\n"
            "static int seen;\n"
            "void consume(const char *p) {\n"
            "  seen = p == exported_message && p[0] == 'e' && p[9] == 'a';\n"
            "}\n"
            "int main(void) {\n"
            "  producer();\n"
            "  return !(seen && exported_message[17] == 'c');\n"
            "}\n");

  std::vector<std::string> CompileArgs = {"-c",
                                          "-std=c11",
                                          "-O2",
                                          "-fno-lto",
                                          "-fencrypt-call-strings",
                                          "-fno-builtin-mimalloc"};
  for (auto &Flag : sysrootFlags())
    CompileArgs.push_back(Flag);
  for (auto &Flag : archFlags())
    CompileArgs.push_back(Flag);
  CompileArgs.push_back(Producer.string());
  CompileArgs.push_back("-o");
  CompileArgs.push_back(ProducerObject.string());
  CmdResult ProducerBuild = ncc(CompileArgs);
  ASSERT_EQ(ProducerBuild.exitCode, 0) << ProducerBuild.err;

  std::vector<std::string> LinkArgs = {"-std=c11", "-O2", "-fno-lto",
                                       "-fno-builtin-mimalloc"};
  for (auto &Flag : sysrootFlags())
    LinkArgs.push_back(Flag);
  for (auto &Flag : archFlags())
    LinkArgs.push_back(Flag);
  LinkArgs.push_back(Consumer.string());
  LinkArgs.push_back(ProducerObject.string());
  LinkArgs.push_back("-o");
  LinkArgs.push_back(Executable.string());
  CmdResult Link = ncc(LinkArgs);
  ASSERT_EQ(Link.exitCode, 0) << Link.err;
  CmdResult Run = exec(Executable.string(), {});
  EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
}

TEST_F(XorStrTest, EncryptCallStrings_ProtectsO0CyclicPhiAndDynamicGEP) {
  auto src = xorStrDir() / "encrypt_call_strings_cfg_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto executable = tmpFile("encrypt_call_strings_cfg_runtime");
  std::vector<std::string> args = {"-std=c11", "-O0", "-fno-lto",
                                   "-fencrypt-call-strings",
                                   "-fno-builtin-mimalloc"};
  for (auto &flag : sysrootFlags())
    args.push_back(flag);
  for (auto &flag : archFlags())
    args.push_back(flag);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(executable.string());

  auto build = ncc(args);
  ASSERT_EQ(build.exitCode, 0) << build.err;
  const std::string bytes = readFile(executable);
  for (const char *plaintext : {"dynamic-alpha", "dynamic-omega",
                                "alpha-loop-secret", "omega-loop-secret"})
    EXPECT_EQ(bytes.find(plaintext), std::string::npos) << plaintext;
  auto run = exec(executable.string(), {});
  EXPECT_EQ(run.exitCode, 0) << run.out << run.err;
}

TEST_F(XorStrTest, EncryptCallStrings_ProtectsArgumentsBeforeIPOPropagation) {
  auto src = xorStrDir() / "encrypt_call_strings_identity_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto executable = tmpFile("encrypt_call_strings_ipo_runtime");
  std::vector<std::string> args = {"-std=c11", "-O1", "-fno-lto",
                                   "-fencrypt-call-strings",
                                   "-fno-builtin-mimalloc"};
  for (auto &flag : sysrootFlags())
    args.push_back(flag);
  for (auto &flag : archFlags())
    args.push_back(flag);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(executable.string());
  auto build = ncc(args);
  ASSERT_EQ(build.exitCode, 0) << build.err;
  EXPECT_EQ(readFile(executable).find("identity-secret"), std::string::npos);
  auto run = exec(executable.string(), {});
  EXPECT_EQ(run.exitCode, 0) << run.out << run.err;
}

TEST_F(XorStrTest, EncryptCallStrings_ProtectsLTOExposedArguments) {
  const auto sourceA = xorStrDir() / "encrypt_call_strings_lto_late_a.c";
  const auto sourceB = xorStrDir() / "encrypt_call_strings_lto_late_b.c";
  const auto consumeSource =
      xorStrDir() / "encrypt_call_strings_lto_late_consume.c";
  if (!fs::exists(sourceA) || !fs::exists(sourceB) ||
      !fs::exists(consumeSource))
    GTEST_SKIP() << "late-LTO xorstr fixtures not found";

  auto consumeObject = tmpFile("encrypt_call_strings_lto_consume.o");
  std::vector<std::string> consumeArgs = {"-c", "-std=c11", "-O2", "-fno-lto",
                                          "-fno-builtin-mimalloc"};
  for (auto &flag : sysrootFlags())
    consumeArgs.push_back(flag);
  for (auto &flag : archFlags())
    consumeArgs.push_back(flag);
  consumeArgs.push_back(consumeSource.string());
  consumeArgs.push_back("-o");
  consumeArgs.push_back(consumeObject.string());
  auto consumeBuild = ncc(consumeArgs);
  ASSERT_EQ(consumeBuild.exitCode, 0) << consumeBuild.err;

  for (const char *mode : {"", "-flto=full"}) {
    SCOPED_TRACE(mode[0] ? mode : "auto-lto");
    auto executable =
        tmpFile(mode[0] ? "encrypt_lto_late_full" : "encrypt_lto_late_auto");
    std::vector<std::string> args = {
        "-std=c11", "-O2", "-fencrypt-call-strings", "-fstring-encrypt-key=1",
        "-fno-builtin-mimalloc"};
    if (mode[0])
      args.push_back(mode);
    for (auto &flag : sysrootFlags())
      args.push_back(flag);
    for (auto &flag : archFlags())
      args.push_back(flag);
    args.push_back(sourceA.string());
    args.push_back(sourceB.string());
    args.push_back(consumeObject.string());
    args.push_back("-o");
    args.push_back(executable.string());

    auto build = ncc(args);
    ASSERT_EQ(build.exitCode, 0) << build.err;
    const std::string bytes = readFile(executable);
    EXPECT_EQ(bytes.find("lto-late-secret-alpha"), std::string::npos);
    auto run = exec(executable.string(), {});
    EXPECT_EQ(run.exitCode, 0) << run.out << run.err;
  }
}
