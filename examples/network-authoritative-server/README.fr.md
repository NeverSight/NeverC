**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Serveur de jeu autoritaire

Cet exemple exécutable utilise la pile réseau portable de NeverC plutôt que
des sockets bruts de la plateforme. Il fournit :

- un plan de contrôle TCP qui émet des jetons de session basés sur un CSPRNG ;
- des plans d'entrée temps réel UDP et QUIC natif ;
- une boucle de simulation autoritaire à 60 Hz et une file d'entrée bornée ;
- une protection contre le rejeu timestamp/nonce pour les jointures et des
  numéros de séquence d'entrée monotones par session.

Compilation pour l'hôte, ou définition de n'importe quel triple cible NeverC
pris en charge :

```bash
neverc make          # debug : -g (par défaut au premier build)
neverc make release  # release : -O2 --strip
neverc make debug    # revenir en debug
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=authoritative-server.exe
```

Le Makefile conserve `PROFILE`, donc les `neverc make` suivants gardent le
même choix debug/release. La version release utilise le `--strip` intégré.
Voir [Builds de release](../../docs/release-builds/README.fr.md).


Exécution avec un certificat et une clé TLS P-256 pour le point de terminaison
QUIC :

```bash
./authoritative-server cert.pem key.pem
```

Les adresses optionnelles valent par défaut TCP `:7000`, UDP `:7001` et
QUIC `:7002`. La ligne de jointure TCP est
`JOIN <client-id> <unix-ms> <32-hex-nonce>`. L'entrée UDP est
`INPUT <32-hex-session-token> <sequence> <dx> <dy>`. Un client QUIC négocie
`neverc-game/1`, s'authentifie sur son premier flux avec `AUTH <token>`, puis
envoie des datagrammes contenant `<sequence> <dx> <dy>`.
