**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 文档](../README.zh-CN.md)

# NeverC 内置 `string` 类型

## 概述

NeverC 提供了一个面向 C 语言的内置 `string` 值类型。它融合了 C++ `std::string` 的接口设计和 Qt `QString` 的 Unicode 能力，让 C 代码也能写出安全、高效的字符串操作。

**启用方式：** 编译时传递 `-fbuiltin-string`（默认关闭）。`-fshellcode` 模式自动启用。

```bash
neverc -fbuiltin-string main.c -o main
```

```c
string greeting = "你好世界";
string msg = greeting + ", NeverC!";
printf("字节长度: %zu\n", msg.len);
printf("字符数: %zu\n", msg.utf8_count());
printf("内容: %s\n", msg.c_str());
```

### 核心特点

- **值语义**：`string` 是 `{ data, len, cap }` 三字段结构体，按值传递和返回
- **自动内存管理**：编译器通过 `CleanupAttr` 在作用域退出时自动释放 owned string，无需手动 `free`
- **UTF-8 原生**：所有字面量（含 `L"..."`、`u"..."`、`U"..."`）在编译期统一折叠为 UTF-8
- **借用视图**：字面量赋值 `string s = "hello"` 产生零分配视图（`cap == 0`），不拥有内存
- **Dot-call 语法**：`s.find("abc")`、`s.to_upper().trim()` 由 Sema 重写为对应的 C 函数调用
- **命名空间隔离**：所有 runtime 符号使用 `neverc_string_*` 前缀，不会与用户代码中的 `string_eq`、`STRING_NPOS` 等标识符冲突

---

## 核心概念

### 类型定义

```c
typedef struct __neverc_string {
  const char *data;  // 指向 UTF-8 字节的指针
  __SIZE_TYPE__ len; // 字节长度
  __SIZE_TYPE__ cap; // 容量（0 = 借用视图，>0 = 拥有内存）
} string;
```

### 所有权模型：Owned vs Borrowed

`cap` 字段同时承担"容量"和"所有权标记"双重角色：

| 状态 | `cap` | 含义 | 示例 |
|------|-------|------|------|
| **Borrowed（借用视图）** | `== 0` | 不拥有内存，指向外部存储（字面量、栈缓冲区等） | `string s = "hello"` |
| **Owned（拥有内存）** | `> 0` | 拥有通过 `NEVERC_STRING_ALLOC` 分配的缓冲区 | `string s = a + b` |

```c
string a = "hello";           // Borrowed: data → 字面量存储, cap == 0
string b = a + ", world!";    // Owned: 分配新缓冲区, cap > 0
string c = a.to_upper();      // Owned: 变换产生新缓冲区
string d = neverc_string_view(buf, len); // Borrowed: 包装外部缓冲区
```

### 自动内存管理

编译器自动处理 owned string 的生命周期：

```c
void example() {
    string a = "hello";               // borrowed, 无需释放
    string b = a + ", world!";        // owned, 编译器自动附加 CleanupAttr
    string c = b.to_upper().trim();   // 链式调用: 中间值自动释放
    printf("%s\n", c.c_str());
}   // 作用域退出: b, c 自动释放; a 无需释放 (borrowed)
```

宽字符指针（`w_str()`、`to_utf16_owned()`、`to_utf32_owned()` 返回的指针）同样自动释放——Sema 检测到这些初始化时自动附加 `__neverc_wptr_cleanup`。

### 安全性保证

- **Forged handle 防护**：`len > 0 && data == NULL` 的伪造句柄会被 `__neverc_string_invalid` 拦截，短路返回空字符串
- **Oversized handle 防护**：`len > NEVERC_STRING_MAX_LEN` 的超大句柄同样被拦截
- **临时值安全**：`.c_str()` 和 `.data()` 对临时值（prvalue）会触发编译错误 `err_neverc_string_cstr_temporary`，防止悬空指针
- **零泄漏测试**：macOS 上所有 string 测试在 `leaks --atExit` 门控下运行，断言 "0 leaks"

### 常量

| 常量 | 值 | 说明 |
|------|-----|------|
| `NEVERC_STRING_NPOS` | `(size_t)-1` | 搜索未命中时的返回值，等价于 `std::string::npos` |
| `NEVERC_STRING_MAX_LEN` | `(size_t)-2` | 最大有效负载长度上限 |

---

## 运算符

Sema 将运算符重写为对应的 runtime 调用：

| 运算符 | 等价调用 | 返回类型 |
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

## 完整 API 参考

### 大小与访问

| Dot-call | 别名 | Runtime 函数 | 签名 | 返回值 |
|----------|------|-------------|------|--------|
| `s.len()` | `s.size()` `s.length()` | `neverc_string_len` | `(string s) → size_t` | 字节长度 |
| `s.empty()` | — | `neverc_string_empty` | `(string s) → int` | `s.len == 0` |
| `s.c_str()` | — | `neverc_string_cstr` | `(string s) → const char *` | NUL 终止的字符串指针（借用） |
| `s.data()` | — | `neverc_string_data` | `(string s) → void *` | 原始字节指针（借用，无 const） |
| `s.at(i)` | `s[i]` | `neverc_string_at` | `(string s, size_t i) → char` | 越界返回 `'\0'` |
| `s.front()` | — | `neverc_string_front` | `(string s) → char` | 首字节 |
| `s.back()` | — | `neverc_string_back` | `(string s) → char` | 末字节 |

> **注意**：`.c_str()` 和 `.data()` 返回的是指向 string 内部缓冲区的借用指针，禁止对临时值使用（编译器会报错）。

### 容量

| Dot-call | Runtime 函数 | 签名 | 说明 |
|----------|-------------|------|------|
| `s.reserve(n)` | `neverc_string_reserve` | `(string s, size_t n) → string` | 预留至少 `n` 字节容量 |
| `s.shrink_to_fit()` | `neverc_string_shrink_to_fit` | `(string s) → string` | 缩减多余容量 |
| `s.capacity()` | `neverc_string_capacity` | `(const string *s) → size_t` | 当前容量（通过 `&s` 传递） |
| `s.max_size()` | `neverc_string_max_size` | `(string s) → size_t` | 返回 `NEVERC_STRING_MAX_LEN` |

### 相等与比较

| Dot-call | Runtime 函数 | 签名 | 返回值 |
|----------|-------------|------|--------|
| `s.eq(t)` | `neverc_string_eq` | `(string a, string b) → int` | `1`=相等, `0`=不等 |
| `s.compare(t)` | `neverc_string_compare` | `(string a, string b) → int` | `-1` / `0` / `+1` |
| `s.compare(pos, n, t)` | `neverc_string_compare_substr` | `(string a, size_t pos, size_t n, string b) → int` | 子串比较 |
| `s.compare(p1, n1, t, p2, n2)` | `neverc_string_compare_substr2` | `(string a, size_t p1, size_t n1, string b, size_t p2, size_t n2) → int` | 双子串比较 |

#### ASCII 大小写不敏感

折叠规则：`A-Z → a-z`，其余字节（含 `>= 0x80` 的 UTF-8 续字节）不变。适用于 HTTP 头匹配、文件扩展名比较等场景。

| Dot-call | Runtime 函数 | 说明 |
|----------|-------------|------|
| `s.eq_ic(t)` | `neverc_string_eq_ic` | 不区分大小写相等 |
| `s.compare_ic(t)` | `neverc_string_compare_ic` | 不区分大小写 3-way 比较 |
| `s.find_ic(t)` | `neverc_string_find_ic` | 不区分大小写查找 |
| `s.contains_ic(t)` | `neverc_string_contains_ic` | 不区分大小写包含 |
| `s.starts_with_ic(t)` | `neverc_string_starts_with_ic` | 不区分大小写前缀匹配 |
| `s.ends_with_ic(t)` | `neverc_string_ends_with_ic` | 不区分大小写后缀匹配 |

```c
string ct = "Content-Type";
if (ct.eq_ic("content-type")) { /* 匹配 */ }

string path = "photo.PNG";
if (path.ends_with_ic(".png")) { /* 匹配 */ }
```

### 搜索

所有搜索函数支持 string 和 char 两种重载——Sema 根据首参数类型自动分派。

| Dot-call | char 重载 | Runtime 函数 | 返回值 |
|----------|-----------|-------------|--------|
| `s.find(t)` | `s.find(ch)` | `neverc_string_find` / `find_char` | 首次出现的字节偏移，未找到返回 `NEVERC_STRING_NPOS` |
| `s.find(t, pos)` | `s.find(ch, pos)` | `neverc_string_find_from` / `find_char_from` | 从 `pos` 开始查找 |
| `s.rfind(t)` | `s.rfind(ch)` | `neverc_string_rfind` / `rfind_char` | 最后一次出现的偏移 |
| `s.rfind(t, pos)` | `s.rfind(ch, pos)` | `neverc_string_rfind_to` / `rfind_char_to` | 在 `[0, pos]` 范围内反向查找 |
| `s.contains(t)` | `s.contains(ch)` | `neverc_string_contains` / `contains_char` | `1`=包含, `0`=不包含 |
| `s.starts_with(t)` | `s.starts_with(ch)` | `neverc_string_starts_with` / `starts_with_char` | 前缀匹配 |
| `s.ends_with(t)` | `s.ends_with(ch)` | `neverc_string_ends_with` / `ends_with_char` | 后缀匹配 |
| `s.count(t)` | `s.count(ch)` | `neverc_string_count` / `count_char` | 出现次数 |

#### 字符集搜索

内部使用 256-bit bitmap 实现 O(1) 字符集成员检测，总体复杂度 O(n+m)。

| Dot-call | 带位置重载 | Runtime 函数 | 说明 |
|----------|-----------|-------------|------|
| `s.find_first_of(chars)` | `s.find_first_of(chars, pos)` | `neverc_string_find_first_of` / `_from` | 首次出现 `chars` 中任一字符 |
| `s.find_last_of(chars)` | `s.find_last_of(chars, pos)` | `neverc_string_find_last_of` / `_to` | 最后出现 |
| `s.find_first_not_of(chars)` | `s.find_first_not_of(chars, pos)` | `neverc_string_find_first_not_of` / `_from` | 首次出现 **不在** `chars` 中的字符 |
| `s.find_last_not_of(chars)` | `s.find_last_not_of(chars, pos)` | `neverc_string_find_last_not_of` / `_to` | 最后出现不在 `chars` 中的字符 |

char 重载同样可用：`s.find_first_of('x')` 等价于 `s.find('x')`，`s.find_first_not_of(' ')` 是经典的"跳过前导空格"用法。

### 子串与复制

| Dot-call | Runtime 函数 | 签名 | 说明 |
|----------|-------------|------|------|
| `s.substr(pos)` | `neverc_string_substr` | `(string s, size_t pos, size_t len) → string` | 1 参数时 `len` 默认为 `NPOS`（到末尾） |
| `s.substr(pos, len)` | 同上 | 同上 | 提取 `[pos, pos+len)` 子串 |
| `s.copy(out, n)` | `neverc_string_copy` | `(string s, char *out, size_t n) → size_t` | 复制到外部缓冲区，返回实际复制字节数 |
| `s.copy(out, n, pos)` | `neverc_string_copy_from` | `(string s, char *out, size_t n, size_t pos) → size_t` | 从 `pos` 开始复制 |

### 变更操作

变更操作遵循值类型的"消费输入 + 返回新值"模式：函数消费传入的 string（释放其 owned 缓冲区），返回一个新的 owned string。编译器自动将 `s.append(x)` 重写为 `s = neverc_string_append(s, x)`，从用户角度看是"原地修改"。

只有 `assign`、`swap`、`capacity` 通过 `string *` 指针接收（见下方"指针接收者方法"）。

#### 消费 + 返回新值

| Dot-call | Runtime 函数 | 签名 | 说明 |
|----------|-------------|------|------|
| `s.append(t)` | `neverc_string_append` | `(string s, string t) → string` | 追加 string |
| `s.append(n, ch)` | `neverc_string_append_char` | `(string s, size_t n, char ch) → string` | 追加 n 个 ch |
| `s.push_back(ch)` | `neverc_string_push_back` | `(string s, char ch) → string` | 追加单字符 |
| `s.pop_back()` | `neverc_string_pop_back` | `(string s) → string` | 移除末字符 |
| `s.insert(pos, t)` | `neverc_string_insert` | `(string s, size_t pos, string t) → string` | 插入 string |
| `s.insert(pos, n, ch)` | `neverc_string_insert_char` | `(string s, size_t pos, size_t n, char ch) → string` | 插入 n 个 ch |
| `s.erase(pos)` | `neverc_string_erase` | `(string s, size_t pos, size_t count) → string` | 1 参数时 `count` 默认为 `NPOS` |
| `s.erase(pos, count)` | 同上 | 同上 | 删除 `[pos, pos+count)` |
| `s.replace(pos, n, t)` | `neverc_string_replace` | `(string s, size_t pos, size_t n, string t) → string` | 替换子串 |
| `s.replace(pos, n, m, ch)` | `neverc_string_replace_char` | `(string s, size_t pos, size_t n, size_t m, char ch) → string` | 用 m 个 ch 替换 |
| `s.replace_all(from, to)` | `neverc_string_replace_all` | `(string s, string from, string to) → string` | 全局替换（NeverC 扩展） |
| `s.clear()` | `neverc_string_clear` | `(string s) → string` | 清空内容（返回空字符串） |
| `s.resize(n)` | `neverc_string_resize` | `(string s, size_t n, char ch) → string` | 1 参数时 `ch` 默认为 `'\0'` |
| `s.resize(n, ch)` | 同上 | 同上 | 调整大小，扩展部分用 `ch` 填充 |
| `s.pad_left(w, ch)` | `neverc_string_pad_left` | `(string s, size_t width, char ch) → string` | 左填充 |
| `s.pad_right(w, ch)` | `neverc_string_pad_right` | `(string s, size_t width, char ch) → string` | 右填充 |
| `s.clone()` | `neverc_string_clone` | `(string s) → string` | 深拷贝（owned 直接移动，borrowed 分配新缓冲区） |
| `s.to_lower()` | `neverc_string_to_lower` | `(string s) → string` | ASCII 转小写 |
| `s.to_upper()` | `neverc_string_to_upper` | `(string s) → string` | ASCII 转大写 |
| `s.trim()` | `neverc_string_trim` | `(string s) → string` | 去除两端空白 |
| `s.ltrim()` | `neverc_string_ltrim` | `(string s) → string` | 去除左侧空白 |
| `s.rtrim()` | `neverc_string_rtrim` | `(string s) → string` | 去除右侧空白 |
| `s.repeat(n)` | `neverc_string_repeat` | `(string s, size_t n) → string` | 重复 n 次 |
| `s.reverse()` | `neverc_string_reverse` | `(string s) → string` | 字节级反转 |
| `s.hash()` | `neverc_string_hash` | `(string s) → unsigned long long` | FNV-1a 64-bit hash |

#### 指针接收者方法

这些方法需要直接操作调用者的存储，Sema 将接收者包装为 `&s`：

| Dot-call | Runtime 函数 | 签名 | 说明 |
|----------|-------------|------|------|
| `s.assign(t)` | `neverc_string_assign` | `(string *dst, string src)` | 替换内容（释放旧值 + 安装新值） |
| `s.assign(n, ch)` | `neverc_string_assign_char` | `(string *dst, size_t n, char ch)` | 用 n 个 ch 替换 |
| `s.swap(t)` | `neverc_string_swap` | `(string *a, string *b)` | 交换两个 string 的句柄 |

### Split / Join

| Dot-call | Runtime 函数 | 签名 | 说明 |
|----------|-------------|------|------|
| `s.split_first(sep)` | `neverc_string_split_first` | `(string s, string sep) → string` | 分隔符前的第一段 |
| `s.split_rest(sep)` | `neverc_string_split_rest` | `(string s, string sep) → string` | 第一个分隔符后的其余部分 |
| `s.split_before_last(sep)` | `neverc_string_split_before_last` | `(string s, string sep) → string` | 最后一个分隔符之前 |
| `s.split_after_last(sep)` | `neverc_string_split_after_last` | `(string s, string sep) → string` | 最后一个分隔符之后 |
| `s.split(sep, &items, &n)` | `neverc_string_split` | `(string s, string sep, string **items, size_t *count)` | 完整分割为数组 |
| — | `neverc_string_split_free` | `(string *parts, size_t n)` | 释放 `split` 产生的数组 |
| — | `neverc_string_join` | `(const string *items, size_t n, string sep) → string` | 用分隔符连接数组 |

```c
// 逐段剥离
string path = "/usr/local/bin";
string first = path.split_first("/");   // ""
string rest  = path.split_rest("/");    // "usr/local/bin"

// 路径分解
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

### 数值转换

| 方向 | Dot-call | Runtime 函数 | 签名 |
|------|----------|-------------|------|
| int → string | — | `neverc_string_from_int` | `(ptrdiff_t v) → string` |
| uint → string | — | `neverc_string_from_uint` | `(size_t v) → string` |
| int → string（指定进制） | — | `neverc_string_from_int_base` | `(ptrdiff_t v, int base) → string` |
| uint → string（指定进制） | — | `neverc_string_from_uint_base` | `(size_t v, int base) → string` |
| string → int | `s.to_int()` | `neverc_string_to_int` | `(string s) → ptrdiff_t` |
| string → uint | `s.to_uint()` | `neverc_string_to_uint` | `(string s) → size_t` |

`to_int` / `to_uint` 仅解析十进制数字，遇到非数字字符停止。`from_int_base` / `from_uint_base` 支持 2-36 进制。

```c
string hex = neverc_string_from_int_base(255, 16);  // "ff"
string dec = neverc_string_from_int(-42);            // "-42"

string s = "12345";
ptrdiff_t v = s.to_int();  // 12345
```

### 工厂函数

不通过 dot-call 使用，需要完整的 `neverc_string_*` 前缀拼写：

| Runtime 函数 | 签名 | 说明 |
|-------------|------|------|
| `neverc_string_from_cstr(p)` | `(const char *p) → string` | 从 C 字符串创建 owned string |
| `neverc_string_from_char(ch)` | `(char ch) → string` | 从单字符创建 |
| `neverc_string_from_utf32_char(cp)` | `(uint32_t cp) → string` | 从 Unicode codepoint 创建 UTF-8 |
| `neverc_string_from_utf16(d, n)` | `(const uint16_t *d, size_t n) → string` | 从 UTF-16 缓冲区创建 |
| `neverc_string_from_utf32(d, n)` | `(const uint32_t *d, size_t n) → string` | 从 UTF-32 缓冲区创建 |
| `neverc_string_from_latin1(d, n)` | `(const char *d, size_t n) → string` | 从 Latin-1 (ISO-8859-1) 创建 |
| `neverc_string_view(p, n)` | `(const char *p, size_t n) → string` | 创建借用视图（不拷贝） |
| `neverc_string_clone(s)` | `(string s) → string` | 深拷贝 |
| `neverc_string_free(s)` | `(string s)` | 释放 owned string（borrowed 为 no-op） |
| `neverc_string_array_free(items, n)` | `(string *items, size_t n)` | 释放 string 数组的所有元素 |

---

## 格式化

### `s.format(...)` — printf 风格格式化

`neverc_string_format` 提供无 libc 依赖的 printf 子集：

```c
string name = "世界";
string msg = "你好 %S, 数字=%d".format(name, 42);
// msg = "你好 世界, 数字=42"
```

#### 支持的格式说明符

| 说明符 | 类型 | 说明 |
|--------|------|------|
| `%d` / `%i` | `int` | 有符号十进制 |
| `%u` | `unsigned int` | 无符号十进制 |
| `%ld` / `%lu` | `long` / `unsigned long` | 长整型 |
| `%lld` / `%llu` | `long long` / `unsigned long long` | 长长整型 |
| `%x` / `%lx` / `%llx` | 对应无符号类型 | 小写十六进制 |
| `%s` | `const char *` | C 字符串（NUL 终止；NULL 输出 `(null)`） |
| `%S` | `string` | **NeverC `string` 值**（按值传递，输出后释放） |
| `%c` | `int` | 单字节字符 |
| `%p` | `void *` | 指针（小写 hex 带 `0x` 前缀） |
| `%%` | — | 字面 `%` |

**`%s` vs `%S` 的区别：**

```c
string s = "NeverC";
const char *c = "world";
string result = "hello %s, %S!".format(c, s);
// %s 接收 const char *, %S 接收 string 值
```

### 与标准 `printf` 配合

NeverC `string` 不能直接传给 C 标准库的 `printf`。使用 `.c_str()` 转换：

```c
string s = "你好世界";
printf("内容: %s\n", s.c_str());
printf("长度: %zu\n", s.len);
```

---

## UTF-8 / Unicode

内部始终为 UTF-8 编码。字节级别的操作（`s.len`、`s.at(i)`、`s.find(...)` 等）以**字节**为单位，codepoint 级别的操作使用 `utf8_*` 系列：

### Codepoint 操作

| Dot-call | 别名 | Runtime 函数 | 返回值 |
|----------|------|-------------|--------|
| `s.utf8_count()` | `s.utf8_size()` `s.utf8_length()` | `neverc_string_utf8_count` | codepoint 数量 |
| `s.utf8_valid()` | `s.is_utf8()` | `neverc_string_utf8_valid` | `1`=有效 UTF-8 |
| `s.utf8_at(i)` | — | `neverc_string_utf8_at` | 第 i 个 codepoint（`uint32_t`） |
| `s.utf8_byte_index(i)` | — | `neverc_string_utf8_byte_index` | 第 i 个 codepoint 的字节偏移 |
| `s.utf8_substr(pos)` | — | `neverc_string_utf8_substr` | 从第 pos 个 codepoint 到末尾 |
| `s.utf8_substr(pos, n)` | — | 同上 | 提取 n 个 codepoint |
| `s.is_ascii()` | — | `neverc_string_is_ascii` | `1`=全部字节 < 0x80 |

```c
string s = "你好世界 🎉";
printf("字节: %zu\n", s.len);          // 16
printf("字符: %zu\n", s.utf8_count()); // 5 (4 个汉字 + 1 个 emoji)
printf("第2个: U+%04X\n", s.utf8_at(1)); // U+597D (好)
```

### 编码转换

| Dot-call | Runtime 函数 | 签名 | 说明 |
|----------|-------------|------|------|
| `s.to_utf16(out, cap)` | `neverc_string_to_utf16` | `(string s, uint16_t *out, size_t cap) → size_t` | 写入 caller-owned 缓冲区 |
| `s.to_utf32(out, cap)` | `neverc_string_to_utf32` | `(string s, uint32_t *out, size_t cap) → size_t` | 写入 caller-owned 缓冲区 |
| `s.to_utf16_owned()` | `neverc_string_to_utf16_owned` | `(string s) → uint16_t *` | 返回新分配的 NUL 终止缓冲区 |
| `s.to_utf32_owned()` | `neverc_string_to_utf32_owned` | `(string s) → uint32_t *` | 返回新分配的 NUL 终止缓冲区 |
| `s.w_str()` | `neverc_string_w_str` | `(string s) → wchar_t *` | 平台自适应：Windows=UTF-16, Linux/macOS=UTF-32 |
| `s.to_latin1(out, cap)` | `neverc_string_to_latin1` | `(string s, char *out, size_t cap) → size_t` | 转为 Latin-1 (ISO-8859-1) |

`to_utf16_owned()`、`to_utf32_owned()`、`w_str()` 返回的指针**自动释放**（Sema 附加 `__neverc_wptr_cleanup`）。手动释放使用 `neverc_string_wfree(buf)`。

```c
// UTF-8 直接输出（现代终端原生支持）
string s = "你好世界 🎉";
printf("%s\n", s.c_str());

// Win32 宽字符 API
string title = "NeverC";
__WCHAR_TYPE__ *ws = title.w_str();
MessageBoxW(NULL, ws, L"标题", MB_OK);
// ws 自动释放，无需手动管理
```

---

## 字节编码

所有编码方法消费接收者并返回新的 owned string，支持链式调用。

### Base64 / Base32 / Hex

| Dot-call | Runtime 函数 | 标准 | 说明 |
|----------|-------------|------|------|
| `s.to_base64()` | `neverc_string_to_base64` | RFC 4648 §4 | 标准 Base64 + `=` 填充 |
| `s.from_base64()` | `neverc_string_from_base64` | RFC 4648 §4 | 解码 |
| `s.to_base64_url()` | `neverc_string_to_base64_url` | RFC 4648 §5 | URL 安全 Base64（无填充），用于 JWT/JOSE |
| `s.from_base64_url()` | `neverc_string_from_base64_url` | RFC 4648 §5 | 解码 |
| `s.to_base32()` | `neverc_string_to_base32` | RFC 4648 §6 | 大写 + `=` 填充，用于 TOTP/Google Authenticator |
| `s.from_base32()` | `neverc_string_from_base32` | RFC 4648 §6 | 解码（大小写均接受） |
| `s.to_hex()` | `neverc_string_to_hex` | — | 小写十六进制编码 |
| `s.from_hex()` | `neverc_string_from_hex` | — | 大小写不敏感解码 |

```c
string data = "Hello, NeverC!";
string b64 = data.to_base64();         // "SGVsbG8sIE5ldmVyQyE="
string back = b64.from_base64();       // "Hello, NeverC!"

string jwt_part = data.to_base64_url(); // URL-safe, 无填充
string hex = data.to_hex();             // "48656c6c6f2c204e657665724321"
```

### URL / Form 编码

| Dot-call | Runtime 函数 | 标准 | 说明 |
|----------|-------------|------|------|
| `s.url_encode()` | `neverc_string_url_encode` | RFC 3986 | 百分号编码（unreserved set 不变，`%XX` 大写） |
| `s.url_decode()` | `neverc_string_url_decode` | RFC 3986 | 解码 |
| `s.form_encode()` | `neverc_string_form_encode` | `application/x-www-form-urlencoded` | 空格 → `+`，`+` → `%2B` |
| `s.form_decode()` | `neverc_string_form_decode` | 同上 | `+` → 空格 |

> `url_encode` 和 `form_encode` 不可互换——通过 `form_encode` 编码的 `+` 号用 `url_decode` 解码会保留为字面 `+`。

---

## Web 编解码

HTML / JSON / CSV 字面量级别的转义/反转义。每对保持严格往返（round-trip）。

| Dot-call | Runtime 函数 | 标准 | 说明 |
|----------|-------------|------|------|
| `s.html_escape()` | `neverc_string_html_escape` | OWASP Rule #1/#2 | 5 特殊字符实体转义（`& < > " '`） |
| `s.html_unescape()` | `neverc_string_html_unescape` | HTML5 | 实体解码（含数字实体 `&#xNNNN;`） |
| `s.json_escape()` | `neverc_string_json_escape` | RFC 8259 §7 | JSON 字符串字面量转义 |
| `s.json_unescape()` | `neverc_string_json_unescape` | RFC 8259 §7 | 解码（含 `\uXXXX` 代理对） |
| `s.csv_escape()` | `neverc_string_csv_escape` | RFC 4180 §2 | CSV 字段转义 |
| `s.csv_unescape()` | `neverc_string_csv_unescape` | RFC 4180 §2 | CSV 字段解码 |

```c
string html = "<div class=\"test\">Hello & 世界</div>";
string safe = html.html_escape();
// "&lt;div class=&quot;test&quot;&gt;Hello &amp; 世界&lt;/div&gt;"

string json_val = "line1\nline2\ttab";
string escaped = json_val.json_escape();
// "line1\\nline2\\ttab"
```

---

## 链式调用

所有返回 `string` 的方法都支持链式调用。中间值自动释放，不会泄漏：

```c
string result = input.to_upper().trim().replace_all(" ", "_");
string encoded = payload.to_base64_url();
string cleaned = raw.url_decode().from_base64().trim();
```

---

## 编译模式

### 三种工作模式

| 模式 | 描述 | string 函数体来源 | 符号可见性 |
|------|------|------------------|-----------|
| **Hosted（默认）** | 普通可执行文件 | 预编译 LLVM bitcode 合并 | LTO 下 0 个符号 |
| **Shellcode** | 位置无关的平坦二进制 | 完整源码 prelude 注入 + arena 改写 | 0 个符号（AlwaysInliner） |
| **LTO** | 链接时优化 | 同 Hosted，LTO DCE 进一步精简 | 0 个符号 |

最终输出的二进制文件中**不会暴露任何 `neverc_string_*` 符号**。

### 编译器标志

| 标志 | 说明 |
|------|------|
| `-fbuiltin-string` | 启用 builtin string 类型（默认关闭） |
| `-fshellcode` | 启用 shellcode 模式（自动启用 builtin string） |
| `-DNEVERC_STRING_ALLOC=xxx` | 自定义 allocator（触发完整源码 prelude） |
| `-DNEVERC_STRING_FREE=xxx` | 自定义 free 函数 |

### 可配置旋钮

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `NEVERC_STRING_ALLOC` | `__builtin_malloc` | Owned 缓冲区分配器 |
| `NEVERC_STRING_FREE` | `__builtin_free` | 对应的释放函数 |
| `NEVERC_STRING_NPOS` | `(size_t)-1` | "未找到"哨兵值 |
| `NEVERC_STRING_MAX_LEN` | `(size_t)-2` | 负载长度上限 |
| `NEVERC_STRING_INT_BUF` | `24` | `from_int` / `from_uint` 的栈缓冲区大小 |
| `NEVERC_STRING_USER_ARENA_SIZE` | `64 KB` | Shellcode 用户态 arena 大小 |
| `NEVERC_STRING_KERNEL_ARENA_SIZE` | `4 KB` | Shellcode 内核态 arena 大小 |

---

## Hosted 模式：预编译 Bitcode 架构

编译器构建时将 string runtime 函数预编译为 LLVM bitcode，嵌入编译器二进制。编译用户代码时只注入薄 header（struct + macros + extern 声明），在 CodeGen 之后通过 `llvm::Linker::linkModules()` 合并 bitcode。

### 构建时流程

```
prelude .inc (11 片段)
       │
       ├──→ gen_thin_header.py ──→ extern 声明
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
                                   (嵌入编译器二进制)
```

### 编译用户代码时

```
用户.c
  │
  ▼ FrontendAction: 注入薄 header
  │    struct string { data, len, cap }
  │    macros + inline 函数
  │    extern 声明
  │
  ▼ Lexer → Parser → Sema → CodeGen
  │
  ▼ StringRuntimeLinkerPass (PipelineStartEP)
  │    1. 解析嵌入 bitcode → Module
  │    2. stamp kRuntimeFnAttr（零名字匹配）
  │    3. 计算用户代码引用的函数 + 调用图 BFS 传递闭包
  │    4. Pre-merge 裁剪未引用函数
  │    5. llvm::Linker::linkModules() 合并
  │    6. InternalizePass → 所有符号变 internal
  │    7. Mark-and-sweep DCE → 清除不可达函数
  │    8. llvm.used 清理
  │
  ▼ 优化管线（含 AlwaysInliner / GlobalDCE）
  │
  ▼ 输出 .o / bitcode
```

### `kRuntimeFnAttr` — 跨层函数识别

`StringRuntimeLinkerPass` 是整个代码库中唯一的"名字→属性"转换点。之后所有下游 IR pass 通过 `F.hasFnAttribute("neverc-string-runtime")` 识别 runtime 函数（一个位检查），不再依赖字符串前缀匹配。

**好处：** 混淆 pass 可以安全地重命名 runtime 函数而不破坏识别链。

### Bootstrap 流程

bitcode 的生成采用两阶段 bootstrap：

```bash
ninja neverc                        # stage 1（空 bitcode placeholder）
ninja neverc-bootstrap-string-bc    # 用 neverc 自身编译 string runtime → .bc
ninja neverc                        # stage 2（嵌入真正的 bitcode）
```

### Bitcode 回退条件

| 条件 | 原因 |
|------|------|
| `-fshellcode` | StringRuntimePass 需要源码级别的函数体 |
| `-DNEVERC_STRING_ALLOC=xxx` | 自定义 allocator 被烘焙在 bitcode 中，必须重新编译 |
| 空的嵌入 bitcode | 首次构建（bootstrap stage 1）没有 bitcode |

---

## Shellcode 模式

不使用 bitcode 合并，而是注入完整源码 prelude：

```
FrontendAction: 注入完整 prelude
    │
    ▼ Lexer → Parser → Sema → CodeGen
    │
    ▼ StringRuntimeLinkerPass ← legacy 分支：stamp kRuntimeFnAttr，跳过 merge
    ▼ ZeroRelocPass
    ▼ IndirectBrPass
    ▼ MemIntrinPass
    ▼ StringRuntimePass ← 改写 malloc → stack arena
    ▼ AlwaysInliner ← 内联所有函数
    ▼ Data2TextPass
    │
    ▼ shellcode.bin
```

`StringRuntimePass` 将 `__builtin_malloc`/`__builtin_free` 改写为 stack arena allocator：
- 所有内存分配在栈上完成，不链接任何外部库
- arena 使用 `{size, next, self, tag}` per-allocation header 进行验证
- 用户态 arena 默认 64 KB，内核态默认 4 KB
- `AlwaysInliner` 最终将所有函数内联，shellcode 中零独立符号

---

## 方法分发机制

Sema 通过 `buildNeverCStringRuntimeCall()` 将 dot-call 语法重写为 C 函数调用：

```c
// 用户写的
string result = s.find("hello");

// Sema 重写为
string result = neverc_string_find(s, __neverc_string_make_view("hello", 5));
```

### 4 层分发优先级

当用户写 `s.method(args...)` 时，Sema 按以下优先级查找目标函数：

1. **Char 重载**（`BuiltinStringMethodCharOverloads.def`）：首参数为整数/char 类型时优先匹配
2. **Arity 重载**（`BuiltinStringMethodOverloads.def`）：按参数数量匹配特化版本
3. **默认参数补全**（`BuiltinStringMethodDefaults.def`）：追加默认值（如 `s.substr(pos)` → `s.substr(pos, NPOS)`）
4. **默认映射**（`BuiltinStringMethodNames.def`）：通用的 method → runtime function 映射

### 接收者类型

大多数方法通过 `string` 值传递接收者。少数方法需要指针语义：

| 方法 | 接收者 | 原因 |
|------|--------|------|
| `s.assign(t)` | `string *` | 需要原地修改目标 |
| `s.swap(t)` | `string *, string *` | 两侧都需要可寻址 |
| `s.capacity()` | `const string *` | 避免 retain 拷贝抹平真实容量 |

---

## 符号可见性

| 编译模式 | 最终二进制中的 `neverc_string_*` 符号 |
|---------|-------------------------------------|
| LTO (默认) | **0 个** — internalize + LTO DCE 完全消除 |
| 非 LTO -O2 | 少量 `t`（internal）— GlobalDCE 消除未使用函数 |
| 非 LTO -O0 | 全部 `t`（internal）— DCE 未运行但符号仍 internal |
| Shellcode | **0 个** — AlwaysInliner 内联所有函数 |

---

## 文件结构

```
neverc/
├── include/neverc/Foundation/Builtin/
│   ├── BuiltinString.h                         # API 声明
│   ├── BuiltinStringNames.h                    # 编译时函数名常量 + 前缀不变量 static_assert
│   ├── BuiltinStringRoster.def                 # 所有函数的 X-macro 注册表（单一事实来源）
│   ├── BuiltinStringRuntimeNames.def           # Roster 的 IsPublic=1 过滤视图
│   ├── BuiltinStringMethodNames.def            # dot-call 方法名 → runtime 函数映射
│   ├── BuiltinStringMethodOverloads.def        # 按 arity 的方法重载表
│   ├── BuiltinStringMethodCharOverloads.def    # char 类型参数的方法重载
│   ├── BuiltinStringMethodDefaults.def         # 默认参数补全规则
│   ├── BuiltinStringMethodDefaultArgKinds.def  # 默认参数类型枚举
│   ├── BuiltinStringMethodReceiverKinds.def    # 指针接收者的方法列表
│   ├── BuiltinStringMethodReceiverKindsRoster.def  # 接收者类型枚举
│   ├── BuiltinStringBorrowedViewHelpers.def    # 借用视图 helper 列表（CStr/Data）
│   ├── BuiltinStringLValueDirectHelpers.def    # Lvalue-direct helper 白名单
│   ├── BuiltinStringWptrProducers.def          # 宽指针 producer 列表
│   ├── BuiltinStringPreludeFragments.def       # Prelude 片段排序
│   └── BuiltinStringPrelude/                   # 11 个 .inc 函数实现片段
│       ├── Type.inc                            # struct 定义 + 基础操作
│       ├── Allocation.inc                      # 内存分配/释放/赋值
│       ├── Accessors.inc                       # c_str, data, at, front, back
│       ├── Capacity.inc                        # reserve, shrink_to_fit, capacity
│       ├── Compare.inc                         # eq, compare + case-insensitive 系列
│       ├── Search.inc                          # find, rfind, contains, starts/ends_with + 字符集搜索 + _ic 系列
│       ├── Mutation.inc                        # append, insert, erase, replace, pad, clear, swap, resize
│       ├── Utility.inc                         # clone, substr, trim, repeat, reverse, hash, count, split/join, 数值转换
│       ├── Encoding.inc                        # UTF-8/16/32, Latin-1, Base64/Base32/Hex, URL/Form 编码
│       ├── WebCodec.inc                        # HTML/JSON/CSV escape/unescape
│       └── Format.inc                          # printf 风格 format
│
├── include/neverc/Shellcode/IR/
│   ├── StringRuntimeABI.h                      # 跨层 ABI：kRuntimeFnAttr、arena 常量
│   └── StringRuntimePass.h                     # Shellcode arena 改写 pass 声明
│
├── lib/Foundation/Builtin/
│   ├── BuiltinString.cpp                       # 核心实现（方法分发表 + 薄 header + bitcode API）
│   ├── BuiltinStringThinHeaderPrologue.inc     # 薄 header 固定部分
│   ├── gen_thin_header.py                      # 从 prelude 提取 extern 声明
│   ├── gen_string_runtime.py                   # 生成独立编译单元
│   └── bin2c.py                                # .bc → C 头文件嵌入
│
├── lib/Analyze/Core/
│   └── Sema.cpp                                # NeverCStringFnKinds DenseMap
│
├── lib/Emit/Backend/
│   ├── StringRuntimeLinker.h                   # bitcode 合并 pass 声明
│   ├── StringRuntimeLinker.cpp                 # bitcode 合并 + kRuntimeFnAttr stamp
│   └── BackendUtil.cpp                         # 在所有模式注册 linker pass
│
└── lib/Shellcode/IR/
    └── StringRuntimePass.cpp                   # Shellcode arena 改写 pass
```

### 添加新 runtime 函数的步骤

1. 在 `BuiltinStringRoster.def` 添加一行 `NEVERC_BUILTIN_STRING_FN(NameId, "neverc_string_xxx", 1)`
2. 在对应的 `BuiltinStringPrelude/*.inc` 中实现函数体
3. （可选）在 `BuiltinStringMethodNames.def` 添加 dot-call 映射
4. （可选）在 `BuiltinStringMethodOverloads.def` 添加 arity 重载
5. （可选）在 `BuiltinStringMethodCharOverloads.def` 添加 char 重载

不需要修改任何其他声明点。
