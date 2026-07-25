**Idiomas**: [English](dyncode.md) | [简体中文](dyncode.zh-CN.md) | [繁體中文](dyncode.zh-TW.md) | [日本語](dyncode.ja.md) | [한국어](dyncode.ko.md) | [Français](dyncode.fr.md) | [Deutsch](dyncode.de.md) | [Español](dyncode.es.md) | [Italiano](dyncode.it.md) | [Русский](dyncode.ru.md) | [العربية](dyncode.ar.md)

[← ABI de complementos de NeverC](README.es.md)

# Plugins de DynCode

`-fdyncode` compila una unidad de traducción en una imagen plana e independiente
de la posición (`.bin`) cuyo código no tiene reubicaciones ni sección de datos.
Apunta a arm64/x86_64 en macOS, Linux, Android y Windows, con nivel de ejecución
de usuario o de núcleo. Los plugins observan, interceptan o sustituyen las fases
tipadas que convierten C en esa imagen mediante la misma ABI de C puro que usan
los demás dominios: sin objetos C++ de LLVM, sin tipos de la STL, sin excepciones
y sin punteros del anfitrión cuya duración no declare una tabla de la API.

## Interfaces

```c
#include "neverc/Plugin/PluginDynCode.h"
```

| Interfaz | Tabla | Ranuras | Propósito |
|---|---|--:|---|
| `NEVERC_INTERFACE_DYNCODE_{HIGH,LOW}` | `NevercDynCodeAPI` | 16 | Leer la petición, la imagen, el informe y los mapas de secciones/símbolos/reubicaciones/externos |
| `NEVERC_INTERFACE_DYNCODE_REGISTRAR_{HIGH,LOW}` | `NevercDynCodeRegistrarAPI` | 5 | `RegisterTarget`, `RegisterImportProvider`, `RegisterExtractor`, `RegisterCharsetEncoder`, `RegisterBinaryVerifier` |
| `NEVERC_INTERFACE_DYNCODE_PHASE_{HIGH,LOW}` | `NevercDynCodePhaseAPI` | 4 | `GetPhaseInfo`, `GetRequest`, `GetImage`, `GetReport` |

Las tres son `NEVERC_INTERFACE_STABLE` en la mayor 1. Dentro de un callback de
fase, `NevercDynCodePhaseAPI` es el punto de entrada: convierte el marco en los
handles que consume la otra tabla:

```c
NevercDynCodeRequestHandle Request;
Phase->GetRequest(Phase->Context, Frame, Frame->Input, &Request);

NevercDynCodeRequestInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_DYNCODE_API_MAJOR,
                                     NEVERC_DYNCODE_API_MINOR, 0};
DynCode->GetRequestInfo(DynCode->Context, Task, Request, &Info);
```

Las cuatro familias de mapas —mapas de secciones, mapas de símbolos,
reubicaciones y referencias externas— se recorren todas con el mismo trío
first/next/info, por ejemplo `GetFirstRelocation`, `GetNextRelocation`,
`GetRelocationInfo`. Así lee un plugin lo que decidió la extracción sin analizar
el JSON del informe.

## DynCode es un producto de compilación, no un paso posterior a `main()`

`-fdyncode` es una acción/trabajo normal del DAG del driver. El trabajo de
compilación publica un `ObjectGraph` verificado en memoria; un trabajo
`-dyncode-extract` consume ese grafo y escribe la imagen `-o` del usuario.
`-###`, la impresión de fases y el grafo de trabajos muestran todos el trabajo de
extracción, así que un plugin nunca tiene que reconstruir un argv reescrito para
descubrir el modo. La petición congelada se comparte de forma local a la tarea
con la generación de código en proceso; no hay `getCurrentDynCodeOptions()`, ni
indicador de modo global al proceso, ni ida y vuelta por un objeto temporal.

Exactamente una unidad de traducción se rebaja a una imagen. Las entradas
múltiples, `-c/-S/-E` y los triples no admitidos se rechazan de entrada con
diagnósticos estables.

## Niveles de compatibilidad

Los identificadores de fase, los de artefacto, los contenedores de
petición/informe/imagen y los contratos de callback son ABI STABLE de la primera
versión. Las clases de reubicación específicas del objetivo y los esquemas de
secciones/símbolos de los formatos de objeto son LOCKSTEP: compare el
identificador de esquema del objetivo y su resumen antes de consumirlos. NeverC
rechaza un esquema no coincidente antes de invocar a un proveedor.

## La petición congelada

Al inicio del trabajo el driver normaliza la línea de órdenes en un
`DynCodeRequest` inmutable y lo congela. Las tareas hijas toman prestada la
instantánea; nunca la mutan. La petición lleva la clave del objetivo y el formato
de objeto, el nivel de ejecución (user/kernel), la política de entrada (símbolo
explícito, lista de candidatos por defecto, exigencia de entrada en cero), la
política de PIC/secciones, la política de referencias externas, el conjunto o
perfil de bytes prohibidos y el indicador de reescritura, el identificador del
proveedor de juego de caracteres, y la longitud máxima, la alineación y el byte
de relleno.

## El grafo de fases tipadas

DynCode es un grafo fijo de 34 fases. Treinta transiciones ordinarias son
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`; cuatro son
`OBSERVABLE | SEALED_HOST_GATE`. Las puertas selladas son la verificación final
de IR, la verificación final de MIR, la verificación de la imagen y la
confirmación. Un plugin puede observar cualquier fase, envolver una transición
sustituible con un interceptor o sustituir su proveedor por completo; nunca puede
sustituir, omitir ni sortear una puerta sellada, y no puede expresar una
transformación desactivada como un callback omitido: una transformación
desactivada ejecuta un proveedor no-op explícito cuya salida equivalente el
verificador del anfitrión sigue demostrando.

Las fases, en orden, son:

1. congelación de la petición;
2. las transformaciones de IR: preparación, rebaje de saltos indirectos, rebaje
   de intrínsecos de memoria (antes y después del montón), rebaje del tiempo de
   ejecución de cadenas, arena de montón, tres posiciones de `compiler_rt`
   (pre/post/final), rebaje de importaciones de syscall/PEB/núcleo, dos
   posiciones de `data_to_text` (pre/post), optimización de inlining,
   finalización de cadenas, stackify, all-`blr`, y la verificación final sellada
   de IR;
3. la transformación de preparación de MIR y la verificación final sellada de
   MIR;
4. importación de objeto: ligar el `ObjectGraph` verificado a la tarea;
5. extracción: plan, disposición, reubicación y construcción de la imagen
   candidata;
6. las fases binarias acotadas: post-extracción, reescritura de bytes
   prohibidos, codificación de juego de caracteres, tamaño/alineación/relleno y
   preverificación;
7. la verificación sellada de la imagen;
8. la confirmación sellada.

La fuente normativa de los identificadores, políticas, niveles de estabilidad y
puertas es [`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`]; el contrato
de cobertura ejecutable es [`docs/plugin-api/coverage.json`].

## Las transformaciones incorporadas también son proveedores

Cada pase de IR/MIR incorporado se envuelve como proveedor tipado; el objeto de
pase de LLVM nunca se expone a través de la ABI de C. Sustituir una fase
significa que el proveedor incorporado no se ejecuta: la prueba que pasa
demuestra el comportamiento o la traza, no meramente que una registración tuvo
éxito. Las fases `mem_intrin`, `compiler_rt` y `data_to_text` aparecen en más de
una posición; cada posición es un identificador de fase distinto con su propia
demostración, así que reejecutar es idempotente y nunca depende de estado oculto
del pase.

## ObjectGraph es la única entrada de objeto ordinaria

La extracción consume exactamente un `ObjectGraph` verificado producido por la
ruta de generación de código del objetivo. `dyncode.object.import` liga ese grafo
y comprueba la clave de objetivo y la procedencia; nunca vuelve a leer bytes del
disco ni ejecuta un segundo análisis de objeto. Un formato de objeto propio entra
en DynCode en cuanto puede leerse como `ObjectGraph` y tiene proveedores de
reubicación y de objetivo acordes. Los objetos múltiples y los conjuntos de
grafos de LTO se rechazan en la congelación con un `CAPABILITY_UNAVAILABLE`
estable.

## Referencias externas y rebaje de importaciones

El conjunto de externos permitidos de la petición solo significa «un proveedor
puede encargarse de esto»; nunca permite que una reubicación sin resolver
sobreviva hasta la imagen plana. Toda referencia externa debe acabar como una de
estas: eliminada en IR/MIR, resuelta a un símbolo dentro de la imagen, convertida
en un contrato de resolutor en tiempo de ejecución declarado y aceptado por el
verificador, o un error duro. El stub de syscall, la importación de PEB y la
importación de núcleo son los tres `ImportProvider` incorporados; cada uno
declara su comparador de objetivo/nivel/símbolo y el contrato de ABI que produce.
Un plugin puede añadir un `ImportProvider`, pero debe devolver la procedencia de
sustitución, el cambio en la ABI de entrada, los parámetros del resolutor y las
referencias residuales.

## Imagen, informe y ediciones de bytes acotadas

La extracción produce un `DynCodeImage` y un `DynCodeReport`. La imagen es un
constructor de bytes acotado más el desplazamiento/símbolo de entrada, los mapas
de salida de secciones y símbolos de origen, las disposiciones de reubicación y
los registros de contratos externos/de tiempo de ejecución. Toda edición de bytes
pasa por la API comprobada de read/write/insert/append/resize del constructor; no
hay ningún `uint8_t **`. Una edición actualiza la generación de la imagen e
invalida cualquier demostración de reubicación/PIC/entrada que se solape con el
rango cambiado.

El informe es un producto de auditoría inmutable y determinista: resúmenes de
petición/ruta/entrada/salida, el diario de proveedores por fase, las secciones
seleccionadas y rechazadas con su motivo, la elección de entrada, las
reubicaciones parcheadas/rechazadas/con contrato en ejecución, los externos
restantes, tamaño/alineación/relleno, el escaneo de bytes prohibidos y la lista
de comprobación del verificador. `-fdyncode-report=<path>` escribe su JSON
canónico; los diagnósticos detallados se renderizan a partir de ese mismo informe
en vez de un segundo juego de recuentos.

La cadena de reescritura de bytes prohibidos se ejecuta en un orden topológico
congelado y cada paso devuelve un registro de cambios. El codificador de juego de
caracteres se selecciona por identificador estable exacto y devuelve un stub
decodificador, la carga codificada, una actualización de la entrada y una
demostración de objetivo; un identificador desconocido o ambiguo es un error
duro. Desactivar la reescritura selecciona un paso no-op explícito: la auditoría
final se ejecuta igualmente.

## Verificador final y momento posterior a la finalización

Todas las fases escribibles terminan antes del verificador final sellado. El
verificador comprueba que no queda ninguna reubicación o referencia externa sin
tratar, que no hay ninguna sección prohibida de datos/TLS/desenrollado/depuración/
metadatos, que la entrada existe y está bien alineada y (cuando se exige) en el
desplazamiento cero, que cada punto de reubicación cae dentro de rango con una
demostración PIC acorde a los bytes actuales de la imagen, que los mapas de
secciones y símbolos no se solapan, que se cumplen las reglas de
longitud/alineación/relleno, y que los bytes finales —incluidos decodificador,
cabecera y relleno— no contienen ningún byte prohibido. Cualquier fallo devuelve
un diagnóstico estructurado y descarta todo el paquete de salida.

Después de la auditoría no hay ningún gancho escribible. Si una transformación de
bytes toca un rango ejecutable, la ruta congelada debe aportar una capacidad de
verificación binaria acorde que el anfitrión invoca para reemitir la demostración
PIC sobre la imagen final e inmutable.

## Opciones del driver

`-fdyncode` habilita el modo. `-fdyncode-entry=` elige el símbolo de entrada.
`-fdyncode-bad-bytes=` / `-fdyncode-bad-byte-profile=` fijan los bytes
prohibidos, `-fdyncode-bad-byte-rewrite` (activo por defecto) selecciona la
cadena de reescritura, y `-fdyncode-charset=` selecciona un codificador
registrado. `-fdyncode-max-length=`, `-fdyncode-align=` y `-fdyncode-pad=`
acotan el tamaño final. `-fdyncode-keep-obj=` deriva el objeto reubicable
intermedio y `-fdyncode-report=` escribe el informe de auditoría.
`-mdyncode-context=user|kernel` selecciona el nivel de ejecución.

## Reglas de concurrencia y de fallo

- Mantenga el estado mutable en los ámbitos de proceso/sesión/tarea que
  proporciona el anfitrión; nunca use un singleton de plugin actual ni de
  opciones actuales.
- No guarde en caché handles de tarea ni vistas prestadas después de que un
  callback retorne.
- Invoque la continuación de un interceptor como máximo una vez, en el hilo del
  callback.
- Devuelva el `NevercStatus` original; un `REPLACE` declarado que falla no
  retrocede en silencio al proveedor incorporado.
- Declare los modelos de concurrencia y reentrada más estrechos que sean
  ciertos.

Vea [`pluginsdk/examples/DynCodeTracePlugin.c`] para un trazador de fases de solo
lectura y [`pluginsdk/examples/DynCodeEncoderPlugin.c`] para un codificador de
juego de caracteres.

<!-- reference links -->
[`docs/plugin-api/coverage.json`]: coverage.json
[`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`pluginsdk/examples/DynCodeEncoderPlugin.c`]: ../../pluginsdk/examples/DynCodeEncoderPlugin.c
[`pluginsdk/examples/DynCodeTracePlugin.c`]: ../../pluginsdk/examples/DynCodeTracePlugin.c
