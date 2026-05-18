//===- llvm/Support/FileUtilities.h - File System Utilities -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a family of utility functions which are useful for doing
// various things with files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FILEUTILITIES_H
#define LLVM_SUPPORT_FILEUTILITIES_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"

#include "csupport/cpp_compat_stl.h"
#include "csupport/lprocess.h"
#include <system_error>

namespace llvm {

/// DiffFilesWithTolerance - Compare the two files specified, returning 0 if
/// the files match, 1 if they are different, and 2 if there is a file error.
/// This function allows you to specify an absolute and relative FP error that
/// is allowed to exist.  If you specify a string to fill in for the error
/// option, it will set the string to an error message if an error occurs, or
/// if the files are different.
///
int DiffFilesWithTolerance(StringRef FileA, StringRef FileB, double AbsTol,
                           double RelTol,
                           SmallVectorImpl<char> *Error = nullptr);

/// FileRemover - This class is a simple object meant to be stack allocated.
/// If an exception is thrown from a region, the object removes the filename
/// specified (if deleteIt is true).
///
class FileRemover {
  SmallString<128> Filename;
  bool DeleteIt;

public:
  FileRemover() : DeleteIt(false) {}

  explicit FileRemover(const Twine &filename, bool deleteIt = true)
      : DeleteIt(deleteIt) {
    filename.toVector(Filename);
  }

  ~FileRemover() {
    if (DeleteIt) {
      // Ignore problems deleting the file.
      sys::fs::remove(Filename);
    }
  }

  /// setFile - Give ownership of the file to the FileRemover so it will
  /// be removed when the object is destroyed.  If the FileRemover already
  /// had ownership of a file, remove it first.
  void setFile(const Twine &filename, bool deleteIt = true) {
    if (DeleteIt) {
      // Ignore problems deleting the file.
      sys::fs::remove(Filename);
    }

    Filename.clear();
    filename.toVector(Filename);
    DeleteIt = deleteIt;
  }

  /// releaseFile - Take ownership of the file away from the FileRemover so it
  /// will not be removed when the object is destroyed.
  void releaseFile() { DeleteIt = false; }
};

/// FilePermssionsApplier helps to copy permissions from an input file to
/// an output one. It memorizes the status of the input file and can apply
/// permissions and dates to the output file.
class FilePermissionsApplier {
public:
  static Expected<FilePermissionsApplier> create(StringRef InputFilename);

  /// Apply stored permissions to the \p OutputFilename.
  /// Copy LastAccess and ModificationTime if \p CopyDates is true.
  /// Overwrite stored permissions if \p OverwritePermissions is specified.
  Error apply(StringRef OutputFilename, bool CopyDates = false,
              sys::fs::perms OverwritePermissions = sys::fs::perms(0xFFFF));

private:
  FilePermissionsApplier(StringRef InputFilename, sys::fs::file_status Status)
      : InputFilename(InputFilename), InputStatus(Status) {}

  StringRef InputFilename;
  sys::fs::file_status InputStatus;
};
} // namespace llvm

/*== Inline implementations (moved from cpp_bridge.cpp) ==*/

#include "csupport/lfile_lutilities.h"

#define isNumberChar(C) csupport_is_number_char(C)
#define BackupNumber(Pos, FirstChar) csupport_backup_number(Pos, FirstChar)
#define EndOfNumber(Pos) csupport_end_of_number(Pos)

inline static bool CompareNumbers(const char *&F1P, const char *&F2P,
                                  const char *F1End, const char *F2End,
                                  double AbsTolerance, double RelTolerance,
                                  llvm::SmallVectorImpl<char> *ErrorMsg) {
  double V1 = 0.0, V2 = 0.0;
  int rc = csupport_diff_numbers_tol_ex(&F1P, &F2P, F1End, F2End, AbsTolerance,
                                        RelTolerance, &V1, &V2);
  if (rc < 0) {
    if (ErrorMsg) {
      char buf[256];
      int n = snprintf(buf, sizeof(buf),
                       "FP Comparison failed, not a numeric difference between "
                       "'%c' and '%c'",
                       F1P[0], F2P[0]);
      ErrorMsg->assign(buf, buf + n);
    }
    return true;
  }
  if (rc > 0) {
    if (ErrorMsg) {
      double da = V1 - V2;
      if (da < 0)
        da = -da;
      double dr;
      if (V2 != 0.0) {
        dr = V1 / V2 - 1.0;
        if (dr < 0)
          dr = -dr;
      } else if (V1 != 0.0) {
        dr = V2 / V1 - 1.0;
        if (dr < 0)
          dr = -dr;
      } else
        dr = 0.0;
      char buf[512];
      int n = snprintf(buf, sizeof(buf),
                       "Compared: %g and %g\nabs. diff = %g rel.diff = %g\n"
                       "Out of tolerance: rel/abs: %g/%g",
                       V1, V2, da, dr, RelTolerance, AbsTolerance);
      ErrorMsg->assign(buf, buf + n);
    }
    return true;
  }
  return false;
}

namespace llvm {

inline int DiffFilesWithTolerance(StringRef NameA, StringRef NameB,
                                  double AbsTol, double RelTol,
                                  SmallVectorImpl<char> *Error) {
  ErrorOr<uptr_t<MemoryBuffer>> F1OrErr = MemoryBuffer::getFile(NameA);
  if (errc_t EC = F1OrErr.getError()) {
    if (Error) {
      auto m = EC.message();
      Error->assign(m.begin(), m.end());
    }
    return 2;
  }
  MemoryBuffer &F1 = *F1OrErr.get();

  ErrorOr<uptr_t<MemoryBuffer>> F2OrErr = MemoryBuffer::getFile(NameB);
  if (errc_t EC = F2OrErr.getError()) {
    if (Error) {
      auto m = EC.message();
      Error->assign(m.begin(), m.end());
    }
    return 2;
  }
  MemoryBuffer &F2 = *F2OrErr.get();

  const char *File1Start = F1.getBufferStart();
  const char *File2Start = F2.getBufferStart();
  const char *File1End = F1.getBufferEnd();
  const char *File2End = F2.getBufferEnd();
  const char *F1P = File1Start;
  const char *F2P = File2Start;
  uint64_t A_size = F1.getBufferSize();
  uint64_t B_size = F2.getBufferSize();

  if (A_size == B_size && memcmp(File1Start, File2Start, A_size) == 0)
    return 0;

  if (AbsTol == 0 && RelTol == 0) {
    if (Error) {
      StringRef Msg = "Files differ without tolerance allowance";
      Error->assign(Msg.begin(), Msg.end());
    }
    return 1;
  }

  bool CompareFailed = false;
  while (true) {
    while (F1P < File1End && F2P < File2End && *F1P == *F2P) {
      ++F1P;
      ++F2P;
    }

    if (F1P >= File1End || F2P >= File2End)
      break;

    F1P = BackupNumber(F1P, File1Start);
    F2P = BackupNumber(F2P, File2Start);

    if (CompareNumbers(F1P, F2P, File1End, File2End, AbsTol, RelTol, Error)) {
      CompareFailed = true;
      break;
    }
  }

  bool F1AtEnd = F1P >= File1End;
  bool F2AtEnd = F2P >= File2End;
  if (!CompareFailed && (!F1AtEnd || !F2AtEnd)) {
    if (F1AtEnd && isNumberChar(F1P[-1]))
      --F1P;
    if (F2AtEnd && isNumberChar(F2P[-1]))
      --F2P;
    F1P = BackupNumber(F1P, File1Start);
    F2P = BackupNumber(F2P, File2Start);

    if (CompareNumbers(F1P, F2P, File1End, File2End, AbsTol, RelTol, Error))
      CompareFailed = true;

    if (F1P < File1End || F2P < File2End)
      CompareFailed = true;
  }

  return CompareFailed;
}

inline Expected<FilePermissionsApplier>
FilePermissionsApplier::create(StringRef InputFilename) {
  sys::fs::file_status Status;

  if (InputFilename != "-") {
    if (auto EC = sys::fs::status(InputFilename, Status))
      return createFileError(InputFilename, EC);
  } else {
    Status.permissions((sys::fs::perms)(0777));
  }

  return FilePermissionsApplier(InputFilename, Status);
}

inline Error
FilePermissionsApplier::apply(StringRef OutputFilename, bool CopyDates,
                              sys::fs::perms OverwritePermissions) {
  sys::fs::file_status Status = InputStatus;

  if (OverwritePermissions != sys::fs::perms(0xFFFF))
    Status.permissions(OverwritePermissions);

  int FD = 0;

  if (OutputFilename == "-")
    return Error::success();

  if (errc_t EC = sys::fs::openFileForWrite(OutputFilename, FD,
                                            sys::fs::CD_OpenExisting))
    return createFileError(OutputFilename, EC);

  if (CopyDates)
    if (errc_t EC = sys::fs::setLastAccessAndModificationTime(
            FD, Status.getLastAccessedTime(), Status.getLastModificationTime()))
      return createFileError(OutputFilename, EC);

  sys::fs::file_status OStat;
  if (errc_t EC = sys::fs::status(FD, OStat))
    return createFileError(OutputFilename, EC);
  if (OStat.type() == sys::fs::file_type::regular_file) {
#ifndef _WIN32
    if (OutputFilename == InputFilename && OStat.getUser() == 0)
      sys::fs::changeFileOwnership(FD, Status.getUser(), Status.getGroup());
#endif

    sys::fs::perms Perm = Status.permissions();
    if (OutputFilename != InputFilename)
      Perm = (sys::fs::perms)(Perm & ~sys::fs::getUmask() & ~06000);
#ifdef _WIN32
    if (errc_t EC = sys::fs::setPermissions(OutputFilename, Perm))
#else
    if (errc_t EC = sys::fs::setPermissions(FD, Perm))
#endif
      return createFileError(OutputFilename, EC);
  }

  if (int ec = csupport_safely_close_fd(FD))
    return createFileError(OutputFilename, ec_errno(ec));

  return Error::success();
}

} // namespace llvm

#endif
