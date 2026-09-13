# Sanitizers And Hardening

## Sanitizers

### AddressSanitizer (ASan)
Detects memory errors: buffer overflow, use-after-free, use-after-scope.

```bash
# Compile with ASan
clang -fsanitize=address -g program.c -o program

# Key features
# - Stack buffer overflow detection
# - Heap buffer overflow detection
# - Use-after-free detection
# - Memory leak detection
```

### MemorySanitizer (MSan)
Detects uninitialized memory reads.

```bash
clang -fsanitize=memory -g program.c -o program
```

### ThreadSanitizer (TSan)
Detects data races in multithreaded programs.

```bash
clang -fsanitize=thread -g program.c -o program
```

### UndefinedBehaviorSanitizer (UBSan)
Detects undefined behavior at runtime.

```bash
clang -fsanitize=undefined -g program.c -o program

# Specific checks
clang -fsanitize=signed-integer-overflow,null program.c
```

### Custom Sanitizer Development
```cpp
// Implementing custom memory tracking
extern "C" void __asan_poison_memory_region(void const volatile *addr, size_t size);
extern "C" void __asan_unpoison_memory_region(void const volatile *addr, size_t size);

class SecureAllocator {
public:
    void* allocate(size_t size) {
        // Add red zones around allocation
        void* ptr = malloc(size + 2 * REDZONE_SIZE);
        __asan_poison_memory_region(ptr, REDZONE_SIZE);
        __asan_poison_memory_region((char*)ptr + REDZONE_SIZE + size, REDZONE_SIZE);
        return (char*)ptr + REDZONE_SIZE;
    }
};
```

## Hardening Techniques

### Stack Protection
```bash
# Stack canaries
clang -fstack-protector-strong program.c

# Stack clash protection
clang -fstack-clash-protection program.c

# Safe stack (separate stacks for safe/unsafe data)
clang -fsanitize=safe-stack program.c
```

### Control Flow Integrity (CFI)
```bash
# Forward-edge CFI
clang -fsanitize=cfi -flto program.c

# Specific CFI schemes
clang -fsanitize=cfi-vcall      # Virtual call checks
clang -fsanitize=cfi-nvcall     # Non-virtual member call checks
clang -fsanitize=cfi-icall      # Indirect call checks
```

### Shadow Call Stack
```bash
# Backward-edge protection (return address protection)
clang -fsanitize=shadow-call-stack program.c
```

### Position Independent Executables
```bash
# Full ASLR support
clang -fPIE -pie program.c

# Position independent code for shared libraries
clang -fPIC -shared library.c -o library.so
```
