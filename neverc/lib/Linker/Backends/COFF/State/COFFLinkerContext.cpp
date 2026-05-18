#include "Linker/COFF/COFFLinkerContext.h"
#include "Linker/COFF/Symbols.h"
#include "Linker/Core/Runtime/Allocator.h"
#include "llvm/BinaryFormat/COFF.h"
namespace linker::coff {
COFFLinkerContext::COFFLinkerContext()
    : driver(*this), symtab(*this),
      ltoTextSection(llvm::COFF::IMAGE_SCN_MEM_EXECUTE),
      ltoDataSection(llvm::COFF::IMAGE_SCN_CNT_INITIALIZED_DATA),
      ltoTextSectionChunk(&ltoTextSection.section),
      ltoDataSectionChunk(&ltoDataSection.section),
      rootTimer("Total Linking Time"),
      inputFileTimer("Input File Reading", rootTimer),
      ltoTimer("LTO", rootTimer), gcTimer("GC", rootTimer),
      icfTimer("ICF", rootTimer), codeLayoutTimer("Code Layout", rootTimer),
      outputCommitTimer("Commit Output File", rootTimer),
      totalMapTimer("MAP Emission (Cumulative)", rootTimer),
      symbolGatherTimer("Gather Symbols", totalMapTimer),
      symbolStringsTimer("Build Symbol Strings", totalMapTimer),
      writeTimer("Write to File", totalMapTimer) {}
} // namespace linker::coff
