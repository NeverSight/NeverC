#ifndef LINKER_MACHO_MACHOLINKERCONTEXT_H
#define LINKER_MACHO_MACHOLINKERCONTEXT_H

#include "Linker/Core/Runtime/Session.h"
#include <memory>

namespace linker::macho {

class MachOLinkerContext final : public CommonLinkerContext {
public:
  struct Impl;

  MachOLinkerContext();
  MachOLinkerContext(const MachOLinkerContext &) = delete;
  MachOLinkerContext &operator=(const MachOLinkerContext &) = delete;
  ~MachOLinkerContext() override;

  Impl &state() { return *State; }

private:
  std::unique_ptr<Impl> State;
};

MachOLinkerContext &machoContext();

} // namespace linker::macho

#endif
