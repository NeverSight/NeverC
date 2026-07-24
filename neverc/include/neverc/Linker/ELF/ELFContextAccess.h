#ifndef LINKER_ELF_ELFCONTEXTACCESS_H
#define LINKER_ELF_ELFCONTEXTACCESS_H

#include "Linker/ELF/Config.h"
#include "Linker/ELF/ELFLinkerContext.h"
#include "Linker/ELF/SymbolTable.h"
#include "Linker/ELF/SyntheticSections.h"
#include "Linker/Core/Runtime/LinkerParallel.h"

// `in` below rewrites an identifier the standard library also uses
// (std::ios_base::in), so every standard header that spells it must be parsed
// before the macro exists.  Pulling the stream headers in here keeps the
// compat layer from depending on include order in its consumers.
#include <fstream>
#include <iomanip>
#include <ios>
#include <istream>
#include <ostream>
#include <sstream>
#include <streambuf>

// Source-only compatibility spellings while upstream-style ELF routines are
// incrementally converted to explicit ELFLinkerContext parameters.
#define ctx elfState()
#define symtab elfSymtab()
#define in elfIn()

#endif
