**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC-Plugin-ABI](../README.de.md)

# Benutzerdefinierte Aufrufkonventionen

NeverC unterstützt **datengesteuerte benutzerdefinierte Aufrufkonventionen** — Sie können den Argumenten und Rückgabewerten jeder Funktion beliebige physische Register zuweisen, vollständig aus einem externen Plugin oder über Attribute auf Quellcode-Ebene, ohne den Compiler oder irgendeine TableGen-Definition zu ändern.

## Überblick

Herkömmliche LLVM-Aufrufkonventionen sind über `.td` / `.inc`-Dateien fest ins Backend eingebacken. Eine hinzuzufügen oder zu ändern erfordert, die Compiler-Quellen zu bearbeiten und TableGen erneut auszuführen. NeverC ersetzt das durch ein **zur Laufzeit datengesteuertes** Modell aus zwei Schichten:

- Eine **Spec** — eine kurze, von Hand schreibbare Zeichenkette wie `gpr:rcx,rdx;ret:rax` — wird einer Funktion als String-Attribut `"neverc-callconv"` angehängt, entweder von einem Plugin oder von einem Attribut auf Quellcode-Ebene.
- Vor der Codegenerierung **materialisiert** der Host diese Spec zu einem Attribut `"neverc-cc-plan-v1"`: einer unveränderlichen, validierten Tabelle exakter Orte, gebunden an ein bestimmtes Zielschema. Das Backend verarbeitet ausschließlich den Plan.

Die Spec ist das, was Sie schreiben; der Plan ist das, worauf das Backend vertraut. Aufrufkonventionen wandern damit von „zur Übersetzungszeit im Backend hartcodiert“ zu „zur Laufzeit durch externe Richtlinien gesteuert“ — ohne die Überprüfung aufzugeben.

## Spec-Format

Eine Spec ist eine durch Semikolon getrennte Zeichenkette. Jedes Segment besteht aus einem Schlüssel und einer kommagetrennten Liste von Registernamen (Groß-/Kleinschreibung egal, Leerzeichen werden toleriert):

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| Segment | Aliase | Bedeutung |
|---|---|---|
| `args` | | **Positionsmodus**: Jedes Token ist ein Registername oder `stack`/`mem` und wird den Argumenten nach Index zugewiesen |
| `gpr` | `arg_gpr` | **Pool-Modus**: Argumentregister für Ganzzahlen/Zeiger, der Reihe nach belegt; Überlauf geht auf den Stack |
| `xmm` | `arg_xmm` | **Pool-Modus**: Argumentregister für Gleitkomma-/Vektorwerte |
| `fpr` | | Zielneutraler Alias für `xmm` |
| `ret_gpr` | `ret` | Rückgaberegister für Ganzzahlen/Zeiger |
| `ret_xmm` | | Rückgaberegister für Gleitkomma-/Vektorwerte |
| `ret_fpr` | | Zielneutraler Alias für `ret_xmm` |
| `csr` | | Benutzerdefinierter Satz callee-saved Register (Vorgabe: der Standard-ABI-Satz) |

Jedes Segment darf entfallen, unbekannte Segmente werden ignoriert. Die Schlüssel sind genau einmal in `llvm/include/llvm/CodeGen/NeverCCallConv.h` definiert, sodass Erzeuger und Parser nicht auseinanderdriften können.

### Zwei Argumentmodi

**Pool-Modus** (`gpr:` / `xmm:`): Ganzzahlargumente nehmen der Reihe nach Register aus dem `gpr`-Pool; Gleitkomma- und Vektorargumente bedienen sich aus `xmm`. Ist ein Pool erschöpft, gehen die restlichen Argumente auf den Stack.

**Positionsmodus** (`args:`): Argument *i* verwendet das *i*-te Token. Jedes Token ist entweder ein Registername oder `stack` / `mem`, was dieses Argument auf den Stack zwingt:

```
args:rcx,stack,r8;ret:rax   # arg0→rcx, arg1→Stack, arg2→r8, Rückgabe→rax
```

Ist `args` vorhanden, hat es Vorrang vor `gpr` / `xmm`. Ein Token, das die falsche Registerklasse für den Typ des Arguments nennt, ein Index jenseits der Tokenliste und ein bereits belegtes Register führen allesamt auf einen Stack-Platz, statt die Übersetzung scheitern zu lassen.

### Unterstützte Architekturen

Registernamen werden über eine zielspezifische Tabelle aufgelöst — die einzige Quelle der Wahrheit dafür, was eine Spec benennen darf.

| Architektur | GPR-Namen | SIMD-Namen | Breitenwahl |
|---|---|---|---|
| **x86-64** | `rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`, `rbp`, `r8`–`r15` | `xmm0`–`xmm15` | i32 → 32-Bit-Unterregister, i64/Zeiger → 64 Bit |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f16→`h`, f32→`s`, f64→`d`, f128/Vektor→`q` |

GPRs werden stets in ihrer 64-Bit-Schreibweise notiert; das Backend verengt sie auf das Unterregister, das zum Typ des jeweiligen Werts passt. Vektornamen schreibt man auf AArch64 als `v0`–`v31`, und das Backend wählt anhand des Typs die Form `H`/`S`/`D`/`Q`.

### Einschränkungen

- **Reservierte Register**: Der Stackpointer fehlt in beiden Tabellen (`rsp` auf x86-64, `sp`/`x31` auf AArch64), ebenso `x29`/`x30` (FP/LR) auf AArch64. Eine Spec, die eines davon nennt, überspringt es schlicht, und der Wert landet am nächsten gültigen Ort.
- **Frame-Pointer**: `rbp` *ist* auf x86-64 wählbar, denn es ist ein legitimes callee-saved Register; als Argumentregister ist es aber nur unter `-fomit-frame-pointer` unbedenklich. Verwendung auf eigene Gefahr.
- **Callee-saved**: Vorgabe ist der Standard-ABI-Satz. `csr:r12,r13` deklariert einen eigenen Satz, und der Aufrufer baut eine passende Preserved-Register-Maske, damit er weiß, welche Register den Aufruf überleben. Auf x86-64 wie auf AArch64 unterstützt.
- **csr-Konflikte**: Steht ein Register sowohl in `csr` als auch in einer Argument-/Rückgabeliste, warnt das Plugin — der Aufgerufene würde es wiederherstellen und damit seine Rolle als Wertträger zerstören. Die Übersetzung gelingt trotzdem.
- **Variadische Funktionen**: nicht unterstützt. Beide Backends geben eine klare Diagnose aus, statt den variadischen Teil stillschweigend falsch zu übergeben.
- **Indirekte Aufrufe**: Ein Aufruf über Funktionszeiger kann keine benutzerdefinierte Konvention tragen. Das Plugin warnt, wenn die Adresse einer Funktion mit benutzerdefinierter Konvention genommen wird; indirekte Aufrufe fallen auf die Standardkonvention zurück.
- **Tail Calls**: deaktiviert, sobald eine der beiden Seiten eines Aufrufs die benutzerdefinierte Konvention verwendet — auf beiden Backends.
- **Nicht erfasste Werte**: Jedes Argument und jeder Rückgabewert, den der Plan nicht abdeckt, fällt auf die Standardkonvention des Ziels zurück (SysV auf x86-64, AAPCS auf AArch64).

## Verwendung

### 1. Plugin-gesteuert (empfohlen)

Das Referenz-Plugin `CustomCallConvPlugin.c` liegt unter `pluginsdk/examples/`. Es registriert einen modulweiten IR-Pass in der Phase `neverc.ir.pass.post_opt`.

**Plugin bauen:**

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib   # oder .so / .dll
```

**Attributmodus** (Vorgabe) — betroffen sind nur Funktionen mit einer `custom_attr`-Annotation im Quellcode:

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib input.c -o output.o
```

**Globaler Modus** — wendet eine einzige Spec auf jede definierte Funktion an (erfordert ein ausdrückliches `cc-all`):

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

**Nach Namenspräfix filtern:**

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccprefix=secret_ \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r9,r8;ret:rax" \
       input.c -o output.o
```

**Diversifizieren** — vier eingebaute Layouts im Wechsel, damit die Funktionen sich nicht ein einziges teilen (Anti-Reverse-Engineering):

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccshuffle \
       input.c -o output.o
```

Das Plugin registriert vier Optionen: `cc-all` und `ccshuffle` (Flags, `=1` oder `=true` ist also optional) sowie `ccspec` und `ccprefix` (Zeichenkettenwerte). Ohne `ccspec` verwendet der globale Modus die Vorgabe `gpr:r10,r11,rsi,rdi;ret:rdx`.

### 2. Attribute auf Quellcode-Ebene

Annotieren Sie Funktionen direkt in C mit dem Attribut `custom_attr`, in GNU- oder Microsoft-Syntax:

```c
// GNU-Syntax
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

// Microsoft-Syntax
__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

`custom_attr("key", "value")` erzeugt ein sauberes String-Attribut an der Funktion (`"key"="value"`) — **ohne** Warnungen und **ohne** `llvm.global.annotations`. Es ist ein **Allzweckmechanismus**: Jedes Schlüssel/Wert-Paar funktioniert, nicht nur Aufrufkonventionen. IR- und MIR-Passes lesen es mit `F.getFnAttribute("key")` wieder aus.

### 3. Kombiniert

Quellcode-Attribute und Plugin-Argumente arbeiten zusammen. Eine Funktion mit `custom_attr` läuft über den Attributmodus-Pfad des Plugins; `cc-all` deckt den Rest ab. Jede Funktion wird höchstens einmal verarbeitet.

## Materialisierte Pläne

Eine Spec benennt Register; sie sagt nicht, wo jedes Byte jedes Werts liegt. Nach der Optimierungspipeline und vor der Codegenerierung führt der Host `materializeCallingConventionPlans` aus, was jede Funktion mit `CallingConv::NeverC_Custom` in einen exakten, validierten Plan überführt:

- Eine Funktion, die bereits ein Attribut `"neverc-cc-plan-v1"` besitzt, wird **validiert, nicht neu erzeugt** — ihr Schema-Digest, ihre Target-ID und ihre Konventions-ID müssen zum aktuellen Ziel passen.
- Bei einer Funktion mit einer `"neverc-callconv"`-Spec werden die Registernamen gegen die Registertabelle des Ziels aufgelöst. Der entstehende Plan ersetzt die Spec, die danach aus der IR entfernt wird.
- Eine Funktion ohne beides, deren Ziel aber über die Plugin-ABI eine Aufrufkonvention registriert, wird vom `PlanCallingConvention`-Callback dieser Konvention geplant.

Jede direkte Aufrufstelle erbt den Plan ihres Aufgerufenen — das hält Aufrufer und Aufgerufenen über Übersetzungseinheiten hinweg beim selben Layout. Der Plan ist eine flache Zeichenkette:

```
neverc-cc-plan-v1;schema=<Digest>;target=<high>:<low>;cc=<high>:<low>;stack=<Bytes>;returns=<Orte>;arguments=<Orte>;callee-saved=<Registernummern>
```

Jeder Ort lautet `<r|s>,<Wertindex>,<Fragment-Offset>,<Größe>,<Ausrichtung>,<Registernummer>,<Stack-Offset>,<Flags>`, mehrere Orte werden durch `|` getrennt. Auf dem eingebauten Weg ist der Schema-Digest `llvm-<Ziel-Triple>`; ein per Plugin registriertes Ziel liefert seinen eigenen.

Da Registernummern nur gegenüber dem Schema Sinn ergeben, das sie definiert, ist eine Abweichung ein harter Fehler statt einer stillen Fehlübersetzung:

| Situation | Diagnose |
|---|---|
| Die Plan-Zeichenkette lässt sich nicht parsen | `malformed NeverC calling convention plan` |
| Der Schema-Digest weicht ab | `NeverC calling convention plan belongs to a foreign target schema` |
| Die Target-ID weicht ab | `NeverC calling convention plan has a foreign target ID` |
| Die Konventions-ID weicht ab | `NeverC calling convention plan has a foreign convention ID` |

Genau das macht es sicher, einen Plan in Bitcode einzubetten und durch LTO zu tragen: Ein für ein anderes Ziel erzeugter Plan kann nicht versehentlich angewendet werden.

## Plugin-API

Das Beispiel-Plugin nutzt allein die stabile IR-Core-Tabelle — einen eigenen Einstiegspunkt für Aufrufkonventionen gibt es nicht. Eine Konvention auf eine Funktion anzuwenden, sind drei Aufrufe plus die Synchronisierung der Aufrufstellen:

```c
NevercIRAttributeHandle Attribute = {0};
Core->CreateStringAttribute(Core->Context, Task, SV("neverc-callconv"), Spec,
                            &Attribute);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION, 0, Attribute);
Core->SetFunctionCallingConvention(Core->Context, Task, Function,
                                   NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM);
```

`NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM` ist der ABI-stabile Name für `CallingConv::NeverC_Custom` (LLVM-Wert 1000). Anschließend läuft das Plugin mit `GetValueUseCount` / `GetValueUse` die Verwendungen der Funktion ab und setzt für jede Verwendung, die der Callee-Operand eines `call`, `invoke` oder `callbr` ist, dieselbe Konvention per `SetInstructionProperty` mit `NEVERC_IR_PROPERTY_CALLING_CONVENTION` auf der Instruktion. Jede andere Verwendung bedeutet, dass die Adresse entkommen ist — daher die Warnung zur genommenen Adresse.

Ein Plugin, das ein eigenes Ziel registriert, kann stattdessen einen `PlanCallingConvention`-Callback an seinem `NevercCallingConventionDescriptor` bereitstellen und Pläne direkt erzeugen, wodurch die Spec-Schicht entfällt. Siehe [Target, MC, Assembly, Objekte](../target-mc-object.de.md).

## Tests

Die GoogleTest-Suite liegt in `tests/neverc/CustomCallConvTests.cpp` und umfasst 26 Tests. Jeder baut das Beispiel-Plugin, übersetzt ein kleines Programm unter einer gegebenen Spec nach Assembly und prüft die resultierende Register- oder Stack-Platzierung.

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

Abdeckung:

| Kategorie | Tests |
|---|---|
| x86-64 Pool / Position / Stack / Überlauf / i64 / sret / byval / Rückfall | 9 |
| AArch64 GPR / FPR / Stack / `csr` / Aufruf über unterschiedliche Specs hinweg | 5 |
| Frontend `custom_attr` (GNU / `__declspec` / Ende zu Ende) | 3 |
| Plan-Materialisierung und Schema-Zurückweisung | 3 |
| Härtung (`csr`, Varargs auf beiden Zielen, indirekt, `rsp`, csr-Konflikt) | 6 |

## Architektur

```
Quellcode-Attribut            Plugin-IR-Pass
custom_attr(...)              (neverc.ir.pass.post_opt)
       │                            │
       └─────────────┬──────────────┘
                     ▼
   "neverc-callconv" = Spec, CallingConv::NeverC_Custom
   an der Funktion und ihren direkten Aufrufstellen
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ materializeCallingConventionPlans        │
   │ (nach Optimierung, vor Codegen)          │
   │                                          │
   │  Spec            → Namen zu Physregs     │
   │  Plugin-CC       → PlanCallingConvention │
   │  Plan vorhanden  → Schema/Ziel prüfen    │
   └──────────────────────────────────────────┘
                     │
                     ▼
   "neverc-cc-plan-v1" = validierte Orte
   Spec entfernt; Plan auf direkte Aufrufstellen kopiert
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ Backend-CCAssignFn (eine pro Ziel)       │
   │  CC_X86_NeverC     / RetCC_X86_NeverC    │
   │  CC_AArch64_NeverC / RetCC_AArch64_NeverC│
   │                                          │
   │  liest den Plan → weist Orte zu          │
   │  nicht erfasste Werte → Standardkonv.    │
   │  Tail Calls deaktiviert                  │
   └──────────────────────────────────────────┘
                     │
                     ▼
   Maschinencode mit dem eigenen Register-Layout
```

Der Ausführer im Backend ist eine **einmalige Implementierung** — sämtliche Richtlinienentscheidungen leben im Plugin. Eine neue Konvention hinzuzufügen erfordert nie einen Neubau von NeverC.
