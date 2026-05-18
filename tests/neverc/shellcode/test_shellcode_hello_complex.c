// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Realistic hello world: long string literal + hand-written strlen +
 * libSystem write/exit.
 *
 * Exercises:
 *   - User code keeps looking like plain C; no shellcode-specific tricks.
 *   - A 30-byte string literal that Data2TextPass must stackify; the
 *     backend must not fall back to `adrp + ldr` against __cstring.
 *   - my_strlen gets always_inline'd away.
 *   - write/exit get replaced by inline `svc #0x80` sequences.
 *
 * Expected stdout: "shellcode says: hello, world!\n", exit code 0.
 */
void write(int fd, const void *buf, unsigned long n);
void exit(int status);

static unsigned long my_strlen(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

int main(void) {
    const char banner[] = "shellcode says: hello, world!\n";
    write(1, banner, my_strlen(banner));
    exit(0);
    return 0;
}
