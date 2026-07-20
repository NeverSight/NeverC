#ifndef NEVERC_PLUGIN_HOST_MCENCODERREGISTRY_H
#define NEVERC_PLUGIN_HOST_MCENCODERREGISTRY_H

#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/PluginMC.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class MCCodeEmitter;
class MCExpr;
class MCSubtargetInfo;
}

namespace neverc::plugin {

class PluginModule;
class PluginTaskContext;

struct MCCodecRegistrationView {
  llvm::StringRef PluginID;
  std::shared_ptr<const PluginModule> Owner;
  llvm::ArrayRef<NevercMCEncoderDescriptor> Encoders;
  llvm::ArrayRef<NevercMCDecoderDescriptor> Decoders;
};

struct EncodedMCFixup {
  uint64_t Offset = 0;
  uint32_t Width = 0;
  bool IsPCRelative = false;
  bool IsSigned = false;
  bool MayRelax = false;
  NevercMCFixupKind Kind = NEVERC_MC_FIXUP_NONE;
  uint32_t TargetKind = 0;
  uint32_t OperandIndex = NEVERC_MC_NO_FIXUP_OPERAND;
  int64_t Addend = 0;
  const llvm::MCExpr *NativeExpression = nullptr;
};

struct EncodedMCInstruction {
  std::vector<uint8_t> Bytes;
  std::vector<EncodedMCFixup> Fixups;
};

struct DecodedMCInstruction {
  NevercMCDecodeOutcome Outcome = NEVERC_MC_DECODE_UNKNOWN;
  uint64_t ConsumedBytes = 0;
  std::unique_ptr<llvm::MCInst> Instruction;
};

class MCEncoderRegistry {
public:
  struct EncoderRecord {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    NevercInterfaceID ProviderID{};
    NevercTargetID TargetID{};
    NevercInterfaceID SchemaID{};
    uint32_t MaximumInstructionLength = 0;
    NevercMCEncodeInstructionFn EncodeInstruction = nullptr;
    void *UserData = nullptr;
  };

  struct DecoderRecord {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    NevercInterfaceID ProviderID{};
    NevercTargetID TargetID{};
    NevercInterfaceID SchemaID{};
    uint32_t MaximumInstructionLength = 0;
    NevercMCDecodeInstructionFn DecodeInstruction = nullptr;
    void *UserData = nullptr;
  };

  static llvm::Expected<std::shared_ptr<const MCEncoderRegistry>>
  freeze(llvm::ArrayRef<MCCodecRegistrationView> Registrations,
         const PluginTargetSnapshot &Targets);
  static llvm::Expected<std::shared_ptr<const MCEncoderRegistry>>
  freeze(llvm::ArrayRef<std::shared_ptr<const PluginModule>> Modules,
         const PluginTargetSnapshot &Targets);

  const EncoderRecord *findEncoder(NevercTargetID Target,
                                   NevercInterfaceID Schema) const;
  const DecoderRecord *findDecoder(NevercTargetID Target,
                                   NevercInterfaceID Schema) const;
  size_t encoderCount() const { return Encoders.size(); }
  size_t decoderCount() const { return Decoders.size(); }

private:
  std::vector<EncoderRecord> Encoders;
  std::vector<DecoderRecord> Decoders;
};

class PluginMCCodeEmitter {
public:
  static llvm::Expected<std::unique_ptr<PluginMCCodeEmitter>>
  create(std::shared_ptr<const MCEncoderRegistry> Registry,
         std::shared_ptr<const PluginTargetSnapshot> Targets,
         NevercTargetID Target);
  ~PluginMCCodeEmitter();

  llvm::Expected<EncodedMCInstruction>
  encode(PluginTaskContext &Task, const llvm::MCInst &Instruction,
         uint64_t Address,
         llvm::ArrayRef<llvm::StringRef> Features) const;

private:
  struct Impl;
  explicit PluginMCCodeEmitter(std::unique_ptr<Impl> State);
  std::unique_ptr<Impl> State;
};

class PluginMCDecoder {
public:
  static llvm::Expected<std::unique_ptr<PluginMCDecoder>>
  create(std::shared_ptr<const MCEncoderRegistry> Registry,
         std::shared_ptr<const PluginTargetSnapshot> Targets,
         NevercTargetID Target);
  ~PluginMCDecoder();

  llvm::Expected<DecodedMCInstruction>
  decode(PluginTaskContext &Task, llvm::ArrayRef<uint8_t> Bytes,
         uint64_t Address,
         llvm::ArrayRef<llvm::StringRef> Features) const;

private:
  struct Impl;
  explicit PluginMCDecoder(std::unique_ptr<Impl> State);
  std::unique_ptr<Impl> State;
};

class BuiltinMCCodeEmitterAdapter {
public:
  static llvm::Expected<EncodedMCInstruction>
  encode(const llvm::MCCodeEmitter &Emitter,
         const llvm::MCInst &Instruction,
         const llvm::MCSubtargetInfo &Subtarget);
};

} // namespace neverc::plugin

#endif
