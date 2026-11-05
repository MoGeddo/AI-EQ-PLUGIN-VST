# ٢٢. جرد صادق: كل اللي اتطلب ⇄ اللي موجود فعلًا

ده مش تسويق. كل سطر تحت إما **بأمر تشغّله** أو **بملف تشوفه** أو **بمكالمة API في الكود**.
والقاعدة: لو مفيش دليل، الحالة **❌**، ولو الدليل جزئي، الحالة **⚠️**.

الخلاصة في سطر واحد: **الغرض من البلجن موجود وشغال ومتسلّم في `.exe`؛ اللي لسه مش موجود هو
إن الشكل اللي إنت وافقت عليه (v9) يبقى هو نفس الشكل اللي جوّه البلجن.**

---

## أ. الغرض نفسه — «أوتوماتيك: يسمع التيك كله، إنت اللي بتوقفه، وبعدها يثبّت»

| الطلب | الحالة | الدليل |
|---|---|---|
| يسمع التيك كله من غير ما تلمسه | ✅ | `test_auto`: «settled by itself after 10.8 s of playback (8 s of sung audio)» |
| وهو بيتعلم الصوت **معدّي زي ما هو** (bypass بالحرف) | ✅ | `test_commit`: «while it learns, the audio is bit-exact (worst \|diff\| 0.0e+00)» |
| **إنت** اللي بتوقفه | ✅ | `stopLearning()` → `RestoreCore::commit()` (`RestoreCore.h:124`) — مفيش أي قاطع تلقائي بيختم |
| بعد الوقفة بيتثبّت/يتجمّد | ✅ | `test_commit`: «after the commit the decision does not move» + «but the meter keeps counting (34.0 s heard)» |
| بيدخل المسار فورًا وبـfade مش بقفزة | ✅ | نفس الاختبار: «applies inside 250 ms of the stop (11 ms)» و«with a fade, not a step» |
| القرار بيعيش بعد ما تقفل المشروع وتفتحه | ✅ | `getStateInformation/setStateInformation` + `test_commit` خامس مجموعة: «the same take commits to the same decision, twice» |
| إعادة التعلّم بترجّع كل حاجة | ✅ | «relearn goes back to learning… relearn is bit-exact again» |
| الطبقة الثابتة (static bands) | ✅ | `EqSolver.h` (258 سطر) + `test_core` ٢٨ تأكيد |
| الطبقة الديناميكية | ✅ | `DynamicProfile.h` + `test_damage`: «the body of the voice is still there: −0.68 dB at 150–500 Hz» |
| **الباند المتابع للنغمة** (اللي قلت عليه «مهم جدًا») | ✅ | `TrackedBand.h` (298 سطر) + `test_tracked` ٢٧ تأكيد: «centre follows 110 → 220 Hz within 7 cents (limit 36)»، «no click while moving»، «settled in 16 ms (limit 50)» |
| الأنالايزر سريع | ✅ | `test_live`: «display updates: 27 per second of audio»، «falling 8 dB down 250 ms after the tone stops» |
| مستقر في FL Studio (مكانش يكرش) | ✅ | `test_threads` (٢/٢) + المحرر «never touches the filter objects» (`PluginEditor.cpp` timerCallback) |

**الإجمالي المقاس الآن على الشجرة دي:** ١٠ أجنحة، **١١٩ تأكيد، ٠ فشل**.
الأمر:

```bash
for t in core smooth auto damage live bounds commit tracked layers threads; do
  g++ -std=c++17 -O2 -Iplugin/Source plugin/Tests/test_$t.cpp -o build/$t && build/$t | tail -1
done
```

> ملاحظة صدق: `test_threads` لو شغّلته بـ`ulimit -s 512` (زي باقي اختبارات الـstack) بيسقط
> segfault، لأن الـthreads محتاجة stack عادي. الاختبار نفسه سليم، والقيد في الهارنس ومحتاج
> توثيق/تثبيت في البوابة.

## ب. «خد أحسن حاجة من الأربع فلاتر»

| الفكرة | مصدرها | فين في الكود | الحالة |
|---|---|---|---|
| تتعلّم التيك كله مرة واحدة وبعدها تقفل | sonible smart:EQ 4 | `RestoreCore` (learn → commit → freeze) | ✅ |
| طبقة ديناميك بتتحرك مع الليفل | Ozone Stabilizer | `DynamicProfile.h` | ✅ |
| تحلّل الفرق عن المعيار وتطلع منحنى | Waves Curves AQ / Equator | `EqSolver.h` + `model/TargetCurve.h` | ✅ |
| باند بيتعلّق بنغمة المغني | Antares Vocal EQ | `TrackedBand.h` (YIN + 7 cents) | ✅ |
| ماكرو واحد يحرّك كل الباندات بنسبة | smart:EQ (macro) | ⚠️ المفاتيح موجودة (القوة/الخرج) لكن مفيش ماكرو بيحرّك الباندات كلها بنسبة واحدة |

## ج. المطلوب «بالنسبة للشكل» — وهنا الفرق الحقيقي

| الطلب | الحالة | الدليل |
|---|---|---|
| هوية بصرية مش نسخة من أي بلجن | ✅ | `check_identity.py`: أكسنتين بس، مفيش پيل في نص الهيدر (0 px)، مفيش كروت متساوية — والـ`--against-v6` بيرجّع FAIL |
| عصري مش بدائي | ✅ في الرندر | `check_modern.py` (G56): الجراف 69% من الطول، المارِجن hf 0.01–0.32 |
| ألوان مبهجة | ✅ في الرندر | v9 neon: أكسنتين 322°/195° على فيوليت، والمفاتيح متأنودة 335° |
| **نفس الشكل يبقى هو اللي جوّه البلجن** | **❌** | `PluginEditor.cpp` (632 سطر) لسه بيرسم التصميم **القديم**: شريط عنوان + dock فيه knobs + عمود تقرير يمين + منحنى شمال. مفيش هيدر v9، ولا هيرو، ولا شريط تيك، ولا شرايط باندات، ولا فوتر |
| عربي مقروء | ✅ | `check_panel`: ٢١ سلسلة كلها مغطاة بـCairo؛ `Theme.h` بيعدّي على `ArabicText.cpp` (shaping) |
| كل ادعاء برقم/بوابة | ✅ | `GATES.md`: **٤٣ بوابة، كلها ناجحة** + `node …/gate-check.mjs --status` |
| ملف `.exe` واحد لكل نسخة، والمنشور ما يتغيّرش | ✅ | ريليز `v1.2.8`: `AEQ_Restore_Setup_1.2.8.exe` (4.83 MB) + `.sha256`، من CI على Windows |
| نسخة **1.3.0** بالشكل الجديد | ❌ | لسه ما اتنشرتش (آخر ريليز 1.2.8، والـCMake لسه `1.2.8`) |

## د. المتبقي بالترتيب، وكل واحدة ليها معيار «خلصت»

1. **نقل v9 للمحرر** (الشغل الحقيقي الباقي): هيدر + هيرو + شريط تيك + ٤ شرايط + فوتر
   بجوه `PluginEditor.cpp` على نفس أرقام `ui_v9.py` (22 px مارِجن، 40/486/64/36).
   **خلصت لما**: لقطة البلجن من CI تطابق الرندر في البلوكات الأربعة وارتفاعاتها.
2. **بوابة تطابق**: سكربت بيقارن لقطة الـVST3 بالرندر (SSIM/فرق بيكسلات) — عشان «الشكل
   اللي شوفته = اللي هيشتغل» يبقى مقاس مش وعد.
3. **ترحيل `Palette::` القديمة** (18 `muted`, 12 `text`, 11 `accent` …) لـ`Theme.h`.
4. **1.3.0**: bump في الأربع ملفات + ريليز واحد + sha256.
