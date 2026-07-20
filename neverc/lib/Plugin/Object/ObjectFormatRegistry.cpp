#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "llvm/Support/Errc.h"
#include <algorithm>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

} // namespace

Expected<std::unique_ptr<ObjectFormatRegistry>>
ObjectFormatRegistry::create(
    std::shared_ptr<const PluginTargetSnapshot> Snapshot) {
  if (!Snapshot)
    return createStringError(errc::invalid_argument,
                             "object Format registry has no Target snapshot");

  auto Registry =
      std::unique_ptr<ObjectFormatRegistry>(
          new ObjectFormatRegistry(std::move(Snapshot)));
  Registry->Formats.assign(Registry->Snapshot->objectFormats().begin(),
                           Registry->Snapshot->objectFormats().end());
  appendBuiltinLLVMObjectFormats(Registry->Formats);

  for (size_t I = 0; I != Registry->Formats.size(); ++I) {
    const auto &Format = Registry->Formats[I];
    for (size_t J = 0; J != I; ++J)
      if (sameID(Registry->Formats[J].ID, Format.ID))
        return createStringError(
            errc::invalid_argument,
            "duplicate object Format ID in registry for '" +
                Registry->Formats[J].CanonicalName + "' and '" +
                Format.CanonicalName + "'");
  }

  return Registry;
}

const PluginTargetSnapshot::ObjectFormatRecord *
ObjectFormatRegistry::find(NevercObjectFormatID ID) const {
  auto It = std::find_if(
      Formats.begin(), Formats.end(),
      [&](const PluginTargetSnapshot::ObjectFormatRecord &Format) {
        return sameID(Format.ID, ID);
      });
  return It == Formats.end() ? nullptr : &*It;
}

} // namespace neverc::plugin
