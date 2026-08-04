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

`--target neverc` — повседневная stage-1 сборка (встроенные runtime — пустые
заглушки); этого хватает для большей части локальной работы. Чтобы встроить
string / mimalloc / std / NVK в сам бинарник (или получить компилятор как в CI),
запустите umbrella stage-2:

```bash
cmake --build build-neverc --target neverc-embed-runtime-bitcode
```

Подробности двухэтапного bootstrap — в [Builtins](../builtins/README.ru.md).

### Сборка с тестами

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

`check-neverc` зависит от `neverc-embed-runtime-bitcode`, поэтому при первом
запуске тестов bootstrap и перелинковка компилятора выполняются автоматически.
Отдельно вызывать embed не нужно.

---

## Настройка PATH (macOS / Linux)

После сборки исполняемый файл `neverc` находится в `build-neverc/bin/neverc`. Используйте вспомогательный скрипт, чтобы добавить его в `PATH` без необходимости каждый раз вводить полный путь:

```bash
source ./utils/build/neverc-env.sh
```

Теперь можно запускать `neverc` напрямую:

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### Удаление из PATH

Чтобы убрать локальную сборку из `PATH` в текущей сессии оболочки:

```bash
source ./utils/build/neverc-env.sh --remove   # или -r
```

### Постоянная настройка

Автоматически записать строку `source` в rc-файл оболочки (`~/.zshrc`, `~/.bashrc` или `~/.profile`):

```bash
source ./utils/build/neverc-env.sh --install
```

Отменить:

```bash
source ./utils/build/neverc-env.sh --uninstall
```

### Переключение между локальной сборкой и release

Если у вас установлена release (по умолчанию: `~/.neverc`) и есть сборка из исходников, используйте `neverc-env.sh` для переключения активного `neverc` в текущей сессии shell без перезаписи установок:

```bash
source ./utils/build/neverc-env.sh              # локальная сборка (build-neverc/bin)
source ./utils/build/neverc-env.sh --local      # то же самое
source ./utils/build/neverc-env.sh --release    # release (~/.neverc/bin)
source ./utils/build/neverc-env.sh --status     # показать активный neverc
source ./utils/build/neverc-env.sh --remove     # удалить оба из PATH
```

При переключении устанавливается `NEVERC_ENV` в `local` или `release`:

```bash
echo "$NEVERC_ENV"
neverc --version
which neverc
```

Если release установлена в другой prefix, укажите тот же каталог, что и для `install.sh`:

```bash
NEVERC_INSTALL_DIR=$HOME/.neverc-v3389.1.2 source ./utils/build/neverc-env.sh --release
```

По желанию — алиасы в конфигурации shell (замените путь на абсолютный к вашему репозиторию):

```bash
alias neverc-dev='source /path/to/NeverC/utils/build/neverc-env.sh --local'
alias neverc-rel='source /path/to/NeverC/utils/build/neverc-env.sh --release'
```

---

## Windows (CMD)

В Windows используйте скрипт `.bat` (права администратора не требуются):

```cmd
utils\build\neverc-env.bat             &REM добавить в PATH (текущая сессия)
utils\build\neverc-env.bat --remove    &REM удалить из PATH (текущая сессия)
utils\build\neverc-env.bat --global    &REM сохранить в PATH пользователя через setx
utils\build\neverc-env.bat --global -r &REM удалить из PATH пользователя через setx
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
