**Idiomas**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

# Complementos de destino, MC, ensamblador y objetos

La ABI de complementos de la primera versión de NeverC permite que un complemento
en C describa un destino, reemplace rutas de generación de código, observe la
emisión de código máquina, analice o imprima ensamblador y lea o escriba archivos
objeto. La frontera pública es una ABI de C puro: los complementos no deben
intercambiar objetos C++ de LLVM, tipos de la STL, excepciones ni punteros
propiedad del anfitrión cuya vida útil no esté declarada por una tabla de API.

## Niveles de compatibilidad

Los descriptores independientes del destino, los identificadores de fase y de
artefacto, los contenedores MC, los contenedores ObjectGraph, las transacciones de
salida y los contratos de devolución de llamada son ABI STABLE de la primera
versión. Los esquemas específicos del destino —códigos de operación, registros,
operandos, fixups, reubicaciones y convenciones de llamada— son LOCKSTEP. Un
complemento debe comparar el identificador y el resumen del esquema de destino
antes de consumir valores LOCKSTEP. NeverC rechaza los esquemas discordantes antes
de invocar al proveedor.

## Registrar un destino y una ruta de generación de código

Consulte `NevercTargetAPI` durante el registro, registre uno o varios registros
`NevercTargetDescriptor` y adjunte descriptores de máquina de destino y aristas de
generación de código. Una ruta se selecciona a partir de la clave canónica de
destino: identificador de destino, triple, CPU, características, ABI, modelo de
reubicación, modelo de código, formato de objeto y resumen del esquema.

Las rutas de grano fino usan `IR -> MIR -> MC -> ObjectGraph -> ObjectImage`. Una
arista gruesa puede reemplazar la ruta completa `IR -> ObjectImage`. La salida
gruesa sigue pasando por el verificador de producto obligatorio del anfitrión y
por la confirmación transaccional de salida; un proveedor no puede eludir ninguna
de las dos compuertas.

## Construir y observar el MC

`NevercMCAPI` posee las mutaciones de `MCUnit` locales a la tarea. Inicie una
mutación, cree secciones, fragmentos, símbolos, expresiones, instrucciones y
operandos, y luego confírmela o descártela. Los manejadores están acotados a la
tarea y se comprueban por generación.

El flujo de emisión independiente del destino expone eventos ordenados para
cambios de sección, etiquetas, instrucciones, alineación, atributos de símbolos,
CFI, ubicaciones de depuración y datos.
`neverc.mc.emission.pre_instruction` es reemplazable; el resto de las fases de
eventos son puntos de observación de solo lectura. Véase
`pluginsdk/examples/MCObserverPlugin.c`.

Los proveedores de codificación, decodificación y disposición operan sobre la
misma clave de destino y el mismo resumen de esquema. La disposición se encarga de
la relajación y emite un resumen de prueba. Cualquier mutación posterior a la
disposición invalida esa prueba y obliga a redisponer antes de escribir el objeto.

## Reemplazar la sintaxis de ensamblador

Un proveedor de analizador de ensamblador consume bytes de origen y publica un
`MCUnit`. Un impresor de ensamblador consume un `MCUnit` y escribe únicamente a
través de la transacción de salida suministrada. El ensamblador preprocesado
(`.S`) pasa por el preprocesador frontal habitual antes del proveedor de análisis;
el ensamblador simple (`.s`) entra directamente en el analizador.

Los proveedores preparan primero la salida. La verificación de análisis o
impresión y la compuerta de confirmación del anfitrión se ejecutan antes de que
los bytes sean visibles, de modo que un fallo no deja salida parcial.

## Leer, reescribir y escribir objetos

`NevercObjectAPI` representa un archivo reubicable como un ObjectGraph
normalizado: secciones, símbolos, reubicaciones, grupos/COMDAT,
importaciones/exportaciones, metadatos TLS, registros de desenrollado y registros
de depuración. Los adaptadores integrados cubren ELF, COFF y Mach-O, y los
complementos pueden registrar formatos adicionales.

La canalización de objetos es:

1. sondear y leer los bytes en un ObjectGraph;
2. ejecutar los interceptores de grafo `object.pre_write`;
3. disponer y ejecutar `object.post_layout` (redisponer tras una mutación);
4. escribir una imagen candidata acotada;
5. ejecutar los interceptores binarios `object.post_write`;
6. ejecutar el verificador final sellado y la confirmación atómica del anfitrión.

Los observadores reciben puentes de solo lectura. Las mutaciones intentadas desde
un observador se rechazan con `NEVERC_STATUS_POLICY_VIOLATION`. Los escritores y
los interceptores posteriores a la escritura solo pueden acceder al constructor
transaccional acotado; un desbordamiento, un fallo en la devolución de llamada o
un fallo de verificación aborta la preparación. Véase
`pluginsdk/examples/ObjectRewritePlugin.c`.

## Reglas de concurrencia y de fallo

- Mantenga el estado mutable en el estado de proceso/sesión/tarea que suministra
  el anfitrión.
- No almacene en caché manejadores de tarea ni vistas prestadas después de que la
  devolución de llamada retorne.
- Invoque la continuación de un interceptor como máximo una vez y en el hilo de la
  devolución de llamada.
- Devuelva el `NevercStatus` original; no publique productos parciales.
- Declare los modos de concurrencia y reentrada más estrechos que sean veraces.

El contrato de cobertura ejecutable es `docs/plugin-api/coverage.json`. Asigna a
cada fase estable pruebas positivas, negativas, de reemplazo, de observador de
solo lectura y de compuerta sellada.
