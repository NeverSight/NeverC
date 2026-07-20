#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCEmissionObserver.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;

namespace {

std::string errorText(Error E) {
  return toString(std::move(E)).str().str();
}

void initializeTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    InitializeAllTargetInfos();
    InitializeAllTargetMCs();
    InitializeAllTargets();
    InitializeAllAsmPrinters();
  });
}

unsigned findOpcode(const MCInstrInfo &MII, StringRef Name) {
  for (unsigned Opcode = 0; Opcode != MII.getNumOpcodes(); ++Opcode)
    if (MII.getName(Opcode) == Name)
      return Opcode;
  return MII.getNumOpcodes();
}

class RecordingEmissionObserver final : public MCEmissionObserver {
public:
  explicit RecordingEmissionObserver(unsigned ReplacementOpcodeValue)
      : ReplacementOpcode(ReplacementOpcodeValue) {}

  std::vector<MCEmissionEventKind> Events;
  std::vector<char> EncodedBytes;
  unsigned FinalOpcode = 0;
  bool FailPreInstruction = false;

protected:
  Error onUnitBegin(MCStreamer &) override {
    Events.push_back(MCEmissionEventKind::UnitBegin);
    return Error::success();
  }

  Error onUnitEnd(MCStreamer &) override {
    Events.push_back(MCEmissionEventKind::UnitEnd);
    return Error::success();
  }

  Error onSectionChange(MCStreamer &, const MCSection &,
                        const MCExpr *) override {
    Events.push_back(MCEmissionEventKind::SectionChange);
    return Error::success();
  }

  Expected<MCInst>
  onPreInstruction(MCStreamer &, const MCInst &Inst,
                   const MCSubtargetInfo &) override {
    Events.push_back(MCEmissionEventKind::PreInstruction);
    if (FailPreInstruction)
      return createStringError(inconvertibleErrorCode(),
                               "intentional emission callback failure");
    MCInst Replacement = Inst;
    Replacement.setOpcode(ReplacementOpcode);
    return Replacement;
  }

  Error onPostInstruction(MCStreamer &, const MCInst &Inst,
                          const MCSubtargetInfo &) override {
    Events.push_back(MCEmissionEventKind::PostInstruction);
    FinalOpcode = Inst.getOpcode();
    return Error::success();
  }

  Error onPostEncode(MCContext &, const MCInst &, StringRef Bytes,
                     ArrayRef<MCFixup>) override {
    Events.push_back(MCEmissionEventKind::PostEncode);
    EncodedBytes.assign(Bytes.begin(), Bytes.end());
    return Error::success();
  }

  Error onRelaxationRound(MCAssembler &, const MCAsmLayout &, unsigned,
                          bool) override {
    Events.push_back(MCEmissionEventKind::RelaxationRound);
    return Error::success();
  }

  Error onPreLayout(MCAssembler &) override {
    Events.push_back(MCEmissionEventKind::PreLayout);
    return Error::success();
  }

  Error onPostLayout(MCAssembler &, const MCAsmLayout &) override {
    Events.push_back(MCEmissionEventKind::PostLayout);
    return Error::success();
  }

  Error onPreObjectWrite(MCAssembler &, const MCAsmLayout &) override {
    Events.push_back(MCEmissionEventKind::PreObjectWrite);
    return Error::success();
  }

private:
  unsigned ReplacementOpcode;
};

struct EmissionResult {
  std::vector<char> Output;
  std::vector<MCEmissionEventKind> Events;
  std::vector<char> EncodedBytes;
  unsigned FinalOpcode = 0;
  unsigned ReplacementOpcode = 0;
  bool HadError = false;
};

Expected<EmissionResult>
emitX86(CodeGenFileType FileType, bool FailPreInstruction = false) {
  initializeTargets();
  Triple TargetTriple("x86_64-unknown-linux-gnu");
  std::string LookupError;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TargetTriple.str(), LookupError);
  if (!TheTarget)
    return createStringError(inconvertibleErrorCode(), LookupError);

  TargetOptions Options;
  Options.MCOptions.ShowMCEncoding = true;
  std::unique_ptr<TargetMachine> TM(TheTarget->createTargetMachine(
      TargetTriple.str(), "generic", "", Options, std::nullopt,
      CodeGenOptLevel::None));
  if (!TM)
    return createStringError(inconvertibleErrorCode(),
                             "failed to create x86 test target machine");
  auto *LLVMTarget = static_cast<LLVMTargetMachine *>(TM.get());

  const MCInstrInfo &MII = *TM->getMCInstrInfo();
  const unsigned OriginalOpcode = findOpcode(MII, "NOOP");
  const unsigned ReplacementOpcode = findOpcode(MII, "RET64");
  if (OriginalOpcode == MII.getNumOpcodes() ||
      ReplacementOpcode == MII.getNumOpcodes())
    return createStringError(inconvertibleErrorCode(),
                             "required x86 test opcodes are unavailable");

  MCContext Context(TargetTriple, TM->getMCAsmInfo(),
                    TM->getMCRegisterInfo(),
                    TM->getMCSubtargetInfo());
  MCObjectFileInfo ObjectInfo;
  ObjectInfo.initMCObjectFileInfo(Context);
  Context.setObjectFileInfo(&ObjectInfo);
  RecordingEmissionObserver Observer(ReplacementOpcode);
  Observer.FailPreInstruction = FailPreInstruction;
  Context.setEmissionObserver(&Observer);

  SmallVector<char, 0> Storage;
  raw_svector_ostream Output(Storage);
  auto Stream =
      LLVMTarget->createMCStreamer(Output, nullptr, FileType, Context);
  if (!Stream)
    return Stream.takeError();
  (*Stream)->initSections(false, *TM->getMCSubtargetInfo());
  MCInst Instruction;
  Instruction.setOpcode(OriginalOpcode);
  (*Stream)->emitInstruction(Instruction, *TM->getMCSubtargetInfo());
  (*Stream)->finish();

  EmissionResult Result;
  Result.Output.assign(Storage.begin(), Storage.end());
  Result.Events = Observer.Events;
  Result.EncodedBytes = Observer.EncodedBytes;
  Result.FinalOpcode = Observer.FinalOpcode;
  Result.ReplacementOpcode = ReplacementOpcode;
  Result.HadError = Context.hadError();
  return Result;
}

size_t eventIndex(ArrayRef<MCEmissionEventKind> Events,
                  MCEmissionEventKind Event) {
  auto It = std::find(Events.begin(), Events.end(), Event);
  return static_cast<size_t>(std::distance(Events.begin(), It));
}

TEST(PluginMCEmissionHookTest,
     ObjectEmissionMutatesInstructionAndReportsOrderedEvents) {
  auto Result = emitX86(CodeGenFileType::ObjectFile);
  ASSERT_TRUE(static_cast<bool>(Result))
      << errorText(Result.takeError());
  ASSERT_FALSE(Result->HadError);
  EXPECT_EQ(Result->FinalOpcode, Result->ReplacementOpcode);
  ASSERT_EQ(Result->EncodedBytes.size(), 1U);
  EXPECT_EQ(static_cast<unsigned char>(Result->EncodedBytes.front()), 0xc3U);

  const auto &Events = Result->Events;
  ASSERT_FALSE(Events.empty());
  EXPECT_EQ(Events.front(), MCEmissionEventKind::UnitBegin);
  EXPECT_EQ(Events.back(), MCEmissionEventKind::UnitEnd);
  EXPECT_LT(eventIndex(Events, MCEmissionEventKind::SectionChange),
            eventIndex(Events, MCEmissionEventKind::PreInstruction));
  EXPECT_LT(eventIndex(Events, MCEmissionEventKind::PreInstruction),
            eventIndex(Events, MCEmissionEventKind::PostEncode));
  EXPECT_LT(eventIndex(Events, MCEmissionEventKind::PostEncode),
            eventIndex(Events, MCEmissionEventKind::PostInstruction));
  EXPECT_LT(eventIndex(Events, MCEmissionEventKind::PostInstruction),
            eventIndex(Events, MCEmissionEventKind::PreLayout));
  EXPECT_LT(eventIndex(Events, MCEmissionEventKind::PreLayout),
            eventIndex(Events, MCEmissionEventKind::RelaxationRound));
  EXPECT_LT(eventIndex(Events, MCEmissionEventKind::RelaxationRound),
            eventIndex(Events, MCEmissionEventKind::PostLayout));
  EXPECT_LT(eventIndex(Events, MCEmissionEventKind::PostLayout),
            eventIndex(Events, MCEmissionEventKind::PreObjectWrite));
}

TEST(PluginMCEmissionHookTest,
     AssemblyEmissionUsesTheSameInstructionInterceptor) {
  auto Result = emitX86(CodeGenFileType::AssemblyFile);
  ASSERT_TRUE(static_cast<bool>(Result))
      << errorText(Result.takeError());
  ASSERT_FALSE(Result->HadError);
  EXPECT_EQ(Result->FinalOpcode, Result->ReplacementOpcode);
  EXPECT_NE(std::string(Result->Output.begin(), Result->Output.end())
                .find("retq"),
            std::string::npos);
  EXPECT_LT(eventIndex(Result->Events,
                       MCEmissionEventKind::PreInstruction),
            eventIndex(Result->Events,
                       MCEmissionEventKind::PostInstruction));
}

TEST(PluginMCEmissionHookTest,
     CallbackFailureStopsObjectWriterAndMarksTheContext) {
  auto Result = emitX86(CodeGenFileType::ObjectFile, true);
  ASSERT_TRUE(static_cast<bool>(Result))
      << errorText(Result.takeError());
  EXPECT_TRUE(Result->HadError);
  EXPECT_TRUE(Result->Output.empty());
  EXPECT_EQ(Result->Events.back(), MCEmissionEventKind::UnitEnd);
  EXPECT_EQ(std::count(Result->Events.begin(), Result->Events.end(),
                       MCEmissionEventKind::PostInstruction),
            0);
}

} // namespace
