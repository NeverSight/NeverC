#include "PluginFrontendFuzzSupport.h"
#include "neverc/Plugin/Host/MCUnit.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::fuzz;
using namespace neverc::plugin;

namespace {

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

std::string name(ByteCursor &Input, unsigned Index) {
  return "fuzz." + std::to_string(Index) + "." +
         std::to_string(Input.takeU32());
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);
  PluginMCUnit Unit;
  Unit.setTargetIdentity(
      {Input.takeU64(), Input.takeU64()},
      std::string(64, static_cast<char>('0' + Input.takeByte() % 10)));

  const unsigned SectionCount =
      std::min<unsigned>(Input.takeByte(), 8);
  std::vector<PluginMCSection *> Sections;
  for (unsigned I = 0; I != SectionCount; ++I) {
    auto Section = std::make_unique<PluginMCSection>();
    Section->Name = name(Input, I);
    Section->Alignment = UINT64_C(1) << (Input.takeByte() % 8);
    Section->Flags = Input.takeU64();
    const unsigned FragmentCount =
        std::min<unsigned>(Input.takeByte(), 8);
    for (unsigned J = 0; J != FragmentCount; ++J) {
      auto Fragment = std::make_unique<PluginMCFragment>();
      Fragment->Parent = Section.get();
      Fragment->Kind =
          static_cast<NevercMCFragmentKind>(Input.takeU32());
      Fragment->ExplicitOffset =
          (Input.takeByte() & 1U) != 0
              ? NEVERC_MC_AUTOMATIC_OFFSET
              : Input.takeU64();
      Fragment->Alignment =
          UINT64_C(1) << (Input.takeByte() % 8);
      ArrayRef<uint8_t> Bytes = Input.takeBytes(64);
      Fragment->Contents.assign(Bytes.begin(), Bytes.end());
      Section->Fragments.push_back(std::move(Fragment));
    }
    Sections.push_back(Section.get());
    Unit.sections().push_back(std::move(Section));
  }

  const unsigned SymbolCount =
      std::min<unsigned>(Input.takeByte(), 16);
  for (unsigned I = 0; I != SymbolCount; ++I) {
    auto Symbol = std::make_unique<PluginMCSymbol>();
    Symbol->Name = name(Input, I);
    Symbol->Binding =
        static_cast<NevercMCSymbolBinding>(Input.takeU32());
    Symbol->Visibility =
        static_cast<NevercMCSymbolVisibility>(Input.takeU32());
    Symbol->Type = static_cast<NevercMCSymbolType>(Input.takeU32());
    Symbol->Definition =
        static_cast<NevercMCSymbolDefinition>(Input.takeU32());
    if (!Sections.empty() && (Input.takeByte() & 1U) != 0)
      Symbol->Section =
          Sections[Input.takeByte() % Sections.size()];
    Symbol->Value = Input.takeU64();
    Symbol->Size = Input.takeU64();
    Symbol->Alignment = Input.takeU64();
    Symbol->Flags = Input.takeU64();
    Unit.symbols().push_back(std::move(Symbol));
  }

  const unsigned InstructionCount =
      std::min<unsigned>(Input.takeByte(), 16);
  for (unsigned I = 0; I != InstructionCount; ++I) {
    auto Instruction = std::make_unique<MCInst>();
    Instruction->setOpcode(Input.takeU32());
    switch (Input.takeByte() % 3) {
    case 0:
      Instruction->addOperand(MCOperand::createImm(
          static_cast<int64_t>(Input.takeU64())));
      break;
    case 1:
      Instruction->addOperand(
          MCOperand::createReg(Input.takeU32() % 1024 + 1));
      break;
    default:
      break;
    }
    Unit.append(std::move(Instruction));
  }

  consume(verifyPluginMCUnit(Unit, nullptr));
  return 0;
}
