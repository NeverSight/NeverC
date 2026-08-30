#include "Driver/Parallelism.h"
#include "Linker/MachO/MachOLinkerContext.h"
#include "Linker/MachO/Config.h"
#include "Linker/MachO/MachOContextAccess.h"
#include "Linker/MachO/SymbolTable.h"
#include "Linker/MachO/Target.h"
#include "MachO/MachOLinkGraphAdapter.h"

namespace linker::macho {

struct MachOLinkerContext::Impl {
  std::unique_ptr<Configuration> Config;
  std::unique_ptr<DependencyTracker> DependencyTrackerState;
  std::unique_ptr<SymbolTable> Symbols;
  TargetInfo *Target = nullptr;
  InStruct SyntheticInputs;
  std::vector<SyntheticSection *> SyntheticSectionList;
  OutputSection *FirstTLVDataSection = nullptr;
  PriorityBuilder Priorities;
  std::vector<OutputSegment *> OutputSegmentList;
  std::vector<ConcatInputSection *> InputSectionList;
  llvm::SetVector<InputFile *> InputFileList;
  llvm::DenseMap<llvm::CachedHashStringRef, MemoryBufferRef> CachedReadMap;
  std::mutex CachedReadsMutex;
  llvm::SmallVector<StringRef> UnprocessedLCLinkerOptions;
  llvm::MapVector<NamePair, ConcatOutputSection *> ConcatOutputSectionMap;
  llvm::DenseMap<Symbol *, ThunkInfo> Thunks;
  llvm::DenseMap<llvm::CachedHashStringRef, StringRef> ResolvedLibraries;
  llvm::DenseMap<llvm::CachedHashStringRef, StringRef> ResolvedFrameworks;
  llvm::DenseMap<StringRef, ArchiveFileInfo> LoadedArchives;
  std::vector<StringRef> MissingAutolinkWarnings;
  llvm::DenseSet<StringRef> LoadedObjectFrameworks;
  llvm::DenseMap<llvm::CachedHashStringRef, DylibFile *> LoadedDylibs;
  std::unique_ptr<MachOLinkGraphAdapter> PluginLinkAdapter;
  detail::IncrementalInputWorkload AdaptiveInputWorkload;
  int NextInputFileId = 0;
  uint32_t LCDylibCount = 0;
};

MachOLinkerContext::MachOLinkerContext()
    : State(std::make_unique<Impl>()) {}
MachOLinkerContext::~MachOLinkerContext() { finalizeOwnedState(); }

MachOLinkerContext &machoContext() {
  return static_cast<MachOLinkerContext &>(commonContext());
}

std::unique_ptr<Configuration> &machoConfig() {
  return machoContext().state().Config;
}
std::unique_ptr<DependencyTracker> &machoDependencyTracker() {
  return machoContext().state().DependencyTrackerState;
}
std::unique_ptr<SymbolTable> &machoSymtab() {
  return machoContext().state().Symbols;
}
TargetInfo *&machoTarget() { return machoContext().state().Target; }
InStruct &machoIn() { return machoContext().state().SyntheticInputs; }
std::vector<SyntheticSection *> &machoSyntheticSections() {
  return machoContext().state().SyntheticSectionList;
}
OutputSection *&machoFirstTLVDataSection() {
  return machoContext().state().FirstTLVDataSection;
}
PriorityBuilder &machoPriorityBuilder() {
  return machoContext().state().Priorities;
}
std::vector<OutputSegment *> &machoOutputSegments() {
  return machoContext().state().OutputSegmentList;
}
std::vector<ConcatInputSection *> &machoInputSections() {
  return machoContext().state().InputSectionList;
}
llvm::SetVector<InputFile *> &machoInputFiles() {
  return machoContext().state().InputFileList;
}
llvm::DenseMap<llvm::CachedHashStringRef, MemoryBufferRef> &
machoCachedReads() {
  return machoContext().state().CachedReadMap;
}
llvm::SmallVector<StringRef> &machoUnprocessedLCLinkerOptions() {
  return machoContext().state().UnprocessedLCLinkerOptions;
}
llvm::MapVector<NamePair, ConcatOutputSection *> &
machoConcatOutputSections() {
  return machoContext().state().ConcatOutputSectionMap;
}
llvm::DenseMap<Symbol *, ThunkInfo> &machoThunkMap() {
  return machoContext().state().Thunks;
}
llvm::DenseMap<llvm::CachedHashStringRef, StringRef> &
machoResolvedLibraries() {
  return machoContext().state().ResolvedLibraries;
}
llvm::DenseMap<llvm::CachedHashStringRef, StringRef> &
machoResolvedFrameworks() {
  return machoContext().state().ResolvedFrameworks;
}
llvm::DenseMap<StringRef, ArchiveFileInfo> &machoLoadedArchives() {
  return machoContext().state().LoadedArchives;
}
std::vector<StringRef> &machoMissingAutolinkWarnings() {
  return machoContext().state().MissingAutolinkWarnings;
}
llvm::DenseSet<StringRef> &machoLoadedObjectFrameworks() {
  return machoContext().state().LoadedObjectFrameworks;
}
llvm::DenseMap<llvm::CachedHashStringRef, DylibFile *> &
machoLoadedDylibs() {
  return machoContext().state().LoadedDylibs;
}
std::mutex &machoCachedReadsMutex() {
  return machoContext().state().CachedReadsMutex;
}
int &machoInputFileIdCount() {
  return machoContext().state().NextInputFileId;
}
uint32_t &machoLCDylibCount() {
  return machoContext().state().LCDylibCount;
}
std::unique_ptr<MachOLinkGraphAdapter> &machoPluginLinkAdapter() {
  return machoContext().state().PluginLinkAdapter;
}

detail::IncrementalInputWorkload &detail::incrementalInputWorkload() {
  return machoContext().state().AdaptiveInputWorkload;
}

} // namespace linker::macho
