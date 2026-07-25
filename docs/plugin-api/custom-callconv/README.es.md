**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ABI de complementos de NeverC](../README.es.md)

# Convenciones de llamada personalizadas

NeverC admite **convenciones de llamada personalizadas basadas en datos**: puedes asignar registros físicos arbitrarios a los argumentos y valores de retorno de cualquier función, íntegramente desde un plugin externo o mediante atributos en el código fuente, sin modificar el compilador ni ninguna definición de TableGen.

## Visión general

Las convenciones de llamada tradicionales de LLVM están grabadas en el backend a través de archivos `.td` / `.inc`. Añadir o modificar una obliga a editar las fuentes del compilador y volver a ejecutar TableGen. NeverC lo sustituye por un modelo **basado en datos en tiempo de ejecución**, construido sobre dos capas:

- Una **spec** —una cadena breve y escribible a mano, como `gpr:rcx,rdx;ret:rax`— se adjunta a una función como atributo de cadena `"neverc-callconv"`, ya sea desde un plugin o desde un atributo en el código fuente.
- Antes de la generación de código, el anfitrión **materializa** esa spec en un atributo `"neverc-cc-plan-v1"`: una tabla de ubicaciones exacta, inmutable y validada, ligada a un esquema de destino concreto. El backend solo consume el plan.

La spec es lo que escribes; el plan es lo que el backend da por bueno. Las convenciones de llamada pasan así de estar «codificadas en el backend en tiempo de compilación» a estar «dirigidas por datos de una política externa en tiempo de ejecución», sin renunciar a la verificación.

## Formato de la spec

Una spec es una cadena delimitada por puntos y coma. Cada segmento tiene una clave y una lista de nombres de registro separados por comas (no distingue mayúsculas y tolera espacios):

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| Segmento | Alias | Significado |
|---|---|---|
| `args` | | **Modo posicional**: cada token es un nombre de registro o `stack`/`mem`, asignado a los argumentos por índice |
| `gpr` | `arg_gpr` | **Modo pool**: registros de argumento enteros/punteros, usados en orden; el sobrante va a la pila |
| `xmm` | `arg_xmm` | **Modo pool**: registros de argumento de coma flotante/vectoriales |
| `fpr` | | Alias neutro respecto al destino para `xmm` |
| `ret_gpr` | `ret` | Registros de retorno enteros/punteros |
| `ret_xmm` | | Registros de retorno de coma flotante/vectoriales |
| `ret_fpr` | | Alias neutro respecto al destino para `ret_xmm` |
| `csr` | | Conjunto personalizado de registros callee-saved (por defecto: el conjunto ABI estándar) |

Cualquier segmento puede omitirse y los segmentos desconocidos se ignoran. Las claves se definen una sola vez en [`llvm/include/llvm/CodeGen/NeverCCallConv.h`], de modo que productores y analizador no pueden divergir.

### Dos modos de argumentos

**Modo pool** (`gpr:` / `xmm:`): los argumentos enteros toman registros del pool `gpr` en orden; los de coma flotante y vectoriales toman de `xmm`. Cuando un pool se agota, los argumentos restantes van a la pila.

**Modo posicional** (`args:`): el argumento *i* usa el token *i*-ésimo. Cada token es un nombre de registro o bien `stack` / `mem`, lo que fuerza ese argumento a la pila:

```
args:rcx,stack,r8;ret:rax   # arg0→rcx, arg1→pila, arg2→r8, retorno→rax
```

Cuando `args` está presente, tiene prioridad sobre `gpr` / `xmm`. Un token que nombra una clase de registro equivocada para el tipo del argumento, un índice más allá de la lista de tokens y un registro ya asignado acaban todos en una ranura de pila en lugar de hacer fallar la compilación.

### Arquitecturas admitidas

Los nombres de registro se resuelven contra una tabla propia de cada destino, la única fuente de verdad sobre lo que una spec puede nombrar.

| Arquitectura | Nombres GPR | Nombres SIMD | Elección de anchura |
|---|---|---|---|
| **x86-64** | `rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`, `rbp`, `r8`–`r15` | `xmm0`–`xmm15` | i32 → subregistro de 32 bits, i64/puntero → 64 bits |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f16→`h`, f32→`s`, f64→`d`, f128/vector→`q` |

Los GPR se escriben siempre en su forma de 64 bits; el backend los estrecha al subregistro que corresponde al tipo de cada valor. En AArch64 los nombres vectoriales se escriben `v0`–`v31` y el backend elige la forma `H`/`S`/`D`/`Q` según el tipo.

### Restricciones

- **Registros reservados**: el puntero de pila no figura en ninguna de las dos tablas (`rsp` en x86-64, `sp`/`x31` en AArch64), como tampoco `x29`/`x30` (FP/LR) en AArch64. Una spec que nombre uno de ellos simplemente lo omite y el valor pasa a la siguiente ubicación válida.
- **Puntero de marco**: `rbp` *sí* es seleccionable en x86-64 porque es un registro callee-saved legítimo, pero usarlo como registro de argumento solo es correcto bajo `-fomit-frame-pointer`. Úsalo bajo tu propia responsabilidad.
- **Callee-saved**: por defecto, el conjunto ABI estándar. `csr:r12,r13` declara un conjunto personalizado, y el llamador construye una máscara de registros preservados acorde para saber cuáles sobreviven a la llamada. Admitido tanto en x86-64 como en AArch64.
- **Conflictos de csr**: si un registro aparece a la vez en `csr` y en una lista de argumentos o de retorno, el plugin emite un aviso: el llamado lo restauraría y destruiría su papel de transporte de valores. La compilación aun así tiene éxito.
- **Funciones variádicas**: no admitidas. El compilador emite un diagnóstico claro en ambos backends en lugar de pasar mal la parte variádica en silencio.
- **Llamadas indirectas**: una llamada por puntero a función no puede llevar una convención personalizada. El plugin avisa cuando se toma la dirección de una función con convención personalizada; las llamadas indirectas recaen en la convención estándar.
- **Llamadas de cola**: desactivadas en cuanto cualquiera de los dos lados de una llamada usa la convención personalizada, en ambos backends.
- **Valores no cubiertos**: todo argumento o valor de retorno que el plan no cubra recae en la convención estándar del destino (SysV en x86-64, AAPCS en AArch64).

## Uso

### 1. Dirigido por plugin (recomendado)

El plugin de referencia [`CustomCallConvPlugin.c`] se distribuye en `pluginsdk/examples/`. Registra un pase de IR a nivel de módulo en la fase `neverc.ir.pass.post_opt`.

**Compilar el plugin:**

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib   # o .so / .dll
```

**Modo atributo** (por defecto): solo se ven afectadas las funciones que llevan una anotación `custom_attr` en el fuente:

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib input.c -o output.o
```

**Modo global**: aplica una única spec a todas las funciones definidas (exige un `cc-all` explícito):

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

**Filtrar por prefijo de nombre:**

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccprefix=secret_ \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r9,r8;ret:rax" \
       input.c -o output.o
```

**Diversificar**: alterna entre cuatro disposiciones integradas para que las funciones no compartan una sola (anti-ingeniería inversa):

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccshuffle \
       input.c -o output.o
```

Las cuatro opciones que registra el plugin son `cc-all` y `ccshuffle` (indicadores, así que `=1` o `=true` es opcional), más `ccspec` y `ccprefix` (valores de cadena). Sin `ccspec`, el modo global usa el valor por defecto `gpr:r10,r11,rsi,rdi;ret:rdx`.

### 2. Atributos en el código fuente

Anota las funciones directamente en C con el atributo `custom_attr`, en sintaxis GNU o Microsoft:

```c
// Sintaxis GNU
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

// Sintaxis Microsoft
__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

`custom_attr("key", "value")` produce un atributo de cadena limpio en la función (`"key"="value"`), **sin** avisos y **sin** `llvm.global.annotations`. Es un mecanismo **de propósito general**: sirve cualquier par clave/valor, no solo convenciones de llamada. Los pases de IR y MIR lo releen con `F.getFnAttribute("key")`.

### 3. Combinados

Los atributos del fuente y los argumentos del plugin funcionan juntos. Una función con `custom_attr` la atiende la vía de modo atributo del plugin; `cc-all` cubre el resto. Cada función se procesa como mucho una vez.

## Planes materializados

Una spec nombra registros; no dice dónde vive cada byte de cada valor. Tras la canalización de optimización y antes de la generación de código, el anfitrión ejecuta `materializeCallingConventionPlans`, que convierte cada función `CallingConv::NeverC_Custom` en un plan exacto y validado:

- Una función que ya tiene un atributo `"neverc-cc-plan-v1"` se **valida, no se regenera**: su huella de esquema, su ID de destino y su ID de convención deben coincidir con el destino actual.
- A una función con una spec `"neverc-callconv"` se le resuelven los nombres de registro contra la tabla de registros del destino. El plan resultante sustituye a la spec, que después se elimina de la IR.
- Una función sin ninguna de las dos cosas, pero cuyo destino registra una convención de llamada a través de la ABI de plugins, la planifica la retrollamada `PlanCallingConvention` de esa convención.

Cada punto de llamada directo hereda el plan de su función llamada, y eso es lo que mantiene de acuerdo a llamador y llamado sobre la disposición entre unidades de traducción. El plan es una cadena plana:

```
neverc-cc-plan-v1;schema=<huella>;target=<high>:<low>;cc=<high>:<low>;stack=<bytes>;returns=<ubicaciones>;arguments=<ubicaciones>;callee-saved=<números de registro>
```

Cada ubicación es `<r|s>,<índice de valor>,<desplazamiento del fragmento>,<tamaño>,<alineación>,<número de registro>,<desplazamiento de pila>,<indicadores>`, y varias ubicaciones se separan con `|`. Para la vía integrada, la huella de esquema es `llvm-<triple del destino>`; un destino registrado por un plugin aporta la suya.

Como los números de registro solo tienen sentido frente al esquema que los define, una discordancia es un error rotundo y no una compilación silenciosamente incorrecta:

| Situación | Diagnóstico |
|---|---|
| La cadena del plan no se puede analizar | `malformed NeverC calling convention plan` |
| La huella de esquema difiere | `NeverC calling convention plan belongs to a foreign target schema` |
| El ID de destino difiere | `NeverC calling convention plan has a foreign target ID` |
| El ID de convención difiere | `NeverC calling convention plan has a foreign convention ID` |

Esto es lo que hace seguro incrustar un plan en bitcode y arrastrarlo a través de LTO: un plan producido para otro destino no puede aplicarse por accidente.

## API del plugin

El plugin de ejemplo usa únicamente la tabla estable de IR core: no hay un punto de entrada dedicado a las convenciones de llamada. Aplicar una convención a una función son tres llamadas más la sincronización de los puntos de llamada:

```c
NevercIRAttributeHandle Attribute = {0};
Core->CreateStringAttribute(Core->Context, Task, SV("neverc-callconv"), Spec,
                            &Attribute);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION, 0, Attribute);
Core->SetFunctionCallingConvention(Core->Context, Task, Function,
                                   NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM);
```

`NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM` es el nombre estable a nivel de ABI de `CallingConv::NeverC_Custom` (valor 1000 en LLVM). A continuación el plugin recorre los usos de la función con `GetValueUseCount` / `GetValueUse` y, por cada uso que sea el operando llamado de un `call`, `invoke` o `callbr`, fija la misma convención en la instrucción mediante `SetInstructionProperty` con `NEVERC_IR_PROPERTY_CALLING_CONVENTION`. Cualquier otro uso significa que la dirección se escapó, y de ahí sale el aviso de dirección tomada.

Un plugin que registre su propio destino puede en cambio aportar una retrollamada `PlanCallingConvention` en su `NevercCallingConventionDescriptor` y producir planes directamente, saltándose la capa de spec. Véase [Destino, MC, ensamblador, objetos](../target-mc-object.es.md).

## Pruebas

La suite de GoogleTest está en [`tests/neverc/CustomCallConvTests.cpp`] y contiene 26 pruebas. Cada una compila el plugin de ejemplo, traduce un programa pequeño a ensamblador bajo una spec dada y comprueba la colocación en registro o en pila resultante.

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

Cobertura:

| Categoría | Pruebas |
|---|---|
| x86-64 pool / posicional / pila / desbordamiento / i64 / sret / byval / repliegue | 9 |
| AArch64 GPR / FPR / pila / `csr` / llamada cruzada entre specs distintas | 5 |
| Frontend `custom_attr` (GNU / `__declspec` / de extremo a extremo) | 3 |
| Materialización del plan y rechazo de esquema | 3 |
| Endurecimiento (`csr`, variádicas en ambos destinos, indirecta, `rsp`, conflicto de csr) | 6 |

## Arquitectura

```
Atributo de origen            Pase IR del plugin
custom_attr(...)              (neverc.ir.pass.post_opt)
       │                            │
       └─────────────┬──────────────┘
                     ▼
   "neverc-callconv" = spec, CallingConv::NeverC_Custom
   en la función y sus puntos de llamada directos
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ materializeCallingConventionPlans        │
   │ (tras la optimización, antes de codegen) │
   │                                          │
   │  spec            → nombres a physregs    │
   │  CC de plugin    → PlanCallingConvention │
   │  plan existente  → valida esquema/destino│
   └──────────────────────────────────────────┘
                     │
                     ▼
   "neverc-cc-plan-v1" = ubicaciones validadas
   spec eliminada; plan copiado a las llamadas directas
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ CCAssignFn del backend (una por destino) │
   │  CC_X86_NeverC     / RetCC_X86_NeverC    │
   │  CC_AArch64_NeverC / RetCC_AArch64_NeverC│
   │                                          │
   │  lee el plan → asigna ubicaciones        │
   │  valores sin cubrir → convención estándar│
   │  llamadas de cola desactivadas           │
   └──────────────────────────────────────────┘
                     │
                     ▼
   Código máquina con la disposición propia
```

El ejecutor del backend es una **implementación de una sola vez**: todas las decisiones de política viven en el plugin. Añadir una convención nueva nunca exige reconstruir NeverC.

<!-- reference links -->
[`CustomCallConvPlugin.c`]: ../../../pluginsdk/examples/CustomCallConvPlugin.c
[`llvm/include/llvm/CodeGen/NeverCCallConv.h`]: ../../../llvm/include/llvm/CodeGen/NeverCCallConv.h
[`tests/neverc/CustomCallConvTests.cpp`]: ../../../tests/neverc/CustomCallConvTests.cpp
