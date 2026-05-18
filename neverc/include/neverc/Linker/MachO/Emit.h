#ifndef LINKER_MACHO_EMIT_H
#define LINKER_MACHO_EMIT_H

#include <cstdint>

namespace linker::macho {

class OutputSection;
class InputSection;
class Symbol;

class LoadCommand {
public:
  virtual ~LoadCommand() = default;
  virtual uint32_t getSize() const = 0;
  virtual void writeTo(uint8_t *buf) const = 0;
};

template <class LP> void writeOutput();
void resetEmitState();

void createSyntheticSections();

// Add bindings for symbols that need weak or non-lazy bindings.
void addNonLazyBindingEntries(const Symbol *, const InputSection *,
                              uint64_t offset, int64_t addend = 0);

extern OutputSection *firstTLVDataSection;

} // namespace linker::macho

#endif
