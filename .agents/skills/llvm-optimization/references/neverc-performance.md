# Neverc Performance

## NeverC Compiler-Internal Performance Optimization Pitfalls

When optimizing NeverC's own compiler code (lexer, preprocessor, Sema), the following bug patterns have been observed and must be avoided. **Always check for these before committing any "performance refactoring" to the compiler codebase.**

### 1. Packed-Integer Endian Checks: Never Use `LLVM_IS_LITTLE_ENDIAN`

`LLVM_IS_LITTLE_ENDIAN` is defined in `llvm/Support/SwapByteOrder.h`. NeverC `.cpp` files that don't transitively include it will silently evaluate `#if LLVM_IS_LITTLE_ENDIAN` as false, using big-endian encoding on a little-endian host. This produces silent data corruption (wrong key values in switch tables).

**Rule**: Always use the compiler built-in instead:
```cpp
// BAD — may be undefined, silently evaluates to 0
#if LLVM_IS_LITTLE_ENDIAN

// GOOD — always available in GCC/Clang
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
```

**Files historically affected**: `IdentifierTable.cpp` (`getPPKeywordID`), `BuiltinString.cpp` (`isRuntimeFunctionName`), `ModuleLayout.cpp` (module name matching).

### 2. Token Kind Enum Range Checks: Values Are NOT Contiguous

Token kinds (`tok::kw_void`, `tok::kw_int`, `tok::kw_enum`, etc.) are assigned values by the order they appear in `TokenKinds.def`, which includes non-keyword tokens interspersed. Their numeric values are **not contiguous or monotonically ordered by semantic group**.

**Rule**: Never add range-guard optimizations like `if (Raw >= KwVoid && Raw <= KwEnum)` before a switch on token kinds. The switch statement itself is the correct dispatch — the compiler's jump-table optimization handles it.

```cpp
// BAD — kw_int (84) < kw_void (99), so kw_int is excluded!
unsigned KwVoid = static_cast<unsigned>(tok::kw_void);   // 99
unsigned KwEnum = static_cast<unsigned>(tok::kw_enum);
if (Raw >= KwVoid && Raw <= KwEnum) { switch(K) { ... } }

// GOOD — let the compiler optimize the switch
switch (K) {
  case tok::kw_void: case tok::kw_int: ... return true;
  default: return false;
}
```

**File historically affected**: `RunParser.cpp` (`isIntrinsicTypeToken`) — broke headerless forward-declaration generation for `int`-returning functions.

### 3. Hand-Written Small memcpy: Use `std::memcpy` for <= 64 Bytes

Custom overlapping-store patterns for small copies (`Dst[0]=Src[0]; Dst[Len>>1]=Src[Len>>1]; Dst[Len-1]=Src[Len-1]`) are fragile. For `Len=2`, the middle store `Dst[1]=Src[1]` is correct but `Dst[0]` may read a stale/wrong byte if the source is a scratch-buffer location with alignment padding.

**Rule**: For sizes <= 64 bytes, always use `std::memcpy`. Only use SIMD for bulk copies > 64 bytes where the overhead is justified.

```cpp
LLVM_ATTRIBUTE_ALWAYS_INLINE
static void fastCopy(char *Dst, const char *Src, unsigned Len) {
  if (LLVM_LIKELY(Len <= 64)) {
    std::memcpy(Dst, Src, Len);  // safe for all lengths
    return;
  }
  // SIMD path for large copies...
}
```

**File historically affected**: `TokenScratch.cpp` — broke `#(42)` stringification producing `" 2"` instead of `"42"`.

### 4. Scratch Buffer Alignment: Don't Change Location Math

`TokenScratch::getToken` returns a `SourceLocation` offset that diagnostics and `getSpelling` depend on. The offset calculation `BytesUsed - Len - 1` assumes a specific write sequence (`\n` + data + `\0`). Changing alignment (e.g., from 4 to 16) without updating the offset formula breaks source location mapping.

**Rule**: If you change the alignment in `getToken`, trace the location offset formula end-to-end and verify with `#(two_char_token)` stringification tests, which are the most sensitive to off-by-one location errors.

### 5. Pre-Commit Checklist for Compiler Performance Patches

Before committing any "performance optimization" to the compiler's own C++ code:

- [ ] `grep -r 'LLVM_IS_LITTLE_ENDIAN' neverc/` — must return 0 hits
- [ ] No `#if` range guards on `tok::TokenKind` numeric values
- [ ] No hand-written byte-copy patterns for `Len < 8`
- [ ] Run full test suite (`ctest --test-dir build-neverc`) — not just the first section
- [ ] Test `#(42)` stringification: `echo '#define S(x) #x\nS(42)' | neverc -E -xc -` must produce `"42"`
- [ ] Test `int`-returning functions with `string` params get headerless forward declarations

## NeverC Bench-Proven Optimization Playbook

When optimizing NeverC frontend internals (`Scan`, `PP`, `Sema`), use this short list of hard conclusions:

- **Proven fast**: scope-chain bitmap fast-reject (`ContextBitmap` + `mayHaveDeclInContext`) and single-decl early-exit in `Sema::ResolveName`.
- **Proven fast**: identifier path hot/cold split (`IdentifierTable::get` inline find + noinline cold insert) and inline resolution chain (`ScanIdentRest -> ResolveRawIdent -> getIdentifierInfo -> IdentifierTable::get`).
- **Proven fast**: lexer hot-path flattening (common punctuation/comment fast dispatch, single-space scalar fast path, short-span 16B SIMD pre-probe).
- **Proven fast**: token reset store reduction (`startTokenFast` style minimal reset) when omitted fields are guaranteed overwritten.
- **Proven bad**: replacing `IdentifierTable` with Swiss-style design regressed heavily; keep `StringMap` baseline unless new design clearly wins.
- **Proven caveat**: UTF-8 SIMD gains can be huge on ASCII fixtures but near 1x on dense non-ASCII; never generalize from ASCII-only results.
- **Merge gate**: only keep changes with clear target-path gain and no neighboring hotspot regressions; pending-bench changes are not validated.
