// PzaHelp.h - PZA help page: a "?" button opens a framed panel INSIDE the window, over its controls, with the window's help
// text wrapped to the panel and split into pages (user 2026-11-04: "each menu deserves a Help button that explains things,
// fitting within the confines of the page"). Author: PZA. Licence: GPLv3 or later. Shared by LEAF, PZA Camera, PZA's SlowMo.
//
// MyGUI's TextBox doesn't wrap, and fonts differ with resolution, Kenshi's font option and the language, so the text is wrapped
// at run time by MEASURING it (exported TextBox::getTextSize, the call the hover tooltips already use): break at spaces, or
// between characters for Chinese / Japanese (closing punctuation stays on its line). Lines that don't fit the panel's height go
// to further pages ("More"). Exported MyGUI calls only; the caller runs everything under its own SEH guard, wires the two
// buttons' clicks to its own handler (Back() / More()), and calls Forget() when the game rebuilds its GUI.
#pragma once
#include <string>
#include <vector>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Button.h>

namespace pzahelp
{
    struct Panel
    {
        MyGUI::Widget*  bg;      // Kenshi's framed text-box skin; takes the mouse, so the controls under it can't be clicked
        MyGUI::TextBox* text;
        MyGUI::TextBox* meas;    // hidden: measures candidate lines
        MyGUI::Button*  back;
        MyGUI::Button*  more;
        std::vector<std::string> pages;
        std::vector<MyGUI::Widget*> under;   // the window's controls we hid while the page is open (Cover / Close)
        int page, w, h, textW, textH, lines, logs;
        bool open, estimated;   // estimated: the measure call returned nothing, widths were estimated
        Panel() : bg(0), text(0), meas(0), back(0), more(0), page(0), w(0), h(0), textW(0), textH(0), lines(0), logs(0), open(false), estimated(false) {}
    };

    inline void SetText(MyGUI::TextBox* t, const std::string& s) { if (t) t->MyGUI::TextBox::setCaption(MyGUI::UString(s.c_str())); }

    // Builds the (hidden) panel at x, y, w, h inside the window `win`. Created after the window's own controls, so it draws
    // over them. click: the window's own click handler (it calls Back / More when the sender is p.back / p.more).
    inline void Create(Panel& p, MyGUI::Widget* win, int x, int y, int w, int h, int bh, int pad,
                       const char* backLabel, const char* moreLabel, void (*click)(MyGUI::Widget*))
    {
        if (!win || p.bg) return;
        p.w = w; p.h = h;
        p.bg = win->createWidget<MyGUI::Widget>("Kenshi_GenericTextBoxSkin", MyGUI::IntCoord(x, y, w, h), MyGUI::Align::Default);
        p.textW = w - 2 * pad; p.textH = h - 2 * pad - bh - pad;
        p.text = p.bg->createWidget<MyGUI::TextBox>("Kenshi_TextboxStandardText", MyGUI::IntCoord(pad, pad, p.textW, p.textH), MyGUI::Align::Default);
        p.text->setNeedMouseFocus(false);
        p.meas = p.bg->createWidget<MyGUI::TextBox>("Kenshi_TextboxStandardText", MyGUI::IntCoord(0, 0, 4000, 400), MyGUI::Align::Default);
        p.meas->setNeedMouseFocus(false);
        p.meas->MyGUI::Widget::setVisible(false);
        const int bw = (w - 3 * pad) / 2;
        p.back = p.bg->createWidget<MyGUI::Button>("Kenshi_Button1", MyGUI::IntCoord(pad, h - pad - bh, bw, bh), MyGUI::Align::Default);
        p.more = p.bg->createWidget<MyGUI::Button>("Kenshi_Button1", MyGUI::IntCoord(2 * pad + bw, h - pad - bh, bw, bh), MyGUI::Align::Default);
        SetText(p.back, backLabel); SetText(p.more, moreLabel);
        p.back->eventMouseButtonClick += MyGUI::newDelegate(click);
        p.more->eventMouseButtonClick += MyGUI::newDelegate(click);
        p.bg->MyGUI::Widget::setVisible(false);
    }
    inline void Forget(Panel& p) { p.under.clear(); p.bg = 0; p.text = 0; p.meas = 0; p.back = 0; p.more = 0; p.pages.clear(); p.open = false; p.page = 0; }

    inline int MeasureW(Panel& p, const std::string& s) { SetText(p.meas, s); return p.meas->MyGUI::TextBox::getTextSize().width; }
    inline int MeasureH(Panel& p, const std::string& s) { SetText(p.meas, s); return p.meas->MyGUI::TextBox::getTextSize().height; }

    // UTF-8 helpers: the code point at s[i] and its byte length (malformed bytes count as one byte, code point '?')
    inline unsigned CodePoint(const std::string& s, size_t i, int& len)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { len = 1; return c; }
        int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        if (n == 1 || i + n > s.size()) { len = 1; return '?'; }
        unsigned cp = c & (0x3F >> (n - 1));
        for (int k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        len = n; return cp;
    }
    inline bool Wide(unsigned cp) { return cp >= 0x2E80; }   // CJK and full-width forms: a line may break between them
    inline bool Closing(unsigned cp)
    {   // never at the start of a line: CJK closing punctuation and full-width , . ! ? : ; ) ]
        return cp == 0x3001 || cp == 0x3002 || cp == 0xFF0C || cp == 0xFF0E || cp == 0xFF01 || cp == 0xFF1F || cp == 0xFF1A ||
               cp == 0xFF1B || cp == 0xFF09 || cp == 0x300D || cp == 0x300F || cp == 0x3011 || cp == 0xFF3D || cp == 0x30FC;
    }

    // Splits one paragraph into tokens: a word with its trailing space, or one wide character with any closing punctuation
    inline void Tokens(const std::string& para, std::vector<std::string>& out)
    {
        std::string cur;
        for (size_t i = 0; i < para.size();)
        {
            int len = 1; const unsigned cp = CodePoint(para, i, len);
            const std::string ch = para.substr(i, len);
            i += len;
            if (Wide(cp) && Closing(cp)) { if (!cur.empty()) cur += ch; else if (!out.empty()) out.back() += ch; else cur = ch; continue; }
            if (Wide(cp)) { if (!cur.empty()) out.push_back(cur); cur = ch; continue; }
            if (!cur.empty()) { int pl = 1; size_t last = cur.size() - 1; while (last > 0 && ((unsigned char)cur[last] & 0xC0) == 0x80) --last;
                                if (Wide(CodePoint(cur, last, pl))) { out.push_back(cur); cur.clear(); } }
            cur += ch;
            if (cp == ' ') { out.push_back(cur); cur.clear(); }
        }
        if (!cur.empty()) out.push_back(cur);
    }
    // fallback width when the measure call gives nothing (never seen; logged by the caller via p.estimated): 9 px per
    // narrow and 18 px per wide character, Kenshi's standard font at 1080p, rounded up
    inline int EstimateW(const std::string& s)
    {
        int w = 0;
        for (size_t i = 0; i < s.size();) { int len = 1; const unsigned cp = CodePoint(s, i, len); i += len; w += Wide(cp) ? 18 : 9; }
        return w;
    }
    inline std::string TrimRight(const std::string& s) { size_t e = s.size(); while (e > 0 && s[e - 1] == ' ') --e; return s.substr(0, e); }

    // Plain text with a '#' must reach MyGUI as "##", or '#' + the next characters read as a colour code (user 2026-11-08:
    // a text with "#1" in it turned the rest of the text green and ate letters).
    inline std::string EscapeHash(const std::string& s)
    {
        std::string o; o.reserve(s.size() + 8);
        for (size_t i = 0; i < s.size(); ++i) { o += s[i]; if (s[i] == '#') o += '#'; }
        return o;
    }
    inline bool HexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
    // MyGUI colour codes in a line: "##" is a literal '#'; '#' + 6 hex digits sets the colour. Updates `cur` to the colour in
    // force at the end of `s` (callers start it empty at each paragraph: a text may reset the colour at every new line).
    inline void TrackColour(const std::string& s, std::string& cur)
    {
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] != '#') continue;
            if (i + 1 < s.size() && s[i + 1] == '#') { ++i; continue; }
            if (i + 7 <= s.size())
            {
                bool hex = true; for (int k = 1; k <= 6; ++k) hex = hex && HexDigit(s[i + k]);
                if (hex) { cur = s.substr(i, 7); i += 6; }
            }
        }
    }

    // Wraps `text` (UTF-8, "\n" = new paragraph) to the panel and splits it into pages. Returns the page count.
    // A coloured paragraph cut by a page break keeps its colour on the next page (user 2026-11-08: the friendly-fire line
    // that ran onto page 3 of a report came out white): each wrapped line remembers the colour in force at its start, and a
    // page that starts with such a line starts with that colour code.
    inline int Layout(Panel& p, const char* text)
    {
        p.pages.clear(); p.page = 0; p.lines = 0;
        if (!p.meas || !text) return 0;
        std::vector<std::string> lines, startColour;
        p.estimated = MeasureW(p, "AgAg") <= 0;
        const std::string all(text);
        size_t start = 0;
        while (start <= all.size())
        {
            size_t nl = all.find('\n', start); if (nl == std::string::npos) nl = all.size();
            const std::string para = all.substr(start, nl - start);
            start = nl + 1;
            if (para.empty()) { lines.push_back(""); startColour.push_back(""); if (nl == all.size()) break; continue; }
            std::vector<std::string> tok; Tokens(para, tok);
            std::string line, colour;   // colour: the code in force at the start of `line` (empty = the text's own)
            for (size_t k = 0; k < tok.size() && lines.size() < 400; ++k)
            {
                const std::string cand = line + tok[k];
                if (line.empty() || (p.estimated ? EstimateW(TrimRight(cand)) : MeasureW(p, TrimRight(cand))) <= p.textW) { line = cand; continue; }
                lines.push_back(TrimRight(line)); startColour.push_back(colour);
                TrackColour(line, colour);
                line = tok[k];
                while (!line.empty() && line[0] == ' ') line.erase(0, 1);
            }
            if (!line.empty()) { lines.push_back(TrimRight(line)); startColour.push_back(colour); }
            if (nl == all.size()) break;
        }
        int lineH = p.estimated ? 0 : MeasureH(p, "Ag");
        if (lineH <= 0) lineH = 24;
        int perPage = p.textH / lineH; if (perPage < 3) perPage = 3;
        std::string pg; int n = 0;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (n == 0 && lines[i].empty()) continue;   // no blank line at the top of a page
            if (n == perPage) { p.pages.push_back(pg); pg.clear(); n = 0; if (lines[i].empty()) continue; }
            if (n) pg += "\n";
            else if (i < startColour.size()) pg += startColour[i];   // a page that starts mid-paragraph keeps its colour
            pg += lines[i]; ++n;
        }
        if (n) p.pages.push_back(pg);
        p.lines = (int)lines.size();
        return (int)p.pages.size();
    }

    inline void ShowPage(Panel& p, const char* moreFmt /* e.g. "More (%d/%d)" */)
    {
        if (!p.bg) return;
        const int n = (int)p.pages.size();
        if (p.page >= n) p.page = 0;
        SetText(p.text, n ? p.pages[p.page] : std::string());
        if (p.more)
        {
            p.more->MyGUI::Widget::setVisible(n > 1);
            char b[64]; _snprintf_s(b, sizeof(b), _TRUNCATE, moreFmt ? moreFmt : "More (%d/%d)", p.page + 1, n);
            SetText(p.more, b);
        }
    }
    // Hides the window's own controls under the open page and remembers them for Close (user screenshot 2026-11-06: labels
    // and -/+ showed through the help text). A frame can't cover them: MyGUI batches a layer's drawing by texture, so every
    // widget's TEXT is drawn after every skin, whatever the creation order. Controls above the page's top (the title row with
    // "?") stay. Exported Widget::getParent / getChildCount / getChildAt / getVisible / setVisible.
    inline void Cover(Panel& p)
    {
        if (!p.bg || !p.open) return;
        MyGUI::Widget* parent = p.bg->MyGUI::Widget::getParent();
        if (!parent) return;
        const int top = p.bg->getTop();
        const size_t n = parent->MyGUI::Widget::getChildCount();
        for (size_t i = 0; i < n && i < 512; ++i)
        {
            MyGUI::Widget* c = parent->MyGUI::Widget::getChildAt(i);
            if (!c || c == p.bg || !c->MyGUI::Widget::getVisible() || c->getTop() < top) continue;
            c->MyGUI::Widget::setVisible(false);
            bool known = false;
            for (size_t k = 0; k < p.under.size(); ++k) if (p.under[k] == c) { known = true; break; }
            if (!known) p.under.push_back(c);
        }
    }
    // Callers whose refresh code shows controls while the page may be open call this at the end of the refresh.
    inline void Rehide(Panel& p) { Cover(p); }
    inline void Open(Panel& p, const char* text, const char* moreFmt)
    {
        if (!p.bg) return;
        Layout(p, text);
        ShowPage(p, moreFmt);
        p.bg->MyGUI::Widget::setVisible(true);
        p.open = true;
        Cover(p);
    }
    inline void Close(Panel& p)
    {
        if (p.bg) p.bg->MyGUI::Widget::setVisible(false);
        for (size_t i = 0; i < p.under.size(); ++i) if (p.under[i]) p.under[i]->MyGUI::Widget::setVisible(true);
        p.under.clear();
        p.open = false;
    }
    inline void More(Panel& p, const char* moreFmt) { if (p.pages.size() > 1) { p.page = (p.page + 1) % (int)p.pages.size(); ShowPage(p, moreFmt); } }
}
