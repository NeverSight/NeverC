//===-- llvm/Support/Compression.h ---Compression----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains basic functions for compression/decompression.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COMPRESSION_H
#define LLVM_SUPPORT_COMPRESSION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Error.h"

namespace llvm {
template <typename T> class SmallVectorImpl;

// None indicates no compression. The other members are a subset of
// compression::Format, which is used for compressed debug sections in some
// object file formats (e.g. ELF). This is a separate class as we may add new
// compression::Format members for non-debugging purposes.
enum class DebugCompressionType {
  None, ///< No compression
  Zlib, ///< zlib
  Zstd, ///< Zstandard
};

namespace compression {
namespace zlib {

constexpr int NoCompression = 0;
constexpr int BestSpeedCompression = 1;
constexpr int DefaultCompression = 6;
constexpr int BestSizeCompression = 9;

bool isAvailable();

void compress(ArrayRef<uint8_t> Input,
              SmallVectorImpl<uint8_t> &CompressedBuffer,
              int Level = DefaultCompression);

Error decompress(ArrayRef<uint8_t> Input, uint8_t *Output,
                 size_t &UncompressedSize);

Error decompress(ArrayRef<uint8_t> Input, SmallVectorImpl<uint8_t> &Output,
                 size_t UncompressedSize);

} // End of namespace zlib

namespace zstd {

constexpr int NoCompression = -5;
constexpr int BestSpeedCompression = 1;
constexpr int DefaultCompression = 5;
constexpr int BestSizeCompression = 12;

bool isAvailable();

void compress(ArrayRef<uint8_t> Input,
              SmallVectorImpl<uint8_t> &CompressedBuffer,
              int Level = DefaultCompression);

Error decompress(ArrayRef<uint8_t> Input, uint8_t *Output,
                 size_t &UncompressedSize);

Error decompress(ArrayRef<uint8_t> Input, SmallVectorImpl<uint8_t> &Output,
                 size_t UncompressedSize);

} // End of namespace zstd

enum class Format {
  Zlib,
  Zstd,
};

inline Format formatFor(DebugCompressionType Type) {
  switch (Type) {
  case DebugCompressionType::None:
    llvm_unreachable("not a compression type");
  case DebugCompressionType::Zlib:
    return Format::Zlib;
  case DebugCompressionType::Zstd:
    return Format::Zstd;
  }
  llvm_unreachable("");
}

struct Params {
  constexpr Params(Format F)
      : format(F), level(F == Format::Zlib ? zlib::DefaultCompression
                                           : zstd::DefaultCompression) {}
  Params(DebugCompressionType Type) : Params(formatFor(Type)) {}

  Format format;
  int level;
  // This may support multi-threading for zstd in the future. Note that
  // different threads may produce different output, so be careful if certain
  // output determinism is desired.
};

// Return nullptr if LLVM was built with support (LLVM_ENABLE_ZLIB,
// LLVM_ENABLE_ZSTD) for the specified compression format; otherwise
// return a string literal describing the reason.
const char *getReasonIfUnsupported(Format F);

// Compress Input with the specified format P.Format. If Level is -1, use
// *::DefaultCompression for the format.
void compress(Params P, ArrayRef<uint8_t> Input,
              SmallVectorImpl<uint8_t> &Output);

// Decompress Input. The uncompressed size must be available.
Error decompress(DebugCompressionType T, ArrayRef<uint8_t> Input,
                 uint8_t *Output, size_t UncompressedSize);
Error decompress(Format F, ArrayRef<uint8_t> Input,
                 SmallVectorImpl<uint8_t> &Output, size_t UncompressedSize);
Error decompress(DebugCompressionType T, ArrayRef<uint8_t> Input,
                 SmallVectorImpl<uint8_t> &Output, size_t UncompressedSize);

} // End of namespace compression

} // End of namespace llvm

/*== Inline implementations (moved from cpp_bridge.cpp) ==*/

#if LLVM_ENABLE_ZLIB
#include "csupport/lcompression.h"
#include <zlib.h>
#endif
#if LLVM_ENABLE_ZSTD
#include <zstd.h>
#endif

namespace llvm {
namespace compression {

inline const char *getReasonIfUnsupported(compression::Format F) {
  switch (F) {
  case compression::Format::Zlib:
    if (zlib::isAvailable())
      return 0;
    return "LLVM was not built with LLVM_ENABLE_ZLIB or did not find zlib at "
           "build time";
  case compression::Format::Zstd:
    if (zstd::isAvailable())
      return 0;
    return "LLVM was not built with LLVM_ENABLE_ZSTD or did not find zstd at "
           "build time";
  }
  llvm_unreachable("");
}

inline void compress(Params P, ArrayRef<uint8_t> Input,
                     SmallVectorImpl<uint8_t> &Output) {
  switch (P.format) {
  case compression::Format::Zlib:
    zlib::compress(Input, Output, P.level);
    break;
  case compression::Format::Zstd:
    zstd::compress(Input, Output, P.level);
    break;
  }
}

inline Error decompress(DebugCompressionType T, ArrayRef<uint8_t> Input,
                        uint8_t *Output, size_t UncompressedSize) {
  switch (formatFor(T)) {
  case compression::Format::Zlib:
    return zlib::decompress(Input, Output, UncompressedSize);
  case compression::Format::Zstd:
    return zstd::decompress(Input, Output, UncompressedSize);
  }
  llvm_unreachable("");
}

inline Error decompress(compression::Format F, ArrayRef<uint8_t> Input,
                        SmallVectorImpl<uint8_t> &Output,
                        size_t UncompressedSize) {
  switch (F) {
  case compression::Format::Zlib:
    return zlib::decompress(Input, Output, UncompressedSize);
  case compression::Format::Zstd:
    return zstd::decompress(Input, Output, UncompressedSize);
  }
  llvm_unreachable("");
}

inline Error decompress(DebugCompressionType T, ArrayRef<uint8_t> Input,
                        SmallVectorImpl<uint8_t> &Output,
                        size_t UncompressedSize) {
  return decompress(formatFor(T), Input, Output, UncompressedSize);
}

#if LLVM_ENABLE_ZLIB

namespace zlib {
inline bool isAvailable() { return true; }

inline void compress(ArrayRef<uint8_t> Input,
                     SmallVectorImpl<uint8_t> &CompressedBuffer, int Level) {
  unsigned long CompressedSize = ::compressBound(Input.size());
  CompressedBuffer.resize_for_overwrite(CompressedSize);
  int Res = ::compress2((Bytef *)CompressedBuffer.data(), &CompressedSize,
                        (const Bytef *)Input.data(), Input.size(), Level);
  if (Res == Z_MEM_ERROR)
    report_bad_alloc_error("Allocation failed");
  assert(Res == Z_OK);
  __msan_unpoison(CompressedBuffer.data(), CompressedSize);
  if (CompressedSize < CompressedBuffer.size())
    CompressedBuffer.truncate(CompressedSize);
}

inline Error decompress(ArrayRef<uint8_t> Input, uint8_t *Output,
                        size_t &UncompressedSize) {
  int Res = ::uncompress((Bytef *)Output, (uLongf *)&UncompressedSize,
                         (const Bytef *)Input.data(), Input.size());
  __msan_unpoison(Output, UncompressedSize);
  return Res ? make_error<StringError>(csupport_zlib_error_string(Res),
                                       inconvertibleErrorCode())
             : Error::success();
}

inline Error decompress(ArrayRef<uint8_t> Input,
                        SmallVectorImpl<uint8_t> &Output,
                        size_t UncompressedSize) {
  Output.resize_for_overwrite(UncompressedSize);
  Error E = zlib::decompress(Input, Output.data(), UncompressedSize);
  if (UncompressedSize < Output.size())
    Output.truncate(UncompressedSize);
  return E;
}
} // namespace zlib

#else
namespace zlib {
inline bool isAvailable() { return false; }
inline void compress(ArrayRef<uint8_t> Input,
                     SmallVectorImpl<uint8_t> &CompressedBuffer, int Level) {
  llvm_unreachable("zlib::compress is unavailable");
}
inline Error decompress(ArrayRef<uint8_t> Input, uint8_t *UncompressedBuffer,
                        size_t &UncompressedSize) {
  llvm_unreachable("zlib::decompress is unavailable");
}
inline Error decompress(ArrayRef<uint8_t> Input,
                        SmallVectorImpl<uint8_t> &UncompressedBuffer,
                        size_t UncompressedSize) {
  llvm_unreachable("zlib::decompress is unavailable");
}
} // namespace zlib
#endif

#if LLVM_ENABLE_ZSTD

namespace zstd {
inline bool isAvailable() { return true; }

inline void compress(ArrayRef<uint8_t> Input,
                     SmallVectorImpl<uint8_t> &CompressedBuffer, int Level) {
  unsigned long CompressedBufferSize = ::ZSTD_compressBound(Input.size());
  CompressedBuffer.resize_for_overwrite(CompressedBufferSize);
  unsigned long CompressedSize =
      ::ZSTD_compress((char *)CompressedBuffer.data(), CompressedBufferSize,
                      (const char *)Input.data(), Input.size(), Level);
  if (ZSTD_isError(CompressedSize))
    report_bad_alloc_error("Allocation failed");
  __msan_unpoison(CompressedBuffer.data(), CompressedSize);
  if (CompressedSize < CompressedBuffer.size())
    CompressedBuffer.truncate(CompressedSize);
}

inline Error decompress(ArrayRef<uint8_t> Input, uint8_t *Output,
                        size_t &UncompressedSize) {
  const size_t Res = ::ZSTD_decompress(
      Output, UncompressedSize, (const uint8_t *)Input.data(), Input.size());
  UncompressedSize = Res;
  __msan_unpoison(Output, UncompressedSize);
  return ZSTD_isError(Res) ? make_error<StringError>(ZSTD_getErrorName(Res),
                                                     inconvertibleErrorCode())
                           : Error::success();
}

inline Error decompress(ArrayRef<uint8_t> Input,
                        SmallVectorImpl<uint8_t> &Output,
                        size_t UncompressedSize) {
  Output.resize_for_overwrite(UncompressedSize);
  Error E = zstd::decompress(Input, Output.data(), UncompressedSize);
  if (UncompressedSize < Output.size())
    Output.truncate(UncompressedSize);
  return E;
}
} // namespace zstd

#else
namespace zstd {
inline bool isAvailable() { return false; }
inline void compress(ArrayRef<uint8_t> Input,
                     SmallVectorImpl<uint8_t> &CompressedBuffer, int Level) {
  llvm_unreachable("zstd::compress is unavailable");
}
inline Error decompress(ArrayRef<uint8_t> Input, uint8_t *Output,
                        size_t &UncompressedSize) {
  llvm_unreachable("zstd::decompress is unavailable");
}
inline Error decompress(ArrayRef<uint8_t> Input,
                        SmallVectorImpl<uint8_t> &Output,
                        size_t UncompressedSize) {
  llvm_unreachable("zstd::decompress is unavailable");
}
} // namespace zstd
#endif

} // namespace compression
} // namespace llvm

#endif
