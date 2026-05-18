// Validates that all three overridden functions resolved to the override
// definitions (10+20+30 = 60), not the library defaults (1+2+3 = 6).
extern int alpha(void), beta(void), gamma(void);
int main(void) { return (alpha() + beta() + gamma()) == 60 ? 0 : 1; }
