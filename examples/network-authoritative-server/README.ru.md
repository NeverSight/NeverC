**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Авторитетный игровой сервер

Этот исполняемый пример использует переносимый сетевой стек NeverC вместо
сырых платформенных сокетов. Он предоставляет:

- плоскость управления TCP, выдающую токены сессий на основе CSPRNG;
- плоскости ввода в реальном времени UDP и нативного QUIC;
- авторитетный цикл симуляции 60 Гц и ограниченную очередь ввода;
- защиту от повторного воспроизведения timestamp/nonce для подключений и
  монотонные порядковые номера ввода для каждой сессии.

Сборка для хоста или с любым поддерживаемым целевым triple NeverC:

```bash
neverc make          # debug: -g (по умолчанию при первой сборке)
neverc make release  # release: -O2 --strip
neverc make debug    # вернуться к debug
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=authoritative-server.exe
```

Makefile сохраняет `PROFILE`, поэтому последующие `neverc make` оставляют тот
же выбор debug/release. Release использует встроенный `--strip` NeverC.
См. [Release-сборки](../../docs/release-builds/README.ru.md).


Запуск с TLS-сертификатом и ключом P-256 для QUIC-эндпоинта:

```bash
./authoritative-server cert.pem key.pem
```

Необязательные адреса по умолчанию: TCP `:7000`, UDP `:7001` и QUIC `:7002`.
Строка подключения TCP:
`JOIN <client-id> <unix-ms> <32-hex-nonce>`. Ввод UDP:
`INPUT <32-hex-session-token> <sequence> <dx> <dy>`. QUIC-клиент согласует
`neverc-game/1`, аутентифицируется на первом потоке с `AUTH <token>`, затем
отправляет датаграммы, содержащие `<sequence> <dx> <dy>`.
