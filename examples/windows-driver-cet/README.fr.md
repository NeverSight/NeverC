# Pilote noyau Windows avec CET Shadow Stack

Un pilote noyau WDM minimal construit avec NeverC, avec le Shadow Stack CET
(Control-flow Enforcement Technology) d'Intel activé. Compilation croisée depuis macOS / Linux.

## Compilation

```bash
cd examples/windows-driver-cet
make
```

Depuis une version autonome de NeverC :

```bash
make NEVERC=/path/to/neverc
```

Le résultat est `CetDriver.sys` (optimisé auto-LTO).
La compilation par défaut inclut `-g` pour le débogage ; **les versions de
production doivent supprimer `-g`** pour retirer les symboles de débogage et
réduire la taille du binaire.

## Drapeaux spécifiques au CET

| Drapeau | Couche | Objectif |
|---------|--------|----------|
| `-fcf-protection=return` | Compilateur | Générer du code compatible Shadow Stack |
| `-Xlinker --cetcompat` | Éditeur de liens | Définir `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` dans le PE |

## Compilation manuelle (sans Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fcf-protection=return \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -Xlinker --cetcompat \
  -lntoskrnl -lhal \
  -o CetDriver.sys driver.c
```

## Fonctionnalités

- Crée un objet périphérique à `\Device\CetDriver`
- Crée un lien symbolique à `\DosDevices\CetDriver`
- Exerce des appels indirects (pointeur de fonction `ComputeFn`) pour valider la compatibilité CET — le Shadow Stack protège les adresses de retour de ces appels
- Affiche les messages de chargement/déchargement via `DbgPrint`

---

## Détails techniques du CET

Le CET dispose de **deux mécanismes de protection indépendants** :

### 1. Shadow Stack — protection du bord arrière (RET)

Le matériel maintient une seconde pile (shadow stack) qui reflète les opérations CALL/RET. **Aucune instruction spéciale n'est nécessaire à l'entrée de la fonction** — c'est entièrement transparent :

```
┌─ CALL target ─────────────────────────────────┐
│                                                │
│  Pile normale :  PUSH return_addr  (RSP)       │
│  Shadow stack :  PUSH return_addr  (SSP, HW)   │
│                                                │
└────────────────────────────────────────────────┘

┌─ RET ─────────────────────────────────────────┐
│                                                │
│  Pile normale :  POP return_addr_A  (RSP)      │
│  Shadow stack :  POP return_addr_B  (SSP, HW)  │
│                                                │
│  Comparaison : return_addr_A == return_addr_B ? │
│    ✓ correspondance → retour normal             │
│    ✗ non-correspondance → exception #CP         │
│                                                │
└────────────────────────────────────────────────┘
```

### 2. Indirect Branch Tracking (IBT) — protection du bord avant (CALL/JMP indirect)

Nécessite une instruction `ENDBR64` (`F3 0F 1E FA`, 4 octets) à chaque cible valide d'appel/saut indirect. Sur les CPU sans CET, `ENDBR64` est un NOP.

### Choix du noyau Windows

| Protection | Mécanisme | Utilisé par le noyau Windows ? |
|------------|-----------|-------------------------------|
| Bord arrière (RET) | CET Shadow Stack | **Oui** (KCET) |
| Bord avant (CALL/JMP indirect) | CET IBT (ENDBR) | **Non** — CFG utilisé à la place |

### Comparaison assembleur : modes `-fcf-protection`

Code source :

```c
unsigned long rotate13(unsigned long val) {
    return (val << 13) | (val >> 19);
}
```

#### `-fcf-protection=none` (pas de CET)

```asm
rotate13:
    mov  eax, ecx
    rol  eax, 13
    ret
```

#### `-fcf-protection=return` (Shadow Stack uniquement — cet exemple utilise ce mode)

```asm
rotate13:
    mov  eax, ecx      ; identique à "none" !
    rol  eax, 13        ; le Shadow Stack est entièrement transparent
    ret
```

#### `-fcf-protection=full` (Shadow Stack + IBT)

```asm
rotate13:
    endbr64             ; ← marqueur IBT (F3 0F 1E FA)
    mov  eax, ecx
    rol  eax, 13
    ret
```

---

## Compilateur vs bin2bin : qui est compatible avec CET ?

CET trace une ligne nette entre les **compilateurs au niveau source** et
les **outils bin2bin** (packers, obfuscateurs, hookeurs, dump+rebuild).
Le Shadow Stack matériel impose trois règles qui remodèlent toute
l'industrie de la protection / obfuscation :

> 1. **Ne pas modifier les adresses de retour.**
> 2. **Ne pas auto-patcher le code** (HVCI impose W^X sur les pages de code).
> 3. **Chercher des transformations d'obfuscation fortes** respectant 1 et 2.

### Un compilateur peut-il « chiffrer les adresses de retour » ?

**Non.** C'est une idée fausse courante. Le Shadow Stack est imposé par le CPU,
pas par l'OS, et il est invisible au code utilisateur. Si vous XOR-chiffrez
l'adresse de retour sur la pile régulière dans l'épilogue de votre fonction :

```c
void my_func() {
    // ... corps de fonction ...
    // l'épilogue tente de chiffrer l'adresse de retour :
    // XOR [rsp], 0xDEADBEEF
    // RET           <- le matériel compare pile régulière vs shadow stack
                     //   elles ne correspondent plus -> exception #CP -> BUGCHECK
}
```

Le shadow stack conserve toujours l'adresse de retour originale. RET déclenche
une comparaison matérielle ; un désaccord déclenche `#CP` et bugcheck le noyau.
Le compilateur **ne peut pas** atteindre le shadow stack :

- Mode utilisateur : aucune instruction ne peut écrire dans le shadow stack
- Mode noyau : `WRSSQ` est privilégié, seul `ntoskrnl` l'utilise

### Obfuscations compatibles CET que le compilateur PEUT faire

| Transformation | Pourquoi sûr CET |
|----------------|------------------|
| **Aplatissement de flux de contrôle** | Le dispatcher switch utilise CALL/JMP direct ; les cas reçoivent ENDBR64 si nécessaire |
| **Virtualisation VM** | Handlers connectés via JMP indirect (avec ENDBR64), pas push+ret |
| **Chiffrement chaînes / constantes** | Pure transformation de données, pas d'impact sur le flux de contrôle |
| **Expressions MBA** | `x + y → (x ^ y) + 2*(x & y)` — données seulement |
| **Prédicats opaques** | Branches conditionnelles via sauts directs |
| **Clonage / inlining de fonctions** | Pas de changement de sémantique de pile d'appel |
| **Substitution d'instructions** | `MOV → XOR + ADD` — pas d'effets sur la pile |

### Schémas hostiles à CET (ils meurent sous KCET)

| Schéma | Pourquoi ça casse |
|--------|-------------------|
| **Chiffrement d'adresse de retour** | Désaccord shadow stack → `#CP` |
| **PUSH addr; RET dispatcher** (style VMProtect / Themida classique) | Idem — le shadow stack n'a pas d'entrée pour `addr` |
| **Stack pivoting** (chaînes ROP) | Le shadow stack ne peut pas suivre le pivot |
| **Code auto-modifiant** | HVCI bloque les écritures sur les pages exécutables |
| **Génération de code à l'exécution** | Idem — violation HVCI W^X |
| **Hooks inline à base de trampoline** | Modifier le prologue de fonction déclenche HVCI ; même en contournant HVCI, le shadow stack casse sur le RET du trampoline |

### Pourquoi les outils bin2bin ont un désavantage structurel

Un compilateur émet du code CET-correct depuis l'IR sémantique. Un outil
bin2bin doit **redécouvrir** la sémantique depuis les octets compilés :

1. **Ambiguïté des limites d'instructions** — x86 est à longueur variable. Ajouter ENDBR64 (4 octets) au mauvais offset casse tout l'adressage RIP-relatif et les relocations.
2. **Identification des cibles indirectes** — bin2bin ne peut pas toujours dire quelles adresses dans `.data` sont des entrées de table de saut vs des données brutes. Soit sur-marquer (gonflement du code, nouvelles graines de ROP gadget), soit sous-marquer (`#CP` à l'exécution).
3. **Risque d'auto-attestation** — Définir `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` est une promesse. Si la sortie bin2bin contient un schéma hostile à CET, le pilote se chargera bien sur les machines non-CET mais BSOD instantanément sur les hôtes KCET.
4. **Complétude du CFG** — Les compilateurs voient tout le graphe d'appels ; bin2bin doit l'inférer, et les appels indirects sans cibles précises forcent un placement conservateur d'ENDBR.

### État de l'industrie

| Outil / classe | État CET |
|----------------|----------|
| **NeverC / Clang / MSVC (compilateurs)** | Nativement compatible CET via `-fcf-protection` + drapeau éditeur de liens |
| **OLLVM / Tigress / passes NeverC** | Transformations au niveau IR → naturellement CET-safe |
| **Microsoft Detours (4.0+)** | Mis à jour pour être CET-compatible |
| **VMProtect / Themida (ancien)** | Le dispatcher Push+RET tue le pilote sur les hôtes KCET |
| **VMProtect / Themida (récent)** | Ajout de dispatchers conscients d'ENDBR, support mixte |
| **Chargeurs manual map / dump+rebuild** | Doivent reconstruire tous les marqueurs ENDBR — sujets aux erreurs |

### Angle sécurité des jeux

Les pilotes anti-triche (EAC, BattlEye, FACEIT AC, Vanguard) sortent avec
`--cetcompat` activé, ils s'exécutent donc proprement sur les machines à KCET
activé. Les pilotes de triche — typiquement packés, hookés ou injectés par
trampoline via outillage bin2bin — peinent à rester CET-conformes. KCET + HVCI
forment un **mur matériel « ami du compilateur, hostile au bin2bin »** qui
favorise asymétriquement les logiciels de sécurité bien conçus par rapport
au code de style malware.

C'est la raison plus profonde pour laquelle Microsoft pousse fortement KCET
pour les logiciels noyau : il rend le code noyau légitime plus facile à durcir
tout en rendant le métier de l'attaquant progressivement plus difficile.

---

## Activation du KCET sur la machine cible

```cmd
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity /v Enabled /t REG_DWORD /d 1 /f
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\KernelShadowStacks /v Enabled /t REG_DWORD /d 1 /f
```

Redémarrage requis. Vérifiez avec `msinfo32.exe`.

**Prérequis :** HVCI activé, Windows build 21389+, CPU avec support CET (Intel Tiger Lake+ / AMD Zen 3+).

## Chargement

```cmd
sc create CetDriver type= kernel binPath= C:\path\to\CetDriver.sys
sc start CetDriver
sc stop CetDriver
sc delete CetDriver
```

Activez la signature de test ou utilisez un certificat de signature de code pour la production.
