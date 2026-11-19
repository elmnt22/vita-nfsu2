/* ug2conv.c -- NFSU2 Vita Prep Tool (Windows; was "UG2Converter").
 *
 * Gets a player's NFSU2 PC install ready for the Vita:
 *  - checks the game files against the v1.2 data the port is tested with
 *    (game_manifest.inc: names, sizes, CRC32s -- tools/gen_prep_manifest.py)
 *  - converts every track's world into prebuilt tiles: the Vita's one-time
 *    first boot step ("OPTIMIZING WORLD DATA", 15-20+ minutes on the console),
 *    run here with the game's own world_tiles.c and resource.c, built 32-bit
 *    like the Vita, plus the other first-use caches (conv_extras.c)
 *  - sends the game files the Vita needs (only those: the manifest leaves out
 *    Support / Uninstall / #JIMBO and files the port never opens) and the
 *    cache to the Vita over FTP (VitaShell or vitacompanion, port 1337), or
 *    copies them to a folder (the memory card over VitaShell's USB mode).
 *    Game files already there at the same size are skipped.
 *
 *   NFSU2VitaPrep.exe                          the window
 *   NFSU2VitaPrep.exe --check GAME             check the game files (report on stderr)
 *  - optionally, the World Expansion (conv_xp.c): Rockport from Need for Speed
 *    Most Wanted (2005) and Palmont from Carbon, translated and converted here
 *    and sent with the rest; the player switches it on in the Vita's Settings.
 *
 *   NFSU2VitaPrep.exe --cli GAME OUT [MW|- [CARBON|-]]
 *                                              check + convert without the window
 *                                              (+ the World Expansion from MW / CARBON)
 *   NFSU2VitaPrep.exe --send IP OUT [GAME]     send OUT's cache (+ GAME's files) to the Vita
 *   NFSU2VitaPrep.exe --copy DEST OUT [GAME]   the same into DEST (ux0's root, or any folder)
 *   NFSU2VitaPrep.exe --import-save SAVE GAME OUT
 *                                         a PC save (%LOCALAPPDATA%\NFS Underground 2\
 *                                         <NAME>\<NAME>) into OUT\profiles\<NAME>\<NAME>
 *                                         (pcsave.c), sent with the rest
 */
#define TOOL_NAME "NFSU2 Vita Prep Tool"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wininet.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "world_tiles.h"
#include "pcsave.h"
#include "conv_xp.h"

void conv_set_folders(const char *game, const char *out);
unsigned char *res_bundle(const char *path, long *len);
void res_unpin(void);
void world_instance_library_release(void);
void world_instance_cache_flush(void);
void world_instance_set_detail(int low);
/* conv_extras.c: the caches the game writes on first use */
int conv_track_extras_done(const char *bun);
int conv_track_extras(const char *troot, const char *bun);
int conv_car_count(void);
const char *conv_car_name(int i);
int conv_car_mods(const char *dataroot, int i);
const char *conv_shop_file(void);
int conv_shop_done(void);
int conv_shop(void);

static const struct { const char *bun, *name; } k_tracks[] = {
    { "STREAML4RA", "The City" },        { "STREAML4RB", "Drag Strips 1" },
    { "STREAML4RC", "Drift Venues" },    { "STREAML4RD", "Drag Strips 2" },
    { "STREAML4RF", "Street X Tracks" }, { "STREAML4RG", "URL Tracks" } };
#define NTRACKS ((int)(sizeof k_tracks / sizeof *k_tracks))
#define TRACKS_ROOT "ux0:data/NFSU2/TRACKS"   /* the game folder's TRACKS (conv_platform.c) */
#define VITA_CACHE  "ux0:/data/NFSU2/cache"   /* on the Vita, as its FTP servers name it */
#define FTP_PORT    1337

static int file_exists(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static long long file_size(const char *p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(p, GetFileExInfoStandard, &d)) return -1;
    return ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

/* The NFSU2 folder holds TRACKS\STREAML4RA.BUN; picking TRACKS itself works too. */
static int game_dir_fix(char *dir, size_t cap) {
    size_t n = strlen(dir);
    while (n > 3 && (dir[n - 1] == '\\' || dir[n - 1] == '/')) dir[--n] = 0;
    char p[MAX_PATH + 64];
    snprintf(p, sizeof p, "%s\\TRACKS\\STREAML4RA.BUN", dir);
    if (file_exists(p)) return 1;
    snprintf(p, sizeof p, "%s\\STREAML4RA.BUN", dir);
    if (file_exists(p)) {
        char *s = strrchr(dir, '\\'); if (!s) s = strrchr(dir, '/');
        if (s) { *s = 0; return 1; }
    }
    (void)cap;
    return 0;
}
static int game_dir_detect(char *out, size_t cap) {
    static const char *const keys[] = {
        "SOFTWARE\\WOW6432Node\\EA GAMES\\Need for Speed Underground 2",
        "SOFTWARE\\EA GAMES\\Need for Speed Underground 2" };
    for (int i = 0; i < 2; i++) {
        DWORD n = (DWORD)cap;
        if (RegGetValueA(HKEY_LOCAL_MACHINE, keys[i], "Install Dir", RRF_RT_REG_SZ, NULL, out, &n) == ERROR_SUCCESS &&
            game_dir_fix(out, cap)) {
            char l[MAX_PATH];   /* the installer may store a short 8.3 name */
            DWORD k = GetLongPathNameA(out, l, sizeof l);
            if (k && k < sizeof l) snprintf(out, cap, "%s", l);
            return 1;
        }
    }
    GetModuleFileNameA(NULL, out, (DWORD)cap);   /* the tool dropped into the game folder */
    char *s = strrchr(out, '\\'); if (s) *s = 0;
    if (game_dir_fix(out, cap)) return 1;
    out[0] = 0;
    return 0;
}

/* ---- the game files the Vita needs, and the check against v1.2 ----------- */
typedef struct { const char *rel; long long size; unsigned crc; } GameFile;
static const GameFile k_game[] = {
#include "game_manifest.inc"
};
#define NGAME ((int)(sizeof k_game / sizeof *k_game))
/* Other genuine retail versions of a file that the port handles the same way.
   FrontB.lzc: an earlier retail print (2004-10-22); the only change inside is
   the online lobby screen OL_Lobby.fng, which the port never loads. */
static const GameFile k_game_alt[] = {
    { "FRONTEND/FrontB.lzc", 1750887LL, 0xa0d8bc03u },
};
static const GameFile *game_alt(const char *rel, long long size) {
    for (size_t i = 0; i < sizeof k_game_alt / sizeof *k_game_alt; i++)
        if (!strcmp(k_game_alt[i].rel, rel) && k_game_alt[i].size == size) return &k_game_alt[i];
    return NULL;
}
#define V12_EXE_SIZE 4800512LL   /* SPEED2.EXE v1.2 (Feb 9 2005); the data files are v1.0's, unchanged */

static unsigned g_crctab[256];
static unsigned crc32_add(unsigned c, const unsigned char *p, DWORD n) {   /* zlib's crc32 */
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
static long long game_total(void) {
    long long t = 0;
    for (int i = 0; i < NGAME; i++) t += k_game[i].size;
    return t;
}

static struct {
    volatile int busy, done, ok;   /* ok: every file there and matching */
    volatile long long bytes, total;
    int missing, bad;
    volatile int xp;   /* checking the World Expansion's files (after the game's) */
    char msg[4200];
} g_chk;

/* A fingerprint of the install as the file system sees it (sizes + write
   times): a later check of an unchanged folder that passed skips the CRCs. */
static unsigned game_fingerprint(const char *game) {
    unsigned c = crc32_add(0, (const unsigned char *)game, (DWORD)strlen(game));
    char p[MAX_PATH + 160];
    for (int i = 0; i < NGAME; i++) {
        game_path(p, sizeof p, game, k_game[i].rel);
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (!GetFileAttributesExA(p, GetFileExInfoStandard, &d)) memset(&d, 0, sizeof d);
        c = crc32_add(c, (const unsigned char *)&d.nFileSizeLow, 4);
        c = crc32_add(c, (const unsigned char *)&d.ftLastWriteTime, sizeof d.ftLastWriteTime);
    }
    return c;
}
static int setting_get(const char *name, char *out, DWORD cap);
static void setting_put(const char *name, const char *v);

/* Checks GAME against the manifest. Returns 1 when everything matches; the
   report is in g_chk.msg either way. */
static int check_game(const char *game) {
    static unsigned char buf[1 << 20];
    char p[MAX_PATH + 160], list[1200] = "";
    int nlist = 0;
    g_chk.missing = g_chk.bad = 0; g_chk.bytes = 0; g_chk.total = game_total();
    char fp[32], prev[32];
    snprintf(fp, sizeof fp, "%08x", game_fingerprint(game));
    setting_get("CheckedOK", prev, sizeof prev);
    int quick = !strcmp(fp, prev);   /* passed before and nothing changed: sizes only */
    for (int i = 0; i < NGAME; i++) {
        const GameFile *g = &k_game[i];
        game_path(p, sizeof p, game, g->rel);
        long long sz = file_size(p);
        const char *why = NULL;
        if (sz < 0) { why = "missing"; g_chk.missing++; }
        else if (sz != g->size && !game_alt(g->rel, sz)) { why = "different size"; g_chk.bad++; }
        else if (!quick) {
            HANDLE h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            unsigned c = 0; DWORD got; int rd = h != INVALID_HANDLE_VALUE;
            while (rd && ReadFile(h, buf, sizeof buf, &got, NULL) && got) { c = crc32_add(c, buf, got); g_chk.bytes += got; }
            if (rd) CloseHandle(h);
            if (!rd) { why = "can't be read"; g_chk.bad++; }
            else if (sz == g->size ? c != g->crc : c != game_alt(g->rel, sz)->crc) { why = "different contents"; g_chk.bad++; }
            g_chk.bytes -= sz;   /* counted below */
        }
        g_chk.bytes += g->size;
        if (why && nlist < 12) {
            size_t n = strlen(list);
            snprintf(list + n, sizeof list - n, "   %s (%s)\n", g->rel, why);
            nlist++;
        }
    }
    int problems = g_chk.missing + g_chk.bad;
    if (!problems) setting_put("CheckedOK", fp);
    char exe[MAX_PATH + 32], exe_note[200] = "";
    snprintf(exe, sizeof exe, "%s\\SPEED2.EXE", game);
    long long esz = file_size(exe);
    if (esz == V12_EXE_SIZE) snprintf(exe_note, sizeof exe_note, "\nSPEED2.EXE is v1.2.");
    else if (esz > 0) snprintf(exe_note, sizeof exe_note, "\nSPEED2.EXE is not v1.2. That's fine: the Vita "
                               "only uses the data files, and the v1.2 patch didn't change them.");
    if (!problems)
        snprintf(g_chk.msg, sizeof g_chk.msg, "All %d game files the Vita needs are there and match the "
                 "original game.%s", NGAME, exe_note);
    else
        snprintf(g_chk.msg, sizeof g_chk.msg, "%d of the %d game files the Vita needs %s missing and %d %s "
                 "different from the original game:\n\n%s%s\nMissing files can crash the game on the Vita. "
                 "Changed files (mods, a different release, damaged copies) may crash or look wrong. "
                 "Reinstalling the game, or copying it again, usually fixes this.%s",
                 g_chk.missing, NGAME, g_chk.missing == 1 ? "is" : "are", g_chk.bad, g_chk.bad == 1 ? "is" : "are",
                 list, problems > nlist ? "   ...\n" : "", exe_note);
    printf("prep: game check (%s): %d missing, %d different\n", quick ? "sizes" : "full", g_chk.missing, g_chk.bad);
    g_chk.ok = !problems;
    return !problems;
}

/* Output: "NFSU2 Vita Prep" next to the tool; Documents if that is read-only. */
static char g_out[MAX_PATH];
static void out_dir_init(void) {
    GetModuleFileNameA(NULL, g_out, sizeof g_out);
    char *s = strrchr(g_out, '\\'); if (s) *s = 0;
    char probe[MAX_PATH + 32];
    snprintf(probe, sizeof probe, "%s\\NFSU2 Vita Prep", g_out);
    CreateDirectoryA(probe, NULL);
    DWORD a = GetFileAttributesA(probe);
    char test[MAX_PATH + 48]; snprintf(test, sizeof test, "%s\\.write_test", probe);
    HANDLE h = a != INVALID_FILE_ATTRIBUTES ? CreateFileA(test, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                                          FILE_FLAG_DELETE_ON_CLOSE, NULL) : INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); snprintf(g_out, sizeof g_out, "%s", probe); return; }
    char docs[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, docs) != S_OK) snprintf(docs, sizeof docs, ".");
    snprintf(g_out, sizeof g_out, "%s\\NFSU2 Vita Prep", docs);
    CreateDirectoryA(g_out, NULL);
}

static void compact_path(char *out, size_t cap, int t) {
    snprintf(out, cap, "%s\\cache\\%s.BUN.compact", g_out, k_tracks[t].bun);
}
static int track_present(const char *game, int t) {
    char p[MAX_PATH + 64]; snprintf(p, sizeof p, "%s\\TRACKS\\%s.BUN", game, k_tracks[t].bun);
    return file_exists(p);
}
static int track_done(int t) {
    char p[MAX_PATH + 64]; compact_path(p, sizeof p, t);
    return world_tiles_ready(k_tracks[t].bun) && file_exists(p);
}

/* ---- the conversion (worker thread, or the command line) ------------------ */
static struct {
    volatile int busy, done, ok;
    volatile int cur, ncur, todo;   /* converting the cur-th of todo tracks (track ncur) */
    WTileProgress prog;
    volatile DWORD start;
    char game[MAX_PATH];
    char msg[512];
    char extra[128];   /* the first-use cache being made after the tracks */
    int gui;           /* the window: ask before converting a folder that failed the check */
    volatile int checking;
    volatile int in_xp;   /* on the World Expansion (conv_xp.c), after the tracks */
    XppJob xp;
} g_job;
static HWND g_wnd;

void conv_fatal(const char *what) {
    printf("converter: unexpected call to %s -- stopping\n", what);
    fflush(stdout);
    char m[256]; snprintf(m, sizeof m, "The conversion reached code it does not support (%s).\n"
                          "Please report this. Nothing was copied to your Vita.", what);
    MessageBoxA(NULL, m, TOOL_NAME, MB_ICONERROR);
    ExitProcess(3);
}

static void convert_all(void) {
    conv_set_folders(g_job.game, g_out);
    char p[MAX_PATH + 64];
    snprintf(p, sizeof p, "%s\\cache", g_out); CreateDirectoryA(p, NULL);
    snprintf(p, sizeof p, "%s\\convert_log.txt", g_out);
    freopen(p, "w", stdout);
    setvbuf(stdout, NULL, _IOLBF, 4096);
    printf(TOOL_NAME ": game folder %s, output %s\n", g_job.game, g_out);
    /* the game files first: a broken or modded copy is better caught here
       than as a crash on the Vita */
    g_job.checking = 1;
    int good = check_game(g_job.game);
    g_job.checking = 0;
    printf("%s\n", g_chk.msg);
    if (!good && g_job.gui) {
        char q[2200];
        snprintf(q, sizeof q, "%s\n\nConvert anyway?", g_chk.msg);
        if (MessageBoxA(g_wnd, q, TOOL_NAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            snprintf(g_job.msg, sizeof g_job.msg, "Stopped: some game files are missing or changed (see Check game files).");
            printf("converter: %s\n", g_job.msg); fflush(stdout);
            g_job.ok = 0; g_job.done = 1; g_job.busy = 0;
            return;
        }
    }
    int todo[NTRACKS], n = 0, missing = 0;
    for (int t = 0; t < NTRACKS; t++) {
        if (!track_present(g_job.game, t)) { printf("%s: no %s.BUN, skipped\n", k_tracks[t].name, k_tracks[t].bun); missing++; continue; }
        if (track_done(t)) { printf("%s: already converted\n", k_tracks[t].name); continue; }
        todo[n++] = t;
    }
    g_job.todo = n;
    g_job.start = GetTickCount();
    /* the console's world detail (main.c: low unless world_detail.txt says
       full, a debug switch); the tiles are built at the detail it plays */
    world_instance_set_detail(1);
    int ok = 1;
    for (int i = 0; i < n && ok; i++) {
        int t = todo[i];
        g_job.cur = i; g_job.ncur = t;
        /* as the Vita's boot does: one pinned bundle at a time, and nothing
           keyed to the previous one */
        res_unpin(); world_instance_library_release(); world_instance_cache_flush();
        DWORD t0 = GetTickCount();
        ok = world_tiles_convert(TRACKS_ROOT, k_tracks[t].bun, &g_job.prog) && g_job.prog.ok;
        if (ok) {
            char cp[MAX_PATH + 64]; compact_path(cp, sizeof cp, t);
            if (!file_exists(cp)) {   /* the conversion pins it; a set without it is built here */
                char bp[128]; snprintf(bp, sizeof bp, "%s/%s.BUN", TRACKS_ROOT, k_tracks[t].bun);
                long len = 0; res_unpin(); res_bundle(bp, &len);
            }
            ok = track_done(t);
        }
        printf("converter: %s %s in %.1f s\n", k_tracks[t].name, ok ? "converted" : "FAILED",
               (GetTickCount() - t0) / 1000.0);
    }
    res_unpin(); world_instance_library_release(); world_instance_cache_flush();
    /* then what the game would build the first time it needs it: each
       track's stripped bundle and start, the cars' parts lists, the shop */
    int extras_ok = 1;
    for (int t = 0; t < NTRACKS && ok; t++) {
        if (!track_done(t) || conv_track_extras_done(k_tracks[t].bun)) continue;
        snprintf(g_job.extra, sizeof g_job.extra, "Preparing %s...", k_tracks[t].name);
        extras_ok &= conv_track_extras(TRACKS_ROOT, k_tracks[t].bun);
        world_instance_library_release(); world_instance_cache_flush();
    }
    for (int i = 0; i < conv_car_count() && ok; i++) {
        snprintf(g_job.extra, sizeof g_job.extra, "Preparing car parts lists (%d of %d)...", i + 1, conv_car_count());
        extras_ok &= conv_car_mods("ux0:data/NFSU2", i);
    }
    if (ok) {
        snprintf(g_job.extra, sizeof g_job.extra, "Preparing the shop screens...");
        extras_ok &= conv_shop();
    }
    g_job.extra[0] = 0;
    if (!extras_ok) printf("converter: some first-use caches failed (the Vita builds those itself)\n");
    unsigned secs = (GetTickCount() - g_job.start) / 1000;
    if (!ok) snprintf(g_job.msg, sizeof g_job.msg, "Conversion failed on %s. See convert_log.txt in the output folder.",
                      k_tracks[g_job.ncur].name);
    else if (!track_done(0)) { ok = 0; snprintf(g_job.msg, sizeof g_job.msg, "This folder has no city (TRACKS\\STREAML4RA.BUN)."); }
    else if (n) snprintf(g_job.msg, sizeof g_job.msg, "Done: %d track%s converted in %u:%02u%s. Now send it to your Vita.",
                         n, n == 1 ? "" : "s", secs / 60, secs % 60, missing ? " (some tracks were missing)" : "");
    else snprintf(g_job.msg, sizeof g_job.msg, "Everything is already converted. Now send it to your Vita.");
    printf("converter: %s\n", g_job.msg);
    /* then the World Expansion cities whose game folders are picked */
    if (ok && xpp_any_dir()) {
        g_job.in_xp = 1;
        int xok = xpp_convert(&g_job.xp, g_job.gui, g_wnd);
        conv_set_folders(g_job.game, g_out);
        g_job.in_xp = 0;
        char m[sizeof g_job.msg];
        if (xok) snprintf(m, sizeof m, "Done: %s Now send it to your Vita.", g_job.xp.msg);
        else snprintf(m, sizeof m, "Underground 2 is ready, but: %s", g_job.xp.msg);
        snprintf(g_job.msg, sizeof g_job.msg, "%s", m);
        printf("converter: %s\n", g_job.msg);
        ok = xok;
    }
    fflush(stdout);
    g_job.ok = ok;
    g_job.done = 1;
    g_job.busy = 0;
}
static DWORD WINAPI convert_thread(LPVOID a) { (void)a; convert_all(); return 0; }

/* ---- sending to the Vita over FTP (worker thread) ------------------------- */
typedef struct { char local[MAX_PATH + 160], remote[256]; long long size, mtime; int game; } SendFile;
static struct {
    volatile int busy, done, ok;
    volatile long long sent, total;
    volatile int file, nfiles, skipped;
    char ip[64];
    char dest[MAX_PATH];   /* set: copy into this folder (the memory card's root) instead of FTP */
    char game[MAX_PATH];   /* set: the game files go too */
    int xp;                /* the converted World Expansion cities go too */
    volatile int xp_sent;  /* ...and some did */
    char msg[768];
} g_send;

#define SEND_CAP (NGAME + 5 * NTRACKS + 64 + 16 + XPP_NCITY * XPP_FILES_PER_CITY)
#define VITA_DATA "ux0:/data/NFSU2"
#define VITA_PROFILES "ux0:/data/NFSU2/profiles"
/* the imported PC saves: OUT\profiles\<NAME>\<NAME> */
static int send_profiles(SendFile *f, int n, int cap) {
    char pat[MAX_PATH + 32];
    snprintf(pat, sizeof pat, "%s\\profiles\\*", g_out);
    WIN32_FIND_DATAA d;
    HANDLE h = FindFirstFileA(pat, &d);
    if (h == INVALID_HANDLE_VALUE) return n;
    do {
        if (d.cFileName[0] == '.' || !(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || n >= cap) continue;
        snprintf(f[n].local, sizeof f[n].local, "%s\\profiles\\%s\\%s", g_out, d.cFileName, d.cFileName);
        snprintf(f[n].remote, sizeof f[n].remote, VITA_PROFILES "/%s/%s", d.cFileName, d.cFileName);
        if (file_exists(f[n].local)) n++;
    } while (FindNextFileA(h, &d));
    FindClose(h);
    return n;
}
static int profiles_present(void) { SendFile f[16]; return send_profiles(f, 0, 16) > 0; }

/* ---- importing a PC save (pcsave.c) ---------------------------------------- */
static int import_save(const char *save, const char *game, char *msg, size_t cap) {
    char prof[MAX_PATH + 32];
    conv_set_folders(game, g_out);
    snprintf(prof, sizeof prof, "%s\\profiles", g_out);
    CreateDirectoryA(g_out, NULL);
    PcSaveInfo info;
    int ok = pcsave_import(save, "ux0:data/NFSU2", prof, &info);
    if (ok) snprintf(msg, cap, "Imported profile %s: %d career car%s, %d garage car%s, %d high score%s.\n\n"
                     "It is in the output folder's profiles folder; Send to Vita copies it, then load %s "
                     "in the game's Profile Manager.%s", info.name, info.ncareer, info.ncareer == 1 ? "" : "s",
                     info.ntuned, info.ntuned == 1 ? "" : "s", info.nhs, info.nhs == 1 ? "" : "s", info.name,
                     info.crc_ok ? "" : "\n\nNote: the save's checksum does not match its contents (the PC game "
                                        "itself would call it corrupt); it was imported anyway.");
    else snprintf(msg, cap, "Could not import the save: %s.", info.msg);
    fflush(stdout);
    return ok;
}
static int send_add(SendFile *f, int n, const char *name) {   /* a cache file, if made */
    snprintf(f[n].local, sizeof f[n].local, "%s\\cache\\%s", g_out, name);
    snprintf(f[n].remote, sizeof f[n].remote, VITA_CACHE "/%s", name);
    return file_exists(f[n].local) ? n + 1 : n;
}
typedef struct { SendFile *f; int n, cap; } XpAdd;
static int xp_add(void *ud, const char *local, const char *remote) {
    XpAdd *a = ud;
    if (a->n >= a->cap || !file_exists(local)) return 0;
    SendFile *s = &a->f[a->n++];
    snprintf(s->local, sizeof s->local, "%s", local);
    snprintf(s->remote, sizeof s->remote, "%s", remote);
    s->game = 0;   /* always sent: a new translation can keep a file's size */
    return 1;
}
static int send_list(SendFile *f, int cap, const char *game) {
    int n = 0;
    char name[128];
    /* the game files first (only the ones the Vita needs), so the cache --
       whose indexes say "converted" -- only lands next to a whole install */
    for (int i = 0; game && game[0] && i < NGAME && n < cap; i++) {
        game_path(f[n].local, sizeof f[n].local, game, k_game[i].rel);
        snprintf(f[n].remote, sizeof f[n].remote, VITA_DATA "/%s", k_game[i].rel);
        f[n].game = 1;
        if (file_exists(f[n].local)) n++;
    }
    /* per track: the compact bundle, its stripped copy and start, the tile
       pack, then the index -- its presence tells the game the set is whole,
       so it goes last */
    for (int i = n; i < cap; i++) f[i].game = 0;
    for (int t = 0; t < NTRACKS && n + 5 <= cap; t++) {
        if (!track_done(t)) continue;
        compact_path(f[n].local, sizeof f[n].local, t);
        snprintf(f[n].remote, sizeof f[n].remote, VITA_CACHE "/%s.BUN.compact", k_tracks[t].bun); n++;
        snprintf(name, sizeof name, "%s.BUN.compact.strip", k_tracks[t].bun); n = send_add(f, n, name);
        snprintf(name, sizeof name, "%s.focus", k_tracks[t].bun + 6); n = send_add(f, n, name);
        snprintf(f[n].local, sizeof f[n].local, "%s\\cache\\tiles\\%s_v%d.pak", g_out, k_tracks[t].bun, WTILE_VERSION);
        snprintf(f[n].remote, sizeof f[n].remote, VITA_CACHE "/tiles/%s_v%d.pak", k_tracks[t].bun, WTILE_VERSION); n++;
        snprintf(f[n].local, sizeof f[n].local, "%s\\cache\\tiles\\%s_v%d.idx", g_out, k_tracks[t].bun, WTILE_VERSION);
        snprintf(f[n].remote, sizeof f[n].remote, VITA_CACHE "/tiles/%s_v%d.idx", k_tracks[t].bun, WTILE_VERSION); n++;
    }
    for (int i = 0; i < conv_car_count() && n + 1 < cap; i++) {
        snprintf(name, sizeof name, "mods_%s.bin", conv_car_name(i)); n = send_add(f, n, name);
    }
    if (n < cap) n = send_add(f, n, conv_shop_file());
    n = send_profiles(f, n, cap);
    g_send.xp_sent = 0;
    if (g_send.xp) {   /* the World Expansion (conv_xp.c), each city's index last */
        XpAdd a = { f, n, cap };
        g_send.xp_sent = xpp_send_files(xp_add, &a, cap - n) > 0;
        n = a.n;
    }
    for (int i = 0; i < n; i++) {
        f[i].size = file_size(f[i].local);
        WIN32_FILE_ATTRIBUTE_DATA a;
        f[i].mtime = GetFileAttributesExA(f[i].local, GetFileExInfoStandard, &a)
                   ? ((long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime : 0;
    }
    return n;
}

/* Two ways onto the Vita: FTP (c set), or a folder (g_send.dest: the memory
   card's root as VitaShell's USB mode shows it, or anywhere to copy later). */
static void dest_path(char *out, size_t cap, const char *remote) {   /* "ux0:/x/y" -> DEST\x\y */
    const char *r = !strncmp(remote, "ux0:/", 5) ? remote + 5 : remote;
    snprintf(out, cap, "%s\\%s", g_send.dest, r);
    for (char *s = out; *s; s++) if (*s == '/') *s = '\\';
}

/* the remote folder's file sizes, to skip game files already there */
typedef struct { char name[96]; long long size; } RemoteEnt;
static RemoteEnt g_rent[512]; static int g_nrent; static char g_rdir[256];
static void remote_list(HINTERNET c, const char *dir) {
    if (!strcmp(dir, g_rdir)) return;
    snprintf(g_rdir, sizeof g_rdir, "%s", dir);
    g_nrent = 0;
    if (c) {
        if (!FtpSetCurrentDirectoryA(c, dir)) return;
        WIN32_FIND_DATAA d;
        HINTERNET f = FtpFindFirstFileA(c, NULL, &d, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (!f) { FtpSetCurrentDirectoryA(c, "/"); return; }
        do {
            if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || g_nrent >= 512) continue;
            snprintf(g_rent[g_nrent].name, sizeof g_rent[0].name, "%s", d.cFileName);
            g_rent[g_nrent++].size = ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
        } while (InternetFindNextFileA(f, &d));
        InternetCloseHandle(f);
        FtpSetCurrentDirectoryA(c, "/");   /* back to the root: the uploads' paths start at ux0:/ */
    }
}
static int already_there(HINTERNET c, const SendFile *f) {
    if (!f->game) return 0;   /* the cache always goes: same size does not mean the same tiles */
    if (!c) { char p[MAX_PATH + 300]; dest_path(p, sizeof p, f->remote); return file_size(p) == f->size; }
    char dir[256]; snprintf(dir, sizeof dir, "%s", f->remote);
    char *s = strrchr(dir, '/'); if (!s) return 0;
    *s = 0;
    remote_list(c, dir);
    for (int i = 0; i < g_nrent; i++)
        if (!_stricmp(g_rent[i].name, s + 1)) return g_rent[i].size == f->size;
    return 0;
}

/* What was last sent to each Vita / memory card: OUT\sent_<where>.txt, one line per
   file (size, PC write time, remote path; later lines win). A converted file goes
   again only when it changed since, or is not on the Vita at that size (11-19:
   adding the World Expansion no longer resends the Underground 2 data). */
typedef struct { char remote[256]; long long size, mtime; } SentRec;
static SentRec *g_sent; static int g_nsent, g_sent_cap;
static char g_sent_file[MAX_PATH + 160];
static void sent_load(void) {
    char key[MAX_PATH + 8];
    snprintf(key, sizeof key, "%s%s", g_send.dest[0] ? "card_" : "vita_", g_send.dest[0] ? g_send.dest : g_send.ip);
    for (char *q = key; *q; q++) if (!isalnum((unsigned char)*q) && *q != '.' && *q != '_') *q = '_';
    snprintf(g_sent_file, sizeof g_sent_file, "%s\\sent_%s.txt", g_out, key);
    g_nsent = 0;
    FILE *fp = fopen(g_sent_file, "r");
    if (!fp) return;
    char line[400];
    while (fgets(line, sizeof line, fp)) {
        SentRec r; char *e = strchr(line, '\n'); if (e) *e = 0;
        if (sscanf(line, "%lld\t%lld\t%255[^\n]", &r.size, &r.mtime, r.remote) != 3) continue;
        if (g_nsent == g_sent_cap) {
            int cap = g_sent_cap ? g_sent_cap * 2 : 256;
            SentRec *m = realloc(g_sent, (size_t)cap * sizeof *m); if (!m) break;
            g_sent = m; g_sent_cap = cap;
        }
        g_sent[g_nsent++] = r;
    }
    fclose(fp);
}
static int sent_same(const SendFile *f) {   /* the last record of this file matches it */
    for (int i = g_nsent - 1; i >= 0; i--)
        if (!strcmp(g_sent[i].remote, f->remote)) return g_sent[i].size == f->size && g_sent[i].mtime == f->mtime && f->mtime;
    return 0;
}
static int sent_known(const SendFile *f) {   /* any record of this file */
    for (int i = g_nsent - 1; i >= 0; i--) if (!strcmp(g_sent[i].remote, f->remote)) return 1;
    return 0;
}
/* An imported PC save on the Vita is the player's live save from then on: it goes
   only when the Vita has none of that name, or it was imported again on the PC since
   it was last sent here (11-19: a resend used to put the old import over the Vita's
   newer progress). */
static int is_profile(const SendFile *f) { return !strncmp(f->remote, VITA_PROFILES "/", sizeof VITA_PROFILES); }
static int g_kept_profiles;
static void sent_note(const SendFile *f) {
    FILE *fp = fopen(g_sent_file, "a");
    if (!fp) return;
    fprintf(fp, "%lld\t%lld\t%s\n", f->size, f->mtime, f->remote);
    fclose(fp);
}
/* the Vita's folders, each listed once per send (the files alternate between cache
   and cache/tiles, and a listing is a round trip over Wi-Fi) */
typedef struct { char dir[256]; int n; RemoteEnt *e; } RemoteDir;
static RemoteDir g_rdirs[24]; static int g_nrdirs;
static void remote_dirs_reset(void) {
    for (int i = 0; i < g_nrdirs; i++) free(g_rdirs[i].e);
    g_nrdirs = 0;
}
static long long remote_size(HINTERNET c, const char *remote) {   /* -1: not there */
    if (!c) { char p[MAX_PATH + 300]; dest_path(p, sizeof p, remote); return file_exists(p) ? file_size(p) : -1; }
    char dir[256]; snprintf(dir, sizeof dir, "%s", remote);
    char *s = strrchr(dir, '/'); if (!s) return -1;
    *s = 0;
    RemoteDir *d = NULL;
    for (int i = 0; i < g_nrdirs && !d; i++) if (!strcmp(g_rdirs[i].dir, dir)) d = &g_rdirs[i];
    if (!d) {
        if (g_nrdirs == (int)(sizeof g_rdirs / sizeof g_rdirs[0])) remote_dirs_reset();
        d = &g_rdirs[g_nrdirs++];
        snprintf(d->dir, sizeof d->dir, "%s", dir);
        d->n = 0; d->e = NULL;
        int cap = 0;
        if (FtpSetCurrentDirectoryA(c, dir)) {
            WIN32_FIND_DATAA fd;
            HINTERNET f = FtpFindFirstFileA(c, NULL, &fd, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
            if (f) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    if (d->n == cap) {
                        int nc = cap ? cap * 2 : 256;
                        RemoteEnt *m = realloc(d->e, (size_t)nc * sizeof *m); if (!m) break;
                        d->e = m; cap = nc;
                    }
                    snprintf(d->e[d->n].name, sizeof d->e[0].name, "%s", fd.cFileName);
                    d->e[d->n++].size = ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                } while (InternetFindNextFileA(f, &fd));
                InternetCloseHandle(f);
            }
            FtpSetCurrentDirectoryA(c, "/");
        }
    }
    for (int i = 0; i < d->n; i++)
        if (!_stricmp(d->e[i].name, s + 1)) return d->e[i].size;
    return -1;
}
static int is_marker(const SendFile *f) { return strstr(f->remote, ".idx") || strstr(f->remote, ".xp"); }

/* every folder on the way to the file (made once each per send) */
static char g_made[256];
static void make_dirs(HINTERNET c, const char *remote) {
    char *made = g_made;
    char dir[256]; snprintf(dir, sizeof dir, "%s", remote);
    char *s = strrchr(dir, '/'); if (!s) return;
    *s = 0;
    if (!strcmp(dir, made)) return;
    snprintf(made, sizeof g_made, "%s", dir);
    if (!c) { char p[MAX_PATH + 300]; dest_path(p, sizeof p, dir); SHCreateDirectoryExA(NULL, p, NULL); return; }
    for (char *q = dir + 5; (q = strchr(q, '/')) != NULL; q++) {   /* past "ux0:/" */
        *q = 0; FtpCreateDirectoryA(c, dir); *q = '/';   /* already there is fine */
    }
    FtpCreateDirectoryA(c, dir);
}

static char g_why[200];   /* put_file's reason when it fails */
static void why_net(const char *what) {
    DWORD e = GetLastError(), ext = 0, n = sizeof g_why;
    char resp[160] = ""; DWORD rn = sizeof resp;
    if (e == ERROR_INTERNET_EXTENDED_ERROR) InternetGetLastResponseInfoA(&ext, resp, &rn);
    for (char *q = resp; *q; q++) if (*q == '\r' || *q == '\n') *q = ' ';
    (void)n;
    snprintf(g_why, sizeof g_why, "%s (error %lu%s%s)", what, (unsigned long)e, resp[0] ? ", the Vita said: " : "", resp);
}
static int put_file(HINTERNET c, const SendFile *f, char *buf, DWORD bufsz) {
    g_why[0] = 0;
    HANDLE h = CreateFileA(f->local, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        snprintf(g_why, sizeof g_why, "this PC could not read %s (error %lu)", f->local, (unsigned long)GetLastError());
        return 0;
    }
    HINTERNET r = NULL; HANDLE o = INVALID_HANDLE_VALUE;
    if (c) r = FtpOpenFileA(c, f->remote, GENERIC_WRITE, FTP_TRANSFER_TYPE_BINARY, 0);
    else { char p[MAX_PATH + 300]; dest_path(p, sizeof p, f->remote);
           o = CreateFileA(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); }
    int ok = c ? r != NULL : o != INVALID_HANDLE_VALUE;
    if (!ok) { if (c) why_net("the Vita refused the file"); else snprintf(g_why, sizeof g_why, "could not create it (error %lu)", (unsigned long)GetLastError()); }
    DWORD got;
    while (ok && ReadFile(h, buf, bufsz, &got, NULL) && got) {
        for (DWORD off = 0; ok && off < got; ) {
            DWORD w = 0;
            ok = (c ? InternetWriteFile(r, buf + off, got - off, &w) : WriteFile(o, buf + off, got - off, &w, NULL)) && w;
            if (!ok) { if (c) why_net("the connection dropped"); else snprintf(g_why, sizeof g_why, "writing failed (error %lu%s)",
                           (unsigned long)GetLastError(), GetLastError() == ERROR_DISK_FULL ? ", the memory card is full" : ""); }
            off += w; g_send.sent += w;
        }
    }
    CloseHandle(h);
    if (r && !InternetCloseHandle(r)) { if (ok) why_net("the Vita did not finish the file"); ok = 0; }
    if (o != INVALID_HANDLE_VALUE && !CloseHandle(o)) ok = 0;
    return ok;
}

static void send_all(void) {
    static SendFile files[SEND_CAP];
    int to_dir = g_send.dest[0] != 0;
    int n = send_list(files, SEND_CAP, g_send.game);
    long long total = 0;
    for (int i = 0; i < n; i++) total += files[i].size;
    g_send.total = total; g_send.sent = 0; g_send.nfiles = n; g_send.skipped = 0;
    int ok = n > 0;
    long long moved = 0;
    HINTERNET net = NULL, c = NULL;
    if (!to_dir) {
        net = InternetOpenA(TOOL_NAME, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        c = net ? InternetConnectA(net, g_send.ip, FTP_PORT, NULL, NULL, INTERNET_SERVICE_FTP,
                                   INTERNET_FLAG_PASSIVE, 0) : NULL;
    }
    g_rdir[0] = 0; g_made[0] = 0;
    if (!to_dir && !c) {
        ok = 0;
        snprintf(g_send.msg, sizeof g_send.msg, "Could not reach the Vita at %s. Is its FTP server on "
                 "(VitaShell: press SELECT) and the IP right?", g_send.ip);
    } else if (ok) {
        /* what can stay: game files there at the same size; converted files sent
           here before, unchanged, and still there. A set's index (or a city's .xp
           marker) goes again whenever any file of its set does. */
        static unsigned char keep[SEND_CAP];
        g_kept_profiles = 0;
        sent_load();
        remote_dirs_reset();
        for (int i = 0, start = 0; i < n; i++) {
            if (files[i].game) { keep[i] = (unsigned char)already_there(c, &files[i]); start = i + 1; continue; }
            if (is_profile(&files[i])) {
                long long rs = remote_size(c, files[i].remote);
                keep[i] = rs >= 0 && (sent_same(&files[i]) || !sent_known(&files[i]));
                if (keep[i] && !sent_same(&files[i])) g_kept_profiles++;
                continue;   /* no marker after the profiles */
            }
            keep[i] = sent_same(&files[i]) && remote_size(c, files[i].remote) == files[i].size;
            if (is_marker(&files[i])) {   /* a set ends at its tile index (a city: ... .xp, .idx) */
                for (int k = start; k < i; k++) if (!files[k].game && !keep[k]) keep[i] = 0;
                if (strstr(files[i].remote, ".idx")) start = i + 1;
            }
        }
        /* the old indexes go first: a set cut off half way reads as missing
           (and the Vita converts it) instead of as whole */
        for (int i = 0; i < n; i++)   /* (and the World Expansion's .xp markers) */
            if (is_marker(&files[i]) && !keep[i]) {
                if (c) FtpDeleteFileA(c, files[i].remote);
                else { char p[MAX_PATH + 300]; dest_path(p, sizeof p, files[i].remote); DeleteFileA(p); }
            }
        static char buf[256 << 10];
        for (int i = 0; i < n && ok; i++) {
            g_send.file = i;
            if (keep[i]) { g_send.sent += files[i].size; g_send.skipped++; continue; }
            long long sent0 = g_send.sent;
            make_dirs(c, files[i].remote);
            ok = put_file(c, &files[i], buf, sizeof buf);
            if (!ok && c) {   /* once more on a fresh connection (a Wi-Fi drop, a server that lost its place) */
                InternetCloseHandle(c);
                c = InternetConnectA(net, g_send.ip, FTP_PORT, NULL, NULL, INTERNET_SERVICE_FTP, INTERNET_FLAG_PASSIVE, 0);
                g_send.sent = sent0; g_made[0] = 0; g_rdir[0] = 0;
                if (c) { make_dirs(c, files[i].remote); ok = put_file(c, &files[i], buf, sizeof buf); }
                else snprintf(g_why, sizeof g_why, "the Vita stopped answering");
            }
            if (ok) { moved += files[i].size; if (!files[i].game) sent_note(&files[i]); }
            if (!ok) snprintf(g_send.msg, sizeof g_send.msg, to_dir
                              ? "Copying %s failed: %s. Is the memory card still connected, with enough free space?"
                              : "Sending %s failed: %s. Check the Wi-Fi, keep the Vita awake (Settings > Power Save: "
                                "auto-standby off), and try again (files already sent are skipped).",
                              files[i].remote + 5, g_why[0] ? g_why : "unknown reason");
        }
        char skip[96] = "";
        if (g_send.skipped) snprintf(skip, sizeof skip, " (%d file%s already there, skipped)", g_send.skipped,
                                     g_send.skipped == 1 ? "" : "s");
        if (ok) snprintf(g_send.msg, sizeof g_send.msg, "%s %.0f MB%s%s. Start NFS Underground 2%s.%s%s",
                         to_dir ? "Copied" : "Sent", moved / 1048576.0,
                         to_dir ? "" : " to the Vita", skip,
                         track_done(0) ? " -- it skips \"Optimizing world data\"" : "",
                         g_kept_profiles ? "\n\nYour profile already on the Vita was kept (not replaced by the "
                                           "PC import). To replace it, import the PC save again, then send." : "",
                         g_send.xp_sent ? "\n\nTo drive Rockport and Palmont: on the game's LiveArea screen press "
                                          "Settings (or hold L+R while it starts), tick \"Rockport and Palmont\" "
                                          "under World Expansion, then pick the city in Free Run." : "");
    } else snprintf(g_send.msg, sizeof g_send.msg, "Nothing to send yet: convert first, or tick \"Game files too\".");
    if (c) InternetCloseHandle(c);
    if (net) InternetCloseHandle(net);
    g_send.ok = ok;
    g_send.done = 1;
    g_send.busy = 0;
}
static DWORD WINAPI send_thread(LPVOID a) { (void)a; send_all(); return 0; }

/* ---- settings ------------------------------------------------------------- */
#define REG_KEY "Software\\NFSU2Vita\\PrepTool"
static int setting_get_key(const char *key, const char *name, char *out, DWORD cap) {
    if (RegGetValueA(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, NULL, out, &cap) == ERROR_SUCCESS) return 1;
    out[0] = 0;
    return 0;
}
static int setting_get(const char *name, char *out, DWORD cap) { return setting_get_key(REG_KEY, name, out, cap); }
static void setting_put(const char *name, const char *v) {
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)strlen(v) + 1);
    RegCloseKey(k);
}

/* ---- the window ----------------------------------------------------------- */
enum { ID_GAME = 100, ID_BROWSE, ID_CONVERT, ID_PROGRESS, ID_STATUS, ID_IP, ID_SEND, ID_OPEN, ID_TIMER, ID_IMPORT,
       ID_CHECK, ID_GAMEFILES, ID_COPY, ID_XPDIR0, ID_XPDIR1, ID_XPBROWSE0, ID_XPBROWSE1, ID_XPSTATUS, ID_XPFILES };
static HWND g_game_ed, g_browse, g_convert, g_bar, g_ip_ed, g_send_btn, g_open, g_import,
            g_check, g_gamefiles, g_copy;
static HWND g_xp_ed[XPP_NCITY], g_xp_browse[XPP_NCITY], g_xpfiles;   /* the World Expansion */
static HFONT g_font, g_bold, g_title, g_small, g_head, g_sub, g_btnf, g_btnbig, g_glyph, g_glyph_s, g_glyph_l;
static HBRUSH g_white;
static int g_dpi = 96;
static int S(int v) { return MulDiv(v, g_dpi, 96); }

/* ---- the look (11-19 redesign): painted cards, icon-font glyphs, GDI+ for the
   smooth shapes. Every position below is in 96-dpi units; S() scales it. ---- */
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread; BOOL SuppressExternalCodecs; } GpStartupIn;
int WINAPI GdiplusStartup(ULONG_PTR *token, const GpStartupIn *in, void *out);
int WINAPI GdipCreateFromHDC(HDC dc, void **g);
int WINAPI GdipDeleteGraphics(void *g);
int WINAPI GdipSetSmoothingMode(void *g, int mode);
int WINAPI GdipSetPixelOffsetMode(void *g, int mode);
int WINAPI GdipCreateSolidFill(DWORD argb, void **brush);
int WINAPI GdipDeleteBrush(void *brush);
int WINAPI GdipCreatePen1(DWORD argb, float width, int unit, void **pen);
int WINAPI GdipDeletePen(void *pen);
int WINAPI GdipCreatePath(int fillmode, void **path);
int WINAPI GdipDeletePath(void *path);
int WINAPI GdipAddPathArc(void *path, float x, float y, float w, float h, float start, float sweep);
int WINAPI GdipClosePathFigure(void *path);
int WINAPI GdipFillPath(void *g, void *brush, void *path);
int WINAPI GdipDrawPath(void *g, void *pen, void *path);
int WINAPI GdipFillEllipse(void *g, void *brush, float x, float y, float w, float h);
int WINAPI GdipDrawLine(void *g, void *pen, float x1, float y1, float x2, float y2);

#define ARGB(a, r, g, b) (((DWORD)(a) << 24) | ((DWORD)(r) << 16) | ((DWORD)(g) << 8) | (DWORD)(b))
static void *gp_begin(HDC dc) {
    void *g = NULL;
    if (GdipCreateFromHDC(dc, &g) || !g) return NULL;
    GdipSetSmoothingMode(g, 4);      /* anti-alias */
    GdipSetPixelOffsetMode(g, 4);    /* half */
    return g;
}
static void *gp_round_path(float x, float y, float w, float h, float r) {
    void *p = NULL; GdipCreatePath(0, &p);
    if (!p) return NULL;
    float d = r * 2;
    if (d > h) d = h;
    if (d > w) d = w;
    GdipAddPathArc(p, x, y, d, d, 180, 90);
    GdipAddPathArc(p, x + w - d, y, d, d, 270, 90);
    GdipAddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
    GdipAddPathArc(p, x, y + h - d, d, d, 90, 90);
    GdipClosePathFigure(p);
    return p;
}
/* a rounded box in 96-dpi units: fill (0 = none) and a 1 px border (0 = none) */
static void gp_box(void *g, int x, int y, int w, int h, int r, DWORD fill, DWORD border, float bw) {
    if (!g) return;
    float o = border ? 0.5f : 0.0f;
    void *p = gp_round_path(S(x) + o, S(y) + o, S(w) - 2 * o, S(h) - 2 * o, (float)S(r));
    if (!p) return;
    if (fill) { void *b = NULL; GdipCreateSolidFill(fill, &b); if (b) { GdipFillPath(g, b, p); GdipDeleteBrush(b); } }
    if (border) { void *pen = NULL; GdipCreatePen1(border, bw * g_dpi / 96.0f, 2, &pen); if (pen) { GdipDrawPath(g, pen, p); GdipDeletePen(pen); } }
    GdipDeletePath(p);
}
static void gp_circle(void *g, float cx, float cy, float d, DWORD fill) {
    if (!g) return;
    float k = g_dpi / 96.0f;   /* centre and diameter in 96-dpi units */
    void *b = NULL; GdipCreateSolidFill(fill, &b);
    if (b) { GdipFillEllipse(g, b, (cx - d / 2) * k, (cy - d / 2) * k, d * k, d * k); GdipDeleteBrush(b); }
}
/* text in 96-dpi units */
static void text_at(HDC dc, HFONT f, COLORREF c, int x, int y, int w, int h, const wchar_t *s, UINT fmt) {
    RECT r = { S(x), S(y), S(x + w), S(y + h) };
    SelectObject(dc, f); SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, &r, fmt | DT_NOPREFIX);
}
static int text_w(HDC dc, HFONT f, const wchar_t *s) {   /* in pixels */
    SIZE z; SelectObject(dc, f); GetTextExtentPoint32W(dc, s, (int)wcslen(s), &z); return z.cx;
}

/* the layout: the header, then five cards */
enum { CW = 600, CX0 = 16, CX1 = 584, CL = 80, CR = 564 };
enum { HDR_H = 112 };
static const int k_card_y[5] = { 112, 252, 420, 578, 788 }, k_card_h[5] = { 132, 160, 150, 202, 72 };
#define CLIENT_H (788 + 72 + 14)
#define CY(i) (k_card_y[i])

static HWND g_wnd_focus_ed;   /* the edit box with the keyboard: its frame turns blue */
typedef struct { HWND ed; int x, y, w, h; } EdFrame;
static EdFrame g_frames[8]; static int g_nframes;

static HWND mk(const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id, HFONT f) {
    if (strcmp(cls, "STATIC") && strcmp(cls, PROGRESS_CLASSA)) style |= WS_TABSTOP;
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             S(x), S(y), S(w), S(h), g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}
/* a borderless edit box inside a painted rounded frame */
static HWND mk_edit(int x, int y, int w, int h, int id) {
    HWND e = mk("EDIT", "", ES_AUTOHSCROLL, x + 9, y + (h - 18) / 2, w - 18, 18, id, g_font);
    if (g_nframes < 8) { EdFrame *f = &g_frames[g_nframes++]; f->ed = e; f->x = x; f->y = y; f->w = w; f->h = h; }
    return e;
}
static void inval(int x, int y, int w, int h) {
    RECT r = { S(x), S(y), S(x + w), S(y + h) };
    InvalidateRect(g_wnd, &r, FALSE);
}

/* the status line under the progress bar, and the result beside it */
static char g_status_txt[600];
static void set_status(const char *s) {
    snprintf(g_status_txt, sizeof g_status_txt, "%s", s);
    inval(CL, CY(2) + 122, CR - CL, 20);
}
/* the result label beside the progress bar: 0 blank, 1 working, 2 done, 3 failed */
static int g_done_state;
static void set_done(int st) {
    if (st == g_done_state) return;
    g_done_state = st;
    inval(CL + 400, CY(2) + 100, CR - CL - 400, 24);
    if (st >= 2 && GetForegroundWindow() != g_wnd) {   /* a long job ended: flash the taskbar button */
        FLASHWINFO fw = { sizeof fw, g_wnd, FLASHW_ALL | FLASHW_TIMERNOFG, 0, 0 };
        FlashWindowEx(&fw);
    }
}
static void ui_idle(void) {
    int busy = g_job.busy || g_send.busy || g_chk.busy;
    EnableWindow(g_convert, !busy); EnableWindow(g_browse, !busy); EnableWindow(g_game_ed, !busy);
    EnableWindow(g_check, !busy);
    EnableWindow(g_send_btn, !busy); EnableWindow(g_copy, !busy); EnableWindow(g_ip_ed, !busy);
    EnableWindow(g_gamefiles, !busy);
    EnableWindow(g_import, !busy);
    for (int c = 0; c < XPP_NCITY; c++) { EnableWindow(g_xp_ed[c], !busy); EnableWindow(g_xp_browse[c], !busy); }
    EnableWindow(g_xpfiles, !busy);
}
/* the World Expansion pills: each city's state, from its folder box and the output
   (0 not set, 1 found, 2 ready to send, 3 folder not right) */
static int g_city_st[XPP_NCITY];
static void xp_status_update(void) {
    char dir[MAX_PATH];
    for (int c = 0; c < XPP_NCITY; c++) {
        GetWindowTextA(g_xp_ed[c], dir, sizeof dir);
        if (xpp_city_done(c)) g_city_st[c] = 2;
        else if (!dir[0]) g_city_st[c] = 0;
        else if (xpp_dir_fix(c, dir)) g_city_st[c] = 1;
        else g_city_st[c] = 3;
    }
    inval(CL, CY(1) + 118, CR - CL, 30);
}
/* the line beside Check Game Files: 0 no folder, 1 found, 2 checked OK, 3 check failed,
   4 not the game's folder, 5 converted */
static int g_game_st, g_chk_res;   /* g_chk_res: 0 not checked, 1 OK, 2 problems */
static void game_state_update(void) {
    char game[MAX_PATH]; GetWindowTextA(g_game_ed, game, sizeof game);
    int st;
    if (!game[0]) st = 0;
    else if (!game_dir_fix(game, sizeof game)) st = 4;
    else if (g_chk_res == 2) st = 3;
    else {
        int all = 1; for (int t = 0; t < NTRACKS; t++) all &= track_done(t);
        st = all ? 5 : g_chk_res == 1 ? 2 : 1;
    }
    if (st != g_game_st) { g_game_st = st; inval(CL + 150, CY(0) + 88, CR - CL - 150, 30); }
}
/* the World Expansion folder boxes -> conv_xp.c (and the settings). 0 = one is wrong
   (says so; the player fixes or clears it) */
static int xp_read_dirs(int warn) {
    for (int c = 0; c < XPP_NCITY; c++) {
        char d[MAX_PATH]; GetWindowTextA(g_xp_ed[c], d, sizeof d);
        char *a = d; while (*a == ' ' || *a == '"') a++;
        char *e = a + strlen(a); while (e > a && (e[-1] == ' ' || e[-1] == '"')) *--e = 0;
        if (*a && !xpp_dir_fix(c, a)) {
            if (warn) {
                char q[600];
                snprintf(q, sizeof q, "The %s folder has no TRACKS\\%s.BUN.\n\nPick the folder %s is installed "
                         "in, or clear the box to leave %s out.", xpp_game_name(c), xpp_bun(c), xpp_game_name(c),
                         xpp_city_name(c));
                MessageBoxA(g_wnd, q, TOOL_NAME, MB_ICONWARNING);
            }
            return 0;
        }
        SetWindowTextA(g_xp_ed[c], a);
        setting_put(c ? "CarbonDir" : "MWDir", a);
        xpp_set_dir(c, a);
    }
    return 1;
}
static void ui_initial_status(void) {
    char game[MAX_PATH]; GetWindowTextA(g_game_ed, game, sizeof game);
    if (track_done(0)) {
        int n = 0; for (int t = 0; t < NTRACKS; t++) n += track_done(t);
        char m[160]; snprintf(m, sizeof m, "%d of %d tracks already converted. Convert finishes the rest; "
                              "Send copies them to the Vita.", n, NTRACKS);
        set_status(m);
        SendMessageA(g_bar, PBM_SETPOS, n == NTRACKS ? 1000 : 0, 0);
    } else if (!game[0]) set_status("Pick your Need for Speed Underground 2 folder, then press Convert.");
    else set_status("Ready. Press Convert -- it checks your game files, then converts (about 2 minutes).");
}

static void on_browse(void) {
    BROWSEINFOA bi = {0};
    bi.hwndOwner = g_wnd;
    bi.lpszTitle = "Pick your Need for Speed Underground 2 folder (the one with the TRACKS folder in it)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderA(&bi);
    if (!id) return;
    char p[MAX_PATH];
    if (SHGetPathFromIDListA(id, p)) {
        if (!game_dir_fix(p, sizeof p))
            MessageBoxA(g_wnd, "That folder has no TRACKS\\STREAML4RA.BUN.\n\nPick the folder NFS Underground 2 is "
                        "installed in (or the folder you copied the game data from).", TOOL_NAME, MB_ICONWARNING);
        else SetWindowTextA(g_game_ed, p);
    }
    CoTaskMemFree(id);
    ui_initial_status();
}
static void on_xp_browse(int c) {
    char title[200];
    snprintf(title, sizeof title, "Pick your %s folder (the one with the TRACKS folder in it)", xpp_game_name(c));
    BROWSEINFOA bi = {0};
    bi.hwndOwner = g_wnd;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderA(&bi);
    if (!id) return;
    char p[MAX_PATH];
    if (SHGetPathFromIDListA(id, p)) {
        if (!xpp_dir_fix(c, p)) {
            char q[600];
            snprintf(q, sizeof q, "That folder has no TRACKS\\%s.BUN.\n\nPick the folder %s is installed in "
                     "(or the folder you copied its game data to).", xpp_bun(c), xpp_game_name(c));
            MessageBoxA(g_wnd, q, TOOL_NAME, MB_ICONWARNING);
        } else { SetWindowTextA(g_xp_ed[c], p); setting_put(c ? "CarbonDir" : "MWDir", p); }
    }
    CoTaskMemFree(id);
    xp_status_update();
}
static void on_convert(void) {
    char game[MAX_PATH]; GetWindowTextA(g_game_ed, game, sizeof game);
    if (!game_dir_fix(game, sizeof game)) {
        MessageBoxA(g_wnd, "Pick your Need for Speed Underground 2 folder first (it has a TRACKS folder in it).",
                    TOOL_NAME, MB_ICONWARNING);
        return;
    }
    if (!xp_read_dirs(1)) return;
    SetWindowTextA(g_game_ed, game);
    setting_put("GameDir", game);
    memset(&g_job, 0, sizeof g_job);
    snprintf(g_job.game, sizeof g_job.game, "%s", game);
    g_job.gui = 1;
    g_job.busy = 1;
    SendMessageA(g_bar, PBM_SETPOS, 0, 0);
    set_status("Starting...");
    ui_idle();
    HANDLE h = CreateThread(NULL, 16 << 20, convert_thread, NULL, 0, NULL);
    if (h) CloseHandle(h); else { g_job.busy = 0; set_status("Could not start the conversion."); ui_idle(); }
}
/* Send to Vita (FTP) and Copy to memory card (a folder) start the same job */
static void start_send(const char *ip, const char *dest) {
    int with_game = SendMessageA(g_gamefiles, BM_GETCHECK, 0, 0) == BST_CHECKED;
    setting_put("GameFiles", with_game ? "1" : "0");
    char game[MAX_PATH]; GetWindowTextA(g_game_ed, game, sizeof game);
    if (with_game && !game_dir_fix(game, sizeof game)) {
        MessageBoxA(g_wnd, "Pick your Need for Speed Underground 2 folder first (step 1), or untick "
                    "\"Game files too\" to send only the converted data.", TOOL_NAME, MB_ICONWARNING);
        return;
    }
    memset(&g_send, 0, sizeof g_send);
    if (ip) snprintf(g_send.ip, sizeof g_send.ip, "%s", ip);
    if (dest) snprintf(g_send.dest, sizeof g_send.dest, "%s", dest);
    if (with_game) snprintf(g_send.game, sizeof g_send.game, "%s", game);
    g_send.xp = SendMessageA(g_xpfiles, BM_GETCHECK, 0, 0) == BST_CHECKED;
    setting_put("XpFiles", g_send.xp ? "1" : "0");
    g_send.busy = 1;
    SendMessageA(g_bar, PBM_SETPOS, 0, 0);
    set_status(dest ? "Copying..." : "Connecting to the Vita...");
    ui_idle();
    HANDLE h = CreateThread(NULL, 0, send_thread, NULL, 0, NULL);
    if (h) CloseHandle(h); else { g_send.busy = 0; ui_idle(); }
}
static void on_send(void) {
    char ip[64]; GetWindowTextA(g_ip_ed, ip, sizeof ip);
    char *s = ip; while (*s == ' ') s++;
    char *e = s + strlen(s); while (e > s && e[-1] == ' ') *--e = 0;
    if (!*s) { MessageBoxA(g_wnd, "Type your Vita's IP address (VitaShell shows it when you press SELECT).",
                           TOOL_NAME, MB_ICONINFORMATION); return; }
    setting_put("VitaIP", s);
    start_send(s, NULL);
}
/* dest folder -> the memory card's root: a pick of its data or data\NFSU2
   folder means the same card */
static void dest_fix(char *p) {
    for (char *s = p; *s; s++) if (*s == '/') *s = '\\';
    size_t n = strlen(p);
    while (n > 0 && p[n - 1] == '\\') p[--n] = 0;   /* "E:\" -> "E:" */
    if (n >= 11 && !_stricmp(p + n - 11, "\\data\\NFSU2")) p[n -= 11] = 0;
    if (n >= 5 && !_stricmp(p + n - 5, "\\data")) p[n -= 5] = 0;
}
/* the memory card over USB (VitaShell: START > USB) shows up as a drive */
static void on_copy(void) {
    BROWSEINFOA bi = {0};
    bi.hwndOwner = g_wnd;
    bi.lpszTitle = "Pick the Vita's memory card (ux0) as it shows up over USB, or any folder to copy it to later";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderA(&bi);
    if (!id) return;
    char p[MAX_PATH];
    int got = SHGetPathFromIDListA(id, p);
    CoTaskMemFree(id);
    if (!got) return;
    dest_fix(p);
    start_send(NULL, p);
}
/* Check game files: the same check Convert runs first, on its own */
static DWORD WINAPI check_thread(LPVOID a) {
    int ok = check_game((const char *)a);
    if (xpp_any_dir()) {
        static char m[2048];
        g_chk.xp = 1;
        int x = xpp_check(&g_chk.bytes, &g_chk.total, m, sizeof m);
        g_chk.xp = 0;
        size_t n = strlen(g_chk.msg);
        snprintf(g_chk.msg + n, sizeof g_chk.msg - n, "\n\nWorld Expansion: %s", m);
        g_chk.ok = ok && x != 0;
    }
    g_chk.done = 1; g_chk.busy = 0;
    return 0;
}
static void on_check(void) {
    static char game[MAX_PATH];
    GetWindowTextA(g_game_ed, game, sizeof game);
    if (!game_dir_fix(game, sizeof game)) {
        MessageBoxA(g_wnd, "Pick your Need for Speed Underground 2 folder first (it has a TRACKS folder in it).",
                    TOOL_NAME, MB_ICONWARNING);
        return;
    }
    if (!xp_read_dirs(1)) return;
    SetWindowTextA(g_game_ed, game);
    setting_put("GameDir", game);
    g_chk.done = 0; g_chk.busy = 1;
    SendMessageA(g_bar, PBM_SETPOS, 0, 0);
    set_status("Checking your game files...");
    ui_idle();
    HANDLE h = CreateThread(NULL, 0, check_thread, game, 0, NULL);
    if (h) CloseHandle(h); else { g_chk.busy = 0; ui_idle(); }
}
/* "Import PC save...": the PC game's profile file (its folder in
   %LOCALAPPDATA%\NFS Underground 2), into the output's profiles folder */
static void on_import(void) {
    char game[MAX_PATH]; GetWindowTextA(g_game_ed, game, sizeof game);
    if (!game_dir_fix(game, sizeof game)) {
        MessageBoxA(g_wnd, "Pick your Need for Speed Underground 2 folder first (it has a TRACKS folder in it): "
                    "the import reads the game's car data.", TOOL_NAME, MB_ICONWARNING);
        return;
    }
    char dir[MAX_PATH] = "", file[MAX_PATH] = "";
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, dir) == S_OK)
        strncat(dir, "\\NFS Underground 2", sizeof dir - strlen(dir) - 1);
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof ofn; ofn.hwndOwner = g_wnd;
    ofn.lpstrFilter = "NFSU2 profile (no extension)\0*.*\0";
    ofn.lpstrFile = file; ofn.nMaxFile = sizeof file;
    ofn.lpstrInitialDir = dir[0] ? dir : NULL;
    ofn.lpstrTitle = "Pick your NFSU2 profile: the file named like the profile, in its folder";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&ofn)) return;
    char log[MAX_PATH + 32]; snprintf(log, sizeof log, "%s\\import_log.txt", g_out);
    freopen(log, "a", stdout);
    char m[768];
    set_status("Importing the save...");
    int ok = import_save(file, game, m, sizeof m);
    set_status(ok ? "Save imported. Send to Vita copies it." : "The save could not be imported.");
    ui_idle();
    MessageBoxA(g_wnd, m, TOOL_NAME, ok ? MB_ICONINFORMATION : MB_ICONERROR);
}
static void on_timer(void) {
    char m[256];
    if (g_chk.busy || g_job.busy || g_send.busy) set_done(1);
    if (g_chk.busy || (g_job.busy && g_job.checking)) {
        long long t = g_chk.total > 0 ? g_chk.total : 1;
        SendMessageA(g_bar, PBM_SETPOS, (WPARAM)(g_chk.bytes * 1000 / t), 0);
        snprintf(m, sizeof m, g_chk.xp ? "Checking your Most Wanted / Carbon files: %d%%" : "Checking your game files: %d%%",
                 (int)(g_chk.bytes * 100 / t));
        set_status(m);
    } else if (g_chk.done) {
        g_chk.done = 0;
        SendMessageA(g_bar, PBM_SETPOS, 1000, 0);
        set_status(g_chk.ok ? "Game files OK." : "Some game files are missing or changed.");
        set_done(g_chk.ok ? 2 : 3);
        g_chk_res = g_chk.ok ? 1 : 2; game_state_update();
        ui_idle();
        MessageBoxA(g_wnd, g_chk.msg, TOOL_NAME, g_chk.ok ? MB_ICONINFORMATION : MB_ICONWARNING);
    } else if (g_job.busy && g_job.in_xp) {   /* the World Expansion, after the tracks */
        XppJob *x = &g_job.xp;
        unsigned secs = (GetTickCount() - x->start) / 1000;
        if (x->checking) {
            long long t = x->chk_total > 0 ? x->chk_total : 1;
            SendMessageA(g_bar, PBM_SETPOS, (WPARAM)(x->chk_bytes * 1000 / t), 0);
            snprintf(m, sizeof m, "Checking your Most Wanted / Carbon files: %d%%", (int)(x->chk_bytes * 100 / t));
            set_status(m);
        } else if (x->extra[0]) set_status(x->extra);
        else if (x->todo) {
            float f;   /* translating is the first third, the world conversion the rest */
            if (x->phase == 1) f = x->xp_total > 0 ? (float)x->xp_done / (float)x->xp_total / 3.0f : 0.0f;
            else f = 1.0f / 3.0f + (x->prog.cells_total > 0 ? (float)x->prog.cells_done / (float)x->prog.cells_total : 0.0f) * 2.0f / 3.0f;
            if (f > 1.0f) f = 1.0f;
            SendMessageA(g_bar, PBM_SETPOS, (WPARAM)((x->cur + f) / (float)x->todo * 1000.0f), 0);
            snprintf(m, sizeof m, "World Expansion %d of %d: %s  -  %s  %d%%      (%u:%02u)", x->cur + 1, x->todo,
                     xpp_city_name(x->ncur), x->phase == 1 ? "translating" : "building the world", (int)(f * 100.0f),
                     secs / 60, secs % 60);
            set_status(m);
        } else set_status("World Expansion: starting...");
    } else if (g_job.busy && g_job.todo && !g_job.extra[0]) {
        const WTileProgress *p = &g_job.prog;
        float f = p->cells_total > 0 ? (float)p->cells_done / (float)p->cells_total : 0.0f;
        if (f > 1.0f) f = 1.0f;
        float all = (g_job.cur + f) / (float)g_job.todo;
        SendMessageA(g_bar, PBM_SETPOS, (WPARAM)(all * 1000.0f), 0);
        unsigned secs = (GetTickCount() - g_job.start) / 1000;
        snprintf(m, sizeof m, "Converting %d of %d: %s  -  %d%%      (%u:%02u)", g_job.cur + 1, g_job.todo,
                 k_tracks[g_job.ncur].name, (int)(f * 100.0f), secs / 60, secs % 60);
        set_status(m);
    } else if (g_job.busy && g_job.extra[0]) {
        set_status(g_job.extra);
    } else if (g_job.done) {
        g_job.done = 0;
        if (g_job.ok) SendMessageA(g_bar, PBM_SETPOS, 1000, 0);
        set_status(g_job.msg);
        set_done(g_job.ok ? 2 : 3);
        ui_idle();
        xp_status_update();
        game_state_update();
        if (!g_job.ok) MessageBoxA(g_wnd, g_job.msg, TOOL_NAME, MB_ICONERROR);
    }
    if (g_send.busy && g_send.total > 0) {
        SendMessageA(g_bar, PBM_SETPOS, (WPARAM)(g_send.sent * 1000 / g_send.total), 0);
        snprintf(m, sizeof m, "%s: %.0f of %.0f MB (file %d of %d)", g_send.dest[0] ? "Copying" : "Sending to the Vita",
                 g_send.sent / 1048576.0, g_send.total / 1048576.0, g_send.file + 1, g_send.nfiles);
        set_status(m);
    } else if (g_send.done) {
        g_send.done = 0;
        set_status(g_send.msg);
        set_done(g_send.ok ? 2 : 3);
        ui_idle();
        MessageBoxA(g_wnd, g_send.msg, TOOL_NAME, g_send.ok ? MB_ICONINFORMATION : MB_ICONERROR);
    }
}

static void build_ui(void) {
    int y;
    y = CY(0);   /* 1. the game folder */
    g_game_ed = mk_edit(CL, y + 52, 374, 30, ID_GAME);
    g_browse = mk("BUTTON", "Browse...", BS_OWNERDRAW, 464, y + 52, 100, 30, ID_BROWSE, g_btnf);
    g_check = mk("BUTTON", "Check Game Files", BS_OWNERDRAW, CL, y + 90, 140, 30, ID_CHECK, g_btnf);

    y = CY(1);   /* the World Expansion */
    for (int c = 0; c < XPP_NCITY; c++) {
        int ry = y + 52 + c * 36;
        g_xp_ed[c] = mk_edit(CL + 96, ry, 278, 30, ID_XPDIR0 + c);
        g_xp_browse[c] = mk("BUTTON", "Browse...", BS_OWNERDRAW, 464, ry, 100, 30, ID_XPBROWSE0 + c, g_btnf);
    }
    SendMessageW(g_xp_ed[0], EM_SETCUEBANNER, FALSE, (LPARAM)L"Need for Speed Most Wanted folder (optional)");
    SendMessageW(g_xp_ed[1], EM_SETCUEBANNER, FALSE, (LPARAM)L"Need for Speed Carbon folder (optional)");
    SendMessageW(g_game_ed, EM_SETCUEBANNER, FALSE, (LPARAM)L"Pick your Need for Speed Underground 2 folder");

    y = CY(2);   /* 2. Convert */
    g_convert = mk("BUTTON", "Convert", BS_OWNERDRAW, CL, y + 56, 190, 40, ID_CONVERT, g_btnbig);
    g_bar = mk(PROGRESS_CLASSA, "", 0, CL, y + 108, 392, 10, ID_PROGRESS, g_font);
    SendMessageA(g_bar, PBM_SETRANGE32, 0, 1000);

    y = CY(3);   /* 3. Put it on your Vita */
    g_gamefiles = mk("BUTTON", "Game files too (1.7 GB the first time, then skipped)",
                     BS_AUTOCHECKBOX, CL, y + 56, 316, 22, ID_GAMEFILES, g_font);
    g_xpfiles = mk("BUTTON", "World Expansion too", BS_AUTOCHECKBOX, CL + 326, y + 56, 158, 22, ID_XPFILES, g_font);
    g_ip_ed = mk_edit(CL + 104, y + 90, 196, 30, ID_IP);
    g_send_btn = mk("BUTTON", "Send to Vita", BS_OWNERDRAW, CL + 312, y + 90, CR - CL - 312, 30, ID_SEND, g_btnf);
    g_copy = mk("BUTTON", "Copy to Memory Card...", BS_OWNERDRAW, 36, y + 152, 180, 32, ID_COPY, g_btnf);
    g_open = mk("BUTTON", "Open Output Folder", BS_OWNERDRAW, 412, y + 152, 152, 32, ID_OPEN, g_btnf);

    y = CY(4);   /* bring your PC career */
    g_import = mk("BUTTON", "Import PC Save...", BS_OWNERDRAW, CR - 160, y + 20, 160, 32, ID_IMPORT, g_btnf);
}

/* the card icons: glyphs of the Windows icon font, the handheld drawn */
static void glyph_at(HDC dc, HFONT f, COLORREF c, int x, int y, int w, int h, wchar_t g) {
    wchar_t s[2] = { g, 0 };
    text_at(dc, f, c, x, y, w, h, s, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
static void icon_check(void *g, HDC dc, int cx, int cy, int d, COLORREF bg) {   /* a ticked circle */
    gp_circle(g, (float)cx, (float)cy, (float)d, ARGB(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
    HFONT f = d >= 16 ? g_glyph_s : g_small;
    (void)f;
    glyph_at(dc, g_glyph_s, RGB(255, 255, 255), cx - d / 2, cy - d / 2, d, d, 0xE73E);
}
static void card(void *g, HDC dc, int i, const wchar_t *head, const wchar_t *sub, int subw) {
    int y = CY(i), h = k_card_h[i];
    gp_box(g, CX0, y + 1, CX1 - CX0, h, 12, ARGB(14, 20, 40, 80), 0, 0);   /* a faint shadow */
    gp_box(g, CX0, y, CX1 - CX0, h, 12, ARGB(255, 255, 255, 255), ARGB(255, 218, 225, 235), 1.0f);
    text_at(dc, g_head, RGB(16, 30, 54), CL, y + 12, CR - CL, 24, head, DT_SINGLELINE | DT_END_ELLIPSIS);
    text_at(dc, g_sub, RGB(72, 84, 102), CL, y + 34, subw ? subw : CR - CL, 36, sub, DT_WORDBREAK);
}
static void paint_all(HDC dc, int cw, int chh) {
    {   /* the background: a soft blue-grey wash */
        for (int y = 0; y < chh; y++) {
            float t = (float)y / (chh > 1 ? chh - 1 : 1);
            HBRUSH b = CreateSolidBrush(RGB(240 - (int)(8 * t), 245 - (int)(6 * t), 252 - (int)(3 * t)));
            RECT r = { 0, y, cw, y + 1 }; FillRect(dc, &r, b); DeleteObject(b);
        }
    }
    void *g = gp_begin(dc);
    /* the header: soft light shapes on the right, the title */
    gp_circle(g, 520, -10, 300, ARGB(70, 255, 255, 255));
    gp_circle(g, 600, 40, 220, ARGB(60, 214, 230, 252));
    text_at(dc, g_title, RGB(14, 28, 52), CX0 + 8, 20, 540, 36, L"NFSU2 Vita Prep Tool", DT_SINGLELINE);
    text_at(dc, g_sub, RGB(56, 70, 92), CX0 + 8, 58, 540, 40,
            L"Gets your NFSU2 PC files ready for the Vita: checks them, does the Vita’s one-time "
            L"“Optimizing world data” step here, and puts everything on the Vita.", DT_WORDBREAK);

    /* the cards */
    card(g, dc, 0, L"1.  Your Need for Speed Underground 2 Folder", L"Pick the folder NFSU2 is installed in.", 0);
    card(g, dc, 1, L"Optional:  World Expansion — Rockport and Palmont",
         L"Own Need for Speed Most Wanted (2005) or Carbon? Drive its city in Free Run too.", 0);
    card(g, dc, 2, L"2.  Convert", L"Prepares the world data for the Vita. Your game files are only read, never changed.", 0);
    card(g, dc, 3, L"3.  Put It on Your Vita", L"Send the converted files to your PS Vita, over Wi-Fi or USB.", 0);
    card(g, dc, 4, L"Optional:  Bring Your PC Career",
         L"Import a PC save to continue your progress on the Vita (career, cars, options, high scores).", CR - 172 - CL);

    /* card icons */
    glyph_at(dc, g_glyph, RGB(47, 123, 224), CX0 + 14, CY(0) + 12, 40, 36, 0xE8B7);   /* folder */
    glyph_at(dc, g_glyph, RGB(123, 97, 255), CX0 + 14, CY(1) + 12, 40, 36, 0xEA86);   /* puzzle piece */
    gp_circle(g, CX0 + 34, CY(2) + 30, 32, ARGB(255, 37, 110, 230));                  /* play */
    glyph_at(dc, g_glyph_s, RGB(255, 255, 255), CX0 + 21, CY(2) + 16, 30, 28, 0xF5B0);
    {   /* a handheld */
        int x = CX0 + 17, y = CY(3) + 20;
        gp_box(g, x, y, 34, 20, 7, 0, ARGB(255, 37, 110, 230), 2.2f);
        gp_box(g, x + 10, y + 5, 14, 10, 2, ARGB(255, 37, 110, 230), 0, 0);
        gp_circle(g, x + 5.5f, y + 10, 3.5f, ARGB(255, 37, 110, 230));
        gp_circle(g, x + 28.5f, y + 10, 3.5f, ARGB(255, 37, 110, 230));
    }
    glyph_at(dc, g_glyph, RGB(37, 110, 230), CX0 + 14, CY(4) + 18, 40, 36, 0xE7FC);   /* gamepad */

    /* the edit frames */
    for (int i = 0; i < g_nframes; i++) {
        const EdFrame *f = &g_frames[i];
        int foc = f->ed == g_wnd_focus_ed;
        gp_box(g, f->x, f->y, f->w, f->h, 6, ARGB(255, 255, 255, 255),
               foc ? ARGB(255, 47, 123, 224) : ARGB(255, 197, 206, 218), foc ? 1.6f : 1.0f);
    }

    /* card 1: the game folder's state */
    {
        static const wchar_t *t[6] = { L"Pick your NFSU2 folder.", L"Game files found — ready to convert.",
                                       L"Game files OK — ready to convert.", L"Some game files are missing or changed.",
                                       L"That folder has no TRACKS folder.", L"Converted — ready to send." };
        int st = g_game_st, y = CY(0) + 105, x = CL + 156;
        if (st == 1 || st == 2 || st == 5) icon_check(g, dc, x + 9, y, 18, RGB(34, 160, 80));
        else if (st == 0) gp_circle(g, (float)x + 9, (float)y, 8, ARGB(255, 150, 160, 175));
        else { gp_circle(g, (float)x + 9, (float)y, 18, ARGB(255, 214, 54, 54));
               text_at(dc, g_bold, RGB(255, 255, 255), x, y - 9, 18, 18, L"!", DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
        COLORREF c = st == 0 ? RGB(90, 100, 115) : (st == 3 || st == 4) ? RGB(190, 30, 30) : RGB(22, 128, 60);
        text_at(dc, g_font, c, x + 26, y - 10, CR - x - 26, 20, t[st], DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
    /* card 2: the labels and the city pills */
    for (int c = 0; c < XPP_NCITY; c++)
        text_at(dc, g_font, RGB(40, 52, 70), CL, CY(1) + 52 + c * 36, 94, 30, c ? L"Carbon:" : L"Most Wanted:",
                DT_SINGLELINE | DT_VCENTER);
    {
        int x = CL, y = CY(1) + 122;
        for (int c = 0; c < XPP_NCITY; c++) {
            static const wchar_t *st[4] = { L"Not set", L"Found — press Convert", L"Ready to send", L"Folder not right" };
            wchar_t s[80]; swprintf(s, 80, L"%hs: %ls", xpp_city_name(c), st[g_city_st[c]]);
            int k = g_city_st[c];
            int w = text_w(dc, g_font, s) * 96 / g_dpi + 44;
            DWORD bg = k == 0 ? ARGB(255, 236, 239, 243) : k == 3 ? ARGB(255, 253, 236, 236) : ARGB(255, 228, 244, 233);
            COLORREF fg = k == 0 ? RGB(70, 80, 96) : k == 3 ? RGB(180, 35, 24) : RGB(22, 120, 58);
            gp_box(g, x, y, w, 26, 13, bg, 0, 0);
            if (k == 1 || k == 2) icon_check(g, dc, x + 17, y + 13, 16, RGB(34, 160, 80));
            else gp_circle(g, (float)x + 17, (float)y + 13, 8, k == 3 ? ARGB(255, 214, 54, 54) : ARGB(255, 140, 150, 165));
            text_at(dc, g_font, fg, x + 31, y, w - 31, 26, s, DT_SINGLELINE | DT_VCENTER);
            x += w + 10;
        }
    }
    /* card 3: the time note, the result, the status line */
    {
        int y = CY(2);
        HPEN p = CreatePen(PS_SOLID, 1, RGB(214, 221, 231)); HGDIOBJ op = SelectObject(dc, p);
        MoveToEx(dc, S(CL + 208), S(y + 62), NULL); LineTo(dc, S(CL + 208), S(y + 92));
        SelectObject(dc, op); DeleteObject(p);
        glyph_at(dc, g_glyph, RGB(40, 52, 70), CL + 222, y + 58, 30, 36, 0xE823);   /* clock */
        text_at(dc, g_bold, RGB(16, 30, 54), CL + 260, y + 58, 220, 18, L"About 2 minutes", DT_SINGLELINE);
        text_at(dc, g_small, RGB(72, 84, 102), CL + 260, y + 77, 230, 18, L"(longer with the World Expansion)", DT_SINGLELINE);
        static const wchar_t *dt[4] = { L"", L"Working…", L"✔ Done!", L"✖ Failed" };
        COLORREF dc2 = g_done_state == 2 ? RGB(22, 140, 60) : g_done_state == 3 ? RGB(200, 30, 30) : RGB(110, 118, 130);
        text_at(dc, g_bold, dc2, CL + 400, y + 102, CR - CL - 400, 22, dt[g_done_state], DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
        wchar_t w[600]; MultiByteToWideChar(CP_ACP, 0, g_status_txt, -1, w, 600);
        text_at(dc, g_small, RGB(72, 84, 102), CL, y + 124, CR - CL, 18, w, DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    /* card 4: the IP label and hint, the USB note, the output folder's caption */
    {
        int y = CY(3);
        text_at(dc, g_font, RGB(40, 52, 70), CL, y + 90, 104, 30, L"Vita IP address:", DT_SINGLELINE | DT_VCENTER);
        text_at(dc, g_small, RGB(96, 106, 122), CL, y + 124, CR - CL, 18,
                L"VitaShell shows the IP when you press SELECT. Close NFS Underground 2 on the Vita first.",
                DT_SINGLELINE | DT_END_ELLIPSIS);
        text_at(dc, g_small, RGB(72, 84, 102), 226, y + 150, 170, 48,
                L"Faster than Wi-Fi: in VitaShell press START, pick USB, then pick the card here.", DT_WORDBREAK);
        HPEN p = CreatePen(PS_SOLID, 1, RGB(214, 221, 231)); HGDIOBJ op = SelectObject(dc, p);
        MoveToEx(dc, S(404), S(y + 152), NULL); LineTo(dc, S(404), S(y + 184));
        SelectObject(dc, op); DeleteObject(p);
        text_at(dc, g_small, RGB(96, 106, 122), 412, y + 186, CR - 412, 16, L"Converted data and logs",
                DT_SINGLELINE | DT_CENTER);
    }
    if (g) GdipDeleteGraphics(g);
}

/* the owner-drawn buttons: rounded, a glyph before the text; Convert is the blue one */
static wchar_t btn_glyph(int id) {
    switch (id) {
    case ID_BROWSE: case ID_XPBROWSE0: case ID_XPBROWSE1: return 0xE8B7;
    case ID_SEND: return 0xE701;
    case ID_COPY: return 0xE7F1;
    case ID_OPEN: return 0xE838;
    case ID_IMPORT: return 0xE898;
    case ID_CONVERT: return 0xF5B0;
    }
    return 0;
}
static void draw_button(const DRAWITEMSTRUCT *d) {
    HDC dc = d->hDC; RECT r = d->rcItem;
    int primary = d->CtlID == ID_CONVERT;
    int dis = (d->itemState & ODS_DISABLED) != 0, down = (d->itemState & ODS_SELECTED) != 0;
    FillRect(dc, &r, g_white);
    void *g = gp_begin(dc);
    float k = 96.0f / g_dpi;   /* gp_box works in 96-dpi units */
    int w = (int)((r.right - r.left) * k + 0.5f), h = (int)((r.bottom - r.top) * k + 0.5f);
    if (primary)
        gp_box(g, 0, 0, w, h, 8, dis ? ARGB(255, 157, 183, 227) : down ? ARGB(255, 21, 84, 184) : ARGB(255, 26, 102, 224), 0, 0);
    else
        gp_box(g, 0, 0, w, h, 6, down ? ARGB(255, 236, 241, 247) : ARGB(255, 255, 255, 255),
               dis ? ARGB(255, 222, 227, 234) : ARGB(255, 190, 200, 214), 1.0f);
    if (g) GdipDeleteGraphics(g);
    wchar_t t[64]; GetWindowTextW(d->hwndItem, t, 64);
    wchar_t gl = btn_glyph((int)d->CtlID);
    HFONT f = primary ? g_btnbig : g_btnf;
    COLORREF c = primary ? RGB(255, 255, 255) : dis ? RGB(160, 168, 180) : RGB(24, 36, 56);
    int tw = text_w(dc, f, t), gw = gl ? S(primary ? 18 : 16) + S(10) : 0;
    int x = (r.left + r.right - tw - gw) / 2;
    SetBkMode(dc, TRANSPARENT);
    if (gl) {
        wchar_t s[2] = { gl, 0 };
        RECT gr = { x, r.top, x + S(primary ? 18 : 16), r.bottom };
        SelectObject(dc, g_glyph_s); SetTextColor(dc, primary ? c : dis ? c : RGB(40, 52, 70));
        DrawTextW(dc, s, 1, &gr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    RECT tr = { x + gw, r.top, r.right, r.bottom };
    SelectObject(dc, f); SetTextColor(dc, c);
    DrawTextW(dc, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
        RECT fr = r; InflateRect(&fr, -S(4), -S(4)); DrawFocusRect(dc, &fr);
    }
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE: on_browse(); g_chk_res = 0; game_state_update(); return 0;
        case ID_CONVERT: on_convert(); return 0;
        case ID_SEND: on_send(); return 0;
        case ID_COPY: on_copy(); return 0;
        case ID_CHECK: on_check(); return 0;
        case ID_IMPORT: on_import(); return 0;
        case ID_OPEN: ShellExecuteA(h, "open", g_out, NULL, NULL, SW_SHOWNORMAL); return 0;
        case ID_XPBROWSE0: case ID_XPBROWSE1: on_xp_browse(LOWORD(wp) - ID_XPBROWSE0); return 0;
        }
        if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) {   /* the focused frame turns blue */
            g_wnd_focus_ed = HIWORD(wp) == EN_SETFOCUS ? (HWND)lp : NULL;
            for (int i = 0; i < g_nframes; i++)
                if (g_frames[i].ed == (HWND)lp) inval(g_frames[i].x - 2, g_frames[i].y - 2, g_frames[i].w + 4, g_frames[i].h + 4);
            if (HIWORD(wp) == EN_KILLFOCUS && (LOWORD(wp) == ID_XPDIR0 || LOWORD(wp) == ID_XPDIR1)) xp_status_update();
            return 0;
        }
        if (HIWORD(wp) == EN_CHANGE && LOWORD(wp) == ID_GAME) { g_chk_res = 0; game_state_update(); return 0; }
        if (HIWORD(wp) == EN_CHANGE && (LOWORD(wp) == ID_XPDIR0 || LOWORD(wp) == ID_XPDIR1)) { xp_status_update(); return 0; }
        break;
    case WM_TIMER: on_timer(); return 0;
    case WM_DRAWITEM: draw_button((const DRAWITEMSTRUCT *)lp); return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT cr; GetClientRect(h, &cr);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bm = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        HGDIOBJ ob = SelectObject(mem, bm);
        paint_all(mem, cr.right, cr.bottom);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
               mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bm); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: {   /* the edit boxes and the check boxes sit on white */
        HDC dc = (HDC)wp; SetBkColor(dc, RGB(255, 255, 255));
        if (msg == WM_CTLCOLORSTATIC && !IsWindowEnabled((HWND)lp)) SetTextColor(dc, RGB(150, 158, 170));
        else SetTextColor(dc, RGB(24, 36, 56));
        return (LRESULT)g_white;
    }
    case WM_CLOSE:
        if ((g_job.busy || g_send.busy || g_chk.busy) &&
            MessageBoxA(h, g_job.busy ? "The conversion is still running. Quit anyway? (The track in progress "
                                        "starts over next time.)"
                                      : g_chk.busy ? "Still checking the game files. Quit anyway?"
                                      : "Still sending to the Vita. Quit anyway? (Send again later: files already "
                                        "there are skipped.)",
                        TOOL_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static HFONT font(int pt, int weight) {
    return CreateFontA(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                       0, "Segoe UI");
}
static HFONT font_px(int px, int weight, const char *face) {
    return CreateFontA(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}
/* the icon font: Segoe Fluent Icons (Windows 11), else Segoe MDL2 Assets (Windows 10) */
static const char *icon_face(void) {
    static char face[40];
    if (face[0]) return face;
    HDC dc = GetDC(NULL);
    HFONT f = CreateFontA(-16, 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe Fluent Icons");
    HGDIOBJ o = SelectObject(dc, f);
    char got[40] = ""; GetTextFaceA(dc, sizeof got, got);
    SelectObject(dc, o); DeleteObject(f); ReleaseDC(NULL, dc);
    snprintf(face, sizeof face, "%s", !_stricmp(got, "Segoe Fluent Icons") ? got : "Segoe MDL2 Assets");
    return face;
}
static void make_fonts(void) {
    g_font = font(9, FW_NORMAL); g_bold = font(9, FW_SEMIBOLD); g_title = font(19, FW_SEMIBOLD);
    g_small = font(8, FW_NORMAL); g_head = font(12, FW_SEMIBOLD); g_sub = font(9, FW_NORMAL);
    g_btnf = font(9, FW_NORMAL); g_btnbig = font(11, FW_SEMIBOLD);
    g_glyph = font_px(26, FW_NORMAL, icon_face()); g_glyph_s = font_px(14, FW_NORMAL, icon_face());
    g_glyph_l = font_px(38, FW_NORMAL, icon_face());
    g_white = CreateSolidBrush(RGB(255, 255, 255));
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)cmd;
    if (__argc >= 4 && !strcmp(__argv[1], "--cli")) {   /* GAME OUT: no window, log in OUT */
        snprintf(g_job.game, sizeof g_job.game, "%s", __argv[2]);
        snprintf(g_out, sizeof g_out, "%s", __argv[3]);
        CreateDirectoryA(g_out, NULL);
        if (!game_dir_fix(g_job.game, sizeof g_job.game)) return 2;
        xpp_set_out(g_out);
        for (int c = 0; c < XPP_NCITY && 4 + c < __argc; c++) {   /* MW, CARBON ("-" = none) */
            char d[MAX_PATH]; snprintf(d, sizeof d, "%s", __argv[4 + c]);
            if (!strcmp(d, "-")) continue;
            if (!xpp_dir_fix(c, d)) { fprintf(stderr, "no %s folder at %s\n", xpp_game_name(c), __argv[4 + c]); return 2; }
            xpp_set_dir(c, d);
        }
        convert_all();
        return g_job.ok ? 0 : 1;
    }
    if (__argc >= 5 && !strcmp(__argv[1], "--import-save")) {   /* SAVE GAME OUT */
        char game[MAX_PATH], m[768];
        snprintf(game, sizeof game, "%s", __argv[3]);
        snprintf(g_out, sizeof g_out, "%s", __argv[4]);
        if (!game_dir_fix(game, sizeof game)) { fprintf(stderr, "no NFSU2 game folder at %s\n", __argv[3]); return 2; }
        int ok = import_save(__argv[2], game, m, sizeof m);
        fprintf(stderr, "%s\n", m);
        return ok ? 0 : 1;
    }
    if (__argc >= 3 && !strcmp(__argv[1], "--check")) {   /* GAME: 0 = all match, 1 = problems */
        char game[MAX_PATH];
        snprintf(game, sizeof game, "%s", __argv[2]);
        if (!game_dir_fix(game, sizeof game)) { fprintf(stderr, "no NFSU2 game folder at %s\n", __argv[2]); return 2; }
        int ok = check_game(game);
        fprintf(stderr, "%s\n", g_chk.msg);
        return ok ? 0 : 1;
    }
    if (__argc >= 4 && (!strcmp(__argv[1], "--send") || !strcmp(__argv[1], "--copy"))) {   /* IP|DEST OUT [GAME] */
        if (!strcmp(__argv[1], "--send")) snprintf(g_send.ip, sizeof g_send.ip, "%s", __argv[2]);
        else { snprintf(g_send.dest, sizeof g_send.dest, "%s", __argv[2]); dest_fix(g_send.dest); }
        snprintf(g_out, sizeof g_out, "%s", __argv[3]);
        if (__argc >= 5) {
            snprintf(g_send.game, sizeof g_send.game, "%s", __argv[4]);
            if (!game_dir_fix(g_send.game, sizeof g_send.game)) { fprintf(stderr, "no NFSU2 game folder at %s\n", __argv[4]); return 2; }
        }
        conv_set_folders("", g_out);
        xpp_set_out(g_out);
        g_send.xp = 1;   /* the World Expansion cities converted in OUT, if any */
        send_all();
        fprintf(stderr, "%s\n", g_send.msg);
        return g_send.ok ? 0 : 1;
    }
    out_dir_init();
    SetProcessDPIAware();
    HDC sdc = GetDC(NULL); g_dpi = GetDeviceCaps(sdc, LOGPIXELSY); ReleaseDC(NULL, sdc);
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    {   /* a screen too short for the window (a 1080p laptop at 150%): scale it down to fit */
        RECT wa; int need = CLIENT_H + 40;   /* the client height + the caption */
        if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0) && MulDiv(need, g_dpi, 96) > wa.bottom - wa.top)
            g_dpi = MulDiv(wa.bottom - wa.top, 96, need);
    }
    { ULONG_PTR tok; GpStartupIn in = { 1, NULL, FALSE, FALSE }; GdiplusStartup(&tok, &in, NULL); }
    make_fonts();

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = "NFSU2VitaPrep";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = NULL;
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    RegisterClassA(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT r = { 0, 0, S(CW), S(CLIENT_H) };
    AdjustWindowRect(&r, style, FALSE);
    g_wnd = CreateWindowA("NFSU2VitaPrep", TOOL_NAME, style, CW_USEDEFAULT, CW_USEDEFAULT,
                          r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    build_ui();

    char v[MAX_PATH];
    setting_get("GameDir", v, sizeof v);
    if (!v[0] || !game_dir_fix(v, sizeof v)) game_dir_detect(v, sizeof v);
    SetWindowTextA(g_game_ed, v);
    setting_get("VitaIP", v, sizeof v);
    SetWindowTextA(g_ip_ed, v);
    setting_get("GameFiles", v, sizeof v);
    SendMessageA(g_gamefiles, BM_SETCHECK, strcmp(v, "0") ? BST_CHECKED : BST_UNCHECKED, 0);
    for (int c = 0; c < XPP_NCITY; c++) {   /* the World Expansion: as last time, else where the installer put it */
        const char *name = c ? "CarbonDir" : "MWDir";
        if (!setting_get(name, v, sizeof v) &&
            !setting_get_key("Software\\NFSU2Vita\\ExpansionPrep", name, v, sizeof v))
            xpp_dir_detect(c, v, sizeof v);
        SetWindowTextA(g_xp_ed[c], v);
        if (v[0] && xpp_dir_fix(c, v)) xpp_set_dir(c, v);
    }
    setting_get("XpFiles", v, sizeof v);
    SendMessageA(g_xpfiles, BM_SETCHECK, strcmp(v, "0") ? BST_CHECKED : BST_UNCHECKED, 0);
    xpp_set_out(g_out);
    conv_set_folders("", g_out);   /* track_done reads the output's cache */
    xp_status_update();
    game_state_update();
    ui_initial_status();
    ui_idle();
    SetTimer(g_wnd, ID_TIMER, 200, NULL);
    ShowWindow(g_wnd, show);

    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (IsDialogMessageA(g_wnd, &m)) continue;
        TranslateMessage(&m); DispatchMessageA(&m);
    }
    return 0;
}
