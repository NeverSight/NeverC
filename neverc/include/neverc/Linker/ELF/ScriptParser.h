#ifndef LINKER_ELF_SCRIPT_PARSER_H
#define LINKER_ELF_SCRIPT_PARSER_H

#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/Support/MemoryBufferRef.h"

namespace linker::elf {

// Parses a linker script. Calling this function updates
// linker::elf::config and linker::elf::script.
void readLinkerScript(MemoryBufferRef mb);

// Parses a version script.
void readVersionScript(MemoryBufferRef mb);

void readDynamicList(MemoryBufferRef mb);

// Parses the defsym expression.
void readDefsym(StringRef name, MemoryBufferRef mb);

bool hasWildcard(StringRef s);

} // namespace linker::elf

#endif
