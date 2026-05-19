/*===- Path.c - File path manipulation (pure C) -----------------*- C -*-===*/
#include "include/csupport/lpath.h"
#include "llvm/Config/config.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#ifdef __linux__
#include <sys/sendfile.h>
#endif
#include <time.h>
#include <limits.h>
#include <dirent.h>
#include <pwd.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#endif

#if !defined(_WIN32)
#if !defined(__APPLE__) && !defined(__OpenBSD__) && !defined(__FreeBSD__) &&   \
    !defined(__linux__) && !defined(__FreeBSD_kernel__) && !defined(_AIX)
#include <sys/statvfs.h>
#define CSUPPORT_STATVFS statvfs
#define CSUPPORT_FSTATVFS fstatvfs
#define CSUPPORT_FRSIZE(vfs) ((uint64_t)(vfs).f_frsize)
#else
#if defined(__OpenBSD__) || defined(__FreeBSD__)
#include <sys/param.h>
#include <sys/mount.h>
#elif defined(__linux__)
#if defined(HAVE_LINUX_MAGIC_H)
#include <linux/magic.h>
#else
#if defined(HAVE_LINUX_NFS_FS_H)
#include <linux/nfs_fs.h>
#endif
#if defined(HAVE_LINUX_SMB_H)
#include <linux/smb.h>
#endif
#endif
#include <sys/vfs.h>
#elif defined(_AIX)
#include <sys/statfs.h>
#elif defined(__APPLE__)
#include <sys/mount.h>
#else
#include <sys/mount.h>
#endif
#define CSUPPORT_STATVFS statfs
#define CSUPPORT_FSTATVFS fstatfs
#define CSUPPORT_FRSIZE(vfs) ((uint64_t)(vfs).f_bsize)
#endif

#if defined(__NetBSD__) || defined(__DragonFly__) || defined(__GNU__) || \
    defined(__MVS__)
#define CSUPPORT_FFLAGS(vfs) ((vfs).f_flag)
#else
#define CSUPPORT_FFLAGS(vfs) ((vfs).f_flags)
#endif
#endif /* !_WIN32 */

#ifdef __APPLE__
#include <copyfile.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#if __has_include(<sys/clonefile.h>)
#include <sys/clonefile.h>
#endif
#endif

#ifdef _WIN32
#define IS_SEP(c) ((c) == '/' || (c) == '\\')
#else
#define IS_SEP(c) ((c) == '/')
#endif

size_t csupport_path_root_name_len(const char *path, size_t len) {
#ifdef _WIN32
  if (len >= 2 && path[1] == ':' &&
      ((path[0] >= 'A' && path[0] <= 'Z') ||
       (path[0] >= 'a' && path[0] <= 'z')))
    return 2;
  if (len >= 2 && IS_SEP(path[0]) && IS_SEP(path[1])) {
    size_t i = 2;
    while (i < len && !IS_SEP(path[i])) i++;
    return i;
  }
#endif
  (void)path; (void)len;
  return 0;
}

size_t csupport_path_root_dir_pos(const char *path, size_t len) {
  size_t rn = csupport_path_root_name_len(path, len);
  if (rn < len && IS_SEP(path[rn]))
    return rn;
  return (size_t)-1;
}

int csupport_path_is_absolute(const char *path, size_t len) {
  size_t rn = csupport_path_root_name_len(path, len);
  return (rn < len && IS_SEP(path[rn]));
}

int csupport_path_is_relative(const char *path, size_t len) {
  return !csupport_path_is_absolute(path, len);
}

size_t csupport_path_parent_end(const char *path, size_t len) {
  if (len == 0) return (size_t)-1;
  size_t fn_pos = csupport_path_filename_pos(path, len);
  int filename_was_sep = (len > 0 && IS_SEP(path[fn_pos]));
  size_t root_dir = csupport_path_root_dir_pos(path, len);
  size_t end = fn_pos;
  while (end > 0 &&
         (root_dir == (size_t)-1 || end > root_dir) &&
         IS_SEP(path[end - 1]))
    --end;
  if (end == root_dir && root_dir != (size_t)-1 && !filename_was_sep)
    return root_dir + 1;
  return end > 0 ? end : (size_t)-1;
}

size_t csupport_path_filename_pos(const char *path, size_t len) {
  if (len == 0) return 0;
  if (IS_SEP(path[len - 1]))
    return len - 1;
  size_t i = len;
  while (i > 0 && !IS_SEP(path[i - 1])) i--;
  return i;
}

size_t csupport_path_extension_pos(const char *path, size_t len) {
  size_t fn = csupport_path_filename_pos(path, len);
  size_t i = len;
  while (i > fn && path[i - 1] != '.') i--;
  if (i <= fn) return len;
  if (i - 1 == fn) return len;
  return i - 1;
}

size_t csupport_path_stem_end(const char *path, size_t len) {
  return csupport_path_extension_pos(path, len);
}

size_t csupport_path_append(char *buf, size_t buflen, size_t cur_len,
                            const char *component, size_t comp_len) {
  if (comp_len == 0) return cur_len;
  if (csupport_path_is_absolute(component, comp_len)) {
    if (comp_len >= buflen) return cur_len;
    memcpy(buf, component, comp_len);
    buf[comp_len] = '\0';
    return comp_len;
  }
  size_t need = cur_len;
  if (need > 0 && !IS_SEP(buf[need - 1])) {
    if (need + 1 >= buflen) return cur_len;
    buf[need++] = '/';
  }
  if (need + comp_len >= buflen) return cur_len;
  memcpy(buf + need, component, comp_len);
  need += comp_len;
  buf[need] = '\0';
  return need;
}

void csupport_path_replace_extension(char *buf, size_t *len, size_t buflen,
                                     const char *ext, size_t ext_len) {
  size_t dot = csupport_path_extension_pos(buf, *len);
  size_t pos = dot;
  int need_dot = (ext_len > 0 && ext[0] != '.');
  if (pos + (need_dot ? 1 : 0) + ext_len >= buflen) return;
  if (need_dot) buf[pos++] = '.';
  memcpy(buf + pos, ext, ext_len);
  pos += ext_len;
  buf[pos] = '\0';
  *len = pos;
}

void csupport_path_native(char *buf, size_t len) {
#ifdef _WIN32
  for (size_t i = 0; i < len; i++)
    if (buf[i] == '/') buf[i] = '\\';
#else
  (void)buf; (void)len;
#endif
}

static int is_win_style(int style) {
  if (style == CSUPPORT_PATH_NATIVE) {
#ifdef _WIN32
    return 1;
#else
    return 0;
#endif
  }
  return style == CSUPPORT_PATH_WINDOWS ||
         style == CSUPPORT_PATH_WINDOWS_SLASH ||
         style == CSUPPORT_PATH_WINDOWS_BACKSLASH;
}

int csupport_path_is_separator(char c, int style) {
  if (c == '/') return 1;
  if (is_win_style(style) && c == '\\') return 1;
  return 0;
}

static int is_alpha(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

size_t csupport_path_find_first_component(const char *path, size_t len,
                                          int style) {
  if (len == 0) return 0;

  if (is_win_style(style)) {
    if (len >= 2 && is_alpha(path[0]) && path[1] == ':')
      return 2;
  }

  if (len > 2 && csupport_path_is_separator(path[0], style) &&
      path[0] == path[1] && !csupport_path_is_separator(path[2], style)) {
    size_t end = 2;
    while (end < len && !csupport_path_is_separator(path[end], style)) end++;
    return end;
  }

  if (csupport_path_is_separator(path[0], style))
    return 1;

  {
    size_t end = 0;
    while (end < len && !csupport_path_is_separator(path[end], style)) end++;
    return end;
  }
}

size_t csupport_path_filename_pos_styled(const char *path, size_t len,
                                         int style) {
  if (len > 0 && csupport_path_is_separator(path[len - 1], style))
    return len - 1;

  size_t pos = (size_t)-1;
  {
    size_t i = len;
    while (i > 0) {
      i--;
      if (csupport_path_is_separator(path[i], style)) {
        pos = i;
        break;
      }
    }
  }

  if (is_win_style(style) && pos == (size_t)-1 && len >= 2) {
    size_t i = len - 1;
    while (i > 0) {
      i--;
      if (path[i] == ':') {
        pos = i;
        break;
      }
    }
  }

  if (pos == (size_t)-1 || (pos == 1 && csupport_path_is_separator(path[0], style)))
    return 0;

  return pos + 1;
}

size_t csupport_path_root_dir_start(const char *path, size_t len, int style) {
  if (is_win_style(style)) {
    if (len > 2 && path[1] == ':' && csupport_path_is_separator(path[2], style))
      return 2;
  }

  if (len > 3 && csupport_path_is_separator(path[0], style) &&
      path[0] == path[1] && !csupport_path_is_separator(path[2], style)) {
    size_t i = 2;
    while (i < len && !csupport_path_is_separator(path[i], style)) i++;
    if (i < len) return i;
    return (size_t)-1;
  }

  if (len > 0 && csupport_path_is_separator(path[0], style))
    return 0;

  return (size_t)-1;
}

size_t csupport_path_parent_path_end(const char *path, size_t len, int style) {
  size_t end_pos = csupport_path_filename_pos_styled(path, len, style);
  int filename_was_sep = (len > 0 &&
      csupport_path_is_separator(path[end_pos], style));

  size_t root_dir_pos = csupport_path_root_dir_start(path, len, style);
  while (end_pos > 0 &&
         (root_dir_pos == (size_t)-1 || end_pos > root_dir_pos) &&
         csupport_path_is_separator(path[end_pos - 1], style))
    --end_pos;

  if (end_pos == root_dir_pos && !filename_was_sep) {
    return root_dir_pos + 1;
  }

  return end_pos;
}

void csupport_path_convert_backslash(char *buf, size_t len) {
  for (size_t i = 0; i < len; i++)
    if (buf[i] == '\\') buf[i] = '/';
}

int csupport_path_has_extension(const char *path, size_t len,
                                const char *ext, size_t ext_len) {
  if (len < ext_len) return 0;
  size_t fpos = csupport_path_filename_pos(path, len);
  const char *fname = path + fpos;
  size_t flen = len - fpos;
  if (flen < ext_len) return 0;
  for (size_t i = 0; i < ext_len; i++) {
    char a = fname[flen - ext_len + i];
    char b = ext[i];
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    if (a != b) return 0;
  }
  return 1;
}

size_t csupport_path_remove_dots(char *buf, size_t len, int remove_dot_dot) {
  size_t write = 0;
  size_t i = 0;
  size_t root_len = 0;

  while (i < len && (buf[i] == '/' || buf[i] == '\\')) {
    buf[write++] = buf[i++];
  }
  root_len = write;
  int absolute = (root_len > 0);

  typedef struct { size_t off; size_t len; } comp_t;
  comp_t comps[256];
  size_t ncomps = 0;

  while (i < len) {
    size_t comp_start = i;
    while (i < len && buf[i] != '/' && buf[i] != '\\') i++;
    size_t comp_len = i - comp_start;
    if (i < len) i++;

    if (comp_len == 0 || (comp_len == 1 && buf[comp_start] == '.')) {
      continue;
    }
    if (remove_dot_dot && comp_len == 2 &&
        buf[comp_start] == '.' && buf[comp_start + 1] == '.') {
      if (ncomps > 0 && !(comps[ncomps-1].len == 2 &&
            buf[comps[ncomps-1].off] == '.' && buf[comps[ncomps-1].off+1] == '.')) {
        ncomps--;
      } else if (!absolute && ncomps < 256) {
        comps[ncomps].off = comp_start;
        comps[ncomps].len = comp_len;
        ncomps++;
      }
      continue;
    }
    if (ncomps < 256) {
      comps[ncomps].off = comp_start;
      comps[ncomps].len = comp_len;
      ncomps++;
    }
  }

  write = root_len;
  for (size_t c = 0; c < ncomps; c++) {
    if (c > 0) buf[write++] = '/';
    memmove(buf + write, buf + comps[c].off, comps[c].len);
    write += comps[c].len;
  }
  if (write == 0) { buf[0] = '.'; write = 1; }
  buf[write] = '\0';
  return write;
}

size_t csupport_path_join(char *buf, size_t buflen,
                          const char *base, size_t base_len,
                          const char *name, size_t name_len) {
  if (!buf || buflen == 0) return 0;
  if (base_len == 0) {
    size_t n = name_len < buflen - 1 ? name_len : buflen - 1;
    memcpy(buf, name, n);
    buf[n] = '\0';
    return n;
  }
  size_t pos = 0;
  size_t n = base_len < buflen - 1 ? base_len : buflen - 1;
  memcpy(buf, base, n);
  pos = n;
  if (pos > 0 && buf[pos - 1] != '/' && buf[pos - 1] != '\\') {
    if (pos < buflen - 1) buf[pos++] = '/';
  }
  n = (pos < buflen - 1) ? (name_len < buflen - 1 - pos ? name_len : buflen - 1 - pos) : 0;
  memcpy(buf + pos, name, n);
  pos += n;
  buf[pos] = '\0';
  return pos;
}

size_t csupport_path_stem(const char *path, size_t len,
                          const char **out_stem, int style) {
  size_t fname_pos = csupport_path_filename_pos_styled(path, len, style);
  const char *fname = path + fname_pos;
  size_t fname_len = len - fname_pos;

  if (fname_len == 0) { *out_stem = fname; return 0; }
  if (fname_len == 1 && fname[0] == '.') { *out_stem = fname; return 1; }
  if (fname_len == 2 && fname[0] == '.' && fname[1] == '.') {
    *out_stem = fname; return 2;
  }

  size_t dot_pos = fname_len;
  for (size_t i = fname_len; i > 0; i--) {
    if (fname[i - 1] == '.') { dot_pos = i - 1; break; }
  }
  *out_stem = fname;
  return dot_pos;
}

size_t csupport_path_extension(const char *path, size_t len,
                               const char **out_ext, int style) {
  size_t fname_pos = csupport_path_filename_pos_styled(path, len, style);
  const char *fname = path + fname_pos;
  size_t fname_len = len - fname_pos;

  if (fname_len == 0) { *out_ext = path + len; return 0; }
  if (fname_len == 1 && fname[0] == '.') { *out_ext = path + len; return 0; }
  if (fname_len == 2 && fname[0] == '.' && fname[1] == '.') {
    *out_ext = path + len; return 0;
  }

  for (size_t i = fname_len; i > 0; i--) {
    if (fname[i - 1] == '.') {
      *out_ext = fname + i - 1;
      return fname_len - (i - 1);
    }
  }
  *out_ext = path + len;
  return 0;
}

size_t csupport_path_remove_leading_dotslash(const char *path, size_t len,
                                             const char **out, int style) {
  const char *p = path;
  size_t remaining = len;
  while (remaining > 2 && p[0] == '.' &&
         csupport_path_is_separator(p[1], style)) {
    p += 2; remaining -= 2;
    while (remaining > 0 && csupport_path_is_separator(p[0], style)) {
      p++; remaining--;
    }
  }
  *out = p;
  return remaining;
}

static char to_lower_char(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

int csupport_path_starts_with_insensitive(const char *path, size_t path_len,
                                          const char *prefix, size_t prefix_len,
                                          int style) {
  if (style == CSUPPORT_PATH_STYLE_WINDOWS ||
      style == CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH ||
      style == CSUPPORT_PATH_STYLE_WINDOWS_SLASH) {
    if (path_len < prefix_len) return 0;
    for (size_t i = 0; i < prefix_len; i++) {
      int sep_p = csupport_path_is_separator(path[i], style);
      int sep_x = csupport_path_is_separator(prefix[i], style);
      if (sep_p != sep_x) return 0;
      if (!sep_p && to_lower_char(path[i]) != to_lower_char(prefix[i]))
        return 0;
    }
    return 1;
  }
  if (path_len < prefix_len) return 0;
  return memcmp(path, prefix, prefix_len) == 0;
}

void csupport_path_make_preferred(char *buf, size_t len, int style) {
  if (style == CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH ||
      style == CSUPPORT_PATH_STYLE_WINDOWS) {
    for (size_t i = 0; i < len; i++)
      if (buf[i] == '/') buf[i] = '\\';
  } else if (style == CSUPPORT_PATH_STYLE_POSIX ||
             style == CSUPPORT_PATH_STYLE_WINDOWS_SLASH) {
    for (size_t i = 0; i < len; i++)
      if (buf[i] == '\\') buf[i] = '/';
  }
}

size_t csupport_path_replace_extension_buf(char *buf, size_t len,
                                           const char *ext, size_t ext_len,
                                           int style) {
  size_t fpos = csupport_path_filename_pos_styled(buf, len, style);
  size_t dot_pos = len;
  for (size_t i = len; i > fpos; i--) {
    if (buf[i - 1] == '.') { dot_pos = i - 1; break; }
  }
  size_t base = dot_pos;
  size_t pos = base;
  if (ext_len > 0 && ext[0] != '.') {
    if (pos < len + ext_len + 2) buf[pos++] = '.';
  }
  for (size_t i = 0; i < ext_len && pos < len + ext_len + 2; i++)
    buf[pos++] = ext[i];
  buf[pos] = '\0';
  return pos;
}

int csupport_path_is_absolute_styled(const char *path, size_t len, int style) {
  if (len == 0) return 0;
  if (style == CSUPPORT_PATH_STYLE_POSIX) {
    return path[0] == '/';
  }
  if (path[0] == '/' || path[0] == '\\') return 1;
  if (len >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') ||
                    (path[0] >= 'a' && path[0] <= 'z')) &&
      path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
    return 1;
  return 0;
}

size_t csupport_path_remove_dots_buf(
    const char *path, size_t len, int remove_dot_dot, int style,
    char *out, size_t out_cap) {
  if (len == 0) { if (out_cap > 0) out[0] = '\0'; return 0; }

  char sep = (style == CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH ||
              style == CSUPPORT_PATH_STYLE_WINDOWS) ? '\\' : '/';

  size_t root_len = 0;
  size_t rds = csupport_path_root_dir_start(path, len, style);
  if (rds != (size_t)-1) {
    root_len = rds + 1;
  } else {
    size_t fc = csupport_path_find_first_component(path, len, style);
    if (fc > 0 && len >= 2 && path[1] == ':') root_len = fc;
  }
  int absolute = (root_len > 0);

  const char *comp_ptrs[256];
  size_t comp_lens[256];
  size_t ncomp = 0;
  size_t i = root_len;
  while (i < len) {
    while (i < len && csupport_path_is_separator(path[i], style)) i++;
    if (i >= len) break;
    size_t start = i;
    while (i < len && !csupport_path_is_separator(path[i], style)) i++;
    size_t clen = i - start;

    if (clen == 1 && path[start] == '.') {
      continue;
    } else if (remove_dot_dot && clen == 2 && path[start] == '.' && path[start + 1] == '.') {
      if (ncomp > 0 && !(comp_lens[ncomp - 1] == 2 &&
                          comp_ptrs[ncomp - 1][0] == '.' &&
                          comp_ptrs[ncomp - 1][1] == '.')) {
        ncomp--;
      } else if (!absolute && ncomp < 256) {
        comp_ptrs[ncomp] = path + start;
        comp_lens[ncomp] = clen;
        ncomp++;
      }
    } else if (ncomp < 256) {
      comp_ptrs[ncomp] = path + start;
      comp_lens[ncomp] = clen;
      ncomp++;
    }
  }

  size_t pos = 0;
  for (size_t r = 0; r < root_len && pos < out_cap; r++) {
    char c = path[r];
    if (csupport_path_is_separator(c, style)) c = sep;
    out[pos++] = c;
  }
  for (size_t c = 0; c < ncomp; c++) {
    if (c > 0 || root_len > 0) {
      if (pos < out_cap) out[pos++] = sep;
    }
    for (size_t j = 0; j < comp_lens[c] && pos < out_cap; j++)
      out[pos++] = comp_ptrs[c][j];
  }
  if (pos < out_cap) out[pos] = '\0';
  return pos;
}

size_t csupport_path_native_buf(const char *path, size_t len,
                                char *out, size_t out_cap, int style) {
  if (!out || out_cap == 0) return 0;
  size_t n = len < out_cap - 1 ? len : out_cap - 1;
  memcpy(out, path, n);
  out[n] = '\0';
  csupport_path_make_preferred(out, n, style);
  return n;
}

int csupport_path_get_existing_style(const char *path, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (path[i] == '/')
      return CSUPPORT_PATH_STYLE_POSIX;
    if (path[i] == '\\')
      return CSUPPORT_PATH_STYLE_WINDOWS;
  }
  return CSUPPORT_PATH_STYLE_NATIVE;
}

int csupport_path_is_traversal_component(const char *comp, size_t len) {
  return (len == 2 && comp[0] == '.' && comp[1] == '.') ||
         (len == 1 && comp[0] == '.');
}

int csupport_path_has_traversal(const char *path, size_t len, int style) {
  size_t start = 0;
  for (size_t i = 0; i <= len; i++) {
    int is_sep = (i == len) || csupport_path_is_separator(path[i], style);
    if (is_sep) {
      size_t comp_len = i - start;
      if (comp_len > 0 &&
          csupport_path_is_traversal_component(path + start, comp_len))
        return 1;
      start = i + 1;
    }
  }
  return 0;
}

size_t csupport_path_canonicalize(const char *path, size_t len,
                                  char *out, size_t out_cap) {
  int style = csupport_path_get_existing_style(path, len);
  const char *dotslash_out = NULL;
  size_t after_dotslash_len = csupport_path_remove_leading_dotslash(
      path, len, &dotslash_out, style);
  return csupport_path_remove_dots_buf(
      dotslash_out, after_dotslash_len, 1, style, out, out_cap);
}

size_t csupport_path_expand_tilde(const char *path, size_t len,
                                  char *buf, size_t buflen) {
  if (!buf || buflen == 0) return 0;
  if (len == 0 || path[0] != '~') {
    size_t n = len < buflen - 1 ? len : buflen - 1;
    memcpy(buf, path, n);
    buf[n] = '\0';
    return n;
  }
  if (len == 1 || csupport_path_is_separator(path[1], CSUPPORT_PATH_STYLE_NATIVE)) {
    const char *home = getenv("HOME");
    if (!home) home = "";
    size_t hlen = strlen(home);
    size_t rest = len > 1 ? len - 1 : 0;
    size_t total = hlen + rest;
    if (total >= buflen) total = (buflen > 0) ? buflen - 1 : 0;
    size_t h = hlen < total ? hlen : total;
    memcpy(buf, home, h);
    size_t r = total - h;
    if (r > 0) memcpy(buf + h, path + 1, r);
    buf[total] = '\0';
    return total;
  }
  size_t n = (buflen > 0) ? (len < buflen - 1 ? len : buflen - 1) : 0;
  memcpy(buf, path, n);
  buf[n] = '\0';
  return n;
}

size_t csupport_path_append_styled(char *base, size_t base_len, size_t base_cap,
                                    const char *component, size_t comp_len,
                                    int style) {
  if (!base || base_cap == 0) return base_len;
  if (comp_len == 0) return base_len;
  if (base_len == 0) {
    size_t n = comp_len < base_cap - 1 ? comp_len : base_cap - 1;
    memcpy(base, component, n);
    base[n] = '\0';
    return n;
  }
  char sep = (style == CSUPPORT_PATH_STYLE_WINDOWS) ? '\\' : '/';
  int base_has_sep = csupport_path_is_separator(base[base_len - 1], style);
  int comp_has_sep = csupport_path_is_separator(component[0], style);
  if (!base_has_sep && !comp_has_sep) {
    if (base_len + 1 < base_cap) base[base_len++] = sep;
  } else if (base_has_sep && comp_has_sep) {
    component++;
    comp_len--;
  }
  size_t n = (base_len < base_cap - 1)
      ? (comp_len < base_cap - base_len - 1 ? comp_len : base_cap - base_len - 1)
      : 0;
  memcpy(base + base_len, component, n);
  base_len += n;
  base[base_len] = '\0';
  return base_len;
}

int csupport_path_is_relative_styled(const char *path, size_t len, int style) {
  return !csupport_path_is_absolute_styled(path, len, style);
}

size_t csupport_path_lexically_normal(const char *path, size_t len,
                                       char *buf, size_t buflen, int style) {
  return csupport_path_remove_dots_buf(path, len, 1, style, buf, buflen);
}

size_t csupport_path_split_components(const char *path, size_t path_len,
                                       const char **comps, size_t *comp_lens,
                                       size_t max_comps, int style) {
  size_t count = 0;
  size_t i = 0;
  while (i < path_len && count < max_comps) {
    while (i < path_len && csupport_path_is_separator(path[i], style)) i++;
    if (i >= path_len) break;
    size_t start = i;
    while (i < path_len && !csupport_path_is_separator(path[i], style)) i++;
    comps[count] = path + start;
    comp_lens[count] = i - start;
    count++;
  }
  return count;
}

size_t csupport_path_join2(const char *a, size_t a_len,
                            const char *b, size_t b_len,
                            char *buf, size_t buflen, int style) {
  if (!buf || buflen == 0) return 0;
  if (a_len == 0) {
    size_t n = b_len < buflen - 1 ? b_len : buflen - 1;
    memcpy(buf, b, n);
    buf[n] = '\0';
    return n;
  }
  if (b_len == 0) {
    size_t n = a_len < buflen - 1 ? a_len : buflen - 1;
    memcpy(buf, a, n);
    buf[n] = '\0';
    return n;
  }
  if (csupport_path_is_absolute_styled(b, b_len, style)) {
    size_t n = b_len < buflen - 1 ? b_len : buflen - 1;
    memcpy(buf, b, n);
    buf[n] = '\0';
    return n;
  }
  size_t pos = a_len < buflen - 1 ? a_len : buflen - 1;
  memcpy(buf, a, pos);
  int has_sep = csupport_path_is_separator(a[a_len - 1], style);
  if (!has_sep && pos + 1 < buflen) {
    buf[pos++] = (style == CSUPPORT_PATH_STYLE_WINDOWS) ? '\\' : '/';
  }
  size_t n = b_len < (buflen - pos - 1) ? b_len : (buflen - pos - 1);
  memcpy(buf + pos, b, n);
  pos += n;
  buf[pos] = '\0';
  return pos;
}

size_t csupport_path_normalize_separators(const char *path, size_t len,
                                           char *buf, size_t buflen,
                                           int style) {
  if (!buf || buflen == 0) return 0;
  char target_sep = (style == CSUPPORT_PATH_STYLE_WINDOWS) ? '\\' : '/';
  size_t n = len < buflen - 1 ? len : buflen - 1;
  for (size_t i = 0; i < n; i++) {
    if (csupport_path_is_separator(path[i], style))
      buf[i] = target_sep;
    else
      buf[i] = path[i];
  }
  buf[n] = '\0';
  return n;
}

int csupport_path_has_root_name(const char *path, size_t len, int style) {
  if (style != CSUPPORT_PATH_STYLE_WINDOWS) return 0;
  if (len >= 2 && ((path[0] >= 'A' && path[0] <= 'Z') ||
                    (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':')
    return 1;
  if (len >= 2 && csupport_path_is_separator(path[0], style) &&
      csupport_path_is_separator(path[1], style))
    return 1;
  return 0;
}

int csupport_path_is_root_path(const char *path, size_t len, int style) {
  if (len == 0) return 0;
  if (len == 1 && csupport_path_is_separator(path[0], style)) return 1;
  if (style == CSUPPORT_PATH_STYLE_WINDOWS && len == 3 &&
      ((path[0] >= 'A' && path[0] <= 'Z') ||
       (path[0] >= 'a' && path[0] <= 'z')) &&
      path[1] == ':' && csupport_path_is_separator(path[2], style))
    return 1;
  return 0;
}

size_t csupport_path_common_prefix(const char *a, size_t a_len,
                                    const char *b, size_t b_len, int style) {
  size_t n = a_len < b_len ? a_len : b_len;
  size_t last_sep = 0;
  for (size_t i = 0; i < n; i++) {
    char ca = a[i], cb = b[i];
    if (style == CSUPPORT_PATH_STYLE_WINDOWS) {
      if (ca >= 'a' && ca <= 'z') ca -= 32;
      if (cb >= 'a' && cb <= 'z') cb -= 32;
      if (ca == '\\') ca = '/';
      if (cb == '\\') cb = '/';
    }
    if (ca != cb) break;
    if (csupport_path_is_separator(a[i], style)) last_sep = i + 1;
    if (i + 1 == n) last_sep = i + 1;
  }
  return last_sep;
}

size_t csupport_path_relative(const char *path, size_t path_len,
                               const char *base, size_t base_len,
                               char *buf, size_t buflen, int style) {
  size_t prefix = csupport_path_common_prefix(path, path_len, base, base_len, style);
  if (prefix == 0) {
    size_t n = path_len < buflen ? path_len : buflen;
    memcpy(buf, path, n);
    if (n < buflen) buf[n] = '\0';
    return n;
  }

  size_t pos = 0;
  size_t bi = prefix;
  while (bi < base_len) {
    if (csupport_path_is_separator(base[bi], style)) {
      if (pos + 3 <= buflen) {
        buf[pos++] = '.'; buf[pos++] = '.';
        buf[pos++] = (style == CSUPPORT_PATH_STYLE_WINDOWS) ? '\\' : '/';
      } else pos += 3;
    }
    bi++;
  }
  if (bi > prefix && !csupport_path_is_separator(base[base_len - 1], style)) {
    if (pos + 3 <= buflen) {
      buf[pos++] = '.'; buf[pos++] = '.';
      buf[pos++] = (style == CSUPPORT_PATH_STYLE_WINDOWS) ? '\\' : '/';
    } else pos += 3;
  }

  size_t rem = path_len - prefix;
  if (rem > 0) {
    size_t n = rem < (buflen - pos) ? rem : (buflen > pos ? buflen - pos : 0);
    if (n > 0) memcpy(buf + pos, path + prefix, n);
    pos += rem;
  }
  if (pos < buflen) buf[pos] = '\0';
  return pos;
}

int csupport_path_is_dotfile(const char *path, size_t len, int style) {
  size_t fname_pos = 0;
  for (size_t i = len; i > 0; ) {
    --i;
    if (csupport_path_is_separator(path[i], style)) { fname_pos = i + 1; break; }
  }
  if (fname_pos >= len) return 0;
  return (path[fname_pos] == '.' && (fname_pos + 1 >= len || path[fname_pos + 1] != '.'));
}

int csupport_path_matches_extension(const char *path, size_t path_len,
                                     const char *ext, size_t ext_len,
                                     int style) {
  (void)style;
  if (ext_len > path_len) return 0;
  size_t offset = path_len - ext_len;
  for (size_t i = 0; i < ext_len; i++) {
    char a = path[offset + i], b = ext[i];
    if (a != b) return 0;
  }
  return 1;
}

size_t csupport_path_get_parent(const char *path, size_t len) {
  if (!path || len == 0) return 0;
  size_t end = len;
  while (end > 0 && path[end-1] != '/' && path[end-1] != '\\') end--;
  if (end > 1) end--;
  return end;
}

size_t csupport_path_remove_trailing_sep(const char *path, size_t len) {
  while (len > 1 && csupport_path_is_separator(path[len-1], CSUPPORT_PATH_STYLE_NATIVE))
    len--;
  return len;
}

int csupport_path_is_network_path(const char *path, size_t len) {
  return len >= 2 && path[0] == '\\' && path[1] == '\\';
}

size_t csupport_path_system_temp_dir(char *buf, size_t cap) {
  if (!buf || cap == 0) return 0;
  const char *tmp = 0;
#ifdef _WIN32
  tmp = getenv("TEMP");
  if (!tmp) tmp = getenv("TMP");
#else
  tmp = getenv("TMPDIR");
#endif
  if (!tmp) tmp = "/tmp";
  size_t len = strlen(tmp);
  if (len >= cap) len = cap - 1;
  memcpy(buf, tmp, len);
  buf[len] = '\0';
  return len;
}

size_t csupport_path_replace_filename(const char *path, size_t path_len,
                                       const char *new_name, size_t name_len,
                                       char *buf, size_t cap) {
  if (!buf || cap == 0) return 0;
  size_t parent_len = 0;
  for (size_t i = path_len; i > 0; i--) {
    if (path[i-1] == '/' || path[i-1] == '\\') {
      parent_len = i;
      break;
    }
  }
  size_t total = parent_len + name_len;
  size_t n = total < cap - 1 ? total : cap - 1;
  if (parent_len > 0 && parent_len <= n) memcpy(buf, path, parent_len);
  size_t name_copy = n > parent_len ? n - parent_len : 0;
  if (name_copy > name_len) name_copy = name_len;
  if (name_copy > 0) memcpy(buf + parent_len, new_name, name_copy);
  buf[parent_len + name_copy] = '\0';
  return parent_len + name_copy;
}

size_t csupport_path_remove_filename(const char *path, size_t len,
                                      char *buf, size_t cap) {
  if (!buf || cap == 0) return 0;
  size_t last_sep = 0;
  int found = 0;
  for (size_t i = len; i > 0; i--) {
    if (path[i-1] == '/' || path[i-1] == '\\') {
      last_sep = i;
      found = 1;
      break;
    }
  }
  size_t result_len = found ? last_sep : 0;
  size_t n = result_len < cap - 1 ? result_len : cap - 1;
  if (n > 0) memcpy(buf, path, n);
  buf[n] = '\0';
  return n;
}

size_t csupport_path_count_components(const char *path, size_t len, int style) {
  if (len == 0) return 0;
  size_t count = 0;
  int in_sep = 1;
  for (size_t i = 0; i < len; i++) {
    if (csupport_path_is_separator(path[i], style)) {
      in_sep = 1;
    } else {
      if (in_sep) count++;
      in_sep = 0;
    }
  }
  return count;
}

size_t csupport_path_get_component(const char *path, size_t len,
                                    unsigned index, int style,
                                    const char **comp_start) {
  if (len == 0) { *comp_start = path; return 0; }
  unsigned current = 0;
  size_t start = 0;
  int in_sep = 1;
  for (size_t i = 0; i <= len; i++) {
    int is_sep = (i == len) || csupport_path_is_separator(path[i], style);
    if (is_sep && !in_sep) {
      if (current == index) {
        *comp_start = path + start;
        return i - start;
      }
      current++;
      in_sep = 1;
    } else if (!is_sep && in_sep) {
      start = i;
      in_sep = 0;
    }
  }
  *comp_start = path + len;
  return 0;
}

int csupport_path_is_valid_component(const char *comp, size_t len) {
  if (len == 0) return 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)comp[i];
    if (c == 0) return 0;
#ifdef _WIN32
    if (c == '/' || c == '\\' || c == ':' || c == '*' ||
        c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
      return 0;
#else
    if (c == '/') return 0;
#endif
  }
  return 1;
}

size_t csupport_path_make_absolute_buf(const char *cwd, size_t cwd_len,
                                        const char *path, size_t path_len,
                                        char *buf, size_t cap, int style) {
  if (path_len > 0 && csupport_path_is_separator(path[0], style)) return 0;
#ifdef _WIN32
  if (path_len >= 2 && ((path[0] >= 'A' && path[0] <= 'Z') ||
      (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':') return 0;
#endif
  size_t total = cwd_len + 1 + path_len;
  if (total >= cap) return 0;
  memcpy(buf, cwd, cwd_len);
  char sep = (style == 1) ? '\\' : '/';
  if (cwd_len > 0 && !csupport_path_is_separator(cwd[cwd_len-1], style))
    buf[cwd_len++] = sep;
  memcpy(buf + cwd_len, path, path_len);
  buf[cwd_len + path_len] = '\0';
  return cwd_len + path_len;
}

int csupport_path_is_hidden(const char *path, size_t len) {
  if (len == 0) return 0;
  size_t fname_start = 0;
  for (size_t i = len; i > 0; i--) {
    if (path[i-1] == '/' || path[i-1] == '\\') {
      fname_start = i;
      break;
    }
  }
  if (fname_start < len && path[fname_start] == '.') return 1;
  return 0;
}

size_t csupport_path_lexically_relative(const char *path, size_t path_len,
                                         const char *base, size_t base_len,
                                         char sep,
                                         char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t common = 0;
  size_t last_sep = 0;
  size_t min_len = path_len < base_len ? path_len : base_len;
  for (size_t i = 0; i < min_len; i++) {
    char pc = path[i], bc = base[i];
    if (sep == '\\') {
      if (pc == '/') pc = '\\';
      if (bc == '/') bc = '\\';
      if (pc >= 'A' && pc <= 'Z') pc = pc - 'A' + 'a';
      if (bc >= 'A' && bc <= 'Z') bc = bc - 'A' + 'a';
    }
    if (pc != bc) break;
    common = i + 1;
    if (path[i] == sep || path[i] == '/' || path[i] == '\\')
      last_sep = common;
  }
  if (common == base_len && (common == path_len || path[common] == sep ||
      path[common] == '/' || path[common] == '\\'))
    last_sep = common;
  else if (common == path_len && (common == base_len || base[common] == sep ||
      base[common] == '/' || base[common] == '\\'))
    last_sep = common;

  unsigned ups = 0;
  for (size_t i = last_sep; i < base_len; i++) {
    if (base[i] == sep || base[i] == '/' || base[i] == '\\') ups++;
  }
  if (last_sep < base_len) ups++;

  size_t pos = 0;
  for (unsigned i = 0; i < ups; i++) {
    if (i > 0 && pos < out_cap - 1) out[pos++] = sep;
    if (pos < out_cap - 1) out[pos++] = '.';
    if (pos < out_cap - 1) out[pos++] = '.';
  }
  size_t rest_start = last_sep;
  while (rest_start < path_len && (path[rest_start] == sep ||
         path[rest_start] == '/' || path[rest_start] == '\\'))
    rest_start++;
  if (rest_start < path_len) {
    if (pos > 0 && pos < out_cap - 1) out[pos++] = sep;
    size_t rest_len = path_len - rest_start;
    size_t copy = rest_len < out_cap - pos - 1 ? rest_len : out_cap - pos - 1;
    memcpy(out + pos, path + rest_start, copy);
    pos += copy;
  }
  if (pos == 0 && pos < out_cap - 1) out[pos++] = '.';
  out[pos] = '\0';
  return pos;
}

size_t csupport_path_append_component(const char *path, size_t path_len,
                                       const char *comp, size_t comp_len,
                                       char sep,
                                       char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  size_t copy = path_len < out_cap - 1 ? path_len : out_cap - 1;
  memcpy(out, path, copy);
  pos = copy;
  if (pos > 0 && out[pos - 1] != sep && out[pos - 1] != '/' && out[pos - 1] != '\\') {
    if (pos < out_cap - 1) out[pos++] = sep;
  }
  copy = comp_len < out_cap - pos - 1 ? comp_len : out_cap - pos - 1;
  memcpy(out + pos, comp, copy);
  pos += copy;
  out[pos] = '\0';
  return pos;
}

int csupport_path_is_special_name(const char *name, size_t len) {
  if (len == 1 && name[0] == '.') return 1;
  if (len == 2 && name[0] == '.' && name[1] == '.') return 1;
  return 0;
}

int csupport_path_has_parent(const char *path, size_t len, int style) {
  size_t pe = csupport_path_parent_path_end(path, len, style);
  return pe != (size_t)-1 && pe > 0;
}

size_t csupport_path_strip_trailing_separators(const char *path, size_t len,
                                                int style) {
  if (len == 0) return 0;
  while (len > 1) {
    char c = path[len - 1];
    if (c != '/' && (style != 1 || c != '\\')) break;
    len--;
  }
  return len;
}

int csupport_path_is_separator_char(char c, int style) {
  if (c == '/') return 1;
  if (style == 1 && c == '\\') return 1;
  return 0;
}

size_t csupport_path_filename_only(const char *path, size_t len, int style) {
  size_t pos = csupport_path_filename_pos_styled(path, len, style);
  return pos;
}

int csupport_path_ends_with_separator(const char *path, size_t len, int style) {
  if (len == 0) return 0;
  return csupport_path_is_separator_char(path[len - 1], style);
}

size_t csupport_path_replace_path_prefix(const char *path, size_t path_len,
                                          const char *old_prefix, size_t old_len,
                                          const char *new_prefix, size_t new_len,
                                          char *buf, size_t cap, int style) {
  if (path_len < old_len) return 0;
  int match = 1;
  for (size_t i = 0; i < old_len; i++) {
    char a = path[i], b = old_prefix[i];
    if (style == 1) {
      if (a >= 'A' && a <= 'Z') a += 32;
      if (b >= 'A' && b <= 'Z') b += 32;
      if (a == '\\') a = '/';
      if (b == '\\') b = '/';
    }
    if (a != b) { match = 0; break; }
  }
  if (!match) return 0;
  size_t suffix_len = path_len - old_len;
  size_t total = new_len + suffix_len;
  if (total >= cap) return 0;
  memcpy(buf, new_prefix, new_len);
  memcpy(buf + new_len, path + old_len, suffix_len);
  buf[total] = '\0';
  return total;
}

/* -- File locking -- */

#ifdef _WIN32
int csupport_lock_file(int fd) { (void)fd; return -1; }
int csupport_unlock_file(int fd) { (void)fd; return -1; }
int csupport_try_lock_file(int fd, unsigned timeout_ms) {
  (void)fd; (void)timeout_ms; return -1;
}
#else
int csupport_lock_file(int fd) {
  struct flock lock;
  memset(&lock, 0, sizeof(lock));
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;
  if (fcntl(fd, F_SETLKW, &lock) != -1)
    return 0;
  return errno;
}

int csupport_unlock_file(int fd) {
  struct flock lock;
  lock.l_type = F_UNLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;
  if (fcntl(fd, F_SETLK, &lock) != -1)
    return 0;
  return errno;
}

int csupport_try_lock_file(int fd, unsigned timeout_ms) {
  struct timespec start, now;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (;;) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    if (fcntl(fd, F_SETLK, &lock) != -1)
      return 0;
    int e = errno;
    if (e != EACCES && e != EAGAIN)
      return e;
    usleep(1000);
    clock_gettime(CLOCK_MONOTONIC, &now);
    unsigned elapsed = (unsigned)((now.tv_sec - start.tv_sec) * 1000 +
                                  (now.tv_nsec - start.tv_nsec) / 1000000);
    if (elapsed >= timeout_ms)
      return EAGAIN;
  }
}
#endif

/* -- test_dir / getprogpath -- */

#ifndef _WIN32
int csupport_test_dir(char ret[], int pathmax, const char *dir, const char *bin) {
  struct stat sb;
  char fullpath[PATH_MAX];
  int chars = snprintf(fullpath, (size_t)pathmax, "%s/%s", dir, bin);
  if (chars >= pathmax)
    return 1;
  if (!realpath(fullpath, ret))
    return 1;
  if (stat(fullpath, &sb) != 0)
    return 1;
  return 0;
}

char *csupport_getprogpath(char ret[], int pathmax, const char *bin) {
  if (bin == NULL)
    return NULL;
  if (bin[0] == '/') {
    if (csupport_test_dir(ret, pathmax, "/", bin) == 0)
      return ret;
    return NULL;
  }
  if (strchr(bin, '/')) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, (size_t)pathmax))
      return NULL;
    if (csupport_test_dir(ret, pathmax, cwd, bin) == 0)
      return ret;
    return NULL;
  }
  char *pv = getenv("PATH");
  if (pv == NULL)
    return NULL;
  char *s = strdup(pv);
  if (!s)
    return NULL;
  char *state;
  for (char *t = strtok_r(s, ":", &state); t != NULL;
       t = strtok_r(NULL, ":", &state)) {
    if (csupport_test_dir(ret, pathmax, t, bin) == 0) {
      free(s);
      return ret;
    }
  }
  free(s);
  return NULL;
}
#else
int csupport_test_dir(char ret[], int pathmax, const char *dir, const char *bin) {
  (void)ret; (void)pathmax; (void)dir; (void)bin;
  return 1;
}
char *csupport_getprogpath(char ret[], int pathmax, const char *bin) {
  (void)ret; (void)pathmax; (void)bin;
  return NULL;
}
#endif

/* -- typeForMode -- */

int csupport_type_for_mode(unsigned mode) {
#ifndef _WIN32
  if (S_ISDIR(mode))  return CSUPPORT_FILE_TYPE_DIRECTORY;
  if (S_ISREG(mode))  return CSUPPORT_FILE_TYPE_REGULAR;
  if (S_ISBLK(mode))  return CSUPPORT_FILE_TYPE_BLOCK;
  if (S_ISCHR(mode))  return CSUPPORT_FILE_TYPE_CHARACTER;
  if (S_ISFIFO(mode)) return CSUPPORT_FILE_TYPE_FIFO;
  if (S_ISSOCK(mode)) return CSUPPORT_FILE_TYPE_SOCKET;
  if (S_ISLNK(mode))  return CSUPPORT_FILE_TYPE_SYMLINK;
#else
  (void)mode;
#endif
  return CSUPPORT_FILE_TYPE_UNKNOWN;
}

/* -- convertAccessMode -- */

int csupport_convert_access_mode(int mode) {
#ifndef _WIN32
  switch (mode) {
  case 0: return F_OK;
  case 1: return W_OK;
  case 2: return R_OK | X_OK;
  }
#else
  (void)mode;
#endif
  return 0;
}

/* -- nativeOpenFlags -- */

/*
 * disp: CD_CreateAlways=0, CD_CreateNew=1, CD_OpenExisting=2, CD_OpenAlways=3
 * flags: OF_Text=1, OF_CRLF=2, OF_Append=4, OF_ChildInherit=16
 * access_mode: FA_Read=1, FA_Write=2
 */
int csupport_native_open_flags(int disp, int flags, int access_mode) {
#ifndef _WIN32
  int result = 0;
  if (access_mode == 1)      result |= O_RDONLY;  /* FA_Read */
  else if (access_mode == 2) result |= O_WRONLY;  /* FA_Write */
  else if (access_mode == 3) result |= O_RDWR;    /* FA_Read | FA_Write */

  /* OF_Append => CD_OpenAlways */
  if (flags & 4) disp = 3;

  switch (disp) {
  case 0: result |= O_CREAT | O_TRUNC; break;  /* CD_CreateAlways */
  case 1: result |= O_CREAT | O_EXCL; break;    /* CD_CreateNew */
  case 2: break;                                  /* CD_OpenExisting */
  case 3: result |= O_CREAT; break;              /* CD_OpenAlways */
  }

#ifndef __MVS__
  if (flags & 4) result |= O_APPEND;
#endif
#ifdef O_CLOEXEC
  if (!(flags & 16)) result |= O_CLOEXEC;
#endif
  return result;
#else
  (void)disp; (void)flags; (void)access_mode;
  return 0;
#endif
}

/* -- temp dir helpers -- */

const char *csupport_get_env_temp_dir(void) {
  static const char *vars[] = {"TMPDIR", "TMP", "TEMP", "TEMPDIR"};
  for (int i = 0; i < 4; i++) {
    const char *d = getenv(vars[i]);
    if (d) return d;
  }
  return NULL;
}

const char *csupport_get_default_temp_dir(int erased_on_reboot) {
#ifdef P_tmpdir
  if (P_tmpdir && P_tmpdir[0])
    return P_tmpdir;
#endif
  return erased_on_reboot ? "/tmp" : "/var/tmp";
}

/* -- /proc/self/fd availability -- */

int csupport_has_proc_self_fd(void) {
#if !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(_WIN32)
  static int result = -1;
  if (result < 0)
    result = (access("/proc/self/fd", R_OK) == 0) ? 1 : 0;
  return result;
#else
  return 0;
#endif
}

/* -- signal alt stack -- */

void csupport_create_sig_alt_stack(void) {
#if !defined(_WIN32)
  stack_t old_stack;
  size_t alt_stack_size = MINSIGSTKSZ + 64 * 1024;
  if (sigaltstack(NULL, &old_stack) != 0 ||
      (old_stack.ss_flags & SS_ONSTACK) ||
      (old_stack.ss_sp && old_stack.ss_size >= alt_stack_size))
    return;
  stack_t new_stack;
  memset(&new_stack, 0, sizeof(new_stack));
  new_stack.ss_sp = malloc(alt_stack_size);
  if (!new_stack.ss_sp) return;
  new_stack.ss_size = alt_stack_size;
  if (sigaltstack(&new_stack, NULL) != 0)
    free(new_stack.ss_sp);
#endif
}

/* -- mmap helpers -- */

void csupport_munmap(void *addr, size_t len) {
#ifndef _WIN32
  if (addr)
    munmap(addr, len);
#else
  (void)addr; (void)len;
#endif
}

void csupport_madvise_dontneed(void *addr, size_t len) {
#if !defined(_WIN32) && !defined(__MVS__) && !defined(_AIX)
  if (!addr) return;
#if defined(POSIX_MADV_DONTNEED)
  posix_madvise(addr, len, POSIX_MADV_DONTNEED);
#else
  madvise(addr, len, MADV_DONTNEED);
#endif
#else
  (void)addr; (void)len;
#endif
}

/* -- Apple copy_file -- */

int csupport_copy_file_apfs(const char *from, const char *to) {
#ifdef __APPLE__
#if __has_builtin(__builtin_available)
  if (__builtin_available(macos 10.12, *)) {
    if (!clonefile(from, to, 0))
      return 0;
    int e = errno;
    switch (e) {
    case EEXIST: case ENOTSUP: case EXDEV: break;
    default: return e;
    }
  }
#endif
  if (!copyfile(from, to, NULL, COPYFILE_DATA))
    return 0;
  return errno;
#else
  (void)from; (void)to;
  return -1;
#endif
}

/* -- copy between file descriptors -- */

#ifndef _WIN32
int csupport_copy_fd(int read_fd, int write_fd) {
#if defined(__linux__)
  struct stat st;
  if (fstat(read_fd, &st) == 0 && S_ISREG(st.st_mode)) {
    off_t off = 0;
    size_t remaining = (size_t)st.st_size;
    while (remaining > 0) {
      ssize_t n = sendfile(write_fd, read_fd, &off, remaining);
      if (n < 0) {
        if (errno == EINTR) continue;
        if (errno == EINVAL || errno == ENOSYS)
          break;
        return errno;
      }
      if (n == 0) break;
      remaining -= (size_t)n;
    }
    if (remaining == 0) return 0;
    lseek(read_fd, off, SEEK_SET);
  }
#endif
  char buf[4096];
  for (;;) {
    ssize_t nr = read(read_fd, buf, sizeof(buf));
    if (nr == 0) break;
    if (nr < 0) {
      if (errno == EINTR) continue;
      return errno;
    }
    char *p = buf;
    ssize_t rem = nr;
    while (rem > 0) {
      ssize_t nw = write(write_fd, p, (size_t)rem);
      if (nw < 0) {
        if (errno == EINTR) continue;
        return errno;
      }
      p += nw;
      rem -= nw;
    }
  }
  return 0;
}
#else
int csupport_copy_fd(int read_fd, int write_fd) {
  (void)read_fd; (void)write_fd;
  return -1;
}
#endif

/* -- Apple findModulesAndOffsets for stack traces -- */

int csupport_find_modules_offsets_apple(void **stack_trace, int depth,
                                        const char **modules,
                                        intptr_t *offsets) {
#if defined(__APPLE__) && defined(__LP64__)
  uint32_t num_imgs = _dyld_image_count();
  for (uint32_t img = 0; img < num_imgs; img++) {
    const char *name = _dyld_get_image_name(img);
    intptr_t slide = _dyld_get_image_vmaddr_slide(img);
    const struct mach_header_64 *hdr =
        (const struct mach_header_64 *)_dyld_get_image_header(img);
    if (!hdr) continue;
    const struct load_command *cmd =
        (const struct load_command *)((const char *)hdr + sizeof(*hdr));
    for (uint32_t c = 0; c < hdr->ncmds; c++) {
      uint32_t base_cmd = cmd->cmd & ~LC_REQ_DYLD;
      if (base_cmd == LC_SEGMENT_64) {
        const struct segment_command_64 *seg =
            (const struct segment_command_64 *)cmd;
        for (int j = 0; j < depth; j++) {
          if (modules[j]) continue;
          intptr_t addr = (intptr_t)stack_trace[j];
          if ((intptr_t)seg->vmaddr + slide <= addr &&
              addr < (intptr_t)(seg->vmaddr + seg->vmsize + slide)) {
            modules[j] = name;
            offsets[j] = addr - slide;
          }
        }
      }
      cmd = (const struct load_command *)((const char *)cmd + cmd->cmdsize);
    }
  }
  return 1;
#else
  (void)stack_trace; (void)depth; (void)modules; (void)offsets;
  return 0;
#endif
}

/* -- getUmask -- */

#ifdef _WIN32
unsigned csupport_get_umask(void) { return 0; }
#else
unsigned csupport_get_umask(void) {
  unsigned mask = (unsigned)umask(0);
  (void)umask((mode_t)mask);
  return mask;
}
#endif

/* -- resize_file: returns 0 on success, errno-value on failure -- */

#ifdef _WIN32
int csupport_resize_file(int fd, uint64_t size) {
#ifdef HAVE__CHSIZE_S
  return (int)_chsize_s(fd, (__int64)size);
#else
  return (int)_chsize(fd, (long)size);
#endif
}
#else
int csupport_resize_file(int fd, uint64_t size) {
  if (ftruncate(fd, (off_t)size) == -1)
    return errno;
  return 0;
}
#endif

/* -- open() with EINTR retry + O_CLOEXEC fallback -- */

int csupport_open_file_retry(const char *path, int flags, unsigned mode) {
#ifndef _WIN32
  int fd;
  do {
    fd = open(path, flags, (mode_t)mode);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) return -1;
#ifndef O_CLOEXEC
  if (!(flags & O_CLOEXEC)) {
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
  }
#endif
  return fd;
#else
  (void)path; (void)flags; (void)mode;
  return -1;
#endif
}

/* -- resolve real path from fd -- */

size_t csupport_resolve_fd_path(int fd, const char *original_path,
                                char *buf, size_t cap) {
#ifndef _WIN32
  char tmp[PATH_MAX];
#if defined(F_GETPATH)
  if (fcntl(fd, F_GETPATH, tmp) != -1) {
    size_t len = strlen(tmp);
    if (len < cap) {
      memcpy(buf, tmp, len);
      buf[len] = '\0';
      return len;
    }
  }
#else
  if (csupport_has_proc_self_fd()) {
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", fd);
    ssize_t n = readlink(proc_path, tmp, sizeof(tmp));
    if (n > 0) {
      size_t len = (size_t)n;
      if (len < cap) {
        memcpy(buf, tmp, len);
        buf[len] = '\0';
        return len;
      }
    }
  } else if (original_path) {
    if (realpath(original_path, tmp) != NULL) {
      size_t len = strlen(tmp);
      if (len < cap) {
        memcpy(buf, tmp, len);
        buf[len] = '\0';
        return len;
      }
    }
  }
#endif
  (void)fd; (void)original_path;
  return 0;
#else
  (void)fd; (void)original_path; (void)buf; (void)cap;
  return 0;
#endif
}

/* -- Apple getMainExecutable -- */

size_t csupport_get_main_executable_apple(char *buf, size_t cap) {
#ifdef __APPLE__
  char exe_path[PATH_MAX];
  uint32_t size = sizeof(exe_path);
  if (_NSGetExecutablePath(exe_path, &size) == 0) {
    char link_path[PATH_MAX];
    if (realpath(exe_path, link_path)) {
      size_t len = strlen(link_path);
      if (len < cap) {
        memcpy(buf, link_path, len);
        buf[len] = '\0';
        return len;
      }
    }
  }
#else
  (void)buf; (void)cap;
#endif
  return 0;
}

/* -- getMainExecutable for non-Apple Unix platforms -- */

#if !defined(__APPLE__) && !defined(_WIN32)

static size_t copy_to_buf(const char *src, size_t len, char *buf, size_t cap) {
  if (len >= cap) return 0;
  memcpy(buf, src, len);
  buf[len] = '\0';
  return len;
}

size_t csupport_get_main_executable_unix(const char *argv0, void *main_addr,
                                         char *buf, size_t cap) {
  char exe_path[PATH_MAX];

#if defined(__FreeBSD__)
#if __FreeBSD_version >= 1300057
  if (elf_aux_info(AT_EXECPATH, exe_path, sizeof(exe_path)) == 0) {
    char link_path[PATH_MAX];
    if (realpath(exe_path, link_path))
      return copy_to_buf(link_path, strlen(link_path), buf, cap);
  }
#endif
  if (csupport_getprogpath(exe_path, PATH_MAX, argv0) != NULL)
    return copy_to_buf(exe_path, strlen(exe_path), buf, cap);

#elif defined(_AIX) || defined(__DragonFly__) || defined(__FreeBSD_kernel__) || \
    defined(__NetBSD__)
  const char *curproc = "/proc/curproc/file";
  if (access(curproc, F_OK) == 0) {
    ssize_t len = readlink(curproc, exe_path, sizeof(exe_path));
    if (len > 0) {
      if (len >= (ssize_t)sizeof(exe_path) - 1)
        len = (ssize_t)sizeof(exe_path) - 1;
      exe_path[len] = '\0';
      return copy_to_buf(exe_path, (size_t)len, buf, cap);
    }
  }
  if (csupport_getprogpath(exe_path, PATH_MAX, argv0) != NULL)
    return copy_to_buf(exe_path, strlen(exe_path), buf, cap);

#elif defined(__linux__) || defined(__CYGWIN__) || defined(__gnu_hurd__)
  const char *aPath = "/proc/self/exe";
  if (access(aPath, F_OK) == 0) {
    ssize_t len = readlink(aPath, exe_path, sizeof(exe_path));
    if (len >= 0) {
      if (len >= (ssize_t)sizeof(exe_path) - 1)
        len = (ssize_t)sizeof(exe_path) - 1;
      exe_path[len] = '\0';
      char *real = realpath(exe_path, NULL);
      if (real) {
        size_t rlen = strlen(real);
        size_t r = copy_to_buf(real, rlen, buf, cap);
        free(real);
        return r;
      }
      return copy_to_buf(exe_path, (size_t)len, buf, cap);
    }
  }
  if (csupport_getprogpath(exe_path, PATH_MAX, argv0) != NULL)
    return copy_to_buf(exe_path, strlen(exe_path), buf, cap);

#elif defined(__OpenBSD__) || defined(__HAIKU__)
  if (csupport_getprogpath(exe_path, PATH_MAX, argv0) != NULL)
    return copy_to_buf(exe_path, strlen(exe_path), buf, cap);

#elif defined(__sun__) && defined(__svr4__)
  const char *aPath = "/proc/self/execname";
  if (access(aPath, F_OK) == 0) {
    int fd = open(aPath, O_RDONLY);
    if (fd != -1) {
      ssize_t len = read(fd, exe_path, sizeof(exe_path) - 1);
      close(fd);
      if (len > 0) {
        exe_path[len] = '\0';
        return copy_to_buf(exe_path, (size_t)len, buf, cap);
      }
    }
  }
  if (csupport_getprogpath(exe_path, PATH_MAX, argv0) != NULL)
    return copy_to_buf(exe_path, strlen(exe_path), buf, cap);

#elif defined(HAVE_DLFCN_H) && defined(HAVE_DLADDR)
  (void)argv0;
  Dl_info dlinfo;
  if (main_addr && dladdr(main_addr, &dlinfo) != 0) {
    char link_path[PATH_MAX];
    if (realpath(dlinfo.dli_fname, link_path))
      return copy_to_buf(link_path, strlen(link_path), buf, cap);
  }

#else
  (void)argv0; (void)main_addr;
#endif

  return 0;
}

#endif /* !__APPLE__ && !_WIN32 */

/* -- home_directory core -- */

#ifndef _WIN32
size_t csupport_get_home_dir(char *buf, size_t cap) {
  const char *home = getenv("HOME");
  if (!home) {
    long pw_buf_size = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (pw_buf_size <= 0) pw_buf_size = 16384;
    char *pw_buf = (char *)malloc((size_t)pw_buf_size);
    if (!pw_buf) return 0;
    struct passwd pwd;
    struct passwd *pw = NULL;
    getpwuid_r(getuid(), &pwd, pw_buf, (size_t)pw_buf_size, &pw);
    if (pw && pw->pw_dir) {
      size_t len = strlen(pw->pw_dir);
      if (len < cap) {
        memcpy(buf, pw->pw_dir, len);
        buf[len] = '\0';
        free(pw_buf);
        return len;
      }
    }
    free(pw_buf);
    return 0;
  }
  size_t len = strlen(home);
  if (len >= cap) return 0;
  memcpy(buf, home, len);
  buf[len] = '\0';
  return len;
}
#else
size_t csupport_get_home_dir(char *buf, size_t cap) {
  (void)buf; (void)cap;
  return 0;
}
#endif

/* -- Darwin conf dir -- */

size_t csupport_get_darwin_conf_dir(int temp_dir, char *buf, size_t cap) {
#if defined(__APPLE__) && defined(_CS_DARWIN_USER_TEMP_DIR) && defined(_CS_DARWIN_USER_CACHE_DIR)
  int conf_name = temp_dir ? _CS_DARWIN_USER_TEMP_DIR : _CS_DARWIN_USER_CACHE_DIR;
  size_t conf_len = confstr(conf_name, NULL, 0);
  if (conf_len > 0 && conf_len <= cap) {
    conf_len = confstr(conf_name, buf, cap);
    if (conf_len > 0 && conf_len <= cap) {
      if (buf[conf_len - 1] == '\0')
        conf_len--;
      return conf_len;
    }
  }
#else
  (void)temp_dir; (void)buf; (void)cap;
#endif
  return 0;
}

/* -- mmap file region -- */

#ifndef _WIN32
void *csupport_mmap_file(size_t size, int mode, int fd, uint64_t offset) {
  int flags = (mode == 1) ? MAP_SHARED : MAP_PRIVATE;
  int prot = (mode == 0) ? PROT_READ : (PROT_READ | PROT_WRITE);
#if defined(MAP_NORESERVE)
  flags |= MAP_NORESERVE;
#endif
#if defined(__APPLE__)
  if (mode == 0) {
#if defined(MAP_RESILIENT_CODESIGN)
    flags |= MAP_RESILIENT_CODESIGN;
#endif
#if defined(MAP_RESILIENT_MEDIA)
    flags |= MAP_RESILIENT_MEDIA;
#endif
  }
#endif
  void *mapping = mmap(NULL, size, prot, flags, fd, (off_t)offset);
  if (mapping == MAP_FAILED)
    return NULL;
  return mapping;
}
#else
void *csupport_mmap_file(size_t size, int mode, int fd, uint64_t offset) {
  (void)size; (void)mode; (void)fd; (void)offset;
  return NULL;
}
#endif

/* ================================================================ */
/* Session 10: batch POSIX extractions from Path.inc                */
/* ================================================================ */

#ifndef _WIN32

int csupport_chdir(const char *path) {
  if (chdir(path) == -1) return errno;
  return 0;
}

int csupport_mkdir_p(const char *path, unsigned perms, int ignore_existing) {
  if (mkdir(path, (mode_t)perms) == -1) {
    if (errno != EEXIST || !ignore_existing)
      return errno;
  }
  return 0;
}

int csupport_symlink_path(const char *target, const char *linkpath) {
  if (symlink(target, linkpath) == -1) return errno;
  return 0;
}

int csupport_hardlink_path(const char *target, const char *linkpath) {
  if (link(target, linkpath) == -1) return errno;
  return 0;
}

int csupport_rename_path(const char *from, const char *to) {
  if (rename(from, to) == -1) return errno;
  return 0;
}

int csupport_chmod_path(const char *path, unsigned perms) {
  if (chmod(path, (mode_t)perms) != 0) return errno;
  return 0;
}

int csupport_chmod_fd(int fd, unsigned perms) {
  if (fchmod(fd, (mode_t)perms) != 0) return errno;
  return 0;
}

int csupport_fchown_fd(int fd, uint32_t owner, uint32_t group) {
  int rc;
  do {
    rc = fchown(fd, (uid_t)owner, (gid_t)group);
  } while (rc == -1 && errno == EINTR);
  if (rc < 0) return errno;
  return 0;
}

int csupport_remove_path(const char *path, int ignore_nonexisting) {
  struct stat buf;
  if (lstat(path, &buf) != 0) {
    if (errno != ENOENT || !ignore_nonexisting)
      return errno;
    return 0;
  }
  if (!S_ISREG(buf.st_mode) && !S_ISDIR(buf.st_mode) && !S_ISLNK(buf.st_mode))
    return -1;
  if (remove(path) == -1) {
    if (errno != ENOENT || !ignore_nonexisting)
      return errno;
  }
  return 0;
}

int csupport_access_path(const char *path, int mode) {
  int cmode = csupport_convert_access_mode(mode);
  if (access(path, cmode) == -1) return errno;
  if (mode == 2) {
    struct stat buf;
    if (stat(path, &buf) != 0)
      return EACCES;
    if (!S_ISREG(buf.st_mode))
      return EACCES;
  }
  return 0;
}

int csupport_set_file_times(int fd, int64_t atime_sec, int32_t atime_nsec,
                            int64_t mtime_sec, int32_t mtime_nsec) {
#if defined(HAVE_FUTIMENS)
  struct timespec times[2];
  times[0].tv_sec = (time_t)atime_sec;
  times[0].tv_nsec = (long)atime_nsec;
  times[1].tv_sec = (time_t)mtime_sec;
  times[1].tv_nsec = (long)mtime_nsec;
  if (futimens(fd, times) != 0) return errno;
  return 0;
#elif defined(HAVE_FUTIMES)
  struct timeval times[2];
  times[0].tv_sec = (time_t)atime_sec;
  times[0].tv_usec = (suseconds_t)(atime_nsec / 1000);
  times[1].tv_sec = (time_t)mtime_sec;
  times[1].tv_usec = (suseconds_t)(mtime_nsec / 1000);
  if (futimes(fd, times) != 0) return errno;
  return 0;
#else
  (void)fd; (void)atime_sec; (void)atime_nsec; (void)mtime_sec; (void)mtime_nsec;
  return ENOSYS;
#endif
}

int csupport_disk_space(const char *path,
                        uint64_t *capacity, uint64_t *free_space,
                        uint64_t *available) {
  struct CSUPPORT_STATVFS vfs;
  if (CSUPPORT_STATVFS(path, &vfs) != 0) return errno;
  uint64_t fr = CSUPPORT_FRSIZE(vfs);
  *capacity = (uint64_t)vfs.f_blocks * fr;
  *free_space = (uint64_t)vfs.f_bfree * fr;
  *available = (uint64_t)vfs.f_bavail * fr;
  return 0;
}

static int csupport_is_local_impl(struct CSUPPORT_STATVFS *vfs) {
#if defined(__linux__) || defined(__GNU__)
#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC 0x6969
#endif
#ifndef SMB_SUPER_MAGIC
#define SMB_SUPER_MAGIC 0x517B
#endif
#ifndef CIFS_MAGIC_NUMBER
#define CIFS_MAGIC_NUMBER 0xFF534D42
#endif
#ifdef __GNU__
  switch ((uint32_t)vfs->__f_type) {
#else
  switch ((uint32_t)vfs->f_type) {
#endif
  case NFS_SUPER_MAGIC:
  case SMB_SUPER_MAGIC:
  case CIFS_MAGIC_NUMBER:
    return 0;
  default:
    return 1;
  }
#elif defined(__CYGWIN__) || defined(__HAIKU__) || defined(__MVS__)
  (void)vfs;
  return 0;
#elif defined(__Fuchsia__) || defined(__EMSCRIPTEN__)
  (void)vfs;
  return 1;
#elif defined(__sun)
  (void)vfs;
  return 1;
#elif defined(_AIX)
  (void)vfs;
  return 0;
#else
  return !!(CSUPPORT_FFLAGS(*vfs) & MNT_LOCAL);
#endif
}

int csupport_is_local_path(const char *path) {
  struct CSUPPORT_STATVFS vfs;
  if (CSUPPORT_STATVFS(path, &vfs) != 0) return -1;
  return csupport_is_local_impl(&vfs);
}

int csupport_is_local_fd(int fd) {
  struct CSUPPORT_STATVFS vfs;
  if (CSUPPORT_FSTATVFS(fd, &vfs) != 0) return -1;
  return csupport_is_local_impl(&vfs);
}

static int csupport_fill_stat_impl(int stat_ret, const struct stat *st,
                                   csupport_file_stat_t *out) {
  if (stat_ret != 0) {
    int e = errno;
    memset(out, 0, sizeof(*out));
    if (e == ENOENT || e == ENOTDIR)
      out->type = CSUPPORT_FILE_TYPE_NOT_FOUND;
    else
      out->type = CSUPPORT_FILE_TYPE_STATUS_ERROR;
    return e;
  }
  out->type = csupport_type_for_mode((unsigned)st->st_mode);
  out->perms = (unsigned)(st->st_mode & 07777);
  out->dev = (uint64_t)st->st_dev;
  out->nlinks = (uint32_t)st->st_nlink;
  out->ino = (uint64_t)st->st_ino;
  out->uid = (uint32_t)st->st_uid;
  out->gid = (uint32_t)st->st_gid;
  out->size = (uint64_t)st->st_size;
#if defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
  out->atime_sec = (int64_t)st->st_atimespec.tv_sec;
  out->atime_nsec = (uint32_t)st->st_atimespec.tv_nsec;
  out->mtime_sec = (int64_t)st->st_mtimespec.tv_sec;
  out->mtime_nsec = (uint32_t)st->st_mtimespec.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
  out->atime_sec = (int64_t)st->st_atim.tv_sec;
  out->atime_nsec = (uint32_t)st->st_atim.tv_nsec;
  out->mtime_sec = (int64_t)st->st_mtim.tv_sec;
  out->mtime_nsec = (uint32_t)st->st_mtim.tv_nsec;
#else
  out->atime_sec = (int64_t)st->st_atime;
  out->atime_nsec = 0;
  out->mtime_sec = (int64_t)st->st_mtime;
  out->mtime_nsec = 0;
#endif
  return 0;
}

int csupport_stat_path(const char *path, int follow, csupport_file_stat_t *out) {
  struct stat st;
  int rc = follow ? stat(path, &st) : lstat(path, &st);
  return csupport_fill_stat_impl(rc, &st, out);
}

int csupport_stat_fd(int fd, csupport_file_stat_t *out) {
  struct stat st;
  int rc = fstat(fd, &st);
  return csupport_fill_stat_impl(rc, &st, out);
}

size_t csupport_getcwd(char *buf, size_t cap) {
  const char *pwd = getenv("PWD");
  if (pwd && pwd[0] == '/') {
    struct stat pwd_st, dot_st;
    if (stat(pwd, &pwd_st) == 0 && stat(".", &dot_st) == 0 &&
        pwd_st.st_dev == dot_st.st_dev && pwd_st.st_ino == dot_st.st_ino) {
      size_t len = strlen(pwd);
      if (len < cap) {
        memcpy(buf, pwd, len);
        buf[len] = '\0';
        return len;
      }
    }
  }
  if (getcwd(buf, cap) != NULL) {
    return strlen(buf);
  }
  if (errno == ERANGE && cap < 65536) {
    char *tmp = (char *)malloc(65536);
    if (tmp && getcwd(tmp, 65536)) {
      size_t len = strlen(tmp);
      if (len < cap) {
        memcpy(buf, tmp, len);
        buf[len] = '\0';
        free(tmp);
        return len;
      }
      free(tmp);
    } else {
      free(tmp);
    }
  }
  return 0;
}

size_t csupport_realpath(const char *path, char *buf, size_t cap) {
  char resolved[PATH_MAX];
  if (realpath(path, resolved) == NULL) return 0;
  size_t len = strlen(resolved);
  if (len >= cap) return 0;
  memcpy(buf, resolved, len);
  buf[len] = '\0';
  return len;
}

size_t csupport_lookup_user_homedir(const char *username, char *buf, size_t cap) {
  long pw_buf_size = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (pw_buf_size <= 0) pw_buf_size = 16384;
  char *pw_buf = (char *)malloc((size_t)pw_buf_size);
  if (!pw_buf) return 0;
  struct passwd pwd;
  struct passwd *entry = NULL;
  getpwnam_r(username, &pwd, pw_buf, (size_t)pw_buf_size, &entry);
  if (!entry || !entry->pw_dir) { free(pw_buf); return 0; }
  size_t len = strlen(entry->pw_dir);
  if (len >= cap) { free(pw_buf); return 0; }
  memcpy(buf, entry->pw_dir, len);
  buf[len] = '\0';
  free(pw_buf);
  return len;
}

size_t csupport_expand_tilde_full(const char *path, size_t len,
                                  char *buf, size_t cap) {
  if (!buf || cap == 0) return 0;
  if (len == 0 || path[0] != '~')
    return csupport_path_expand_tilde(path, len, buf, cap);

  size_t i = 1;
  while (i < len && path[i] != '/' && path[i] != '\\')
    i++;

  if (i == 1)
    return csupport_path_expand_tilde(path, len, buf, cap);

  char user[256];
  size_t uname_len = i - 1;
  if (uname_len >= sizeof(user))
    return 0;
  memcpy(user, path + 1, uname_len);
  user[uname_len] = '\0';

  char home[PATH_MAX];
  size_t home_len = csupport_lookup_user_homedir(user, home, sizeof(home));
  if (home_len == 0 || home_len >= cap)
    return 0;

  size_t rem_len = (i < len) ? (len - i) : 0;
  if (home_len + rem_len >= cap)
    return 0;

  memcpy(buf, home, home_len);
  if (rem_len > 0)
    memcpy(buf + home_len, path + i, rem_len);
  buf[home_len + rem_len] = '\0';
  return home_len + rem_len;
}

int csupport_read_native(int fd, char *buf, size_t size, ssize_t *out_bytes) {
#if defined(__APPLE__)
  if (size > (size_t)0x7FFFFFFF) size = (size_t)0x7FFFFFFF;
#endif
  ssize_t nr;
  do {
    nr = read(fd, buf, size);
  } while (nr == -1 && errno == EINTR);
  if (nr == -1) return errno;
  *out_bytes = nr;
  return 0;
}

int csupport_pread_native(int fd, char *buf, size_t size,
                          uint64_t offset, ssize_t *out_bytes) {
#if defined(__APPLE__)
  if (size > (size_t)0x7FFFFFFF) size = (size_t)0x7FFFFFFF;
#endif
#ifdef HAVE_PREAD
  ssize_t nr;
  do {
    nr = pread(fd, buf, size, (off_t)offset);
  } while (nr == -1 && errno == EINTR);
  if (nr == -1) return errno;
  *out_bytes = nr;
  return 0;
#else
  if (lseek(fd, (off_t)offset, SEEK_SET) == (off_t)-1) return errno;
  return csupport_read_native(fd, buf, size, out_bytes);
#endif
}

void *csupport_opendir(const char *path) {
  return (void *)opendir(path);
}

void csupport_closedir(void *handle) {
  if (handle) closedir((DIR *)handle);
}

int csupport_readdir_entry(void *handle, char *name_buf, size_t name_cap,
                           int *out_type) {
  errno = 0;
  struct dirent *ent = readdir((DIR *)handle);
  if (!ent) {
    if (errno != 0) return -1;
    return 0;
  }
  size_t nlen = strlen(ent->d_name);
  if (nlen >= name_cap) nlen = name_cap - 1;
  memcpy(name_buf, ent->d_name, nlen);
  name_buf[nlen] = '\0';
#if defined(DTTOIF)
  *out_type = csupport_type_for_mode(DTTOIF(ent->d_type));
#else
  *out_type = CSUPPORT_FILE_TYPE_UNKNOWN;
#endif
  return 1;
}

int csupport_remove_directories_recursive(const char *path, int ignore_errors) {
  DIR *dir = opendir(path);
  if (!dir) {
    if (ignore_errors && errno == ENOENT)
      return 0;
    return errno;
  }

  int ret = 0;
  for (;;) {
    errno = 0;
    struct dirent *ent = readdir(dir);
    if (!ent) {
      if (errno != 0 && !ret)
        ret = errno;
      break;
    }

    if ((ent->d_name[0] == '.' && ent->d_name[1] == '\0') ||
        (ent->d_name[0] == '.' && ent->d_name[1] == '.' &&
         ent->d_name[2] == '\0'))
      continue;

    char child[PATH_MAX];
    int n = snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
    if (n < 0 || (size_t)n >= sizeof(child)) {
      if (!ret) ret = ENAMETOOLONG;
      if (!ignore_errors) break;
      continue;
    }

    struct stat st;
    if (lstat(child, &st) != 0) {
      if (!ret) ret = errno;
      if (!ignore_errors) break;
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      int e = csupport_remove_directories_recursive(child, ignore_errors);
      if (e && !ret) ret = e;
      if (e && !ignore_errors) break;
    } else {
      if (remove(child) != 0) {
        if (!ret) ret = errno;
        if (!ignore_errors) break;
      }
    }
  }

  closedir(dir);
  if (ret && !ignore_errors)
    return ret;

  if (rmdir(path) != 0) {
    if (ignore_errors && errno == ENOENT)
      return 0;
    if (!ret) ret = errno;
  }
  return ignore_errors ? 0 : ret;
}

/* ===================================================================== */
/* Session 13: Directory lookup functions (pure C)                       */
/* ===================================================================== */

size_t csupport_user_config_directory(char *buf, size_t cap) {
  if (!buf || cap == 0) return 0;
#ifdef __APPLE__
  size_t hlen = csupport_get_home_dir(buf, cap);
  if (hlen == 0) return 0;
  const char *suffix = "/Library/Preferences";
  size_t slen = strlen(suffix);
  if (hlen + slen >= cap) return 0;
  memcpy(buf + hlen, suffix, slen);
  buf[hlen + slen] = '\0';
  return hlen + slen;
#else
  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0]) {
    size_t len = strlen(xdg);
    if (len >= cap) return 0;
    memcpy(buf, xdg, len);
    buf[len] = '\0';
    return len;
  }
  size_t hlen = csupport_get_home_dir(buf, cap);
  if (hlen == 0) return 0;
  const char *suffix = "/.config";
  size_t slen = strlen(suffix);
  if (hlen + slen >= cap) return 0;
  memcpy(buf + hlen, suffix, slen);
  buf[hlen + slen] = '\0';
  return hlen + slen;
#endif
}

size_t csupport_cache_directory(char *buf, size_t cap) {
  if (!buf || cap == 0) return 0;
#ifdef __APPLE__
  size_t len = csupport_get_darwin_conf_dir(0, buf, cap);
  if (len > 0) return len;
#else
  const char *xdg = getenv("XDG_CACHE_HOME");
  if (xdg && xdg[0]) {
    size_t len = strlen(xdg);
    if (len >= cap) return 0;
    memcpy(buf, xdg, len);
    buf[len] = '\0';
    return len;
  }
#endif
  size_t hlen = csupport_get_home_dir(buf, cap);
  if (hlen == 0) return 0;
  const char *suffix = "/.cache";
  size_t slen = strlen(suffix);
  if (hlen + slen >= cap) return 0;
  memcpy(buf + hlen, suffix, slen);
  buf[hlen + slen] = '\0';
  return hlen + slen;
}

size_t csupport_system_temp_directory(int erased_on_reboot, char *buf, size_t cap) {
  if (!buf || cap == 0) return 0;

  if (erased_on_reboot) {
    const char *env = csupport_get_env_temp_dir();
    if (env && env[0]) {
      size_t len = strlen(env);
      if (len >= cap) return 0;
      memcpy(buf, env, len);
      buf[len] = '\0';
      return len;
    }
  }

  size_t dlen = csupport_get_darwin_conf_dir(erased_on_reboot, buf, cap);
  if (dlen > 0) return dlen;

  const char *def = csupport_get_default_temp_dir(erased_on_reboot);
  if (def) {
    size_t len = strlen(def);
    if (len >= cap) return 0;
    memcpy(buf, def, len);
    buf[len] = '\0';
    return len;
  }
  return 0;
}

size_t csupport_find_program(const char *name, size_t name_len,
                             const char *const *extra_paths, size_t num_paths,
                             char *buf, size_t cap) {
  if (!name || name_len == 0 || !buf || cap == 0) return 0;

  for (size_t i = 0; i < name_len; i++) {
    if (name[i] == '/') {
      if (name_len >= cap) return 0;
      memcpy(buf, name, name_len);
      buf[name_len] = '\0';
      return name_len;
    }
  }

  const char *const *search_paths = extra_paths;
  size_t n_search = num_paths;

  char *env_path = NULL;
  char **env_dirs = NULL;
  size_t n_env = 0;

  if (!search_paths || n_search == 0) {
    const char *path_env = getenv("PATH");
    if (path_env) {
      env_path = strdup(path_env);
      if (env_path) {
        size_t max_dirs = 1;
        for (const char *p = env_path; *p; p++)
          if (*p == ':') max_dirs++;
        env_dirs = (char **)calloc(max_dirs, sizeof(char *));
        if (env_dirs) {
          char *tok = env_path;
          while (tok) {
            char *sep = strchr(tok, ':');
            if (sep) *sep = '\0';
            if (tok[0])
              env_dirs[n_env++] = tok;
            tok = sep ? sep + 1 : NULL;
          }
          search_paths = (const char *const *)env_dirs;
          n_search = n_env;
        }
      }
    }
  }

  size_t result = 0;
  for (size_t i = 0; i < n_search; i++) {
    if (!search_paths[i] || !search_paths[i][0]) continue;
    size_t dlen = strlen(search_paths[i]);
    if (dlen + 1 + name_len >= cap) continue;
    memcpy(buf, search_paths[i], dlen);
    buf[dlen] = '/';
    memcpy(buf + dlen + 1, name, name_len);
    buf[dlen + 1 + name_len] = '\0';
    if (access(buf, X_OK) == 0) {
      result = dlen + 1 + name_len;
      break;
    }
  }

  if (env_dirs) free(env_dirs);
  if (env_path) free(env_path);
  return result;
}

#else /* _WIN32 stubs */

int csupport_chdir(const char *p) { (void)p; return ENOSYS; }
int csupport_mkdir_p(const char *p, unsigned m, int i) { (void)p;(void)m;(void)i; return ENOSYS; }
int csupport_symlink_path(const char *t, const char *l) { (void)t;(void)l; return ENOSYS; }
int csupport_hardlink_path(const char *t, const char *l) { (void)t;(void)l; return ENOSYS; }
int csupport_rename_path(const char *f, const char *t) { (void)f;(void)t; return ENOSYS; }
int csupport_chmod_path(const char *p, unsigned m) { (void)p;(void)m; return ENOSYS; }
int csupport_chmod_fd(int f, unsigned m) { (void)f;(void)m; return ENOSYS; }
int csupport_fchown_fd(int f, uint32_t o, uint32_t g) { (void)f;(void)o;(void)g; return ENOSYS; }
int csupport_remove_path(const char *p, int i) { (void)p;(void)i; return ENOSYS; }
int csupport_access_path(const char *p, int m) { (void)p;(void)m; return ENOSYS; }
int csupport_set_file_times(int f, int64_t a, int32_t an, int64_t m, int32_t mn)
{ (void)f;(void)a;(void)an;(void)m;(void)mn; return ENOSYS; }
int csupport_disk_space(const char *p, uint64_t *c, uint64_t *f, uint64_t *a)
{ (void)p;(void)c;(void)f;(void)a; return ENOSYS; }
int csupport_is_local_path(const char *p) { (void)p; return -1; }
int csupport_is_local_fd(int f) { (void)f; return -1; }
int csupport_stat_path(const char *p, int fo, csupport_file_stat_t *o) { (void)p;(void)fo;(void)o; return ENOSYS; }
int csupport_stat_fd(int f, csupport_file_stat_t *o) { (void)f;(void)o; return ENOSYS; }
size_t csupport_getcwd(char *b, size_t c) { (void)b;(void)c; return 0; }
size_t csupport_realpath(const char *p, char *b, size_t c) { (void)p;(void)b;(void)c; return 0; }
size_t csupport_lookup_user_homedir(const char *u, char *b, size_t c) { (void)u;(void)b;(void)c; return 0; }
size_t csupport_expand_tilde_full(const char *p, size_t l, char *b, size_t c)
{ (void)p;(void)l;(void)b;(void)c; return 0; }
int csupport_read_native(int f, char *b, size_t s, ssize_t *o) { (void)f;(void)b;(void)s;(void)o; return ENOSYS; }
int csupport_pread_native(int f, char *b, size_t s, uint64_t o2, ssize_t *o) { (void)f;(void)b;(void)s;(void)o2;(void)o; return ENOSYS; }
void *csupport_opendir(const char *p) { (void)p; return 0; }
void csupport_closedir(void *h) { (void)h; }
int csupport_readdir_entry(void *h, char *n, size_t c, int *t) { (void)h;(void)n;(void)c;(void)t; return 0; }
int csupport_remove_directories_recursive(const char *p, int i)
{ (void)p;(void)i; return ENOSYS; }
size_t csupport_get_main_executable_unix(const char *a, void *m, char *b, size_t c)
{ (void)a;(void)m;(void)b;(void)c; return 0; }
size_t csupport_user_config_directory(char *b, size_t c) { (void)b;(void)c; return 0; }
size_t csupport_cache_directory(char *b, size_t c) { (void)b;(void)c; return 0; }
size_t csupport_system_temp_directory(int e, char *b, size_t c) { (void)e;(void)b;(void)c; return 0; }
size_t csupport_find_program(const char *n, size_t nl, const char *const *p, size_t np,
                             char *b, size_t c)
{ (void)n;(void)nl;(void)p;(void)np;(void)b;(void)c; return 0; }

#endif /* _WIN32 */
