#ifndef NEVERC_LIB_PLUGIN_PYTHON_PYTHONPLUGINLOADER_H
#define NEVERC_LIB_PLUGIN_PYTHON_PYTHONPLUGINLOADER_H

#include "../Core/PluginRuntime.h"
#include "neverc/Plugin/Host/PluginRegistry.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace neverc::plugin {

struct PythonPluginLoadResult {
  PluginDescriptorRecord Descriptor;
  std::unique_ptr<PluginRuntime> Runtime;
};

llvm::Expected<PythonPluginLoadResult>
loadPythonPlugin(llvm::StringRef CanonicalPath);

} // namespace neverc::plugin

#endif
