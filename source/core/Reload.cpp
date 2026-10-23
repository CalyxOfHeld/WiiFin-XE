/* Reload.cpp — WiiFin started again from its own DOL (the HOME menu's Reset
 * when a loader such as the Homebrew Channel launched it: no channel to
 * launch again, and exit() only goes back to the loader).
 *
 * The DOL is read whole into the heap (above the program's sections, which
 * the new one, the same file, takes again), then a small routine copied
 * there too, out of the way, puts each section where it goes, clears the
 * BSS, flushes the caches, hands the DOL its path (libogc's "_arg" block
 * by its entry: argv[0], for its folder) and jumps to its entry.  It
 * works in registers only: the stack is in the BSS it clears. */
#include "Reload.h"
#include <gccore.h>
#include <ogc/machine/processor.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

/* r3: the DOL in memory; r4: the six words of its "_arg" block, or 0 */
extern "C" void reloadTrampoline(const void* dol, const void* argv);
extern "C" char reloadTrampolineEnd[];

asm(R"(
    .section .text
    .balign 32
    .globl reloadTrampoline
    .globl reloadTrampolineEnd
reloadTrampoline:
    mfmsr   5
    rlwinm  5, 5, 0, 17, 15         # interrupts off
    mtmsr   5
    li      6, 0                    # the 18 sections (7 text, 11 data)
1:  lwzx    7, 3, 6                 # its offset in the file
    addi    8, 6, 0x48
    lwzx    8, 3, 8                 # its address
    addi    9, 6, 0x90
    lwzx    9, 3, 9                 # its size
    cmpwi   9, 0
    beq     4f
    add     7, 7, 3
    mr      10, 8
    mr      11, 9
2:  lwz     12, 0(7)
    stw     12, 0(10)
    addi    7, 7, 4
    addi    10, 10, 4
    addic.  11, 11, -4
    bgt     2b
    mr      10, 8
    add     11, 8, 9
3:  dcbf    0, 10                   # to memory, out of the instruction cache
    icbi    0, 10
    addi    10, 10, 32
    cmplw   10, 11
    blt     3b
4:  addi    6, 6, 4
    cmpwi   6, 72
    blt     1b
    lwz     8, 0xD8(3)              # the BSS, cleared
    lwz     9, 0xDC(3)
    cmpwi   9, 0
    beq     6f
    li      12, 0
    mr      10, 8
    mr      11, 9
5:  stw     12, 0(10)
    addi    10, 10, 4
    addic.  11, 11, -4
    bgt     5b
    mr      10, 8
    add     11, 8, 9
55: dcbf    0, 10
    addi    10, 10, 32
    cmplw   10, 11
    blt     55b
6:  sync
    isync
    lwz     8, 0xE0(3)              # its entry
    cmpwi   4, 0
    beq     8f
    lis     10, 0x5f61              # "_arg" after its first branch (or its
    ori     10, 10, 0x7267          # second: WiiFin's crt0_pre), its block after
    addi    5, 8, 4
    lwz     9, 0(5)
    cmpw    9, 10
    beq     65f
    addi    5, 8, 8
    lwz     9, 0(5)
    cmpw    9, 10
    bne     8f
65: addi    5, 5, 4
    li      6, 0
7:  lwzx    9, 4, 6
    stwx    9, 5, 6
    addi    6, 6, 4
    cmpwi   6, 24
    blt     7b
    dcbf    0, 5
    addi    10, 5, 24
    dcbf    0, 10
    sync
    icbi    0, 5
    isync
8:  mtctr   8
    bctr
reloadTrampolineEnd:
    .previous
)");

namespace {
/* argv[0], where the new program finds it: high in MEM2, out of reach of its
 * first allocations */
char* const ARG_AREA = (char*)0x93300000;
/* at the jump, the DOL and the routine moved here, in MEM2: out of the way
 * of the sections of any program (another one's may cover the heap the
 * DOL was read into) */
u8*   const STAGE_AREA = (u8*)0x92000000;
const long  STAGE_MAX  = 0x1200000;               /* 18 MB: up to 0x93200000 */
u8*   const CODE_AREA  = (u8*)0x93280000;
}

bool Reload::launch(const char* path, const std::vector<std::string>& args)
{
    if (!path || !path[0]) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0x100 || size > STAGE_MAX) { fclose(f); return false; }
    u8* dol = (u8*)memalign(32, (size + 31) & ~31);
    if (!dol) { fclose(f); return false; }
    const bool read = fread(dol, 1, size, f) == (size_t)size;
    fclose(f);
    const u32 entry = *(u32*)(dol + 0xE0);
    if (!read || (entry & 0xF0000000) != 0x80000000) { free(dol); return false; }

    /* its "_arg" block: magic, command line (argv[0], then the arguments,
     * each ended by a 0), its length, argc, argv (0, 0: libogc splits the
     * line), end */
    std::string line(path);
    line += '\0';
    for (const std::string& a : args) { line += a; line += '\0'; }
    if (line.size() > 0x10000) { free(dol); return false; }
    memcpy(ARG_AREA + 64, line.data(), line.size());
    u32* arg = (u32*)ARG_AREA;
    arg[0] = 0x5f617267;
    arg[1] = (u32)(ARG_AREA + 64);
    arg[2] = (u32)line.size();
    arg[3] = 0;
    arg[4] = 0;
    arg[5] = 0;
    DCFlushRange(ARG_AREA, 64 + line.size());

    const size_t len = (size_t)(reloadTrampolineEnd - (char*)reloadTrampoline);
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    u32 level;
    _CPU_ISR_Disable(level);
    (void)level;
    /* nothing runs any more but this: the DOL and the routine to MEM2 */
    memmove(STAGE_AREA, dol, size);
    DCFlushRange(STAGE_AREA, (size + 31) & ~31);
    memcpy(CODE_AREA, (const void*)reloadTrampoline, len);
    DCFlushRange(CODE_AREA, (len + 31) & ~31);
    ICInvalidateRange(CODE_AREA, (len + 31) & ~31);
    ((void (*)(const void*, const void*))(void*)CODE_AREA)(STAGE_AREA, arg);
    return false;   /* never back */
}

bool Reload::self(const char* path)
{
    return launch(path, {});
}

/* the <arg> lines of the app's meta.xml (the Homebrew Channel passes them;
 * started from here it would not), the commented-out ones left out */
std::vector<std::string> Reload::metaArgs(const char* metaPath)
{
    std::vector<std::string> out;
    FILE* f = fopen(metaPath, "rb");
    if (!f) return out;
    std::string xml;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && xml.size() < 65536) xml.append(buf, n);
    fclose(f);
    for (size_t c; (c = xml.find("<!--")) != std::string::npos; ) {
        const size_t e = xml.find("-->", c);
        xml.erase(c, e == std::string::npos ? std::string::npos : e + 3 - c);
    }
    for (size_t a = 0; (a = xml.find("<arg>", a)) != std::string::npos; ) {
        a += 5;
        const size_t e = xml.find("</arg>", a);
        if (e == std::string::npos) break;
        std::string v = xml.substr(a, e - a);
        const size_t b = v.find_first_not_of(" \t\r\n"), z = v.find_last_not_of(" \t\r\n");
        if (b != std::string::npos) out.push_back(v.substr(b, z - b + 1));
        a = e + 6;
    }
    return out;
}
