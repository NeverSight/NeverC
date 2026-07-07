**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Указатель документации](../README.ru.md)

# Локальная разработка

Руководство по сборке NeverC из исходного кода и настройке локальной среды разработки.

---

## Требования

- CMake 3.20+
- Ninja
- Компилятор C++17 (GCC, Clang или MSVC)

---

## Сборка

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` автоматически обнаруживается и включается при наличии.

### Сборка с тестами

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

---

## Настройка PATH (macOS / Linux)

После сборки исполняемый файл `neverc` находится в `build-neverc/bin/neverc`. Используйте вспомогательный скрипт, чтобы добавить его в `PATH` без необходимости каждый раз вводить полный путь:

```bash
source ./tools/neverc-env.sh
```

Теперь можно запускать `neverc` напрямую:

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### Удаление из PATH

Чтобы убрать локальную сборку из `PATH` в текущей сессии оболочки:

```bash
source ./tools/neverc-env.sh --remove   # или -r
```

### Постоянная настройка

Автоматически записать строку `source` в rc-файл оболочки (`~/.zshrc`, `~/.bashrc` или `~/.profile`):

```bash
source ./tools/neverc-env.sh --install
```

Отменить:

```bash
source ./tools/neverc-env.sh --uninstall
```

---

## Windows (CMD)

В Windows используйте скрипт `.bat` (права администратора не требуются):

```cmd
tools\neverc-env.bat             &REM добавить в PATH (текущая сессия)
tools\neverc-env.bat --remove    &REM удалить из PATH (текущая сессия)
tools\neverc-env.bat --global    &REM сохранить в PATH пользователя через setx
tools\neverc-env.bat --global -r &REM удалить из PATH пользователя через setx
```

В отличие от Unix-скрипта, `source` не требуется — `.bat` напрямую изменяет текущую сессию `cmd`. `--global` записывает в реестр пользователя через `setx` (права администратора не требуются).

---

## Готовые бинарники для macOS

Релиз подписан сертификатом Apple Developer ID и нотариально заверен Apple. Распакуйте архив и используйте напрямую.

---

## Кросс-компиляция под Windows

NeverC включает SDK всех платформ в `runtime/` (Windows SDK/WDK, Linux sysroot, macOS sysroot, Android NDK); внешняя настройка SDK не требуется.

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

О шеллкоде для Windows (`-fdyncode`, PEB-разрешение импортов и т.д.) см. [документацию dyncode-компилятора](../dyncode-compiler/README.ru.md).

---

## Проверка

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```
