/*===- GraphWriter.c - Graph visualization utilities (pure C) --*- C -*-===*/
#include "include/csupport/lgraph_lwriter.h"
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
  const char *path_env = getenv("PATH");
  if (!path_env) return 0;

  char buf[4096];
  const char *p = path_env;
  while (*p) {
    const char *end = strchr(p, PATH_LIST_SEP);
    size_t dir_len = end ? (size_t)(end - p) : strlen(p);
    if (dir_len > 0 && dir_len < sizeof(buf) - strlen(name) - 2) {
      memcpy(buf, p, dir_len);
      buf[dir_len] = PATH_SEP;
      strcpy(buf + dir_len + 1, name);
#ifdef _WIN32
      FILE *f = fopen(buf, "rb");
      if (f) { fclose(f);
#else
      if (access(buf, X_OK) == 0) {
#endif
        size_t len = strlen(buf);
        if (len < out_size) {
          memcpy(out_path, buf, len + 1);
          return 1;
        }
      }
    }
    if (!end) break;
    p = end + 1;
  }
  return 0;
}

int csupport_create_temp_file(const char *prefix, const char *suffix,
                              char *out_path, size_t out_size) {
  const char *tmp = getenv("TMPDIR");
  if (!tmp) tmp = getenv("TMP");
  if (!tmp) tmp = "/tmp";

  int suffix_len = 0;
  if (suffix && suffix[0]) {
    suffix_len = (int)strlen(suffix);
    snprintf(out_path, out_size, "%s/%s-XXXXXX%s", tmp, prefix, suffix);
  } else {
    snprintf(out_path, out_size, "%s/%s-XXXXXX", tmp, prefix);
  }
#ifndef _WIN32
  int fd;
  if (suffix_len > 0)
    fd = mkstemps(out_path, suffix_len);
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
  if (!dst || dst_cap == 0) return 0;
  size_t out = 0;
#define EMIT(c) do { if (out + 1 < dst_cap) dst[out++] = (c); } while(0)
  for (size_t i = 0; i < src_len; i++) {
    char c = src[i];
    switch (c) {
    case '\n':
      EMIT('\\'); EMIT('n');
      break;
    case '\t':
      EMIT(' '); EMIT(' ');
      break;
    case '\\':
      if (i + 1 < src_len) {
        char next = src[i + 1];
        if (next == 'l') { EMIT('\\'); EMIT('l'); i++; continue; }
        if (next == '|' || next == '{' || next == '}') { i++; EMIT(next); continue; }
      }
      EMIT('\\'); EMIT('\\');
      break;
    case '{': case '}': case '<': case '>': case '|': case '"':
      EMIT('\\'); EMIT(c);
      break;
    default:
      EMIT(c);
      break;
    }
  }
#undef EMIT
  if (out < dst_cap) dst[out] = '\0';
  return out;
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
  if (!buf || buflen == 0) return 0;
  int n;
  if (color && color[0])
    n = snprintf(buf, buflen, "  Node%d [shape=%s,color=\"#%s\",label=\"%s\"];\n",
                 node_id, shape ? shape : "record", color, label ? label : "");
  else
    n = snprintf(buf, buflen, "  Node%d [shape=%s,label=\"%s\"];\n",
                 node_id, shape ? shape : "record", label ? label : "");
  return (n > 0 && (size_t)n < buflen) ? (size_t)n : buflen - 1;
}

size_t csupport_dot_format_edge(char *buf, size_t buflen,
                                 int src_id, int dst_id,
                                 const char *label) {
  if (!buf || buflen == 0) return 0;
  int n;
  if (label && label[0])
    n = snprintf(buf, buflen, "  Node%d -> Node%d [label=\"%s\"];\n",
                 src_id, dst_id, label);
  else
    n = snprintf(buf, buflen, "  Node%d -> Node%d;\n", src_id, dst_id);
  return (n > 0 && (size_t)n < buflen) ? (size_t)n : buflen - 1;
}
