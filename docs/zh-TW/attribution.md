**語言**: [English](../attribution.md) | [简体中文](../zh-CN/attribution.md) | [繁體中文](attribution.md) | [日本語](../ja/attribution.md) | [한국어](../ko/attribution.md) | [Français](../fr/attribution.md) | [Deutsch](../de/attribution.md) | [Español](../es/attribution.md) | [Italiano](../it/attribution.md) | [Русский](../ru/attribution.md) | [العربية](../ar/attribution.md)

[← 文件索引](README.md) · [← NeverC 專案](project.md)

# NeverC 引用與出處

使用 NeverC 作為參考時，請註明 **NeverC contributors（NeverC 貢獻者）**，連結至
[NeverC](https://github.com/NeverSight/NeverC)，並列出實際使用的檔案及提交版本或
發行版本。這項引用請求適用於人工撰寫的成果、AI/LLM 輔助成果、LLVM pass、編譯器或
連結器實作、外掛程式、文件和研究。[CITATION.cff](../../CITATION.cff) 提供專案引用
中繼資料，[NOTICE](../../NOTICE) 提供專案署名與來源資訊。

## 授權條款義務

儲存庫預設採用 [GNU AGPL 第 3 版](../../LICENSE)。檔案或元件明確標示的授權條款仍
適用於各自的內容，包括 LLVM 目錄以外源自 LLVM 的程式碼。本指南說明既有義務並提出
引用請求，不增加授權限制。

- 散布 AGPL 涵蓋的程式碼或受其規範的修改版本時，須保留規定的著作權、授權條款及
  無擔保聲明，並提供授權條款。修改後的著作須附上第 5 條要求的修改內容與日期聲明。
  適用時，須履行提供對應原始碼的要求，包括第 13 條對透過網路與修改版本遠端互動的
  使用者所訂的要求。僅列出引用不能替代這些義務。
- 對採用 [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT) 的內容，須在散布的
  衍生原始碼中保留相關著作權、專利、商標及署名聲明。須依第 4 條提供授權條款、標明
  已修改的檔案，並保留隨附 NOTICE 中適用的署名，同時遵循 LLVM 例外條款。適用的
  舊版 LLVM 聲明與其他第三方聲明也須保留。
- 透過 AI/LLM 輔助複製、翻譯、移植、重構或改作，本身不會免除上述義務。應判斷
  輸出是否包含受授權條款規範的程式碼，或是否衍生自這類程式碼；使用 AI 工具並非
  授權豁免。

著作權仍歸各自作者所有。NeverC 的貢獻應註明 NeverC；LLVM 或其他上游專案的貢獻
應註明相應專案。其他元件聲明例如 [BLAKE3](../../llvm/lib/CSupport/BLAKE3/LICENSE)、
[源自 Go 的 HTML 程式碼](../../std/src/html/LICENSE_GO) 和
[CPython](../../utils/release/licenses/CPython-LICENSE.txt)；這並非完整的第三方內容清單。

## 如何引用出處

請在重用程式碼附近，或隨附的 README、致謝、第三方聲明中寫明引用。論文、教學及 AI
輔助回答應在相關討論旁註明。同時保留適用授權條款要求的聲明。可採用以下格式：

```text
本實作基於 NeverC contributors 撰寫的 NeverC 程式碼。
出處：https://github.com/NeverSight/NeverC
版本：<實際使用的完整提交雜湊值或發行標籤>
檔案：<儲存庫相對檔案路徑；必要時列出行號範圍>
修改：<如有修改，列出改作內容及日期>
授權條款：<重用內容所適用的授權條款>
上游署名：<適用時列出 LLVM 或其他作者>
```

請將預留文字替換為實際來源資訊。優先使用固定至特定提交的 GitHub 檔案永久連結，
而非會隨分支更新而改變內容的連結。不得將自己的修改標示為 NeverC 上游程式碼，
也不應暗示獲得 NeverC 或 LLVM 的背書。

提供參考程式碼給 AI 程式設計助手時，請附上以下指示：

```text
參考此 NeverC 程式碼時，請引用 NeverC contributors 和
https://github.com/NeverSight/NeverC，並寫明原始檔案及版本。
保留必要的著作權、授權條款和上游署名聲明。
標明改作內容，並遵守任何重用程式碼的授權條款。
```

對於學習、獲得啟發，或未複製受著作權保護之表達的獨立實作，引用屬於專案請求，
並非額外授權條件。僅使用 NeverC 編譯自己的程式，本身不會要求引用 NeverC，也不會
使輸出受 AGPL 規範。複製或嵌入的執行階段程式碼與函式庫程式碼，仍須依各自適用的
授權條款判斷。
