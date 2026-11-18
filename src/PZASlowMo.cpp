// PZASlowMo.cpp - PZA's SlowMo: one click (or Shift+L) puts Kenshi in slow motion, a slider picks how slow.
// Author: PZA. Licence: GPLv3 or later (KenshiLib requires it). RE_Kenshi / KenshiLib plugin, VS2010 (v100) toolset.
//
// How (read in our 1.0.65 exe, 2026-10-31): GameWorld::setGameSpeed(speed, click) (Steam 0x787300) stores the speed and
// only compares it with 0 (pause) and the standard speeds (to light Kenshi's buttons): no rounding, no clamping. So any
// speed works. The speed is kept in whole hundredths (10..60 = 0.10..0.60), so 0.10, 0.50 and 0.60 are always exact.
//
// UI: a small Kenshi-style window (draggable by its title), made only of MyGUI calls exported by Kenshi's MyGUIEngine
// (Gui::createWidgetT, Widget::createWidgetT, Button/TextBox captions, ScrollBar::setScrollRange/Position/
// getScrollPosition). The slider is READ every frame (no slider event delegate). Every MyGUI call runs under SEH; a
// fault switches the window off for the session (the hotkey keeps working).
//
// Rules (research method): every rule logs when it fires; a read-back check confirms Kenshi took the speed; if Kenshi's
// own speed buttons change the speed while slow motion is on, slow motion switches off (logged).
#include <ogre/OgrePrerequisites.h>
#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/gui/ForgottenGUI.h>

#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_ScrollBar.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_EditBox.h>
#include "PzaTip.h"
#include "PzaHelp.h"   // the "?" help page (user 2026-11-04)
#include "SlowCore.h"
#include "Strings.h"   // every user-visible text (en compiled in; lang/ru.txt, zh.txt, ja.txt)

#include <Windows.h>
#include <stdio.h>
#include <math.h>
#include <string>

namespace
{
    // ---------- settings (PZASlowMo.ini next to the DLL) ----------
    struct Settings
    {
        int enabled, showWindow, popups, collapsed, hideWithUi;
        int speed;                 // slow speed in hundredths (10..60)
        int minH, maxH, stepH;     // slider range and step, hundredths
        int hkCtrl, hkShift, hkAlt, hkKey;
        float winX, winY;          // window position as a fraction of the screen (0..1); <0 = default
        float tinyX, tinyY;        // v0.2.4: tiny button position (fraction of the screen); <0 = top-right corner
        float tinyAlpha;           // v0.2.5: tiny button opacity, 0.35 (faint, never invisible) .. 1
    };
    Settings    g;
    std::string s_dir;

    void Log(const char* fmt, ...)
    {
        char buf[600] = "[PZASlowMo] ";
        size_t p = strlen(buf);
        va_list ap; va_start(ap, fmt);
        _vsnprintf_s(buf + p, sizeof(buf) - p, _TRUNCATE, fmt, ap);
        va_end(ap);
        DebugLog(buf);
    }
    void LogLine(const char* s) { Log("%s", s); }   // for Strings.cpp
    std::string IniPath() { return s_dir + "\\PZASlowMo.ini"; }
    float IniF(const char* key, float def)
    {
        char v[64]; char d[64]; _snprintf_s(d, sizeof(d), _TRUNCATE, "%g", def);
        GetPrivateProfileStringA("PZASlowMo", key, d, v, sizeof(v), IniPath().c_str());
        return (float)atof(v);
    }
    // the rules live in SlowCore.h (tests/test_core.cpp checks and fuzzes them)
    void ParseKey(const char* t, int& ctrl, int& shift, int& alt, int& key, int defKey) { slowcore::ParseKey(t, ctrl, shift, alt, key, defKey); }
    slowcore::Range s_range = { 10, 60, 5 };
    void LoadSettings()
    {
        g.enabled    = (int)IniF("Enabled", 1);
        g.showWindow = (int)IniF("ShowWindow", 1);
        g.popups     = (int)IniF("Popups", 1);
        g.collapsed  = (int)IniF("Collapsed", 0); if (g.collapsed < 0 || g.collapsed > 2) g.collapsed = 0; if (g.collapsed == 1) g.collapsed = 2;   // v0.2.2: 0 full, 2 tiny (the compact step is gone)
        g.hideWithUi = (int)IniF("HideWithUI", 1);
        s_range = slowcore::MakeRange(slowcore::ToH(IniF("MinSpeed", 0.10f), 10), slowcore::ToH(IniF("MaxSpeed", 0.60f), 60),
                                      slowcore::ToH(IniF("SliderStep", 0.05f), 5));
        g.minH = s_range.minH; g.maxH = s_range.maxH; g.stepH = s_range.stepH;
        g.speed = slowcore::Snap(s_range, slowcore::ToH(IniF("Speed", 0.50f), 50));
        g.winX = IniF("WindowX", -1); g.winY = IniF("WindowY", -1);
        g.tinyX = IniF("TinyX", -1); g.tinyY = IniF("TinyY", -1);
        g.tinyAlpha = IniF("TinyAlpha", 1.0f); if (!(g.tinyAlpha >= 0.35f)) g.tinyAlpha = 0.35f; if (g.tinyAlpha > 1.0f) g.tinyAlpha = 1.0f;
        char hk[64]; GetPrivateProfileStringA("PZASlowMo", "Hotkey", "Shift+L", hk, sizeof(hk), IniPath().c_str());
        ParseKey(hk, g.hkCtrl, g.hkShift, g.hkAlt, g.hkKey, 'L');
        Log("settings: Speed=%.2f range %.2f..%.2f step %.2f Hotkey=%s ShowWindow=%d", g.speed / 100.0f, g.minH / 100.0f, g.maxH / 100.0f, g.stepH / 100.0f, hk, g.showWindow);
    }
    void SaveSpeed()
    {
        char v[16]; _snprintf_s(v, sizeof(v), _TRUNCATE, "%.2f", g.speed / 100.0f);
        WritePrivateProfileStringA("PZASlowMo", "Speed", v, IniPath().c_str());
    }

    // ---------- the game ----------
    bool s_on = false, s_dead = false, s_readbackOk = true, s_readbackChecked = false, s_hkDown = false, s_popupDead = false;
    float s_prevSpeed = 1.0f;      // the speed to return to when slow motion goes off
    int   s_setH = 0;              // the speed we last set (hundredths)
    int   n_toggles = 0, n_external = 0;

    void PopupInner(GameWorld* w, const char* text) { const std::string s(text); w->showPlayerAMessage(s, false); }
    void Popup(GameWorld* w, const char* text)
    {
        Log("popup: %s", text);
        if (!g.popups || s_popupDead || !w) return;
        __try { PopupInner(w, text); }
        __except (EXCEPTION_EXECUTE_HANDLER) { s_popupDead = true; Log("popup FAULTED - on-screen messages off"); }
    }
    void SetSpeed(GameWorld* w, int h)
    {
        s_setH = h;
        w->setGameSpeed(h / 100.0f, false);
        if (!s_readbackChecked)
        {   // falsifier: does Kenshi report back the speed we set? if not, don't trust read-back for "you pressed Kenshi's buttons"
            s_readbackChecked = true;
            const float got = w->getFrameSpeedMultiplier();
            s_readbackOk = fabsf(got - h / 100.0f) < 0.001f;
            Log("read-back check: set %.2f, Kenshi reports %.3f -> %s", h / 100.0f, got, s_readbackOk ? "OK" : "differs: external-change detection off");
        }
    }
    void SlowOn(GameWorld* w)
    {
        const float cur = w->getFrameSpeedMultiplier();
        s_prevSpeed = slowcore::ReturnSpeed(cur);                         // never return to a slow speed or to 0
        s_on = true; ++n_toggles;
        SetSpeed(w, g.speed);
        Log("slow motion ON at %.2f (was %.2f; returns there when off); Kenshi now reports %.2f", g.speed / 100.0f, cur, w->getFrameSpeedMultiplier());
        char sp[16], m[256]; _snprintf_s(sp, sizeof(sp), _TRUNCATE, "%.2f", g.speed / 100.0f);
        Popup(w, slowmo::TF(m, sizeof(m), slowmo::S_MSG_ON, sp));
    }
    void SlowOff(GameWorld* w, const char* why)
    {
        s_on = false;
        const bool kenshiSetIt = strcmp(why, "Kenshi speed button") == 0 || strcmp(why, "after loading") == 0;   // Kenshi chose the speed: keep it
        if (w && !kenshiSetIt) w->setGameSpeed(s_prevSpeed, false);
        Log("slow motion OFF (%s), speed %.2f", why, w ? w->getFrameSpeedMultiplier() : 0.0f);
        if (!kenshiSetIt) Popup(w, slowmo::T(slowmo::S_MSG_OFF));
    }
    void ChangeSpeed(GameWorld* w, int h)
    {
        h = slowcore::Snap(s_range, h);
        if (h == g.speed) return;
        g.speed = h; SaveSpeed();
        Log("slow speed set to %.2f (saved)", h / 100.0f);
        if (s_on) SetSpeed(w, h);
    }

    // ---------- the window (proven MyGUI call set, every call under SEH) ----------
    const char* const WIN_NAME = "PZASlowMo_Window";
    MyGUI::Window*    s_win = NULL;
    MyGUI::Button*    s_toggle = NULL;
    MyGUI::Button*    s_minus = NULL;
    MyGUI::Button*    s_plus = NULL;
    MyGUI::Button*    s_helpBtn = NULL;   // "?": the help page (the window grows to fit it while it is open)
    pzahelp::Panel    s_help;
    int               s_helpH = 0, s_helpMovedFromY = -1;
    // the help page: four paragraphs in the player's language, a blank line between them
    std::string HelpText()
    {
        const slowmo::StrId p[4] = { slowmo::S_HELP_1, slowmo::S_HELP_2, slowmo::S_HELP_3, slowmo::S_HELP_4 };
        std::string t;
        for (int i = 0; i < 4; ++i) { if (i) t += "\n\n"; t += slowmo::T(p[i]); }
        return t;
    }
    // PzaHelp's "More" label takes a printf format: built from the translated "More ({1}/{2})" by slowmo::PageFormat
    // (a translation is never used as a format itself; '%' doubled, falls back to English)
    const char* MoreFmt() { static char f[128]; return slowmo::PageFormat(f, sizeof(f), slowmo::S_HELP_MORE); }
    MyGUI::Button*    s_fold = NULL;    // v0.2.0: "-" makes the window smaller (full -> compact -> tiny)
    MyGUI::Button*    s_unfold = NULL;  // v0.2.1: "+" in the compact window opens it fully again
    MyGUI::Button*    s_mini = NULL;    // v0.2.1 tiny mode: one small speed button (click = on/off), no window frame
    MyGUI::Button*    s_miniOpen = NULL;//          and "+" beside it to open the full window
    bool s_kenshiUi = true, s_shownVisible = true;
    // F91 (2026-11-03): ForgottenGUI::visible read 'hidden' while Kenshi's interface was on screen, so the window stayed hidden.
    // A hotkey press shows the window whatever the flag says; F7 hides it again only when the flag CHANGES.
    bool s_uiForce = false;
    inline bool UiOn() { return s_kenshiUi || s_uiForce; }
    MyGUI::ScrollBar* s_slider = NULL;
    MyGUI::TextBox*   s_value = NULL;
    MyGUI::Widget*    s_clicked = NULL;
    bool s_uiBroken = false;
    int  s_shownH = -1, s_shownOn = -1;
    size_t s_lastPos = (size_t)-1;
    long s_screenW = 1920, s_screenH = 1080;
    float s_scale = 1.0f;
    int  s_pending = 0;            // 1 toggle, 2 minus, 3 plus (clicks are queued and handled in the frame)

    bool Guard(void (*fn)(), const char* what)
    {
        __try { fn(); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { s_uiBroken = true; Log("window FAULT in %s - window off for this session (Shift+L still works)", what); return false; }
    }
    void SetCaption(MyGUI::TextBox* w, const char* t) { if (w) w->MyGUI::TextBox::setCaption(MyGUI::UString(t)); }
    // ---- tiny button drag (v0.2.4, user 2026-11-01: "locked in place in that obnoxious spot") ----
    // Press on the tiny button starts a drag (plain eventMouseButtonPressed, the same delegate family as the click); the frame
    // polls the mouse while the left button is held and moves both tiny widgets; release saves TinyX / TinyY. A move of more
    // than a few pixels means "drag", so the click that follows the release does not toggle slow motion.
    int  s_winPosOk = -1, s_winPX = 0, s_winPY = 0;   // UI pass: the full window's position (verified inline read, saved when moved)
    const int MINI_W = 120;   // tiny button width at 1080p (was 96; a few px spare over the widest; v1.0: "SLOW 0.50x" and Japanese both 106 px)
    int  s_tinyX = 0, s_tinyY = 0, s_grabDX = 0, s_grabDY = 0, s_pressX = 0, s_pressY = 0;
    bool s_dragArmed = false, s_dragMoved = false;
    unsigned long long s_dragEndT = 0;
    void OnMiniPressed(MyGUI::Widget*, int left, int top, MyGUI::MouseButton id)
    {
        if (id != MyGUI::MouseButton::Left) return;
        s_dragArmed = true; s_dragMoved = false;
        s_pressX = left; s_pressY = top; s_grabDX = left - s_tinyX; s_grabDY = top - s_tinyY;
    }
    // v0.2.5 (user 2026-11-01): mouse wheel over the tiny button = opacity, 0.35 .. 1 in 0.1 steps (queued, applied in the frame)
    int s_alphaSteps = 0;
    const float TINY_ALPHA_MIN = 0.35f;
    void OnMiniWheel(MyGUI::Widget*, int rel) { s_alphaSteps += rel > 0 ? 1 : (rel < 0 ? -1 : 0); }
    void ApplyTinyAlphaInner()
    {
        if (s_mini) s_mini->MyGUI::Widget::setAlpha(g.tinyAlpha);
        if (s_miniOpen) s_miniOpen->MyGUI::Widget::setAlpha(g.tinyAlpha);
    }
    unsigned long long s_miniT = 0; int s_miniX = -10000, s_miniY = -10000; bool s_openAfter = false;
    void OnClickInner()
    {
        if (s_clicked == s_mini && (s_dragMoved || GetTickCount64() - s_dragEndT < 250)) { s_dragMoved = false; return; }   // that was a drag
        if (s_clicked == s_toggle) s_pending = 1;
        else if (s_clicked == s_minus) s_pending = 2;
        else if (s_clicked == s_plus) s_pending = 3;
        else if (s_clicked == s_fold) s_pending = 4;
        else if (s_clicked && (s_clicked == s_helpBtn || s_clicked == s_help.back)) s_pending = 6;   // help on / off
        else if (s_clicked && s_clicked == s_help.more) s_pending = 7;
        else if (s_clicked == s_unfold || s_clicked == s_miniOpen) s_pending = 5;
        else if (s_clicked == s_mini)
        {   // one click toggles slow motion; a DOUBLE click (2026-11-02, user: "doesn't re-open on a double-click") also opens
            // the full window. Its two clicks toggle on and back off, so the speed ends where it started.
            const MyGUI::IntPoint& m = MyGUI::InputManager::getInstance().getMousePosition();
            const unsigned long long now = GetTickCount64();
            const bool dbl = now - s_miniT < 400 && abs(m.left - s_miniX) < 6 && abs(m.top - s_miniY) < 6;
            s_miniT = dbl ? 0 : now; s_miniX = m.left; s_miniY = m.top;
            if (dbl && s_pending == 1) s_pending = 0;   // both clicks before a frame ran: they cancel out
            else s_pending = 1;
            if (dbl) s_openAfter = true;
        }
    }
    void OnClick(MyGUI::Widget* sender) { s_clicked = sender; Guard(&OnClickInner, "click"); }
    // hover help (user, 2026-11-02 night: "LEAF tiny tells us the opacity can be changed with the mouse wheel: the others
    // should do the same")
    const char* TipFor(MyGUI::Widget* w)
    {
        if (!w) return NULL;
        if (w == s_mini) return slowmo::T(slowmo::S_TIP_MINI);
        if (w == s_miniOpen) return slowmo::T(slowmo::S_TIP_MINI_OPEN);
        if (w == s_toggle) return slowmo::T(slowmo::S_TIP_TOGGLE);
        if (w == s_minus) return slowmo::T(slowmo::S_TIP_MINUS);
        if (w == s_plus) return slowmo::T(slowmo::S_TIP_PLUS);
        if (w == s_fold) return slowmo::T(slowmo::S_TIP_FOLD);
        if (w == s_helpBtn) return slowmo::T(slowmo::S_TIP_HELP);
        return NULL;
    }
    void TipInner() { pzatip::Tick(&TipFor, s_screenW, s_screenH); }
    // Title bar (UI rules 11, user 2026-11-01): a DOUBLE click on the window title minimizes. Our own timing over the plain
    // click event of the exported Window::getCaptionWidget (two clicks within 400 ms, the mouse moved less than 6 px:
    // a drag of the window never counts).
    unsigned long long s_titleT = 0; int s_titleX = -10000, s_titleY = -10000;
    bool TitleDoubleClick()
    {
        const MyGUI::IntPoint& m = MyGUI::InputManager::getInstance().getMousePosition();
        const unsigned long long now = GetTickCount64();
        const bool dbl = now - s_titleT < 400 && abs(m.left - s_titleX) < 6 && abs(m.top - s_titleY) < 6;
        s_titleT = dbl ? 0 : now; s_titleX = m.left; s_titleY = m.top;
        return dbl;
    }
    void OnTitleClickInner() { if (TitleDoubleClick() && g.collapsed == 0) s_pending = 4; }   // full window -> tiny
    void OnTitleClick(MyGUI::Widget*) { Guard(&OnTitleClickInner, "title click"); }
    BOOL CALLBACK FindGameWindow(HWND w, LPARAM lp)
    {
        DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
        if (pid != GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
        RECT rc; if (GetClientRect(w, &rc) && rc.bottom > *(LONG*)lp) { *(LONG*)lp = rc.bottom; s_screenW = rc.right; s_screenH = rc.bottom; }
        return TRUE;
    }
    int P(float v) { return (int)(v * s_scale + 0.5f); }
    MyGUI::Button* Btn(int x, int y, int w, int h, const char* t)
    {
        MyGUI::Button* b = s_win->createWidget<MyGUI::Button>("Kenshi_Button1", MyGUI::IntCoord(x, y, w, h), MyGUI::Align::Default);
        SetCaption(b, t);
        b->eventMouseButtonClick += MyGUI::newDelegate(&OnClick);
        return b;
    }
    int s_fullH = 0, s_smallH = 0, s_winW = 0, s_toggleFullW = 0, s_toggleSmallW = 0;
    // three sizes (Collapsed in the ini): 0 full window; 1 compact (title + on/off + "-" "+"); 2 tiny (no window: one small
    // speed button + "+"). Exported Widget::setSize / setVisible only; the tiny widgets sit where the window opens.
    void ApplyFold()
    {
        const int c = g.collapsed;
        const bool full = c == 0;
        if (!full) pzahelp::Close(s_help);   // first: Close shows again what the help page hid
        // the help page covers the speed row (pzahelp::Cover hides it too; user screenshot 2026-11-06: text overlapped)
        const bool row = full && !s_help.open;
        if (s_value) s_value->MyGUI::Widget::setVisible(row);
        if (s_minus) s_minus->MyGUI::Widget::setVisible(row);
        if (s_slider) s_slider->MyGUI::Widget::setVisible(row);
        if (s_plus) s_plus->MyGUI::Widget::setVisible(row);
        if (s_unfold) s_unfold->MyGUI::Widget::setVisible(c == 1);
        if (s_helpBtn) s_helpBtn->MyGUI::Widget::setVisible(full);
        if (s_toggle) s_toggle->MyGUI::Widget::setSize(c == 1 ? s_toggleSmallW : s_toggleFullW, P(30));
        if (s_win && s_winW > 0) s_win->MyGUI::Widget::setSize(s_winW, full ? (s_help.open ? s_helpH : s_fullH) : s_smallH);
        if (s_win) s_win->MyGUI::Widget::setVisible(UiOn() && c != 2);
        if (s_mini) s_mini->MyGUI::Widget::setVisible(UiOn() && c == 2);
        if (s_miniOpen) s_miniOpen->MyGUI::Widget::setVisible(UiOn() && c == 2);
    }
    void VisInner() { ApplyFold(); }
    // help on / off: the window grows to s_helpH while it is open, moving up if it would run off the bottom of the screen
    // (only when its position read is trusted), and goes back to where it was on close
    void HelpToggleInner()
    {
        if (!s_win) return;
        if (s_help.open)
        {
            pzahelp::Close(s_help);
            if (s_helpMovedFromY >= 0 && s_winPosOk == 1) { const MyGUI::IntPoint at = s_win->getPosition(); s_win->MyGUI::Widget::setPosition(at.left, s_helpMovedFromY); }
            s_helpMovedFromY = -1;
            ApplyFold(); Log("help closed");
            return;
        }
        { const std::string ht = HelpText(); pzahelp::Open(s_help, ht.c_str(), MoreFmt()); }
        if (s_winPosOk == 1)
        {
            const MyGUI::IntPoint at = s_win->getPosition();
            if (at.top + s_helpH > s_screenH) { s_helpMovedFromY = at.top; int ny = (int)s_screenH - s_helpH; if (ny < 0) ny = 0; s_win->MyGUI::Widget::setPosition(at.left, ny); }
        }
        ApplyFold();
        Log("help opened: %d lines on %d page(s)%s", s_help.lines, (int)s_help.pages.size(), s_help.estimated ? " (text width ESTIMATED: the measure call returned nothing)" : "");
    }
    void HelpMoreInner() { pzahelp::More(s_help, MoreFmt()); }
    void BuildInner()
    {
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (!gui) return;
        LONG h = 0; EnumWindows(&FindGameWindow, (LPARAM)&h);
        s_scale = h > 0 ? floorf((float)h / 1080.0f * 4.0f + 0.5f) / 4.0f : 1.0f; if (s_scale < 1) s_scale = 1; if (s_scale > 3) s_scale = 3;
        const int pad = P(10), bh = P(30), th = P(24), gap = P(6), innerW = P(330);   // 330 (was 290, 250): v1.0 fits every language (tests/lang/check_lang.py: English "Slow motion: off (0.50x)" is 235 px)
        const int winW = pad * 2 + innerW + P(16), winH = P(6) + bh + gap + th + gap + bh + P(8) + P(54);
        s_fullH = winH; s_smallH = P(6) + bh + P(8) + P(54); s_winW = winW;
        int x = g.winX >= 0 ? (int)(g.winX * s_screenW) : (int)(s_screenW * 0.73f) - winW;
        int y = g.winY >= 0 ? (int)(g.winY * s_screenH) : (int)(s_screenH * 0.62f);
        if (x < 0) x = 0; if (y < 0) y = 0; if (x > s_screenW - winW) x = s_screenW - winW; if (y > s_screenH - winH) y = s_screenH - winH;
        s_win = gui->createWidget<MyGUI::Window>("Kenshi_WindowC", MyGUI::IntCoord(x, y, winW, winH), MyGUI::Align::Default, "Window", WIN_NAME);
        {   // the position getter is an inline member read: trust it only if it returns where we just put the window
            const MyGUI::IntPoint got = s_win->getPosition();
            s_winPosOk = (got.left == x && got.top == y) ? 1 : 0; s_winPX = x; s_winPY = y;
            Log("window position read check: put %d,%d, read %d,%d -> %s", x, y, got.left, got.top, s_winPosOk ? "position will be remembered" : "MISMATCH, position not saved");
        }
        s_win->MyGUI::Window::setCaption(MyGUI::UString(slowmo::T(slowmo::S_WIN_TITLE)));
        { MyGUI::TextBox* cap = s_win->getCaptionWidget(); if (cap) cap->eventMouseButtonClick += MyGUI::newDelegate(&OnTitleClick); }
        int yy = P(6);
        const int fw = P(34);
        s_toggleFullW = innerW - 2 * (fw + gap); s_toggleSmallW = innerW - 2 * (fw + gap);
        s_toggle = Btn(pad, yy, s_toggleFullW, bh, "");
        s_unfold = Btn(pad + innerW - 2 * fw - gap, yy, fw, bh, "+");
        s_helpBtn = Btn(pad + innerW - 2 * fw - gap, yy, fw, bh, "?");
        s_fold   = Btn(pad + innerW - fw, yy, fw, bh, "-"); yy += bh + gap;
        s_value = s_win->createWidget<MyGUI::TextBox>("Kenshi_TextboxStandardText", MyGUI::IntCoord(pad, yy, innerW, th), MyGUI::Align::Default); yy += th + gap;
        const int sb = P(34);
        s_minus = Btn(pad, yy, sb, bh, "-");
        s_slider = s_win->createWidget<MyGUI::ScrollBar>("Kenshi_SliderHBig", MyGUI::IntCoord(pad + sb + gap, yy, innerW - 2 * (sb + gap), bh), MyGUI::Align::Default);
        s_slider->setScrollRange((size_t)slowcore::Positions(s_range));   // positions 0..n-1 = min, min+step, ... max
        s_slider->setScrollPage(1); s_slider->setScrollViewPage(1);
        s_plus = Btn(pad + innerW - sb, yy, sb, bh, "+");
        {   // tiny mode widgets: root-level buttons (no frame, no title), same layer as the window
            const int mw = P(MINI_W), ow = P(30);
            // v0.2.4: its own spot, saved when dragged; default the top-right corner (out of the way of Kenshi's panels)
            s_tinyX = g.tinyX >= 0 ? (int)(g.tinyX * s_screenW) : (int)s_screenW - mw - ow - P(4) - P(24);
            s_tinyY = g.tinyY >= 0 ? (int)(g.tinyY * s_screenH) : P(14);
            if (s_tinyX < 0) s_tinyX = 0; if (s_tinyY < 0) s_tinyY = 0;
            if (s_tinyX > s_screenW - mw - ow - P(4)) s_tinyX = s_screenW - mw - ow - P(4); if (s_tinyY > s_screenH - bh) s_tinyY = s_screenH - bh;
            s_mini = gui->createWidget<MyGUI::Button>("Kenshi_Button1", MyGUI::IntCoord(s_tinyX, s_tinyY, mw, bh), MyGUI::Align::Default, "Window", "PZASlowMo_Mini");
            s_mini->eventMouseButtonClick += MyGUI::newDelegate(&OnClick);
            s_mini->eventMouseButtonPressed += MyGUI::newDelegate(&OnMiniPressed);
            s_mini->eventMouseWheel += MyGUI::newDelegate(&OnMiniWheel);
            s_miniOpen = gui->createWidget<MyGUI::Button>("Kenshi_Button1", MyGUI::IntCoord(s_tinyX + mw + P(4), s_tinyY, ow, bh), MyGUI::Align::Default, "Window", "PZASlowMo_MiniOpen");
            SetCaption(s_miniOpen, "+");
            s_miniOpen->eventMouseButtonClick += MyGUI::newDelegate(&OnClick);
            s_miniOpen->eventMouseWheel += MyGUI::newDelegate(&OnMiniWheel);
            pzatip::Create(MyGUI::Gui::getInstancePtr(), "PZASlowMo_ToolTip");
            ApplyTinyAlphaInner();
        }
        {   // the help page: below the top row; the window grows to s_helpH while it is open (created last: draws on top)
            s_helpH = P(470);
            const int hy = P(6) + bh + gap;
            pzahelp::Create(s_help, s_win, pad, hy, innerW, s_helpH - P(54) - hy, bh, P(10), slowmo::T(slowmo::S_HELP_BACK), MoreFmt(), &OnClick);   // ShowPage sets the "More" caption
        }
        s_win->MyGUI::Widget::setVisible(true); s_shownVisible = true;
        s_shownH = -1; s_shownOn = -1; s_lastPos = (size_t)-1;
        ApplyFold();
        Log("window built at %d,%d (scale %.2f, screen %ldx%ld); slider %d positions", x, y, s_scale, s_screenW, s_screenH, slowcore::Positions(s_range));
    }
    void CheckAliveInner()
    {
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        MyGUI::Widget* found = gui ? gui->findWidgetT(WIN_NAME, false) : NULL;
        if (s_win && found != s_win) { Log("window was destroyed by the game - will rebuild"); s_win = NULL; s_toggle = s_minus = s_plus = s_fold = s_unfold = s_helpBtn = NULL; pzahelp::Forget(s_help); s_helpMovedFromY = -1; s_slider = NULL; s_value = NULL; s_mini = s_miniOpen = NULL; pzatip::Forget(); }
    }
    int  s_sliderH = -1;           // speed the slider asks for this frame (-1 = no change)
    void SyncInner()
    {
        // slider -> speed (snapped to the step, so 0.10 / 0.50 / 0.60 land exactly)
        const size_t pos = s_slider->getScrollPosition();
        if (s_lastPos != (size_t)-1 && pos != s_lastPos) s_sliderH = slowcore::PosToH(s_range, (int)pos);
        s_lastPos = pos;
        // speed / state -> captions and slider
        if (g.speed != s_shownH || (int)s_on != s_shownOn)
        {
            char b[256], sp[16], lo[16], hi[16];   // numbers are formatted here; the texts only place them ({1}..{3})
            _snprintf_s(sp, sizeof(sp), _TRUNCATE, "%.2f", g.speed / 100.0f);
            _snprintf_s(lo, sizeof(lo), _TRUNCATE, "%.2f", g.minH / 100.0f);
            _snprintf_s(hi, sizeof(hi), _TRUNCATE, "%.2f", g.maxH / 100.0f);
            SetCaption(s_toggle, slowmo::TF(b, sizeof(b), s_on ? slowmo::S_TOGGLE_ON : slowmo::S_TOGGLE_OFF, sp));
            if (s_mini) { SetCaption(s_mini, slowmo::TF(b, sizeof(b), s_on ? slowmo::S_MINI_ON : slowmo::S_MINI_OFF, sp)); s_mini->setStateSelected(s_on); }
            s_toggle->setStateSelected(s_on);
            SetCaption(s_value, slowmo::TF(b, sizeof(b), slowmo::S_SPEED_ROW, sp, lo, hi));
            const int p = slowcore::HToPos(s_range, g.speed);
            if (s_lastPos != (size_t)p) { s_slider->setScrollPosition((size_t)p); s_lastPos = (size_t)p; }
            s_shownH = g.speed; s_shownOn = s_on;
        }
    }
    void DragInner()
    {
        const MyGUI::IntPoint& m = MyGUI::InputManager::getInstance().getMousePosition();
        const bool held = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (!s_dragMoved && (abs(m.left - s_pressX) > P(4) || abs(m.top - s_pressY) > P(4))) s_dragMoved = true;
        if (s_dragMoved && s_mini && s_miniOpen)
        {
            const int mw = P(MINI_W), ow = P(30), bh = P(30);
            int nx = m.left - s_grabDX, ny = m.top - s_grabDY;
            if (nx < 0) nx = 0; if (ny < 0) ny = 0;
            if (nx > s_screenW - mw - ow - P(4)) nx = s_screenW - mw - ow - P(4); if (ny > s_screenH - bh) ny = s_screenH - bh;
            if (nx != s_tinyX || ny != s_tinyY)
            {
                s_tinyX = nx; s_tinyY = ny;
                s_mini->MyGUI::Widget::setPosition(nx, ny);
                s_miniOpen->MyGUI::Widget::setPosition(nx + mw + P(4), ny);
            }
        }
        if (held) return;
        s_dragArmed = false;
        if (!s_dragMoved) return;
        s_dragEndT = GetTickCount64();
        g.tinyX = (float)s_tinyX / (float)s_screenW; g.tinyY = (float)s_tinyY / (float)s_screenH;
        char v[32];
        _snprintf_s(v, sizeof(v), _TRUNCATE, "%.4f", g.tinyX); WritePrivateProfileStringA("PZASlowMo", "TinyX", v, IniPath().c_str());
        _snprintf_s(v, sizeof(v), _TRUNCATE, "%.4f", g.tinyY); WritePrivateProfileStringA("PZASlowMo", "TinyY", v, IniPath().c_str());
        Log("tiny button moved to %d,%d (saved as %.3f, %.3f)", s_tinyX, s_tinyY, g.tinyX, g.tinyY);
    }
    void WinPosInner()
    {
        if (!s_win || s_winPosOk != 1 || g.collapsed == 2) return;
        const MyGUI::IntPoint got = s_win->getPosition();
        if (got.left == s_winPX && got.top == s_winPY) return;
        s_winPX = got.left; s_winPY = got.top;
        g.winX = (float)s_winPX / (float)s_screenW; g.winY = (float)s_winPY / (float)s_screenH;
        char v[32];
        _snprintf_s(v, sizeof(v), _TRUNCATE, "%.4f", g.winX); WritePrivateProfileStringA("PZASlowMo", "WindowX", v, IniPath().c_str());
        _snprintf_s(v, sizeof(v), _TRUNCATE, "%.4f", g.winY); WritePrivateProfileStringA("PZASlowMo", "WindowY", v, IniPath().c_str());
        static int n_pos = 0;
        if (++n_pos <= 20) Log("window moved to %d,%d (saved as %.3f, %.3f)", s_winPX, s_winPY, g.winX, g.winY);
    }
    void KeyFocusInner(MyGUI::Widget** out) { *out = MyGUI::InputManager::getInstance().getKeyFocusWidget(); }
    // 2026-10-31 log: the guard blocked Shift+K (PZA Camera) once because OUR menu window had the keyboard focus (clicking a window gives it
    // focus). Only a real text box counts as typing: an EditBox, or anything derived from one. MSVC's dynamic_cast compares
    // the type by its decorated name, and MyGUIEngine_x64.dll carries EditBox's type descriptor, so this works across DLLs.
    // 2026-11-01 (PZA Camera log 468-477 s: Shift+K "ignored" five times with the keyboard on the menu's own 'Caption'):
    // a window title is an EditBox in Kenshi's skin, nested BELOW the window (not its direct child), so the old test
    // (read-only, or parent is a Window) missed it and the hand-back never fired. A title = named "Caption" with a
    // Window within 4 levels up. Titles never count as typing.
    static bool IsTitleInner(MyGUI::Widget* w)
    {
        if (!w || w->getName() != "Caption") return false;
        MyGUI::Widget* p = w->getParent();
        for (int d = 0; p && d < 4; ++d, p = p->getParent()) if (dynamic_cast<MyGUI::Window*>(p)) return true;
        return false;
    }
    static bool IsEditInner(MyGUI::Widget* w) { return dynamic_cast<MyGUI::EditBox*>(w) != NULL && !IsTitleInner(w); }
    static bool IsEditBox(MyGUI::Widget* w)
    {
        __try { return IsEditInner(w); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return true; }   // can't tell: behave as before (treat as typing)
    }
    bool Typing()
    {
        MyGUI::Widget* w = NULL;
        __try { KeyFocusInner(&w); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        if (!w) return false;
        const bool edit = IsEditBox(w);
        static int n_notEdit = 0;
        if (!edit && n_notEdit++ < 5) Log("keyboard focus on a widget that is not a text box - hotkey allowed (shown %d of 5)", n_notEdit);
        return edit;
    }
    // ---- keyboard hand-back (v0.2.0, finding F68; same rule as PZA Camera v0.15.0) ----
    // 2026-11-01 log: the keyboard sat on a widget named 'Caption' (a window's title bar: a text box in Kenshi's skin) after a
    // window was clicked or dragged by its title. While a text box has the keyboard, Kenshi swallows Space / F7 and our hotkeys
    // stand down. A title bar is not for typing: give the keyboard back. Exported MyGUI calls only, under SEH; logs capped.
    static void FocusWatchInner(int* released, char* chain, int n)
    {
        MyGUI::InputManager& im = MyGUI::InputManager::getInstance();
        MyGUI::Widget* w = im.getKeyFocusWidget();
        if (!w) return;
        MyGUI::EditBox* e = dynamic_cast<MyGUI::EditBox*>(w);
        if (!e || w->getName() != "Caption") return;
        if (!e->getEditReadOnly() && !IsTitleInner(w)) return;   // a real, typable box: leave it
        std::string c;
        MyGUI::Widget* p = w;
        for (int d = 0; p && d < 5; ++d, p = p->getParent()) { if (d) c += " < "; c += p->getName().empty() ? std::string("(unnamed)") : p->getName(); }
        _snprintf_s(chain, n, _TRUNCATE, "%s", c.c_str());
        im.resetKeyFocusWidget();
        *released = 1;
    }
    void FocusWatch()
    {
        static bool dead = false; static int n_rel = 0;
        if (dead) return;
        int released = 0; char chain[256] = { 0 };
        __try { FocusWatchInner(&released, chain, sizeof(chain)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { dead = true; Log("keyboard hand-back FAULTED - off for this session"); return; }
        if (released && ++n_rel <= 20) Log("keyboard handed back from a window title (%s) - count %d", chain, n_rel);
    }

    bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
    bool OurWindowFocused() { DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid); return pid == GetCurrentProcessId(); }

    // Fresh-press latch (2026-11-13, user: "shift gets stuck and starts opening all the menus ... I had to click every letter
    // related to our hotkeys before one of them finally unlocked it"; log 5638 s: Shift+O and Shift+R fired in the same
    // millisecond, then Shift+R 17 more times in 16 s with no R typed). Windows was reporting the LETTERS as held, so every
    // Shift press completed the combination. A hotkey now starts only on a fresh press of its letter while the modifiers
    // match, and lasts while both stay held. A letter reported held for 4 s without that is logged once (stuck = true).
    struct PzaKeyLatch
    {
        bool prev, on, warned; unsigned long long since;
        PzaKeyLatch() : prev(false), on(false), warned(false), since(0) {}
        bool tick(bool keyDown, bool mods, bool* stuck)
        {
            if (stuck) *stuck = false;
            const unsigned long long now = GetTickCount64();
            if (!keyDown) { on = false; warned = false; }
            else if (!prev) { since = now; on = mods; }
            else if (!mods) on = false;
            else if (!on && !warned && now - since > 4000) { warned = true; if (stuck) *stuck = true; }
            prev = keyDown;
            return on;
        }
    };
    void Frame(GameWorld* w)
    {
        if (s_dead || !w) return;
        // Kenshi's loading panel (isLoadingFromASaveGame = that panel is visible; it also shows in normal play, 2026-11-01 log):
        // wait while it shows, decide when it goes (see slowcore::StillOurs). Logs capped at 20 pairs.
        static bool s_panel = false; static unsigned long long s_panelT = 0; static int n_panel = 0;
        if (w->isLoadingFromASaveGame())
        {
            if (!s_panel) { s_panel = true; s_panelT = GetTickCount64(); if (++n_panel <= 20) Log("Kenshi's loading panel is showing (slow motion %s) - waiting (count %d)", s_on ? "on" : "off", n_panel); }
            return;
        }
        if (s_panel)
        {
            s_panel = false;
            const float cur = w->getFrameSpeedMultiplier();
            const double secs = (GetTickCount64() - s_panelT) / 1000.0;
            if (s_on && !slowcore::StillOurs(cur, s_setH)) { if (n_panel <= 20) Log("loading panel gone after %.1f s; Kenshi runs at %.2f, not ours", secs, cur); SlowOff(w, "after loading"); }
            else if (n_panel <= 20) Log("loading panel gone after %.1f s; speed %.2f -> slow motion stays %s", secs, cur, s_on ? "on" : "off");
        }
        FocusWatch();
        // hotkey (exact modifiers; not while typing)
        static PzaKeyLatch s_latch; bool stuck = false;
        const bool down = s_latch.tick(g.hkKey && KeyDown(g.hkKey), OurWindowFocused() && KeyDown(VK_CONTROL) == (g.hkCtrl != 0) && KeyDown(VK_SHIFT) == (g.hkShift != 0)
                          && KeyDown(VK_MENU) == (g.hkAlt != 0), &stuck);
        if (stuck) Log("hotkey: its key has been reported held for 4 s with no fresh press - a stuck key or another program holds it; press and release that key once to free it");
        {   // v0.2.3: hold the hotkey + tap + / - (main row or numpad) = speed steps, works with F7 on; alone = toggle on release
            static slowcore::Chord s_chord = { false, false, false, false };
            const bool plus = OurWindowFocused() && (KeyDown(VK_OEM_PLUS) || KeyDown(VK_ADD));
            const bool minus = OurWindowFocused() && (KeyDown(VK_OEM_MINUS) || KeyDown(VK_SUBTRACT));
            const int act = slowcore::ChordStep(s_chord, down, plus, minus);
            if (act != 0 && Typing()) Log("hotkey ignored: a text box has the keyboard");
            else if (act == 1)
            {
                if (s_on) SlowOff(w, "hotkey"); else SlowOn(w);
                if (!s_kenshiUi && !s_uiForce && s_win)
                {   // rule F91: log every time it fires
                    static int n_force = 0;
                    s_uiForce = true;
                    if (Guard(&VisInner, "visibility") && ++n_force <= 20) Log("hotkey while Kenshi's interface flag reads hidden (F7 / F91) - window shown anyway (count %d)", n_force);
                }
            }
            else if (act == 2 || act == 3)
            {
                ChangeSpeed(w, slowcore::PosToH(s_range, slowcore::HToPos(s_range, g.speed) + (act == 2 ? 1 : -1)));
                static int n_chord = 0;
                if (++n_chord <= 20) Log("hotkey + %s: speed %.2f (count %d)", act == 2 ? "+" : "-", g.speed / 100.0f, n_chord);
            }
        }
        s_hkDown = down;
        // Kenshi's own speed buttons / F2-F4 while slow motion is on: hand control back to Kenshi
        if (s_on && s_readbackOk && !w->isPaused())
        {
            const float cur = w->getFrameSpeedMultiplier();
            if (cur > 0.0f && fabsf(cur - s_setH / 100.0f) > 0.004f) { ++n_external; Log("speed changed to %.2f by Kenshi (count %d)", cur, n_external); SlowOff(w, "Kenshi speed button"); }
        }
        // window
        if (!g.showWindow || s_uiBroken) return;
        static unsigned long long s_lastCheck = 0;
        const unsigned long long now = GetTickCount64();
        if (now - s_lastCheck > 1000)
        {
            s_lastCheck = now;
            if (!Guard(&CheckAliveInner, "lookup")) return;
            if (s_win && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) Guard(&WinPosInner, "position");   // not mid-drag
        }
        if (!s_win && !Guard(&BuildInner, "build")) return;
        if (!s_win) return;
        // F7 ("Toggle main gui") flips ForgottenGUI::visible (+0x330, read in ForgottenGUI::toggle, Steam 0x6E26D0): hide with it
        {
            static int n_vis = 0;
            const bool kui = !g.hideWithUi || !gui || gui->visible;
            if (kui != s_shownVisible)
            {
                s_kenshiUi = kui;
                s_uiForce = false;   // F7 really pressed (or the flag moved): follow Kenshi's interface again
                if (Guard(&VisInner, "visibility")) { s_shownVisible = kui; if (++n_vis <= 20) Log("Kenshi's interface %s - window %s (count %d)", kui ? "shown" : "hidden (F7)", kui ? "shown" : "hidden", n_vis); }
            }
            if (!UiOn()) return;
        }
        if (s_dragArmed && g.collapsed == 2) Guard(&DragInner, "drag");
        Guard(&TipInner, "tooltip");
        if (s_alphaSteps != 0)
        {
            const int st = s_alphaSteps; s_alphaSteps = 0;
            float a = g.tinyAlpha + 0.1f * st;
            if (a < TINY_ALPHA_MIN) a = TINY_ALPHA_MIN; if (a > 1.0f) a = 1.0f;
            if (a != g.tinyAlpha)
            {
                g.tinyAlpha = a;
                Guard(&ApplyTinyAlphaInner, "opacity");
                char v[16]; _snprintf_s(v, sizeof(v), _TRUNCATE, "%.2f", a);
                WritePrivateProfileStringA("PZASlowMo", "TinyAlpha", v, IniPath().c_str());
                static int n_alpha = 0;
                if (++n_alpha <= 20) Log("tiny button opacity %.2f (saved; wheel over it, %.2f minimum) - count %d", a, TINY_ALPHA_MIN, n_alpha);
            }
        }
        s_sliderH = -1;
        if (!Guard(&SyncInner, "sync")) return;
        if (s_sliderH >= 0) ChangeSpeed(w, s_sliderH);
        if (!s_pending && s_openAfter) { s_openAfter = false; s_pending = 5; Log("tiny button double-clicked: opening the window"); }
        if (s_pending)
        {
            const int p = s_pending; s_pending = 0;
            if (p == 1) { if (s_on) SlowOff(w, "button"); else SlowOn(w); }
            else if (p == 4 || p == 5)
            {
                g.collapsed = p == 5 ? 0 : 2;   // v0.2.2 (user 2026-11-01: compact looked squished, tiny was one click too far): "-" = tiny, "+" = full
                Guard(&ApplyFold, "fold");
                char cv[4]; _snprintf_s(cv, sizeof(cv), _TRUNCATE, "%d", g.collapsed);
                WritePrivateProfileStringA("PZASlowMo", "Collapsed", cv, IniPath().c_str());
                Log("window %s (saved)", g.collapsed == 0 ? "full" : (g.collapsed == 1 ? "compact" : "tiny"));
            }
            else if (p == 6) Guard(&HelpToggleInner, "help");
            else if (p == 7) Guard(&HelpMoreInner, "help more");
            else ChangeSpeed(w, slowcore::PosToH(s_range, slowcore::HToPos(s_range, g.speed) + (p == 3 ? 1 : -1)));
        }
    }

    typedef void (*LoopFn)(GameWorld*, float);
    LoopFn s_loopOrig = NULL;
    void LoopHookSafe(GameWorld* self)
    {
        __try { Frame(self); }
        __except (EXCEPTION_EXECUTE_HANDLER) { s_dead = true; Log("FAULT in frame update - plugin off for this session"); }
    }
    void LoopHook(GameWorld* self, float t)
    {
        s_loopOrig(self, t);
        LoopHookSafe(self);
    }

    std::string ModuleDir()
    {
        HMODULE self = 0;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&ModuleDir, &self);
        char path[MAX_PATH] = { 0 };
        GetModuleFileNameA(self, path, MAX_PATH);
        std::string p = path; size_t s = p.find_last_of("\\/");
        return s == std::string::npos ? std::string(".") : p.substr(0, s);
    }
}

__declspec(dllexport) void startPlugin()
{
    char mtx[64]; _snprintf_s(mtx, sizeof(mtx), _TRUNCATE, "Local\\PZASlowMo_%lu", GetCurrentProcessId());
    HANDLE h = CreateMutexA(NULL, FALSE, mtx);
    if (h && GetLastError() == ERROR_ALREADY_EXISTS) { Log("already loaded - this copy is disabled"); CloseHandle(h); return; }
    s_dir = ModuleDir();
    Log("v1.0.1 - PZA's SlowMo (one click or Shift+L; slider 0.10-0.60) for Kenshi, by PZA");
    LoadSettings();
    slowmo::Strings_Init(IniPath().c_str(), &LogLine);
    if (!g.enabled) { Log("Enabled=0 - inactive"); return; }
    if (KenshiLib::AddHook(KenshiLib::GetRealAddress(&GameWorld::_NV_mainLoop_GPUSensitiveStuff), &LoopHook, &s_loopOrig) != KenshiLib::SUCCESS)
    { Log("main loop hook FAILED - inactive"); return; }
    Log("hooked GameWorld::mainLoop_GPUSensitiveStuff");
}
