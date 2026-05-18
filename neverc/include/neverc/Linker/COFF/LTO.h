#ifndef LINKER_COFF_LTO_H
#define LINKER_COFF_LTO_H

#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/ADT/SmallString.h"
#include <memory>
#include <vector>

namespace llvm::lto {
struct Config;
class LTO;
} // namespace llvm::lto

namespace linker::coff {

class BitcodeFile;
class InputFile;
class COFFLinkerContext;

class BitcodeCompiler {
public:
  BitcodeCompiler(COFFLinkerContext &ctx);
  ~BitcodeCompiler();

  void add(BitcodeFile &f);
  std::vector<InputFile *> compile();

public:
  std::unique_ptr<llvm::lto::LTO> ltoObj;
  std::vector<SmallString<0>> buf;

  llvm::lto::Config createConfig();

  COFFLinkerContext &ctx;
};
} // namespace linker::coff

#endif
