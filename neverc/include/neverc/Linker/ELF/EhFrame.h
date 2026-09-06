#ifndef LINKER_ELF_EHFRAME_H
#define LINKER_ELF_EHFRAME_H

#include "Linker/Core/Support/LlvmAliases.h"

namespace linker::elf {
struct EhSectionPiece;

uint8_t getFdeEncoding(EhSectionPiece *p);
bool hasLSDAOrPersonality(const EhSectionPiece &p);
bool hasZeroPcRange(const EhSectionPiece &p, uint8_t enc);
} // namespace linker::elf

#endif
