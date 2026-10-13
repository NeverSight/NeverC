**Languages**: [English](../hosted-dyncode.md) | [简体中文](../zh-CN/hosted-dyncode.md) | [繁體中文](../zh-TW/hosted-dyncode.md) | [日本語](../ja/hosted-dyncode.md) | [한국어](../ko/hosted-dyncode.md) | [Français](hosted-dyncode.md) | [Deutsch](../de/hosted-dyncode.md) | [Español](../es/hosted-dyncode.md) | [Italiano](../it/hosted-dyncode.md) | [Русский](../ru/hosted-dyncode.md) | [العربية](../ar/hosted-dyncode.md)

[← DynCode](dyncode-compiler.md)

# DynCode hébergé et traduction C++

Utilisez `-fdyncode-hosted` pour installer une fonction sans données dans un programme existant. `-fdyncode-keep-obj` est obligatoire : les CFI/SEH du compilateur restent dans cet objet, hors de la charge plate. Une section native unique et une entrée à zéro conservent la correspondance avec le déroulage.

Ce mode désactive les conversions implicites syscall/PEB, arène de tas et bibliothèque de chaînes. Les globales modifiables/TLS et appels non résolus échouent. Les pointeurs de données ou de rappel peuvent être des paramètres. DynCode autonome conserve son comportement.

La traduction C++ de base accepte `--target` et lie C23, carte source et manifeste à cet ABI. `cpp-core-v2` couvre le sous-ensemble C++17 documenté, dont modèles et durées de vie normales. Exceptions C++, RTTI, dispatch virtuel et nettoyage lors de propagation d’une exception externe restent hors profil.

```sh
neverc translate --from cpp replacement.cpp --profile cpp-core-v2 \
  --target aarch64-unknown-linux-gnu -o replacement.nc -- -std=c++17
neverc --no-default-config --target=aarch64-unknown-linux-gnu \
  -fdyncode -fdyncode-hosted -fdyncode-entry=replacement \
  -fdyncode-keep-obj=replacement.o -fdyncode-report=replacement.audit.json \
  -o replacement.bin replacement.nc
```

L’audit contient `execution_profile="hosted-v1"`, `keep_unwind=true` et `target_triple`. `input_digest` hache l’objet conservé ; `output_digest` hache la charge finale relocalisée et vérifiée. L’installateur doit vérifier les deux empreintes, les octets et l’entrée, puis enregistrer les vrais éléments de déroulage.
