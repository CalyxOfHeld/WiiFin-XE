#include "Avatars.h"
#include "Ui.h"
#include "JpegTexture.h"
#include <grrlib.h>
#include <ogc/isfs.h>
#include <ogc/system.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <map>
#include <algorithm>

namespace {

std::string s_dir;                    /* pictures/, with its '/' */
std::string s_current;
std::vector<Avatars::Mii> s_miis;
bool s_miisRead = false;

/* the pictures loaded, by name (nullptr: it would not load: not tried again) */
std::map<std::string, GRRLIB_texImg*> s_tex;

/* ---- the Mii database ----------------------------------------------------
 * /shared2/menu/FaceLib/RFL_DB.dat: "RNOD", then 100 Miis of 74 bytes,
 * big-endian (WiiBrew, Mii data) */
const char* DB_PATH = "/shared2/menu/FaceLib/RFL_DB.dat";
const int MII_SIZE = 74, MII_SLOTS = 100;

u16 be16(const u8* p) { return (u16)(p[0] << 8 | p[1]); }
/* bits [hi..lo] (15 = the top one) of the 16 at off */
int bits(const u8* m, int off, int hi, int lo) { return (be16(m + off) >> lo) & ((1 << (hi - lo + 1)) - 1); }

std::string utf16Name(const u8* p, int chars)
{
    std::string s;
    for (int i = 0; i < chars; ++i) {
        const u16 c = be16(p + i * 2);
        if (!c) break;
        if (c < 0x80) s += (char)c;
        else if (c < 0x800) { s += (char)(0xC0 | c >> 6); s += (char)(0x80 | (c & 0x3F)); }
        else { s += (char)(0xE0 | c >> 12); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
    }
    return s;
}

void readMiis()
{
    s_miisRead = true;
    const u32 LEN = (4 + MII_SIZE * MII_SLOTS + 31) & ~31u;
    u8* buf = (u8*)memalign(32, LEN);
    if (!buf) return;
    ISFS_Initialize();
    const s32 fd = ISFS_Open(DB_PATH, ISFS_OPEN_READ);
    s32 got = fd >= 0 ? ISFS_Read(fd, buf, LEN) : fd;
    if (fd >= 0) ISFS_Close(fd);
    if (got < 4 + MII_SIZE || memcmp(buf, "RNOD", 4) != 0) {
        SYS_Report("[Avatars] Mii database: none (%d)\n", (int)got);
        free(buf);
        return;
    }
    for (int i = 0; i < MII_SLOTS && 4 + (i + 1) * MII_SIZE <= got; ++i) {
        const u8* m = buf + 4 + i * MII_SIZE;
        if (!be16(m + 2) || (m[0] & 0x80)) continue;   /* an empty slot, or marked invalid */
        Avatars::Mii mii;
        char id[24];
        snprintf(id, sizeof id, "mii:%02X%02X%02X%02X", m[0x18], m[0x19], m[0x1A], m[0x1B]);
        mii.id = id;
        mii.name = utf16Name(m + 2, 10);
        memcpy(mii.raw, m, MII_SIZE);
        s_miis.push_back(mii);
    }
    SYS_Report("[Avatars] %d Miis\n", (int)s_miis.size());
    free(buf);
}

const Avatars::Mii* findMii(const std::string& id)
{
    for (const Avatars::Mii& m : Avatars::miis()) if (m.id == id) return &m;
    return nullptr;
}

/* ---- a Mii drawn ---------------------------------------------------------
 * Its features from the data, as plain shapes: not the Mii Channel's
 * rendering (that needs Nintendo's face library), a likeness at a glance. */
const u32 FAV[12]  = { 0xD21E14FF, 0xFF6E19FF, 0xFFD820FF, 0x78D220FF, 0x00782FFF, 0x0A48B4FF,
                       0x3CAADEFF, 0xF55A7DFF, 0x7328ADFF, 0x483818FF, 0xE0E0E0FF, 0x181814FF };
const u32 SKIN[6]  = { 0xFFD3ADFF, 0xFFB663FF, 0xDE7942FF, 0xFFAA8CFF, 0xAD5129FF, 0x632C18FF };
const u32 HAIR[8]  = { 0x1E1A18FF, 0x382018FF, 0x551E12FF, 0x703C21FF, 0x727272FF, 0x4A3A1EFF,
                       0x885A2CFF, 0xD0A04BFF };
const u32 EYE[6]   = { 0x000000FF, 0x6C7070FF, 0x663C2CFF, 0x605E30FF, 0x4664B4FF, 0x387C5AFF };
const u32 LIPS[3]  = { 0xD8523EFF, 0xF0583AFF, 0xE07A82FF };
const u32 GLASS[6] = { 0x141414FF, 0x6C3A1EFF, 0xB42828FF, 0x2848A0FF, 0xC8A020FF, 0x787878FF };
/* the faces' widths and jaws, by shape */
const float FACE_W[8] = { 0.98f, 0.92f, 1.04f, 0.88f, 1.0f, 1.08f, 0.94f, 1.0f };
const float FACE_R[8] = { 0.48f, 0.30f, 0.42f, 0.36f, 0.24f, 0.40f, 0.46f, 0.34f };

u32 shade(u32 c, float k) { return Ui::mix(c, 0x000000FF, k); }

void drawMii(const u8* m, float cx, float cy, float r)
{
    const int fav = bits(m, 0x00, 4, 1) % 12;
    const int face = bits(m, 0x20, 15, 13), skin = bits(m, 0x20, 12, 10) % 6;
    const int hairType = bits(m, 0x22, 15, 9), hairCol = bits(m, 0x22, 8, 6), hairPart = bits(m, 0x22, 5, 5);
    const int browCol = bits(m, 0x26, 15, 13), browVert = bits(m, 0x26, 8, 4);
    const int eyeVert = bits(m, 0x28, 4, 0), eyeCol = bits(m, 0x2A, 15, 13) % 6;
    const int eyeSize = bits(m, 0x2A, 11, 9), eyeSpace = bits(m, 0x2A, 8, 5);
    const int noseVert = bits(m, 0x2C, 7, 3);
    const int lipCol = bits(m, 0x2E, 10, 9) % 3, lipSize = bits(m, 0x2E, 8, 5), lipVert = bits(m, 0x2E, 4, 0);
    const int glasses = bits(m, 0x30, 15, 12), glassCol = bits(m, 0x30, 11, 9) % 6;
    const int mustache = bits(m, 0x32, 15, 14), beard = bits(m, 0x32, 13, 12), fhCol = bits(m, 0x32, 11, 9);

    /* the disc: the favourite colour */
    Ui::circle(cx, cy, r, FAV[fav]);
    /* the head, a little low in the disc */
    const float W = r * 1.12f * FACE_W[face], H = r * 1.30f;
    const float hx = cx - W / 2, hy = cy - H / 2 + r * 0.16f;
    const u32 skinC = SKIN[skin], hairC = HAIR[hairCol & 7];
    (void)hairType;   /* one shape for every style */
    /* the hair behind: its sides */
        Ui::roundRect(hx - W * 0.08f, hy - H * 0.06f, W * 1.16f, H * 0.70f, W * 0.5f, hairC);
    Ui::roundRect(hx, hy, W, H, W * FACE_R[face], skinC);
    {   /* the top, and a fringe swept to its parting */
        Ui::roundRect(hx - W * 0.06f, hy - H * 0.10f, W * 1.12f, H * 0.30f, H * 0.15f, hairC);
        const float px = hairPart ? hx + W * 0.70f : hx + W * 0.30f;
        Ui::triangle(hx, hy + H * 0.12f, px, hy + H * 0.06f, hairPart ? hx : hx + W, hy + H * 0.24f, hairC);
    }
    /* the eyes: their height (0..18), spacing (0..12), size (0..7) */
    const float ey = hy + H * (0.22f + eyeVert * 0.016f);
    const float ex = W * (0.15f + eyeSpace * 0.012f);
    const float ew = r * (0.12f + eyeSize * 0.012f), eh = ew * 1.25f;
    const float by = hy + H * (0.14f + browVert * 0.014f);
    for (int s = -1; s <= 1; s += 2) {
        const float x = cx + s * ex;
        Ui::roundRect(x - ew / 2, ey - eh / 2, ew, eh, ew / 2, 0xFFFFFFFF);
        Ui::circle(x, ey + eh * 0.08f, ew * 0.36f, EYE[eyeCol]);
        Ui::roundRect(x - ew * 0.65f, by - r * 0.03f, ew * 1.3f, r * 0.06f, r * 0.03f, HAIR[browCol & 7]);
        if (glasses)
            Ui::roundBorder(x - ew * 1.0f, ey - eh * 0.85f, ew * 2.0f, eh * 1.7f, ew * 0.5f,
                            r > 30 ? 2.0f : 1.2f, GLASS[glassCol]);
    }
    /* the nose and the mouth */
    const float ny = hy + H * (0.40f + noseVert * 0.014f);
    Ui::roundRect(cx - r * 0.05f, ny - r * 0.04f, r * 0.10f, r * 0.08f, r * 0.04f, shade(skinC, 0.18f));
    const float my = hy + H * (0.52f + lipVert * 0.014f);
    const float mw = r * (0.22f + lipSize * 0.02f);
    if (beard)
        Ui::roundRect(hx + W * 0.08f, my - r * 0.10f, W * 0.84f, hy + H - my + r * 0.10f, W * 0.36f, HAIR[fhCol & 7]);
    if (mustache)
        Ui::roundRect(cx - mw * 0.65f, my - r * 0.11f, mw * 1.3f, r * 0.08f, r * 0.04f, HAIR[fhCol & 7]);
    Ui::roundRect(cx - mw / 2, my - r * 0.03f, mw, r * 0.07f, r * 0.035f, LIPS[lipCol]);
}

GRRLIB_texImg* loadPicture(const std::string& name)
{
    auto it = s_tex.find(name);
    if (it != s_tex.end()) return it->second;
    GRRLIB_texImg* t = nullptr;
    FILE* f = fopen((s_dir + name).c_str(), "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        /* a picture for a disc 40 px across: a big photo is refused */
        u8* data = n > 0 && n < 3 * 1024 * 1024 ? (u8*)memalign(32, n) : nullptr;
        if (data && fread(data, 1, n, f) == (size_t)n) {
            if (n > 3 && data[0] == 0xFF && data[1] == 0xD8) t = loadJPEGTexture(data, (u32)n);
            else                                            t = GRRLIB_LoadTexture(data);
        }
        free(data);
        fclose(f);
    }
    if (t && (t->w > 1024 || t->h > 1024)) { GRRLIB_FreeTexture(t); t = nullptr; }   /* more than GX draws */
    SYS_Report("[Avatars] %s: %s\n", name.c_str(), t ? "loaded" : "not loaded");
    s_tex[name] = t;
    return t;
}

bool picture(const std::string& name)
{
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos || name[0] == '.') return false;
    const char* e = name.c_str() + dot + 1;
    return !strcasecmp(e, "png") || !strcasecmp(e, "jpg") || !strcasecmp(e, "jpeg");
}

} // namespace

void Avatars::setDir(const std::string& appDir) { s_dir = appDir + "pictures/"; }

const std::vector<Avatars::Mii>& Avatars::miis()
{
    if (!s_miisRead) readMiis();
    return s_miis;
}

std::vector<std::string> Avatars::files()
{
    std::vector<std::string> out;
    DIR* d = s_dir.empty() ? nullptr : opendir(s_dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d))
        if (picture(e->d_name)) out.push_back(std::string("file:") + e->d_name);
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

void Avatars::setCurrent(const std::string& pic) { s_current = pic; }
const std::string& Avatars::current() { return s_current; }

std::string Avatars::label(const std::string& pic)
{
    if (pic.compare(0, 4, "mii:") == 0) {
        const Mii* m = findMii(pic);
        return m ? m->name : "Mii";
    }
    if (pic.compare(0, 5, "file:") == 0) {
        std::string n = pic.substr(5);
        const size_t dot = n.rfind('.');
        return dot == std::string::npos ? n : n.substr(0, dot);
    }
    return "";
}

bool Avatars::draw(const std::string& pic, float cx, float cy, float r)
{
    if (pic.compare(0, 4, "mii:") == 0) {
        const Mii* m = findMii(pic);
        if (!m) return false;
        drawMii(m->raw, cx, cy, r);
        return true;
    }
    if (pic.compare(0, 5, "file:") == 0) {
        GRRLIB_texImg* t = loadPicture(pic.substr(5));
        if (!t) return false;
        Ui::texCover(t, cx - r, cy - r, r * 2, r * 2, r, 1.0f);
        return true;
    }
    return false;
}
