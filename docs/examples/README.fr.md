**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation](../README.fr.md) · [← Projet NeverC](../../docs/i18n/README.fr.md)

# Exemples NeverC

Exemples compilables démontrant les capacités de compilation croisée de NeverC. Tous compilent depuis macOS / Linux — aucun environnement Windows requis.

---

## Exemples disponibles

| Exemple | Description | Fonctionnalités clés |
|---------|-------------|---------------------|
| [Pilote noyau Windows](../../examples/windows-driver/README.fr.md) | Pilote WDM minimal | Compilation croisée `.sys` depuis macOS/Linux, auto-LTO, éditeur de liens intégré |
| [Pilote Windows + CET](../../examples/windows-driver-cet/README.fr.md) | Pilote avec Intel CET Shadow Stack | Code noyau compatible CET, `/guard:ehcont` |
| [Pilote Windows + virgule flottante](../../examples/windows-driver-float/README.fr.md) | Pilote avec virgule flottante/SIMD | Virgule flottante sécurisée en mode noyau |

---

## Démarrage rapide

```bash
cd examples/<nom-exemple>
make
```

Spécifier le chemin du compilateur : `make NEVERC=/path/to/neverc`

Tous les exemples utilisent **neverc** et produisent des binaires Windows PE (`.sys`) via l'éditeur de liens intégré.
