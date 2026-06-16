**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Hello

Módulo de kernel Android NeverC mínimo (.ko). Inicializa `kallsyms_lookup_name` vía kprobe, imprime un mensaje de carga y sale limpiamente. Validación de extremo a extremo: compilación → enlace → insmod.

## Compilación

```bash
cd examples/android-kernel-hello
neverc make
```

Cambie `KERNEL` a `515`, `601`, `606` o `612` para otras versiones.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_hello.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hello.ko'
adb shell su -c 'dmesg | grep nvk_hello'
```

## Descarga

```bash
neverc make rmmod
```

O manualmente:

```bash
adb shell su -c 'rmmod nvk_hello'
```
