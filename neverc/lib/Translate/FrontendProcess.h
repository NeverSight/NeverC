#ifndef NEVERC_TRANSLATE_FRONTENDPROCESS_H
#define NEVERC_TRANSLATE_FRONTENDPROCESS_H
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <vector>
namespace neverc::translate {
class TranslationCancellation {
public:
  TranslationCancellation();
  ~TranslationCancellation();
  TranslationCancellation(const TranslationCancellation &) = delete;
  TranslationCancellation &operator=(const TranslationCancellation &) = delete;
};
bool translationCancelled();
struct ProcessResult {
  int ExitCode = -1;
  bool Cancelled = false;
  std::string Error;
};
/// Standalone command only: an invocation-wide TranslationCancellation must be
/// alive. Requests termination of the owned child on cancellation/timeout, then
/// polls for at most 200 ms to reap it. An OS-unreapable killed process cannot
/// block cleanup; the result reports this condition and any Windows process
/// handle is closed before returning. No process group is signalled.
/// Uses an OS execution environment allowlist and disables implicit NeverC
/// configuration; source options and dependencies must be explicit inputs.
ProcessResult runProcess(const std::vector<std::string> &Arguments,
                         llvm::StringRef Stdout, llvm::StringRef Stderr,
                         unsigned TimeoutSeconds = 60);
} // namespace neverc::translate
#endif
