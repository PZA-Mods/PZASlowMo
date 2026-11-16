// StringTable.h - PZA's SlowMo's translated-string table (same parser as LEAF's): the language-file parser, placeholder check and safe formatter.
// Author: PZA. Licence: GPLv3 or later.
//
// No Windows, Kenshi or MyGUI code: tests\test_strings.cpp and tests\fuzz\fuzz_strings.cpp use it as is.
// Language file format (lang\<code>.txt, UTF-8, a BOM is allowed and skipped):
//     # comment            (also ';'), blank lines ignored, CRLF or LF
//     key = text           text is trimmed; "\n" = line break, "\\" = backslash, "\=" or "\#" literal
// A line is REFUSED (the English text stays, an Issue is recorded) when: not valid UTF-8, a control byte, no '=',
// an empty key or text, an unknown key, a second copy of a key (the first wins), text over MAX_VALUE bytes, a bad
// escape, or a placeholder set ({1}..{9}) different from the English line. Nothing in a file is ever used as a
// printf format: Format() substitutes {n} itself (localization-plan-2026-11-01.md, text rules 1-3).
#pragma once
#include <string>
#include <vector>
#include <string.h>

namespace slowmo
{
namespace str
{
    const size_t MAX_FILE  = 256 * 1024;   // bytes read from one language file (the rest is ignored, Issue I_FILE_CAP)
    const size_t MAX_VALUE = 1024;         // bytes of one text after unescaping
    const size_t MAX_KEY   = 64;
    const int    MAX_ISSUES = 200;         // recorded issues per load (the count keeps going)

    enum IssueKind { I_BAD_UTF8 = 0, I_CONTROL, I_NO_EQUALS, I_EMPTY_KEY, I_EMPTY_TEXT, I_UNKNOWN_KEY, I_DUPLICATE,
                     I_TOO_LONG, I_BAD_ESCAPE, I_PLACEHOLDERS, I_FILE_CAP, I_KIND_COUNT };
    inline const char* IssueName(int k)
    {
        static const char* const n[I_KIND_COUNT] = { "not valid UTF-8", "control character", "no '='", "empty key",
            "empty text", "unknown key", "duplicate key (first kept)", "text too long", "bad escape",
            "placeholders differ from English", "file too big (rest ignored)" };
        return (k >= 0 && k < I_KIND_COUNT) ? n[k] : "?";
    }
    struct Issue { int line; int kind; std::string key; };

    // Strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF.
    inline bool Utf8Valid(const char* s, size_t n)
    {
        const unsigned char* p = (const unsigned char*)s;
        size_t i = 0;
        while (i < n)
        {
            const unsigned c = p[i];
            if (c < 0x80) { ++i; continue; }
            int len; unsigned cp, minv;
            if ((c & 0xE0) == 0xC0)      { len = 2; cp = c & 0x1F; minv = 0x80; }
            else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; minv = 0x800; }
            else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; minv = 0x10000; }
            else return false;
            if (n - i < (size_t)len) return false;
            for (int k = 1; k < len; ++k)
            {
                const unsigned d = p[i + k];
                if ((d & 0xC0) != 0x80) return false;
                cp = (cp << 6) | (d & 0x3F);
            }
            if (cp < minv || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
            i += len;
        }
        return true;
    }

    // Decodes the next code point of a VALID UTF-8 string; returns bytes used (0 at the end).
    inline int Utf8Next(const char* s, size_t n, size_t i, unsigned& cp)
    {
        if (i >= n) return 0;
        const unsigned char* p = (const unsigned char*)s + i;
        const unsigned c = p[0];
        int len = c < 0x80 ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : 4));
        if (n - i < (size_t)len) { cp = 0xFFFD; return (int)(n - i); }
        cp = len == 1 ? c : (len == 2 ? (c & 0x1F) : (len == 3 ? (c & 0x0F) : (c & 0x07)));
        for (int k = 1; k < len; ++k) cp = (cp << 6) | (p[k] & 0x3F);
        return len;
    }

    // The longest prefix of s[0..n) that is at most maxBytes long and does not end inside a multi-byte character.
    inline size_t Utf8Cut(const char* s, size_t n, size_t maxBytes)
    {
        if (n <= maxBytes) return n;
        size_t k = maxBytes;
        while (k > 0 && (((unsigned char)s[k]) & 0xC0) == 0x80) --k;   // s[k] is the first byte NOT kept
        return k;
    }

    // Bit k set = placeholder {k} (1..9) appears. "{" followed by anything else is plain text.
    inline unsigned PlaceholderMask(const char* s)
    {
        unsigned m = 0;
        for (; s && *s; ++s)
            if (s[0] == '{' && s[1] >= '1' && s[1] <= '9' && s[2] == '}') m |= 1u << (s[1] - '0');
        return m;
    }

    // Writes fmt into out (always NUL-terminated when outSize > 0), {k} replaced by args[k-1] (missing or NULL = "").
    // Truncates on a UTF-8 boundary. Returns the length written.
    inline size_t Format(char* out, size_t outSize, const char* fmt, const char* const* args, int nargs)
    {
        if (!out || outSize == 0) return 0;
        size_t o = 0;
        const size_t cap = outSize - 1;
        bool full = false;
        for (const char* s = fmt ? fmt : ""; *s && !full; )
        {
            const char* piece = s; size_t plen; bool isArg = false;
            if (s[0] == '{' && s[1] >= '1' && s[1] <= '9' && s[2] == '}')
            {
                const int k = s[1] - '1';
                piece = (args && k < nargs && args[k]) ? args[k] : "";
                plen = strlen(piece);
                s += 3; isArg = true;
            }
            else
            {   // one whole character of fmt (its lead byte gives the length; never past the NUL)
                const unsigned c = (unsigned char)s[0];
                const size_t want = c < 0xC0 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
                plen = 1; while (plen < want && s[plen]) ++plen;
                s += plen;
            }
            if (o + plen > cap)
            {   // keep only whole characters (for a single fmt character: none of it)
                plen = isArg ? Utf8Cut(piece, plen, cap - o) : 0;
                full = true;
            }
            memcpy(out + o, piece, plen); o += plen;
        }
        out[o] = 0;
        return o;
    }

    struct Table
    {
        const char* const* keys; const char* const* en; int n;
        std::vector<std::string> text;            // accepted translations
        std::vector<unsigned char> have;          // 1 = text[i] came from the file
        std::vector<Issue> issues;
        int issueCount, accepted, lines;

        Table() : keys(0), en(0), n(0), issueCount(0), accepted(0), lines(0) {}
        void Init(const char* const* k, const char* const* e, int count)
        {
            keys = k; en = e; n = count < 0 ? 0 : count;
            Clear();
        }
        void Clear()
        {
            text.assign(n, std::string()); have.assign(n, 0); issues.clear(); issueCount = 0; accepted = 0; lines = 0;
        }
        const char* Get(int id) const
        {
            if (id < 0 || id >= n) return "";
            return have[id] ? text[id].c_str() : en[id];
        }
        int Missing() const { int m = 0; for (int i = 0; i < n; ++i) if (!have[i]) ++m; return m; }
        int Find(const char* k, size_t len) const
        {
            for (int i = 0; i < n; ++i) if (strlen(keys[i]) == len && memcmp(keys[i], k, len) == 0) return i;
            return -1;
        }
        void Note(int line, int kind, const char* k, size_t klen)
        {
            ++issueCount;
            if ((int)issues.size() >= MAX_ISSUES) return;
            Issue is; is.line = line; is.kind = kind;
            if (k && klen) is.key.assign(k, klen > MAX_KEY ? MAX_KEY : klen);
            issues.push_back(is);
        }
        static bool Space(char c) { return c == ' ' || c == '\t'; }

        // Parses one language file (any bytes); returns the number of texts accepted. Clear()s first.
        int Load(const char* data, size_t len)
        {
            Clear();
            if (!data) return 0;
            if (len > MAX_FILE) { Note(0, I_FILE_CAP, 0, 0); len = MAX_FILE; }
            size_t i = 0;
            if (len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) i = 3;
            int lineNo = 0;
            std::string val;
            while (i < len)
            {
                size_t e = i; while (e < len && data[e] != '\n') ++e;
                const size_t next = e < len ? e + 1 : e;
                ++lineNo;
                size_t a = i, b = e;
                if (b > a && data[b - 1] == '\r') --b;
                i = next;
                while (a < b && Space(data[a])) ++a;
                while (b > a && Space(data[b - 1])) --b;
                if (a == b || data[a] == '#' || data[a] == ';') continue;
                ++lines;
                if (!Utf8Valid(data + a, b - a)) { Note(lineNo, I_BAD_UTF8, 0, 0); continue; }
                bool ctl = false;
                for (size_t k = a; k < b; ++k) { const unsigned char c = (unsigned char)data[k]; if ((c < 0x20 && c != '\t') || c == 0x7F) { ctl = true; break; } }
                size_t eq = a; while (eq < b && data[eq] != '=') ++eq;
                size_t ka = a, kb = eq; while (kb > ka && Space(data[kb - 1])) --kb;
                if (ctl) { Note(lineNo, I_CONTROL, data + ka, kb - ka); continue; }
                if (eq == b) { Note(lineNo, I_NO_EQUALS, data + ka, kb - ka); continue; }
                if (kb == ka) { Note(lineNo, I_EMPTY_KEY, 0, 0); continue; }
                const int id = Find(data + ka, kb - ka);
                if (id < 0) { Note(lineNo, I_UNKNOWN_KEY, data + ka, kb - ka); continue; }
                size_t va = eq + 1; while (va < b && Space(data[va])) ++va;
                if (va == b) { Note(lineNo, I_EMPTY_TEXT, data + ka, kb - ka); continue; }
                if (b - va > MAX_VALUE * 2) { Note(lineNo, I_TOO_LONG, data + ka, kb - ka); continue; }
                val.clear();
                bool bad = false;
                for (size_t k = va; k < b; ++k)
                {
                    const char c = data[k];
                    if (c == '\t') { bad = true; break; }                 // tabs are refused inside text (layout)
                    if (c != '\\') { val += c; continue; }
                    if (k + 1 >= b) { bad = true; break; }
                    const char d = data[++k];
                    if (d == 'n') val += '\n'; else if (d == '\\') val += '\\'; else if (d == '=') val += '='; else if (d == '#') val += '#';
                    else { bad = true; break; }
                }
                if (bad) { Note(lineNo, I_BAD_ESCAPE, data + ka, kb - ka); continue; }
                if (val.size() > MAX_VALUE) { Note(lineNo, I_TOO_LONG, data + ka, kb - ka); continue; }
                if (PlaceholderMask(val.c_str()) != PlaceholderMask(en[id])) { Note(lineNo, I_PLACEHOLDERS, data + ka, kb - ka); continue; }
                if (have[id]) { Note(lineNo, I_DUPLICATE, data + ka, kb - ka); continue; }
                text[id] = val; have[id] = 1; ++accepted;
            }
            return accepted;
        }
    };
}
}
