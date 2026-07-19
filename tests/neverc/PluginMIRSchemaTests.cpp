#include "neverc/Plugin/PluginMIR.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "gtest/gtest.h"
#include <cstddef>
#include <set>
#include <string>

namespace {

struct SchemaMapping {
  uint32_t LLVMValue;
  uint32_t StableID;
  const char *Name;
};

constexpr SchemaMapping OperandMappings[] = {
#define NEVERC_MIR_SCHEMA_OPERAND(Internal, Symbol, ID, LLVMValue, Name)       \
  {LLVMValue, ID, Name},
#include "neverc/Plugin/Schema/PluginMIRSchema.inc"
#undef NEVERC_MIR_SCHEMA_OPERAND
};

constexpr SchemaMapping GenericOpcodeMappings[] = {
#define NEVERC_MIR_SCHEMA_GENERIC_OPCODE(Internal, Symbol, ID, LLVMValue,      \
                                         Name, RequiresTarget)                 \
  {LLVMValue, ID, Name},
#include "neverc/Plugin/Schema/PluginMIRSchema.inc"
#undef NEVERC_MIR_SCHEMA_GENERIC_OPCODE
};

constexpr SchemaMapping PropertyMappings[] = {
#define NEVERC_MIR_SCHEMA_PROPERTY(Internal, Symbol, ID, LLVMValue, Name)      \
  {LLVMValue, ID, Name},
#include "neverc/Plugin/Schema/PluginMIRSchema.inc"
#undef NEVERC_MIR_SCHEMA_PROPERTY
};

TEST(PluginMIRSchemaTest, CoversLLVMOperandOpcodeAndPropertyInventories) {
  static_assert(std::size(OperandMappings) == NEVERC_MIR_OPERAND_COUNT);
  static_assert(std::size(GenericOpcodeMappings) ==
                NEVERC_MIR_GENERIC_OPCODE_COUNT);
  static_assert(std::size(PropertyMappings) == NEVERC_MIR_PROPERTY_COUNT);
  static_assert(std::size(OperandMappings) ==
                static_cast<size_t>(llvm::MachineOperand::MO_Last) + 1);
  static_assert(
      std::size(GenericOpcodeMappings) ==
      static_cast<size_t>(llvm::TargetOpcode::PRE_ISEL_GENERIC_OPCODE_END) + 1);
  static_assert(std::size(PropertyMappings) ==
                static_cast<size_t>(
                    llvm::MachineFunctionProperties::Property::LastProperty) +
                    1);

  for (size_t I = 0; I != std::size(OperandMappings); ++I)
    EXPECT_EQ(OperandMappings[I].LLVMValue, I);
  for (size_t I = 0; I != std::size(GenericOpcodeMappings); ++I)
    EXPECT_EQ(GenericOpcodeMappings[I].LLVMValue, I);
  for (size_t I = 0; I != std::size(PropertyMappings); ++I)
    EXPECT_EQ(PropertyMappings[I].LLVMValue, I);
}

TEST(PluginMIRSchemaTest, KeepsStableIDsSeparateFromLLVMNumbers) {
  std::set<uint32_t> IDs;
  const auto Check = [&IDs](const auto &Mappings) {
    for (const SchemaMapping &Mapping : Mappings) {
      EXPECT_TRUE(IDs.insert(Mapping.StableID).second);
      EXPECT_NE(Mapping.StableID, Mapping.LLVMValue);
      EXPECT_NE(Mapping.Name[0], '\0');
    }
  };
  Check(OperandMappings);
  Check(GenericOpcodeMappings);
  Check(PropertyMappings);

  EXPECT_EQ(NEVERC_MIR_OPERAND_REGISTER, UINT32_C(0x52000001));
  EXPECT_EQ(NEVERC_MIR_GENERIC_OPCODE_COPY, UINT32_C(0x53000013));
  EXPECT_EQ(NEVERC_MIR_PROPERTY_TRACKS_LIVENESS, UINT32_C(0x54000003));
  EXPECT_EQ(std::char_traits<char>::length(NEVERC_MIR_SCHEMA_DIGEST), 64U);
}

} // namespace
