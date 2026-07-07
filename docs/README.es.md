**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Proyecto NeverC](i18n/README.es.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (dyncode README and breadcrumbs).

# Documentación de NeverC

Notas de diseño, referencia API y guías para cada subsistema de NeverC.

---

## Compilador de dyncode

El pipeline de compilación de dyncode es el foco principal de investigación de NeverC. Arquitectura, opciones CLI, matriz de plataformas y ejemplos:

**[Compilador de dyncode →](dyncode-compiler/README.es.md)**

| Documento | Descripción |
|-----------|-------------|
| [README](dyncode-compiler/README.es.md) | Resumen, inicio rápido, objetivos soportados |
| [Pipeline & PIC](dyncode-compiler/pipeline-and-pic/README.es.md) | Diseño IR → objeto → extracción |
| [IR Pass Design](dyncode-compiler/ir-pass-design/README.es.md) | Razón de cada pasada IR |
| [MIR Pass Design](dyncode-compiler/mir-pass-design/README.es.md) | Pasadas MIR del backend |
| [Kernel-Mode DynCode](dyncode-compiler/kernel-mode-dyncode/README.es.md) | Compilación Ring-0 |
| [Cross-Platform Architecture](dyncode-compiler/cross-platform-architecture/README.es.md) | `TargetDesc` y extractores |
| [Platform Extension Guide](dyncode-compiler/platform-extension-guide/README.es.md) | Añadir plataforma |
| [ARM64 Assembly Tutorial](dyncode-compiler/arm64-assembly-tutorial/README.es.md) | Instrucciones ARM64 desde la perspectiva de dyncode |
| [Roadmap](dyncode-compiler/roadmap/README.es.md) | Trabajo planificado |
| [Progress](dyncode-compiler/progress/README.es.md) | Estado de implementación |

---

## La extensión de archivo `.nc`

NeverC reconoce `.nc` como su extensión de archivo fuente nativa. Con `.nc`, todas las extensiones del lenguaje NeverC (`-fneverc-types`, `-fbuiltin-string`) se habilitan automáticamente — sin flags adicionales.

**[Extensión `.nc` →](nc-extension/README.es.md)**

---

## Runtimes Integrados

NeverC extiende el C estándar con runtimes integrados como bitcode LLVM. Cada uno se controla con un flag `-fbuiltin-<name>`. Los archivos `.nc` habilitan `string` automáticamente.

**[Sistema de Runtime Integrado →](builtins/README.es.md)**

| Integrado | Flag | Descripción |
|-----------|------|-------------|
| [String integrado](builtins/string/README.es.md) | `-fbuiltin-string` | Tipo `string` con semántica de valor, métodos con punto, gestión automática de memoria, UTF-8 nativo |
| [mimalloc integrado](builtins/mimalloc/README.es.md) | `-fbuiltin-mimalloc` | Reemplazo transparente de asignador `mimalloc` de alto rendimiento `malloc`/`free`/`calloc`/`realloc` |
| [Cifrado de cadenas (xorstr)](builtins/xorstr/README.es.md) | `-fencrypt-call-strings` | Cifrado de cadenas en tiempo de compilación, descifrado XOR en pila, anti-firma |

---

## API de Plugins

NeverC proporciona una ABI C pura para plugins de pases fuera del árbol. Un plugin es una biblioteca compartida (`.dll` / `.so` / `.dylib`) que registra pases personalizados en puntos de enganche designados del pipeline. Un solo encabezado, cero dependencias LLVM/CRT.

**[API de Plugins →](plugin-api/README.es.md)**

---

## Hoja de ruta

Principales direcciones planificadas del proyecto NeverC: biblioteca estándar, backend EVM para contratos inteligentes, backend Solana eBPF.

**[Hoja de ruta →](roadmap/README.es.md)**

| Característica | Descripción |
|----------------|-------------|
| Biblioteca estándar (`std`) | Paquetes al estilo Go: `fmt`, `os`, `io`, `net`, `crypto`, `encoding`, `sync` y más |
| Suite de plugins de ofuscación (`neverc-obfuscation`) | VM, MBA, aplanamiento de flujo de control, motor polimórfico, anti-manipulación — plugins de primera parte |
| Biblioteca de componentes UI (`neverc-ui`) | UI multiplataforma tipo Qt, renderizador HTML/JS/CSS, diseñador drag-and-drop, flujo nativo IA |
| IDE y herramientas de lenguaje (`neverc-ide`) | Extensión VSCode + IDE independiente para archivos `.nc`, IntelliSense, depuración, visualización de pipeline dyncode |
| Contratos inteligentes EVM | Compilar C a bytecode EVM — escribir contratos en C en lugar de Solidity |
| Solana eBPF | Compilar C a bytecode eBPF de Solana — desarrollo de programas on-chain en C |

---

## Desarrollo local

Compilar NeverC desde el código fuente y configurar el entorno de desarrollo local, incluida la configuración del PATH.

**[Desarrollo local →](local-dev/README.es.md)**

---

## Ejemplos

Ejemplos compilables que demuestran las capacidades de compilación cruzada de NeverC. Todos compilan desde macOS / Linux.

**[Ejemplos →](examples/README.es.md)**
