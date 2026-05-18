#ifndef LINKER_MACHO_LTO_H
#define LINKER_MACHO_LTO_H

#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <vector>

namespace linker::macho {

class BitcodeFile;
class ObjFile;

class BitcodeCompiler {
public:
  BitcodeCompiler();

  // Compute LTO resolutions for `f` without mutating linker state.
  // Safe to call from multiple threads on different files.
  std::vector<llvm::lto::SymbolResolution> prepare(BitcodeFile &f) const;

  // Commit the precomputed resolutions: replaces prevailing symbols with
  // undefineds and hands the bitcode to the LTO engine. Must be serial.
  void commit(BitcodeFile &f, std::vector<llvm::lto::SymbolResolution> resols);

  void add(BitcodeFile &f) { commit(f, prepare(f)); }

  // Parallel batch entry: prepare() runs over all files concurrently,
  // commit() is then called serially in input order.
  void addBatch(llvm::ArrayRef<BitcodeFile *> files);

  std::vector<ObjFile *> compile();

private:
  std::unique_ptr<llvm::lto::LTO> ltoObj;
  std::vector<llvm::SmallString<0>> buf;
  bool hasFiles = false;
};

} // namespace linker::macho

#endif
