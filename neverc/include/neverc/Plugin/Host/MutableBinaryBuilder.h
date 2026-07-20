#ifndef NEVERC_PLUGIN_HOST_MUTABLEBINARYBUILDER_H
#define NEVERC_PLUGIN_HOST_MUTABLEBINARYBUILDER_H

#include "neverc/Plugin/PluginObject.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <vector>

namespace neverc::plugin {

class PluginTaskContext;

class MutableBinaryBuilder {
public:
  static llvm::Expected<std::unique_ptr<MutableBinaryBuilder>>
  create(PluginTaskContext &Task, const NevercIOAPI &IO,
         NevercOutputSinkHandle Sink);

  ~MutableBinaryBuilder();

  MutableBinaryBuilder(const MutableBinaryBuilder &) = delete;
  MutableBinaryBuilder &operator=(const MutableBinaryBuilder &) = delete;

  const NevercMutableBinaryAPI &api() const { return API; }
  NevercMutableBinaryBuilderHandle handle() const { return Handle; }

  llvm::Expected<NevercOutputSummary> summary() const;
  llvm::Expected<NevercOutputSeal> finish();
  NevercStatus abort();

private:
  MutableBinaryBuilder(PluginTaskContext &Task, const NevercIOAPI &IO,
                       NevercOutputSinkHandle Sink);
  static NevercStatus NEVERC_CALL reserve(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, uint64_t Size);
  static NevercStatus NEVERC_CALL write(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, NevercByteView Bytes);
  static NevercStatus NEVERC_CALL writeAt(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, uint64_t Offset,
      NevercByteView Bytes);
  static NevercStatus NEVERC_CALL tell(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, uint64_t *OutPosition);
  static NevercStatus NEVERC_CALL readAt(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, uint64_t Offset,
      NevercMutableByteView Bytes);
  static NevercStatus NEVERC_CALL insert(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, uint64_t Offset,
      NevercByteView Bytes);
  static NevercStatus NEVERC_CALL append(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, NevercByteView Bytes);
  static NevercStatus NEVERC_CALL resize(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, uint64_t Size);
  NevercStatus rewrite(NevercTaskHandle Task,
                       const std::vector<uint8_t> &Replacement);
  NevercStatus validate(NevercTaskHandle Task,
                        NevercMutableBinaryBuilderHandle Builder) const;

  PluginTaskContext &Task;
  const NevercIOAPI &IO;
  NevercOutputSinkHandle Sink{};
  NevercMutableBinaryAPI API{};
  NevercMutableBinaryBuilderHandle Handle{};
  std::vector<uint8_t> Bytes;
};

} // namespace neverc::plugin

#endif
