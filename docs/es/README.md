**Idiomas**: [English](../README.md) | [简体中文](../zh-CN/README.md) | [繁體中文](../zh-TW/README.md) | [日本語](../ja/README.md) | [한국어](../ko/README.md) | [Français](../fr/README.md) | [Deutsch](../de/README.md) | [Español](README.md) | [Italiano](../it/README.md) | [Русский](../ru/README.md) | [العربية](../ar/README.md)

[← Proyecto NeverC](project.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (dyncode README and breadcrumbs).

# Documentación de NeverC

Notas de diseño, referencia API y guías para cada subsistema de NeverC.

---

## Compilador de dyncode

El pipeline de compilación de dyncode es el foco principal de investigación de NeverC. Arquitectura, opciones CLI, matriz de plataformas y ejemplos:

**[Compilador de dyncode →](dyncode-compiler/README.md)**

| Documento | Descripción |
|-----------|-------------|
| [README](dyncode-compiler/README.md) | Resumen, inicio rápido, objetivos soportados |
| [Pipeline & PIC](dyncode-compiler/pipeline-and-pic.md) | Diseño IR → objeto → extracción |
| [IR Pass Design](dyncode-compiler/ir-pass-design.md) | Razón de cada pasada IR |
| [MIR Pass Design](dyncode-compiler/mir-pass-design.md) | Pasadas MIR del backend |
| [Kernel-Mode DynCode](dyncode-compiler/kernel-mode-dyncode.md) | Compilación Ring-0 |
| [Cross-Platform Architecture](dyncode-compiler/cross-platform-architecture.md) | `TargetDesc` y extractores |
| [Platform Extension Guide](dyncode-compiler/platform-extension-guide.md) | Añadir plataforma |
| [ARM64 Assembly Tutorial](dyncode-compiler/arm64-assembly-tutorial.md) | Instrucciones ARM64 desde la perspectiva de dyncode |
| [Roadmap](dyncode-compiler/roadmap.md) | Trabajo planificado |
| [Progress](dyncode-compiler/progress.md) | Estado de implementación |

---

## La extensión de archivo `.nc`

NeverC reconoce `.nc` como su extensión de archivo fuente nativa. Con `.nc`, todas las extensiones del lenguaje NeverC (`-fneverc-types`, `-fbuiltin-string`) se habilitan automáticamente — sin flags adicionales.

**[Extensión `.nc` →](nc-extension.md)**

---

## Runtimes Integrados

NeverC extiende el C estándar con runtimes integrados como bitcode LLVM. Cada uno se controla con un flag `-fbuiltin-<name>`. Los archivos `.nc` habilitan `string` automáticamente.

**[Sistema de Runtime Integrado →](builtins/README.md)**

| Integrado | Flag | Descripción |
|-----------|------|-------------|
| [String integrado](builtins/string.md) | `-fbuiltin-string` | Tipo `string` con semántica de valor, métodos con punto, gestión automática de memoria, UTF-8 nativo |
| [mimalloc integrado](builtins/mimalloc.md) | `-fbuiltin-mimalloc` | Reemplazo transparente de asignador `mimalloc` de alto rendimiento `malloc`/`free`/`calloc`/`realloc` |
| [Cifrado de cadenas (xorstr)](builtins/xorstr.md) | `-fencrypt-call-strings` | Cifrado por instancia, sellado tardío obligatorio, expansión por punto de llamada y limpieza volátil de pila |
| [Hash de cadenas (strhash)](builtins/strhash.md) | `-fstrhash-algo` / `-fstrhash-fold` | Hash de cadenas en tiempo de compilación, mismo algoritmo en runtime, pliegue IR opcional |

---

## API de Plugins

NeverC abre toda su cadena de herramientas mediante una ABI C pura. Un complemento es un módulo compartido (`.dll` / `.so` / `.dylib`) que se engancha a cualquiera de las 130 fases de compilación con nombre —desde el análisis de la línea de órdenes hasta la imagen enlazada final— como observador, como interceptor o como proveedor sustituto. El SDK es solo de cabeceras: sin cabeceras de LLVM y sin enlazar con el compilador.

**[API de Plugins →](plugin-api/README.md)**

| Documento | Descripción |
|-----------|-------------|
| [README](plugin-api/README.md) | Punto de entrada, fases, negociación de interfaces, registro, reglas ABI |
| [Plugins de Python](plugin-api/python.md) | Python embebido opcional, ciclo de vida, opciones, observers de solo lectura, diagnósticos y límites |
| [API del driver](plugin-api/driver.md) | Línea de órdenes, selección de cadena de herramientas, grafo de acciones, grafo de trabajos |
| [API de fuentes y E/S](plugin-api/source.md) | Proveedores VFS, ubicaciones de origen, búferes, sumideros de salida, dependencias |
| [API del preprocesador](plugin-api/prep.md) | Tokens, macros, pragmas, inclusiones, consultas de características, 39 tipos de eventos |
| [API de AST y semántica](plugin-api/ast-sema.md) | Extensión del analizador, mutación del AST, búsqueda de nombres, tipos, constantes |
| [API de IR](plugin-api/ir.md) | Lectura de IR de LLVM, construcción transaccional, análisis, pases, proveedores |
| [API de MIR](plugin-api/mir.md) | Funciones máquina, registros, marcos de pila, pases y análisis de MIR |
| [Destino, MC, ensamblador, objeto](plugin-api/target-mc-object.md) | Registro de destinos, convenciones de llamada, codificación MC, grafos de objetos |
| [API de enlazado y LTO](plugin-api/link-lto.md) | Grafo de enlazado, resolución de símbolos, GC/ICF, proveedores de enlazador y LTO |
| [API de DynCode](plugin-api/dyncode.md) | Imágenes planas independientes de la posición, rebajado de importaciones, codificación de juegos de caracteres |
| [Convenciones de llamada personalizadas](plugin-api/custom-callconv.md) | Complementos de convención de llamada dirigidos por datos |

---

## Hoja de ruta

Principales direcciones planificadas del proyecto NeverC: biblioteca estándar, backend EVM para contratos inteligentes, backend Solana eBPF.

**[Hoja de ruta →](roadmap.md)**

| Característica | Descripción |
|----------------|-------------|
| Biblioteca estándar (`std`) | Paquetes al estilo Go: `fmt`, `os`, `io`, `net`, `crypto`, `encoding`, `sync` y más |
| Suite de plugins de ofuscación (`neverc-obfuscation`) | VM, MBA, aplanamiento de flujo de control, motor polimórfico, anti-manipulación — plugins de primera parte |
| Biblioteca de componentes UI (`neverc-ui`) | UI multiplataforma tipo Qt, renderizador HTML/JS/CSS, diseñador drag-and-drop, flujo nativo IA |
| IDE y herramientas de lenguaje (`neverc-ide`) | Extensión VSCode + IDE independiente para archivos `.nc`, IntelliSense, depuración, visualización de pipeline dyncode |
| Contratos inteligentes EVM | Compilar C a bytecode EVM — escribir contratos en C en lugar de Solidity |
| Solana eBPF | Compilar C a bytecode eBPF de Solana — desarrollo de programas on-chain en C |

---

## Herramientas CLI

Comandos orientados al usuario más allá de una compilación individual.

| Documento | Descripción |
|-----------|-------------|
| [`neverc run`](run.md) | Compilar, ejecutar localmente y descartar un binario temporal (estilo `go run`) |
| [`neverc translate`](translate.md) | Traducir C++ a NeverC |
| [`neverc update`](update.md) | Subir o bajar una instalación release (compilador + runtimes instalados en una etiqueta) |
| [`neverc runtime`](runtime.md) | Instalar, listar, actualizar o quitar sysroots de cross-compilación |
| [`neverc build` / `neverc make`](build.md) | Controlador compatible con GNU Make para Makefiles de ejemplos y proyectos |
| [Binarios de publicación y `--strip`](release-builds.md) | Eliminar símbolos no necesarios y debug fuente, con renombrado estructural de símbolos `.ko` consciente del kernel (no es hash ni encryption) |

---

## Objetivos de seguridad de Windows

| Documento | Descripción |
|-----------|-------------|
| [DLL de enclave VBS](vbs-enclave.md) | Enlazar, validar, procesar, firmar y cargar imágenes de enclave VBS compatibles con Microsoft |

---

## Desarrollo local

Compilar NeverC desde el código fuente y configurar el entorno de desarrollo local, incluida la configuración del PATH.

**[Desarrollo local →](local-dev.md)**

---

## Ejemplos

Ejemplos compilables que demuestran las capacidades de compilación cruzada de NeverC. Todos compilan desde macOS / Linux.

**[Ejemplos →](examples.md)**

---

## Atribución y cita de fuentes

**[guía de atribución →](attribution.md)**
