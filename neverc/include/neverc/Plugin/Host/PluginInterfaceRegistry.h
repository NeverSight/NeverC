#ifndef NEVERC_PLUGIN_HOST_PLUGININTERFACEREGISTRY_H
#define NEVERC_PLUGIN_HOST_PLUGININTERFACEREGISTRY_H

#include "neverc/Plugin/Host/PluginRegistry.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace neverc::plugin {

struct InterfaceQueryResult {
  const void *Table = nullptr;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  NevercInterfaceStability Stability = NEVERC_INTERFACE_STABLE;
  const OwnedCompatibilityKey *Compatibility = nullptr;
};

class PluginInterfaceRegistry {
public:
  llvm::Error registerInterface(
      NevercInterfaceID Interface, NevercInterfaceStability Stability,
      const void *Table, OwnedCompatibilityKey Compatibility);
  llvm::Error freeze();

  llvm::Expected<InterfaceQueryResult>
  query(NevercInterfaceID Interface, uint16_t Major,
        uint16_t MinimumMinor) const;
  llvm::Error
  validateRequirement(const OwnedInterfaceRequirement &Requirement) const;

  bool isFrozen() const { return Frozen; }
  size_t size() const { return Records.size(); }

  NevercStatus queryForC(NevercInterfaceID Interface, uint16_t Major,
                         uint16_t MinimumMinor, const void **OutTable,
                         uint16_t *OutMinor,
                         uint64_t *OutStructSize) const;

private:
  struct InterfaceRecord {
    NevercInterfaceID Interface{};
    uint16_t Major = 0;
    uint16_t Minor = 0;
    uint64_t StructSize = 0;
    NevercInterfaceStability Stability = NEVERC_INTERFACE_STABLE;
    const void *Table = nullptr;
    OwnedCompatibilityKey Compatibility;
  };

  const InterfaceRecord *find(NevercInterfaceID Interface,
                              uint16_t Major) const;
  bool containsID(NevercInterfaceID Interface) const;

  std::vector<InterfaceRecord> Records;
  bool Frozen = false;
};

NevercStatus NEVERC_CALL
queryPluginInterface(void *Context, NevercInterfaceID Interface,
                     uint16_t Major, uint16_t MinimumMinor,
                     const void **OutTable, uint16_t *OutMinor,
                     uint64_t *OutStructSize);

} // namespace neverc::plugin

#endif
