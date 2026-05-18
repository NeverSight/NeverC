// Main TU used by all override scenarios. Exits 0 iff `compute()` returns 42
// — i.e. the override definition was selected over the library default.
extern int compute(void);
int main(void) { return compute() == 42 ? 0 : 1; }
