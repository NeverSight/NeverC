#ifndef LINKER_ELF_ELFCONTEXTACCESS_H
#define LINKER_ELF_ELFCONTEXTACCESS_H

#include "Linker/ELF/Config.h"
#include "Linker/ELF/ELFLinkerContext.h"
#include "Linker/ELF/SymbolTable.h"
#include "Linker/ELF/SyntheticSections.h"
#include "Linker/Core/Runtime/LinkerParallel.h"

// Source-only compatibility spellings while upstream-style ELF routines are
// incrementally converted to explicit ELFLinkerContext parameters.
#define ctx elfState()
#define symtab elfSymtab()
#define in elfIn()

#endif
