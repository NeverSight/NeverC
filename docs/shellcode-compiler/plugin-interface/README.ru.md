**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Компилятор shellcode](../README.ru.md)

# Интерфейс плагинов Shellcode (Plugin SDK)

Конвейер NeverC имеет структуру **конвейер ядра + подключаемый пользовательский слой**. Обфускация, антидизассемблирование, обход EDR и т.д. **намеренно не встроены**.

## 1. Конвейер финализации
Хук PostExtract → цепочка перезаписчиков запрещённых байтов → кодировщик charset → аудит запрещённых байтов → размер → хук PostFinalize.

## 2. Перезаписчик запрещённых байтов
`registerBadByteRewriteStrategy`. Идемпотентный, детерминированный, только байтовый поток.

## 3. Кодировщик charset
`registerCharsetEncoder` с `(Name, Encode, Stub, IsCharsetMember)`. Вывод должен проходить проверку charset.

## 4. Размер / выравнивание / заполнение
`-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`.

## 5–6. Маппинг хуков и матрица PIC
11 хуков (6 IR + 3 MIR + 2 байт). Более ранняя регистрация = более широкое покрытие PIC. Рекомендация: шифрование строк → `RunAfterPrep`; CFF → `RunAfterInlining`; подстановка инструкций → `RunAfterPreEmit`; шифрование полезной нагрузки → `RunPostFinalize`.
