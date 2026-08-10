#ifndef NEVERC_PLUGIN_OBJECT_BUILTINOBJECTWRITERPREFLIGHT_H
#define NEVERC_PLUGIN_OBJECT_BUILTINOBJECTWRITERPREFLIGHT_H

#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

namespace neverc::plugin {

llvm::Error verifyBuiltinObjectWriterComdatRepresentability(
    const PluginObjectComdat &Comdat, llvm::StringRef Boundary);

llvm::Error verifyBuiltinObjectWriterSectionRepresentability(
    const PluginObjectSection &Section, const llvm::Triple &Target,
    llvm::StringRef Boundary);

llvm::Error verifyBuiltinObjectWriterSymbolNameRepresentability(
    llvm::StringRef Name, const llvm::Triple &Target, llvm::StringRef Boundary);

llvm::Error
verifyBuiltinObjectWriterGraphRepresentability(const PluginObjectGraph &Graph,
                                               llvm::StringRef Boundary);

} // namespace neverc::plugin

#endif // NEVERC_PLUGIN_OBJECT_BUILTINOBJECTWRITERPREFLIGHT_H
