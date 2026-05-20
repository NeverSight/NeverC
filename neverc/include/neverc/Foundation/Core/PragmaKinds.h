#ifndef NEVERC_FOUNDATION_PRAGMAKINDS_H
#define NEVERC_FOUNDATION_PRAGMAKINDS_H

namespace neverc {

enum PragmaMSCommentKind {
  PCK_Unknown,
  PCK_Linker,   // #pragma comment(linker, ...)
  PCK_Lib,      // #pragma comment(lib, ...)
  PCK_Compiler, // #pragma comment(compiler, ...)
  PCK_ExeStr,   // #pragma comment(exestr, ...)
  PCK_User      // #pragma comment(user, ...)
};

enum PragmaMSStructKind {
  PMSST_OFF, // #pragms ms_struct off
  PMSST_ON   // #pragms ms_struct on
};

enum PragmaFloatControlKind {
  PFC_Unknown,
  PFC_Precise,   // #pragma float_control(precise, [,on])
  PFC_NoPrecise, // #pragma float_control(precise, off)
  PFC_Except,    // #pragma float_control(except [,on])
  PFC_NoExcept,  // #pragma float_control(except, off)
  PFC_Push,      // #pragma float_control(push)
  PFC_Pop        // #pragma float_control(pop)
};

enum PragmaFPKind {
  PFK_Contract,    // #pragma neverc fp contract
  PFK_Reassociate, // #pragma neverc fp reassociate
  PFK_Reciprocal,  // #pragma neverc fp reciprocal
  PFK_Exceptions,  // #pragma neverc fp exceptions
  PFK_EvalMethod   // #pragma neverc fp eval_method
};
} // namespace neverc

#endif
