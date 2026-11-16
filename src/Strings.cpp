// Strings.cpp - loads PZA's SlowMo's language file and picks the language. Author: PZA. Licence: GPLv3 or later.
// Same rules as LEAF's (LEAF src\Strings.cpp). The parser and the formatter live in StringTable.h (tested offline:
// tests\test_strings.cpp, tests\fuzz\fuzz_strings.cpp); this file is tested by tests\test_strings_runtime.cpp.
#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "StringTable.h"
#include "Strings.h"

namespace slowmo
{
    namespace
    {
        const char* const kKeys[S_COUNT + 1] = {
#define SLOW_STR(id, key, font, width, english) key,
#include "StringList.h"
#undef SLOW_STR
            0 };
        const char* const kEnglish[S_COUNT + 1] = {
#define SLOW_STR(id, key, font, width, english) english,
#include "StringList.h"
#undef SLOW_STR
            0 };

        str::Table s_tab;
        bool s_tabReady = false;
        char s_lang[8] = "en";
        StrLogFn s_log = 0;

        void Log(const char* m)
        {
            if (!s_log) return;
            char b[700]; _snprintf_s(b, sizeof(b), _TRUNCATE, "language: %s", m);
            s_log(b);
        }
        void EnsureTable()
        {
            if (s_tabReady) return;
            s_tab.Init(kKeys, kEnglish, S_COUNT);
            s_tabReady = true;
        }

        // Reads up to cap bytes of a file by a wide path (a user folder with non-ASCII letters breaks the ...A APIs).
        bool ReadFileW(const std::wstring& path, std::string& out, size_t cap)
        {
            out.clear();
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) return false;
            out.resize(cap);
            DWORD got = 0;
            const BOOL ok = ReadFile(h, cap ? &out[0] : NULL, (DWORD)cap, &got, NULL);
            CloseHandle(h);
            out.resize(ok ? got : 0);
            return ok != FALSE;
        }
        std::wstring DirOf(const wchar_t* p)
        {
            std::wstring s(p ? p : L"");
            const size_t k = s.find_last_of(L"\\/");
            return k == std::wstring::npos ? std::wstring() : s.substr(0, k + 1);
        }
        std::wstring DllDir()
        {
            HMODULE self = NULL;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&DllDir, &self);
            wchar_t p[MAX_PATH * 2] = { 0 };
            GetModuleFileNameW(self, p, MAX_PATH * 2);
            return DirOf(p);
        }
        std::string Narrow(const std::wstring& w)   // for log lines only (UTF-8)
        {
            if (w.empty()) return std::string();
            const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
            std::string s(n > 0 ? n : 0, '\0');
            if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
            return s;
        }

        // Kenshi's own language: settings.cfg in the install root, line "language=ru_RU" (written by Kenshi's launcher;
        // checked for LEAF on the 1.0.68 Steam install 2026-11-04: "language=en_GB"). The root is found from the working
        // directory, the exe (Kenshi\ or Kenshi\RE_Kenshi\) and our own folder (Kenshi\mods\PZASlowMo\).
        bool KenshiLanguage(char* out, int n, std::string& where)
        {
            out[0] = 0;
            std::wstring dirs[4];
            wchar_t cwd[MAX_PATH * 2] = { 0 };
            if (GetCurrentDirectoryW(MAX_PATH * 2, cwd)) { dirs[0] = cwd; dirs[0] += L"\\"; }
            wchar_t exe[MAX_PATH * 2] = { 0 };
            GetModuleFileNameW(NULL, exe, MAX_PATH * 2);
            dirs[1] = DirOf(exe);
            if (!dirs[1].empty()) dirs[2] = dirs[1] + L"..\\";
            dirs[3] = DllDir(); if (!dirs[3].empty()) dirs[3] += L"..\\..\\";
            for (int d = 0; d < 4; ++d)
            {
                if (dirs[d].empty()) continue;
                std::string cfg;
                if (!ReadFileW(dirs[d] + L"settings.cfg", cfg, 64 * 1024)) continue;
                size_t i = 0;
                while (i < cfg.size())
                {
                    size_t e = cfg.find('\n', i); if (e == std::string::npos) e = cfg.size();
                    std::string line = cfg.substr(i, e - i); i = e + 1;
                    while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ')) line.erase(line.size() - 1);
                    if (line.size() > 9 && _strnicmp(line.c_str(), "language=", 9) == 0)
                    {
                        _snprintf_s(out, n, _TRUNCATE, "%s", line.c_str() + 9);
                        where = Narrow(dirs[d]) + "settings.cfg";
                        return true;
                    }
                }
            }
            return false;
        }

        const char* CodeFor(const char* v)   // "ru_RU" / "ru" / "RU" -> "ru"; anything else -> NULL
        {
            if (!v) return NULL;
            if (_strnicmp(v, "en", 2) == 0) return "en";
            if (_strnicmp(v, "ru", 2) == 0) return "ru";
            if (_strnicmp(v, "zh", 2) == 0) return "zh";
            if (_strnicmp(v, "ja", 2) == 0) return "ja";
            return NULL;
        }
    }

    void Strings_Init(const char* iniPath, StrLogFn log)
    {
        s_log = log;
        EnsureTable();
        s_tab.Clear();
        _snprintf_s(s_lang, sizeof(s_lang), _TRUNCATE, "en");
        char want[32] = { 0 };
        GetPrivateProfileStringA("PZASlowMo", "Language", "auto", want, sizeof(want), iniPath ? iniPath : "");
        char kl[32] = { 0 }; std::string where;
        const bool haveKenshi = KenshiLanguage(kl, sizeof(kl), where);
        const char* kcode = haveKenshi ? CodeFor(kl) : NULL;
        if (!kcode) kcode = "en";   // de_DE, fr_FR, ... : no translation yet, English
        {
            char b[600];
            if (haveKenshi) _snprintf_s(b, sizeof(b), _TRUNCATE, "Kenshi runs in '%s' (%s) -> %s; PZASlowMo.ini Language=%s", kl, where.c_str(), kcode, want);
            else _snprintf_s(b, sizeof(b), _TRUNCATE, "Kenshi's settings.cfg not found (no language= line) -> en; PZASlowMo.ini Language=%s", want);
            Log(b);
        }
        const char* code = kcode;
        if (_stricmp(want, "auto") != 0 && want[0])
        {
            const char* forced = CodeFor(want);
            if (!forced) { char b[200]; _snprintf_s(b, sizeof(b), _TRUNCATE, "Language=%s is not one of auto|en|ru|zh|ja - auto used", want); Log(b); }
            else if (strcmp(forced, "en") != 0 && haveKenshi && strcmp(forced, kcode) != 0)
            {   // Kenshi loads the fonts with Cyrillic / CJK letters only while it runs in that language
                // (locale\<lang>\gui\fonts\kenshi_fonts.xml); in any other language the text would be blank boxes
                char b[300]; _snprintf_s(b, sizeof(b), _TRUNCATE, "Language=%s needs Kenshi itself set to that language (only then are its fonts loaded "
                                         "with those letters) - English used", want);
                Log(b);
                code = "en";
            }
            else code = forced;
        }
        _snprintf_s(s_lang, sizeof(s_lang), _TRUNCATE, "%s", code);
        if (!strcmp(code, "en")) { Log("using English (compiled in)"); return; }

        const std::wstring path = DllDir() + L"lang\\" + std::wstring(code, code + strlen(code)) + L".txt";
        std::string data;
        if (!ReadFileW(path, data, str::MAX_FILE + 1))
        {
            char b[600]; _snprintf_s(b, sizeof(b), _TRUNCATE, "%s NOT found - English used", Narrow(path).c_str());
            Log(b);
            _snprintf_s(s_lang, sizeof(s_lang), _TRUNCATE, "en");
            return;
        }
        const int ok = s_tab.Load(data.data(), data.size());
        {
            char b[700]; _snprintf_s(b, sizeof(b), _TRUNCATE, "%s: %d of %d texts loaded, %d missing (English shown), %d line(s) refused",
                                     Narrow(path).c_str(), ok, (int)S_COUNT, s_tab.Missing(), s_tab.issueCount);
            Log(b);
        }
        for (size_t i = 0; i < s_tab.issues.size() && i < 30; ++i)
        {
            const str::Issue& is = s_tab.issues[i];
            char b[200]; _snprintf_s(b, sizeof(b), _TRUNCATE, "  line %d refused: %s%s%s", is.line, str::IssueName(is.kind),
                                     is.key.empty() ? "" : ", key ", is.key.c_str());
            Log(b);
        }
        {   // the missing ids, once (they show in English)
            std::string line; int shown = 0;
            for (int i = 0; i < S_COUNT; ++i)
            {
                if (s_tab.have[i]) continue;
                if (shown < 40) { line += line.empty() ? "" : ", "; line += kKeys[i]; }
                ++shown;
            }
            if (shown)
            {
                char b[700]; _snprintf_s(b, sizeof(b), _TRUNCATE, "missing, shown in English: %s%s", line.c_str(), shown > 40 ? ", ..." : "");
                Log(b);
            }
        }
    }

    const char* T(StrId id)
    {
        EnsureTable();
        return s_tab.Get((int)id);
    }

    const char* TF(char* out, int n, StrId id, const char* a1, const char* a2, const char* a3)
    {
        const char* args[3] = { a1, a2, a3 };
        if (!out || n <= 0) return "";
        str::Format(out, (size_t)n, T(id), args, 3);
        return out;
    }

    const char* PageFormat(char* out, int n, StrId id)
    {
        if (!out || n <= 0) return "";
        for (int pass = 0; pass < 2; ++pass)
        {
            const char* s = pass == 0 ? T(id) : (id >= 0 && id < S_COUNT ? kEnglish[id] : "");
            std::string f; int next = 1; bool bad = false;
            for (; *s && !bad; ++s)
            {
                if (s[0] == '{' && s[1] >= '1' && s[1] <= '9' && s[2] == '}')
                {
                    if (s[1] - '0' != next || next > 2) bad = true;
                    else { f += "%d"; ++next; s += 2; }
                }
                else if (*s == '%') f += "%%";
                else f += *s;
            }
            if (bad || next != 3) continue;
            if (f.size() + 1 > (size_t)n) continue;   // never cut a format (a cut "%d" or "%%" would misread): try English
            memcpy(out, f.data(), f.size()); out[f.size()] = 0;
            return out;
        }
        _snprintf_s(out, n, _TRUNCATE, "%s", n >= 6 ? "%d/%d" : "");   // only when even English does not fit in out
        return out;
    }

    const char* Strings_Lang() { return s_lang; }
}
