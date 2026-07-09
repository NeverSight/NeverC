//===- MergerDispatch.cpp - Format dispatch for object mergers -===//
//
// Thin façade that routes `mergeObjects` to the right per-format
// implementation.  Kept separate so consumers can include `Merger.h`
// without pulling the full LLVM object headers into their compile unit.
//
//===---------------------------------------------------------------===//

#include "neverc/Merge/Merger.h"

#include "llvm/ADT/ArrayRef.h"

namespace neverc::merge {

bool mergeObjects(llvm::ArrayRef<llvm::SmallVector<char, 0>> Buffers,
                  llvm::raw_pwrite_stream &OS, Format Fmt,
                  const Options &Opts) {
  switch (Fmt) {
  case Format::ELF64LE:
    return mergeELF64LEObjects(Buffers, OS, Opts);
  case Format::MachO64:
    return mergeMachO64Objects(Buffers, OS, Opts);
  case Format::COFF:
    return mergeCOFFObjects(Buffers, OS, Opts);
  }
  return false;
}

bool mergeObjects(llvm::ArrayRef<llvm::StringRef> Buffers,
                  llvm::raw_pwrite_stream &OS, Format Fmt,
                  const Options &Opts) {
  switch (Fmt) {
  case Format::ELF64LE:
    return mergeELF64LEObjects(Buffers, OS, Opts);
  case Format::MachO64:
    return mergeMachO64Objects(Buffers, OS, Opts);
  case Format::COFF:
    return mergeCOFFObjects(Buffers, OS, Opts);
  }
  return false;
}

} // namespace neverc::merge
