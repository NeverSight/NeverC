**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Netlink kernel Android

Canal IPC netlink bidireccional. Crea un socket netlink para comunicación usuario↔kernel. Soporta PING (retorna PONG), VERSION (cadena de versión del kernel) y ECHO. Demuestra `nvk_nl_open`, `nvk_nl_reply` y patrón dispatch-callback.

## Compilación

```bash
cd examples/android-kernel-netlink
neverc make          # debug: -g (predeterminado en la primera compilación)
neverc make release  # release: -O2 --strip
neverc make debug    # volver a debug
```

Seleccione otro preset, por ejemplo, con `neverc make KERNEL=612 release`.
El Makefile guarda `KERNEL` y `PROFILE`, por lo que los siguientes
`make push`/`run` conservan el artefacto elegido.

El strip de release está integrado en NeverC y limitado para ser seguro en
módulos del kernel. Elimina DWARF, `.comment` y nombres privados/indefinidos no
necesarios por reubicaciones, pero conserva las tablas de símbolos/cadenas
ET_REL, reubicaciones, importaciones, definiciones globales, `__versions`,
`.codetag.alloc_tags` y el ABI del cargador. No es strip-all ni ofuscación; los
nombres necesarios por reubicaciones pueden permanecer. Firme siempre después
del strip. No haga strip en `clean`, no use `llvm-strip --strip-all` con un
`.ko` ni elimine a ciegas `.codetag.alloc_tags` o `__codetag_*`.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_netlink.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
adb shell su -c 'dmesg | grep neverc_krt_netlink'
```

## Registro del kernel (en vivo)

En el dispositivo, `cat /proc/kmsg` transmite el ring buffer del kernel en tiempo real — similar a **DbgView** en Windows. Úselo cuando `insmod` falle con un error vago o necesite ver el motivo real del rechazo (vermagic, modversions, tamaño de sección, etc.).

Terminal 1 (dejar en ejecución):

```bash
adb shell
su
cat /proc/kmsg
```

Terminal 2:

```bash
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
```

Las líneas nuevas aparecen en el terminal 1 al cargar. Ctrl+C para detener.

Nota: en algunas builds de Android falta `dmesg -w`; `/proc/kmsg` requiere root pero sigue la salida del kernel en directo de forma fiable.

## Descarga

```bash
neverc make rmmod
```

O manualmente:

```bash
adb shell su -c 'rmmod neverc_krt_netlink'
```
