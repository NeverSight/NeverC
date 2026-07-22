//===- ConformanceSummary.cpp - per-capability conformance summary ------===//

#include "ConformanceSummary.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

namespace neverc::conformance {

namespace {

struct Entry {
  CapStatus Status = CapStatus::Skip;
  std::string Reason;
};

std::mutex &summaryMutex() {
  static std::mutex Mutex;
  return Mutex;
}

// Ordered so the JSON is stable regardless of test execution order.
std::map<std::string, Entry> &summaryEntries() {
  static std::map<std::string, Entry> Entries;
  return Entries;
}

const char *statusName(CapStatus Status) {
  switch (Status) {
  case CapStatus::Pass:
    return "pass";
  case CapStatus::Fail:
    return "fail";
  case CapStatus::Skip:
    break;
  }
  return "skip";
}

// Rank so a stronger result never gets overwritten by a weaker later one:
// fail > pass > skip.
int rank(CapStatus Status) {
  switch (Status) {
  case CapStatus::Fail:
    return 2;
  case CapStatus::Pass:
    return 1;
  case CapStatus::Skip:
    break;
  }
  return 0;
}

std::string jsonEscape(const std::string &Input) {
  std::string Output;
  Output.reserve(Input.size() + 2);
  for (char C : Input) {
    switch (C) {
    case '"':
      Output += "\\\"";
      break;
    case '\\':
      Output += "\\\\";
      break;
    case '\n':
      Output += "\\n";
      break;
    case '\r':
      Output += "\\r";
      break;
    case '\t':
      Output += "\\t";
      break;
    default:
      Output += C;
    }
  }
  return Output;
}

} // namespace

void recordCapability(const std::string &Capability, CapStatus Status,
                      const std::string &Reason) {
  std::lock_guard<std::mutex> Lock(summaryMutex());
  Entry &Existing = summaryEntries()[Capability];
  if (rank(Status) >= rank(Existing.Status)) {
    Existing.Status = Status;
    if (!Reason.empty())
      Existing.Reason = Reason;
  }
}

bool writeConformanceSummary() {
  const char *Path = std::getenv("NEVERC_CONFORMANCE_SUMMARY");
  if (Path == nullptr || Path[0] == '\0')
    return false;

  std::lock_guard<std::mutex> Lock(summaryMutex());
  std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
  if (!Stream)
    return false;

  size_t Pass = 0, Skip = 0, Fail = 0;
  for (const auto &Pair : summaryEntries()) {
    switch (Pair.second.Status) {
    case CapStatus::Pass:
      ++Pass;
      break;
    case CapStatus::Fail:
      ++Fail;
      break;
    case CapStatus::Skip:
      ++Skip;
      break;
    }
  }

  Stream << "{\n";
  Stream << "  \"pass\": " << Pass << ",\n";
  Stream << "  \"skip\": " << Skip << ",\n";
  Stream << "  \"fail\": " << Fail << ",\n";
  Stream << "  \"capabilities\": {\n";
  size_t Index = 0;
  const size_t Count = summaryEntries().size();
  for (const auto &Pair : summaryEntries()) {
    Stream << "    \"" << jsonEscape(Pair.first) << "\": {\"status\": \""
           << statusName(Pair.second.Status) << "\", \"reason\": \""
           << jsonEscape(Pair.second.Reason) << "\"}";
    if (++Index != Count)
      Stream << ",";
    Stream << "\n";
  }
  Stream << "  }\n";
  Stream << "}\n";
  return static_cast<bool>(Stream);
}

} // namespace neverc::conformance
