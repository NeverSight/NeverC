#ifndef LINKER_ELF_LTO_H
#define LINKER_ELF_LTO_H

#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/LTO/LTO.h"
#include <memory>
#include <vector>

namespace linker::elf {

class BitcodeFile;
class InputFile;

class BitcodeCompiler {
public:
  BitcodeCompiler();
  ~BitcodeCompiler();

  void add(BitcodeFile &f);
  void addBatch(llvm::ArrayRef<BitcodeFile *> files);
  std::vector<InputFile *> compile();

private:
  // Build per-symbol LTO resolutions. Thread-safe (read-only inspection).
  std::vector<llvm::lto::SymbolResolution> prepare(BitcodeFile &f) const;
  // Commit the resolutions: demote prevailing definitions to Undefined and
  // hand the input over to lto::LTO. Must run on the main thread because
  // lto::LTO::add mutates shared LTO state.
  void commit(BitcodeFile &f, std::vector<llvm::lto::SymbolResolution> resols);

  std::unique_ptr<llvm::lto::LTO> ltoObj;
  std::vector<SmallString<0>> buf;
  llvm::DenseSet<StringRef> usedStartStop;
};
} // namespace linker::elf

#endif
