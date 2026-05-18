// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* End-to-end integration test: NeverC builtin string + libsystem syscall.
 *
 * Combines two shellcode subsystems in a single payload:
 *
 *   * `StringRuntimePass`  — owned-string allocations (`+`, `.append()`,
 *                             `neverc_string_clone`, etc.) routed through the
 *                             stackified `__sc_string_alloc / __sc_string_free`
 *                             arena instead of libc heap.
 *   * `SyscallStubPass`    — extern `write(2)` / `exit(2)` lowered to inline
 *                             `svc #0x80` (Darwin) / `svc #0` (Linux) so the
 *                             payload has no dyld / libc dependency.
 *
 * Both passes inline their helpers into `main`; ZeroRelocPass(Stackify) then
 * promotes the arena onto `main`'s stack frame, leaving zero data section,
 * zero relocations, and zero unresolved externs.  The loader runs `main(a, b)`
 * and only checks stdout for the expected greeting — exit code is incidental
 * (the syscall may have torn the process down before `exit` returns).
 *
 * Why this test exists
 * ====================
 *
 * Every other `neverc_string_*` test exercises pure computation (no
 * libsystem), and `hello_complex` exercises libsystem with raw
 * `const char[]` / hand-rolled strlen.  Neither path proves the two
 * subsystems coexist when used together — a regression that, say,
 * accidentally re-introduces a `malloc` extern call inside the string
 * runtime would still pass both tests, but would fail this one because
 * the extractor's "no libc heap" gate would reject the payload during
 * compile.  Conversely, a regression that inlines `write` incorrectly
 * (e.g. dropping the syscall stub for an arena-allocated buffer) would
 * surface as missing stdout content here.
 *
 * Pinning the cross-subsystem contract via a single end-to-end payload
 * keeps the regression triage focused: when this test fails,
 * `compile-fail` means StringRuntimePass leaked an extern; `stdout-fail`
 * means SyscallStubPass mis-handled an arena-resident buffer.
 *
 * Expected stdout (literal): "shellcode says: hi via syscall!\n"
 */

extern long write(int fd, const void *buf, unsigned long n);
extern void exit(int status);

int main(int a, int b) {
    (void)a;
    (void)b;

    /* (1) Owned via `+`: `__neverc_string_cat` allocates from the
       stackified arena, copies both literal payloads, returns a
       `cap > 0` owned handle.  The intermediate operands are
       borrowed (`cap == 0`) literal-derived views, so the only
       buffer that touches the arena is the result. */
    string greeting = "shellcode says: " + "hi";

    /* (2) Append a third literal: `neverc_string_append` aliases
       `__neverc_string_cat`, so this releases the buffer from
       step (1) back to the free-list, allocates a fresh slot,
       and copies all three payloads.  The free-list slot from
       (1) is now eligible for reuse by any subsequent owned-
       string allocation in the same arena -- proves the arena
       can recycle blocks across consecutive mutator calls. */
    greeting = greeting.append(" via syscall!\n");

    /* (3) The loader observes both subsystems in one call: the
       arena-resident byte payload at `greeting.data` is handed
       to the syscall-lowered `write`.  No retain copy, no
       trampoline -- just a direct register pass-through. */
    write(1, greeting.data, greeting.len);

    /* (4) Release the owned buffer explicitly so the free-list
       has the slot available before we tear the process down.
       `neverc_string_free` is a no-op on borrowed handles, so this is
       safe even if a future refactor flips `greeting` to a view. */
    neverc_string_free(greeting);

    exit(0);
    return 0;
}
