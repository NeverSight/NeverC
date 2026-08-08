**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Collecteur de télémétrie anti-triche mTLS

Ce collecteur exécutable sert le protocole NRPC multiplexé sur TLS 1.3 avec
des certificats client obligatoires. `anticheat.Telemetry/Collect` est un flux
bidirectionnel : les agents envoient des enregistrements de télémétrie signés
et reçoivent un ACK nonce par enregistrement accepté.

Chaque message DATA est un en-tête de 64 octets suivi d'un corps opaque
(maximum 1 MiB). L'en-tête contient la version `1`, trois octets nuls, un
horodatage Unix en millisecondes sur huit octets en big-endian, un nonce de
16 octets, une longueur de corps sur quatre octets en big-endian et un
HMAC-SHA256 de 32 octets. Le MAC couvre
`agent-id || first-32-header-bytes || body`. La valeur de métadonnées NRPC
`agent-id` est requise. Les nonces ne sont acceptés qu'une seule fois dans
une fenêtre horaire de 30 secondes.

Les enregistrements acceptés entrent dans une file bornée. Un écrivain unique
dédié ajoute un événement d'audit JSONL contenant l'empreinte du certificat
client, le nonce, le digest du corps, l'horodatage et la taille du corps ; le
collecteur n'écrit jamais directement les octets bruts non fiables du corps
dans le journal d'audit.

Cible par défaut : `x86_64-linux-gnu`. Remplacez-la par toute cible NeverC prise en charge :

```bash
neverc make          # debug : -g (par défaut au premier build)
neverc make release  # release : -O2 --strip
neverc make debug    # revenir en debug
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=anticheat-collector.exe
```

Le Makefile conserve `TARGET` et `PROFILE`, donc les `neverc make` suivants
gardent le même choix d'artefact. La version release utilise le `--strip` intégré.
Voir [Builds de release](../../docs/release-builds/README.fr.md).


Exécution avec un certificat serveur, une clé serveur, une CA client de
confiance, une clé de signature partagée de 32 octets et un chemin d'audit :

```bash
./anticheat-collector server.pem server-key.pem agent-ca.pem \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  telemetry-audit.jsonl
```
