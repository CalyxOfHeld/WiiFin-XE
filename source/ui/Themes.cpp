/* Ui: themes of one's own, as text files in themes/ (see Ui.h).  A file
 * names its base theme and the colours it changes; the palette is the
 * base's with those applied, the main ones first (they set the shades
 * that go with them), then the palette's own names (exactly one). */
#include "Ui.h"
#include "../core/Lang.h"
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>

namespace Ui {

const ThemeColor THEME_COLORS[THEME_COLOR_COUNT] = {
    { "background",     N_("Background"),       N_("The screen behind everything") },
    { "card",           N_("Cards"),            N_("Buttons, tiles, panels and list rows") },
    { "border",         N_("Borders"),          N_("The outlines of cards, bars and text fields") },
    { "text",           N_("Text"),             N_("Titles and labels") },
    { "text_dim",       N_("Dim Text"),         N_("Details, descriptions and hints") },
    { "accent",         N_("Accent"),           N_("The ring around what is selected, and setting values") },
    { "highlight",      N_("Highlighted Text"), N_("The label of what is selected") },
    { "text_on_accent", N_("Text on Accent"),   N_("Words drawn on the accent colour") },
    { "bar",            N_("Bars"),             N_("The bar along the bottom of the screen") },
    { "field",          N_("Text Fields"),      N_("Where you type: search, the server address") },
};

/* the palette's own colours, one each */
static const struct { const char* key; u32 Palette::* m; } EXACT[] = {
    { "bg_top", &Palette::bgTop },           { "bg_bottom", &Palette::bgBottom },
    { "stripe", &Palette::stripe },          { "card_top", &Palette::cardTop },
    { "card_bottom", &Palette::cardBottom }, { "card_border", &Palette::cardBorder },
    { "accent_dark", &Palette::accentDark }, { "glow", &Palette::glow },
    { "shadow", &Palette::shadow },          { "bar_top", &Palette::barTop },
    { "bar_bottom", &Palette::barBottom },   { "bar_border", &Palette::barBorder },
    { "field_border", &Palette::fieldBorder }, { "danger", &Palette::danger },
    { "ok", &Palette::ok },                  { "dim", &Palette::dim },
};

static std::vector<CustomTheme> s_list;
static std::string              s_dir;   /* ends in '/' */

const std::vector<CustomTheme>& customThemes() { return s_list; }

static int mainIndex(const std::string& key)
{
    for (int i = 0; i < THEME_COLOR_COUNT; ++i) if (key == THEME_COLORS[i].key) return i;
    return -1;
}
static int exactIndex(const std::string& key)
{
    for (int i = 0; i < (int)(sizeof(EXACT) / sizeof(EXACT[0])); ++i) if (key == EXACT[i].key) return i;
    return -1;
}

static bool light(u32 c) { return ((c >> 24) * 299 + ((c >> 16) & 0xFF) * 587 + ((c >> 8) & 0xFF) * 114) / 1000 > 140; }
static u32  darker(u32 c, float k) { return mix(c, c & 0xFF, k); }   /* toward black, same alpha */

static void applyMain(Palette& p, int i, u32 c)
{
    switch (i) {
    case 0: p.bgTop = c;   p.bgBottom = darker(c, light(c) ? 0.10f : 0.50f); break;
    case 1: p.cardTop = c; p.cardBottom = darker(c, light(c) ? 0.06f : 0.24f); break;
    case 2: p.cardBorder = p.barBorder = p.fieldBorder = c; break;
    case 3: p.text = c; break;
    case 4: p.textDim = c; break;
    case 5: p.accent = c; p.glow = (c & 0xFFFFFF00) | 0x80; break;
    case 6: p.accentDark = c; break;
    case 7: p.textOnAccent = c; break;
    case 8: p.barTop = c;  p.barBottom = darker(c, light(c) ? 0.15f : 0.38f); break;
    case 9: p.field = c; break;
    }
}

u32 themeColor(const Palette& p, int i)
{
    const u32 v[THEME_COLOR_COUNT] = { p.bgTop, p.cardTop, p.cardBorder, p.text, p.textDim,
                                       p.accent, p.accentDark, p.textOnAccent, p.barTop, p.field };
    return i >= 0 && i < THEME_COLOR_COUNT ? v[i] : 0;
}

Palette buildPalette(const CustomTheme& t)
{
    Palette p = builtInPalette(t.base);
    for (int i = 0; i < THEME_COLOR_COUNT; ++i)
        for (const auto& kc : t.colors) if (kc.first == THEME_COLORS[i].key) applyMain(p, i, kc.second);
    for (const auto& kc : t.colors) {
        const int e = exactIndex(kc.first);
        if (e >= 0) p.*(EXACT[e].m) = kc.second;
    }
    return p;
}

void setThemeColor(CustomTheme& t, const std::string& key, u32 c)
{
    for (auto& kc : t.colors) if (kc.first == key) { kc.second = c; return; }
    t.colors.push_back({ key, c });
}

static std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
static std::string lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }

bool parseColor(const std::string& in, u32& out)
{
    std::string s = lower(trim(in));
    if (s.compare(0, 5, "rgba(") == 0) s = s.substr(5);
    else if (s.compare(0, 4, "rgb(") == 0) s = s.substr(4);
    if (!s.empty() && s.back() == ')') s.pop_back();
    if (s.find(',') != std::string::npos || s.find(' ') != std::string::npos) {
        /* r,g,b(,a) or r g b, each 0..255 */
        int v[4] = { 0, 0, 0, 255 }, n = 0;
        const char* p = s.c_str();
        while (*p && n < 4) {
            while (*p == ' ' || *p == ',') ++p;
            if (!*p) break;
            if (!isdigit((unsigned char)*p)) return false;
            char* end;
            const long x = strtol(p, &end, 10);
            if (x < 0 || x > 255) return false;
            v[n++] = (int)x;
            p = end;
        }
        while (*p == ' ' || *p == ',') ++p;
        if (n < 3 || *p) return false;
        out = (u32)v[0] << 24 | (u32)v[1] << 16 | (u32)v[2] << 8 | (u32)v[3];
        return true;
    }
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    else if (s.compare(0, 2, "0x") == 0) s = s.substr(2);
    for (char c : s) if (!isxdigit((unsigned char)c)) return false;
    if (s.size() == 3) s = std::string() + s[0] + s[0] + s[1] + s[1] + s[2] + s[2];
    if (s.size() == 6) s += "ff";
    if (s.size() != 8) return false;
    out = (u32)strtoul(s.c_str(), nullptr, 16);
    return true;
}

std::string colorHex(u32 c)
{
    char b[12];
    if ((c & 0xFF) == 0xFF) snprintf(b, sizeof(b), "#%06X", (unsigned)(c >> 8));
    else                    snprintf(b, sizeof(b), "#%08X", (unsigned)c);
    return b;
}

static bool readTheme(const std::string& path, CustomTheme& t)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == '#' || l[0] == ';') continue;
        const size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(l.substr(0, eq)));
        std::string val = trim(l.substr(eq + 1));
        /* a comment after the value: "#" after a space (a colour may start with #) */
        for (size_t i = 1; i < val.size(); ++i)
            if (val[i] == '#' && isspace((unsigned char)val[i - 1])) { val = trim(val.substr(0, i)); break; }
        if (key == "name") { t.name = val; continue; }
        if (key == "base") {
            for (int b = 0; b < THEME_COUNT; ++b)
                if (lower(val) == lower(themeName((Theme)b))) t.base = (Theme)b;
            continue;
        }
        u32 c;
        if ((mainIndex(key) >= 0 || exactIndex(key) >= 0) && parseColor(val, c)) setThemeColor(t, key, c);
    }
    fclose(f);
    return true;
}

void loadThemes(const std::string& dir)
{
    s_dir = dir;
    if (!s_dir.empty() && s_dir.back() != '/') s_dir += '/';
    const std::string inUse = customTheme() >= 0 ? s_list[customTheme()].file : std::string();
    s_list.clear();
    DIR* d = opendir(s_dir.c_str());
    if (d) {
        while (struct dirent* e = readdir(d)) {
            const std::string fn = e->d_name;
            if (fn.size() < 7 || lower(fn.substr(fn.size() - 6)) != ".theme") continue;
            CustomTheme t;
            t.file = fn;
            if (!readTheme(s_dir + fn, t)) continue;
            if (t.name.empty()) t.name = fn.substr(0, fn.size() - 6);
            s_list.push_back(t);
        }
        closedir(d);
    }
    std::sort(s_list.begin(), s_list.end(), [](const CustomTheme& a, const CustomTheme& b) {
        return lower(a.name) < lower(b.name);
    });
    SYS_Report("[Theme] %d of your own in %s\n", (int)s_list.size(), s_dir.c_str());
    setCustomTheme(inUse.empty() ? -1 : findCustomTheme(inUse));
}

int findCustomTheme(const std::string& file)
{
    for (int i = 0; i < (int)s_list.size(); ++i) if (lower(s_list[i].file) == lower(file)) return i;
    return -1;
}

/* the file a name makes: letters, digits, - and _ (spaces too, as _) */
static std::string fileFor(const std::string& name)
{
    std::string f;
    for (char c : name) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_') f += c;
        else if (c == ' ' && !f.empty() && f.back() != '_') f += '_';
        if (f.size() >= 40) break;
    }
    while (!f.empty() && f.back() == '_') f.pop_back();
    return (f.empty() ? std::string("Theme") : f) + ".theme";
}

int saveTheme(CustomTheme& t)
{
    if (s_dir.empty()) return -1;
    mkdir(s_dir.substr(0, s_dir.size() - 1).c_str(), 0777);   /* EEXIST is fine */
    t.file = fileFor(t.name);
    FILE* f = fopen((s_dir + t.file).c_str(), "w");
    if (!f) { SYS_Report("[Theme] can't write %s%s\n", s_dir.c_str(), t.file.c_str()); return -1; }
    fprintf(f, "# WiiFin-XE theme: sd:/apps/WiiFin/themes/\n");
    fprintf(f, "# Colours: #RRGGBB, or r,g,b (0-255). Any colour left out comes from the base theme.\n");
    fprintf(f, "name = %s\n", t.name.c_str());
    fprintf(f, "base = %s\n", themeName(t.base));
    for (int i = 0; i < THEME_COLOR_COUNT; ++i)
        for (const auto& kc : t.colors)
            if (kc.first == THEME_COLORS[i].key) fprintf(f, "%-14s = %s\n", kc.first.c_str(), colorHex(kc.second).c_str());
    for (const auto& kc : t.colors)
        if (exactIndex(kc.first) >= 0) fprintf(f, "%-14s = %s\n", kc.first.c_str(), colorHex(kc.second).c_str());
    fclose(f);
    SYS_Report("[Theme] saved %s\n", t.file.c_str());
    const std::string file = t.file;
    loadThemes(s_dir);
    return findCustomTheme(file);
}

bool removeTheme(const std::string& file)
{
    if (s_dir.empty() || file.empty() || file.find('/') != std::string::npos) return false;
    const std::string from = s_dir + file;
    struct stat st;
    if (stat(from.c_str(), &st) != 0) { loadThemes(s_dir); return true; }   /* gone already */
    const std::string bin = s_dir + "removed";
    mkdir(bin.c_str(), 0777);   /* EEXIST is fine */
    /* never over one removed before: Name.theme, Name-2.theme... */
    const std::string stem = file.substr(0, file.size() - 6);
    std::string to = bin + "/" + file;
    for (int n = 2; stat(to.c_str(), &st) == 0 && n < 100; ++n) {
        char suffix[16];
        snprintf(suffix, sizeof(suffix), "-%d.theme", n);
        to = bin + "/" + stem + suffix;
    }
    if (stat(to.c_str(), &st) == 0 || rename(from.c_str(), to.c_str()) != 0) {
        SYS_Report("[Theme] can't move %s to %s\n", from.c_str(), to.c_str());
        return false;
    }
    SYS_Report("[Theme] removed %s (now %s)\n", file.c_str(), to.c_str());
    loadThemes(s_dir);   /* in use: back to its base, a built-in */
    return true;
}

} // namespace Ui
