**Idiomas**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

# API MIR de complementos de NeverC

La primera ABI pública de complementos expone Machine IR a través de
`PluginMIR.h`. La API usa identificadores C estables y manejadores opacos; los
complementos no dependen de la disposición de clases de LLVM, de números de
enumeración ni de la ABI de C++.

## Negociación

Consulte `NEVERC_INTERFACE_MIR` para `NevercMIRAPI` y
`NEVERC_INTERFACE_MIR_PASS` para `NevercMIRPassAPI`. Compruebe el tamaño de tabla
devuelto antes de usar un puntero a función e ignore los campos añadidos por un
anfitrión más reciente.

El resumen (digest) del esquema identifica la correspondencia exacta entre los
valores estables y el anfitrión. `GetEntityInfo`, `GetOperandKindInfo`,
`GetGenericOpcodeInfo` y `GetMachinePropertyInfo` exponen los nombres canónicos e
indican si una operación necesita un esquema de destino.

## Modelo estable

Los manejadores opacos representan:

- funciones máquina y bloques básicos;
- instrucciones máquina y operandos;
- transacciones de mutación;
- resultados de análisis;
- entradas del pool de constantes, objetos de marco, tablas de salto, operandos
  de memoria y referencias de destino.

Un manejador pertenece a una única tarea de generación de código. Las entidades
borradas, las revertidas y los resultados de análisis invalidados por una
mutación quedan obsoletos.

El esquema genérico cubre códigos de operación independientes del destino,
géneros de operandos, propiedades de máquina, tipos de bajo nivel, banderas de
instrucción, asignaciones de registros, objetos de marco, constantes, tablas de
salto, formas de puntero de memoria y órdenes atómicos. Los códigos de operación
específicos del destino requieren un esquema de destino negociado explícitamente.

## Leer la MIR

`NevercMIRAPI` admite:

- propiedades de funciones máquina y recorrido de bloques;
- enumeración de predecesores, sucesores, live-in, instrucciones y operandos;
- consultas de código de operación y banderas de instrucción;
- todas las formas públicas de operando de máquina;
- información de registros virtuales y físicos;
- estado de marcos, pool de constantes, tablas de salto y operandos de memoria.

Use pares de conteo y consulta junto con búferes de salida acotados. Salvo que se
documente lo contrario, las vistas devueltas se prestan para la devolución de
llamada actual.

## Mutación transaccional

Los cambios de MIR ocurren bajo un arrendamiento de mutación:

1. `BeginMutation` para una función máquina.
2. Crear, mover o borrar bloques e instrucciones.
3. Añadir o actualizar operandos y aristas del CFG.
4. Aplicar cambios de propiedades de máquina con la prueba requerida.
5. `CommitMutation` o `AbortMutation`.

La confirmación realiza una comprobación estructural previa y la verificación de
Machine IR. Los operandos, el CFG, el uso de códigos genéricos o las afirmaciones
de propiedades inválidos se revierten atómicamente. El aborto restaura el orden de
los bloques, las instrucciones, los operandos, las aristas del CFG y las
propiedades de máquina.

Los cambios de propiedades usan `NevercMIRPropertyProof`. Una prueba debe o bien
invalidar una propiedad cuyas suposiciones ya no se cumplen, o bien solicitar una
comprobación estructural antes de establecerla.

## Pases y fases

`NevercMIRPassDescriptor.Level` admite los adaptadores MachineModule,
MachineFunction y MachineBasicBlock. Los enganches estables son:

- tras la selección de instrucciones;
- tras la legalización;
- antes y después del planificador;
- antes y después de la asignación de registros;
- tras el prólogo/epílogo;
- pre-emit;
- la ranura final para complementos.

Los pases de función pueden ejecutarse en particiones de generación de código
paralelas. Los pases de nivel de módulo se ejecutan en barreras de canalización
serializadas. Las declaraciones de concurrencia y reentrada del complemento
siguen vigentes.

Toda canalización de generación de código termina con un `MachineVerifier`
propiedad del anfitrión, después de la ranura final para complementos. Es una
compuerta sellada y un complemento no puede desactivarla.

## Análisis

La tabla de análisis expone variables vivas, intervalos de vida, índices de
ranura, árbol de dominadores, información de bucles y presión de registros. La
disponibilidad depende del enganche elegido, porque algunos análisis de LLVM no
existen antes o después de su etapa nativa de canalización.

Declare los análisis requeridos y preservados en el descriptor del pase. Una
mutación confirmada invalida los manejadores de resultados afectados. Afirmar que
se preserva todo tras una mutación se rechaza.

## Ejemplo mínimo

`pluginsdk/examples/MachinePass.c` registra un pase de función máquina de solo
lectura en el enganche estable pre-emit.

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

Use el sufijo de módulo que CMake produce para la plataforma.

## Requisitos de seguridad

- No conserve manejadores de tarea, manejadores MIR ni vistas prestadas después de
  una devolución de llamada.
- No fabrique valores de manejador ni números de código de operación de LLVM.
- No mute fuera de un arrendamiento.
- Inicialice las cabeceras de tabla y el almacenamiento reservado.
- Devuelva estados a través de la frontera C; nunca deje que una excepción de C++
  la cruce.

Consulte `PluginMIR.h`, `MIRSchema.json`, `PluginPhaseSchema.h` y
`coverage.json` para las declaraciones normativas y las pruebas de cobertura.
