#ifndef LINKER_ELF_EMIT_H
#define LINKER_ELF_EMIT_H

#include "Linker/ELF/Config.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace linker::elf {
class InputFile;
class OutputSection;
void copySectionsIntoPartitions();
template <class ELFT> void createSyntheticSections();
template <class ELFT> void writeOutput();

// This describes a program header entry.
// Each contains type, access flags and range of output sections that will be
// placed in it.
struct PhdrEntry {
  PhdrEntry(unsigned type, unsigned flags)
      : p_align(type == llvm::ELF::PT_LOAD ? config->maxPageSize : 0),
        p_type(type), p_flags(flags) {}
  void add(OutputSection *sec);

  uint64_t p_paddr = 0;
  uint64_t p_vaddr = 0;
  uint64_t p_memsz = 0;
  uint64_t p_filesz = 0;
  uint64_t p_offset = 0;
  uint32_t p_align = 0;
  uint32_t p_type = 0;
  uint32_t p_flags = 0;

  OutputSection *firstSec = nullptr;
  OutputSection *lastSec = nullptr;
  bool hasLMA = false;

  uint64_t lmaOffset = 0;
};

void addReservedSymbols();
bool includeInSymtab(const Symbol &b);

bool canHaveMemtagGlobals();
} // namespace linker::elf

#endif
