/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* AArch64PostLegalizerLoweringImpl Combiner Match Table                      *|
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
struct AArch64PostLegalizerLoweringImplRuleConfig {
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
  case 3:	 // 6 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'd':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "up", 2) != 0)
        break;
      return 0;	 // "dup"
    case 'e':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "xt", 2) != 0)
        break;
      return 2;	 // "ext"
    case 'r':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ev", 2) != 0)
        break;
      return 1;	 // "rev"
    case 't':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "rn", 2) != 0)
        break;
      return 5;	 // "trn"
    case 'u':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "zp", 2) != 0)
        break;
      return 4;	 // "uzp"
    case 'z':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ip", 2) != 0)
        break;
      return 3;	 // "zip"
    }
    break;
  case 10:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "lower_mull", 10) != 0)
      break;
    return 16;	 // "lower_mull"
  case 11:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "shuf_to_ins", 11) != 0)
      break;
    return 7;	 // "shuf_to_ins"
  case 12:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "form_duplane", 12) != 0)
      break;
    return 6;	 // "form_duplane"
  case 15:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'a':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "djust_icmp_imm", 14) != 0)
        break;
      return 9;	 // "adjust_icmp_imm"
    case 'f':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "orm_truncstore", 14) != 0)
        break;
      return 13;	 // "form_truncstore"
    case 'v':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ashr_vlshr_imm", 14) != 0)
        break;
      return 8;	 // "vashr_vlshr_imm"
    }
    break;
  case 17:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "lower_vector_fcmp", 17) != 0)
      break;
    return 12;	 // "lower_vector_fcmp"
  case 18:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "swap_icmp_operands", 18) != 0)
      break;
    return 10;	 // "swap_icmp_operands"
  case 19:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "build_vector_to_dup", 19) != 0)
      break;
    return 11;	 // "build_vector_to_dup"
  case 22:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "unmerge_ext_to_unmerge", 22) != 0)
      break;
    return 15;	 // "unmerge_ext_to_unmerge"
  case 23:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "vector_unmerge_lowering", 23) != 0)
      break;
    return 17;	 // "vector_unmerge_lowering"
  case 26:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "vector_sext_inreg_to_shift", 26) != 0)
      break;
    return 14;	 // "vector_sext_inreg_to_shift"
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
    return {{0, 18}};
  }
  const auto I = getRuleIdxForIdentifier(RangePair.first);
  if (!I)
    return std::nullopt;
  return {{*I, *I + 1}};
}

bool AArch64PostLegalizerLoweringImplRuleConfig::setRuleEnabled(StringRef RuleIdentifier) {
  auto MaybeRange = getRuleRangeForIdentifier(RuleIdentifier);
  if (!MaybeRange)
    return false;
  for (auto I = MaybeRange->first; I < MaybeRange->second; ++I)
    DisabledRules.reset(I);
  return true;
}

bool AArch64PostLegalizerLoweringImplRuleConfig::setRuleDisabled(StringRef RuleIdentifier) {
  auto MaybeRange = getRuleRangeForIdentifier(RuleIdentifier);
  if (!MaybeRange)
    return false;
  for (auto I = MaybeRange->first; I < MaybeRange->second; ++I)
    DisabledRules.set(I);
  return true;
}

static std::vector<std::string> AArch64PostLegalizerLoweringOption;
static cl::list<std::string> AArch64PostLegalizerLoweringDisableOption(
    "aarch64postlegalizerlowering-disable-rule",
    cl::desc("Disable one or more combiner rules temporarily in the AArch64PostLegalizerLowering pass"),
    cl::CommaSeparated,
    cl::Hidden,
    cl::cat(GICombinerOptionCategory),
    cl::callback([](const std::string &Str) {
      AArch64PostLegalizerLoweringOption.push_back(Str);
    }));
static cl::list<std::string> AArch64PostLegalizerLoweringOnlyEnableOption(
    "aarch64postlegalizerlowering-only-enable-rule",
    cl::desc("Disable all rules in the AArch64PostLegalizerLowering pass then re-enable the specified ones"),
    cl::Hidden,
    cl::cat(GICombinerOptionCategory),
    cl::callback([](const std::string &CommaSeparatedArg) {
      StringRef Str = CommaSeparatedArg;
      AArch64PostLegalizerLoweringOption.push_back("*");
      do {
        auto X = Str.split(",");
        AArch64PostLegalizerLoweringOption.push_back(("!" + X.first).str());
        Str = X.second;
      } while (!Str.empty());
    }));


bool AArch64PostLegalizerLoweringImplRuleConfig::isRuleEnabled(unsigned RuleID) const {
    return  !DisabledRules.test(RuleID);
}
bool AArch64PostLegalizerLoweringImplRuleConfig::parseCommandLineOption() {
  for (StringRef Identifier : AArch64PostLegalizerLoweringOption) {
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
  typedef ComplexRendererFns(AArch64PostLegalizerLoweringImpl::*ComplexMatcherMemFn)(MachineOperand &) const;
  typedef void(AArch64PostLegalizerLoweringImpl::*CustomRendererFn)(MachineInstrBuilder &, const MachineInstr &, int) const;
  const ExecInfoTy<PredicateBitset, ComplexMatcherMemFn, CustomRendererFn> ExecInfo;
  static AArch64PostLegalizerLoweringImpl::ComplexMatcherMemFn ComplexPredicateFns[];
  static AArch64PostLegalizerLoweringImpl::CustomRendererFn CustomRenderers[];
  bool testImmPredicate_I64(unsigned PredicateID, int64_t Imm) const override;
  bool testImmPredicate_APInt(unsigned PredicateID, const APInt &Imm) const override;
  bool testImmPredicate_APFloat(unsigned PredicateID, const APFloat &Imm) const override;
  const uint8_t *getMatchTable() const override;
  bool testMIPredicate_MI(unsigned PredicateID, const MachineInstr &MI, const MatcherState &State) const override;
  bool testSimplePredicate(unsigned PredicateID) const override;
  void runCustomAction(unsigned FnID, const MatcherState &State, NewMIVector &OutMIs) const override;
  struct MatchInfosTy {
    std::pair<unsigned, int> MDInfo1;
    Register MDInfo5;
    ShuffleVectorPseudo MDInfo0;
    std::tuple<Register, int, Register, int> MDInfo2;
    int64_t MDInfo3;
    std::pair<uint64_t, CmpInst::Predicate> MDInfo4;
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

PredicateBitset AArch64PostLegalizerLoweringImpl::
computeAvailableModuleFeatures(const AArch64Subtarget *Subtarget) const {
  PredicateBitset Features;
  return Features;
}

void AArch64PostLegalizerLoweringImpl::setupGeneratedPerFunctionState(MachineFunction &MF) {
  AvailableFunctionFeatures = computeAvailableFunctionFeatures((const AArch64Subtarget *)&MF.getSubtarget(), &MF);
}
PredicateBitset AArch64PostLegalizerLoweringImpl::
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

AArch64PostLegalizerLoweringImpl::ComplexMatcherMemFn
AArch64PostLegalizerLoweringImpl::ComplexPredicateFns[] = {
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
};
bool AArch64PostLegalizerLoweringImpl::testMIPredicate_MI(unsigned PredicateID, const MachineInstr & MI, const MatcherState &State) const {
  switch (PredicateID) {
  case GICXXPred_MI_Predicate_GICombiner0: {
    return matchDup(*State.MIs[0], MRI, MatchInfos.MDInfo0);
  }
  case GICXXPred_MI_Predicate_GICombiner1: {
    return matchREV(*State.MIs[0], MRI, MatchInfos.MDInfo0);
  }
  case GICXXPred_MI_Predicate_GICombiner2: {
    return matchEXT(*State.MIs[0], MRI, MatchInfos.MDInfo0);
  }
  case GICXXPred_MI_Predicate_GICombiner3: {
    return matchZip(*State.MIs[0], MRI, MatchInfos.MDInfo0);
  }
  case GICXXPred_MI_Predicate_GICombiner4: {
    return matchUZP(*State.MIs[0], MRI, MatchInfos.MDInfo0);
  }
  case GICXXPred_MI_Predicate_GICombiner5: {
    return matchTRN(*State.MIs[0], MRI, MatchInfos.MDInfo0);
  }
  case GICXXPred_MI_Predicate_GICombiner6: {
    return matchDupLane(*State.MIs[0], MRI, MatchInfos.MDInfo1);
  }
  case GICXXPred_MI_Predicate_GICombiner7: {
    return matchINS(*State.MIs[0], MRI, MatchInfos.MDInfo2);
  }
  case GICXXPred_MI_Predicate_GICombiner8: {
    return matchVAshrLshrImm(*State.MIs[0], MRI, MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner9: {
    return matchAdjustICmpImmAndPred(*State.MIs[0], MRI, MatchInfos.MDInfo4);
  }
  case GICXXPred_MI_Predicate_GICombiner10: {
    return trySwapICmpOperands(*State.MIs[0], MRI);
  }
  case GICXXPred_MI_Predicate_GICombiner11: {
    return matchBuildVectorToDup(*State.MIs[0], MRI);
  }
  case GICXXPred_MI_Predicate_GICombiner12: {
    return matchLowerVectorFCMP(*State.MIs[0], MRI, B);
  }
  case GICXXPred_MI_Predicate_GICombiner13: {
    return matchFormTruncstore(*State.MIs[0], MRI, MatchInfos.MDInfo5);
  }
  case GICXXPred_MI_Predicate_GICombiner14: {
    return matchVectorSextInReg(*State.MIs[0], MRI);
  }
  case GICXXPred_MI_Predicate_GICombiner15: {
    return matchUnmergeExtToUnmerge(*State.MIs[0], MRI, MatchInfos.MDInfo5);
  }
  case GICXXPred_MI_Predicate_GICombiner16: {
    return matchExtMulToMULL(*State.MIs[0], MRI);
  }
  case GICXXPred_MI_Predicate_GICombiner17: {
    return matchScalarizeVectorUnmerge(*State.MIs[0], MRI);
  }
  }
  llvm_unreachable("Unknown predicate");
  return false;
}
bool AArch64PostLegalizerLoweringImpl::testImmPredicate_I64(unsigned PredicateID, int64_t Imm) const {
  llvm_unreachable("Unknown predicate");
  return false;
}
bool AArch64PostLegalizerLoweringImpl::testImmPredicate_APFloat(unsigned PredicateID, const APFloat & Imm) const {
  llvm_unreachable("Unknown predicate");
  return false;
}
bool AArch64PostLegalizerLoweringImpl::testImmPredicate_APInt(unsigned PredicateID, const APInt & Imm) const {
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
};

bool AArch64PostLegalizerLoweringImpl::testSimplePredicate(unsigned Predicate) const {
    return RuleConfig.isRuleEnabled(Predicate - GICXXPred_Invalid - 1);
}
// Custom renderers.
enum {
  GICR_Invalid,
};
AArch64PostLegalizerLoweringImpl::CustomRendererFn
AArch64PostLegalizerLoweringImpl::CustomRenderers[] = {
  nullptr, // GICR_Invalid
};

bool AArch64PostLegalizerLoweringImpl::tryCombineAll(MachineInstr &I) const {
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
};
void AArch64PostLegalizerLoweringImpl::runCustomAction(unsigned ApplyID, const MatcherState &State, NewMIVector &OutMIs) const {
  switch(ApplyID) {
  case GICXXCustomAction_CombineApplyGICombiner0:{
    applyShuffleVectorPseudo(*State.MIs[0], MatchInfos.MDInfo0);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner1:{
    applyEXT(*State.MIs[0], MatchInfos.MDInfo0);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner2:{
    applyDupLane(*State.MIs[0], MRI, B, MatchInfos.MDInfo1);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner3:{
    applyINS(*State.MIs[0], MRI, B, MatchInfos.MDInfo2);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner4:{
    applyVAshrLshrImm(*State.MIs[0], MRI, MatchInfos.MDInfo3);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner5:{
    applyAdjustICmpImmAndPred(*State.MIs[0], MatchInfos.MDInfo4, B, Observer);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner6:{
    applySwapICmpOperands(*State.MIs[0], Observer);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner7:{
    applyBuildVectorToDup(*State.MIs[0], MRI, B);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner8:{
    applyLowerVectorFCMP(*State.MIs[0], MRI, B);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner9:{
    applyFormTruncstore(*State.MIs[0], MRI, B, Observer, MatchInfos.MDInfo5);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner10:{
    applyVectorSextInReg(*State.MIs[0], MRI, B, Observer);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner11:{
    applyUnmergeExtToUnmerge(*State.MIs[0], MRI, B, Observer, MatchInfos.MDInfo5);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner12:{
    applyExtMulToMULL(*State.MIs[0], MRI, B, Observer);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner13:{
    applyScalarizeVectorUnmerge(*State.MIs[0], MRI, B);
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
const uint8_t *AArch64PostLegalizerLoweringImpl::getMatchTable() const {
  constexpr static uint8_t MatchTable0[] = {
    GIM_SwitchOpcode, /*MI*/0, /*[*/GIMT_Encode2(47), GIMT_Encode2(212), /*)*//*default:*//*Label 10*/ GIMT_Encode4(984),
    /*TargetOpcode::G_MUL*//*Label 0*/ GIMT_Encode4(670), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_UNMERGE_VALUES*//*Label 1*/ GIMT_Encode4(687), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_BUILD_VECTOR*//*Label 2*/ GIMT_Encode4(720), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_STORE*//*Label 3*/ GIMT_Encode4(737), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_SEXT_INREG*//*Label 4*/ GIMT_Encode4(754), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_LSHR*//*Label 5*/ GIMT_Encode4(771),
    /*TargetOpcode::G_ASHR*//*Label 6*/ GIMT_Encode4(788), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_ICMP*//*Label 7*/ GIMT_Encode4(805),
    /*TargetOpcode::G_FCMP*//*Label 8*/ GIMT_Encode4(838), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_SHUFFLE_VECTOR*//*Label 9*/ GIMT_Encode4(855),
    // Label 0: @670
    GIM_Try, /*On fail goto*//*Label 11*/ GIMT_Encode4(686), // Rule ID 17 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule16Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner16),
      // Combiner Rule #16: lower_mull; wip_match_opcode 'G_MUL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner12),
      GIR_Done,
    // Label 11: @686
    GIM_Reject,
    // Label 1: @687
    GIM_Try, /*On fail goto*//*Label 12*/ GIMT_Encode4(703), // Rule ID 16 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule15Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner15),
      // Combiner Rule #15: unmerge_ext_to_unmerge; wip_match_opcode 'G_UNMERGE_VALUES'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 12: @703
    GIM_Try, /*On fail goto*//*Label 13*/ GIMT_Encode4(719), // Rule ID 18 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule17Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner17),
      // Combiner Rule #17: vector_unmerge_lowering; wip_match_opcode 'G_UNMERGE_VALUES'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner13),
      GIR_Done,
    // Label 13: @719
    GIM_Reject,
    // Label 2: @720
    GIM_Try, /*On fail goto*//*Label 14*/ GIMT_Encode4(736), // Rule ID 12 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule11Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner11),
      // Combiner Rule #11: build_vector_to_dup; wip_match_opcode 'G_BUILD_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 14: @736
    GIM_Reject,
    // Label 3: @737
    GIM_Try, /*On fail goto*//*Label 15*/ GIMT_Encode4(753), // Rule ID 14 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule13Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner13),
      // Combiner Rule #13: form_truncstore; wip_match_opcode 'G_STORE'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner9),
      GIR_Done,
    // Label 15: @753
    GIM_Reject,
    // Label 4: @754
    GIM_Try, /*On fail goto*//*Label 16*/ GIMT_Encode4(770), // Rule ID 15 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule14Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner14),
      // Combiner Rule #14: vector_sext_inreg_to_shift; wip_match_opcode 'G_SEXT_INREG'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner10),
      GIR_Done,
    // Label 16: @770
    GIM_Reject,
    // Label 5: @771
    GIM_Try, /*On fail goto*//*Label 17*/ GIMT_Encode4(787), // Rule ID 9 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule8Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner8),
      // Combiner Rule #8: vashr_vlshr_imm; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner4),
      GIR_Done,
    // Label 17: @787
    GIM_Reject,
    // Label 6: @788
    GIM_Try, /*On fail goto*//*Label 18*/ GIMT_Encode4(804), // Rule ID 8 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule8Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner8),
      // Combiner Rule #8: vashr_vlshr_imm; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner4),
      GIR_Done,
    // Label 18: @804
    GIM_Reject,
    // Label 7: @805
    GIM_Try, /*On fail goto*//*Label 19*/ GIMT_Encode4(821), // Rule ID 10 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule9Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #9: adjust_icmp_imm; wip_match_opcode 'G_ICMP'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner5),
      GIR_Done,
    // Label 19: @821
    GIM_Try, /*On fail goto*//*Label 20*/ GIMT_Encode4(837), // Rule ID 11 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule10Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner10),
      // Combiner Rule #10: swap_icmp_operands; wip_match_opcode 'G_ICMP'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner6),
      GIR_Done,
    // Label 20: @837
    GIM_Reject,
    // Label 8: @838
    GIM_Try, /*On fail goto*//*Label 21*/ GIMT_Encode4(854), // Rule ID 13 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule12Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner12),
      // Combiner Rule #12: lower_vector_fcmp; wip_match_opcode 'G_FCMP'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner8),
      GIR_Done,
    // Label 21: @854
    GIM_Reject,
    // Label 9: @855
    GIM_Try, /*On fail goto*//*Label 22*/ GIMT_Encode4(871), // Rule ID 0 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule0Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner0),
      // Combiner Rule #0: dup; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner0),
      GIR_Done,
    // Label 22: @871
    GIM_Try, /*On fail goto*//*Label 23*/ GIMT_Encode4(887), // Rule ID 1 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule1Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner1),
      // Combiner Rule #1: rev; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner0),
      GIR_Done,
    // Label 23: @887
    GIM_Try, /*On fail goto*//*Label 24*/ GIMT_Encode4(903), // Rule ID 2 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule2Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner2),
      // Combiner Rule #2: ext; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner1),
      GIR_Done,
    // Label 24: @903
    GIM_Try, /*On fail goto*//*Label 25*/ GIMT_Encode4(919), // Rule ID 3 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule3Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner3),
      // Combiner Rule #3: zip; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner0),
      GIR_Done,
    // Label 25: @919
    GIM_Try, /*On fail goto*//*Label 26*/ GIMT_Encode4(935), // Rule ID 4 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule4Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner4),
      // Combiner Rule #4: uzp; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner0),
      GIR_Done,
    // Label 26: @935
    GIM_Try, /*On fail goto*//*Label 27*/ GIMT_Encode4(951), // Rule ID 5 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule5Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner5),
      // Combiner Rule #5: trn; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner0),
      GIR_Done,
    // Label 27: @951
    GIM_Try, /*On fail goto*//*Label 28*/ GIMT_Encode4(967), // Rule ID 6 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule6Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner6),
      // Combiner Rule #6: form_duplane; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 28: @967
    GIM_Try, /*On fail goto*//*Label 29*/ GIMT_Encode4(983), // Rule ID 7 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule7Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner7),
      // Combiner Rule #7: shuf_to_ins; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner3),
      GIR_Done,
    // Label 29: @983
    GIM_Reject,
    // Label 10: @984
    GIM_Reject,
    }; // Size: 985 bytes
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

