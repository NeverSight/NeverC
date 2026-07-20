#include "neverc/Plugin/PluginMC.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFragment.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <iterator>
#include <limits>
#include <set>
#include <string>

namespace {

struct SchemaMapping {
  uint32_t LLVMValue;
  uint32_t StableID;
  const char *Name;
  bool RequiresTargetSchema;
};

constexpr SchemaMapping OperandMappings[] = {
#define NEVERC_MC_SCHEMA_OPERAND(Internal, Symbol, ID, LLVMValue, Name,        \
                                 RequiresTarget)                              \
  {LLVMValue, ID, Name, RequiresTarget},
#include "neverc/Plugin/Schema/PluginMCSchema.inc"
#undef NEVERC_MC_SCHEMA_OPERAND
};

constexpr SchemaMapping ExpressionMappings[] = {
#define NEVERC_MC_SCHEMA_EXPRESSION(Internal, Symbol, ID, LLVMValue, Name,     \
                                    RequiresTarget)                           \
  {LLVMValue, ID, Name, RequiresTarget},
#include "neverc/Plugin/Schema/PluginMCSchema.inc"
#undef NEVERC_MC_SCHEMA_EXPRESSION
};

constexpr SchemaMapping FragmentMappings[] = {
#define NEVERC_MC_SCHEMA_FRAGMENT(Internal, Symbol, ID, LLVMValue, Name,       \
                                  RequiresTarget)                             \
  {LLVMValue, ID, Name, RequiresTarget},
#include "neverc/Plugin/Schema/PluginMCSchema.inc"
#undef NEVERC_MC_SCHEMA_FRAGMENT
};

constexpr SchemaMapping FixupMappings[] = {
#define NEVERC_MC_SCHEMA_FIXUP(Internal, Symbol, ID, LLVMValue, Name,          \
                               RequiresTarget)                                \
  {LLVMValue, ID, Name, RequiresTarget},
#include "neverc/Plugin/Schema/PluginMCSchema.inc"
#undef NEVERC_MC_SCHEMA_FIXUP
};

TEST(PluginMCSchemaTest, CoversStableMachineCodeModel) {
  static_assert(NEVERC_MC_ENTITY_COUNT == 10);
  static_assert(std::size(OperandMappings) == NEVERC_MC_OPERAND_COUNT);
  static_assert(std::size(ExpressionMappings) == NEVERC_MC_EXPRESSION_COUNT);
  static_assert(std::size(FragmentMappings) == NEVERC_MC_FRAGMENT_COUNT);
  static_assert(std::size(FixupMappings) == NEVERC_MC_FIXUP_COUNT);
  static_assert(NEVERC_MC_LAYOUT_STATE_COUNT == 5);

  EXPECT_EQ(NEVERC_MC_ENTITY_MC_UNIT, UINT32_C(0x61000001));
  EXPECT_EQ(NEVERC_MC_ENTITY_SOURCE_LOC, UINT32_C(0x61000009));
  EXPECT_EQ(NEVERC_MC_ENTITY_LAYOUT_STATE, UINT32_C(0x6100000a));
  EXPECT_EQ(NEVERC_MC_OPERAND_REGISTER, UINT32_C(0x62000002));
  EXPECT_EQ(NEVERC_MC_EXPRESSION_TARGET_VARIANT, UINT32_C(0x63000005));
  EXPECT_EQ(NEVERC_MC_FRAGMENT_ENCODED_WITH_FIXUPS,
            UINT32_C(0x64000003));
  EXPECT_EQ(NEVERC_MC_LAYOUT_STATE_COMMITTED, UINT32_C(0x66000005));
  EXPECT_EQ(std::char_traits<char>::length(NEVERC_MC_SCHEMA_DIGEST), 64U);
}

TEST(PluginMCSchemaTest, TracksLLVMOperandAndFragmentInventories) {
  ASSERT_EQ(std::size(OperandMappings), 8U);
  for (size_t I = 0; I != 7; ++I) {
    EXPECT_EQ(OperandMappings[I].LLVMValue, I);
    EXPECT_FALSE(OperandMappings[I].RequiresTargetSchema);
  }
  EXPECT_EQ(OperandMappings[7].LLVMValue,
            std::numeric_limits<uint32_t>::max());
  EXPECT_TRUE(OperandMappings[7].RequiresTargetSchema);

  ASSERT_EQ(std::size(FragmentMappings), 15U);
  for (size_t I = 0; I != 14; ++I) {
    EXPECT_EQ(FragmentMappings[I].LLVMValue, I);
    EXPECT_FALSE(FragmentMappings[I].RequiresTargetSchema);
  }
  EXPECT_EQ(FragmentMappings[13].LLVMValue,
            static_cast<uint32_t>(llvm::MCFragment::FT_Dummy));
  EXPECT_EQ(FragmentMappings[14].LLVMValue,
            std::numeric_limits<uint32_t>::max());
  EXPECT_TRUE(FragmentMappings[14].RequiresTargetSchema);
}

TEST(PluginMCSchemaTest, TracksLLVMExpressionAndFixupInventories) {
  ASSERT_EQ(std::size(ExpressionMappings), 5U);
  EXPECT_EQ(ExpressionMappings[0].LLVMValue,
            static_cast<uint32_t>(llvm::MCExpr::Binary));
  EXPECT_EQ(ExpressionMappings[1].LLVMValue,
            static_cast<uint32_t>(llvm::MCExpr::Constant));
  EXPECT_EQ(ExpressionMappings[2].LLVMValue,
            static_cast<uint32_t>(llvm::MCExpr::SymbolRef));
  EXPECT_EQ(ExpressionMappings[3].LLVMValue,
            static_cast<uint32_t>(llvm::MCExpr::Unary));
  EXPECT_EQ(ExpressionMappings[4].LLVMValue,
            static_cast<uint32_t>(llvm::MCExpr::Target));
  EXPECT_TRUE(ExpressionMappings[4].RequiresTargetSchema);

  ASSERT_EQ(std::size(FixupMappings), 23U);
  for (size_t I = 0; I != 22; ++I) {
    EXPECT_EQ(FixupMappings[I].LLVMValue, I);
    EXPECT_FALSE(FixupMappings[I].RequiresTargetSchema);
  }
  EXPECT_EQ(FixupMappings[21].LLVMValue,
            static_cast<uint32_t>(llvm::FK_SecRel_8));
  EXPECT_EQ(FixupMappings[22].LLVMValue,
            std::numeric_limits<uint32_t>::max());
  EXPECT_TRUE(FixupMappings[22].RequiresTargetSchema);
}

TEST(PluginMCSchemaTest, KeepsStableIDsUniqueAndOpaque) {
  std::set<uint32_t> IDs;
  const auto Check = [&IDs](const auto &Mappings) {
    for (const SchemaMapping &Mapping : Mappings) {
      EXPECT_TRUE(IDs.insert(Mapping.StableID).second);
      EXPECT_NE(Mapping.StableID, Mapping.LLVMValue);
      EXPECT_NE(Mapping.Name[0], '\0');
    }
  };
  Check(OperandMappings);
  Check(ExpressionMappings);
  Check(FragmentMappings);
  Check(FixupMappings);
}

} // namespace
