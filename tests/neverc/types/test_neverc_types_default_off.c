// test_neverc_types_default_off.c — guard against accidentally re-enabling the
// Rust-style integer keywords by default.
//
// Background: third-party C codebases (Linux kernel, OpenSSL, Redis, etc.)
// frequently `typedef`/`#define` names like `u8`, `u32`, `usize`.  If NeverC
// turned those into reserved keywords by default, every such codebase would
// stop compiling under our driver.  This file pins the contract on the
// commit by:
//
//   1. Negative case: with the flag OFF (default) any bare `u32 v = 0;` must
//      be rejected as a syntax error (otherwise we silently reintroduced the
//      keywords).
//   2. Positive case: with `-fneverc-types` the same source must compile.
//   3. Third-party-style case: a translation unit that `typedef`s `u32`
//      itself must compile cleanly with the flag OFF — i.e. NeverC never
//      steals the identifier from the user.
//
// RUN: ! %neverc -std=c11 -fsyntax-only -DBARE_RUST_KEYWORD %s 2>&1 | grep -F "expected ';' after expression"
// RUN: %neverc -std=c11 -fneverc-types -fsyntax-only -DBARE_RUST_KEYWORD %s
// RUN: %neverc -std=c11 -fsyntax-only -DTHIRD_PARTY_TYPEDEF %s

#if defined(BARE_RUST_KEYWORD)

// Without -fneverc-types, `u32` is just an identifier — `u32 v = 0;` is then
// parsed as `u32 v = 0` (expression-statement), which triggers the
// "expected ';' after expression" we grep for above.
int main(void) {
    u32 v = 0;
    return (int)v;
}

#elif defined(THIRD_PARTY_TYPEDEF)

// Typical third-party flavour: the project owns these names itself.
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long  u64;

static u32 thirdparty_sum(u8 a, u16 b, u32 c, u64 d) {
    return (u32)(a + b + c + (u32)d);
}

int main(void) {
    return (int)thirdparty_sum(1, 2, 3, 4);
}

#else
#  error "must define one of BARE_RUST_KEYWORD / THIRD_PARTY_TYPEDEF"
#endif
