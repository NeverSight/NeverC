**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilador de shellcode](../README.es.md)

# Interfaz de plugin Shellcode (Plugin SDK)

La pipeline de NeverC tiene una estructura **pipeline core + capa de usuario enchufable**. Ofuscación, anti-desensamblaje, evasión EDR etc. son **intencionalmente no integrados**.

## 1. Pipeline de finalización
Hook PostExtract → cadena reescritores bytes prohibidos → codificador charset → auditoría bytes prohibidos → tamaño → hook PostFinalize.

## 2. Reescritor de bytes prohibidos
`registerBadByteRewriteStrategy`. Idempotente, determinista, solo flujo de bytes.

## 3. Codificador de charset
`registerCharsetEncoder` con `(Name, Encode, Stub, IsCharsetMember)`. Salida debe pasar charset.

## 4. Tamaño / alineación / relleno
`-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`.

## 5–6. Mapeo de hooks y matriz PIC
11 hooks (6 IR + 3 MIR + 2 bytes). Registro más temprano = cobertura PIC más amplia. Recomendación: cifrado string → `RunAfterPrep`; CFF → `RunAfterInlining`; sustitución instrucciones → `RunAfterPreEmit`; cifrado payload → `RunPostFinalize`.
