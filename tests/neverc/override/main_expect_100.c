// Main TU used by the "two override files" scenario where the alt override
// (returns 100) is linked last and must win over the regular override
// (returns 42). Exits 0 iff `compute()` returns 100.
extern int compute(void);
int main(void) { return compute() == 100 ? 0 : 1; }
