#ifndef NEVERC_PLUGIN_HOST_BUILTINLLVMASMPARSER_H
#define NEVERC_PLUGIN_HOST_BUILTINLLVMASMPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
class Target;
class raw_pwrite_stream;
}

namespace neverc::plugin {

struct BuiltinLLVMAsmParserRequest {
  const llvm::Target *Target = nullptr;
  llvm::Triple TargetTriple;
  llvm::StringRef CPU;
  llvm::StringRef Features;
  llvm::VersionTuple SDKVersion;
  llvm::MemoryBufferRef Input;
  llvm::raw_pwrite_stream *Output = nullptr;
};

llvm::Error
runBuiltinLLVMAsmParser(const BuiltinLLVMAsmParserRequest &Request);

} // namespace neverc::plugin

#endif
