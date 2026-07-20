#ifndef LINKER_MACHO_MACHOCONTEXTACCESS_H
#define LINKER_MACHO_MACHOCONTEXTACCESS_H

#include "Linker/MachO/ConcatOutputSection.h"
#include "Linker/MachO/Driver.h"
#include "Linker/MachO/Emit.h"
#include "Linker/MachO/InputFiles.h"
#include "Linker/MachO/InputSection.h"
#include "Linker/MachO/OutputSegment.h"
#include "Linker/MachO/SectionPriorities.h"
#include "Linker/MachO/SyntheticSections.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include <mutex>

namespace linker::macho {

struct ArchiveFileInfo {
  ArchiveFile *file = nullptr;
  bool isCommandLineLoad = false;
};

llvm::DenseMap<llvm::CachedHashStringRef, StringRef> &
machoResolvedLibraries();
llvm::DenseMap<llvm::CachedHashStringRef, StringRef> &
machoResolvedFrameworks();
llvm::DenseMap<StringRef, ArchiveFileInfo> &machoLoadedArchives();
std::vector<StringRef> &machoMissingAutolinkWarnings();
llvm::DenseSet<StringRef> &machoLoadedObjectFrameworks();
llvm::DenseMap<llvm::CachedHashStringRef, DylibFile *> &
machoLoadedDylibs();
std::mutex &machoCachedReadsMutex();
uint32_t &machoLCDylibCount();

} // namespace linker::macho

// Source-only compatibility spellings while Mach-O routines are incrementally
// converted to explicit MachOLinkerContext parameters.
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
