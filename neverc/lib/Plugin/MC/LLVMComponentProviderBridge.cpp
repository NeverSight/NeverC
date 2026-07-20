#include "neverc/Plugin/Host/LLVMComponentProviderBridge.h"
#include "neverc/Plugin/Host/MCEncoderRegistry.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SMLoc.h"
#include <limits>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error componentError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

Expected<MCFixupKind> mapFixupKind(
    const EncodedMCFixup &Fixup) {
  if (Fixup.Kind == NEVERC_MC_FIXUP_TARGET_EXTENSION) {
    if (Fixup.TargetKind < FirstTargetFixupKind ||
        Fixup.TargetKind > MaxFixupKind)
      return componentError(
          "plugin encoder returned an invalid target fixup kind");
    return static_cast<MCFixupKind>(Fixup.TargetKind);
  }
  switch (Fixup.Kind) {
#define NEVERC_MC_SCHEMA_FIXUP(LLVMName, Name, Stable, Backend, Text, Ext) \
  case Stable:                                                            \
    return static_cast<MCFixupKind>(Backend);
#include "neverc/Plugin/Schema/PluginMCSchema.inc"
#undef NEVERC_MC_SCHEMA_FIXUP
  default:
    return componentError(
        "plugin encoder returned an unknown fixup kind");
  }
}

Expected<const MCExpr *>
makeFixupExpression(const MCInst &Instruction,
                    const EncodedMCFixup &Fixup,
                    MCContext &Context) {
  const MCExpr *Expression = Fixup.NativeExpression;
  int64_t Constant = Fixup.Addend;
  if (Fixup.OperandIndex != NEVERC_MC_NO_FIXUP_OPERAND) {
    if (Fixup.OperandIndex >= Instruction.getNumOperands())
      return componentError(
          "plugin encoder fixup references an invalid operand");
    const MCOperand &Operand =
        Instruction.getOperand(Fixup.OperandIndex);
    if (Operand.isExpr())
      Expression = Operand.getExpr();
    else if (Operand.isImm()) {
      if (AddOverflow(Constant, Operand.getImm(), Constant))
        return componentError(
            "plugin encoder fixup addend overflows");
    } else {
      return componentError(
          "plugin encoder fixup operand is not an expression or immediate");
    }
  }
  if (!Expression)
    return MCConstantExpr::create(Constant, Context);
  if (Constant == 0)
    return Expression;
  return MCBinaryExpr::createAdd(
      Expression, MCConstantExpr::create(Constant, Context), Context);
}

class PluginCodeEmitterAdapter final : public MCCodeEmitter {
public:
  PluginCodeEmitterAdapter(
      PluginTaskContext &TaskValue, MCContext &ContextValue,
      std::unique_ptr<PluginMCCodeEmitter> EmitterValue)
      : Task(TaskValue), Context(ContextValue),
        Emitter(std::move(EmitterValue)) {}

  void encodeInstruction(
      const MCInst &Instruction, SmallVectorImpl<char> &Buffer,
      SmallVectorImpl<MCFixup> &Fixups,
      const MCSubtargetInfo &Subtarget) const override {
    SmallVector<StringRef, 0> Features;
    auto Encoded =
        Emitter->encode(Task, Instruction, 0, Features);
    if (!Encoded) {
      Context.reportError(
          SMLoc(), toString(Encoded.takeError()));
      return;
    }
    Buffer.append(
        reinterpret_cast<const char *>(Encoded->Bytes.data()),
        reinterpret_cast<const char *>(Encoded->Bytes.data()) +
            Encoded->Bytes.size());
    for (const EncodedMCFixup &EncodedFixup : Encoded->Fixups) {
      auto Kind = mapFixupKind(EncodedFixup);
      if (!Kind) {
        Context.reportError(SMLoc(), toString(Kind.takeError()));
        return;
      }
      auto Expression = makeFixupExpression(
          Instruction, EncodedFixup, Context);
      if (!Expression) {
        Context.reportError(
            SMLoc(), toString(Expression.takeError()));
        return;
      }
      Fixups.push_back(MCFixup::create(
          static_cast<uint32_t>(EncodedFixup.Offset), *Expression,
          *Kind));
    }
  }

private:
  PluginTaskContext &Task;
  MCContext &Context;
  std::unique_ptr<PluginMCCodeEmitter> Emitter;
};

} // namespace

struct LLVMComponentProviderBridge::Impl {
  PluginTaskContext &Task;
  std::shared_ptr<const PluginTargetSnapshot> Targets;
  std::shared_ptr<const MCEncoderRegistry> Codecs;
  NevercTargetID TargetID{};
  NevercInterfaceID SchemaID{};
  bool ReplaceEmitter = false;
};

LLVMComponentProviderBridge::LLVMComponentProviderBridge(
    std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

LLVMComponentProviderBridge::~LLVMComponentProviderBridge() = default;

Expected<std::unique_ptr<LLVMComponentProviderBridge>>
LLVMComponentProviderBridge::create(
    PluginTaskContext &Task,
    std::shared_ptr<const PluginTargetSnapshot> Targets) {
  if (!Targets)
    return componentError(
        "LLVM component bridge has no target snapshot");
  auto State = std::make_unique<Impl>(
      Impl{Task, std::move(Targets), {}, {}, {}, false});
  const auto *Target = State->Targets->selectedTarget();
  if (!Target)
    return std::unique_ptr<LLVMComponentProviderBridge>(
        new LLVMComponentProviderBridge(std::move(State)));
  auto Codecs = MCEncoderRegistry::freeze(
      Task.session().plugins(), *State->Targets);
  if (!Codecs)
    return Codecs.takeError();
  State->TargetID = Target->ID;
  State->SchemaID = Target->MCSchemaID;
  State->ReplaceEmitter =
      (*Codecs)->findEncoder(Target->ID, Target->MCSchemaID) != nullptr;
  State->Codecs = std::move(*Codecs);
  return std::unique_ptr<LLVMComponentProviderBridge>(
      new LLVMComponentProviderBridge(std::move(State)));
}

bool LLVMComponentProviderBridge::hasReplacements() const {
  return State->ReplaceEmitter;
}

Expected<std::unique_ptr<MCCodeEmitter>>
LLVMComponentProviderBridge::provideCodeEmitter(
    MCContext &Context,
    std::unique_ptr<MCCodeEmitter> Fallback) {
  if (!State->ReplaceEmitter)
    return std::move(Fallback);
  auto Emitter = PluginMCCodeEmitter::create(
      State->Codecs, State->Targets, State->TargetID);
  if (!Emitter)
    return Emitter.takeError();
  return std::unique_ptr<MCCodeEmitter>(
      new PluginCodeEmitterAdapter(
          State->Task, Context, std::move(*Emitter)));
}

} // namespace neverc::plugin
