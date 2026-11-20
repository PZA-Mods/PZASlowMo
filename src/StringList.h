// StringList.h - every user-visible PZA's SlowMo string: id, key in the language files, where it is drawn, English text.
// Author: PZA. Licence: GPLv3 or later.
//
// An X-macro list: include it after defining SLOW_STR(id, key, font, width, english). No include guard on purpose.
//   key     the name used in lang\<code>.txt ("key = text")
//   font    where the text is drawn, for tests\lang\check_lang.py (glyph + width check):
//             'P' Kenshi_PaintedTextFont_Medium (buttons: Kenshi_Button1, the window title)
//             'S' Kenshi_StandardFont_Medium    (text rows: Kenshi_TextboxStandardText)
//             'H' help page paragraph: Kenshi_StandardFont_Medium, wrapped at run time (PzaHelp.h) to the page width;
//                 every unbreakable piece (a word, or one CJK character) must fit that width
//             'T' tooltip: Kenshi_StandardFont_Medium, the box sizes itself; each line is checked against a cap
//             'M' Kenshi's own player message (GameWorld::showPlayerAMessage): font unknown, width not checked
//   width   the widget's text width in pixels at layout scale 1 (1080p; PZASlowMo.cpp BuildInner()); 0 = no fixed width
//   english the compiled-in default. ASCII ONLY: VS2010 reads source in the ANSI code page, and Kenshi's English
//           fonts carry ASCII only.
// Placeholders are {1}..{9}, never printf formats: a translation must use exactly the same set or it is refused
// (localization-plan-2026-11-01.md, text rule 2). Log lines are NOT here: they stay English (they're for us).
//
// Widths from BuildInner(): inner 330, toggle 250 (330 - 2 x (34 + 6)), tiny button 120 (MINI_W), help page text 310
// (330 - 2 x 10), help Back / More 150 ((330 - 3 x 10) / 2), window title ~330 (window 366).

// ---- window
SLOW_STR(S_WIN_TITLE,   "win.title",   'P', 330, "PZA's SlowMo")
SLOW_STR(S_TOGGLE_ON,   "toggle.on",   'P', 250, "Slow motion: ON ({1}x)")
SLOW_STR(S_TOGGLE_OFF,  "toggle.off",  'P', 250, "Slow motion: off ({1}x)")
SLOW_STR(S_SPEED_ROW,   "speed.row",   'S', 330, "Speed {1}x  ({2} - {3})")
// ---- the tiny button (no window frame)
SLOW_STR(S_MINI_ON,     "mini.on",     'P', 120, "SLOW {1}x")
SLOW_STR(S_MINI_OFF,    "mini.off",    'P', 120, "slow {1}x")
// ---- help page ("?"): four paragraphs, joined with a blank line
SLOW_STR(S_HELP_BACK,   "help.back",   'P', 150, "Back")
SLOW_STR(S_HELP_MORE,   "help.more",   'P', 150, "More ({1}/{2})")
SLOW_STR(S_HELP_1,      "help.1",      'H', 310, "PZA's SlowMo slows the whole game down for dramatic moments, then puts it back.")
SLOW_STR(S_HELP_2,      "help.2",      'H', 310, "The top button switches slow motion on and off (also Shift+L). The slider, - and + pick how slow. Hold Shift+L and tap + or - to change the speed from the keyboard.")
SLOW_STR(S_HELP_3,      "help.3",      'H', 310, "Kenshi's own speed buttons (or F2 to F4) while it's on hand control back to Kenshi.")
SLOW_STR(S_HELP_5,      "help.5",      'H', 310, "Kill cam (KillCam=1 in PZASlowMo.ini, off by default): when a selected character kills, knocks out or severs a limb, time runs slow for a moment (0.25x for 1 second, both set in the ini; KillCamKills, KillCamKnockouts and KillCamLimbs switch each kind; KillCamDodges and KillCamOwn add dodges and your own character going down).")
SLOW_STR(S_HELP_4,      "help.4",      'H', 310, "The - button, or a double-click on the title, shrinks the window to a tiny speed button: click it to switch slow motion, double-click it or press its + to open this window again, drag it to move it, mouse wheel for opacity. F7 hides it with Kenshi's interface. Everything you change is saved.")
// ---- hover help
SLOW_STR(S_TIP_MINI,    "tip.mini",    'T',   0, "Slow motion: click to switch it on/off.\nDouble-click: open the full window.\nDrag to move it. Mouse wheel: opacity.")
SLOW_STR(S_TIP_MINI_OPEN, "tip.mini_open", 'T', 0, "Open the full SlowMo window.\nMouse wheel: opacity.")
SLOW_STR(S_TIP_TOGGLE,  "tip.toggle",  'T',   0, "Slow motion on/off (also Shift+L).")
SLOW_STR(S_TIP_MINUS,   "tip.minus",   'T',   0, "Slower (one step).")
SLOW_STR(S_TIP_PLUS,    "tip.plus",    'T',   0, "Faster (one step).")
SLOW_STR(S_TIP_FOLD,    "tip.fold",    'T',   0, "Shrink to the tiny button\n(or double-click the title).")
SLOW_STR(S_TIP_HELP,    "tip.help",    'T',   0, "Help: what everything in this\nwindow does.")
// ---- on-screen messages (Kenshi's own message line)
SLOW_STR(S_MSG_ON,      "msg.on",      'M',   0, "Slow motion {1}x")
SLOW_STR(S_MSG_OFF,     "msg.off",     'M',   0, "Slow motion off")
