/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* AArch64PreLegalizerCombinerImpl Combiner Match Table                       *|
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
struct AArch64PreLegalizerCombinerImplRuleConfig {
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
  case 9:	 // 4 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'a':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ddo_by_0", 8) != 0)
        break;
      return 84;	 // "addo_by_0"
    case 'c':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "opy_prop", 8) != 0)
        break;
      return 0;	 // "copy_prop"
    case 'm':	 // 2 strings to match.
      if (memcmp(RuleIdentifier.data()+1, "ulo_by_", 7) != 0)
        break;
      switch (RuleIdentifier[8]) {
      default: break;
      case '0':	 // 1 string to match.
        return 83;	 // "mulo_by_0"
      case '2':	 // 1 string to match.
        return 82;	 // "mulo_by_2"
      }
      break;
    }
    break;
  case 10:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'i':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "2p_to_p2i", 9) != 0)
        break;
      return 29;	 // "i2p_to_p2i"
    case 'm':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ul_to_shl", 9) != 0)
        break;
      return 1;	 // "mul_to_shl"
    case 'p':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "2i_to_i2p", 9) != 0)
        break;
      return 28;	 // "p2i_to_i2p"
    }
    break;
  case 11:	 // 4 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'a':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "dd_sub_reg", 10) != 0)
        break;
      return 34;	 // "add_sub_reg"
    case 's':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ub_add_reg", 10) != 0)
        break;
      return 122;	 // "sub_add_reg"
    case 't':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "runc_shift", 10) != 0)
        break;
      return 72;	 // "trunc_shift"
    case 'u':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "nmerge_cst", 10) != 0)
        break;
      return 67;	 // "unmerge_cst"
    }
    break;
  case 12:	 // 6 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'a':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "dde_to_addo", 11) != 0)
        break;
      return 85;	 // "adde_to_addo"
    case 'e':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "xt_ext_fold", 11) != 0)
        break;
      return 63;	 // "ext_ext_fold"
    case 'f':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "sub_to_fneg", 11) != 0)
        break;
      return 125;	 // "fsub_to_fneg"
    case 'm':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ulh_to_lshr", 11) != 0)
        break;
      return 110;	 // "mulh_to_lshr"
    case 'n':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ot_cmp_fold", 11) != 0)
        break;
      return 64;	 // "not_cmp_fold"
    case 'r':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "edundant_or", 11) != 0)
        break;
      return 57;	 // "redundant_or"
    }
    break;
  case 13:	 // 8 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'c':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ommute_shift", 12) != 0)
        break;
      return 98;	 // "commute_shift"
    case 'l':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "oad_and_mask", 12) != 0)
        break;
      return 9;	 // "load_and_mask"
    case 'm':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "erge_unmerge", 12) != 0)
        break;
      return 70;	 // "merge_unmerge"
    case 'r':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "edundant_and", 12) != 0)
        break;
      return 55;	 // "redundant_and"
    case 's':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "div_by_const", 12) != 0)
        break;
      return 109;	 // "sdiv_by_const"
    case 'u':	 // 3 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'd':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "iv_by_const", 11) != 0)
          break;
        return 108;	 // "udiv_by_const"
      case 'n':	 // 2 strings to match.
        if (memcmp(RuleIdentifier.data()+2, "merge_", 6) != 0)
          break;
        switch (RuleIdentifier[8]) {
        default: break;
        case 'm':	 // 1 string to match.
          if (memcmp(RuleIdentifier.data()+9, "erge", 4) != 0)
            break;
          return 66;	 // "unmerge_merge"
        case 'u':	 // 1 string to match.
          if (memcmp(RuleIdentifier.data()+9, "ndef", 4) != 0)
            break;
          return 21;	 // "unmerge_undef"
        }
        break;
      }
      break;
    }
    break;
  case 14:	 // 7 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "inop_same_val", 13) != 0)
        break;
      return 25;	 // "binop_same_val"
    case 'f':	 // 2 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'a':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "bs_fneg_fold", 12) != 0)
          break;
        return 107;	 // "fabs_fneg_fold"
      case 'n':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "eg_fneg_fold", 12) != 0)
          break;
        return 31;	 // "fneg_fneg_fold"
      }
      break;
    case 'm':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ul_by_neg_one", 13) != 0)
        break;
      return 3;	 // "mul_by_neg_one"
    case 'r':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "eassoc_ptradd", 13) != 0)
        break;
      return 45;	 // "reassoc_ptradd"
    case 's':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "hifts_too_big", 13) != 0)
        break;
      return 44;	 // "shifts_too_big"
    case 't':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "runc_ext_fold", 13) != 0)
        break;
      return 71;	 // "trunc_ext_fold"
    }
    break;
  case 15:	 // 6 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'e':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "xtending_loads", 14) != 0)
        break;
      return 8;	 // "extending_loads"
    case 'i':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "dempotent_prop", 14) != 0)
        break;
      return 4;	 // "idempotent_prop"
    case 'l':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "oad_or_combine", 14) != 0)
        break;
      return 91;	 // "load_or_combine"
    case 'o':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "verlapping_and", 14) != 0)
        break;
      return 81;	 // "overlapping_and"
    case 's':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "elect_same_val", 14) != 0)
        break;
      return 23;	 // "select_same_val"
    case 'z':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ext_trunc_fold", 14) != 0)
        break;
      return 59;	 // "zext_trunc_fold"
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
        return 123;	 // "select_to_minmax"
      case 'u':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+8, "ndef_cmp", 8) != 0)
          break;
        return 52;	 // "select_undef_cmp"
      }
      break;
    case 'u':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ndef_to_fp_zero", 15) != 0)
        break;
      return 11;	 // "undef_to_fp_zero"
    }
    break;
  case 17:	 // 10 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'a':	 // 2 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'd':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "d_p2i_to_ptradd", 15) != 0)
          break;
        return 2;	 // "add_p2i_to_ptradd"
      case 'n':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "yext_trunc_fold", 15) != 0)
          break;
        return 30;	 // "anyext_trunc_fold"
      }
      break;
    case 'c':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "onstant_fold_fma", 16) != 0)
        break;
      return 105;	 // "constant_fold_fma"
    case 'd':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "iv_rem_to_divrem", 16) != 0)
        break;
      return 92;	 // "div_rem_to_divrem"
    case 'e':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "rase_undef_store", 16) != 0)
        break;
      return 20;	 // "erase_undef_store"
    case 'p':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "tr_add_with_zero", 16) != 0)
        break;
      return 88;	 // "ptr_add_with_zero"
    case 's':	 // 2 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'e':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "lect_to_logical", 15) != 0)
          break;
        return 54;	 // "select_to_logical"
      case 'h':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "ift_immed_chain", 15) != 0)
          break;
        return 89;	 // "shift_immed_chain"
      }
      break;
    case 'u':	 // 2 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'n':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "def_to_int_zero", 15) != 0)
          break;
        return 12;	 // "undef_to_int_zero"
      case 'r':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "em_pow2_to_mask", 15) != 0)
          break;
        return 58;	 // "urem_pow2_to_mask"
      }
      break;
    }
    break;
  case 18:	 // 8 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "inop_left_to_zero", 17) != 0)
        break;
      return 26;	 // "binop_left_to_zero"
    case 'c':	 // 3 strings to match.
      if (RuleIdentifier[1] != 'o')
        break;
      switch (RuleIdentifier[2]) {
      default: break;
      case 'm':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+3, "bine_minmax_nan", 15) != 0)
          break;
        return 86;	 // "combine_minmax_nan"
      case 'n':	 // 2 strings to match.
        if (memcmp(RuleIdentifier.data()+3, "stant_fold_f", 12) != 0)
          break;
        switch (RuleIdentifier[15]) {
        default: break;
        case 'a':	 // 1 string to match.
          if (memcmp(RuleIdentifier.data()+16, "bs", 2) != 0)
            break;
          return 74;	 // "constant_fold_fabs"
        case 'n':	 // 1 string to match.
          if (memcmp(RuleIdentifier.data()+16, "eg", 2) != 0)
            break;
          return 73;	 // "constant_fold_fneg"
        }
        break;
      }
      break;
    case 'f':	 // 2 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'o':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "ld_global_offset", 16) != 0)
          break;
        return 130;	 // "fold_global_offset"
      case 'p':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "trunc_fpext_fold", 16) != 0)
          break;
        return 39;	 // "fptrunc_fpext_fold"
      }
      break;
    case 's':	 // 2 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'e':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "xt_inreg_of_load", 16) != 0)
          break;
        return 49;	 // "sext_inreg_of_load"
      case 'h':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "uffle_to_extract", 16) != 0)
          break;
        return 131;	 // "shuffle_to_extract"
      }
      break;
    }
    break;
  case 19:	 // 12 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "inop_right_to_zero", 18) != 0)
        break;
      return 27;	 // "binop_right_to_zero"
    case 'c':	 // 4 strings to match.
      if (memcmp(RuleIdentifier.data()+1, "onst", 4) != 0)
        break;
      switch (RuleIdentifier[5]) {
      default: break;
      case '_':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+6, "ptradd_to_i2p", 13) != 0)
          break;
        return 80;	 // "const_ptradd_to_i2p"
      case 'a':	 // 3 strings to match.
        if (memcmp(RuleIdentifier.data()+6, "nt_fold_", 8) != 0)
          break;
        switch (RuleIdentifier[14]) {
        default: break;
        case 'b':	 // 1 string to match.
          if (memcmp(RuleIdentifier.data()+15, "inop", 4) != 0)
            break;
          return 103;	 // "constant_fold_binop"
        case 'f':	 // 2 strings to match.
          switch (RuleIdentifier[15]) {
          default: break;
          case 'l':	 // 1 string to match.
            if (memcmp(RuleIdentifier.data()+16, "og2", 3) != 0)
              break;
            return 76;	 // "constant_fold_flog2"
          case 's':	 // 1 string to match.
            if (memcmp(RuleIdentifier.data()+16, "qrt", 3) != 0)
              break;
            return 75;	 // "constant_fold_fsqrt"
          }
          break;
        }
        break;
      }
      break;
    case 'e':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "xtend_through_phis", 18) != 0)
        break;
      return 41;	 // "extend_through_phis"
    case 'p':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "tr_add_immed_chain", 18) != 0)
        break;
      return 47;	 // "ptr_add_immed_chain"
    case 'r':	 // 2 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'e':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "assoc_comm_binops", 17) != 0)
          break;
        return 46;	 // "reassoc_comm_binops"
      case 'i':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "ght_identity_zero", 17) != 0)
          break;
        return 24;	 // "right_identity_zero"
      }
      break;
    case 's':	 // 2 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'e':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "lect_constant_cmp", 17) != 0)
          break;
        return 53;	 // "select_constant_cmp"
      case 'i':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "mplify_add_to_sub", 17) != 0)
          break;
        return 42;	 // "simplify_add_to_sub"
      }
      break;
    case 'u':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "nary_undef_to_zero", 18) != 0)
        break;
      return 16;	 // "unary_undef_to_zero"
    }
    break;
  case 20:	 // 6 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'a':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "nd_or_disjoint_mask", 19) != 0)
        break;
      return 112;	 // "and_or_disjoint_mask"
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "itcast_bitcast_fold", 19) != 0)
        break;
      return 38;	 // "bitcast_bitcast_fold"
    case 'i':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "cmp_redundant_trunc", 19) != 0)
        break;
      return 129;	 // "icmp_redundant_trunc"
    case 'r':	 // 2 strings to match.
      if (memcmp(RuleIdentifier.data()+1, "edu", 3) != 0)
        break;
      switch (RuleIdentifier[4]) {
      default: break;
      case 'c':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+5, "e_shl_of_extend", 15) != 0)
          break;
        return 50;	 // "reduce_shl_of_extend"
      case 'n':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+5, "dant_sext_inreg", 15) != 0)
          break;
        return 56;	 // "redundant_sext_inreg"
      }
      break;
    case 'u':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "nmerge_zext_to_zext", 19) != 0)
        break;
      return 69;	 // "unmerge_zext_to_zext"
    }
    break;
  case 21:	 // 7 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'c':	 // 2 strings to match.
      if (memcmp(RuleIdentifier.data()+1, "onstant_fold_", 13) != 0)
        break;
      switch (RuleIdentifier[14]) {
      default: break;
      case 'c':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+15, "ast_op", 6) != 0)
          break;
        return 106;	 // "constant_fold_cast_op"
      case 'f':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+15, "ptrunc", 6) != 0)
          break;
        return 77;	 // "constant_fold_fptrunc"
      }
      break;
    case 'e':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "xt_addv_to_udot_addv", 20) != 0)
        break;
      return 132;	 // "ext_addv_to_udot_addv"
    case 'f':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "constant_to_constant", 20) != 0)
        break;
      return 128;	 // "fconstant_to_constant"
    case 'r':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ight_identity_one_fp", 20) != 0)
        break;
      return 33;	 // "right_identity_one_fp"
    case 'u':	 // 2 strings to match.
      if (RuleIdentifier[1] != 'n')
        break;
      switch (RuleIdentifier[2]) {
      default: break;
      case 'd':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+3, "ef_to_negative_one", 18) != 0)
          break;
        return 13;	 // "undef_to_negative_one"
      case 'm':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+3, "erge_dead_to_trunc", 18) != 0)
          break;
        return 68;	 // "unmerge_dead_to_trunc"
      }
      break;
    }
    break;
  case 22:	 // 11 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'c':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "onstant_fold_fp_binop", 21) != 0)
        break;
      return 104;	 // "constant_fold_fp_binop"
    case 'f':	 // 4 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'o':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "ld_binop_into_select", 20) != 0)
          break;
        return 121;	 // "fold_binop_into_select"
      case 'u':	 // 3 strings to match.
        if (memcmp(RuleIdentifier.data()+2, "nnel_shift_", 11) != 0)
          break;
        switch (RuleIdentifier[13]) {
        default: break;
        case 'l':	 // 1 string to match.
          if (memcmp(RuleIdentifier.data()+14, "eft_zero", 8) != 0)
            break;
          return 96;	 // "funnel_shift_left_zero"
        case 'o':	 // 1 string to match.
          if (memcmp(RuleIdentifier.data()+14, "vershift", 8) != 0)
            break;
          return 97;	 // "funnel_shift_overshift"
        case 't':	 // 1 string to match.
          if (memcmp(RuleIdentifier.data()+14, "o_rotate", 8) != 0)
            break;
          return 94;	 // "funnel_shift_to_rotate"
        }
        break;
      }
      break;
    case 'i':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "cmp_to_lhs_known_bits", 21) != 0)
        break;
      return 61;	 // "icmp_to_lhs_known_bits"
    case 'p':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ropagate_undef_any_op", 21) != 0)
        break;
      return 17;	 // "propagate_undef_any_op"
    case 'r':	 // 2 strings to match.
      switch (RuleIdentifier[1]) {
      default: break;
      case 'e':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "dundant_neg_operands", 20) != 0)
          break;
        return 111;	 // "redundant_neg_operands"
      case 'i':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "ght_identity_one_int", 20) != 0)
          break;
        return 32;	 // "right_identity_one_int"
      }
      break;
    case 's':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "hl_ashr_to_sext_inreg", 21) != 0)
        break;
      return 48;	 // "shl_ashr_to_sext_inreg"
    case 't':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "runc_buildvector_fold", 21) != 0)
        break;
      return 36;	 // "trunc_buildvector_fold"
    }
    break;
  case 23:	 // 4 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'f':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "unnel_shift_right_zero", 22) != 0)
        break;
      return 95;	 // "funnel_shift_right_zero"
    case 'i':	 // 2 strings to match.
      if (memcmp(RuleIdentifier.data()+1, "tof_const_zero_fold_", 20) != 0)
        break;
      switch (RuleIdentifier[21]) {
      default: break;
      case 's':	 // 1 string to match.
        if (RuleIdentifier[22] != 'i')
          break;
        return 78;	 // "itof_const_zero_fold_si"
      case 'u':	 // 1 string to match.
        if (RuleIdentifier[22] != 'i')
          break;
        return 79;	 // "itof_const_zero_fold_ui"
      }
      break;
    case 'p':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ropagate_undef_all_ops", 22) != 0)
        break;
      return 18;	 // "propagate_undef_all_ops"
    }
    break;
  case 24:	 // 4 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "inop_left_undef_to_zero", 23) != 0)
        break;
      return 14;	 // "binop_left_undef_to_zero"
    case 'n':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "arrow_binop_feeding_and", 23) != 0)
        break;
      return 51;	 // "narrow_binop_feeding_and"
    case 's':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ext_inreg_to_zext_inreg", 23) != 0)
        break;
      return 62;	 // "sext_inreg_to_zext_inreg"
    case 'x':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "or_of_and_with_same_reg", 23) != 0)
        break;
      return 87;	 // "xor_of_and_with_same_reg"
    }
    break;
  case 25:	 // 4 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 3 strings to match.
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
          return 100;	 // "bitfield_extract_from_and"
        case 's':	 // 1 string to match.
          if (memcmp(RuleIdentifier.data()+23, "hr", 2) != 0)
            break;
          return 101;	 // "bitfield_extract_from_shr"
        }
        break;
      case 'u':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+2, "ildvector_identity_fold", 23) != 0)
          break;
        return 35;	 // "buildvector_identity_fold"
      }
      break;
    case 'e':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "xtract_vec_elt_build_vec", 24) != 0)
        break;
      return 6;	 // "extract_vec_elt_build_vec"
    }
    break;
  case 26:	 // 4 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "inop_right_undef_to_undef", 25) != 0)
        break;
      return 15;	 // "binop_right_undef_to_undef"
    case 'c':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ommute_fp_constant_to_rhs", 25) != 0)
        break;
      return 127;	 // "commute_fp_constant_to_rhs"
    case 'f':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "unnel_shift_from_or_shift", 25) != 0)
        break;
      return 93;	 // "funnel_shift_from_or_shift"
    case 'r':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ight_identity_neg_zero_fp", 25) != 0)
        break;
      return 40;	 // "right_identity_neg_zero_fp"
    }
    break;
  case 27:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'c':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ommute_int_constant_to_rhs", 26) != 0)
        break;
      return 126;	 // "commute_int_constant_to_rhs"
    case 'r':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "edundant_binop_in_equality", 26) != 0)
        break;
      return 124;	 // "redundant_binop_in_equality"
    case 't':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "runc_lshr_buildvector_fold", 26) != 0)
        break;
      return 37;	 // "trunc_lshr_buildvector_fold"
    }
    break;
  case 28:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'o':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "pt_brcond_by_inverting_cond", 27) != 0)
        break;
      return 65;	 // "opt_brcond_by_inverting_cond"
    case 'p':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ropagate_undef_shuffle_mask", 27) != 0)
        break;
      return 19;	 // "propagate_undef_shuffle_mask"
    case 's':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "hift_of_shifted_logic_chain", 27) != 0)
        break;
      return 90;	 // "shift_of_shifted_logic_chain"
    }
    break;
  case 29:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "itfield_extract_from_shr_and", 28) != 0)
        break;
      return 102;	 // "bitfield_extract_from_shr_and"
    case 'c':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ombine_extracted_vector_load", 28) != 0)
        break;
      return 10;	 // "combine_extracted_vector_load"
    case 'i':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "cmp_to_true_false_known_bits", 28) != 0)
        break;
      return 60;	 // "icmp_to_true_false_known_bits"
    }
    break;
  case 32:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'b':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "itfield_extract_from_sext_inreg", 31) != 0)
        break;
      return 99;	 // "bitfield_extract_from_sext_inreg"
    case 'c':	 // 2 strings to match.
      if (memcmp(RuleIdentifier.data()+1, "ombine_f", 8) != 0)
        break;
      switch (RuleIdentifier[9]) {
      default: break;
      case 'a':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+10, "dd_fmul_to_fmad_or_fma", 22) != 0)
          break;
        return 113;	 // "combine_fadd_fmul_to_fmad_or_fma"
      case 's':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+10, "ub_fmul_to_fmad_or_fma", 22) != 0)
          break;
        return 117;	 // "combine_fsub_fmul_to_fmad_or_fma"
      }
      break;
    }
    break;
  case 34:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "extract_all_elts_from_build_vector", 34) != 0)
      break;
    return 7;	 // "extract_all_elts_from_build_vector"
  case 36:	 // 3 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'c':	 // 2 strings to match.
      if (memcmp(RuleIdentifier.data()+1, "ombine_", 7) != 0)
        break;
      switch (RuleIdentifier[8]) {
      default: break;
      case 'f':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+9, "add_fma_fmul_to_fmad_or_fma", 27) != 0)
          break;
        return 115;	 // "combine_fadd_fma_fmul_to_fmad_or_fma"
      case 'i':	 // 1 string to match.
        if (memcmp(RuleIdentifier.data()+9, "nsert_vec_elts_build_vector", 27) != 0)
          break;
        return 5;	 // "combine_insert_vec_elts_build_vector"
      }
      break;
    case 'i':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "nsert_extract_vec_elt_out_of_bounds", 35) != 0)
        break;
      return 22;	 // "insert_extract_vec_elt_out_of_bounds"
    }
    break;
  case 37:	 // 2 strings to match.
    switch (RuleIdentifier[0]) {
    default: break;
    case 'c':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "ombine_fsub_fneg_fmul_to_fmad_or_fma", 36) != 0)
        break;
      return 118;	 // "combine_fsub_fneg_fmul_to_fmad_or_fma"
    case 'h':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+1, "oist_logic_op_with_same_opcode_hands", 36) != 0)
        break;
      return 43;	 // "hoist_logic_op_with_same_opcode_hands"
    }
    break;
  case 38:	 // 2 strings to match.
    if (memcmp(RuleIdentifier.data()+0, "combine_f", 9) != 0)
      break;
    switch (RuleIdentifier[9]) {
    default: break;
    case 'a':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+10, "dd_fpext_fmul_to_fmad_or_fma", 28) != 0)
        break;
      return 114;	 // "combine_fadd_fpext_fmul_to_fmad_or_fma"
    case 's':	 // 1 string to match.
      if (memcmp(RuleIdentifier.data()+10, "ub_fpext_fmul_to_fmad_or_fma", 28) != 0)
        break;
      return 119;	 // "combine_fsub_fpext_fmul_to_fmad_or_fma"
    }
    break;
  case 42:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "combine_fadd_fpext_fma_fmul_to_fmad_or_fma", 42) != 0)
      break;
    return 116;	 // "combine_fadd_fpext_fma_fmul_to_fmad_or_fma"
  case 43:	 // 1 string to match.
    if (memcmp(RuleIdentifier.data()+0, "combine_fsub_fpext_fneg_fmul_to_fmad_or_fma", 43) != 0)
      break;
    return 120;	 // "combine_fsub_fpext_fneg_fmul_to_fmad_or_fma"
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
    return {{0, 133}};
  }
  const auto I = getRuleIdxForIdentifier(RangePair.first);
  if (!I)
    return std::nullopt;
  return {{*I, *I + 1}};
}

bool AArch64PreLegalizerCombinerImplRuleConfig::setRuleEnabled(StringRef RuleIdentifier) {
  auto MaybeRange = getRuleRangeForIdentifier(RuleIdentifier);
  if (!MaybeRange)
    return false;
  for (auto I = MaybeRange->first; I < MaybeRange->second; ++I)
    DisabledRules.reset(I);
  return true;
}

bool AArch64PreLegalizerCombinerImplRuleConfig::setRuleDisabled(StringRef RuleIdentifier) {
  auto MaybeRange = getRuleRangeForIdentifier(RuleIdentifier);
  if (!MaybeRange)
    return false;
  for (auto I = MaybeRange->first; I < MaybeRange->second; ++I)
    DisabledRules.set(I);
  return true;
}

static std::vector<std::string> AArch64PreLegalizerCombinerOption;
static cl::list<std::string> AArch64PreLegalizerCombinerDisableOption(
    "aarch64prelegalizercombiner-disable-rule",
    cl::desc("Disable one or more combiner rules temporarily in the AArch64PreLegalizerCombiner pass"),
    cl::CommaSeparated,
    cl::Hidden,
    cl::cat(GICombinerOptionCategory),
    cl::callback([](const std::string &Str) {
      AArch64PreLegalizerCombinerOption.push_back(Str);
    }));
static cl::list<std::string> AArch64PreLegalizerCombinerOnlyEnableOption(
    "aarch64prelegalizercombiner-only-enable-rule",
    cl::desc("Disable all rules in the AArch64PreLegalizerCombiner pass then re-enable the specified ones"),
    cl::Hidden,
    cl::cat(GICombinerOptionCategory),
    cl::callback([](const std::string &CommaSeparatedArg) {
      StringRef Str = CommaSeparatedArg;
      AArch64PreLegalizerCombinerOption.push_back("*");
      do {
        auto X = Str.split(",");
        AArch64PreLegalizerCombinerOption.push_back(("!" + X.first).str());
        Str = X.second;
      } while (!Str.empty());
    }));


bool AArch64PreLegalizerCombinerImplRuleConfig::isRuleEnabled(unsigned RuleID) const {
    return  !DisabledRules.test(RuleID);
}
bool AArch64PreLegalizerCombinerImplRuleConfig::parseCommandLineOption() {
  for (StringRef Identifier : AArch64PreLegalizerCombinerOption) {
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
const unsigned MAX_SUBTARGET_PREDICATES = 1;
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
  typedef ComplexRendererFns(AArch64PreLegalizerCombinerImpl::*ComplexMatcherMemFn)(MachineOperand &) const;
  typedef void(AArch64PreLegalizerCombinerImpl::*CustomRendererFn)(MachineInstrBuilder &, const MachineInstr &, int) const;
  const ExecInfoTy<PredicateBitset, ComplexMatcherMemFn, CustomRendererFn> ExecInfo;
  static AArch64PreLegalizerCombinerImpl::ComplexMatcherMemFn ComplexPredicateFns[];
  static AArch64PreLegalizerCombinerImpl::CustomRendererFn CustomRenderers[];
  bool testImmPredicate_I64(unsigned PredicateID, int64_t Imm) const override;
  bool testImmPredicate_APInt(unsigned PredicateID, const APInt &Imm) const override;
  bool testImmPredicate_APFloat(unsigned PredicateID, const APFloat &Imm) const override;
  const uint8_t *getMatchTable() const override;
  bool testMIPredicate_MI(unsigned PredicateID, const MachineInstr &MI, const MatcherState &State) const override;
  bool testSimplePredicate(unsigned PredicateID) const override;
  void runCustomAction(unsigned FnID, const MatcherState &State, NewMIVector &OutMIs) const override;
  struct MatchInfosTy {
    std::pair<Register, bool> MDInfo1;
    ConstantFP* MDInfo23;
    InstructionStepsMatchInfo MDInfo9;
    MachineInstr* MDInfo7;
    SmallVector<Register, 4> MDInfo2;
    RegisterImmPair MDInfo13;
    Register MDInfo3;
    std::pair<Register, unsigned> MDInfo18;
    APInt MDInfo20;
    std::pair<uint64_t, uint64_t> MDInfo24;
    unsigned MDInfo0;
    SmallVector<std::pair<Register, MachineInstr*>> MDInfo4;
    std::tuple<Register, unsigned> MDInfo12;
    MachineInstr * MDInfo15;
    ShiftOfShiftedLogic MDInfo22;
    std::pair<MachineInstr*, LLT> MDInfo19;
    PreferredTuple MDInfo5;
    SmallVector<APInt, 8> MDInfo17;
    std::tuple<Register, Register> MDInfo8;
    std::tuple<Register, int64_t> MDInfo11;
    SmallVector<Register, 8> MDInfo16;
    std::function<void(MachineIRBuilder &)> MDInfo6;
    int64_t MDInfo14;
    std::tuple<Register, Register, bool> MDInfo25;
    std::pair<Register, Register> MDInfo21;
    PtrAddChain MDInfo10;
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
  Feature_HasDotProdBit = 0,
};

PredicateBitset AArch64PreLegalizerCombinerImpl::
computeAvailableModuleFeatures(const AArch64Subtarget *Subtarget) const {
  PredicateBitset Features;
  if (Subtarget->hasDotProd())
    Features.set(Feature_HasDotProdBit);
  return Features;
}

void AArch64PreLegalizerCombinerImpl::setupGeneratedPerFunctionState(MachineFunction &MF) {
  AvailableFunctionFeatures = computeAvailableFunctionFeatures((const AArch64Subtarget *)&MF.getSubtarget(), &MF);
}
PredicateBitset AArch64PreLegalizerCombinerImpl::
computeAvailableFunctionFeatures(const AArch64Subtarget *Subtarget, const MachineFunction *MF) const {
  PredicateBitset Features;
  return Features;
}

// Feature bitsets.
enum {
  GIFBS_Invalid,
  GIFBS_HasDotProd,
};
constexpr static PredicateBitset FeatureBitsets[] {
  {}, // GIFBS_Invalid
  {Feature_HasDotProdBit, },
};

// ComplexPattern predicates.
enum {
  GICP_Invalid,
};
// See constructor for table contents

AArch64PreLegalizerCombinerImpl::ComplexMatcherMemFn
AArch64PreLegalizerCombinerImpl::ComplexPredicateFns[] = {
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
  GICXXPred_MI_Predicate_GICombiner49,
  GICXXPred_MI_Predicate_GICombiner50,
  GICXXPred_MI_Predicate_GICombiner51,
  GICXXPred_MI_Predicate_GICombiner52,
  GICXXPred_MI_Predicate_GICombiner53,
  GICXXPred_MI_Predicate_GICombiner54,
  GICXXPred_MI_Predicate_GICombiner55,
  GICXXPred_MI_Predicate_GICombiner56,
  GICXXPred_MI_Predicate_GICombiner57,
  GICXXPred_MI_Predicate_GICombiner58,
  GICXXPred_MI_Predicate_GICombiner59,
  GICXXPred_MI_Predicate_GICombiner60,
  GICXXPred_MI_Predicate_GICombiner61,
  GICXXPred_MI_Predicate_GICombiner62,
  GICXXPred_MI_Predicate_GICombiner63,
  GICXXPred_MI_Predicate_GICombiner64,
  GICXXPred_MI_Predicate_GICombiner65,
  GICXXPred_MI_Predicate_GICombiner66,
  GICXXPred_MI_Predicate_GICombiner67,
  GICXXPred_MI_Predicate_GICombiner68,
  GICXXPred_MI_Predicate_GICombiner69,
  GICXXPred_MI_Predicate_GICombiner70,
  GICXXPred_MI_Predicate_GICombiner71,
  GICXXPred_MI_Predicate_GICombiner72,
  GICXXPred_MI_Predicate_GICombiner73,
  GICXXPred_MI_Predicate_GICombiner74,
  GICXXPred_MI_Predicate_GICombiner75,
  GICXXPred_MI_Predicate_GICombiner76,
  GICXXPred_MI_Predicate_GICombiner77,
  GICXXPred_MI_Predicate_GICombiner78,
  GICXXPred_MI_Predicate_GICombiner79,
  GICXXPred_MI_Predicate_GICombiner80,
  GICXXPred_MI_Predicate_GICombiner81,
  GICXXPred_MI_Predicate_GICombiner82,
  GICXXPred_MI_Predicate_GICombiner83,
  GICXXPred_MI_Predicate_GICombiner84,
  GICXXPred_MI_Predicate_GICombiner85,
  GICXXPred_MI_Predicate_GICombiner86,
  GICXXPred_MI_Predicate_GICombiner87,
  GICXXPred_MI_Predicate_GICombiner88,
  GICXXPred_MI_Predicate_GICombiner89,
  GICXXPred_MI_Predicate_GICombiner90,
  GICXXPred_MI_Predicate_GICombiner91,
  GICXXPred_MI_Predicate_GICombiner92,
  GICXXPred_MI_Predicate_GICombiner93,
  GICXXPred_MI_Predicate_GICombiner94,
  GICXXPred_MI_Predicate_GICombiner95,
  GICXXPred_MI_Predicate_GICombiner96,
  GICXXPred_MI_Predicate_GICombiner97,
  GICXXPred_MI_Predicate_GICombiner98,
  GICXXPred_MI_Predicate_GICombiner99,
  GICXXPred_MI_Predicate_GICombiner100,
  GICXXPred_MI_Predicate_GICombiner101,
  GICXXPred_MI_Predicate_GICombiner102,
  GICXXPred_MI_Predicate_GICombiner103,
  GICXXPred_MI_Predicate_GICombiner104,
  GICXXPred_MI_Predicate_GICombiner105,
  GICXXPred_MI_Predicate_GICombiner106,
  GICXXPred_MI_Predicate_GICombiner107,
  GICXXPred_MI_Predicate_GICombiner108,
};
bool AArch64PreLegalizerCombinerImpl::testMIPredicate_MI(unsigned PredicateID, const MachineInstr & MI, const MatcherState &State) const {
  switch (PredicateID) {
  case GICXXPred_MI_Predicate_GICombiner0: {
    return Helper.matchCombineCopy(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner1: {
    return Helper.matchCombineMulToShl(*State.MIs[0], MatchInfos.MDInfo0);
  }
  case GICXXPred_MI_Predicate_GICombiner2: {
    return Helper.matchCombineAddP2IToPtrAdd(*State.MIs[0], MatchInfos.MDInfo1);
  }
  case GICXXPred_MI_Predicate_GICombiner3: {
    return Helper.matchCombineInsertVecElts(*State.MIs[0], MatchInfos.MDInfo2);
  }
  case GICXXPred_MI_Predicate_GICombiner4: {
    return Helper.matchExtractVecEltBuildVec(*State.MIs[0], MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner5: {
    return Helper.matchExtractAllEltsFromBuildVector(*State.MIs[0], MatchInfos.MDInfo4);
  }
  case GICXXPred_MI_Predicate_GICombiner6: {
    return Helper.matchCombineExtendingLoads(*State.MIs[0], MatchInfos.MDInfo5);
  }
  case GICXXPred_MI_Predicate_GICombiner7: {
    return Helper.matchCombineLoadWithAndMask(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner8: {
    return Helper.matchCombineExtractedVectorLoad(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner9: {
    return Helper.matchAnyExplicitUseIsUndef(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner10: {
    return Helper.matchOperandIsUndef(*State.MIs[0], 1);
  }
  case GICXXPred_MI_Predicate_GICombiner11: {
    return Helper.matchOperandIsUndef(*State.MIs[0], 2);
  }
  case GICXXPred_MI_Predicate_GICombiner12: {
    return Helper.matchAllExplicitUsesAreUndef(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner13: {
    return Helper.matchUndefShuffleVectorMask(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner14: {
    return Helper.matchUndefStore(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner15: {
    return Helper.matchCombineUnmergeUndef(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner16: {
    return Helper.matchInsertExtractVecEltOutOfBounds(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner17: {
    return Helper.matchSelectSameVal(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner18: {
    return Helper.matchOperandIsZero(*State.MIs[0], 1);
  }
  case GICXXPred_MI_Predicate_GICombiner19: {
    return Helper.matchCombineI2PToP2I(*State.MIs[0], MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner20: {
    MatchInfos.MDInfo3 = State.MIs[1]->getOperand(1).getReg(); return true;
    llvm_unreachable("GICombiner20 should have returned");
  }
  case GICXXPred_MI_Predicate_GICombiner21: {
    return Helper.matchCombineAnyExtTrunc(*State.MIs[0], MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner22: {
    return Helper.matchConstantFPOp(State.MIs[0]->getOperand(2), 1.0);
  }
  case GICXXPred_MI_Predicate_GICombiner23: {
    return Helper.matchBuildVectorIdentityFold(*State.MIs[0], MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner24: {
    return Helper.matchTruncBuildVectorFold(*State.MIs[0], MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner25: {
    return Helper.matchTruncLshrBuildVectorFold(*State.MIs[0], MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner26: {
    return MRI.getType(State.MIs[1]->getOperand(1).getReg()) == MRI.getType(State.MIs[0]->getOperand(0).getReg());
  }
  case GICXXPred_MI_Predicate_GICombiner27: {
    return Helper.matchConstantFPOp(State.MIs[0]->getOperand(2), -0.0);
  }
  case GICXXPred_MI_Predicate_GICombiner28: {
    return Helper.matchExtendThroughPhis(*State.MIs[0], MatchInfos.MDInfo7);
  }
  case GICXXPred_MI_Predicate_GICombiner29: {
    return Helper.matchSimplifyAddToSub(*State.MIs[0], MatchInfos.MDInfo8);
  }
  case GICXXPred_MI_Predicate_GICombiner30: {
    return Helper.matchHoistLogicOpWithSameOpcodeHands(*State.MIs[0], MatchInfos.MDInfo9);
  }
  case GICXXPred_MI_Predicate_GICombiner31: {
    return Helper.matchShiftsTooBig(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner32: {
    return Helper.matchReassocPtrAdd(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner33: {
    return Helper.matchReassocCommBinOp(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner34: {
    return Helper.matchPtrAddImmedChain(*State.MIs[0], MatchInfos.MDInfo10);
  }
  case GICXXPred_MI_Predicate_GICombiner35: {
    return Helper.matchAshrShlToSextInreg(*State.MIs[0], MatchInfos.MDInfo11);
  }
  case GICXXPred_MI_Predicate_GICombiner36: {
    return Helper.matchSextInRegOfLoad(*State.MIs[0], MatchInfos.MDInfo12);
  }
  case GICXXPred_MI_Predicate_GICombiner37: {
    return Helper.matchCombineShlOfExtend(*State.MIs[0], MatchInfos.MDInfo13);
  }
  case GICXXPred_MI_Predicate_GICombiner38: {
    return Helper.matchNarrowBinopFeedingAnd(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner39: {
    return Helper.matchConstantSelectCmp(*State.MIs[0], MatchInfos.MDInfo0);
  }
  case GICXXPred_MI_Predicate_GICombiner40: {
    return Helper.matchSelectToLogical(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner41: {
    return Helper.matchRedundantAnd(*State.MIs[0], MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner42: {
    return Helper.matchRedundantSExtInReg(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner43: {
    return Helper.matchRedundantOr(*State.MIs[0], MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner44: {
    return Helper.matchOperandIsKnownToBeAPowerOfTwo(*State.MIs[0], 2);
  }
  case GICXXPred_MI_Predicate_GICombiner45: {
    return Helper.matchCombineZextTrunc(*State.MIs[0], MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner46: {
    return Helper.matchICmpToTrueFalseKnownBits(*State.MIs[0], MatchInfos.MDInfo14);
  }
  case GICXXPred_MI_Predicate_GICombiner47: {
    return Helper.matchICmpToLHSKnownBits(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner48: {
    unsigned BitWidth = MRI.getType(State.MIs[0]->getOperand(1).getReg()).getScalarSizeInBits();
                return Helper.getKnownBits()->maskedValueIsZero(State.MIs[0]->getOperand(1).getReg(),
                         APInt::getOneBitSet(BitWidth, State.MIs[0]->getOperand(2).getImm() - 1));
    llvm_unreachable("GICombiner48 should have returned");
  }
  case GICXXPred_MI_Predicate_GICombiner49: {
    return Helper.matchCombineExtOfExt(*State.MIs[0], MatchInfos.MDInfo12);
  }
  case GICXXPred_MI_Predicate_GICombiner50: {
    return Helper.matchNotCmp(*State.MIs[0], MatchInfos.MDInfo2);
  }
  case GICXXPred_MI_Predicate_GICombiner51: {
    return Helper.matchOptBrCondByInvertingCond(*State.MIs[0], MatchInfos.MDInfo15);
  }
  case GICXXPred_MI_Predicate_GICombiner52: {
    return Helper.matchCombineUnmergeMergeToPlainValues(*State.MIs[0], MatchInfos.MDInfo16);
  }
  case GICXXPred_MI_Predicate_GICombiner53: {
    return Helper.matchCombineUnmergeConstant(*State.MIs[0], MatchInfos.MDInfo17);
  }
  case GICXXPred_MI_Predicate_GICombiner54: {
    return Helper.matchCombineUnmergeWithDeadLanesToTrunc(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner55: {
    return Helper.matchCombineUnmergeZExtToZExt(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner56: {
    return Helper.matchCombineMergeUnmerge(*State.MIs[0], MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner57: {
    return Helper.matchCombineTruncOfExt(*State.MIs[0], MatchInfos.MDInfo18);
  }
  case GICXXPred_MI_Predicate_GICombiner58: {
    return Helper.matchCombineTruncOfShift(*State.MIs[0], MatchInfos.MDInfo19);
  }
  case GICXXPred_MI_Predicate_GICombiner59: {
    return Helper.matchCombineConstPtrAddToI2P(*State.MIs[0], MatchInfos.MDInfo20);
  }
  case GICXXPred_MI_Predicate_GICombiner60: {
    return Helper.matchOverlappingAnd(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner61: {
    return Helper.matchMulOBy2(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner62: {
    return Helper.matchMulOBy0(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner63: {
    return Helper.matchAddOBy0(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner64: {
    return Helper.matchAddEToAddO(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner65: {
    return Helper.matchCombineFMinMaxNaN(*State.MIs[0], MatchInfos.MDInfo0);
  }
  case GICXXPred_MI_Predicate_GICombiner66: {
    return Helper.matchXorOfAndWithSameReg(*State.MIs[0], MatchInfos.MDInfo21);
  }
  case GICXXPred_MI_Predicate_GICombiner67: {
    return Helper.matchPtrAddZero(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner68: {
    return Helper.matchShiftImmedChain(*State.MIs[0], MatchInfos.MDInfo13);
  }
  case GICXXPred_MI_Predicate_GICombiner69: {
    return Helper.matchShiftOfShiftedLogic(*State.MIs[0], MatchInfos.MDInfo22);
  }
  case GICXXPred_MI_Predicate_GICombiner70: {
    return Helper.matchLoadOrCombine(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner71: {
    return Helper.matchCombineDivRem(*State.MIs[0], MatchInfos.MDInfo15);
  }
  case GICXXPred_MI_Predicate_GICombiner72: {
    return Helper.matchOrShiftToFunnelShift(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner73: {
    return Helper.matchFunnelShiftToRotate(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner74: {
    return Helper.matchConstantLargerBitWidth(*State.MIs[0], 3);
  }
  case GICXXPred_MI_Predicate_GICombiner75: {
    return Helper.matchCommuteShift(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner76: {
    return Helper.matchBitfieldExtractFromSExtInReg(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner77: {
    return Helper.matchBitfieldExtractFromAnd(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner78: {
    return Helper.matchBitfieldExtractFromShr(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner79: {
    return Helper.matchBitfieldExtractFromShrAnd(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner80: {
    return Helper.matchConstantFoldBinOp(*State.MIs[0], MatchInfos.MDInfo20);
  }
  case GICXXPred_MI_Predicate_GICombiner81: {
    return Helper.matchConstantFoldFPBinOp(*State.MIs[0], MatchInfos.MDInfo23);
  }
  case GICXXPred_MI_Predicate_GICombiner82: {
    return Helper.matchConstantFoldFMA(*State.MIs[0], MatchInfos.MDInfo23);
  }
  case GICXXPred_MI_Predicate_GICombiner83: {
    return Helper.matchConstantFoldCastOp(*State.MIs[0], MatchInfos.MDInfo20);
  }
  case GICXXPred_MI_Predicate_GICombiner84: {
    return Helper.matchUDivByConst(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner85: {
    return Helper.matchSDivByConst(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner86: {
    return Helper.matchUMulHToLShr(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner87: {
    return Helper.matchRedundantNegOperands(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner88: {
    return Helper.matchAndOrDisjointMask(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner89: {
    return Helper.matchCombineFAddFMulToFMadOrFMA(*State.MIs[0],
                                                                  MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner90: {
    return Helper.matchCombineFAddFpExtFMulToFMadOrFMA(*State.MIs[0],
                                                                       MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner91: {
    return Helper.matchCombineFAddFMAFMulToFMadOrFMA(*State.MIs[0],
                                                                     MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner92: {
    return Helper.matchCombineFAddFpExtFMulToFMadOrFMAAggressive(
                                                          *State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner93: {
    return Helper.matchCombineFSubFMulToFMadOrFMA(*State.MIs[0],
                                                                  MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner94: {
    return Helper.matchCombineFSubFNegFMulToFMadOrFMA(*State.MIs[0],
                                                                      MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner95: {
    return Helper.matchCombineFSubFpExtFMulToFMadOrFMA(*State.MIs[0],
                                                                       MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner96: {
    return Helper.matchCombineFSubFpExtFNegFMulToFMadOrFMA(
                                                    *State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner97: {
    return Helper.matchFoldBinOpIntoSelect(*State.MIs[0], MatchInfos.MDInfo0);
  }
  case GICXXPred_MI_Predicate_GICombiner98: {
    return Helper.matchSubAddSameReg(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner99: {
    return Helper.matchSimplifySelectToMinMax(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner100: {
    return Helper.matchRedundantBinOpInEquality(*State.MIs[0], MatchInfos.MDInfo6);
  }
  case GICXXPred_MI_Predicate_GICombiner101: {
    return Helper.matchFsubToFneg(*State.MIs[0], MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner102: {
    return Helper.matchCommuteConstantToRHS(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner103: {
    return Helper.matchCommuteFPConstantToRHS(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner104: {
    return matchFConstantToConstant(*State.MIs[0], MRI);
  }
  case GICXXPred_MI_Predicate_GICombiner105: {
    return matchICmpRedundantTrunc(*State.MIs[0], MRI, Helper.getKnownBits(), MatchInfos.MDInfo3);
  }
  case GICXXPred_MI_Predicate_GICombiner106: {
    return matchFoldGlobalOffset(*State.MIs[0], MRI, MatchInfos.MDInfo24);
  }
  case GICXXPred_MI_Predicate_GICombiner107: {
    return Helper.matchShuffleToExtract(*State.MIs[0]);
  }
  case GICXXPred_MI_Predicate_GICombiner108: {
    return matchExtAddvToUdotAddv(*State.MIs[0], MRI, STI, MatchInfos.MDInfo25);
  }
  }
  llvm_unreachable("Unknown predicate");
  return false;
}
bool AArch64PreLegalizerCombinerImpl::testImmPredicate_I64(unsigned PredicateID, int64_t Imm) const {
  llvm_unreachable("Unknown predicate");
  return false;
}
bool AArch64PreLegalizerCombinerImpl::testImmPredicate_APFloat(unsigned PredicateID, const APFloat & Imm) const {
  llvm_unreachable("Unknown predicate");
  return false;
}
bool AArch64PreLegalizerCombinerImpl::testImmPredicate_APInt(unsigned PredicateID, const APInt & Imm) const {
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
  GICXXPred_Simple_IsRule61Enabled,
  GICXXPred_Simple_IsRule62Enabled,
  GICXXPred_Simple_IsRule63Enabled,
  GICXXPred_Simple_IsRule64Enabled,
  GICXXPred_Simple_IsRule65Enabled,
  GICXXPred_Simple_IsRule66Enabled,
  GICXXPred_Simple_IsRule67Enabled,
  GICXXPred_Simple_IsRule68Enabled,
  GICXXPred_Simple_IsRule69Enabled,
  GICXXPred_Simple_IsRule70Enabled,
  GICXXPred_Simple_IsRule71Enabled,
  GICXXPred_Simple_IsRule72Enabled,
  GICXXPred_Simple_IsRule73Enabled,
  GICXXPred_Simple_IsRule74Enabled,
  GICXXPred_Simple_IsRule75Enabled,
  GICXXPred_Simple_IsRule76Enabled,
  GICXXPred_Simple_IsRule77Enabled,
  GICXXPred_Simple_IsRule78Enabled,
  GICXXPred_Simple_IsRule79Enabled,
  GICXXPred_Simple_IsRule80Enabled,
  GICXXPred_Simple_IsRule81Enabled,
  GICXXPred_Simple_IsRule82Enabled,
  GICXXPred_Simple_IsRule83Enabled,
  GICXXPred_Simple_IsRule84Enabled,
  GICXXPred_Simple_IsRule85Enabled,
  GICXXPred_Simple_IsRule86Enabled,
  GICXXPred_Simple_IsRule87Enabled,
  GICXXPred_Simple_IsRule88Enabled,
  GICXXPred_Simple_IsRule89Enabled,
  GICXXPred_Simple_IsRule90Enabled,
  GICXXPred_Simple_IsRule91Enabled,
  GICXXPred_Simple_IsRule92Enabled,
  GICXXPred_Simple_IsRule93Enabled,
  GICXXPred_Simple_IsRule94Enabled,
  GICXXPred_Simple_IsRule95Enabled,
  GICXXPred_Simple_IsRule96Enabled,
  GICXXPred_Simple_IsRule97Enabled,
  GICXXPred_Simple_IsRule98Enabled,
  GICXXPred_Simple_IsRule99Enabled,
  GICXXPred_Simple_IsRule100Enabled,
  GICXXPred_Simple_IsRule101Enabled,
  GICXXPred_Simple_IsRule102Enabled,
  GICXXPred_Simple_IsRule103Enabled,
  GICXXPred_Simple_IsRule104Enabled,
  GICXXPred_Simple_IsRule105Enabled,
  GICXXPred_Simple_IsRule106Enabled,
  GICXXPred_Simple_IsRule107Enabled,
  GICXXPred_Simple_IsRule108Enabled,
  GICXXPred_Simple_IsRule109Enabled,
  GICXXPred_Simple_IsRule110Enabled,
  GICXXPred_Simple_IsRule111Enabled,
  GICXXPred_Simple_IsRule112Enabled,
  GICXXPred_Simple_IsRule113Enabled,
  GICXXPred_Simple_IsRule114Enabled,
  GICXXPred_Simple_IsRule115Enabled,
  GICXXPred_Simple_IsRule116Enabled,
  GICXXPred_Simple_IsRule117Enabled,
  GICXXPred_Simple_IsRule118Enabled,
  GICXXPred_Simple_IsRule119Enabled,
  GICXXPred_Simple_IsRule120Enabled,
  GICXXPred_Simple_IsRule121Enabled,
  GICXXPred_Simple_IsRule122Enabled,
  GICXXPred_Simple_IsRule123Enabled,
  GICXXPred_Simple_IsRule124Enabled,
  GICXXPred_Simple_IsRule125Enabled,
  GICXXPred_Simple_IsRule126Enabled,
  GICXXPred_Simple_IsRule127Enabled,
  GICXXPred_Simple_IsRule128Enabled,
  GICXXPred_Simple_IsRule129Enabled,
  GICXXPred_Simple_IsRule130Enabled,
  GICXXPred_Simple_IsRule131Enabled,
  GICXXPred_Simple_IsRule132Enabled,
};

bool AArch64PreLegalizerCombinerImpl::testSimplePredicate(unsigned Predicate) const {
    return RuleConfig.isRuleEnabled(Predicate - GICXXPred_Invalid - 1);
}
// Custom renderers.
enum {
  GICR_Invalid,
};
AArch64PreLegalizerCombinerImpl::CustomRendererFn
AArch64PreLegalizerCombinerImpl::CustomRenderers[] = {
  nullptr, // GICR_Invalid
};

bool AArch64PreLegalizerCombinerImpl::tryCombineAllImpl(MachineInstr &I) const {
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
  GICXXCustomAction_CombineApplyGICombiner30,
  GICXXCustomAction_CombineApplyGICombiner31,
  GICXXCustomAction_CombineApplyGICombiner32,
  GICXXCustomAction_CombineApplyGICombiner33,
  GICXXCustomAction_CombineApplyGICombiner34,
  GICXXCustomAction_CombineApplyGICombiner35,
  GICXXCustomAction_CombineApplyGICombiner36,
  GICXXCustomAction_CombineApplyGICombiner37,
  GICXXCustomAction_CombineApplyGICombiner38,
  GICXXCustomAction_CombineApplyGICombiner39,
  GICXXCustomAction_CombineApplyGICombiner40,
  GICXXCustomAction_CombineApplyGICombiner41,
  GICXXCustomAction_CombineApplyGICombiner42,
  GICXXCustomAction_CombineApplyGICombiner43,
  GICXXCustomAction_CombineApplyGICombiner44,
  GICXXCustomAction_CombineApplyGICombiner45,
  GICXXCustomAction_CombineApplyGICombiner46,
  GICXXCustomAction_CombineApplyGICombiner47,
  GICXXCustomAction_CombineApplyGICombiner48,
  GICXXCustomAction_CombineApplyGICombiner49,
  GICXXCustomAction_CombineApplyGICombiner50,
  GICXXCustomAction_CombineApplyGICombiner51,
  GICXXCustomAction_CombineApplyGICombiner52,
  GICXXCustomAction_CombineApplyGICombiner53,
  GICXXCustomAction_CombineApplyGICombiner54,
  GICXXCustomAction_CombineApplyGICombiner55,
  GICXXCustomAction_CombineApplyGICombiner56,
  GICXXCustomAction_CombineApplyGICombiner57,
  GICXXCustomAction_CombineApplyGICombiner58,
  GICXXCustomAction_CombineApplyGICombiner59,
  GICXXCustomAction_CombineApplyGICombiner60,
  GICXXCustomAction_CombineApplyGICombiner61,
  GICXXCustomAction_CombineApplyGICombiner62,
};
void AArch64PreLegalizerCombinerImpl::runCustomAction(unsigned ApplyID, const MatcherState &State, NewMIVector &OutMIs) const {
  switch(ApplyID) {
  case GICXXCustomAction_CombineApplyGICombiner0:{
    Helper.applyCombineCopy(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner1:{
    Helper.applyCombineMulToShl(*State.MIs[0], MatchInfos.MDInfo0);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner2:{
    Helper.applyCombineAddP2IToPtrAdd(*State.MIs[0], MatchInfos.MDInfo1);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner3:{
    Helper.applyCombineInsertVecElts(*State.MIs[0], MatchInfos.MDInfo2);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner4:{
    Helper.applyExtractVecEltBuildVec(*State.MIs[0], MatchInfos.MDInfo3);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner5:{
    Helper.applyExtractAllEltsFromBuildVector(*State.MIs[0], MatchInfos.MDInfo4);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner6:{
    Helper.applyCombineExtendingLoads(*State.MIs[0], MatchInfos.MDInfo5);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner7:{
    Helper.applyBuildFn(*State.MIs[0], MatchInfos.MDInfo6);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner8:{
    Helper.replaceInstWithFConstant(*State.MIs[0], 0.0);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner9:{
    Helper.replaceInstWithConstant(*State.MIs[0], 0);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner10:{
    Helper.replaceInstWithConstant(*State.MIs[0], -1);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner11:{
    Helper.replaceInstWithUndef(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner12:{
    Helper.eraseInst(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner13:{
    Helper.replaceSingleDefInstWithOperand(*State.MIs[0], 2);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner14:{
    Helper.replaceSingleDefInstWithOperand(*State.MIs[0], 1);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner15:{
    Helper.applyCombineI2PToP2I(*State.MIs[0], MatchInfos.MDInfo3);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner16:{
    Helper.applyCombineP2IToI2P(*State.MIs[0], MatchInfos.MDInfo3);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner17:{
    Helper.replaceSingleDefInstWithReg(*State.MIs[0], MatchInfos.MDInfo3);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner18:{
    Helper.replaceSingleDefInstWithReg(*State.MIs[0], State.MIs[1]->getOperand(1).getReg());
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner19:{
    Helper.applyExtendThroughPhis(*State.MIs[0], MatchInfos.MDInfo7);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner20:{
    Helper.applySimplifyAddToSub(*State.MIs[0], MatchInfos.MDInfo8);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner21:{
    Helper.applyBuildInstructionSteps(*State.MIs[0], MatchInfos.MDInfo9);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner22:{
    Helper.applyBuildFnNoErase(*State.MIs[0], MatchInfos.MDInfo6);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner23:{
    Helper.applyPtrAddImmedChain(*State.MIs[0], MatchInfos.MDInfo10);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner24:{
    Helper.applyAshShlToSextInreg(*State.MIs[0], MatchInfos.MDInfo11);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner25:{
    Helper.applySextInRegOfLoad(*State.MIs[0], MatchInfos.MDInfo12);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner26:{
    Helper.applyCombineShlOfExtend(*State.MIs[0], MatchInfos.MDInfo13);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner27:{
    Helper.replaceSingleDefInstWithOperand(*State.MIs[0], MatchInfos.MDInfo0);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner28:{
    Helper.applySimplifyURemByPow2(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner29:{
    Helper.replaceInstWithConstant(*State.MIs[0], MatchInfos.MDInfo14);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner30:{
    Helper.getBuilder().setInstrAndDebugLoc(*State.MIs[0]);
              Helper.getBuilder().buildZExtInReg(State.MIs[0]->getOperand(0), State.MIs[0]->getOperand(1), State.MIs[0]->getOperand(2).getImm());
              State.MIs[0]->eraseFromParent();
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner31:{
    Helper.applyCombineExtOfExt(*State.MIs[0], MatchInfos.MDInfo12);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner32:{
    Helper.applyNotCmp(*State.MIs[0], MatchInfos.MDInfo2);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner33:{
    Helper.applyOptBrCondByInvertingCond(*State.MIs[0], MatchInfos.MDInfo15);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner34:{
    Helper.applyCombineUnmergeMergeToPlainValues(*State.MIs[0], MatchInfos.MDInfo16);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner35:{
    Helper.applyCombineUnmergeConstant(*State.MIs[0], MatchInfos.MDInfo17);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner36:{
    Helper.applyCombineUnmergeWithDeadLanesToTrunc(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner37:{
    Helper.applyCombineUnmergeZExtToZExt(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner38:{
    Helper.applyCombineTruncOfExt(*State.MIs[0], MatchInfos.MDInfo18);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner39:{
    Helper.applyCombineTruncOfShift(*State.MIs[0], MatchInfos.MDInfo19);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner40:{
    Helper.applyCombineConstantFoldFpUnary(*State.MIs[0], State.MIs[1]->getOperand(1).getFPImm());
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner41:{
    Helper.replaceInstWithFConstant(*State.MIs[0]->getOperand(0).getParent(), 0.0);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner42:{
    Helper.applyCombineConstPtrAddToI2P(*State.MIs[0], MatchInfos.MDInfo20);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner43:{
    Helper.applyXorOfAndWithSameReg(*State.MIs[0], MatchInfos.MDInfo21);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner44:{
    Helper.applyPtrAddZero(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner45:{
    Helper.applyShiftImmedChain(*State.MIs[0], MatchInfos.MDInfo13);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner46:{
    Helper.applyShiftOfShiftedLogic(*State.MIs[0], MatchInfos.MDInfo22);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner47:{
    Helper.applyCombineDivRem(*State.MIs[0], MatchInfos.MDInfo15);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner48:{
    Helper.applyFunnelShiftToRotate(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner49:{
    Helper.applyFunnelShiftConstantModulo(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner50:{
    Helper.replaceInstWithConstant(*State.MIs[0], MatchInfos.MDInfo20);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner51:{
    Helper.replaceInstWithFConstant(*State.MIs[0], MatchInfos.MDInfo23);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner52:{
    Helper.applyUDivByConst(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner53:{
    Helper.applySDivByConst(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner54:{
    Helper.applyUMulHToLShr(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner55:{
    Helper.applyFoldBinOpIntoSelect(*State.MIs[0], MatchInfos.MDInfo0);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner56:{
    Helper.applyFsubToFneg(*State.MIs[0], MatchInfos.MDInfo3);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner57:{
    Helper.applyCommuteBinOpOperands(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner58:{
    applyFConstantToConstant(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner59:{
    applyICmpRedundantTrunc(*State.MIs[0], MRI, B, Observer, MatchInfos.MDInfo3);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner60:{
    applyFoldGlobalOffset(*State.MIs[0], MRI, B, Observer, MatchInfos.MDInfo24);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner61:{
    Helper.applyShuffleToExtract(*State.MIs[0]);
    return;
  }
  case GICXXCustomAction_CombineApplyGICombiner62:{
    applyExtAddvToUdotAddv(*State.MIs[0], MRI, B, Observer, STI, MatchInfos.MDInfo25);
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
const uint8_t *AArch64PreLegalizerCombinerImpl::getMatchTable() const {
  constexpr static uint8_t MatchTable0[] = {
    GIM_SwitchOpcode, /*MI*/0, /*[*/GIMT_Encode2(18), GIMT_Encode2(256), /*)*//*default:*//*Label 83*/ GIMT_Encode4(5327),
    /*TargetOpcode::COPY*//*Label 0*/ GIMT_Encode4(962), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_ADD*//*Label 1*/ GIMT_Encode4(979),
    /*TargetOpcode::G_SUB*//*Label 2*/ GIMT_Encode4(1189),
    /*TargetOpcode::G_MUL*//*Label 3*/ GIMT_Encode4(1279),
    /*TargetOpcode::G_SDIV*//*Label 4*/ GIMT_Encode4(1441),
    /*TargetOpcode::G_UDIV*//*Label 5*/ GIMT_Encode4(1506),
    /*TargetOpcode::G_SREM*//*Label 6*/ GIMT_Encode4(1587),
    /*TargetOpcode::G_UREM*//*Label 7*/ GIMT_Encode4(1636), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_AND*//*Label 8*/ GIMT_Encode4(1717),
    /*TargetOpcode::G_OR*//*Label 9*/ GIMT_Encode4(1920),
    /*TargetOpcode::G_XOR*//*Label 10*/ GIMT_Encode4(2100), GIMT_Encode4(0),
    /*TargetOpcode::G_PHI*//*Label 11*/ GIMT_Encode4(2238), GIMT_Encode4(0),
    /*TargetOpcode::G_GLOBAL_VALUE*//*Label 12*/ GIMT_Encode4(2255), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_UNMERGE_VALUES*//*Label 13*/ GIMT_Encode4(2272), GIMT_Encode4(0),
    /*TargetOpcode::G_MERGE_VALUES*//*Label 14*/ GIMT_Encode4(2353),
    /*TargetOpcode::G_BUILD_VECTOR*//*Label 15*/ GIMT_Encode4(2370),
    /*TargetOpcode::G_BUILD_VECTOR_TRUNC*//*Label 16*/ GIMT_Encode4(2403), GIMT_Encode4(0),
    /*TargetOpcode::G_PTRTOINT*//*Label 17*/ GIMT_Encode4(2420),
    /*TargetOpcode::G_INTTOPTR*//*Label 18*/ GIMT_Encode4(2447),
    /*TargetOpcode::G_BITCAST*//*Label 19*/ GIMT_Encode4(2464),
    /*TargetOpcode::G_FREEZE*//*Label 20*/ GIMT_Encode4(2491), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_LOAD*//*Label 21*/ GIMT_Encode4(2523),
    /*TargetOpcode::G_SEXTLOAD*//*Label 22*/ GIMT_Encode4(2540),
    /*TargetOpcode::G_ZEXTLOAD*//*Label 23*/ GIMT_Encode4(2557), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_STORE*//*Label 24*/ GIMT_Encode4(2574), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_ANYEXT*//*Label 25*/ GIMT_Encode4(2591),
    /*TargetOpcode::G_TRUNC*//*Label 26*/ GIMT_Encode4(2640), GIMT_Encode4(0),
    /*TargetOpcode::G_FCONSTANT*//*Label 27*/ GIMT_Encode4(2721), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_SEXT*//*Label 28*/ GIMT_Encode4(2738),
    /*TargetOpcode::G_SEXT_INREG*//*Label 29*/ GIMT_Encode4(2771),
    /*TargetOpcode::G_ZEXT*//*Label 30*/ GIMT_Encode4(2836),
    /*TargetOpcode::G_SHL*//*Label 31*/ GIMT_Encode4(2885),
    /*TargetOpcode::G_LSHR*//*Label 32*/ GIMT_Encode4(3055),
    /*TargetOpcode::G_ASHR*//*Label 33*/ GIMT_Encode4(3209),
    /*TargetOpcode::G_FSHL*//*Label 34*/ GIMT_Encode4(3379),
    /*TargetOpcode::G_FSHR*//*Label 35*/ GIMT_Encode4(3439),
    /*TargetOpcode::G_ROTR*//*Label 36*/ GIMT_Encode4(3499),
    /*TargetOpcode::G_ROTL*//*Label 37*/ GIMT_Encode4(3525),
    /*TargetOpcode::G_ICMP*//*Label 38*/ GIMT_Encode4(3551), GIMT_Encode4(0),
    /*TargetOpcode::G_SELECT*//*Label 39*/ GIMT_Encode4(3616),
    /*TargetOpcode::G_UADDO*//*Label 40*/ GIMT_Encode4(3712),
    /*TargetOpcode::G_UADDE*//*Label 41*/ GIMT_Encode4(3729), GIMT_Encode4(0),
    /*TargetOpcode::G_USUBE*//*Label 42*/ GIMT_Encode4(3746),
    /*TargetOpcode::G_SADDO*//*Label 43*/ GIMT_Encode4(3763),
    /*TargetOpcode::G_SADDE*//*Label 44*/ GIMT_Encode4(3780), GIMT_Encode4(0),
    /*TargetOpcode::G_SSUBE*//*Label 45*/ GIMT_Encode4(3797),
    /*TargetOpcode::G_UMULO*//*Label 46*/ GIMT_Encode4(3814),
    /*TargetOpcode::G_SMULO*//*Label 47*/ GIMT_Encode4(3847),
    /*TargetOpcode::G_UMULH*//*Label 48*/ GIMT_Encode4(3880), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_USHLSAT*//*Label 49*/ GIMT_Encode4(3897),
    /*TargetOpcode::G_SSHLSAT*//*Label 50*/ GIMT_Encode4(3930), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_FADD*//*Label 51*/ GIMT_Encode4(3963),
    /*TargetOpcode::G_FSUB*//*Label 52*/ GIMT_Encode4(4117),
    /*TargetOpcode::G_FMUL*//*Label 53*/ GIMT_Encode4(4246),
    /*TargetOpcode::G_FMA*//*Label 54*/ GIMT_Encode4(4336),
    /*TargetOpcode::G_FMAD*//*Label 55*/ GIMT_Encode4(4369),
    /*TargetOpcode::G_FDIV*//*Label 56*/ GIMT_Encode4(4402),
    /*TargetOpcode::G_FREM*//*Label 57*/ GIMT_Encode4(4451), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_FLOG2*//*Label 58*/ GIMT_Encode4(4468), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_FNEG*//*Label 59*/ GIMT_Encode4(4491), GIMT_Encode4(0),
    /*TargetOpcode::G_FPTRUNC*//*Label 60*/ GIMT_Encode4(4545),
    /*TargetOpcode::G_FPTOSI*//*Label 61*/ GIMT_Encode4(4594),
    /*TargetOpcode::G_FPTOUI*//*Label 62*/ GIMT_Encode4(4611),
    /*TargetOpcode::G_SITOFP*//*Label 63*/ GIMT_Encode4(4628),
    /*TargetOpcode::G_UITOFP*//*Label 64*/ GIMT_Encode4(4661),
    /*TargetOpcode::G_FABS*//*Label 65*/ GIMT_Encode4(4694), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_FCANONICALIZE*//*Label 66*/ GIMT_Encode4(4781),
    /*TargetOpcode::G_FMINNUM*//*Label 67*/ GIMT_Encode4(4813),
    /*TargetOpcode::G_FMAXNUM*//*Label 68*/ GIMT_Encode4(4846), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_FMINIMUM*//*Label 69*/ GIMT_Encode4(4879),
    /*TargetOpcode::G_FMAXIMUM*//*Label 70*/ GIMT_Encode4(4912), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_PTR_ADD*//*Label 71*/ GIMT_Encode4(4945), GIMT_Encode4(0),
    /*TargetOpcode::G_SMIN*//*Label 72*/ GIMT_Encode4(5051),
    /*TargetOpcode::G_SMAX*//*Label 73*/ GIMT_Encode4(5068),
    /*TargetOpcode::G_UMIN*//*Label 74*/ GIMT_Encode4(5085),
    /*TargetOpcode::G_UMAX*//*Label 75*/ GIMT_Encode4(5102),
    /*TargetOpcode::G_ABS*//*Label 76*/ GIMT_Encode4(5119), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_BR*//*Label 77*/ GIMT_Encode4(5136), GIMT_Encode4(0),
    /*TargetOpcode::G_INSERT_VECTOR_ELT*//*Label 78*/ GIMT_Encode4(5153),
    /*TargetOpcode::G_EXTRACT_VECTOR_ELT*//*Label 79*/ GIMT_Encode4(5186),
    /*TargetOpcode::G_SHUFFLE_VECTOR*//*Label 80*/ GIMT_Encode4(5235), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_FSQRT*//*Label 81*/ GIMT_Encode4(5284), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0), GIMT_Encode4(0),
    /*TargetOpcode::G_VECREDUCE_ADD*//*Label 82*/ GIMT_Encode4(5307),
    // Label 0: @962
    GIM_Try, /*On fail goto*//*Label 84*/ GIMT_Encode4(978), // Rule ID 0 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule0Enabled),
      // MIs[0] d
      // No operand predicates
      // MIs[0] s
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner0),
      // Combiner Rule #0: copy_prop
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner0),
      GIR_Done,
    // Label 84: @978
    GIM_Reject,
    // Label 1: @979
    GIM_Try, /*On fail goto*//*Label 85*/ GIMT_Encode4(995), // Rule ID 2 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule2Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner2),
      // Combiner Rule #2: add_p2i_to_ptradd; wip_match_opcode 'G_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner2),
      GIR_Done,
    // Label 85: @995
    GIM_Try, /*On fail goto*//*Label 86*/ GIMT_Encode4(1011), // Rule ID 27 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule17Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #17: propagate_undef_any_op; wip_match_opcode 'G_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 86: @1011
    GIM_Try, /*On fail goto*//*Label 87*/ GIMT_Encode4(1027), // Rule ID 73 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule42Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner29),
      // Combiner Rule #42: simplify_add_to_sub; wip_match_opcode 'G_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner20),
      GIR_Done,
    // Label 87: @1027
    GIM_Try, /*On fail goto*//*Label 88*/ GIMT_Encode4(1043), // Rule ID 164 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule103Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner80),
      // Combiner Rule #103: constant_fold_binop; wip_match_opcode 'G_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner50),
      GIR_Done,
    // Label 88: @1043
    GIM_Try, /*On fail goto*//*Label 89*/ GIMT_Encode4(1059), // Rule ID 201 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 89: @1059
    GIM_Try, /*On fail goto*//*Label 90*/ GIMT_Encode4(1075), // Rule ID 231 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule126Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner102),
      // Combiner Rule #126: commute_int_constant_to_rhs; wip_match_opcode 'G_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner57),
      GIR_Done,
    // Label 90: @1075
    GIM_Try, /*On fail goto*//*Label 91*/ GIMT_Encode4(1111), // Rule ID 64 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule34Enabled),
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
      // Combiner Rule #34: add_sub_reg @ [__add_sub_reg_match_0[1]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/1, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 91: @1111
    GIM_Try, /*On fail goto*//*Label 92*/ GIMT_Encode4(1147), // Rule ID 63 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule34Enabled),
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
      // Combiner Rule #34: add_sub_reg @ [__add_sub_reg_match_0[0]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/1, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 92: @1147
    GIM_Try, /*On fail goto*//*Label 93*/ GIMT_Encode4(1172), // Rule ID 41 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #24: right_identity_zero @ [__right_identity_zero_match_0[1]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 93: @1172
    GIM_Try, /*On fail goto*//*Label 94*/ GIMT_Encode4(1188), // Rule ID 81 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule46Enabled),
      // MIs[0] root
      // No operand predicates
      // MIs[0] src1
      // No operand predicates
      // MIs[0] src2
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner33),
      // Combiner Rule #46: reassoc_comm_binops
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 94: @1188
    GIM_Reject,
    // Label 2: @1189
    GIM_Try, /*On fail goto*//*Label 95*/ GIMT_Encode4(1205), // Rule ID 30 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule17Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #17: propagate_undef_any_op; wip_match_opcode 'G_SUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 95: @1205
    GIM_Try, /*On fail goto*//*Label 96*/ GIMT_Encode4(1221), // Rule ID 165 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule103Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner80),
      // Combiner Rule #103: constant_fold_binop; wip_match_opcode 'G_SUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner50),
      GIR_Done,
    // Label 96: @1221
    GIM_Try, /*On fail goto*//*Label 97*/ GIMT_Encode4(1237), // Rule ID 202 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_SUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 97: @1237
    GIM_Try, /*On fail goto*//*Label 98*/ GIMT_Encode4(1253), // Rule ID 227 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule122Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner98),
      // Combiner Rule #122: sub_add_reg; wip_match_opcode 'G_SUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 98: @1253
    GIM_Try, /*On fail goto*//*Label 99*/ GIMT_Encode4(1278), // Rule ID 40 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #24: right_identity_zero @ [__right_identity_zero_match_0[0]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 99: @1278
    GIM_Reject,
    // Label 3: @1279
    GIM_Try, /*On fail goto*//*Label 100*/ GIMT_Encode4(1295), // Rule ID 18 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule12Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #12: undef_to_int_zero; wip_match_opcode 'G_MUL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner9),
      GIR_Done,
    // Label 100: @1295
    GIM_Try, /*On fail goto*//*Label 101*/ GIMT_Encode4(1311), // Rule ID 166 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule103Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner80),
      // Combiner Rule #103: constant_fold_binop; wip_match_opcode 'G_MUL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner50),
      GIR_Done,
    // Label 101: @1311
    GIM_Try, /*On fail goto*//*Label 102*/ GIMT_Encode4(1327), // Rule ID 232 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule126Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner102),
      // Combiner Rule #126: commute_int_constant_to_rhs; wip_match_opcode 'G_MUL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner57),
      GIR_Done,
    // Label 102: @1327
    GIM_Try, /*On fail goto*//*Label 103*/ GIMT_Encode4(1374), // Rule ID 3 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule3Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] x
      GIM_RecordRegType, /*MI*/0, /*Op*/1, /*TempTypeIdx*/uint8_t(-1),
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, uint8_t(-1),
      GIR_MakeTempReg, /*TempRegID*/0, /*TypeID*/uint8_t(-1),
      GIR_BuildConstant, /*TempRegID*/0, /*Val*/GIMT_Encode8(0),
      // Combiner Rule #3: mul_by_neg_one
      GIR_BuildMI, /*InsnID*/0, /*Opcode*/GIMT_Encode2(TargetOpcode::G_SUB),
      GIR_Copy, /*NewInsnID*/0, /*OldInsnID*/0, /*OpIdx*/0, // dst
      GIR_AddSimpleTempRegister, /*InsnID*/0, /*TempRegID*/0,
      GIR_Copy, /*NewInsnID*/0, /*OldInsnID*/0, /*OpIdx*/1, // x
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 103: @1374
    GIM_Try, /*On fail goto*//*Label 104*/ GIMT_Encode4(1399), // Rule ID 56 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule27Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] zero
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/2,
      // Combiner Rule #27: binop_right_to_zero
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/2,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 104: @1399
    GIM_Try, /*On fail goto*//*Label 105*/ GIMT_Encode4(1424), // Rule ID 61 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule32Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] x
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 1,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #32: right_identity_one_int
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 105: @1424
    GIM_Try, /*On fail goto*//*Label 106*/ GIMT_Encode4(1440), // Rule ID 1 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule1Enabled),
      // MIs[0] d
      // No operand predicates
      // MIs[0] op1
      // No operand predicates
      // MIs[0] op2
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner1),
      // Combiner Rule #1: mul_to_shl
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner1),
      GIR_Done,
    // Label 106: @1440
    GIM_Reject,
    // Label 4: @1441
    GIM_Try, /*On fail goto*//*Label 107*/ GIMT_Encode4(1457), // Rule ID 52 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule26Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner18),
      // Combiner Rule #26: binop_left_to_zero; wip_match_opcode 'G_SDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner14),
      GIR_Done,
    // Label 107: @1457
    GIM_Try, /*On fail goto*//*Label 108*/ GIMT_Encode4(1473), // Rule ID 146 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule92Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner71),
      // Combiner Rule #92: div_rem_to_divrem; wip_match_opcode 'G_SDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner47),
      GIR_Done,
    // Label 108: @1473
    GIM_Try, /*On fail goto*//*Label 109*/ GIMT_Encode4(1489), // Rule ID 184 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule109Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner85),
      // Combiner Rule #109: sdiv_by_const; wip_match_opcode 'G_SDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner53),
      GIR_Done,
    // Label 109: @1489
    GIM_Try, /*On fail goto*//*Label 110*/ GIMT_Encode4(1505), // Rule ID 207 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_SDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 110: @1505
    GIM_Reject,
    // Label 5: @1506
    GIM_Try, /*On fail goto*//*Label 111*/ GIMT_Encode4(1522), // Rule ID 21 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule14Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner10),
      // Combiner Rule #14: binop_left_undef_to_zero; wip_match_opcode 'G_UDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner9),
      GIR_Done,
    // Label 111: @1522
    GIM_Try, /*On fail goto*//*Label 112*/ GIMT_Encode4(1538), // Rule ID 53 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule26Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner18),
      // Combiner Rule #26: binop_left_to_zero; wip_match_opcode 'G_UDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner14),
      GIR_Done,
    // Label 112: @1538
    GIM_Try, /*On fail goto*//*Label 113*/ GIMT_Encode4(1554), // Rule ID 147 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule92Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner71),
      // Combiner Rule #92: div_rem_to_divrem; wip_match_opcode 'G_UDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner47),
      GIR_Done,
    // Label 113: @1554
    GIM_Try, /*On fail goto*//*Label 114*/ GIMT_Encode4(1570), // Rule ID 183 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule108Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner84),
      // Combiner Rule #108: udiv_by_const; wip_match_opcode 'G_UDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner52),
      GIR_Done,
    // Label 114: @1570
    GIM_Try, /*On fail goto*//*Label 115*/ GIMT_Encode4(1586), // Rule ID 209 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_UDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 115: @1586
    GIM_Reject,
    // Label 6: @1587
    GIM_Try, /*On fail goto*//*Label 116*/ GIMT_Encode4(1603), // Rule ID 54 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule26Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner18),
      // Combiner Rule #26: binop_left_to_zero; wip_match_opcode 'G_SREM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner14),
      GIR_Done,
    // Label 116: @1603
    GIM_Try, /*On fail goto*//*Label 117*/ GIMT_Encode4(1619), // Rule ID 148 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule92Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner71),
      // Combiner Rule #92: div_rem_to_divrem; wip_match_opcode 'G_SREM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner47),
      GIR_Done,
    // Label 117: @1619
    GIM_Try, /*On fail goto*//*Label 118*/ GIMT_Encode4(1635), // Rule ID 208 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_SREM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 118: @1635
    GIM_Reject,
    // Label 7: @1636
    GIM_Try, /*On fail goto*//*Label 119*/ GIMT_Encode4(1652), // Rule ID 22 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule14Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner10),
      // Combiner Rule #14: binop_left_undef_to_zero; wip_match_opcode 'G_UREM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner9),
      GIR_Done,
    // Label 119: @1652
    GIM_Try, /*On fail goto*//*Label 120*/ GIMT_Encode4(1668), // Rule ID 55 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule26Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner18),
      // Combiner Rule #26: binop_left_to_zero; wip_match_opcode 'G_UREM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner14),
      GIR_Done,
    // Label 120: @1668
    GIM_Try, /*On fail goto*//*Label 121*/ GIMT_Encode4(1684), // Rule ID 93 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule58Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner44),
      // Combiner Rule #58: urem_pow2_to_mask; wip_match_opcode 'G_UREM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner28),
      GIR_Done,
    // Label 121: @1684
    GIM_Try, /*On fail goto*//*Label 122*/ GIMT_Encode4(1700), // Rule ID 149 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule92Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner71),
      // Combiner Rule #92: div_rem_to_divrem; wip_match_opcode 'G_UREM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner47),
      GIR_Done,
    // Label 122: @1700
    GIM_Try, /*On fail goto*//*Label 123*/ GIMT_Encode4(1716), // Rule ID 210 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_UREM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 123: @1716
    GIM_Reject,
    // Label 8: @1717
    GIM_Try, /*On fail goto*//*Label 124*/ GIMT_Encode4(1733), // Rule ID 13 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule9Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner7),
      // Combiner Rule #9: load_and_mask; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 124: @1733
    GIM_Try, /*On fail goto*//*Label 125*/ GIMT_Encode4(1749), // Rule ID 17 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule12Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #12: undef_to_int_zero; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner9),
      GIR_Done,
    // Label 125: @1749
    GIM_Try, /*On fail goto*//*Label 126*/ GIMT_Encode4(1765), // Rule ID 74 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule43Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner30),
      // Combiner Rule #43: hoist_logic_op_with_same_opcode_hands; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner21),
      GIR_Done,
    // Label 126: @1765
    GIM_Try, /*On fail goto*//*Label 127*/ GIMT_Encode4(1781), // Rule ID 86 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule51Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner38),
      // Combiner Rule #51: narrow_binop_feeding_and; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 127: @1781
    GIM_Try, /*On fail goto*//*Label 128*/ GIMT_Encode4(1797), // Rule ID 90 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule55Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner41),
      // Combiner Rule #55: redundant_and; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 128: @1797
    GIM_Try, /*On fail goto*//*Label 129*/ GIMT_Encode4(1813), // Rule ID 118 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule81Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner60),
      // Combiner Rule #81: overlapping_and; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 129: @1813
    GIM_Try, /*On fail goto*//*Label 130*/ GIMT_Encode4(1829), // Rule ID 159 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule100Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner77),
      // Combiner Rule #100: bitfield_extract_from_and; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 130: @1829
    GIM_Try, /*On fail goto*//*Label 131*/ GIMT_Encode4(1845), // Rule ID 167 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule103Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner80),
      // Combiner Rule #103: constant_fold_binop; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner50),
      GIR_Done,
    // Label 131: @1845
    GIM_Try, /*On fail goto*//*Label 132*/ GIMT_Encode4(1861), // Rule ID 192 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule112Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner88),
      // Combiner Rule #112: and_or_disjoint_mask; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 132: @1861
    GIM_Try, /*On fail goto*//*Label 133*/ GIMT_Encode4(1877), // Rule ID 204 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 133: @1877
    GIM_Try, /*On fail goto*//*Label 134*/ GIMT_Encode4(1893), // Rule ID 233 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule126Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner102),
      // Combiner Rule #126: commute_int_constant_to_rhs; wip_match_opcode 'G_AND'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner57),
      GIR_Done,
    // Label 134: @1893
    GIM_Try, /*On fail goto*//*Label 135*/ GIMT_Encode4(1919), // Rule ID 50 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule25Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src
      // No operand predicates
      // MIs[0] src
      GIM_CheckIsSameOperandIgnoreCopies, /*MI*/0, /*OpIdx*/2, /*OtherMI*/0, /*OtherOpIdx*/1,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #25: binop_same_val @ [__binop_same_val_match_0[0]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 135: @1919
    GIM_Reject,
    // Label 9: @1920
    GIM_Try, /*On fail goto*//*Label 136*/ GIMT_Encode4(1936), // Rule ID 19 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule13Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #13: undef_to_negative_one; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner10),
      GIR_Done,
    // Label 136: @1936
    GIM_Try, /*On fail goto*//*Label 137*/ GIMT_Encode4(1952), // Rule ID 75 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule43Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner30),
      // Combiner Rule #43: hoist_logic_op_with_same_opcode_hands; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner21),
      GIR_Done,
    // Label 137: @1952
    GIM_Try, /*On fail goto*//*Label 138*/ GIMT_Encode4(1968), // Rule ID 92 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule57Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner43),
      // Combiner Rule #57: redundant_or; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 138: @1968
    GIM_Try, /*On fail goto*//*Label 139*/ GIMT_Encode4(1984), // Rule ID 145 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule91Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner70),
      // Combiner Rule #91: load_or_combine; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 139: @1984
    GIM_Try, /*On fail goto*//*Label 140*/ GIMT_Encode4(2000), // Rule ID 150 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule93Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner72),
      // Combiner Rule #93: funnel_shift_from_or_shift; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 140: @2000
    GIM_Try, /*On fail goto*//*Label 141*/ GIMT_Encode4(2016), // Rule ID 168 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule103Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner80),
      // Combiner Rule #103: constant_fold_binop; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner50),
      GIR_Done,
    // Label 141: @2016
    GIM_Try, /*On fail goto*//*Label 142*/ GIMT_Encode4(2032), // Rule ID 205 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 142: @2032
    GIM_Try, /*On fail goto*//*Label 143*/ GIMT_Encode4(2048), // Rule ID 234 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule126Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner102),
      // Combiner Rule #126: commute_int_constant_to_rhs; wip_match_opcode 'G_OR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner57),
      GIR_Done,
    // Label 143: @2048
    GIM_Try, /*On fail goto*//*Label 144*/ GIMT_Encode4(2074), // Rule ID 51 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule25Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src
      // No operand predicates
      // MIs[0] src
      GIM_CheckIsSameOperandIgnoreCopies, /*MI*/0, /*OpIdx*/2, /*OtherMI*/0, /*OtherOpIdx*/1,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #25: binop_same_val @ [__binop_same_val_match_0[1]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 144: @2074
    GIM_Try, /*On fail goto*//*Label 145*/ GIMT_Encode4(2099), // Rule ID 42 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #24: right_identity_zero @ [__right_identity_zero_match_0[2]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 145: @2099
    GIM_Reject,
    // Label 10: @2100
    GIM_Try, /*On fail goto*//*Label 146*/ GIMT_Encode4(2116), // Rule ID 31 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule17Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #17: propagate_undef_any_op; wip_match_opcode 'G_XOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 146: @2116
    GIM_Try, /*On fail goto*//*Label 147*/ GIMT_Encode4(2132), // Rule ID 76 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule43Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner30),
      // Combiner Rule #43: hoist_logic_op_with_same_opcode_hands; wip_match_opcode 'G_XOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner21),
      GIR_Done,
    // Label 147: @2132
    GIM_Try, /*On fail goto*//*Label 148*/ GIMT_Encode4(2148), // Rule ID 101 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule64Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner50),
      // Combiner Rule #64: not_cmp_fold; wip_match_opcode 'G_XOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner32),
      GIR_Done,
    // Label 148: @2148
    GIM_Try, /*On fail goto*//*Label 149*/ GIMT_Encode4(2164), // Rule ID 133 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule87Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner66),
      // Combiner Rule #87: xor_of_and_with_same_reg; wip_match_opcode 'G_XOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner43),
      GIR_Done,
    // Label 149: @2164
    GIM_Try, /*On fail goto*//*Label 150*/ GIMT_Encode4(2180), // Rule ID 169 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule103Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner80),
      // Combiner Rule #103: constant_fold_binop; wip_match_opcode 'G_XOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner50),
      GIR_Done,
    // Label 150: @2180
    GIM_Try, /*On fail goto*//*Label 151*/ GIMT_Encode4(2196), // Rule ID 206 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_XOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 151: @2196
    GIM_Try, /*On fail goto*//*Label 152*/ GIMT_Encode4(2212), // Rule ID 235 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule126Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner102),
      // Combiner Rule #126: commute_int_constant_to_rhs; wip_match_opcode 'G_XOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner57),
      GIR_Done,
    // Label 152: @2212
    GIM_Try, /*On fail goto*//*Label 153*/ GIMT_Encode4(2237), // Rule ID 43 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #24: right_identity_zero @ [__right_identity_zero_match_0[3]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 153: @2237
    GIM_Reject,
    // Label 11: @2238
    GIM_Try, /*On fail goto*//*Label 154*/ GIMT_Encode4(2254), // Rule ID 72 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule41Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner28),
      // Combiner Rule #41: extend_through_phis; wip_match_opcode 'G_PHI'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner19),
      GIR_Done,
    // Label 154: @2254
    GIM_Reject,
    // Label 12: @2255
    GIM_Try, /*On fail goto*//*Label 155*/ GIMT_Encode4(2271), // Rule ID 240 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule130Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner106),
      // Combiner Rule #130: fold_global_offset; wip_match_opcode 'G_GLOBAL_VALUE'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner60),
      GIR_Done,
    // Label 155: @2271
    GIM_Reject,
    // Label 13: @2272
    GIM_Try, /*On fail goto*//*Label 156*/ GIMT_Encode4(2288), // Rule ID 36 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule21Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner15),
      // Combiner Rule #21: unmerge_undef; wip_match_opcode 'G_UNMERGE_VALUES'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 156: @2288
    GIM_Try, /*On fail goto*//*Label 157*/ GIMT_Encode4(2304), // Rule ID 103 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule66Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner52),
      // Combiner Rule #66: unmerge_merge; wip_match_opcode 'G_UNMERGE_VALUES'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner34),
      GIR_Done,
    // Label 157: @2304
    GIM_Try, /*On fail goto*//*Label 158*/ GIMT_Encode4(2320), // Rule ID 104 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule67Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner53),
      // Combiner Rule #67: unmerge_cst; wip_match_opcode 'G_UNMERGE_VALUES'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner35),
      GIR_Done,
    // Label 158: @2320
    GIM_Try, /*On fail goto*//*Label 159*/ GIMT_Encode4(2336), // Rule ID 105 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule68Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner54),
      // Combiner Rule #68: unmerge_dead_to_trunc; wip_match_opcode 'G_UNMERGE_VALUES'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner36),
      GIR_Done,
    // Label 159: @2336
    GIM_Try, /*On fail goto*//*Label 160*/ GIMT_Encode4(2352), // Rule ID 106 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule69Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner55),
      // Combiner Rule #69: unmerge_zext_to_zext; wip_match_opcode 'G_UNMERGE_VALUES'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner37),
      GIR_Done,
    // Label 160: @2352
    GIM_Reject,
    // Label 14: @2353
    GIM_Try, /*On fail goto*//*Label 161*/ GIMT_Encode4(2369), // Rule ID 107 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule70Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner56),
      // Combiner Rule #70: merge_unmerge; wip_match_opcode 'G_MERGE_VALUES'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 161: @2369
    GIM_Reject,
    // Label 15: @2370
    GIM_Try, /*On fail goto*//*Label 162*/ GIMT_Encode4(2386), // Rule ID 9 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule7Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner5),
      // Combiner Rule #7: extract_all_elts_from_build_vector; wip_match_opcode 'G_BUILD_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner5),
      GIR_Done,
    // Label 162: @2386
    GIM_Try, /*On fail goto*//*Label 163*/ GIMT_Encode4(2402), // Rule ID 66 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule35Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner23),
      // Combiner Rule #35: buildvector_identity_fold; wip_match_opcode 'G_BUILD_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 163: @2402
    GIM_Reject,
    // Label 16: @2403
    GIM_Try, /*On fail goto*//*Label 164*/ GIMT_Encode4(2419), // Rule ID 65 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule35Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner23),
      // Combiner Rule #35: buildvector_identity_fold; wip_match_opcode 'G_BUILD_VECTOR_TRUNC'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 164: @2419
    GIM_Reject,
    // Label 17: @2420
    GIM_Try, /*On fail goto*//*Label 165*/ GIMT_Encode4(2446), // Rule ID 58 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule29Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] t
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_INTTOPTR),
      // MIs[1] ptr
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner20),
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #29: i2p_to_p2i
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner16),
      GIR_Done,
    // Label 165: @2446
    GIM_Reject,
    // Label 18: @2447
    GIM_Try, /*On fail goto*//*Label 166*/ GIMT_Encode4(2463), // Rule ID 57 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule28Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner19),
      // Combiner Rule #28: p2i_to_i2p; wip_match_opcode 'G_INTTOPTR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner15),
      GIR_Done,
    // Label 166: @2463
    GIM_Reject,
    // Label 19: @2464
    GIM_Try, /*On fail goto*//*Label 167*/ GIMT_Encode4(2490), // Rule ID 69 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule38Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src1
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_BITCAST),
      // MIs[1] src0
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner26),
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #38: bitcast_bitcast_fold
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner18),
      GIR_Done,
    // Label 167: @2490
    GIM_Reject,
    // Label 20: @2491
    GIM_Try, /*On fail goto*//*Label 168*/ GIMT_Encode4(2522), // Rule ID 4 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule4Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FREEZE),
      // MIs[1] __idempotent_prop_match_0.x
      // No operand predicates
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #4: idempotent_prop @ [__idempotent_prop_match_0[0]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 168: @2522
    GIM_Reject,
    // Label 21: @2523
    GIM_Try, /*On fail goto*//*Label 169*/ GIMT_Encode4(2539), // Rule ID 10 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule8Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner6),
      // Combiner Rule #8: extending_loads; wip_match_opcode 'G_LOAD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner6),
      GIR_Done,
    // Label 169: @2539
    GIM_Reject,
    // Label 22: @2540
    GIM_Try, /*On fail goto*//*Label 170*/ GIMT_Encode4(2556), // Rule ID 11 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule8Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner6),
      // Combiner Rule #8: extending_loads; wip_match_opcode 'G_SEXTLOAD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner6),
      GIR_Done,
    // Label 170: @2556
    GIM_Reject,
    // Label 23: @2557
    GIM_Try, /*On fail goto*//*Label 171*/ GIMT_Encode4(2573), // Rule ID 12 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule8Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner6),
      // Combiner Rule #8: extending_loads; wip_match_opcode 'G_ZEXTLOAD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner6),
      GIR_Done,
    // Label 171: @2573
    GIM_Reject,
    // Label 24: @2574
    GIM_Try, /*On fail goto*//*Label 172*/ GIMT_Encode4(2590), // Rule ID 35 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule20Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner14),
      // Combiner Rule #20: erase_undef_store; wip_match_opcode 'G_STORE'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner12),
      GIR_Done,
    // Label 172: @2590
    GIM_Reject,
    // Label 25: @2591
    GIM_Try, /*On fail goto*//*Label 173*/ GIMT_Encode4(2607), // Rule ID 59 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule30Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner21),
      // Combiner Rule #30: anyext_trunc_fold; wip_match_opcode 'G_ANYEXT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 173: @2607
    GIM_Try, /*On fail goto*//*Label 174*/ GIMT_Encode4(2623), // Rule ID 98 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule63Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner49),
      // Combiner Rule #63: ext_ext_fold; wip_match_opcode 'G_ANYEXT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner31),
      GIR_Done,
    // Label 174: @2623
    GIM_Try, /*On fail goto*//*Label 175*/ GIMT_Encode4(2639), // Rule ID 181 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule106Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner83),
      // Combiner Rule #106: constant_fold_cast_op; wip_match_opcode 'G_ANYEXT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner50),
      GIR_Done,
    // Label 175: @2639
    GIM_Reject,
    // Label 26: @2640
    GIM_Try, /*On fail goto*//*Label 176*/ GIMT_Encode4(2656), // Rule ID 32 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule17Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #17: propagate_undef_any_op; wip_match_opcode 'G_TRUNC'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 176: @2656
    GIM_Try, /*On fail goto*//*Label 177*/ GIMT_Encode4(2672), // Rule ID 67 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule36Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner24),
      // Combiner Rule #36: trunc_buildvector_fold; wip_match_opcode 'G_TRUNC'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 177: @2672
    GIM_Try, /*On fail goto*//*Label 178*/ GIMT_Encode4(2688), // Rule ID 68 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule37Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner25),
      // Combiner Rule #37: trunc_lshr_buildvector_fold; wip_match_opcode 'G_TRUNC'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 178: @2688
    GIM_Try, /*On fail goto*//*Label 179*/ GIMT_Encode4(2704), // Rule ID 108 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule71Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner57),
      // Combiner Rule #71: trunc_ext_fold; wip_match_opcode 'G_TRUNC'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner38),
      GIR_Done,
    // Label 179: @2704
    GIM_Try, /*On fail goto*//*Label 180*/ GIMT_Encode4(2720), // Rule ID 109 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule72Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner58),
      // Combiner Rule #72: trunc_shift; wip_match_opcode 'G_TRUNC'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner39),
      GIR_Done,
    // Label 180: @2720
    GIM_Reject,
    // Label 27: @2721
    GIM_Try, /*On fail goto*//*Label 181*/ GIMT_Encode4(2737), // Rule ID 238 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule128Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner104),
      // Combiner Rule #128: fconstant_to_constant; wip_match_opcode 'G_FCONSTANT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner58),
      GIR_Done,
    // Label 181: @2737
    GIM_Reject,
    // Label 28: @2738
    GIM_Try, /*On fail goto*//*Label 182*/ GIMT_Encode4(2754), // Rule ID 99 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule63Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner49),
      // Combiner Rule #63: ext_ext_fold; wip_match_opcode 'G_SEXT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner31),
      GIR_Done,
    // Label 182: @2754
    GIM_Try, /*On fail goto*//*Label 183*/ GIMT_Encode4(2770), // Rule ID 180 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule106Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner83),
      // Combiner Rule #106: constant_fold_cast_op; wip_match_opcode 'G_SEXT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner50),
      GIR_Done,
    // Label 183: @2770
    GIM_Reject,
    // Label 29: @2771
    GIM_Try, /*On fail goto*//*Label 184*/ GIMT_Encode4(2787), // Rule ID 84 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule49Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner36),
      // Combiner Rule #49: sext_inreg_of_load; wip_match_opcode 'G_SEXT_INREG'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner25),
      GIR_Done,
    // Label 184: @2787
    GIM_Try, /*On fail goto*//*Label 185*/ GIMT_Encode4(2803), // Rule ID 91 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule56Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner42),
      // Combiner Rule #56: redundant_sext_inreg; wip_match_opcode 'G_SEXT_INREG'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner14),
      GIR_Done,
    // Label 185: @2803
    GIM_Try, /*On fail goto*//*Label 186*/ GIMT_Encode4(2819), // Rule ID 158 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule99Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner76),
      // Combiner Rule #99: bitfield_extract_from_sext_inreg; wip_match_opcode 'G_SEXT_INREG'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 186: @2819
    GIM_Try, /*On fail goto*//*Label 187*/ GIMT_Encode4(2835), // Rule ID 97 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule62Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src
      // No operand predicates
      // MIs[0] imm
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner48),
      // Combiner Rule #62: sext_inreg_to_zext_inreg
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner30),
      GIR_Done,
    // Label 187: @2835
    GIM_Reject,
    // Label 30: @2836
    GIM_Try, /*On fail goto*//*Label 188*/ GIMT_Encode4(2852), // Rule ID 94 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule59Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner45),
      // Combiner Rule #59: zext_trunc_fold; wip_match_opcode 'G_ZEXT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner17),
      GIR_Done,
    // Label 188: @2852
    GIM_Try, /*On fail goto*//*Label 189*/ GIMT_Encode4(2868), // Rule ID 100 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule63Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner49),
      // Combiner Rule #63: ext_ext_fold; wip_match_opcode 'G_ZEXT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner31),
      GIR_Done,
    // Label 189: @2868
    GIM_Try, /*On fail goto*//*Label 190*/ GIMT_Encode4(2884), // Rule ID 179 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule106Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner83),
      // Combiner Rule #106: constant_fold_cast_op; wip_match_opcode 'G_ZEXT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner50),
      GIR_Done,
    // Label 190: @2884
    GIM_Reject,
    // Label 31: @2885
    GIM_Try, /*On fail goto*//*Label 191*/ GIMT_Encode4(2901), // Rule ID 20 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule14Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner10),
      // Combiner Rule #14: binop_left_undef_to_zero; wip_match_opcode 'G_SHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner9),
      GIR_Done,
    // Label 191: @2901
    GIM_Try, /*On fail goto*//*Label 192*/ GIMT_Encode4(2917), // Rule ID 23 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule15Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner11),
      // Combiner Rule #15: binop_right_undef_to_undef; wip_match_opcode 'G_SHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 192: @2917
    GIM_Try, /*On fail goto*//*Label 193*/ GIMT_Encode4(2933), // Rule ID 77 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule44Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner31),
      // Combiner Rule #44: shifts_too_big; wip_match_opcode 'G_SHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 193: @2933
    GIM_Try, /*On fail goto*//*Label 194*/ GIMT_Encode4(2949), // Rule ID 135 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule89Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner68),
      // Combiner Rule #89: shift_immed_chain; wip_match_opcode 'G_SHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner45),
      GIR_Done,
    // Label 194: @2949
    GIM_Try, /*On fail goto*//*Label 195*/ GIMT_Encode4(2965), // Rule ID 140 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule90Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner69),
      // Combiner Rule #90: shift_of_shifted_logic_chain; wip_match_opcode 'G_SHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner46),
      GIR_Done,
    // Label 195: @2965
    GIM_Try, /*On fail goto*//*Label 196*/ GIMT_Encode4(2981), // Rule ID 157 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule98Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner75),
      // Combiner Rule #98: commute_shift; wip_match_opcode 'G_SHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 196: @2981
    GIM_Try, /*On fail goto*//*Label 197*/ GIMT_Encode4(2997), // Rule ID 170 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule103Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner80),
      // Combiner Rule #103: constant_fold_binop; wip_match_opcode 'G_SHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner50),
      GIR_Done,
    // Label 197: @2997
    GIM_Try, /*On fail goto*//*Label 198*/ GIMT_Encode4(3013), // Rule ID 213 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_SHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 198: @3013
    GIM_Try, /*On fail goto*//*Label 199*/ GIMT_Encode4(3038), // Rule ID 44 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #24: right_identity_zero @ [__right_identity_zero_match_0[4]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 199: @3038
    GIM_Try, /*On fail goto*//*Label 200*/ GIMT_Encode4(3054), // Rule ID 85 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule50Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src0
      // No operand predicates
      // MIs[0] src1
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner37),
      // Combiner Rule #50: reduce_shl_of_extend
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner26),
      GIR_Done,
    // Label 200: @3054
    GIM_Reject,
    // Label 32: @3055
    GIM_Try, /*On fail goto*//*Label 201*/ GIMT_Encode4(3071), // Rule ID 25 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule15Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner11),
      // Combiner Rule #15: binop_right_undef_to_undef; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 201: @3071
    GIM_Try, /*On fail goto*//*Label 202*/ GIMT_Encode4(3087), // Rule ID 79 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule44Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner31),
      // Combiner Rule #44: shifts_too_big; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 202: @3087
    GIM_Try, /*On fail goto*//*Label 203*/ GIMT_Encode4(3103), // Rule ID 137 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule89Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner68),
      // Combiner Rule #89: shift_immed_chain; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner45),
      GIR_Done,
    // Label 203: @3103
    GIM_Try, /*On fail goto*//*Label 204*/ GIMT_Encode4(3119), // Rule ID 142 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule90Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner69),
      // Combiner Rule #90: shift_of_shifted_logic_chain; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner46),
      GIR_Done,
    // Label 204: @3119
    GIM_Try, /*On fail goto*//*Label 205*/ GIMT_Encode4(3135), // Rule ID 161 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule101Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner78),
      // Combiner Rule #101: bitfield_extract_from_shr; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 205: @3135
    GIM_Try, /*On fail goto*//*Label 206*/ GIMT_Encode4(3151), // Rule ID 163 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule102Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner79),
      // Combiner Rule #102: bitfield_extract_from_shr_and; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 206: @3151
    GIM_Try, /*On fail goto*//*Label 207*/ GIMT_Encode4(3167), // Rule ID 171 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule103Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner80),
      // Combiner Rule #103: constant_fold_binop; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner50),
      GIR_Done,
    // Label 207: @3167
    GIM_Try, /*On fail goto*//*Label 208*/ GIMT_Encode4(3183), // Rule ID 211 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_LSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 208: @3183
    GIM_Try, /*On fail goto*//*Label 209*/ GIMT_Encode4(3208), // Rule ID 46 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #24: right_identity_zero @ [__right_identity_zero_match_0[6]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 209: @3208
    GIM_Reject,
    // Label 33: @3209
    GIM_Try, /*On fail goto*//*Label 210*/ GIMT_Encode4(3225), // Rule ID 24 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule15Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner11),
      // Combiner Rule #15: binop_right_undef_to_undef; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 210: @3225
    GIM_Try, /*On fail goto*//*Label 211*/ GIMT_Encode4(3241), // Rule ID 78 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule44Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner31),
      // Combiner Rule #44: shifts_too_big; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 211: @3241
    GIM_Try, /*On fail goto*//*Label 212*/ GIMT_Encode4(3257), // Rule ID 83 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule48Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner35),
      // Combiner Rule #48: shl_ashr_to_sext_inreg; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner24),
      GIR_Done,
    // Label 212: @3257
    GIM_Try, /*On fail goto*//*Label 213*/ GIMT_Encode4(3273), // Rule ID 136 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule89Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner68),
      // Combiner Rule #89: shift_immed_chain; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner45),
      GIR_Done,
    // Label 213: @3273
    GIM_Try, /*On fail goto*//*Label 214*/ GIMT_Encode4(3289), // Rule ID 141 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule90Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner69),
      // Combiner Rule #90: shift_of_shifted_logic_chain; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner46),
      GIR_Done,
    // Label 214: @3289
    GIM_Try, /*On fail goto*//*Label 215*/ GIMT_Encode4(3305), // Rule ID 160 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule101Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner78),
      // Combiner Rule #101: bitfield_extract_from_shr; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 215: @3305
    GIM_Try, /*On fail goto*//*Label 216*/ GIMT_Encode4(3321), // Rule ID 162 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule102Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner79),
      // Combiner Rule #102: bitfield_extract_from_shr_and; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 216: @3321
    GIM_Try, /*On fail goto*//*Label 217*/ GIMT_Encode4(3337), // Rule ID 172 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule103Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner80),
      // Combiner Rule #103: constant_fold_binop; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner50),
      GIR_Done,
    // Label 217: @3337
    GIM_Try, /*On fail goto*//*Label 218*/ GIMT_Encode4(3353), // Rule ID 212 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_ASHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 218: @3353
    GIM_Try, /*On fail goto*//*Label 219*/ GIMT_Encode4(3378), // Rule ID 45 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #24: right_identity_zero @ [__right_identity_zero_match_0[5]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 219: @3378
    GIM_Reject,
    // Label 34: @3379
    GIM_Try, /*On fail goto*//*Label 220*/ GIMT_Encode4(3395), // Rule ID 151 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule94Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner73),
      // Combiner Rule #94: funnel_shift_to_rotate; wip_match_opcode 'G_FSHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner48),
      GIR_Done,
    // Label 220: @3395
    GIM_Try, /*On fail goto*//*Label 221*/ GIMT_Encode4(3411), // Rule ID 155 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule97Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner74),
      // Combiner Rule #97: funnel_shift_overshift; wip_match_opcode 'G_FSHL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner49),
      GIR_Done,
    // Label 221: @3411
    GIM_Try, /*On fail goto*//*Label 222*/ GIMT_Encode4(3438), // Rule ID 154 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule96Enabled),
      // MIs[0] x
      // No operand predicates
      // MIs[0] y
      // No operand predicates
      // MIs[0] z
      // No operand predicates
      // MIs[0] Operand 3
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/3, 0,
      // Combiner Rule #96: funnel_shift_left_zero
      GIR_BuildMI, /*InsnID*/0, /*Opcode*/GIMT_Encode2(TargetOpcode::COPY),
      GIR_Copy, /*NewInsnID*/0, /*OldInsnID*/0, /*OpIdx*/0, // x
      GIR_Copy, /*NewInsnID*/0, /*OldInsnID*/0, /*OpIdx*/1, // y
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 222: @3438
    GIM_Reject,
    // Label 35: @3439
    GIM_Try, /*On fail goto*//*Label 223*/ GIMT_Encode4(3455), // Rule ID 152 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule94Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner73),
      // Combiner Rule #94: funnel_shift_to_rotate; wip_match_opcode 'G_FSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner48),
      GIR_Done,
    // Label 223: @3455
    GIM_Try, /*On fail goto*//*Label 224*/ GIMT_Encode4(3471), // Rule ID 156 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule97Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner74),
      // Combiner Rule #97: funnel_shift_overshift; wip_match_opcode 'G_FSHR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner49),
      GIR_Done,
    // Label 224: @3471
    GIM_Try, /*On fail goto*//*Label 225*/ GIMT_Encode4(3498), // Rule ID 153 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule95Enabled),
      // MIs[0] x
      // No operand predicates
      // MIs[0] y
      // No operand predicates
      // MIs[0] z
      // No operand predicates
      // MIs[0] Operand 3
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/3, 0,
      // Combiner Rule #95: funnel_shift_right_zero
      GIR_BuildMI, /*InsnID*/0, /*Opcode*/GIMT_Encode2(TargetOpcode::COPY),
      GIR_Copy, /*NewInsnID*/0, /*OldInsnID*/0, /*OpIdx*/0, // x
      GIR_Copy, /*NewInsnID*/0, /*OldInsnID*/0, /*OpIdx*/2, // z
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 225: @3498
    GIM_Reject,
    // Label 36: @3499
    GIM_Try, /*On fail goto*//*Label 226*/ GIMT_Encode4(3524), // Rule ID 49 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #24: right_identity_zero @ [__right_identity_zero_match_0[9]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 226: @3524
    GIM_Reject,
    // Label 37: @3525
    GIM_Try, /*On fail goto*//*Label 227*/ GIMT_Encode4(3550), // Rule ID 48 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #24: right_identity_zero @ [__right_identity_zero_match_0[8]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 227: @3550
    GIM_Reject,
    // Label 38: @3551
    GIM_Try, /*On fail goto*//*Label 228*/ GIMT_Encode4(3567), // Rule ID 95 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule60Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner46),
      // Combiner Rule #60: icmp_to_true_false_known_bits; wip_match_opcode 'G_ICMP'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner29),
      GIR_Done,
    // Label 228: @3567
    GIM_Try, /*On fail goto*//*Label 229*/ GIMT_Encode4(3583), // Rule ID 96 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule61Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner47),
      // Combiner Rule #61: icmp_to_lhs_known_bits; wip_match_opcode 'G_ICMP'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 229: @3583
    GIM_Try, /*On fail goto*//*Label 230*/ GIMT_Encode4(3599), // Rule ID 229 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule124Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner100),
      // Combiner Rule #124: redundant_binop_in_equality; wip_match_opcode 'G_ICMP'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 230: @3599
    GIM_Try, /*On fail goto*//*Label 231*/ GIMT_Encode4(3615), // Rule ID 239 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule129Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner105),
      // Combiner Rule #129: icmp_redundant_trunc; wip_match_opcode 'G_ICMP'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner59),
      GIR_Done,
    // Label 231: @3615
    GIM_Reject,
    // Label 39: @3616
    GIM_Try, /*On fail goto*//*Label 232*/ GIMT_Encode4(3632), // Rule ID 39 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule23Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner17),
      // Combiner Rule #23: select_same_val; wip_match_opcode 'G_SELECT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner13),
      GIR_Done,
    // Label 232: @3632
    GIM_Try, /*On fail goto*//*Label 233*/ GIMT_Encode4(3648), // Rule ID 88 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule53Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner39),
      // Combiner Rule #53: select_constant_cmp; wip_match_opcode 'G_SELECT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 233: @3648
    GIM_Try, /*On fail goto*//*Label 234*/ GIMT_Encode4(3664), // Rule ID 89 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule54Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner40),
      // Combiner Rule #54: select_to_logical; wip_match_opcode 'G_SELECT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 234: @3664
    GIM_Try, /*On fail goto*//*Label 235*/ GIMT_Encode4(3680), // Rule ID 228 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule123Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner99),
      // Combiner Rule #123: select_to_minmax; wip_match_opcode 'G_SELECT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 235: @3680
    GIM_Try, /*On fail goto*//*Label 236*/ GIMT_Encode4(3711), // Rule ID 87 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule52Enabled),
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
      // Combiner Rule #52: select_undef_cmp
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/3,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 236: @3711
    GIM_Reject,
    // Label 40: @3712
    GIM_Try, /*On fail goto*//*Label 237*/ GIMT_Encode4(3728), // Rule ID 123 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule84Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner63),
      // Combiner Rule #84: addo_by_0; wip_match_opcode 'G_UADDO'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 237: @3728
    GIM_Reject,
    // Label 41: @3729
    GIM_Try, /*On fail goto*//*Label 238*/ GIMT_Encode4(3745), // Rule ID 125 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule85Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner64),
      // Combiner Rule #85: adde_to_addo; wip_match_opcode 'G_UADDE'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 238: @3745
    GIM_Reject,
    // Label 42: @3746
    GIM_Try, /*On fail goto*//*Label 239*/ GIMT_Encode4(3762), // Rule ID 127 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule85Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner64),
      // Combiner Rule #85: adde_to_addo; wip_match_opcode 'G_USUBE'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 239: @3762
    GIM_Reject,
    // Label 43: @3763
    GIM_Try, /*On fail goto*//*Label 240*/ GIMT_Encode4(3779), // Rule ID 124 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule84Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner63),
      // Combiner Rule #84: addo_by_0; wip_match_opcode 'G_SADDO'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 240: @3779
    GIM_Reject,
    // Label 44: @3780
    GIM_Try, /*On fail goto*//*Label 241*/ GIMT_Encode4(3796), // Rule ID 126 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule85Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner64),
      // Combiner Rule #85: adde_to_addo; wip_match_opcode 'G_SADDE'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 241: @3796
    GIM_Reject,
    // Label 45: @3797
    GIM_Try, /*On fail goto*//*Label 242*/ GIMT_Encode4(3813), // Rule ID 128 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule85Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner64),
      // Combiner Rule #85: adde_to_addo; wip_match_opcode 'G_SSUBE'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 242: @3813
    GIM_Reject,
    // Label 46: @3814
    GIM_Try, /*On fail goto*//*Label 243*/ GIMT_Encode4(3830), // Rule ID 119 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule82Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner61),
      // Combiner Rule #82: mulo_by_2; wip_match_opcode 'G_UMULO'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 243: @3830
    GIM_Try, /*On fail goto*//*Label 244*/ GIMT_Encode4(3846), // Rule ID 121 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule83Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner62),
      // Combiner Rule #83: mulo_by_0; wip_match_opcode 'G_UMULO'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 244: @3846
    GIM_Reject,
    // Label 47: @3847
    GIM_Try, /*On fail goto*//*Label 245*/ GIMT_Encode4(3863), // Rule ID 120 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule82Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner61),
      // Combiner Rule #82: mulo_by_2; wip_match_opcode 'G_SMULO'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 245: @3863
    GIM_Try, /*On fail goto*//*Label 246*/ GIMT_Encode4(3879), // Rule ID 122 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule83Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner62),
      // Combiner Rule #83: mulo_by_0; wip_match_opcode 'G_SMULO'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 246: @3879
    GIM_Reject,
    // Label 48: @3880
    GIM_Try, /*On fail goto*//*Label 247*/ GIMT_Encode4(3896), // Rule ID 185 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule110Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner86),
      // Combiner Rule #110: mulh_to_lshr; wip_match_opcode 'G_UMULH'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner54),
      GIR_Done,
    // Label 247: @3896
    GIM_Reject,
    // Label 49: @3897
    GIM_Try, /*On fail goto*//*Label 248*/ GIMT_Encode4(3913), // Rule ID 139 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule89Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner68),
      // Combiner Rule #89: shift_immed_chain; wip_match_opcode 'G_USHLSAT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner45),
      GIR_Done,
    // Label 248: @3913
    GIM_Try, /*On fail goto*//*Label 249*/ GIMT_Encode4(3929), // Rule ID 143 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule90Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner69),
      // Combiner Rule #90: shift_of_shifted_logic_chain; wip_match_opcode 'G_USHLSAT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner46),
      GIR_Done,
    // Label 249: @3929
    GIM_Reject,
    // Label 50: @3930
    GIM_Try, /*On fail goto*//*Label 250*/ GIMT_Encode4(3946), // Rule ID 138 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule89Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner68),
      // Combiner Rule #89: shift_immed_chain; wip_match_opcode 'G_SSHLSAT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner45),
      GIR_Done,
    // Label 250: @3946
    GIM_Try, /*On fail goto*//*Label 251*/ GIMT_Encode4(3962), // Rule ID 144 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule90Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner69),
      // Combiner Rule #90: shift_of_shifted_logic_chain; wip_match_opcode 'G_SSHLSAT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner46),
      GIR_Done,
    // Label 251: @3962
    GIM_Reject,
    // Label 51: @3963
    GIM_Try, /*On fail goto*//*Label 252*/ GIMT_Encode4(3979), // Rule ID 173 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule104Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner81),
      // Combiner Rule #104: constant_fold_fp_binop; wip_match_opcode 'G_FADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner51),
      GIR_Done,
    // Label 252: @3979
    GIM_Try, /*On fail goto*//*Label 253*/ GIMT_Encode4(3995), // Rule ID 186 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule111Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner87),
      // Combiner Rule #111: redundant_neg_operands; wip_match_opcode 'G_FADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 253: @3995
    GIM_Try, /*On fail goto*//*Label 254*/ GIMT_Encode4(4011), // Rule ID 193 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule113Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner89),
      // Combiner Rule #113: combine_fadd_fmul_to_fmad_or_fma; wip_match_opcode 'G_FADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 254: @4011
    GIM_Try, /*On fail goto*//*Label 255*/ GIMT_Encode4(4027), // Rule ID 194 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule114Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner90),
      // Combiner Rule #114: combine_fadd_fpext_fmul_to_fmad_or_fma; wip_match_opcode 'G_FADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 255: @4027
    GIM_Try, /*On fail goto*//*Label 256*/ GIMT_Encode4(4043), // Rule ID 195 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule115Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner91),
      // Combiner Rule #115: combine_fadd_fma_fmul_to_fmad_or_fma; wip_match_opcode 'G_FADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 256: @4043
    GIM_Try, /*On fail goto*//*Label 257*/ GIMT_Encode4(4059), // Rule ID 196 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule116Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner92),
      // Combiner Rule #116: combine_fadd_fpext_fma_fmul_to_fmad_or_fma; wip_match_opcode 'G_FADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 257: @4059
    GIM_Try, /*On fail goto*//*Label 258*/ GIMT_Encode4(4075), // Rule ID 219 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_FADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 258: @4075
    GIM_Try, /*On fail goto*//*Label 259*/ GIMT_Encode4(4091), // Rule ID 236 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule127Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner103),
      // Combiner Rule #127: commute_fp_constant_to_rhs; wip_match_opcode 'G_FADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner57),
      GIR_Done,
    // Label 259: @4091
    GIM_Try, /*On fail goto*//*Label 260*/ GIMT_Encode4(4116), // Rule ID 71 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule40Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] x
      // No operand predicates
      // MIs[0] y
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner27),
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #40: right_identity_neg_zero_fp
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 260: @4116
    GIM_Reject,
    // Label 52: @4117
    GIM_Try, /*On fail goto*//*Label 261*/ GIMT_Encode4(4133), // Rule ID 174 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule104Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner81),
      // Combiner Rule #104: constant_fold_fp_binop; wip_match_opcode 'G_FSUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner51),
      GIR_Done,
    // Label 261: @4133
    GIM_Try, /*On fail goto*//*Label 262*/ GIMT_Encode4(4149), // Rule ID 187 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule111Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner87),
      // Combiner Rule #111: redundant_neg_operands; wip_match_opcode 'G_FSUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 262: @4149
    GIM_Try, /*On fail goto*//*Label 263*/ GIMT_Encode4(4165), // Rule ID 197 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule117Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner93),
      // Combiner Rule #117: combine_fsub_fmul_to_fmad_or_fma; wip_match_opcode 'G_FSUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 263: @4165
    GIM_Try, /*On fail goto*//*Label 264*/ GIMT_Encode4(4181), // Rule ID 198 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule118Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner94),
      // Combiner Rule #118: combine_fsub_fneg_fmul_to_fmad_or_fma; wip_match_opcode 'G_FSUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 264: @4181
    GIM_Try, /*On fail goto*//*Label 265*/ GIMT_Encode4(4197), // Rule ID 199 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule119Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner95),
      // Combiner Rule #119: combine_fsub_fpext_fmul_to_fmad_or_fma; wip_match_opcode 'G_FSUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 265: @4197
    GIM_Try, /*On fail goto*//*Label 266*/ GIMT_Encode4(4213), // Rule ID 200 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule120Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner96),
      // Combiner Rule #120: combine_fsub_fpext_fneg_fmul_to_fmad_or_fma; wip_match_opcode 'G_FSUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 266: @4213
    GIM_Try, /*On fail goto*//*Label 267*/ GIMT_Encode4(4229), // Rule ID 220 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_FSUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 267: @4229
    GIM_Try, /*On fail goto*//*Label 268*/ GIMT_Encode4(4245), // Rule ID 230 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule125Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner101),
      // Combiner Rule #125: fsub_to_fneg; wip_match_opcode 'G_FSUB'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner56),
      GIR_Done,
    // Label 268: @4245
    GIM_Reject,
    // Label 53: @4246
    GIM_Try, /*On fail goto*//*Label 269*/ GIMT_Encode4(4262), // Rule ID 175 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule104Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner81),
      // Combiner Rule #104: constant_fold_fp_binop; wip_match_opcode 'G_FMUL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner51),
      GIR_Done,
    // Label 269: @4262
    GIM_Try, /*On fail goto*//*Label 270*/ GIMT_Encode4(4278), // Rule ID 188 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule111Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner87),
      // Combiner Rule #111: redundant_neg_operands; wip_match_opcode 'G_FMUL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 270: @4278
    GIM_Try, /*On fail goto*//*Label 271*/ GIMT_Encode4(4294), // Rule ID 218 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_FMUL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 271: @4294
    GIM_Try, /*On fail goto*//*Label 272*/ GIMT_Encode4(4310), // Rule ID 237 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule127Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner103),
      // Combiner Rule #127: commute_fp_constant_to_rhs; wip_match_opcode 'G_FMUL'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner57),
      GIR_Done,
    // Label 272: @4310
    GIM_Try, /*On fail goto*//*Label 273*/ GIMT_Encode4(4335), // Rule ID 62 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule33Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] x
      // No operand predicates
      // MIs[0] y
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner22),
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #33: right_identity_one_fp
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 273: @4335
    GIM_Reject,
    // Label 54: @4336
    GIM_Try, /*On fail goto*//*Label 274*/ GIMT_Encode4(4352), // Rule ID 178 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule105Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner82),
      // Combiner Rule #105: constant_fold_fma; wip_match_opcode 'G_FMA'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner51),
      GIR_Done,
    // Label 274: @4352
    GIM_Try, /*On fail goto*//*Label 275*/ GIMT_Encode4(4368), // Rule ID 191 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule111Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner87),
      // Combiner Rule #111: redundant_neg_operands; wip_match_opcode 'G_FMA'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 275: @4368
    GIM_Reject,
    // Label 55: @4369
    GIM_Try, /*On fail goto*//*Label 276*/ GIMT_Encode4(4385), // Rule ID 177 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule105Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner82),
      // Combiner Rule #105: constant_fold_fma; wip_match_opcode 'G_FMAD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner51),
      GIR_Done,
    // Label 276: @4385
    GIM_Try, /*On fail goto*//*Label 277*/ GIMT_Encode4(4401), // Rule ID 190 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule111Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner87),
      // Combiner Rule #111: redundant_neg_operands; wip_match_opcode 'G_FMAD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 277: @4401
    GIM_Reject,
    // Label 56: @4402
    GIM_Try, /*On fail goto*//*Label 278*/ GIMT_Encode4(4418), // Rule ID 176 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule104Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner81),
      // Combiner Rule #104: constant_fold_fp_binop; wip_match_opcode 'G_FDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner51),
      GIR_Done,
    // Label 278: @4418
    GIM_Try, /*On fail goto*//*Label 279*/ GIMT_Encode4(4434), // Rule ID 189 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule111Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner87),
      // Combiner Rule #111: redundant_neg_operands; wip_match_opcode 'G_FDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 279: @4434
    GIM_Try, /*On fail goto*//*Label 280*/ GIMT_Encode4(4450), // Rule ID 221 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_FDIV'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 280: @4450
    GIM_Reject,
    // Label 57: @4451
    GIM_Try, /*On fail goto*//*Label 281*/ GIMT_Encode4(4467), // Rule ID 222 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_FREM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 281: @4467
    GIM_Reject,
    // Label 58: @4468
    GIM_Try, /*On fail goto*//*Label 282*/ GIMT_Encode4(4490), // Rule ID 113 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule76Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src0
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FCONSTANT),
      // MIs[1] cst
      // No operand predicates
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #76: constant_fold_flog2
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner40),
      GIR_Done,
    // Label 282: @4490
    GIM_Reject,
    // Label 59: @4491
    GIM_Try, /*On fail goto*//*Label 283*/ GIMT_Encode4(4513), // Rule ID 110 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule73Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src0
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FCONSTANT),
      // MIs[1] cst
      // No operand predicates
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #73: constant_fold_fneg
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner40),
      GIR_Done,
    // Label 283: @4513
    GIM_Try, /*On fail goto*//*Label 284*/ GIMT_Encode4(4544), // Rule ID 60 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule31Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] t
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FNEG),
      // MIs[1] src
      // No operand predicates
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/1, /*NewOpIdx*/1,
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #31: fneg_fneg_fold
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/1, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 284: @4544
    GIM_Reject,
    // Label 60: @4545
    GIM_Try, /*On fail goto*//*Label 285*/ GIMT_Encode4(4567), // Rule ID 114 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule77Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src0
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FCONSTANT),
      // MIs[1] cst
      // No operand predicates
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #77: constant_fold_fptrunc
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner40),
      GIR_Done,
    // Label 285: @4567
    GIM_Try, /*On fail goto*//*Label 286*/ GIMT_Encode4(4593), // Rule ID 70 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule39Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src1
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FPEXT),
      // MIs[1] src0
      // No operand predicates
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner26),
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #39: fptrunc_fpext_fold
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner18),
      GIR_Done,
    // Label 286: @4593
    GIM_Reject,
    // Label 61: @4594
    GIM_Try, /*On fail goto*//*Label 287*/ GIMT_Encode4(4610), // Rule ID 28 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule17Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #17: propagate_undef_any_op; wip_match_opcode 'G_FPTOSI'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 287: @4610
    GIM_Reject,
    // Label 62: @4611
    GIM_Try, /*On fail goto*//*Label 288*/ GIMT_Encode4(4627), // Rule ID 29 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule17Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #17: propagate_undef_any_op; wip_match_opcode 'G_FPTOUI'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 288: @4627
    GIM_Reject,
    // Label 63: @4628
    GIM_Try, /*On fail goto*//*Label 289*/ GIMT_Encode4(4644), // Rule ID 16 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule11Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #11: undef_to_fp_zero; wip_match_opcode 'G_SITOFP'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner8),
      GIR_Done,
    // Label 289: @4644
    GIM_Try, /*On fail goto*//*Label 290*/ GIMT_Encode4(4660), // Rule ID 115 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule78Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] Operand 1
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/1, 0,
      // Combiner Rule #78: itof_const_zero_fold_si
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner41),
      GIR_Done,
    // Label 290: @4660
    GIM_Reject,
    // Label 64: @4661
    GIM_Try, /*On fail goto*//*Label 291*/ GIMT_Encode4(4677), // Rule ID 15 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule11Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner9),
      // Combiner Rule #11: undef_to_fp_zero; wip_match_opcode 'G_UITOFP'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner8),
      GIR_Done,
    // Label 291: @4677
    GIM_Try, /*On fail goto*//*Label 292*/ GIMT_Encode4(4693), // Rule ID 116 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule79Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] Operand 1
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/1, 0,
      // Combiner Rule #79: itof_const_zero_fold_ui
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner41),
      GIR_Done,
    // Label 292: @4693
    GIM_Reject,
    // Label 65: @4694
    GIM_Try, /*On fail goto*//*Label 293*/ GIMT_Encode4(4725), // Rule ID 5 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule4Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FABS),
      // MIs[1] __idempotent_prop_match_0.x
      // No operand predicates
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #4: idempotent_prop @ [__idempotent_prop_match_0[1]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 293: @4725
    GIM_Try, /*On fail goto*//*Label 294*/ GIMT_Encode4(4747), // Rule ID 111 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule74Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src0
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FCONSTANT),
      // MIs[1] cst
      // No operand predicates
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #74: constant_fold_fabs
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner40),
      GIR_Done,
    // Label 294: @4747
    GIM_Try, /*On fail goto*//*Label 295*/ GIMT_Encode4(4780), // Rule ID 182 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule107Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] tmp
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FNEG),
      // MIs[1] x
      // No operand predicates
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #107: fabs_fneg_fold
      GIR_BuildMI, /*InsnID*/0, /*Opcode*/GIMT_Encode2(TargetOpcode::G_FABS),
      GIR_Copy, /*NewInsnID*/0, /*OldInsnID*/0, /*OpIdx*/0, // dst
      GIR_Copy, /*NewInsnID*/0, /*OldInsnID*/1, /*OpIdx*/1, // x
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 295: @4780
    GIM_Reject,
    // Label 66: @4781
    GIM_Try, /*On fail goto*//*Label 296*/ GIMT_Encode4(4812), // Rule ID 6 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule4Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FCANONICALIZE),
      // MIs[1] __idempotent_prop_match_0.x
      // No operand predicates
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #4: idempotent_prop @ [__idempotent_prop_match_0[2]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 296: @4812
    GIM_Reject,
    // Label 67: @4813
    GIM_Try, /*On fail goto*//*Label 297*/ GIMT_Encode4(4829), // Rule ID 129 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule86Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner65),
      // Combiner Rule #86: combine_minmax_nan; wip_match_opcode 'G_FMINNUM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 297: @4829
    GIM_Try, /*On fail goto*//*Label 298*/ GIMT_Encode4(4845), // Rule ID 223 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_FMINNUM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 298: @4845
    GIM_Reject,
    // Label 68: @4846
    GIM_Try, /*On fail goto*//*Label 299*/ GIMT_Encode4(4862), // Rule ID 130 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule86Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner65),
      // Combiner Rule #86: combine_minmax_nan; wip_match_opcode 'G_FMAXNUM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 299: @4862
    GIM_Try, /*On fail goto*//*Label 300*/ GIMT_Encode4(4878), // Rule ID 224 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_FMAXNUM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 300: @4878
    GIM_Reject,
    // Label 69: @4879
    GIM_Try, /*On fail goto*//*Label 301*/ GIMT_Encode4(4895), // Rule ID 131 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule86Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner65),
      // Combiner Rule #86: combine_minmax_nan; wip_match_opcode 'G_FMINIMUM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 301: @4895
    GIM_Try, /*On fail goto*//*Label 302*/ GIMT_Encode4(4911), // Rule ID 225 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_FMINIMUM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 302: @4911
    GIM_Reject,
    // Label 70: @4912
    GIM_Try, /*On fail goto*//*Label 303*/ GIMT_Encode4(4928), // Rule ID 132 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule86Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner65),
      // Combiner Rule #86: combine_minmax_nan; wip_match_opcode 'G_FMAXIMUM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner27),
      GIR_Done,
    // Label 303: @4928
    GIM_Try, /*On fail goto*//*Label 304*/ GIMT_Encode4(4944), // Rule ID 226 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_FMAXIMUM'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 304: @4944
    GIM_Reject,
    // Label 71: @4945
    GIM_Try, /*On fail goto*//*Label 305*/ GIMT_Encode4(4961), // Rule ID 80 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule45Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner32),
      // Combiner Rule #45: reassoc_ptradd; wip_match_opcode 'G_PTR_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner22),
      GIR_Done,
    // Label 305: @4961
    GIM_Try, /*On fail goto*//*Label 306*/ GIMT_Encode4(4977), // Rule ID 82 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule47Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner34),
      // Combiner Rule #47: ptr_add_immed_chain; wip_match_opcode 'G_PTR_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner23),
      GIR_Done,
    // Label 306: @4977
    GIM_Try, /*On fail goto*//*Label 307*/ GIMT_Encode4(4993), // Rule ID 117 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule80Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner59),
      // Combiner Rule #80: const_ptradd_to_i2p; wip_match_opcode 'G_PTR_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner42),
      GIR_Done,
    // Label 307: @4993
    GIM_Try, /*On fail goto*//*Label 308*/ GIMT_Encode4(5009), // Rule ID 134 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule88Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner67),
      // Combiner Rule #88: ptr_add_with_zero; wip_match_opcode 'G_PTR_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner44),
      GIR_Done,
    // Label 308: @5009
    GIM_Try, /*On fail goto*//*Label 309*/ GIMT_Encode4(5025), // Rule ID 203 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_PTR_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 309: @5025
    GIM_Try, /*On fail goto*//*Label 310*/ GIMT_Encode4(5050), // Rule ID 47 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule24Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] lhs
      // No operand predicates
      // MIs[0] Operand 2
      GIM_CheckConstantInt8, /*MI*/0, /*Op*/2, 0,
      GIM_CheckCanReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      // Combiner Rule #24: right_identity_zero @ [__right_identity_zero_match_0[7]]
      GIR_ReplaceReg, /*OldInsnID*/0, /*OldOpIdx*/0, /*NewInsnId*/0, /*NewOpIdx*/1,
      GIR_EraseFromParent, /*InsnID*/0,
      GIR_Done,
    // Label 310: @5050
    GIM_Reject,
    // Label 72: @5051
    GIM_Try, /*On fail goto*//*Label 311*/ GIMT_Encode4(5067), // Rule ID 214 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_SMIN'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 311: @5067
    GIM_Reject,
    // Label 73: @5068
    GIM_Try, /*On fail goto*//*Label 312*/ GIMT_Encode4(5084), // Rule ID 215 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_SMAX'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 312: @5084
    GIM_Reject,
    // Label 74: @5085
    GIM_Try, /*On fail goto*//*Label 313*/ GIMT_Encode4(5101), // Rule ID 216 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_UMIN'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 313: @5101
    GIM_Reject,
    // Label 75: @5102
    GIM_Try, /*On fail goto*//*Label 314*/ GIMT_Encode4(5118), // Rule ID 217 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule121Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner97),
      // Combiner Rule #121: fold_binop_into_select; wip_match_opcode 'G_UMAX'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner55),
      GIR_Done,
    // Label 314: @5118
    GIM_Reject,
    // Label 76: @5119
    GIM_Try, /*On fail goto*//*Label 315*/ GIMT_Encode4(5135), // Rule ID 26 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule16Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner10),
      // Combiner Rule #16: unary_undef_to_zero; wip_match_opcode 'G_ABS'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner9),
      GIR_Done,
    // Label 315: @5135
    GIM_Reject,
    // Label 77: @5136
    GIM_Try, /*On fail goto*//*Label 316*/ GIMT_Encode4(5152), // Rule ID 102 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule65Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner51),
      // Combiner Rule #65: opt_brcond_by_inverting_cond; wip_match_opcode 'G_BR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner33),
      GIR_Done,
    // Label 316: @5152
    GIM_Reject,
    // Label 78: @5153
    GIM_Try, /*On fail goto*//*Label 317*/ GIMT_Encode4(5169), // Rule ID 7 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule5Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner3),
      // Combiner Rule #5: combine_insert_vec_elts_build_vector; wip_match_opcode 'G_INSERT_VECTOR_ELT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner3),
      GIR_Done,
    // Label 317: @5169
    GIM_Try, /*On fail goto*//*Label 318*/ GIMT_Encode4(5185), // Rule ID 37 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule22Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner16),
      // Combiner Rule #22: insert_extract_vec_elt_out_of_bounds; wip_match_opcode 'G_INSERT_VECTOR_ELT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 318: @5185
    GIM_Reject,
    // Label 79: @5186
    GIM_Try, /*On fail goto*//*Label 319*/ GIMT_Encode4(5202), // Rule ID 8 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule6Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner4),
      // Combiner Rule #6: extract_vec_elt_build_vec; wip_match_opcode 'G_EXTRACT_VECTOR_ELT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner4),
      GIR_Done,
    // Label 319: @5202
    GIM_Try, /*On fail goto*//*Label 320*/ GIMT_Encode4(5218), // Rule ID 14 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule10Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner8),
      // Combiner Rule #10: combine_extracted_vector_load; wip_match_opcode 'G_EXTRACT_VECTOR_ELT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner7),
      GIR_Done,
    // Label 320: @5218
    GIM_Try, /*On fail goto*//*Label 321*/ GIMT_Encode4(5234), // Rule ID 38 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule22Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner16),
      // Combiner Rule #22: insert_extract_vec_elt_out_of_bounds; wip_match_opcode 'G_EXTRACT_VECTOR_ELT'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 321: @5234
    GIM_Reject,
    // Label 80: @5235
    GIM_Try, /*On fail goto*//*Label 322*/ GIMT_Encode4(5251), // Rule ID 33 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule18Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner12),
      // Combiner Rule #18: propagate_undef_all_ops; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 322: @5251
    GIM_Try, /*On fail goto*//*Label 323*/ GIMT_Encode4(5267), // Rule ID 34 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule19Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner13),
      // Combiner Rule #19: propagate_undef_shuffle_mask; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner11),
      GIR_Done,
    // Label 323: @5267
    GIM_Try, /*On fail goto*//*Label 324*/ GIMT_Encode4(5283), // Rule ID 241 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule131Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner107),
      // Combiner Rule #131: shuffle_to_extract; wip_match_opcode 'G_SHUFFLE_VECTOR'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner61),
      GIR_Done,
    // Label 324: @5283
    GIM_Reject,
    // Label 81: @5284
    GIM_Try, /*On fail goto*//*Label 325*/ GIMT_Encode4(5306), // Rule ID 112 //
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule75Enabled),
      // MIs[0] dst
      // No operand predicates
      // MIs[0] src0
      GIM_RecordInsnIgnoreCopies, /*DefineMI*/1, /*MI*/0, /*OpIdx*/1, // MIs[1]
      GIM_CheckOpcode, /*MI*/1, GIMT_Encode2(TargetOpcode::G_FCONSTANT),
      // MIs[1] cst
      // No operand predicates
      GIM_CheckIsSafeToFold, /*InsnID*/1,
      // Combiner Rule #75: constant_fold_fsqrt
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner40),
      GIR_Done,
    // Label 325: @5306
    GIM_Reject,
    // Label 82: @5307
    GIM_Try, /*On fail goto*//*Label 326*/ GIMT_Encode4(5326), // Rule ID 242 //
      GIM_CheckFeatures, GIMT_Encode2(GIFBS_HasDotProd),
      GIM_CheckSimplePredicate, GIMT_Encode2(GICXXPred_Simple_IsRule132Enabled),
      GIM_CheckCxxInsnPredicate, /*MI*/0, /*FnId*/GIMT_Encode2(GICXXPred_MI_Predicate_GICombiner108),
      // Combiner Rule #132: ext_addv_to_udot_addv; wip_match_opcode 'G_VECREDUCE_ADD'
      GIR_CustomAction, GIMT_Encode2(GICXXCustomAction_CombineApplyGICombiner62),
      GIR_Done,
    // Label 326: @5326
    GIM_Reject,
    // Label 83: @5327
    GIM_Reject,
    }; // Size: 5328 bytes
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

