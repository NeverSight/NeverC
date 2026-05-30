#include "BridgeCastHelpers.h"
#include "neverc/Plugin/PluginLoader.h"

using namespace llvm;

namespace neverc {
namespace plugin {

// ===----------------------------------------------------------------------===
//  Linker API forwarding
//  These forward to the linker backend accessor table installed via
//  setLinkerBackend() for the duration of a LINK_* hook.  With no backend
//  installed (the common, non-linking path) or a missing accessor they return
//  safe defaults, so plugins that probe the linker API outside a link run get
//  well-defined empty results instead of crashing.
// ===----------------------------------------------------------------------===

static NevercLinkerSymbolRef bridgeLinkGetFirstSymbol(void) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->GetFirstSymbol ? B->GetFirstSymbol() : nullptr;
}
static NevercLinkerSymbolRef bridgeLinkGetNextSymbol(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->GetNextSymbol ? B->GetNextSymbol(S) : nullptr;
}
static NevercLinkerSymbolRef bridgeLinkFindSymbol(const char *Name) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->FindSymbol ? B->FindSymbol(Name) : nullptr;
}
static const char *bridgeLinkSymbolGetName(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SymbolGetName ? B->SymbolGetName(S) : "";
}
static uint64_t bridgeLinkSymbolGetValue(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SymbolGetValue ? B->SymbolGetValue(S) : 0;
}
static uint64_t bridgeLinkSymbolGetSize(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SymbolGetSize ? B->SymbolGetSize(S) : 0;
}
static int bridgeLinkSymbolIsDefined(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SymbolIsDefined ? B->SymbolIsDefined(S) : 0;
}
static int bridgeLinkSymbolIsLocal(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SymbolIsLocal ? B->SymbolIsLocal(S) : 0;
}
static int bridgeLinkSymbolIsHidden(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SymbolIsHidden ? B->SymbolIsHidden(S) : 0;
}
static void bridgeLinkSymbolSetVisibilityHidden(NevercLinkerSymbolRef S,
                                                int IsHidden) {
  const NevercLinkerBackend *B = getLinkerBackend();
  if (B && B->SymbolSetVisibilityHidden)
    B->SymbolSetVisibilityHidden(S, IsHidden);
}
static NevercLinkerSectionRef bridgeLinkGetFirstSection(void) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->GetFirstSection ? B->GetFirstSection() : nullptr;
}
static NevercLinkerSectionRef
bridgeLinkGetNextSection(NevercLinkerSectionRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->GetNextSection ? B->GetNextSection(S) : nullptr;
}
static NevercLinkerSectionRef bridgeLinkFindSection(const char *Name) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->FindSection ? B->FindSection(Name) : nullptr;
}
static const char *bridgeLinkSectionGetName(NevercLinkerSectionRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SectionGetName ? B->SectionGetName(S) : "";
}
static uint64_t bridgeLinkSectionGetSize(NevercLinkerSectionRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SectionGetSize ? B->SectionGetSize(S) : 0;
}
static uint64_t bridgeLinkSectionGetAlignment(NevercLinkerSectionRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SectionGetAlignment ? B->SectionGetAlignment(S) : 0;
}
static unsigned bridgeLinkSectionGetFlags(NevercLinkerSectionRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SectionGetFlags ? B->SectionGetFlags(S) : 0;
}
static const char *bridgeLinkGetOutputPath(void) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->GetOutputPath ? B->GetOutputPath() : "";
}
static unsigned bridgeLinkGetOutputFormat(void) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->GetOutputFormat ? B->GetOutputFormat()
                                 : NEVERC_LINK_FORMAT_UNKNOWN;
}

static const char *bridgeLinkGetOutputFormatName(void) {
  const NevercLinkerBackend *B = getLinkerBackend();
  unsigned Fmt = B && B->GetOutputFormat ? B->GetOutputFormat()
                                         : NEVERC_LINK_FORMAT_UNKNOWN;
  switch (Fmt) {
  case NEVERC_LINK_FORMAT_ELF:   return "ELF";
  case NEVERC_LINK_FORMAT_COFF:  return "COFF";
  case NEVERC_LINK_FORMAT_MACHO: return "Mach-O";
  default:                       return "unknown";
  }
}

// ===----------------------------------------------------------------------===
//  Hook point name lookup
// ===----------------------------------------------------------------------===

static const char *bridgeHookPointGetName(unsigned Hook) {
  switch (Hook) {
  case NEVERC_HOOK_PRE_OPT:                 return "PRE_OPT";
  case NEVERC_HOOK_POST_OPT:                return "POST_OPT";
  case NEVERC_HOOK_PIPELINE_START:          return "PIPELINE_START";
  case NEVERC_HOOK_PIPELINE_LAST:           return "PIPELINE_LAST";
  case NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT:  return "BEFORE_CODEGEN_PREEMIT";
  case NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR: return "AFTER_CODEGEN_FINAL_MIR";
  case NEVERC_HOOK_SC_BEFORE_PREP:          return "SC_BEFORE_PREP";
  case NEVERC_HOOK_SC_AFTER_PREP:           return "SC_AFTER_PREP";
  case NEVERC_HOOK_SC_BEFORE_INLINING:      return "SC_BEFORE_INLINING";
  case NEVERC_HOOK_SC_AFTER_INLINING:       return "SC_AFTER_INLINING";
  case NEVERC_HOOK_SC_AFTER_STACKIFY:       return "SC_AFTER_STACKIFY";
  case NEVERC_HOOK_SC_AFTER_FINAL_IR:       return "SC_AFTER_FINAL_IR";
  case NEVERC_HOOK_SC_BEFORE_PREEMIT:       return "SC_BEFORE_PREEMIT";
  case NEVERC_HOOK_SC_AFTER_PREEMIT:        return "SC_AFTER_PREEMIT";
  case NEVERC_HOOK_SC_AFTER_FINAL_MIR:      return "SC_AFTER_FINAL_MIR";
  case NEVERC_HOOK_SC_POST_EXTRACT:         return "SC_POST_EXTRACT";
  case NEVERC_HOOK_SC_POST_FINALIZE:        return "SC_POST_FINALIZE";
  case NEVERC_HOOK_LTO_PRE_OPT:             return "LTO_PRE_OPT";
  case NEVERC_HOOK_LTO_POST_OPT:            return "LTO_POST_OPT";
  case NEVERC_HOOK_LINK_PRE_LAYOUT:         return "LINK_PRE_LAYOUT";
  case NEVERC_HOOK_LINK_POST_LAYOUT:        return "LINK_POST_LAYOUT";
  case NEVERC_HOOK_LINK_POST_EMIT:          return "LINK_POST_EMIT";
  default:                                  return "<unknown>";
  }
}

void populateLinkerBridge(NevercHostAPI &API) {
  API.LinkGetFirstSymbol = bridgeLinkGetFirstSymbol;
  API.LinkGetNextSymbol = bridgeLinkGetNextSymbol;
  API.LinkFindSymbol = bridgeLinkFindSymbol;
  API.LinkSymbolGetName = bridgeLinkSymbolGetName;
  API.LinkSymbolGetValue = bridgeLinkSymbolGetValue;
  API.LinkSymbolGetSize = bridgeLinkSymbolGetSize;
  API.LinkSymbolIsDefined = bridgeLinkSymbolIsDefined;
  API.LinkSymbolIsLocal = bridgeLinkSymbolIsLocal;
  API.LinkSymbolIsHidden = bridgeLinkSymbolIsHidden;
  API.LinkSymbolSetVisibilityHidden = bridgeLinkSymbolSetVisibilityHidden;

  API.LinkGetFirstSection = bridgeLinkGetFirstSection;
  API.LinkGetNextSection = bridgeLinkGetNextSection;
  API.LinkFindSection = bridgeLinkFindSection;
  API.LinkSectionGetName = bridgeLinkSectionGetName;
  API.LinkSectionGetSize = bridgeLinkSectionGetSize;
  API.LinkSectionGetAlignment = bridgeLinkSectionGetAlignment;
  API.LinkSectionGetFlags = bridgeLinkSectionGetFlags;

  API.LinkGetOutputPath = bridgeLinkGetOutputPath;
  API.LinkGetOutputFormat = bridgeLinkGetOutputFormat;
  API.LinkGetOutputFormatName = bridgeLinkGetOutputFormatName;

  API.HookPointGetName = bridgeHookPointGetName;
}

} // namespace plugin
} // namespace neverc
