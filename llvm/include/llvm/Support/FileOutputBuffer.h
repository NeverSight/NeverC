//=== FileOutputBuffer.h - File Output Buffer -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utility for creating a in-memory buffer that will be written to a file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FILEOUTPUTBUFFER_H
#define LLVM_SUPPORT_FILEOUTPUTBUFFER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
/// FileOutputBuffer - This interface provides simple way to create an in-memory
/// buffer which will be written to a file. During the lifetime of these
/// objects, the content or existence of the specified file is undefined. That
/// is, creating an OutputBuffer for a file may immediately remove the file.
/// If the FileOutputBuffer is committed, the target file's content will become
/// the buffer content at the time of the commit.  If the FileOutputBuffer is
/// not committed, the file will be deleted in the FileOutputBuffer destructor.
class FileOutputBuffer {
public:
  enum {
    /// Set the 'x' bit on the resulting file.
    F_executable = 1,

    /// Don't use mmap and instead write an in-memory buffer to a file when this
    /// buffer is closed.
    F_no_mmap = 2,
  };

  /// Factory method to create an OutputBuffer object which manages a read/write
  /// buffer of the specified size. When committed, the buffer will be written
  /// to the file at the specified path.
  ///
  /// When F_modify is specified and \p FilePath refers to an existing on-disk
  /// file \p Size may be set to -1, in which case the entire file is used.
  /// Otherwise, the file shrinks or grows as necessary based on the value of
  /// \p Size.  It is an error to specify F_modify and Size=-1 if \p FilePath
  /// does not exist.
  static Expected<std::unique_ptr<FileOutputBuffer>>
  create(StringRef FilePath, size_t Size, unsigned Flags = 0);

  /// Returns a pointer to the start of the buffer.
  virtual uint8_t *getBufferStart() const = 0;

  /// Returns a pointer to the end of the buffer.
  virtual uint8_t *getBufferEnd() const = 0;

  /// Returns size of the buffer.
  virtual size_t getBufferSize() const = 0;

  /// Returns path where file will show up if buffer is committed.
  StringRef getPath() const { return FinalPath; }

  /// Flushes the content of the buffer to its file and deallocates the
  /// buffer.  If commit() is not called before this object's destructor
  /// is called, the file is deleted in the destructor. The optional parameter
  /// is used if it turns out you want the file size to be smaller than
  /// initially requested.
  virtual Error commit() = 0;

  /// If this object was previously committed, the destructor just deletes
  /// this object.  If this object was not committed, the destructor
  /// deallocates the buffer and the target file is never written.
  virtual ~FileOutputBuffer() = default;

  /// This removes the temporary file (unless it already was committed)
  /// but keeps the memory mapping alive.
  virtual void discard() {}

protected:
  FileOutputBuffer(StringRef Path) : FinalPath(Path) {}

  std::string FinalPath;
};
namespace detail {
class OnDiskBuffer : public FileOutputBuffer {
public:
  OnDiskBuffer(StringRef Path, sys::fs::TempFile Temp,
               sys::fs::mapped_file_region Buf)
      : FileOutputBuffer(Path), Buffer(std::move(Buf)), Temp(std::move(Temp)) {}
  uint8_t *getBufferStart() const override { return (uint8_t *)Buffer.data(); }
  uint8_t *getBufferEnd() const override {
    return (uint8_t *)Buffer.data() + Buffer.size();
  }
  size_t getBufferSize() const override { return Buffer.size(); }
  Error commit() override {
    TimeTraceScope timeScope("Commit buffer to disk");
    Buffer.unmap();
    return Temp.keep(FinalPath);
  }
  ~OnDiskBuffer() override {
    Buffer.unmap();
    consumeError(Temp.discard());
  }
  void discard() override { consumeError(Temp.discard()); }

private:
  sys::fs::mapped_file_region Buffer;
  sys::fs::TempFile Temp;
};
class InMemoryBuffer : public FileOutputBuffer {
public:
  InMemoryBuffer(StringRef Path, sys::MemoryBlock Buf, size_t BufSize,
                 unsigned Mode)
      : FileOutputBuffer(Path), Buffer(Buf), BufferSize(BufSize), Mode(Mode) {}
  uint8_t *getBufferStart() const override { return (uint8_t *)Buffer.base(); }
  uint8_t *getBufferEnd() const override {
    return (uint8_t *)Buffer.base() + BufferSize;
  }
  size_t getBufferSize() const override { return BufferSize; }
  Error commit() override {
    if (FinalPath == "-") {
      outs() << StringRef((const char *)Buffer.base(), BufferSize);
      outs().flush();
      return Error::success();
    }
    using namespace sys::fs;
    int FD;
    if (auto EC =
            openFileForWrite(FinalPath, FD, CD_CreateAlways, OF_None, Mode))
      return errorCodeToError(EC);
    raw_fd_ostream OS(FD, true, true);
    OS << StringRef((const char *)Buffer.base(), BufferSize);
    return Error::success();
  }

private:
  sys::OwningMemoryBlock Buffer;
  size_t BufferSize;
  unsigned Mode;
};

inline Expected<std::unique_ptr<InMemoryBuffer>>
createInMemoryBuffer(StringRef Path, size_t Size, unsigned Mode) {
  int EC;
  sys::MemoryBlock MB = sys::Memory::allocateMappedMemory(
      Size, 0, sys::Memory::MF_READ | sys::Memory::MF_WRITE, EC);
  if (EC)
    return errorCodeToError(std::error_code(EC, std::generic_category()));
  return std::unique_ptr<InMemoryBuffer>(
      new InMemoryBuffer(Path, MB, Size, Mode));
}

inline Expected<std::unique_ptr<FileOutputBuffer>>
createOnDiskBuffer(StringRef Path, size_t Size, unsigned Mode) {
  Expected<sys::fs::TempFile> FileOrErr =
      sys::fs::TempFile::create(Path + ".tmp%%%%%%%", Mode);
  if (!FileOrErr)
    return FileOrErr.takeError();
  sys::fs::TempFile File = std::move(*FileOrErr);
  if (auto EC = sys::fs::resize_file_before_mapping_readwrite(File.FD, Size)) {
    consumeError(File.discard());
    return errorCodeToError(EC);
  }
  std::error_code EC;
  sys::fs::mapped_file_region MappedFile = sys::fs::mapped_file_region(
      sys::fs::convertFDToNativeFile(File.FD),
      sys::fs::mapped_file_region::readwrite, Size, 0, EC);
  if (EC) {
    consumeError(File.discard());
    return createInMemoryBuffer(Path, Size, Mode);
  }
  return std::unique_ptr<OnDiskBuffer>(
      new OnDiskBuffer(Path, std::move(File), std::move(MappedFile)));
}
} // namespace detail

inline Expected<std::unique_ptr<FileOutputBuffer>>
FileOutputBuffer::create(StringRef Path, size_t Size, unsigned Flags) {
  if (Path == "-")
    return llvm::detail::createInMemoryBuffer("-", Size, 0);
  unsigned Mode = sys::fs::all_read | sys::fs::all_write;
  if (Flags & F_executable)
    Mode |= sys::fs::all_exe;
  if (Size == 0)
    return llvm::detail::createInMemoryBuffer(Path, Size, Mode);
  sys::fs::file_status Stat;
  sys::fs::status(Path, Stat);
  switch (Stat.type()) {
  case sys::fs::file_type::directory_file:
    return errorCodeToError(make_error_code(errc::is_a_directory));
  case sys::fs::file_type::regular_file:
  case sys::fs::file_type::file_not_found:
  case sys::fs::file_type::status_error:
    if (Flags & F_no_mmap)
      return llvm::detail::createInMemoryBuffer(Path, Size, Mode);
    return llvm::detail::createOnDiskBuffer(Path, Size, Mode);
  default:
    return llvm::detail::createInMemoryBuffer(Path, Size, Mode);
  }
}

} // end namespace llvm

#endif
