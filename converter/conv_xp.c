/* conv_xp.c -- the Prep Tool's World Expansion step (was the separate NFSU2
 * Expansion Prep Tool, merged 11-19). Gets a player's Need for Speed Most
 * Wanted (2005) and Carbon PC files ready to drive in the port as Rockport and
 * Palmont:
 *  - checks the files the expansion reads (xp_manifest.inc: names, sizes, CRC32s;
 *    tools/gen_xp_manifest.py)
 *  - translates each city's world stream into Underground 2's format
 *    (src/xpconv.c, xproads.c) into OUT\stage\TRACKS
 *  - runs the game's own one-time world conversion on it (tiles, compact
 *    bundle, stripped copy, start, skyline), like the NFSU2 tracks
 *  - lists what goes to the Vita (ug2conv.c sends it with the rest).
 * Without the Prep Tool the Vita does the same from the raw files in
 * ux0:data/NFSU2/expansion/<game>/TRACKS (slower, same result).
 * The cities are switched on in the Vita's Settings (World Expansion), which
 * writes ux0:data/NFSU2/expansion/enable.txt. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world_tiles.h"
#include "skyline.h"
#include "xpconv.h"
#include "conv_xp.h"

void conv_set_folders(const char *game, const char *out);
unsigned char *res_bundle(const char *path, long *len);
void res_pin_strip(void);
void res_unpin(void);
void world_instance_library_release(void);
void world_instance_cache_flush(void);
void world_instance_set_detail(int low);
int conv_track_extras_done(const char *bun);
int conv_track_extras(const char *troot, const char *bun);

typedef struct { int game; const char *name, *game_name, *bun, *companion, *reg; int supported; } City;
static const City k_city[] = {
    { XP_GAME_MW,     "Rockport", "Need for Speed Most Wanted", "STREAML2RA", "L2RA",
      "Need for Speed Most Wanted", 1 },
    { XP_GAME_CARBON, "Palmont",  "Need for Speed Carbon",      "STREAML5RA", "L5RA",
      "Need for Speed Carbon", 1 },
};
#define NCITY XPP_NCITY
#define TRACKS_ROOT "ux0:data/NFSU2/TRACKS"   /* the staging folder's TRACKS (conv_platform.c) */
#define VITA_DATA   "ux0:/data/NFSU2"
#define VITA_CACHE  "ux0:/data/NFSU2/cache"

static char g_dir[NCITY][MAX_PATH];   /* each game's folder ("" = not used) */
static char g_out[MAX_PATH];          /* the Prep Tool's output: cache\ + stage\TRACKS\ */
static XppJob *g_job;                 /* the running conversion's progress */

static int file_exists(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static long long file_size(const char *p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(p, GetFileExInfoStandard, &d)) return -1;
    return ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}
static void refresh_focus(int c);

const char *xpp_city_name(int c) { return k_city[c].name; }
const char *xpp_game_name(int c) { return k_city[c].game_name; }
const char *xpp_bun(int c) { return k_city[c].bun; }
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

static void stage_path(char *out, size_t cap, int c, const char *ext) {   /* stage\TRACKS\<stream><ext> */
    snprintf(out, cap, "%s\\stage\\TRACKS\\%s%s", g_out, k_city[c].bun, ext);
}
static void compact_path(char *out, size_t cap, int c) {
    snprintf(out, cap, "%s\\cache\\%s.BUN.compact", g_out, k_city[c].bun);
}
static int translated(int c, long long source_size) {
    char p[MAX_PATH + 64], m[MAX_PATH + 64];
    stage_path(p, sizeof p, c, ".BUN"); stage_path(m, sizeof m, c, ".xp");
    return file_exists(p) && xp_marker_ok(m, k_city[c].game, source_size);
}
int xpp_city_done(int c) {   /* translated + tiles + compact bundle + skyline */
    char p[MAX_PATH + 64], m[MAX_PATH + 64]; compact_path(p, sizeof p, c); stage_path(m, sizeof m, c, ".xp");
    char sk[256], skl[MAX_PATH + 300]; skyline_cache_file(k_city[c].bun, sk, sizeof sk);
    snprintf(skl, sizeof skl, "%s\\cache\\tiles\\%s", g_out, strrchr(sk, '/') ? strrchr(sk, '/') + 1 : sk);
    return translated(c, -1) && world_tiles_ready(k_city[c].bun) && file_exists(p) && file_exists(skl);   /* the marker at this XP_VERSION (11-19: an older one read as done) */
}

/* ---- the conversion ------------------------------------------------------ */
static int xp_progress(void *ud, long long done, long long total) {
    (void)ud; g_job->xp_done = done; g_job->xp_total = total; return 0;
}
static int copy_file(const char *from, const char *to) {
    return CopyFileA(from, to, FALSE) != 0;
}

int xpp_convert(XppJob *job, int ask_on_bad, HWND owner) {
    g_job = job;
    char p[MAX_PATH + 64];
    snprintf(p, sizeof p, "%s\\cache", g_out); CreateDirectoryA(p, NULL);
    snprintf(p, sizeof p, "%s\\stage", g_out); CreateDirectoryA(p, NULL);
    snprintf(p, sizeof p, "%s\\stage\\TRACKS", g_out); CreateDirectoryA(p, NULL);
    printf("World Expansion: Most Wanted %s, Carbon %s, output %s\n", g_dir[0][0] ? g_dir[0] : "-",
           g_dir[1][0] ? g_dir[1] : "-", g_out);
    job->checking = 1;
    static char cmsg[2048];
    int good = xpp_check(&job->chk_bytes, &job->chk_total, cmsg, sizeof cmsg);
    job->checking = 0;
    printf("%s\n", cmsg);
    if (good == 0 && ask_on_bad) {
        char q[2400]; snprintf(q, sizeof q, "%s\n\nConvert the World Expansion anyway?", cmsg);
        if (MessageBoxA(owner, q, "NFSU2 Vita Prep Tool", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            snprintf(job->msg, sizeof job->msg, "World Expansion skipped: some Most Wanted / Carbon files are "
                     "missing or changed.");
            return 0;
        }
    }
    int todo[NCITY], n = 0;
    for (int c = 0; c < NCITY; c++) {
        if (!g_dir[c][0]) continue;
        if (xpp_city_done(c)) { printf("%s: already converted\n", k_city[c].name); continue; }
        todo[n++] = c;
    }
    g_job->todo = n;
    g_job->start = GetTickCount();
    world_instance_set_detail(1);   /* the console's world detail, as the UG2 tool builds it */
    int ok = 1;
    for (int i = 0; i < n && ok; i++) {
        int c = todo[i];
        g_job->cur = i; g_job->ncur = c;
        DWORD t0 = GetTickCount();
        /* 1. translate into stage\TRACKS (skipped when the marker says it is current) */
        char src[MAX_PATH + 64], dst[MAX_PATH + 64], mark[MAX_PATH + 64];
        snprintf(src, sizeof src, "%s\\TRACKS\\%s.BUN", g_dir[c], k_city[c].bun);
        long long src_size = file_size(src);
        stage_path(dst, sizeof dst, c, ".BUN"); stage_path(mark, sizeof mark, c, ".xp");
        if (!translated(c, src_size)) {
            g_job->phase = 1;
            DeleteFileA(mark);
            {   /* everything built from the older translation goes with it (as xpcity.c does on
                   the Vita): the compact copy is keyed by the stream's size only, and a translation
                   that moves bytes without changing the size kept the old one (rainbow fronts 11-19) */
                char cp[MAX_PATH + 96];
                compact_path(cp, sizeof cp, c); DeleteFileA(cp);
                snprintf(cp, sizeof cp, "%s\\cache\\tiles\\%s_v%d.pak", g_out, k_city[c].bun, WTILE_VERSION); DeleteFileA(cp);
                snprintf(cp, sizeof cp, "%s\\cache\\tiles\\%s_v%d.idx", g_out, k_city[c].bun, WTILE_VERSION); DeleteFileA(cp);
                snprintf(cp, sizeof cp, "%s\\cache\\%s.focus", g_out, k_city[c].bun + 6); DeleteFileA(cp);
                skyline_forget(k_city[c].bun);
            }
            XpStats st;
            int rc = xp_convert(src, dst, k_city[c].game, xp_progress, NULL, &st);
            printf("xpprep: %s translated (rc %d): %.1f -> %.1f MB, %ld solids, %ld bad, %ld instances, %ld vertices reordered\n",
                   k_city[c].name, rc, st.in_bytes / 1048576.0, st.out_bytes / 1048576.0, st.solids, st.bad, st.insts, st.relaid);
            ok = rc == XP_OK;
            char csrc[MAX_PATH + 64], cdst[MAX_PATH + 64];
            snprintf(csrc, sizeof csrc, "%s\\TRACKS\\%s.BUN", g_dir[c], k_city[c].companion);
            snprintf(cdst, sizeof cdst, "%s\\stage\\TRACKS\\%s.BUN", g_out, k_city[c].companion);
            if (ok) ok = xp_companion(csrc, cdst, k_city[c].game) == XP_OK;   /* scenery group tables -> UG2 (race barriers) */
            if (ok) {   /* the minimap (BMAP), as the Vita's xpcity makes it */
                char msrc[MAX_PATH + 64], mdst[MAX_PATH + 64];
                snprintf(msrc, sizeof msrc, "%s\\TRACKS\\%s\\MINI_MAP.BIN", g_dir[c], k_city[c].companion);
                if (k_city[c].game == XP_GAME_CARBON) snprintf(msrc, sizeof msrc, "%s", src);   /* Carbon's map is in its stream */
                snprintf(mdst, sizeof mdst, "%s\\stage\\TRACKS\\%s.MAP", g_out, k_city[c].companion);
                int mrc = xp_minimap(msrc, mdst, k_city[c].game);
                printf("xpprep: %s minimap: %s\n", k_city[c].name, mrc == XP_OK ? "made" : "none");
            }
            if (ok) {   /* the free-roam road net, as the Vita's xpcity makes it */
                char rdir[MAX_PATH + 64], rdst[MAX_PATH + 96]; XpRoadStats rs;
                snprintf(rdir, sizeof rdir, "%s\\stage\\TRACKS\\ROUTES%s", g_out, k_city[c].companion);
                CreateDirectoryA(rdir, NULL);
                snprintf(rdst, sizeof rdst, "%s\\RoutesFreeRoam.bin", rdir);
                int rrc = xp_routes(cdst, rdst, k_city[c].game, &rs);
                printf("xpprep: %s road net: %s (%ld sections, %ld links)\n", k_city[c].name, rrc == XP_OK ? "made" : "none", rs.sections, rs.links);
            }
            if (ok) {   /* the race events, as the Vita's xpcity makes them (Rockport 7301 on, Palmont 7601 on) */
                char rdir[MAX_PATH + 64], gp[MAX_PATH + 64]; XpEventStats es;
                snprintf(rdir, sizeof rdir, "%s\\stage\\TRACKS\\ROUTES%s", g_out, k_city[c].companion);
                snprintf(gp, sizeof gp, "%s\\GLOBAL\\gameplay.bin", g_dir[c]);
                int erc = xp_events(gp, csrc, src, rdir, k_city[c].game, k_city[c].name,
                                    k_city[c].game == XP_GAME_CARBON ? 7601 : 7301, &es);
                printf("xpprep: %s race events: %s (%ld races -> %ld events: %ld circuits, %ld sprints, %ld drags; %ld skipped, %ld dropped)\n",
                       k_city[c].name, erc == XP_OK ? "made" : "none", es.races, es.events, es.circuits, es.sprints, es.drags, es.skipped, es.dropped);
                if (erc == XP_OK && k_city[c].game == XP_GAME_MW) {   /* each race's own map (the game's xpcity_race_map reads them) */
                    char mdir[MAX_PATH + 64], src[MAX_PATH + 160], dst[MAX_PATH + 160], pat[MAX_PATH + 80];
                    snprintf(mdir, sizeof mdir, "%s\\stage\\TRACKS\\RACEMAPS%s", g_out, k_city[c].companion);
                    CreateDirectoryA(mdir, NULL);
                    snprintf(pat, sizeof pat, "%s\\TRACKS\\%s\\MINI_MAP_*.BIN", g_dir[c], k_city[c].companion);
                    int nmap = 0;
                    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
                    if (h != INVALID_HANDLE_VALUE) {
                        do {
                            if (!strcmp(fd.cFileName, "MINI_MAP.BIN")) continue;   /* the city map (made into the .MAP) */
                            snprintf(src, sizeof src, "%s\\TRACKS\\%s\\%s", g_dir[c], k_city[c].companion, fd.cFileName);
                            snprintf(dst, sizeof dst, "%s\\%s", mdir, fd.cFileName);
                            nmap += copy_file(src, dst);
                        } while (FindNextFileA(h, &fd));
                        FindClose(h);
                    }
                    printf("xpprep: %s race maps: %d copied\n", k_city[c].name, nmap);
                }
            }
            if (ok) {   /* the smackable props, as the Vita's xpcity makes them */
                char pdst[MAX_PATH + 64]; XpPropStats ps;
                snprintf(pdst, sizeof pdst, "%s\\stage\\TRACKS\\XPSMK%s.BUN", g_out, k_city[c].companion);
                int prc = xp_props(csrc, src, pdst, k_city[c].game, &ps);
                printf("xpprep: %s props: %s (%ld smackables -> %ld props, %ld infos)\n",
                       k_city[c].name, prc == XP_OK ? "made" : "none", ps.mw, ps.props, ps.infos);
            }
            if (ok) ok = xp_marker_write(mark, k_city[c].game, src_size);
            if (!ok) { printf("xpprep: %s: translation failed\n", k_city[c].name); break; }
        }
        if (k_city[c].game == XP_GAME_CARBON) {   /* translated before the areas' maps (11-19): the map again */
            char a0[MAX_PATH + 64], mdst[MAX_PATH + 64];
            snprintf(a0, sizeof a0, "%s\\stage\\TRACKS\\%s_A0.MAP", g_out, k_city[c].companion);
            if (!file_exists(a0)) {
                snprintf(mdst, sizeof mdst, "%s\\stage\\TRACKS\\%s.MAP", g_out, k_city[c].companion);
                int mrc = xp_minimap(src, mdst, k_city[c].game);
                printf("xpprep: %s minimap + area maps: %s\n", k_city[c].name, mrc == XP_OK ? "made" : "none");
            }
        }
        /* 2. the game's world conversion on the staged city, as the Vita's boot does */
        g_job->phase = 2;
        char stage[MAX_PATH + 16]; snprintf(stage, sizeof stage, "%s\\stage", g_out);
        conv_set_folders(stage, g_out);
        res_unpin(); world_instance_library_release(); world_instance_cache_flush();
        char spath[MAX_PATH + 96];   /* the stripped copy is keyed to the stream: never keep an old one
                                        (a 19:14 copy outlived the 22:10 stream; the Vita then fell back
                                        to the 490 MB copy and drew nothing, hwxp3 11-19) */
        snprintf(spath, sizeof spath, "%s\\cache\\%s.BUN.compact.strip", g_out, k_city[c].bun);
        {   /* converted before the skyline was part of it: the skyline only */
            char cp[MAX_PATH + 96], m[MAX_PATH + 64];
            compact_path(cp, sizeof cp, c); stage_path(m, sizeof m, c, ".xp");
            if (world_tiles_ready(k_city[c].bun) && file_exists(cp) && file_exists(spath) && file_exists(m)) {
                char sk[256];
                ok = skyline_build_file(k_city[c].bun, sk, sizeof sk) && xpp_city_done(c);
                printf("xpprep: %s skyline %s in %.1f s\n", k_city[c].name, ok ? "made" : "FAILED", (GetTickCount() - t0) / 1000.0);
                continue;
            }
        }
        DeleteFileA(spath);
        memset(&g_job->prog, 0, sizeof g_job->prog);
        ok = world_tiles_convert(TRACKS_ROOT, k_city[c].bun, &g_job->prog) && g_job->prog.ok;
        if (ok) {
            char bp[128]; snprintf(bp, sizeof bp, "%s/%s.BUN", TRACKS_ROOT, k_city[c].bun);
            long len = 0; res_unpin();
            ok = res_bundle(bp, &len) != NULL;   /* the compact copy (made here if missing) */
            if (ok && !file_exists(spath)) res_pin_strip();   /* writes the .strip */
            res_unpin();
            if (ok && !file_exists(spath)) { printf("xpprep: %s: no stripped copy\n", k_city[c].name); ok = 0; }
            if (ok) refresh_focus(c);
            if (ok) { char sk[256]; ok = skyline_build_file(k_city[c].bun, sk, sizeof sk); }   /* the far skyline (11-19: ~70 s on the Vita otherwise) */
            if (ok) ok = xpp_city_done(c);
        }
        printf("xpprep: %s %s in %.1f s\n", k_city[c].name, ok ? "converted" : "FAILED", (GetTickCount() - t0) / 1000.0);
    }
    res_unpin(); world_instance_library_release(); world_instance_cache_flush();
    int extras_ok = 1;
    for (int c = 0; c < NCITY && ok; c++) {
        if (!xpp_city_done(c) || conv_track_extras_done(k_city[c].bun)) continue;
        snprintf(g_job->extra, sizeof g_job->extra, "Preparing %s...", k_city[c].name);
        extras_ok &= conv_track_extras(TRACKS_ROOT, k_city[c].bun);
        world_instance_library_release(); world_instance_cache_flush();
    }
    g_job->extra[0] = 0;
    if (!extras_ok) printf("xpprep: some first-use caches failed (the Vita builds those itself)\n");
    unsigned secs = (GetTickCount() - g_job->start) / 1000;
    int any = 0; for (int c = 0; c < NCITY; c++) any |= xpp_city_done(c);
    if (!ok) snprintf(g_job->msg, sizeof g_job->msg, "Converting %s failed. See convert_log.txt in the output folder.",
                      k_city[g_job->ncur].name);
    else if (!any) { ok = 0; snprintf(g_job->msg, sizeof g_job->msg, "No Most Wanted or Carbon folder picked."); }
    else if (n) snprintf(g_job->msg, sizeof g_job->msg, "%d World Expansion cit%s converted in %u:%02u.",
                         n, n == 1 ? "y" : "ies", secs / 60, secs % 60);
    else snprintf(g_job->msg, sizeof g_job->msg, "World Expansion already converted.");
    printf("xpprep: %s\n", g_job->msg);
    fflush(stdout);
    return ok;
}
/* ---- what goes to the Vita ------------------------------------------------ */
/* the city's race events: Paths<id>.bin + Routes<id>F.bin, starts, barriers, events.txt
   last (the game builds them itself when events.txt is missing) */
static int send_events(XppSendFn add, void *ud, int n, int cap, int c) {
    char dir[MAX_PATH + 64], pat[MAX_PATH + 80], l[MAX_PATH + 160], r[256];
    snprintf(dir, sizeof dir, "%s\\stage\\TRACKS\\ROUTES%s", g_out, k_city[c].companion);
    snprintf(l, sizeof l, "%s\\events.txt", dir);
    if (GetFileAttributesA(l) == INVALID_FILE_ATTRIBUTES) return n;
    for (int pass = 0; pass < 2; pass++) {
        snprintf(pat, sizeof pat, "%s\\%s", dir, pass ? "Routes*F.bin" : "Paths*.bin");
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (n + 5 >= cap) break;                 /* room for the five files below */
            snprintf(l, sizeof l, "%s\\%s", dir, fd.cFileName);
            snprintf(r, sizeof r, VITA_DATA "/TRACKS/ROUTES%s/%s", k_city[c].companion, fd.cFileName);
            n += add(ud, l, r);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    static const char *const k_txt[] = { "starts.txt", "barriers.txt", "shops.txt", "trackpoly.bin", "modes.txt", "gates.txt", "names.txt", "mmroute.bin", "events.txt" };
    /* MW: each race's own map (MINI_MAP_<race>.BIN, copied at Convert; xpcity_race_map) */
    snprintf(pat, sizeof pat, "%s\\stage\\TRACKS\\RACEMAPS%s\\*.BIN", g_out, k_city[c].companion);
    {
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (n + 9 >= cap) break;
                snprintf(l, sizeof l, "%s\\stage\\TRACKS\\RACEMAPS%s\\%s", g_out, k_city[c].companion, fd.cFileName);
                snprintf(r, sizeof r, VITA_DATA "/TRACKS/RACEMAPS%s/%s", k_city[c].companion, fd.cFileName);
                n += add(ud, l, r);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    for (int i = 0; i < (int)(sizeof k_txt / sizeof k_txt[0]) && n < cap; i++) {   /* events.txt last */
        snprintf(l, sizeof l, "%s\\%s", dir, k_txt[i]);
        snprintf(r, sizeof r, VITA_DATA "/TRACKS/ROUTES%s/%s", k_city[c].companion, k_txt[i]);
        n += add(ud, l, r);
    }
    return n;
}
/* the game's start-point cache: {companion size, stream size} as 32-bit longs, then x y.
   Keyed to the files being sent (a cache from an older conversion had a stale key and
   made the game read Palmont's 1 GB stream whole: out of memory, hwxp2 11-19). */
static void refresh_focus(int c) {
    char l[MAX_PATH + 160], sp[MAX_PATH + 160], cp[MAX_PATH + 160];
    snprintf(l, sizeof l, "%s\\cache\\%s.focus", g_out, k_city[c].bun + 6);
    stage_path(sp, sizeof sp, c, ".BUN");
    snprintf(cp, sizeof cp, "%s\\stage\\TRACKS\\%s.BUN", g_out, k_city[c].companion);
    int key[2] = { (int)file_size(cp), (int)file_size(sp) };
    float xy[2] = { c == 0 ? 204.65f : 706.73f, c == 0 ? -143.31f : 339.70f };
    unsigned char old[16]; FILE *f = fopen(l, "rb");
    if (f) { if (fread(old, 1, 16, f) == 16) memcpy(xy, old + 8, 8); fclose(f); }
    if (key[0] <= 0 || key[1] <= 0) return;
    if ((f = fopen(l, "wb"))) { fwrite(key, 4, 2, f); fwrite(xy, 4, 2, f); fclose(f); }
}
/* per city: the translated stream + companion, the caches, then the marker and
   the tile index last -- their presence tells the game the set is whole */
int xpp_send_files(XppSendFn add, void *ud, int cap) {
    int n = 0;
    char l[MAX_PATH + 160], r[256];
    for (int c = 0; c < NCITY && n + XPP_FILES_PER_CITY <= cap; c++) {
        if (!xpp_city_done(c)) continue;
        const char *b = k_city[c].bun;
        stage_path(l, sizeof l, c, ".BUN"); snprintf(r, sizeof r, VITA_DATA "/TRACKS/%s.BUN", b); n += add(ud, l, r);
        snprintf(l, sizeof l, "%s\\stage\\TRACKS\\%s.BUN", g_out, k_city[c].companion);
        snprintf(r, sizeof r, VITA_DATA "/TRACKS/%s.BUN", k_city[c].companion); n += add(ud, l, r);
        compact_path(l, sizeof l, c); snprintf(r, sizeof r, VITA_CACHE "/%s.BUN.compact", b); n += add(ud, l, r);
        snprintf(l, sizeof l, "%s\\cache\\%s.BUN.compact.strip", g_out, b);
        snprintf(r, sizeof r, VITA_CACHE "/%s.BUN.compact.strip", b); n += add(ud, l, r);
        refresh_focus(c);
        snprintf(l, sizeof l, "%s\\cache\\%s.focus", g_out, b + 6);
        snprintf(r, sizeof r, VITA_CACHE "/%s.focus", b + 6); n += add(ud, l, r);
        snprintf(l, sizeof l, "%s\\cache\\tiles\\%s_v%d.pak", g_out, b, WTILE_VERSION);
        snprintf(r, sizeof r, VITA_CACHE "/tiles/%s_v%d.pak", b, WTILE_VERSION); n += add(ud, l, r);
        {   /* the skyline after the pack: the game builds it again when the pack is newer */
            char sk[256]; skyline_cache_file(b, sk, sizeof sk);
            const char *nm = strrchr(sk, '/') ? strrchr(sk, '/') + 1 : sk;
            snprintf(l, sizeof l, "%s\\cache\\tiles\\%s", g_out, nm);
            snprintf(r, sizeof r, VITA_CACHE "/tiles/%s", nm);
            if (file_exists(l)) n += add(ud, l, r);
        }
        snprintf(l, sizeof l, "%s\\stage\\TRACKS\\%s.MAP", g_out, k_city[c].companion);
        if (GetFileAttributesA(l) != INVALID_FILE_ATTRIBUTES) {   /* the minimap (none: no radar) */
            snprintf(r, sizeof r, VITA_DATA "/TRACKS/%s.MAP", k_city[c].companion); n += add(ud, l, r); }
        snprintf(l, sizeof l, "%s\\stage\\TRACKS\\%s.SEA", g_out, k_city[c].companion);
        if (GetFileAttributesA(l) != INVALID_FILE_ATTRIBUTES) {   /* the sea mask (Carbon; none: the sea is not cut) */
            snprintf(r, sizeof r, VITA_DATA "/TRACKS/%s.SEA", k_city[c].companion); n += add(ud, l, r); }
        for (int ar = 0; ar < 64; ar++) {   /* Carbon: the maps of the areas apart from the city (<comp>_A<k>.MAP) */
            snprintf(l, sizeof l, "%s\\stage\\TRACKS\\%s_A%d.MAP", g_out, k_city[c].companion, ar);
            if (GetFileAttributesA(l) == INVALID_FILE_ATTRIBUTES) break;
            snprintf(r, sizeof r, VITA_DATA "/TRACKS/%s_A%d.MAP", k_city[c].companion, ar); n += add(ud, l, r);
        }
        snprintf(l, sizeof l, "%s\\stage\\TRACKS\\ROUTES%s\\RoutesFreeRoam.bin", g_out, k_city[c].companion);
        if (GetFileAttributesA(l) != INVALID_FILE_ATTRIBUTES) {   /* the road net (none: no traffic) */
            snprintf(r, sizeof r, VITA_DATA "/TRACKS/ROUTES%s/RoutesFreeRoam.bin", k_city[c].companion); n += add(ud, l, r); }
        snprintf(l, sizeof l, "%s\\stage\\TRACKS\\XPSMK%s.BUN", g_out, k_city[c].companion);
        if (GetFileAttributesA(l) != INVALID_FILE_ATTRIBUTES) {   /* the props (none: the game makes them, if it has the raw files) */
            snprintf(r, sizeof r, VITA_DATA "/TRACKS/XPSMK%s.BUN", k_city[c].companion); n += add(ud, l, r); }
        n = send_events(add, ud, n, cap, c);   /* the race events (events.txt last: the set is whole) */
        stage_path(l, sizeof l, c, ".xp"); snprintf(r, sizeof r, VITA_DATA "/TRACKS/%s.xp", b); n += add(ud, l, r);
        snprintf(l, sizeof l, "%s\\cache\\tiles\\%s_v%d.idx", g_out, b, WTILE_VERSION);
        snprintf(r, sizeof r, VITA_CACHE "/tiles/%s_v%d.idx", b, WTILE_VERSION); n += add(ud, l, r);
    }
    return n;
}
