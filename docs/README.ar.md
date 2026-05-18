<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مشروع NeverC](../README.ar.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (shellcode README and breadcrumbs).

# توثيق NeverC

ملاحظات التصميم ومرجع API والأدلة لكل نظام فرعي في NeverC.

---

## مُجمِّع shellcode

مسار تجميع shellcode هو محور أبحاث NeverC الأساسي. للبنية وخيارات CLI ومصفوفة المنصات والأمثلة:

**[مُجمِّع shellcode →](shellcode-compiler/README.ar.md)**

| المستند | الوصف |
|---------|--------|
| [README](shellcode-compiler/README.ar.md) | نظرة عامة، بدء سريع، الأهداف المدعومة |
| [Pipeline & PIC](shellcode-compiler/pipeline-and-pic/README.ar.md) | تصميم IR → كائن → استخراج |
| [IR Pass Design](shellcode-compiler/ir-pass-design/README.ar.md) | مبررات كل مرور IR |
| [MIR Pass Design](shellcode-compiler/mir-pass-design/README.ar.md) | مرورات MIR للخلفية |
| [Kernel-Mode Shellcode](shellcode-compiler/kernel-mode-shellcode/README.ar.md) | تجميع Ring-0 |
| [Plugin Interface](shellcode-compiler/plugin-interface/README.ar.md) | إضافات التشويش والترميز |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.ar.md) | `TargetDesc` والمستخرجات |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.ar.md) | إضافة منصة |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.ar.md) | تعليمات ARM64 من منظور shellcode |
| [Roadmap](shellcode-compiler/roadmap/README.ar.md) | العمل المخطط |
| [Progress](shellcode-compiler/progress/README.ar.md) | حالة التنفيذ |

---

## نوع `string` المدمج

يوفر NeverC نوع قيمة `string` مدمجًا للغة C، يجمع بين سهولة استخدام `std::string` ودعم Unicode على مستوى `QString`. يُفعّل عبر `-fbuiltin-string` (تلقائيًا في وضع `-fshellcode`).

**[السلسلة النصية المدمجة →](builtin-string/README.ar.md)**

</div>
