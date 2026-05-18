#ifndef CSUPPORT_LPATH_H
#define CSUPPORT_LPATH_H
#include <stddef.h>
#include <stdint.h>
#ifndef _WIN32
#include <sys/types.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif

enum csupport_path_style {
  CSUPPORT_PATH_NATIVE = 0,
  CSUPPORT_PATH_POSIX = 1,
  CSUPPORT_PATH_WINDOWS = 2,
  CSUPPORT_PATH_WINDOWS_SLASH = 3,
  CSUPPORT_PATH_WINDOWS_BACKSLASH = 4
};

size_t csupport_path_root_name_len(const char *path, size_t len);
size_t csupport_path_root_dir_pos(const char *path, size_t len);
int csupport_path_is_absolute(const char *path, size_t len);
int csupport_path_is_relative(const char *path, size_t len);
size_t csupport_path_parent_end(const char *path, size_t len);
size_t csupport_path_filename_pos(const char *path, size_t len);
size_t csupport_path_extension_pos(const char *path, size_t len);
size_t csupport_path_stem_end(const char *path, size_t len);
size_t csupport_path_append(char *buf, size_t buflen, size_t cur_len,
                            const char *component, size_t comp_len);
void csupport_path_replace_extension(char *buf, size_t *len, size_t buflen,
                                     const char *ext, size_t ext_len);
void csupport_path_native(char *buf, size_t len);

int csupport_path_is_separator(char c, int style);
size_t csupport_path_find_first_component(const char *path, size_t len,
                                          int style);
size_t csupport_path_filename_pos_styled(const char *path, size_t len,
                                         int style);
size_t csupport_path_root_dir_start(const char *path, size_t len, int style);
size_t csupport_path_parent_path_end(const char *path, size_t len, int style);

void csupport_path_convert_backslash(char *buf, size_t len);

/* Check if path has the given extension (case-insensitive) */
int csupport_path_has_extension(const char *path, size_t len, const char *ext,
                                size_t ext_len);

/* Remove . and .. components from path in-place. Returns new length. */
size_t csupport_path_remove_dots(char *buf, size_t len, int remove_dot_dot);

/* Join base and name with separator. Returns bytes written. */
size_t csupport_path_join(char *buf, size_t buflen, const char *base,
                          size_t base_len, const char *name, size_t name_len);

#define CSUPPORT_PATH_STYLE_NATIVE 0
#define CSUPPORT_PATH_STYLE_POSIX 1
#define CSUPPORT_PATH_STYLE_WINDOWS 2
#define CSUPPORT_PATH_STYLE_WINDOWS_SLASH 3
#define CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH 4

size_t csupport_path_find_first_component(const char *path, size_t len,
                                          int style);
size_t csupport_path_filename_pos_styled(const char *path, size_t len,
                                         int style);
size_t csupport_path_root_dir_start(const char *path, size_t len, int style);
size_t csupport_path_parent_path_end(const char *path, size_t len, int style);

size_t csupport_path_stem(const char *path, size_t len, const char **out_stem,
                          int style);
size_t csupport_path_extension(const char *path, size_t len,
                               const char **out_ext, int style);
size_t csupport_path_remove_leading_dotslash(const char *path, size_t len,
                                             const char **out, int style);
int csupport_path_starts_with_insensitive(const char *path, size_t path_len,
                                          const char *prefix, size_t prefix_len,
                                          int style);
size_t csupport_path_expand_tilde(const char *path, size_t len, char *buf,
                                  size_t buflen);

void csupport_path_make_preferred(char *buf, size_t len, int style);
size_t csupport_path_replace_extension_buf(char *buf, size_t len,
                                           const char *ext, size_t ext_len,
                                           int style);
int csupport_path_is_absolute_styled(const char *path, size_t len, int style);
size_t csupport_path_remove_dots_buf(const char *path, size_t len,
                                     int remove_dot_dot, int style, char *out,
                                     size_t out_cap);
size_t csupport_path_native_buf(const char *path, size_t len, char *out,
                                size_t out_cap, int style);

int csupport_path_get_existing_style(const char *path, size_t len);
int csupport_path_is_traversal_component(const char *comp, size_t len);
int csupport_path_has_traversal(const char *path, size_t len, int style);
size_t csupport_path_canonicalize(const char *path, size_t len, char *out,
                                  size_t out_cap);

size_t csupport_path_append_styled(char *base, size_t base_len, size_t base_cap,
                                   const char *component, size_t comp_len,
                                   int style);
int csupport_path_is_relative_styled(const char *path, size_t len, int style);
size_t csupport_path_lexically_normal(const char *path, size_t len, char *buf,
                                      size_t buflen, int style);

size_t csupport_path_split_components(const char *path, size_t path_len,
                                      const char **comps, size_t *comp_lens,
                                      size_t max_comps, int style);
size_t csupport_path_join2(const char *a, size_t a_len, const char *b,
                           size_t b_len, char *buf, size_t buflen, int style);
size_t csupport_path_normalize_separators(const char *path, size_t len,
                                          char *buf, size_t buflen, int style);
int csupport_path_has_root_name(const char *path, size_t len, int style);
int csupport_path_is_root_path(const char *path, size_t len, int style);
size_t csupport_path_common_prefix(const char *a, size_t a_len, const char *b,
                                   size_t b_len, int style);
size_t csupport_path_relative(const char *path, size_t path_len,
                              const char *base, size_t base_len, char *buf,
                              size_t buflen, int style);
int csupport_path_is_dotfile(const char *path, size_t len, int style);
int csupport_path_matches_extension(const char *path, size_t path_len,
                                    const char *ext, size_t ext_len, int style);

size_t csupport_path_get_parent(const char *path, size_t len);
size_t csupport_path_remove_trailing_sep(const char *path, size_t len);
int csupport_path_is_network_path(const char *path, size_t len);
size_t csupport_path_system_temp_dir(char *buf, size_t cap);
size_t csupport_path_replace_filename(const char *path, size_t path_len,
                                      const char *new_name, size_t name_len,
                                      char *buf, size_t cap);
size_t csupport_path_remove_filename(const char *path, size_t len, char *buf,
                                     size_t cap);

int csupport_path_is_separator(char c, int style);
size_t csupport_path_count_components(const char *path, size_t len, int style);
size_t csupport_path_get_component(const char *path, size_t len, unsigned index,
                                   int style, const char **comp_start);

int csupport_path_is_valid_component(const char *comp, size_t len);
size_t csupport_path_make_absolute_buf(const char *cwd, size_t cwd_len,
                                       const char *path, size_t path_len,
                                       char *buf, size_t cap, int style);
int csupport_path_is_hidden(const char *path, size_t len);

size_t csupport_path_lexically_relative(const char *path, size_t path_len,
                                        const char *base, size_t base_len,
                                        char sep, char *out, size_t out_cap);

size_t csupport_path_append_component(const char *path, size_t path_len,
                                      const char *comp, size_t comp_len,
                                      char sep, char *out, size_t out_cap);

/* Check if name is "." or "..". */
int csupport_path_is_special_name(const char *name, size_t len);

/* Check if path has a parent directory. */
int csupport_path_has_parent(const char *path, size_t len, int style);

/* Strip trailing path separators, preserving root. */
size_t csupport_path_strip_trailing_separators(const char *path, size_t len,
                                               int style);

/* Check if char is a path separator for given style. */
int csupport_path_is_separator_char(char c, int style);

/* Get position of filename component start. */
size_t csupport_path_filename_only(const char *path, size_t len, int style);

/* Check if path ends with a separator. */
int csupport_path_ends_with_separator(const char *path, size_t len, int style);

/* Replace path prefix: if path starts with old_prefix, replace with new_prefix.
 */
size_t csupport_path_replace_path_prefix(const char *path, size_t path_len,
                                         const char *old_prefix, size_t old_len,
                                         const char *new_prefix, size_t new_len,
                                         char *buf, size_t cap, int style);

unsigned csupport_get_umask(void);
int csupport_resize_file(int fd, uint64_t size);

/* File locking (returns 0 on success, errno-value on failure) */
int csupport_lock_file(int fd);
int csupport_unlock_file(int fd);
int csupport_try_lock_file(int fd, unsigned timeout_ms);

/* Executable path helpers (returns path in buf, NULL on failure) */
int csupport_test_dir(char ret[], int pathmax, const char *dir,
                      const char *bin);
char *csupport_getprogpath(char ret[], int pathmax, const char *bin);

/* File type for POSIX mode_t (returns csupport_file_type_* integer) */
enum {
  CSUPPORT_FILE_TYPE_UNKNOWN = 0,
  CSUPPORT_FILE_TYPE_NOT_FOUND = 1,
  CSUPPORT_FILE_TYPE_REGULAR = 2,
  CSUPPORT_FILE_TYPE_DIRECTORY = 3,
  CSUPPORT_FILE_TYPE_SYMLINK = 4,
  CSUPPORT_FILE_TYPE_BLOCK = 5,
  CSUPPORT_FILE_TYPE_CHARACTER = 6,
  CSUPPORT_FILE_TYPE_FIFO = 7,
  CSUPPORT_FILE_TYPE_SOCKET = 8,
  CSUPPORT_FILE_TYPE_STATUS_ERROR = 9
};
int csupport_type_for_mode(unsigned mode);

/* Access mode conversion: 0=Exist->F_OK, 1=Write->W_OK, 2=Execute->R_OK|X_OK */
int csupport_convert_access_mode(int mode);

/* Open flag construction */
int csupport_native_open_flags(int disp, int flags, int access);

/* Temp directory helpers */
const char *csupport_get_env_temp_dir(void);
const char *csupport_get_default_temp_dir(int erased_on_reboot);

/* /proc/self/fd availability */
int csupport_has_proc_self_fd(void);

/* Signal alt stack setup */
void csupport_create_sig_alt_stack(void);

/* mmap helpers */
void csupport_munmap(void *addr, size_t len);
void csupport_madvise_dontneed(void *addr, size_t len);

/* Apple copy_file (returns 0 on success, errno on failure) */
int csupport_copy_file_apfs(const char *from, const char *to);

/* Apple findModulesAndOffsets for stack traces */
int csupport_find_modules_offsets_apple(void **stack_trace, int depth,
                                        const char **modules,
                                        intptr_t *offsets);

/* Apple getMainExecutable core (returns length, 0 on failure) */
size_t csupport_get_main_executable_apple(char *buf, size_t cap);

/* home directory (returns dir in buf, length; 0 on failure) */
size_t csupport_get_home_dir(char *buf, size_t cap);

/* Darwin conf dir (returns length, 0 on failure) */
size_t csupport_get_darwin_conf_dir(int temp_dir, char *buf, size_t cap);

/* mmap file region (returns mapped ptr; NULL+errno on failure; mode:
 * 0=readonly, 1=readwrite) */
void *csupport_mmap_file(size_t size, int mode, int fd, uint64_t offset);

/* --- Session 10: batch POSIX extractions from Path.inc --- */

/* Simple POSIX wrappers (return 0 on success, errno on failure) */
int csupport_chdir(const char *path);
int csupport_mkdir_p(const char *path, unsigned perms, int ignore_existing);
int csupport_symlink_path(const char *target, const char *linkpath);
int csupport_hardlink_path(const char *target, const char *linkpath);
int csupport_rename_path(const char *from, const char *to);
int csupport_chmod_path(const char *path, unsigned perms);
int csupport_chmod_fd(int fd, unsigned perms);
int csupport_fchown_fd(int fd, uint32_t owner, uint32_t group);

/* remove: 0=success, positive=errno, -1=operation_not_permitted */
int csupport_remove_path(const char *path, int ignore_nonexisting);

/* access: 0=accessible, positive=errno */
int csupport_access_path(const char *path, int mode);

/* file times: 0 or errno */
int csupport_set_file_times(int fd, int64_t atime_sec, int32_t atime_nsec,
                            int64_t mtime_sec, int32_t mtime_nsec);

/* disk_space: 0 or errno */
int csupport_disk_space(const char *path, uint64_t *capacity,
                        uint64_t *free_space, uint64_t *available);

/* is_local: 1=local, 0=remote, -1=error(errno set) */
int csupport_is_local_path(const char *path);
int csupport_is_local_fd(int fd);

/* stat result struct */
typedef struct {
  int type;
  unsigned perms;
  uint64_t dev;
  uint32_t nlinks;
  uint64_t ino;
  int64_t atime_sec;
  uint32_t atime_nsec;
  int64_t mtime_sec;
  uint32_t mtime_nsec;
  uint32_t uid;
  uint32_t gid;
  uint64_t size;
} csupport_file_stat_t;

int csupport_stat_path(const char *path, int follow, csupport_file_stat_t *out);
int csupport_stat_fd(int fd, csupport_file_stat_t *out);

/* getcwd with retry (returns length, 0 on failure) */
size_t csupport_getcwd(char *buf, size_t cap);

/* realpath wrapper (returns length, 0 on failure) */
size_t csupport_realpath(const char *path, char *buf, size_t cap);

/* Expand ~username → home directory (returns length, 0 on failure) */
size_t csupport_lookup_user_homedir(const char *username, char *buf,
                                    size_t cap);
size_t csupport_expand_tilde_full(const char *path, size_t len, char *buf,
                                  size_t cap);

/* read/pread wrappers: 0=success, errno on failure */
int csupport_read_native(int fd, char *buf, size_t size, ssize_t *out_bytes);
int csupport_pread_native(int fd, char *buf, size_t size, uint64_t offset,
                          ssize_t *out_bytes);

/* opendir/readdir/closedir */
void *csupport_opendir(const char *path);
void csupport_closedir(void *handle);
int csupport_readdir_entry(void *handle, char *name_buf, size_t name_cap,
                           int *out_type);
int csupport_remove_directories_recursive(const char *path, int ignore_errors);

/* open() with EINTR retry + O_CLOEXEC fallback. Returns fd >= 0 on success, -1
 * on failure (errno set). */
int csupport_open_file_retry(const char *path, int flags, unsigned mode);

/* Resolve real path from fd. Tries F_GETPATH, /proc/self/fd, realpath fallback.
 * original_path used as fallback for realpath. Returns length in buf, 0 on
 * failure. */
size_t csupport_resolve_fd_path(int fd, const char *original_path, char *buf,
                                size_t cap);

/* Get main executable path for non-Apple Unix (returns length, 0 on failure) */
size_t csupport_get_main_executable_unix(const char *argv0, void *main_addr,
                                         char *buf, size_t cap);

/* --- Session 13: directory lookup functions (pure C) --- */

/* Get user config directory: XDG_CONFIG_HOME or ~/.config (non-Apple),
 * ~/Library/Preferences (Apple). Returns length in buf, 0 on failure. */
size_t csupport_user_config_directory(char *buf, size_t cap);

/* Get cache directory: XDG_CACHE_HOME or ~/.cache (non-Apple),
 * Darwin conf dir (Apple). Returns length in buf, 0 on failure. */
size_t csupport_cache_directory(char *buf, size_t cap);

/* Get system temp directory. Returns length in buf, 0 on failure. */
size_t csupport_system_temp_directory(int erased_on_reboot, char *buf,
                                      size_t cap);

/* Get the path for find_program_by_name (search PATH for executable).
 * If name contains '/', copies name directly.
 * Returns length in buf, 0 on failure. */
size_t csupport_find_program(const char *name, size_t name_len,
                             const char *const *extra_paths, size_t num_paths,
                             char *buf, size_t cap);

/* csupport_copy_fd declared in lpath.h extern "C" block below */
/* csupport_safely_close_fd already implemented inline or in Path.c */
/* csupport_get_page_size declared in lprocess.h */

#ifdef __cplusplus
}

/* ===== Path C++ inline implementations (from cpp_bridge.cpp) ===== */

#include "cpp_compat_stl.h"
#include "csupport/lrandom_lnumber_lgenerator.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

extern "C" int csupport_copy_fd(int read_fd, int write_fd);

namespace path_detail {
using llvm::StringRef;
using llvm::sys::path::is_separator;
using llvm::sys::path::Style;

inline Style real_style(Style style) {
  if (style != Style::native)
    return style;
  if (is_style_posix(style))
    return Style::posix;
  return LLVM_WINDOWS_PREFER_FORWARD_SLASH ? Style::windows_slash
                                           : Style::windows_backslash;
}

inline const char *separators(Style style) {
  if (is_style_windows(style))
    return "\\/";
  return "/";
}

inline char preferred_separator(Style style) {
  if (real_style(style) == Style::windows)
    return '\\';
  return '/';
}

inline StringRef find_first_component(StringRef path, Style style) {
  if (path.empty())
    return path;
  int cstyle = is_style_posix(style) ? CSUPPORT_PATH_STYLE_POSIX
                                     : CSUPPORT_PATH_STYLE_WINDOWS;
  size_t len =
      csupport_path_find_first_component(path.data(), path.size(), cstyle);
  return path.substr(0, len);
}

inline size_t filename_pos(StringRef str, Style style) {
  if (str.empty())
    return 0;
  int cstyle = is_style_posix(style) ? CSUPPORT_PATH_STYLE_POSIX
                                     : CSUPPORT_PATH_STYLE_WINDOWS;
  return csupport_path_filename_pos_styled(str.data(), str.size(), cstyle);
}

inline size_t root_dir_start(StringRef str, Style style) {
  int cstyle = is_style_posix(style) ? CSUPPORT_PATH_STYLE_POSIX
                                     : CSUPPORT_PATH_STYLE_WINDOWS;
  size_t r = csupport_path_root_dir_start(str.data(), str.size(), cstyle);
  return r == (size_t)-1 ? StringRef::npos : r;
}

inline size_t parent_path_end(StringRef path, Style style) {
  int cstyle = is_style_posix(style) ? CSUPPORT_PATH_STYLE_POSIX
                                     : CSUPPORT_PATH_STYLE_WINDOWS;
  return csupport_path_parent_path_end(path.data(), path.size(), cstyle);
}
} // namespace path_detail

namespace llvm {
namespace sys {
namespace path {

using path_detail::filename_pos;
using path_detail::find_first_component;
using path_detail::parent_path_end;
using path_detail::preferred_separator;
using path_detail::real_style;
using path_detail::root_dir_start;
using path_detail::separators;

inline const_iterator begin(StringRef path, Style style) {
  const_iterator i;
  i.Path = path;
  i.Component = find_first_component(path, style);
  i.Position = 0;
  i.S = style;
  return i;
}

inline const_iterator end(StringRef path) {
  const_iterator i;
  i.Path = path;
  i.Position = path.size();
  return i;
}

inline const_iterator &const_iterator::operator++() {
  assert(Position < Path.size() && "Tried to increment past end!");

  // Increment Position to past the current component
  Position += Component.size();

  // Check for end.
  if (Position == Path.size()) {
    Component = StringRef();
    return *this;
  }

  // Both POSIX and Windows treat paths that begin with exactly two separators
  // specially.
  bool was_net = Component.size() > 2 && is_separator(Component[0], S) &&
                 Component[1] == Component[0] && !is_separator(Component[2], S);

  // Handle separators.
  if (is_separator(Path[Position], S)) {
    // Root dir.
    if (was_net ||
        // c:/
        (is_style_windows(S) && Component.ends_with(":"))) {
      Component = Path.substr(Position, 1);
      return *this;
    }

    // Skip extra separators.
    while (Position != Path.size() && is_separator(Path[Position], S)) {
      ++Position;
    }

    // Treat trailing '/' as a '.', unless it is the root dir.
    if (Position == Path.size() && Component != "/") {
      --Position;
      Component = ".";
      return *this;
    }
  }

  // Find next component.
  size_t end_pos = Path.find_first_of(separators(S), Position);
  Component = Path.slice(Position, end_pos);

  return *this;
}

inline bool const_iterator::operator==(const const_iterator &RHS) const {
  return Path.begin() == RHS.Path.begin() && Position == RHS.Position;
}

inline ptrdiff_t const_iterator::operator-(const const_iterator &RHS) const {
  return Position - RHS.Position;
}

inline reverse_iterator rbegin(StringRef Path, Style style) {
  reverse_iterator I;
  I.Path = Path;
  I.Position = Path.size();
  I.S = style;
  ++I;
  return I;
}

inline reverse_iterator rend(StringRef Path) {
  reverse_iterator I;
  I.Path = Path;
  I.Component = Path.substr(0, 0);
  I.Position = 0;
  return I;
}

inline reverse_iterator &reverse_iterator::operator++() {
  size_t root_dir_pos = root_dir_start(Path, S);

  // Skip separators unless it's the root directory.
  size_t end_pos = Position;
  while (end_pos > 0 && (end_pos - 1) != root_dir_pos &&
         is_separator(Path[end_pos - 1], S))
    --end_pos;

  // Treat trailing '/' as a '.', unless it is the root dir.
  if (Position == Path.size() && !Path.empty() &&
      is_separator(Path.back(), S) &&
      (root_dir_pos == StringRef::npos || end_pos - 1 > root_dir_pos)) {
    --Position;
    Component = ".";
    return *this;
  }

  // Find next separator.
  size_t start_pos = filename_pos(Path.substr(0, end_pos), S);
  Component = Path.slice(start_pos, end_pos);
  Position = start_pos;
  return *this;
}

inline bool reverse_iterator::operator==(const reverse_iterator &RHS) const {
  return Path.begin() == RHS.Path.begin() && Component == RHS.Component &&
         Position == RHS.Position;
}

inline ptrdiff_t
reverse_iterator::operator-(const reverse_iterator &RHS) const {
  return Position - RHS.Position;
}

inline StringRef root_path(StringRef path, Style style) {
  const_iterator b = begin(path, style), pos = b, e = end(path);
  if (b != e) {
    bool has_net =
        b->size() > 2 && is_separator((*b)[0], style) && (*b)[1] == (*b)[0];
    bool has_drive = is_style_windows(style) && b->ends_with(":");

    if (has_net || has_drive) {
      if ((++pos != e) && is_separator((*pos)[0], style)) {
        // {C:/,//net/}, so get the first two components.
        return path.substr(0, b->size() + pos->size());
      }
      // just {C:,//net}, return the first component.
      return *b;
    }

    // POSIX style root directory.
    if (is_separator((*b)[0], style)) {
      return *b;
    }
  }

  return StringRef();
}

inline StringRef root_name(StringRef path, Style style) {
  const_iterator b = begin(path, style), e = end(path);
  if (b != e) {
    bool has_net =
        b->size() > 2 && is_separator((*b)[0], style) && (*b)[1] == (*b)[0];
    bool has_drive = is_style_windows(style) && b->ends_with(":");

    if (has_net || has_drive) {
      // just {C:,//net}, return the first component.
      return *b;
    }
  }

  // No path or no name.
  return StringRef();
}

inline StringRef root_directory(StringRef path, Style style) {
  const_iterator b = begin(path, style), pos = b, e = end(path);
  if (b != e) {
    bool has_net =
        b->size() > 2 && is_separator((*b)[0], style) && (*b)[1] == (*b)[0];
    bool has_drive = is_style_windows(style) && b->ends_with(":");

    if ((has_net || has_drive) &&
        // {C:,//net}, skip to the next component.
        (++pos != e) && is_separator((*pos)[0], style)) {
      return *pos;
    }

    // POSIX style root directory.
    if (!has_net && is_separator((*b)[0], style)) {
      return *b;
    }
  }

  // No path or no root.
  return StringRef();
}

inline StringRef relative_path(StringRef path, Style style) {
  StringRef root = root_path(path, style);
  return path.substr(root.size());
}

inline void append(SmallVectorImpl<char> &path, Style style, const Twine &a,
                   const Twine &b, const Twine &c, const Twine &d) {
  SmallString<32> a_storage;
  SmallString<32> b_storage;
  SmallString<32> c_storage;
  SmallString<32> d_storage;

  SmallVector<StringRef, 4> components;
  if (!a.isTriviallyEmpty())
    components.push_back(a.toStringRef(a_storage));
  if (!b.isTriviallyEmpty())
    components.push_back(b.toStringRef(b_storage));
  if (!c.isTriviallyEmpty())
    components.push_back(c.toStringRef(c_storage));
  if (!d.isTriviallyEmpty())
    components.push_back(d.toStringRef(d_storage));

  for (auto &component : components) {
    bool path_has_sep =
        !path.empty() && is_separator(path[path.size() - 1], style);
    if (path_has_sep) {
      // Strip separators from beginning of component.
      size_t loc = component.find_first_not_of(separators(style));
      StringRef c = component.substr(loc);

      // Append it.
      path.append(c.begin(), c.end());
      continue;
    }

    bool component_has_sep =
        !component.empty() && is_separator(component[0], style);
    if (!component_has_sep &&
        !(path.empty() || has_root_name(component, style))) {
      // Add a separator.
      path.push_back(preferred_separator(style));
    }

    path.append(component.begin(), component.end());
  }
}

inline void append(SmallVectorImpl<char> &path, const Twine &a, const Twine &b,
                   const Twine &c, const Twine &d) {
  append(path, Style::native, a, b, c, d);
}

inline void append(SmallVectorImpl<char> &path, const_iterator begin,
                   const_iterator end, Style style) {
  for (; begin != end; ++begin)
    path::append(path, style, *begin);
}

inline StringRef parent_path(StringRef path, Style style) {
  size_t end_pos = parent_path_end(path, style);
  if (end_pos == StringRef::npos)
    return StringRef();
  return path.substr(0, end_pos);
}

inline void remove_filename(SmallVectorImpl<char> &path, Style style) {
  size_t end_pos = parent_path_end(StringRef(path.begin(), path.size()), style);
  if (end_pos != StringRef::npos)
    path.truncate(end_pos);
}

inline void replace_extension(SmallVectorImpl<char> &path,
                              const Twine &extension, Style style) {
  StringRef p(path.begin(), path.size());
  SmallString<32> ext_storage;
  StringRef ext = extension.toStringRef(ext_storage);

  // Erase existing extension.
  size_t pos = p.find_last_of('.');
  if (pos != StringRef::npos && pos >= filename_pos(p, style))
    path.truncate(pos);

  // Append '.' if needed.
  if (ext.size() > 0 && ext[0] != '.')
    path.push_back('.');

  // Append extension.
  path.append(ext.begin(), ext.end());
}

inline bool starts_with(StringRef Path, StringRef Prefix,
                        Style style = Style::native) {
  return csupport_path_starts_with_insensitive(Path.data(), Path.size(),
                                               Prefix.data(), Prefix.size(),
                                               (int)(real_style(style)));
}

inline bool replace_path_prefix(SmallVectorImpl<char> &Path,
                                StringRef OldPrefix, StringRef NewPrefix,
                                Style style) {
  if (OldPrefix.empty() && NewPrefix.empty())
    return false;

  StringRef OrigPath(Path.begin(), Path.size());
  if (!starts_with(OrigPath, OldPrefix, style))
    return false;

  // If prefixes have the same size we can simply copy the new one over.
  if (OldPrefix.size() == NewPrefix.size()) {
    memcpy(Path.begin(), NewPrefix.data(), NewPrefix.size());
    return true;
  }

  StringRef RelPath = OrigPath.substr(OldPrefix.size());
  SmallString<256> NewPath;
  (Twine(NewPrefix) + RelPath).toVector(NewPath);
  Path.swap(NewPath);
  return true;
}

inline void native(const Twine &path, SmallVectorImpl<char> &result,
                   Style style) {
  assert((!path.isSingleStringRef() ||
          path.getSingleStringRef().data() != result.data()) &&
         "path and result are not allowed to overlap!");
  // Clear result.
  result.clear();
  path.toVector(result);
  native(result, style);
}

inline void native(SmallVectorImpl<char> &Path, Style style) {
  if (Path.empty())
    return;
  if (is_style_windows(style)) {
    for (char &Ch : Path)
      if (is_separator(Ch, style))
        Ch = preferred_separator(style);
    if (Path[0] == '~' && (Path.size() == 1 || is_separator(Path[1], style))) {
      SmallString<128> PathHome;
      home_directory(PathHome);
      PathHome.append(Path.begin() + 1, Path.end());
      Path = PathHome;
    }
  } else {
    for (auto it = Path.begin(); it != Path.end(); ++it)
      if (*it == '\\')
        *it = '/';
  }
}

inline SmallString<256> convert_to_slash(StringRef path, Style style) {
  if (is_style_posix(style))
    return SmallString<256>(path);

  SmallString<256> s(path);
  csupport_path_convert_backslash(s.data(), s.size());
  return s;
}

inline StringRef filename(StringRef path, Style style) {
  return *rbegin(path, style);
}

inline StringRef stem(StringRef path, Style style) {
  const char *out;
  size_t len = csupport_path_stem(path.data(), path.size(), &out,
                                  (int)(real_style(style)));
  return StringRef(out, len);
}

inline StringRef extension(StringRef path, Style style) {
  const char *out;
  size_t len = csupport_path_extension(path.data(), path.size(), &out,
                                       (int)(real_style(style)));
  return StringRef(out, len);
}

inline bool is_separator(char value, Style style) {
  if (value == '/')
    return true;
  if (is_style_windows(style))
    return value == '\\';
  return false;
}

inline StringRef get_separator(Style style) {
  if (real_style(style) == Style::windows)
    return "\\";
  return "/";
}

inline bool has_root_name(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !root_name(p, style).empty();
}

inline bool has_root_directory(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !root_directory(p, style).empty();
}

inline bool has_root_path(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !root_path(p, style).empty();
}

inline bool has_relative_path(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !relative_path(p, style).empty();
}

inline bool has_filename(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !filename(p, style).empty();
}

inline bool has_parent_path(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !parent_path(p, style).empty();
}

inline bool has_stem(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !stem(p, style).empty();
}

inline bool has_extension(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !extension(p, style).empty();
}

inline bool is_absolute(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  bool rootDir = has_root_directory(p, style);
  bool rootName = is_style_posix(style) || has_root_name(p, style);

  return rootDir && rootName;
}

inline bool is_absolute_gnu(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  // Handle '/' which is absolute for both Windows and POSIX systems.
  // Handle '\\' on Windows.
  if (!p.empty() && is_separator(p.front(), style))
    return true;

  if (is_style_windows(style)) {
    // Handle drive letter pattern (a character followed by ':') on Windows.
    if (p.size() >= 2 && (p[0] && p[1] == ':'))
      return true;
  }

  return false;
}

inline bool is_relative(const Twine &path, Style style) {
  return !is_absolute(path, style);
}

inline StringRef remove_leading_dotslash(StringRef Path, Style style) {
  const char *out;
  size_t len = csupport_path_remove_leading_dotslash(
      Path.data(), Path.size(), &out, (int)(real_style(style)));
  return StringRef(out, len);
}

// Remove path traversal components ("." and "..") when possible, and
// canonicalize slashes.
inline bool remove_dots(SmallVectorImpl<char> &the_path, bool remove_dot_dot,
                        Style style) {
  style = real_style(style);
  StringRef remaining(the_path.data(), the_path.size());
  bool needs_change = false;
  SmallVector<StringRef, 16> components;

  // Consume the root path, if present.
  StringRef root = path::root_path(remaining, style);
  bool absolute = !root.empty();
  if (absolute)
    remaining = remaining.drop_front(root.size());

  // Loop over path components manually. This makes it easier to detect
  // non-preferred slashes and double separators that must be canonicalized.
  while (!remaining.empty()) {
    size_t next_slash = remaining.find_first_of(separators(style));
    if (next_slash == StringRef::npos)
      next_slash = remaining.size();
    StringRef component = remaining.take_front(next_slash);
    remaining = remaining.drop_front(next_slash);

    // Eat the slash, and check if it is the preferred separator.
    if (!remaining.empty()) {
      needs_change |= remaining.front() != preferred_separator(style);
      remaining = remaining.drop_front();
      // The path needs to be rewritten if it has a trailing slash.
      // FIXME: This is emergent behavior that could be removed.
      needs_change |= remaining.empty();
    }

    // Check for path traversal components or double separators.
    if (component.empty() || component == ".") {
      needs_change = true;
    } else if (remove_dot_dot && component == "..") {
      needs_change = true;
      // Do not allow ".." to remove the root component. If this is the
      // beginning of a relative path, keep the ".." component.
      if (!components.empty() && components.back() != "..") {
        components.pop_back();
      } else if (!absolute) {
        components.push_back(component);
      }
    } else {
      components.push_back(component);
    }
  }

  SmallString<256> buffer = root;
  // "root" could be "/", which may need to be translated into "\".
  make_preferred(buffer, style);
  needs_change |= root != buffer;

  // Avoid rewriting the path unless we have to.
  if (!needs_change)
    return false;

  if (!components.empty()) {
    buffer += components[0];
    for (StringRef C : ArrayRef(components).drop_front()) {
      buffer += preferred_separator(style);
      buffer += C;
    }
  }
  the_path.swap(buffer);
  return true;
}

} // end namespace path

namespace fs {

using llvm::errc;

enum FSEntity { FS_Dir, FS_File, FS_Name };

inline errc_t getUniqueID(const Twine Path, UniqueID &Result) {
  file_status Status;
  auto EC = status(Path, Status);
  if (EC)
    return EC;
  Result = Status.getUniqueID();
  return {};
}

inline void createUniquePath(const Twine &Model,
                             SmallVectorImpl<char> &ResultPath,
                             bool MakeAbsolute) {
  SmallString<128> ModelStorage;
  Model.toVector(ModelStorage);

  if (MakeAbsolute) {
    // Make model absolute by prepending a temp directory if it's not already.
    if (!sys::path::is_absolute(Twine(ModelStorage))) {
      SmallString<128> TDir;
      sys::path::system_temp_directory(true, TDir);
      sys::path::append(TDir, Twine(ModelStorage));
      ModelStorage.swap(TDir);
    }
  }

  ResultPath = ModelStorage;
  ResultPath.push_back(0);
  ResultPath.pop_back();

  // Replace '%' with random chars.
  for (unsigned i = 0, e = ModelStorage.size(); i != e; ++i) {
    if (ModelStorage[i] == '%') {
      unsigned char b = 0;
      (void)csupport_get_random_bytes(&b, 1);
      ResultPath[i] = "0123456789abcdef"[b & 15];
    }
  }
}

inline errc_t createUniqueEntity(const Twine &Model, int &ResultFD,
                                 SmallVectorImpl<char> &ResultPath,
                                 bool MakeAbsolute, FSEntity Type,
                                 OpenFlags Flags = OF_None, unsigned Mode = 0) {
  errc_t EC;
  for (int Retries = 128; Retries > 0; --Retries) {
    createUniquePath(Model, ResultPath, MakeAbsolute);
    switch (Type) {
    case FS_File: {
      EC = openFileForReadWrite(Twine(ResultPath.begin()), ResultFD,
                                CD_CreateNew, Flags, Mode);
      if (EC) {
        if (EC == errc::file_exists || EC == errc::permission_denied)
          continue;
        return EC;
      }
      return {};
    }
    case FS_Name: {
      EC = access(ResultPath.begin(), AccessMode::Exist);
      if (EC == errc::no_such_file_or_directory)
        return {};
      if (EC)
        return EC;
      continue;
    }
    case FS_Dir: {
      EC = create_directory(ResultPath.begin(), false);
      if (EC) {
        if (EC == errc::file_exists)
          continue;
        return EC;
      }
      return {};
    }
    }
    llvm_unreachable("Invalid Type");
  }
  return EC;
}

inline errc_t createUniqueFile(const Twine &Model, int &ResultFd,
                               SmallVectorImpl<char> &ResultPath,
                               OpenFlags Flags, unsigned Mode) {
  return createUniqueEntity(Model, ResultFd, ResultPath, false, FS_File, Flags,
                            Mode);
}

inline errc_t createUniqueFile(const Twine &Model,
                               SmallVectorImpl<char> &ResultPath,
                               unsigned Mode) {
  int FD;
  auto EC = createUniqueFile(Model, FD, ResultPath, OF_None, Mode);
  if (EC)
    return EC;
  // FD is only needed to avoid race conditions. Close it right away.
  close(FD);
  return EC;
}

inline errc_t createTemporaryFile(const Twine &Model, int &ResultFD,
                                  llvm::SmallVectorImpl<char> &ResultPath,
                                  FSEntity Type,
                                  sys::fs::OpenFlags Flags = sys::fs::OF_None) {
  SmallString<128> Storage;
  StringRef P = Model.toNullTerminatedStringRef(Storage);
  assert(P.find_first_of(separators(Style::native)) == StringRef::npos &&
         "Model must be a simple filename.");
  // Use P.begin() so that createUniqueEntity doesn't need to recreate Storage.
  return createUniqueEntity(P.begin(), ResultFD, ResultPath, true, Type, Flags,
                            owner_read | owner_write);
}

inline errc_t createTemporaryFile(const Twine &Prefix, StringRef Suffix,
                                  int &ResultFD,
                                  llvm::SmallVectorImpl<char> &ResultPath,
                                  FSEntity Type,
                                  sys::fs::OpenFlags Flags = sys::fs::OF_None) {
  const char *Middle = Suffix.empty() ? "-%%%%%%" : "-%%%%%%.";
  return createTemporaryFile(Prefix + Middle + Suffix, ResultFD, ResultPath,
                             Type, Flags);
}

inline errc_t createTemporaryFile(const Twine &Prefix, StringRef Suffix,
                                  int &ResultFD,
                                  SmallVectorImpl<char> &ResultPath,
                                  sys::fs::OpenFlags Flags) {
  return createTemporaryFile(Prefix, Suffix, ResultFD, ResultPath, FS_File,
                             Flags);
}

inline errc_t createTemporaryFile(const Twine &Prefix, StringRef Suffix,
                                  SmallVectorImpl<char> &ResultPath,
                                  sys::fs::OpenFlags Flags) {
  int FD;
  auto EC = createTemporaryFile(Prefix, Suffix, FD, ResultPath, Flags);
  if (EC)
    return EC;
  // FD is only needed to avoid race conditions. Close it right away.
  close(FD);
  return EC;
}

// This is a mkdtemp with a different pattern. We use createUniqueEntity mostly
// for consistency. We should try using mkdtemp.
inline errc_t createUniqueDirectory(const Twine &Prefix,
                                    SmallVectorImpl<char> &ResultPath) {
  int Dummy;
  return createUniqueEntity(Prefix + "-%%%%%%", Dummy, ResultPath, true,
                            FS_Dir);
}

inline errc_t getPotentiallyUniqueFileName(const Twine &Model,
                                           SmallVectorImpl<char> &ResultPath) {
  int Dummy;
  return createUniqueEntity(Model, Dummy, ResultPath, false, FS_Name);
}

inline errc_t
getPotentiallyUniqueTempFileName(const Twine &Prefix, StringRef Suffix,
                                 SmallVectorImpl<char> &ResultPath) {
  int Dummy;
  return createTemporaryFile(Prefix, Suffix, Dummy, ResultPath, FS_Name);
}

inline void make_absolute(const Twine &current_directory,
                          SmallVectorImpl<char> &path) {
  StringRef p(path.data(), path.size());

  bool rootDirectory = path::has_root_directory(p);
  bool rootName = path::has_root_name(p);

  // Already absolute.
  if ((rootName || is_style_posix(path::Style::native)) && rootDirectory)
    return;

  // All of the following conditions will need the current directory.
  SmallString<128> current_dir;
  current_directory.toVector(current_dir);

  // Relative path. Prepend the current directory.
  if (!rootName && !rootDirectory) {
    // Append path to the current directory.
    path::append(current_dir, p);
    // Set path to the result.
    path.swap(current_dir);
    return;
  }

  if (!rootName && rootDirectory) {
    StringRef cdrn = path::root_name(current_dir);
    SmallString<128> curDirRootName(cdrn.begin(), cdrn.end());
    path::append(curDirRootName, p);
    // Set path to the result.
    path.swap(curDirRootName);
    return;
  }

  if (rootName && !rootDirectory) {
    StringRef pRootName = path::root_name(p);
    StringRef bRootDirectory = path::root_directory(current_dir);
    StringRef bRelativePath = path::relative_path(current_dir);
    StringRef pRelativePath = path::relative_path(p);

    SmallString<128> res;
    path::append(res, pRootName, bRootDirectory, bRelativePath, pRelativePath);
    path.swap(res);
    return;
  }

  llvm_unreachable("All rootName and rootDirectory combinations should have "
                   "occurred above!");
}

inline errc_t make_absolute(SmallVectorImpl<char> &path) {
  if (path::is_absolute(path))
    return {};

  SmallString<128> current_dir;
  if (errc_t ec = current_path(current_dir))
    return ec;

  make_absolute(current_dir, path);
  return {};
}

inline errc_t create_directories(const Twine &Path, bool IgnoreExisting,
                                 perms Perms) {
  SmallString<128> PathStorage;
  StringRef P = Path.toStringRef(PathStorage);

  // Be optimistic and try to create the directory
  auto EC = create_directory(P, IgnoreExisting, Perms);
  // If we succeeded, or had any error other than the parent not existing, just
  // return it.
  if (EC != errc::no_such_file_or_directory)
    return EC;

  // We failed because of a no_such_file_or_directory, try to create the
  // parent.
  StringRef Parent = path::parent_path(P);
  if (Parent.empty())
    return EC;

  if ((EC = create_directories(Parent, IgnoreExisting, Perms)))
    return EC;

  return create_directory(P, IgnoreExisting, Perms);
}

inline errc_t copy_file_internal(int ReadFD, int WriteFD) {
  int err = csupport_copy_fd(ReadFD, WriteFD);
  if (err)
    return ec_errno(err);
  return {};
}

#ifndef __APPLE__
inline errc_t copy_file(const Twine &From, const Twine &To) {
  int ReadFD, WriteFD;
  if (errc_t EC = openFileForRead(From, ReadFD, OF_None))
    return EC;
  if (errc_t EC = openFileForWrite(To, WriteFD, CD_CreateAlways, OF_None)) {
    close(ReadFD);
    return EC;
  }

  auto EC = copy_file_internal(ReadFD, WriteFD);

  close(ReadFD);
  close(WriteFD);

  return EC;
}
#endif

inline errc_t copy_file(const Twine &From, int ToFD) {
  int ReadFD;
  if (errc_t EC = openFileForRead(From, ReadFD, OF_None))
    return EC;

  auto EC = copy_file_internal(ReadFD, ToFD);

  close(ReadFD);

  return EC;
}

inline ErrorOr<MD5::MD5Result> md5_contents(int FD) {
  MD5 Hash;

  const size_t BufSize = 4096;
  SmallVector<uint8_t, 4096> Buf(BufSize);
  int BytesRead = 0;
  for (;;) {
    BytesRead = read(FD, Buf.data(), BufSize);
    if (BytesRead <= 0)
      break;
    Hash.update(ArrayRef(Buf.data(), BytesRead));
  }

  if (BytesRead < 0)
    return ec_errno(errno);
  MD5::MD5Result Result;
  Hash.final(Result);
  return Result;
}

inline ErrorOr<MD5::MD5Result> md5_contents(const Twine &Path) {
  int FD;
  if (auto EC = openFileForRead(Path, FD, OF_None))
    return EC;

  auto Result = md5_contents(FD);
  close(FD);
  return Result;
}

inline bool exists(const basic_file_status &status) {
  return status_known(status) && status.type() != file_type::file_not_found;
}

inline bool status_known(const basic_file_status &s) {
  return s.type() != file_type::status_error;
}

inline file_type get_file_type(const Twine &Path, bool Follow) {
  file_status st;
  if (status(Path, st, Follow))
    return file_type::status_error;
  return st.type();
}

inline bool is_directory(const basic_file_status &status) {
  return status.type() == file_type::directory_file;
}

inline errc_t is_directory(const Twine &path, bool &result) {
  file_status st;
  if (errc_t ec = status(path, st))
    return ec;
  result = is_directory(st);
  return {};
}

inline bool is_regular_file(const basic_file_status &status) {
  return status.type() == file_type::regular_file;
}

inline errc_t is_regular_file(const Twine &path, bool &result) {
  file_status st;
  if (errc_t ec = status(path, st))
    return ec;
  result = is_regular_file(st);
  return {};
}

inline bool is_symlink_file(const basic_file_status &status) {
  return status.type() == file_type::symlink_file;
}

inline errc_t is_symlink_file(const Twine &path, bool &result) {
  file_status st;
  if (errc_t ec = status(path, st, false))
    return ec;
  result = is_symlink_file(st);
  return {};
}

inline bool is_other(const basic_file_status &status) {
  return exists(status) && !is_regular_file(status) && !is_directory(status);
}

inline errc_t is_other(const Twine &Path, bool &Result) {
  file_status FileStatus;
  if (errc_t EC = status(Path, FileStatus))
    return EC;
  Result = is_other(FileStatus);
  return {};
}

inline void directory_entry::replace_filename(const Twine &Filename,
                                              file_type Type,
                                              basic_file_status Status) {
  SmallString<128> PathStr = path::parent_path(Path);
  path::append(PathStr, Filename);
  this->Path = PathStr.str().str();
  this->Type = Type;
  this->Status = Status;
}

inline ErrorOr<perms> getPermissions(const Twine &Path) {
  file_status Status;
  if (errc_t EC = status(Path, Status))
    return EC;

  return Status.permissions();
}

inline Error readNativeFileToEOF(file_t FileHandle,
                                 SmallVectorImpl<char> &Buffer,
                                 ssize_t ChunkSize) {
  // Install a handler to truncate the buffer to the correct size on exit.
  size_t Size = Buffer.size();
  auto TruncateOnExit = make_scope_exit([&]() { Buffer.truncate(Size); });

  // Read into Buffer until we hit EOF.
  for (;;) {
    Buffer.resize_for_overwrite(Size + ChunkSize);
    Expected<size_t> ReadBytes = readNativeFile(
        FileHandle, MutableArrayRef(Buffer.begin() + Size, ChunkSize));
    if (!ReadBytes)
      return ReadBytes.takeError();
    if (*ReadBytes == 0)
      return Error::success();
    Size += *ReadBytes;
  }
}

} // end namespace fs
} // end namespace sys
} // end namespace llvm

/* Unix/Path.inc and Windows/Path.inc define non-inline llvm::sys::fs/path
 * symbols. They MUST NOT be included from this header or every TU duplicates
 * them at link time. Include Path.inc from exactly one C++ TU (support_cpp.cpp
 * in CSupportHost). Declarations remain in llvm/Support/FileSystem.h and
 * Path.h. */

namespace llvm {
namespace sys {
namespace fs {

using llvm::errc;

inline TempFile::TempFile(StringRef Name, int FD) : TmpName(Name), FD(FD) {}
inline TempFile::TempFile(TempFile &&Other) { *this = CMOVE(Other); }
inline TempFile &TempFile::operator=(TempFile &&Other) {
  TmpName = Other.TmpName;
  FD = Other.FD;
  Other.Done = true;
  Other.FD = -1;
#ifdef _WIN32
  RemoveOnClose = Other.RemoveOnClose;
  Other.RemoveOnClose = false;
#endif
  return *this;
}

inline TempFile::~TempFile() { assert(Done); }

inline Error TempFile::discard() {
  Done = true;
  if (FD != -1 && close(FD) == -1) {
    auto EC = ec_errno(errno);
    return errorCodeToError(EC);
  }
  FD = -1;

#ifdef _WIN32
  // On Windows, closing will remove the file, if we set the delete
  // disposition. If not, remove it manually.
  bool Remove = RemoveOnClose;
#else
  // Always try to remove the file.
  bool Remove = true;
#endif
  errc_t RemoveEC;
  if (Remove && !TmpName.empty()) {
    RemoveEC = fs::remove(TmpName);
    sys::DontRemoveFileOnSignal(TmpName);
    if (!RemoveEC)
      TmpName = "";
  } else {
    TmpName = "";
  }
  return errorCodeToError(RemoveEC);
}

inline Error TempFile::keep(const Twine &Name) {
  assert(!Done);
  Done = true;
  // Always try to close and rename.
#ifdef _WIN32
  // If we can't cancel the delete don't rename.
  auto H = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  errc_t RenameEC = RemoveOnClose ? errc_t{} : setDeleteDisposition(H, false);
  bool ShouldDelete = false;
  if (!RenameEC) {
    RenameEC = rename_handle(H, Name);
    // If rename failed because it's cross-device, copy instead
    if (RenameEC == ec_sys(ERROR_NOT_SAME_DEVICE)) {
      RenameEC = copy_file(TmpName, Name);
      ShouldDelete = true;
    }
  }

  // If we can't rename or copy, discard the temporary file.
  if (RenameEC)
    ShouldDelete = true;
  if (ShouldDelete) {
    if (!RemoveOnClose)
      setDeleteDisposition(H, true);
    else
      remove(TmpName);
  }
#else
  auto RenameEC = fs::rename(TmpName, Name);
  if (RenameEC) {
    // If we can't rename, try to copy to work around cross-device link issues.
    RenameEC = sys::fs::copy_file(TmpName, Name);
    // If we can't rename or copy, discard the temporary file.
    if (RenameEC)
      remove(TmpName);
  }
#endif
  sys::DontRemoveFileOnSignal(TmpName);

  if (!RenameEC)
    TmpName = "";

  if (close(FD) == -1) {
    auto EC = ec_errno(errno);
    return errorCodeToError(EC);
  }
  FD = -1;

  return errorCodeToError(RenameEC);
}

inline Error TempFile::keep() {
  assert(!Done);
  Done = true;

#ifdef _WIN32
  auto H = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  if (errc_t EC = setDeleteDisposition(H, false))
    return errorCodeToError(EC);
#endif
  sys::DontRemoveFileOnSignal(TmpName);

  TmpName = "";

  if (close(FD) == -1) {
    auto EC = ec_errno(errno);
    return errorCodeToError(EC);
  }
  FD = -1;

  return Error::success();
}

inline Expected<TempFile> TempFile::create(const Twine &Model, unsigned Mode,
                                           OpenFlags ExtraFlags) {
  int FD;
  SmallString<128> ResultPath;
  if (errc_t EC =
          createUniqueFile(Model, FD, ResultPath, OF_Delete | ExtraFlags, Mode))
    return errorCodeToError(EC);

  TempFile Ret(ResultPath, FD);
#ifdef _WIN32
  auto H = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  bool SetSignalHandler = false;
  if (errc_t EC = setDeleteDisposition(H, true)) {
    Ret.RemoveOnClose = true;
    SetSignalHandler = true;
  }
#else
  bool SetSignalHandler = true;
#endif
  if (SetSignalHandler && sys::RemoveFileOnSignal(ResultPath)) {
    // Make sure we delete the file when RemoveFileOnSignal fails.
    consumeError(Ret.discard());
    return errorCodeToError(make_error_code(errc::operation_not_permitted));
  }
  return Ret;
}
} // namespace fs

} // namespace sys
} // namespace llvm

#endif /* __cplusplus */
#endif /* CSUPPORT_LPATH_H */
