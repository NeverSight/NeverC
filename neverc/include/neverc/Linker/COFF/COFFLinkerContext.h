#ifndef LINKER_COFF_COFFLINKERCONTEXT_H
#define LINKER_COFF_COFFLINKERCONTEXT_H

#include "Linker/COFF/Chunks.h"
#include "Linker/COFF/Config.h"
#include "Linker/COFF/Driver.h"
#include "Linker/COFF/Emit.h"
#include "Linker/COFF/InputFiles.h"
#include "Linker/COFF/SymbolTable.h"
#include "Linker/Core/Runtime/Session.h"
#include "Linker/Core/Runtime/Stopwatch.h"
#include "llvm/ADT/DenseSet.h"

namespace linker::coff {

class COFFLinkerContext : public CommonLinkerContext {
public:
  COFFLinkerContext();
  COFFLinkerContext(const COFFLinkerContext &) = delete;
  COFFLinkerContext &operator=(const COFFLinkerContext &) = delete;
  ~COFFLinkerContext() = default;

  LinkerDriver driver;
  SymbolTable symtab;
  COFFOptTable optTable;

  std::vector<ObjFile *> objFileInstances;
  std::vector<ImportFile *> importFileInstances;
  std::vector<BitcodeFile *> bitcodeFileInstances;

  MergeChunk *mergeChunkInstances[Log2MaxSectionAlignment + 1] = {};

  /// List of all output sections. After output sections are finalized, this
  /// can be indexed by getOutputSection.
  std::vector<OutputSection *> outputSections;

  OutputSection *getOutputSection(const Chunk *c) const {
    return c->osidx == 0 ? nullptr : outputSections[c->osidx - 1];
  }

  // Fake sections for parsing bitcode files.
  FakeSection ltoTextSection;
  FakeSection ltoDataSection;
  FakeSectionChunk ltoTextSectionChunk;
  FakeSectionChunk ltoDataSectionChunk;

  // All timers used in the COFF linker.
  Timer rootTimer;
  Timer inputFileTimer;
  Timer ltoTimer;
  Timer gcTimer;
  Timer icfTimer;

  // Image emission timers.
  Timer codeLayoutTimer;
  Timer outputCommitTimer;
  Timer totalMapTimer;
  Timer symbolGatherTimer;
  Timer symbolStringsTimer;
  Timer writeTimer;

  Configuration config;

  // Symbols marked with __attribute__((override)) or --override=<sym>.
  // Maps symbol name → originating InputFile (nullptr for /override: flag).
  llvm::DenseMap<llvm::StringRef, const InputFile *> overrideSymbols;
};

} // namespace linker::coff

#endif
