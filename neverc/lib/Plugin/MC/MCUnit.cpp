#include "neverc/Plugin/Host/MCUnit.h"
#include "llvm/ADT/STLExtras.h"
#include <sstream>

using namespace llvm;

namespace neverc::plugin {
namespace {

template <typename Storage, typename Value>
bool containsPointer(const Storage &Values, const Value *Needle) {
  return llvm::any_of(Values, [Needle](const auto &Entry) {
    return Entry.get() == Needle;
  });
}

} // namespace

MCInst &PluginMCUnit::append(std::unique_ptr<MCInst> Instruction) {
  Instructions.push_back(std::move(Instruction));
  return *Instructions.back();
}

MCInst *PluginMCUnit::at(size_t Index) {
  auto It = Instructions.begin();
  while (It != Instructions.end() && Index != 0) {
    ++It;
    --Index;
  }
  return It == Instructions.end() ? nullptr : It->get();
}

const MCInst *PluginMCUnit::at(size_t Index) const {
  auto It = Instructions.begin();
  while (It != Instructions.end() && Index != 0) {
    ++It;
    --Index;
  }
  return It == Instructions.end() ? nullptr : It->get();
}

size_t PluginMCUnit::fragmentCount() const {
  size_t Count = 0;
  for (const auto &Section : Sections)
    Count += Section->Fragments.size();
  return Count;
}

size_t PluginMCUnit::instructionCount() const {
  size_t Count = Instructions.size();
  for (const auto &Section : Sections)
    for (const auto &Fragment : Section->Fragments)
      Count += Fragment->Instructions.size();
  return Count;
}

size_t PluginMCUnit::fixupCount() const {
  size_t Count = 0;
  for (const auto &Section : Sections)
    for (const auto &Fragment : Section->Fragments)
      Count += Fragment->Fixups.size();
  return Count;
}

void PluginMCUnit::setTargetIdentity(NevercTargetID ID,
                                     std::string Digest) {
  TargetID = ID;
  SchemaDigest = std::move(Digest);
}

bool PluginMCUnit::contains(const PluginMCSection *Section) const {
  return containsPointer(Sections, Section);
}

bool PluginMCUnit::contains(const PluginMCSymbol *Symbol) const {
  return containsPointer(Symbols, Symbol);
}

bool PluginMCUnit::contains(const PluginMCExpression *Expression) const {
  return containsPointer(Expressions, Expression);
}

bool PluginMCUnit::contains(const PluginMCFragment *Fragment) const {
  for (const auto &Section : Sections)
    if (containsPointer(Section->Fragments, Fragment))
      return true;
  return false;
}

bool PluginMCUnit::contains(const PluginMCFixup *Fixup) const {
  for (const auto &Section : Sections)
    for (const auto &Fragment : Section->Fragments)
      if (containsPointer(Fragment->Fixups, Fixup))
        return true;
  return false;
}

bool PluginMCUnit::contains(const MCInst *Instruction) const {
  if (containsPointer(Instructions, Instruction))
    return true;
  for (const auto &Section : Sections)
    for (const auto &Fragment : Section->Fragments)
      if (containsPointer(Fragment->Instructions, Instruction))
        return true;
  return false;
}

std::string dumpPluginMCUnit(const PluginMCUnit &Unit) {
  std::ostringstream Stream;
  Stream << "target=" << std::hex << Unit.targetID().High << ":"
         << Unit.targetID().Low << std::dec
         << " schema=" << Unit.targetSchemaDigest() << "\n";
  for (const auto &Section : Unit.sections()) {
    Stream << "section " << Section->Name << " align="
           << Section->Alignment << " flags=" << Section->Flags << "\n";
    for (const auto &Symbol : Unit.symbols())
      if (Symbol->Section == Section.get())
        Stream << "  symbol " << Symbol->Name << " value="
               << Symbol->Value << " size=" << Symbol->Size << "\n";
    for (const auto &Fragment : Section->Fragments) {
      Stream << "  fragment kind=" << Fragment->Kind << " offset="
             << Fragment->ExplicitOffset << " bytes="
             << Fragment->Contents.size() << "\n";
      for (const auto &Instruction : Fragment->Instructions)
        Stream << "    inst opcode=" << Instruction->getOpcode()
               << " operands=" << Instruction->getNumOperands() << "\n";
      for (const auto &Fixup : Fragment->Fixups)
        Stream << "    fixup offset=" << Fixup->Offset
               << " width=" << Fixup->Width << " kind=" << Fixup->Kind
               << "\n";
    }
  }
  for (const auto &Instruction : Unit.instructions())
    Stream << "inst opcode=" << Instruction->getOpcode()
           << " operands=" << Instruction->getNumOperands() << "\n";
  return Stream.str();
}

std::string dumpLLVMCompatibleMCUnit(const PluginMCUnit &Unit) {
  std::ostringstream Stream;
  for (const auto &Section : Unit.sections()) {
    Stream << ".section " << Section->Name << "\n";
    for (const auto &Symbol : Unit.symbols())
      if (Symbol->Section == Section.get())
        Stream << Symbol->Name << ":\n";
    for (const auto &Fragment : Section->Fragments) {
      if (!Fragment->Contents.empty()) {
        Stream << "  .bytes";
        for (uint8_t Byte : Fragment->Contents)
          Stream << " " << static_cast<unsigned>(Byte);
        Stream << "\n";
      }
      for (const auto &Instruction : Fragment->Instructions) {
        Stream << "  opcode " << Instruction->getOpcode();
        for (const MCOperand &Operand : *Instruction) {
          if (Operand.isReg())
            Stream << " reg:" << Operand.getReg();
          else if (Operand.isImm())
            Stream << " imm:" << Operand.getImm();
        }
        Stream << "\n";
      }
      for (const auto &Fixup : Fragment->Fixups)
        Stream << "  fixup " << Fixup->Offset << ":"
               << Fixup->Width << " kind:" << Fixup->Kind << "\n";
    }
  }
  return Stream.str();
}

} // namespace neverc::plugin
