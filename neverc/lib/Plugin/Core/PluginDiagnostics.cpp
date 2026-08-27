#include "neverc/Plugin/Host/PluginDiagnostics.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <tuple>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus diagnosticStatus(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

bool validText(NevercStringView View, size_t MaximumLength,
               bool AllowEmpty) {
  if (View.Length > MaximumLength ||
      View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  StringRef Text(View.Data ? View.Data : "",
                 static_cast<size_t>(View.Length));
  return (AllowEmpty || !Text.empty()) && !Text.contains('\0') &&
         json::isUTF8(Text);
}

StringRef text(NevercStringView View) {
  return StringRef(View.Data ? View.Data : "",
                   static_cast<size_t>(View.Length));
}

uint64_t phaseOrder(StringRef PhaseID) {
  uint64_t Known =
      StringSwitch<uint64_t>(PhaseID)
          .Case("neverc.driver.raw_arguments", 1)
          .Case("neverc.driver.parsed_arguments", 2)
          .Case("neverc.driver.select_toolchain", 3)
          .Case("neverc.driver.action_graph", 4)
          .Case("neverc.driver.job_graph", 5)
          .Case("neverc.driver.execute_job", 6)
          .Default(0);
  if (Known != 0)
    return Known;
  uint64_t Hash = UINT64_C(0xcbf29ce484222325);
  for (unsigned char Byte : PhaseID.bytes()) {
    Hash ^= Byte;
    Hash *= UINT64_C(1099511628211);
  }
  return UINT64_C(0x100000000) | (Hash & UINT64_C(0xffffffff));
}

} // namespace

uint64_t PluginDiagnostics::beginTransaction() {
  std::lock_guard<std::mutex> Lock(Mutex);
  return NextTransactionID++;
}

void PluginDiagnostics::discardTransaction(uint64_t TransactionID) {
  std::lock_guard<std::mutex> Lock(Mutex);
  llvm::erase_if(Records, [&](const PluginDiagnosticRecord &Record) {
    return Record.TransactionID == TransactionID &&
           Record.Severity != NEVERC_DIAGNOSTIC_FATAL;
  });
}

uint64_t PluginDiagnostics::append(
    PluginSession &Session, PluginTaskContext *Task,
    NevercDiagnosticSeverity Severity, uint32_t Code, StringRef PluginID,
    StringRef PhaseID, StringRef Message, std::vector<std::string> Notes,
    uint64_t TransactionID, bool Implicit) {
  uint64_t PluginOrder = 0;
  ArrayRef<std::shared_ptr<const PluginModule>> Plugins =
      Session.plugins();
  for (size_t I = 0; I != Plugins.size(); ++I)
    if (Plugins[I]->descriptor().PluginID == PluginID) {
      PluginOrder = I + 1;
      break;
    }

  std::lock_guard<std::mutex> Lock(Mutex);
  uint64_t Sequence = NextSequence++;
  uint64_t DetailToken =
      (UINT64_C(4) << 56) | (Sequence & UINT64_C(0x00ffffffffffffff));
  Records.push_back(
      {Severity,
       Code,
       PluginID.str(),
       PhaseID.str(),
       Message.str(),
       std::move(Notes),
       Task ? Task->handle().Owner : 0,
       phaseOrder(PhaseID),
       PluginOrder,
       Sequence,
       DetailToken,
       TransactionID,
       Implicit});
  return DetailToken;
}

NevercStatus PluginDiagnostics::emit(
    PluginSession &Session, PluginTaskContext *Task,
    StringRef ActivePluginID, StringRef CallbackName,
    uint64_t TransactionID,
    const NevercDiagnosticDescriptor &Descriptor,
    NevercDiagnosticHandle &OutDiagnostic) {
  OutDiagnostic = {};
  constexpr uint64_t Required =
      offsetof(NevercDiagnosticDescriptor, Message) +
      sizeof(NevercDiagnosticDescriptor::Message);
  if (Descriptor.Header.StructSize < Required ||
      Descriptor.Header.Major != NEVERC_CORE_API_MAJOR ||
      Descriptor.Header.Flags != 0 ||
      Descriptor.Severity > NEVERC_DIAGNOSTIC_FATAL ||
      !validText(Descriptor.PluginID, 512, true) ||
      !validText(Descriptor.PhaseID, 512, true) ||
      !validText(Descriptor.Message, 1024 * 1024, false))
    return diagnosticStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  StringRef PluginID = text(Descriptor.PluginID);
  if (PluginID.empty())
    PluginID = ActivePluginID;
  if (PluginID.empty() || PluginID != ActivePluginID)
    return diagnosticStatus(NEVERC_STATUS_WRONG_SCOPE);

  StringRef PhaseID = text(Descriptor.PhaseID);
  if (PhaseID.empty())
    PhaseID = CallbackName.split('/').first;

  std::vector<std::string> Notes;
  if (NEVERC_ABI_FIELD_AVAILABLE(
          &Descriptor.Header, NevercDiagnosticDescriptor, Notes)) {
    if (Descriptor.Notes.Count > 64 ||
        Descriptor.Notes.Count > std::numeric_limits<size_t>::max() ||
        (Descriptor.Notes.Count != 0 && !Descriptor.Notes.Data) ||
        (Descriptor.Notes.Count != 0 &&
         Descriptor.Notes.ElementStride < sizeof(NevercDiagnosticNote)) ||
        Descriptor.Notes.ElementStride >
            std::numeric_limits<size_t>::max())
      return diagnosticStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    const auto *Bytes =
        static_cast<const unsigned char *>(Descriptor.Notes.Data);
    Notes.reserve(static_cast<size_t>(Descriptor.Notes.Count));
    for (uint64_t I = 0; I != Descriptor.Notes.Count; ++I) {
      if (I != 0 &&
          Descriptor.Notes.ElementStride >
              std::numeric_limits<size_t>::max() / I)
        return diagnosticStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      const auto *Note = reinterpret_cast<const NevercDiagnosticNote *>(
          Bytes + static_cast<size_t>(I * Descriptor.Notes.ElementStride));
      if (Note->Header.StructSize < sizeof(*Note) ||
          Note->Header.Major != NEVERC_CORE_API_MAJOR ||
          Note->Header.Flags != 0 ||
          !validText(Note->Message, 1024 * 1024, false))
        return diagnosticStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Notes.push_back(text(Note->Message).str());
    }
  }
  if (NEVERC_ABI_FIELD_AVAILABLE(
          &Descriptor.Header, NevercDiagnosticDescriptor, Location) &&
      (Descriptor.Location.Owner != 0 ||
       Descriptor.Location.Value != 0))
    return diagnosticStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  if (NEVERC_ABI_FIELD_AVAILABLE(
          &Descriptor.Header, NevercDiagnosticDescriptor, Ranges) &&
      Descriptor.Ranges.Count != 0)
    return diagnosticStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  if (NEVERC_ABI_FIELD_AVAILABLE(
          &Descriptor.Header, NevercDiagnosticDescriptor, FixIts) &&
      Descriptor.FixIts.Count != 0)
    return diagnosticStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);

  uint64_t DetailToken =
      append(Session, Task, Descriptor.Severity, Descriptor.Code,
             PluginID, PhaseID, text(Descriptor.Message),
             std::move(Notes), TransactionID, false);
  OutDiagnostic.Owner = Session.handle().Owner;
  OutDiagnostic.Value = DetailToken;
  if (Descriptor.Severity == NEVERC_DIAGNOSTIC_FATAL)
    Session.cancel();
  return neverc_status_ok();
}

void PluginDiagnostics::emitImplicit(
    PluginSession &Session, PluginTaskContext *Task, StringRef PluginID,
    StringRef PhaseID, StringRef Message, uint64_t TransactionID) {
  append(Session, Task, NEVERC_DIAGNOSTIC_ERROR, 0, PluginID,
         PhaseID, Message, {}, TransactionID, true);
}

bool PluginDiagnostics::ownsDetail(uint64_t DetailToken,
                                   StringRef PluginID,
                                   uint64_t TransactionID) const {
  if (DetailToken == 0)
    return false;
  std::lock_guard<std::mutex> Lock(Mutex);
  return llvm::any_of(Records, [&](const PluginDiagnosticRecord &Record) {
    return Record.DetailToken == DetailToken &&
           Record.PluginID == PluginID &&
           Record.TransactionID == TransactionID &&
           Record.Severity >= NEVERC_DIAGNOSTIC_ERROR;
  });
}

std::string
PluginDiagnostics::messageForDetail(uint64_t DetailToken) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto It = llvm::find_if(
      Records, [&](const PluginDiagnosticRecord &Record) {
        return Record.DetailToken == DetailToken;
      });
  return It == Records.end() ? std::string() : It->Message;
}

std::vector<PluginDiagnosticRecord> PluginDiagnostics::takeSorted() {
  std::vector<PluginDiagnosticRecord> Result;
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    Result.swap(Records);
  }
  llvm::stable_sort(
      Result, [](const PluginDiagnosticRecord &Left,
                 const PluginDiagnosticRecord &Right) {
        return std::tie(Left.TaskOrder, Left.PhaseOrder,
                        Left.PluginOrder, Left.Sequence) <
               std::tie(Right.TaskOrder, Right.PhaseOrder,
                        Right.PluginOrder, Right.Sequence);
      });
  return Result;
}

} // namespace neverc::plugin
