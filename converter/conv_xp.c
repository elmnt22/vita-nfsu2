/* conv_xp.c -- the Prep Tool's World Expansion step (was the separate NFSU2
 * Expansion Prep Tool, merged 11-19). Gets a player's Need for Speed Most
 * Wanted (2005) and Carbon PC files onto the Vita, to drive in the port as
 * Rockport and Palmont:
 *  - checks the files the expansion reads (xp_manifest.inc: names, sizes, CRC32s;
 *    tools/gen_xp_manifest.py)
 *  - lists them for ug2conv.c to send with the rest, into
 *    ux0:data/NFSU2/expansion/<mw|carbon>/, the folders the game reads.
 * Nothing is converted any more (11-25): the game streams both cities from these
 * files (rstream.c) and prepares the rest itself on the first trip (xpcity.c),
 * so copying them by hand does the same. The game turns the expansion on when it
 * finds them (Settings can hide it again). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world_tiles.h"
#include "xpconv.h"
#include "conv_xp.h"


typedef struct { int game; const char *name, *game_name, *bun, *companion, *reg; int supported; } City;
static const City k_city[] = {
    { XP_GAME_MW,     "Rockport", "Need for Speed Most Wanted", "STREAML2RA", "L2RA",
      "Need for Speed Most Wanted", 1 },
    { XP_GAME_CARBON, "Palmont",  "Need for Speed Carbon",      "STREAML5RA", "L5RA",
      "Need for Speed Carbon", 1 },
};
#define NCITY XPP_NCITY
#define VITA_DATA   "ux0:/data/NFSU2"

static char g_dir[NCITY][MAX_PATH];   /* each game's folder ("" = not used) */
static char g_out[MAX_PATH];          /* the Prep Tool's output: cache\ + stage\TRACKS\ */

static int file_exists(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static long long file_size(const char *p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(p, GetFileExInfoStandard, &d)) return -1;
    return ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

const char *xpp_city_name(int c) { return k_city[c].name; }
const char *xpp_game_name(int c) { return k_city[c].game_name; }
const char *xpp_bun(int c) { return k_city[c].bun; }
int xpp_city_game(int c) { return k_city[c].game; }
void xpp_set_out(const char *out) { snprintf(g_out, sizeof g_out, "%s", out); }
void xpp_set_dir(int c, const char *dir) { snprintf(g_dir[c], sizeof g_dir[c], "%s", dir ? dir : ""); }
int xpp_any_dir(void) { for (int c = 0; c < NCITY; c++) if (g_dir[c][0]) return 1; return 0; }

/* A game folder holds TRACKS\<stream>.BUN; picking TRACKS itself works too. */
int xpp_dir_fix(int c, char *dir) {
    size_t n = strlen(dir);
    while (n > 3 && (dir[n - 1] == '\\' || dir[n - 1] == '/')) dir[--n] = 0;
    char p[MAX_PATH + 64];
    snprintf(p, sizeof p, "%s\\TRACKS\\%s.BUN", dir, k_city[c].bun);
    if (file_exists(p)) return 1;
    snprintf(p, sizeof p, "%s\\%s.BUN", dir, k_city[c].bun);
    if (file_exists(p)) {
        char *s = strrchr(dir, '\\'); if (!s) s = strrchr(dir, '/');
        if (s) { *s = 0; return 1; }
    }
    return 0;
}
int xpp_dir_detect(int c, char *out, size_t cap) {
    char k1[160], k2[160];
    snprintf(k1, sizeof k1, "SOFTWARE\\WOW6432Node\\EA GAMES\\%s", k_city[c].reg);
    snprintf(k2, sizeof k2, "SOFTWARE\\EA GAMES\\%s", k_city[c].reg);
    const char *keys[] = { k1, k2 };
    for (int i = 0; i < 2; i++) {
        DWORD n = (DWORD)cap;
        if (RegGetValueA(HKEY_LOCAL_MACHINE, keys[i], "Install Dir", RRF_RT_REG_SZ, NULL, out, &n) == ERROR_SUCCESS &&
            xpp_dir_fix(c, out)) {
            char l[MAX_PATH]; DWORD k = GetLongPathNameA(out, l, sizeof l);
            if (k && k < sizeof l) snprintf(out, cap, "%s", l);
            return 1;
        }
    }
    out[0] = 0;
    return 0;
}

/* ---- the file check -------------------------------------------------------- */
typedef struct { int game; const char *rel; long long size; unsigned crc; } GameFile;
static const GameFile k_files[] = {
#include "xp_manifest.inc"
};
#define NFILES ((int)(sizeof k_files / sizeof *k_files))
static unsigned g_crctab[256];
static unsigned crc32_add(unsigned c, const unsigned char *p, DWORD n) {
    if (!g_crctab[1])
        for (unsigned i = 0; i < 256; i++) {
            unsigned k = i;
            for (int j = 0; j < 8; j++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1;
            g_crctab[i] = k;
        }
    c = ~c;
    while (n--) c = g_crctab[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}
static void game_path(char *out, size_t cap, const char *game, const char *rel) {
    snprintf(out, cap, "%s\\%s", game, rel);
    for (char *s = out; *s; s++) if (*s == '/') *s = '\\';
}

/* Checks every city whose folder is set. 1 = all match, 0 = problems,
   -1 = no folder picked; the report is in msg. */
int xpp_check(volatile long long *bytes_out, volatile long long *total_out, char *msg, size_t cap) {
    long long bytes_l = 0, total_l = 0;
    volatile long long *bytes = bytes_out ? bytes_out : &bytes_l, *total = total_out ? total_out : &total_l;
    static unsigned char buf[1 << 20];
    char p[MAX_PATH + 160], list[1400] = "";
    int nlist = 0, missing = 0, bad = 0, checked = 0;
    *bytes = 0; *total = 0;
    for (int i = 0; i < NFILES; i++) if (g_dir[k_files[i].game - 1][0]) *total += k_files[i].size;
    for (int i = 0; i < NFILES; i++) {
        const GameFile *g = &k_files[i];
        const char *dir = g_dir[g->game - 1];
        if (!dir[0]) continue;
        checked++;
        game_path(p, sizeof p, dir, g->rel);
        long long sz = file_size(p);
        const char *why = NULL;
        if (sz < 0) { why = "missing"; missing++; }
        else if (sz != g->size) { why = "different size"; bad++; }
        else {
            HANDLE h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            unsigned c = 0; DWORD got; int rd = h != INVALID_HANDLE_VALUE;
            long long b0 = *bytes;
            while (rd && ReadFile(h, buf, sizeof buf, &got, NULL) && got) { c = crc32_add(c, buf, got); *bytes += got; }
            if (rd) CloseHandle(h);
            *bytes = b0;
            if (!rd) { why = "can't be read"; bad++; }
            else if (c != g->crc) { why = "different contents"; bad++; }
        }
        *bytes += g->size;
        if (why && nlist < 12) {
            size_t n = strlen(list);
            snprintf(list + n, sizeof list - n, "   %s: %s (%s)\n", k_city[g->game - 1].game_name, g->rel, why);
            nlist++;
        }
    }
    int problems = missing + bad;
    if (!checked) snprintf(msg, cap, "No Most Wanted or Carbon folder picked.");
    else if (!problems)
        snprintf(msg, cap, "All %d World Expansion files are there and match the original games.",
                 checked);
    else
        snprintf(msg, cap, "%d of the %d World Expansion files %s missing and %d %s different "
                 "from the copies it was made with:\n\n%s%s\nA different release or a modded copy may still work, but "
                 "it may also crash or look wrong on the Vita.", missing, checked, missing == 1 ? "is" : "are",
                 bad, bad == 1 ? "is" : "are", list, problems > nlist ? "   ...\n" : "");
    printf("xpprep: file check: %d checked, %d missing, %d different\n", checked, missing, bad);
    return !checked ? -1 : !problems;
}

/* ---- what goes to the Vita ------------------------------------------------ */
static const char *const k_vdir[NCITY] = { "mw", "carbon" };   /* xpcity.c k_city[].dir */
/* where the city's stream goes on the Vita */
void xpp_vita_stream(int c, char *out, size_t cap) {
    snprintf(out, cap, VITA_DATA "/expansion/%s/TRACKS/%s.BUN", k_vdir[c], k_city[c].bun);
}
long long xpp_stream_size(int c) {
    for (int i = 0; i < NFILES; i++)
        if (k_files[i].game - 1 == c && strstr(k_files[i].rel, "STREAM")) return k_files[i].size;
    return -1;
}

/* ready to send: the city's world files there, at the sizes the check expects */
int xpp_city_done(int c) {
    if (!g_dir[c][0]) return 0;
    char p[MAX_PATH + 160];
    for (int i = 0; i < NFILES; i++) {
        const GameFile *g = &k_files[i];
        if (g->game - 1 != c || !strstr(g->rel, ".BUN")) continue;
        game_path(p, sizeof p, g_dir[c], g->rel);
        if (file_size(p) != g->size) return 0;
    }
    return 1;
}

/* Convert: the file check only (nothing to convert); 1 = some city is ready to send.
   ask_on_bad: ask (owner's message box) before going on with files that don't match */
int xpp_convert(XppJob *job, int ask_on_bad, HWND owner) {
    printf("World Expansion: Most Wanted %s, Carbon %s\n", g_dir[0][0] ? g_dir[0] : "-", g_dir[1][0] ? g_dir[1] : "-");
    job->todo = 0;
    job->start = GetTickCount();
    job->checking = 1;
    static char cmsg[2048];
    int good = xpp_check(&job->chk_bytes, &job->chk_total, cmsg, sizeof cmsg);
    job->checking = 0;
    printf("%s\n", cmsg);
    if (good == 0 && ask_on_bad) {
        char q[2400]; snprintf(q, sizeof q, "%s\n\nSend the World Expansion to the Vita anyway?", cmsg);
        if (MessageBoxA(owner, q, "NFSU2 Vita Prep Tool", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            for (int c = 0; c < NCITY; c++) g_dir[c][0] = 0;   /* left out of the send too */
            snprintf(job->msg, sizeof job->msg, "World Expansion left out: some Most Wanted / Carbon files are "
                     "missing or changed.");
            return 0;
        }
    }
    char names[64] = ""; int n = 0;
    for (int c = 0; c < NCITY; c++) {
        if (!xpp_city_done(c)) continue;
        size_t k = strlen(names);
        snprintf(names + k, sizeof names - k, "%s%s", n ? " and " : "", k_city[c].name);
        n++;
    }
    if (n) snprintf(job->msg, sizeof job->msg, "%s ready to send (the Vita plays %s from the game's own files, "
                    "nothing to convert).", names, n > 1 ? "them" : "it");
    else snprintf(job->msg, sizeof job->msg, "World Expansion: no city ready (its TRACKS files are missing or "
                  "a different size).");
    printf("xpprep: %s\n", job->msg);
    return n > 0;
}

/* per ready city: gameplay.bin and the minimap, the stream, then the companion last --
   the game takes the city once both .BUN files are there (xpcity.c raw_present), and a
   send cut off in the 1 GB stream leaves the companion missing */
int xpp_send_files(XppSendFn add, void *ud, int cap) {
    int n = 0;
    char l[MAX_PATH + 160], r[256];
    for (int c = 0; c < NCITY; c++) {
        if (!xpp_city_done(c)) continue;
        for (int pass = 0; pass < 3; pass++)
            for (int i = 0; i < NFILES && n < cap; i++) {
                const GameFile *g = &k_files[i];
                if (g->game - 1 != c || strstr(g->rel, "attributes.bin")) continue;   /* that one: the check only */
                int bun = strstr(g->rel, ".BUN") != NULL, stream = bun && strstr(g->rel, "STREAM") != NULL;
                if (pass != (!bun ? 0 : stream ? 1 : 2)) continue;
                game_path(l, sizeof l, g_dir[c], g->rel);
                snprintf(r, sizeof r, VITA_DATA "/expansion/%s/%s", k_vdir[c], g->rel);
                n += add(ud, l, r);
            }
    }
    return n;
}
