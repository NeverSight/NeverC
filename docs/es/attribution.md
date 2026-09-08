**Idiomas**: [English](../attribution.md) | [简体中文](../zh-CN/attribution.md) | [繁體中文](../zh-TW/attribution.md) | [日本語](../ja/attribution.md) | [한국어](../ko/attribution.md) | [Français](../fr/attribution.md) | [Deutsch](../de/attribution.md) | [Español](attribution.md) | [Italiano](../it/attribution.md) | [Русский](../ru/attribution.md) | [العربية](../ar/attribution.md)

[← Índice de documentación](README.md) · [← Proyecto NeverC](project.md)

# Atribución de fuentes de NeverC

Cuando utilice NeverC como referencia, mencione **NeverC contributors**, incluya
un enlace a [NeverC](https://github.com/NeverSight/NeverC) e identifique los
archivos y el commit o la versión utilizados. Esto se aplica al trabajo escrito
por personas, al trabajo asistido por IA/LLM, a los pases de LLVM, a las
implementaciones de compiladores o enlazadores, a los plugins, a la documentación
y a la investigación. [CITATION.cff](../../CITATION.cff) proporciona los metadatos
de cita del proyecto, y [NOTICE](../../NOTICE) proporciona la atribución y la
procedencia del proyecto.

## Obligaciones de las licencias

La licencia predeterminada del repositorio es la [GNU AGPL versión 3](../../LICENSE).
Las licencias explícitas de archivos y componentes siguen rigiendo sus
respectivos materiales, incluido el código procedente de LLVM que se encuentre
fuera del directorio LLVM. Esta guía explica las obligaciones existentes y
solicita citas; no añade restricciones a las licencias.

- Cuando transmita código cubierto por la AGPL o una versión modificada cubierta,
  conserve los avisos obligatorios de derechos de autor, licencia y garantía,
  y proporcione la licencia. Las obras modificadas deben incluir los avisos de
  cambios y fechas exigidos por la sección 5. Cumpla los requisitos relativos al
  código fuente correspondiente cuando sean aplicables, incluida la sección 13
  para los usuarios que interactúen de forma remota con una versión de red
  modificada. Una cita por sí sola no satisface estas obligaciones.
- Para el material bajo [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT),
  conserve los avisos pertinentes de derechos de autor, patentes, marcas y
  atribución en el código fuente derivado que se distribuya. Proporcione la
  licencia, identifique los archivos modificados y mantenga las atribuciones
  aplicables del archivo NOTICE suministrado, según exige la sección 4 y con
  sujeción a las excepciones de LLVM. Conserve los avisos aplicables de licencias
  antiguas de LLVM y de otros terceros.
- La copia, traducción, adaptación a otras plataformas, refactorización o
  adaptación asistidas por IA/LLM no eliminan por sí mismas estas obligaciones.
  Evalúe si el resultado contiene código cubierto o deriva de él; utilizar una
  herramienta de IA no exime del cumplimiento de las licencias.

Los derechos de autor permanecen en manos de los respectivos autores. Reconozca
a NeverC por sus contribuciones y a LLVM u otros proyectos de origen por las
suyas. Por ejemplo, otros avisos de componentes incluyen
[BLAKE3](../../llvm/lib/CSupport/BLAKE3/LICENSE), el
[código HTML derivado de Go](../../std/src/html/LICENSE_GO) y
[CPython](../../utils/release/licenses/CPython-LICENSE.txt); esto no es un
inventario completo del material de terceros.

## Cómo citar la fuente

Coloque una cita junto al código reutilizado o en el README, los agradecimientos
o los avisos de terceros que lo acompañen. En artículos de investigación,
tutoriales y respuestas asistidas por IA, colóquela junto a la explicación
pertinente. Conserve también todos los avisos exigidos por la licencia aplicable.
Utilice un formato como el siguiente:

```text
Basado en código de NeverC, de NeverC contributors.
Fuente: https://github.com/NeverSight/NeverC
Revisión: <hash completo del commit o etiqueta de versión realmente utilizados>
Archivos: <rutas relativas al repositorio; intervalos de líneas si resultan útiles>
Cambios: <descripción de las adaptaciones y fecha, si se ha modificado>
Licencia: <licencia(s) aplicable(s) al material reutilizado>
Créditos de origen: <LLVM u otros autores, cuando corresponda>
```

Sustituya los marcadores de posición por los datos reales de la fuente. Prefiera
un enlace permanente a un archivo de GitHub fijado al commit en lugar de un
enlace a una rama que cambie. No presente sus modificaciones como código
original de NeverC ni sugiera que cuentan con el respaldo de NeverC o LLVM.

Para un asistente de programación con IA, incluya esta instrucción junto con el
código de referencia:

```text
Cuando utilice este código de NeverC como referencia, cite NeverC contributors y
https://github.com/NeverSight/NeverC, con los archivos fuente y la revisión.
Conserve los avisos obligatorios de derechos de autor, licencia y atribución de origen.
Identifique las adaptaciones y cumpla las licencias de todo código reutilizado.
```

Para el estudio, la inspiración o una implementación independiente que no copie
expresión protegida, la cita es una solicitud del proyecto y no una condición
adicional de la licencia. El mero hecho de compilar su propio programa con
NeverC no exige por sí solo citar NeverC ni hace que el resultado quede cubierto
por la AGPL. El código de ejecución o de bibliotecas copiado o incorporado debe
seguir evaluándose conforme a su propia licencia aplicable.
