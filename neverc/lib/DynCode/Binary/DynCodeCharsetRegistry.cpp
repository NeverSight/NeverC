// The dyncode charset encoder registry: see DynCodeCharsetRegistry.h.

#include "Binary/DynCodeCharsetRegistry.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace neverc {
namespace dyncode {

llvm::Error
DynCodeCharsetRegistry::registerProvider(DynCodeCharsetProvider Provider) {
  if (Provider.ID.empty())
    return createStringError(errc::invalid_argument,
                             "dyncode charset registry: provider needs an ID");
  if (!Provider.Encode)
    return createStringError(
        errc::invalid_argument,
        "dyncode charset registry: provider '%s' has no callback",
        Provider.ID.c_str());
  if (find(Provider.ID))
    return createStringError(
        errc::invalid_argument,
        "dyncode charset registry: duplicate charset provider '%s'",
        Provider.ID.c_str());
  Providers.push_back(std::move(Provider));
  return Error::success();
}

const DynCodeCharsetProvider *
DynCodeCharsetRegistry::find(llvm::StringRef ID) const {
  for (const DynCodeCharsetProvider &P : Providers)
    if (ID == P.ID)
      return &P;
  return nullptr;
}

llvm::Error DynCodeCharsetRegistry::run(llvm::StringRef ID, DynCodeImage &Image,
                                        ArrayRef<uint8_t> BadBytes) const {
  const DynCodeCharsetProvider *P = find(ID);
  if (!P)
    return createStringError(
        errc::invalid_argument,
        "dyncode charset registry: no charset encoder registered for '%s'; "
        "register one via registerCharsetEncoder or drop -fdyncode-charset=",
        ID.str().c_str());
  return P->Encode(Image, BadBytes);
}

} // namespace dyncode
} // namespace neverc
