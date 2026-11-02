# 12 — الـInstaller: ملف `.exe` واحد، Next → Next → Install

> الهدف: تحمّل ملف واحد، تدوس Next كذا مرة، وخلاص — البلجن في مكانه والـDAW بيلاقيه،
> وفيه Uninstall في Add/Remove Programs. زي أي شركة.

---

## 1. اللي بتعمله في خطوة واحدة

```powershell
cd <repo>
.\installer\build_installer.ps1
```

السكربت بيعمل تلات حاجات بالترتيب:

1. يبني البلجن (بينادي `plugin\build_windows.ps1` ويشغّل الـ18 بوابة — **مش هيكمّل لو فشلت**).
2. يجمّع الناتج في `installer\staging\` (الـVST3 bundle + الـStandalone exe).
3. يشغّل **Inno Setup** ويطلّع:

```
dist\AEQ_Restore_Setup_1.0.0.exe          (~2–4 MB)
dist\AEQ_Restore_Setup_1.0.0.sha256
```

محتاج حاجة واحدة زيادة على متطلبات البناء: **Inno Setup 6.3+**

```powershell
winget install JRSoftware.InnoSetup
```

> السكربت بيقرأ إصدار ISCC وبيقف برسالة واضحة لو النسخة أقدم من 6.3
> (لأن الإصدارات الأقدم مش بتفهم `x64compatible`).

---

## 2. اللي المستخدم بيشوفه

| الشاشة | إيه فيها |
|---|---|
| Welcome | اسم البلجن + الإصدار + الشعار (أيقونة الكيرف) |
| License | نص الرخصة (لازم يوافق) |
| Select Tasks | ☑ VST3 — ☑ Standalone — ☐ Reference curve + quick start |
| Ready | ملخص المساحة والمكان |
| Installing | شريط تقدم عادي |
| Finish | "افتح الـDAW واعمل Rescan" + زرار يفتح الـStandalone |

**اللي بيتسطب فين:**

| الحاجة | المكان |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\AEQ Restore.vst3` |
| Standalone + الملفات | `C:\Program Files\AEQ Restore\` |
| اختصارات | Start Menu + Desktop |
| إزالة | Settings → Apps → AEQ Restore (أو الاختصار) |

**سلوك مهم:** الـinstaller بيوقف الـDAW المفتوح لو البلجن محمّل جواه (`CloseApplications`)
— لأن ويندوز مش بيسمح بكتابة فوق DLL مفتوح، وفشل التسطيب بنص رسالة غامضة أسوأ من سؤال
"اقفل الـDAW؟".

**Upgrade:** الـ`AppId` ثابت، فنسخة جديدة **بتستبدل** القديمة ومش بتتسطب جنبها.
**Uninstall:** بيشيل مجلد الـ`.vst3` كامل (مش بيسيب فولدرات فاضية)، وسايب مشاريعك في حالها.

---

## 3. الحاجات اللي **لازم** تعرفها قبل ما توزّع

### 3.1 التوقيع — الفرق بين "برنامج شركة" و"ملف مشبوه"

الـinstaller **غير موقّع**، فـWindows SmartScreen هيقول للمستخدم:

> Windows protected your PC → **More info** → **Run anyway**

ده طبيعي لأي ملف جديد ومش موقّع، ومش معناه إن فيه فيروس. بس لو عايز الشكل الاحترافي
الحقيقي (زي Waves أو FabFilter) لازم **شهادة توقيع كود**:

| النوع | التكلفة التقريبية | الأثر |
|---|---|---|
| OV (Organization Validation) | ‎$100–250/سنة | SmartScreen بيبني سمعته مع الوقت/عدد التحميلات |
| EV (Extended Validation) | ‎$300–500/سنة | سمعة فورية عند Microsoft — مفيش تحذير من أول يوم |

بعد ما تجيبها:

```powershell
.\installer\build_installer.ps1 -Sign    # بيستخدم signtool + timestamp
```
وتحتاج تحدد الشهادة مرة واحدة: `signtool` بياخدها من المتجر أو من ملف `.pfx`
(`$env:AEQ_CERT`). لو مش عايز تشتري دلوقتي: وزّع على ناس تعرفهم، وقولهم يدوسوا
More info → Run anyway.

### 3.2 رخصة JUCE — قبل أي توزيع عام

البلجن مبني على **JUCE 8**، وهو مزدوج الرخصة: **AGPLv3** أو **رخصة تجارية مدفوعة**.
لو هتوزّع الـinstaller للناس بشكل عام:

- **AGPLv3** → لازم المصدر يبقى متاح للجميع تحت AGPL (الكود بتاعنا موجود أصلاً في الريبو،
  فالموضوع مش كارثة — بس لازم يكون واضح ومقصود).
- **رخصة JUCE تجارية** → لو عايز تسيب كودك مقفول. بتتحسب على حسب حجم الشركة.

ده مش رأي قانوني، بس لازم تعرفه قبل أول توزيع. الاستخدام الشخصي مش مشكلة.

### 3.3 رخصة البرنامج نفسه

`installer/LICENSE.txt` مكتوب كنموذج (Grant / Restrictions / No warranty).
عدّله قبل التوزيع — أنا مش محامي.

---

## 4. الملفات

| الملف | إيه فيه |
|---|---|
| `installer/AEQ_Restore.iss` | سكربت Inno: الشاشات، المهام، المسارات، الـUninstall |
| `installer/build_installer.ps1` | أمر واحد: بناء → تجميع → installer → SHA256 |
| `installer/make_assets.py` | بيولّد الأيقونة وصور الـwizard **من `presets/target_curve.csv`** نفسه |
| `installer/aeq.ico` | أيقونة 7 مقاسات (16 → 256) — BMP داخلي، مقبول في كل ويندوز |
| `installer/wizard.bmp` / `wizard_small.bmp` | صور شاشات التسطيب (BMP لتوافق مع كل نسخ Inno) |
| `installer/LICENSE.txt` | نص الرخصة (نموذج) |
| `installer/QUICKSTART.txt` | ورقة بدء سريع عربي/إنجليزي — بتتسطب مع البلجن |
| `installer/staging/` | مجلد مؤقت بيتبني تلقائياً (مش في git) |
| `dist/` | الـinstaller النهائي + الهاش (مش في git) |

الأيقونة بتتولد من كيرف الهدف الحقيقي — يعني لو المعيار اتغيّر، الأيقونة بتتغيّر معاه
(`python installer/make_assets.py`).

---

## 4.5 من غير ما تبني على جهازك: GitHub Actions

الـ`.exe` بيتولّد كمان على ويندوز في السحابة. `.github/workflows/build-windows.yml` بيعمل:
بناء البلجن (بنفس `build_windows.ps1` اللي بتشغله إنت) → 18 بوابة → Inno Setup → الـinstaller.

**تنزيل الناتج:**

- **الطريقة العادية:** افتح `Actions` في الريبو → آخر run ناجح → `Artifacts` →
  `AEQ_Restore_Setup`. الملف جوّه zip.

- **لو الأرتيفاكت مش موصّل عندك:** اكتب `[exe]` في رسالة أي commit وارفع — الـworkflow
  هيلزق الـsetup على فرع `ci/artifacts`، وبعدها:

```bash
git fetch origin ci/artifacts
git show FETCH_ORIGIN_HEAD:ci-artifacts/AEQ_Restore_Setup_1.0.0.exe > AEQ_Restore_Setup_1.0.0.exe
```

**ملاحظة مهمة عن المعمارية:** ملف التسطيب نفسه **32-bit** — ده عادي ومقصود،
Inno Setup بيطلّع installer 32-bit دايماً. **البلجن جوّاه 64-bit** وبيتسطب في
`Common Files\VST3` الصح. لو شفت "x86" في خصائص الملف، ده الـinstaller مش البلجن.

## 5. أوامر مفيدة

```powershell
# بناء الـinstaller بس من غير إعادة بناء البلجن
.\installer\build_installer.ps1 -SkipBuild

# إصدار جديد
.\installer\build_installer.ps1 -Version 1.0.1

# تجديد الأيقونة/الصور بعد تغيير الكيرف
python .\installer\make_assets.py

# تسطيب صامت (للمختبرات أو السكربتات)
.\dist\AEQ_Restore_Setup_1.0.0.exe /VERYSILENT /NORESTART /SUPPRESSMSGBOXES

# إزالة صامتة
"C:\Program Files\AEQ Restore\unins000.exe" /VERYSILENT
```

**قبل التوزيع:** جرّب التسطيب وإنت **مش admin**؟ لأ — التسطيب محتاج admin (بيعتمد)
وده الطبيعي للـplugins. بس جرّب السيناريو ده على جهازك: تسطيب → فتح DAW → Rescan →
شغل → إزالة → تسطيب تاني. لو الأربعة عدّوا من أول مرة، يبقى الـinstaller سليم.

---

## 6. حدود واضحة

| مش موجود | ليه |
|---|---|
| AAX (Pro Tools) | محتاج اشتراك Avid — مش مطلوب للمرحلة دي |
| VST2 | Steinberg وقّف ترخيصه |
| نسخة 32-bit | مفيش DAW حديث محتاجها |
| تسطيب تلقائي في كل مجلدات الـplugins | المسار الرسمي واحد، وأي DAW محترم بيقرأ منه |
| إضافة البلجن لـplugin cache بتاع الـDAW | مستحيل نعرفه لكل DAW — المستخدم بيدوس Rescan |

**الخلاصة:** الـinstaller جاهز ككود؛ اللي ناقص هو **أول تشغيل على ويندوز** (البناء +
التسطيب) و**شهادة توقيع** لو عايز شكل تجاري بجد.

---

## 7. الإصدارات المسلَّمة (مُتحقَّق منها بالـhash)

| الإصدار | المحرك | الحجم | SHA-256 |
|---|---|---|---|
| 1.0.0 | كاشف المنطقة القديم (طيف خام، تخنة عمياء) | 4,961,572 بايت | `D2B7A5BC…03A853` |
| **1.1.0** | **قرارات المظروف** (عكارة/صندوقية على المظروف، تخنة نسبية، قراءة σ) | 4,973,053 بايت | `C5C53496…82EF78` |

الإصدار 1.1.0 خرج من بناء ويندوز رقم `41455029657` (ناجح) — البوابات الـ15 كلها
ناجحة على نفس الشجرة. الملفات في `dist/` ومعاها ملف `.sha256` للتحقق.

**قبل ما تسطّب:** النسخة غير موقّعة بشهادة، فهيظهر تحذير SmartScreen:
**More info → Run anyway**. ده متوقع، ومش عيب في الـinstaller.

---

## 8. الإصدارات (Releases) — من هنا بتنزّل

كل إصدار بيتنشر تلقائياً في صفحة **Releases** على الريبو، وفيه ملف التسطيب نفسه
جاهز للتنزيل:

```
https://github.com/MoGeddo/AI-EQ-PLUGIN-VST/releases/latest
```

كل Release فيه:

| الملف | إيه هو |
|---|---|
| `AEQ_Restore_Setup_<version>.exe` | الـinstaller (Next → Next → Install) |
| `AEQ_Restore_Setup_<version>.sha256` | البصمة للتحقق إن الملف نزل سليم |

### إزاي إصدار جديد ينزل «واحد ورا التاني»

الـworkflow بيقرا رقم الإصدار من `installer/AEQ_Restore.iss`، ولو الإصدار ده
**مش منشور قبل كده** بيعمله Release ويحط الملفات جواه، ومعه قائمة اللي اتغير من
آخر tag. ولو الإصدار **موجود بالفعل** بيقف ويقول: "الإصدار ده منشور — كبّر الرقم".

يعني الإصدارات **مابتتغيّرش من تحت** حد نزّلها. عشان تنزّل نسخة جديدة:

1. غيّر الرقم في **مكانين** (لازم الاتنين يتطابقوا):
   - `installer/AEQ_Restore.iss` → `#define AppVersion "1.2.0"`
   - `plugin/CMakeLists.txt` → `project(AEQRestore VERSION 1.2.0 ...)`
   - `installer/build_installer.ps1` و`.github/workflows/build-windows.yml` بياخدوا
     الرقم من نفس الـworkflow
2. ادفع الكوميت، والـCI هيبني، يشغّل الـ15 بوابة، ويعمل الـRelease.

**التحقق بعد التنزيل** (PowerShell):

```powershell
Get-FileHash .\AEQ_Restore_Setup_1.2.0.exe -Algorithm SHA256
# قارن بالـsha256 اللي في صفحة الـRelease
```
