// Strings.h - PZA's SlowMo's user-visible text in the player's language (English, Russian, Chinese, Japanese).
// Author: PZA. Licence: GPLv3 or later.
//
// Every string the player sees goes through T(id). The English text is compiled in (src\StringList.h); the other
// languages are UTF-8 files next to the DLL: lang\ru.txt, lang\zh.txt, lang\ja.txt ("key = text"). The language
// follows Kenshi's own (settings.cfg "language=ru_RU", read once at start), overridden by PZASlowMo.ini
// Language=auto|en|ru|zh|ja. A text missing from the file falls back to English and is logged once.
// Log lines stay English. Changing the language needs a game restart (the window builds its captions once).
// No KenshiLib code: tests\test_strings_runtime.cpp builds it as is.
#pragma once

namespace slowmo
{
    enum StrId
    {
#define SLOW_STR(id, key, font, width, english) id,
#include "StringList.h"
#undef SLOW_STR
        S_COUNT
    };

    typedef void (*StrLogFn)(const char* line);
    // once at start: iniPath = PZASlowMo.ini (reads [PZASlowMo] Language=), log = where the "language: ..." lines go
    void Strings_Init(const char* iniPath, StrLogFn log);
    const char* T(StrId id);             // UTF-8 text in the current language; never NULL
    // T(id) with {1}..{3} replaced by a1..a3 (NULL = empty); out is always terminated, cut on a UTF-8 boundary
    const char* TF(char* out, int n, StrId id, const char* a1, const char* a2 = 0, const char* a3 = 0);
    const char* Strings_Lang();          // "en", "ru", "zh" or "ja": the language in use
    // A printf format with exactly two %d for a two-placeholder text ("More ({1}/{2})" -> "More (%d/%d)", any '%' doubled).
    // Falls back to the English text when {1} and {2} are not each there once, in that order. For PzaHelp's "More" label.
    const char* PageFormat(char* out, int n, StrId id);
}
