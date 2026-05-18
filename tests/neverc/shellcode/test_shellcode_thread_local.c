// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// `_Thread_local static int counter = N;` is typically used by
// single-threaded state machines or tests.  Shellcode only ever
// executes inside the host's calling thread (there is no `pthread_create`
// in the bin), so the TLS storage class is semantically equivalent to
// `static` in this context.
//
// ZeroRelocPass.Prep silently drops the TLS attribute (via
// `setThreadLocalMode(NotThreadLocal)`), after which the Stackify phase
// moves the variable onto the entry function's stack frame like any
// other mutable global.  No user-visible workaround is required.
//
// For args ignored: counter++ returns the pre-increment value (42).

static _Thread_local int counter = 42;

int main(int a, int b) {
    (void)a;
    (void)b;
    return counter++;
}
