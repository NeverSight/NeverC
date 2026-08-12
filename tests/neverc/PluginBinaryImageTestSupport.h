#ifndef NEVERC_TESTS_PLUGINBINARYIMAGETESTSUPPORT_H
#define NEVERC_TESTS_PLUGINBINARYIMAGETESTSUPPORT_H

#include "Link/BinaryImage.h"
#include "Link/LinkPhaseExecutor.h"
#include "PluginLinkTestSupport.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "gtest/gtest.h"
#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace neverc::plugin::test_support {

namespace plugin_binary_image_detail {

inline llvm::Expected<const NevercIOAPI *> ioAPI(LinkTaskScope &Scope) {
  auto Query = Scope.services().interfaces().query(
      ioPluginInterfaceID(), NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR);
  if (!Query)
    return Query.takeError();
  return static_cast<const NevercIOAPI *>(Query->Table);
}

} // namespace plugin_binary_image_detail

inline llvm::Expected<std::shared_ptr<PluginBinaryImage>>
makeImage(LinkTaskScope &Scope, uint64_t Budget = UINT64_C(8192)) {
  auto Target = makeTargetKey();
  if (!Target)
    return Target.takeError();
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_THUNKS_RELAXED);
  GraphEntities Entities = populateValidGraph(*Graph);
  Graph->findAtom(Entities.AtomID)->Flags |= NEVERC_LINK_ATOM_LIVE;
  PluginLinkConstraint FileBase;
  FileBase.Kind = "file-base";
  FileBase.Value = 0;
  FileBase.Required = true;
  Graph->addConstraint(std::move(FileBase));
  auto LinkPipeline = LinkPhasePipeline::create(Scope.task());
  if (!LinkPipeline)
    return LinkPipeline.takeError();
  auto Relocated =
      (*LinkPipeline)->execute(Graph, NEVERC_LINK_STATE_RELOCATIONS_APPLIED);
  if (!Relocated)
    return Relocated.takeError();

  static std::atomic<uint64_t> NextOutput{1};
  std::string Name = "binary-image-" + std::to_string(NextOutput.fetch_add(1));
  NevercOutputSinkHandle Sink{};
  auto IO = plugin_binary_image_detail::ioAPI(Scope);
  if (!IO)
    return IO.takeError();
  NevercStatus Status =
      (*IO)->BeginMemoryOutput((*IO)->Context, Scope.task().handle(),
                               {Name.data(), Name.size()}, Budget, &Sink);
  if (!neverc_status_is_ok(Status))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "could not create BinaryImage output sink");
  return PluginBinaryImage::emit(Scope.task(), **IO, Sink, **Relocated);
}

struct TemporaryDirectory {
  llvm::SmallString<128> Path;

  TemporaryDirectory() {
    EXPECT_FALSE(llvm::sys::fs::createUniqueDirectory(
        "neverc-plugin-binary-image", Path));
  }

  ~TemporaryDirectory() {
    [[maybe_unused]] const std::error_code RemoveError =
        llvm::sys::fs::remove_directories(Path);
  }

  std::string file(llvm::StringRef Name) const {
    llvm::SmallString<160> Result(Path);
    llvm::sys::path::append(Result, Name);
    return Result.str().str();
  }
};

inline std::string readFile(llvm::StringRef Path) {
  auto Buffer = llvm::MemoryBuffer::getFile(Path);
  if (!Buffer)
    return {};
  return (*Buffer)->getBuffer().str();
}

inline void writeFile(llvm::StringRef Path, llvm::StringRef Contents) {
  std::ofstream Stream(Path.str(), std::ios::binary);
  Stream.write(Contents.data(), Contents.size());
}

} // namespace neverc::plugin::test_support

#endif
