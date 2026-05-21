**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation NeverC](../README.fr.md)

# L'extension de fichier `.nc`

## Présentation

NeverC reconnaît `.nc` comme son extension de fichier source native. Lorsque le compilateur détecte un fichier d'entrée `.nc`, il **active automatiquement** toutes les extensions du langage NeverC — aucun drapeau supplémentaire n'est requis.

## Fonctionnalités activées automatiquement

| Drapeau | Effet |
|---------|-------|
| `-fneverc-types` | Alias d'entiers de style Rust (`u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`, `usize`, `isize`) |
| `-fbuiltin-string` | Type valeur `string` intégré avec gestion automatique de la mémoire, syntaxe dot-call et support UTF-8 |

## Utilisation

Nommez simplement votre fichier source avec l'extension `.nc` :

```bash
# Automatique — ni -fbuiltin-string ni -fneverc-types nécessaires
neverc hello.nc -o hello

# Équivalent à :
neverc -fneverc-types -fbuiltin-string hello.c -o hello
```

```c
// hello.nc
#include <stdio.h>

int main(void) {
    string greeting = "Bonjour, NeverC !";
    printf("%s (len=%zu)\n", greeting.c_str(), greeting.len);

    u32 x = 42;
    i64 y = -100;

    string msg = greeting + " x=%u, y=%lld".format(x, y);
    printf("%s\n", msg.c_str());
    return 0;
}
```

## Fonctionnement

La détection opère à deux niveaux du pipeline du compilateur :

### 1. Couche Driver / Toolchain

Le driver inspecte l'extension de chaque fichier d'entrée avant de construire l'invocation du compilateur. Pour les fichiers `.nc`, `-fneverc-types` et `-fbuiltin-string` sont injectés inconditionnellement dans la ligne de commande — l'utilisateur n'a pas besoin de les passer manuellement.

Pour les fichiers `.c`, ces drapeaux restent optionnels : l'utilisateur doit explicitement passer `-fneverc-types` et/ou `-fbuiltin-string`.

### 2. Couche CompilerInvocation

Par sécurité, le frontend vérifie également les extensions des fichiers d'entrée lors de l'analyse de l'invocation. Si une entrée a l'extension `.nc`, `LangOpts.NeverCTypes` et `LangOpts.BuiltinString` sont mis à `1`, garantissant que les fonctionnalités sont actives même si la couche driver est contournée (par ex., lors de l'appel direct de `-cc1`).

## Compatibilité

- Les fichiers `.nc` sont traités comme du code source C — le langage est toujours C (C23 par défaut), pas un nouveau langage
- Tous les drapeaux C standard (`-std=c11`, `-O2`, `-g`, `-Wall`, etc.) fonctionnent de manière identique
- `-fshellcode` se combine naturellement avec `.nc` : le mode shellcode active déjà `string`, et `.nc` s'assure que `neverc-types` est aussi actif
- La compilation croisée (`-target aarch64-linux-gnu`, etc.) fonctionne de la même manière
- Les fichiers `.c` ne sont pas affectés — ils se comportent exactement comme avant sauf si vous passez les drapeaux explicitement

## Quand utiliser `.nc` vs `.c`

| Scénario | Recommandation |
|----------|---------------|
| Nouveau projet NeverC utilisant `string` et les types Rust | Utiliser `.nc` |
| Base de code C existante à garder compatible avec d'autres compilateurs | Utiliser `.c` + drapeaux explicites |
| Projet shellcode | Les deux conviennent — `-fshellcode` active `string` dans tous les cas |
| Base de code mixte | `.nc` pour les fichiers NeverC, `.c` pour le code portable |
