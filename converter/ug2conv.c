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
typedef struct { char local[MAX_PATH + 160], remote[256]; long long size; int game; } SendFile;
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
    for (int i = 0; i < n; i++) f[i].size = file_size(f[i].local);
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
        /* the old indexes go first: a set cut off half way reads as missing
           (and the Vita converts it) instead of as whole */
        for (int i = 0; i < n; i++)   /* (and the World Expansion's .xp markers) */
            if (strstr(files[i].remote, ".idx") || strstr(files[i].remote, ".xp")) {
                if (c) FtpDeleteFileA(c, files[i].remote);
                else { char p[MAX_PATH + 300]; dest_path(p, sizeof p, files[i].remote); DeleteFileA(p); }
            }
        static char buf[256 << 10];
        for (int i = 0; i < n && ok; i++) {
            g_send.file = i;
            if (already_there(c, &files[i])) { g_send.sent += files[i].size; g_send.skipped++; continue; }
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
            if (ok) moved += files[i].size;
            if (!ok) snprintf(g_send.msg, sizeof g_send.msg, to_dir
                              ? "Copying %s failed: %s. Is the memory card still connected, with enough free space?"
                              : "Sending %s failed: %s. Check the Wi-Fi, keep the Vita awake (Settings > Power Save: "
                                "auto-standby off), and try again (files already sent are skipped).",
                              files[i].remote + 5, g_why[0] ? g_why : "unknown reason");
        }
        char skip[96] = "";
        if (g_send.skipped) snprintf(skip, sizeof skip, " (%d game file%s already there, skipped)", g_send.skipped,
                                     g_send.skipped == 1 ? "" : "s");
        if (ok) snprintf(g_send.msg, sizeof g_send.msg, "%s %.0f MB%s%s. Start NFS Underground 2%s.%s",
                         to_dir ? "Copied" : "Sent", moved / 1048576.0,
                         to_dir ? "" : " to the Vita", skip,
                         track_done(0) ? " -- it skips \"Optimizing world data\"" : "",
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
static HWND g_game_ed, g_browse, g_convert, g_bar, g_status, g_ip_ed, g_send_btn, g_open, g_import,
            g_check, g_gamefiles, g_copy;
static HWND g_xp_ed[XPP_NCITY], g_xp_browse[XPP_NCITY], g_xp_status, g_xpfiles;   /* the World Expansion */
static HFONT g_font, g_bold, g_title;
static int g_dpi = 96;
static int S(int v) { return MulDiv(v, g_dpi, 96); }

static HWND mk(const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id, HFONT f) {
    if (strcmp(cls, "STATIC") && strcmp(cls, PROGRESS_CLASSA)) style |= WS_TABSTOP;
    HWND c = CreateWindowExA(!strcmp(cls, "EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             S(x), S(y), S(w), S(h), g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}
static void set_status(const char *s) { SetWindowTextA(g_status, s); }
/* the result label beside the progress bar: 0 blank, 1 working, 2 done, 3 failed */
static HWND g_done; static int g_done_state;
static void set_done(int st) {
    if (st == g_done_state) return;
    g_done_state = st;
    static const char *t[4] = { "", "Working...", "Done!", "Failed" };
    SetWindowTextA(g_done, t[st]);
    InvalidateRect(g_done, NULL, TRUE);
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
/* the World Expansion line: each city's state, from its folder box and the output */
static void xp_status_update(void) {
    char m[300] = "", dir[MAX_PATH];
    for (int c = 0; c < XPP_NCITY; c++) {
        GetWindowTextA(g_xp_ed[c], dir, sizeof dir);
        const char *st;
        if (xpp_city_done(c)) st = "ready to send";
        else if (!dir[0]) st = "not set";
        else if (xpp_dir_fix(c, dir)) st = "found -- press Convert";
        else st = "folder not right";
        size_t n = strlen(m);
        snprintf(m + n, sizeof m - n, "%s%s: %s", c ? "      " : "", xpp_city_name(c), st);
    }
    SetWindowTextA(g_xp_status, m);
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
    int y = 14;
    mk("STATIC", TOOL_NAME, 0, 20, y, 500, 28, 0, g_title); y += 32;
    mk("STATIC", "Gets your NFSU2 PC files ready for the Vita: checks them, does the Vita's one-time "
       "\"Optimizing world data\" step here, and puts everything on the Vita.", 0, 20, y, 500, 34, 0, g_font); y += 46;

    mk("STATIC", "1.  Your Need for Speed Underground 2 folder", 0, 20, y, 500, 18, 0, g_bold); y += 22;
    g_game_ed = mk("EDIT", "", ES_AUTOHSCROLL, 20, y, 400, 24, ID_GAME, g_font);
    g_browse = mk("BUTTON", "Browse...", BS_PUSHBUTTON, 428, y, 92, 24, ID_BROWSE, g_font); y += 30;
    g_check = mk("BUTTON", "Check game files", BS_PUSHBUTTON, 20, y, 140, 24, ID_CHECK, g_font);
    mk("STATIC", "Makes sure your copy is complete and unmodified.", 0, 170, y + 4, 350, 18,
       0, g_font); y += 36;

    mk("STATIC", "Optional:  World Expansion -- Rockport and Palmont", 0, 20, y, 500, 18, 0, g_bold); y += 21;
    mk("STATIC", "Have Need for Speed Most Wanted (2005) or Carbon on your PC? Pick its folder to drive its "
       "city in Free Run too. Leave a box empty to skip it.", 0, 20, y, 500, 34, 0, g_font); y += 38;
    for (int c = 0; c < XPP_NCITY; c++) {
        mk("STATIC", c ? "Carbon:" : "Most Wanted:", 0, 20, y + 4, 90, 18, 0, g_font);
        g_xp_ed[c] = mk("EDIT", "", ES_AUTOHSCROLL, 112, y, 308, 24, ID_XPDIR0 + c, g_font);
        g_xp_browse[c] = mk("BUTTON", "Browse...", BS_PUSHBUTTON, 428, y, 92, 24, ID_XPBROWSE0 + c, g_font); y += 28;
    }
    g_xp_status = mk("STATIC", "", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 20, y + 1, 500, 18, ID_XPSTATUS, g_font); y += 32;

    mk("STATIC", "2.  Convert", 0, 20, y, 500, 18, 0, g_bold); y += 22;
    g_convert = mk("BUTTON", "Convert", BS_DEFPUSHBUTTON, 20, y, 140, 30, ID_CONVERT, g_bold);
    mk("STATIC", "About 2 minutes (longer with the World Expansion). Your game files are only read, never "
       "changed.", 0, 172, y - 1, 348, 34, 0, g_font); y += 40;
    g_bar = mk(PROGRESS_CLASSA, "", 0, 20, y, 400, 16, ID_PROGRESS, g_font);
    g_done = mk("STATIC", "", SS_CENTER, 428, y - 3, 92, 22, 0, g_bold);
    SendMessageA(g_bar, PBM_SETRANGE32, 0, 1000); y += 22;
    g_status = mk("STATIC", "", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 20, y, 500, 18, ID_STATUS, g_font); y += 36;

    mk("STATIC", "3.  Put it on your Vita", 0, 20, y, 500, 18, 0, g_bold); y += 24;
    g_gamefiles = mk("BUTTON", "Game files too (1.7 GB the first time, then skipped)",
                     BS_AUTOCHECKBOX, 20, y, 300, 20, ID_GAMEFILES, g_font);
    g_xpfiles = mk("BUTTON", "World Expansion too", BS_AUTOCHECKBOX, 330, y, 190, 20, ID_XPFILES, g_font); y += 28;
    mk("STATIC", "Vita IP address:", 0, 20, y + 4, 104, 18, 0, g_font);
    g_ip_ed = mk("EDIT", "", ES_AUTOHSCROLL, 126, y, 150, 24, ID_IP, g_font);
    g_send_btn = mk("BUTTON", "Send to Vita", BS_PUSHBUTTON, 284, y, 120, 24, ID_SEND, g_font); y += 30;
    mk("STATIC", "Open VitaShell on the Vita and press SELECT to see its IP (vitacompanion works too). "
       "Close NFS Underground 2 on the Vita first.", 0, 20, y, 500, 34, 0, g_font); y += 42;
    g_copy = mk("BUTTON", "Copy to memory card...", BS_PUSHBUTTON, 20, y, 140, 24, ID_COPY, g_font);
    mk("STATIC", "Faster than Wi-Fi: in VitaShell press START, pick USB, then pick the memory card's "
       "drive here.", 0, 170, y - 2, 350, 34, 0, g_font); y += 36;
    g_open = mk("BUTTON", "Open output folder", BS_PUSHBUTTON, 20, y, 140, 24, ID_OPEN, g_font);
    mk("STATIC", "The converted data and the logs.", 0, 170, y + 4, 350, 18, 0, g_font); y += 44;

    mk("STATIC", "Optional:  bring your PC career", 0, 20, y, 500, 18, 0, g_bold); y += 24;
    g_import = mk("BUTTON", "Import PC save...", BS_PUSHBUTTON, 20, y, 140, 24, ID_IMPORT, g_font);
    mk("STATIC", "Turns a profile of the PC game into a Vita profile (career, cars, options, "
       "high scores). Sending or copying takes it along.", 0, 170, y - 2, 350, 34, 0, g_font);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE: on_browse(); return 0;
        case ID_CONVERT: on_convert(); return 0;
        case ID_SEND: on_send(); return 0;
        case ID_COPY: on_copy(); return 0;
        case ID_CHECK: on_check(); return 0;
        case ID_IMPORT: on_import(); return 0;
        case ID_OPEN: ShellExecuteA(h, "open", g_out, NULL, NULL, SW_SHOWNORMAL); return 0;
        case ID_XPBROWSE0: case ID_XPBROWSE1: on_xp_browse(LOWORD(wp) - ID_XPBROWSE0); return 0;
        case ID_XPDIR0: case ID_XPDIR1: if (HIWORD(wp) == EN_KILLFOCUS) xp_status_update(); return 0;
        }
        break;
    case WM_TIMER: on_timer(); return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp; SetBkMode(dc, TRANSPARENT);
        if ((HWND)lp == g_done)
            SetTextColor(dc, g_done_state == 2 ? RGB(0, 140, 40) : g_done_state == 3 ? RGB(200, 0, 0) : RGB(110, 110, 110));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
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
        RECT wa; int need = 764 + 40;   /* the client height + the caption */
        if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0) && MulDiv(need, g_dpi, 96) > wa.bottom - wa.top)
            g_dpi = MulDiv(wa.bottom - wa.top, 96, need);
    }
    g_font = font(9, FW_NORMAL); g_bold = font(9, FW_SEMIBOLD); g_title = font(14, FW_SEMIBOLD);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = "NFSU2VitaPrep";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    RegisterClassA(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, S(540), S(764) };
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
