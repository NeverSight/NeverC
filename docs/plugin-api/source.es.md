**Idiomas**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

# API de complementos para Source y E/S

La primera ABI pública de complementos expone las entradas de código fuente, los
archivos virtuales, las dependencias y las salidas del compilador a través de
`PluginSource.h`. Todas las rutas son rutas VFS normalizadas y todos los
manejadores están acotados a la tarea `TranslationUnit` actual.

## Fases de source

La canalización estable de source es:

1. `neverc.source.resolve_input` valida y normaliza la entrada solicitada.
2. `neverc.source.open` la abre a través del VFS compuesto de anfitrión y
   complemento.
3. `neverc.source.after_open` publica un evento de solo lectura para el
   `SourceUnit` verificado.

`resolve_input` es observable e interceptable; `open` además es reemplazable. El
anfitrión verifica cada reemplazo antes de publicarlo como `SourceUnit`. Un
complemento no puede reemplazar `after_open`.

## Proveedores de VFS

Consulte `NevercIOAPI` durante el registro del complemento y llame a
`RegisterVFSProvider`. Un proveedor responde primero a `MatchesPath` y luego
implementa las operaciones de las que se hace cargo. Devolver
`NEVERC_VFS_RESULT_NOT_HANDLED` delega en el siguiente proveedor; devolver
`HANDLED` convierte un estado o contenido mal formado en un error grave en lugar
de un retroceso silencioso.

Los búferes que devuelve un proveedor solo se prestan durante la devolución de
llamada. NeverC copia los bytes aceptados a un almacenamiento propiedad de la
tarea. Los proveedores deben declarar si su resultado es determinista y
almacenable en caché.

El ejemplo compilable
[`VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
suministra una cabecera en memoria sin eludir el VFS del anfitrión.

## Sumideros de salida y dependencias

Las salidas a archivo y a memoria usan el mismo sumidero transaccional:

- escribir en un candidato;
- llamar a finish para hacerlo apto para la verificación;
- dejar que la compuerta sellada del anfitrión lo verifique;
- confirmar atómicamente si la tarea tiene éxito, o abortar ante cualquier error
  o cancelación.

Un complemento nunca publica escribiendo directamente en la ruta de destino. Los
destinos de flujo que no admiten reversión rechazan las transformaciones que
exigen un candidato atómico. Los registros de dependencia usan identidades VFS
normalizadas, de modo que los archivos nativos y los suministrados por
complementos comparten la misma procedencia y semántica de caché.

## Reglas de seguridad

- No conserve manejadores de source, archivo, búfer, sumidero o tarea después de
  la devolución de llamada.
- Trate `NevercStringView` y `NevercByteView` como vistas delimitadas por
  longitud.
- Use el asignador del anfitrión cuando los datos deban sobrevivir a la
  devolución de llamada.
- No use las API de sistema de archivos del anfitrión por detrás del contrato
  VFS.
- Compruebe la cancelación antes de un trabajo costoso del proveedor.
