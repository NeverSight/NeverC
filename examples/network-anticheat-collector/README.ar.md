<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← أمثلة NeverC](../../docs/examples/README.ar.md)

# جامع تيليمتري مكافحة الغش mTLS

يقدّم هذا الجامع القابل للتشغيل بروتوكول NRPC متعدد القنوات عبر TLS 1.3
مع شهادات عميل إلزامية. `anticheat.Telemetry/Collect` هو تدفق ثنائي
الاتجاه: ترسل الوكلاء سجلات تيليمتري موقّعة وتستقبل ACK nonce واحد لكل
سجل مقبول.

كل رسالة DATA تتكون من رأس 64 بايت يتبعه جسم غير شفاف (بحد أقصى 1 MiB).
يحتوي الرأس على الإصدار `1`، وثلاثة بايتات صفر، وطابع زمني Unix
بالميلي ثانية من ثمانية بايتات big-endian، وnonce من 16 بايت، وطول جسم
من أربعة بايتات big-endian، وHMAC-SHA256 من 32 بايت. يغطي MAC
`agent-id || first-32-header-bytes || body`. قيمة البيانات الوصفية NRPC
`agent-id` مطلوبة. تُقبل nonces مرة واحدة فقط ضمن نافذة زمنية مدتها 30
ثانية.

تدخل السجلات المقبولة في طابور محدود. يضيف كاتب واحد مخصص حدث تدقيق
JSONL يتضمن بصمة شهادة العميل وnonce وملخص الجسم والطابع الزمني وحجم
الجسم؛ لا يكتب الجامع أبداً بايتات الجسم الخام غير الموثوقة مباشرة في
سجل التدقيق.

البناء للمضيف أو أي هدف مدعوم:

```bash
neverc make
neverc make TARGET=aarch64-pc-windows-msvc OUTPUT=anticheat-collector.exe
```

التشغيل مع شهادة خادم ومفتاح خادم وCA عميل موثوق ومفتاح توقيع مشترك
32 بايت ومسار تدقيق:

```bash
./anticheat-collector server.pem server-key.pem agent-ca.pem \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  telemetry-audit.jsonl
```

</div>
