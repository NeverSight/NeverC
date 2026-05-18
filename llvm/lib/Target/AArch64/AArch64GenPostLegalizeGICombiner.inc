/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* AArch64PostLegalizerCombinerImpl Combiner Match Table                      *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifdef GET_GICOMBINER_DEPS
#include "llvm/ADT/SparseBitVector.h"
namespace llvm {
extern cl::OptionCategory GICombinerOptionCategory;
} // end namespace llvm
#endif // ifdef GET_GICOMBINER_DEPS

#ifdef GET_GICOMBINER_TYPES
struct AArch64PostLegalizerCombinerImplRuleConfig {
  SparseBitVector<> DisabledRules;

  bool isRuleEnabled(unsigned RuleID) const;
  bool parseCommandLineOption();
  bool setRuleEnabled(StringRef RuleIdentifier);
  bool setRuleDisabled(StringRef RuleIdentifier);
};

static std::optional<uint64_t> getRuleIdxForIdentifier(StringRef RuleIdentifier) {
  uint64_t I;
  // getAtInteger(...) returns false on success
  bool Parsed = !RuleIdentifier.getAsInteger(0, I);
  if (Parsed)
    return I;

#ifndef NDEBUG
  switch (RuleIdentifier.size()) {
  default: break;
  case 9:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'c':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "opy_prop", 8) != 0)
        break;
      return 0;	 // "copy_prop"
    case 'm':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ul_const", 8) != 0)
        break;
      return 11;	 // "mul_const"
    case 'o':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "r_to_bsp", 8) != 0)
        break;
      return 60;	 // "or_to_bsp"
    }
    break;
  case 10:	 // 2 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'i':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "2p_to_p2i", 9) != 0)
        break;
      return 32;	 // "i2p_to_p2i"
    case 'p':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "2i_to_i2p", 9) != 0)
        break;
      return 31;	 // "p2i_to_i2p"
    }
    break;
  case 11:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "add_sub_reg", 11) != 0)
      break;
    return 37;	 // "add_sub_reg"
  case 12:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "redundant_or", 12) != 0)
      break;
    return 10;	 // "redundant_or"
  case 13:	 // 4 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'l':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "oad_and_mask", 12) != 0)
        break;
      return 2;	 // "load_and_mask"
    case 'm':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "erge_unmerge", 12) != 0)
        break;
      return 19;	 // "merge_unmerge"
    case 'r':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "edundant_and", 12) != 0)
        break;
      return 7;	 // "redundant_and"
    case 'u':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "nmerge_undef", 12) != 0)
        break;
      return 57;	 // "unmerge_undef"
    }
    break;
  case 14:	 // 2 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "inop_same_val", 13) != 0)
        break;
      return 28;	 // "binop_same_val"
    case 'f':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "neg_fneg_fold", 13) != 0)
        break;
      return 34;	 // "fneg_fneg_fold"
    }
    break;
  case 15:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'e':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "xtending_loads", 14) != 0)
        break;
      return 1;	 // "extending_loads"
    case 'o':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "verlapping_and", 14) != 0)
        break;
      return 45;	 // "overlapping_and"
    case 's':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "elect_same_val", 14) != 0)
        break;
      return 26;	 // "select_same_val"
    }
    break;
  case 16:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 's':	 // 2 strings to match.
      if (memcmp(RuleIdentifier.data()+1, "elect_", 6) != 0)
        break;
      switch (RuleIdentifier[7]) {
      default: break;
      case 't':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+8, "o_minmax", 8) != 0)
          break;
        return 59;	 // "select_to_minmax"
      case 'u':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+8, "ndef_cmp", 8) != 0)
          break;
        return 20;	 // "select_undef_cmp"
      }
      break;
    case 'u':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ndef_to_fp_zero", 15) != 0)
        break;
      return 47;	 // "undef_to_fp_zero"
    }
    break;
  case 17:	 // 4 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'a':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "nyext_trunc_fold", 16) != 0)
        break;
      return 33;	 // "anyext_trunc_fold"
    case 'e':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "rase_undef_store", 16) != 0)
        break;
      return 56;	 // "erase_undef_store"
    case 's':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "elect_to_logical", 16) != 0)
        break;
      return 22;	 // "select_to_logical"
    case 'u':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ndef_to_int_zero", 16) != 0)
        break;
      return 48;	 // "undef_to_int_zero"
    }
    break;
  case 18:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "inop_left_to_zero", 17) != 0)
        break;
      return 29;	 // "binop_left_to_zero"
    case 'f':	 // 2 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'o':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "ld_merge_to_zext", 16) != 0)
          break;
        return 23;	 // "fold_merge_to_zext"
      case 'p':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "trunc_fpext_fold", 16) != 0)
          break;
        return 42;	 // "fptrunc_fpext_fold"
      }
      break;
    }
    break;
  case 19:	 // 8 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "inop_right_to_zero", 18) != 0)
        break;
      return 30;	 // "binop_right_to_zero"
    case 'c':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "onstant_fold_binop", 18) != 0)
        break;
      return 24;	 // "constant_fold_binop"
    case 'p':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "tr_add_immed_chain", 18) != 0)
        break;
      return 44;	 // "ptr_add_immed_chain"
    case 'r':	 // 2 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'i':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "ght_identity_zero", 17) != 0)
          break;
        return 27;	 // "right_identity_zero"
      case 'o':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "tate_out_of_range", 17) != 0)
          break;
        return 17;	 // "rotate_out_of_range"
      }
      break;
    case 's':	 // 2 strings to match.
      if (RuleIdentifier[1] != 'e')
        break;
      switch (RuleIdentifier[2]) {
      default: break;
      case 'l':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+3, "ect_constant_cmp", 16) != 0)
          break;
        return 21;	 // "select_constant_cmp"
      case 'x':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+3, "t_trunc_sextload", 16) != 0)
          break;
        return 4;	 // "sext_trunc_sextload"
      }
      break;
    case 'u':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "nary_undef_to_zero", 18) != 0)
        break;
      return 52;	 // "unary_undef_to_zero"
    }
    break;
  case 20:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "itcast_bitcast_fold", 19) != 0)
        break;
      return 41;	 // "bitcast_bitcast_fold"
    case 'r':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "edundant_sext_inreg", 19) != 0)
        break;
      return 12;	 // "redundant_sext_inreg"
    case 's':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "plit_store_zero_128", 19) != 0)
        break;
      return 46;	 // "split_store_zero_128"
    }
    break;
  case 21:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'm':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "utate_anyext_to_zext", 20) != 0)
        break;
      return 5;	 // "mutate_anyext_to_zext"
    case 'r':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ight_identity_one_fp", 20) != 0)
        break;
      return 36;	 // "right_identity_one_fp"
    case 'u':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ndef_to_negative_one", 20) != 0)
        break;
      return 49;	 // "undef_to_negative_one"
    }
    break;
  case 22:	 // 4 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'c':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "onstant_fold_fp_binop", 21) != 0)
        break;
      return 25;	 // "constant_fold_fp_binop"
    case 'p':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ropagate_undef_any_op", 21) != 0)
        break;
      return 53;	 // "propagate_undef_any_op"
    case 'r':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ight_identity_one_int", 21) != 0)
        break;
      return 35;	 // "right_identity_one_int"
    case 't':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "runc_buildvector_fold", 21) != 0)
        break;
      return 39;	 // "trunc_buildvector_fold"
    }
    break;
  case 23:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "propagate_undef_all_ops", 23) != 0)
      break;
    return 54;	 // "propagate_undef_all_ops"
  case 24:	 // 2 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "inop_left_undef_to_zero", 23) != 0)
        break;
      return 50;	 // "binop_left_undef_to_zero"
    case 'x':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "or_of_and_with_same_reg", 23) != 0)
        break;
      return 8;	 // "xor_of_and_with_same_reg"
    }
    break;
  case 25:	 // 3 strings to match.
    if (RuleIdentifier[0] != 'b')
      break;
    switch (RuleIdentifier[1]) {
    default: break;
    case 'i':	 // 2 strings to match.
      if (memcmp(RuleIdentifier.data()+2, "tfield_extract_from_", 20) != 0)
        break;
      switch (RuleIdentifier[22]) {
      default: break;
      case 'a':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+23, "nd", 2) != 0)
          break;
        return 14;	 // "bitfield_extract_from_and"
      case 's':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+23, "hr", 2) != 0)
          break;
        return 15;	 // "bitfield_extract_from_shr"
      }
      break;
    case 'u':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+2, "ildvector_identity_fold", 23) != 0)
        break;
      return 38;	 // "buildvector_identity_fold"
    }
    break;
  case 26:	 // 4 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "inop_right_undef_to_undef", 25) != 0)
        break;
      return 51;	 // "binop_right_undef_to_undef"
    case 'c':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ombine_indexed_load_store", 25) != 0)
        break;
      return 3;	 // "combine_indexed_load_store"
    case 'e':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "xtractvecelt_pairwise_add", 25) != 0)
        break;
      return 9;	 // "extractvecelt_pairwise_add"
    case 'r':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ight_identity_neg_zero_fp", 25) != 0)
        break;
      return 43;	 // "right_identity_neg_zero_fp"
    }
    break;
  case 27:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "trunc_lshr_buildvector_fold", 27) != 0)
      break;
    return 40;	 // "trunc_lshr_buildvector_fold"
  case 28:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "propagate_undef_shuffle_mask", 28) != 0)
      break;
    return 55;	 // "propagate_undef_shuffle_mask"
  case 29:	 // 2 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "itfield_extract_from_shr_and", 28) != 0)
        break;
      return 16;	 // "bitfield_extract_from_shr_and"
    case 'i':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "cmp_to_true_false_known_bits", 28) != 0)
        break;
      return 18;	 // "icmp_to_true_false_known_bits"
    }
    break;
  case 32:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "bitfield_extract_from_sext_inreg", 32) != 0)
      break;
    return 13;	 // "bitfield_extract_from_sext_inreg"
  case 36:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "insert_extract_vec_elt_out_of_bounds", 36) != 0)
      break;
    return 58;	 // "insert_extract_vec_elt_out_of_bounds"
  case 37:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "hoist_logic_op_with_same_opcode_hands", 37) != 0)
      break;
    return 6;	 // "hoist_logic_op_with_same_opcode_hands"
  }
#endif // ifndef NDEBUG

  return std::nullopt;
}
static std::optional<std::pair<uint64_t, uint64_t>> getRuleRangeForIdentifier(StringRef RuleIdentifier) {
  std::pair<StringRef, StringRef> RangePair = RuleIdentifier.split('-');
  if (!RangePair.second.empty()) {
    const auto First = getRuleIdxForIdentifier(RangePair.first);
    const auto Last = getRuleIdxForIdentifier(RangePair.second);
    if (!First || !Last)
      return std::nullopt;
    if (First >= Last)
      report_fatal_error("Beginning of range should be before end of range");
    return {{*First, *Last + 1}};
  }
  if (RangePair.first == "*") {
    return {{0, 61}};
  }
  const auto I = getRuleIdxForIdentifier(RangePair.first);
  if (!I)
    return std::nullopt;
  return {{*I, *I + 1}};
}

bool AArch64PostLegalizerCombinerImplRuleConfig::setRuleEnabled(StringRef RuleIdentifier) {
  auto MaybeRange = getRuleRangeForIdentifier(RuleIdentifier);
  if (!MaybeRange)
    return false;
  for (auto I = MaybeRange->first; I < MaybeRange->second; ++I)
    DisabledRules.reset(I);
  return true;
}

bool AArch64PostLegalizerCombinerImplRuleConfig::setRuleDisabled(StringRef RuleIdentifier) {
  auto MaybeRange = getRuleRangeForIdentifier(RuleIdentifier);
  if (!MaybeRange)
    return false;
  for (auto I = MaybeRange->first; I < MaybeRange->second; ++I)
    DisabledRules.set(I);
  return true;
}

static std::vector<std::string> AArch64PostLegalizerCombinerOption;
static cl::list<std::string> AArch64PostLegalizerCombinerDisableOption(
    "aarch64postlegalizercombiner-disable-rule",
    cl::desc("Disable one or more combiner rules temporarily in the AArch64PostLegalizerCombiner pass"),
    cl::CommaSeparated,
    cl::Hidden,
    cl::cat(GICombinerOptionCategory),
    cl::callback([](const std::string &Str) {
      AArch64PostLegalizerCombinerOption.push_back(Str);
    }));
static cl::list<std::string> AArch64PostLegalizerCombinerOnlyEnableOption(
    "aarch64postlegalizercombiner-only-enable-rule",
    cl::desc("Disable all rules in the AArch64PostLegalizerCombiner pass then re-enable the specified ones"),
    cl::Hidden,
    cl::cat(GICombinerOptionCategory),
    cl::callback([](const std::string &CommaSeparatedArg) {
      StringRef Str = CommaSeparatedArg;
      AArch64PostLegalizerCombinerOption.push_back("*");
      do {
        auto X = Str.split(",");
        AArch64PostLegalizerCombinerOption.push_back(("!" + X.first).str());
        Str = X.second;
      } while (!Str.empty());
    }));


bool AArch64PostLegalizerCombinerImplRuleConfig::isRuleEnabled(unsigned RuleID) const {
    return  !DisabledRules.test(RuleID);
}
bool AArch64PostLegalizerCombinerImplRuleConfig::parseCommandLineOption() {
  for (StringRef Identifier : AArch64PostLegalizerCombinerOption) {
    bool Enabled = Identifier.consume_front("!");
    if (Enabled && !setRuleEnabled(Identifier))
      return false;
    if (!Enabled && !setRuleDisabled(Identifier))
      return false;
  }
  return true;
}

#endif // ifdef GET_GICOMBINER_TYPES

#ifdef GET_GICOMBINER_TYPES
const unsigned MAX_SUBTARGET_PREDICATES = 0;
using PredicateBitset = llvm::Bitset<MAX_SUBTARGET_PREDICATES>;
#endif // ifdef GET_GICOMBINER_TYPES

#ifdef GET_GICOMBINER_CLASS_MEMBERS
PredicateBitset AvailableModuleFeatures;
mutable PredicateBitset AvailableFunctionFeatures;
PredicateBitset getAvailableFeatures() const {
  return AvailableModuleFeatures | AvailableFunctionFeatures;
}
PredicateBitset
computeAvailableModuleFeatures(const AArch64Subtarget *Subtarget) const;
PredicateBitset
computeAvailableFunctionFeatures(const AArch64Subtarget *Subtarget,
                                 const MachineFunction *MF) const;
void setupGeneratedPerFunctionState(MachineFunction &MF) override;
#endif // ifdef GET_GICOMBINER_CLASS_MEMBERS
#ifdef GET_GICOMBINER_CLASS_MEMBERS
  mutable MatcherState State;
  typedef ComplexRendererFns(AArch64PostLegalizerCombinerImpl::*ComplexMatcherMemFn)(MachineOperand &) const;
  typedef void(AArch64PostLegalizerCombinerImpl::*CustomRendererFn)(MachineInstrBuilder &, const MachineInstr &, int) const;
  const ExecInfoTy<PredicateBitset, ComplexMatcherMemFn, CustomRendererFn> ExecInfo;
  static AArch64PostLegalizerCombinerImpl::ComplexMatcherMemFn ComplexPredicateFns[];
  static AArch64PostLegalizerCombinerImpl::CustomRendererFn CustomRenderers[];
  bool testImmPredicate_I64(unsigned PredicateID, int64_t Imm) const override;
  bool testImmPredicate_APInt(unsigned PredicateID, const APInt &Imm) const override;
  bool testImmPredicate_APFloat(unsigned PredicateID, const APFloat &Imm) const override;
  const uint8_t *getMatchTable() const override;
  bool testMIPredicate_MI(unsigned PredicateID, const MachineInstr &MI, const MatcherState &State) const override;
  bool testSimplePredicate(unsigned PredicateID) const override;
  void runCustomAction(unsigned FnID, const MatcherState &State, NewMIVector &OutMIs) const override;
  struct MatchInfosTy {
    std::tuple<unsigned, LLT, Register> MDInfo6;
    unsigned MDInfo9;
    ConstantFP* MDInfo11;
    InstructionStepsMatchInfo MDInfo3;
    IndexedLoadStoreMatchInfo MDInfo2;
    std::function<void(MachineIRBuilder&, Register)> MDInfo7;
    PreferredTuple MDInfo0;
    std::tuple<Register, Register, Register> MDInfo13;
    Register MDInfo4;
    std::function<void(MachineIRBuilder &)> MDInfo1;
    int64_t MDInfo8;
    APInt MDInfo10;
    std::pair<Register, Register> MDInfo5;
    PtrAddChain MDInfo12;
  };
  mutable MatchInfosTy MatchInfos;

#endif // ifdef GET_GICOMBINER_CLASS_MEMBERS

#ifdef GET_GICOMBINER_IMPL
// LLT Objects.
enum {
  GILLT_s1,
};
const static size_t NumTypeObjects = 1;
const static LLT TypeObjects[] = {
  LLT::scalar(1),
};

// Bits for subtarget features that participate in instruction matching.
enum SubtargetFeatureBits : uint8_t {
};

PredicateBitset AArch64PostLegalizerCombinerImpl::
computeAvailableModuleFeatures(const AArch64Subtarget *Subtarget) const {
  PredicateBitset Features;
  return Features;
}

void AArch64PostLegalizerCombinerImpl::setupGeneratedPerFunctionState(MachineFunction &MF) {
  AvailableFunctionFeatures = computeAvailableFunctionFeatures((const AArch64Subtarget *)&MF.getSubtarget(), &MF);
}
PredicateBitset AArch64PostLegalizerCombinerImpl::
computeAvailableFunctionFeatures(const AArch64Subtarget *Subtarget, const MachineFunction *MF) const {
  PredicateBitset Features;
  return Features;
}

// Feature bitsets.
enum {
  GIFBS_Invalid,
};
constexpr static PredicateBitset FeatureBitsets[] {
  {}, // GIFBS_Invalid
};

// ComplexPattern predicates.
enum {
  GICP_Invalid,
};
// See constructor for table contents

AArch64PostLegalizerCombinerImpl::ComplexMatcherMemFn
AArch64PostLegalizerCombinerImpl::ComplexPredicateFns[] = {
  nullptr, // GICP_Invalid
};

enum {
  GICXXPred_MI_Predicate_GICombiner0 = GICXXPred_Invalid + 1,
  GICXXPred_MI_Predicate_GICombiner1,
  GICXXPred_MI_Predicate_GICombiner2,
  GICXXPred_MI_Predicate_GICombiner3,
  GICXXPred_MI_Predicate_GICombiner4,
  GICXXPred_MI_Predicate_GICombiner5,
  GICXXPred_MI_Predicate_GICombiner6,
  GICXXPred_MI_Predicate_GICombiner7,
  GICXXPred_MI_Predicate_GICombiner8,
  GICXXPred_MI_Predicate_GICombiner9,
  GICXXPred_MI_Predicate_GICombiner10,
  GICXXPred_MI_Predicate_GICombiner11,
  GICXXPred_MI_Predicate_GICombiner12,
  GICXXPred_MI_Predicate_GICombiner13,
  GICXXPred_MI_Predicate_GICombiner14,
  GICXXPred_MI_Predicate_GICombiner15,
  GICXXPred_MI_Predicate_GICombiner16,
  GICXXPred_MI_Predicate_GICombiner17,
  GICXXPred_MI_Predicate_GICombiner18,
  GICXXPred_MI_Predicate_GICombiner19,
  GICXXPred_MI_Predicate_GICombiner20,
  GICXXPred_MI_Predicate_GICombiner21,
  GICXXPred_MI_Predicate_GICombiner22,
  GICXXPred_MI_Predicate_GICombiner23,
  GICXXPred_MI_Predicate_GICombiner24,
  GICXXPred_MI_Predicate_GICombiner25,
  GICXXPred_MI_Predicate_GICombiner26,
  GICXXPred_MI_Predicate_GICombiner27,
  GICXXPred_MI_Predicate_GICombiner28,
  GICXXPred_MI_Predicate_GICombiner29,
  GICXXPred_MI_Predicate_GICombiner30,
  GICXXPred_MI_Predicate_GICombiner31,
  GICXXPred_MI_Predicate_GICombiner32,
  GICXXPred_MI_Predicate_GICombiner33,
  GICXXPred_MI_Predicate_GICombiner34,
  GICXXPred_MI_Predicate_GICombiner35,
  GICXXPred_MI_Predicate_GICombiner36,
  GICXXPred_MI_Predicate_GICombiner37,
  GICXXPred_MI_Predicate_GICombiner38,
  GICXXPred_MI_Predicate_GICombiner39,
  GICXXPred_MI_Predicate_GICombiner40,
  GICXXPred_MI_Predicate_GICombiner41,
  GICXXPred_MI_Predicate_GICombiner42,
  GICXXPred_MI_Predicate_GICombiner43,
  GICXXPred_MI_Predicate_GICombiner44,
  GICXXPred_MI_Predicate_GICombiner45,
  GICXXPred_MI_Predicate_GICombiner46,
  GICXXPred_MI_Predicate_GICombiner47,
  GICXXPred_MI_Predicate_GICombiner48,
};
bool AArch64PostLegalizerCombinerImpl::testMIPredicate_MI(unsigned PredicateID, const MachineInstr & MI, const MatcherState &State) const {
  switch (PredicateID) {
  case GICXXPred_MI_Predicate_GICombiner0: {
    return Helper.matchCombineCopy(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner1: {
    return Helper.matchCombineExtendingLoads(*State.MIs[0], MatchInfos.MDInfo0);
  }
  case GICXXPred_MI_Predicate_GICombiner2: {
    return Helper.matchCombineLoadWithAndMask(*State.MIs[0], MatchInfos.MDInfo1);
  }
  case GICXXPred_MI_Predicate_GICombiner3: {
    return Helper.matchCombineIndexedLoadStore(*State.MIs[0], MatchInfos.MDInfo2);
  }
  case GICXXPred_MI_Predicate_GICombiner4: {
    return Helper.matchSextTruncSextLoad(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner5: {
    return matchMutateAnyExtToZExt(*State.MIs[0], MRI);
  }
  case GICXXPred_MI_Predicate_GICombiner6: {
    return Helper.matchHoistLogicOpWithSameOpcodeHands(*State.MIs[0], MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner7: {
    return Helper.matchRedundantAnd(*State.MIs[0], MatchInfos.MDInfo4);
  }
  case GICXXPred_MI_Predicate_GICombiner8: {
    return Helper.matchXorOfAndWithSameReg(*State.MIs[0], MatchInfos.MDInfo5);
  }
  case GICXXPred_MI_Predicate_GICombiner9: {
    return matchExtractVecEltPairwiseAdd(*State.MIs[0], MRI, MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner10: {
    return Helper.matchRedundantOr(*State.MIs[0], MatchInfos.MDInfo4);
  }
  case GICXXPred_MI_Predicate_GICombiner11: {
    return matchAArch64MulConstCombine(*State.MIs[0], MRI, MatchInfos.MDInfo7);
  }
  case GICXXPred_MI_Predicate_GICombiner12: {
    return Helper.matchRedundantSExtInReg(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner13: {
    return Helper.matchBitfieldExtractFromSExtInReg(*State.MIs[0], MatchInfos.MDInfo1);
  }
  case GICXXPred_MI_Predicate_GICombiner14: {
    return Helper.matchBitfieldExtractFromAnd(*State.MIs[0], MatchInfos.MDInfo1);
  }
  case GICXXPred_MI_Predicate_GICombiner15: {
    return Helper.matchBitfieldExtractFromShr(*State.MIs[0], MatchInfos.MDInfo1);
  }
  case GICXXPred_MI_Predicate_GICombiner16: {
    return Helper.matchBitfieldExtractFromShrAnd(*State.MIs[0], MatchInfos.MDInfo1);
  }
  case GICXXPred_MI_Predicate_GICombiner17: {
    return Helper.matchRotateOutOfRange(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner18: {
    return Helper.matchICmpToTrueFalseKnownBits(*State.MIs[0], MatchInfos.MDInfo8);
  }
  case GICXXPred_MI_Predicate_GICombiner19: {
    return Helper.matchCombineMergeUnmerge(*State.MIs[0], MatchInfos.MDInfo4);
  }
  case GICXXPred_MI_Predicate_GICombiner20: {
    return Helper.matchConstantSelectCmp(*State.MIs[0], MatchInfos.MDInfo9);
  }
  case GICXXPred_MI_Predicate_GICombiner21: {
    return Helper.matchSelectToLogical(*State.MIs[0], MatchInfos.MDInfo1);
  }
  case GICXXPred_MI_Predicate_GICombiner22: {
    return matchFoldMergeToZext(*State.MIs[0], MRI);
  }
  case GICXXPred_MI_Predicate_GICombiner23: {
    return Helper.matchConstantFoldBinOp(*State.MIs[0], MatchInfos.MDInfo10);
  }
  case GICXXPred_MI_Predicate_GICombiner24: {
    return Helper.matchConstantFoldFPBinOp(*State.MIs[0], MatchInfos.MDInfo11);
  }
  case GICXXPred_MI_Predicate_GICombiner25: {
    return Helper.matchSelectSameVal(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner26: {
    return Helper.matchOperandIsZero(*State.MIs[0], 1);
  }
  case GICXXPred_MI_Predicate_GICombiner27: {
    return Helper.matchCombineI2PToP2I(*State.MIs[0], MatchInfos.MDInfo4);
  }
  case GICXXPred_MI_Predicate_GICombiner28: {
    MatchInfos.MDInfo4 = State.MIs[1]->getOperand(1).getReg(); return true;
    llvm_unreachable("GICombiner28 should have returned");
  }
  case GICXXPred_MI_Predicate_GICombiner29: {
    return Helper.matchCombineAnyExtTrunc(*State.MIs[0], MatchInfos.MDInfo4);
  }
  case GICXXPred_MI_Predicate_GICombiner30: {
    return Helper.matchConstantFPOp(State.MIs[0]->getOperand(2), 1.0);
  }
  case GICXXPred_MI_Predicate_GICombiner31: {
    return Helper.matchBuildVectorIdentityFold(*State.MIs[0], MatchInfos.MDInfo4);
  }
  case GICXXPred_MI_Predicate_GICombiner32: {
    return Helper.matchTruncBuildVectorFold(*State.MIs[0], MatchInfos.MDInfo4);
  }
  case GICXXPred_MI_Predicate_GICombiner33: {
    return Helper.matchTruncLshrBuildVectorFold(*State.MIs[0], MatchInfos.MDInfo4);
  }
  case GICXXPred_MI_Predicate_GICombiner34: {
    return MRI.getType(State.MIs[1]->getOperand(1).getReg()) == MRI.getType(State.MIs[0]->getOperand(0).getReg());
  }
  case GICXXPred_MI_Predicate_GICombiner35: {
    return Helper.matchConstantFPOp(State.MIs[0]->getOperand(2), -0.0);
  }
  case GICXXPred_MI_Predicate_GICombiner36: {
    return Helper.matchPtrAddImmedChain(*State.MIs[0], MatchInfos.MDInfo12);
  }
  case GICXXPred_MI_Predicate_GICombiner37: {
    return Helper.matchOverlappingAnd(*State.MIs[0], MatchInfos.MDInfo1);
  }
  case GICXXPred_MI_Predicate_GICombiner38: {
    return matchSplitStoreZero128(*State.MIs[0], MRI);
  }
  case GICXXPred_MI_Predicate_GICombiner39: {
    return Helper.matchAnyExplicitUseIsUndef(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner40: {
    return Helper.matchOperandIsUndef(*State.MIs[0], 1);
  }
  case GICXXPred_MI_Predicate_GICombiner41: {
    return Helper.matchOperandIsUndef(*State.MIs[0], 2);
  }
  case GICXXPred_MI_Predicate_GICombiner42: {
    return Helper.matchAllExplicitUsesAreUndef(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner43: {
    return Helper.matchUndefShuffleVectorMask(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner44: {
    return Helper.matchUndefStore(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner45: {
    return Helper.matchCombineUnmergeUndef(*State.MIs[0], MatchInfos.MDInfo1);
  }
  case GICXXPred_MI_Predicate_GICombiner46: {
    return Helper.matchInsertExtractVecEltOutOfBounds(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner47: {
    return Helper.matchSimplifySelectToMinMax(*State.MIs[0], MatchInfos.MDInfo1);
  }
  case GICXXPred_MI_Predicate_GICombiner48: {
    return matchOrToBSP(*State.MIs[0], MRI, MatchInfos.MDInfo13);
  }
  }
  llvm_unreachable("Unknown predicate");
  return false;
}
bool AArch64PostLegalizerCombinerImpl::testImmPredicate_I64(unsigned PredicateID, int64_t Imm) const {
  llvm_unreachable("Unknown predicate");
  return false;
}
bool AArch64PostLegalizerCombinerImpl::testImmPredicate_APFloat(unsigned PredicateID, const APFloat & Imm) const {
  llvm_unreachable("Unknown predicate");
  return false;
}
bool AArch64PostLegalizerCombinerImpl::testImmPredicate_APInt(unsigned PredicateID, const APInt & Imm) const {
  llvm_unreachable("Unknown predicate");
  return false;
}
enum {
  GICXXPred_Simple_IsRule0Enabled = GICXXPred_Invalid + 1,
  GICXXPred_Simple_IsRule1Enabled,
  GICXXPred_Simple_IsRule2Enabled,
  GICXXPred_Simple_IsRule3Enabled,
  GICXXPred_Simple_IsRule4Enabled,
  GICXXPred_Simple_IsRule5Enabled,
  GICXXPred_Simple_IsRule6Enabled,
  GICXXPred_Simple_IsRule7Enabled,
  GICXXPred_Simple_IsRule8Enabled,
  GICXXPred_Simple_IsRule9Enabled,
  GICXXPred_Simple_IsRule10Enabled,
  GICXXPred_Simple_IsRule11Enabled,
  GICXXPred_Simple_IsRule12Enabled,
  GICXXPred_Simple_IsRule13Enabled,
  GICXXPred_Simple_IsRule14Enabled,
  GICXXPred_Simple_IsRule15Enabled,
  GICXXPred_Simple_IsRule16Enabled,
  GICXXPred_Simple_IsRule17Enabled,
  GICXXPred_Simple_IsRule18Enabled,
  GICXXPred_Simple_IsRule19Enabled,
  GICXXPred_Simple_IsRule20Enabled,
  GICXXPred_Simple_IsRule21Enabled,
  GICXXPred_Simple_IsRule22Enabled,
  GICXXPred_Simple_IsRule23Enabled,
  GICXXPred_Simple_IsRule24Enabled,
  GICXXPred_Simple_IsRule25Enabled,
  GICXXPred_Simple_IsRule26Enabled,
  GICXXPred_Simple_IsRule27Enabled,
  GICXXPred_Simple_IsRule28Enabled,
  GICXXPred_Simple_IsRule29Enabled,
  GICXXPred_Simple_IsRule30Enabled,
  GICXXPred_Simple_IsRule31Enabled,
  GICXXPred_Simple_IsRule32Enabled,
  GICXXPred_Simple_IsRule33Enabled,
  GICXXPred_Simple_IsRule34Enabled,
  GICXXPred_Simple_IsRule35Enabled,
  GICXXPred_Simple_IsRule36Enabled,
  GICXXPred_Simple_IsRule37Enabled,
  GICXXPred_Simple_IsRule38Enabled,
  GICXXPred_Simple_IsRule39Enabled,
  GICXXPred_Simple_IsRule40Enabled,
  GICXXPred_Simple_IsRule41Enabled,
  GICXXPred_Simple_IsRule42Enabled,
  GICXXPred_Simple_IsRule43Enabled,
  GICXXPred_Simple_IsRule44Enabled,
  GICXXPred_Simple_IsRule45Enabled,
  GICXXPred_Simple_IsRule46Enabled,
  GICXXPred_Simple_IsRule47Enabled,
  GICXXPred_Simple_IsRule48Enabled,
  GICXXPred_Simple_IsRule49Enabled,
  GICXXPred_Simple_IsRule50Enabled,
  GICXXPred_Simple_IsRule51Enabled,
  GICXXPred_Simple_IsRule52Enabled,
  GICXXPred_Simple_IsRule53Enabled,
  GICXXPred_Simple_IsRule54Enabled,
  GICXXPred_Simple_IsRule55Enabled,
  GICXXPred_Simple_IsRule56Enabled,
  GICXXPred_Simple_IsRule57Enabled,
  GICXXPred_Simple_IsRule58Enabled,
  GICXXPred_Simple_IsRule59Enabled,
  GICXXPred_Simple_IsRule60Enabled,
};

bool AArch64PostLegalizerCombinerImpl::testSimplePredicate(unsigned Predicate) const {
    return RuleConfig.isRuleEnabled(Predicate - GICXXPred_Invalid - 1);
}
// Custom renderers.
enum {
  GICR_Invalid,
};
AArch64PostLegalizerCombinerImpl::CustomRendererFn
AArch64PostLegalizerCombinerImpl::CustomRenderers[] = {
  nullptr, // GICR_Invalid
};

bool AArch64PostLegalizerCombinerImpl::tryCombineAll(MachineInstr &I) const {
  const TargetSubtargetInfo &ST = MF.getSubtarget();
  const PredicateBitset AvailableFeatures = getAvailableFeatures();
  B.setInstrAndDebugLoc(I);
  State.MIs.clear();
  State.MIs.push_back(&I);
  MatchInfos = MatchInfosTy();

  if (executeMatchTable(*this, State, ExecInfo, B, getMatchTable(), *ST.getInstrInfo(), MRI, *MRI.getTargetRegisterInfo(), *ST.getRegBankInfo(), AvailableFeatures, /*CoverageInfo*/ nullptr)) {
    return true;
  }

  return false;
}

enum {
  GICXXCustomAction_CombineApplyGICombiner0 = GICXXCustomAction_Invalid + 1,
  GICXXCustomAction_CombineApplyGICombiner1,
  GICXXCustomAction_CombineApplyGICombiner2,
  GICXXCustomAction_CombineApplyGICombiner3,
  GICXXCustomAction_CombineApplyGICombiner4,
  GICXXCustomAction_CombineApplyGICombiner5,
  GICXXCustomAction_CombineApplyGICombiner6,
  GICXXCustomAction_CombineApplyGICombiner7,
  GICXXCustomAction_CombineApplyGICombiner8,
  GICXXCustomAction_CombineApplyGICombiner9,
  GICXXCustomAction_CombineApplyGICombiner10,
  GICXXCustomAction_CombineApplyGICombiner11,
  GICXXCustomAction_CombineApplyGICombiner12,
  GICXXCustomAction_CombineApplyGICombiner13,
  GICXXCustomAction_CombineApplyGICombiner14,
  GICXXCustomAction_CombineApplyGICombiner15,
  GICXXCustomAction_CombineApplyGICombiner16,
  GICXXCustomAction_CombineApplyGICombiner17,
  GICXXCustomAction_CombineApplyGICombiner18,
  GICXXCustomAction_CombineApplyGICombiner19,
  GICXXCustomAction_CombineApplyGICombiner20,
  GICXXCustomAction_CombineApplyGICombiner21,
  GICXXCustomAction_CombineApplyGICombiner22,
  GICXXCustomAction_CombineApplyGICombiner23,
  GICXXCustomAction_CombineApplyGICombiner24,
  GICXXCustomAction_CombineApplyGICombiner25,
  GICXXCustomAction_CombineApplyGICombiner26,
  GICXXCustomAction_CombineApplyGICombiner27,
  GICXXCustomAction_CombineApplyGICombiner28,
  GICXXCustomAction_CombineApplyGICombiner29,
};
void AArch64PostLegalizerCombinerImpl::runCustomAction(unsigned ApplyID, const MatcherState &State, NewMIVector &OutMIs) const {
  switch(ApplyID) {
  case GICXXCustomAction_CombineApplyGICombiner0:{
    Helper.applyCombineCopy(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner1:{
    Helper.applyCombineExtendingLoads(*State.MIs[0], MatchInfos.MDInfo0);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner2:{
    Helper.applyBuildFn(*State.MIs[0], MatchInfos.MDInfo1);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner3:{
    Helper.applyCombineIndexedLoadStore(*State.MIs[0], MatchInfos.MDInfo2);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner4:{
    Helper.applySextTruncSextLoad(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner5:{
    applyMutateAnyExtToZExt(*State.MIs[0], MRI, B, Observer);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner6:{
    Helper.applyBuildInstructionSteps(*State.MIs[0], MatchInfos.MDInfo3);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner7:{
    Helper.replaceSingleDefInstWithReg(*State.MIs[0], MatchInfos.MDInfo4);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner8:{
    Helper.applyXorOfAndWithSameReg(*State.MIs[0], MatchInfos.MDInfo5);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner9:{
    applyExtractVecEltPairwiseAdd(*State.MIs[0], MRI, B, MatchInfos.MDInfo6);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner10:{
    applyAArch64MulConstCombine(*State.MIs[0], MRI, B, MatchInfos.MDInfo7);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner11:{
    Helper.replaceSingleDefInstWithOperand(*State.MIs[0], 1);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner12:{
    Helper.applyRotateOutOfRange(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner13:{
    Helper.replaceInstWithConstant(*State.MIs[0], MatchInfos.MDInfo8);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner14:{
    Helper.replaceSingleDefInstWithOperand(*State.MIs[0], MatchInfos.MDInfo9);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner15:{
    applyFoldMergeToZext(*State.MIs[0], MRI, B, Observer);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner16:{
    Helper.replaceInstWithConstant(*State.MIs[0], MatchInfos.MDInfo10);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner17:{
    Helper.replaceInstWithFConstant(*State.MIs[0], MatchInfos.MDInfo11);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner18:{
    Helper.replaceSingleDefInstWithOperand(*State.MIs[0], 2);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner19:{
    Helper.applyCombineI2PToP2I(*State.MIs[0], MatchInfos.MDInfo4);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner20:{
    Helper.applyCombineP2IToI2P(*State.MIs[0], MatchInfos.MDInfo4);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner21:{
    Helper.replaceSingleDefInstWithReg(*State.MIs[0], State.MIs[1]->getOperand(1).getReg());
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner22:{
    Helper.applyPtrAddImmedChain(*State.MIs[0], MatchInfos.MDInfo12);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner23:{
    applySplitStoreZero128(*State.MIs[0], MRI, B, Observer);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner24:{
    Helper.replaceInstWithFConstant(*State.MIs[0], 0.0);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner25:{
    Helper.replaceInstWithConstant(*State.MIs[0], 0);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner26:{
    Helper.replaceInstWithConstant(*State.MIs[0], -1);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner27:{
    Helper.replaceInstWithUndef(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner28:{
    Helper.eraseInst(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner29:{
    applyOrToBSP(*State.MIs[0], MRI, B, MatchInfos.MDInfo13);
    return;
  }
}
  llvm_unreachable("Unknown Apply Action");
}
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define GIMT_Encode2(Val) uint8_t(Val), uint8_t((uint16_t)Val >> 8)
#define GIMT_Encode4(Val) uint8_t(Val), uint8_t((uint32_t)Val >> 8), uint8_t((uint32_t)Val >> 16), uint8_t((uint32_t)Val >> 24)
#define GIMT_Encode8(Val) uint8_t(Val), uint8_t((uint64_t)Val >> 8), uint8_t((uint64_t)Val >> 16), uint8_t((uint64_t)Val >> 24),  uint8_t((uint64_t)Val >> 32), uint8_t((uint64_t)Val >> 40), uint8_t((uint64_t)Val >> 48), uint8_t((uint64_t)Val >> 56)
#else
#define GIMT_Encode2(Val) uint8_t((uint16_t)Val >> 8), uint8_t(Val)
#define GIMT_Encode4(Val) uint8_t((uint32_t)Val >> 24), uint8_t((uint32_t)Val >> 16), uint8_t((uint32_t)Val >> 8), uint8_t(Val)
#define GIMT_Encode8(Val) uint8_t((uint64_t)Val >> 56), uint8_t((uint64_t)Val >> 48), uint8_t((uint64_t)Val >> 40), uint8_t((uint64_t)Val >> 32),  uint8_t((uint64_t)Val >> 24), uint8_t((uint64_t)Val >> 16), uint8_t((uint64_t)Val >> 8), uint8_t(Val)
#endif
const uint8_t *AArch64PostLegalizerCombinerImpl::getMatchTable() const {
  constexpr static uint8_t MatchTable0[] = {
    GIM_SwitchOpcode, /*MI*/0, /*[*/GIMT_Encode2(18), GIMT_Encode2(212), /*)*//*default:*//*Label 47*/ GIMT_Encode4(2823),
    /*TargetOpcode::COPY*//*Label 0*/ GIMT_Encode4(786), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_ADD*//*Label 1*/ GIMT_Encode4(803),
    /*TargetOpcode::G_SUB*//*Label 2*/ GIMT_Encode4(933),
    /*TargetOpcode::G_MUL*//*Label 3*/ GIMT_Encode4(991),
    /*TargetOpcode::G_SDIV*//*Label 4*/ GIMT_Encode4(1090),
    /*TargetOpcode::G_UDIV*//*Label 5*/ GIMT_Encode4(1107),
    /*TargetOpcode::G_SREM*//*Label 6*/ GIMT_Encode4(1140),
    /*TargetOpcode::G_UREM*//*Label 7*/ GIMT_Encode4(1157), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_AND*//*Label 8*/ GIMT_Encode4(1190),
    /*TargetOpcode::G_OR*//*Label 9*/ GIMT_Encode4(1329),
    /*TargetOpcode::G_XOR*//*Label 10*/ GIMT_Encode4(1461), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_UNMERGE_VALUES*//*Label 11*/ GIMT_Encode4(1551), GIMT_Encode4(0),
    /*TargetOpcode::G_MERGE_VALUES*//*Label 12*/ GIMT_Encode4(1568),
    /*TargetOpcode::G_BUILD_VECTOR*//*Label 13*/ GIMT_Encode4(1601),
    /*TargetOpcode::G_BUILD_VECTOR_TRUNC*//*Label 14*/ GIMT_Encode4(1618), GIMT_Encode4(0),
    /*TargetOpcode::G_PTRTOINT*//*Label 15*/ GIMT_Encode4(1635),
    /*TargetOpcode::G_INTTOPTR*//*Label 16*/ GIMT_Encode4(1662),
    /*TargetOpcode::G_BITCAST*//*Label 17*/ GIMT_Encode4(1679), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_LOAD*//*Label 18*/ GIMT_Encode4(1706),
    /*TargetOpcode::G_SEXTLOAD*//*Label 19*/ GIMT_Encode4(1739),
    /*TargetOpcode::G_ZEXTLOAD*//*Label 20*/ GIMT_Encode4(1772), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_STORE*//*Label 21*/ GIMT_Encode4(1805), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_ANYEXT*//*Label 22*/ GIMT_Encode4(1854),
    /*TargetOpcode::G_TRUNC*//*Label 23*/ GIMT_Encode4(1887), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_SEXT_INREG*//*Label 24*/ GIMT_Encode4(1936), GIMT_Encode4(0),
    /*TargetOpcode::G_SHL*//*Label 25*/ GIMT_Encode4(1985),
    /*TargetOpcode::G_LSHR*//*Label 26*/ GIMT_Encode4(2059),
    /*TargetOpcode::G_ASHR*//*Label 27*/ GIMT_Encode4(2149), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_ROTR*//*Label 28*/ GIMT_Encode4(2239),
    /*TargetOpcode::G_ROTL*//*Label 29*/ GIMT_Encode4(2281),
    /*TargetOpcode::G_ICMP*//*Label 30*/ GIMT_Encode4(2323), GIMT_Encode4(0),
    /*TargetOpcode::G_SELECT*//*Label 31*/ GIMT_Encode4(2340), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_FADD*//*Label 32*/ GIMT_Encode4(2436),
    /*TargetOpcode::G_FSUB*//*Label 33*/ GIMT_Encode4(2478),
    /*TargetOpcode::G_FMUL*//*Label 34*/ GIMT_Encode4(2495), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_FDIV*//*Label 35*/ GIMT_Encode4(2537), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_FNEG*//*Label 36*/ GIMT_Encode4(2554), GIMT_Encode4(0),
    /*TargetOpcode::G_FPTRUNC*//*Label 37*/ GIMT_Encode4(2586),
    /*TargetOpcode::G_FPTOSI*//*Label 38*/ GIMT_Encode4(2613),
    /*TargetOpcode::G_FPTOUI*//*Label 39*/ GIMT_Encode4(2630),
    /*TargetOpcode::G_SITOFP*//*Label 40*/ GIMT_Encode4(2647),
    /*TargetOpcode::G_UITOFP*//*Label 41*/ GIMT_Encode4(2664), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_PTR_ADD*//*Label 42*/ GIMT_Encode4(2681), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_ABS*//*Label 43*/ GIMT_Encode4(2723), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_INSERT_VECTOR_ELT*//*Label 44*/ GIMT_Encode4(2740),
    /*TargetOpcode::G_EXTRACT_VECTOR_ELT*//*Label 45*/ GIMT_Encode4(2757),
    /*TargetOpcode::G_SHUFFLE_VECTOR*//*Label 46*/ GIMT_Encode4(2790),
    // Label 0: @786
    GIM_Try, /*On fail goto*//*Label 48*/ GIMT_Encode4(802), // Rule ID 0 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule0Enabled),
      // MIs[0] d
      // No operand predicates
      // MIs[0] s
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner0),
      // Combiner Rule #0: copy_prop
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner0),
      GIR_Done,
    // Label 48: @802
    GIM_Reject,
    // Label 1: @803
    GIM_Try, /*On fail goto*//*Label 49*/ GIMT_Encode4(819), // Rule ID 34 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner23),
      // Combiner Rule #24: constant_fold_binop; wip_match_opcode 'G_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner16),
      GIR_Done,
    // Label 49: @819
    GIM_Try, /*On fail goto*//*Label 50*/ GIMT_Encode4(835), // Rule ID 95 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule53Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner39),
      // Combiner Rule #53: propagate_undef_any_op; wip_match_opcode 'G_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 50: @835
    GIM_Try, /*On fail goto*//*Label 51*/ GIMT_Encode4(871), // Rule ID 72 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule37Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] __add_sub_reg_match_0.tmp
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_SUB),
      // MIs[1] src
      // No operand predicates
      // MIs[1] __add_sub_reg_match_0.x
      // No operand predicates
      // MIs[0] __add_sub_reg_match_0.x
      GIM_CheckIsSameOperandIgnoreCopies, /*MI*/0, /*OpIdx*/2, /*OtherMI*/1, /*OtherOpIdx*/2,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/1, /*NewOpIdx*/1,
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #37: add_sub_reg @ [__add_sub_reg_match_0[1]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/1, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 51: @871
    GIM_Try, /*On fail goto*//*Label 52*/ GIMT_Encode4(907), // Rule ID 71 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule37Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] __add_sub_reg_match_0.x
      // No operand predicates
      // MIs[0] __add_sub_reg_match_0.tmp
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/2, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_SUB),
      // MIs[1] src
      // No operand predicates
      // MIs[1] __add_sub_reg_match_0.x
      GIM_CheckIsSameOperandIgnoreCopies, /*MI*/1, /*OpIdx*/2, /*OtherMI*/0, /*OtherOpIdx*/1,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/1, /*NewOpIdx*/1,
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #37: add_sub_reg @ [__add_sub_reg_match_0[0]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/1, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 52: @907
    GIM_Try, /*On fail goto*//*Label 53*/ GIMT_Encode4(932), // Rule ID 49 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule27Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #27: right_identity_zero @ [__right_identity_zero_match_0[1]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 53: @932
    GIM_Reject,
    // Label 2: @933
    GIM_Try, /*On fail goto*//*Label 54*/ GIMT_Encode4(949), // Rule ID 35 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner23),
      // Combiner Rule #24: constant_fold_binop; wip_match_opcode 'G_SUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner16),
      GIR_Done,
    // Label 54: @949
    GIM_Try, /*On fail goto*//*Label 55*/ GIMT_Encode4(965), // Rule ID 98 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule53Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner39),
      // Combiner Rule #53: propagate_undef_any_op; wip_match_opcode 'G_SUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 55: @965
    GIM_Try, /*On fail goto*//*Label 56*/ GIMT_Encode4(990), // Rule ID 48 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule27Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #27: right_identity_zero @ [__right_identity_zero_match_0[0]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 56: @990
    GIM_Reject,
    // Label 3: @991
    GIM_Try, /*On fail goto*//*Label 57*/ GIMT_Encode4(1007), // Rule ID 18 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule11Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner11),
      // Combiner Rule #11: mul_const; wip_match_opcode 'G_MUL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner10),
      GIR_Done,
    // Label 57: @1007
    GIM_Try, /*On fail goto*//*Label 58*/ GIMT_Encode4(1023), // Rule ID 36 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner23),
      // Combiner Rule #24: constant_fold_binop; wip_match_opcode 'G_MUL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner16),
      GIR_Done,
    // Label 58: @1023
    GIM_Try, /*On fail goto*//*Label 59*/ GIMT_Encode4(1039), // Rule ID 86 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule48Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner39),
      // Combiner Rule #48: undef_to_int_zero; wip_match_opcode 'G_MUL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner25),
      GIR_Done,
    // Label 59: @1039
    GIM_Try, /*On fail goto*//*Label 60*/ GIMT_Encode4(1064), // Rule ID 64 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule30Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] zero
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/2,
      // Combiner Rule #30: binop_right_to_zero
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/2,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 60: @1064
    GIM_Try, /*On fail goto*//*Label 61*/ GIMT_Encode4(1089), // Rule ID 69 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule35Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] x
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 1,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #35: right_identity_one_int
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 61: @1089
    GIM_Reject,
    // Label 4: @1090
    GIM_Try, /*On fail goto*//*Label 62*/ GIMT_Encode4(1106), // Rule ID 60 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule29Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner26),
      // Combiner Rule #29: binop_left_to_zero; wip_match_opcode 'G_SDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 62: @1106
    GIM_Reject,
    // Label 5: @1107
    GIM_Try, /*On fail goto*//*Label 63*/ GIMT_Encode4(1123), // Rule ID 61 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule29Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner26),
      // Combiner Rule #29: binop_left_to_zero; wip_match_opcode 'G_UDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 63: @1123
    GIM_Try, /*On fail goto*//*Label 64*/ GIMT_Encode4(1139), // Rule ID 89 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule50Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner40),
      // Combiner Rule #50: binop_left_undef_to_zero; wip_match_opcode 'G_UDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner25),
      GIR_Done,
    // Label 64: @1139
    GIM_Reject,
    // Label 6: @1140
    GIM_Try, /*On fail goto*//*Label 65*/ GIMT_Encode4(1156), // Rule ID 62 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule29Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner26),
      // Combiner Rule #29: binop_left_to_zero; wip_match_opcode 'G_SREM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 65: @1156
    GIM_Reject,
    // Label 7: @1157
    GIM_Try, /*On fail goto*//*Label 66*/ GIMT_Encode4(1173), // Rule ID 63 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule29Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner26),
      // Combiner Rule #29: binop_left_to_zero; wip_match_opcode 'G_UREM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 66: @1173
    GIM_Try, /*On fail goto*//*Label 67*/ GIMT_Encode4(1189), // Rule ID 90 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule50Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner40),
      // Combiner Rule #50: binop_left_undef_to_zero; wip_match_opcode 'G_UREM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner25),
      GIR_Done,
    // Label 67: @1189
    GIM_Reject,
    // Label 8: @1190
    GIM_Try, /*On fail goto*//*Label 68*/ GIMT_Encode4(1206), // Rule ID 4 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule2Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner2),
      // Combiner Rule #2: load_and_mask; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 68: @1206
    GIM_Try, /*On fail goto*//*Label 69*/ GIMT_Encode4(1222), // Rule ID 11 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule6Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner6),
      // Combiner Rule #6: hoist_logic_op_with_same_opcode_hands; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner6),
      GIR_Done,
    // Label 69: @1222
    GIM_Try, /*On fail goto*//*Label 70*/ GIMT_Encode4(1238), // Rule ID 14 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule7Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner7),
      // Combiner Rule #7: redundant_and; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 70: @1238
    GIM_Try, /*On fail goto*//*Label 71*/ GIMT_Encode4(1254), // Rule ID 21 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule14Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner14),
      // Combiner Rule #14: bitfield_extract_from_and; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 71: @1254
    GIM_Try, /*On fail goto*//*Label 72*/ GIMT_Encode4(1270), // Rule ID 37 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner23),
      // Combiner Rule #24: constant_fold_binop; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner16),
      GIR_Done,
    // Label 72: @1270
    GIM_Try, /*On fail goto*//*Label 73*/ GIMT_Encode4(1286), // Rule ID 81 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule45Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner37),
      // Combiner Rule #45: overlapping_and; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 73: @1286
    GIM_Try, /*On fail goto*//*Label 74*/ GIMT_Encode4(1302), // Rule ID 85 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule48Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner39),
      // Combiner Rule #48: undef_to_int_zero; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner25),
      GIR_Done,
    // Label 74: @1302
    GIM_Try, /*On fail goto*//*Label 75*/ GIMT_Encode4(1328), // Rule ID 58 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule28Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src
      // No operand predicates
      // MIs[0] src
      GIM_CheckIsSameOperandIgnoreCopies, /*MI*/0, /*OpIdx*/2, /*OtherMI*/0, /*OtherOpIdx*/1,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #28: binop_same_val @ [__binop_same_val_match_0[0]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 75: @1328
    GIM_Reject,
    // Label 9: @1329
    GIM_Try, /*On fail goto*//*Label 76*/ GIMT_Encode4(1345), // Rule ID 12 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule6Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner6),
      // Combiner Rule #6: hoist_logic_op_with_same_opcode_hands; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner6),
      GIR_Done,
    // Label 76: @1345
    GIM_Try, /*On fail goto*//*Label 77*/ GIMT_Encode4(1361), // Rule ID 17 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule10Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner10),
      // Combiner Rule #10: redundant_or; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 77: @1361
    GIM_Try, /*On fail goto*//*Label 78*/ GIMT_Encode4(1377), // Rule ID 38 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner23),
      // Combiner Rule #24: constant_fold_binop; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner16),
      GIR_Done,
    // Label 78: @1377
    GIM_Try, /*On fail goto*//*Label 79*/ GIMT_Encode4(1393), // Rule ID 87 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule49Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner39),
      // Combiner Rule #49: undef_to_negative_one; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner26),
      GIR_Done,
    // Label 79: @1393
    GIM_Try, /*On fail goto*//*Label 80*/ GIMT_Encode4(1409), // Rule ID 108 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule60Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner48),
      // Combiner Rule #60: or_to_bsp; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner29),
      GIR_Done,
    // Label 80: @1409
    GIM_Try, /*On fail goto*//*Label 81*/ GIMT_Encode4(1435), // Rule ID 59 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule28Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src
      // No operand predicates
      // MIs[0] src
      GIM_CheckIsSameOperandIgnoreCopies, /*MI*/0, /*OpIdx*/2, /*OtherMI*/0, /*OtherOpIdx*/1,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #28: binop_same_val @ [__binop_same_val_match_0[1]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 81: @1435
    GIM_Try, /*On fail goto*//*Label 82*/ GIMT_Encode4(1460), // Rule ID 50 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule27Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #27: right_identity_zero @ [__right_identity_zero_match_0[2]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 82: @1460
    GIM_Reject,
    // Label 10: @1461
    GIM_Try, /*On fail goto*//*Label 83*/ GIMT_Encode4(1477), // Rule ID 13 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule6Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner6),
      // Combiner Rule #6: hoist_logic_op_with_same_opcode_hands; wip_match_opcode 'G_XOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner6),
      GIR_Done,
    // Label 83: @1477
    GIM_Try, /*On fail goto*//*Label 84*/ GIMT_Encode4(1493), // Rule ID 15 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule8Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner8),
      // Combiner Rule #8: xor_of_and_with_same_reg; wip_match_opcode 'G_XOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner8),
      GIR_Done,
    // Label 84: @1493
    GIM_Try, /*On fail goto*//*Label 85*/ GIMT_Encode4(1509), // Rule ID 39 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner23),
      // Combiner Rule #24: constant_fold_binop; wip_match_opcode 'G_XOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner16),
      GIR_Done,
    // Label 85: @1509
    GIM_Try, /*On fail goto*//*Label 86*/ GIMT_Encode4(1525), // Rule ID 99 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule53Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner39),
      // Combiner Rule #53: propagate_undef_any_op; wip_match_opcode 'G_XOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 86: @1525
    GIM_Try, /*On fail goto*//*Label 87*/ GIMT_Encode4(1550), // Rule ID 51 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule27Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #27: right_identity_zero @ [__right_identity_zero_match_0[3]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 87: @1550
    GIM_Reject,
    // Label 11: @1551
    GIM_Try, /*On fail goto*//*Label 88*/ GIMT_Encode4(1567), // Rule ID 104 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule57Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner45),
      // Combiner Rule #57: unmerge_undef; wip_match_opcode 'G_UNMERGE_VALUES'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 88: @1567
    GIM_Reject,
    // Label 12: @1568
    GIM_Try, /*On fail goto*//*Label 89*/ GIMT_Encode4(1584), // Rule ID 29 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule19Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner19),
      // Combiner Rule #19: merge_unmerge; wip_match_opcode 'G_MERGE_VALUES'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 89: @1584
    GIM_Try, /*On fail goto*//*Label 90*/ GIMT_Encode4(1600), // Rule ID 33 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule23Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner22),
      // Combiner Rule #23: fold_merge_to_zext; wip_match_opcode 'G_MERGE_VALUES'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner15),
      GIR_Done,
    // Label 90: @1600
    GIM_Reject,
    // Label 13: @1601
    GIM_Try, /*On fail goto*//*Label 91*/ GIMT_Encode4(1617), // Rule ID 74 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule38Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner31),
      // Combiner Rule #38: buildvector_identity_fold; wip_match_opcode 'G_BUILD_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 91: @1617
    GIM_Reject,
    // Label 14: @1618
    GIM_Try, /*On fail goto*//*Label 92*/ GIMT_Encode4(1634), // Rule ID 73 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule38Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner31),
      // Combiner Rule #38: buildvector_identity_fold; wip_match_opcode 'G_BUILD_VECTOR_TRUNC'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 92: @1634
    GIM_Reject,
    // Label 15: @1635
    GIM_Try, /*On fail goto*//*Label 93*/ GIMT_Encode4(1661), // Rule ID 66 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule32Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] t
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_INTTOPTR),
      // MIs[1] ptr
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner28),
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #32: i2p_to_p2i
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner20),
      GIR_Done,
    // Label 93: @1661
    GIM_Reject,
    // Label 16: @1662
    GIM_Try, /*On fail goto*//*Label 94*/ GIMT_Encode4(1678), // Rule ID 65 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule31Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner27),
      // Combiner Rule #31: p2i_to_i2p; wip_match_opcode 'G_INTTOPTR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner19),
      GIR_Done,
    // Label 94: @1678
    GIM_Reject,
    // Label 17: @1679
    GIM_Try, /*On fail goto*//*Label 95*/ GIMT_Encode4(1705), // Rule ID 77 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule41Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src1
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_BITCAST),
      // MIs[1] src0
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner34),
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #41: bitcast_bitcast_fold
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner21),
      GIR_Done,
    // Label 95: @1705
    GIM_Reject,
    // Label 18: @1706
    GIM_Try, /*On fail goto*//*Label 96*/ GIMT_Encode4(1722), // Rule ID 1 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule1Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner1),
      // Combiner Rule #1: extending_loads; wip_match_opcode 'G_LOAD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner1),
      GIR_Done,
    // Label 96: @1722
    GIM_Try, /*On fail goto*//*Label 97*/ GIMT_Encode4(1738), // Rule ID 5 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule3Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner3),
      // Combiner Rule #3: combine_indexed_load_store; wip_match_opcode 'G_LOAD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner3),
      GIR_Done,
    // Label 97: @1738
    GIM_Reject,
    // Label 19: @1739
    GIM_Try, /*On fail goto*//*Label 98*/ GIMT_Encode4(1755), // Rule ID 2 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule1Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner1),
      // Combiner Rule #1: extending_loads; wip_match_opcode 'G_SEXTLOAD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner1),
      GIR_Done,
    // Label 98: @1755
    GIM_Try, /*On fail goto*//*Label 99*/ GIMT_Encode4(1771), // Rule ID 6 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule3Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner3),
      // Combiner Rule #3: combine_indexed_load_store; wip_match_opcode 'G_SEXTLOAD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner3),
      GIR_Done,
    // Label 99: @1771
    GIM_Reject,
    // Label 20: @1772
    GIM_Try, /*On fail goto*//*Label 100*/ GIMT_Encode4(1788), // Rule ID 3 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule1Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner1),
      // Combiner Rule #1: extending_loads; wip_match_opcode 'G_ZEXTLOAD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner1),
      GIR_Done,
    // Label 100: @1788
    GIM_Try, /*On fail goto*//*Label 101*/ GIMT_Encode4(1804), // Rule ID 7 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule3Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner3),
      // Combiner Rule #3: combine_indexed_load_store; wip_match_opcode 'G_ZEXTLOAD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner3),
      GIR_Done,
    // Label 101: @1804
    GIM_Reject,
    // Label 21: @1805
    GIM_Try, /*On fail goto*//*Label 102*/ GIMT_Encode4(1821), // Rule ID 8 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule3Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner3),
      // Combiner Rule #3: combine_indexed_load_store; wip_match_opcode 'G_STORE'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner3),
      GIR_Done,
    // Label 102: @1821
    GIM_Try, /*On fail goto*//*Label 103*/ GIMT_Encode4(1837), // Rule ID 82 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule46Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner38),
      // Combiner Rule #46: split_store_zero_128; wip_match_opcode 'G_STORE'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner23),
      GIR_Done,
    // Label 103: @1837
    GIM_Try, /*On fail goto*//*Label 104*/ GIMT_Encode4(1853), // Rule ID 103 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule56Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner44),
      // Combiner Rule #56: erase_undef_store; wip_match_opcode 'G_STORE'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner28),
      GIR_Done,
    // Label 104: @1853
    GIM_Reject,
    // Label 22: @1854
    GIM_Try, /*On fail goto*//*Label 105*/ GIMT_Encode4(1870), // Rule ID 10 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule5Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner5),
      // Combiner Rule #5: mutate_anyext_to_zext; wip_match_opcode 'G_ANYEXT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner5),
      GIR_Done,
    // Label 105: @1870
    GIM_Try, /*On fail goto*//*Label 106*/ GIMT_Encode4(1886), // Rule ID 67 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule33Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner29),
      // Combiner Rule #33: anyext_trunc_fold; wip_match_opcode 'G_ANYEXT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 106: @1886
    GIM_Reject,
    // Label 23: @1887
    GIM_Try, /*On fail goto*//*Label 107*/ GIMT_Encode4(1903), // Rule ID 75 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule39Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner32),
      // Combiner Rule #39: trunc_buildvector_fold; wip_match_opcode 'G_TRUNC'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 107: @1903
    GIM_Try, /*On fail goto*//*Label 108*/ GIMT_Encode4(1919), // Rule ID 76 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule40Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner33),
      // Combiner Rule #40: trunc_lshr_buildvector_fold; wip_match_opcode 'G_TRUNC'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 108: @1919
    GIM_Try, /*On fail goto*//*Label 109*/ GIMT_Encode4(1935), // Rule ID 100 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule53Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner39),
      // Combiner Rule #53: propagate_undef_any_op; wip_match_opcode 'G_TRUNC'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 109: @1935
    GIM_Reject,
    // Label 24: @1936
    GIM_Try, /*On fail goto*//*Label 110*/ GIMT_Encode4(1952), // Rule ID 9 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule4Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner4),
      // Combiner Rule #4: sext_trunc_sextload; wip_match_opcode 'G_SEXT_INREG'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner4),
      GIR_Done,
    // Label 110: @1952
    GIM_Try, /*On fail goto*//*Label 111*/ GIMT_Encode4(1968), // Rule ID 19 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule12Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner12),
      // Combiner Rule #12: redundant_sext_inreg; wip_match_opcode 'G_SEXT_INREG'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 111: @1968
    GIM_Try, /*On fail goto*//*Label 112*/ GIMT_Encode4(1984), // Rule ID 20 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule13Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner13),
      // Combiner Rule #13: bitfield_extract_from_sext_inreg; wip_match_opcode 'G_SEXT_INREG'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 112: @1984
    GIM_Reject,
    // Label 25: @1985
    GIM_Try, /*On fail goto*//*Label 113*/ GIMT_Encode4(2001), // Rule ID 40 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner23),
      // Combiner Rule #24: constant_fold_binop; wip_match_opcode 'G_SHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner16),
      GIR_Done,
    // Label 113: @2001
    GIM_Try, /*On fail goto*//*Label 114*/ GIMT_Encode4(2017), // Rule ID 88 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule50Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner40),
      // Combiner Rule #50: binop_left_undef_to_zero; wip_match_opcode 'G_SHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner25),
      GIR_Done,
    // Label 114: @2017
    GIM_Try, /*On fail goto*//*Label 115*/ GIMT_Encode4(2033), // Rule ID 91 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule51Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner41),
      // Combiner Rule #51: binop_right_undef_to_undef; wip_match_opcode 'G_SHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 115: @2033
    GIM_Try, /*On fail goto*//*Label 116*/ GIMT_Encode4(2058), // Rule ID 52 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule27Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #27: right_identity_zero @ [__right_identity_zero_match_0[4]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 116: @2058
    GIM_Reject,
    // Label 26: @2059
    GIM_Try, /*On fail goto*//*Label 117*/ GIMT_Encode4(2075), // Rule ID 23 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule15Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner15),
      // Combiner Rule #15: bitfield_extract_from_shr; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 117: @2075
    GIM_Try, /*On fail goto*//*Label 118*/ GIMT_Encode4(2091), // Rule ID 25 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule16Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner16),
      // Combiner Rule #16: bitfield_extract_from_shr_and; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 118: @2091
    GIM_Try, /*On fail goto*//*Label 119*/ GIMT_Encode4(2107), // Rule ID 41 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner23),
      // Combiner Rule #24: constant_fold_binop; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner16),
      GIR_Done,
    // Label 119: @2107
    GIM_Try, /*On fail goto*//*Label 120*/ GIMT_Encode4(2123), // Rule ID 93 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule51Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner41),
      // Combiner Rule #51: binop_right_undef_to_undef; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 120: @2123
    GIM_Try, /*On fail goto*//*Label 121*/ GIMT_Encode4(2148), // Rule ID 54 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule27Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #27: right_identity_zero @ [__right_identity_zero_match_0[6]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 121: @2148
    GIM_Reject,
    // Label 27: @2149
    GIM_Try, /*On fail goto*//*Label 122*/ GIMT_Encode4(2165), // Rule ID 22 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule15Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner15),
      // Combiner Rule #15: bitfield_extract_from_shr; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 122: @2165
    GIM_Try, /*On fail goto*//*Label 123*/ GIMT_Encode4(2181), // Rule ID 24 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule16Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner16),
      // Combiner Rule #16: bitfield_extract_from_shr_and; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 123: @2181
    GIM_Try, /*On fail goto*//*Label 124*/ GIMT_Encode4(2197), // Rule ID 42 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner23),
      // Combiner Rule #24: constant_fold_binop; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner16),
      GIR_Done,
    // Label 124: @2197
    GIM_Try, /*On fail goto*//*Label 125*/ GIMT_Encode4(2213), // Rule ID 92 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule51Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner41),
      // Combiner Rule #51: binop_right_undef_to_undef; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 125: @2213
    GIM_Try, /*On fail goto*//*Label 126*/ GIMT_Encode4(2238), // Rule ID 53 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule27Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #27: right_identity_zero @ [__right_identity_zero_match_0[5]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 126: @2238
    GIM_Reject,
    // Label 28: @2239
    GIM_Try, /*On fail goto*//*Label 127*/ GIMT_Encode4(2255), // Rule ID 26 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule17Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner17),
      // Combiner Rule #17: rotate_out_of_range; wip_match_opcode 'G_ROTR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner12),
      GIR_Done,
    // Label 127: @2255
    GIM_Try, /*On fail goto*//*Label 128*/ GIMT_Encode4(2280), // Rule ID 57 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule27Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #27: right_identity_zero @ [__right_identity_zero_match_0[9]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 128: @2280
    GIM_Reject,
    // Label 29: @2281
    GIM_Try, /*On fail goto*//*Label 129*/ GIMT_Encode4(2297), // Rule ID 27 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule17Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner17),
      // Combiner Rule #17: rotate_out_of_range; wip_match_opcode 'G_ROTL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner12),
      GIR_Done,
    // Label 129: @2297
    GIM_Try, /*On fail goto*//*Label 130*/ GIMT_Encode4(2322), // Rule ID 56 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule27Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #27: right_identity_zero @ [__right_identity_zero_match_0[8]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 130: @2322
    GIM_Reject,
    // Label 30: @2323
    GIM_Try, /*On fail goto*//*Label 131*/ GIMT_Encode4(2339), // Rule ID 28 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule18Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner18),
      // Combiner Rule #18: icmp_to_true_false_known_bits; wip_match_opcode 'G_ICMP'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner13),
      GIR_Done,
    // Label 131: @2339
    GIM_Reject,
    // Label 31: @2340
    GIM_Try, /*On fail goto*//*Label 132*/ GIMT_Encode4(2356), // Rule ID 31 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule21Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner20),
      // Combiner Rule #21: select_constant_cmp; wip_match_opcode 'G_SELECT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner14),
      GIR_Done,
    // Label 132: @2356
    GIM_Try, /*On fail goto*//*Label 133*/ GIMT_Encode4(2372), // Rule ID 32 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule22Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner21),
      // Combiner Rule #22: select_to_logical; wip_match_opcode 'G_SELECT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 133: @2372
    GIM_Try, /*On fail goto*//*Label 134*/ GIMT_Encode4(2388), // Rule ID 47 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule26Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner25),
      // Combiner Rule #26: select_same_val; wip_match_opcode 'G_SELECT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner18),
      GIR_Done,
    // Label 134: @2388
    GIM_Try, /*On fail goto*//*Label 135*/ GIMT_Encode4(2404), // Rule ID 107 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule59Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner47),
      // Combiner Rule #59: select_to_minmax; wip_match_opcode 'G_SELECT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 135: @2404
    GIM_Try, /*On fail goto*//*Label 136*/ GIMT_Encode4(2435), // Rule ID 30 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule20Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] undef
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_IMPLICIT_DEF),
      // MIs[0] x
      // No operand predicates
      // MIs[0] y
      // No operand predicates
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/3,
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #20: select_undef_cmp
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/3,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 136: @2435
    GIM_Reject,
    // Label 32: @2436
    GIM_Try, /*On fail goto*//*Label 137*/ GIMT_Encode4(2452), // Rule ID 43 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule25Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner24),
      // Combiner Rule #25: constant_fold_fp_binop; wip_match_opcode 'G_FADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 137: @2452
    GIM_Try, /*On fail goto*//*Label 138*/ GIMT_Encode4(2477), // Rule ID 79 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule43Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] x
      // No operand predicates
      // MIs[0] y
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner35),
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #43: right_identity_neg_zero_fp
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 138: @2477
    GIM_Reject,
    // Label 33: @2478
    GIM_Try, /*On fail goto*//*Label 139*/ GIMT_Encode4(2494), // Rule ID 44 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule25Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner24),
      // Combiner Rule #25: constant_fold_fp_binop; wip_match_opcode 'G_FSUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 139: @2494
    GIM_Reject,
    // Label 34: @2495
    GIM_Try, /*On fail goto*//*Label 140*/ GIMT_Encode4(2511), // Rule ID 45 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule25Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner24),
      // Combiner Rule #25: constant_fold_fp_binop; wip_match_opcode 'G_FMUL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 140: @2511
    GIM_Try, /*On fail goto*//*Label 141*/ GIMT_Encode4(2536), // Rule ID 70 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule36Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] x
      // No operand predicates
      // MIs[0] y
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner30),
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #36: right_identity_one_fp
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 141: @2536
    GIM_Reject,
    // Label 35: @2537
    GIM_Try, /*On fail goto*//*Label 142*/ GIMT_Encode4(2553), // Rule ID 46 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule25Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner24),
      // Combiner Rule #25: constant_fold_fp_binop; wip_match_opcode 'G_FDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 142: @2553
    GIM_Reject,
    // Label 36: @2554
    GIM_Try, /*On fail goto*//*Label 143*/ GIMT_Encode4(2585), // Rule ID 68 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule34Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] t
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FNEG),
      // MIs[1] src
      // No operand predicates
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/1, /*NewOpIdx*/1,
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #34: fneg_fneg_fold
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/1, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 143: @2585
    GIM_Reject,
    // Label 37: @2586
    GIM_Try, /*On fail goto*//*Label 144*/ GIMT_Encode4(2612), // Rule ID 78 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule42Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src1
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FPEXT),
      // MIs[1] src0
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner34),
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #42: fptrunc_fpext_fold
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner21),
      GIR_Done,
    // Label 144: @2612
    GIM_Reject,
    // Label 38: @2613
    GIM_Try, /*On fail goto*//*Label 145*/ GIMT_Encode4(2629), // Rule ID 96 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule53Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner39),
      // Combiner Rule #53: propagate_undef_any_op; wip_match_opcode 'G_FPTOSI'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 145: @2629
    GIM_Reject,
    // Label 39: @2630
    GIM_Try, /*On fail goto*//*Label 146*/ GIMT_Encode4(2646), // Rule ID 97 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule53Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner39),
      // Combiner Rule #53: propagate_undef_any_op; wip_match_opcode 'G_FPTOUI'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 146: @2646
    GIM_Reject,
    // Label 40: @2647
    GIM_Try, /*On fail goto*//*Label 147*/ GIMT_Encode4(2663), // Rule ID 84 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule47Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner39),
      // Combiner Rule #47: undef_to_fp_zero; wip_match_opcode 'G_SITOFP'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner24),
      GIR_Done,
    // Label 147: @2663
    GIM_Reject,
    // Label 41: @2664
    GIM_Try, /*On fail goto*//*Label 148*/ GIMT_Encode4(2680), // Rule ID 83 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule47Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner39),
      // Combiner Rule #47: undef_to_fp_zero; wip_match_opcode 'G_UITOFP'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner24),
      GIR_Done,
    // Label 148: @2680
    GIM_Reject,
    // Label 42: @2681
    GIM_Try, /*On fail goto*//*Label 149*/ GIMT_Encode4(2697), // Rule ID 80 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule44Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner36),
      // Combiner Rule #44: ptr_add_immed_chain; wip_match_opcode 'G_PTR_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 149: @2697
    GIM_Try, /*On fail goto*//*Label 150*/ GIMT_Encode4(2722), // Rule ID 55 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule27Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #27: right_identity_zero @ [__right_identity_zero_match_0[7]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 150: @2722
    GIM_Reject,
    // Label 43: @2723
    GIM_Try, /*On fail goto*//*Label 151*/ GIMT_Encode4(2739), // Rule ID 94 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule52Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner40),
      // Combiner Rule #52: unary_undef_to_zero; wip_match_opcode 'G_ABS'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner25),
      GIR_Done,
    // Label 151: @2739
    GIM_Reject,
    // Label 44: @2740
    GIM_Try, /*On fail goto*//*Label 152*/ GIMT_Encode4(2756), // Rule ID 105 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule58Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner46),
      // Combiner Rule #58: insert_extract_vec_elt_out_of_bounds; wip_match_opcode 'G_INSERT_VECTOR_ELT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 152: @2756
    GIM_Reject,
    // Label 45: @2757
    GIM_Try, /*On fail goto*//*Label 153*/ GIMT_Encode4(2773), // Rule ID 16 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule9Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #9: extractvecelt_pairwise_add; wip_match_opcode 'G_EXTRACT_VECTOR_ELT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner9),
      GIR_Done,
    // Label 153: @2773
    GIM_Try, /*On fail goto*//*Label 154*/ GIMT_Encode4(2789), // Rule ID 106 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule58Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner46),
      // Combiner Rule #58: insert_extract_vec_elt_out_of_bounds; wip_match_opcode 'G_EXTRACT_VECTOR_ELT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 154: @2789
    GIM_Reject,
    // Label 46: @2790
    GIM_Try, /*On fail goto*//*Label 155*/ GIMT_Encode4(2806), // Rule ID 101 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule54Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner42),
      // Combiner Rule #54: propagate_undef_all_ops; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 155: @2806
    GIM_Try, /*On fail goto*//*Label 156*/ GIMT_Encode4(2822), // Rule ID 102 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule55Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner43),
      // Combiner Rule #55: propagate_undef_shuffle_mask; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 156: @2822
    GIM_Reject,
    // Label 47: @2823
    GIM_Reject,
    }; // Size: 2824 bytes
  return MatchTable0;
}
#undef GIMT_Encode2
#undef GIMT_Encode4
#undef GIMT_Encode8

#endif // ifdef GET_GICOMBINER_IMPL

#ifdef GET_GICOMBINER_CONSTRUCTOR_INITS
AvailableModuleFeatures(computeAvailableModuleFeatures(&STI)),
AvailableFunctionFeatures()
#endif // ifdef GET_GICOMBINER_CONSTRUCTOR_INITS
#ifdef GET_GICOMBINER_CONSTRUCTOR_INITS
, State(0),
ExecInfo(TypeObjects, NumTypeObjects, FeatureBitsets, ComplexPredicateFns, CustomRenderers)
#endif // ifdef GET_GICOMBINER_CONSTRUCTOR_INITS

