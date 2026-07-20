#ifndef NEVERC_PLUGIN_HOST_MCEMISSIONPLAN_H
#define NEVERC_PLUGIN_HOST_MCEMISSIONPLAN_H

#include "llvm/MC/MCEmissionObserver.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace neverc::plugin {

class PluginProcessServices;
class PluginTaskContext;

class MCEmissionPlan final : public llvm::MCEmissionObserver {
public:
  static llvm::Expected<std::unique_ptr<MCEmissionPlan>>
  create(PluginTaskContext &Task);

  ~MCEmissionPlan() override;
  bool empty() const;

protected:
  llvm::Error onUnitBegin(llvm::MCStreamer &Streamer) override;
  llvm::Error onUnitEnd(llvm::MCStreamer &Streamer) override;
  llvm::Error onSectionChange(llvm::MCStreamer &Streamer,
                              const llvm::MCSection &Section,
                              const llvm::MCExpr *Subsection) override;
  llvm::Expected<llvm::MCInst>
  onPreInstruction(llvm::MCStreamer &Streamer,
                   const llvm::MCInst &Instruction,
                   const llvm::MCSubtargetInfo &STI) override;
  llvm::Error onPostInstruction(
      llvm::MCStreamer &Streamer, const llvm::MCInst &Instruction,
      const llvm::MCSubtargetInfo &STI) override;
  llvm::Error onPostEncode(llvm::MCContext &Context,
                           const llvm::MCInst &Instruction,
                           llvm::StringRef Bytes,
                           llvm::ArrayRef<llvm::MCFixup> Fixups) override;
  llvm::Error onFixup(llvm::MCAssembler &Assembler,
                      const llvm::MCAsmLayout &Layout,
                      const llvm::MCFragment &Fragment,
                      const llvm::MCFixup &Fixup, uint64_t Value,
                      bool IsResolved) override;
  llvm::Error onRelaxationRound(llvm::MCAssembler &Assembler,
                                const llvm::MCAsmLayout &Layout,
                                unsigned Round, bool Changed) override;
  llvm::Error onPreLayout(llvm::MCAssembler &Assembler) override;
  llvm::Error onPostLayout(llvm::MCAssembler &Assembler,
                           const llvm::MCAsmLayout &Layout) override;
  llvm::Error onPreObjectWrite(
      llvm::MCAssembler &Assembler,
      const llvm::MCAsmLayout &Layout) override;

private:
  struct Impl;
  explicit MCEmissionPlan(std::unique_ptr<Impl> State);

  std::unique_ptr<Impl> State;
};

llvm::Error
registerPluginMCEmissionInterface(PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
