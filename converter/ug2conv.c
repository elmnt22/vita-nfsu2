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
 *   NFSU2VitaPrep.exe --cli GAME OUT           check + convert without the window
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
    char msg[2048];
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
static void setting_get(const char *name, char *out, DWORD cap);
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
    char msg[512];
} g_send;

#define SEND_CAP (NGAME + 5 * NTRACKS + 64 + 16)
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
        if (!f) return;
        do {
            if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || g_nrent >= 512) continue;
            snprintf(g_rent[g_nrent].name, sizeof g_rent[0].name, "%s", d.cFileName);
            g_rent[g_nrent++].size = ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
        } while (InternetFindNextFileA(f, &d));
        InternetCloseHandle(f);
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

/* every folder on the way to the file (made once each) */
static void make_dirs(HINTERNET c, const char *remote) {
    static char made[256];
    char dir[256]; snprintf(dir, sizeof dir, "%s", remote);
    char *s = strrchr(dir, '/'); if (!s) return;
    *s = 0;
    if (!strcmp(dir, made)) return;
    snprintf(made, sizeof made, "%s", dir);
    if (!c) { char p[MAX_PATH + 300]; dest_path(p, sizeof p, dir); SHCreateDirectoryExA(NULL, p, NULL); return; }
    for (char *q = dir + 5; (q = strchr(q, '/')) != NULL; q++) {   /* past "ux0:/" */
        *q = 0; FtpCreateDirectoryA(c, dir); *q = '/';   /* already there is fine */
    }
    FtpCreateDirectoryA(c, dir);
}

static int put_file(HINTERNET c, const SendFile *f, char *buf, DWORD bufsz) {
    HANDLE h = CreateFileA(f->local, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    HINTERNET r = NULL; HANDLE o = INVALID_HANDLE_VALUE;
    if (c) r = FtpOpenFileA(c, f->remote, GENERIC_WRITE, FTP_TRANSFER_TYPE_BINARY, 0);
    else { char p[MAX_PATH + 300]; dest_path(p, sizeof p, f->remote);
           o = CreateFileA(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); }
    int ok = c ? r != NULL : o != INVALID_HANDLE_VALUE;
    DWORD got;
    while (ok && ReadFile(h, buf, bufsz, &got, NULL) && got) {
        for (DWORD off = 0; ok && off < got; ) {
            DWORD w = 0;
            ok = (c ? InternetWriteFile(r, buf + off, got - off, &w) : WriteFile(o, buf + off, got - off, &w, NULL)) && w;
            off += w; g_send.sent += w;
        }
    }
    CloseHandle(h);
    if (r && !InternetCloseHandle(r)) ok = 0;
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
    g_rdir[0] = 0;
    if (!to_dir && !c) {
        ok = 0;
        snprintf(g_send.msg, sizeof g_send.msg, "Could not reach the Vita at %s. Is its FTP server on "
                 "(VitaShell: press SELECT) and the IP right?", g_send.ip);
    } else if (ok) {
        /* the old indexes go first: a set cut off half way reads as missing
           (and the Vita converts it) instead of as whole */
        for (int i = 0; i < n; i++)
            if (strstr(files[i].remote, ".idx")) {
                if (c) FtpDeleteFileA(c, files[i].remote);
                else { char p[MAX_PATH + 300]; dest_path(p, sizeof p, files[i].remote); DeleteFileA(p); }
            }
        static char buf[256 << 10];
        for (int i = 0; i < n && ok; i++) {
            g_send.file = i;
            if (already_there(c, &files[i])) { g_send.sent += files[i].size; g_send.skipped++; continue; }
            make_dirs(c, files[i].remote);
            ok = put_file(c, &files[i], buf, sizeof buf);
            if (ok) moved += files[i].size;
            if (!ok) snprintf(g_send.msg, sizeof g_send.msg, to_dir
                              ? "Copying %s failed. Is the memory card still connected, with enough free space?"
                              : "Sending %s failed. Check the Wi-Fi and the free space on the memory card, and try again "
                                "(files already sent are skipped).", files[i].remote + 5);
        }
        char skip[96] = "";
        if (g_send.skipped) snprintf(skip, sizeof skip, " (%d game file%s already there, skipped)", g_send.skipped,
                                     g_send.skipped == 1 ? "" : "s");
        if (ok) snprintf(g_send.msg, sizeof g_send.msg, "%s %.0f MB%s%s. Start NFS Underground 2%s.",
                         to_dir ? "Copied" : "Sent", moved / 1048576.0,
                         to_dir ? "" : " to the Vita", skip,
                         track_done(0) ? " -- it skips \"Optimizing world data\"" : "");
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
static void setting_get(const char *name, char *out, DWORD cap) {
    if (RegGetValueA(HKEY_CURRENT_USER, REG_KEY, name, RRF_RT_REG_SZ, NULL, out, &cap) != ERROR_SUCCESS) out[0] = 0;
}
static void setting_put(const char *name, const char *v) {
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)strlen(v) + 1);
    RegCloseKey(k);
}

/* ---- the window ----------------------------------------------------------- */
enum { ID_GAME = 100, ID_BROWSE, ID_CONVERT, ID_PROGRESS, ID_STATUS, ID_IP, ID_SEND, ID_OPEN, ID_TIMER, ID_IMPORT,
       ID_CHECK, ID_GAMEFILES, ID_COPY };
static HWND g_game_ed, g_browse, g_convert, g_bar, g_status, g_ip_ed, g_send_btn, g_open, g_import,
            g_check, g_gamefiles, g_copy;
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
static void ui_idle(void) {
    int busy = g_job.busy || g_send.busy || g_chk.busy;
    EnableWindow(g_convert, !busy); EnableWindow(g_browse, !busy); EnableWindow(g_game_ed, !busy);
    EnableWindow(g_check, !busy);
    EnableWindow(g_send_btn, !busy); EnableWindow(g_copy, !busy); EnableWindow(g_ip_ed, !busy);
    EnableWindow(g_gamefiles, !busy);
    EnableWindow(g_import, !busy);
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
static void on_convert(void) {
    char game[MAX_PATH]; GetWindowTextA(g_game_ed, game, sizeof game);
    if (!game_dir_fix(game, sizeof game)) {
        MessageBoxA(g_wnd, "Pick your Need for Speed Underground 2 folder first (it has a TRACKS folder in it).",
                    TOOL_NAME, MB_ICONWARNING);
        return;
    }
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
    check_game((const char *)a);
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
    if (g_chk.busy || (g_job.busy && g_job.checking)) {
        long long t = g_chk.total > 0 ? g_chk.total : 1;
        SendMessageA(g_bar, PBM_SETPOS, (WPARAM)(g_chk.bytes * 1000 / t), 0);
        snprintf(m, sizeof m, "Checking your game files: %d%%", (int)(g_chk.bytes * 100 / t));
        set_status(m);
    } else if (g_chk.done) {
        g_chk.done = 0;
        SendMessageA(g_bar, PBM_SETPOS, 1000, 0);
        set_status(g_chk.ok ? "Game files OK." : "Some game files are missing or changed.");
        ui_idle();
        MessageBoxA(g_wnd, g_chk.msg, TOOL_NAME, g_chk.ok ? MB_ICONINFORMATION : MB_ICONWARNING);
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
        ui_idle();
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
       0, g_font); y += 38;

    mk("STATIC", "2.  Convert", 0, 20, y, 500, 18, 0, g_bold); y += 22;
    g_convert = mk("BUTTON", "Convert", BS_DEFPUSHBUTTON, 20, y, 140, 30, ID_CONVERT, g_bold);
    mk("STATIC", "About 2 minutes. Your game files are only read, never changed.", 0, 172, y + 7, 348, 18, 0, g_font); y += 40;
    g_bar = mk(PROGRESS_CLASSA, "", 0, 20, y, 500, 16, ID_PROGRESS, g_font);
    SendMessageA(g_bar, PBM_SETRANGE32, 0, 1000); y += 22;
    g_status = mk("STATIC", "", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 20, y, 500, 18, ID_STATUS, g_font); y += 36;

    mk("STATIC", "3.  Put it on your Vita", 0, 20, y, 500, 18, 0, g_bold); y += 24;
    g_gamefiles = mk("BUTTON", "Game files too (about 1.7 GB the first time; files already there are skipped)",
                     BS_AUTOCHECKBOX, 20, y, 500, 20, ID_GAMEFILES, g_font); y += 28;
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
        }
        break;
    case WM_TIMER: on_timer(); return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp; SetBkMode(dc, TRANSPARENT);
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
    g_font = font(9, FW_NORMAL); g_bold = font(9, FW_SEMIBOLD); g_title = font(14, FW_SEMIBOLD);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = "NFSU2VitaPrep";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    RegisterClassA(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, S(540), S(640) };
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
    conv_set_folders("", g_out);   /* track_done reads the output's cache */
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
