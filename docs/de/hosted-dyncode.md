**Languages**: [English](../hosted-dyncode.md) | [简体中文](../zh-CN/hosted-dyncode.md) | [繁體中文](../zh-TW/hosted-dyncode.md) | [日本語](../ja/hosted-dyncode.md) | [한국어](../ko/hosted-dyncode.md) | [Français](../fr/hosted-dyncode.md) | [Deutsch](hosted-dyncode.md) | [Español](../es/hosted-dyncode.md) | [Italiano](../it/hosted-dyncode.md) | [Русский](../ru/hosted-dyncode.md) | [العربية](../ar/hosted-dyncode.md)

[← DynCode](dyncode-compiler.md)

# Hosted DynCode und C++-Übersetzung

Für reine Codefunktionen in vorhandenen Programmen verwenden Sie `-fdyncode-hosted`. `-fdyncode-keep-obj` ist erforderlich: Compiler-CFI/SEH bleiben im Objekt und außerhalb der flachen Nutzlast. Ein nativer Codeabschnitt mit Einstieg bei Offset null erhält die Zuordnung zu den Unwind-Daten.

Der Modus deaktiviert implizite Systemaufruf-/PEB-, Heap-Arena- und String-Laufzeitumwandlungen. Schreibbare/TLS-Globals und unaufgelöste Aufrufe scheitern. Daten- oder Callback-Zeiger können als Parameter dienen. Eigenständiges DynCode bleibt unverändert.

Die C++-Kernübersetzung akzeptiert `--target` und bindet C23, Quellkarte und Manifest an diese ABI. `cpp-core-v2` unterstützt die dokumentierte C++17-Teilmenge einschließlich Templates und normaler Konstruktor-/Destruktorlebenszeiten. C++-Exceptions, RTTI, virtuelle Aufrufe und Aufräumen bei fremden Exceptions gehören nicht dazu.

```sh
neverc translate --from cpp replacement.cpp --profile cpp-core-v2 \
  --target aarch64-unknown-linux-gnu -o replacement.nc -- -std=c++17
neverc --no-default-config --target=aarch64-unknown-linux-gnu \
  -fdyncode -fdyncode-hosted -fdyncode-entry=replacement \
  -fdyncode-keep-obj=replacement.o -fdyncode-report=replacement.audit.json \
  -o replacement.bin replacement.nc
```

Der Bericht enthält `execution_profile="hosted-v1"`, `keep_unwind=true` und `target_triple`. `input_digest` prüft das behaltene Objekt, `output_digest` die endgültig relokierte und verifizierte Nutzlast. Der Installer muss beide Hashes sowie Code und Einstieg prüfen und echte Unwind-Daten registrieren.
