#ifndef LINKER_ELF_ELFCONTEXTACCESS_H
#define LINKER_ELF_ELFCONTEXTACCESS_H

#include "Linker/Core/Runtime/LinkerParallel.h"
#include "Linker/ELF/Config.h"
#include "Linker/ELF/ELFLinkerContext.h"
#include "Linker/ELF/SymbolTable.h"
#include "Linker/ELF/SyntheticSections.h"

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
// incrementally converted to explicit accessors.  The linker context itself is
// always accessed through elfState(): a generic `ctx` macro leaks into headers
// included later and can rewrite unrelated local variables.
#define symtab elfSymtab()
#define in elfIn()

#endif
