#ifndef NEVERC_LIB_PLUGIN_OBJECT_BUILTINLLVMOBJECTWRITER_H
#define NEVERC_LIB_PLUGIN_OBJECT_BUILTINLLVMOBJECTWRITER_H

#include "neverc/Plugin/PluginObject.h"

namespace neverc::plugin {

NevercStatus NEVERC_CALL writeBuiltinLLVMObject(
    void *UserData, const NevercObjectWriteRequest *Request);

} // namespace neverc::plugin

#endif
