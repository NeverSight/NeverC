**Idiomas**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

# API de complementos del preprocesador

`PluginPrep.h` expone esquemas estables de tokens, identificadores, macros,
pragmas y flujos de tokens sin filtrar tipos C++ de NeverC ni de LLVM. El esquema
generado `Schema/PluginPrepSchema.inc` es la fuente de verdad para los géneros
numéricos estables, las categorías, las grafías y la construibilidad.

## Niveles de extensión

Un complemento puede participar en tres niveles:

- eventos de preprocesador de solo lectura para inclusiones, expansiones de
  macros, condicionales, pragmas y transiciones de archivo;
- interceptores tipados para las fases de token, inclusión, macro, pragma y
  consulta de características;
- un proveedor `neverc.prep.build_token_stream` completo que publica un
  `TokenStream` verificado.

La fase de token admite reemplazo, eliminación y expansión acotados. El anfitrión
impone el presupuesto de expansión y verifica la grafía, la ubicación, las
banderas, la colocación del EOF y la propiedad de los tokens antes de publicar un
reemplazo.

## Constructores de tokens

Cree tokens sintetizados con `CreateTokenBuilder`, establezca exactamente una
carga útil de token, asigne una ubicación válida propiedad de la tarea y llame a
`TokenBuilderCommit`. Destruya el constructor en todas las rutas. Un constructor
confirmado es inmutable y una confirmación fallida no publica ningún token.

Los flujos de tokens son artefactos de tarea contiguos e inmutables. Un flujo de
reemplazo debe contener exactamente un token EOF final y no puede superar
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS`.

## Reglas para observadores e interceptores

Los observadores reciben datos de eventos de solo lectura y no pueden afectar al
preprocesamiento. Los interceptores siguen el contrato común de continuación:

- llamar a `InvokeNext` como máximo una vez y luego devolver `CONTINUE`; o
- no llamarlo y publicar un reemplazo verificado.

Los objetos de continuación y todos los manejadores del preprocesador solo son
válidos dentro de su ámbito declarado de devolución de llamada o tarea. Un hilo
creado por el complemento debe unirse antes de que la devolución de llamada
retorne si toca esos valores.

## Verificación

Tras cambiar definiciones de tokens, ejecute las comprobaciones de esquema
generado y de cobertura:

```sh
python3 utils/plugin-api/gen-prep-schema.py --check
python3 utils/plugin-api/check-coverage.py docs/plugin-api/coverage.json
```

Con `NEVERC_ENABLE_PLUGIN_FUZZERS=ON`,
`plugin-prep-token-builder-fuzzer` ejercita constructores de tokens mal formados,
manejadores de tarea, capacidades de salida y consultas de flujos de tokens.
