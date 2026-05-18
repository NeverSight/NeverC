//===- TwineFormatv.h - Twine ↔ formatv bridge declarations --------*- C++
//-*-===//
//
// Twine cannot include FormatVariadic.h (include cycle via ArrayRef → Hashing →
// ErrorHandling → Twine). Implementations live in FormatVariadic.h after
// formatv_object_base is complete.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_TWINEFORMATV_H
#define LLVM_ADT_TWINEFORMATV_H

#include "llvm/ADT/StringRef.h"

#include <string>

namespace llvm {

class formatv_object_base;
class raw_ostream;

std::string twine_str_from_formatv(const formatv_object_base *O);
void twine_print_formatv_to_stream(raw_ostream &OS,
                                   const formatv_object_base *O);
void twine_print_formatv_repr_to_stream(raw_ostream &OS,
                                        const formatv_object_base *O);

} // namespace llvm

#endif
