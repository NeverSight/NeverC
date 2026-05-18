// Indirect-call test main: only calls `bar`, never `foo` directly. The
// override of `foo` must still take effect through `bar`'s internal call.
// Expected: bar() == 1042 (override 42 + 1000) instead of 1001 (default).
extern int bar(void);
int main(void) { return bar() == 1042 ? 0 : 1; }
