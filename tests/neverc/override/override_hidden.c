// Override + hidden visibility. Visibility controls dynamic exposure (e.g.
// whether dlsym can find the symbol in a shared library), but it must not
// affect the static link-time override resolution: this hidden override
// must still replace the library default at link time.
__attribute__((override, visibility("hidden")))
int compute(void) { return 42; }
