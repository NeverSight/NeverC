#pragma once

#include "../NeverCTestFixture.h"

#include <utility>

class X86NativeBuiltinTest : public NeverCTest {
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
    if (r.exitCode != 0)
      return {r.exitCode, r.err};
    return {0, readFile(outFile)};
  }
};
