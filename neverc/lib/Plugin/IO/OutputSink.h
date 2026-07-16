#ifndef NEVERC_LIB_PLUGIN_IO_OUTPUTSINK_H
#define NEVERC_LIB_PLUGIN_IO_OUTPUTSINK_H

#include "neverc/Foundation/Core/OutputTransaction.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace neverc::plugin {

class PluginIOProcessBridge;
class PluginOutputState;
class PluginTaskContext;

void initializePluginOutputAPI(NevercIOAPI &API,
                               PluginIOProcessBridge &Bridge);

class PluginOutputState {
public:
  PluginOutputState(NevercTaskHandle Task,
                    std::shared_ptr<OutputTransaction> Transaction);

  NevercStatus write(llvm::ArrayRef<uint8_t> Bytes);
  NevercStatus writeAt(uint64_t Offset, llvm::ArrayRef<uint8_t> Bytes);
  NevercStatus tell(uint64_t *OutPosition) const;
  NevercStatus truncate(uint64_t Size);
  NevercStatus setMetadata(llvm::StringRef Key, llvm::StringRef Value);
  NevercStatus finish();
  NevercStatus abort();
  llvm::Expected<NevercOutputSummary> commit();

  NevercOutputSummary summary() const;
  NevercOutputSeal seal(NevercOutputSealHandle Handle) const;
  std::optional<PluginMemoryOutputSnapshot> memorySnapshot() const;

  NevercTaskHandle taskHandle() const { return Task; }
  llvm::StringRef logicalName() const {
    return Transaction->destination();
  }

private:
  static NevercStatus translate(OutputTransactionResult Result);
  static NevercOutputSummary
  translate(const OutputTransactionSummary &Summary);

  NevercTaskHandle Task{};
  std::shared_ptr<OutputTransaction> Transaction;
};

} // namespace neverc::plugin

#endif
