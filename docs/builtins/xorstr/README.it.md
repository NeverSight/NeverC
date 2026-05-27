**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Sistema runtime integrato NeverC](../README.it.md)

# Crittografia delle stringhe a tempo di compilazione (`xorstr`)

## Panoramica

NeverC fornisce una crittografia delle stringhe a due livelli a tempo di compilazione per il codice C, progettata per scenari di sicurezza in cui le stringhe in chiaro (nomi API, percorsi del registro) non devono essere visibili nel binario compilato.

- **Livello 1 — Macro esplicita**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` per il controllo preciso per stringa
- **Livello 2 — Pass IR automatico**: `-fencrypt-call-strings` per crittografare automaticamente tutti gli argomenti stringa nelle chiamate di funzione

Entrambi i livelli utilizzano buffer allocati sullo stack (nessuna allocazione heap), un algoritmo di decrittazione senza istruzione XOR (anti-firma) e pulizia con `memset` volatile prima del ritorno della funzione.

---

## Avvio rapido

```c
#include <neverc/xorstr.h>
FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## Decrittazione anti-firma

L'operazione di decrittazione evita completamente l'istruzione XOR, utilizzando l'equivalente matematico: `a + b − 2 × (a & b)`.

---

## Riferimento flag del compilatore

| Flag | Descrizione |
|------|-------------|
| `-fencrypt-call-strings` | Abilita la crittografia automatica delle stringhe |
| `-fno-encrypt-call-strings` | Disabilita la crittografia automatica |
| `-fencrypt-call-strings-max-len=N` | Lunghezza massima in byte (predefinito: 1024) |
| `-fstring-encrypt-key=0xHEX` | Sovrascrivere il seed della chiave XOR |
