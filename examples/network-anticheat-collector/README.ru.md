**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Сборщик телеметрии античита mTLS

Этот исполняемый сборщик обслуживает мультиплексированный протокол NRPC
поверх TLS 1.3 с обязательными клиентскими сертификатами.
`anticheat.Telemetry/Collect` — двунаправленный поток: агенты отправляют
подписанные записи телеметрии и получают один nonce ACK на каждую принятую
запись.

Каждое DATA-сообщение состоит из 64-байтового заголовка и непрозрачного тела
(максимум 1 MiB). Заголовок содержит версию `1`, три нулевых байта,
восьмибайтовую метку времени Unix в миллисекундах в формате big-endian,
16-байтовый nonce, четырёхбайтовую длину тела в big-endian и 32-байтовый
HMAC-SHA256. MAC покрывает
`agent-id || first-32-header-bytes || body`. Значение метаданных NRPC
`agent-id` обязательно. Nonce принимаются только один раз в 30-секундном
окне времени.

Принятые записи попадают в ограниченную очередь. Выделенный единственный
писатель добавляет событие аудита JSONL с отпечатком клиентского сертификата,
nonce, дайджестом тела, меткой времени и размером тела; сборщик никогда не
записывает недоверенные сырые байты тела напрямую в журнал аудита.

Сборка для хоста или любой поддерживаемой цели:

```bash
neverc make          # debug: -g (по умолчанию при первой сборке)
neverc make release  # release: -O2 --strip
neverc make debug    # вернуться к debug
neverc make TARGET=aarch64-pc-windows-msvc OUTPUT=anticheat-collector.exe
```

Makefile сохраняет `PROFILE`, поэтому последующие `neverc make` оставляют тот
же выбор debug/release. Release использует встроенный `--strip` NeverC.
См. [Release-сборки](../../docs/release-builds/README.ru.md).


Запуск с серверным сертификатом, серверным ключом, доверенным клиентским CA,
общим 32-байтовым ключом подписи и путём аудита:

```bash
./anticheat-collector server.pem server-key.pem agent-ca.pem \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  telemetry-audit.jsonl
```
