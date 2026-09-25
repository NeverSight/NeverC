#ifndef LINKER_ELF_ELFHOTSTATE_H
#define LINKER_ELF_ELFHOTSTATE_H

#include "Linker/Core/Runtime/Session.h"

namespace linker::elf {

// The per-link state the ELF backend reads most often. ELFLinkerContext
// caches its address in CommonLinkerContext::backendHotState so that the
// accessors (elfConfig(), elfSymtab(), ...) are inline loads.
enum ElfHotState : unsigned {
  HotConfig,
  HotSymtab,
  HotTarget,
  HotBackendState,
  HotDiscardedSection,
  HotSyntheticInputs,
};

template <typename T> inline T &elfHotState(ElfHotState slot) {
  return *static_cast<T *>(commonContext().backendHotState[slot]);
}

} // namespace linker::elf

#endif
