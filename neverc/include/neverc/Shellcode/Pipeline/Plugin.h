#ifndef NEVERC_SHELLCODE_PLUGIN_H
#define NEVERC_SHELLCODE_PLUGIN_H

#include "neverc/Shellcode/Pipeline/ShellcodeOptions.h"
#include "neverc/Shellcode/Pipeline/TargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <functional>
#include <string>

namespace neverc {
namespace shellcode {

struct BadByteRewriteContext {
  llvm::SmallVectorImpl<uint8_t> *Bytes = nullptr;
  llvm::ArrayRef<uint8_t> BadBytes;
  const TargetDesc *Target = nullptr;
  const ShellcodeOptions *Opts = nullptr;
};

enum class BadByteRewriteResult : uint8_t {
  NotApplied,
  Applied,
  Error,
};

using BadByteRewriteStrategy =
    std::function<BadByteRewriteResult(BadByteRewriteContext &)>;

size_t registerBadByteRewriteStrategy(BadByteRewriteStrategy Strategy);
void clearBadByteRewriteStrategies();
llvm::ArrayRef<BadByteRewriteStrategy> getBadByteRewriteStrategies();

struct CharsetEncoderEntry {
  std::string Name;
  std::string Description;

  std::function<llvm::SmallVector<uint8_t, 256>(llvm::ArrayRef<uint8_t> Raw,
                                                const TargetDesc &)>
      Encode;

  std::function<llvm::SmallVector<uint8_t, 256>(const TargetDesc &)> Stub;

  std::function<bool(uint8_t)> IsCharsetMember;
};

void registerCharsetEncoder(CharsetEncoderEntry Entry);
const CharsetEncoderEntry *getCharsetEncoder(llvm::StringRef Name);
void clearCharsetEncoders();

}
}

#endif
