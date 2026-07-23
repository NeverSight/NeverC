#ifndef NEVERC_LIB_DYNCODE_BINARY_BUILTINDYNCODEBINARYVERIFIER_H
#define NEVERC_LIB_DYNCODE_BINARY_BUILTINDYNCODEBINARYVERIFIER_H

// The builtin structural dyncode binary verifier (x86_64 / AArch64).
//
// This is the sealed final check that runs after every byte-level transform:
// it confirms the flat PIC image is non-empty, keeps the entry at output offset
// 0, stays within the size/alignment budget, and contains no forbidden byte
// anywhere (payload, decoder stub and padding).  It is deliberately structural
// -- it does not disassemble the payload; a plugin that performs a raw
// executable rewrite must supply a matching target binary verifier capability
// rather than relying on this host check to bless arbitrary bytes.

#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <optional>

namespace neverc {
namespace dyncode {

/// Returns the first offset holding a forbidden byte, or nullopt if the byte
/// range is clean.
std::optional<uint64_t>
firstDynCodeBadByte(llvm::ArrayRef<uint8_t> Bytes,
                    llvm::ArrayRef<uint8_t> BadBytes);

/// Structural PIC image verifier.  Fails (structured error) on an empty image,
/// an entry not at offset 0 / out of range, a size over the max length, a size
/// not aligned to the requested alignment, or any forbidden byte in the final
/// bytes.
llvm::Error verifyDynCodeBinary(const DynCodeImage &Image,
                                const DynCodeOptions &Opts);

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_LIB_DYNCODE_BINARY_BUILTINDYNCODEBINARYVERIFIER_H
