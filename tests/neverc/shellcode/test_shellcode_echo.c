// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Real-world-ish shellcode: read up to 32 bytes from stdin, echo them
 * to stdout with a "> " prefix, then exit.  Stresses the svc pipeline
 * on read + write + exit simultaneously, and uses a mutable local
 * buffer that the pipeline must keep entirely on the stack.
 *
 * Loader contract:
 *   stdin is closed by the loader (argc == 2), so `read` returns 0 and
 *   we write just the "> \n" prefix + newline.
 *   stdout captured by the test harness: expect "> \n".
 *
 * The only external declarations are read/write/exit, replaced by
 * SyscallStubPass with inline `svc #0x80` sequences (Darwin).
 */
long read(int fd, void *buf, unsigned long n);
long write(int fd, const void *buf, unsigned long n);
void exit(int status);

int main(void) {
    char buf[32];
    for (int i = 0; i < 32; i++) buf[i] = 0;

    /* "> " prefix — two bytes, lives entirely on the stack. */
    const char prefix[] = {'>', ' '};
    write(1, prefix, 2);

    long n = read(0, buf, sizeof(buf));
    if (n > 0) {
        write(1, buf, (unsigned long)n);
    }

    const char nl = '\n';
    write(1, &nl, 1);
    exit(0);
    return 0;
}
