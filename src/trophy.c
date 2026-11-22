/* trophy.c -- see trophy.h. Conditions: re-tools/out/TROPHIES.md (step 2). */
#include "trophy.h"
#include "career.h"
#include "options.h"
#include "profile.h"
#include <stdio.h>
#include <string.h>

/* RA display order (re-tools/out/trophies/ra_game5613.json) */
static const TrophyInfo k_info[TROPHY_N] = {
    { 261034u, "Welcome to Bayview" },
    { 261033u, "Yo Noobie" },
    { 261028u, "Bring My Car Back... Now!" },
    { 261035u, "City Core Shopper" },
    { 261040u, "Making a Name" },
    { 261036u, "Beacon Hill Shopper" },
    { 261057u, "I'm so Hood" },
    { 261070u, "Turbo & High-Tech Performance Magazine" },
    { 261071u, "Sports Compact Car Magazine" },
    { 261090u, "Bayview's Best DVD" },
    { 261045u, "Sponsor 1 - Sponsor Events" },
    { 261041u, "Sponsor 1 - World Events" },
    { 261049u, "Pimped Out Escalade" },
    { 261029u, "Stage 2 - Full Clear" },
    { 261053u, "Stage 2 - Hard Finish" },
    { 261058u, "On the Come Up" },
    { 261037u, "Jackson Heights Shopper" },
    { 261059u, "The Upper Hand" },
    { 261062u, "Rims Real Big" },
    { 261072u, "Street Car Magazine" },
    { 261073u, "Car Audio and Electronics Magazine" },
    { 261074u, "GTI Magazine" },
    { 261075u, "Hot 4's Magazine" },
    { 261091u, "Four on the Floor DVD" },
    { 261092u, "Underground Speed Zone DVD" },
    { 261046u, "Sponsor 2 - Sponsor Events" },
    { 261042u, "Sponsor 2 - World Events" },
    { 261050u, "Navigator, Garage with the Elevator" },
    { 261030u, "Stage 3 - Full Clear" },
    { 261054u, "Stage 3 - Hard Finish" },
    { 261063u, "House in the Hills" },
    { 261038u, "Coal Harbor East Shopper" },
    { 261064u, "No Spoilers!" },
    { 261065u, "Wrapped Up" },
    { 261060u, "The Upper Upper Hand" },
    { 261076u, "Elaborare Magazine" },
    { 261077u, "Maxi Tuning Magazine" },
    { 261078u, "Spoiler Magazine" },
    { 261079u, "AutoMaxx Magazine" },
    { 261080u, "Stuff Magazine" },
    { 261081u, "Chrom und Flammen Magazine" },
    { 261093u, "Speed Demons 3 DVD" },
    { 261094u, "Modified to the Max DVD" },
    { 261095u, "Burning Wheels DVD" },
    { 261047u, "Sponsor 3 - Sponsor Events" },
    { 261043u, "Sponsor 3 - World Events" },
    { 261051u, "Hummer be Hummin" },
    { 261031u, "Stage 4 - Full Clear" },
    { 261055u, "Stage 4 - Hard Finish" },
    { 261066u, "Notorious" },
    { 261039u, "Coal Harbor West Shopper" },
    { 261061u, "The Uppermost Hand" },
    { 261067u, "Big Body" },
    { 261082u, "Import Tuner Magazine" },
    { 261083u, "Street Car Magazine II" },
    { 261084u, "GTI Magazine II" },
    { 261085u, "Auto Motor Magazine" },
    { 261086u, "Swiss Tuner Magazine" },
    { 261087u, "Super Street Magazine" },
    { 261088u, "Power Magazine" },
    { 261089u, "Street Tuner Magazine" },
    { 261096u, "Slammed & Stretched DVD" },
    { 261097u, "Straight From The Streets IV DVD" },
    { 261098u, "Racing Street Unleashed DVD" },
    { 261099u, "Blacktop Bandits DVD" },
    { 261048u, "Sponsor 4 - Sponsor Events" },
    { 261044u, "Sponsor 4 - World Events" },
    { 261052u, "King Kong in the Trunk" },
    { 261032u, "Stage 5 - Full Clear" },
    { 261056u, "Stage 5 - Hard Finish" },
    { 261068u, "Riders on the Storm" },
    { 261069u, "Making Rachel Proud" },
    { 261027u, "Makes all the Difference" },
    { 261100u, "Insane Drift" },
    { 261101u, "Drift King" },
    { 261102u, "Drift God" },
};

static struct {
    uint8_t bit[(TROPHY_N + 7) / 8];
    int off;                    /* no trophies.txt (the default): nothing reported to the system */
    float drift_peak;           /* the race's best single drift (points) */
} g_tr;

void (*trophy_backend)(int i);

const TrophyInfo *trophy_info(int i) { return i >= 0 && i < TROPHY_N ? &k_info[i] : NULL; }
int trophy_enabled(void) { return !g_tr.off; }
int trophy_unlocked(int i) { return i >= 0 && i < TROPHY_N && (g_tr.bit[i >> 3] >> (i & 7) & 1); }
int trophy_count(void) { int n = 0; for (int i = 0; i < TROPHY_N; i++) n += trophy_unlocked(i); return n; }

void trophy_init(void)
{
    FILE *f = fopen(TROPHY_ON_FILE, "rb");
    g_tr.off = f == NULL;
    if (f) fclose(f);
    printf("trophy: %s (%s)\n", g_tr.off ? "off" : "on", TROPHY_ON_FILE);
}
void trophy_default(void) { memset(g_tr.bit, 0, sizeof g_tr.bit); }

static int index_of(uint32_t ra)
{
    for (int i = 0; i < TROPHY_N; i++) if (k_info[i].ra_id == ra) return i;
    return -1;
}
static void unlock(uint32_t ra)
{
    int i = index_of(ra);
    if (i < 0 || trophy_unlocked(i)) return;
    g_tr.bit[i >> 3] |= (uint8_t)(1u << (i & 7));
    printf("trophy: unlocked %d/%d \"%s\" (RA %u)%s\n", trophy_count(), TROPHY_N, k_info[i].title,
           (unsigned)ra, g_tr.off ? " -- trophies off, not reported" : "");
    if (!g_tr.off && trophy_backend) trophy_backend(i);
}
void trophy_report_all(void)
{
    if (g_tr.off || !trophy_backend) return;
    for (int i = 0; i < TROPHY_N; i++) if (trophy_unlocked(i)) trophy_backend(i);
}

/* ---- the save block: u32 version 1, u32 count, the bits ---- */
long trophy_save(unsigned char *out, long cap)
{
    long n = 8 + (long)sizeof g_tr.bit;
    if (!out) return n;
    if (cap < n) return 0;
    uint32_t v = 1, c = TROPHY_N;
    memcpy(out, &v, 4); memcpy(out + 4, &c, 4); memcpy(out + 8, g_tr.bit, sizeof g_tr.bit);
    return n;
}
int trophy_load(const unsigned char *p, long n)
{
    trophy_default();
    uint32_t v = 0, c = 0;
    if (!p || n < 8) return 0;
    memcpy(&v, p, 4); memcpy(&c, p + 4, 4);
    /* a save from a build with a different trophy count: the ids are kept in
       order, so the earned ones that still exist carry over instead of all
       being reset (and saved over) (audit 11-17) */
    if (v != 1 || !c || c > 4096 || n < 8 + (long)((c + 7) / 8)) return 0;
    int keep = c < TROPHY_N ? (int)c : TROPHY_N;
    for (int i = 0; i < keep; i++)
        if (p[8 + (i >> 3)] >> (i & 7) & 1) g_tr.bit[i >> 3] |= (uint8_t)(1u << (i & 7));
    if (c != TROPHY_N) printf("trophy: save has %u trophies, this build %d; %d carried over\n", (unsigned)c, TROPHY_N, keep);
    printf("trophy: %d of %d unlocked in this profile\n", trophy_count(), TROPHY_N);
    return 1;
}

/* ---- the career state ---- */
static uint32_t u32le(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int ev_stage(const unsigned char *ev) { return (signed char)ev[0x37]; }
static int ev_won(const unsigned char *ev) { return career_has_completed(u32le(ev + 8)); }
static int name_won(const char *name) { return career_has_completed(career_hash(name)); }
/* a missable one: only while its stage is the current one (stage_cleared
   runs the check before the stage advances) */
static int open_(int stage) { return g_career.stage <= stage; }
static const CareerStage *st(int stage) { return &g_career.st[stage & 7]; }

/* every shop of stage <= max entered (record +6 = 5, NotifyEnterShopFromFreeRoam);
   the uniques shop has no world spot */
static int shops_entered(int max)
{
    int n = 0;
    for (int i = 0; i < career_nshops(); i++) {
        const unsigned char *sh = career_shop(i);
        if (!strcmp((const char *)sh, "THE_UNIQUES_SHOP") || (signed char)sh[0x9d] > max) continue;
        const CareerRecord *r = career_record(u32le(sh + 0x38));
        if (!r || r->flag[CREC_DONE] != 5) return 0;
        n++;
    }
    return n > 0;
}
static int suv_done(int stage)
{
    int n = 0;
    for (int i = 0; i < career_nevents(); i++) {
        const unsigned char *ev = career_event(i);
        if (ev_stage(ev) != stage || ev[0xd] != 1) continue;
        if (!ev_won(ev)) return 0;
        n++;
    }
    return n > 0;
}
/* the stage's world (not SUV) and URL events won, and the chosen sponsor's
   3 races (only those can appear) */
static int full_clear(int stage)
{
    for (int i = 0; i < career_nevents(); i++) {
        const unsigned char *ev = career_event(i);
        if (ev_stage(ev) != stage) continue;
        int world = ev[0x34] == 3 && ev[0xd] != 1, url = ev[0x34] == 2;
        if ((world || url) && !ev_won(ev)) return 0;
    }
    return cstage_sponsor_done(st(stage)) >= 3;
}
/* every event that can be won: all but the sponsor class (one sponsor's
   races per stage) and the scripted tutorial, plus 3 sponsor wins a stage.
   (FillStatistics' completion, won / all 224, can't pass ~86 %.) */
static int all_winnable(void)
{
    if (!career_nevents()) return 0;
    for (int i = 0; i < career_nevents(); i++) {
        const unsigned char *ev = career_event(i);
        if (ev[0x34] != 1 && ev[0x34] != 0 && !ev_won(ev)) return 0;
    }
    for (int s = 2; s <= 5; s++) if (cstage_sponsor_done(st(s)) < 3) return 0;
    return 1;
}
static int showcase_done(const char *name)
{
    for (int i = 0; i < career_nshowcases(); i++) {
        const unsigned char *sc = career_showcase(i);
        if (!strncmp((const char *)sc, name, 0x20)) return career_has_completed(u32le(sc + 0x20));
    }
    return 0;
}
/* a world challenge's record (by its name hash) done: NotifySucceedMission */
static int challenge_done(const char *name)
{
    const CareerRecord *r = career_record(career_hash(name));
    return r && r->flag[CREC_DONE] == 1;
}

/* Which magazine is which cover is not known yet (TROPHIES.md, open
   question 1): provisional, in showcase order. Fix the names here once each
   cover's masthead has been seen. DVDs go by their star rating (exact). */
static const struct { uint32_t ra; const char *sc; } k_cover[] = {
    { 261070u, "STAGE_2_SHOW_1_PICTURE" }, { 261071u, "STAGE_2_SHOW_2_PICTURE" }, { 261090u, "STAGE_2_DVD_1" },
    { 261072u, "STAGE_3_SHOW_1_PICTURE" }, { 261073u, "STAGE_3_SHOW_2_PICTURE" }, { 261074u, "STAGE_3_SHOW_3_PICTURE" },
    { 261075u, "STAGE_3_SHOW_4_PICTURE" }, { 261091u, "STAGE_3_DVD_1" }, { 261092u, "STAGE_3_DVD_2" },
    { 261076u, "STAGE_4_SHOW_1_PICTURE" }, { 261077u, "STAGE_4_SHOW_2_PICTURE" }, { 261078u, "STAGE_4_SHOW_3_PICTURE" },
    { 261079u, "STAGE_4_SHOW_4_PICTURE" }, { 261080u, "STAGE_4_SHOW_5_PICTURE" }, { 261081u, "STAGE_4_SHOW_6_PICTURE" },
    { 261093u, "STAGE_4_DVD_1" }, { 261094u, "STAGE_4_DVD_2" }, { 261095u, "STAGE_4_DVD_3" },
    { 261082u, "STAGE_5_SHOW_1_PICTURE" }, { 261083u, "STAGE_5_SHOW_2_PICTURE" }, { 261084u, "STAGE_5_SHOW_3_PICTURE" },
    { 261085u, "STAGE_5_SHOW_4_PICTURE" }, { 261086u, "STAGE_5_SHOW_5_PICTURE" }, { 261087u, "STAGE_5_SHOW_6_PICTURE" },
    { 261088u, "STAGE_5_SHOW_7_PICTURE" }, { 261089u, "STAGE_5_SHOW_8_PICTURE" },
    { 261096u, "STAGE_5_DVD_1" }, { 261097u, "STAGE_5_DVD_2" }, { 261098u, "STAGE_5_DVD_3" }, { 261099u, "STAGE_5_DVD_4" },
};
/* the Outrun unique parts: { trophy, world challenge, its stage } (missable) */
static const struct { uint32_t ra; const char *wc; int stage; } k_unique[] = {
    { 261057u, "STAGE_2_VISUAL_1", 2 }, { 261059u, "STAGE_3_PERF_1", 3 }, { 261062u, "STAGE_3_VISUAL_2", 3 },
    { 261064u, "STAGE_4_VISUAL_3", 4 }, { 261065u, "STAGE_4_VISUAL_4", 4 }, { 261060u, "STAGE_4_PERF_2", 4 },
    { 261061u, "STAGE_5_PERF_3", 5 }, { 261067u, "STAGE_5_VISUAL_5", 5 },
};
/* per stage: clear, shops (missable: every shop up to this stage), SUV and
   full clear (missable), sponsor races, world races (FillStatistics 0x7f7ba8) */
static const struct { int stage; uint32_t clear, shops, suv, full, spon, world; int world_req; } k_stage[] = {
    { 1, 261040u, 261035u, 0,       0,       0,       0,       0 },
    { 2, 261058u, 261036u, 261049u, 261029u, 261045u, 261041u, 10 },
    { 3, 261063u, 261037u, 261050u, 261030u, 261046u, 261042u, 20 },
    { 4, 261066u, 261038u, 261051u, 261031u, 261047u, 261043u, 30 },
    { 5, 0,       261039u, 261052u, 261032u, 261048u, 261044u, 35 },
};

void trophy_check(void)
{
    if (!career_nevents()) return;
    if (profile_xf_city()) return;   /* XF8 11-22: a city's own career (Start fresh) is no Bayview progress */
    /* the tutorial (stage 0) */
    if (career_stage_cleared(0)) unlock(261034u);
    if (open_(0) && st(0)->dc > 0) unlock(261033u);
    if (open_(0) && name_won("STAGE_0_CIRCUIT_0") && name_won("STAGE_0_CIRCUIT_1") && name_won("STAGE_0_SPRINT_0"))
        unlock(261028u);
    for (unsigned k = 0; k < sizeof k_stage / sizeof k_stage[0]; k++) {
        int s = k_stage[k].stage;
        if (k_stage[k].clear && career_stage_cleared(s)) unlock(k_stage[k].clear);
        if (open_(s) && shops_entered(s)) unlock(k_stage[k].shops);
        if (k_stage[k].suv && open_(s) && suv_done(s)) unlock(k_stage[k].suv);
        if (k_stage[k].spon && cstage_sponsor_done(st(s)) >= 3) unlock(k_stage[k].spon);
        if (k_stage[k].world && cstage_world_done(st(s)) >= k_stage[k].world_req) unlock(k_stage[k].world);
    }
    for (unsigned k = 0; k < sizeof k_unique / sizeof k_unique[0]; k++)
        if (open_(k_unique[k].stage) && challenge_done(k_unique[k].wc)) unlock(k_unique[k].ra);
    for (unsigned k = 0; k < sizeof k_cover / sizeof k_cover[0]; k++)
        if (showcase_done(k_cover[k].sc)) unlock(k_cover[k].ra);
    if (career_completed_career()) unlock(261068u);
    if (all_winnable()) unlock(261069u);
}

/* NotifyStageCleared (the stage is still the current one): the full clear
   is decided here -- the stage's final, the last race, was just won */
void trophy_stage_cleared(int stage)
{
    if (profile_xf_city()) return;
    for (unsigned k = 0; k < sizeof k_stage / sizeof k_stage[0]; k++)
        if (k_stage[k].stage == stage && k_stage[k].full && full_clear(stage)) unlock(k_stage[k].full);
    trophy_check();
}

/* CommitWinEvent: a stage final won on Hard */
void trophy_event_won(const unsigned char *ev)
{
    static const uint32_t hard[6] = { 0, 0, 261053u, 261054u, 261055u, 261056u };
    if (!ev || profile_xf_city()) return;
    int s = ev_stage(ev);
    const unsigned char *sd = s >= 2 && s <= 5 ? career_stage_data(s) : NULL;
    if (sd && u32le(sd + 0x28) == u32le(ev + 8) && g_opt.career_diff == 2) unlock(hard[s]);
    trophy_check();
}

void trophy_race_start(void) { g_tr.drift_peak = 0; }
void trophy_drift_bank(float points) { if (points > g_tr.drift_peak) g_tr.drift_peak = points; }
void trophy_race_end(int won, int neon, const unsigned char *ev)
{
    if (won && neon) unlock(261027u);
    if (won && ev && ev[0xf] == 5) {                 /* a career drift event */
        printf("trophy: best single drift %.0f\n", g_tr.drift_peak);
        if (g_tr.drift_peak >= 50000.0f) unlock(261100u);
        if (g_tr.drift_peak >= 100000.0f) unlock(261101u);
        if (g_tr.drift_peak >= 200000.0f) unlock(261102u);
    }
    g_tr.drift_peak = 0;
}
