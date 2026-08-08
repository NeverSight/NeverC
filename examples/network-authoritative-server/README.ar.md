<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← أمثلة NeverC](../../docs/examples/README.ar.md)

# خادم لعبة موثوق

يستخدم هذا المثال القابل للتشغيل مكدس شبكة NeverC المحمول بدلاً من
مقابس المنصة الخام. يوفر:

- مستوى تحكم TCP يصدر رموز جلسة مدعومة بـ CSPRNG؛
- مستويات إدخال فورية UDP وQUIC أصلي؛
- حلقة محاكاة موثوقة 60 Hz وطابور إدخال محدود؛
- حماية من إعادة تشغيل الطابع الزمني/nonce للانضمام وأرقام تسلسل إدخال
  متزايدة لكل جلسة.

البناء للمضيف، أو تعيين أي triple هدف NeverC مدعوم:

```bash
neverc make          # debug: ‏-g (الافتراضي في أول بناء)
neverc make release  # release: ‏-O2 --strip
neverc make debug    # العودة إلى debug
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=authoritative-server.exe
```

يحفظ Makefile قيمة `PROFILE`، لذلك تبقي أوامر `neverc make` اللاحقة نفس
اختيار debug/release. يستخدم الإصدار `--strip` المدمج في NeverC.
انظر [بناء الإصدار](../../docs/release-builds/README.ar.md).


التشغيل بشهادة TLS P-256 ومفتاح لنقطة نهاية QUIC:

```bash
./authoritative-server cert.pem key.pem
```

العناوين الاختيارية الافتراضية: TCP `:7000` وUDP `:7001` وQUIC `:7002`.
سطر الانضمام TCP هو
`JOIN <client-id> <unix-ms> <32-hex-nonce>`. إدخال UDP هو
`INPUT <32-hex-session-token> <sequence> <dx> <dy>`. عميل QUIC يتفاوض على
`neverc-game/1`، ويصادق على أول تدفق بـ `AUTH <token>`، ثم يرسل
datagrams تحتوي `<sequence> <dx> <dy>`.

</div>
