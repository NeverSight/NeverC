**Языки**: [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← ABI плагинов NeverC](README.ru.md)

# Плагины Python

NeverC может загружать исходный файл Python через тот же параметр
`-fplugin=`, что и нативные плагины. Поддержка Python необязательна, поэтому
обычная сборка не получает зависимость от CPython:

```sh
cmake -S llvm -B build -DNEVERC_ENABLE_PYTHON_PLUGINS=ON
cmake --build build --target neverc
```

Для включённой сборки нужны CPython 3.10 или новее и файлы разработки для
встраивания. Установите пакет авторинга командой
`python3 -m pip install ./pluginsdk/python`, добавьте каталог в `PYTHONPATH`
или соберите и установите компонент `neverc-pluginsdk`. NeverC также находит
подготовленный SDK в `<каталог neverc>/../pluginsdk/python`.

## Минимальный плагин

```python
from neverc_plugin import Plugin


@Plugin(id="com.example.minimal", name="Minimal Python Plugin", version="1.0.0")
class MinimalPlugin:
    def on_process_begin(self, ctx):
        ctx.state = {"sessions": 0}
```

Загрузите его по пути в файловой системе:

```sh
neverc -fplugin=/absolute/path/to/minimal.py -fsyntax-only input.c
```

Декоратор принимает канонический ID плагина, непустое отображаемое имя и
строгую семантическую версию. Один скрипт объявляет ровно один класс плагина.
Разные скрипты являются независимыми модулями и могут сочетаться с нативными
плагинами.

## Жизненный цикл

Все hooks необязательны:

- `on_process_begin(ctx)` и `on_destroy(ctx)` обрамляют процесс компилятора.
- `register(ctx)` регистрирует опции и observers до заморозки графа фаз.
- `on_session_begin(ctx)` и `on_session_end(ctx)` обрамляют один запуск.
- `on_task_begin(ctx)` и `on_task_end(ctx)` обрамляют единицу работы компилятора.

Begin-hook может вернуть значение Python или присвоить `ctx.state`; парный
end-hook увидит это значение. Остальные hooks и observer callbacks должны
возвращать `None`. Плагины Python v1 работают session-serial и без reentrancy.

## Опции и observers

```python
from neverc_plugin import Plugin
from neverc_plugin.domains import driver


@Plugin(id="com.example.trace", name="Trace", version="1.0.0")
class TracePlugin:
    def register(self, ctx):
        ctx.option(
            "--trace-python",
            kind="flag",
            value_type="bool",
            help="Trace raw driver arguments",
        )
        ctx.observer(
            driver.RAW_ARGUMENTS,
            when=("before", "after"),
            fn=self.observe,
        )

    def observe(self, frame):
        if frame.option_values("--trace-python"):
            frame.check_cancelled()
            frame.emit_remark(f"arguments: {frame.arguments}", code=1001)
```

`neverc_plugin.phases` содержит все 130 встроенных констант фаз, сгенерированных
из нормативной схемы. Observer frames предоставляют данные фазы и маршрута,
непрозрачные handles входа/выхода, разобранные опции, диагностику, проверку
отмены и исходные аргументы для `driver.RAW_ARGUMENTS`. Нативные handles
контекста и frame проверяют lifetime: использование сохранённого объекта после
callback вызывает `RuntimeError`.

Kinds опций: `flag`, `joined`, `separate`, `multi_arg`; типы значений: `bool`,
`int`, `uint`, `string`, `enum`, `path`; multiplicity: `single`, `last_wins`,
`append`. Для enum передаётся mapping `enum_values={имя: целое}`.
`argument_count` применяется только к `multi_arg`.

## Ошибки, безопасность и текущий объём

Необработанное исключение Python превращается в
`NEVERC_STATUS_PLUGIN_EXCEPTION`. В активном session/task callback NeverC
выводит форматированный traceback как структурированную диагностику; ошибки
import и activation включают его в сообщение loader. Встроенный интерпретатор
общий для процесса и намеренно не финализируется, а объекты отдельного плагина
освобождаются при выгрузке.

Плагины Python — доверенные расширения компилятора. Они выполняются в процессе,
могут импортировать любые модули и имеют те же системные права, что и NeverC.
Sandbox отсутствует.

Версия v1 намеренно доступна только для чтения, кроме регистрации опций. Она не
предоставляет interceptors, providers, изменение artifacts, доменные модели
IR/MIR/Link, subinterpreters, manifests или точки входа module/factory. Для
этого нужны wrappers транзакций и continuations с проверяемым lifetime; когда
такие возможности необходимы, остаётся доступен нативный C ABI.
