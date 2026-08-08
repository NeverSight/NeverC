**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← فهرس التوثيق](../README.ar.md) · [← مشروع NeverC](../../README.md)

# `neverc build` / `neverc make`

يوفر NeverC مشغّل بناء **متوافقًا مع GNU Make**. `neverc build` و`neverc make`
أمر واحد: يقرآن Makefile ويوسّعان المتغيرات/الدوال وينفّذان الوصفات.
مجلد [`examples/`](../examples/README.ar.md) مكتوب لهذا المسار.

هذا **ليس** أداة مشاريع `neverc.toml`. مرّر خيارات Make العادية و`VAR=value`.

## الصيغة

```text
neverc build [options] [target...]
neverc make  [options] [target...]
```

```bash
cd examples/linux-hello
neverc make
neverc make clean
neverc make NEVERC=/path/to/neverc TARGET=aarch64-linux-gnu
```

الخيارات: `neverc make --help`.

## الخيارات

| الخيار | المعنى |
|--------|--------|
| `-f FILE` | قراءة Makefile المحدد |
| `-j [N]` | مهام متوازية (`-j` وحده = عدد المعالجات) |
| `-C DIR` | تغيير الدليل قبل القراءة |
| `-n`, `--dry-run` | طباعة دون تنفيذ |
| `-k`, `--keep-going` | المتابعة بعد الأخطاء |
| `-s`, `--silent` | عدم عرض الوصفات |
| `-B`, `--always-make` | إعادة بناء الكل |
| `-p` | طباعة قاعدة القواعد/المتغيرات |
| `VAR=VALUE` | متغير سطر أوامر |
| `-h`, `--help` | عرض المساعدة |

## اكتشاف Makefile

بدون `-f`، بهذا الترتيب: `GNUmakefile` → `makefile` → `Makefile`.

## سطح Make المدعوم (ملخص)

قواعد وأنماط، `.PHONY`، بادئات الوصفات، التعيينات، الشروط،
`include`/`export`، ودوال شائعة (`subst`، `patsubst`، `wildcard`،
`foreach`، `call`، `eval`، `shell`، …). `MAKE_VERSION` يبلّغ `4.3`.
مجموعة فرعية مقصودة وليست GNU Make كاملًا.

## Makefile نموذجي

```make
NEVERC ?= neverc
TARGET  = x86_64-linux-gnu
OUTPUT  = hello
SRCS    = main.c

FLAGS = --target=$(TARGET) -O2

all: $(OUTPUT)

$(OUTPUT): $(SRCS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
```

أمثلة الترجمة المتقاطعة تمرّر غالبًا `ARCH=…` أو `TARGET=…`. انظر
[Examples](../examples/README.ar.md).

## أوامر ذات صلة

| الأمر | الاستخدام |
|-------|-----------|
| `neverc file.c -o out` | ترجمة بلا Makefile |
| [`neverc run`](../run/README.ar.md) | ترجمة وتشغيل مؤقت على المضيف |
| [`neverc runtime`](../runtime/README.ar.md) | تثبيت sysroot للترجمة المتقاطعة |
| [الإصدار و`--strip`](../release-builds/README.ar.md) | تجريد الصورة النهائية |
