**Langues** : [English](../vbs-enclave.md) | [简体中文](../zh-CN/vbs-enclave.md) | [繁體中文](../zh-TW/vbs-enclave.md) | [日本語](../ja/vbs-enclave.md) | [한국어](../ko/vbs-enclave.md) | [Français](vbs-enclave.md) | [Deutsch](../de/vbs-enclave.md) | [Español](../es/vbs-enclave.md) | [Italiano](../it/vbs-enclave.md) | [Русский](../ru/vbs-enclave.md) | [العربية](../ar/vbs-enclave.md)

[← Index de la documentation](README.md) · [← Projet NeverC](project.md)

# DLL d’enclave VBS sous Windows

NeverC peut lier des DLL d’enclave VBS compatibles avec Microsoft pour les cibles Windows 64 bits. Le contrat de l’éditeur de liens pris en charge est le suivant :

```text
/DLL /INCREMENTAL:NO /INTEGRITYCHECK /ENCLAVE /GUARD:MIXED
```

Transmettez les options de l’éditeur de liens Microsoft via le pilote Windows avec `-Xmslink` ou `-Wl,` :

```powershell
neverc.exe --target=x86_64-pc-windows-msvc -fno-lto -shared -nostdlib `
  enclave.obj guarded.obj legacy.obj `
  -lvertdll -lbcrypt -llibcmt -llibvcruntime -lucrt `
  -Xmslink /INCREMENTAL:NO `
  -Xmslink /NODEFAULTLIB `
  -Xmslink /ENCLAVE `
  -Xmslink /INTEGRITYCHECK `
  -Xmslink /GUARD:MIXED `
  -Xmslink /DYNAMICBASE `
  -Xmslink /MACHINE:X64 `
  -o game-security-enclave.dll
```

Cet exemple sélectionne explicitement avec `-l` les variantes enclave des bibliothèques CRT MSVC et UCRT. Toute sélection explicite de `-vctoolsdir` ou de `-winsysroot` conserve sa priorité habituelle. Sans ces substitutions, toute liaison `/ENCLAVE` sous macOS, Linux ou Windows résout les bibliothèques Windows uniquement dans le runtime cible fourni avec NeverC ; le pilote ne détecte pas automatiquement une installation Visual Studio ou un SDK Windows sur l’hôte et ne s’y rabat pas.

## Builds multi-hôtes avec le runtime fourni

La compilation et la liaison COFF sont indépendantes de l’hôte. La même commande peut s’exécuter sous macOS, Linux ou Windows après l’installation du runtime cible :

```text
neverc runtime install windows-x64
neverc runtime install windows-arm64
```

Le paquet cible contient les en-têtes Windows, le CRT d’enclave, l’UCRT d’enclave, `vertdll.lib`, `bcrypt.lib` et les autres bibliothèques d’importation Windows requises. Lorsque la résolution utilise le runtime fourni, seule la combinaison d’un `/ENCLAVE` explicite et d’un `/NODEFAULTLIB` global fait passer NeverC des répertoires CRT/UCRT ordinaires fournis aux répertoires CRT/UCRT d’enclave. Dans ce mode, le pilote vérifie avant la liaison que les fichiers fournis `libcmt.lib`, `libvcruntime.lib`, `ucrt.lib`, `vertdll.lib` et `bcrypt.lib` existent tous. Les bibliothèques restent sélectionnées explicitement avec `-l...`. `/ENCLAVE` seul n’active pas les répertoires CRT/UCRT d’enclave et ne sélectionne pas leurs bibliothèques ; les chemins de recherche ordinaires du runtime fourni restent utilisés.

L’étape de liaison multi-hôtes produit une DLL d’enclave non signée et non traitée. Le traitement VEIID, la signature avec SignTool et le chargement effectif au moyen de `CreateEnclave`/`LoadEnclaveImage` restent réservés à Windows ; déplacez donc une DLL liée sous macOS ou Linux vers une machine Windows de packaging ou de test pour les trois dernières étapes. Consultez [Runtimes cibles](runtime.md) pour l’installation et la détection des runtimes.

## Entrées d’image requises

Une liaison d’enclave doit fournir ces deux définitions de données d’image :

- `__enclave_config`, qui contient les données `IMAGE_ENCLAVE_CONFIG` de l’image ;
- `_load_config_used`, avec une structure load-config suffisamment grande pour contenir `EnclaveConfigurationPointer`.

NeverC maintient `__enclave_config` en vie lors de l’élimination du code mort, l’extrait d’une archive si nécessaire et vérifie que le pointeur load-config finalement relocalisé est égal à l’adresse virtuelle de cet objet de configuration. Une définition manquante, absolue, éliminée, tronquée ou incorrectement relocalisée provoque une erreur de liaison.

`/GUARD:MIXED` active la sortie CFG pour un mélange de fichiers objets protégés et hérités. Il émet des entrées GFID et GIAT de cinq octets : une RVA de quatre octets suivie d’un octet de métadonnées, nul pour les cibles ordinaires actuelles. Les `GuardFlags` portent les bits CFG et taille d’entrée. Les objets hérités apportent les cibles dont l’adresse est prise en parcourant les relocalisations de manière prudente, à l’exclusion des métadonnées d’unwind.
Lorsque `/GUARD:MIXED` est combiné à `/GUARD:EHCONT`, la table des cibles de continuation EH utilise elle aussi des entrées de cinq octets : une RVA de quatre octets suivie d’un octet de métadonnées nul.

Une demande explicite de liaison incrémentale est incompatible avec `/ENCLAVE` et est donc rejetée. La dernière option `/INCREMENTAL` effective est utilisée, y compris les options provenant des directives des fichiers objets.

`/ENCLAVE` ne sélectionne pas implicitement une sortie DLL, CFG, la vérification d’intégrité, les bibliothèques CRT d’enclave, le traitement VEIID ou la signature. Gardez ces choix explicites dans le pipeline de build. En mode runtime fourni, les chemins de recherche CRT/UCRT d’enclave et la validation des cinq bibliothèques décrits ci-dessus ne sont activés qu’avec un `/NODEFAULTLIB` global explicite ; sans cette option, les chemins ordinaires du runtime Windows fourni restent utilisés. Les substitutions explicites de la chaîne d’outils utilisateur conservent leur priorité habituelle.

## Flux de build et de déploiement

1. Compilez les sources sensibles à la sécurité avec CFG activé, par exemple avec `-fms-guard=cf`. Les objets hérités peuvent rester non instrumentés lorsque la liaison finale utilise `/GUARD:MIXED`.
2. Définissez la configuration et le point d’entrée de l’enclave, puis liez avec les CRT/UCRT d’enclave et les bibliothèques d’importation Vertdll et BCrypt requises.
3. Inspectez l’image PE non signée et vérifiez son répertoire load-config, ses tables CFG, son pointeur de configuration d’enclave et ses relocalisations de base.
4. Sous Windows, exécutez l’outil VEIID du SDK Windows sur l’image terminée.
5. Sous Windows, signez l’image traitée par VEIID avec SignTool. La signature doit être la dernière modification du fichier.
6. Dans l’hôte Windows, vérifiez `IsEnclaveTypeSupported(ENCLAVE_TYPE_VBS)`, allouez l’enclave avec `CreateEnclave`, chargez la DLL avec `LoadEnclaveImage` et appelez `InitializeEnclave`.
7. Résolvez une fonction exportée de l’enclave avec `GetProcAddress`, exécutez-la via `CallEnclave` et vérifiez sa valeur de retour. Répétez les appels en couvrant les chemins d’appels indirects protégés par CFG et hérités.
8. Terminez et libérez l’enclave avec `TerminateEnclave` et `DeleteEnclave`, en vérifiant la réussite des deux opérations.

Pour les systèmes anti-triche, l’enclave convient à un petit composant de vérification ou de gestion de clés dont le code et l’état privé nécessitent une frontière plus forte avec le processus de jeu ordinaire. Gardez l’interface de l’enclave étroite et validez toutes les données fournies par l’hôte : celui-ci contrôle toujours les entrées, l’ordonnancement, le stockage et la disponibilité. Une enclave VBS complète l’autorité côté serveur, la télémétrie, les défenses du pilote et le durcissement ordinaire du processus ; elle ne les remplace pas.

## Validation

Le workflow `VBS enclave differential CI` s’exécute sous Windows. Sa barrière statique :

- construit l’éditeur de liens NeverC et les tests COFF ciblés ;
- crée des DLL d’enclave équivalentes liées par Microsoft et par NeverC ;
- compare la sémantique publique PE/load-config/CFG ;
- exécute des tests de mutation sur le vérificateur PE ;
- prépare des images traitées par VEIID pour une sonde d’exécution différentielle.

La sonde d’exécution x64 lance d’abord l’image Microsoft, puis les deux candidats NeverC. Elle résout une fonction exportée avec `GetProcAddress`, répète les appels `CallEnclave` en couvrant les chemins d’appels indirects protégés par CFG et hérités, puis vérifie les valeurs de retour. `PASS` exige des preuves dans l’ordre pour chaque étape du cycle de vie, y compris la réussite de la terminaison et de la libération ; le chargement et l’initialisation ne suffisent pas. Dans ce workflow, ARM64 est couvert par la validation statique et la liaison différentielle ; la sonde d’exécution habituelle reste limitée à x64.

Une validation distincte sur Windows 11 ARM64 natif a réussi le 2026-09-11 ([exécution 34554067852](https://github.com/NeverSight/NeverC/actions/runs/34554067852)). Elle a réutilisé les deux DLL ARM64 de NeverC construites à partir de `c46e3a4cc4b3732823a7241a43bca6c44b0f0b74` et les a testées aux côtés d’une image de référence nouvellement construite avec MSVC. Les 48 étapes du cycle de vie et les 12 appels avec vérification des résultats ont réussi ; la terminaison et la libération des enclaves ainsi que le nettoyage du certificat de signature ont également réussi. Cela valide ces artefacts précis, et non une nouvelle construction du compilateur NeverC ni un contrôle permanent d’exécution ARM64 en CI.

Une sonde d’exécution facultative ne peut signaler `SKIP` que pour une indisponibilité reconnue des prérequis de l’environnement, avant l’exécution de la fonction. Les plantages, délais dépassés, preuves d’étapes manquantes ou désordonnées et erreurs fonctionnelles produisent `FAIL`, y compris pour la référence Microsoft. Un runner VBS auto-hébergé et configuré peut rendre obligatoire la réussite à l’exécution.

L’éditeur de liens prend en charge les images d’enclave COFF x86-64 et ARM64. Il valide le pointeur de configuration publié, puis déduit de l’ensemble final des imports DLL ordinaires une séquence contiguë d’entrées `IMAGE_ENCLAVE_IMPORT` de 80 octets. Au départ, les entrées ne contiennent que le nom d’importation et des champs d’identité nuls pour la liaison par VEIID ; l’éditeur de liens réécrit le nombre, la liste et la taille d’entrée. Les imports à chargement différé actifs sont refusés. Il n’impose aucune politique supplémentaire aux champs versionnés de `IMAGE_ENCLAVE_CONFIG`.
