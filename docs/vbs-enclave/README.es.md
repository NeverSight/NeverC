**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Índice de documentación](../README.es.md) · [← Proyecto NeverC](../i18n/README.es.md)

# DLL de enclave VBS en Windows

NeverC puede enlazar DLL de enclave VBS compatibles con Microsoft para objetivos Windows de 64 bits. El contrato de enlazador admitido es:

```text
/DLL /INCREMENTAL:NO /INTEGRITYCHECK /ENCLAVE /GUARD:MIXED
```

Pase las opciones del enlazador de Microsoft a través del controlador de Windows con `-Xmslink` o `-Wl,`:

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

Este ejemplo selecciona explícitamente con `-l` las variantes para enclave de las bibliotecas CRT de MSVC y UCRT. Toda selección explícita de `-vctoolsdir` o `-winsysroot` conserva su prioridad habitual. Sin esas anulaciones, todo enlace con `/ENCLAVE` en macOS, Linux o Windows resuelve las bibliotecas de Windows únicamente desde el runtime de destino incluido con NeverC; el controlador no detecta automáticamente ni recurre como alternativa a un conjunto de herramientas de Visual Studio o un SDK de Windows instalados en el host.

## Compilaciones entre hosts con el runtime incluido

La compilación y el enlace COFF son independientes del host. El mismo comando puede ejecutarse en macOS, Linux o Windows después de instalar el runtime del objetivo:

```text
neverc runtime install windows-x64
neverc runtime install windows-arm64
```

El paquete del objetivo contiene los encabezados de Windows, el CRT de enclave, el UCRT de enclave, `vertdll.lib`, `bcrypt.lib` y las demás bibliotecas de importación de Windows requeridas. Cuando se usa el runtime incluido para la resolución, NeverC solo cambia de los directorios CRT/UCRT ordinarios incluidos a los directorios CRT/UCRT de enclave si se combina un `/ENCLAVE` explícito con un `/NODEFAULTLIB` global. En este modo, antes de enlazar, el controlador comprueba que existan los archivos incluidos `libcmt.lib`, `libvcruntime.lib`, `ucrt.lib`, `vertdll.lib` y `bcrypt.lib`. Las bibliotecas se siguen seleccionando explícitamente con `-l...`. `/ENCLAVE` por sí solo no habilita los directorios CRT/UCRT de enclave ni selecciona sus bibliotecas; se mantienen las rutas de búsqueda ordinarias del runtime incluido.

La etapa de enlace entre hosts produce la DLL de enclave sin firmar y sin procesar. El procesamiento con VEIID, la firma con SignTool y la carga real mediante `CreateEnclave`/`LoadEnclaveImage` siguen siendo exclusivos de Windows; por ello, traslade una DLL enlazada en macOS o Linux a una máquina Windows de empaquetado o pruebas para las tres etapas finales. Consulte [Runtimes de destino](../runtime/README.es.md) para conocer la instalación y detección del runtime.

## Entradas de imagen requeridas

Un enlace de enclave debe proporcionar estas dos definiciones de datos de imagen:

- `__enclave_config`, que contiene los datos `IMAGE_ENCLAVE_CONFIG` de la imagen.
- `_load_config_used`, con una estructura load-config lo bastante grande como para contener `EnclaveConfigurationPointer`.

NeverC mantiene vivo `__enclave_config` durante la eliminación de código muerto, lo extrae de un archivo si es necesario y verifica que el puntero load-config finalmente reubicado sea igual a la dirección virtual de ese objeto de configuración. Una definición ausente, absoluta, descartada, truncada o reubicada incorrectamente produce un error de enlace.

`/GUARD:MIXED` habilita la salida CFG para una mezcla de archivos objeto protegidos y heredados. Emite entradas GFID y GIAT de cinco bytes: un RVA de cuatro bytes seguido de un byte de metadatos, que es cero en los objetivos ordinarios actuales. Sus `GuardFlags` contienen los bits de CFG, delay-IAT protegido y tamaño de entrada. Los objetos heredados aportan destinos cuya dirección se toma mediante un análisis conservador de las reubicaciones, excluyendo los metadatos de unwind.
Cuando `/GUARD:MIXED` se combina con `/GUARD:EHCONT`, la tabla de destinos de continuación de EH también usa entradas de cinco bytes: un RVA de cuatro bytes seguido de un byte de metadatos con valor cero.

Una solicitud explícita de enlace incremental es incompatible con `/ENCLAVE` y se rechaza. Se utiliza la última opción `/INCREMENTAL` efectiva, incluidas las opciones procedentes de las directivas de los archivos objeto.

`/ENCLAVE` no selecciona implícitamente la salida DLL, CFG, la comprobación de integridad, las bibliotecas CRT de enclave, el procesamiento de VEIID ni la firma. Mantenga explícitas estas decisiones en el pipeline de compilación. En el modo de runtime incluido, las rutas de búsqueda CRT/UCRT de enclave y la validación de las cinco bibliotecas descritas anteriormente solo se activan con un `/NODEFAULTLIB` global explícito; sin esa opción, se mantienen las rutas ordinarias del runtime de Windows incluido. Las anulaciones explícitas de la cadena de herramientas del usuario conservan su prioridad habitual.

## Flujo de compilación y despliegue

1. Compile las fuentes sensibles para la seguridad con CFG habilitado, por ejemplo con `-fms-guard=cf`. Los objetos heredados pueden permanecer sin instrumentar cuando el enlace final usa `/GUARD:MIXED`.
2. Defina la configuración y el punto de entrada del enclave y, a continuación, enlace con CRT/UCRT de enclave y las bibliotecas de importación Vertdll y BCrypt requeridas.
3. Inspeccione la imagen PE sin firmar y verifique su directorio load-config, sus tablas CFG, el puntero de configuración del enclave y las reubicaciones de base.
4. En Windows, ejecute la herramienta VEIID del SDK de Windows sobre la imagen terminada.
5. En Windows, firme con SignTool la imagen procesada por VEIID. La firma debe ser la última modificación del archivo.
6. En el host Windows, compruebe `IsEnclaveTypeSupported(ENCLAVE_TYPE_VBS)`, asigne el enclave con `CreateEnclave`, cargue la DLL con `LoadEnclaveImage` y llame a `InitializeEnclave`.

En sistemas antitrampas, el enclave es apropiado para un pequeño componente de verificación o manejo de claves cuyo código y estado privado necesiten una frontera más fuerte respecto al proceso ordinario del juego. Mantenga estrecha la interfaz del enclave y valide todos los datos proporcionados por el host: este sigue controlando las entradas, la planificación, el almacenamiento y la disponibilidad. Un enclave VBS complementa la autoridad del servidor, la telemetría, las defensas del controlador y el endurecimiento habitual del proceso; no los reemplaza.

## Validación

El workflow `VBS enclave differential CI` se ejecuta en Windows. Su puerta estática:

- compila el enlazador de NeverC y las pruebas COFF específicas;
- crea DLL de enclave equivalentes enlazadas por Microsoft y por NeverC;
- compara la semántica pública de PE/load-config/CFG;
- ejecuta pruebas de mutación contra el verificador de PE;
- prepara imágenes procesadas por VEIID para una sonda de ejecución diferencial.

La sonda de ejecución ejecuta primero la imagen de Microsoft. Si el runner alojado carece de VBS o de un entorno de firma utilizable, el resultado se marca explícitamente como omisión debida al entorno. Una vez que la imagen de referencia de Microsoft se carga correctamente, el fallo de cualquiera de los candidatos de NeverC es un fallo de prueba estricto. Un runner VBS autoalojado y configurado puede hacer obligatorio el éxito en tiempo de ejecución.

El enlazador admite imágenes de enclave COFF x86-64 y ARM64. Valida el puntero de configuración publicado y después deriva, del conjunto final de importaciones DLL ordinarias, una secuencia contigua de entradas `IMAGE_ENCLAVE_IMPORT` de 80 bytes. Inicialmente las entradas solo contienen el nombre de importación y campos de identidad a cero para que VEIID los vincule; el enlazador escribe el recuento, la lista y el tamaño de entrada. Se rechazan las importaciones de carga diferida activas. El enlazador no impone políticas adicionales sobre los campos versionados de `IMAGE_ENCLAVE_CONFIG`.
