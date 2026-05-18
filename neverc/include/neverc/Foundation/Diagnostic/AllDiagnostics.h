#ifndef NEVERC_BASIC_ALLDIAGNOSTICS_H
#define NEVERC_BASIC_ALLDIAGNOSTICS_H

#include "neverc/Foundation/Diagnostic/DiagnosticAST.h"
#include "neverc/Foundation/Diagnostic/DiagnosticDriver.h"
#include "neverc/Foundation/Diagnostic/DiagnosticFrontend.h"
#include "neverc/Foundation/Diagnostic/DiagnosticLex.h"
#include "neverc/Foundation/Diagnostic/DiagnosticParse.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"

namespace neverc {
template <size_t SizeOfStr, typename FieldType> class StringSizerHelper {
  static_assert(SizeOfStr <= FieldType(~0U), "Field too small!");

public:
  enum { Size = SizeOfStr };
};
} // end namespace neverc

#define STR_SIZE(str, fieldTy)                                                 \
  neverc::StringSizerHelper<sizeof(str) - 1, fieldTy>::Size

#endif
