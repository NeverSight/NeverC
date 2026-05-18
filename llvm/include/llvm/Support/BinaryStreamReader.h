//===- BinaryStreamReader.h - Reads objects from a binary stream *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BINARYSTREAMREADER_H
#define LLVM_SUPPORT_BINARYSTREAMREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LEB128.h"
#include <type_traits>

namespace llvm {

/// Provides read only access to a subclass of `BinaryStream`.  Provides
/// bounds checking and helpers for writing certain common data types such as
/// null-terminated strings, integers in various flavors of endianness, etc.
/// Can be subclassed to provide reading of custom datatypes, although no
/// are overridable.
class BinaryStreamReader {
public:
  BinaryStreamReader() = default;
  explicit BinaryStreamReader(BinaryStreamRef Ref);
  explicit BinaryStreamReader(BinaryStream &Stream);
  explicit BinaryStreamReader(ArrayRef<uint8_t> Data, llvm::endianness Endian);
  explicit BinaryStreamReader(StringRef Data, llvm::endianness Endian);

  BinaryStreamReader(const BinaryStreamReader &Other) = default;

  BinaryStreamReader &operator=(const BinaryStreamReader &Other) = default;

  virtual ~BinaryStreamReader() = default;

  /// Read as much as possible from the underlying string at the current offset
  /// without invoking a copy, and set \p Buffer to the resulting data slice.
  /// Updates the stream's offset to point after the newly read data.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readLongestContiguousChunk(ArrayRef<uint8_t> &Buffer);

  /// Read \p Size bytes from the underlying stream at the current offset and
  /// and set \p Buffer to the resulting data slice.  Whether a copy occurs
  /// depends on the implementation of the underlying stream.  Updates the
  /// stream's offset to point after the newly read data.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readBytes(ArrayRef<uint8_t> &Buffer, uint32_t Size);

  /// Read an integer of the specified endianness into \p Dest and update the
  /// stream's offset.  The data is always copied from the stream's underlying
  /// buffer into \p Dest. Updates the stream's offset to point after the newly
  /// read data.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T> Error readInteger(T &Dest) {
    static_assert(std::is_integral_v<T>,
                  "Cannot call readInteger with non-integral value!");

    ArrayRef<uint8_t> Bytes;
    if (auto EC = readBytes(Bytes, sizeof(T)))
      return EC;

    Dest = llvm::support::endian::read<T>(Bytes.data(), Stream.getEndian());
    return Error::success();
  }

  /// Similar to readInteger.
  template <typename T> Error readEnum(T &Dest) {
    static_assert(std::is_enum<T>::value,
                  "Cannot call readEnum with non-enum value!");
    std::underlying_type_t<T> N;
    if (auto EC = readInteger(N))
      return EC;
    Dest = static_cast<T>(N);
    return Error::success();
  }

  /// Read an unsigned LEB128 encoded value.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readULEB128(uint64_t &Dest);

  /// Read a signed LEB128 encoded value.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readSLEB128(int64_t &Dest);

  /// Read a null terminated string from \p Dest.  Whether a copy occurs depends
  /// on the implementation of the underlying stream.  Updates the stream's
  /// offset to point after the newly read data.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readCString(StringRef &Dest);

  /// Similar to readCString, however read a null-terminated UTF16 string
  /// instead.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readWideString(ArrayRef<UTF16> &Dest);

  /// Read a \p Length byte string into \p Dest.  Whether a copy occurs depends
  /// on the implementation of the underlying stream.  Updates the stream's
  /// offset to point after the newly read data.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readFixedString(StringRef &Dest, uint32_t Length);

  /// Read the entire remainder of the underlying stream into \p Ref.  This is
  /// equivalent to calling getUnderlyingStream().slice(Offset).  Updates the
  /// stream's offset to point to the end of the stream.  Never causes a copy.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readStreamRef(BinaryStreamRef &Ref);

  /// Read \p Length bytes from the underlying stream into \p Ref.  This is
  /// equivalent to calling getUnderlyingStream().slice(Offset, Length).
  /// Updates the stream's offset to point after the newly read object.  Never
  /// causes a copy.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readStreamRef(BinaryStreamRef &Ref, uint32_t Length);

  /// Read \p Length bytes from the underlying stream into \p Ref.  This is
  /// equivalent to calling getUnderlyingStream().slice(Offset, Length).
  /// Updates the stream's offset to point after the newly read object.  Never
  /// causes a copy.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  Error readSubstream(BinarySubstreamRef &Ref, uint32_t Length);

  /// Get a pointer to an object of type T from the underlying stream, as if by
  /// memcpy, and store the result into \p Dest.  It is up to the caller to
  /// ensure that objects of type T can be safely treated in this manner.
  /// Updates the stream's offset to point after the newly read object.  Whether
  /// a copy occurs depends upon the implementation of the underlying
  /// stream.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T> Error readObject(const T *&Dest) {
    ArrayRef<uint8_t> Buffer;
    if (auto EC = readBytes(Buffer, sizeof(T)))
      return EC;
    Dest = reinterpret_cast<const T *>(Buffer.data());
    return Error::success();
  }

  /// Get a reference to a \p NumElements element array of objects of type T
  /// from the underlying stream as if by memcpy, and store the resulting array
  /// slice into \p array.  It is up to the caller to ensure that objects of
  /// type T can be safely treated in this manner.  Updates the stream's offset
  /// to point after the newly read object.  Whether a copy occurs depends upon
  /// the implementation of the underlying stream.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T>
  Error readArray(ArrayRef<T> &Array, uint32_t NumElements) {
    ArrayRef<uint8_t> Bytes;
    if (NumElements == 0) {
      Array = ArrayRef<T>();
      return Error::success();
    }

    if (NumElements > UINT32_MAX / sizeof(T))
      return make_error<BinaryStreamError>(
          stream_error_code::invalid_array_size);

    if (auto EC = readBytes(Bytes, NumElements * sizeof(T)))
      return EC;

    assert(isAddrAligned(Align::Of<T>(), Bytes.data()) &&
           "Reading at invalid alignment!");

    Array = ArrayRef<T>(reinterpret_cast<const T *>(Bytes.data()), NumElements);
    return Error::success();
  }

  /// Read a VarStreamArray of size \p Size bytes and store the result into
  /// \p Array.  Updates the stream's offset to point after the newly read
  /// array.  Never causes a copy (although iterating the elements of the
  /// VarStreamArray may, depending upon the implementation of the underlying
  /// stream).
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T, typename U>
  Error readArray(VarStreamArray<T, U> &Array, uint32_t Size,
                  uint32_t Skew = 0) {
    BinaryStreamRef S;
    if (auto EC = readStreamRef(S, Size))
      return EC;
    Array.setUnderlyingStream(S, Skew);
    return Error::success();
  }

  /// Read a FixedStreamArray of \p NumItems elements and store the result into
  /// \p Array.  Updates the stream's offset to point after the newly read
  /// array.  Never causes a copy (although iterating the elements of the
  /// FixedStreamArray may, depending upon the implementation of the underlying
  /// stream).
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T>
  Error readArray(FixedStreamArray<T> &Array, uint32_t NumItems) {
    if (NumItems == 0) {
      Array = FixedStreamArray<T>();
      return Error::success();
    }

    if (NumItems > UINT32_MAX / sizeof(T))
      return make_error<BinaryStreamError>(
          stream_error_code::invalid_array_size);

    BinaryStreamRef View;
    if (auto EC = readStreamRef(View, NumItems * sizeof(T)))
      return EC;

    Array = FixedStreamArray<T>(View);
    return Error::success();
  }

  bool empty() const { return bytesRemaining() == 0; }
  void setOffset(uint64_t Off) { Offset = Off; }
  uint64_t getOffset() const { return Offset; }
  uint64_t getLength() const { return Stream.getLength(); }
  uint64_t bytesRemaining() const { return getLength() - getOffset(); }

  /// Advance the stream's offset by \p Amount bytes.
  ///
  /// \returns a success error code if at least \p Amount bytes remain in the
  /// stream, otherwise returns an appropriate error code.
  Error skip(uint64_t Amount);

  /// Examine the next byte of the underlying stream without advancing the
  /// stream's offset.  If the stream is empty the behavior is undefined.
  ///
  /// \returns the next byte in the stream.
  uint8_t peek() const;

  Error padToAlignment(uint32_t Align);

  void split(uint64_t Off, BinaryStreamReader &First,
             BinaryStreamReader &Second) const;

private:
  BinaryStreamRef Stream;
  uint64_t Offset = 0;
};
inline BinaryStreamReader::BinaryStreamReader(BinaryStreamRef Ref)
    : Stream(Ref) {}
inline BinaryStreamReader::BinaryStreamReader(BinaryStream &Stream)
    : Stream(Stream) {}
inline BinaryStreamReader::BinaryStreamReader(ArrayRef<uint8_t> Data,
                                              endianness Endian)
    : Stream(Data, Endian) {}
inline BinaryStreamReader::BinaryStreamReader(StringRef Data, endianness Endian)
    : Stream(Data, Endian) {}
inline Error
BinaryStreamReader::readLongestContiguousChunk(ArrayRef<uint8_t> &Buffer) {
  if (auto EC = Stream.readLongestContiguousChunk(Offset, Buffer))
    return EC;
  Offset += Buffer.size();
  return Error::success();
}
inline Error BinaryStreamReader::readBytes(ArrayRef<uint8_t> &Buffer,
                                           uint32_t Size) {
  if (auto EC = Stream.readBytes(Offset, Size, Buffer))
    return EC;
  Offset += Size;
  return Error::success();
}
inline Error BinaryStreamReader::readULEB128(uint64_t &Dest) {
  SmallVector<uint8_t, 10> EncodedBytes;
  ArrayRef<uint8_t> NextByte;
  do {
    if (auto Err = readBytes(NextByte, 1))
      return Err;
    EncodedBytes.push_back(NextByte[0]);
  } while (NextByte[0] & 0x80);
  Dest = decodeULEB128(EncodedBytes.begin(), 0, EncodedBytes.end());
  return Error::success();
}
inline Error BinaryStreamReader::readSLEB128(int64_t &Dest) {
  SmallVector<uint8_t, 10> EncodedBytes;
  ArrayRef<uint8_t> NextByte;
  do {
    if (auto Err = readBytes(NextByte, 1))
      return Err;
    EncodedBytes.push_back(NextByte[0]);
  } while (NextByte[0] & 0x80);
  Dest = decodeSLEB128(EncodedBytes.begin(), 0, EncodedBytes.end());
  return Error::success();
}
inline Error BinaryStreamReader::readCString(StringRef &Dest) {
  uint64_t OriginalOffset = getOffset();
  uint64_t FoundOffset = 0;
  while (true) {
    uint64_t ThisOffset = getOffset();
    ArrayRef<uint8_t> Buffer;
    if (auto EC = readLongestContiguousChunk(Buffer))
      return EC;
    StringRef S((const char *)(Buffer.begin()), Buffer.size());
    size_t Pos = S.find_first_of('\0');
    if (LLVM_LIKELY(Pos != StringRef::npos)) {
      FoundOffset = Pos + ThisOffset;
      break;
    }
  }
  assert(FoundOffset >= OriginalOffset);
  setOffset(OriginalOffset);
  size_t Length = FoundOffset - OriginalOffset;
  if (auto EC = readFixedString(Dest, Length))
    return EC;
  setOffset(FoundOffset + 1);
  return Error::success();
}
inline Error BinaryStreamReader::readWideString(ArrayRef<UTF16> &Dest) {
  uint64_t Length = 0;
  uint64_t OriginalOffset = getOffset();
  const UTF16 *C;
  while (true) {
    if (auto EC = readObject(C))
      return EC;
    if (*C == 0x0000)
      break;
    ++Length;
  }
  uint64_t NewOffset = getOffset();
  setOffset(OriginalOffset);
  if (auto EC = readArray(Dest, Length))
    return EC;
  setOffset(NewOffset);
  return Error::success();
}
inline Error BinaryStreamReader::readFixedString(StringRef &Dest,
                                                 uint32_t Length) {
  ArrayRef<uint8_t> Bytes;
  if (auto EC = readBytes(Bytes, Length))
    return EC;
  Dest = StringRef((const char *)(Bytes.begin()), Bytes.size());
  return Error::success();
}
inline Error BinaryStreamReader::readStreamRef(BinaryStreamRef &Ref) {
  return readStreamRef(Ref, bytesRemaining());
}
inline Error BinaryStreamReader::readStreamRef(BinaryStreamRef &Ref,
                                               uint32_t Length) {
  if (bytesRemaining() < Length)
    return make_error<BinaryStreamError>(stream_error_code::stream_too_short);
  Ref = Stream.slice(Offset, Length);
  Offset += Length;
  return Error::success();
}
inline Error BinaryStreamReader::readSubstream(BinarySubstreamRef &Ref,
                                               uint32_t Length) {
  Ref.Offset = getOffset();
  return readStreamRef(Ref.StreamData, Length);
}
inline Error BinaryStreamReader::skip(uint64_t Amount) {
  if (Amount > bytesRemaining())
    return make_error<BinaryStreamError>(stream_error_code::stream_too_short);
  Offset += Amount;
  return Error::success();
}
inline Error BinaryStreamReader::padToAlignment(uint32_t Align) {
  uint32_t NewOffset = alignTo(Offset, Align);
  return skip(NewOffset - Offset);
}
inline uint8_t BinaryStreamReader::peek() const {
  ArrayRef<uint8_t> Buffer;
  auto EC = Stream.readBytes(Offset, 1, Buffer);
  assert(!EC && "Cannot peek an empty buffer!");
  llvm::consumeError(std::move(EC));
  return Buffer[0];
}
inline void BinaryStreamReader::split(uint64_t Off, BinaryStreamReader &First,
                                      BinaryStreamReader &Second) const {
  assert(getLength() >= Off);
  BinaryStreamRef R = Stream.drop_front(Offset);
  BinaryStreamRef S = R.drop_front(Off);
  R = R.keep_front(Off);
  First = BinaryStreamReader{R};
  Second = BinaryStreamReader{S};
}

} // namespace llvm

#endif // LLVM_SUPPORT_BINARYSTREAMREADER_H
