/* Resistance 2 PC port: console wording -> PC wording in the game's text.
 *
 * Every localised text file (IGHW, core_text_tags.*) is rewritten in memory
 * as it is loaded: "PS3 system", "PLAYSTATION(R)3", "PlayStation(R)Network",
 * "PlayStation(R)Store", "wireless controller", "SIXAXIS", HDD / power-off
 * warnings and "press and hold the PS button" become PC wording. The files on
 * disk are never touched. Strings are referenced by offset, so each one is
 * edited in place: a replacement is never longer than what it replaces
 * (PCT_TO_END rules replace the rest of the string and only apply when the
 * result fits), and the freed bytes become NULs. The pass is idempotent.
 *
 * Credits / trademark lines are left alone (PCT_SENTENCE rules skip strings
 * that look like a credit: no closing punctuation and a job-title word), and
 * so are PlayStation Move strings (PCT_NO_MOVE).
 *
 * R2_PC_TEXT=0 turns this off (default 1).
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

enum { PCT_TO_END = 1, PCT_SENTENCE = 2, PCT_NO_MOVE = 4 };

struct PcTextRule { const char* find; const char* repl; int flags; };

const PcTextRule k_rules[] = {
    { "shake the wireless controller or the left and right sticks", "shake the mouse or the left and right sticks", 0 },
    { "Shaking the wireless controller", "Shaking the mouse or the sticks", 0 },
    { "using the motion sensor function of the wireless controller", "by shaking the mouse or the left and right sticks", 0 },
    { " or a quick jab with the wireless controller", "", 0 },
    { "Wireless controller %d has been disconnected.", "Controller %d has been disconnected.", 0 },
    { "Press and hold the PS button to quit the application.", "Press F10 to quit the game.", 0 },
    { "Insufficient hard disk space.", "Insufficient disk space.", 0 },
    { "Your HDD has insufficient space.", "Insufficient disk space.", 0 },
    { "The HDD access indicator will blink during save operations.", "Do not quit the game or turn off your PC while this indicator is on screen.", PCT_TO_END },
    { "Please do not turn off your PLAYSTATION", "Please do not close the game during this time.", PCT_TO_END },
    { "sleep with a PS3 controller", "sleep with a controller", 0 },
    { "Prior to playing, see the WARNING section in the Safety and Support Manual", "Before playing, please read the health and safety warnings for this game.", PCT_TO_END },
    { "saving to the HDD. When you see this icon, please do not power off the system.", "saving. When you see this icon, do not quit the game or turn off your PC.", 0 },
    { "Your HDD is full.", "The disk is full.", 0 },
    { "language data on the HDD.", "language data on disk.", 0 },
    { "with the PlayStation\xC2\xAENetwork", "with the online service", 0 },
    { "with the PlayStation\xC2\xAE Network", "with the online service", 0 },
    { "signed out of the PlayStation\xC2\xAENetwork", "signed out of the online service", 0 },
    { "signed out of the PlayStation\xC2\xAE Network", "signed out of the online service", 0 },
    { "your PlayStation\xC2\xAENetwork account", "your online account", 0 },
    { "Your PlayStation\xC2\xAENetwork account", "Your online account", 0 },
    { "your PlayStation\xC2\xAE Network account", "your online account", 0 },
    { "Your PlayStation\xC2\xAE Network account", "Your online account", 0 },
    { "USB connector on the PS3\xE2\x84\xA2 system", "USB port on your PC", 0 },
    { "from the Settings > Display Settings menu in the XrossMediaBar.", "in your PC's display settings.", PCT_TO_END },
    { "the Settings > Display Settings menu in the XrossMediaBar and", "your PC's display settings and", 0 },
    { "your Playstation 3.", "your PC.", 0 },
    { "your PlayStation 3.", "your PC.", 0 },
    { "PSN Friends", "Friends", 0 },
    { "PLAYSTATION\xC2\xAE" "3", "PC", 0 },
    { "PlayStation\xC2\xAE" "3", "PC", 0 },
    { "PLAYSTATION(R) 3", "PC", 0 },
    { "PLAYSTATION(R)3", "PC", 0 },
    { "PlayStation(R)3", "PC", 0 },
    { "PS3\xE2\x84\xA2", "PC", 0 },
    { "Playstation 3", "PC", 0 },
    { "PlayStation 3", "PC", 0 },
    { "PS3", "PC", 0 },
    { "PlayStation\xC2\xAENetwork", "Online", PCT_SENTENCE },
    { "PlayStation\xC2\xAE Network", "Online", PCT_SENTENCE },
    { "PLAYSTATION\xC2\xAENetwork", "Online", PCT_SENTENCE },
    { "PLAYSTATION(R)Network", "Online", PCT_SENTENCE },
    { "PlayStation(R)Network", "Online", PCT_SENTENCE },
    { "PlayStation@Network", "Online", PCT_SENTENCE },
    { "PLAYSTATION\xC2\xAE Network", "Online", PCT_SENTENCE },
    { "PLAYSTATION(R) Network", "Online", PCT_SENTENCE },
    { "PlayStation(R) Network", "Online", PCT_SENTENCE },
    { "PlayStation\xC2\xAEStore", "Store", PCT_SENTENCE },
    { "PLAYSTATION\xC2\xAEStore", "Store", PCT_SENTENCE },
    { "PlayStation(R)Store", "Store", PCT_SENTENCE },
    { "PLAYSTATION(R)Store", "Store", PCT_SENTENCE },
    { " de Playstation", "", 0 },
    { "SIXAXIS\xE2\x84\xA2-", "", 0 },
    { "SIXAXIS\xE2\x84\xA2 ", "", 0 },
    { " di PSN", "", 0 },
    { " p\xC3\xA5 PSN", "", 0 },
    { "PSN\xE3\x83\x95\xE3\x83\xAC\xE3\x83\xB3\xE3\x83\x89", "\xE3\x83\x95\xE3\x83\xAC\xE3\x83\xB3\xE3\x83\x89", 0 },
    { "PSN-", "", 0 },
    { "PSN ", "", 0 },
    { " PSN", "", 0 },
    { "Zum Verlassen der Anwendung PS-Taste dr", "Zum Verlassen der Anwendung F10 dr", PCT_NO_MOVE },
    { "die PS-Taste", "F10", PCT_NO_MOVE },
    { "PS-Taste", "F10", PCT_NO_MOVE },
    { "sur la touche PS", "sur F10", PCT_NO_MOVE },
    { "la touche PS", "F10", PCT_NO_MOVE },
    { "il tasto PS", "F10", PCT_NO_MOVE },
    { "el bot\xC3\xB3n PS", "F10", PCT_NO_MOVE },
    { "o bot\xC3\xA3o PS", "F10", PCT_NO_MOVE },
    { "de PS-toets", "F10", PCT_NO_MOVE },
    { "PS-knappen", "F10", PCT_NO_MOVE },
    { "PS-n\xC3\xA4pp\xC3\xA4int\xC3\xA4", "F10:t\xC3\xA4", PCT_NO_MOVE },
    { "PS-n\xC3\xA4pp\xC3\xA4imen", "F10:n", PCT_NO_MOVE },
    { "PS-n\xC3\xA4pp\xC3\xA4in", "F10", PCT_NO_MOVE },
    { "\xD0\xBA\xD0\xBD\xD0\xBE\xD0\xBF\xD0\xBA\xD1\x83 PS", "F10", PCT_NO_MOVE },
    { "PS\xE3\x83\x9C\xE3\x82\xBF\xE3\x83\xB3", "F10\xE3\x82\xAD\xE3\x83\xBC", PCT_NO_MOVE },
    { "PS \xE3\x83\x9C\xE3\x82\xBF\xE3\x83\xB3", "F10\xE3\x82\xAD\xE3\x83\xBC", PCT_NO_MOVE },
    { "PS \xEB\xB2\x84\xED\x8A\xBC\xEC\x9C\xBC\xEB\xA1\x9C", "F10 \xED\x82\xA4\xEB\xA1\x9C", PCT_NO_MOVE },
    { "PS \xEB\xB2\x84\xED\x8A\xBC\xEC\x9D\x84", "F10 \xED\x82\xA4\xEB\xA5\xBC", PCT_NO_MOVE },
    { "PS \xEB\xB2\x84\xED\x8A\xBC", "F10 \xED\x82\xA4", PCT_NO_MOVE },
    { "PS\xEB\xB2\x84\xED\x8A\xBC", "F10 \xED\x82\xA4", PCT_NO_MOVE },
};

const char* const k_credit_words[] = {
    "Manager", "Director", "Specialist", "Artist", "Team", "Group", "Engineer",
    "Producer", "Coordinator", "Operation",
};

int find_in(const uint8_t* s, uint32_t len, const char* w, size_t wl, uint32_t from)
{
    if (wl == 0 || wl > len) return -1;
    for (uint32_t k = from; k + wl <= len; k++)
        if (s[k] == (uint8_t)w[0] && !memcmp(s + k, w, wl)) return (int)k;
    return -1;
}

bool ends_with(const uint8_t* s, uint32_t len, const char* w)
{
    const size_t wl = strlen(w);
    return len >= wl && !memcmp(s + len - wl, w, wl);
}

/* A credits line ("Senior Director, PlayStation(R)Network") or a bare name. */
bool credit_like(const uint8_t* s, uint32_t len)
{
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == '\t')) len--;
    if (!len) return true;
    const uint8_t c = s[len - 1];
    if (c == '.' || c == '!' || c == '?' || ends_with(s, len, "\xE3\x80\x82") || ends_with(s, len, "\xEF\xBC\x81"))
        return false;
    for (const char* w : k_credit_words)
        if (find_in(s, len, w, strlen(w), 0) >= 0) return true;
    return false;
}

/* Tag names (LOBBY_CAPTION_REMOVECABLEFROMPS3 ...) are keys, not text. */
bool is_ident(const uint8_t* s, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        const uint8_t c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '.' || c == '/' || c == '-')) return false;
    }
    return true;
}

/* Rewrite one NUL-terminated string of m bytes in place; true if it changed. */
bool rewrite(uint8_t* s, uint32_t m)
{
    if (is_ident(s, m)) return false;
    uint32_t len = m;
    bool changed = false;
    for (const PcTextRule& r : k_rules) {
        const size_t fl = strlen(r.find), rl = strlen(r.repl);
        int k = find_in(s, len, r.find, fl, 0);
        if (k < 0) continue;
        if ((r.flags & PCT_SENTENCE) && credit_like(s, len)) continue;
        if ((r.flags & PCT_NO_MOVE) && find_in(s, len, "Move", 4, 0) >= 0) continue;
        while (k >= 0) {
            if (r.flags & PCT_TO_END) {
                if ((uint32_t)k + rl <= len) { memcpy(s + k, r.repl, rl); len = (uint32_t)(k + rl); changed = true; }
                break;
            }
            memmove(s + k + rl, s + k + fl, len - k - fl);
            memcpy(s + k, r.repl, rl);
            len -= (uint32_t)(fl - rl);
            changed = true;
            k = find_in(s, len, r.find, fl, (uint32_t)(k + rl));
        }
    }
    if (changed) memset(s + len, 0, m - len);
    return changed;
}

} // namespace

/* p = the IGHW text file in guest memory, end = its size. Returns the number
 * of strings changed. */
extern "C" int port_pc_text(uint8_t* p, uint32_t end)
{
    static int en = -1;
    if (en < 0) {
        const char* e = getenv("R2_PC_TEXT");
        en = e ? atoi(e) : 1;
    }
    if (!en || !p) return 0;
    int n = 0;
    for (uint32_t i = 1; i < end; i++) {
        if (p[i - 1] != 0 || p[i] == 0) continue;
        uint32_t m = 0; while (i + m < end && p[i + m]) m++;
        if (i + m >= end) break;
        if (rewrite(p + i, m)) n++;
        i += m;
    }
    if (n) { static int said = 0; if (said++ < 4) fprintf(stderr, "[prompts] text file: %d console wording(s) -> PC wording\n", n); }
    return n;
}
