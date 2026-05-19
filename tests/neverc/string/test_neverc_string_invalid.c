// NeverC builtin string safety diagnostics
// RUN: ! %neverc -std=c23 -fsyntax-only %s 2>&1 | grep -F "cannot take c_str/data from a temporary string"

int main(void) {
    const char *p = ("temporary " + "owned").c_str();
    const char *q = ("temporary " + "data").data();
    return p[0] + q[0];
}
