// PzaTip.h - PZA hover help (port of LEAF's LeafWindow tooltips, 2026-11-02). Author: PZA. Licence: GPLv3 or later.
// A framed text box on MyGUI's ToolTip layer that follows the hovered widget after 450 ms. Its size comes from the text
// MyGUI really laid out (exported TextBox::getTextSize: fonts differ with resolution and Kenshi's font option). Exported
// MyGUI calls only; the caller runs Tick() under its own SEH guard and calls Forget() when the game rebuilds its GUI.
#pragma once
#include <windows.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_InputManager.h>

namespace pzatip
{
    struct State { MyGUI::Widget* box; MyGUI::TextBox* text; MyGUI::Widget* hover; unsigned long long since; bool shown; };
    inline State& S() { static State s = { 0, 0, 0, 0, false }; return s; }
    typedef const char* (*TipFn)(MyGUI::Widget*);

    inline void Create(MyGUI::Gui* gui, const char* name)
    {
        State& s = S();
        if (s.box || !gui) return;
        s.box = gui->createWidget<MyGUI::Widget>("Kenshi_GenericTextBoxSkin", MyGUI::IntCoord(0, 0, 440, 80), MyGUI::Align::Default, "ToolTip", name);
        s.text = s.box->createWidget<MyGUI::TextBox>("Kenshi_TextboxStandardText", MyGUI::IntCoord(12, 8, 416, 64), MyGUI::Align::Default);
        s.box->setNeedMouseFocus(false); s.text->setNeedMouseFocus(false);
        s.box->MyGUI::Widget::setVisible(false);
    }
    inline void Forget() { State& s = S(); s.box = 0; s.text = 0; s.hover = 0; s.shown = false; }
    inline void Hide() { State& s = S(); if (s.box && s.shown) s.box->MyGUI::Widget::setVisible(false); s.shown = false; }

    inline void Tick(TipFn tipFor, long screenW, long screenH)
    {
        State& s = S();
        if (!s.box) return;
        MyGUI::Widget* f = MyGUI::InputManager::getInstance().getMouseFocusWidget();
        const char* tip = NULL; MyGUI::Widget* hit = NULL;
        for (int depth = 0; f && depth < 6 && !tip; ++depth, f = f->getParent())
            if ((tip = tipFor(f)) != NULL) hit = f;
        const unsigned long long now = GetTickCount64();
        if (hit != s.hover) { s.hover = hit; s.since = now; Hide(); return; }
        if (!hit || s.shown || now - s.since < 450) return;
        int lines = 1; for (const char* c = tip; *c; ++c) if (*c == '\n') ++lines;
        s.text->setSize(4000, 1000);
        s.text->MyGUI::TextBox::setCaption(MyGUI::UString(tip));
        const MyGUI::IntSize ts = s.text->MyGUI::TextBox::getTextSize();
        int w = 440, h = 22 * lines + 18;
        if (ts.width > 0 && ts.height > 0) { w = ts.width + 28; h = ts.height + 20; }
        if (w < 160) w = 160; if (w > screenW - 8) w = screenW - 8;
        const MyGUI::IntPoint& m = MyGUI::InputManager::getInstance().getMousePosition();
        int x = m.left + 18, y = m.top + 24;
        if (x + w > screenW) x = screenW - w - 4;
        if (y + h > screenH) y = m.top - h - 8;
        s.text->setSize(w - 24, h - 12);
        s.box->setSize(w, h);
        s.box->setPosition(x, y);
        s.box->MyGUI::Widget::setVisible(true);
        s.shown = true;
    }
}
