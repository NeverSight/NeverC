/*===- GraphWriter.c - Graph visualization utilities (pure C) --*- C -*-===*/
#include "include/csupport/lgraph_lwriter.h"
#include "include/csupport/buffer.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define PATH_SEP '\\'
#define PATH_LIST_SEP ';'
#else
#include <unistd.h>
#define PATH_SEP '/'
#define PATH_LIST_SEP ':'
#endif

int csupport_find_program_in_path(const char *name,
                                  char *out_path, size_t out_size) {
  if (!name || !out_path || out_size == 0)
    return 0;
  const char *path_env = getenv("PATH");
  if (!path_env) return 0;

  size_t name_len = strlen(name);
  const char *p = path_env;
  for (;;) {
    const char *end = strchr(p, PATH_LIST_SEP);
    size_t dir_len = end ? (size_t)(end - p) : strlen(p);
    const char *directory = p;
    if (dir_len == 0) {
      directory = ".";
      dir_len = 1;
    }
    if (name_len <= SIZE_MAX - 2 &&
        dir_len <= SIZE_MAX - name_len - 2) {
      size_t candidate_len = dir_len + 1 + name_len;
      char *candidate = (char *)malloc(candidate_len + 1);
      if (!candidate)
        return 0;
      memcpy(candidate, directory, dir_len);
      candidate[dir_len] = PATH_SEP;
      memcpy(candidate + dir_len + 1, name, name_len + 1);
#ifdef _WIN32
      FILE *f = fopen(candidate, "rb");
      if (f) { fclose(f);
#else
      if (access(candidate, X_OK) == 0) {
#endif
        if (candidate_len < out_size) {
          memcpy(out_path, candidate, candidate_len + 1);
          free(candidate);
          return 1;
        }
      }
      free(candidate);
    }
    if (!end) break;
    p = end + 1;
  }
  return 0;
}

int csupport_create_temp_file(const char *prefix, const char *suffix,
                              char *out_path, size_t out_size) {
  if (!prefix || !out_path || out_size == 0)
    return 0;
  const char *tmp = getenv("TMPDIR");
  if (!tmp) tmp = getenv("TMP");
  if (!tmp) tmp = "/tmp";

  size_t suffix_len = 0;
  int needed;
  if (suffix && suffix[0]) {
    suffix_len = strlen(suffix);
    needed =
        snprintf(out_path, out_size, "%s/%s-XXXXXX%s", tmp, prefix, suffix);
  } else {
    needed = snprintf(out_path, out_size, "%s/%s-XXXXXX", tmp, prefix);
  }
  if (needed < 0 || (size_t)needed >= out_size || suffix_len > INT_MAX)
    return 0;
#ifndef _WIN32
  int fd;
  if (suffix_len > 0)
    fd = mkstemps(out_path, (int)suffix_len);
  else
    fd = mkstemp(out_path);
  if (fd < 0) return 0;
  close(fd);
  return 1;
#else
  return 0;
#endif
}

size_t csupport_dot_escape_string(const char *src, size_t src_len,
                                  char *dst, size_t dst_cap) {
  csupport_obuf_t out = csupport_obuf(dst, dst_cap);
  for (size_t i = 0; i < src_len; i++) {
    char c = src[i];
    switch (c) {
    case '\n':
      csupport_obuf_write(&out, "\\n", 2);
      break;
    case '\t':
      csupport_obuf_write(&out, "  ", 2);
      break;
    case '\\':
      if (i + 1 < src_len) {
        char next = src[i + 1];
        if (next == 'l') { csupport_obuf_write(&out, "\\l", 2); i++; continue; }
        if (next == '|' || next == '{' || next == '}') {
          i++;
          csupport_obuf_put(&out, next);
          continue;
        }
      }
      csupport_obuf_write(&out, "\\\\", 2);
      break;
    case '{': case '}': case '<': case '>': case '|': case '"':
      csupport_obuf_put(&out, '\\');
      csupport_obuf_put(&out, c);
      break;
    default:
      csupport_obuf_put(&out, c);
      break;
    }
  }
  return csupport_obuf_finish(&out);
}

size_t csupport_replace_illegal_filename_chars(char *str, size_t len, char repl) {
#ifdef _WIN32
  const char *illegal = "\\/:?\"<>|";
#else
  const char *illegal = "/";
#endif
  size_t count = 0;
  for (size_t i = 0; i < len; i++) {
    for (const char *p = illegal; *p; p++) {
      if (str[i] == *p) {
        str[i] = repl;
        count++;
        break;
      }
    }
  }
  return count;
}

const char *csupport_dot_color_string(unsigned color_number) {
  static const char *colors[] = {
    "aaaaaa", "aa0000", "00aa00", "aa5500", "0055ff",
    "aa00aa", "00aaaa", "555555", "ff5555", "55ff55",
    "ffff55", "5555ff", "ff55ff", "55ffff", "ffaaaa",
    "aaffaa", "ffffaa", "aaaaff", "ffaaff", "aaffff"
  };
  return colors[color_number % 20];
}

const char *csupport_graph_program_name(int program) {
  switch (program) {
  case 0: return "dot";
  case 1: return "fdp";
  case 2: return "neato";
  case 3: return "twopi";
  case 4: return "circo";
  default: return "dot";
  }
}

size_t csupport_dot_format_node(char *buf, size_t buflen,
                                 const char *label, const char *shape,
                                 const char *color, int node_id) {
  csupport_obuf_t out = csupport_obuf(buf, buflen);
  const char *node_shape = shape ? shape : "record";
  const char *node_label = label ? label : "";
  csupport_obuf_printf(&out, "  Node%d [shape=", node_id);
  csupport_obuf_write(&out, node_shape, strlen(node_shape));
  if (color && color[0]) {
    csupport_obuf_write(&out, ",color=\"#", 9);
    csupport_obuf_write(&out, color, strlen(color));
    csupport_obuf_put(&out, '"');
  }
  csupport_obuf_write(&out, ",label=\"", 8);
  csupport_obuf_write(&out, node_label, strlen(node_label));
  csupport_obuf_write(&out, "\"];\n", 4);
  return csupport_obuf_finish(&out);
}

size_t csupport_dot_format_edge(char *buf, size_t buflen,
                                 int src_id, int dst_id,
                                 const char *label) {
  csupport_obuf_t out = csupport_obuf(buf, buflen);
  csupport_obuf_printf(&out, "  Node%d -> Node%d", src_id, dst_id);
  if (label && label[0]) {
    csupport_obuf_write(&out, " [label=\"", 9);
    csupport_obuf_write(&out, label, strlen(label));
    csupport_obuf_write(&out, "\"]", 2);
  }
  csupport_obuf_write(&out, ";\n", 2);
  return csupport_obuf_finish(&out);
}
