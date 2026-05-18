// Library default for a *global variable* `g_config`. The variable-override
// test pairs this with `override_data.c` and expects the override copy of
// `g_config` to win at link time.
int g_config = 1;
