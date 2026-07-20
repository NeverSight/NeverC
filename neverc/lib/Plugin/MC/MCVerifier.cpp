#include "neverc/Plugin/Host/MCUnit.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Errc.h"
#include <limits>
#include <unordered_map>
#include <unordered_set>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error invalid(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

bool isPowerOfTwo(uint64_t Value) {
  return Value != 0 && (Value & (Value - 1)) == 0;
}

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

bool knownExpressionKind(NevercMCExpressionKind Kind) {
  return Kind >= NEVERC_MC_EXPRESSION_BINARY &&
         Kind <= NEVERC_MC_EXPRESSION_TARGET_VARIANT;
}

bool knownFragmentKind(NevercMCFragmentKind Kind) {
  return Kind >= NEVERC_MC_FRAGMENT_ALIGN &&
         Kind <= NEVERC_MC_FRAGMENT_FORMAT_EXTENSION;
}

bool knownFixupKind(NevercMCFixupKind Kind) {
  return Kind >= NEVERC_MC_FIXUP_NONE &&
         Kind <= NEVERC_MC_FIXUP_TARGET_EXTENSION;
}

template <typename Records>
bool hasStableValue(const Records &Values, uint32_t Value) {
  return llvm::any_of(Values, [Value](const auto &Entry) {
    return Entry.StableID == Value;
  });
}

template <typename Records>
bool hasBackendValue(const Records &Values, uint32_t Value) {
  return llvm::any_of(Values, [Value](const auto &Entry) {
    return Entry.BackendValue == Value;
  });
}

Error verifyExpressionShape(const PluginMCUnit &Unit,
                            const PluginMCExpression &Expression,
                            const PluginTargetSnapshot::NamedRecord *Schema) {
  if (!knownExpressionKind(Expression.Kind))
    return invalid("MC expression uses an unknown stable kind");
  if (Expression.Left != nullptr && !Unit.contains(Expression.Left))
    return invalid("MC expression left operand belongs to another unit");
  if (Expression.Right != nullptr && !Unit.contains(Expression.Right))
    return invalid("MC expression right operand belongs to another unit");
  if (Expression.Symbol != nullptr && !Unit.contains(Expression.Symbol))
    return invalid("MC expression symbol belongs to another unit");

  switch (Expression.Kind) {
  case NEVERC_MC_EXPRESSION_CONSTANT:
    if (Expression.Left != nullptr || Expression.Right != nullptr ||
        Expression.Symbol != nullptr)
      return invalid("constant MC expression has operands");
    break;
  case NEVERC_MC_EXPRESSION_SYMBOL_REF:
    if (Expression.Symbol == nullptr || Expression.Left != nullptr ||
        Expression.Right != nullptr)
      return invalid("symbol-reference MC expression is malformed");
    break;
  case NEVERC_MC_EXPRESSION_UNARY:
    if (Expression.Left == nullptr || Expression.Right != nullptr ||
        Expression.Operator < NEVERC_MC_UNARY_PLUS ||
        Expression.Operator > NEVERC_MC_UNARY_NOT)
      return invalid("unary MC expression is malformed");
    break;
  case NEVERC_MC_EXPRESSION_BINARY:
    if (Expression.Left == nullptr || Expression.Right == nullptr ||
        Expression.Operator < NEVERC_MC_BINARY_ADD ||
        Expression.Operator > NEVERC_MC_BINARY_SHIFT_RIGHT)
      return invalid("binary MC expression is malformed");
    break;
  case NEVERC_MC_EXPRESSION_TARGET_VARIANT:
    if (Expression.Left == nullptr || Expression.Right != nullptr ||
        !nonzero(Expression.Extension.Owner))
      return invalid("target-variant MC expression lacks its owner or operand");
    if (Schema != nullptr && !Schema->Variants.empty() &&
        !hasStableValue(Schema->Variants, Expression.TargetVariant))
      return invalid("target-variant MC expression is absent from target schema");
    break;
  default:
    llvm_unreachable("known MC expression kind");
  }
  return Error::success();
}

Error visitExpression(
    const PluginMCExpression *Expression,
    std::unordered_map<const PluginMCExpression *, uint8_t> &Colors) {
  uint8_t &Color = Colors[Expression];
  if (Color == 1)
    return invalid("MC expression graph contains a cycle");
  if (Color == 2)
    return Error::success();
  Color = 1;
  if (Expression->Left)
    if (Error Err = visitExpression(Expression->Left, Colors))
      return Err;
  if (Expression->Right)
    if (Error Err = visitExpression(Expression->Right, Colors))
      return Err;
  Color = 2;
  return Error::success();
}

Error verifyInstruction(const PluginMCUnit &Unit,
                        const MCInst &Instruction,
                        const PluginTargetSnapshot::NamedRecord *Schema) {
  if (Schema != nullptr && !Schema->Opcodes.empty() &&
      !hasBackendValue(Schema->Opcodes, Instruction.getOpcode()))
    return invalid("MC instruction opcode is absent from target schema");
  for (const MCOperand &Operand : Instruction) {
    if (Operand.isReg() && Schema != nullptr &&
        !Schema->SchemaRegisters.empty() &&
        !hasBackendValue(Schema->SchemaRegisters, Operand.getReg()))
      return invalid("MC register operand is absent from target schema");
    if (Operand.isExpr())
      return invalid(
          "raw LLVM MCExpr operand cannot appear in the stable MC model");
    if (Operand.isInst() && !Unit.contains(Operand.getInst()))
      return invalid("nested MC instruction belongs to another unit");
  }
  return Error::success();
}

} // namespace

Error verifyPluginMCUnit(
    const PluginMCUnit &Unit,
    const PluginTargetSnapshot::NamedRecord *Schema) {
  if (Schema != nullptr) {
    NevercTargetID TargetID = Unit.targetID();
    if ((TargetID.High != 0 || TargetID.Low != 0) &&
        (TargetID.High != Schema->TargetID.High ||
         TargetID.Low != Schema->TargetID.Low))
      return invalid("MC unit target does not match its target schema");
    if (!Unit.targetSchemaDigest().empty() &&
        Unit.targetSchemaDigest() != Schema->Digest)
      return invalid("MC unit schema digest does not match target schema");
  }

  std::unordered_set<std::string> SectionNames;
  for (const auto &Section : Unit.sections()) {
    if (Section->Name.empty())
      return invalid("MC section name must not be empty");
    if (!SectionNames.insert(Section->Name).second)
      return invalid("MC section names must be unique");
    if (!isPowerOfTwo(Section->Alignment))
      return invalid("MC section alignment must be a power of two");

    uint64_t PreviousEnd = 0;
    bool HasExplicitFragment = false;
    for (const auto &Fragment : Section->Fragments) {
      if (Fragment->Parent != Section.get())
        return invalid("MC fragment parent does not match its section");
      if (!knownFragmentKind(Fragment->Kind))
        return invalid("MC fragment uses an unknown stable kind");
      if (!isPowerOfTwo(Fragment->Alignment))
        return invalid("MC fragment alignment must be a power of two");
      if (Fragment->Kind == NEVERC_MC_FRAGMENT_FORMAT_EXTENSION &&
          !nonzero(Fragment->Extension.Owner))
        return invalid("format-extension MC fragment lacks an owner");
      if (Fragment->ExplicitOffset != NEVERC_MC_AUTOMATIC_OFFSET) {
        if (HasExplicitFragment &&
            Fragment->ExplicitOffset < PreviousEnd)
          return invalid("MC fragment ranges overlap");
        if (Fragment->Contents.size() >
            std::numeric_limits<uint64_t>::max() -
                Fragment->ExplicitOffset)
          return invalid("MC fragment range overflows");
        PreviousEnd =
            Fragment->ExplicitOffset + Fragment->Contents.size();
        HasExplicitFragment = true;
      }

      for (const auto &Instruction : Fragment->Instructions)
        if (Error Err = verifyInstruction(Unit, *Instruction, Schema))
          return Err;
      for (const auto &Fixup : Fragment->Fixups) {
        if (Fixup->Parent != Fragment.get())
          return invalid("MC fixup parent does not match its fragment");
        if (!knownFixupKind(Fixup->Kind) || Fixup->Width == 0)
          return invalid("MC fixup kind or width is invalid");
        if (Fixup->Expression == nullptr ||
            !Unit.contains(Fixup->Expression))
          return invalid("MC fixup expression is missing or foreign");
        if (Fixup->Kind == NEVERC_MC_FIXUP_TARGET_EXTENSION) {
          if (Schema == nullptr || Schema->Relocations.empty() ||
              !hasStableValue(Schema->Relocations, Fixup->TargetKind))
            return invalid("target MC fixup is absent from target schema");
        }
        uint64_t ByteWidth = (Fixup->Width + 7) / 8;
        if (!Fragment->Contents.empty() &&
            (Fixup->Offset > Fragment->Contents.size() ||
             ByteWidth > Fragment->Contents.size() - Fixup->Offset))
          return invalid("MC fixup range exceeds fragment contents");
      }
    }
  }

  std::unordered_set<std::string> SymbolNames;
  for (const auto &Symbol : Unit.symbols()) {
    if (Symbol->Name.empty())
      return invalid("MC symbol name must not be empty");
    if (!SymbolNames.insert(Symbol->Name).second)
      return invalid("MC symbol names must be unique");
    if (!isPowerOfTwo(Symbol->Alignment))
      return invalid("MC symbol alignment must be a power of two");
    if (Symbol->Section != nullptr && !Unit.contains(Symbol->Section))
      return invalid("MC symbol section belongs to another unit");
    if (Symbol->Definition == NEVERC_MC_SYMBOL_DEFINITION_SECTION &&
        Symbol->Section == nullptr)
      return invalid("section-defined MC symbol has no section");
    if (Symbol->Definition != NEVERC_MC_SYMBOL_DEFINITION_SECTION &&
        Symbol->Section != nullptr)
      return invalid("non-section MC symbol unexpectedly has a section");
  }

  std::unordered_map<const PluginMCExpression *, uint8_t> Colors;
  for (const auto &Expression : Unit.expressions()) {
    if (Error Err = verifyExpressionShape(Unit, *Expression, Schema))
      return Err;
    if (Error Err = visitExpression(Expression.get(), Colors))
      return Err;
  }

  for (const auto &Instruction : Unit.instructions())
    if (Error Err = verifyInstruction(Unit, *Instruction, Schema))
      return Err;
  return Error::success();
}

} // namespace neverc::plugin
