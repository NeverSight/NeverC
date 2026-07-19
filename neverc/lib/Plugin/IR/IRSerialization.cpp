#include "IRBuilderPluginBridge.h"
#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <limits>
#include <new>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

struct SerializedIRBuffer {
  std::vector<uint8_t> Bytes;
};

NevercStatus serializationStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

IRPluginBridge *getBridge(void *Context, NevercTaskHandle Task,
                          NevercStatus *OutStatus) {
  if (Context == nullptr || OutStatus == nullptr) {
    if (OutStatus != nullptr)
      *OutStatus = serializationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return nullptr;
  }
  auto *Bridge = static_cast<IRPluginBridge *>(Context);
  NevercTaskHandle Expected = Bridge->taskHandle();
  if (Expected.Owner != Task.Owner || Expected.Value != Task.Value) {
    *OutStatus = serializationStatus(NEVERC_STATUS_WRONG_SCOPE);
    return nullptr;
  }
  *OutStatus = neverc_status_ok();
  return Bridge;
}

NevercStatus NEVERC_CALL ImportModule(void *Context, NevercTaskHandle Task,
                                      NevercIRSerializationFormat Format,
                                      NevercByteView Bytes) {
  NevercStatus Status{};
  IRPluginBridge *Bridge = getBridge(Context, Task, &Status);
  if (Bridge == nullptr)
    return Status;
  return Bridge->importModule(Format, Bytes);
}

NevercStatus NEVERC_CALL
ExportModule(void *Context, NevercTaskHandle Task,
             NevercIRSerializationFormat Format,
             NevercIRSerializedBufferHandle *OutBuffer) {
  NevercStatus Status{};
  IRPluginBridge *Bridge = getBridge(Context, Task, &Status);
  if (Bridge == nullptr)
    return Status;
  if (OutBuffer == nullptr)
    return serializationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBuffer = {};
  auto Exported = Bridge->exportModule(Format);
  if (!Exported) {
    consumeError(Exported.takeError());
    return serializationStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutBuffer = *Exported;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetSerializedBufferView(
    void *Context, NevercTaskHandle Task, NevercIRSerializedBufferHandle Buffer,
    NevercByteView *OutBytes) {
  NevercStatus Status{};
  IRPluginBridge *Bridge = getBridge(Context, Task, &Status);
  if (Bridge == nullptr)
    return Status;
  return Bridge->getSerializedBufferView(Buffer, OutBytes);
}

NevercStatus NEVERC_CALL
ReleaseSerializedBuffer(void *Context, NevercTaskHandle Task,
                        NevercIRSerializedBufferHandle Buffer) {
  NevercStatus Status{};
  IRPluginBridge *Bridge = getBridge(Context, Task, &Status);
  if (Bridge == nullptr)
    return Status;
  return Bridge->releaseSerializedBuffer(Buffer);
}

} // namespace

void IRPluginBridge::initializeSerializationAPI() {
  CoreAPI.ImportModule = ImportModule;
  CoreAPI.ExportModule = ExportModule;
  CoreAPI.GetSerializedBufferView = GetSerializedBufferView;
  CoreAPI.ReleaseSerializedBuffer = ReleaseSerializedBuffer;
}

NevercStatus IRPluginBridge::importModule(NevercIRSerializationFormat Format,
                                          NevercByteView Bytes) {
  NevercStatus MutationStatus = checkMutationAllowed();
  if (MutationStatus.Code != NEVERC_STATUS_OK)
    return MutationStatus;
  if ((Bytes.Data == nullptr && Bytes.Length != 0) ||
      Bytes.Length > std::numeric_limits<size_t>::max())
    return serializationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (BuilderBridge != nullptr && BuilderBridge->hasActiveMutation())
    return serializationStatus(NEVERC_STATUS_BUSY);
  if (!ownsModule())
    return serializationStatus(NEVERC_STATUS_INVALID_STATE);

  StringRef Contents(reinterpret_cast<const char *>(Bytes.Data),
                     static_cast<size_t>(Bytes.Length));
  std::unique_ptr<llvm::Module> Candidate;
  if (Format == NEVERC_IR_SERIALIZATION_BITCODE) {
    std::unique_ptr<MemoryBuffer> Buffer =
        MemoryBuffer::getMemBuffer(Contents, "<plugin-bitcode>", false);
    auto Parsed = parseBitcodeFile(Buffer->getMemBufferRef(), context());
    if (!Parsed) {
      consumeError(Parsed.takeError());
      return serializationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    Candidate = std::move(*Parsed);
  } else if (Format == NEVERC_IR_SERIALIZATION_TEXT) {
    // NeverC's LLVM fork intentionally omits the textual IR parser. Keep the
    // format value reserved in the ABI, but report the missing capability
    // instead of depending on an unavailable LLVMAsmParser component.
    return serializationStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  } else {
    return serializationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }

  if (verifyModule(*Candidate, &errs()))
    return serializationStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  StringRef RequiredTriple = module().getTargetTriple();
  if (!RequiredTriple.empty() &&
      Candidate->getTargetTriple() != RequiredTriple)
    return serializationStatus(NEVERC_STATUS_WRONG_SCOPE);
  StringRef RequiredLayout = module().getDataLayoutStr();
  if (!RequiredLayout.empty()) {
    DataLayout NormalizedRequired(UpgradeDataLayoutString(
        RequiredLayout, module().getTargetTriple()));
    if (Candidate->getDataLayout() != NormalizedRequired)
      return serializationStatus(NEVERC_STATUS_WRONG_SCOPE);
  }

  // A raw buffer has no stable logical filename. BitcodeReader otherwise uses
  // the temporary MemoryBuffer identifier ("<plugin-bitcode>"), so preserve
  // the host artifact's identifier across an atomic content replacement.
  Candidate->setModuleIdentifier(module().getModuleIdentifier());

  auto CreatedModule = Task.handles().create(PluginIRModuleHandleKind,
                                              Candidate.get());
  if (!CreatedModule) {
    consumeError(CreatedModule.takeError());
    return serializationStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }

  invalidateModuleHandles();
  OwnedModule = std::move(Candidate);
  Module = OwnedModule.get();
  ModuleHandle = *CreatedModule;
  noteMutation();
  return neverc_status_ok();
}

Expected<NevercIRSerializedBufferHandle>
IRPluginBridge::exportModule(NevercIRSerializationFormat Format) {
  SmallVector<char, 0> Storage;
  raw_svector_ostream Stream(Storage);
  if (Format == NEVERC_IR_SERIALIZATION_BITCODE)
    WriteBitcodeToFile(module(), Stream);
  else if (Format == NEVERC_IR_SERIALIZATION_TEXT)
    module().print(Stream, nullptr);
  else
    return createStringError(inconvertibleErrorCode(),
                             "unknown IR serialization format");

  auto *Payload = new (std::nothrow) SerializedIRBuffer();
  if (Payload == nullptr)
    return createStringError(inconvertibleErrorCode(),
                             "serialized IR buffer allocation failed");
  Payload->Bytes.assign(reinterpret_cast<const uint8_t *>(Storage.data()),
                        reinterpret_cast<const uint8_t *>(Storage.data()) +
                            Storage.size());
  auto Handle = Task.handles().create(
      PluginIRSerializedBufferHandleKind, Payload, [](void *Value) {
        delete static_cast<SerializedIRBuffer *>(Value);
      });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  SerializedBuffers.push_back(*Handle);
  return *Handle;
}

NevercStatus IRPluginBridge::getSerializedBufferView(
    NevercIRSerializedBufferHandle Buffer, NevercByteView *OutBytes) const {
  if (OutBytes == nullptr)
    return serializationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBytes = {};
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Buffer, PluginIRSerializedBufferHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto &Bytes = static_cast<SerializedIRBuffer *>(Payload)->Bytes;
  OutBytes->Data = Bytes.data();
  OutBytes->Length = Bytes.size();
  return neverc_status_ok();
}

NevercStatus IRPluginBridge::releaseSerializedBuffer(
    NevercIRSerializedBufferHandle Buffer) {
  NevercStatus Status =
      Task.handles().release(Buffer, PluginIRSerializedBufferHandleKind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  SerializedBuffers.erase(
      std::remove_if(SerializedBuffers.begin(), SerializedBuffers.end(),
                     [Buffer](NevercIRSerializedBufferHandle Candidate) {
                       return Candidate.Owner == Buffer.Owner &&
                              Candidate.Value == Buffer.Value;
                     }),
      SerializedBuffers.end());
  return neverc_status_ok();
}

void IRPluginBridge::invalidateSerializedBuffers() {
  for (NevercIRSerializedBufferHandle Buffer : SerializedBuffers)
    (void)Task.handles().release(Buffer,
                                 PluginIRSerializedBufferHandleKind);
  SerializedBuffers.clear();
}

} // namespace neverc::plugin
