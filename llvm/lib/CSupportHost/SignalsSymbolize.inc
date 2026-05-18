//===- SignalsSymbolize.inc - Symbolized stack trace helpers -*- C++ -*-===//
//
// Included from Unix/Signals.inc and Windows/Signals.inc after FileSystem and
// related headers. Kept out of Signals.h to avoid including FileSystem.h while
// FileSystem.h is still being parsed (recursive include leaves namespace fs empty).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CSUPPORT_INTERNAL_SIGNALS_SYMBOLIZE_INC
#define LLVM_LIB_CSUPPORT_INTERNAL_SIGNALS_SYMBOLIZE_INC

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

static bool findModulesAndOffsets(void **StackTrace, int Depth,
                                  const char **Modules, intptr_t *Offsets,
                                  const char *MainExecutableName,
                                  StringSaver &StrPool);

inline static FormattedNumber format_ptr(void *PC) {
  unsigned PtrWidth = 2 + 2 * sizeof(void *);
  return format_hex((uint64_t)PC, PtrWidth);
}

inline LLVM_ATTRIBUTE_USED
static bool printSymbolizedStackTrace(StringRef Argv0, void **StackTrace,
                                      int Depth, llvm::raw_ostream &OS) {
  if (DisableSymbolicationFlag || getenv(DisableSymbolizationEnv))
    return false;

  if (Argv0.contains("llvm-symbolizer"))
    return false;

  ErrorOr<SmallString<256>> LLVMSymbolizerPathOrErr{std::error_code()};
  if (const char *Path = getenv(LLVMSymbolizerPathEnv)) {
    LLVMSymbolizerPathOrErr = sys::findProgramByName(Path);
  } else if (!Argv0.empty()) {
    StringRef Parent = llvm::sys::path::parent_path(Argv0);
    if (!Parent.empty())
      LLVMSymbolizerPathOrErr =
          sys::findProgramByName("llvm-symbolizer", Parent);
  }
  if (!LLVMSymbolizerPathOrErr)
    LLVMSymbolizerPathOrErr = sys::findProgramByName("llvm-symbolizer");
  if (!LLVMSymbolizerPathOrErr)
    return false;
  const auto &LLVMSymbolizerPath = *LLVMSymbolizerPathOrErr;

  SmallString<256> MainExecutableName(
      sys::fs::exists(Argv0) ? StringRef(Argv0)
                             : StringRef(sys::fs::getMainExecutable(0, 0)));
  BumpPtrAllocator Allocator;
  StringSaver StrPool(Allocator);
  SmallVector<const char *, 64> Modules(Depth, 0);
  SmallVector<intptr_t, 64> Offsets(Depth, 0);
  if (!findModulesAndOffsets(StackTrace, Depth, Modules.data(), Offsets.data(),
                             MainExecutableName.c_str(), StrPool))
    return false;
  int InputFD;
  SmallString<32> InputFile, OutputFile;
  sys::fs::createTemporaryFile("symbolizer-input", "", InputFD, InputFile);
  sys::fs::createTemporaryFile("symbolizer-output", "", OutputFile);
  FileRemover InputRemover(InputFile.c_str());
  FileRemover OutputRemover(OutputFile.c_str());

  {
    raw_fd_ostream Input(InputFD, true);
    for (int i = 0; i < Depth; i++) {
      if (Modules[i])
        Input << Modules[i] << " " << (void *)Offsets[i] << "\n";
    }
  }

  StringRef Redirects[] = {InputFile.str(), OutputFile.str(),
                           StringRef("")};
  StringRef Args[] = {"llvm-symbolizer", "--functions=linkage", "--inlining",
#ifdef _WIN32
                      "--relative-address",
#endif
                      "--demangle"};
  int RunResult =
      sys::ExecuteAndWait(LLVMSymbolizerPath, Args, {}, Redirects);
  if (RunResult != 0)
    return false;

  auto OutputBuf = MemoryBuffer::getFile(OutputFile.c_str());
  if (!OutputBuf)
    return false;
  StringRef Output = OutputBuf.get()->getBuffer();
  SmallVector<StringRef, 32> Lines;
  Output.split(Lines, "\n");
  auto CurLine = Lines.begin();
  int frame_no = 0;
  for (int i = 0; i < Depth; i++) {
    auto PrintLineHeader = [&]() {
      OS << right_justify(formatv("#{0}", frame_no++).str(),
                          log10(Depth) + 2)
         << ' ' << format_ptr(StackTrace[i]) << ' ';
    };
    if (!Modules[i]) {
      PrintLineHeader();
      OS << '\n';
      continue;
    }
    for (;;) {
      if (CurLine == Lines.end())
        return false;
      StringRef FunctionName = *CurLine++;
      if (FunctionName.empty())
        break;
      PrintLineHeader();
      if (!FunctionName.starts_with("??"))
        OS << FunctionName << ' ';
      if (CurLine == Lines.end())
        return false;
      StringRef FileLineInfo = *CurLine++;
      if (!FileLineInfo.starts_with("??"))
        OS << FileLineInfo;
      else
        OS << "(" << Modules[i] << '+' << format_hex(Offsets[i], 0) << ")";
      OS << "\n";
    }
  }
  return true;
}

static bool printMarkupContext(raw_ostream &OS, const char *MainExecutableName);

inline LLVM_ATTRIBUTE_USED
static bool printMarkupStackTrace(StringRef Argv0, void **StackTrace, int Depth,
                                  raw_ostream &OS) {
  const char *Env = getenv(EnableSymbolizerMarkupEnv);
  if (!Env || !*Env)
    return false;

  SmallString<256> MainExecutableName2(
      sys::fs::exists(Argv0) ? StringRef(Argv0)
                             : StringRef(sys::fs::getMainExecutable(0, 0)));
  if (!printMarkupContext(OS, MainExecutableName2.c_str()))
    return false;
  for (int I = 0; I < Depth; I++)
    OS << format("{{{bt:%d:%#016x}}}\n", I, StackTrace[I]);
  return true;
}

} // namespace llvm

#endif
