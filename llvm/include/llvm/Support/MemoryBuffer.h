//===--- MemoryBuffer.h - Memory Buffer Interface ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the MemoryBuffer interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MEMORYBUFFER_H
#define LLVM_SUPPORT_MEMORYBUFFER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <memory>
#include <stddef.h>
#include <stdint.h>

#include "csupport/lchrono.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"

namespace llvm {
namespace sys {
namespace fs {
// Duplicated from FileSystem.h to avoid a dependency.
#if defined(_WIN32)
// A Win32 HANDLE is a typedef of void*
using file_t = void *;
#else
using file_t = int;
#endif
} // namespace fs
} // namespace sys

/// This interface provides simple read-only access to a block of memory, and
/// provides simple methods for reading files and standard input into a memory
/// buffer.  In addition to basic access to the characters in the file, this
/// interface guarantees you can read one character past the end of the file,
/// and that this character will read as '\0'.
///
/// The '\0' guarantee is needed to support an optimization -- it's intended to
/// be more efficient for clients which are reading all the data to stop
/// reading when they encounter a '\0' than to continually check the file
/// position to see if it has reached the end of the file.
class MemoryBuffer {
  // hack it
public:
  const char *BufferStart; // Start of the buffer.
  const char *BufferEnd;   // End of the buffer.

protected:
  MemoryBuffer() = default;

  void init(const char *BufStart, const char *BufEnd,
            bool RequiresNullTerminator);

public:
  MemoryBuffer(const MemoryBuffer &) = delete;
  MemoryBuffer &operator=(const MemoryBuffer &) = delete;
  virtual ~MemoryBuffer();

  const char *getBufferStart() const { return BufferStart; }
  const char *getBufferEnd() const { return BufferEnd; }
  size_t getBufferSize() const { return BufferEnd - BufferStart; }

  StringRef getBuffer() const {
    return StringRef(BufferStart, getBufferSize());
  }

  /// Return an identifier for this buffer, typically the filename it was read
  /// from.
  virtual StringRef getBufferIdentifier() const { return "Unknown buffer"; }

  /// For read-only MemoryBuffer_MMap, mark the buffer as unused in the near
  /// future and the kernel can free resources associated with it. Further
  /// access is supported but may be expensive. This calls
  /// madvise(MADV_DONTNEED) on read-only file mappings on *NIX systems. This
  /// function should not be called on a writable buffer.
  virtual void dontNeedIfMmap() {}

  /// Open the specified file as a MemoryBuffer, returning a new MemoryBuffer
  /// if successful, otherwise returning null.
  ///
  /// \param IsText Set to true to indicate that the file should be read in
  /// text mode.
  ///
  /// \param IsVolatile Set to true to indicate that the contents of the file
  /// can change outside the user's control, e.g. when libclang tries to parse
  /// while the user is editing/updating the file or if the file is on an NFS.
  ///
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getFile(const Twine &Filename, bool IsText = false,
          bool RequiresNullTerminator = true, bool IsVolatile = false,
          Align Alignment = Align(16));

  /// Read all of the specified file into a MemoryBuffer as a stream
  /// (i.e. until EOF reached). This is useful for special files that
  /// look like a regular file but have 0 size (e.g. /proc/cpuinfo on Linux).
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getFileAsStream(const Twine &Filename);

  /// Given an already-open file descriptor, map some slice of it into a
  /// MemoryBuffer. The slice is specified by an \p Offset and \p MapSize.
  /// Since this is in the middle of a file, the buffer is not null terminated.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getOpenFileSlice(sys::fs::file_t FD, const Twine &Filename, uint64_t MapSize,
                   int64_t Offset, bool IsVolatile = false,
                   Align Alignment = Align(16));

  /// Given an already-open file descriptor, read the file and return a
  /// MemoryBuffer.
  ///
  /// \param IsVolatile Set to true to indicate that the contents of the file
  /// can change outside the user's control, e.g. when libclang tries to parse
  /// while the user is editing/updating the file or if the file is on an NFS.
  ///
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getOpenFile(sys::fs::file_t FD, const Twine &Filename, uint64_t FileSize,
              bool RequiresNullTerminator = true, bool IsVolatile = false,
              Align Alignment = Align(16));

  /// Open the specified memory range as a MemoryBuffer. Note that InputData
  /// must be null terminated if RequiresNullTerminator is true.
  static std::unique_ptr<MemoryBuffer>
  getMemBuffer(StringRef InputData, StringRef BufferName = "",
               bool RequiresNullTerminator = true);

  static std::unique_ptr<MemoryBuffer>
  getMemBuffer(MemoryBufferRef Ref, bool RequiresNullTerminator = true);

  /// Open the specified memory range as a MemoryBuffer, copying the contents
  /// and taking ownership of it. InputData does not have to be null terminated.
  static std::unique_ptr<MemoryBuffer>
  getMemBufferCopy(StringRef InputData, const Twine &BufferName = "");

  /// Read all of stdin into a file buffer, and return it.
  static ErrorOr<std::unique_ptr<MemoryBuffer>> getSTDIN();

  /// Open the specified file as a MemoryBuffer, or open stdin if the Filename
  /// is "-".
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getFileOrSTDIN(const Twine &Filename, bool IsText = false,
                 bool RequiresNullTerminator = true,
                 Align Alignment = Align(16));

  /// Map a subrange of the specified file as a MemoryBuffer.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getFileSlice(const Twine &Filename, uint64_t MapSize, uint64_t Offset,
               bool IsVolatile = false, Align Alignment = Align(16));

  //===--------------------------------------------------------------------===//
  // Provided for performance analysis.
  //===--------------------------------------------------------------------===//

  /// The kind of memory backing used to support the MemoryBuffer.
  enum BufferKind { MemoryBuffer_Malloc, MemoryBuffer_MMap };

  /// Return information on the memory mechanism used to support the
  /// MemoryBuffer.
  virtual BufferKind getBufferKind() const = 0;

  MemoryBufferRef getMemBufferRef() const;
};

/// This class is an extension of MemoryBuffer, which allows copy-on-write
/// access to the underlying contents.  It only supports creation methods that
/// are guaranteed to produce a writable buffer.  For example, mapping a file
/// read-only is not supported.
class WritableMemoryBuffer : public MemoryBuffer {
protected:
  WritableMemoryBuffer() = default;

public:
  using MemoryBuffer::getBuffer;
  using MemoryBuffer::getBufferEnd;
  using MemoryBuffer::getBufferStart;

  // const_cast is well-defined here, because the underlying buffer is
  // guaranteed to have been initialized with a mutable buffer.
  char *getBufferStart() {
    return const_cast<char *>(MemoryBuffer::getBufferStart());
  }
  char *getBufferEnd() {
    return const_cast<char *>(MemoryBuffer::getBufferEnd());
  }
  MutableArrayRef<char> getBuffer() {
    return {getBufferStart(), getBufferEnd()};
  }

  static ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
  getFile(const Twine &Filename, bool IsVolatile = false,
          Align Alignment = Align(16));

  /// Map a subrange of the specified file as a WritableMemoryBuffer.
  static ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
  getFileSlice(const Twine &Filename, uint64_t MapSize, uint64_t Offset,
               bool IsVolatile = false, Align Alignment = Align(16));

  /// Allocate a new MemoryBuffer of the specified size that is not initialized.
  /// Note that the caller should initialize the memory allocated by this
  /// method. The memory is owned by the MemoryBuffer object.
  ///
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  static std::unique_ptr<WritableMemoryBuffer>
  getNewUninitMemBuffer(size_t Size, const Twine &BufferName = "",
                        Align Alignment = Align(16));

  /// Allocate a new zero-initialized MemoryBuffer of the specified size. Note
  /// that the caller need not initialize the memory allocated by this method.
  /// The memory is owned by the MemoryBuffer object.
  static std::unique_ptr<WritableMemoryBuffer>
  getNewMemBuffer(size_t Size, const Twine &BufferName = "");

private:
  // Hide these base class factory function so one can't write
  //   WritableMemoryBuffer::getXXX()
  // and be surprised that he got a read-only Buffer.
  using MemoryBuffer::getFileAsStream;
  using MemoryBuffer::getFileOrSTDIN;
  using MemoryBuffer::getMemBuffer;
  using MemoryBuffer::getMemBufferCopy;
  using MemoryBuffer::getOpenFile;
  using MemoryBuffer::getOpenFileSlice;
  using MemoryBuffer::getSTDIN;
};

/// This class is an extension of MemoryBuffer, which allows write access to
/// the underlying contents and committing those changes to the original source.
/// It only supports creation methods that are guaranteed to produce a writable
/// buffer.  For example, mapping a file read-only is not supported.
class WriteThroughMemoryBuffer : public MemoryBuffer {
protected:
  WriteThroughMemoryBuffer() = default;

public:
  using MemoryBuffer::getBuffer;
  using MemoryBuffer::getBufferEnd;
  using MemoryBuffer::getBufferStart;

  // const_cast is well-defined here, because the underlying buffer is
  // guaranteed to have been initialized with a mutable buffer.
  char *getBufferStart() {
    return const_cast<char *>(MemoryBuffer::getBufferStart());
  }
  char *getBufferEnd() {
    return const_cast<char *>(MemoryBuffer::getBufferEnd());
  }
  MutableArrayRef<char> getBuffer() {
    return {getBufferStart(), getBufferEnd()};
  }

  static ErrorOr<std::unique_ptr<WriteThroughMemoryBuffer>>
  getFile(const Twine &Filename, int64_t FileSize = -1);

  /// Map a subrange of the specified file as a ReadWriteMemoryBuffer.
  static ErrorOr<std::unique_ptr<WriteThroughMemoryBuffer>>
  getFileSlice(const Twine &Filename, uint64_t MapSize, uint64_t Offset);

private:
  // Hide these base class factory function so one can't write
  //   WritableMemoryBuffer::getXXX()
  // and be surprised that he got a read-only Buffer.
  using MemoryBuffer::getFileAsStream;
  using MemoryBuffer::getFileOrSTDIN;
  using MemoryBuffer::getMemBuffer;
  using MemoryBuffer::getMemBufferCopy;
  using MemoryBuffer::getOpenFile;
  using MemoryBuffer::getOpenFileSlice;
  using MemoryBuffer::getSTDIN;
};

inline MemoryBufferRef::MemoryBufferRef(const MemoryBuffer &Buffer)
    : Buffer(Buffer.getBuffer()), Identifier(Buffer.getBufferIdentifier()) {}

} // end namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===

namespace llvm {

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <limits.h>
#include <unistd.h>
#else
#include <io.h>
#endif

#ifdef __MVS__
#include "llvm/Support/AutoConvert.h"
#endif

//===----------------------------------------------------------------------===//
// MemoryBuffer implementation itself.
//===----------------------------------------------------------------------===//

inline MemoryBuffer::~MemoryBuffer() = default;

/// init - Initialize this MemoryBuffer as a reference to externally allocated
/// memory, memory that we know is already null terminated.
inline void MemoryBuffer::init(const char *BufStart, const char *BufEnd,
                               bool RequiresNullTerminator) {
  assert((!RequiresNullTerminator || BufEnd[0] == 0) &&
         "Buffer is not null terminated!");
  BufferStart = BufStart;
  BufferEnd = BufEnd;
}

} // namespace llvm

//===----------------------------------------------------------------------===//
// MemoryBufferMem implementation (global placement new; must not be in llvm::).
//===----------------------------------------------------------------------===//

#define CopyStringRef(Memory, Data)                                            \
  do {                                                                         \
    if (!(Data).empty())                                                       \
      memcpy((Memory), (Data).data(), (Data).size());                          \
    (Memory)[(Data).size()] = 0;                                               \
  } while (0)

namespace {
struct NamedBufferAlloc {
  const llvm::Twine &Name;
  NamedBufferAlloc(const llvm::Twine &Name) : Name(Name) {}
};
} // namespace

inline void *operator new(size_t N, const NamedBufferAlloc &Alloc) {
  llvm::SmallString<256> NameBuf;
  llvm::StringRef NameRef = Alloc.Name.toStringRef(NameBuf);

  char *Mem = (char *)malloc(N + sizeof(size_t) + NameRef.size() + 1);
  *(size_t *)(Mem + N) = NameRef.size();
  CopyStringRef(Mem + N + sizeof(size_t), NameRef);
  return Mem;
}

namespace llvm {

namespace {
/// MemoryBufferMem - Named MemoryBuffer pointing to a block of memory.
template <typename MB> class MemoryBufferMem : public MB {
public:
  MemoryBufferMem(StringRef InputData, bool RequiresNullTerminator) {
    MemoryBuffer::init(InputData.begin(), InputData.end(),
                       RequiresNullTerminator);
  }

  void operator delete(void *p) { free(p); }

  StringRef getBufferIdentifier() const override {
    // The name is stored after the class itself.
    return StringRef((const char *)(this + 1) + sizeof(size_t),
                     *(const size_t *)(this + 1));
  }

  MemoryBuffer::BufferKind getBufferKind() const override {
    return MemoryBuffer::MemoryBuffer_Malloc;
  }
};
} // namespace

template <typename MB>
inline static ErrorOr<std::unique_ptr<MB>>
getFileAux(const Twine &Filename, uint64_t MapSize, uint64_t Offset,
           bool IsText, bool RequiresNullTerminator, bool IsVolatile,
           Align Alignment);

inline std::unique_ptr<MemoryBuffer>
MemoryBuffer::getMemBuffer(StringRef InputData, StringRef BufferName,
                           bool RequiresNullTerminator) {
  auto *Ret = new (NamedBufferAlloc(BufferName))
      MemoryBufferMem<MemoryBuffer>(InputData, RequiresNullTerminator);
  return std::unique_ptr<MemoryBuffer>(Ret);
}

inline std::unique_ptr<MemoryBuffer>
MemoryBuffer::getMemBuffer(MemoryBufferRef Ref, bool RequiresNullTerminator) {
  return std::unique_ptr<MemoryBuffer>(getMemBuffer(
      Ref.getBuffer(), Ref.getBufferIdentifier(), RequiresNullTerminator));
}

inline static ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
getMemBufferCopyImpl(StringRef InputData, const Twine &BufferName) {
  auto Buf =
      WritableMemoryBuffer::getNewUninitMemBuffer(InputData.size(), BufferName);
  if (!Buf)
    return make_error_code(errc::not_enough_memory);
  // Calling memcpy with null src/dst is UB, and an empty StringRef is
  // represented with {0, 0}.
  if (InputData.data())
    memcpy(Buf->getBufferStart(), InputData.data(), InputData.size());
  return Buf;
}

inline std::unique_ptr<MemoryBuffer>
MemoryBuffer::getMemBufferCopy(StringRef InputData, const Twine &BufferName) {
  auto Buf = getMemBufferCopyImpl(InputData, BufferName);
  if (Buf)
    return std::move(*Buf);
  return nullptr;
}

inline ErrorOr<std::unique_ptr<MemoryBuffer>>
MemoryBuffer::getFileOrSTDIN(const Twine &Filename, bool IsText,
                             bool RequiresNullTerminator, Align Alignment) {
  SmallString<256> NameBuf;
  StringRef NameRef = Filename.toStringRef(NameBuf);

  if (NameRef == "-")
    return getSTDIN();
  return getFile(Filename, IsText, RequiresNullTerminator,
                 /*IsVolatile=*/false, Alignment);
}

inline ErrorOr<std::unique_ptr<MemoryBuffer>>
MemoryBuffer::getFileSlice(const Twine &FilePath, uint64_t MapSize,
                           uint64_t Offset, bool IsVolatile, Align Alignment) {
  return getFileAux<MemoryBuffer>(FilePath, MapSize, Offset, /*IsText=*/false,
                                  /*RequiresNullTerminator=*/false, IsVolatile,
                                  Alignment);
}

//===----------------------------------------------------------------------===//
// MemoryBuffer::getFile implementation.
//===----------------------------------------------------------------------===//

namespace {

template <typename MB>
inline const sys::fs::mapped_file_region::mapmode Mapmode =
    sys::fs::mapped_file_region::readonly;
template <>
inline const sys::fs::mapped_file_region::mapmode Mapmode<MemoryBuffer> =
    sys::fs::mapped_file_region::readonly;
template <>
inline const sys::fs::mapped_file_region::mapmode
    Mapmode<WritableMemoryBuffer> = sys::fs::mapped_file_region::priv;
template <>
const sys::fs::mapped_file_region::mapmode inline Mapmode<
    WriteThroughMemoryBuffer> = sys::fs::mapped_file_region::readwrite;

/// Memory maps a file descriptor using sys::fs::mapped_file_region.
///
/// This handles converting the offset into a legal offset on the platform.
template <typename MB> class MemoryBufferMMapFile : public MB {
  sys::fs::mapped_file_region MFR;

  static uint64_t getLegalMapOffset(uint64_t Offset) {
    return Offset & ~(sys::fs::mapped_file_region::alignment() - 1);
  }

  static uint64_t getLegalMapSize(uint64_t Len, uint64_t Offset) {
    return Len + (Offset - getLegalMapOffset(Offset));
  }

  const char *getStart(uint64_t Len, uint64_t Offset) {
    return MFR.const_data() + (Offset - getLegalMapOffset(Offset));
  }

public:
  MemoryBufferMMapFile(bool RequiresNullTerminator, sys::fs::file_t FD,
                       uint64_t Len, uint64_t Offset, std::error_code &EC)
      : MFR(FD, Mapmode<MB>, getLegalMapSize(Len, Offset),
            getLegalMapOffset(Offset), EC) {
    if (!EC) {
      const char *Start = getStart(Len, Offset);
      MemoryBuffer::init(Start, Start + Len, RequiresNullTerminator);
    }
  }

  void operator delete(void *p) { free(p); }

  StringRef getBufferIdentifier() const override {
    // The name is stored after the class itself.
    return StringRef((const char *)(this + 1) + sizeof(size_t),
                     *(const size_t *)(this + 1));
  }

  MemoryBuffer::BufferKind getBufferKind() const override {
    return MemoryBuffer::MemoryBuffer_MMap;
  }

  void dontNeedIfMmap() override { MFR.dontNeed(); }
};
} // namespace

inline static ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
getMemoryBufferForStream(sys::fs::file_t FD, const Twine &BufferName) {
  SmallString<sys::fs::DefaultReadChunkSize> Buffer;
  if (Error E = sys::fs::readNativeFileToEOF(FD, Buffer))
    return errorToErrorCode(std::move(E));
  return getMemBufferCopyImpl(Buffer, BufferName);
}

inline ErrorOr<std::unique_ptr<MemoryBuffer>>
MemoryBuffer::getFile(const Twine &Filename, bool IsText,
                      bool RequiresNullTerminator, bool IsVolatile,
                      Align Alignment) {
  return getFileAux<MemoryBuffer>(Filename, /*MapSize=*/-1, /*Offset=*/0,
                                  IsText, RequiresNullTerminator, IsVolatile,
                                  Alignment);
}

template <typename MB>
inline static ErrorOr<std::unique_ptr<MB>>
getOpenFileImpl(sys::fs::file_t FD, const Twine &Filename, uint64_t FileSize,
                uint64_t MapSize, int64_t Offset, bool RequiresNullTerminator,
                bool IsVolatile, Align Alignment);

template <typename MB>
inline static ErrorOr<std::unique_ptr<MB>>
getFileAux(const Twine &Filename, uint64_t MapSize, uint64_t Offset,
           bool IsText, bool RequiresNullTerminator, bool IsVolatile,
           Align Alignment) {
  Expected<sys::fs::file_t> FDOrErr = sys::fs::openNativeFileForRead(
      Filename, IsText ? sys::fs::OF_TextWithCRLF : sys::fs::OF_None);
  if (!FDOrErr)
    return errorToErrorCode(FDOrErr.takeError());
  sys::fs::file_t FD = *FDOrErr;
  auto Ret = getOpenFileImpl<MB>(FD, Filename, /*FileSize=*/-1, MapSize, Offset,
                                 RequiresNullTerminator, IsVolatile, Alignment);
  sys::fs::closeFile(FD);
  return Ret;
}

inline ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
WritableMemoryBuffer::getFile(const Twine &Filename, bool IsVolatile,
                              Align Alignment) {
  return getFileAux<WritableMemoryBuffer>(
      Filename, /*MapSize=*/-1, /*Offset=*/0, /*IsText=*/false,
      /*RequiresNullTerminator=*/false, IsVolatile, Alignment);
}

inline ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
WritableMemoryBuffer::getFileSlice(const Twine &Filename, uint64_t MapSize,
                                   uint64_t Offset, bool IsVolatile,
                                   Align Alignment) {
  return getFileAux<WritableMemoryBuffer>(
      Filename, MapSize, Offset, /*IsText=*/false,
      /*RequiresNullTerminator=*/false, IsVolatile, Alignment);
}

inline std::unique_ptr<WritableMemoryBuffer>
WritableMemoryBuffer::getNewUninitMemBuffer(size_t Size,
                                            const Twine &BufferName,
                                            Align Alignment) {
  using MemBuffer = MemoryBufferMem<WritableMemoryBuffer>;

  // Use 16-byte alignment if no alignment is specified.
  Align BufAlign = Alignment;

  // Allocate space for the MemoryBuffer, the data and the name. It is important
  // that MemoryBuffer and data are aligned so PointerIntPair works with them.
  SmallString<256> NameBuf;
  StringRef NameRef = BufferName.toStringRef(NameBuf);

  size_t StringLen = sizeof(MemBuffer) + sizeof(size_t) + NameRef.size() + 1;
  size_t RealLen = StringLen + Size + 1 + BufAlign.value();
  if (RealLen <= Size) // Check for rollover.
    return nullptr;
  char *Mem = (char *)malloc(RealLen);
  if (!Mem)
    return nullptr;

  // The name is stored after the class itself.
  *(size_t *)(Mem + sizeof(MemBuffer)) = NameRef.size();
  CopyStringRef(Mem + sizeof(MemBuffer) + sizeof(size_t), NameRef);

  // The buffer begins after the name and must be aligned.
  char *Buf = (char *)alignAddr(Mem + StringLen, BufAlign);
  Buf[Size] = 0; // Null terminate buffer.

  auto *Ret = new (Mem) MemBuffer(StringRef(Buf, Size), true);
  return std::unique_ptr<WritableMemoryBuffer>(Ret);
}

inline std::unique_ptr<WritableMemoryBuffer>
WritableMemoryBuffer::getNewMemBuffer(size_t Size, const Twine &BufferName) {
  auto SB = WritableMemoryBuffer::getNewUninitMemBuffer(Size, BufferName);
  if (!SB)
    return nullptr;
  memset(SB->getBufferStart(), 0, Size);
  return SB;
}

inline static bool shouldUseMmap(sys::fs::file_t FD, size_t FileSize,
                                 size_t MapSize, off_t Offset,
                                 bool RequiresNullTerminator, int PageSize,
                                 bool IsVolatile) {
  int r = csupport_should_use_mmap(FileSize, MapSize, (int64_t)Offset,
                                   RequiresNullTerminator ? 1 : 0, PageSize,
                                   IsVolatile ? 1 : 0);
  if (r >= 0)
    return r != 0;
  // r == -1: need file size via fstat
  sys::fs::file_status Status;
  if (sys::fs::status(FD, Status))
    return false;
  FileSize = Status.getSize();
  r = csupport_should_use_mmap(FileSize, MapSize, (int64_t)Offset,
                               RequiresNullTerminator ? 1 : 0, PageSize,
                               IsVolatile ? 1 : 0);
  return r > 0;
}

inline static ErrorOr<std::unique_ptr<WriteThroughMemoryBuffer>>
getReadWriteFile(const Twine &Filename, uint64_t FileSize, uint64_t MapSize,
                 uint64_t Offset) {
  Expected<sys::fs::file_t> FDOrErr = sys::fs::openNativeFileForReadWrite(
      Filename, sys::fs::CD_OpenExisting, sys::fs::OF_None);
  if (!FDOrErr)
    return errorToErrorCode(FDOrErr.takeError());
  sys::fs::file_t FD = *FDOrErr;

  // Default is to map the full file.
  if (MapSize == uint64_t(-1)) {
    // If we don't know the file size, use fstat to find out.  fstat on an open
    // file descriptor is cheaper than stat on a random path.
    if (FileSize == uint64_t(-1)) {
      sys::fs::file_status Status;
      auto EC = sys::fs::status(FD, Status);
      if (EC)
        return EC;

      // If this not a file or a block device (e.g. it's a named pipe
      // or character device), we can't mmap it, so error out.
      sys::fs::file_type Type = Status.type();
      if (Type != sys::fs::file_type::regular_file &&
          Type != sys::fs::file_type::block_file)
        return make_error_code(errc::invalid_argument);

      FileSize = Status.getSize();
    }
    MapSize = FileSize;
  }

  std::error_code EC;
  std::unique_ptr<WriteThroughMemoryBuffer> Result(
      new (NamedBufferAlloc(Filename))
          MemoryBufferMMapFile<WriteThroughMemoryBuffer>(false, FD, MapSize,
                                                         Offset, EC));
  if (EC)
    return EC;
  return Result;
}

inline ErrorOr<std::unique_ptr<WriteThroughMemoryBuffer>>
WriteThroughMemoryBuffer::getFile(const Twine &Filename, int64_t FileSize) {
  return getReadWriteFile(Filename, FileSize, FileSize, 0);
}

/// Map a subrange of the specified file as a WritableMemoryBuffer.
inline ErrorOr<std::unique_ptr<WriteThroughMemoryBuffer>>
WriteThroughMemoryBuffer::getFileSlice(const Twine &Filename, uint64_t MapSize,
                                       uint64_t Offset) {
  return getReadWriteFile(Filename, -1, MapSize, Offset);
}

inline static int getMemoryBufferPageSizeEstimate() {
#if defined(_WIN32)
  return 4096;
#else
  long PS = sysconf(_SC_PAGESIZE);
  if (PS <= 0 || PS > INT_MAX)
    return 4096;
  return (int)PS;
#endif
}

template <typename MB>
inline static ErrorOr<std::unique_ptr<MB>>
getOpenFileImpl(sys::fs::file_t FD, const Twine &Filename, uint64_t FileSize,
                uint64_t MapSize, int64_t Offset, bool RequiresNullTerminator,
                bool IsVolatile, Align Alignment) {
  static int PageSize = getMemoryBufferPageSizeEstimate();

  // Default is to map the full file.
  if (MapSize == uint64_t(-1)) {
    // If we don't know the file size, use fstat to find out.  fstat on an open
    // file descriptor is cheaper than stat on a random path.
    if (FileSize == uint64_t(-1)) {
      sys::fs::file_status Status;
      auto EC = sys::fs::status(FD, Status);
      if (EC)
        return EC;

      // If this not a file or a block device (e.g. it's a named pipe
      // or character device), we can't trust the size. Create the memory
      // buffer by copying off the stream.
      sys::fs::file_type Type = Status.type();
      if (Type != sys::fs::file_type::regular_file &&
          Type != sys::fs::file_type::block_file)
        return getMemoryBufferForStream(FD, Filename);

      FileSize = Status.getSize();
    }
    MapSize = FileSize;
  }

  if (shouldUseMmap(FD, FileSize, MapSize, Offset, RequiresNullTerminator,
                    PageSize, IsVolatile)) {
    std::error_code EC;
    std::unique_ptr<MB> Result(
        new (NamedBufferAlloc(Filename)) MemoryBufferMMapFile<MB>(
            RequiresNullTerminator, FD, MapSize, Offset, EC));
    if (!EC)
      return Result;
  }

#ifdef __MVS__
  // Set codepage auto-conversion for z/OS.
  if (auto EC = llvm::enableAutoConversion(FD))
    return EC;
#endif

  auto Buf =
      WritableMemoryBuffer::getNewUninitMemBuffer(MapSize, Filename, Alignment);
  if (!Buf) {
    // Failed to create a buffer. The only way it can fail is if
    // new(nothrow) returns 0.
    return make_error_code(errc::not_enough_memory);
  }

  // Read until EOF, zero-initialize the rest.
  MutableArrayRef<char> ToRead = Buf->getBuffer();
  while (!ToRead.empty()) {
    Expected<size_t> ReadBytes =
        sys::fs::readNativeFileSlice(FD, ToRead, Offset);
    if (!ReadBytes)
      return errorToErrorCode(ReadBytes.takeError());
    if (*ReadBytes == 0) {
      memset(ToRead.data(), 0, ToRead.size());
      break;
    }
    ToRead = ToRead.drop_front(*ReadBytes);
    Offset += *ReadBytes;
  }

  return Buf;
}

inline ErrorOr<std::unique_ptr<MemoryBuffer>>
MemoryBuffer::getOpenFile(sys::fs::file_t FD, const Twine &Filename,
                          uint64_t FileSize, bool RequiresNullTerminator,
                          bool IsVolatile, Align Alignment) {
  return getOpenFileImpl<MemoryBuffer>(FD, Filename, FileSize, FileSize, 0,
                                       RequiresNullTerminator, IsVolatile,
                                       Alignment);
}

inline ErrorOr<std::unique_ptr<MemoryBuffer>>
MemoryBuffer::getOpenFileSlice(sys::fs::file_t FD, const Twine &Filename,
                               uint64_t MapSize, int64_t Offset,
                               bool IsVolatile, Align Alignment) {
  assert(MapSize != uint64_t(-1));
  return getOpenFileImpl<MemoryBuffer>(FD, Filename, -1, MapSize, Offset, false,
                                       IsVolatile, Alignment);
}

inline ErrorOr<std::unique_ptr<MemoryBuffer>> MemoryBuffer::getSTDIN() {
  // Read in all of the data from stdin, we cannot mmap stdin.
  //
  // FIXME: That isn't necessarily true, we should try to mmap stdin and
  // fallback if it fails.
  sys::ChangeStdinMode(sys::fs::OF_Text);

  return getMemoryBufferForStream(sys::fs::getStdinHandle(), "<stdin>");
}

inline ErrorOr<std::unique_ptr<MemoryBuffer>>
MemoryBuffer::getFileAsStream(const Twine &Filename) {
  Expected<sys::fs::file_t> FDOrErr =
      sys::fs::openNativeFileForRead(Filename, sys::fs::OF_None);
  if (!FDOrErr)
    return errorToErrorCode(FDOrErr.takeError());
  sys::fs::file_t FD = *FDOrErr;
  ErrorOr<std::unique_ptr<MemoryBuffer>> Ret =
      getMemoryBufferForStream(FD, Filename);
  sys::fs::closeFile(FD);
  return Ret;
}

inline MemoryBufferRef MemoryBuffer::getMemBufferRef() const {
  StringRef Data = getBuffer();
  StringRef Identifier = getBufferIdentifier();
  return MemoryBufferRef(Data, Identifier);
}

} // namespace llvm

#endif // LLVM_SUPPORT_MEMORYBUFFER_H
