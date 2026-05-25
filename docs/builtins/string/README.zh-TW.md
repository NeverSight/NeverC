**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 內建執行時系統](../README.zh-TW.md) · [NeverC 文件](../../README.zh-TW.md)

# NeverC 內建 `string` 型別

## 概述

NeverC 提供了一個面向 C 語言的內建 `string` 值型別。它融合了 C++ `std::string` 的介面設計和 Qt `QString` 的 Unicode 能力，讓 C 程式碼也能寫出安全、高效的字串操作。

**啟用方式：** 編譯時傳遞 `-fbuiltin-string`（預設關閉）。以下情況自動啟用：
- 輸入檔案為 `.nc` 副檔名（參見 [`.nc` 副檔名文件](../../nc-extension/README.zh-TW.md)）
- `-fshellcode` 模式啟動時

```bash
neverc hello.nc -o hello                # 自動 — .nc 副檔名啟用
neverc -fbuiltin-string main.c -o main  # .c 檔案需明確旗標
```

```c
string greeting = "你好世界";
string msg = greeting + ", NeverC!";
printf("位元組長度: %zu\n", msg.len);
printf("字元數: %zu\n", msg.utf8_count());
printf("內容: %s\n", msg.c_str());
```

### 核心特點

- **值語義**：`string` 是 `{ data, len, cap }` 三欄位結構體，按值傳遞和返回
- **自動記憶體管理**：編譯器透過 `CleanupAttr` 在作用域退出時自動釋放 owned string，無需手動 `free`
- **UTF-8 原生**：所有字面量（含 `L"..."`、`u"..."`、`U"..."`）在編譯期統一摺疊為 UTF-8
- **借用檢視**：字面量賦值 `string s = "hello"` 產生零分配檢視（`cap == 0`），不擁有記憶體
- **Dot-call 語法**：`s.find("abc")`、`s.to_upper().trim()` 由 Sema 重寫為對應的 C 函式呼叫
- **名稱空間隔離**：所有 runtime 符號使用 `neverc_string_*` 字首，不會與使用者程式碼中的 `string_eq`、`STRING_NPOS` 等識別符號衝突

---

## 核心概念

### 型別定義

```c
typedef struct __neverc_string {
  const char *data;  // 指向 UTF-8 位元組的指標
  __SIZE_TYPE__ len; // 位元組長度
  __SIZE_TYPE__ cap; // 容量（0 = 借用檢視，>0 = 擁有記憶體）
} string;
```

### 所有權模型：Owned vs Borrowed

`cap` 欄位同時承擔"容量"和"所有權標記"雙重角色：

| 狀態 | `cap` | 含義 | 示例 |
|------|-------|------|------|
| **Borrowed（借用檢視）** | `== 0` | 不擁有記憶體，指向外部儲存（字面量、棧緩衝區等） | `string s = "hello"` |
| **Owned（擁有記憶體）** | `> 0` | 擁有透過 `NEVERC_STRING_ALLOC` 分配的緩衝區 | `string s = a + b` |

```c
string a = "hello";           // Borrowed: data → 字面量儲存, cap == 0
string b = a + ", world!";    // Owned: 分配新緩衝區, cap > 0
string c = a.to_upper();      // Owned: 變換產生新緩衝區
string d = neverc_string_view(buf, len); // Borrowed: 包裝外部緩衝區
```

### 自動記憶體管理

編譯器自動處理 owned string 的生命週期：

```c
void example() {
    string a = "hello";               // borrowed, 無需釋放
    string b = a + ", world!";        // owned, 編譯器自動附加 CleanupAttr
    string c = b.to_upper().trim();   // 鏈式呼叫: 中間值自動釋放
    printf("%s\n", c.c_str());
}   // 作用域退出: b, c 自動釋放; a 無需釋放 (borrowed)
```

寬字元指標（`w_str()`、`to_utf16_owned()`、`to_utf32_owned()` 返回的指標）同樣自動釋放——Sema 檢測到這些初始化時自動附加 `__neverc_wptr_cleanup`。

#### 複合型別自動清理

編譯器遞迴偵測並自動釋放陣列、結構體及巢狀組合中的 `string` 成員：

```c
string keys[] = {"admin".encrypt(), "root".encrypt(), "user".encrypt()};
typedef struct { string name; string value; } kv_pair;
kv_pair p = {.name = "key".encrypt(), .value = "val".encrypt()};
```

清理邏輯在編譯時（CodeGen 層）走訪型別樹，為每個巢狀的 `string` 產生逐元素 cleanup 呼叫。僅釋放 owned 字串（`cap != 0`），view 會被跳過。

### 安全性保證

- **Forged handle 防護**：`len > 0 && data == NULL` 的偽造控制代碼會被 `__neverc_string_invalid` 攔截，短路返回空字串
- **Oversized handle 防護**：`len > NEVERC_STRING_MAX_LEN` 的超大控制代碼同樣被攔截
- **臨時值安全**：`.c_str()` 和 `.data()` 對臨時值（prvalue）會觸發編譯錯誤 `err_neverc_string_cstr_temporary`，防止懸空指標
- **零洩漏測試**：macOS 上所有 string 測試在 `leaks --atExit` 門控下執行，斷言 "0 leaks"

### 常量

| 常量 | 值 | 說明 |
|------|-----|------|
| `NEVERC_STRING_NPOS` | `(size_t)-1` | 搜尋未命中時的返回值，等價於 `std::string::npos` |
| `NEVERC_STRING_MAX_LEN` | `(size_t)-2` | 最大有效負載長度上限 |

---

## 運算子

Sema 將運算子重寫為對應的 runtime 呼叫：

| 運算子 | 等價呼叫 | 返回型別 |
|--------|---------|----------|
| `a + b` | `__neverc_string_cat(a, b)` | `string`（owned） |
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

## 完整 API 參考

### 大小與訪問

| Dot-call | 別名 | Runtime 函式 | 簽名 | 返回值 |
|----------|------|-------------|------|--------|
| `s.len()` | `s.size()` `s.length()` | `neverc_string_len` | `(string s) → size_t` | 位元組長度 |
| `s.empty()` | — | `neverc_string_empty` | `(string s) → int` | `s.len == 0` |
| `s.c_str()` | — | `neverc_string_cstr` | `(string s) → const char *` | NUL 終止的字串指標（借用） |
| `s.data()` | — | `neverc_string_data` | `(string s) → void *` | 原始位元組指標（借用，無 const） |
| `s.at(i)` | `s[i]` | `neverc_string_at` | `(string s, size_t i) → char` | 越界返回 `'\0'` |
| `s.front()` | — | `neverc_string_front` | `(string s) → char` | 首位元組 |
| `s.back()` | — | `neverc_string_back` | `(string s) → char` | 末位元組 |

> **注意**：`.c_str()` 和 `.data()` 返回的是指向 string 內部緩衝區的借用指標，禁止對臨時值使用（編譯器會報錯）。

### 容量

| Dot-call | Runtime 函式 | 簽名 | 說明 |
|----------|-------------|------|------|
| `s.reserve(n)` | `neverc_string_reserve` | `(string s, size_t n) → string` | 預留至少 `n` 位元組容量 |
| `s.shrink_to_fit()` | `neverc_string_shrink_to_fit` | `(string s) → string` | 縮減多餘容量 |
| `s.capacity()` | `neverc_string_capacity` | `(const string *s) → size_t` | 當前容量（透過 `&s` 傳遞） |
| `s.max_size()` | `neverc_string_max_size` | `(string s) → size_t` | 返回 `NEVERC_STRING_MAX_LEN` |

### 相等與比較

| Dot-call | Runtime 函式 | 簽名 | 返回值 |
|----------|-------------|------|--------|
| `s.eq(t)` | `neverc_string_eq` | `(string a, string b) → int` | `1`=相等, `0`=不等 |
| `s.compare(t)` | `neverc_string_compare` | `(string a, string b) → int` | `-1` / `0` / `+1` |
| `s.compare(pos, n, t)` | `neverc_string_compare_substr` | `(string a, size_t pos, size_t n, string b) → int` | 子串比較 |
| `s.compare(p1, n1, t, p2, n2)` | `neverc_string_compare_substr2` | `(string a, size_t p1, size_t n1, string b, size_t p2, size_t n2) → int` | 雙子串比較 |

#### ASCII 大小寫不敏感

摺疊規則：`A-Z → a-z`，其餘位元組（含 `>= 0x80` 的 UTF-8 續位元組）不變。適用於 HTTP 頭匹配、副檔名比較等場景。

| Dot-call | Runtime 函式 | 說明 |
|----------|-------------|------|
| `s.eq_ic(t)` | `neverc_string_eq_ic` | 不區分大小寫相等 |
| `s.compare_ic(t)` | `neverc_string_compare_ic` | 不區分大小寫 3-way 比較 |
| `s.find_ic(t)` | `neverc_string_find_ic` | 不區分大小寫查詢 |
| `s.contains_ic(t)` | `neverc_string_contains_ic` | 不區分大小寫包含 |
| `s.starts_with_ic(t)` | `neverc_string_starts_with_ic` | 不區分大小寫字首匹配 |
| `s.ends_with_ic(t)` | `neverc_string_ends_with_ic` | 不區分大小寫字尾匹配 |

```c
string ct = "Content-Type";
if (ct.eq_ic("content-type")) { /* 匹配 */ }

string path = "photo.PNG";
if (path.ends_with_ic(".png")) { /* 匹配 */ }
```

### 搜尋

所有搜尋函式支援 string 和 char 兩種過載——Sema 根據首引數型別自動分派。

| Dot-call | char 過載 | Runtime 函式 | 返回值 |
|----------|-----------|-------------|--------|
| `s.find(t)` | `s.find(ch)` | `neverc_string_find` / `find_char` | 首次出現的位元組偏移，未找到返回 `NEVERC_STRING_NPOS` |
| `s.find(t, pos)` | `s.find(ch, pos)` | `neverc_string_find_from` / `find_char_from` | 從 `pos` 開始查詢 |
| `s.rfind(t)` | `s.rfind(ch)` | `neverc_string_rfind` / `rfind_char` | 最後一次出現的偏移 |
| `s.rfind(t, pos)` | `s.rfind(ch, pos)` | `neverc_string_rfind_to` / `rfind_char_to` | 在 `[0, pos]` 範圍內反向查詢 |
| `s.contains(t)` | `s.contains(ch)` | `neverc_string_contains` / `contains_char` | `1`=包含, `0`=不包含 |
| `s.starts_with(t)` | `s.starts_with(ch)` | `neverc_string_starts_with` / `starts_with_char` | 字首匹配 |
| `s.ends_with(t)` | `s.ends_with(ch)` | `neverc_string_ends_with` / `ends_with_char` | 字尾匹配 |
| `s.count(t)` | `s.count(ch)` | `neverc_string_count` / `count_char` | 出現次數 |

#### 字符集搜尋

內部使用 256-bit bitmap 實現 O(1) 字符集成員檢測，總體複雜度 O(n+m)。

| Dot-call | 帶位置過載 | Runtime 函式 | 說明 |
|----------|-----------|-------------|------|
| `s.find_first_of(chars)` | `s.find_first_of(chars, pos)` | `neverc_string_find_first_of` / `_from` | 首次出現 `chars` 中任一字元 |
| `s.find_last_of(chars)` | `s.find_last_of(chars, pos)` | `neverc_string_find_last_of` / `_to` | 最後出現 |
| `s.find_first_not_of(chars)` | `s.find_first_not_of(chars, pos)` | `neverc_string_find_first_not_of` / `_from` | 首次出現 **不在** `chars` 中的字元 |
| `s.find_last_not_of(chars)` | `s.find_last_not_of(chars, pos)` | `neverc_string_find_last_not_of` / `_to` | 最後出現不在 `chars` 中的字元 |

char 過載同樣可用：`s.find_first_of('x')` 等價於 `s.find('x')`，`s.find_first_not_of(' ')` 是經典的"跳過前導空格"用法。

### 子串與複製

| Dot-call | Runtime 函式 | 簽名 | 說明 |
|----------|-------------|------|------|
| `s.substr(pos)` | `neverc_string_substr` | `(string s, size_t pos, size_t len) → string` | 1 引數時 `len` 預設為 `NPOS`（到末尾） |
| `s.substr(pos, len)` | 同上 | 同上 | 提取 `[pos, pos+len)` 子串 |
| `s.copy(out, n)` | `neverc_string_copy` | `(string s, char *out, size_t n) → size_t` | 複製到外部緩衝區，返回實際複製位元組數 |
| `s.copy(out, n, pos)` | `neverc_string_copy_from` | `(string s, char *out, size_t n, size_t pos) → size_t` | 從 `pos` 開始複製 |

### 變更操作

變更操作遵循值型別的"消費輸入 + 返回新值"模式：函式消費傳入的 string（釋放其 owned 緩衝區），返回一個新的 owned string。編譯器自動將 `s.append(x)` 重寫為 `s = neverc_string_append(s, x)`，從使用者角度看是"原地修改"。

只有 `assign`、`swap`、`capacity` 透過 `string *` 指標接收（見下方"指標接收者方法"）。

#### 消費 + 返回新值

| Dot-call | Runtime 函式 | 簽名 | 說明 |
|----------|-------------|------|------|
| `s.append(t)` | `neverc_string_append` | `(string s, string t) → string` | 追加 string |
| `s.append(n, ch)` | `neverc_string_append_char` | `(string s, size_t n, char ch) → string` | 追加 n 個 ch |
| `s.push_back(ch)` | `neverc_string_push_back` | `(string s, char ch) → string` | 追加單字元 |
| `s.pop_back()` | `neverc_string_pop_back` | `(string s) → string` | 移除末字元 |
| `s.insert(pos, t)` | `neverc_string_insert` | `(string s, size_t pos, string t) → string` | 插入 string |
| `s.insert(pos, n, ch)` | `neverc_string_insert_char` | `(string s, size_t pos, size_t n, char ch) → string` | 插入 n 個 ch |
| `s.erase(pos)` | `neverc_string_erase` | `(string s, size_t pos, size_t count) → string` | 1 引數時 `count` 預設為 `NPOS` |
| `s.erase(pos, count)` | 同上 | 同上 | 刪除 `[pos, pos+count)` |
| `s.replace(pos, n, t)` | `neverc_string_replace` | `(string s, size_t pos, size_t n, string t) → string` | 替換子串 |
| `s.replace(pos, n, m, ch)` | `neverc_string_replace_char` | `(string s, size_t pos, size_t n, size_t m, char ch) → string` | 用 m 個 ch 替換 |
| `s.replace_all(from, to)` | `neverc_string_replace_all` | `(string s, string from, string to) → string` | 全域性替換（NeverC 擴充套件） |
| `s.clear()` | `neverc_string_clear` | `(string s) → string` | 清空內容（返回空字串） |
| `s.resize(n)` | `neverc_string_resize` | `(string s, size_t n, char ch) → string` | 1 引數時 `ch` 預設為 `'\0'` |
| `s.resize(n, ch)` | 同上 | 同上 | 調整大小，擴充套件部分用 `ch` 填充 |
| `s.pad_left(w, ch)` | `neverc_string_pad_left` | `(string s, size_t width, char ch) → string` | 左填充 |
| `s.pad_right(w, ch)` | `neverc_string_pad_right` | `(string s, size_t width, char ch) → string` | 右填充 |
| `s.clone()` | `neverc_string_clone` | `(string s) → string` | 深複製（owned 直接移動，borrowed 分配新緩衝區） |
| `s.to_lower()` | `neverc_string_to_lower` | `(string s) → string` | ASCII 轉小寫 |
| `s.to_upper()` | `neverc_string_to_upper` | `(string s) → string` | ASCII 轉大寫 |
| `s.trim()` | `neverc_string_trim` | `(string s) → string` | 去除兩端空白 |
| `s.ltrim()` | `neverc_string_ltrim` | `(string s) → string` | 去除左側空白 |
| `s.rtrim()` | `neverc_string_rtrim` | `(string s) → string` | 去除右側空白 |
| `s.repeat(n)` | `neverc_string_repeat` | `(string s, size_t n) → string` | 重複 n 次 |
| `s.reverse()` | `neverc_string_reverse` | `(string s) → string` | 位元組級反轉 |
| `s.hash()` | `neverc_string_hash` | `(string s) → unsigned long long` | FNV-1a 64-bit hash |

#### 指標接收者方法

這些方法需要直接操作呼叫者的儲存，Sema 將接收者包裝為 `&s`：

| Dot-call | Runtime 函式 | 簽名 | 說明 |
|----------|-------------|------|------|
| `s.assign(t)` | `neverc_string_assign` | `(string *dst, string src)` | 替換內容（釋放舊值 + 安裝新值） |
| `s.assign(n, ch)` | `neverc_string_assign_char` | `(string *dst, size_t n, char ch)` | 用 n 個 ch 替換 |
| `s.swap(t)` | `neverc_string_swap` | `(string *a, string *b)` | 交換兩個 string 的控制代碼 |

### Split / Join

| Dot-call | Runtime 函式 | 簽名 | 說明 |
|----------|-------------|------|------|
| `s.split_first(sep)` | `neverc_string_split_first` | `(string s, string sep) → string` | 分隔符前的第一段 |
| `s.split_rest(sep)` | `neverc_string_split_rest` | `(string s, string sep) → string` | 第一個分隔符後的其餘部分 |
| `s.split_before_last(sep)` | `neverc_string_split_before_last` | `(string s, string sep) → string` | 最後一個分隔符之前 |
| `s.split_after_last(sep)` | `neverc_string_split_after_last` | `(string s, string sep) → string` | 最後一個分隔符之後 |
| `s.split(sep, &items, &n)` | `neverc_string_split` | `(string s, string sep, string **items, size_t *count)` | 完整分割為陣列 |
| — | `neverc_string_split_free` | `(string *parts, size_t n)` | 釋放 `split` 產生的陣列 |
| — | `neverc_string_join` | `(const string *items, size_t n, string sep) → string` | 用分隔符連線陣列 |

```c
// 逐段剝離
string path = "/usr/local/bin";
string first = path.split_first("/");   // ""
string rest  = path.split_rest("/");    // "usr/local/bin"

// 路徑分解
string file = "src/lib/main.c";
string dir  = file.split_before_last("/");  // "src/lib"
string base = file.split_after_last("/");   // "main.c"

// 完整分割
string csv = "a,b,c";
string *parts;
size_t count;
csv.split(",", &parts, &count);
for (size_t i = 0; i < count; i++)
    printf("[%s]", parts[i].c_str());
neverc_string_split_free(parts, count);
```

### 數值轉換

| 方向 | Dot-call | Runtime 函式 | 簽名 |
|------|----------|-------------|------|
| int → string | — | `neverc_string_from_int` | `(ptrdiff_t v) → string` |
| uint → string | — | `neverc_string_from_uint` | `(size_t v) → string` |
| int → string（指定進位制） | — | `neverc_string_from_int_base` | `(ptrdiff_t v, int base) → string` |
| uint → string（指定進位制） | — | `neverc_string_from_uint_base` | `(size_t v, int base) → string` |
| string → int | `s.to_int()` | `neverc_string_to_int` | `(string s) → ptrdiff_t` |
| string → uint | `s.to_uint()` | `neverc_string_to_uint` | `(string s) → size_t` |

`to_int` / `to_uint` 僅解析十進位制數字，遇到非數字字元停止。`from_int_base` / `from_uint_base` 支援 2-36 進位制。

```c
string hex = neverc_string_from_int_base(255, 16);  // "ff"
string dec = neverc_string_from_int(-42);            // "-42"

string s = "12345";
ptrdiff_t v = s.to_int();  // 12345
```

### 工廠函式

不透過 dot-call 使用，需要完整的 `neverc_string_*` 字首拼寫：

| Runtime 函式 | 簽名 | 說明 |
|-------------|------|------|
| `neverc_string_from_cstr(p)` | `(const char *p) → string` | 從 C 字串建立 owned string |
| `neverc_string_from_char(ch)` | `(char ch) → string` | 從單字元建立 |
| `neverc_string_from_utf32_char(cp)` | `(uint32_t cp) → string` | 從 Unicode codepoint 建立 UTF-8 |
| `neverc_string_from_utf16(d, n)` | `(const uint16_t *d, size_t n) → string` | 從 UTF-16 緩衝區建立 |
| `neverc_string_from_utf32(d, n)` | `(const uint32_t *d, size_t n) → string` | 從 UTF-32 緩衝區建立 |
| `neverc_string_from_latin1(d, n)` | `(const char *d, size_t n) → string` | 從 Latin-1 (ISO-8859-1) 建立 |
| `neverc_string_view(p, n)` | `(const char *p, size_t n) → string` | 建立借用檢視（不複製） |
| `neverc_string_clone(s)` | `(string s) → string` | 深複製 |
| `neverc_string_free(s)` | `(string s)` | 釋放 owned string（borrowed 為 no-op） |
| `neverc_string_array_free(items, n)` | `(string *items, size_t n)` | 釋放 string 陣列的所有元素 |

---

## 格式化

### `s.format(...)` — printf 風格格式化

`neverc_string_format` 提供無 libc 依賴的 printf 子集：

```c
string name = "世界";
string msg = "你好 %S, 數字=%d".format(name, 42);
// msg = "你好 世界, 數字=42"
```

#### 支援的格式說明符

| 說明符 | 型別 | 說明 |
|--------|------|------|
| `%d` / `%i` | `int` | 有符號十進位制 |
| `%u` | `unsigned int` | 無符號十進位制 |
| `%ld` / `%lu` | `long` / `unsigned long` | 長整型 |
| `%lld` / `%llu` | `long long` / `unsigned long long` | 長長整型 |
| `%x` / `%lx` / `%llx` | 對應無符號型別 | 小寫十六進位制 |
| `%s` | `const char *` | C 字串（NUL 終止；NULL 輸出 `(null)`） |
| `%S` | `string` | **NeverC `string` 值**（按值傳遞，輸出後釋放） |
| `%c` | `int` | 單位元組字元 |
| `%p` | `void *` | 指標（小寫 hex 帶 `0x` 字首） |
| `%%` | — | 字面 `%` |

**`%s` vs `%S` 的區別：**

```c
string s = "NeverC";
const char *c = "world";
string result = "hello %s, %S!".format(c, s);
// %s 接收 const char *, %S 接收 string 值
```

### 與標準 `printf` 配合

NeverC `string` 不能直接傳給 C 標準庫的 `printf`。使用 `.c_str()` 轉換：

```c
string s = "你好世界";
printf("內容: %s\n", s.c_str());
printf("長度: %zu\n", s.len);
```

---

## UTF-8 / Unicode

內部始終為 UTF-8 編碼。位元組級別的操作（`s.len`、`s.at(i)`、`s.find(...)` 等）以**位元組**為單位，codepoint 級別的操作使用 `utf8_*` 系列：

### Codepoint 操作

| Dot-call | 別名 | Runtime 函式 | 返回值 |
|----------|------|-------------|--------|
| `s.utf8_count()` | `s.utf8_size()` `s.utf8_length()` | `neverc_string_utf8_count` | codepoint 數量 |
| `s.utf8_valid()` | `s.is_utf8()` | `neverc_string_utf8_valid` | `1`=有效 UTF-8 |
| `s.utf8_at(i)` | — | `neverc_string_utf8_at` | 第 i 個 codepoint（`uint32_t`） |
| `s.utf8_byte_index(i)` | — | `neverc_string_utf8_byte_index` | 第 i 個 codepoint 的位元組偏移 |
| `s.utf8_substr(pos)` | — | `neverc_string_utf8_substr` | 從第 pos 個 codepoint 到末尾 |
| `s.utf8_substr(pos, n)` | — | 同上 | 提取 n 個 codepoint |
| `s.is_ascii()` | — | `neverc_string_is_ascii` | `1`=全部位元組 < 0x80 |

```c
string s = "你好世界 🎉";
printf("位元組: %zu\n", s.len);          // 16
printf("字元: %zu\n", s.utf8_count()); // 5 (4 個漢字 + 1 個 emoji)
printf("第2個: U+%04X\n", s.utf8_at(1)); // U+597D (好)
```

### 編碼轉換

| Dot-call | Runtime 函式 | 簽名 | 說明 |
|----------|-------------|------|------|
| `s.to_utf16(out, cap)` | `neverc_string_to_utf16` | `(string s, uint16_t *out, size_t cap) → size_t` | 寫入 caller-owned 緩衝區 |
| `s.to_utf32(out, cap)` | `neverc_string_to_utf32` | `(string s, uint32_t *out, size_t cap) → size_t` | 寫入 caller-owned 緩衝區 |
| `s.to_utf16_owned()` | `neverc_string_to_utf16_owned` | `(string s) → uint16_t *` | 返回新分配的 NUL 終止緩衝區 |
| `s.to_utf32_owned()` | `neverc_string_to_utf32_owned` | `(string s) → uint32_t *` | 返回新分配的 NUL 終止緩衝區 |
| `s.w_str()` | `neverc_string_w_str` | `(string s) → wchar_t *` | 平臺自適應：Windows=UTF-16, Linux/macOS=UTF-32 |
| `s.to_latin1(out, cap)` | `neverc_string_to_latin1` | `(string s, char *out, size_t cap) → size_t` | 轉為 Latin-1 (ISO-8859-1) |

`to_utf16_owned()`、`to_utf32_owned()`、`w_str()` 返回的指標**自動釋放**（Sema 附加 `__neverc_wptr_cleanup`）。手動釋放使用 `neverc_string_wfree(buf)`。

```c
// UTF-8 直接輸出（現代終端原生支援）
string s = "你好世界 🎉";
printf("%s\n", s.c_str());

// Win32 寬字元 API
string title = "NeverC";
__WCHAR_TYPE__ *ws = title.w_str();
MessageBoxW(NULL, ws, L"標題", MB_OK);
// ws 自動釋放，無需手動管理
```

---

## 位元組編碼

所有編碼方法消費接收者並返回新的 owned string，支援鏈式呼叫。

### Base64 / Base32 / Hex

| Dot-call | Runtime 函式 | 標準 | 說明 |
|----------|-------------|------|------|
| `s.to_base64()` | `neverc_string_to_base64` | RFC 4648 §4 | 標準 Base64 + `=` 填充 |
| `s.from_base64()` | `neverc_string_from_base64` | RFC 4648 §4 | 解碼 |
| `s.to_base64_url()` | `neverc_string_to_base64_url` | RFC 4648 §5 | URL 安全 Base64（無填充），用於 JWT/JOSE |
| `s.from_base64_url()` | `neverc_string_from_base64_url` | RFC 4648 §5 | 解碼 |
| `s.to_base32()` | `neverc_string_to_base32` | RFC 4648 §6 | 大寫 + `=` 填充，用於 TOTP/Google Authenticator |
| `s.from_base32()` | `neverc_string_from_base32` | RFC 4648 §6 | 解碼（大小寫均接受） |
| `s.to_hex()` | `neverc_string_to_hex` | — | 小寫十六進位制編碼 |
| `s.from_hex()` | `neverc_string_from_hex` | — | 大小寫不敏感解碼 |

```c
string data = "Hello, NeverC!";
string b64 = data.to_base64();         // "SGVsbG8sIE5ldmVyQyE="
string back = b64.from_base64();       // "Hello, NeverC!"

string jwt_part = data.to_base64_url(); // URL-safe, 無填充
string hex = data.to_hex();             // "48656c6c6f2c204e657665724321"
```

### URL / Form 編碼

| Dot-call | Runtime 函式 | 標準 | 說明 |
|----------|-------------|------|------|
| `s.url_encode()` | `neverc_string_url_encode` | RFC 3986 | 百分號編碼（unreserved set 不變，`%XX` 大寫） |
| `s.url_decode()` | `neverc_string_url_decode` | RFC 3986 | 解碼 |
| `s.form_encode()` | `neverc_string_form_encode` | `application/x-www-form-urlencoded` | 空格 → `+`，`+` → `%2B` |
| `s.form_decode()` | `neverc_string_form_decode` | 同上 | `+` → 空格 |

> `url_encode` 和 `form_encode` 不可互換——透過 `form_encode` 編碼的 `+` 號用 `url_decode` 解碼會保留為字面 `+`。

---

## Web 編解碼

HTML / JSON / CSV 字面量級別的轉義/反轉義。每對保持嚴格往返（round-trip）。

| Dot-call | Runtime 函式 | 標準 | 說明 |
|----------|-------------|------|------|
| `s.html_escape()` | `neverc_string_html_escape` | OWASP Rule #1/#2 | 5 特殊字元實體轉義（`& < > " '`） |
| `s.html_unescape()` | `neverc_string_html_unescape` | HTML5 | 實體解碼（含數字實體 `&#xNNNN;`） |
| `s.json_escape()` | `neverc_string_json_escape` | RFC 8259 §7 | JSON 字串字面量轉義 |
| `s.json_unescape()` | `neverc_string_json_unescape` | RFC 8259 §7 | 解碼（含 `\uXXXX` 代理對） |
| `s.csv_escape()` | `neverc_string_csv_escape` | RFC 4180 §2 | CSV 欄位轉義 |
| `s.csv_unescape()` | `neverc_string_csv_unescape` | RFC 4180 §2 | CSV 欄位解碼 |

```c
string html = "<div class=\"test\">Hello & 世界</div>";
string safe = html.html_escape();
// "&lt;div class=&quot;test&quot;&gt;Hello &amp; 世界&lt;/div&gt;"

string json_val = "line1\nline2\ttab";
string escaped = json_val.json_escape();
// "line1\\nline2\\ttab"
```

---

## 編譯時字串加密

`.encrypt()` 為字串字面量提供編譯時 XOR 加密。明文永遠不會出現在二進位檔案中——編譯時加密，執行時透過 `always_inline` 函式解密，靜態分析工具（如 `strings`）無法提取原始內容。

### 用法

```c
string secret = "API_KEY_12345".encrypt();
printf("%s\n", secret.c_str());  // 執行時輸出 "API_KEY_12345"

// 但 `strings ./binary` 找不到 "API_KEY_12345"
```

### 工作原理

1. **編譯時**：Sema 攔截字面量上的 `.encrypt()` 呼叫，對每個位元組進行 XOR 加密，將密文儲存在 `.rodata` 段
2. **執行時（通用路徑）**：`always_inline` 函式 `__neverc_string_decrypt_literal` 將密文 XOR 解密到新分配的 owned 緩衝區。解密後得到的是普通的 owned `string`。
3. **執行時（比較/搜尋快速路徑）**：當加密字面量直接出現在比較或搜尋表達式中時，Sema 會改寫為零分配的 `__neverc_string_decrypt_*` 變體，逐位元組解密並比較，完全不做堆積分配（參見下方「零分配解密比較」）

### 金鑰生成

- **預設**：每次編譯從 `std::time()` 衍生基礎金鑰，再與 per-literal 計數器混合，為每個字串字面量生成唯一金鑰。每次建置產生不同的密文。
- **CLI 覆蓋**：使用 `-fstring-encrypt-key=0xDEADBEEF` 固定基礎金鑰（適用於可重現建置或測試）。

### 支援所有字面量類型

`.encrypt()` 自動支援所有字串字面量類型——編譯器在加密前將 wide/UTF-16/UTF-32 字面量折疊為 UTF-8：

```c
string a = "hello".encrypt();                  // ordinary
string b = u8"héllo".encrypt();                // UTF-8
string c = L"中文".encrypt();                   // wide
string d = u"\u4E2D\u6587".encrypt();          // UTF-16
string e = U"\U0001F389party".encrypt();       // UTF-32
string f = R"(line1\nline2)".encrypt();        // raw
```

### 零分配解密比較

當加密字面量直接用於比較或搜尋表達式時，編譯器自動繞過堆積分配。不再將整個字串解密到記憶體中再比較，而是逐位元組 XOR 解密並比較——明文永遠不會完整地出現在記憶體中。

**優化的表達式**（當一個運算元是 `.encrypt()` 時全部零分配）：

| 類別 | 表達式 |
|------|--------|
| 等值 | `s == "key".encrypt()`、`s != "key".encrypt()` |
| 關係 | `s < "key".encrypt()`、`s > "key".encrypt()`、`<=`、`>=` |
| 前綴/後綴 | `s.starts_with("prefix".encrypt())`、`s.ends_with("suffix".encrypt())` |
| 包含 | `s.contains("needle".encrypt())` |
| 大小寫不敏感 | `s.eq_ic(...)`、`s.starts_with_ic(...)`、`s.ends_with_ic(...)`、`s.contains_ic(...)` |
| 搜尋 | `s.find("needle".encrypt())`、`s.rfind(...)`、帶位置參數的變體 |
| 計數 | `s.count("pattern".encrypt())` |
| 大小寫不敏感搜尋 | `s.find_ic("needle".encrypt())` |

### 限制

- `.encrypt()` **只能**用於字串字面量。在變數上呼叫會產生編譯錯誤：

```c
string s = "hello";
string e = s.encrypt();  // 錯誤：.encrypt() can only be applied to string literals
```

- `.encrypt()` **不接受參數**：

```c
string e = "hello".encrypt(42);  // 錯誤：.encrypt() takes no arguments
```

- 拒絕雙重加密：

```c
string e = "hello".encrypt().encrypt();  // 錯誤：.encrypt() can only be applied to string literals
```

### 自訂加密演算法

加密和解密操作由兩個巨集控制：

- `NEVERC_STRING_ENCRYPT_BYTE(byte, key, idx)` — 編譯時：明文→密文
- `NEVERC_STRING_DECRYPT_BYTE(byte, key, idx)` — 執行時：密文→明文

`ENCRYPT_BYTE` 預設為 XOR。`DECRYPT_BYTE` 預設使用**無 XOR 指令的算術分解**——透過 `(a + b) - (a & b) - (b & a)` 計算 `a ^ b`，使用 `volatile` 中間變數阻止 LLVM 最佳化回 `xor` 指令。後續可透過 MBA（Mixed Boolean-Arithmetic）混淆 pass 進一步加強。

如需使用非 XOR 演算法，**同時**定義兩個巨集，且它們**必須互為數學逆操作**：

```c
#define NEVERC_STRING_ENCRYPT_BYTE(byte, key, idx) \
  ((char)((unsigned char)(byte) + (unsigned char)((key) >> (8 * ((idx) % sizeof(size_t))))))
#define NEVERC_STRING_DECRYPT_BYTE(byte, key, idx) \
  ((char)((unsigned char)(byte) - (unsigned char)((key) >> (8 * ((idx) % sizeof(size_t))))))
```

### 編譯器旗標

| 旗標 | 說明 |
|------|------|
| `-fstring-encrypt-key=<hex>` | 覆蓋 XOR 基礎金鑰（如 `-fstring-encrypt-key=0xDEADBEEF`） |

### 可設定旋鈕

| 巨集 | 預設值 | 說明 |
|------|--------|------|
| `NEVERC_STRING_DECRYPT_BYTE(byte, key, idx)` | 使用旋轉金鑰位元組的 XOR | 逐位元組解密操作 |

### 結構體與陣列中的加密字串

`.encrypt()` 可用於聚合初始化；擁有的 `string` 成員在作用域結束時自動釋放（見 [複合類型清理](#複合類型清理)）。`string[]`、`struct { string; }`、二維陣列與巢狀組合均支援，且零分配比較路徑同樣有效。

### Shellcode 模式相容

字串加密在所有編譯模式下均可使用，包括 shellcode（`-fshellcode`）。在 shellcode 模式下，加密字串的解密使用 shellcode 本地 arena 分配器。用戶態和核心態 shellcode 上下文（`-mshellcode-context=kernel`）均受支援。

---

## 鏈式呼叫

所有返回 `string` 的方法都支援鏈式呼叫。中間值自動釋放，不會洩漏：

```c
string result = input.to_upper().trim().replace_all(" ", "_");
string encoded = payload.to_base64_url();
string cleaned = raw.url_decode().from_base64().trim();
```

---

## 編譯模式

### 三種工作模式

| 模式 | 描述 | string 函式體來源 | 符號可見性 |
|------|------|------------------|-----------|
| **Hosted（預設）** | 普通可執行檔案 | 預編譯 LLVM bitcode 合併 | LTO 下 0 個符號 |
| **Shellcode** | 位置無關的平坦二進位制 | 完整原始碼 prelude 注入 + arena 改寫 | 0 個符號（AlwaysInliner） |
| **LTO** | 連結時最佳化 | 同 Hosted，LTO DCE 進一步精簡 | 0 個符號 |

最終輸出的二進位制檔案中**不會暴露任何 `neverc_string_*` 符號**。

### 編譯器標誌

| 標誌 | 說明 |
|------|------|
| `-fbuiltin-string` | 啟用 builtin string 型別（預設關閉） |
| `-fshellcode` | 啟用 shellcode 模式（自動啟用 builtin string） |
| `-DNEVERC_STRING_ALLOC=xxx` | 自定義 allocator（觸發完整原始碼 prelude） |
| `-DNEVERC_STRING_FREE=xxx` | 自定義 free 函式 |

### 可配置旋鈕

| 宏 | 預設值 | 說明 |
|----|--------|------|
| `NEVERC_STRING_ALLOC` | `__builtin_malloc` | Owned 緩衝區分配器 |
| `NEVERC_STRING_FREE` | `__builtin_free` | 對應的釋放函式 |
| `NEVERC_STRING_NPOS` | `(size_t)-1` | "未找到"哨兵值 |
| `NEVERC_STRING_MAX_LEN` | `(size_t)-2` | 負載長度上限 |
| `NEVERC_STRING_INT_BUF` | `24` | `from_int` / `from_uint` 的棧緩衝區大小 |
| `NEVERC_STRING_USER_ARENA_SIZE` | `64 KB` | Shellcode 使用者態 arena 大小 |
| `NEVERC_STRING_KERNEL_ARENA_SIZE` | `4 KB` | Shellcode 核心態 arena 大小 |

---

## Hosted 模式：預編譯 Bitcode 架構

編譯器構建時將 string runtime 函式預編譯為 LLVM bitcode，嵌入編譯器二進位制。編譯使用者程式碼時只注入薄 header（struct + macros + extern 宣告），在 CodeGen 之後透過 `llvm::Linker::linkModules()` 合併 bitcode。

### 構建時流程

```
prelude .inc (11 片段)
       │
       ├──→ gen_thin_header.py ──→ extern 宣告
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
                                   (嵌入編譯器二進位制)
```

### 編譯使用者程式碼時

```
使用者.c
  │
  ▼ FrontendAction: 注入薄 header
  │    struct string { data, len, cap }
  │    macros + inline 函式
  │    extern 宣告
  │
  ▼ Lexer → Parser → Sema → CodeGen
  │
  ▼ StringRuntimeLinkerPass (PipelineStartEP)
  │    1. 解析嵌入 bitcode → Module
  │    2. stamp kRuntimeFnAttr（零名字匹配）
  │    3. 計算使用者程式碼引用的函式 + 呼叫圖 BFS 傳遞閉包
  │    4. Pre-merge 裁剪未引用函式
  │    5. llvm::Linker::linkModules() 合併
  │    6. InternalizePass → 所有符號變 internal
  │    7. Mark-and-sweep DCE → 清除不可達函式
  │    8. llvm.used 清理
  │
  ▼ 最佳化管線（含 AlwaysInliner / GlobalDCE）
  │
  ▼ 輸出 .o / bitcode
```

### `kRuntimeFnAttr` — 跨層函式識別

`StringRuntimeLinkerPass` 是整個程式碼庫中唯一的"名字→屬性"轉換點。之後所有下游 IR pass 透過 `F.hasFnAttribute("neverc-string-runtime")` 識別 runtime 函式（一個位檢查），不再依賴字串字首匹配。

**好處：** 混淆 pass 可以安全地重新命名 runtime 函式而不破壞識別鏈。

### Bootstrap 流程

bitcode 的生成採用兩階段 bootstrap：

```bash
ninja neverc                        # stage 1（空 bitcode placeholder）
ninja neverc-bootstrap-string-bc    # 用 neverc 自身編譯 string runtime → .bc
ninja neverc                        # stage 2（嵌入真正的 bitcode）
```

### Bitcode 回退條件

| 條件 | 原因 |
|------|------|
| `-fshellcode` | StringRuntimePass 需要原始碼級別的函式體 |
| `-DNEVERC_STRING_ALLOC=xxx` | 自定義 allocator 被烘焙在 bitcode 中，必須重新編譯 |
| 空的嵌入 bitcode | 首次構建（bootstrap stage 1）沒有 bitcode |

---

## Shellcode 模式

不使用 bitcode 合併，而是注入完整原始碼 prelude：

```
FrontendAction: 注入完整 prelude
    │
    ▼ Lexer → Parser → Sema → CodeGen
    │
    ▼ StringRuntimeLinkerPass ← legacy 分支：stamp kRuntimeFnAttr，跳過 merge
    ▼ ZeroRelocPass
    ▼ IndirectBrPass
    ▼ MemIntrinPass
    ▼ StringRuntimePass ← 改寫 malloc → stack arena
    ▼ AlwaysInliner ← 內聯所有函式
    ▼ Data2TextPass
    │
    ▼ shellcode.bin
```

`StringRuntimePass` 將 `__builtin_malloc`/`__builtin_free` 改寫為 stack arena allocator：
- 所有記憶體分配在棧上完成，不連結任何外部庫
- arena 使用 `{size, next, self, tag}` per-allocation header 進行驗證
- 使用者態 arena 預設 64 KB，核心態預設 4 KB
- `AlwaysInliner` 最終將所有函式內聯，shellcode 中零獨立符號

---

## 方法分發機制

Sema 透過 `buildNeverCStringRuntimeCall()` 將 dot-call 語法重寫為 C 函式呼叫：

```c
// 使用者寫的
string result = s.find("hello");

// Sema 重寫為
string result = neverc_string_find(s, __neverc_string_make_view("hello", 5));
```

### 4 層分發優先順序

當使用者寫 `s.method(args...)` 時，Sema 按以下優先順序查詢目標函式：

1. **Char 過載**（`BuiltinStringMethodCharOverloads.def`）：首引數為整數/char 型別時優先匹配
2. **Arity 過載**（`BuiltinStringMethodOverloads.def`）：按引數數量匹配特化版本
3. **預設引數補全**（`BuiltinStringMethodDefaults.def`）：追加預設值（如 `s.substr(pos)` → `s.substr(pos, NPOS)`）
4. **預設對映**（`BuiltinStringMethodNames.def`）：通用的 method → runtime function 對映

### 接收者型別

大多數方法透過 `string` 值傳遞接收者。少數方法需要指標語義：

| 方法 | 接收者 | 原因 |
|------|--------|------|
| `s.assign(t)` | `string *` | 需要原地修改目標 |
| `s.swap(t)` | `string *, string *` | 兩側都需要可定址 |
| `s.capacity()` | `const string *` | 避免 retain 複製抹平真實容量 |

---

## 符號可見性

| 編譯模式 | 最終二進位制中的 `neverc_string_*` 符號 |
|---------|-------------------------------------|
| LTO (預設) | **0 個** — internalize + LTO DCE 完全消除 |
| 非 LTO -O2 | 少量 `t`（internal）— GlobalDCE 消除未使用函式 |
| 非 LTO -O0 | 全部 `t`（internal）— DCE 未執行但符號仍 internal |
| Shellcode | **0 個** — AlwaysInliner 內聯所有函式 |

---

## 檔案結構

```
neverc/
├── include/neverc/Foundation/Builtin/
│   ├── BuiltinString.h                         # API 宣告
│   ├── BuiltinStringNames.h                    # 編譯時函式名常量 + 字首不變數 static_assert
│   ├── BuiltinStringRoster.def                 # 所有函式的 X-macro 登錄檔（單一事實來源）
│   ├── BuiltinStringRuntimeNames.def           # Roster 的 IsPublic=1 過濾檢視
│   ├── BuiltinStringMethodNames.def            # dot-call 方法名 → runtime 函式對映
│   ├── BuiltinStringMethodOverloads.def        # 按 arity 的方法過載表
│   ├── BuiltinStringMethodCharOverloads.def    # char 型別引數的方法過載
│   ├── BuiltinStringMethodDefaults.def         # 預設引數補全規則
│   ├── BuiltinStringMethodDefaultArgKinds.def  # 預設引數型別列舉
│   ├── BuiltinStringMethodReceiverKinds.def    # 指標接收者的方法列表
│   ├── BuiltinStringMethodReceiverKindsRoster.def  # 接收者型別列舉
│   ├── BuiltinStringBorrowedViewHelpers.def    # 借用檢視 helper 列表（CStr/Data）
│   ├── BuiltinStringLValueDirectHelpers.def    # Lvalue-direct helper 白名單
│   ├── BuiltinStringWptrProducers.def          # 寬指標 producer 列表
│   ├── BuiltinStringPreludeFragments.def       # Prelude 片段排序
│   └── BuiltinStringPrelude/                   # 11 個 .inc 函式實現片段
│       ├── Type.inc                            # struct 定義 + 基礎操作
│       ├── Allocation.inc                      # 記憶體分配/釋放/賦值
│       ├── Accessors.inc                       # c_str, data, at, front, back
│       ├── Capacity.inc                        # reserve, shrink_to_fit, capacity
│       ├── Compare.inc                         # eq, compare + case-insensitive 系列
│       ├── Search.inc                          # find, rfind, contains, starts/ends_with + 字符集搜尋 + _ic 系列
│       ├── Mutation.inc                        # append, insert, erase, replace, pad, clear, swap, resize
│       ├── Utility.inc                         # clone, substr, trim, repeat, reverse, hash, count, split/join, 數值轉換
│       ├── Encoding.inc                        # UTF-8/16/32, Latin-1, Base64/Base32/Hex, URL/Form 編碼
│       ├── WebCodec.inc                        # HTML/JSON/CSV escape/unescape
│       └── Format.inc                          # printf 風格 format
│
├── include/neverc/Shellcode/IR/
│   ├── StringRuntimeABI.h                      # 跨層 ABI：kRuntimeFnAttr、arena 常量
│   └── StringRuntimePass.h                     # Shellcode arena 改寫 pass 宣告
│
├── lib/Foundation/Builtin/
│   ├── BuiltinString.cpp                       # 核心實現（方法分發表 + 薄 header + bitcode API）
│   ├── BuiltinStringThinHeaderPrologue.inc     # 薄 header 固定部分
│   ├── gen_thin_header.py                      # 從 prelude 提取 extern 宣告
│   ├── gen_string_runtime.py                   # 生成獨立編譯單元
│   └── bin2c.py                                # .bc → C 標頭檔案嵌入
│
├── lib/Analyze/Core/
│   └── Sema.cpp                                # NeverCStringFnKinds DenseMap
│
├── lib/Emit/Backend/
│   ├── StringRuntimeLinker.h                   # bitcode 合併 pass 宣告
│   ├── StringRuntimeLinker.cpp                 # bitcode 合併 + kRuntimeFnAttr stamp
│   └── BackendUtil.cpp                         # 在所有模式註冊 linker pass
│
└── lib/Shellcode/IR/
    └── StringRuntimePass.cpp                   # Shellcode arena 改寫 pass
```

### 新增新 runtime 函式的步驟

1. 在 `BuiltinStringRoster.def` 新增一行 `NEVERC_BUILTIN_STRING_FN(NameId, "neverc_string_xxx", 1)`
2. 在對應的 `BuiltinStringPrelude/*.inc` 中實現函式體
3. （可選）在 `BuiltinStringMethodNames.def` 新增 dot-call 對映
4. （可選）在 `BuiltinStringMethodOverloads.def` 新增 arity 過載
5. （可選）在 `BuiltinStringMethodCharOverloads.def` 新增 char 過載

不需要修改任何其他宣告點。
