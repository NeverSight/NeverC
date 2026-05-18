#include "Linker/MachO/OutputSection.h"
#include "Linker/MachO/OutputSegment.h"

using namespace llvm;
using namespace linker;
using namespace linker::macho;

uint64_t OutputSection::getSegmentOffset() const { return addr - parent->addr; }

void OutputSection::assignAddressesToStartEndSymbols() {
  for (Defined *d : sectionStartSymbols)
    d->value = addr;
  for (Defined *d : sectionEndSymbols)
    d->value = addr + getSize();
}
