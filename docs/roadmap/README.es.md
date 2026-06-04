**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Índice de documentación](../README.es.md)

# Hoja de ruta de NeverC

Este documento describe las principales direcciones planificadas para el proyecto NeverC más allá del compilador de shellcode y los runtimes integrados actuales.

---

## 1. Biblioteca estándar (`std`)

NeverC proporcionará una biblioteca estándar integral basada en la de Go — paquetes con baterías incluidas que cubren necesidades comunes de programación de sistemas sin dependencias externas.

### Paquetes planificados

| Paquete | Descripción |
|---------|-------------|
| `fmt` | E/S con formato (familia printf + extensiones type-safe) |
| `os` | Interacción con el SO: variables de entorno, gestión de procesos, permisos de archivos |
| `io` | Interfaces Reader/Writer, E/S con búfer, utilidades de pipe |
| `fs` | Operaciones del sistema de archivos: recorrido, glob, archivos temporales, escritura atómica |
| `net` | Sockets TCP/UDP, resolución DNS, cliente/servidor HTTP |
| `net/http` | Cliente y servidor HTTP/1.1 y HTTP/2 |
| `crypto` | Hashing (SHA-256, SHA-512, BLAKE3), HMAC, AES, ChaCha20, RSA, Ed25519 |
| `encoding` | JSON, Base64, Hex, CSV, binario (little/big endian) |
| `sync` | Mutex, RWLock, WaitGroup, Once, operaciones atómicas |
| `time` | Reloj monótono/de pared, duración, temporizadores, formato |
| `bytes` | Manipulación de segmentos de bytes, búfer |
| `math` | Constantes, funciones elementales, generación de números aleatorios |
| `sort` | Ordenamiento y búsqueda genéricos |
| `container` | Lista enlazada, heap, búfer circular |
| `log` | Registro estructurado con niveles |
| `flag` | Análisis de flags de línea de comandos |
| `path` | Manipulación de rutas (POSIX y Windows) |
| `regexp` | Coincidencia de expresiones regulares (sintaxis RE2) |
| `compress` | gzip, zlib, zstd, lz4 |
| `hash` | CRC32, CRC64, FNV, xxHash |
| `unicode` | Tablas Unicode, plegado de mayúsculas/minúsculas, conversión UTF-8/UTF-16 |

### Principios de diseño

- **C23 puro** — cada paquete compila como NeverC/C23 estándar; sin C++ oculto ni ensamblador específico de plataforma
- **Cero dependencias externas** — la biblioteca estándar se embebe como bitcode LLVM en el compilador, igual que los built-ins `string` y `mimalloc` existentes
- **Multiplataforma** — todos los paquetes funcionan en macOS, Linux y Windows (x86_64 / AArch64)
- **Compatible con shellcode** — los paquetes que tienen sentido en modo freestanding (ej.: `crypto`, `encoding`, `bytes`) funcionan con `-fshellcode`

---

## 2. Biblioteca de componentes UI (`neverc-ui`)

NeverC proporcionará una biblioteca de componentes UI multiplataforma inspirada en Qt — con un motor de renderizado frontend HTML/JS/CSS, intrínsecamente apto para el diseño de interfaces por IA.

### Objetivos

- **Arquitectura basada en componentes** — ventanas, botones, campos de texto, listas, árboles, tablas, menús, diálogos, pestañas y contenedores de diseño como tipos C de primera clase
- **Renderizador HTML/JS/CSS** — la UI se renderiza a través de un motor de navegador ligero integrado; los desarrolladores escriben la lógica en C, la capa visual usa tecnologías web estándar
- **Diseñador visual arrastrar y soltar** — un constructor GUI que genera código C compatible con NeverC, permitiendo prototipado rápido sin escribir código de layout manualmente
- **Flujo de trabajo de diseño nativo IA** — los LLM pueden generar la lógica de negocio C y el layout HTML/CSS en una sola pasada
- **Apariencia nativa** — temas adaptativos por plataforma (macOS, Windows, Linux) vía variables CSS y detección de fuentes/colores del sistema
- **Integración ligera** — el renderizador se proporciona como runtime integrado (como `string` / `mimalloc`); sin sobrecarga a escala de Electron
- **Sistema de eventos** — funciones callback C para interacciones del usuario (clic, entrada, redimensionar, arrastrar, teclado, eventos personalizados)
- **Enlace de datos** — enlace declarativo entre structs C y estado de la UI; los cambios se propagan automáticamente
- **Renderizado personalizado** — acceso directo a canvas/WebGL para UIs de juegos, visualización de datos o widgets personalizados

### ¿Por qué HTML/CSS para una biblioteca UI C?

- Todos los modelos de IA ya conocen HTML/CSS — la generación de código UI no requiere entrenamiento especializado
- Las tecnologías web son el sistema de layout más probado; no hay necesidad de reinventar flexbox, grid o renderizado de texto
- Las herramientas de investigación en seguridad (paneles, visores hexadecimales, inspectores de paquetes) se benefician de interfaces ricas sin aprender una API de widgets propietaria
- El diseñador visual exporta plantillas HTML que funcionan tanto en la app NeverC como en un navegador independiente

---

## 3. IDE & Language Tooling (`neverc-ide`)

NeverC will provide first-class IDE support for the `.nc` language extension — a VSCode extension for immediate productivity and a standalone NeverC IDE for a fully integrated development experience.

### VSCode Extension

- **Syntax highlighting** — full `.nc` grammar with semantic token support for NeverC-specific types (`string`, `u8`–`u64`, `i8`–`i64`, `f32`, `f64`)
- **IntelliSense** — auto-completion for built-in types, dot-call methods (`.c_str()`, `.len()`, `.starts_with()`), and `#include` paths
- **Diagnostics** — real-time error and warning display from `neverc` compiler output
- **Go to definition** — jump to function, struct, and macro definitions across translation units
- **Hover documentation** — inline docs for built-in functions, compiler intrinsics, and standard library packages
- **Code actions** — quick-fix suggestions for common errors, auto-import for `std` packages
- **Debugging** — integrated LLDB/GDB debug adapter with breakpoint, step, and variable inspection support
- **Shellcode mode** — syntax-aware features for `-fshellcode` pipelines: bad-byte highlighting, shellcode size display, target-specific completions
- **Plugin API integration** — plugin hook point visualization and scaffolding

### Standalone IDE

- **Built on NeverC UI (`neverc-ui`)** — the IDE is itself a showcase of the HTML/JS/CSS component library, dogfooding the UI framework
- **Integrated terminal** — build, run, and debug without leaving the IDE
- **Visual shellcode pipeline** — graphical view of the IR → MIR → extraction pipeline with pass-by-pass output inspection
- **Project templates** — one-click scaffolding for hosted binaries, shellcode, EVM contracts, and Solana programs
- **AI-assisted coding** — built-in LLM integration that understands NeverC semantics, generates `.nc` code, and explains compiler diagnostics
- **Cross-compilation dashboard** — visual target selector with platform matrix and build status

### Why Both VSCode and Standalone?

- VSCode captures the majority of developers who already live in that ecosystem
- The standalone IDE provides a deeper, purpose-built experience for security researchers who want shellcode pipeline visualization and integrated binary analysis
- Both share the same language server backend — improvements benefit both simultaneously

---


## 4. Backend EVM para contratos inteligentes

NeverC soportará la compilación de código fuente C a bytecode EVM (Ethereum Virtual Machine) — permitiendo a los desarrolladores escribir contratos inteligentes en C en lugar de Solidity.

### Objetivos

- **Nuevo backend LLVM** — triple objetivo `evm` (ej.: `neverc --target=evm hello.c -o contract.bin`)
- **Compatibilidad ABI** — generación de descriptores ABI compatibles con Solidity para interactuar con herramientas Ethereum (Hardhat, Foundry, ethers.js)
- **Diseño de almacenamiento** — mapeo de structs C a slots de almacenamiento EVM con disposición determinista
- **Primitivas EVM integradas** — `msg.sender`, `msg.value`, `block.number`, `tx.origin` como variables integradas o intrínsecos
- **Modificadores payable / view / pure** — atributos de función mapeados a semánticas de visibilidad de Solidity
- **Emisión de eventos** — generación de opcodes `LOG0`–`LOG4` desde llamadas a funciones anotadas
- **Optimización de gas** — pases IR que minimizan el coste de gas (planificación de pila, plegado de constantes, eliminación de almacenamiento muerto)
- **revert / require** — primitivas de manejo de errores con mensajes personalizados

### ¿Por qué C para EVM?

- La sintaxis de Solidity es familiar para desarrolladores JavaScript pero ajena a programadores de sistemas; C es universal
- El pipeline de optimización IR existente de NeverC puede producir bytecode más compacto que `solc` en muchos casos
- Los investigadores de seguridad ya piensan en C — escribir herramientas de auditoría y fuzzers en C para contratos C es natural
- La API de plugins permite pases personalizados de análisis de gas y detección de vulnerabilidades en tiempo de compilación

---

## 5. Backend Solana eBPF

NeverC soportará la compilación de código fuente C a bytecode eBPF de Solana — habilitando el desarrollo de programas on-chain en C.

### Objetivos

- **Objetivo eBPF** — triple objetivo `sbf` (Solana BPF) (ej.: `neverc --target=sbf-solana hello.c -o program.so`)
- **Bindings de runtime Solana** — cabeceras integradas para llamadas al sistema Solana: `sol_invoke_signed`, `sol_log`, `sol_memcpy`, structs de información de cuenta
- **Modelo de cuentas** — overlays de structs C sobre datos de cuentas Solana con serialización/deserialización automática
- **CPI (Cross-Program Invocation)** — wrappers type-safe para llamar a otros programas on-chain
- **PDA (Program Derived Address)** — funciones integradas para derivación y verificación de PDA
- **Conciencia del presupuesto de cómputo** — advertencias del compilador cuando las unidades de cómputo estimadas exceden los límites del programa
- **Compatibilidad con Anchor** — generación de IDL opcional para interoperabilidad con frontends basados en Anchor

### ¿Por qué C para Solana?

- El runtime de Solana ya ejecuta eBPF — C es el lenguaje fuente más natural para objetivos BPF
- Las cadenas de herramientas C-BPF existentes (clang + solana-bpf) requieren configuración compleja; NeverC empaqueta todo en un solo binario
- Los programas críticos en rendimiento se benefician de la abstracción sin sobrecarga de C y los pases de optimización de NeverC
- La experiencia de compilación de shellcode (independiente de posición, runtime mínimo) se aplica directamente a las restricciones de programas on-chain

---

## Cronograma

Estas características están en fase de investigación y diseño. No se comprometen fechas de lanzamiento específicas. El progreso se actualizará en este documento y se anunciará en la página de versiones del proyecto.

| Característica | Estado |
|----------------|--------|
| Biblioteca estándar (`std`) | Investigación / Diseño |
| Biblioteca de componentes UI (`neverc-ui`) | Investigación / Diseño |
| IDE y herramientas de lenguaje (`neverc-ide`) | Investigación / Diseño |
| Backend EVM para contratos inteligentes | Investigación / Diseño |
| Backend Solana eBPF | Investigación / Diseño |
