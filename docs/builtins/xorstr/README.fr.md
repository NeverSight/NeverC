**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Système d'exécution intégré NeverC](../README.fr.md)

# Chiffrement de chaînes à la compilation (`xorstr`)

## Vue d'ensemble

NeverC fournit un chiffrement de chaînes à deux niveaux pour le code C, conçu pour les scénarios de sécurité où les chaînes en clair (noms d'API, chemins du registre) ne doivent pas être visibles dans le binaire compilé.

- **Niveau 1 — Macro explicite** : `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` pour un contrôle précis par chaîne
- **Niveau 2 — Passe IR automatique** : `-fencrypt-call-strings` pour chiffrer automatiquement tous les arguments chaîne dans les appels de fonctions

Les deux niveaux utilisent des tampons alloués sur la pile (pas d'allocation sur le tas), des flux de clés propres à chaque instance et un nettoyage volatile. À la frontière du code machine natif, les appels explicites au décodeur `NC_XORSTR` sont rechiffrés puis développés directement à chaque site d'appel ; l'objet final ne conserve aucune fonction de décodage partagée.

---

## Démarrage rapide

```c
#include <neverc/xorstr/xorstr.h>
FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## Chaîne de protection

1. **Sema** chiffre chaque littéral avec une clé propre. La graine `0` obtient une nouvelle entropie du système d'exploitation ; `-fstring-encrypt-key=` sélectionne une sortie déterministe sur 64 bits.
2. **IR intermédiaire / entrée LTO** conserve un appel opaque et non spécialisable au décodeur afin d'empêcher les optimisations de rematérialiser le texte clair.
3. **Frontière finale du code machine** déchiffre puis rechiffre le ciphertext côté compilateur, choisit une forme de boucle par site d'appel, la développe sur place et supprime le décodeur, son graphe auxiliaire, l'ancre ABI, l'état de routage et les noms sémantiques.
4. **Nettoyage** est installé avant l'optimisation ou le provider puis répété dans la queue finale ; cette seconde exécution est idempotente et répare le placement après les changements de CFG.

### Diversité des décodeurs

La séquence d'état, les constantes, le ciphertext et les expressions équivalentes par octet varient selon la graine et le site d'appel. Une forme possible est `a + b − 2 × (a & b)`. Les chargements volatiles d'état/ciphertext freinent le constant folding, et `nooutline` empêche Machine Outliner de recréer un décodeur partagé après la finalisation IR.

IDA ne dispose donc plus d'une routine autonome et stable à identifier ou émuler une seule fois. Cela ne prétend pas rendre irrécupérable, par instrumentation dynamique, le texte clair nécessaire à l'exécution.

---

## Chiffrement automatique et nettoyage

`-fencrypt-call-strings` s'exécute avant l'IPO, après l'optimisation ordinaire et à nouveau après chaque phase IR tardive ordinaire ou fournie par un plugin. LTO applique le même scellement obligatoire après les hooks du provider et de pré-codegen.

Les arguments `CallBase` directs et indirects issus de littéraux privés `unnamed_addr` appartenant au compilateur sont traités ; GEP, casts, `freeze`, `select`, PHI et slots locaux de pointeurs promouvables sont préservés. Les intrinsèques, l'assembleur inline, les tableaux visibles de l'extérieur ou définis par l'utilisateur et les littéraux trop grands sont ignorés. Le passage d'un littéral protégé à `musttail` fait échouer proprement la compilation.

`XorStrCleanupPass` efface le tampon complet par `memset` volatile avant chaque `ret`, `resume`, `cleanupret` déroulant vers l'appelant et déroulement `catchswitch` non intercepté. Un stockage non sûr ou impossible à suivre complètement est rejeté au lieu d'être partiellement effacé.

---

## Référence des drapeaux du compilateur

| Drapeau | Description |
|---------|-------------|
| `-fencrypt-call-strings` | Activer le chiffrement automatique des chaînes |
| `-fno-encrypt-call-strings` | Désactiver le chiffrement automatique |
| `-fencrypt-call-strings-max-len=N` | Longueur maximale en octets (défaut : 1024) |
| `-fstring-encrypt-key=0xHEX` | Remplacer la graine complète sur 64 bits ; `0` utilise une nouvelle entropie |

## Frontières de sortie et reproductibilité

- `-fno-lto` finalise pendant la génération de code natif du frontend.
- Auto-LTO et Full LTO conservent le décodeur opaque dans le bitcode de pré-édition de liens, puis le rechiffrent et le développent après l'optimisation globale et celle des plugins.
- Les pipelines remplacés par un provider et les passes tardives de plugins sont toujours suivis du chiffrement, du nettoyage et de la finalisation obligatoires.
- Avec la graine par défaut, deux builds natifs indépendants diffèrent ; les caches whole-link et de partition susceptibles de rejouer un ancien code protégé sont contournés.
- Une graine non nulle est volontairement déterministe et compatible avec le cache : même entrée et même graine 64 bits complète produisent le même code protégé.
- `-emit-llvm` et le bitcode pre-link sont des artefacts intermédiaires qui conservent volontairement l'ABI opaque. La garantie « aucun décodeur partagé » vise le code machine final produit avec succès.
