**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilador de shellcode](../README.es.md)

# Tutorial de ensamblaje ARM64 (AArch64) — Perspectiva Shellcode

> Para lectores no familiarizados con ARM64, enfocado en instrucciones generadas por el compilador shellcode.

## 1. Resumen de registros
Registros generales x0-x30 (64-bit) / w0-w30 (32-bit), sp, xzr/wzr, x29 (fp), x30 (lr). Apple ABI reserva x16-x18. Convención AAPCS64.

## 2. Ramas y llamadas
- `b` (incondicional), `bl` (con enlace → relocalización BRANCH26), `br/blr` (indirecto), `ret`.
- **Shellcode debe evitar `bl`**: El enlazador llena el offset de 26 bits. Shellcode no tiene enlazador → usar `blr`.

## 3. Direccionamiento relativo a PC
`adr` (±1MB), `adrp+add` (±4GB páginas). Equivalente a x86_64 `lea rax, [rip+sym]`.

## 4. Carga de inmediatos
Secuencias `mov` + `movk` para valores de 64 bits. **Núcleo de Data2TextPass**: datos constantes como mov/movk en la pila.

## 5–7. Acceso a memoria, aritmética, comparaciones
`ldr/str`, `ldp/stp`, `add/sub/and/orr/eor/lsr/lsl`, `cmp` + ramas condicionales.

## 8. Secuencias de instrucciones típicas generadas
Cálculo puro, Fibonacci recursivo, inline de string en pila (Data2TextPass), syscall (SyscallStubPass svc directo).

## 9. Resumen clave

| Concepto | x86_64 | ARM64 | Shellcode |
|----------|--------|-------|-----------|
| Llamada a función | `call rel32` | `bl imm26` | Extractor parchea BRANCH26 |
| Carga de dirección | `lea rax,[rip+sym]` | `adrp+add` | PAGE21/PAGEOFF12 parcheados |
| Inmediato 64-bit | `mov rax,imm64` | `mov+movk ×4` | Cero relocalizaciones |
| Syscall | `syscall` | `svc #0x80` | Darwin: x16=nr |
