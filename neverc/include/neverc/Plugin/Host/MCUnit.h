#ifndef NEVERC_PLUGIN_HOST_MCUNIT_H
#define NEVERC_PLUGIN_HOST_MCUNIT_H

#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/PluginMC.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace neverc::plugin {

struct PluginMCSection;
struct PluginMCSymbol;
struct PluginMCExpression;
struct PluginMCFragment;

struct PluginMCExtension {
  NevercInterfaceID Owner{};
  std::vector<uint8_t> Bytes;
};

struct PluginMCFixup {
  PluginMCFragment *Parent = nullptr;
  PluginMCExpression *Expression = nullptr;
  uint64_t Offset = 0;
  uint32_t Width = 0;
  bool IsPCRelative = false;
  bool IsSigned = false;
  bool MayRelax = false;
  NevercMCFixupKind Kind = NEVERC_MC_FIXUP_NONE;
  uint32_t TargetKind = 0;
};

struct PluginMCFragment {
  using InstructionStorage =
      std::list<std::unique_ptr<llvm::MCInst>>;
  using FixupStorage = std::list<std::unique_ptr<PluginMCFixup>>;

  PluginMCSection *Parent = nullptr;
  NevercMCFragmentKind Kind = NEVERC_MC_FRAGMENT_DATA;
  uint32_t FillValue = 0;
  uint64_t ExplicitOffset = NEVERC_MC_AUTOMATIC_OFFSET;
  uint64_t Alignment = 1;
  std::vector<uint8_t> Contents;
  PluginMCExtension Extension;
  InstructionStorage Instructions;
  FixupStorage Fixups;
};

struct PluginMCSection {
  using FragmentStorage =
      std::list<std::unique_ptr<PluginMCFragment>>;

  std::string Name;
  uint64_t Alignment = 1;
  NevercMCSectionFlags Flags = 0;
  FragmentStorage Fragments;
};

struct PluginMCSymbol {
  std::string Name;
  NevercMCSymbolBinding Binding = NEVERC_MC_SYMBOL_BINDING_LOCAL;
  NevercMCSymbolVisibility Visibility =
      NEVERC_MC_SYMBOL_VISIBILITY_DEFAULT;
  NevercMCSymbolType Type = NEVERC_MC_SYMBOL_TYPE_NONE;
  NevercMCSymbolDefinition Definition =
      NEVERC_MC_SYMBOL_DEFINITION_UNDEFINED;
  PluginMCSection *Section = nullptr;
  uint64_t Value = 0;
  uint64_t Size = 0;
  uint64_t Alignment = 1;
  uint64_t Flags = 0;
};

struct PluginMCExpression {
  NevercMCExpressionKind Kind = NEVERC_MC_EXPRESSION_CONSTANT;
  NevercMCExpressionOperator Operator = 0;
  int64_t Constant = 0;
  PluginMCSymbol *Symbol = nullptr;
  PluginMCExpression *Left = nullptr;
  PluginMCExpression *Right = nullptr;
  uint32_t TargetVariant = 0;
  PluginMCExtension Extension;
};

class PluginMCUnit {
public:
  using InstructionStorage =
      std::list<std::unique_ptr<llvm::MCInst>>;
  using SectionStorage =
      std::list<std::unique_ptr<PluginMCSection>>;
  using SymbolStorage = std::list<std::unique_ptr<PluginMCSymbol>>;
  using ExpressionStorage =
      std::list<std::unique_ptr<PluginMCExpression>>;

  llvm::MCInst &append(std::unique_ptr<llvm::MCInst> Instruction);
  size_t size() const { return Instructions.size(); }
  llvm::MCInst *at(size_t Index);
  const llvm::MCInst *at(size_t Index) const;
  InstructionStorage &instructions() { return Instructions; }
  const InstructionStorage &instructions() const { return Instructions; }

  SectionStorage &sections() { return Sections; }
  const SectionStorage &sections() const { return Sections; }
  SymbolStorage &symbols() { return Symbols; }
  const SymbolStorage &symbols() const { return Symbols; }
  ExpressionStorage &expressions() { return Expressions; }
  const ExpressionStorage &expressions() const { return Expressions; }

  size_t sectionCount() const { return Sections.size(); }
  size_t symbolCount() const { return Symbols.size(); }
  size_t expressionCount() const { return Expressions.size(); }
  size_t fragmentCount() const;
  size_t instructionCount() const;
  size_t fixupCount() const;

  void setTargetIdentity(NevercTargetID ID, std::string SchemaDigest);
  NevercTargetID targetID() const { return TargetID; }
  const std::string &targetSchemaDigest() const { return SchemaDigest; }

  bool contains(const PluginMCSection *Section) const;
  bool contains(const PluginMCSymbol *Symbol) const;
  bool contains(const PluginMCExpression *Expression) const;
  bool contains(const PluginMCFragment *Fragment) const;
  bool contains(const PluginMCFixup *Fixup) const;
  bool contains(const llvm::MCInst *Instruction) const;

private:
  InstructionStorage Instructions;
  SectionStorage Sections;
  SymbolStorage Symbols;
  ExpressionStorage Expressions;
  NevercTargetID TargetID{};
  std::string SchemaDigest;
};

llvm::Error verifyPluginMCUnit(
    const PluginMCUnit &Unit,
    const PluginTargetSnapshot::NamedRecord *Schema);

std::string dumpPluginMCUnit(const PluginMCUnit &Unit);
std::string dumpLLVMCompatibleMCUnit(const PluginMCUnit &Unit);

} // namespace neverc::plugin

#endif
