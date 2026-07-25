**Idiomas**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

# API IR de complementos de NeverC

La primera ABI pública de complementos expone el IR de LLVM mediante tablas C
estables. Los complementos no incluyen cabeceras de LLVM y no deben convertir
manejadores de NeverC en objetos de LLVM.

## Interfaces

Consulte las interfaces desde `neverc_plugin_entry` con
`NevercBootstrapAPI.QueryInterface`:

- `NEVERC_INTERFACE_IR_CORE` — consultas de módulos, tipos, valores, CFG,
  metadatos, atributos, constantes y serialización.
- `NEVERC_INTERFACE_IR_BUILDER` — construcción y mutación transaccionales del IR.
- `NEVERC_INTERFACE_IR_ANALYSIS` — análisis integrados y definidos por
  complementos.
- `NEVERC_INTERFACE_IR_PASS` — pases de Module, CGSCC, Function y Loop.
- `NEVERC_INTERFACE_IR_GEN` — reemplazo del descenso de SemanticUnit a IR.
- `NEVERC_INTERFACE_IR_OPTIMIZATION` — reemplazo completo de la canalización de
  optimización.

Solicite siempre el par mayor/menor de la cabecera y compruebe que el
`StructSize` devuelto alcanza el último puntero a función que usa el complemento.
Un anfitrión más reciente puede añadir campos; el complemento debe ignorar las
colas desconocidas.

## Manejadores y propiedad

Los manejadores de IR son pares opacos `{Owner, Value}` acotados a una tarea. El
anfitrión es dueño de todos los objetos a los que hacen referencia.

- Nunca conserve un manejador de ámbito de tarea después de que termine su
  devolución de llamada o su tarea.
- Nunca use un manejador en otra sesión u otra tarea.
- Un reemplazo confirmado invalida los manejadores de los objetos reemplazados.
- Una mutación abortada deja obsoletos los manejadores que creó.
- Las API informan de `NEVERC_STATUS_STALE_HANDLE`, `WRONG_OWNER` o `WRONG_TYPE`
  en lugar de exponer un puntero de LLVM.

Las cadenas y vistas de bytes que devuelven las consultas son prestadas, salvo
que una API devuelva explícitamente un búfer liberable.

## Leer el IR

`NevercIRCoreAPI` proporciona:

- identificador de módulo, triple, disposición de datos y ensamblador en línea;
- cursores de valores estables para funciones, globales, bloques, instrucciones,
  usos y operandos;
- identificadores estables de tipos y códigos de operación;
- propiedades de funciones, globales, instrucciones, metadatos y atributos;
- constantes enteras, de coma flotante, agregadas, nulas, poison y undef;
- exportación e importación de bitcode y artefactos de módulo verificados.

Los cursores de colección están acotados: pase una capacidad de salida y repita la
recolección hasta que el número devuelto sea cero.

## Mutación transaccional

Toda mutación estructural usa `NevercIRBuilderAPI`:

1. Iniciar una mutación de módulo o de función.
2. Crear un constructor ligado a esa mutación.
3. Fijar el punto de inserción y construir instrucciones, funciones o bloques.
4. Confirmar la mutación.
5. Destruir los constructores y el manejador de mutación.

La confirmación verifica el IR candidato y lo publica atómicamente. Si el
verificador falla, el anfitrión revierte la mutación y conserva el módulo
anterior. `AbortMutation` siempre revierte los cambios preparados.

No declare `NEVERC_IR_PRESERVE_ALL` después de cambiar el IR. El adaptador de
pases comprueba la generación del módulo y rechaza una declaración de
preservación incoherente.

## Niveles de pase y fases

`NevercIRPassDescriptor.Level` admite:

- `NEVERC_IR_PASS_LEVEL_MODULE`
- `NEVERC_IR_PASS_LEVEL_CGSCC`
- `NEVERC_IR_PASS_LEVEL_FUNCTION`
- `NEVERC_IR_PASS_LEVEL_LOOP`

Las fases de inserción estables son `PRE_OPT`, `PIPELINE_START`,
`OPTIMIZER_LAST`, `POST_OPT` y `PRE_CODEGEN`. La invocación contiene solo los
manejadores válidos para su nivel. Los pases de función y de bucle pueden
ejecutarse de forma concurrente, así que el estado mutable del complemento debe
respetar el contrato de concurrencia declarado.

El anfitrión siempre ejecuta el verificador de IR sellado final. Un complemento no
puede reemplazar, interceptar ni omitir esa compuerta.

## Análisis

Los identificadores de análisis integrados cubren el grafo de llamadas, el árbol
de dominadores, el árbol de posdominadores, la información de bucles, la evolución
escalar, MemorySSA y el análisis de alias.

Los análisis de complementos declaran dependencias y devoluciones de llamada de
ciclo de vida. Los resultados se almacenan en caché por invocación y se invalidan
según el resultado de preservación del pase. Se rechazan los ciclos recursivos de
dependencias y las mutaciones desde una devolución de llamada de análisis.

## Proveedores completos

Un proveedor de generación de IR puede reemplazar el descenso integrado y publicar
un artefacto de módulo verificado. Un proveedor de optimización puede reemplazar
toda la canalización de optimización integrada. Ambas rutas:

- consumen una entrada de fase explícita;
- publican a través de una API del anfitrión en lugar de devolver un puntero de
  LLVM;
- verifican la compatibilidad del destino y la validez del módulo;
- conservan atómicamente el módulo antiguo si la publicación falla.

El verificador final sigue siendo obligatorio después de un proveedor de
optimización.

## Ejemplo mínimo

`pluginsdk/examples/FunctionPass.c` es un pase de función de solo lectura.
`pluginsdk/examples/ExamplePlugin.c` muestra la enumeración de un módulo y
`pluginsdk/examples/CustomCallConvPlugin.c` demuestra atributos y propiedades de
sitios de llamada.

Compilar y cargar un ejemplo:

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

Use el sufijo de módulo que CMake produce para la plataforma.

## Reglas de fallo

Devuelva un `NevercStatus` desde cada devolución de llamada. Los fallos del
complemento se convierten en diagnósticos estructurados; no lance excepciones a
través de la frontera C. Inicialice cada cabecera de tabla de salida y cada campo
reservado, y devuelva `INVALID_ARGUMENT` si falta un puntero obligatorio.

Consulte `PluginIR.h`, `PluginPhaseSchema.h` y `coverage.json` para las
declaraciones normativas de la ABI, las políticas de fase y las pruebas de test.
