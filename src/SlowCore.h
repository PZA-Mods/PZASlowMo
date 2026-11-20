// SlowCore.h - PZA's SlowMo's rules with no Kenshi code, so tests/test_core.cpp can check and fuzz them offline.
// Author: PZA. Licence: GPLv3 or later.
//
// Speeds are kept in whole hundredths (10..60 = 0.10..0.60): 0.10, 0.50 and 0.60 are always exact steps, and the float
// handed to Kenshi is h / 100.0f, the same float as the literal 0.10f / 0.50f / 0.60f.
#pragma once
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <string>

namespace slowcore
{
    // ini text -> hundredths, rounded (0.5 -> 50, 0.499 -> 50, 0.123 -> 12); NaN or absurd values -> fallback
    inline int ToH(float v, int fallback)
    {
        if (!(v == v) || v < -1000.0f || v > 1000.0f) return fallback;
        return (int)floorf(v * 100.0f + 0.5f);
    }

    struct Range { int minH, maxH, stepH; };

    // the slider range from the ini: 0.01 .. 0.99, at least one step wide, step 0.01 .. the whole range
    inline Range MakeRange(int minH, int maxH, int stepH)
    {
        Range r;
        r.minH = minH < 1 ? 1 : (minH > 98 ? 98 : minH);
        r.maxH = maxH > 99 ? 99 : maxH;
        if (r.maxH <= r.minH) r.maxH = r.minH + 1;
        r.stepH = stepH < 1 ? 1 : stepH;
        if (r.stepH > r.maxH - r.minH) r.stepH = r.maxH - r.minH;
        return r;
    }
    // slider positions: min, min+step, ... and max itself as the last one even when the step doesn't divide the range
    inline int Positions(const Range& r) { const int n = (r.maxH - r.minH) / r.stepH; return (r.minH + n * r.stepH == r.maxH) ? n + 1 : n + 2; }
    inline int PosToH(const Range& r, int pos)
    {
        const int last = Positions(r) - 1;
        if (pos <= 0) return r.minH;
        if (pos >= last) return r.maxH;
        return r.minH + pos * r.stepH;
    }
    // any speed -> the nearest slider position (ties go up)
    inline int HToPos(const Range& r, int h)
    {
        const int last = Positions(r) - 1;
        if (h <= r.minH) return 0;
        if (h >= r.maxH) return last;
        int best = 0, bestD = 1 << 30;
        for (int p = 0; p <= last; ++p) { const int d = abs(PosToH(r, p) - h); if (d <= bestD) { bestD = d; best = p; } }
        return best;
    }
    // a speed from - / + or the ini, clamped to the range and snapped onto a slider position
    inline int Snap(const Range& r, int h) { return PosToH(r, HToPos(r, h)); }

    // "Shift+L", "ctrl+shift+F5", "Off" -> modifiers + virtual key ('A'..'Z', '0'..'9', F1..F24 = 0x70..). A bare key gets
    // Shift (a bare letter would fire while typing). Unknown keys -> 0 (no hotkey).
    inline void ParseKey(const char* s, int& ctrl, int& shift, int& alt, int& key, int defKey)
    {
        ctrl = shift = alt = 0; key = defKey;
        std::string t(s ? s : "");
        if (t.size() > 64) t.resize(64);
        for (size_t i = 0; i < t.size(); ++i) t[i] = (char)toupper((unsigned char)t[i]);
        size_t plus = t.find_last_of('+');
        const std::string mods = plus == std::string::npos ? std::string() : t.substr(0, plus);
        std::string k = plus == std::string::npos ? t : t.substr(plus + 1);
        while (!k.empty() && isspace((unsigned char)k[0])) k.erase(0, 1);
        while (!k.empty() && isspace((unsigned char)k[k.size() - 1])) k.erase(k.size() - 1);
        if (mods.find("CTRL") != std::string::npos) ctrl = 1;
        if (mods.find("SHIFT") != std::string::npos) shift = 1;
        if (mods.find("ALT") != std::string::npos) alt = 1;
        if (k.empty() || k == "0" || k == "OFF") key = 0;
        else if (k.size() == 1 && (isalpha((unsigned char)k[0]) || isdigit((unsigned char)k[0]))) key = k[0];
        else if (k.size() >= 2 && k.size() <= 3 && k[0] == 'F' && isdigit((unsigned char)k[1]))
        { const int n = atoi(k.c_str() + 1); key = (n >= 1 && n <= 24) ? 0x70 + n - 1 : 0; }
        else key = 0;
        if (key && !ctrl && !shift && !alt) shift = 1;
        if (!key) ctrl = shift = alt = 0;
    }

    // the speed to go back to when slow motion turns off: what Kenshi ran at before, unless that was paused or slow
    // 2026-11-01: GameWorld::isLoadingFromASaveGame only asks whether one of Kenshi's UI panels is visible, and that panel
    // also shows in normal play. So the panel is no proof of a load: when it goes away, slow motion stays on if Kenshi still
    // runs at our speed (or is paused: the speed-button check takes over once unpaused), and hands back otherwise.
    inline bool StillOurs(float cur, int setH) { return cur == cur && (cur <= 0.0f || (cur - setH / 100.0f <= 0.004f && setH / 100.0f - cur <= 0.004f)); }
    inline float ReturnSpeed(float before) { return (before == before && before >= 1.0f && before <= 20.0f) ? before : 1.0f; }

    // v0.2.3 (user 2026-11-01, for machinima with F7 on): hold the hotkey (Shift+L) and tap + or - to step the speed; the
    // hotkey alone toggles slow motion on RELEASE, and not at all if + / - was tapped while it was held.
    // Per frame: feed the key states; returns 0 nothing, 1 toggle, 2 step up, 3 step down.
    struct Chord { bool main, plus, minus, used; };
    inline int ChordStep(Chord& c, bool mainDown, bool plusDown, bool minusDown)
    {
        int act = 0;
        if (mainDown)
        {
            if (plusDown && !c.plus) { act = 2; c.used = true; }
            else if (minusDown && !c.minus) { act = 3; c.used = true; }
        }
        else if (c.main) { if (!c.used) act = 1; c.used = false; }   // released: toggle unless it was a speed chord
        if (!mainDown) c.used = false;
        c.main = mainDown; c.plus = plusDown; c.minus = minusDown;
        return act;
    }

    // ---- v1.1.0 kill cam (KillCam=1 in the ini, off by default; the user, 2026-11-19): when the selected character kills,
    // knocks out or severs a limb, time drops to KillCamSpeed for KillCamSeconds of real time, then comes back.
    // Same rules as PZA's SlowMo for Meitou 0.4.0 (KillCamCore.cs). The hit hooks read the victim just before and just after
    // the hit, so here every "before" is known.
    enum KcKind { KC_NONE = 0, KC_KILL = 1, KC_KO = 2, KC_LIMB = 3, KC_DODGE = 4 };
    struct KcSnap { bool valid, down, dead; int severed; };   // down = unconscious or dead (MedicalSystem::isUnconcious)
    // v1.1.1 (the user, 2026-11-19): each kind can be switched off: on = KC_ON_KILL | KC_ON_KO | KC_ON_LIMB (KillCamKills /
    // KillCamKnockouts / KillCamLimbs). A hit that did two things (a killing blow that also took an arm) counts for the kind that is on.
    enum { KC_ON_KILL = 1, KC_ON_KO = 2, KC_ON_LIMB = 4, KC_ON_ALL = 7 };
    inline int KcJudge(const KcSnap& before, const KcSnap& after, int on = KC_ON_ALL)
    {
        if (!before.valid || !after.valid || before.dead) return KC_NONE;   // unreadable, or hits on a corpse: never a moment
        if ((on & KC_ON_KILL) && after.dead) return KC_KILL;                // a downed victim finished off counts too
        if ((on & KC_ON_KO) && !before.down && after.down && !after.dead) return KC_KO;
        if ((on & KC_ON_LIMB) && after.severed > before.severed) return KC_LIMB;
        return KC_NONE;
    }
    // v1.1.2 (the user, 2026-11-19): whose hit makes a moment. A selected attacker always counts; a selected VICTIM (your own
    // character killed, knocked out or losing a limb) only with KillCamOwn=1. Hitting yourself never counts.
    inline bool KcCounts(bool attackerSelected, bool victimSelected, bool sameCharacter, bool ownOn) { return !sameCharacter && (attackerSelected || (ownOn && victimSelected)); }
    // v1.1.2: a dodge. Character::hitByMeleeAttack returns HIT_MISSED (0) for a blow its victim dodged (MeitouClient
    // docs/game/combat.md: HIT_MISSED dodged, HIT_SWORD blocked, flesh / metal hit); a selected victim with KillCamDodges=1 makes a moment.
    inline bool KcDodge(int hitMaterial, bool victimSelected, bool dodgesOn) { return dodgesOn && victimSelected && hitMaterial == 0; }
    // the ini values, clamped: speed 0.10 .. 0.60 in hundredths (NaN -> 0.25), length 250 .. 3000 ms (NaN -> 1000)
    inline int KcSpeedH(float v) { const int h = ToH(v, 25); return h < 10 ? 10 : (h > 60 ? 60 : h); }
    inline int KcMillis(float v) { if (!(v == v)) return 1000; const float ms = v * 1000.0f; return ms < 250.0f ? 250 : (ms > 3000.0f ? 3000 : (int)(ms + 0.5f)); }
    // a moment starts only when it would make the game slower than it runs now (slow motion at 0.20 is left alone);
    // curSpeed = what Kenshi reports, paused = Kenshi is paused (a pause stays a pause)
    inline bool KcStarts(float curSpeed, bool paused, int kcH) { return !paused && curSpeed == curSpeed && curSpeed > kcH / 100.0f + 0.004f; }
    // the end of a moment: 0 = keep it, 1 = over: put the speed back, 2 = the player changed the speed meanwhile: leave it
    inline int KcEnd(unsigned long long now, unsigned long long until, float curSpeed, bool paused, int kcH)
    {
        if (!paused && curSpeed == curSpeed && curSpeed > 0.0f && (curSpeed - kcH / 100.0f > 0.004f || kcH / 100.0f - curSpeed > 0.004f)) return 2;
        if (paused) return 0;   // the moment waits out a pause (it ends after the game runs again)
        return now >= until ? 1 : 0;
    }
    // a new trigger during a moment extends it, never shortens it
    inline unsigned long long KcExtend(unsigned long long until, unsigned long long now, int ms) { const unsigned long long e = now + (unsigned long long)(ms < 0 ? 0 : ms); return e > until ? e : until; }
}
