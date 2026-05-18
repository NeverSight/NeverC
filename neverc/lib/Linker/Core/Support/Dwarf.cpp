//===----------------------------------------------------------------------===//
//
//  Dwarf — DWARF line/variable lookup cache.
//
//  Backends that decode DWARF (ELF, Mach-O) allocate one `DWARFCache`
//  per input object so later symbol-diagnostic formatting can translate
//  a symbol/offset into a `file:line` pair without re-parsing the debug
//  sections.  The cache is deliberately narrow: only the pieces needed
//  for diagnostics are kept.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Support/Dwarf.h"
#include "Linker/Core/Runtime/Diagnostic.h"

using namespace llvm;

namespace linker {

DWARFCache::DWARFCache(std::unique_ptr<llvm::DWARFContext> d)
    : dwarf(std::move(d)) {
  for (std::unique_ptr<DWARFUnit> &cu : dwarf->compile_units()) {
    auto report = [](Error err) {
      handleAllErrors(std::move(err),
                      [](ErrorInfoBase &info) { warn(info.message()); });
    };
    Expected<const DWARFDebugLine::LineTable *> expectedLT =
        dwarf->getLineTableForUnit(cu.get(), report);
    const DWARFDebugLine::LineTable *lt = nullptr;
    if (expectedLT)
      lt = *expectedLT;
    else
      report(expectedLT.takeError());
    if (!lt)
      continue;
    lineTables.push_back(lt);

    // Walk the variable DIEs and record their location so we can later
    // annotate undefined/duplicate-symbol diagnostics with source
    // coordinates.
    for (const auto &entry : cu->dies()) {
      DWARFDie die(cu.get(), &entry);
      // Skip tags that are not variables.
      if (die.getTag() != dwarf::DW_TAG_variable)
        continue;

      // Skip local variables: diagnostics only ever report failures on
      // non-local symbols.
      if (!dwarf::toUnsigned(die.find(dwarf::DW_AT_external), 0))
        continue;

      unsigned file = dwarf::toUnsigned(die.find(dwarf::DW_AT_decl_file), 0);
      if (!lt->hasFileAtIndex(file))
        continue;

      unsigned line = dwarf::toUnsigned(die.find(dwarf::DW_AT_decl_line), 0);

      // Prefer the linkage name when the variable has one — two
      // variables sharing the same base name across namespaces will
      // otherwise collide in `variableLoc`.  Fall back to the regular
      // name, or skip the entry if neither is available.
      StringRef name =
          dwarf::toString(die.find(dwarf::DW_AT_linkage_name),
                          dwarf::toString(die.find(dwarf::DW_AT_name), ""));
      if (!name.empty())
        variableLoc.insert({name, {lt, file, line}});
    }
  }
}

// Returns `(file name, line number)` of the definition of a data object
// (variable, array, ...).
std::optional<std::pair<std::string, unsigned>>
DWARFCache::getVariableLoc(StringRef name) {
  auto it = variableLoc.find(name);
  if (it == variableLoc.end())
    return std::nullopt;

  std::string fileName;
  if (!it->second.lt->getFileNameByIndex(
          it->second.file, {},
          DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, fileName))
    return std::nullopt;

  return std::make_pair(fileName, it->second.line);
}

// Returns the DWARF line-info record for `(offset, sectionIndex)`, if any.
std::optional<DILineInfo> DWARFCache::getDILineInfo(uint64_t offset,
                                                    uint64_t sectionIndex) {
  DILineInfo info;
  for (const llvm::DWARFDebugLine::LineTable *lt : lineTables) {
    if (lt->getFileLineInfoForAddress(
            {offset, sectionIndex}, nullptr,
            DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, info))
      return info;
  }
  return std::nullopt;
}

} // namespace linker
