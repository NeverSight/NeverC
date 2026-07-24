#ifndef LINKER_MACHO_MACHOCONTEXTACCESS_H
#define LINKER_MACHO_MACHOCONTEXTACCESS_H

#include "Linker/MachO/ConcatOutputSection.h"
#include "Linker/MachO/Driver.h"
#include "Linker/MachO/Emit.h"
#include "Linker/MachO/InputFiles.h"
#include "Linker/MachO/InputSection.h"
#include "Linker/MachO/MachOLinkerContext.h"
#include "Linker/MachO/OutputSegment.h"
#include "Linker/MachO/SectionPriorities.h"
#include "Linker/MachO/SyntheticSections.h"
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

// Source-only compatibility spellings while Mach-O routines are incrementally
// converted to explicit MachOLinkerContext parameters.  New code calls the
// accessors declared in MachOLinkerContext.h / SyntheticSections.h directly.
#define in machoIn()
#define syntheticSections machoSyntheticSections()
#define firstTLVDataSection machoFirstTLVDataSection()
#define priorityBuilder machoPriorityBuilder()
#define outputSegments machoOutputSegments()
#define inputSections machoInputSections()
#define inputFiles machoInputFiles()
#define cachedReads machoCachedReads()
#define cachedReadsMu machoCachedReadsMutex()
#define unprocessedLCLinkerOptions machoUnprocessedLCLinkerOptions()
#define concatOutputSections machoConcatOutputSections()
#define thunkMap machoThunkMap()
#define resolvedLibraries machoResolvedLibraries()
#define resolvedFrameworks machoResolvedFrameworks()
#define loadedArchives machoLoadedArchives()
#define missingAutolinkWarnings machoMissingAutolinkWarnings()
#define loadedObjectFrameworks machoLoadedObjectFrameworks()
#define loadedDylibs machoLoadedDylibs()

#endif
