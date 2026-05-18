**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 編譯器](../README.zh-TW.md)

# Shellcode 外掛介面（Plugin SDK）

NeverC 的 shellcode 流水線採用**核心流水線 + 可插拔使用者層**雙重結構。混淆、反反組譯、EDR 規避、分階段編碼器等策略層功能**有意不內建**。

## 1. Finalize 流水線
`finalizeShellcodeBytes` 按序處理：PostExtract 掛鉤 → 壞位元組重寫器鏈 → 字元集編碼器 → 壞位元組稽核 → 大小約束 → PostFinalize 掛鉤。

## 2. 壞位元組重寫器
`registerBadByteRewriteStrategy` 註冊策略。策略必須冪等、確定性、只看位元組流。

## 3. 字元集編碼器
`registerCharsetEncoder` 註冊 `(Name, Encode, Stub, IsCharsetMember)` 元組。`Stub` 和 `Encode` 輸出必須通過字元集驗證。

## 4. 大小 / 對齊 / 填充
`-fshellcode-max-length=`、`-fshellcode-align=`、`-fshellcode-pad=`，無需外掛。

## 5. 三層掛鉤映射
IR 層 6 個掛鉤、MIR 層 3 個掛鉤、位元組流層 2 個掛鉤。

## 6. 註冊位置選擇 + PIC 涵蓋矩陣
越早註冊 = 內建 PIC 修復涵蓋越廣。推薦：字串加密用 `RunAfterPrep`、CFF 用 `RunAfterInlining`、指令替換用 `RunAfterPreEmit`、全載荷加密用 `RunPostFinalize`。多庫共存用 get/modify/set 模式。
