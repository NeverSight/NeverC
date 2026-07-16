#ifndef NEVERC_PLUGIN_HOST_PLUGINARTIFACTREGISTRY_H
#define NEVERC_PLUGIN_HOST_PLUGINARTIFACTREGISTRY_H

#include "neverc/Plugin/PluginCore.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace neverc::plugin {

enum class PluginArtifactOwnership : uint8_t { Owned, Borrowed };

struct PluginArtifactTypeDescriptor {
  NevercInterfaceID ID{};
  std::string Name;
  PluginArtifactOwnership Ownership = PluginArtifactOwnership::Owned;
  std::function<llvm::Expected<void *>(const void *)> Clone;
  std::function<void(void *)> Destroy;
  std::function<llvm::Error(const void *)> Verify;
};

class PluginArtifactType {
public:
  NevercInterfaceID id() const { return Descriptor.ID; }
  llvm::StringRef name() const { return Descriptor.Name; }
  PluginArtifactOwnership ownership() const {
    return Descriptor.Ownership;
  }
  llvm::Error verifyPayload(const void *Payload) const {
    return Descriptor.Verify(Payload);
  }
  void destroyPayload(void *Payload) const {
    if (Descriptor.Ownership == PluginArtifactOwnership::Owned &&
        Descriptor.Destroy)
      Descriptor.Destroy(Payload);
  }

private:
  explicit PluginArtifactType(PluginArtifactTypeDescriptor DescriptorValue)
      : Descriptor(std::move(DescriptorValue)) {}

  PluginArtifactTypeDescriptor Descriptor;
  friend class PluginArtifactRegistry;
  friend class PluginArtifactSlot;
  friend class PluginArtifactTransaction;
};

class PluginArtifactRegistry {
public:
  llvm::Expected<std::shared_ptr<const PluginArtifactType>>
  registerType(PluginArtifactTypeDescriptor Descriptor);
  llvm::Error freeze();
  std::shared_ptr<const PluginArtifactType>
  find(NevercInterfaceID ID) const;

  bool isFrozen() const;
  size_t size() const;

private:
  mutable std::mutex Mutex;
  std::vector<std::shared_ptr<const PluginArtifactType>> Types;
  bool Frozen = false;
};

class PluginArtifactSlot {
public:
  struct Snapshot {
    std::shared_ptr<const PluginArtifactType> Type;
    const void *Payload = nullptr;
    uint64_t Generation = 0;
  };

  explicit PluginArtifactSlot(
      std::shared_ptr<const PluginArtifactType> ExpectedType);
  ~PluginArtifactSlot();

  PluginArtifactSlot(const PluginArtifactSlot &) = delete;
  PluginArtifactSlot &operator=(const PluginArtifactSlot &) = delete;

  NevercInterfaceID expectedType() const;
  const void *payload() const;
  uint64_t generation() const;
  Snapshot snapshot() const;

private:
  struct ReplacedArtifact {
    std::shared_ptr<const PluginArtifactType> Type;
    void *Payload = nullptr;
  };

  llvm::Expected<ReplacedArtifact>
  publish(std::shared_ptr<const PluginArtifactType> Type, void *Payload);

  std::shared_ptr<const PluginArtifactType> ExpectedType;
  std::shared_ptr<const PluginArtifactType> PublishedType;
  void *Payload = nullptr;
  uint64_t Generation = 0;
  mutable std::mutex Mutex;
  friend class PluginArtifactTransaction;
};

class PluginArtifactTransaction {
public:
  static llvm::Expected<std::unique_ptr<PluginArtifactTransaction>>
  create(const PluginArtifactRegistry &Registry, NevercInterfaceID Type,
         void *Candidate);
  ~PluginArtifactTransaction();

  PluginArtifactTransaction(const PluginArtifactTransaction &) = delete;
  PluginArtifactTransaction &
  operator=(const PluginArtifactTransaction &) = delete;

  llvm::Error verify();
  llvm::Error commit(PluginArtifactSlot &Slot);
  void abort();
  bool isCommitted() const;

private:
  PluginArtifactTransaction(std::shared_ptr<const PluginArtifactType> Type,
                            void *Candidate);
  void destroyCandidate();

  std::shared_ptr<const PluginArtifactType> Type;
  void *Candidate = nullptr;
  mutable std::mutex Mutex;
  bool Verified = false;
  bool Committed = false;
  bool Aborted = false;
};

} // namespace neverc::plugin

#endif
