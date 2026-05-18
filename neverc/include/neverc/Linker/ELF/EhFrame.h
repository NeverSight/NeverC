#ifndef LINKER_ELF_EHFRAME_H
#define LINKER_ELF_EHFRAME_H

#include "Linker/Core/Support/LlvmAliases.h"

namespace linker::elf {
struct EhSectionPiece;

uint8_t getFdeEncoding(EhSectionPiece *p);
bool hasLSDA(const EhSectionPiece &p);
} // namespace linker::elf

#endif
