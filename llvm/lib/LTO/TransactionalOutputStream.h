//===- TransactionalOutputStream.h - Bounded LTO hook output ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_LTO_TRANSACTIONALOUTPUTSTREAM_H
#define LLVM_LIB_LTO_TRANSACTIONALOUTPUTSTREAM_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DiagnosticHandler.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

namespace llvm::lto::detail {

inline constexpr uint64_t DefaultTransactionalOutputMemoryLimit =
    UINT64_C(64) * 1024 * 1024;
inline constexpr uint64_t OwnedTransactionalOutputMemoryLimit =
    static_cast<uint64_t>(std::numeric_limits<size_t>::max());

class LLVM_LIBRARY_VISIBILITY TransactionalOutputStream final
    : public raw_pwrite_stream {
public:
  explicit TransactionalOutputStream(
      uint64_t MemoryLimit = DefaultTransactionalOutputMemoryLimit,
      StringRef TemporaryDirectory = {}, bool AllowSpill = true)
      : raw_pwrite_stream(/*Unbuffered=*/true), MemoryLimit(MemoryLimit),
        TemporaryDirectory(TemporaryDirectory), AllowSpill(AllowSpill) {}

  ~TransactionalOutputStream() override { consumeError(discard()); }

  bool failed() {
    finishFileWrites();
    return static_cast<bool>(Failure);
  }

  bool spilled() const { return static_cast<bool>(TemporaryFile); }

  const char *memoryDataForTesting() const { return Memory.data(); }

  size_t memoryCapacityForTesting() const { return Memory.capacity(); }

  template <typename StreamReadyT>
  Error publish(const AddStreamFn &AddStream, unsigned Task, const Module &Mod,
                StreamReadyT &&StreamReady) {
    finishFileWrites();
    if (Failure)
      return discard();

    std::optional<sys::fs::mapped_file_region> MappedSpill;
    StringRef Bytes;
    if (TemporaryFile) {
      if (Position > std::numeric_limits<size_t>::max())
        return joinErrors(
            createStringError(std::make_error_code(std::errc::file_too_large),
                              "transactional LTO output spool is too large to "
                              "map"),
            discard());
      std::error_code MapError;
      MappedSpill.emplace(sys::fs::convertFDToNativeFile(TemporaryFile->FD),
                          sys::fs::mapped_file_region::readonly,
                          static_cast<size_t>(Position),
                          /*Offset=*/0, MapError);
      if (MapError)
        return joinErrors(
            createStringError(MapError,
                              "failed to map transactional LTO output spool"),
            discard());
      Bytes = StringRef(MappedSpill->const_data(), MappedSpill->size());
      // The mapping remains valid after the descriptor is closed and the
      // directory entry is removed. Finish every fallible spool operation
      // before AddStream can make an output/cache entry externally visible.
      if (Error CleanupError = discard())
        return CleanupError;
    } else {
      Bytes = StringRef(Memory.data(), Memory.size());
    }

    Expected<std::unique_ptr<CachedFileStream>> StreamOrErr =
        AddStream(Task, Mod.getModuleIdentifier());
    if (!StreamOrErr)
      return joinErrors(StreamOrErr.takeError(), discard());
    std::unique_ptr<CachedFileStream> Stream = std::move(*StreamOrErr);
    std::forward<StreamReadyT>(StreamReady)(*Stream);
    Stream->OS->write(Bytes.data(), Bytes.size());
    Stream->OS->flush();
    releaseMemoryStorage();
    return Error::success();
  }

  Error publish(const AddStreamFn &AddStream, unsigned Task,
                const Module &Mod) {
    return publish(AddStream, Task, Mod, [](const CachedFileStream &) {});
  }

  Error publishOwned(const AddOwnedOutputFn &AddOutput, unsigned Task,
                     const Module &Mod) {
    finishFileWrites();
    if (Failure)
      return discard();
    if (TemporaryFile)
      return joinErrors(
          createStringError(inconvertibleErrorCode(),
                            "owned transactional LTO output unexpectedly "
                            "used a temporary file"),
          discard());

    Position = 0;
    SmallVector<char, 0> Bytes = std::move(Memory);
    return AddOutput(Task, Mod.getModuleIdentifier(), std::move(Bytes));
  }

  Error discard() {
    finishFileWrites();
    Error Result = takeFailure();
    FileStream.reset();
    if (TemporaryFile) {
      Error DiscardError = TemporaryFile->discard();
      TemporaryFile.reset();
      if (DiscardError) {
        std::error_code EC = errorToErrorCode(std::move(DiscardError));
        Result = joinErrors(
            std::move(Result),
            createStringError(
                EC, "failed to remove transactional LTO output spool"));
      }
    }
    releaseMemoryStorage();
    return Result;
  }

  void reserveExtraSpace(uint64_t ExtraSize) override {
    if (TemporaryFile || Failure || Position > MemoryLimit ||
        ExtraSize > MemoryLimit - Position)
      return;
    const uint64_t Desired = Position + ExtraSize;
    if (Desired <= std::numeric_limits<size_t>::max())
      Memory.reserve(static_cast<size_t>(Desired));
  }

private:
  void write_impl(const char *Ptr, size_t Size) override {
    if (Failure || Size == 0)
      return;
    if (Size > std::numeric_limits<uint64_t>::max() - Position) {
      Failure = std::make_error_code(std::errc::file_too_large);
      return;
    }
    const uint64_t End = Position + Size;
    if (!TemporaryFile && End <= MemoryLimit) {
      if (End > std::numeric_limits<size_t>::max()) {
        Failure = std::make_error_code(std::errc::file_too_large);
        return;
      }
      Memory.append(Ptr, Ptr + Size);
      Position = End;
      return;
    }
    if (!TemporaryFile && !AllowSpill) {
      Failure = std::make_error_code(std::errc::file_too_large);
      return;
    }
    if (!TemporaryFile && !spillToFile())
      return;
    FileStream->write(Ptr, Size);
    if (FileStream->has_error()) {
      Failure = FileStream->error();
      FileStream->clear_error();
      return;
    }
    Position = End;
  }

  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override {
    if (Failure || Size == 0)
      return;
    // raw_pwrite_stream::pwrite permits callers to patch bytes that are still
    // in the base-class buffer. Materialize them before validating the range.
    flush();
    if (Failure)
      return;
    if (Offset > Position || Size > Position - Offset) {
      Failure = std::make_error_code(std::errc::invalid_argument);
      return;
    }
    if (!TemporaryFile) {
      std::memcpy(Memory.data() + Offset, Ptr, Size);
      return;
    }
    FileStream->pwrite(Ptr, Size, Offset);
    if (FileStream->has_error()) {
      Failure = FileStream->error();
      FileStream->clear_error();
    }
  }

  uint64_t current_pos() const override { return Position; }

  bool spillToFile() {
    SmallString<256> Model;
    if (TemporaryDirectory.empty())
      sys::path::system_temp_directory(/*ErasedOnReboot=*/true, Model);
    else
      Model = TemporaryDirectory;
    sys::path::append(Model, "neverc-lto-output-%%%%%%.tmp");
    Expected<sys::fs::TempFile> Created = sys::fs::TempFile::create(
        Model, sys::fs::owner_read | sys::fs::owner_write);
    if (!Created) {
      Failure = errorToErrorCode(Created.takeError());
      return false;
    }
    TemporaryFile.emplace(std::move(*Created));
    FileStream = std::make_unique<raw_fd_ostream>(
        TemporaryFile->FD, /*ShouldClose=*/false, /*Unbuffered=*/true);
    FileStream->write(Memory.data(), Memory.size());
    if (FileStream->has_error()) {
      Failure = FileStream->error();
      FileStream->clear_error();
      return false;
    }
    releaseMemoryStorage();
    return true;
  }

  void releaseMemoryStorage() {
    if (Memory.capacity() == 0)
      return;
    // Swapping a remote SmallVector with an empty N=0 vector copies the whole
    // payload and leaves the original allocation retained. Force the N=0 move
    // steal path instead so decline/error cleanup cannot allocate or copy.
    if (Memory.empty())
      Memory.push_back(0);
    SmallVector<char, 0> Retired(std::move(Memory));
    assert(Memory.empty() && Memory.capacity() == 0 &&
           "transactional output storage must be released after move");
  }

  void finishFileWrites() {
    // Hooks receive this as a raw_pwrite_stream and may enable its base-class
    // buffer. Drain that buffer through write_impl before finalizing a spill.
    flush();
    if (!FileStream)
      return;
    FileStream->flush();
    if (FileStream->has_error()) {
      Failure = FileStream->error();
      FileStream->clear_error();
    }
  }

  Error takeFailure() {
    if (!Failure)
      return Error::success();
    std::error_code EC = Failure;
    Failure.clear();
    return createStringError(EC, "transactional LTO output spool failed");
  }

  const uint64_t MemoryLimit;
  const SmallString<256> TemporaryDirectory;
  const bool AllowSpill;
  SmallVector<char, 0> Memory;
  std::optional<sys::fs::TempFile> TemporaryFile;
  std::unique_ptr<raw_fd_ostream> FileStream;
  uint64_t Position = 0;
  std::error_code Failure;
};

enum class TransactionalHookResult { Accepted, Declined, ReportedError };

inline bool hasReportedError(const Module &Mod) {
  const DiagnosticHandler *Handler = Mod.getContext().getDiagHandlerPtr();
  return Handler && Handler->HasErrors;
}

template <typename HookT>
Expected<TransactionalHookResult> runTransactionalOutputHook(
    const AddStreamFn &AddStream, unsigned Task, Module &Mod, HookT &&Hook,
    uint64_t MemoryLimit = DefaultTransactionalOutputMemoryLimit,
    StringRef TemporaryDirectory = {}) {
  TransactionalOutputStream Output(MemoryLimit, TemporaryDirectory);
  const bool Accepted = std::forward<HookT>(Hook)(Output);
  if (Output.failed())
    return Output.discard();
  if (hasReportedError(Mod)) {
    if (Error CleanupError = Output.discard())
      return CleanupError;
    return TransactionalHookResult::ReportedError;
  }
  if (!Accepted) {
    if (Error CleanupError = Output.discard())
      return CleanupError;
    return TransactionalHookResult::Declined;
  }
  if (Error PublishError = Output.publish(AddStream, Task, Mod))
    return PublishError;
  return TransactionalHookResult::Accepted;
}

template <typename HookT>
Expected<TransactionalHookResult> runTransactionalOwnedOutputHook(
    const AddOwnedOutputFn &AddOutput, unsigned Task, Module &Mod,
    HookT &&Hook) {
  TransactionalOutputStream Output(OwnedTransactionalOutputMemoryLimit,
                                   /*TemporaryDirectory=*/{},
                                   /*AllowSpill=*/false);
  const bool Accepted = std::forward<HookT>(Hook)(Output);
  if (Output.failed())
    return Output.discard();
  if (hasReportedError(Mod)) {
    if (Error CleanupError = Output.discard())
      return CleanupError;
    return TransactionalHookResult::ReportedError;
  }
  if (!Accepted) {
    if (Error CleanupError = Output.discard())
      return CleanupError;
    return TransactionalHookResult::Declined;
  }
  if (Error PublishError = Output.publishOwned(AddOutput, Task, Mod))
    return PublishError;
  return TransactionalHookResult::Accepted;
}

} // namespace llvm::lto::detail

#endif // LLVM_LIB_LTO_TRANSACTIONALOUTPUTSTREAM_H
