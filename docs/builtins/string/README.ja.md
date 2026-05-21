**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 組み込みランタイムシステム](../README.ja.md) · [NeverC ドキュメント](../../README.ja.md)

# NeverC 組み込み `string` 型

## 概要

NeverC は C 言語向けの組み込み `string` 値型を提供します。C++ `std::string` のインターフェース設計と Qt `QString` の Unicode 機能を融合し、C コードでも安全かつ効率的な文字列操作を可能にします。

**有効化:** コンパイル時に `-fbuiltin-string` を指定（デフォルト無効）。以下の場合に自動的に有効化されます：
- 入力ファイルが `.nc` 拡張子の場合（[`.nc` 拡張子ドキュメント](../../nc-extension/README.ja.md)を参照）
- `-fshellcode` モードがアクティブな場合

```bash
neverc hello.nc -o hello                # 自動 — .nc 拡張子で有効化
neverc -fbuiltin-string main.c -o main  # .c ファイルには明示的フラグが必要
```

```c
string greeting = "Hello World";
string msg = greeting + ", NeverC!";
printf("Byte length: %zu\n", msg.len);
printf("Char count: %zu\n", msg.utf8_count());
printf("Content: %s\n", msg.c_str());
```

### 主な特徴

- **値セマンティクス**: `string` は `{ data, len, cap }` の3フィールド構造体で、値渡し・値返し
- **自動メモリ管理**: コンパイラが `CleanupAttr` を付与し、スコープ終了時に owned string を自動解放 — 手動 `free` 不要
- **ネイティブ UTF-8**: すべてのリテラル（`L"..."`、`u"..."`、`U"..."` 含む）がコンパイル時に UTF-8 に変換
- **借用ビュー**: リテラル代入 `string s = "hello"` はゼロアロケーションのビュー（`cap == 0`）を生成、メモリを所有しない
- **ドットコール構文**: `s.find("abc")`、`s.to_upper().trim()` は Sema により対応する C 関数呼び出しに書き換え
- **名前空間隔離**: すべてのランタイムシンボルは `neverc_string_*` プレフィックスを使用、`string_eq` や `STRING_NPOS` などのユーザー識別子との衝突を回避

---

## 核心概念

### 型定義

```c
typedef struct __neverc_string {
  const char *data;  // pointer to UTF-8 bytes
  __SIZE_TYPE__ len; // byte length
  __SIZE_TYPE__ cap; // capacity (0 = borrowed view, >0 = owned memory)
} string;
```

### 所有権モデル: Owned vs Borrowed

`cap` フィールドは「容量」と「所有権フラグ」の二重の役割：

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

### 自動メモリ管理

コンパイラは owned string のライフタイムを自動管理：

```c
void example() {
    string a = "hello";               // borrowed, no release needed
    string b = a + ", world!";        // owned, compiler attaches CleanupAttr
    string c = b.to_upper().trim();   // chaining: intermediate values auto-released
    printf("%s\n", c.c_str());
}   // scope exit: b, c auto-released; a needs no release (borrowed)
```

ワイド文字ポインタ（`w_str()`、`to_utf16_owned()`、`to_utf32_owned()` の戻り値）も自動解放 — Sema がこれらの初期化を検出時に `__neverc_wptr_cleanup` を付与。

### 安全性保証

- **偽造ハンドル防護**: `len > 0 && data == NULL` のハンドルは `__neverc_string_invalid` で遮断、空文字列にショートサーキット
- **オーバーサイズハンドル防護**: `len > NEVERC_STRING_MAX_LEN` のハンドルも同様に遮断
- **一時値安全性**: 一時値（prvalue）に対する `.c_str()` と `.data()` はコンパイルエラー `err_neverc_string_cstr_temporary` を発生、ダングリングポインタを防止
- **ゼロリークテスト**: macOS 上のすべての string テストは `leaks --atExit` 下で実行、"0 leaks" をアサート

### 定数

| Constant | Value | Description |
|----------|-------|-------------|
| `NEVERC_STRING_NPOS` | `(size_t)-1` | Return value when search finds no match, equivalent to `std::string::npos` |
| `NEVERC_STRING_MAX_LEN` | `(size_t)-2` | Maximum payload length ceiling |

---

## 演算子

Sema は演算子を対応するランタイム呼び出しに書き換え：

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

## 完全 API リファレンス

### サイズとアクセス

| Dot-call | Aliases | Runtime Function | Signature | Returns |
|----------|---------|-----------------|-----------|---------|
| `s.len()` | `s.size()` `s.length()` | `neverc_string_len` | `(string s) → size_t` | Byte length |
| `s.empty()` | — | `neverc_string_empty` | `(string s) → int` | `s.len == 0` |
| `s.c_str()` | — | `neverc_string_cstr` | `(string s) → const char *` | NUL-terminated string pointer (borrowed) |
| `s.data()` | — | `neverc_string_data` | `(string s) → void *` | Raw byte pointer (borrowed, no const) |
| `s.at(i)` | `s[i]` | `neverc_string_at` | `(string s, size_t i) → char` | Returns `'\0'` on out-of-bounds |
| `s.front()` | — | `neverc_string_front` | `(string s) → char` | First byte |
| `s.back()` | — | `neverc_string_back` | `(string s) → char` | Last byte |

> **注意**: `.c_str()` と `.data()` は string 内部バッファへの借用ポインタを返します。一時値への使用はコンパイルエラーになります。

### 容量

| Dot-call | Runtime Function | Signature | Description |
|----------|-----------------|-----------|-------------|
| `s.reserve(n)` | `neverc_string_reserve` | `(string s, size_t n) → string` | Reserve at least `n` bytes of capacity |
| `s.shrink_to_fit()` | `neverc_string_shrink_to_fit` | `(string s) → string` | Reduce excess capacity |
| `s.capacity()` | `neverc_string_capacity` | `(const string *s) → size_t` | Current capacity (passed via `&s`) |
| `s.max_size()` | `neverc_string_max_size` | `(string s) → size_t` | Returns `NEVERC_STRING_MAX_LEN` |

### 等価性と比較

| Dot-call | Runtime Function | Signature | Returns |
|----------|-----------------|-----------|---------|
| `s.eq(t)` | `neverc_string_eq` | `(string a, string b) → int` | `1`=equal, `0`=not equal |
| `s.compare(t)` | `neverc_string_compare` | `(string a, string b) → int` | `-1` / `0` / `+1` |
| `s.compare(pos, n, t)` | `neverc_string_compare_substr` | `(string a, size_t pos, size_t n, string b) → int` | Substring compare |
| `s.compare(p1, n1, t, p2, n2)` | `neverc_string_compare_substr2` | `(string a, size_t p1, size_t n1, string b, size_t p2, size_t n2) → int` | Two-substring compare |

#### ASCII 大文字小文字不区別

折り畳みルール: `A-Z → a-z`、他のバイト（`>= 0x80` の UTF-8 継続バイト含む）は不変。HTTP ヘッダーマッチング、ファイル拡張子比較などに適用。

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

### 検索

すべての検索関数は string と char の両方のオーバーロードをサポート — Sema が最初の引数の型に基づき自動ディスパッチ。

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

#### 文字集合検索

内部で256ビットビットマップを使用し O(1) の文字集合メンバーシップテスト、全体計算量 O(n+m)。

| Dot-call | With position | Runtime Function | Description |
|----------|--------------|-----------------|-------------|
| `s.find_first_of(chars)` | `s.find_first_of(chars, pos)` | `neverc_string_find_first_of` / `_from` | First occurrence of any character in `chars` |
| `s.find_last_of(chars)` | `s.find_last_of(chars, pos)` | `neverc_string_find_last_of` / `_to` | Last occurrence |
| `s.find_first_not_of(chars)` | `s.find_first_not_of(chars, pos)` | `neverc_string_find_first_not_of` / `_from` | First character **not in** `chars` |
| `s.find_last_not_of(chars)` | `s.find_last_not_of(chars, pos)` | `neverc_string_find_last_not_of` / `_to` | Last character not in `chars` |

char オーバーロードも利用可能: `s.find_first_of('x')` は `s.find('x')` と等価、`s.find_first_not_of(' ')` は定番の「先頭スペースをスキップ」イディオム。

### 部分文字列とコピー

| Dot-call | Runtime Function | Signature | Description |
|----------|-----------------|-----------|-------------|
| `s.substr(pos)` | `neverc_string_substr` | `(string s, size_t pos, size_t len) → string` | `len` defaults to `NPOS` (to end) with 1 arg |
| `s.substr(pos, len)` | same | same | Extract `[pos, pos+len)` substring |
| `s.copy(out, n)` | `neverc_string_copy` | `(string s, char *out, size_t n) → size_t` | Copy to external buffer, returns bytes copied |
| `s.copy(out, n, pos)` | `neverc_string_copy_from` | `(string s, char *out, size_t n, size_t pos) → size_t` | Copy starting from `pos` |

### 変更操作

変更操作は値型の「入力を消費 + 新しい値を返す」パターンに従います: 関数は入力 string を消費（owned バッファを解放）し、新しい owned string を返します。コンパイラは `s.append(x)` を `s = neverc_string_append(s, x)` に自動書き換え、ユーザーから見ると「インプレース変更」に見えます。

`assign`、`swap`、`capacity` のみが `string *` ポインタレシーバを使用（下記「ポインタレシーバメソッド」参照）。

#### 消費 + 新しい値を返す

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

#### ポインタレシーバメソッド

これらのメソッドは呼び出し元のストレージを直接操作する必要があり、Sema はレシーバを `&s` にラップ：

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

### 数値変換

| Direction | Dot-call | Runtime Function | Signature |
|-----------|----------|-----------------|-----------|
| int → string | — | `neverc_string_from_int` | `(ptrdiff_t v) → string` |
| uint → string | — | `neverc_string_from_uint` | `(size_t v) → string` |
| int → string (with base) | — | `neverc_string_from_int_base` | `(ptrdiff_t v, int base) → string` |
| uint → string (with base) | — | `neverc_string_from_uint_base` | `(size_t v, int base) → string` |
| string → int | `s.to_int()` | `neverc_string_to_int` | `(string s) → ptrdiff_t` |
| string → uint | `s.to_uint()` | `neverc_string_to_uint` | `(string s) → size_t` |

`to_int` / `to_uint` は10進数字のみを解析、最初の非数字文字で停止。`from_int_base` / `from_uint_base` は 2-36 進数をサポート。

```c
string hex = neverc_string_from_int_base(255, 16);  // "ff"
string dec = neverc_string_from_int(-42);            // "-42"

string s = "12345";
ptrdiff_t v = s.to_int();  // 12345
```

### ファクトリ関数

ドットコール経由では使用不可、完全な `neverc_string_*` プレフィックスが必要：

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

## フォーマット

### `s.format(...)` — printf スタイルフォーマット

`neverc_string_format` は libc 非依存の printf サブセットを提供：

```c
string name = "World";
string msg = "Hello %S, number=%d".format(name, 42);
// msg = "Hello World, number=42"
```

#### サポートされるフォーマット指定子

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

**`%s` と `%S` の違い:**

```c
string s = "NeverC";
const char *c = "world";
string result = "hello %s, %S!".format(c, s);
// %s takes const char *, %S takes string value
```

### 標準 `printf` との併用

NeverC `string` は C 標準ライブラリの `printf` に直接渡せません。`.c_str()` で変換：

```c
string s = "Hello World";
printf("Content: %s\n", s.c_str());
printf("Length: %zu\n", s.len);
```

---

## UTF-8 / Unicode

内部は常に UTF-8 エンコード。バイトレベル操作（`s.len`、`s.at(i)`、`s.find(...)` など）は**バイト**単位、コードポイントレベル操作は `utf8_*` ファミリーを使用：

### コードポイント操作

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

### エンコーディング変換

| Dot-call | Runtime Function | Signature | Description |
|----------|-----------------|-----------|-------------|
| `s.to_utf16(out, cap)` | `neverc_string_to_utf16` | `(string s, uint16_t *out, size_t cap) → size_t` | Write to caller-owned buffer |
| `s.to_utf32(out, cap)` | `neverc_string_to_utf32` | `(string s, uint32_t *out, size_t cap) → size_t` | Write to caller-owned buffer |
| `s.to_utf16_owned()` | `neverc_string_to_utf16_owned` | `(string s) → uint16_t *` | Returns freshly allocated NUL-terminated buffer |
| `s.to_utf32_owned()` | `neverc_string_to_utf32_owned` | `(string s) → uint32_t *` | Returns freshly allocated NUL-terminated buffer |
| `s.w_str()` | `neverc_string_w_str` | `(string s) → wchar_t *` | Platform-adaptive: Windows=UTF-16, Linux/macOS=UTF-32 |
| `s.to_latin1(out, cap)` | `neverc_string_to_latin1` | `(string s, char *out, size_t cap) → size_t` | Convert to Latin-1 (ISO-8859-1) |

`to_utf16_owned()`、`to_utf32_owned()`、`w_str()` の戻りポインタは**自動解放**（Sema が `__neverc_wptr_cleanup` を付与）。手動解放は `neverc_string_wfree(buf)` を使用。

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

## バイトエンコーディング

すべてのエンコーディングメソッドはレシーバを消費し新しい owned string を返し、メソッドチェーンをサポート。

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

### URL / Form エンコーディング

| Dot-call | Runtime Function | Standard | Description |
|----------|-----------------|----------|-------------|
| `s.url_encode()` | `neverc_string_url_encode` | RFC 3986 | Percent-encoding (unreserved set preserved, uppercase `%XX`) |
| `s.url_decode()` | `neverc_string_url_decode` | RFC 3986 | Decode |
| `s.form_encode()` | `neverc_string_form_encode` | `application/x-www-form-urlencoded` | Space → `+`, `+` → `%2B` |
| `s.form_decode()` | `neverc_string_form_decode` | same | `+` → space |

> `url_encode` と `form_encode` は互換性なし — `form_encode` でエンコードされた `+` を `url_decode` でデコードするとリテラル `+` のまま。

---

## Web コーデック

HTML / JSON / CSV リテラルレベルのエスケープ/アンエスケープ。各ペアは厳密なラウンドトリップを維持。

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

## メソッドチェーン

`string` を返すすべてのメソッドはチェーンをサポート。中間値は自動解放、リーク無し：

```c
string result = input.to_upper().trim().replace_all(" ", "_");
string encoded = payload.to_base64_url();
string cleaned = raw.url_decode().from_base64().trim();
```

---

## コンパイルモード

### 3つの動作モード

| Mode | Description | String Function Body Source | Symbol Visibility |
|------|-------------|---------------------------|-------------------|
| **Hosted (default)** | Normal executables | Precompiled LLVM bitcode merge | 0 symbols under LTO |
| **Shellcode** | Position-independent flat binary | Full source prelude injection + arena rewrite | 0 symbols (AlwaysInliner) |
| **LTO** | Link-time optimization | Same as Hosted, LTO DCE further prunes | 0 symbols |

最終出力バイナリに**`neverc_string_*` シンボルは露出しません**。

### コンパイラフラグ

| Flag | Description |
|------|-------------|
| `-fbuiltin-string` | Enable builtin string type (off by default) |
| `-fshellcode` | Enable shellcode mode (auto-enables builtin string) |
| `-DNEVERC_STRING_ALLOC=xxx` | Custom allocator (triggers full source prelude) |
| `-DNEVERC_STRING_FREE=xxx` | Custom free function |

### 設定可能なパラメータ

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

## Hosted モード: プリコンパイル Bitcode アーキテクチャ

コンパイラビルド時に string ランタイム関数は LLVM bitcode にプリコンパイルされ、コンパイラバイナリに埋め込まれます。ユーザーコードのコンパイル時には薄いヘッダー（struct + macros + extern 宣言）のみが注入され、CodeGen 後に `llvm::Linker::linkModules()` で bitcode がマージされます。

### ビルド時のフロー

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

### ユーザーコードのコンパイル

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

### `kRuntimeFnAttr` — レイヤー横断の関数識別

`StringRuntimeLinkerPass` はコードベース全体で唯一の「名前→属性」変換点。すべての下流 IR パスは `F.hasFnAttribute("neverc-string-runtime")`（単一ビットチェック）でランタイム関数を識別、文字列プレフィックスマッチングを排除。

**利点:** 難読化パスはランタイム関数を安全にリネームでき、識別チェーンを壊しません。

### ブートストラッププロセス

Bitcode 生成は2段階ブートストラップを使用：

```bash
ninja neverc                        # stage 1 (empty bitcode placeholder)
ninja neverc-bootstrap-string-bc    # compile string runtime with neverc itself → .bc
ninja neverc                        # stage 2 (embed real bitcode)
```

### Bitcode フォールバック条件

| Condition | Reason |
|-----------|--------|
| `-fshellcode` | StringRuntimePass needs source-level function bodies |
| `-DNEVERC_STRING_ALLOC=xxx` | Custom allocator is baked into bitcode; must recompile |
| Empty embedded bitcode | First build (bootstrap stage 1) has no bitcode |

---

## Shellcode モード

bitcode マージを使用せず、完全なソース prelude を注入：

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

`StringRuntimePass` は `__builtin_malloc`/`__builtin_free` をスタック arena アロケータに書き換え：
- すべてのメモリ割り当てはスタック上、外部ライブラリリンケージなし
- arena はバリデーション用に `{size, next, self, tag}` per-allocation ヘッダーを使用
- ユーザーモード arena はデフォルト 64 KB、カーネルモードは 4 KB
- `AlwaysInliner` は最終的にすべての関数をインライン化 — shellcode 内のスタンドアロンシンボルはゼロ

---

## メソッドディスパッチ

Sema は `buildNeverCStringRuntimeCall()` を通じてドットコール構文を C 関数呼び出しに書き換え：

```c
// User writes
string result = s.find("hello");

// Sema rewrites to
string result = neverc_string_find(s, __neverc_string_make_view("hello", 5));
```

### 4層ディスパッチ優先度

ユーザーが `s.method(args...)` を書くと、Sema は以下の優先順序でターゲット関数を検索：

1. **Char オーバーロード**（`BuiltinStringMethodCharOverloads.def`）: 最初の引数が整数/char 型の場合にマッチ
2. **Arity オーバーロード**（`BuiltinStringMethodOverloads.def`）: 引数の数で特化版にマッチ
3. **デフォルト引数補完**（`BuiltinStringMethodDefaults.def`）: デフォルト値を追加（例: `s.substr(pos)` → `s.substr(pos, NPOS)`）
4. **デフォルトマッピング**（`BuiltinStringMethodNames.def`）: 汎用の method → ランタイム関数マッピング

### レシーバ型

ほとんどのメソッドはレシーバを `string` 値で渡します。一部はポインタセマンティクスが必要：

| Method | Receiver | Reason |
|--------|----------|--------|
| `s.assign(t)` | `string *` | Needs in-place modification of target |
| `s.swap(t)` | `string *, string *` | Both sides must be addressable |
| `s.capacity()` | `const string *` | Avoids retain copy that would flatten true capacity |

---

## シンボル可視性

| Compilation Mode | `neverc_string_*` Symbols in Final Binary |
|-----------------|------------------------------------------|
| LTO (default) | **0** — internalize + LTO DCE fully eliminates |
| Non-LTO -O2 | Few `t` (internal) — GlobalDCE removes unused |
| Non-LTO -O0 | All `t` (internal) — DCE not run but symbols remain internal |
| Shellcode | **0** — AlwaysInliner inlines everything |

---

## ファイル構成

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

### 新しいランタイム関数を追加する手順

1. Add a row to `BuiltinStringRoster.def`: `NEVERC_BUILTIN_STRING_FN(NameId, "neverc_string_xxx", 1)`
2. Implement the function body in the corresponding `BuiltinStringPrelude/*.inc`
3. (Optional) Add a dot-call mapping in `BuiltinStringMethodNames.def`
4. (Optional) Add arity overloads in `BuiltinStringMethodOverloads.def`
5. (Optional) Add char overloads in `BuiltinStringMethodCharOverloads.def`

他の宣言箇所の変更は不要です。
