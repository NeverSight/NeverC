**Idiomas**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

# API de complementos para AST, analizador y semántica

`PluginAST.h` y `PluginSema.h` ofrecen acceso en C puro, acotado a la tarea, al
árbol del frontal y a la canalización semántica. Los identificadores estables de
nodos, propiedades y ranuras hijas se generan a partir de las definiciones
concretas del AST de NeverC; un complemento nunca recibe un puntero C++ a `Decl`,
`Stmt`, `Type` o `Sema`.

## Leer y construir nodos del AST

Use `NevercASTAPI` para consultar información de nodos, propiedades de esquema,
hijos, padres, contextos de declaración, tipos, atributos y detalles de los nodos
concretos habituales. Las API por lotes exigen un número de elementos, una
capacidad y un paso explícitos.

`NevercASTBuilder` solo construye géneros de nodo declarados en el esquema. Las
propiedades y ranuras hijas obligatorias se verifican al confirmar. Una
confirmación correcta publica un nodo propiedad de la tarea; una fallida no deja
ningún nodo parcialmente visible. Destruya cada constructor tras la confirmación
o el fallo.

## Mutación atómica

Los cambios en el AST usan `BeginASTMutation`, operaciones preparadas y
`CommitASTMutation`. El anfitrión valida la propiedad, la compatibilidad de
ranuras, la cardinalidad, los enlaces al padre, los ciclos y los invariantes
semánticos antes de modificar el árbol. `AbortASTMutation` descarta todas las
operaciones preparadas. Las notificaciones nativas de `TreeMutationListener` solo
se envían tras una confirmación correcta.

El ejemplo compilable
[`ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c) muestra un
interceptor del analizador que llama al analizador integrado, construye un
literal entero y reemplaza atómicamente el inicializador de una variable.

## Reemplazo del analizador y de Sema

`neverc.syntax.parse` asigna un flujo de tokens verificado a un `ASTUnit`.
`neverc.sema.analyze` asigna un producto del AST a un `SemanticUnit`. Ambas fases
tienen interceptores tipados y proveedores. Las fases de extensión de grano fino
—declaración, sentencia, expresión, nombre de tipo, atributo, búsqueda, conversión
y palabra clave— siguen disponibles cuando solo se reemplaza una parte del
frontal.

La ruta integrada fusionada de analizador y Sema publica exactamente los mismos
contratos de artefacto que un reemplazo. La reproducción semántica solo acepta
géneros de nodo para los que NeverC puede reconstruir el ámbito, la búsqueda de
nombres, la redeclaración y el estado de comprobación de tipos. Encontrar un
género concreto no admitido devuelve `NEVERC_STATUS_UNSUPPORTED_AST_KIND`; nunca
marca como semánticamente completo un árbol reproducido solo en parte.

## Ciclo de vida y limpieza

Los observadores del ciclo de vida del AST y de Sema se entregan en orden de
código fuente a través del puente `TreeConsumer` del anfitrión. Los eventos de
inicio y fin permanecen emparejados ante errores de sintaxis, errores del
complemento y cancelaciones. Los manejadores de tarea solo dejan de ser válidos
después de que se hayan ejecutado los últimos eventos de fin de solo lectura y
las devoluciones de llamada de limpieza.

## Verificación

```sh
python3 utils/plugin-api/gen-ast-schema.py --check
ctest --test-dir build-neverc \
  -R 'Plugin(AST|Parser|Sema|Frontend)' --output-on-failure
```

Con `NEVERC_ENABLE_PLUGIN_FUZZERS=ON`, `plugin-ast-mutation-fuzzer` cubre la
decodificación de propiedades, los constructores mal formados, los manejadores
falsificados y la reversión de mutaciones.
