#include "neverc/Plugin/Host/PluginTargetInfo.h"
#include "neverc/Foundation/Core/MacroBuilder.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"

using namespace llvm;

namespace neverc::plugin {

void PluginTargetInfo::getTargetDefines(const LangOptions &,
                                        MacroBuilder &Builder) const {
  for (const VerifiedTargetMacro &Macro : Record.Macros) {
    if (Macro.Undefine) {
      Builder.undefineMacro(Macro.Name);
      continue;
    }
    if (Macro.Value.empty())
      Builder.defineMacro(Macro.Name);
    else
      Builder.defineMacro(Macro.Name, Macro.Value);
  }
}

ArrayRef<Builtin::Info> PluginTargetInfo::getTargetBuiltins() const {
  return BuiltinInfos;
}

const VerifiedTargetBuiltin *
PluginTargetInfo::getPluginBuiltin(unsigned BuiltinID) const {
  if (BuiltinID < Builtin::FirstTSBuiltin)
    return nullptr;
  const size_t Index = BuiltinID - Builtin::FirstTSBuiltin;
  return Index < Record.Builtins.size() ? &Record.Builtins[Index]
                                        : nullptr;
}

} // namespace neverc::plugin
