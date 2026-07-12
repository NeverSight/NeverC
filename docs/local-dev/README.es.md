**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Índice de documentación](../README.es.md)

# Desarrollo local

Guía para compilar NeverC desde el código fuente y configurar un entorno de desarrollo local.

---

## Requisitos previos

- CMake 3.20+
- Ninja
- Un compilador C++17 del host (GCC, Clang o MSVC)

---

## Compilación

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` se detecta y activa automáticamente si está presente.

`--target neverc` es la compilación diaria stage-1 (runtimes embebidos vacíos)
y basta para la mayoría del trabajo local. Si necesita string / mimalloc / std /
NVK dentro del binario (o un compilador alineado con CI), ejecute el objetivo
paraguas stage-2:

```bash
cmake --build build-neverc --target neverc-embed-runtime-bitcode
```

El bootstrap en dos etapas se detalla en [Builtins](../builtins/README.es.md).

### Compilación con pruebas

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

`check-neverc` depende de `neverc-embed-runtime-bitcode`, así que la primera
ejecución de pruebas hace bootstrap y reenlace automáticamente. No hace falta
invocar el objetivo embed a mano.

---

## Configuración del PATH (macOS / Linux)

Tras la compilación, el binario `neverc` se encuentra en `build-neverc/bin/neverc`. Use el script auxiliar para añadirlo al `PATH` sin tener que escribir la ruta completa cada vez:

```bash
source ./tools/neverc-env.sh
```

Ahora puede ejecutar `neverc` directamente:

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### Eliminar del PATH

Para retirar la compilación local del `PATH` en la sesión de shell actual:

```bash
source ./tools/neverc-env.sh --remove   # o -r
```

### Configuración permanente

Escribir automáticamente la línea `source` en el archivo rc del shell (`~/.zshrc`, `~/.bashrc` o `~/.profile`):

```bash
source ./tools/neverc-env.sh --install
```

Deshacer:

```bash
source ./tools/neverc-env.sh --uninstall
```

---

## Windows (CMD)

En Windows, utilice el script `.bat` (no requiere privilegios de administrador):

```cmd
tools\neverc-env.bat             &REM añadir al PATH (sesión actual)
tools\neverc-env.bat --remove    &REM eliminar del PATH (sesión actual)
tools\neverc-env.bat --global    &REM persistir en el PATH de usuario vía setx
tools\neverc-env.bat --global -r &REM eliminar del PATH de usuario vía setx
```

A diferencia del script Unix, no se necesita `source` — el `.bat` modifica directamente la sesión `cmd` actual. `--global` escribe en el registro de usuario mediante `setx` (no requiere privilegios de administrador).

---

## Binarios macOS precompilados

La versión está firmada con un certificado Apple Developer ID y notarizada por Apple. Extraiga el archivo y úselo directamente.

---

## Compilación cruzada a Windows

NeverC incluye los SDK de cada plataforma en `runtime/` (Windows SDK/WDK, sysroot de Linux, sysroot de macOS, Android NDK); no se necesita configuración externa.

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Para dyncode de Windows (`-fdyncode`, resolución de importaciones PEB, etc.), consulte la [documentación del compilador dyncode](../dyncode-compiler/README.es.md).

---

## Verificación

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```
