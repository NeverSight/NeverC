//===- FullLLVMBackend.cpp - Full LLVM machine output ---------------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "FullLLVMBackend.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

#include <string>
#include <vector>

using namespace llvm;

Expected<SmallVector<char, 0>>
neverc::emitWithFullLLVM(Module &M, BackendAction Action,
                         const CodeGenOptions &Options,
                         const TargetOptions &Target) {
  const Triple TT(M.getTargetTriple());
  if (TT.getArch() != Triple::x86 ||
      !plugin::findBuiltinTargetRoute(TT.str()) ||
      M.getDataLayout().getPointerSizeInBits() != 32)
    return createStringError(inconvertibleErrorCode(),
                             "full LLVM backend requires supported i386 IR");
  if (Action != Backend_EmitObj && Action != Backend_EmitAssembly)
    return createStringError(
        inconvertibleErrorCode(),
        "full LLVM backend requires object or assembly output");
  if (Options.PrepareForLTO || !Options.SplitDwarfFile.empty() ||
      !Options.SplitDwarfOutput.empty())
    return createStringError(
        inconvertibleErrorCode(),
        "full LLVM backend does not accept LTO or split DWARF");

  auto Program = sys::findProgramByName(Options.FullLLVMBackend);
  if (!Program)
    return createStringError(Program.getError(),
                             "cannot find full LLVM backend '%s'",
                             Options.FullLLVMBackend.c_str());
  const std::string Executable = Program->str().str();

  SmallString<256> Directory;
  if (auto EC = sys::fs::createUniqueDirectory("neverc-full-llvm", Directory))
    return createStringError(EC, "cannot create full LLVM backend workspace");
  auto Cleanup =
      make_scope_exit([&] { sys::fs::remove_directories(Directory); });
  auto Path = [&](StringRef Name) {
    SmallString<256> Result(Directory);
    sys::path::append(Result, Name);
    return Result.str().str();
  };
  const std::string Input = Path("input.ll"), Output = Path("output"),
                    Stdout = Path("stdout"), Stderr = Path("stderr");

  auto Run = [&](ArrayRef<std::string> Arguments) -> Error {
    SmallVector<StringRef, 16> Args;
    for (const auto &Argument : Arguments)
      Args.push_back(Argument);
    SmallString<256> ExecutionError;
    const StringRef Redirects[] = {"", Stdout, Stderr};
    const int Status = sys::ExecuteAndWait(*Program, Args, {}, Redirects, 0, 0,
                                           &ExecutionError);
    if (Status == 0)
      return Error::success();
    auto Log = MemoryBuffer::getFile(Stderr);
    return createStringError(
        inconvertibleErrorCode(), "full LLVM backend failed (%d): %s%s", Status,
        ExecutionError.c_str(), Log ? (*Log)->getBuffer().str().c_str() : "");
  };
  if (auto E = Run({Executable, "--neversight-codegen-protocol"}))
    return std::move(E);
  auto Protocol = MemoryBuffer::getFile(Stdout);
  if (!Protocol)
    return createStringError(Protocol.getError(),
                             "cannot read full LLVM protocol");
  if ((*Protocol)->getBuffer().trim() != "neversight-codegen-v1 x86")
    return createStringError(inconvertibleErrorCode(),
                             "incompatible full LLVM backend protocol");

  std::error_code EC;
  raw_fd_ostream IR(Input, EC);
  if (EC)
    return createStringError(EC, "cannot create full LLVM input");
  M.print(IR, nullptr);
  IR.close();
  if (IR.has_error()) {
    EC = IR.error();
    IR.clear_error();
    return createStringError(EC, "cannot write full LLVM input");
  }

  unsigned PICLevel = 0;
  if (auto *Flag = M.getModuleFlag("PIC Level")) {
    const auto *Value = mdconst::dyn_extract<ConstantInt>(Flag);
    if (!Value || Value->getValue().ugt(2))
      return createStringError(inconvertibleErrorCode(), "invalid PIC Level");
    PICLevel = Value->getZExtValue();
  }
  std::vector<std::string> Args = {
      Executable,
      "--neversight-codegen",
      "-verify-machineinstrs",
      "-mtriple=" + M.getTargetTriple(),
      "-O" + std::to_string(Options.OptimizationLevel),
      Action == Backend_EmitObj ? "-filetype=obj" : "-filetype=asm",
      PICLevel ? "-relocation-model=pic" : "-relocation-model=static",
      Options.AsmVerbose ? "-asm-verbose=true" : "-asm-verbose=false",
      Options.getInlineAsmDialect() == CodeGenOptions::IAD_Intel
          ? "-x86-asm-syntax=intel"
          : "-x86-asm-syntax=att",
      Input,
      "-o",
      Output};
  if (!Target.CPU.empty())
    Args.push_back("-mcpu=" + Target.CPU);
  if (!Target.Features.empty())
    Args.push_back("-mattr=" + join(Target.Features, ","));
  if (!Options.CodeModel.empty() && Options.CodeModel != "default")
    Args.push_back("-code-model=" + Options.CodeModel);
  if (Options.FunctionSections)
    Args.push_back("-function-sections");
  if (Options.DataSections)
    Args.push_back("-data-sections");
  if (Options.Addrsig)
    Args.push_back("-addrsig");
  if (auto E = Run(Args))
    return std::move(E);
  auto Result = MemoryBuffer::getFile(Output);
  if (!Result)
    return createStringError(Result.getError(), "full LLVM produced no output");
  if (Action == Backend_EmitObj) {
    auto Object =
        object::ObjectFile::createObjectFile((*Result)->getMemBufferRef());
    if (!Object)
      return Object.takeError();
    const auto &Obj = **Object;
    const bool FormatMatches = (TT.isOSBinFormatELF() && Obj.isELF()) ||
                               (TT.isOSBinFormatCOFF() && Obj.isCOFF()) ||
                               (TT.isOSBinFormatMachO() && Obj.isMachO());
    if (!FormatMatches || Obj.getArch() != Triple::x86 ||
        !Obj.isLittleEndian() || Obj.getBytesInAddress() != 4 ||
        !Obj.isRelocatableObject())
      return createStringError(
          inconvertibleErrorCode(),
          "full LLVM output is not an i386 relocatable object for '%s'",
          TT.str().c_str());
  }
  return SmallVector<char, 0>((*Result)->getBufferStart(),
                              (*Result)->getBufferEnd());
}
