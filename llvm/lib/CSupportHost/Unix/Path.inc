//===- llvm/Support/Unix/Path.inc - Unix Path Implementation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Unix specific implementation of the Path API.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX code that
//===          is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

#include "Unix/Unix.h"
#include "llvm/Support/Errc.h"
#include <limits.h>
#include <stdio.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
/* Headers for dirent/pwd/sys/file/mmap/copyfile/dyld/clonefile/FreeBSD-elf
 * removed — all now in CSupport/Path.c which uses them directly. */

#ifdef __MVS__
#include "llvm/Support/AutoConvert.h"
#include <sys/ps.h>
#endif

// Both stdio.h and cstdio are included via different paths and
// stdcxx's cstdio doesn't include stdio.h, so it doesn't #undef the macros
// either.
#undef ferror
#undef feof

#if !defined(PATH_MAX)
// For GNU Hurd
#if defined(__GNU__)
#define PATH_MAX 4096
#elif defined(__MVS__)
#define PATH_MAX _XOPEN_PATH_MAX
#endif
#endif

/* STATVFS/FSTATVFS macros and FS-type headers removed — now in CSupport/Path.c */

#include <sys/types.h>

#include "csupport/lprocess.h"
#include "csupport/lpath.h"

namespace llvm {
namespace sys {
namespace fs {

using llvm::errc;

const file_t kInvalidFile = -1;

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) ||      \
    defined(__FreeBSD_kernel__) || defined(__linux__) || defined(__CYGWIN__) || \
    defined(__DragonFly__) || defined(_AIX) || defined(__GNU__) ||              \
    (defined(__sun__) && defined(__svr4__) || defined(__HAIKU__))
/* test_dir/getprogpath moved to CSupport/Path.c → csupport_test_dir/csupport_getprogpath */
static int test_dir(char ret[PATH_MAX], const char *dir, const char *bin) {
  return csupport_test_dir(ret, PATH_MAX, dir, bin);
}
static char *getprogpath(char ret[PATH_MAX], const char *bin) {
  return csupport_getprogpath(ret, PATH_MAX, bin);
}
#endif // __FreeBSD__ || __NetBSD__ || __FreeBSD_kernel__

/* getMainExecutable moved to CSupport/Path.c → csupport_get_main_executable_{apple,unix}() */
std::string getMainExecutable(const char *argv0, void *MainAddr) {
  char path_buf[PATH_MAX];
  size_t len = 0;
#if defined(__APPLE__)
  len = csupport_get_main_executable_apple(path_buf, sizeof(path_buf));
#elif defined(__MVS__)
  int token = 0;
  W_PSPROC buf;
  char exe_path[PS_PATHBLEN];
  pid_t pid = getpid();
  memset(&buf, 0, sizeof(buf));
  buf.ps_pathptr = exe_path;
  buf.ps_pathlen = sizeof(exe_path);
  while (true) {
    if ((token = w_getpsent(token, &buf, sizeof(buf))) <= 0)
      break;
    if (buf.ps_pid != pid)
      continue;
    char real_path[PATH_MAX];
    if (realpath(exe_path, real_path))
      return std::string(real_path);
    break;
  }
#else
  len = csupport_get_main_executable_unix(argv0, MainAddr, path_buf,
                                          sizeof(path_buf));
#endif
  if (len > 0)
    return std::string(path_buf, len);
  return "";
}

TimePoint<> basic_file_status::getLastAccessedTime() const {
  return toTimePoint(fs_st_atime, fs_st_atime_nsec);
}

TimePoint<> basic_file_status::getLastModificationTime() const {
  return toTimePoint(fs_st_mtime, fs_st_mtime_nsec);
}

UniqueID file_status::getUniqueID() const {
  return UniqueID(fs_st_dev, fs_st_ino);
}

uint32_t file_status::getLinkCount() const { return fs_st_nlinks; }

ErrorOr<space_info> disk_space(const Twine &Path) {
  space_info SpaceInfo;
  int err = csupport_disk_space(Path.str().c_str(), &SpaceInfo.capacity,
                                &SpaceInfo.free, &SpaceInfo.available);
  if (err) return std::error_code(err, std::generic_category());
  return SpaceInfo;
}

std::error_code current_path(SmallVectorImpl<char> &result) {
  result.resize_for_overwrite(PATH_MAX);
  size_t len = csupport_getcwd(result.data(), result.size());
  if (len == 0) {
    result.clear();
    return std::error_code(errno, std::generic_category());
  }
  result.truncate(len);
  return std::error_code();
}

std::error_code set_current_path(const Twine &path) {
  SmallString<128> path_storage;
  StringRef p = path.toNullTerminatedStringRef(path_storage);
  int err = csupport_chdir(p.begin());
  if (err) return std::error_code(err, std::generic_category());
  return std::error_code();
}

std::error_code create_directory(const Twine &path, bool IgnoreExisting,
                                 perms Perms) {
  SmallString<128> path_storage;
  StringRef p = path.toNullTerminatedStringRef(path_storage);
  int err = csupport_mkdir_p(p.begin(), Perms, IgnoreExisting ? 1 : 0);
  if (err) return std::error_code(err, std::generic_category());
  return std::error_code();
}

std::error_code create_link(const Twine &to, const Twine &from) {
  SmallString<128> from_storage, to_storage;
  StringRef f = from.toNullTerminatedStringRef(from_storage);
  StringRef t = to.toNullTerminatedStringRef(to_storage);
  int err = csupport_symlink_path(t.begin(), f.begin());
  if (err) return std::error_code(err, std::generic_category());
  return std::error_code();
}

std::error_code create_hard_link(const Twine &to, const Twine &from) {
  SmallString<128> from_storage, to_storage;
  StringRef f = from.toNullTerminatedStringRef(from_storage);
  StringRef t = to.toNullTerminatedStringRef(to_storage);
  int err = csupport_hardlink_path(t.begin(), f.begin());
  if (err) return std::error_code(err, std::generic_category());
  return std::error_code();
}

std::error_code remove(const Twine &path, bool IgnoreNonExisting) {
  SmallString<128> path_storage;
  StringRef p = path.toNullTerminatedStringRef(path_storage);
  int err = csupport_remove_path(p.begin(), IgnoreNonExisting ? 1 : 0);
  if (err < 0) return make_error_code(errc::operation_not_permitted);
  if (err > 0) return std::error_code(err, std::generic_category());
  return std::error_code();
}

/* is_local_impl moved to CSupport/Path.c → csupport_is_local_path/fd */
std::error_code is_local(const Twine &Path, bool &Result) {
  int r = csupport_is_local_path(Path.str().c_str());
  if (r < 0) return std::error_code(errno, std::generic_category());
  Result = (r != 0);
  return std::error_code();
}

std::error_code is_local(int FD, bool &Result) {
  int r = csupport_is_local_fd(FD);
  if (r < 0) return std::error_code(errno, std::generic_category());
  Result = (r != 0);
  return std::error_code();
}

std::error_code rename(const Twine &from, const Twine &to) {
  SmallString<128> from_storage, to_storage;
  StringRef f = from.toNullTerminatedStringRef(from_storage);
  StringRef t = to.toNullTerminatedStringRef(to_storage);
  int err = csupport_rename_path(f.begin(), t.begin());
  if (err) return std::error_code(err, std::generic_category());
  return std::error_code();
}

/* resize_file() moved to CSupport/Path.c → csupport_resize_file(), inline in FileSystem.h */

/* access moved to CSupport/Path.c → csupport_access_path */
std::error_code access(const Twine &Path, AccessMode Mode) {
  SmallString<128> PathStorage;
  StringRef P = Path.toNullTerminatedStringRef(PathStorage);
  int err = csupport_access_path(P.begin(), static_cast<int>(Mode));
  if (err == EACCES) return make_error_code(errc::permission_denied);
  if (err) return std::error_code(err, std::generic_category());
  return std::error_code();
}

bool can_execute(const Twine &Path) {
  return !access(Path, AccessMode::Execute);
}

bool equivalent(file_status A, file_status B) {
  assert(status_known(A) && status_known(B));
  return A.fs_st_dev == B.fs_st_dev && A.fs_st_ino == B.fs_st_ino;
}

std::error_code equivalent(const Twine &A, const Twine &B, bool &result) {
  file_status fsA, fsB;
  if (std::error_code ec = status(A, fsA))
    return ec;
  if (std::error_code ec = status(B, fsB))
    return ec;
  result = equivalent(fsA, fsB);
  return std::error_code();
}

/* expandTildeExpr moved to CSupport/Path.c → csupport_expand_tilde_full */
static void expandTildeExpr(SmallVectorImpl<char> &Path) {
  if (Path.empty() || Path[0] != '~')
    return;
  char out[PATH_MAX];
  size_t n = csupport_expand_tilde_full(Path.data(), Path.size(), out, sizeof(out));
  if (n == 0)
    return;
  Path.clear();
  Path.append(out, out + n);
}

void expand_tilde(const Twine &path, SmallVectorImpl<char> &dest) {
  dest.clear();
  if (path.isTriviallyEmpty())
    return;

  path.toVector(dest);
  expandTildeExpr(dest);
}

/* fillStatus/status moved to CSupport/Path.c → csupport_stat_path/fd */
static file_status csupport_to_file_status(const csupport_file_stat_t &s) {
  static const file_type map[] = {
    file_type::type_unknown, file_type::file_not_found,
    file_type::regular_file, file_type::directory_file,
    file_type::symlink_file, file_type::block_file,
    file_type::character_file, file_type::fifo_file,
    file_type::socket_file, file_type::status_error,
  };
  file_type ft = (s.type >= 0 && s.type <= 9) ? map[s.type] : file_type::type_unknown;
  return file_status(ft, static_cast<perms>(s.perms),
                     s.dev, s.nlinks, s.ino,
                     s.atime_sec, s.atime_nsec,
                     s.mtime_sec, s.mtime_nsec,
                     s.uid, s.gid, s.size);
}

std::error_code status(const Twine &Path, file_status &Result, bool Follow) {
  SmallString<128> PathStorage;
  StringRef P = Path.toNullTerminatedStringRef(PathStorage);
  csupport_file_stat_t st;
  int err = csupport_stat_path(P.begin(), Follow ? 1 : 0, &st);
  Result = csupport_to_file_status(st);
  if (err) return std::error_code(err, std::generic_category());
  return std::error_code();
}

std::error_code status(int FD, file_status &Result) {
  csupport_file_stat_t st;
  int err = csupport_stat_fd(FD, &st);
  Result = csupport_to_file_status(st);
  if (err) return std::error_code(err, std::generic_category());
  return std::error_code();
}

/* getUmask() moved to CSupport/Path.c → csupport_get_umask(), inline in FileSystem.h */

std::error_code setPermissions(const Twine &Path, perms Permissions) {
  SmallString<128> PathStorage;
  StringRef P = Path.toNullTerminatedStringRef(PathStorage);
  int err = csupport_chmod_path(P.begin(), Permissions);
  if (err) return std::error_code(err, std::generic_category());
  return std::error_code();
}

std::error_code setPermissions(int FD, perms Permissions) {
  int err = csupport_chmod_fd(FD, Permissions);
  if (err) return std::error_code(err, std::generic_category());
  return std::error_code();
}

/* setLastAccessAndModificationTime moved to CSupport/Path.c → csupport_set_file_times */
std::error_code setLastAccessAndModificationTime(int FD, TimePoint<> AccessTime,
                                                 TimePoint<> ModificationTime) {
  auto ATS = sys::toTimeSpec(AccessTime);
  auto MTS = sys::toTimeSpec(ModificationTime);
  int err = csupport_set_file_times(FD, ATS.tv_sec, ATS.tv_nsec,
                                    MTS.tv_sec, MTS.tv_nsec);
  if (err == ENOSYS) return make_error_code(errc::function_not_supported);
  if (err) return std::error_code(err, std::generic_category());
  return std::error_code();
}

/* mapped_file_region::init moved to CSupport/Path.c → csupport_mmap_file() */
std::error_code mapped_file_region::init(int FD, uint64_t Offset,
                                         mapmode Mode) {
  assert(Size != 0);
  int cmode = (Mode == readonly) ? 0 : 1;
  Mapping = csupport_mmap_file(Size, cmode, FD, Offset);
  if (!Mapping)
    return std::error_code(errno, std::generic_category());
  return std::error_code();
}

mapped_file_region::mapped_file_region(int fd, mapmode mode, size_t length,
                                       uint64_t offset, std::error_code &ec)
    : Size(length), Mode(mode) {
  (void)Mode;
  ec = init(fd, offset, mode);
  if (ec)
    copyFrom(mapped_file_region());
}

/* unmapImpl/dontNeedImpl moved to CSupport/Path.c → csupport_munmap/madvise_dontneed */
void mapped_file_region::unmapImpl() { csupport_munmap(Mapping, Size); }

void mapped_file_region::dontNeedImpl() {
  assert(Mode == mapped_file_region::readonly);
  csupport_madvise_dontneed(Mapping, Size);
}

int mapped_file_region::alignment() { return (int)csupport_get_page_size(); }

/* directory_iterator moved to CSupport/Path.c → csupport_opendir/readdir_entry/closedir */
std::error_code detail::directory_iterator_construct(detail::DirIterState &it,
                                                     StringRef path,
                                                     bool follow_symlinks) {
  SmallString<128> path_null(path);
  void *handle = csupport_opendir(path_null.c_str());
  if (!handle)
    return std::error_code(errno, std::generic_category());

  it.IterationHandle = reinterpret_cast<intptr_t>(handle);
  path::append(path_null, ".");
  it.CurrentEntry = directory_entry(path_null.str(), follow_symlinks);
  return directory_iterator_increment(it);
}

std::error_code detail::directory_iterator_destruct(detail::DirIterState &it) {
  if (it.IterationHandle)
    csupport_closedir(reinterpret_cast<void *>(it.IterationHandle));
  it.IterationHandle = 0;
  it.CurrentEntry = directory_entry();
  return std::error_code();
}

std::error_code detail::directory_iterator_increment(detail::DirIterState &It) {
  static const file_type type_map[] = {
    file_type::type_unknown, file_type::file_not_found,
    file_type::regular_file, file_type::directory_file,
    file_type::symlink_file, file_type::block_file,
    file_type::character_file, file_type::fifo_file,
    file_type::socket_file, file_type::status_error,
  };
  char name_buf[512];
  int ftype = 0;
  int rc = csupport_readdir_entry(reinterpret_cast<void *>(It.IterationHandle),
                                  name_buf, sizeof(name_buf), &ftype);
  if (rc < 0) return std::error_code(errno, std::generic_category());
  if (rc == 0) return directory_iterator_destruct(It);

  StringRef Name(name_buf);
  if ((Name.size() == 1 && Name[0] == '.') ||
      (Name.size() == 2 && Name[0] == '.' && Name[1] == '.'))
    return directory_iterator_increment(It);
  file_type ft = (ftype >= 0 && ftype <= 9) ? type_map[ftype]
                                             : file_type::type_unknown;
  It.CurrentEntry.replace_filename(Name, ft);
  return std::error_code();
}

ErrorOr<basic_file_status> directory_entry::status() const {
  file_status s;
  if (auto EC = fs::status(Path, s, FollowSymlinks))
    return EC;
  return s;
}

/* nativeOpenFlags moved to CSupport/Path.c → csupport_native_open_flags() */
static int nativeOpenFlags(CreationDisposition Disp, OpenFlags Flags,
                           FileAccess Access) {
  return csupport_native_open_flags(static_cast<int>(Disp),
                                    static_cast<int>(Flags),
                                    static_cast<int>(Access));
}

std::error_code openFile(const Twine &Name, int &ResultFD,
                         CreationDisposition Disp, FileAccess Access,
                         OpenFlags Flags, unsigned Mode) {
  int OpenFlags = nativeOpenFlags(Disp, Flags, Access);

  SmallString<128> Storage;
  StringRef P = Name.toNullTerminatedStringRef(Storage);
  ResultFD = csupport_open_file_retry(P.begin(), OpenFlags, Mode);
  if (ResultFD < 0)
    return std::error_code(errno, std::generic_category());

#ifdef __MVS__
  /* Reason about auto-conversion and file tags. Setting the file tag only
   * applies if file is opened in write mode:
   *
   * Text file:
   *                  File exists       File created
   * CD_CreateNew     n/a               conv: on
   *                                    tag: set 1047
   * CD_CreateAlways  conv: auto        conv: on
   *                  tag: auto 1047    tag: set 1047
   * CD_OpenAlways    conv: auto        conv: on
   *                  tag: auto 1047    tag: set 1047
   * CD_OpenExisting  conv: auto        n/a
   *                  tag: unchanged
   *
   * Binary file:
   *                  File exists       File created
   * CD_CreateNew     n/a               conv: off
   *                                    tag: set binary
   * CD_CreateAlways  conv: off         conv: off
   *                  tag: auto binary  tag: set binary
   * CD_OpenAlways    conv: off         conv: off
   *                  tag: auto binary  tag: set binary
   * CD_OpenExisting  conv: off         n/a
   *                  tag: unchanged
   *
   * Actions:
   *   conv: off        -> auto-conversion is turned off
   *   conv: on         -> auto-conversion is turned on
   *   conv: auto       -> auto-conversion is turned on if the file is untagged
   *   tag: set 1047    -> set the file tag to text encoded in 1047
   *   tag: set binary  -> set the file tag to binary
   *   tag: auto 1047   -> set file tag to 1047 if not set
   *   tag: auto binary -> set file tag to binary if not set
   *   tag: unchanged   -> do not care about the file tag
   *
   * It is not possible to distinguish between the cases "file exists" and
   * "file created". In the latter case, the file tag is not set and the file
   * size is zero. The decision table boils down to:
   *
   * the file tag is set if
   *   - the file is opened for writing
   *   - the create disposition is not equal to CD_OpenExisting
   *   - the file tag is not set
   *   - the file size is zero
   *
   * This only applies if the file is a regular file. E.g. enabling
   * auto-conversion for reading from /dev/null results in error EINVAL when
   * calling read().
   *
   * Using append mode with z/OS UTF-8 auto-conversion results in EINVAL when
   * calling write(). Instead we need to use lseek() to set offset to EOF after
   * open().
   */
  if ((Flags & OF_Append) && lseek(ResultFD, 0, SEEK_END) == -1)
    return std::error_code(errno, std::generic_category());
  struct stat Stat;
  if (fstat(ResultFD, &Stat) == -1)
    return std::error_code(errno, std::generic_category());
  if (S_ISREG(Stat.st_mode)) {
    bool DoSetTag = (Access & FA_Write) && (Disp != CD_OpenExisting) &&
                    !Stat.st_tag.ft_txtflag && !Stat.st_tag.ft_ccsid &&
                    Stat.st_size == 0;
    if (Flags & OF_Text) {
      if (int ecv = llvm::enableAutoConversion(ResultFD))
        return std::error_code(ecv, std::generic_category());
      if (DoSetTag) {
        if (int ecv = llvm::setFileTag(ResultFD, CCSID_IBM_1047, true))
          return std::error_code(ecv, std::generic_category());
      }
    } else {
      if (int ecv = llvm::disableAutoConversion(ResultFD))
        return std::error_code(ecv, std::generic_category());
      if (DoSetTag) {
        if (int ecv = llvm::setFileTag(ResultFD, FT_BINARY, false))
          return std::error_code(ecv, std::generic_category());
      }
    }
  }
#endif

  return std::error_code();
}

Expected<int> openNativeFile(const Twine &Name, CreationDisposition Disp,
                             FileAccess Access, OpenFlags Flags,
                             unsigned Mode) {

  int FD;
  std::error_code EC = openFile(Name, FD, Disp, Access, Flags, Mode);
  if (EC)
    return errorCodeToError(EC);
  return FD;
}

std::error_code openFileForRead(const Twine &Name, int &ResultFD,
                                OpenFlags Flags,
                                SmallVectorImpl<char> *RealPath) {
  std::error_code EC =
      openFile(Name, ResultFD, CD_OpenExisting, FA_Read, Flags, 0666);
  if (EC)
    return EC;

  if (!RealPath)
    return std::error_code();
  RealPath->clear();
  char Buffer[PATH_MAX];
  SmallString<128> Storage;
  StringRef P = Name.toNullTerminatedStringRef(Storage);
  size_t rlen = csupport_resolve_fd_path(ResultFD, P.begin(), Buffer, sizeof(Buffer));
  if (rlen > 0)
    RealPath->append(Buffer, Buffer + rlen);
  return std::error_code();
}

Expected<file_t> openNativeFileForRead(const Twine &Name, OpenFlags Flags,
                                       SmallVectorImpl<char> *RealPath) {
  file_t ResultFD;
  std::error_code EC = openFileForRead(Name, ResultFD, Flags, RealPath);
  if (EC)
    return errorCodeToError(EC);
  return ResultFD;
}

file_t getStdinHandle() { return 0; }
file_t getStdoutHandle() { return 1; }
file_t getStderrHandle() { return 2; }

/* readNativeFile moved to CSupport/Path.c → csupport_read_native/pread_native */
Expected<size_t> readNativeFile(file_t FD, MutableArrayRef<char> Buf) {
  ssize_t nr = 0;
  int err = csupport_read_native(FD, Buf.data(), Buf.size(), &nr);
  if (err) return errorCodeToError(std::error_code(err, std::generic_category()));
  return (size_t)nr;
}

Expected<size_t> readNativeFileSlice(file_t FD, MutableArrayRef<char> Buf,
                                     uint64_t Offset) {
  ssize_t nr = 0;
  int err = csupport_pread_native(FD, Buf.data(), Buf.size(), Offset, &nr);
  if (err) return errorCodeToError(std::error_code(err, std::generic_category()));
  return (size_t)nr;
}

/* File locking moved to CSupport/Path.c → csupport_lock_file/unlock/try */
std::error_code tryLockFile(int FD, std::chrono::milliseconds Timeout) {
  int err = csupport_try_lock_file(FD, (unsigned)Timeout.count());
  if (err == 0) return std::error_code();
  if (err == EAGAIN) return make_error_code(errc::no_lock_available);
  return std::error_code(err, std::generic_category());
}

std::error_code lockFile(int FD) {
  int err = csupport_lock_file(FD);
  if (err == 0) return std::error_code();
  return std::error_code(err, std::generic_category());
}

std::error_code unlockFile(int FD) {
  int err = csupport_unlock_file(FD);
  if (err == 0) return std::error_code();
  return std::error_code(err, std::generic_category());
}

std::error_code closeFile(file_t &F) {
  file_t TmpF = F;
  F = kInvalidFile;
  int ec = csupport_safely_close_fd(TmpF);
  if (ec)
    return std::error_code(ec, std::generic_category());
  return std::error_code();
}

std::error_code remove_directories(const Twine &path, bool IgnoreErrors) {
  SmallString<128> path_storage;
  StringRef p = path.toNullTerminatedStringRef(path_storage);
  int err = csupport_remove_directories_recursive(p.begin(), IgnoreErrors ? 1 : 0);
  if (err == 0)
    return std::error_code();
  return std::error_code(err, std::generic_category());
}

/* real_path core moved to CSupport/Path.c → csupport_realpath */
std::error_code real_path(const Twine &path, SmallVectorImpl<char> &dest,
                          bool expand_tilde) {
  dest.clear();
  if (path.isTriviallyEmpty())
    return std::error_code();

  if (expand_tilde) {
    SmallString<128> Storage;
    path.toVector(Storage);
    expandTildeExpr(Storage);
    return real_path(Storage, dest, false);
  }

  SmallString<128> Storage;
  StringRef P = path.toNullTerminatedStringRef(Storage);
  char Buffer[PATH_MAX];
  size_t len = csupport_realpath(P.begin(), Buffer, sizeof(Buffer));
  if (len == 0)
    return std::error_code(errno, std::generic_category());
  dest.append(Buffer, Buffer + len);
  return std::error_code();
}

std::error_code changeFileOwnership(int FD, uint32_t Owner, uint32_t Group) {
  int err = csupport_fchown_fd(FD, Owner, Group);
  if (err) return std::error_code(err, std::generic_category());
  return std::error_code();
}

} // end namespace fs

namespace path {

/* home_directory core moved to CSupport/Path.c → csupport_get_home_dir() */
bool home_directory(SmallVectorImpl<char> &result) {
  char buf[PATH_MAX];
  size_t len = csupport_get_home_dir(buf, sizeof(buf));
  if (len == 0)
    return false;
  result.clear();
  result.append(buf, buf + len);
  return true;
}

/* getDarwinConfDir removed — now called only from csupport_cache_directory/csupport_system_temp_directory in Path.c */

/* user_config_directory moved to CSupport/Path.c → csupport_user_config_directory */
bool user_config_directory(SmallVectorImpl<char> &result) {
  char buf[PATH_MAX];
  size_t len = csupport_user_config_directory(buf, sizeof(buf));
  if (len == 0) return false;
  result.clear();
  result.append(buf, buf + len);
  return true;
}

/* cache_directory moved to CSupport/Path.c → csupport_cache_directory */
bool cache_directory(SmallVectorImpl<char> &result) {
  char buf[PATH_MAX];
  size_t len = csupport_cache_directory(buf, sizeof(buf));
  if (len == 0) return false;
  result.clear();
  result.append(buf, buf + len);
  return true;
}

/* system_temp_directory moved to CSupport/Path.c → csupport_system_temp_directory */
void system_temp_directory(bool ErasedOnReboot, SmallVectorImpl<char> &Result) {
  char buf[PATH_MAX];
  size_t len = csupport_system_temp_directory(ErasedOnReboot ? 1 : 0,
                                              buf, sizeof(buf));
  Result.clear();
  if (len > 0)
    Result.append(buf, buf + len);
}

} // end namespace path

namespace fs {

/* Apple copy_file moved to CSupport/Path.c → csupport_copy_file_apfs() */
#ifdef __APPLE__
std::error_code copy_file(const Twine &From, const Twine &To) {
  std::string FromS = From.str();
  std::string ToS = To.str();
  int err = csupport_copy_file_apfs(FromS.c_str(), ToS.c_str());
  if (err == 0) return std::error_code();
  return std::error_code(err, std::generic_category());
}
#endif

} // end namespace fs

} // end namespace sys
} // end namespace llvm
