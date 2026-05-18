#ifndef LINKER_MACHO_UNWIND_INFO_H
#define LINKER_MACHO_UNWIND_INFO_H

#include "Linker/MachO/ConcatOutputSection.h"
#include "Linker/MachO/SyntheticSections.h"
#include "llvm/ADT/MapVector.h"

namespace linker::macho {

class UnwindInfoSection : public SyntheticSection {
public:
  // If all functions are free of unwind info, we can omit the unwind info
  // section entirely.
  bool isNeeded() const override { return !allEntriesAreOmitted; }
  void addSymbol(const Defined *);
  virtual void prepare() = 0;

protected:
  UnwindInfoSection();

  llvm::MapVector<std::pair<const InputSection *, uint64_t /*Defined::value*/>,
                  const Defined *>
      symbols;
  bool allEntriesAreOmitted = true;
};

UnwindInfoSection *makeUnwindInfoSection();

} // namespace linker::macho

#endif
