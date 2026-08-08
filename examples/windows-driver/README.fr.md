**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Exemple de pilote noyau Windows

Un pilote noyau WDM minimal construit avec NeverC. Cible **x64** par défaut, et
peut également être compilé pour ARM64. Compilation croisée depuis macOS / Linux.

NeverC est un compilateur tout-en-un — un seul appel gère le prétraitement,
la compilation, l'optimisation (auto-LTO) et l'édition de liens via l'éditeur
de liens intégré.

## Compilation

Depuis le dépôt :

```bash
cd examples/windows-driver
neverc make          # debug : -g (première construction par défaut)
neverc make release  # release : -O2 --strip
neverc make debug    # retour au profil debug
```

Le Makefile mémorise `ARCH`, `PROFILE` et `TESTSIGN`. Pour la publication,
utilisez `neverc make release` (`-O2 --strip` ; imports/exports PE et
métadonnées du chargeur conservés). Avec signature de test :
`neverc make release TESTSIGN=1` (strip puis signature dans le même lien).
Voir [Builds de publication](../../docs/release-builds/README.fr.md).

Cela produit `ExampleDriver-x64.sys`. Pour compiler pour ARM64, ou pour les deux :

```bash
neverc make ARCH=arm64
neverc make all-arch
```

Depuis une version autonome de NeverC :

```bash
neverc make NEVERC=/path/to/neverc
```

Le résultat est `ExampleDriver-<arch>.sys` (optimisé auto-LTO).

## Compilation manuelle (sans Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --driver \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

Pour ARM64, remplacez la cible par `aarch64-pc-windows-msvc` ; rien d'autre ne
change. `-fms-kernel` sélectionne les en-têtes et bibliothèques d'importation du
WDK correspondant à la cible et définit les macros d'architecture attendues par
le WDK, il n'y a donc jamais à les passer à la main.
`--driver` marque l'image comme mode noyau : le code et les données deviennent
non paginables, les tables d'import passent dans la section INIT jetable, et
l'éditeur de liens inscrit la somme de contrôle PE que le chargeur vérifie.

> `-g` intègre les informations de débogage DWARF dans le PE ; inspectez avec
> `llvm-dwarfdump`. Omettez cette option pour les versions de production afin
> de réduire la taille du binaire.

## Signature de test

Windows refuse de charger un pilote noyau non signé. `-ftest-sign` attache une
signature Authenticode pour que l'image passe ce contrôle sur une machine de
test :

```bash
neverc make TESTSIGN=1
neverc make ARCH=arm64 TESTSIGN=1
```

ou ajoutez `-ftest-sign` à une invocation manuelle. L'option n'est acceptée
qu'avec `-fms-kernel`, une signature de test n'ayant aucun sens pour un binaire
en mode utilisateur.

L'identité de signature est intégrée au compilateur — un certificat auto-signé
dont la clé privée est publique par construction. Elle n'apporte aucune
authenticité ; elle satisfait seulement le contrôle d'intégrité du code sur une
machine que vous avez délibérément ouverte. Configurez cette machine une fois,
en administrateur :

```cmd
bcdedit /set testsigning on
certutil -addstore Root neverc-test-signing.cer
certutil -addstore TrustedPublisher neverc-test-signing.cer
```

puis redémarrez. Exportez le certificat depuis le compilateur lui-même, ce qui
garantit qu'il corresponde toujours à l'identité utilisée pour signer :

```bash
neverc --print-test-sign-cert > neverc-test-signing.cer
```

(Une copie se trouve aussi dans `utils/neverc-test-signing.cer` dans les
sources, mais elle ne fait pas partie d'un paquet de release.)

Sans machine Windows, vérifiez la signature avec `osslsigncode`. Notez que
`-CAfile` attend du PEM alors que le certificat est en DER : convertissez-le
d'abord. Passer le DER directement échoue avec un « signature verification
failed » trompeur dont la vraie cause est « no certificate found » :

```bash
openssl x509 -inform DER -in neverc-test-signing.cer -out nc.pem
osslsigncode verify -CAfile nc.pem ExampleDriver-x64.sys
```

**Ne l'utilisez jamais pour quoi que ce soit qui quitte une machine de test.**
En production, signez avec un vrai certificat de signature de code (et, pour
Windows 10 1607 et ultérieur, une signature d'attestation du Microsoft Hardware
Dev Center).

## Fonctionnalités

- Crée un objet périphérique à `\Device\ExampleDriver`
- Crée un lien symbolique à `\DosDevices\ExampleDriver`
- Gère `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Affiche les messages de chargement/déchargement via `DbgPrint`

## Chargement (sur une machine de test Windows)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Activez la signature de test ou utilisez un certificat de signature de code pour
la production.
