// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* LibSystem hello world via dyld resolver stub.
 * Calls write(1, "hi\n", 3) then exit(0).
 * Expected stdout: "hi"
 */
void write(int fd, const void *buf, unsigned long nbyte);
void exit(int status);

int main(void) {
    const char msg[] = {'h', 'i', '\n'};
    write(1, msg, 3);
    exit(0);
    return 0;
}
