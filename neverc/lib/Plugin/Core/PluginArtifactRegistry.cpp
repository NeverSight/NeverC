#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/JSON.h"
#include <limits>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error artifactError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

void destroyArtifact(const std::shared_ptr<const PluginArtifactType> &Type,
                     void *Payload) {
  if (!Type || !Payload ||
      Type->ownership() != PluginArtifactOwnership::Owned)
    return;
  Type->destroyPayload(Payload);
}

} // namespace

Expected<std::shared_ptr<const PluginArtifactType>>
PluginArtifactRegistry::registerType(
    PluginArtifactTypeDescriptor Descriptor) {
  if (Descriptor.ID.High == 0 && Descriptor.ID.Low == 0)
    return artifactError("artifact type ID must be nonzero");
  if (Descriptor.Name.empty() || !json::isUTF8(Descriptor.Name) ||
      StringRef(Descriptor.Name).contains('\0'))
    return artifactError("artifact type name must be valid UTF-8");
  if (Descriptor.Ownership == PluginArtifactOwnership::Owned &&
      !Descriptor.Destroy)
    return artifactError(
        "owned artifact type requires a destroy callback");
  if (!Descriptor.Verify)
    return artifactError("artifact type requires a verifier");

  std::lock_guard<std::mutex> Lock(Mutex);
  if (Frozen)
    return artifactError("artifact type registry is frozen");
  for (const auto &Existing : Types) {
    if (sameID(Existing->id(), Descriptor.ID))
      return artifactError("duplicate artifact type ID");
    if (Existing->name() == Descriptor.Name)
      return artifactError("duplicate artifact type name");
  }
  auto Type = std::shared_ptr<const PluginArtifactType>(
      new PluginArtifactType(std::move(Descriptor)));
  Types.push_back(Type);
  return Type;
}

Error PluginArtifactRegistry::freeze() {
  std::lock_guard<std::mutex> Lock(Mutex);
  Frozen = true;
  return Error::success();
}

std::shared_ptr<const PluginArtifactType>
PluginArtifactRegistry::find(NevercInterfaceID ID) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto It = llvm::find_if(Types, [&](const auto &Type) {
    return sameID(Type->id(), ID);
  });
  return It == Types.end() ? nullptr : *It;
}

bool PluginArtifactRegistry::isFrozen() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Frozen;
}

size_t PluginArtifactRegistry::size() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Types.size();
}

PluginArtifactSlot::PluginArtifactSlot(
    std::shared_ptr<const PluginArtifactType> ExpectedTypeValue)
    : ExpectedType(std::move(ExpectedTypeValue)) {}

PluginArtifactSlot::~PluginArtifactSlot() {
  std::shared_ptr<const PluginArtifactType> Type;
  void *Published = nullptr;
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    Type = std::move(PublishedType);
    Published = Payload;
    Payload = nullptr;
  }
  if (Type && Published) {
    try {
      destroyArtifact(Type, Published);
    } catch (...) {
    }
  }
}

NevercInterfaceID PluginArtifactSlot::expectedType() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return ExpectedType ? ExpectedType->id() : NevercInterfaceID{};
}

const void *PluginArtifactSlot::payload() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Payload;
}

uint64_t PluginArtifactSlot::generation() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Generation;
}

PluginArtifactSlot::Snapshot PluginArtifactSlot::snapshot() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return {PublishedType, Payload, Generation};
}

Expected<PluginArtifactSlot::ReplacedArtifact>
PluginArtifactSlot::publish(
    std::shared_ptr<const PluginArtifactType> Type, void *NewPayload) {
  if (!Type || !NewPayload)
    return artifactError("cannot publish an empty artifact");
  std::lock_guard<std::mutex> Lock(Mutex);
  if (!ExpectedType || !sameID(ExpectedType->id(), Type->id()))
    return artifactError("artifact transaction has the wrong type for slot");
  if (Generation == std::numeric_limits<uint64_t>::max())
    return artifactError("artifact slot generation is exhausted");

  ReplacedArtifact Replaced{std::move(PublishedType), Payload};
  PublishedType = std::move(Type);
  Payload = NewPayload;
  ++Generation;
  return Replaced;
}

PluginArtifactTransaction::PluginArtifactTransaction(
    std::shared_ptr<const PluginArtifactType> TypeValue,
    void *CandidateValue)
    : Type(std::move(TypeValue)), Candidate(CandidateValue) {}

Expected<std::unique_ptr<PluginArtifactTransaction>>
PluginArtifactTransaction::create(const PluginArtifactRegistry &Registry,
                                  NevercInterfaceID Type,
                                  void *Candidate) {
  if (!Candidate)
    return artifactError("artifact candidate must be non-null");
  auto RegisteredType = Registry.find(Type);
  if (!RegisteredType)
    return artifactError("artifact candidate has an unknown type");
  return std::unique_ptr<PluginArtifactTransaction>(
      new PluginArtifactTransaction(std::move(RegisteredType), Candidate));
}

PluginArtifactTransaction::~PluginArtifactTransaction() {
  destroyCandidate();
}

Error PluginArtifactTransaction::verify() {
  std::shared_ptr<const PluginArtifactType> CandidateType;
  void *CandidatePayload = nullptr;
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Committed)
      return artifactError(
          "artifact transaction may only commit once");
    if (Aborted)
      return artifactError("artifact transaction is aborted");
    if (Verified)
      return Error::success();
    CandidateType = Type;
    CandidatePayload = Candidate;
  }
  try {
    if (Error E = CandidateType->verifyPayload(CandidatePayload))
      return std::move(E);
  } catch (...) {
    return artifactError("artifact verifier threw an exception");
  }
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Committed)
      return artifactError(
          "artifact transaction may only commit once");
    if (Aborted || Candidate != CandidatePayload)
      return artifactError(
          "artifact transaction changed during verification");
    Verified = true;
  }
  return Error::success();
}

Error PluginArtifactTransaction::commit(PluginArtifactSlot &Slot) {
  if (Error E = verify())
    return std::move(E);

  PluginArtifactSlot::ReplacedArtifact Replaced;
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Committed)
      return artifactError(
          "artifact transaction may only commit once");
    if (Aborted)
      return artifactError("artifact transaction is aborted");
    auto Published = Slot.publish(Type, Candidate);
    if (!Published)
      return Published.takeError();
    Replaced = std::move(*Published);
    Candidate = nullptr;
    Committed = true;
  }
  if (Replaced.Payload) {
    try {
      destroyArtifact(Replaced.Type, Replaced.Payload);
    } catch (...) {
      return artifactError(
          "replaced artifact destroy callback threw an exception");
    }
  }
  return Error::success();
}

void PluginArtifactTransaction::abort() {
  destroyCandidate();
}

bool PluginArtifactTransaction::isCommitted() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Committed;
}

void PluginArtifactTransaction::destroyCandidate() {
  std::shared_ptr<const PluginArtifactType> CandidateType;
  void *CandidatePayload = nullptr;
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (!Candidate || Committed || Aborted)
      return;
    CandidateType = Type;
    CandidatePayload = Candidate;
    Candidate = nullptr;
    Aborted = true;
  }
  try {
    destroyArtifact(CandidateType, CandidatePayload);
  } catch (...) {
  }
}

} // namespace neverc::plugin
