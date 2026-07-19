#include "neverc/Plugin/PluginIR.h"
#include "gtest/gtest.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include <array>
#include <cstdint>
#include <set>
#include <string_view>

namespace {

struct TypeMapping {
  llvm::Type::TypeID Internal;
  NevercIRTypeKind Stable;
};

constexpr TypeMapping TypeMappings[] = {
#define NEVERC_IR_SCHEMA_INTERNAL_TYPE(Internal, Symbol, ID)                  \
  {llvm::Type::Internal, ID},
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_TYPE
};

struct ValueMapping {
  llvm::Value::ValueTy Internal;
  NevercIRValueKind Stable;
};

constexpr ValueMapping ValueMappings[] = {
#define NEVERC_IR_SCHEMA_INTERNAL_VALUE(Internal, Symbol, ID)                 \
  {llvm::Value::Internal, ID},
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_VALUE
};

struct OpcodeMapping {
  unsigned Internal;
  NevercIROpcode Stable;
};

constexpr OpcodeMapping OpcodeMappings[] = {
#define NEVERC_IR_SCHEMA_INTERNAL_OPCODE(Internal, Symbol, ID)                \
  {llvm::Instruction::Internal, ID},
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_OPCODE
};

struct PredicateMapping {
  llvm::CmpInst::Predicate Internal;
  NevercIRPredicate Stable;
};

constexpr PredicateMapping PredicateMappings[] = {
#define NEVERC_IR_SCHEMA_INTERNAL_PREDICATE(Internal, Symbol, ID)             \
  {llvm::CmpInst::Internal, ID},
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_PREDICATE
};

struct LinkageMapping {
  llvm::GlobalValue::LinkageTypes Internal;
  NevercIRLinkage Stable;
};

constexpr LinkageMapping LinkageMappings[] = {
#define NEVERC_IR_SCHEMA_INTERNAL_LINKAGE(Internal, Symbol, ID)               \
  {llvm::GlobalValue::Internal, ID},
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_LINKAGE
};

struct VisibilityMapping {
  llvm::GlobalValue::VisibilityTypes Internal;
  NevercIRVisibility Stable;
};

constexpr VisibilityMapping VisibilityMappings[] = {
#define NEVERC_IR_SCHEMA_INTERNAL_VISIBILITY(Internal, Symbol, ID)            \
  {llvm::GlobalValue::Internal, ID},
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_VISIBILITY
};

struct CallingConventionMapping {
  llvm::CallingConv::ID Internal;
  NevercIRCallingConvention Stable;
};

constexpr CallingConventionMapping CallingConventionMappings[] = {
#define NEVERC_IR_SCHEMA_INTERNAL_CALLING_CONVENTION(Internal, Symbol, ID)    \
  {llvm::CallingConv::Internal, ID},
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_CALLING_CONVENTION
};

struct OpcodeSchemaEntry {
  NevercIROpcode Opcode;
  NevercIROpcodeCategory Category;
  uint32_t MinimumOperands;
  uint32_t MaximumOperands;
  NevercIRResultConstraint Result;
  NevercBool Terminator;
  NevercIRSideEffectClass SideEffects;
  NevercIRPropertyFlags WritableProperties;
  const char *Name;
};

constexpr OpcodeSchemaEntry Opcodes[] = {
#define NEVERC_IR_SCHEMA_OPCODE(Symbol, ID, Internal, Category, Minimum,       \
                                Maximum, Result, Terminator, SideEffects,      \
                                Properties, Name)                             \
  {ID, Category, Minimum, Maximum, Result, Terminator, SideEffects,           \
   Properties, Name},
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_OPCODE
};

constexpr size_t VerifierDispatchCount =
    0
#define NEVERC_IR_SCHEMA_VERIFY_OPCODE(...) +1
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_VERIFY_OPCODE
    ;

const OpcodeSchemaEntry *findOpcode(NevercIROpcode Opcode) {
  for (const OpcodeSchemaEntry &Entry : Opcodes)
    if (Entry.Opcode == Opcode)
      return &Entry;
  return nullptr;
}

TEST(PluginIRSchemaTest, CoversEveryLLVMTypeValueOpcodeAndEnum) {
  static_assert(std::size(TypeMappings) == NEVERC_IR_TYPE_KIND_COUNT);
  static_assert(std::size(ValueMappings) == NEVERC_IR_VALUE_KIND_COUNT);
  static_assert(std::size(OpcodeMappings) == NEVERC_IR_OPCODE_COUNT);
  static_assert(std::size(PredicateMappings) ==
                NEVERC_IR_PREDICATE_COUNT);
  static_assert(std::size(LinkageMappings) == NEVERC_IR_LINKAGE_COUNT);
  static_assert(std::size(VisibilityMappings) ==
                NEVERC_IR_VISIBILITY_COUNT);
  static_assert(std::size(CallingConventionMappings) ==
                NEVERC_IR_CALLING_CONVENTION_COUNT);
  static_assert(std::size(TypeMappings) ==
                static_cast<size_t>(llvm::Type::TargetExtTyID) + 1);
  static_assert(std::size(ValueMappings) ==
                static_cast<size_t>(llvm::Value::InstructionVal) + 1);
  static_assert(std::size(OpcodeMappings) ==
                llvm::Instruction::OtherOpsEnd - 1);
  static_assert(std::size(LinkageMappings) ==
                static_cast<size_t>(llvm::GlobalValue::CommonLinkage) + 1);
  static_assert(
      std::size(VisibilityMappings) ==
      static_cast<size_t>(llvm::GlobalValue::ProtectedVisibility) + 1);
  static_assert(VerifierDispatchCount == NEVERC_IR_OPCODE_COUNT);

  std::set<uint32_t> StableIDs;
  const auto CheckUnique = [&StableIDs](const auto &Mappings) {
    for (const auto &Mapping : Mappings)
      EXPECT_TRUE(StableIDs.insert(Mapping.Stable).second);
  };
  CheckUnique(TypeMappings);
  CheckUnique(ValueMappings);
  CheckUnique(OpcodeMappings);
  CheckUnique(PredicateMappings);
  CheckUnique(LinkageMappings);
  CheckUnique(VisibilityMappings);
  CheckUnique(CallingConventionMappings);

  EXPECT_NE(NEVERC_IR_OPCODE_ADD,
            static_cast<NevercIROpcode>(llvm::Instruction::Add));
  EXPECT_NE(NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM,
            static_cast<NevercIRCallingConvention>(
                llvm::CallingConv::NeverC_Custom));
}

TEST(PluginIRSchemaTest, PublishesOperandResultAndMutationContracts) {
  static_assert(std::size(Opcodes) == NEVERC_IR_OPCODE_COUNT);
  const OpcodeSchemaEntry *Add = findOpcode(NEVERC_IR_OPCODE_ADD);
  ASSERT_NE(Add, nullptr);
  EXPECT_EQ(Add->Category, NEVERC_IR_OPCODE_CATEGORY_BINARY);
  EXPECT_EQ(Add->MinimumOperands, 2U);
  EXPECT_EQ(Add->MaximumOperands, 2U);
  EXPECT_EQ(Add->Result, NEVERC_IR_RESULT_SAME_AS_OPERAND_0);
  EXPECT_EQ(Add->Terminator, NEVERC_FALSE);
  EXPECT_EQ(Add->SideEffects, NEVERC_IR_SIDE_EFFECT_NEVER);
  EXPECT_NE(Add->WritableProperties & NEVERC_IR_PROPERTY_FLAG_NSW, 0U);
  EXPECT_NE(Add->WritableProperties & NEVERC_IR_PROPERTY_FLAG_NUW, 0U);

  const OpcodeSchemaEntry *Ret = findOpcode(NEVERC_IR_OPCODE_RET);
  ASSERT_NE(Ret, nullptr);
  EXPECT_EQ(Ret->MinimumOperands, 0U);
  EXPECT_EQ(Ret->MaximumOperands, 1U);
  EXPECT_EQ(Ret->Terminator, NEVERC_TRUE);

  const OpcodeSchemaEntry *Load = findOpcode(NEVERC_IR_OPCODE_LOAD);
  ASSERT_NE(Load, nullptr);
  EXPECT_EQ(Load->SideEffects, NEVERC_IR_SIDE_EFFECT_CONDITIONAL);
  EXPECT_NE(Load->WritableProperties & NEVERC_IR_PROPERTY_FLAG_VOLATILE, 0U);
  EXPECT_NE(Load->WritableProperties & NEVERC_IR_PROPERTY_FLAG_ALIGNMENT, 0U);

  const OpcodeSchemaEntry *GEP =
      findOpcode(NEVERC_IR_OPCODE_GET_ELEMENT_PTR);
  ASSERT_NE(GEP, nullptr);
  EXPECT_NE(GEP->WritableProperties & NEVERC_IR_PROPERTY_FLAG_NUSW, 0U);
  EXPECT_NE(GEP->WritableProperties & NEVERC_IR_PROPERTY_FLAG_NUW, 0U);
  EXPECT_EQ(GEP->WritableProperties & NEVERC_IR_PROPERTY_FLAG_NSW, 0U);

  EXPECT_EQ(std::char_traits<char>::length(NEVERC_IR_SCHEMA_DIGEST), 64U);
}

} // namespace
