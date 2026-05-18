#ifndef NEVERC_BASIC_SYNCSCOPE_H
#define NEVERC_BASIC_SYNCSCOPE_H

#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <memory>

namespace neverc {

enum class SyncScope {
  SystemScope,
  DeviceScope,
  WorkgroupScope,
  WavefrontScope,
  SingleScope,
  Last = SingleScope
};

inline llvm::StringRef getAsString(SyncScope S) {
  switch (S) {
  case SyncScope::SystemScope:
    return "system_scope";
  case SyncScope::DeviceScope:
    return "device_scope";
  case SyncScope::WorkgroupScope:
    return "workgroup_scope";
  case SyncScope::WavefrontScope:
    return "wavefront_scope";
  case SyncScope::SingleScope:
    return "single_scope";
  }
  llvm_unreachable("Invalid synch scope");
}

enum class AtomicScopeModelKind { None, Generic };

class AtomicScopeModel {
public:
  virtual ~AtomicScopeModel() {}
  virtual SyncScope map(unsigned S) const = 0;
  virtual bool isValid(unsigned S) const = 0;
  virtual llvm::ArrayRef<unsigned> getRuntimeValues() const = 0;
  virtual unsigned getFallBackValue() const = 0;
  static std::unique_ptr<AtomicScopeModel> create(AtomicScopeModelKind K);
};

class AtomicScopeGenericModel : public AtomicScopeModel {
public:
  enum ID {
    System = 0,
    Device = 1,
    Workgroup = 2,
    Wavefront = 3,
    Single = 4,
    Last = Single
  };

  AtomicScopeGenericModel() = default;

  SyncScope map(unsigned S) const override {
    switch (static_cast<ID>(S)) {
    case Device:
      return SyncScope::DeviceScope;
    case System:
      return SyncScope::SystemScope;
    case Workgroup:
      return SyncScope::WorkgroupScope;
    case Wavefront:
      return SyncScope::WavefrontScope;
    case Single:
      return SyncScope::SingleScope;
    }
    llvm_unreachable("Invalid language sync scope value");
  }

  bool isValid(unsigned S) const override {
    return S >= static_cast<unsigned>(System) &&
           S <= static_cast<unsigned>(Last);
  }

  llvm::ArrayRef<unsigned> getRuntimeValues() const override {
    static_assert(Last == Single, "Does not include all sync scopes");
    static const unsigned Scopes[] = {
        static_cast<unsigned>(Device), static_cast<unsigned>(System),
        static_cast<unsigned>(Workgroup), static_cast<unsigned>(Wavefront),
        static_cast<unsigned>(Single)};
    return llvm::ArrayRef(Scopes);
  }

  unsigned getFallBackValue() const override {
    return static_cast<unsigned>(System);
  }
};

inline std::unique_ptr<AtomicScopeModel>
AtomicScopeModel::create(AtomicScopeModelKind K) {
  switch (K) {
  case AtomicScopeModelKind::None:
    return std::unique_ptr<AtomicScopeModel>{};
  case AtomicScopeModelKind::Generic:
    return std::make_unique<AtomicScopeGenericModel>();
  }
  llvm_unreachable("Invalid atomic scope model kind");
}
} // namespace neverc

#endif
