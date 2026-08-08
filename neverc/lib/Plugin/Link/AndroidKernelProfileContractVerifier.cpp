#include "AndroidKernelProfileContractVerifier.h"
#include "neverc/Foundation/AndroidKernelProfileContract.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MemoryBufferRef.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

Expected<uint64_t> readRecords(ArrayRef<uint8_t> Data, StringRef Context) {
  if (Data.empty() || Data.size() % sizeof(uint64_t) != 0)
    return createStringError(errc::invalid_argument,
                             Context + ": malformed native Android kernel "
                                       "profile contract");

  std::optional<uint64_t> Result;
  for (size_t Offset = 0; Offset != Data.size(); Offset += sizeof(uint64_t)) {
    const uint64_t Contract = support::endian::read64le(Data.data() + Offset);
    if (!AndroidKernelProfileContract::isValid(Contract))
      return createStringError(errc::invalid_argument,
                               Context + ": invalid native Android kernel "
                                         "profile contract");
    if (Result && *Result != Contract)
      return createStringError(errc::invalid_argument,
                               Context + ": internally inconsistent native "
                                         "Android kernel profile contracts");
    Result = Contract;
  }
  return *Result;
}

Error mismatch(StringRef Boundary, uint64_t Expected, uint64_t Actual) {
  return createStringError(
      errc::invalid_argument,
      Boundary + ": incompatible Android kernel profile contracts (expected " +
          Twine(AndroidKernelProfileContract::profile(Expected)) +
          " / KCFI mode " +
          Twine(AndroidKernelProfileContract::kcfiMode(Expected)) + ", got " +
          Twine(AndroidKernelProfileContract::profile(Actual)) +
          " / KCFI mode " +
          Twine(AndroidKernelProfileContract::kcfiMode(Actual)) + ")");
}

template <typename Range, typename Reader>
Expected<uint64_t> requireMatching(Range Inputs, StringRef Boundary,
                                   Reader Read) {
  if (Inputs.empty())
    return createStringError(errc::invalid_argument,
                             Boundary + ": no Android kernel object inputs");

  std::optional<uint64_t> Expected;
  size_t Index = 0;
  for (const auto &Input : Inputs) {
    const std::string Context =
        (Boundary + " input " + Twine(Index++)).str();
    auto Contract = Read(Input, Context);
    if (!Contract)
      return Contract.takeError();
    if (Expected && *Expected != *Contract)
      return mismatch(Boundary, *Expected, *Contract);
    Expected = *Contract;
  }
  return *Expected;
}

} // namespace

Expected<uint64_t>
readAndroidKernelProfileContract(const PluginObjectGraph &Object,
                                 StringRef Context) {
  std::optional<uint64_t> Result;
  for (const PluginObjectSection &Section : Object.sections()) {
    if (Section.Name != AndroidKernelProfileContract::NativeSection)
      continue;
    auto Contract = readRecords(Section.Data, Context);
    if (!Contract)
      return Contract.takeError();
    if (Result && *Result != *Contract)
      return mismatch(Context, *Result, *Contract);
    Result = *Contract;
  }
  if (!Result)
    return createStringError(errc::invalid_argument,
                             Context + ": missing native Android kernel "
                                       "profile contract");
  return *Result;
}

Expected<uint64_t>
readAndroidKernelProfileContract(ArrayRef<uint8_t> Image, StringRef Context) {
  auto Object = object::ObjectFile::createObjectFile(MemoryBufferRef(
      StringRef(reinterpret_cast<const char *>(Image.data()), Image.size()),
      Context));
  if (!Object)
    return Object.takeError();
  if (!(*Object)->isELF() || !(*Object)->isLittleEndian())
    return createStringError(errc::invalid_argument,
                             Context + ": expected a little-endian ELF object");

  std::optional<uint64_t> Result;
  for (const object::SectionRef &Section : (*Object)->sections()) {
    auto Name = Section.getName();
    if (!Name)
      return Name.takeError();
    if (*Name != AndroidKernelProfileContract::NativeSection)
      continue;
    auto Contents = Section.getContents();
    if (!Contents)
      return Contents.takeError();
    auto Contract = readRecords(
        ArrayRef<uint8_t>(
            reinterpret_cast<const uint8_t *>(Contents->data()),
            Contents->size()),
        Context);
    if (!Contract)
      return Contract.takeError();
    if (Result && *Result != *Contract)
      return mismatch(Context, *Result, *Contract);
    Result = *Contract;
  }
  if (!Result)
    return createStringError(errc::invalid_argument,
                             Context + ": missing native Android kernel "
                                       "profile contract");
  return *Result;
}

Expected<uint64_t> requireMatchingAndroidKernelProfileContracts(
    ArrayRef<PluginObjectGraph *> Objects, StringRef Boundary) {
  return requireMatching(
      Objects, Boundary,
      [](PluginObjectGraph *Object, StringRef Context) -> Expected<uint64_t> {
        if (!Object)
          return createStringError(errc::invalid_argument,
                                   Context + ": null object graph");
        return readAndroidKernelProfileContract(*Object, Context);
      });
}

Expected<uint64_t> requireMatchingAndroidKernelProfileContracts(
    ArrayRef<StringRef> Images, StringRef Boundary) {
  return requireMatching(
      Images, Boundary,
      [](StringRef Image, StringRef Context) -> Expected<uint64_t> {
        return readAndroidKernelProfileContract(
            ArrayRef<uint8_t>(
                reinterpret_cast<const uint8_t *>(Image.data()), Image.size()),
            Context);
      });
}

Error requireAndroidKernelProfileContract(const PluginObjectGraph &Object,
                                          uint64_t Expected,
                                          StringRef Boundary) {
  auto Actual = readAndroidKernelProfileContract(Object, Boundary);
  if (!Actual)
    return Actual.takeError();
  if (*Actual != Expected)
    return mismatch(Boundary, Expected, *Actual);
  return Error::success();
}

Error requireAndroidKernelProfileContract(ArrayRef<uint8_t> Image,
                                          uint64_t Expected,
                                          StringRef Boundary) {
  auto Actual = readAndroidKernelProfileContract(Image, Boundary);
  if (!Actual)
    return Actual.takeError();
  if (*Actual != Expected)
    return mismatch(Boundary, Expected, *Actual);
  return Error::success();
}

Error stripAndroidKernelProfileContract(PluginObjectGraph &Object,
                                        StringRef Boundary) {
  DenseSet<uint64_t> DroppedSections;
  DenseSet<uint64_t> DroppedSymbols;
  for (const PluginObjectSection &Section : Object.sections())
    if (Section.Name == AndroidKernelProfileContract::NativeSection)
      DroppedSections.insert(Section.ID);
  for (const PluginObjectSymbol &Symbol : Object.symbols())
    if (Symbol.Name == AndroidKernelProfileContract::NativeSymbol ||
        DroppedSections.contains(Symbol.SectionID))
      DroppedSymbols.insert(Symbol.ID);

  if (DroppedSections.empty() && DroppedSymbols.empty())
    return Error::success();

  for (const PluginObjectRelocation &Relocation : Object.relocations()) {
    const bool AppliedInDroppedSection =
        DroppedSections.contains(Relocation.SectionID);
    const bool TargetsDroppedEntity =
        DroppedSections.contains(Relocation.TargetSectionID) ||
        DroppedSymbols.contains(Relocation.TargetSymbolID);
    if (!AppliedInDroppedSection && TargetsDroppedEntity)
      return createStringError(
          errc::invalid_argument,
          Boundary + ": retained section references the native Android kernel "
                     "profile contract being removed");
  }

  Object.relocations().remove_if([&](const PluginObjectRelocation &Relocation) {
    return DroppedSections.contains(Relocation.SectionID);
  });
  Object.symbols().remove_if([&](const PluginObjectSymbol &Symbol) {
    return DroppedSymbols.contains(Symbol.ID);
  });
  Object.sections().remove_if([&](const PluginObjectSection &Section) {
    return DroppedSections.contains(Section.ID);
  });
  Object.advanceGeneration();
  return Error::success();
}

Error forbidAndroidKernelProfileContract(const PluginObjectGraph &Object,
                                         StringRef Boundary) {
  for (const PluginObjectSection &Section : Object.sections())
    if (Section.Name == AndroidKernelProfileContract::NativeSection)
      return createStringError(
          errc::invalid_argument,
          Boundary + ": must not retain native Android kernel profile contract "
                     "section");
  for (const PluginObjectSymbol &Symbol : Object.symbols())
    if (Symbol.Name == AndroidKernelProfileContract::NativeSymbol)
      return createStringError(
          errc::invalid_argument,
          Boundary + ": must not retain native Android kernel profile contract "
                     "symbol");
  return Error::success();
}

Error forbidAndroidKernelProfileContract(ArrayRef<uint8_t> Image,
                                         StringRef Boundary) {
  auto Object = object::ObjectFile::createObjectFile(MemoryBufferRef(
      StringRef(reinterpret_cast<const char *>(Image.data()), Image.size()),
      Boundary));
  if (!Object)
    return Object.takeError();
  if (!(*Object)->isELF() || !(*Object)->isLittleEndian())
    return createStringError(errc::invalid_argument,
                             Boundary +
                                 ": expected a little-endian ELF object");

  for (const object::SectionRef &Section : (*Object)->sections()) {
    auto Name = Section.getName();
    if (!Name)
      return Name.takeError();
    if (*Name == AndroidKernelProfileContract::NativeSection)
      return createStringError(
          errc::invalid_argument,
          Boundary + ": must not retain native Android kernel profile contract "
                     "section");
  }
  for (const object::SymbolRef &Symbol : (*Object)->symbols()) {
    auto Name = Symbol.getName();
    if (!Name)
      return Name.takeError();
    if (*Name == AndroidKernelProfileContract::NativeSymbol)
      return createStringError(
          errc::invalid_argument,
          Boundary + ": must not retain native Android kernel profile contract "
                     "symbol");
  }
  return Error::success();
}

} // namespace neverc::plugin
