**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Système de Runtime Intégré NeverC](../README.fr.md) · [Documentation NeverC](../../README.fr.md)

# Type `string` intégré de NeverC

## Aperçu

NeverC fournit un type valeur `string` intégré pour le C. Il combine le design d'interface de `std::string` en C++ avec les capacités Unicode de `QString` de Qt, permettant des opérations sur les chaînes sûres et efficaces en C.

**Activation :** Passez `-fbuiltin-string` à la compilation (désactivé par défaut). Activation automatique dans les cas suivants :
- Le fichier d'entrée a l'extension `.nc` (voir [documentation de l'extension `.nc`](../../nc-extension/README.fr.md))
- Le mode `-fshellcode` est actif

```bash
neverc hello.nc -o hello                # automatique — .nc l'active
neverc -fbuiltin-string main.c -o main  # flag explicite pour les fichiers .c
```

```c
string greeting = "Hello World";
string msg = greeting + ", NeverC!";
printf("Byte length: %zu\n", msg.len);
printf("Char count: %zu\n", msg.utf8_count());
printf("Content: %s\n", msg.c_str());
```

### Key Features

- **Value semantics**: `string` is a `{ data, len, cap }` three-field struct, passed and returned by value
- **Automatic memory management**: The compiler attaches `CleanupAttr` to automatically release owned strings on scope exit — no manual `free` needed
- **Native UTF-8**: All literals (including `L"..."`, `u"..."`, `U"..."`) are folded to UTF-8 at compile time
- **Borrowed views**: Literal assignment `string s = "hello"` produces a zero-allocation view (`cap == 0`) that does not own memory
- **Dot-call syntax**: `s.find("abc")`, `s.to_upper().trim()` are rewritten by Sema into corresponding C function calls
- **Namespace isolation**: All runtime symbols use the `neverc_string_*` prefix, avoiding collisions with user identifiers like `string_eq` or `STRING_NPOS`

---

## Concepts fondamentaux

### Définition du type

```c
typedef struct __neverc_string {
  const char *data;  // pointer to UTF-8 bytes
  __SIZE_TYPE__ len; // byte length
  __SIZE_TYPE__ cap; // capacity (0 = borrowed view, >0 = owned memory)
} string;
```

### Modèle de propriété : Owned vs Borrowed

The `cap` field serves double duty as both "capacity" and "ownership flag":

| State | `cap` | Meaning | Example |
|-------|-------|---------|---------|
| **Borrowed (view)** | `== 0` | Does not own memory; points to external storage (literals, stack buffers, etc.) | `string s = "hello"` |
| **Owned** | `> 0` | Owns a buffer allocated via `NEVERC_STRING_ALLOC` | `string s = a + b` |

```c
string a = "hello";           // Borrowed: data → literal storage, cap == 0
string b = a + ", world!";    // Owned: allocates new buffer, cap > 0
string c = a.to_upper();      // Owned: transformation produces new buffer
string d = neverc_string_view(buf, len); // Borrowed: wraps external buffer
```

### Gestion automatique de la mémoire

The compiler automatically handles the lifetime of owned strings:

```c
void example() {
    string a = "hello";               // borrowed, no release needed
    string b = a + ", world!";        // owned, compiler attaches CleanupAttr
    string c = b.to_upper().trim();   // chaining: intermediate values auto-released
    printf("%s\n", c.c_str());
}   // scope exit: b, c auto-released; a needs no release (borrowed)
```

Wide-character pointers returned by `w_str()`, `to_utf16_owned()`, and `to_utf32_owned()` are also auto-released — Sema attaches `__neverc_wptr_cleanup` when it detects these initializations.

### Garanties de sécurité

- **Forged handle protection**: Handles with `len > 0 && data == NULL` are intercepted by `__neverc_string_invalid`, short-circuiting to the empty string
- **Oversized handle protection**: Handles with `len > NEVERC_STRING_MAX_LEN` are similarly intercepted
- **Temporary safety**: `.c_str()` and `.data()` on temporaries (prvalues) trigger compiler error `err_neverc_string_cstr_temporary`, preventing dangling pointers
- **Zero-leak testing**: All string tests on macOS run under `leaks --atExit`, asserting "0 leaks"

### Constantes

| Constant | Value | Description |
|----------|-------|-------------|
| `NEVERC_STRING_NPOS` | `(size_t)-1` | Return value when search finds no match, equivalent to `std::string::npos` |
| `NEVERC_STRING_MAX_LEN` | `(size_t)-2` | Maximum payload length ceiling |

---

## Opérateurs

Sema rewrites operators to corresponding runtime calls:

| Operator | Equivalent Call | Return Type |
|----------|----------------|-------------|
| `a + b` | `__neverc_string_cat(a, b)` | `string` (owned) |
| `a += b` | `__neverc_string_assign(&a, __neverc_string_cat(a, b))` | — |
| `a == b` | `neverc_string_eq(a, b)` | `int` |
| `a != b` | `!neverc_string_eq(a, b)` | `int` |
| `a < b` | `neverc_string_compare(a, b) < 0` | `int` |
| `a <= b` | `neverc_string_compare(a, b) <= 0` | `int` |
| `a > b` | `neverc_string_compare(a, b) > 0` | `int` |
| `a >= b` | `neverc_string_compare(a, b) >= 0` | `int` |
| `a = b` | `__neverc_string_assign(&a, b)` | — |
| `s[i]` | `neverc_string_at(s, i)` | `char` |

---

## Référence API complète

### Taille et accès

| Dot-call | Aliases | Runtime Function | Signature | Returns |
|----------|---------|-----------------|-----------|---------|
| `s.len()` | `s.size()` `s.length()` | `neverc_string_len` | `(string s) → size_t` | Byte length |
| `s.empty()` | — | `neverc_string_empty` | `(string s) → int` | `s.len == 0` |
| `s.c_str()` | — | `neverc_string_cstr` | `(string s) → const char *` | NUL-terminated string pointer (borrowed) |
| `s.data()` | — | `neverc_string_data` | `(string s) → void *` | Raw byte pointer (borrowed, no const) |
| `s.at(i)` | `s[i]` | `neverc_string_at` | `(string s, size_t i) → char` | Returns `'\0'` on out-of-bounds |
| `s.front()` | — | `neverc_string_front` | `(string s) → char` | First byte |
| `s.back()` | — | `neverc_string_back` | `(string s) → char` | Last byte |

> **Note**: `.c_str()` and `.data()` return borrowed pointers into the string's internal buffer. Using them on temporaries is a compile error.

### Capacité

| Dot-call | Runtime Function | Signature | Description |
|----------|-----------------|-----------|-------------|
| `s.reserve(n)` | `neverc_string_reserve` | `(string s, size_t n) → string` | Reserve at least `n` bytes of capacity |
| `s.shrink_to_fit()` | `neverc_string_shrink_to_fit` | `(string s) → string` | Reduce excess capacity |
| `s.capacity()` | `neverc_string_capacity` | `(const string *s) → size_t` | Current capacity (passed via `&s`) |
| `s.max_size()` | `neverc_string_max_size` | `(string s) → size_t` | Returns `NEVERC_STRING_MAX_LEN` |

### Égalité et comparaison

| Dot-call | Runtime Function | Signature | Returns |
|----------|-----------------|-----------|---------|
| `s.eq(t)` | `neverc_string_eq` | `(string a, string b) → int` | `1`=equal, `0`=not equal |
| `s.compare(t)` | `neverc_string_compare` | `(string a, string b) → int` | `-1` / `0` / `+1` |
| `s.compare(pos, n, t)` | `neverc_string_compare_substr` | `(string a, size_t pos, size_t n, string b) → int` | Substring compare |
| `s.compare(p1, n1, t, p2, n2)` | `neverc_string_compare_substr2` | `(string a, size_t p1, size_t n1, string b, size_t p2, size_t n2) → int` | Two-substring compare |

#### Insensible à la casse ASCII

Fold rule: `A-Z → a-z`, all other bytes (including `>= 0x80` UTF-8 continuation bytes) unchanged. Suitable for HTTP header matching, file extension comparison, etc.

| Dot-call | Runtime Function | Description |
|----------|-----------------|-------------|
| `s.eq_ic(t)` | `neverc_string_eq_ic` | Case-insensitive equality |
| `s.compare_ic(t)` | `neverc_string_compare_ic` | Case-insensitive 3-way compare |
| `s.find_ic(t)` | `neverc_string_find_ic` | Case-insensitive find |
| `s.contains_ic(t)` | `neverc_string_contains_ic` | Case-insensitive contains |
| `s.starts_with_ic(t)` | `neverc_string_starts_with_ic` | Case-insensitive prefix match |
| `s.ends_with_ic(t)` | `neverc_string_ends_with_ic` | Case-insensitive suffix match |

```c
string ct = "Content-Type";
if (ct.eq_ic("content-type")) { /* matches */ }

string path = "photo.PNG";
if (path.ends_with_ic(".png")) { /* matches */ }
```

### Recherche

All search functions support both string and char overloads — Sema dispatches automatically based on the first argument type.

| Dot-call | char overload | Runtime Function | Returns |
|----------|---------------|-----------------|---------|
| `s.find(t)` | `s.find(ch)` | `neverc_string_find` / `find_char` | Byte offset of first occurrence, or `NEVERC_STRING_NPOS` |
| `s.find(t, pos)` | `s.find(ch, pos)` | `neverc_string_find_from` / `find_char_from` | Search starting from `pos` |
| `s.rfind(t)` | `s.rfind(ch)` | `neverc_string_rfind` / `rfind_char` | Last occurrence offset |
| `s.rfind(t, pos)` | `s.rfind(ch, pos)` | `neverc_string_rfind_to` / `rfind_char_to` | Reverse search within `[0, pos]` |
| `s.contains(t)` | `s.contains(ch)` | `neverc_string_contains` / `contains_char` | `1`=contains, `0`=does not |
| `s.starts_with(t)` | `s.starts_with(ch)` | `neverc_string_starts_with` / `starts_with_char` | Prefix match |
| `s.ends_with(t)` | `s.ends_with(ch)` | `neverc_string_ends_with` / `ends_with_char` | Suffix match |
| `s.count(t)` | `s.count(ch)` | `neverc_string_count` / `count_char` | Occurrence count |

#### Recherche par jeu de caractères

Internally uses a 256-bit bitmap for O(1) character set membership testing, yielding O(n+m) overall complexity.

| Dot-call | With position | Runtime Function | Description |
|----------|--------------|-----------------|-------------|
| `s.find_first_of(chars)` | `s.find_first_of(chars, pos)` | `neverc_string_find_first_of` / `_from` | First occurrence of any character in `chars` |
| `s.find_last_of(chars)` | `s.find_last_of(chars, pos)` | `neverc_string_find_last_of` / `_to` | Last occurrence |
| `s.find_first_not_of(chars)` | `s.find_first_not_of(chars, pos)` | `neverc_string_find_first_not_of` / `_from` | First character **not in** `chars` |
| `s.find_last_not_of(chars)` | `s.find_last_not_of(chars, pos)` | `neverc_string_find_last_not_of` / `_to` | Last character not in `chars` |

Char overloads also work: `s.find_first_of('x')` is equivalent to `s.find('x')`, and `s.find_first_not_of(' ')` is the classic "skip leading spaces" idiom.

### Sous-chaîne et copie

| Dot-call | Runtime Function | Signature | Description |
|----------|-----------------|-----------|-------------|
| `s.substr(pos)` | `neverc_string_substr` | `(string s, size_t pos, size_t len) → string` | `len` defaults to `NPOS` (to end) with 1 arg |
| `s.substr(pos, len)` | same | same | Extract `[pos, pos+len)` substring |
| `s.copy(out, n)` | `neverc_string_copy` | `(string s, char *out, size_t n) → size_t` | Copy to external buffer, returns bytes copied |
| `s.copy(out, n, pos)` | `neverc_string_copy_from` | `(string s, char *out, size_t n, size_t pos) → size_t` | Copy starting from `pos` |

### Mutation

Mutation follows the value-type "consume input + return new value" pattern: the function consumes the input string (releases its owned buffer) and returns a new owned string. The compiler automatically rewrites `s.append(x)` as `s = neverc_string_append(s, x)`, appearing as "in-place modification" from the user's perspective.

Only `assign`, `swap`, and `capacity` use `string *` pointer receivers (see "Pointer Receiver Methods" below).

#### Consommer + retourner nouvelle valeur

| Dot-call | Runtime Function | Signature | Description |
|----------|-----------------|-----------|-------------|
| `s.append(t)` | `neverc_string_append` | `(string s, string t) → string` | Append string |
| `s.append(n, ch)` | `neverc_string_append_char` | `(string s, size_t n, char ch) → string` | Append n copies of ch |
| `s.push_back(ch)` | `neverc_string_push_back` | `(string s, char ch) → string` | Append single character |
| `s.pop_back()` | `neverc_string_pop_back` | `(string s) → string` | Remove last character |
| `s.insert(pos, t)` | `neverc_string_insert` | `(string s, size_t pos, string t) → string` | Insert string |
| `s.insert(pos, n, ch)` | `neverc_string_insert_char` | `(string s, size_t pos, size_t n, char ch) → string` | Insert n copies of ch |
| `s.erase(pos)` | `neverc_string_erase` | `(string s, size_t pos, size_t count) → string` | `count` defaults to `NPOS` with 1 arg |
| `s.erase(pos, count)` | same | same | Erase `[pos, pos+count)` |
| `s.replace(pos, n, t)` | `neverc_string_replace` | `(string s, size_t pos, size_t n, string t) → string` | Replace substring |
| `s.replace(pos, n, m, ch)` | `neverc_string_replace_char` | `(string s, size_t pos, size_t n, size_t m, char ch) → string` | Replace with m copies of ch |
| `s.replace_all(from, to)` | `neverc_string_replace_all` | `(string s, string from, string to) → string` | Global replace (NeverC extension) |
| `s.clear()` | `neverc_string_clear` | `(string s) → string` | Clear contents (returns empty string) |
| `s.resize(n)` | `neverc_string_resize` | `(string s, size_t n, char ch) → string` | `ch` defaults to `'\0'` with 1 arg |
| `s.resize(n, ch)` | same | same | Resize, filling extension with `ch` |
| `s.pad_left(w, ch)` | `neverc_string_pad_left` | `(string s, size_t width, char ch) → string` | Left-pad |
| `s.pad_right(w, ch)` | `neverc_string_pad_right` | `(string s, size_t width, char ch) → string` | Right-pad |
| `s.clone()` | `neverc_string_clone` | `(string s) → string` | Deep copy (owned moves directly, borrowed allocates new buffer) |
| `s.to_lower()` | `neverc_string_to_lower` | `(string s) → string` | ASCII lowercase |
| `s.to_upper()` | `neverc_string_to_upper` | `(string s) → string` | ASCII uppercase |
| `s.trim()` | `neverc_string_trim` | `(string s) → string` | Trim whitespace from both ends |
| `s.ltrim()` | `neverc_string_ltrim` | `(string s) → string` | Trim left whitespace |
| `s.rtrim()` | `neverc_string_rtrim` | `(string s) → string` | Trim right whitespace |
| `s.repeat(n)` | `neverc_string_repeat` | `(string s, size_t n) → string` | Repeat n times |
| `s.reverse()` | `neverc_string_reverse` | `(string s) → string` | Byte-level reversal |
| `s.hash()` | `neverc_string_hash` | `(string s) → unsigned long long` | FNV-1a 64-bit hash |

#### Méthodes à récepteur pointeur

These methods need to operate directly on the caller's storage; Sema wraps the receiver as `&s`:

| Dot-call | Runtime Function | Signature | Description |
|----------|-----------------|-----------|-------------|
| `s.assign(t)` | `neverc_string_assign` | `(string *dst, string src)` | Replace contents (release old + install new) |
| `s.assign(n, ch)` | `neverc_string_assign_char` | `(string *dst, size_t n, char ch)` | Replace with n copies of ch |
| `s.swap(t)` | `neverc_string_swap` | `(string *a, string *b)` | Swap two string handles |

### Split / Join

| Dot-call | Runtime Function | Signature | Description |
|----------|-----------------|-----------|-------------|
| `s.split_first(sep)` | `neverc_string_split_first` | `(string s, string sep) → string` | First segment before separator |
| `s.split_rest(sep)` | `neverc_string_split_rest` | `(string s, string sep) → string` | Everything after first separator |
| `s.split_before_last(sep)` | `neverc_string_split_before_last` | `(string s, string sep) → string` | Before last separator |
| `s.split_after_last(sep)` | `neverc_string_split_after_last` | `(string s, string sep) → string` | After last separator |
| `s.split(sep, &items, &n)` | `neverc_string_split` | `(string s, string sep, string **items, size_t *count)` | Full split into array |
| — | `neverc_string_split_free` | `(string *parts, size_t n)` | Free array from `split` |
| — | `neverc_string_join` | `(const string *items, size_t n, string sep) → string` | Join array with separator |

```c
// Peel one piece at a time
string path = "/usr/local/bin";
string first = path.split_first("/");   // ""
string rest  = path.split_rest("/");    // "usr/local/bin"

// Path decomposition
string file = "src/lib/main.c";
string dir  = file.split_before_last("/");  // "src/lib"
string base = file.split_after_last("/");   // "main.c"

// Full split
string csv = "a,b,c";
string *parts;
size_t count;
csv.split(",", &parts, &count);
for (size_t i = 0; i < count; i++)
    printf("[%s]", parts[i].c_str());
neverc_string_split_free(parts, count);
```

### Conversion numérique

| Direction | Dot-call | Runtime Function | Signature |
|-----------|----------|-----------------|-----------|
| int → string | — | `neverc_string_from_int` | `(ptrdiff_t v) → string` |
| uint → string | — | `neverc_string_from_uint` | `(size_t v) → string` |
| int → string (with base) | — | `neverc_string_from_int_base` | `(ptrdiff_t v, int base) → string` |
| uint → string (with base) | — | `neverc_string_from_uint_base` | `(size_t v, int base) → string` |
| string → int | `s.to_int()` | `neverc_string_to_int` | `(string s) → ptrdiff_t` |
| string → uint | `s.to_uint()` | `neverc_string_to_uint` | `(string s) → size_t` |

`to_int` / `to_uint` parse decimal digits only, stopping at the first non-digit character. `from_int_base` / `from_uint_base` support bases 2-36.

```c
string hex = neverc_string_from_int_base(255, 16);  // "ff"
string dec = neverc_string_from_int(-42);            // "-42"

string s = "12345";
ptrdiff_t v = s.to_int();  // 12345
```

### Fonctions fabrique

Not used via dot-call; require the full `neverc_string_*` prefix:

| Runtime Function | Signature | Description |
|-----------------|-----------|-------------|
| `neverc_string_from_cstr(p)` | `(const char *p) → string` | Create owned string from C string |
| `neverc_string_from_char(ch)` | `(char ch) → string` | Create from single character |
| `neverc_string_from_utf32_char(cp)` | `(uint32_t cp) → string` | Create UTF-8 from Unicode codepoint |
| `neverc_string_from_utf16(d, n)` | `(const uint16_t *d, size_t n) → string` | Create from UTF-16 buffer |
| `neverc_string_from_utf32(d, n)` | `(const uint32_t *d, size_t n) → string` | Create from UTF-32 buffer |
| `neverc_string_from_latin1(d, n)` | `(const char *d, size_t n) → string` | Create from Latin-1 (ISO-8859-1) |
| `neverc_string_view(p, n)` | `(const char *p, size_t n) → string` | Create borrowed view (no copy) |
| `neverc_string_clone(s)` | `(string s) → string` | Deep copy |
| `neverc_string_free(s)` | `(string s)` | Release owned string (no-op for borrowed) |
| `neverc_string_array_free(items, n)` | `(string *items, size_t n)` | Release all elements in a string array |

---

## Formatage

### `s.format(...)` — printf-style Formatting

`neverc_string_format` provides a libc-free printf subset:

```c
string name = "World";
string msg = "Hello %S, number=%d".format(name, 42);
// msg = "Hello World, number=42"
```

#### Spécificateurs de format supportés

| Specifier | Type | Description |
|-----------|------|-------------|
| `%d` / `%i` | `int` | Signed decimal |
| `%u` | `unsigned int` | Unsigned decimal |
| `%ld` / `%lu` | `long` / `unsigned long` | Long integer |
| `%lld` / `%llu` | `long long` / `unsigned long long` | Long long integer |
| `%x` / `%lx` / `%llx` | corresponding unsigned | Lowercase hexadecimal |
| `%s` | `const char *` | C string (NUL-terminated; NULL outputs `(null)`) |
| `%S` | `string` | **NeverC `string` value** (passed by value, released after output) |
| `%c` | `int` | Single byte character |
| `%p` | `void *` | Pointer (lowercase hex with `0x` prefix) |
| `%%` | — | Literal `%` |

**`%s` vs `%S`:**

```c
string s = "NeverC";
const char *c = "world";
string result = "hello %s, %S!".format(c, s);
// %s takes const char *, %S takes string value
```

### Using with Standard `printf`

NeverC `string` cannot be passed directly to the C standard library's `printf`. Use `.c_str()` to convert:

```c
string s = "Hello World";
printf("Content: %s\n", s.c_str());
printf("Length: %zu\n", s.len);
```

---

## UTF-8 / Unicode

Internally always UTF-8 encoded. Byte-level operations (`s.len`, `s.at(i)`, `s.find(...)`, etc.) work in **bytes**; codepoint-level operations use the `utf8_*` family:

### Opérations sur les points de code

| Dot-call | Aliases | Runtime Function | Returns |
|----------|---------|-----------------|---------|
| `s.utf8_count()` | `s.utf8_size()` `s.utf8_length()` | `neverc_string_utf8_count` | Codepoint count |
| `s.utf8_valid()` | `s.is_utf8()` | `neverc_string_utf8_valid` | `1`=valid UTF-8 |
| `s.utf8_at(i)` | — | `neverc_string_utf8_at` | i-th codepoint (`uint32_t`) |
| `s.utf8_byte_index(i)` | — | `neverc_string_utf8_byte_index` | Byte offset of i-th codepoint |
| `s.utf8_substr(pos)` | — | `neverc_string_utf8_substr` | From pos-th codepoint to end |
| `s.utf8_substr(pos, n)` | — | same | Extract n codepoints |
| `s.is_ascii()` | — | `neverc_string_is_ascii` | `1`=all bytes < 0x80 |

```c
string s = "Hello \xF0\x9F\x8E\x89";  // "Hello 🎉"
printf("Bytes: %zu\n", s.len);          // 10
printf("Chars: %zu\n", s.utf8_count()); // 6
```

### Conversion d'encodage

| Dot-call | Runtime Function | Signature | Description |
|----------|-----------------|-----------|-------------|
| `s.to_utf16(out, cap)` | `neverc_string_to_utf16` | `(string s, uint16_t *out, size_t cap) → size_t` | Write to caller-owned buffer |
| `s.to_utf32(out, cap)` | `neverc_string_to_utf32` | `(string s, uint32_t *out, size_t cap) → size_t` | Write to caller-owned buffer |
| `s.to_utf16_owned()` | `neverc_string_to_utf16_owned` | `(string s) → uint16_t *` | Returns freshly allocated NUL-terminated buffer |
| `s.to_utf32_owned()` | `neverc_string_to_utf32_owned` | `(string s) → uint32_t *` | Returns freshly allocated NUL-terminated buffer |
| `s.w_str()` | `neverc_string_w_str` | `(string s) → wchar_t *` | Platform-adaptive: Windows=UTF-16, Linux/macOS=UTF-32 |
| `s.to_latin1(out, cap)` | `neverc_string_to_latin1` | `(string s, char *out, size_t cap) → size_t` | Convert to Latin-1 (ISO-8859-1) |

Pointers returned by `to_utf16_owned()`, `to_utf32_owned()`, and `w_str()` are **auto-released** (Sema attaches `__neverc_wptr_cleanup`). For manual release use `neverc_string_wfree(buf)`.

```c
// UTF-8 direct output (modern terminals support it natively)
string s = "Hello World";
printf("%s\n", s.c_str());

// Win32 wide-char API
string title = "NeverC";
__WCHAR_TYPE__ *ws = title.w_str();
MessageBoxW(NULL, ws, L"Title", MB_OK);
// ws is auto-released, no manual management needed
```

---

## Encodage d'octets

All encoding methods consume the receiver and return a new owned string, supporting method chaining.

### Base64 / Base32 / Hex

| Dot-call | Runtime Function | Standard | Description |
|----------|-----------------|----------|-------------|
| `s.to_base64()` | `neverc_string_to_base64` | RFC 4648 §4 | Standard Base64 with `=` padding |
| `s.from_base64()` | `neverc_string_from_base64` | RFC 4648 §4 | Decode |
| `s.to_base64_url()` | `neverc_string_to_base64_url` | RFC 4648 §5 | URL-safe Base64 (no padding), for JWT/JOSE |
| `s.from_base64_url()` | `neverc_string_from_base64_url` | RFC 4648 §5 | Decode |
| `s.to_base32()` | `neverc_string_to_base32` | RFC 4648 §6 | Uppercase + `=` padding, for TOTP/Google Authenticator |
| `s.from_base32()` | `neverc_string_from_base32` | RFC 4648 §6 | Decode (case-insensitive) |
| `s.to_hex()` | `neverc_string_to_hex` | — | Lowercase hex encoding |
| `s.from_hex()` | `neverc_string_from_hex` | — | Case-insensitive decode |

```c
string data = "Hello, NeverC!";
string b64 = data.to_base64();         // "SGVsbG8sIE5ldmVyQyE="
string back = b64.from_base64();       // "Hello, NeverC!"

string jwt_part = data.to_base64_url(); // URL-safe, no padding
string hex = data.to_hex();             // "48656c6c6f2c204e657665724321"
```

### URL / Form Encoding

| Dot-call | Runtime Function | Standard | Description |
|----------|-----------------|----------|-------------|
| `s.url_encode()` | `neverc_string_url_encode` | RFC 3986 | Percent-encoding (unreserved set preserved, uppercase `%XX`) |
| `s.url_decode()` | `neverc_string_url_decode` | RFC 3986 | Decode |
| `s.form_encode()` | `neverc_string_form_encode` | `application/x-www-form-urlencoded` | Space → `+`, `+` → `%2B` |
| `s.form_decode()` | `neverc_string_form_decode` | same | `+` → space |

> `url_encode` and `form_encode` are not interchangeable — form-encoded `+` decoded via `url_decode` stays as literal `+`.

---

## Codecs Web

HTML / JSON / CSV literal-level escaping/unescaping. Each pair maintains strict round-trip fidelity.

| Dot-call | Runtime Function | Standard | Description |
|----------|-----------------|----------|-------------|
| `s.html_escape()` | `neverc_string_html_escape` | OWASP Rule #1/#2 | 5 special character entity escapes (`& < > " '`) |
| `s.html_unescape()` | `neverc_string_html_unescape` | HTML5 | Entity decode (including numeric `&#xNNNN;`) |
| `s.json_escape()` | `neverc_string_json_escape` | RFC 8259 §7 | JSON string literal escaping |
| `s.json_unescape()` | `neverc_string_json_unescape` | RFC 8259 §7 | Decode (including `\uXXXX` surrogate pairs) |
| `s.csv_escape()` | `neverc_string_csv_escape` | RFC 4180 §2 | CSV field escaping |
| `s.csv_unescape()` | `neverc_string_csv_unescape` | RFC 4180 §2 | CSV field decode |

```c
string html = "<div class=\"test\">Hello & World</div>";
string safe = html.html_escape();
// "&lt;div class=&quot;test&quot;&gt;Hello &amp; World&lt;/div&gt;"

string json_val = "line1\nline2\ttab";
string escaped = json_val.json_escape();
// "line1\\nline2\\ttab"
```

---

## Chiffrement de chaînes à la compilation

`.encrypt()` fournit un chiffrement XOR à la compilation pour les littéraux de chaîne. Le texte en clair n'est jamais stocké dans le binaire — il est chiffré à la compilation et déchiffré à l'exécution via une fonction `always_inline`, rendant les outils d'analyse statique (ex. `strings`) incapables d'extraire le contenu original.

### Utilisation

```c
string secret = "API_KEY_12345".encrypt();
printf("%s\n", secret.c_str());  // affiche "API_KEY_12345" à l'exécution

// mais `strings ./binary` NE trouvera PAS "API_KEY_12345"
```

### Fonctionnement

1. **Compilation** : Sema intercepte `.encrypt()` sur un littéral, chiffre chaque octet par XOR et stocke le texte chiffré dans `.rodata`
2. **Exécution (chemin général)** : la fonction `always_inline` `__neverc_string_decrypt_literal` déchiffre par XOR dans un buffer owned nouvellement alloué. Le résultat est un `string` owned normal.
3. **Exécution (chemin rapide comparaison/recherche)** : lorsque le littéral chiffré apparaît directement dans une expression de comparaison ou recherche, Sema réécrit vers une variante `__neverc_string_decrypt_*` à zéro allocation qui déchiffre et compare octet par octet (voir « Comparaison déchiffrée à zéro allocation » ci-dessous)

### Génération de clé

- **Par défaut** : chaque compilation dérive une clé de base à partir de `std::time()`, puis la mixe avec un compteur per-literal pour produire une clé unique par littéral. Chaque build produit un texte chiffré différent.
- **Remplacement CLI** : utilisez `-fstring-encrypt-key=0xDEADBEEF` pour fixer la clé de base (utile pour les builds reproductibles ou les tests).

### Tous les types de littéraux supportés

`.encrypt()` supporte automatiquement tous les types de littéraux — le compilateur convertit les littéraux wide/UTF-16/UTF-32 en UTF-8 avant le chiffrement :

```c
string a = "hello".encrypt();                  // ordinary
string b = u8"héllo".encrypt();                // UTF-8
string c = L"中文".encrypt();                   // wide
string d = u"\u4E2D\u6587".encrypt();          // UTF-16
string e = U"\U0001F389party".encrypt();       // UTF-32
string f = R"(line1\nline2)".encrypt();        // raw
```

### Comparaison déchiffrée à zéro allocation

Lorsqu'un littéral chiffré est utilisé directement dans une expression de comparaison ou de recherche, le compilateur contourne automatiquement l'allocation mémoire. Au lieu de déchiffrer la chaîne entière en mémoire puis de comparer, il déchiffre et compare octet par octet — le texte en clair n'apparaît jamais complètement en mémoire.

**Expressions optimisées** (toutes à zéro allocation quand un opérande est `.encrypt()`) :

| Catégorie | Expressions |
|-----------|------------|
| Égalité | `s == "key".encrypt()`, `s != "key".encrypt()` |
| Relationnel | `s < "key".encrypt()`, `s > "key".encrypt()`, `<=`, `>=` |
| Préfixe/Suffixe | `s.starts_with(...)`, `s.ends_with(...)` |
| Contenance | `s.contains(...)` |
| Insensible à la casse | `s.eq_ic(...)`, `s.starts_with_ic(...)`, `s.ends_with_ic(...)`, `s.contains_ic(...)` |
| Recherche | `s.find(...)`, `s.rfind(...)`, variantes avec position |
| Comptage | `s.count(...)` |
| Recherche insensible | `s.find_ic(...)` |

### Restrictions

- `.encrypt()` ne peut être appliqué qu'aux littéraux de chaîne. L'appeler sur une variable produit une erreur de compilation :

```c
string s = "hello";
string e = s.encrypt();  // ERREUR : .encrypt() can only be applied to string literals
```

- `.encrypt()` ne prend aucun argument :

```c
string e = "hello".encrypt(42);  // ERREUR : .encrypt() takes no arguments
```

- Le double chiffrement est rejeté :

```c
string e = "hello".encrypt().encrypt();  // ERREUR : .encrypt() can only be applied to string literals
```

### Algorithme de chiffrement personnalisé

Le chiffrement et le déchiffrement sont contrôlés par deux macros :

- `NEVERC_STRING_ENCRYPT_BYTE(byte, key, idx)` — compilation : clair→chiffré
- `NEVERC_STRING_DECRYPT_BYTE(byte, key, idx)` — exécution : chiffré→clair

`ENCRYPT_BYTE` utilise XOR par défaut. `DECRYPT_BYTE` utilise par défaut une **décomposition arithmétique sans instruction XOR** — calcule `a ^ b` via `(a + b) - (a & b) - (b & a)` avec des intermédiaires `volatile` pour empêcher LLVM de ré-optimiser en `xor`. Peut être renforcé avec des passes d'obfuscation MBA (Mixed Boolean-Arithmetic).

Pour un algorithme non-XOR, définissez **les deux** macros (elles doivent être des inverses mathématiques) :

```c
#define NEVERC_STRING_ENCRYPT_BYTE(byte, key, idx) \
  ((char)((unsigned char)(byte) + (unsigned char)((key) >> (8 * ((idx) % sizeof(size_t))))))
#define NEVERC_STRING_DECRYPT_BYTE(byte, key, idx) \
  ((char)((unsigned char)(byte) - (unsigned char)((key) >> (8 * ((idx) % sizeof(size_t))))))
```

### Drapeaux du compilateur

| Drapeau | Description |
|---------|-------------|
| `-fstring-encrypt-key=<hex>` | Remplacer la clé de base XOR (ex. `-fstring-encrypt-key=0xDEADBEEF`) |

### Paramètres configurables

| Macro | Par défaut | Description |
|-------|-----------|-------------|
| `NEVERC_STRING_DECRYPT_BYTE(byte, key, idx)` | XOR avec octets de clé rotatifs | Opération de déchiffrement par octet |

---

## Chaînage de méthodes

All methods returning `string` support chaining. Intermediate values are auto-released with no leaks:

```c
string result = input.to_upper().trim().replace_all(" ", "_");
string encoded = payload.to_base64_url();
string cleaned = raw.url_decode().from_base64().trim();
```

---

## Modes de compilation

### Trois modes de fonctionnement

| Mode | Description | String Function Body Source | Symbol Visibility |
|------|-------------|---------------------------|-------------------|
| **Hosted (default)** | Normal executables | Precompiled LLVM bitcode merge | 0 symbols under LTO |
| **Shellcode** | Position-independent flat binary | Full source prelude injection + arena rewrite | 0 symbols (AlwaysInliner) |
| **LTO** | Link-time optimization | Same as Hosted, LTO DCE further prunes | 0 symbols |

The final output binary **exposes no `neverc_string_*` symbols**.

### Drapeaux du compilateur

| Flag | Description |
|------|-------------|
| `-fbuiltin-string` | Enable builtin string type (off by default) |
| `-fshellcode` | Enable shellcode mode (auto-enables builtin string) |
| `-DNEVERC_STRING_ALLOC=xxx` | Custom allocator (triggers full source prelude) |
| `-DNEVERC_STRING_FREE=xxx` | Custom free function |

### Paramètres configurables

| Macro | Default | Description |
|-------|---------|-------------|
| `NEVERC_STRING_ALLOC` | `__builtin_malloc` | Owned buffer allocator |
| `NEVERC_STRING_FREE` | `__builtin_free` | Corresponding free function |
| `NEVERC_STRING_NPOS` | `(size_t)-1` | "Not found" sentinel value |
| `NEVERC_STRING_MAX_LEN` | `(size_t)-2` | Payload length ceiling |
| `NEVERC_STRING_INT_BUF` | `24` | Stack buffer size for `from_int` / `from_uint` |
| `NEVERC_STRING_USER_ARENA_SIZE` | `64 KB` | Shellcode user-mode arena size |
| `NEVERC_STRING_KERNEL_ARENA_SIZE` | `4 KB` | Shellcode kernel-mode arena size |

---

## Mode Hosted : architecture bitcode précompilé

At compiler build time, string runtime functions are precompiled into LLVM bitcode and embedded in the compiler binary. When compiling user code, only a thin header (struct + macros + extern declarations) is injected, and the bitcode is merged after CodeGen via `llvm::Linker::linkModules()`.

### Flux au moment de la compilation

```
prelude .inc (11 fragments)
       │
       ├──→ gen_thin_header.py ──→ extern declarations
       │
       └──→ gen_string_runtime.py ──→ BuiltinStringRuntime.c
                                          │
                                          ▼ neverc -c -emit-llvm
                                          │
                                     .bc (bitcode)
                                          │
                                          ▼ bin2c.py
                                          │
                                   BuiltinStringBitcode.h
                                   (embedded in compiler binary)
```

### Compilation du code utilisateur

```
user.c
  │
  ▼ FrontendAction: inject thin header
  │    struct string { data, len, cap }
  │    macros + inline functions
  │    extern declarations
  │
  ▼ Lexer → Parser → Sema → CodeGen
  │
  ▼ StringRuntimeLinkerPass (PipelineStartEP)
  │    1. Parse embedded bitcode → Module
  │    2. Stamp kRuntimeFnAttr (zero name matching)
  │    3. Compute user-referenced functions + call-graph BFS transitive closure
  │    4. Pre-merge prune unreferenced functions
  │    5. llvm::Linker::linkModules() merge
  │    6. InternalizePass → all symbols become internal
  │    7. Mark-and-sweep DCE → remove unreachable functions
  │    8. llvm.used cleanup
  │
  ▼ Optimization pipeline (incl. AlwaysInliner / GlobalDCE)
  │
  ▼ Output .o / bitcode
```

### `kRuntimeFnAttr` — Cross-layer Function Identification

`StringRuntimeLinkerPass` is the only "name → attribute" conversion point in the entire codebase. All downstream IR passes identify runtime functions via `F.hasFnAttribute("neverc-string-runtime")` (a single bit check), eliminating string prefix matching.

**Benefit:** Obfuscation passes can safely rename runtime functions without breaking the identification chain.

### Processus de bootstrap

Bitcode generation uses a two-stage bootstrap:

```bash
ninja neverc                        # stage 1 (empty bitcode placeholder)
ninja neverc-bootstrap-string-bc    # compile string runtime with neverc itself → .bc
ninja neverc                        # stage 2 (embed real bitcode)
```

### Conditions de repli bitcode

| Condition | Reason |
|-----------|--------|
| `-fshellcode` | StringRuntimePass needs source-level function bodies |
| `-DNEVERC_STRING_ALLOC=xxx` | Custom allocator is baked into bitcode; must recompile |
| Empty embedded bitcode | First build (bootstrap stage 1) has no bitcode |

---

## Mode Shellcode

Does not use bitcode merge; instead injects the full source prelude:

```
FrontendAction: inject full prelude
    │
    ▼ Lexer → Parser → Sema → CodeGen
    │
    ▼ StringRuntimeLinkerPass ← legacy branch: stamp kRuntimeFnAttr, skip merge
    ▼ ZeroRelocPass
    ▼ IndirectBrPass
    ▼ MemIntrinPass
    ▼ StringRuntimePass ← rewrite malloc → stack arena
    ▼ AlwaysInliner ← inline all functions
    ▼ Data2TextPass
    │
    ▼ shellcode.bin
```

`StringRuntimePass` rewrites `__builtin_malloc`/`__builtin_free` into a stack arena allocator:
- All memory allocation happens on the stack, no external library linkage
- Arena uses `{size, next, self, tag}` per-allocation headers for validation
- User-mode arena defaults to 64 KB, kernel-mode to 4 KB
- `AlwaysInliner` ultimately inlines all functions — zero standalone symbols in the shellcode

---

## Dispatch des méthodes

Sema rewrites dot-call syntax into C function calls via `buildNeverCStringRuntimeCall()`:

```c
// User writes
string result = s.find("hello");

// Sema rewrites to
string result = neverc_string_find(s, __neverc_string_make_view("hello", 5));
```

### Priorité de dispatch à 4 niveaux

When the user writes `s.method(args...)`, Sema searches for the target function in this priority order:

1. **Char overload** (`BuiltinStringMethodCharOverloads.def`): Matches when first argument has integer/char type
2. **Arity overload** (`BuiltinStringMethodOverloads.def`): Matches specialized versions by argument count
3. **Default argument completion** (`BuiltinStringMethodDefaults.def`): Appends default values (e.g., `s.substr(pos)` → `s.substr(pos, NPOS)`)
4. **Default mapping** (`BuiltinStringMethodNames.def`): General method → runtime function mapping

### Types de récepteur

Most methods pass the receiver by `string` value. A few require pointer semantics:

| Method | Receiver | Reason |
|--------|----------|--------|
| `s.assign(t)` | `string *` | Needs in-place modification of target |
| `s.swap(t)` | `string *, string *` | Both sides must be addressable |
| `s.capacity()` | `const string *` | Avoids retain copy that would flatten true capacity |

---

## Visibilité des symboles

| Compilation Mode | `neverc_string_*` Symbols in Final Binary |
|-----------------|------------------------------------------|
| LTO (default) | **0** — internalize + LTO DCE fully eliminates |
| Non-LTO -O2 | Few `t` (internal) — GlobalDCE removes unused |
| Non-LTO -O0 | All `t` (internal) — DCE not run but symbols remain internal |
| Shellcode | **0** — AlwaysInliner inlines everything |

---

## Structure des fichiers

```
neverc/
├── include/neverc/Foundation/Builtin/
│   ├── BuiltinString.h                         # API declarations
│   ├── BuiltinStringNames.h                    # Compile-time function name constants + prefix invariant static_assert
│   ├── BuiltinStringRoster.def                 # All functions X-macro registry (single source of truth)
│   ├── BuiltinStringRuntimeNames.def           # Roster's IsPublic=1 filtered view
│   ├── BuiltinStringMethodNames.def            # dot-call method name → runtime function mapping
│   ├── BuiltinStringMethodOverloads.def        # Arity-based method overload table
│   ├── BuiltinStringMethodCharOverloads.def    # Char-typed argument method overloads
│   ├── BuiltinStringMethodDefaults.def         # Default argument completion rules
│   ├── BuiltinStringMethodDefaultArgKinds.def  # Default argument type enum
│   ├── BuiltinStringMethodReceiverKinds.def    # Pointer-receiver method list
│   ├── BuiltinStringMethodReceiverKindsRoster.def  # Receiver type enum
│   ├── BuiltinStringBorrowedViewHelpers.def    # Borrowed view helper list (CStr/Data)
│   ├── BuiltinStringLValueDirectHelpers.def    # Lvalue-direct helper whitelist
│   ├── BuiltinStringWptrProducers.def          # Wide pointer producer list
│   ├── BuiltinStringPreludeFragments.def       # Prelude fragment ordering
│   └── BuiltinStringPrelude/                   # 11 .inc function implementation fragments
│       ├── Type.inc                            # struct definition + base operations
│       ├── Allocation.inc                      # Memory allocation/release/assignment
│       ├── Accessors.inc                       # c_str, data, at, front, back
│       ├── Capacity.inc                        # reserve, shrink_to_fit, capacity
│       ├── Compare.inc                         # eq, compare + case-insensitive family
│       ├── Search.inc                          # find, rfind, contains, starts/ends_with + charset search + _ic family
│       ├── Mutation.inc                        # append, insert, erase, replace, pad, clear, swap, resize
│       ├── Utility.inc                         # clone, substr, trim, repeat, reverse, hash, count, split/join, numeric conversion
│       ├── Encoding.inc                        # UTF-8/16/32, Latin-1, Base64/Base32/Hex, URL/Form encoding
│       ├── WebCodec.inc                        # HTML/JSON/CSV escape/unescape
│       └── Format.inc                          # printf-style format
│
├── include/neverc/Shellcode/IR/
│   ├── StringRuntimeABI.h                      # Cross-layer ABI: kRuntimeFnAttr, arena constants
│   └── StringRuntimePass.h                     # Shellcode arena rewrite pass declaration
│
├── lib/Foundation/Builtin/
│   ├── BuiltinString.cpp                       # Core implementation (method dispatch table + thin header + bitcode API)
│   ├── BuiltinStringThinHeaderPrologue.inc     # Thin header fixed part
│   ├── gen_thin_header.py                      # Extract extern declarations from prelude
│   ├── gen_string_runtime.py                   # Generate standalone compilation unit
│   └── bin2c.py                                # .bc → C header embedding
│
├── lib/Analyze/Core/
│   └── Sema.cpp                                # NeverCStringFnKinds DenseMap
│
├── lib/Emit/Backend/
│   ├── StringRuntimeLinker.h                   # Bitcode merge pass declaration
│   ├── StringRuntimeLinker.cpp                 # Bitcode merge + kRuntimeFnAttr stamp
│   └── BackendUtil.cpp                         # Register linker pass in all modes
│
└── lib/Shellcode/IR/
    └── StringRuntimePass.cpp                   # Shellcode arena rewrite pass
```

### Étapes pour ajouter une nouvelle fonction runtime

1. Add a row to `BuiltinStringRoster.def`: `NEVERC_BUILTIN_STRING_FN(NameId, "neverc_string_xxx", 1)`
2. Implement the function body in the corresponding `BuiltinStringPrelude/*.inc`
3. (Optional) Add a dot-call mapping in `BuiltinStringMethodNames.def`
4. (Optional) Add arity overloads in `BuiltinStringMethodOverloads.def`
5. (Optional) Add char overloads in `BuiltinStringMethodCharOverloads.def`

No other declaration sites need to change.
