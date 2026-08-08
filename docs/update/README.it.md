**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Indice della documentazione](../README.it.md) · [← Progetto NeverC](../../README.md)

# `neverc update`

Aggiorna un'**installazione release** così che compilatore e ogni runtime di
cross-compilazione **già installato** passino insieme a **un tag di release concreto**.
`neverc upgrade` è un alias.

Per installazioni via `install.sh` (tipicamente `~/.neverc`). **Non** aggiorna
un albero di build CMake/Ninja — cambi PATH e ricostruisci; vedi
[Sviluppo locale](../local-dev/README.it.md).

## Sintassi

```text
neverc update
neverc update <version>
neverc update --version <version>
neverc update --help
```

Esempi:

```bash
neverc update                 # release completa più recente per questo host
neverc update v3389.1.2       # tag esatto (upgrade o downgrade)
neverc update 3389.1.2        # la «v» iniziale è opzionale
neverc upgrade                # uguale a neverc update
```

`-y` / `--yes` sono accettati per gli script; l'aggiornamento non è interattivo.

## Ambito

| Componente | Comportamento |
|------------|---------------|
| Compilatore (`bin/`, `lib/`, `pluginsdk/`) | Sostituito se il tag obiettivo differisce |
| Runtime già in `runtime/` | Solo i target **già installati** vengono ri-scaricati e fissati |
| Runtime mancanti | **Non** installati automaticamente — [`neverc runtime install`](../runtime/README.it.md) |

## Modello di sicurezza

1. Lock esclusivo in `<install>/.neverc-update.lock`.
2. Risoluzione del tag obiettivo.
3. Download e verifica di `SHA256SUMS` e archivi.
4. Staging, validazione e commit; in caso di errore, rollback.

Se un runtime è difettoso, indica un tag precedente:

```bash
neverc update v3389.0.1
```

## Vincoli

- Solo radice di installazione release (di solito `~/.neverc`). Rifiuta radici FS e alberi CMake.
- L'host deve corrispondere a un asset compilatore pubblicato.
- Su Windows un helper a vita breve può sostituire `neverc.exe` dopo l'uscita.

## Comandi correlati

| Comando | Uso |
|---------|-----|
| [`neverc runtime`](../runtime/README.it.md) | Sysroot singoli senza cambiare il compilatore |
| [`neverc run`](../run/README.it.md) | Compila ed esegue un binario temporaneo |
| [`neverc build` / `make`](../build/README.it.md) | Guida Makefile di esempi/progetti |
| `neverc update --help` | Guida incorporata |
