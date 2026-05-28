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
