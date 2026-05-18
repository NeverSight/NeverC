#ifndef LINKER_MACHO_ICF_H
#define LINKER_MACHO_ICF_H

#include "Linker/Core/Support/LlvmAliases.h"
#include "Linker/MachO/InputFiles.h"
#include <vector>

namespace linker::macho {
class Symbol;

void markAddrSigSymbols();
void markSymAsAddrSig(Symbol *s);
void foldIdenticalSections(bool onlyCfStrings);

} // namespace linker::macho

#endif
