#ifndef CSUPPORT_LGRAPH_LWRITER_H
#define CSUPPORT_LGRAPH_LWRITER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_find_program_in_path(const char *name, char *out_path,
                                  size_t out_size);

int csupport_create_temp_file(const char *prefix, const char *suffix,
                              char *out_path, size_t out_size);

/* Escape a string for use as a DOT graph label.
   Handles \n, \t, special DOT chars ({}<>|"). Returns bytes written. */
size_t csupport_dot_escape_string(const char *src, size_t src_len, char *dst,
                                  size_t dst_cap);

/* Replace illegal filename characters in-place. Returns number replaced. */
size_t csupport_replace_illegal_filename_chars(char *str, size_t len,
                                               char repl);

/* Get a DOT color string for a given node number (round-robin from 20 colors).
 */
const char *csupport_dot_color_string(unsigned color_number);

/* Get graph program name by enum value (0=dot, 1=fdp, 2=neato, 3=twopi,
 * 4=circo). */
const char *csupport_graph_program_name(int program);

size_t csupport_dot_format_node(char *buf, size_t buflen, const char *label,
                                const char *shape, const char *color,
                                int node_id);
size_t csupport_dot_format_edge(char *buf, size_t buflen, int src_id,
                                int dst_id, const char *label);

#ifdef __cplusplus
}
#endif
#endif
