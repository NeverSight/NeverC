#ifndef NEVERC_ANALYZE_SEMAREPLAY_H
#define NEVERC_ANALYZE_SEMAREPLAY_H

#include <cstdint>
#include <string>

namespace neverc {
class Sema;
class TranslationUnitDecl;

enum class SemaReplayStatus : uint8_t {
  Success,
  UnsupportedASTKind,
  InvalidAST,
  SemanticError,
};

struct SemaReplayResult {
  SemaReplayStatus Status = SemaReplayStatus::Success;
  uint64_t DeclarationCount = 0;
  uint64_t StatementCount = 0;
  uint64_t ExpressionCount = 0;
  std::string Message;

  explicit operator bool() const {
    return Status == SemaReplayStatus::Success;
  }
};

class SemaReplay {
public:
  static SemaReplayResult run(Sema &SemanticAnalyzer,
                              TranslationUnitDecl &TranslationUnit);
};

} // namespace neverc

#endif
