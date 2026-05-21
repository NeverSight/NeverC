**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilatore shellcode](../README.it.md)

# Progettazione dei pass IR — Principi, pipeline ed esempi prima/dopo

> Questo documento spiega il **perché** di ogni pass nella pipeline di compilazione shellcode.

## 0. Idea centrale

Obiettivo in una frase: **Eliminare tutto nel `.o` che diventerebbe una rilocazione, lasciando solo un flusso di istruzioni puro direttamente `mmap(RWX)` + `memcpy` + `blr`.**

## 1–13. Pass

| Pass | Funzione |
|------|----------|
| ZeroRelocPass | Prep: unificazione linkage + alwaysinline. Stackify: globali mutabili → alloca |
| IndirectBrPass | computed-goto → switch |
| SyscallStubPass | libc extern → trap inline guidate da TargetDesc + compat POSIX + autofix K&R |
| WinPEBImportPass | Win32 extern → resolver PEB walk (~190 API) + cache indirizzi cifrata + compat Windows POSIX |
| MemIntrinPass | mem*/str*/abs → helper loop-byte inline |
| CompilerRtPass | `__int128` div/mod → divisione lunga inline |
| Data2TextPass | Fase 1+2: GV costanti → immediati/stack + split residuo SROA |
| AllBlrPass | (opzionale) chiamate dirette → indirette |
| KernelImportPass | (ring-0) extern → chiamate indirette via resolver |
| StringRuntimePass | metodi `string` integrati → varianti arena stack |
| HeapArenaPass | `malloc`/`free`/`calloc`/`realloc` → alloc arena + fallback OS per allocazioni grandi |

11 hook di offuscamento. Filosofia diagnostica: 1 errore = 1 diagnostica azionabile. Vedere [plugin-interface.md §6](../plugin-interface/README.it.md#6-registration-position-selection--pic-coverage-matrix) e [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.it.md).
