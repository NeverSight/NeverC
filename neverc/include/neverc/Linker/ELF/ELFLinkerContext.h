#ifndef LINKER_ELF_ELFLINKERCONTEXT_H
#define LINKER_ELF_ELFLINKERCONTEXT_H

#include "Linker/Core/Runtime/Session.h"
#include <cstdint>
#include <memory>

namespace linker::elf {

class ELFLinkGraphAdapter;

class ELFLinkerContext final : public CommonLinkerContext {
public:
  struct Impl;

  ELFLinkerContext();
  ELFLinkerContext(const ELFLinkerContext &) = delete;
  ELFLinkerContext &operator=(const ELFLinkerContext &) = delete;
  ~ELFLinkerContext() override;
  Impl &state() { return *State; }

private:
  std::unique_ptr<Impl> State;
};

ELFLinkerContext &elfContext();
std::unique_ptr<ELFLinkGraphAdapter> &elfPluginLinkAdapter();
bool &elfInputFileIsInGroup();
uint32_t &elfNextGroupId();
unsigned &elfVernauxNum();

} // namespace linker::elf

#endif
