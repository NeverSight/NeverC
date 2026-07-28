**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Ejemplo de controlador de kernel Windows

Un controlador de kernel WDM mínimo construido con NeverC. Apunta a **x64** por
defecto y también puede compilarse para ARM64. Compilación cruzada desde macOS / Linux.

NeverC es un compilador todo-en-uno — una sola invocación maneja preprocesamiento,
compilación, optimización (auto-LTO) y enlazado a través del enlazador integrado.

## Compilación

Desde el repositorio:

```bash
cd examples/windows-driver
neverc make
```

Esto genera `ExampleDriver-x64.sys`. Para compilar para ARM64, o para ambas:

```bash
neverc make ARCH=arm64
neverc make all-arch
```

Desde una versión independiente de NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

La salida es `ExampleDriver-<arq>.sys` (optimizado con auto-LTO).
La compilación por defecto incluye `-g` para depuración; **las versiones de
producción deben eliminar `-g`** para quitar los símbolos de depuración y reducir
el tamaño del binario (~38 KB → ~3 KB).

## Compilación manual (sin Make)

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

Para ARM64, cambie el destino a `aarch64-pc-windows-msvc`; nada más cambia.
`-fms-kernel` selecciona los encabezados y bibliotecas de importación del WDK
correspondientes al destino y define las macros de arquitectura que el WDK
espera, por lo que nunca hay que pasarlas a mano.
`--driver` marca la imagen como modo kernel: el código y los datos pasan a ser
no paginables, las tablas de importación se mueven a la sección descartable
INIT y el enlazador escribe la suma de comprobación PE que el cargador verifica.

> `-g` incrusta información de depuración DWARF en el PE; inspecciónela con
> `llvm-dwarfdump`. Omita esta opción en versiones de producción para reducir
> el tamaño del binario.

## Firma de prueba

Windows se niega a cargar un controlador de kernel sin firmar. `-ftest-sign`
adjunta una firma Authenticode para que la imagen supere esa comprobación en una
máquina de prueba:

```bash
neverc make TESTSIGN=1
neverc make ARCH=arm64 TESTSIGN=1
```

o añada `-ftest-sign` a una invocación manual. Solo se acepta junto con
`-fms-kernel`, ya que una firma de prueba no significa nada para un binario en
modo usuario.

La identidad de firma está integrada en el compilador: un certificado
autofirmado cuya clave privada es pública por construcción. No aporta
autenticidad alguna; solo satisface la comprobación de integridad de código en
una máquina que usted ha abierto deliberadamente. Configure esa máquina una vez,
como administrador:

```cmd
bcdedit /set testsigning on
certutil -addstore Root neverc-test-signing.cer
certutil -addstore TrustedPublisher neverc-test-signing.cer
```

y luego reinicie. Exporte el certificado desde el propio compilador, así siempre
coincide con la identidad con la que firma:

```bash
neverc --print-test-sign-cert > neverc-test-signing.cer
```

(También hay una copia en `utils/neverc-test-signing.cer` en el árbol de
fuentes, pero no forma parte de un paquete de release.)

Sin una máquina Windows, compruebe la firma con `osslsigncode`. Tenga en cuenta
que `-CAfile` espera PEM mientras que el certificado es DER: conviértalo primero.
Pasar el DER directamente falla con un confuso «signature verification failed»
cuya causa real es «no certificate found»:

```bash
openssl x509 -inform DER -in neverc-test-signing.cer -out nc.pem
osslsigncode verify -CAfile nc.pem ExampleDriver-x64.sys
```

**Nunca lo utilice para nada que salga de una máquina de prueba.** En
producción, firme con un certificado de firma de código real (y, para Windows 10
1607 y posteriores, una firma de certificación del Microsoft Hardware Dev
Center).

## Funcionalidades

- Crea un objeto de dispositivo en `\Device\ExampleDriver`
- Crea un enlace simbólico en `\DosDevices\ExampleDriver`
- Maneja `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Imprime mensajes de carga/descarga a través de `DbgPrint`

## Carga (en una máquina de prueba Windows)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Active la firma de prueba o utilice un certificado de firma de código para producción.
