#include "neverc/DynCode/Extractor/DynCodeReport.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>

using namespace llvm;

namespace neverc {
namespace dyncode {
namespace {

Error reportError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "dyncode report: " + Message);
}

void appendHex(std::string &Out, ArrayRef<uint8_t> Bytes) {
  static const char Digits[] = "0123456789abcdef";
  for (uint8_t B : Bytes) {
    Out.push_back(Digits[B >> 4]);
    Out.push_back(Digits[B & 0xf]);
  }
}

/// Minimal RFC 8259 string escaping for canonical JSON output.
void appendJSONString(std::string &Out, StringRef S) {
  Out.push_back('"');
  for (char C : S) {
    switch (C) {
    case '"':
      Out += "\\\"";
      break;
    case '\\':
      Out += "\\\\";
      break;
    case '\b':
      Out += "\\b";
      break;
    case '\f':
      Out += "\\f";
      break;
    case '\n':
      Out += "\\n";
      break;
    case '\r':
      Out += "\\r";
      break;
    case '\t':
      Out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(C) < 0x20) {
        char Buf[8];
        snprintf(Buf, sizeof(Buf), "\\u%04x", static_cast<unsigned char>(C));
        Out += Buf;
      } else {
        Out.push_back(C);
      }
    }
  }
  Out.push_back('"');
}

void appendUInt(std::string &Out, uint64_t Value) {
  Out += std::to_string(Value);
}

void appendHexField(std::string &Out, const char *Key, ArrayRef<uint8_t> Bytes) {
  appendJSONString(Out, Key);
  Out.push_back(':');
  Out.push_back('"');
  appendHex(Out, Bytes);
  Out.push_back('"');
}

void appendUIntField(std::string &Out, const char *Key, uint64_t Value) {
  appendJSONString(Out, Key);
  Out.push_back(':');
  appendUInt(Out, Value);
}

} // namespace

Error DynCodeReport::setSummary(const DynCodeReportSummary &Value) {
  if (Frozen)
    return reportError("cannot set summary on a frozen report");
  Summary = Value;
  return Error::success();
}

Error DynCodeReport::addRecord(DynCodeReportRecord Record) {
  if (Frozen)
    return reportError("cannot add a record to a frozen report");
  if (Record.Key.empty())
    return reportError("report record requires a key");
  Records.push_back(std::move(Record));
  return Error::success();
}

Error DynCodeReport::freeze() {
  if (Frozen)
    return Error::success();
  std::stable_sort(Records.begin(), Records.end(),
                   [](const DynCodeReportRecord &A,
                      const DynCodeReportRecord &B) {
                     if (A.PhaseOrder != B.PhaseOrder)
                       return A.PhaseOrder < B.PhaseOrder;
                     if (A.Provider != B.Provider)
                       return A.Provider < B.Provider;
                     return A.Key < B.Key;
                   });
  Frozen = true;
  return Error::success();
}

Expected<std::string> DynCodeReport::toCanonicalJSON() const {
  if (!Frozen)
    return reportError("report must be frozen before serialization");

  std::string Out;
  Out.push_back('{');

  // Top-level keys are emitted in lexicographic order so the document is
  // canonical regardless of how the summary was populated.
  appendUIntField(Out, "alignment", Summary.Alignment);
  Out.push_back(',');
  appendUIntField(Out, "bad_byte_hit_count", Summary.BadByteHitCount);
  Out.push_back(',');
  appendUIntField(Out, "entry_offset", Summary.EntryOffset);
  Out.push_back(',');
  appendJSONString(Out, "entry_symbol");
  Out.push_back(':');
  appendJSONString(Out, Summary.EntrySymbol);
  Out.push_back(',');
  appendUIntField(Out, "image_size", Summary.ImageSize);
  Out.push_back(',');
  appendHexField(Out, "input_digest", Summary.InputDigest);
  Out.push_back(',');

  // journal: array of records already in stable order.
  appendJSONString(Out, "journal");
  Out.push_back(':');
  Out.push_back('[');
  for (size_t I = 0; I < Records.size(); ++I) {
    if (I)
      Out.push_back(',');
    const DynCodeReportRecord &R = Records[I];
    Out.push_back('{');
    appendJSONString(Out, "key");
    Out.push_back(':');
    appendJSONString(Out, R.Key);
    Out.push_back(',');
    appendUIntField(Out, "phase_order", R.PhaseOrder);
    Out.push_back(',');
    appendJSONString(Out, "provider");
    Out.push_back(':');
    appendJSONString(Out, R.Provider);
    Out.push_back(',');
    appendJSONString(Out, "value");
    Out.push_back(':');
    appendJSONString(Out, R.Value);
    Out.push_back('}');
  }
  Out.push_back(']');
  Out.push_back(',');

  appendHexField(Out, "output_digest", Summary.OutputDigest);
  Out.push_back(',');
  appendUIntField(Out, "padding_size", Summary.PaddingSize);
  Out.push_back(',');
  appendUIntField(Out, "patched_relocation_count",
                  Summary.PatchedRelocationCount);
  Out.push_back(',');
  appendUIntField(Out, "rejected_section_count", Summary.RejectedSectionCount);
  Out.push_back(',');
  appendUIntField(Out, "remaining_external_count",
                  Summary.RemainingExternalCount);
  Out.push_back(',');
  appendHexField(Out, "request_digest", Summary.RequestDigest);
  Out.push_back(',');
  appendHexField(Out, "route_digest", Summary.RouteDigest);
  Out.push_back(',');
  appendUIntField(Out, "runtime_contract_count", Summary.RuntimeContractCount);
  Out.push_back(',');
  appendUIntField(Out, "selected_section_count", Summary.SelectedSectionCount);

  Out.push_back('}');
  return Out;
}

} // namespace dyncode
} // namespace neverc
