//===- BinaryStreamError.h - Error extensions for Binary Streams *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BINARYSTREAMERROR_H
#define LLVM_SUPPORT_BINARYSTREAMERROR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <string>

namespace llvm {
enum class stream_error_code {
  unspecified,
  stream_too_short,
  invalid_array_size,
  invalid_offset,
  filesystem_error
};

/// Base class for errors originating when parsing raw PDB files
class BinaryStreamError : public ErrorInfo<BinaryStreamError> {
public:
  inline static char ID = 0;

  explicit inline BinaryStreamError(stream_error_code C)
      : BinaryStreamError(C, "") {}

  explicit inline BinaryStreamError(StringRef Context)
      : BinaryStreamError(stream_error_code::unspecified, Context) {}

  inline BinaryStreamError(stream_error_code C, StringRef Context) : Code(C) {
    ErrMsg = "Stream Error: ";
    switch (C) {
    case stream_error_code::unspecified:
      ErrMsg += "An unspecified error has occurred.";
      break;
    case stream_error_code::stream_too_short:
      ErrMsg += "The stream is too short to perform the requested operation.";
      break;
    case stream_error_code::invalid_array_size:
      ErrMsg += "The buffer size is not a multiple of the array element size.";
      break;
    case stream_error_code::invalid_offset:
      ErrMsg += "The specified offset is invalid for the current stream.";
      break;
    case stream_error_code::filesystem_error:
      ErrMsg += "An I/O error occurred on the file system.";
      break;
    }
    if (!Context.empty()) {
      ErrMsg += "  ";
      ErrMsg += Context;
    }
  }

  inline void log(raw_ostream &OS) const override { OS << ErrMsg; }
  inline int convertToErrorCode() const override {
    return inconvertibleErrorCode().value();
  }

  inline StringRef getErrorMessage() const { return ErrMsg; }

  stream_error_code getErrorCode() const { return Code; }

private:
  std::string ErrMsg;
  stream_error_code Code;
};
} // namespace llvm

#endif // LLVM_SUPPORT_BINARYSTREAMERROR_H
