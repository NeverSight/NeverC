// Variable-override main: exits 0 iff the override copy of `g_config`
// (value 42) was selected instead of the library default (value 1).
extern int g_config;
int main(void) { return g_config == 42 ? 0 : 1; }
