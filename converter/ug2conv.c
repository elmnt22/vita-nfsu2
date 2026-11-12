/* ug2conv.c -- OpenUG2 World Data Converter (Windows).
 *
 * The Vita build converts every track's world into prebuilt tiles once, on
 * the first boot ("OPTIMIZING WORLD DATA", 15-20+ minutes on the console).
 * This tool runs that same conversion -- the game's own world_tiles.c and
 * resource.c, built 32-bit like the Vita -- on a PC from the player's NFSU2
 * folder, then copies the result to the Vita over FTP (VitaShell or
 * vitacompanion, port 1337) or leaves it in a folder to copy by hand. The
 * Vita finds the tile sets and compact bundles in ux0:data/NFSU2/cache and
 * skips its own conversion.
 *
 *   UG2Converter.exe                      the window
 *   UG2Converter.exe --cli GAME OUT       convert without the window
 *   UG2Converter.exe --send IP OUT        send OUT's converted tracks to the Vita
 *   UG2Converter.exe --import-save SAVE GAME OUT
 *                                         a PC save (%LOCALAPPDATA%\NFS Underground 2\
 *                                         <NAME>\<NAME>) into OUT\profiles\<NAME>\<NAME>
 *                                         (pcsave.c), sent with the rest
 */
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
            game_dir_fix(out, cap)) return 1;
    }
    GetModuleFileNameA(NULL, out, (DWORD)cap);   /* the tool dropped into the game folder */
    char *s = strrchr(out, '\\'); if (s) *s = 0;
    if (game_dir_fix(out, cap)) return 1;
    out[0] = 0;
    return 0;
}

/* Output: "NFSU2 Vita cache" next to the tool; Documents if that is read-only. */
static char g_out[MAX_PATH];
static void out_dir_init(void) {
    GetModuleFileNameA(NULL, g_out, sizeof g_out);
    char *s = strrchr(g_out, '\\'); if (s) *s = 0;
    char probe[MAX_PATH + 32];
    snprintf(probe, sizeof probe, "%s\\NFSU2 Vita cache", g_out);
    CreateDirectoryA(probe, NULL);
    DWORD a = GetFileAttributesA(probe);
    char test[MAX_PATH + 48]; snprintf(test, sizeof test, "%s\\.write_test", probe);
    HANDLE h = a != INVALID_FILE_ATTRIBUTES ? CreateFileA(test, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                                          FILE_FLAG_DELETE_ON_CLOSE, NULL) : INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); snprintf(g_out, sizeof g_out, "%s", probe); return; }
    char docs[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, docs) != S_OK) snprintf(docs, sizeof docs, ".");
    snprintf(g_out, sizeof g_out, "%s\\NFSU2 Vita cache", docs);
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
} g_job;

void conv_fatal(const char *what) {
    printf("converter: unexpected call to %s -- stopping\n", what);
    fflush(stdout);
    char m[256]; snprintf(m, sizeof m, "The conversion reached code it does not support (%s).\n"
                          "Please report this. Nothing was copied to your Vita.", what);
    MessageBoxA(NULL, m, "NFSU2 Vita Converter", MB_ICONERROR);
    ExitProcess(3);
}

static void convert_all(void) {
    conv_set_folders(g_job.game, g_out);
    char p[MAX_PATH + 64];
    snprintf(p, sizeof p, "%s\\cache", g_out); CreateDirectoryA(p, NULL);
    snprintf(p, sizeof p, "%s\\convert_log.txt", g_out);
    freopen(p, "w", stdout);
    setvbuf(stdout, NULL, _IOLBF, 4096);
    printf("NFSU2 Vita converter: game folder %s, output %s\n", g_job.game, g_out);
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
typedef struct { char local[MAX_PATH + 96], remote[256]; long long size; } SendFile;
static struct {
    volatile int busy, done, ok;
    volatile long long sent, total;
    volatile int file, nfiles;
    char ip[64];
    char msg[512];
} g_send;

#define SEND_CAP (5 * NTRACKS + 64 + 16)
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
static int send_list(SendFile *f, int cap) {
    int n = 0;
    char name[128];
    /* per track: the compact bundle, its stripped copy and start, the tile
       pack, then the index -- its presence tells the game the set is whole,
       so it goes last */
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

static int ftp_put(HINTERNET c, const SendFile *f, char *buf, DWORD bufsz) {
    HANDLE h = CreateFileA(f->local, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    HINTERNET r = FtpOpenFileA(c, f->remote, GENERIC_WRITE, FTP_TRANSFER_TYPE_BINARY, 0);
    int ok = r != NULL;
    DWORD got;
    while (ok && ReadFile(h, buf, bufsz, &got, NULL) && got) {
        for (DWORD off = 0; ok && off < got; ) {
            DWORD w = 0;
            ok = InternetWriteFile(r, buf + off, got - off, &w) && w;
            off += w; g_send.sent += w;
        }
    }
    CloseHandle(h);
    if (r && !InternetCloseHandle(r)) ok = 0;
    return ok;
}

static void send_all(void) {
    static SendFile files[SEND_CAP];
    int n = send_list(files, SEND_CAP);
    long long total = 0;
    for (int i = 0; i < n; i++) total += files[i].size;
    g_send.total = total; g_send.sent = 0; g_send.nfiles = n;
    int ok = n > 0;
    HINTERNET net = InternetOpenA("NFSU2 Vita Converter", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    HINTERNET c = net ? InternetConnectA(net, g_send.ip, FTP_PORT, NULL, NULL, INTERNET_SERVICE_FTP,
                                         INTERNET_FLAG_PASSIVE, 0) : NULL;
    if (!c) {
        ok = 0;
        snprintf(g_send.msg, sizeof g_send.msg, "Could not reach the Vita at %s. Is its FTP server on "
                 "(VitaShell: press SELECT) and the IP right?", g_send.ip);
    } else if (ok) {
        FtpCreateDirectoryA(c, "ux0:/data");   /* already there is fine */
        FtpCreateDirectoryA(c, "ux0:/data/NFSU2");
        FtpCreateDirectoryA(c, VITA_CACHE);
        FtpCreateDirectoryA(c, VITA_CACHE "/tiles");
        for (int i = 0; i < n; i++)
            if (!strncmp(files[i].remote, VITA_PROFILES "/", sizeof VITA_PROFILES)) {
                char dir[256];
                snprintf(dir, sizeof dir, "%s", files[i].remote);
                *strrchr(dir, '/') = 0;
                FtpCreateDirectoryA(c, VITA_PROFILES);
                FtpCreateDirectoryA(c, dir);
            }
        /* the old indexes go first: a set cut off half way reads as missing
           (and the Vita converts it) instead of as whole */
        for (int i = 0; i < n; i++)
            if (strstr(files[i].remote, ".idx")) FtpDeleteFileA(c, files[i].remote);
        static char buf[256 << 10];
        for (int i = 0; i < n && ok; i++) {
            g_send.file = i;
            ok = ftp_put(c, &files[i], buf, sizeof buf);
            if (!ok) snprintf(g_send.msg, sizeof g_send.msg, "Sending %s failed. Check the Wi-Fi and try again.",
                              strrchr(files[i].remote, '/') + 1);
        }
        if (ok) snprintf(g_send.msg, sizeof g_send.msg, "Sent %.0f MB to the Vita. Start NFS Underground 2 -- it skips "
                         "\"Optimizing world data\".", total / 1048576.0);
    } else snprintf(g_send.msg, sizeof g_send.msg, "Nothing to send yet: convert first.");
    if (c) InternetCloseHandle(c);
    if (net) InternetCloseHandle(net);
    g_send.ok = ok;
    g_send.done = 1;
    g_send.busy = 0;
}
static DWORD WINAPI send_thread(LPVOID a) { (void)a; send_all(); return 0; }

/* ---- settings ------------------------------------------------------------- */
#define REG_KEY "Software\\NFSU2Vita\\Converter"
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
enum { ID_GAME = 100, ID_BROWSE, ID_CONVERT, ID_PROGRESS, ID_STATUS, ID_IP, ID_SEND, ID_OPEN, ID_TIMER, ID_IMPORT };
static HWND g_wnd, g_game_ed, g_browse, g_convert, g_bar, g_status, g_ip_ed, g_send_btn, g_open, g_import;
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
    int busy = g_job.busy || g_send.busy;
    EnableWindow(g_convert, !busy); EnableWindow(g_browse, !busy); EnableWindow(g_game_ed, !busy);
    EnableWindow(g_send_btn, !busy && (track_done(0) || profiles_present())); EnableWindow(g_ip_ed, !busy);
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
    else set_status("Ready. Press Convert -- it takes about 2 minutes.");
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
                        "installed in (or the folder you copied the game data from).", "NFSU2 Vita Converter", MB_ICONWARNING);
        else SetWindowTextA(g_game_ed, p);
    }
    CoTaskMemFree(id);
    ui_initial_status();
}
static void on_convert(void) {
    char game[MAX_PATH]; GetWindowTextA(g_game_ed, game, sizeof game);
    if (!game_dir_fix(game, sizeof game)) {
        MessageBoxA(g_wnd, "Pick your Need for Speed Underground 2 folder first (it has a TRACKS folder in it).",
                    "NFSU2 Vita Converter", MB_ICONWARNING);
        return;
    }
    SetWindowTextA(g_game_ed, game);
    setting_put("GameDir", game);
    memset(&g_job, 0, sizeof g_job);
    snprintf(g_job.game, sizeof g_job.game, "%s", game);
    g_job.busy = 1;
    SendMessageA(g_bar, PBM_SETPOS, 0, 0);
    set_status("Starting...");
    ui_idle();
    HANDLE h = CreateThread(NULL, 16 << 20, convert_thread, NULL, 0, NULL);
    if (h) CloseHandle(h); else { g_job.busy = 0; set_status("Could not start the conversion."); ui_idle(); }
}
static void on_send(void) {
    char ip[64]; GetWindowTextA(g_ip_ed, ip, sizeof ip);
    char *s = ip; while (*s == ' ') s++;
    char *e = s + strlen(s); while (e > s && e[-1] == ' ') *--e = 0;
    if (!*s) { MessageBoxA(g_wnd, "Type your Vita's IP address (VitaShell shows it when you press SELECT).",
                           "NFSU2 Vita Converter", MB_ICONINFORMATION); return; }
    setting_put("VitaIP", s);
    memset(&g_send, 0, sizeof g_send);
    snprintf(g_send.ip, sizeof g_send.ip, "%s", s);
    g_send.busy = 1;
    SendMessageA(g_bar, PBM_SETPOS, 0, 0);
    set_status("Connecting to the Vita...");
    ui_idle();
    HANDLE h = CreateThread(NULL, 0, send_thread, NULL, 0, NULL);
    if (h) CloseHandle(h); else { g_send.busy = 0; ui_idle(); }
}
/* "Import PC save...": the PC game's profile file (its folder in
   %LOCALAPPDATA%\NFS Underground 2), into the output's profiles folder */
static void on_import(void) {
    char game[MAX_PATH]; GetWindowTextA(g_game_ed, game, sizeof game);
    if (!game_dir_fix(game, sizeof game)) {
        MessageBoxA(g_wnd, "Pick your Need for Speed Underground 2 folder first (it has a TRACKS folder in it): "
                    "the import reads the game's car data.", "NFSU2 Vita Converter", MB_ICONWARNING);
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
    MessageBoxA(g_wnd, m, "NFSU2 Vita Converter", ok ? MB_ICONINFORMATION : MB_ICONERROR);
}
static void on_timer(void) {
    char m[256];
    if (g_job.busy && g_job.todo && !g_job.extra[0]) {
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
        if (!g_job.ok) MessageBoxA(g_wnd, g_job.msg, "NFSU2 Vita Converter", MB_ICONERROR);
    }
    if (g_send.busy && g_send.total > 0) {
        SendMessageA(g_bar, PBM_SETPOS, (WPARAM)(g_send.sent * 1000 / g_send.total), 0);
        snprintf(m, sizeof m, "Sending to the Vita: %.0f of %.0f MB (file %d of %d)", g_send.sent / 1048576.0,
                 g_send.total / 1048576.0, g_send.file + 1, g_send.nfiles);
        set_status(m);
    } else if (g_send.done) {
        g_send.done = 0;
        set_status(g_send.msg);
        ui_idle();
        MessageBoxA(g_wnd, g_send.msg, "NFSU2 Vita Converter", g_send.ok ? MB_ICONINFORMATION : MB_ICONERROR);
    }
}

static void build_ui(void) {
    int y = 14;
    mk("STATIC", "NFSU2 Vita World Data Converter", 0, 20, y, 500, 28, 0, g_title); y += 32;
    mk("STATIC", "Does the Vita's one-time \"Optimizing world data\" step on this PC instead, so the game "
       "is ready to play the first time you start it.", 0, 20, y, 500, 34, 0, g_font); y += 46;

    mk("STATIC", "1.  Your Need for Speed Underground 2 folder", 0, 20, y, 500, 18, 0, g_bold); y += 22;
    g_game_ed = mk("EDIT", "", ES_AUTOHSCROLL, 20, y, 400, 24, ID_GAME, g_font);
    g_browse = mk("BUTTON", "Browse...", BS_PUSHBUTTON, 428, y, 92, 24, ID_BROWSE, g_font); y += 40;

    mk("STATIC", "2.  Convert", 0, 20, y, 500, 18, 0, g_bold); y += 22;
    g_convert = mk("BUTTON", "Convert", BS_DEFPUSHBUTTON, 20, y, 140, 30, ID_CONVERT, g_bold);
    mk("STATIC", "About 2 minutes. Your game files are only read, never changed.", 0, 172, y + 7, 348, 18, 0, g_font); y += 40;
    g_bar = mk(PROGRESS_CLASSA, "", 0, 20, y, 500, 16, ID_PROGRESS, g_font);
    SendMessageA(g_bar, PBM_SETRANGE32, 0, 1000); y += 22;
    g_status = mk("STATIC", "", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 20, y, 500, 18, ID_STATUS, g_font); y += 36;

    mk("STATIC", "3.  Put it on your Vita", 0, 20, y, 500, 18, 0, g_bold); y += 24;
    mk("STATIC", "Vita IP address:", 0, 20, y + 4, 104, 18, 0, g_font);
    g_ip_ed = mk("EDIT", "", ES_AUTOHSCROLL, 126, y, 150, 24, ID_IP, g_font);
    g_send_btn = mk("BUTTON", "Send to Vita", BS_PUSHBUTTON, 284, y, 120, 24, ID_SEND, g_font); y += 30;
    mk("STATIC", "Open VitaShell on the Vita and press SELECT to see its IP (vitacompanion works too). "
       "Close NFS Underground 2 on the Vita first.", 0, 20, y, 500, 34, 0, g_font); y += 42;
    g_open = mk("BUTTON", "Open output folder", BS_PUSHBUTTON, 20, y, 140, 24, ID_OPEN, g_font);
    mk("STATIC", "No Wi-Fi? Copy the \"cache\" folder from there into ux0:data/NFSU2/ on the Vita "
       "(USB or SD card).", 0, 170, y - 2, 350, 34, 0, g_font); y += 44;

    mk("STATIC", "Optional:  bring your PC career", 0, 20, y, 500, 18, 0, g_bold); y += 24;
    g_import = mk("BUTTON", "Import PC save...", BS_PUSHBUTTON, 20, y, 140, 24, ID_IMPORT, g_font);
    mk("STATIC", "Turns a profile of the PC game into a Vita profile (career, cars, options, "
       "high scores). Send to Vita copies it too.", 0, 170, y - 2, 350, 34, 0, g_font);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE: on_browse(); return 0;
        case ID_CONVERT: on_convert(); return 0;
        case ID_SEND: on_send(); return 0;
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
        if ((g_job.busy || g_send.busy) &&
            MessageBoxA(h, g_job.busy ? "The conversion is still running. Quit anyway? (The track in progress "
                                        "starts over next time.)"
                                      : "Still sending to the Vita. Quit anyway? (Send again later.)",
                        "NFSU2 Vita Converter", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
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
    if (__argc >= 4 && !strcmp(__argv[1], "--send")) {
        snprintf(g_send.ip, sizeof g_send.ip, "%s", __argv[2]);
        snprintf(g_out, sizeof g_out, "%s", __argv[3]);
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
    wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = "NFSU2VitaConverter";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    RegisterClassA(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, S(540), S(530) };
    AdjustWindowRect(&r, style, FALSE);
    g_wnd = CreateWindowA("NFSU2VitaConverter", "NFSU2 Vita Converter", style, CW_USEDEFAULT, CW_USEDEFAULT,
                          r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    build_ui();

    char v[MAX_PATH];
    setting_get("GameDir", v, sizeof v);
    if (!v[0] || !game_dir_fix(v, sizeof v)) game_dir_detect(v, sizeof v);
    SetWindowTextA(g_game_ed, v);
    setting_get("VitaIP", v, sizeof v);
    SetWindowTextA(g_ip_ed, v);
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
