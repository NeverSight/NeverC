**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Índice de documentación](../README.es.md) · [← Proyecto NeverC](../../README.md)

# Binarios de publicación y `--strip`

Usa `--strip` al producir un ejecutable, una biblioteca compartida o un módulo
de kernel Android final para distribuir. Su forma abreviada es `-s`; ambas formas
se comportan igual.

## Inicio rápido

```bash
neverc -O2 --strip app.c -o app
neverc -O2 -s app.c -o app

cd examples/android-kernel-hello
neverc make release
```

NeverC elimina los metadatos dentro de su enlazador integrado. No ejecuta un
`llvm-strip` externo, por lo que el mismo comando sirve al compilar de forma
cruzada salidas ELF, Mach-O y PE/COFF.

No confundas esta opción de CLI con el conmutador de empaquetado de CMake
`NEVERC_STRIP_BINARY`: este último solo procesa después de compilar el
ejecutable del compilador `neverc` y puede invocar una herramienta strip
externa. No afecta a los programas compilados por NeverC.

## Política de depuración y símbolos

| Invocación | Información de depuración a nivel fuente | Nombres de símbolos estáticos ordinarios | `.dSYM` de Darwin |
|------------|------------------------------------------|------------------------------------------|-------------------|
| Predeterminada (sin `-g`) | No se genera | Puede permanecer; el valor exacto depende del formato | No se genera |
| `-g` | Se genera | Permanece | Se genera en un enlace Darwin normal |
| `--strip` | Se elimina si existe | Se eliminan nombres no necesarios en ejecución | No se genera |
| `-g --strip` | Prevalece la política strip; no está en la imagen entregada | Se eliminan nombres no necesarios en ejecución | Se suprime |

Sin `-g`, el frontend no genera depuración a nivel de fuente. Eso **no** implica
que la salida esté totalmente depurada de símbolos: ELF y Mach-O aún pueden
tener nombres ordinarios; PE normalmente no tiene tabla estática COFF salvo que
la depuración la solicite. Auto-LTO puede descartar nombres locales, pero no
garantiza un strip-all.

`-g` cambia de no tener depuración fuente a generarla; no añade «más» sobre
información generada por defecto. Datos de unwinding como `.eh_frame` en
ELF/Mach-O o `.pdata`/`.xdata` en PE son metadatos de ejecución, no DWARF a
nivel fuente, y pueden permanecer tras strip.

## Implementación y comportamiento por formato

El controlador convierte `--strip` en una política de enlace tipada y la pasa
a los tres backends. Cada uno la aplica mientras comprende el formato y
conserva los nombres y registros que exige el cargador o la ABI dinámica.

| Formato | Se elimina | Se conserva cuando es necesario |
|---------|-------------|---------------------------------|
| ELF | Datos `.debug*` y tablas ordinarias estáticas de símbolos/cadenas | Importaciones/exportaciones dinámicas, metadatos de reubicación y carga, información de unwinding |
| Kernel Android `.ko` (ELF ET_REL) | `.debug*`, `.comment`, entradas locales/indefinidas innecesarias para reubicaciones y nombres legibles de definiciones ordinarias conservadas | Un `.symtab` enlazado a `.strtab`, todas las reubicaciones y objetivos, nombres exactos del cargador/CFI, importaciones exactas, nombres de secciones protegidas y metadatos ABI del módulo |
| Mach-O | Mapas de depuración/STABS, entradas locales/globales no necesarias en ejecución y generación del `.dSYM` asociado | Datos de binding/importación, nombres ABI exportados, export trie y símbolos referenciados en ejecución |
| PE/COFF | Secciones DWARF integradas y tabla estática COFF de símbolos/cadenas si existe | Importaciones/exportaciones PE, tablas de unwinding, configuración de carga y otros metadatos del cargador |

## Alcance y precedencia

- `--strip` admite ejecutables, bibliotecas compartidas y la excepción estricta
  del `.ko` Android final descrita abajo.
- NeverC lo rechaza con `-c`, un `-r` ordinario, un `.o` Android intermedio,
  `--emit-static-lib` o `-fdyncode`.
- La política strip prevalece sobre `-g` y los controles de depuración backend.
- Se cubren tanto Auto-LTO predeterminado como `-fno-lto`.
- Se mantienen los nombres de importación/exportación necesarios para la ABI.

## Módulos de kernel Android

Un `.ko` final sigue siendo ELF `ET_REL`. El cargador de módulos Linux requiere
tabla de símbolos, su tabla de cadenas, importaciones indefinidas y
reubicaciones, por lo que rechaza strip-all. NeverC solo admite `-r --strip`
para un destino Android con `-fandroid-kernel-driver-mode`, `-r` y un nombre de
salida terminado en `.ko`. El `-r` ordinario y los `.o` intermedios se rechazan.

`neverc make release` sigue siendo el comando recomendado y se expande a
`-O2 --strip`. Sin `.nvk-build-flags`, `make` usa debug por defecto y no elige
release por sí solo. Los Makefile de ejemplo guardan una selección explícita
para que posteriores `make push`, `make run` y `make` sin objetivo usen el mismo
artefacto. `make debug` o un `PROFILE=...` explícito sustituye la selección;
`make clean` borra el estado y devuelve la build siguiente a debug. En esta ruta
final NeverC elimina las secciones de depuración, `.comment` y las entradas
locales/indefinidas innecesarias para reubicaciones, y reconstruye `.strtab`.

Tras una release correcta, NeverC publica de forma transaccional el módulo y
`<module>.ko.symbols.json` junto a él. Los archivos existentes permanecen
visibles hasta cada sustitución atómica. Los errores anteriores a la
publicación revierten todo el paquete; los errores tardíos de durabilidad
conservan un diario de recuperación. Como dos entradas de directorio no pueden
sustituirse mediante una sola operación del sistema de archivos, verifica
siempre `image_sha256` después de un cierre anómalo. El mapa registra los nombres
`original` y `release` de cada símbolo conservado cuyo nombre cambió:

```json
{
  "format": "neverc.android-kernel-symbol-map",
  "version": 2,
  "image_sha256": "…",
  "symbols": [
    {"original": "worker_dispatch", "release": "fn_C000"}
  ]
}
```

Las entradas se ordenan por `release`. Se omiten los símbolos eliminados y los
nombres exactos del cargador, importaciones o CFI porque no necesitan
traducción. Si una build debug u otra build sin strip sobrescribe la misma ruta
de salida, NeverC elimina el mapa obsoleto. ELF permite bytes que no son UTF-8
en los nombres de símbolos; esos originales poco frecuentes se guardan en
Base64 en `original` con `"original_encoding": "base64"`. Los demás nombres
originales siguen siendo legibles. Archiva el mapa como artefacto privado de
depuración; no lo distribuyas con el `.ko` ni lo envíes al dispositivo. Antes
de traducir un nombre de release de un informe de fallos, verifica que el mapa
corresponda al `.ko` actual:

```bash
test "$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' \
  nvk_hello.ko)" = "$(jq -r '.image_sha256' nvk_hello.ko.symbols.json)"

python3 - nvk_hello.ko.symbols.json fn_C000 <<'PY'
import base64, json, sys
with open(sys.argv[1], encoding="utf-8") as stream:
    entry = next(item for item in json.load(stream)["symbols"]
                 if item["release"] == sys.argv[2])
original = entry["original"]
print(repr(base64.b64decode(original))
      if entry.get("original_encoding") == "base64" else original)
PY
```

Las definiciones conservadas aptas reciben nombres estructurales deterministas
inspirados en IDA, sin usar sus prefijos reservados:

- `STT_FUNC` se convierte en `fn_HEX`;
- `STT_OBJECT` se convierte en `obj_HEX`;
- `STT_NOTYPE` ejecutable se convierte en `code_HEX`;
- otro `STT_NOTYPE` asignado se convierte en `sym_HEX`;
- `SHN_ABS` se convierte en `abs_HEX`;
- una definición fuera de `SHF_ALLOC` se convierte en
  `sym_S<FINAL_SECTION_ORDINAL_HEX>_<OFFSET_HEX>`.

Cada campo `HEX`, incluidos los dos de la forma no asignada, usa hexadecimal en
mayúsculas y sin ceros iniciales redundantes. Si varios símbolos necesitan la
misma grafía, se añaden variantes decimales deterministas `_1`, `_2`, etc.

Estas grafías se inspiran en IDA sin ocupar su espacio de nombres ficticios. En
una base nueva de IDA 9.4, los símbolos de usuario ELF `sub_0`, `sub_4` y
`loc_8` aparecen como `_sub_0`, `_sub_4` y `_loc_8`, mientras que `fn_0`,
`code_8` y `obj_10` permanecen intactos. La documentación de Hex-Rays sobre
[`SN_NODUMMY`](https://python.docs.hex-rays.com/ida_name/index.html) confirma
que antepone un guion bajo a un nombre de usuario que comienza por un prefijo
ficticio como `sub_`. NeverC no vacía a propósito el `st_name` de una definición
ordinaria para que IDA sintetice `sub_`: kallsyms de módulos Android/Linux ha
ignorado históricamente las entradas sin nombre, y un nombre vacío eliminaría
el contrato serializado auditable. Las entradas que ya deben estar vacías y los
símbolos de sección siguen siendo exactos.

ELF permite que varios símbolos compartan una misma canonical analysis EA.
NeverC conserva o genera en `.symtab` el conjunto completo de alias; sin
embargo, el modelo de nombres por dirección de IDA 9.4 puede materializar solo
un nombre principal entre los símbolos de esa dirección. Que IDA no muestre un
alias no significa que se haya perdido del ELF; el conjunto completo debe
auditarse con `llvm-readelf` o `llvm-nm`.

En un símbolo asignado, `HEX` es la canonical analysis EA de NeverC: la
dirección efectiva canónica usada solo para análisis estático. Partiendo de un
cursor cero, NeverC recorre las secciones `SHF_ALLOC` finales conservadas en el
orden final de la tabla de secciones, alinea el cursor a
`max(sh_addralign, 1)`, registra la base y avanza `max(sh_size, 1)`; la EA es esa
base más el `st_value` final. `abs_HEX` usa el `st_value` absoluto final. En la
forma no asignada, `FINAL_SECTION_ORDINAL_HEX` es el ordinal final de sección y
`OFFSET_HEX` es el `st_value` final dentro de ella. Estas coordenadas no son un
resumen criptográfico, cifrado, desplazamiento de archivo, dirección virtual
ELF ni dirección de ejecución del kernel. El cargador y KASLR pueden ubicar el
módulo en otro lugar al ejecutarlo.

Permanecen exactos:

- cada importación `SHN_UNDEF`, porque el cargador la resuelve por nombre;
- los símbolos definidos en `.modinfo`, `.text.ftrace_trampoline`,
  `.gnu.linkonce.this_module`, `__versions` o `.codetag.alloc_tags`;
- `init_module`, `cleanup_module`, `__cfi_check`, `__cfi_check_fail`,
  `__cfi_jt_init_module` y `__cfi_jt_cleanup_module`;
- los nombres que comienzan por `__typeid__` o `__kcfi_typeid_`.

El área `extern` que muestra IDA es una vista de análisis sintética, no una
sección ELF real. En un `.ko` `ET_REL` final, los objetivos de reubicación
externos son entradas `SHN_UNDEF` de `.symtab`, cuyos nombres exactos necesita
el cargador. Por eso la política sigue la clase de símbolo ELF y la sección que
lo define: las importaciones indefinidas conservan el nombre exacto y las
definiciones aptas se renombran al margen de cómo las agrupe la herramienta.

Todos los nombres se planifican globalmente antes de modificar. Las definiciones
que comparten candidato base reciben, en orden determinista, el nombre sin
sufijo, luego `_1`, `_2`, etc.; este caso normal no es un error. La finalización
se cancela si un nombre generado colisiona con el espacio reservado para los
nombres que deben conservarse sin cambios o si el cálculo de coordenadas o
numeración excede el intervalo numérico. También rechaza el resultado de forma
segura, sin adivinar, ante `SHN_COMMON`, `SHN_LIVEPATCH` o un
índice de sección ELF reservado desconocido. `SHN_COMMON` no es válido en un
módulo final cargable; compila con `-fno-common`. Los módulos livepatch requieren
el orden e índices originales de la tabla de símbolos y metadatos de reubicación
adicionales que esta política no afirma preservar.

La detección usa señales redundantes: cualquier símbolo `SHN_LIVEPATCH`, sección
`.klp.*`, indicador `SHF_RELA_LIVEPATCH` o campo de `.modinfo` separado por NUL
que empiece por `livepatch=` identifica un módulo livepatch y hace que se rechace
por seguridad. El marcador de `.modinfo` basta por sí solo, aunque no exista ninguna
sección `.klp.*` ni indicador de reubicación livepatch.

Solo se sustituyen los nombres aptos de `.symtab`. Un `.ko` cargable aún
necesita `.symtab`, su `.strtab` enlazada y las reubicaciones, por lo que las
herramientas genéricas pueden describirlo legítimamente como `not stripped`.
Almacenes e interfaces independientes como BTF, exportaciones del módulo,
`.modinfo`, `__versions`, metadatos de traza, `__ksymtab_strings`, `.rodata`
y literales aún pueden revelar nombres originales u otro texto identificador.
Los nombres ordinarios también cambian en kallsyms y diagnósticos, lo que reduce
la utilidad de ftrace por símbolo, enlaces kprobe/BPF e informes de fallos.
Diagnostica con una build debug sin strip y no dependas del nombre original de
un símbolo privado en el módulo release.

### Límite plugin de una release Android finalizada

La finalización establece dos límites de identidad independientes y fail-closed
alrededor de las fases de salida de plugins:

- Antes de cualquier fase `ObjectGraph` reemplazable, el sello del grafo vincula
  el `section ID`, el `final ordinal` y el nombre exacto de cada sección lógica
  conservada. También vincula el `symbol ID` de cada símbolo de nombre exacto
  con su nombre, clase, sección, valor, tamaño, binding, tipo y `st_other`
  completo. El verificador release vuelve a calcular por separado los nombres
  estructurales ordinarios.
- Después de que el host establece una baseline de escritura de confianza y
  antes de `neverc.object.post_write`, el sello de imagen vincula ordinal y
  nombre de cada sección lógica conservada, el número total de entradas
  `.symtab`, y el nombre y los atributos de cada símbolo de nombre exacto con su
  `slot` `.symtab` sin procesar.

Por eso la matriz de capacidades es deliberadamente estrecha:

| Binding de fase | Comportamiento de la release Android finalizada |
|-----------------|------------------------------------------------|
| `neverc.object.write` `provider` / `interceptor` | `REJECTED` antes de que pueda sustituir la baseline de escritura de confianza establecida por el host |
| `plugin-owned ObjectFormat graph writer` | `REJECTED`; una release Android finalizada exige el graph writer propiedad del host que establece la baseline de confianza |
| `observer` | `READ_ONLY`; se permite observar, pero no mutar el artefacto |
| `neverc.object.post_write` `interceptor` | `VALIDATED`; solo puede cambiar bytes de payload fuera de la superficie de identidad, y el resultado debe seguir pasando el verificador release, el contrato ABI de entrada y ambos sellos de identidad |

La propiedad del merge finalizado también queda sellada por el host. Se
descarta cualquier `MergedImage` o byte independiente de un
`third-party ObjectMergeProvider`; el `host-owned graph writer` serializa el
grafo verificado y finalizado de ese provider. En sentido inverso,
`built-in finalized input serialization` omite las `external object phases` y
entrega al merger del host exactamente los `audited native bytes`; este paso
interno de entrada no elude la frontera de salida anterior.

La finalización solo se acepta con `Android module merge semantics`; también
exige tanto una `relocatable output request` como una
`relocatable driver configuration`, o falla `before routing`. En una release
Android relocatable finalizada, `frozen input format`,
`TargetKey.ObjectFormatID` y `frozen output format` deben compartir
`one format identity`. Una discrepancia se rechaza `before provider dispatch`,
por tanto también antes del route planning o de crear el sink; así, el
capability preflight y el graph-writer dispatch real no pueden observar
formatos diferentes.

Con una entrada ordinaria representable por el grafo, los interceptors de grafo
anteriores solo pueden ejecutarse si conservan el sello y toda la semántica
release. Si la entrada requiere passthrough de imagen nativa para hechos que el
`ObjectGraph` no puede representar, se rechazan todos los
`route-matching provider` reemplazables y todos los interceptors. Un provider
cuya ruta target/CPU/features/object-format/execution-level no coincida no se
ejecuta ni bloquea la release; solo se permiten observers de solo lectura. Solo
un rechazo o fallo de validación `before sealed commit` cancela el staging y no
publica ningún archivo. Un fallo de observer `AFTER_COMMIT` se informa después
de la publicación y no puede revertir el archivo ya publicado.

No postproceses un `.ko` con `llvm-strip --strip-all` ni `objcopy`, ni elimines a
ciegas secciones codetag/BTF/ABI. Haz strip antes de firmar los bytes finales;
cualquier cambio posterior invalida la firma. `clean` solo debe borrar archivos,
nunca aplicar strip ni firmar un módulo existente.

## Límite de seguridad

Strip elimina nombres y metadatos valiosos y encarece el análisis, pero no impide
aplicar ingeniería inversa al código máquina. Un binario correctamente tratado
aún puede contener:

- nombres dinámicos de importación/exportación requeridos por el cargador;
- nombres exigidos por el cargador y almacenados fuera de `.symtab` en un `.ko`;
- literales, tablas de reflexión o metadatos de la aplicación;
- registros de unwinding, reubicación, firma y configuración de carga;
- código máquina y su flujo de control observable.

`--strip` solo controla la imagen final. No elimina artefactos solicitados por
separado, como mapas de enlace, registros de optimización o salidas de
`-save-temps`; audita el directorio de publicación y no distribuyas esos
archivos auxiliares.

Usa cifrado de cadenas, ofuscación y medidas antimanipulación como capas
separadas cuando proceda, y no incrustes secretos que deban ser confidenciales.

## Verificación de un artefacto

Inspecciona los artefactos de publicación en CI con las herramientas de objetos
de LLVM. Ajusta los comandos al formato y permite expresamente los nombres ABI.
La comprobación negada de `strings` no debe encontrar coincidencias y solo
entonces termina correctamente.

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
! strings app | grep -Fq -- neverc_private_release_symbol
test ! -e app.dSYM

file examples/android-kernel-hello/nvk_hello.ko
llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

En un `.ko` ELF `ET_REL` cargable, la utilidad genérica `file` aún puede
mostrar `not stripped` porque `.symtab` se conserva deliberadamente. No uses
esa etiqueta para decidir si la release pasó. Comprueba que no haya DWARF ni
`.comment`, que las definiciones aptas usen las formas hexadecimales mayúsculas
canónicas `fn_`/`obj_`/`code_`/`sym_`/`abs_`, que las importaciones
`SHN_UNDEF` y los nombres necesarios del cargador/CFI sigan exactos y que las
reubicaciones sean válidas. Audita por separado BTF, exportaciones, modinfo,
versions, metadatos de traza y cadenas si importa la divulgación de nombres.

Un artefacto con strip no debe tener secciones de depuración fuente ni nombres
de símbolos estáticos privados. Los nombres dinámicos y metadatos necesarios
son esperados y no indican un fallo.
