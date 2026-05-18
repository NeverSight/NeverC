#include "neverc/Analyze/DelayedDiagnostic.h"
#include "neverc/Analyze/Sema.h"
#include <cstring>

using namespace neverc;
using namespace sema;

DelayedDiagnostic DelayedDiagnostic::makeAvailability(
    AvailabilityResult AR, llvm::ArrayRef<SourceLocation> Locs,
    const NamedDecl *ReferringDecl, const NamedDecl *OffendingDecl,
    llvm::StringRef Msg) {
  assert(!Locs.empty());
  DelayedDiagnostic DD;
  DD.Triggered = false;
  DD.Loc = Locs.front();
  DD.ReferringDecl = ReferringDecl;
  DD.OffendingDecl = OffendingDecl;
  char *MessageData = nullptr;
  if (!Msg.empty()) {
    MessageData = new char[Msg.size()];
    memcpy(MessageData, Msg.data(), Msg.size());
  }
  DD.Message = MessageData;
  DD.MessageLen = Msg.size();

  DD.AR = AR;
  return DD;
}

void DelayedDiagnostic::Destroy() { delete[] Message; }

void Sema::DelayedDiagnostics::add(const sema::DelayedDiagnostic &diag) {
  assert(shouldDelayDiagnostics() && "trying to delay without pool");
  CurPool->add(diag);
}
