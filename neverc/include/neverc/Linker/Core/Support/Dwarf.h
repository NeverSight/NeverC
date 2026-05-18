//===----------------------------------------------------------------------===//
//
//  Dwarf — per-object DWARF cache.  A `DWARFCache` parses the line table
//  and the variable-location records once and exposes point queries used
//  by the diagnostic paths (`getDILineInfo`, `getVariableLoc`).
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_SUPPORT_DWARF_H
#define LINKER_CORE_SUPPORT_DWARF_H

#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include <memory>
#include <string>

namespace llvm {
struct DILineInfo;
} // namespace llvm

namespace linker {

class DWARFCache {
public:
  DWARFCache(std::unique_ptr<llvm::DWARFContext> dwarf);

  std::optional<llvm::DILineInfo> getDILineInfo(uint64_t offset,
                                                uint64_t sectionIndex);
  std::optional<std::pair<std::string, unsigned>>
  getVariableLoc(StringRef name);

  llvm::DWARFContext *getContext() { return dwarf.get(); }

private:
  std::unique_ptr<llvm::DWARFContext> dwarf;
  std::vector<const llvm::DWARFDebugLine::LineTable *> lineTables;

  struct VarLoc {
    const llvm::DWARFDebugLine::LineTable *lt;
    unsigned file;
    unsigned line;
  };
  llvm::DenseMap<StringRef, VarLoc> variableLoc;
};

} // namespace linker

#endif // LINKER_CORE_SUPPORT_DWARF_H
