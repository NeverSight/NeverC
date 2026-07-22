#include "neverc/Plugin/Host/PluginCallbackStats.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;

namespace neverc::plugin {

namespace {

// Deterministic, dependency-free JSON string escaping.  Plugin IDs are
// canonical lowercase ASCII and phase names are ASCII, but callback names may
// contain '/', so escape the JSON-significant characters and control bytes.
void appendJSONString(raw_ostream &OS, StringRef Value) {
  OS << '"';
  for (char Ch : Value) {
    unsigned char Byte = static_cast<unsigned char>(Ch);
    switch (Ch) {
    case '"':
      OS << "\\\"";
      break;
    case '\\':
      OS << "\\\\";
      break;
    case '\n':
      OS << "\\n";
      break;
    case '\r':
      OS << "\\r";
      break;
    case '\t':
      OS << "\\t";
      break;
    default:
      if (Byte < 0x20) {
        OS << "\\u" << format_hex_no_prefix(Byte, 4);
      } else {
        OS << Ch;
      }
      break;
    }
  }
  OS << '"';
}

} // namespace

PluginCallbackStats::Entry &
PluginCallbackStats::lookup(StringRef PluginID, StringRef Callback) {
  for (Entry &E : Entries)
    if (E.PluginID == PluginID && E.Callback == Callback)
      return E;
  Entries.push_back(Entry{PluginID.str(), Callback.str(), 0, 0, 0, 0});
  return Entries.back();
}

void PluginCallbackStats::record(StringRef PluginID, StringRef Callback,
                                 uint64_t Nanos, bool Error) {
  if (PluginID.empty())
    return;
  std::lock_guard<std::mutex> Lock(Mutex);
  Entry &E = lookup(PluginID, Callback);
  E.Calls += 1;
  E.TotalNanos += Nanos;
  if (Error)
    E.Errors += 1;
}

void PluginCallbackStats::recordCacheHit(StringRef PluginID,
                                         StringRef Callback) {
  if (PluginID.empty())
    return;
  std::lock_guard<std::mutex> Lock(Mutex);
  lookup(PluginID, Callback).CacheHits += 1;
}

bool PluginCallbackStats::empty() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Entries.empty();
}

uint64_t PluginCallbackStats::totalCalls() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  uint64_t Total = 0;
  for (const Entry &E : Entries)
    Total += E.Calls;
  return Total;
}

uint64_t PluginCallbackStats::totalNanos() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  uint64_t Total = 0;
  for (const Entry &E : Entries)
    Total += E.TotalNanos;
  return Total;
}

std::vector<PluginCallbackStats::Entry> PluginCallbackStats::snapshot() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  std::vector<Entry> Out = Entries;
  std::sort(Out.begin(), Out.end(), [](const Entry &A, const Entry &B) {
    if (A.PluginID != B.PluginID)
      return A.PluginID < B.PluginID;
    return A.Callback < B.Callback;
  });
  return Out;
}

std::string PluginCallbackStats::toJSON() const {
  std::vector<Entry> Items = snapshot();
  std::string Out;
  raw_string_ostream OS(Out);
  OS << "{\"schema\":\"neverc.plugin.callback-stats.v1\",\"entries\":[";
  for (size_t I = 0; I != Items.size(); ++I) {
    const Entry &E = Items[I];
    if (I != 0)
      OS << ',';
    OS << "{\"plugin\":";
    appendJSONString(OS, E.PluginID);
    OS << ",\"callback\":";
    appendJSONString(OS, E.Callback);
    OS << ",\"calls\":" << E.Calls << ",\"total_nanos\":" << E.TotalNanos
       << ",\"errors\":" << E.Errors << ",\"cache_hits\":" << E.CacheHits
       << '}';
  }
  OS << "]}";
  return OS.str();
}

} // namespace neverc::plugin
