**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Conventions d'appel personnalisées

NeverC prend en charge des **conventions d'appel personnalisées pilotées par les données** : vous pouvez affecter n'importe quels registres physiques aux arguments et aux valeurs de retour de n'importe quelle fonction, entièrement depuis un plugin externe ou des attributs au niveau du source, sans modifier le compilateur ni la moindre définition TableGen.

## Vue d'ensemble

Les conventions d'appel LLVM traditionnelles sont figées dans le backend via des fichiers `.td` / `.inc`. En ajouter ou en modifier une impose d'éditer les sources du compilateur et de relancer TableGen. NeverC remplace cela par un modèle **piloté par les données à l'exécution**, construit sur deux couches :

- Une **spec** — une courte chaîne rédigeable à la main, par exemple `gpr:rcx,rdx;ret:rax` — est attachée à une fonction sous forme d'attribut chaîne `"neverc-callconv"`, par un plugin ou par un attribut au niveau du source.
- Avant la génération de code, l'hôte **matérialise** cette spec en un attribut `"neverc-cc-plan-v1"` : une table d'emplacements exacte, immuable et validée, liée à un schéma de cible précis. Le backend ne consomme que le plan.

La spec est ce que vous écrivez ; le plan est ce que le backend croit. Les conventions d'appel passent donc de « codées en dur dans le backend à la compilation » à « pilotées par une politique externe à l'exécution », sans renoncer à la vérification.

## Format de la spec

Une spec est une chaîne délimitée par des points-virgules. Chaque segment comporte une clé et une liste de noms de registres séparés par des virgules (insensible à la casse, tolérant aux espaces) :

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| Segment | Alias | Signification |
|---|---|---|
| `args` | | **Mode positionnel** : chaque jeton est un nom de registre ou `stack`/`mem`, affecté aux arguments par indice |
| `gpr` | `arg_gpr` | **Mode pool** : registres d'arguments entiers/pointeurs, consommés dans l'ordre ; le débordement part sur la pile |
| `xmm` | `arg_xmm` | **Mode pool** : registres d'arguments flottants/vectoriels |
| `fpr` | | Alias neutre vis-à-vis de la cible pour `xmm` |
| `ret_gpr` | `ret` | Registres de retour entiers/pointeurs |
| `ret_xmm` | | Registres de retour flottants/vectoriels |
| `ret_fpr` | | Alias neutre vis-à-vis de la cible pour `ret_xmm` |
| `csr` | | Ensemble personnalisé de registres callee-saved (par défaut : l'ensemble ABI standard) |

Tout segment peut être omis, et les segments inconnus sont ignorés. Ces clés sont définies une seule fois dans `llvm/include/llvm/CodeGen/NeverCCallConv.h`, si bien que producteurs et analyseur ne peuvent pas diverger.

### Deux modes d'arguments

**Mode pool** (`gpr:` / `xmm:`) : les arguments entiers prennent les registres du pool `gpr` dans l'ordre ; les arguments flottants et vectoriels puisent dans `xmm`. Quand un pool est épuisé, les arguments restants partent sur la pile.

**Mode positionnel** (`args:`) : l'argument *i* utilise le *i*-ème jeton. Chaque jeton est soit un nom de registre, soit `stack` / `mem`, ce qui force cet argument sur la pile :

```
args:rcx,stack,r8;ret:rax   # arg0→rcx, arg1→pile, arg2→r8, retour→rax
```

Lorsque `args` est présent, il prime sur `gpr` / `xmm`. Un jeton désignant une mauvaise classe de registre pour le type de l'argument, un indice au-delà de la liste de jetons, ou un registre déjà alloué : tous retombent sur un emplacement de pile plutôt que de faire échouer la compilation.

### Architectures prises en charge

Les noms de registres sont résolus via une table propre à chaque cible, seule source de vérité sur ce qu'une spec peut nommer.

| Architecture | Noms GPR | Noms SIMD | Choix de la largeur |
|---|---|---|---|
| **x86-64** | `rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`, `rbp`, `r8`–`r15` | `xmm0`–`xmm15` | i32 → sous-registre 32 bits, i64/pointeur → 64 bits |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f16→`h`, f32→`s`, f64→`d`, f128/vecteur→`q` |

Les GPR s'écrivent toujours dans leur forme 64 bits ; le backend les réduit au sous-registre correspondant au type de chaque valeur. Sur AArch64, les registres vectoriels s'écrivent `v0`–`v31` et le backend choisit la forme `H`/`S`/`D`/`Q` selon le type.

### Contraintes

- **Registres réservés** : le pointeur de pile est absent des deux tables (`rsp` sur x86-64, `sp`/`x31` sur AArch64), tout comme `x29`/`x30` (FP/LR) sur AArch64. Une spec qui les nomme se contente de les ignorer et la valeur part à l'emplacement valide suivant.
- **Pointeur de trame** : `rbp` *est* sélectionnable sur x86-64 car c'est un registre callee-saved légitime, mais l'employer comme registre d'argument n'est sain que sous `-fomit-frame-pointer`. À utiliser à vos risques et périls.
- **Callee-saved** : par défaut l'ensemble ABI standard. `csr:r12,r13` déclare un ensemble personnalisé, et l'appelant construit un masque de registres préservés correspondant afin de savoir lesquels survivent à l'appel. Pris en charge sur x86-64 comme sur AArch64.
- **Conflits csr** : si un registre figure à la fois dans `csr` et dans une liste d'arguments/retour, le plugin émet un avertissement — l'appelé le restaurerait et détruirait son rôle de transport de valeur. La compilation réussit malgré tout.
- **Fonctions variadiques** : non prises en charge. Les deux backends émettent un diagnostic clair au lieu de mal transmettre silencieusement la partie variadique.
- **Appels indirects** : un appel par pointeur de fonction ne peut pas transporter une convention personnalisée. Le plugin avertit lorsque l'adresse d'une fonction à convention personnalisée est prise ; les appels indirects retombent sur la convention standard.
- **Appels terminaux** : désactivés dès que l'un des deux côtés de l'appel utilise la convention personnalisée, sur les deux backends.
- **Valeurs non couvertes** : tout argument ou retour que le plan ne couvre pas retombe sur la convention standard de la cible (SysV sur x86-64, AAPCS sur AArch64).

## Utilisation

### 1. Piloté par plugin (recommandé)

Le plugin de référence `CustomCallConvPlugin.c` est livré dans `pluginsdk/examples/`. Il enregistre une passe IR au niveau module sur la phase `neverc.ir.pass.post_opt`.

**Compiler le plugin :**

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib   # ou .so / .dll
```

**Mode attribut** (par défaut) — seules les fonctions portant une annotation source `custom_attr` sont concernées :

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib input.c -o output.o
```

**Mode global** — applique une seule spec à toutes les fonctions définies (exige un `cc-all` explicite) :

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

**Filtrer par préfixe de nom :**

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccprefix=secret_ \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r9,r8;ret:rax" \
       input.c -o output.o
```

**Diversifier** — alterner entre quatre dispositions intégrées pour que les fonctions n'en partagent pas une seule (anti-rétro-ingénierie) :

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccshuffle \
       input.c -o output.o
```

Le plugin enregistre quatre options : `cc-all` et `ccshuffle` (des drapeaux, donc `=1` ou `=true` est facultatif), plus `ccspec` et `ccprefix` (valeurs chaîne). Sans `ccspec`, le mode global utilise la valeur par défaut `gpr:r10,r11,rsi,rdi;ret:rdx`.

### 2. Attributs au niveau du source

Annotez directement les fonctions en C avec l'attribut `custom_attr`, en syntaxe GNU ou Microsoft :

```c
// Syntaxe GNU
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

// Syntaxe Microsoft
__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

`custom_attr("key", "value")` produit un attribut chaîne de fonction propre (`"key"="value"`), **sans** avertissement et **sans** passer par `llvm.global.annotations`. C'est un mécanisme **générique** : n'importe quelle paire clé/valeur fonctionne, pas seulement les conventions d'appel. Les passes IR et MIR le relisent avec `F.getFnAttribute("key")`.

### 3. Combinés

Attributs source et arguments de plugin fonctionnent ensemble. Une fonction portant un `custom_attr` passe par le chemin « mode attribut » du plugin ; `cc-all` couvre le reste. Chaque fonction est traitée au plus une fois.

## Plans matérialisés

Une spec nomme des registres ; elle ne dit pas où réside chaque octet de chaque valeur. Après le pipeline d'optimisation et avant la génération de code, l'hôte exécute `materializeCallingConventionPlans`, qui transforme chaque fonction `CallingConv::NeverC_Custom` en un plan exact et validé :

- Une fonction qui possède déjà un attribut `"neverc-cc-plan-v1"` est **validée, pas régénérée** : son empreinte de schéma, son identifiant de cible et son identifiant de convention doivent correspondre à la cible courante.
- Une fonction porteuse d'une spec `"neverc-callconv"` voit ses noms de registres résolus contre la table de registres de la cible. Le plan obtenu remplace la spec, qui est ensuite retirée de l'IR.
- Une fonction sans l'un ni l'autre, mais dont la cible enregistre une convention d'appel via l'ABI de plugin, est planifiée par le rappel `PlanCallingConvention` de cette convention.

Chaque site d'appel direct hérite du plan de son appelé, ce qui maintient l'accord entre appelant et appelé d'une unité de traduction à l'autre. Le plan est une chaîne plate :

```
neverc-cc-plan-v1;schema=<empreinte>;target=<high>:<low>;cc=<high>:<low>;stack=<octets>;returns=<emplacements>;arguments=<emplacements>;callee-saved=<numéros de registres>
```

Chaque emplacement s'écrit `<r|s>,<indice de valeur>,<décalage du fragment>,<taille>,<alignement>,<numéro de registre>,<décalage de pile>,<drapeaux>`, et plusieurs emplacements sont séparés par `|`. Pour le chemin intégré, l'empreinte de schéma est `llvm-<triplet cible>` ; une cible enregistrée par un plugin fournit la sienne.

Comme les numéros de registres n'ont de sens qu'au regard du schéma qui les définit, une discordance est une erreur franche plutôt qu'une compilation silencieusement fausse :

| Situation | Diagnostic |
|---|---|
| La chaîne du plan ne s'analyse pas | `malformed NeverC calling convention plan` |
| L'empreinte de schéma diffère | `NeverC calling convention plan belongs to a foreign target schema` |
| L'identifiant de cible diffère | `NeverC calling convention plan has a foreign target ID` |
| L'identifiant de convention diffère | `NeverC calling convention plan has a foreign convention ID` |

C'est ce qui rend un plan sûr à intégrer dans du bitcode et à transporter à travers le LTO : un plan produit pour une autre cible ne peut pas être appliqué par accident.

## API du plugin

Le plugin d'exemple n'utilise que la table IR core stable — il n'existe pas de point d'entrée dédié aux conventions d'appel. Appliquer une convention à une fonction tient en trois appels, plus la synchronisation des sites d'appel :

```c
NevercIRAttributeHandle Attribute = {0};
Core->CreateStringAttribute(Core->Context, Task, SV("neverc-callconv"), Spec,
                            &Attribute);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION, 0, Attribute);
Core->SetFunctionCallingConvention(Core->Context, Task, Function,
                                   NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM);
```

`NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM` est le nom stable au niveau ABI de `CallingConv::NeverC_Custom` (valeur LLVM 1000). Le plugin parcourt ensuite les usages de la fonction avec `GetValueUseCount` / `GetValueUse` et, pour chaque usage qui est l'opérande appelé d'un `call`, `invoke` ou `callbr`, applique la même convention à l'instruction via `SetInstructionProperty` avec `NEVERC_IR_PROPERTY_CALLING_CONVENTION`. Tout autre usage signifie que l'adresse s'est échappée, d'où l'avertissement sur l'adresse prise.

Un plugin qui enregistre sa propre cible peut au contraire fournir un rappel `PlanCallingConvention` sur son `NevercCallingConventionDescriptor` et produire directement des plans, en sautant la couche spec. Voir [Cible, MC, assembleur, objet](../target-mc-object.fr.md).

## Tests

La suite GoogleTest se trouve dans `tests/neverc/CustomCallConvTests.cpp` et compte 26 tests. Chacun compile le plugin d'exemple, traduit un petit programme en assembleur sous une spec donnée, puis vérifie le placement en registre ou sur la pile obtenu.

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

Couverture :

| Catégorie | Tests |
|---|---|
| x86-64 pool / positionnel / pile / débordement / i64 / sret / byval / repli | 9 |
| AArch64 GPR / FPR / pile / `csr` / appel croisé entre specs différentes | 5 |
| Frontend `custom_attr` (GNU / `__declspec` / bout en bout) | 3 |
| Matérialisation du plan et rejet de schéma | 3 |
| Durcissement (`csr`, variadiques sur les deux cibles, indirect, `rsp`, conflit csr) | 6 |

## Architecture

```
Attribut source               Passe IR du plugin
custom_attr(...)              (neverc.ir.pass.post_opt)
       │                            │
       └─────────────┬──────────────┘
                     ▼
   "neverc-callconv" = spec, CallingConv::NeverC_Custom
   sur la fonction et ses sites d'appel directs
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ materializeCallingConventionPlans        │
   │ (après optimisation, avant codegen)      │
   │                                          │
   │  spec        → noms vers registres phys. │
   │  convention plugin → PlanCallingConv...  │
   │  plan existant → valider schéma / cible  │
   └──────────────────────────────────────────┘
                     │
                     ▼
   "neverc-cc-plan-v1" = emplacements validés
   spec retirée ; plan copié sur les appels directs
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ CCAssignFn du backend (une par cible)    │
   │  CC_X86_NeverC     / RetCC_X86_NeverC    │
   │  CC_AArch64_NeverC / RetCC_AArch64_NeverC│
   │                                          │
   │  lit le plan → affecte les emplacements  │
   │  valeurs non couvertes → convention std  │
   │  appels terminaux désactivés             │
   └──────────────────────────────────────────┘
                     │
                     ▼
   Code machine avec la disposition personnalisée
```

L'exécuteur du backend est une **implémentation faite une fois pour toutes** — toutes les décisions de politique vivent dans le plugin. Ajouter une convention n'exige jamais de reconstruire NeverC.
