**Idiomas**: [English](../translate.md) | [简体中文](../zh-CN/translate.md) | [繁體中文](../zh-TW/translate.md) | [日本語](../ja/translate.md) | [한국어](../ko/translate.md) | [Français](../fr/translate.md) | [Deutsch](../de/translate.md) | [Español](translate.md) | [Italiano](../it/translate.md) | [Русский](../ru/translate.md) | [العربية](../ar/translate.md)

[← Documentación](README.md)

# Traducir C++ a NeverC

El comando experimental `neverc translate` genera código `.nc` revisable con `cpp-core-v1`, `cpp-core-v2`, `cpp-project-v1` y `cpp-math-v1`.

**Actualmente solo está implementada la traducción de código C++.** Está previsto añadir E Language (易语言, `.e`), Python, Go, Rust, TypeScript y JavaScript; sus traductores aún no están disponibles.

## Instalación y traducción escalar

Use una instalación normal de NeverC con sus recursos estándar. El frontend C++ y las cabeceras SDK aprobadas están integrados; no hace falta instalar Clang por separado. Consulte las [notas de compilación del frontend](../../utils/translate-frontends/cpp/README.md).

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

El perfil `cpp-core-v1` acepta un único archivo C++17 autónomo sin includes. Admite `int`, `unsigned int`, `bool`, `void`, tipos agregados triviales, funciones no miembro, espacios de nombres, sobrecargas y el flujo de control documentado. Se comprueban todas las declaraciones de entrada, incluido el código no utilizado.

Seleccione `--profile cpp-core-v2` para añadir alias `typedef`/`using` comprobados, enumeraciones con tipos subyacentes enteros admitidos, `static_assert`, punteros a objetos y referencias a lvalues dentro de un alcance limitado. Los parámetros y resultados por referencia conservan los alias; se admiten punteros nulos y calificaciones `const` anidadas. Se mantienen las restricciones de un solo archivo fuente y sin includes. Consulte el [contrato de core v2](../../utils/translate-frontends/docs/cpp-core-v2.md).

Core v2 añade arrays locales de tamaño fijo y campos de array, indexación multidimensional y punteros o referencias a arrays. La inicialización parcial inicializa a cero los elementos escalares omitidos; los registros siguen su inicialización seleccionada y conserva el orden y los alias. El tamaño y la expansión de la inicialización tienen límites. Los arrays globales y los de longitud variable siguen sin admitirse.

Core v2 admite `switch`/`case`/`default`, con sentencias de inicialización de C++17, continuación entre casos y anotaciones `[[fallthrough]]` validadas. El selector se evalúa una vez y los switches y bucles anidados conservan los destinos de `break`/`continue`. Se rechazan los rangos case de GNU y otros atributos de sentencia.

Core v2 admite enteros con y sin signo de 8, 16, 32 y 64 bits, tipos y literales de caracteres, y consultas constantes `sizeof`/`alignof`. Las promociones y la resolución de sobrecargas preceden a la normalización; `long`, `wchar_t` y el tipo de tamaño siguen el destino. Los enums también admiten tipos subyacentes más estrechos y más anchos. Las cadenas en ejecución y STL siguen en desarrollo.

Core v2 también admite desplazamientos y diferencias de punteros a objetos, incrementos/decrementos y asignaciones compuestas, incluidos recorridos de arrays y pasos multidimensionales. Las funciones auxiliares generadas conservan las reglas de C++17 para un puntero nulo más o menos cero y la diferencia entre dos punteros nulos. Siguen excluidos el orden entre punteros, las conversiones puntero/entero y los contenedores y algoritmos STL.

Core v2 contrasta los tamaños y las alineaciones ABI de los tipos de origen con el modelo de destino de NeverC, incluidos los tamaños de estructuras y los desplazamientos de campos. Las aserciones estáticas del código generado vuelven a comprobar la disposición al compilar, y el manifiesto registra estos datos.

Core v2 admite funciones miembro con nombre no virtuales de los tipos de registro aceptados, incluidas las sobrecargas const, los métodos cualificados para lvalue, los métodos estáticos y `this`. Las llamadas conservan la identidad del objeto original y evalúan el receptor antes que los argumentos. Aún no se admiten llamadas no estáticas sobre objetos temporales, herencia, plantillas ni la STL general.

Core v2 admite constructores ordinarios definidos por el usuario para registros con disposición estándar y operaciones de copia admitidas. Objetos locales, campos y elementos de array se construyen en su almacenamiento final; los campos se inicializan en orden de declaración. Siguen excluidos los constructores delegados o de movimiento y las excepciones.

Core v2 crea un objeto independiente para cada parámetro de registro pasado por valor y escribe el resultado en el destino del llamador. Constructores y métodos siguen las mismas reglas; se conservan las copias necesarias y los alias de las referencias. Para tipos triviales que cumplen los requisitos, otras implementaciones C++17 pueden añadir copias de argumentos o resultados.

Core v2 admite destructores ordinarios definidos por el usuario y destrucción implícita de miembros en salidas normales. Objetos locales, campos y elementos de array se destruyen en orden inverso; los temporales al finalizar la expresión completa, tras guardar los valores necesarios. Retornos, ramas, bucles, break y continue realizan la limpieza correspondiente. Los parámetros por valor se destruyen al salir de la función llamada; los objetos devueltos pertenecen al llamador. Siguen excluidos las llamadas explícitas a destructores, las especificaciones de excepción escritas, la destrucción estática y el desenrollado por excepciones. La copia implícita no trivial, las operaciones de movimiento y STL completo siguen en desarrollo.

Core v2 ejecuta constructores de copia y operadores de asignación por copia ordinarios definidos por el usuario, con un parámetro de origen `R&` o `const R&`. La copia usa el destino real y conserva los efectos secundarios y la referencia devuelta. La sintaxis de asignación evalúa primero el operando derecho; una llamada explícita a `operator=` evalúa primero el receptor. Las asignaciones por copia default y la asignación por copia implícita no trivial siguen excluidas.

Core v2 también admite constructores predeterminados generados o declarados default y destructores default, incluidos miembros de registros y arrays anidados. La construcción usa el destino real y el orden de declaración; la inicialización por valor solo pone a cero cuando C++ lo exige. Se conservan las reglas distintas del default definido fuera de la clase. Los constructores no usados o presentes solo en expresiones no evaluadas no requieren un cuerpo artificial. Los inicializadores de miembros, la asignación por copia implícita no trivial, los movimientos y STL completo siguen en desarrollo.

Core v2 admite constructores de copia implícitos y explícitamente default, incluidos registros anidados y arrays multidimensionales. Las copias de miembros seleccionadas usan el destino real en orden de declaración y de elementos; la dirección del array fuente se evalúa una vez. Las copias triviales conservan los punteros almacenados. Los parámetros por valor y retornos desde objetos fuente mantienen los efectos de copia; transferir directamente una prvalue no añade copias. La asignación generada, los movimientos y las demás funciones C++/STL siguen en desarrollo.

## Proyectos de varios archivos

Seleccione explícitamente las unidades de traducción en una base de datos de compilación y especifique el directorio raíz del proyecto. El frontend integrado analiza cada unidad por separado; la fusión verifica definiciones, enlace, tipos compartidos y el cumplimiento de la regla de una sola definición (ODR) mediante comprobaciones conservadoras.

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

La salida del proyecto es `translated.nc` más `translated.h`. Solo se admiten cabeceras del proyecto situadas dentro de esa raíz. Si un archivo tiene varias configuraciones, seleccione su índice de base cero con `--compdb-entry src/a.cpp=4`. Los comandos de compilación almacenados se interpretan como datos y nunca se ejecutan.

## Matemáticas limitadas de doble precisión

`cpp-math-v1` añade a los proyectos `double`, las conversiones/comparaciones documentadas y exactamente `std::fabs(double)` y `std::floor(double)`. Utiliza las cabeceras integradas de Clang 20.1.8 / libc++ 200100 / macOS 15.5 y requiere un destino macOS 15.0 explícito (arm64 o x86_64). La aritmética general de coma flotante sigue excluida. Las excepciones deben estar enmascaradas y la conversión de subnormales a cero desactivada; se prueban los cuatro modos estándar de redondeo.

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

La traducción matemática también verifica las cabeceras NeverC instaladas, la identidad de las implementaciones integradas y un enlace real. Los módulos generados usan el runtime NeverC. Con `-fno-builtin-std`, la traducción solo falla antes de escribir los archivos si el código final requiere las correspondencias `fabs`/`floor`. El código matemático que no las necesita puede seguir traduciéndose.

## Validación y salida

Use `--check` en lugar de una opción de salida para realizar el mismo análisis, generación y validación de sintaxis y objetos sin conservar los archivos generados. `--report PATH` escribe diagnósticos estructurados. La traducción nunca ejecuta el programa fuente.

Las salidas y los archivos auxiliares nunca se sobrescriben. `--out-dir` requiere un directorio nuevo con un padre existente; `-o` requiere una ruta `.nc` nueva. El manifiesto registra requisitos del destino, hashes de entrada y salida y el procedimiento de compilación; el mapa fuente relaciona las líneas generadas con las ubicaciones originales.

Los resultados de CI, las verificaciones de ejecución e instalación y las pruebas omitidas se registran por plataforma. macOS arm64 nativo y macOS x86_64 mediante Rosetta siguen siendo entornos de validación distintos. El alcance anunciado no incluye C++/STL completo, excepciones, plantillas, cadenas ni `std::vector`. Consulte la [matriz de compatibilidad](../../utils/translate-frontends/docs/support-matrix.md), el [protocolo y las reglas de recuperación](../../utils/translate-frontends/docs/protocol.md) y el [proyecto de ejemplo](../../tests/neverc/Inputs/translate/cpp/project).
