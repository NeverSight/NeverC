#ifndef NEVERC_LIB_PLUGIN_OBJECT_BUILTINLLVMOBJECTWRITER_H
#define NEVERC_LIB_PLUGIN_OBJECT_BUILTINLLVMOBJECTWRITER_H

#include "neverc/Plugin/PluginObject.h"

namespace neverc::plugin {

// Version of the "NCSE" native-section extension the builtin reader emits and
// the builtin writer understands. Version 2 appends the ELF sh_entsize that a
// mergeable section needs to survive a read/rewrite/write round trip; version 1
// still parses, it just carries no entry size.
inline constexpr uint32_t NevercObjectNCSEVersion = 2;

NevercStatus NEVERC_CALL writeBuiltinLLVMObject(
    void *UserData, const NevercObjectWriteRequest *Request);

} // namespace neverc::plugin

#endif
