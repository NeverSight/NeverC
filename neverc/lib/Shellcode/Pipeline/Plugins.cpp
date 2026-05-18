#include "neverc/Shellcode/Pipeline/Plugin.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

#include <mutex>
#include <vector>

namespace neverc {
namespace shellcode {

#ifdef NEVERC_HAVE_SHELLCODE_TEST_PLUGIN
extern "C" void neverc_shellcode_test_plugin_link_anchor();
#endif

namespace {

std::mutex &registryMutex() {
  static std::mutex M;
  return M;
}

std::vector<BadByteRewriteStrategy> &rewriteStorage() {
  static std::vector<BadByteRewriteStrategy> S;
  return S;
}

std::vector<CharsetEncoderEntry> &charsetStorage() {
  static std::vector<CharsetEncoderEntry> S;
  return S;
}

} // namespace

size_t registerBadByteRewriteStrategy(BadByteRewriteStrategy Strategy) {
  std::lock_guard<std::mutex> Lock(registryMutex());
  rewriteStorage().push_back(std::move(Strategy));
  return rewriteStorage().size() - 1;
}

void clearBadByteRewriteStrategies() {
  std::lock_guard<std::mutex> Lock(registryMutex());
  rewriteStorage().clear();
}

llvm::ArrayRef<BadByteRewriteStrategy> getBadByteRewriteStrategies() {
#ifdef NEVERC_HAVE_SHELLCODE_TEST_PLUGIN
  neverc_shellcode_test_plugin_link_anchor();
#endif
  std::lock_guard<std::mutex> Lock(registryMutex());
  return rewriteStorage();
}

void registerCharsetEncoder(CharsetEncoderEntry Entry) {
  std::lock_guard<std::mutex> Lock(registryMutex());
  for (auto &Existing : charsetStorage()) {
    if (Existing.Name == Entry.Name) {
      Existing = std::move(Entry);
      return;
    }
  }
  charsetStorage().push_back(std::move(Entry));
}

const CharsetEncoderEntry *getCharsetEncoder(llvm::StringRef Name) {
#ifdef NEVERC_HAVE_SHELLCODE_TEST_PLUGIN
  neverc_shellcode_test_plugin_link_anchor();
#endif
  std::lock_guard<std::mutex> Lock(registryMutex());
  for (const auto &E : charsetStorage()) {
    if (E.Name == Name)
      return &E;
  }
  return nullptr;
}

void clearCharsetEncoders() {
  std::lock_guard<std::mutex> Lock(registryMutex());
  charsetStorage().clear();
}

} // namespace shellcode
} // namespace neverc
