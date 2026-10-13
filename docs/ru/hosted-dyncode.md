**Languages**: [English](../hosted-dyncode.md) | [简体中文](../zh-CN/hosted-dyncode.md) | [繁體中文](../zh-TW/hosted-dyncode.md) | [日本語](../ja/hosted-dyncode.md) | [한국어](../ko/hosted-dyncode.md) | [Français](../fr/hosted-dyncode.md) | [Deutsch](../de/hosted-dyncode.md) | [Español](../es/hosted-dyncode.md) | [Italiano](../it/hosted-dyncode.md) | [Русский](hosted-dyncode.md) | [العربية](../ar/hosted-dyncode.md)

[← DynCode](dyncode-compiler.md)

# Hosted DynCode и перевод C++

Для функций без секций данных в существующих программах используйте `-fdyncode-hosted`. Нужен `-fdyncode-keep-obj`: CFI/SEH компилятора сохраняются в объекте, но исключаются из плоского кода. Одна секция кода и вход с нулевым смещением сохраняют связь с записями развёртки.

Режим отключает неявное преобразование syscall/PEB, heap arena и строкового runtime. Изменяемые/TLS-глобальные переменные и неразрешённые вызовы отклоняются. Данные и callback-указатели передаются параметрами. Автономный DynCode не меняется.

Перевод C++ core принимает `--target` и связывает C23, карту и манифест с этой ABI. `cpp-core-v2` поддерживает документированное подмножество C++17 с шаблонами и обычными конструкторами/деструкторами. Исключения C++, RTTI, виртуальные вызовы и очистка при внешних исключениях не поддерживаются.

```sh
neverc translate --from cpp replacement.cpp --profile cpp-core-v2 \
  --target aarch64-unknown-linux-gnu -o replacement.nc -- -std=c++17
neverc --no-default-config --target=aarch64-unknown-linux-gnu \
  -fdyncode -fdyncode-hosted -fdyncode-entry=replacement \
  -fdyncode-keep-obj=replacement.o -fdyncode-report=replacement.audit.json \
  -o replacement.bin replacement.nc
```

Отчёт содержит `execution_profile="hosted-v1"`, `keep_unwind=true`, `target_triple`. `input_digest` хеширует объект, `output_digest` — итоговый релокированный и проверенный код. Установщик проверяет оба хеша, код и расположение входа и регистрирует настоящие записи развёртки.
