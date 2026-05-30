**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentación](../README.es.md) · [← Proyecto NeverC](../../docs/i18n/README.es.md)

# Ejemplos NeverC

Ejemplos compilables que demuestran las capacidades de compilación cruzada de NeverC. Todos compilan desde macOS / Linux — sin necesidad de entorno Windows.

---

## Ejemplos disponibles

| Ejemplo | Descripción | Características clave |
|---------|-------------|----------------------|
| [Controlador de kernel Windows](../../examples/windows-driver/README.es.md) | Controlador WDM mínimo | Compilación cruzada `.sys` desde macOS/Linux, auto-LTO, enlazador integrado |
| [Controlador Windows + CET](../../examples/windows-driver-cet/README.es.md) | Controlador con Intel CET Shadow Stack | Código kernel compatible CET, `/guard:ehcont` |
| [Controlador Windows + punto flotante](../../examples/windows-driver-float/README.es.md) | Controlador con punto flotante/SIMD | Punto flotante seguro en modo kernel |

---

## Inicio rápido

```bash
cd examples/<nombre-ejemplo>
make
```

Especificar ruta del compilador: `make NEVERC=/path/to/neverc`

Todos los ejemplos usan **neverc** y producen binarios Windows PE (`.sys`) mediante el enlazador integrado.
