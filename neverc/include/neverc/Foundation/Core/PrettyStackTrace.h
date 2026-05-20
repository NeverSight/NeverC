#ifndef NEVERC_FOUNDATION_PRETTYSTACKTRACE_H
#define NEVERC_FOUNDATION_PRETTYSTACKTRACE_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"

namespace neverc {

class PrettyStackTraceLoc : public llvm::PrettyStackTraceEntry {
  SourceManager &SM;
  SourceLocation Loc;
  const char *Message;

public:
  PrettyStackTraceLoc(SourceManager &sm, SourceLocation L, const char *Msg)
      : SM(sm), Loc(L), Message(Msg) {}
  void print(llvm::raw_ostream &OS) const override;
};
} // namespace neverc

#endif
