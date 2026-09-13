# Workflow And Resources

## Development Workflow

1. **Start Simple**: Begin with Kaleidoscope tutorial
2. **Incremental Features**: Add one language feature at a time
3. **Test Extensively**: Unit tests for each compiler phase
4. **Use LLVM Tools**: opt, llc, llvm-dis for debugging IR
5. **Profile and Optimize**: Focus on common code patterns
6. **Cross-Platform First**: Ensure every new feature works across all 5 target triples

## Cross-Platform Development Golden Rules

1. **Never hardcode OS checks in passes** — use `TargetDesc` table lookup
2. **Always use `llvm::sys::path` / `llvm::sys::fs`** — not raw POSIX or Win32 APIs
3. **Gate `#ifdef _WIN32` to Platform.cpp** — keep it out of compiler logic
4. **Test Windows ARM64 separately** — its ABI is a unique hybrid (AAPCS regs + Win64 semantics)
5. **Remember SyscallNumberMask** — macOS x86_64 uses `0x2000000` offset, everything else is `0`
6. **Mach-O section names have `__` prefix** — `__text` not `.text`
7. **Windows has no direct syscall in dyncode** — always PEB walk, even on ARM64
8. **LTO is not safe on Windows hosts** — known UB surfaces under LTO; build non-LTO on Windows CI

## Resources

### Official Tutorials

- LLVM Kaleidoscope: Building a language from scratch
- Clang internals: Frontend implementation patterns
- Writing an LLVM Backend: Target code generation

### Community Projects

See DIY Compiler section in README.md for 100+ example implementations across different language paradigms.

## Getting Detailed Information

When you need detailed and up-to-date resource links, tool lists, or project references, fetch the latest data from:

```
https://raw.githubusercontent.com/gmh5225/awesome-llvm-security/refs/heads/main/README.md
```

This README contains comprehensive curated lists of:
- 100+ DIY compiler implementations (DIY Compiler section)
- Toolchain configurations and IDE setup
- Compiler development tutorials and books
