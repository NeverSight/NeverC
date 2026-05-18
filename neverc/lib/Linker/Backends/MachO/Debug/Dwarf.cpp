#include "Linker/MachO/Dwarf.h"
#include "Linker/MachO/InputFiles.h"
#include "Linker/MachO/InputSection.h"
#include "Linker/MachO/OutputSegment.h"

#include <memory>

using namespace linker;
using namespace linker::macho;
using namespace llvm;

std::unique_ptr<DwarfObject> DwarfObject::create(ObjFile *obj) {
  auto dObj = std::make_unique<DwarfObject>();
  bool hasDwarfInfo = false;
  // The linker only needs to extract the source file path and line numbers from
  // the debug info, so we initialize DwarfObject with just the sections
  // necessary to get that path. The debugger will locate the debug info via the
  // object file paths that we emit in our STABS symbols, so we don't need to
  // process & emit them ourselves.
  for (const DebugSection &debugSection : obj->debugSections) {
    if (StringRef *s =
            StringSwitch<StringRef *>(debugSection.name)
                .Case(section_names::debugInfo, &dObj->infoSection.Data)
                .Case(section_names::debugLine, &dObj->lineSection.Data)
                .Case(section_names::debugStrOffs, &dObj->strOffsSection.Data)
                .Case(section_names::debugAbbrev, &dObj->abbrevSection)
                .Case(section_names::debugStr, &dObj->strSection)
                .Default(nullptr)) {
      *s = toStringRef(debugSection.data);
      hasDwarfInfo = true;
    }
  }

  if (hasDwarfInfo)
    return dObj;
  return nullptr;
}
