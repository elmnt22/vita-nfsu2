/* career.c -- the career state over the GCareer tables (see career.h).
 *
 * Ported from the GC Speed.elf twins (named) with the PC v1.2 object and
 * save layout: ThePlayerCareer 0x85ad40, wallet +0x7134, stage infos
 * +0x72d8, stage +0x7af8, records +0x7afc/+0x7b00, event pile +0x7148,
 * current event +0x8738. GC <-> PC field pairs are in CAREER.md. */
#include "career.h"
#include "airace.h"
#include "beyond.h"
#include "perfparts.h"
#include "trophy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

CareerState g_career;

static const unsigned char *T[16];      /* chunk 0x34a11 + k */
static long TN[16];
static unsigned char *g_blob;           /* our copy of the container */
static AiRng g_rng = { 0x2545f491u };   /* bRandom 0x43c1c0 */

#define FINAL_EVENT 0x04cce3e1u         /* stage 5's final event (BuildListOfEvents) */
#define TUTORIAL_EVENT 0xdd60e403u      /* STAGE_1_* (stage 0 gate) */
#define SUV_NO_CHALLENGE 0xdd15316eu    /* the challenge an SUV may not take (MaybeAddWorldChallenge) */

static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int16_t rd16s(const unsigned char *p) { return (int16_t)(p[0] | p[1] << 8); }
static float rdf(const unsigned char *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

static uint32_t bstringhash(const char *s)                              /* 0x43db50 */
{
    uint32_t h = 0xffffffffu;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h;
}
static uint32_t fehashupper(const char *s)                              /* FEHashUpper */
{
    uint32_t h = 0xffffffffu;
    while (*s) h = h * 33 + (unsigned char)toupper((unsigned char)*s++);
    return h;
}

/* ------------------------------------------------------------ the tables */

/* LoaderMostWantedCareerChunk 80123520: the first 0x80034a10 container
   (PC 0x512c00 loads once, 0x8641f4); a second, partial copy follows it in
   GLOBALB and is ignored */
int career_init(const unsigned char *g, long len)
{
    free(g_blob); g_blob = NULL;
    memset(T, 0, sizeof T); memset(TN, 0, sizeof TN);
    for (long i = 0; i + 8 <= len; i += 4) {
        if (rd32(g + i) != 0x80034a10u) continue;
        long sz = (long)rd32(g + i + 4);
        if (i + 8 + sz > len) continue;
        g_blob = (unsigned char *)malloc((size_t)sz);
        if (!g_blob) return 0;
        memcpy(g_blob, g + i + 8, (size_t)sz);
        for (long j = 0; j + 8 <= sz; ) {
            uint32_t id = rd32(g_blob + j); long n = (long)rd32(g_blob + j + 4);
            if (j + 8 + n > sz) break;
            if (id >= 0x34a11 && id < 0x34a21 && !T[id - 0x34a11]) { T[id - 0x34a11] = g_blob + j + 8; TN[id - 0x34a11] = n; }
            j += 8 + n;
        }
        break;
    }
    g_rng.s ^= (unsigned)time(NULL);
    career_default();
    printf("career: %d events, %ld shops, %ld stages, %ld sponsors, %ld showcases, %ld challenges\n",
           career_nevents(), TN[1] / CAR_SHOP, TN[7] / CAR_STAGE, TN[8] / CAR_SPONSOR,
           TN[5] / CAR_SHOWCASE, TN[10] / CAR_CHALLENGE);
    return career_nevents();
}

int career_nevents(void) { return (int)(TN[0] / CAR_EVENT); }
const unsigned char *career_event(int i) { return i >= 0 && i < career_nevents() ? T[0] + i * CAR_EVENT : NULL; }
const unsigned char *career_event_by_handle(uint32_t h)
{
    for (int i = 0; i < career_nevents(); i++)
        if (rd32(T[0] + i * CAR_EVENT + 8) == h) return T[0] + i * CAR_EVENT;
    return NULL;
}
const char *career_string(unsigned off) { return T[12] && off < (unsigned)TN[12] ? (const char *)T[12] + off : ""; }
const unsigned char *career_stage_data(int stage)                       /* 0x500190 */
{
    for (long k = 0; k + CAR_STAGE <= TN[7]; k += CAR_STAGE)
        if ((signed char)T[7][k] == stage) return T[7] + k;
    return NULL;
}
const unsigned char *career_sponsor_by_hash(uint32_t h)                 /* FindSponsorDataByName */
{
    for (long k = 0; k + CAR_SPONSOR <= TN[8]; k += CAR_SPONSOR)
        if (rd32(T[8] + k + 8) == h) return T[8] + k;
    return NULL;
}
const unsigned char *career_sms_by_hash(uint32_t h)                     /* 0x501310 */
{
    if (!h) return NULL;
    for (long k = 0; k + CAR_SMS <= TN[6]; k += CAR_SMS)
        if (bstringhash(career_string(rd16(T[6] + k))) == h) return T[6] + k;
    return NULL;
}
int career_nshowcases(void) { return (int)(TN[5] / CAR_SHOWCASE); }
const unsigned char *career_showcase(int i) { return i >= 0 && i < career_nshowcases() ? T[5] + i * CAR_SHOWCASE : NULL; }
int career_nchallenges(void) { return (int)(TN[10] / CAR_CHALLENGE); }
int career_nshops(void) { return (int)(TN[1] / CAR_SHOP); }
const unsigned char *career_shop(int i) { return i >= 0 && i < career_nshops() ? T[1] + (long)i * CAR_SHOP : NULL; }
uint32_t career_hash(const char *s) { return bstringhash(s); }
const unsigned char *career_challenge(int i) { return i >= 0 && i < career_nchallenges() ? T[10] + i * CAR_CHALLENGE : NULL; }

/* ------------------------------------------------------- the stage infos */

static void cstage_reset(CareerStage *s, int stage)                     /* 0x500700 */
{
    memset(s, 0, sizeof *s);
    s->stage = career_stage_data(stage);
}
static int count_nz(const uint32_t *a, int n) { int c = 0; for (int i = 0; i < n; i++) c += a[i] != 0; return c; }
int cstage_world_done(const CareerStage *s) { return count_nz(s->world, 35); }
int cstage_url_done(const CareerStage *s) { return count_nz(s->url, 10); }
int cstage_sponsor_done(const CareerStage *s) { return count_nz(s->sponsor, 4); }

/* CanDoURL 80124dac: unlock method 3 = (param >> 16) world races; 4 = the
   required showcases and byte1 world, byte3 URL, byte2 sponsor races */
static int cstage_showcases_done(const CareerStage *s);
int cstage_can_do_url(const CareerStage *s, const unsigned char *ev)
{
    uint32_t p = rd32(ev + 0x10);
    if (ev[0xc] == 3) return (int)((int32_t)p >> 16) <= cstage_world_done(s);
    if (ev[0xc] != 4 || !cstage_showcases_done(s)) return 0;
    return (int)(p >> 8 & 0xff) <= cstage_world_done(s) && (int)(p >> 24) <= cstage_url_done(s)
        && (int)(p >> 16 & 0xff) <= cstage_sponsor_done(s);
}

/* CheckThatRequiredShowcasesHaveBeenCompleted 80124c54: the stage's
   showcases with +0x35 set, each by its handle (+0x20) */
static int cstage_showcases_done(const CareerStage *s)
{
    if (!s->stage) return 1;
    for (int i = 0; i < career_nshowcases(); i++) {
        const unsigned char *sc = career_showcase(i);
        if ((signed char)sc[0x25] == (signed char)s->stage[0] && sc[0x35] && !career_has_completed(rd32(sc + 0x20)))
            return 0;
    }
    return 1;
}

/* NeedToAskForSponsor 80124940: none chosen yet and a sponsor whose world
   race threshold (StageData +0x1c) has been reached */
int cstage_need_sponsor(const CareerStage *s)
{
    if (s->chosen[0] || !s->stage) return 0;
    int done = cstage_world_done(s);
    for (int i = 0; i < (signed char)s->stage[1]; i++)
        if (rd32(s->stage + 8 + 4 * i) && rd16s(s->stage + 0x1c + 2 * i) <= done) return 1;
    return 0;
}
/* GetAIPerformanceMatchingParameter 80124598 (PC 0x5132e0): the stage's AI
   level, StageData +0x44 to +0x48 over the +0x42 world + sponsor + URL wins */
float cstage_ai_level(const CareerStage *s)
{
    const unsigned char *sd = s->stage;
    if (!sd) return 0;
    float lo, hi;
    memcpy(&lo, sd + 0x44, 4); memcpy(&hi, sd + 0x48, 4);
    int n = cstage_world_done(s) + cstage_sponsor_done(s) + cstage_url_done(s);
    int total = (int16_t)(sd[0x42] | sd[0x43] << 8);
    if ((float)n <= (float)total && total != 0) return (hi - lo) * ((float)n / (float)total) + lo;
    return hi;
}
float cstage_avg_points(const CareerStage *s)                          /* GetAverageSponsorPointsPerRace 801248f0 */
{
    return s->npoints > 0 ? s->points / (float)s->npoints : 0.0f;
}
/* GetNumSponsorsToChooseFrom 801249f8: the slots whose world-race threshold
   is met and whose +0xe (the average sponsor points per race needed, of the
   stage before: cPostRaceScreenManager::NeedToAskForSponsor) is <= avg. The
   slots are in rising order, so the first n of cstage_sponsor_choice. */
int cstage_num_sponsors_to_choose(const CareerStage *s, float avg)
{
    if (!s->stage) return 0;
    int done = cstage_world_done(s), n = 0;
    for (int i = 0; i < (signed char)s->stage[1]; i++) {
        const unsigned char *sp = career_sponsor_by_hash(rd32(s->stage + 8 + 4 * i));
        if (sp && done >= rd16s(s->stage + 0x1c + 2 * i) && (float)rd16s(sp + 0xe) <= avg) n++;
    }
    return n;
}
int career_num_sponsors_chosen(void)                                    /* 80125efc */
{
    int n = 0;
    for (int i = 0; i < 8; i++) n += g_career.st[i].chosen[0] != NULL;
    return n;
}
const unsigned char *cstage_sponsor_choice(const CareerStage *s, int i)  /* GetChooseableSponsorByIndex 80124acc */
{
    if (!s->stage || i < 0 || i >= (signed char)s->stage[1]) return NULL;
    return career_sponsor_by_hash(rd32(s->stage + 8 + 4 * i));
}

static void cstage_won(CareerStage *s, const unsigned char *ev)        /* NotifyWonEvent 801247c4 */
{
    uint32_t h = rd32(ev + 8), *a = NULL; int n = 0;
    if (ev[0x34] == 3 && ev[0xd] != 1) { a = s->world; n = 35; }
    else if (ev[0x34] == 1) { a = s->sponsor; n = 4; }
    else if (ev[0x34] == 2) { a = s->url; n = 10; }
    for (int i = 0; i < n; i++)
        if (!a[i] || a[i] == h) { a[i] = h; return; }
}
/* NotifyCurrentRaceFinished 80126cd0: a won race counts for its stage at the
   finish, before the post-race screens (NeedToAskForSponsor reads the count
   in the cPostRaceScreenManager ctor); CommitWinEvent's own NotifyWonEvent
   later finds it already there */
void career_race_finished(int won)
{
    const unsigned char *ev = g_career.engaged_event;
    if (won && ev) cstage_won(&g_career.st[(signed char)ev[0x37] & 7], ev);
}
static void stage_cleared(int stage);
static void cs_stage_started(int stage);
static void cs_event_begin(const unsigned char *ev);
static void cs_event_end(const unsigned char *ev, int won);
static int cstage_check_cleared(CareerStage *s)                        /* CheckIfClearedStage 80124ce8 */
{
    if (s->cleared || !s->stage) return s->cleared;
    uint32_t fin = rd32(s->stage + 0x28);
    if (!fin ? career_all_events_done((signed char)s->stage[0])
             : career_has_completed(fin) && cstage_showcases_done(s)) {
        s->cleared = 1;
        stage_cleared((signed char)s->stage[0]);
    }
    return s->cleared;
}

/* NotifyCommitSponsorPoints 0x513430: not for the DDAY events */
static void cstage_sponsor_points(CareerStage *s, float margin, const unsigned char *ev)
{
    uint32_t h = rd32(ev + 8);
    if (h == bstringhash("DDAY_EVENT_A") || h == bstringhash("DDAY_EVENT_B")) return;
    s->points += career_rep_for_margin(margin, ev);
    s->npoints++;
}

/* --------------------------------------------------------- the state */

void career_default(void)                                               /* 0x532830 */
{
    CareerState *c = &g_career;
    memset(c, 0, sizeof *c);
    c->f8764 = -1.0f;
    c->cash = 0;          /* 0x576090: the bonus globals 0x86512c (+200) / 0x865130 (+1000), 0 unless granted */
    c->career_hash = fehashupper("CAREER_0");
    memset(c->a8869, 1, sizeof c->a8869);   /* 0x5329ee: 16 x 1 (GC +0x7f5c) */
    for (int i = 0; i < 8; i++) cstage_reset(&c->st[i], i);
    c->stage = 0;
    /* Default(true) (GC 80123934; PC 0x532830 inlines the unlock): the career
       cars, then the first slot unlocked */
    career_cars_default();
    career_unlock_car_slot();
    /* GC Default also: RandomEncounter/Showcase/SMS/Speech/Stage0 defaults -- later phases */
}

CareerRecord *career_record(uint32_t h)                                 /* 0x5003b0 */
{
    for (int i = 0; i < g_career.nrec; i++)
        if (g_career.rec[i].handle == h) return &g_career.rec[i];
    return NULL;
}
CareerRecord *career_add_record(uint32_t h)                             /* 0x512f10 */
{
    CareerRecord *r = career_record(h);
    if (r) return r;
    if (g_career.nrec >= 0x180) return NULL;
    r = &g_career.rec[g_career.nrec++];
    memset(r, 0, sizeof *r);
    r->handle = h;
    return r;
}
int career_has_completed(uint32_t h)
{
    CareerRecord *r = career_record(h);
    return r && r->flag[CREC_DONE] == CDONE_WON;
}
int career_completed_career(void)                                       /* +0x6fd8 = stage 5's StageData */
{
    const unsigned char *s = g_career.st[5].stage;
    return s && career_has_completed(rd32(s + 0x28));
}
int career_all_events_done(int stage)
{
    for (int i = 0; i < career_nevents(); i++) {
        const unsigned char *ev = career_event(i);
        if ((signed char)ev[0x37] == stage && !career_has_completed(rd32(ev + 8))) return 0;
    }
    return 1;
}
int career_stage_cleared(int stage) { return stage >= 0 && stage < 8 && g_career.st[stage].cleared; }

static void stage_cleared(int stage)                                    /* NotifyStageCleared 80125fb0 */
{
    if (stage != g_career.stage) return;
    trophy_stage_cleared(stage);                 /* before the stage advances (its missables) */
    g_career.stage = stage + 1;
    printf("career: stage %d cleared, now stage %d\n", stage, g_career.stage);
    cs_stage_started(g_career.stage);
}

/* -------------------------------------------------------- the event pile */

static void pile_reset(CareerPile *p) { memset(p, 0, sizeof *p); }

static int event_zone_type(const unsigned char *ev)                     /* get_event_trigger_zone_type */
{
    if (ev[0xd] == 1) return 7;
    if (ev[0x34] == 2) return 5;
    switch ((signed char)ev[0xf]) {
    case 2: return 2; case 1: return 4; case 4: return 1; case 5: return 3;
    default: return 0;
    }
}
static int pile_add_event(CareerPile *p, const unsigned char *ev)       /* CertainlyAddToPile */
{
    for (int i = 0; i < 64; i++)
        if (!p->event[i]) { p->event[i] = ev; p->zone_count[event_zone_type(ev)]++; return 1; }
    return 0;                                   /* pile full */
}
static void pile_add_challenge(CareerPile *p, const unsigned char *wc)
{
    for (int i = 0; i < 32; i++)
        if (!p->challenge[i]) { p->challenge[i] = wc; return; }
}
static int pile_has(const CareerPile *p, const unsigned char *ev)
{
    for (int i = 0; i < 64; i++)
        if (p->event[i] && rd32(p->event[i] + 8) == rd32(ev + 8)) return 1;
    return 0;
}
static int pile_hidden(const CareerPile *p)
{
    int n = 0;
    for (int i = 0; i < 64; i++) n += p->event[i] && p->event[i][0x7f];
    return n;
}

/* MatchesType 8012524c: 0 circuit, 1 drag, 2 drift, 3 sprint, 4 street X
   (world, not SUV), 5 SUV, 6..10 the same five as sponsor races */
static int pile_matches(const unsigned char *ev, int type)
{
    static const signed char k_race[11] = { 0, 4, 5, 1, 2, -1, 0, 4, 5, 1, 2 };
    if (type == 5) return ev[0xd] == 1;
    if (type < 0 || type > 10) return 0;
    if (type < 5) return ev[0x34] == 3 && ev[0xd] != 1 && (signed char)ev[0xf] == k_race[type];
    return ev[0x34] == 1 && (signed char)ev[0xf] == k_race[type];
}
static int pile_count_type(const CareerPile *p, int type)
{
    int n = 0;
    for (int i = 0; i < 64; i++) n += p->event[i] && pile_matches(p->event[i], type);
    return n;
}
/* GetMaxSponsoredEventTypes 80125540: the chosen sponsor's four race
   categories (+4..+7) of this kind, less the sponsor races of that type won */
static int pile_max_sponsored(int category, int type)
{
    const CareerStage *s = &g_career.st[g_career.stage];
    int n = 0;
    if (s->chosen[0])
        for (int i = 0; i < 4; i++) n += (signed char)s->chosen[0][4 + i] == category;
    if (n)
        for (int i = 0; i < 4; i++)
            if (s->sponsor[i]) {
                const unsigned char *ev = career_event_by_handle(s->sponsor[i]);
                if (ev && pile_matches(ev, type)) n--;
            }
    return n;
}
/* DAT_803bbed0 (+0x7e2c, b873d: a career car is owned) && IsSUV(GetCurrentCareerCar())
   801345c4 (record +0x88a) -- GetMaxNumberOfEventsByType / GetRandomEvent /
   BuildListOfEvents / MaybeAddWorldChallenge. have_suv_car is a test override. */
static int career_driving_suv(void)
{
    if (g_career.have_suv_car) return 1;
    const CareerCar *c = g_career.b873d ? career_current_car() : NULL;
    return c && airace_type_suv(c->type);
}
/* DoesPlayerOwnSUV 8012420c: any FEPlayerCarDB career car (filter 4, +0x7f8 set) that IsSUV */
static int career_owns_suv(void)
{
    if (g_career.have_suv_car) return 1;
    for (int i = 0; i < CAREER_NSLOTS; i++) {
        const CareerCar *c = career_car_slot(i);
        if (c && c->has && c->type >= 0 && airace_type_suv(c->type)) return 1;
    }
    return 0;
}
static int pile_max(int type)                                           /* GetMaxNumberOfEventsByType 80125614 */
{
    const unsigned char *sd = g_career.st[g_career.stage].stage;
    if (!sd) return 0;
    switch (type) {
    case 0: return (signed char)sd[0x30];
    case 1: return (signed char)sd[0x31];
    case 2: return (signed char)sd[0x33];
    case 3: return (signed char)sd[0x34];
    case 4: return (signed char)sd[0x32];
    case 5: return career_driving_suv() ? (signed char)sd[0x37] : 0;
    case 6: return pile_max_sponsored(1, 6);
    case 7: return pile_max_sponsored(3, 7);
    case 8: return pile_max_sponsored(2, 8);
    case 9: return pile_max_sponsored(4, 9);
    case 10: return pile_max_sponsored(5, 10);
    }
    return -1;
}
/* GetRandomEvent 80125798: the candidates shuffled with bRandom(n) n times,
   the first returned */
static const unsigned char *pile_random(const CareerPile *p, int type)
{
    const unsigned char *c[32]; int n = 0;
    memset(c, 0, sizeof c);
    for (int i = 0; i < career_nevents(); i++) {
        const unsigned char *ev = career_event(i);
        if (pile_has(p, ev) || (signed char)ev[0x37] != g_career.stage || ev[0x34] == 2 || ev[0x7f]
            || career_has_completed(rd32(ev + 8)) || rd32(ev + 8) == FINAL_EVENT || !pile_matches(ev, type))
            continue;
        if (ev[0xd] == 1 && !career_driving_suv()) continue;
        if (n < 32) c[n] = ev;
        n++;
    }
    for (int i = 0; i < n && n <= 32; i++) {
        int j = airng_int(&g_rng, n);
        const unsigned char *t = c[i]; c[i] = c[j]; c[j] = t;
    }
    return c[0];
}
/* MaybeAddWorldChallenge 80125104: kind +6 (0 world, 1 URL, 3 sponsor
   races, 2 stage +0xdc) must reach +7 */
static void pile_maybe_challenge(CareerPile *p, const CareerStage *s, const unsigned char *wc)
{
    uint32_t h = bstringhash(career_string(rd16(wc)));
    CareerRecord *r = career_record(h);
    if (r && r->flag[CREC_DONE] == CDONE_WON) return;
    int ok = 0, need = (signed char)wc[7];
    switch ((signed char)wc[6]) {
    case 0: ok = cstage_world_done(s) >= need; break;
    case 1: ok = cstage_url_done(s) >= need; break;
    case 2: ok = s->dc >= need; break;
    case 3: ok = cstage_sponsor_done(s) >= need; break;
    }
    if (career_driving_suv() && h == SUV_NO_CHALLENGE) ok = 0;
    if (ok) pile_add_challenge(p, wc);
}

void career_build_events(void)                                          /* BuildListOfEvents 0x527d30 */
{
    CareerState *c = &g_career;
    CareerPile old = c->pile;
    pile_reset(&c->pile);
    CareerPile *p = &c->pile;
    /* stage 0: only once the tutorial flags allow (GC +0x7e34 / +0x7e24 / +0x7e30) */
    if (!(c->stage != 0 || c->b873f)) return;
    if (c->stage == 0 && ((c->current_event && c->current_event == TUTORIAL_EVENT) || c->b873e)) return;
    if (c->wcs.wc) return;                               /* (nor while a world challenge runs) */
    if (c->stage == 5) {
        const unsigned char *fin = career_event_by_handle(FINAL_EVENT);
        if (fin && career_has_completed(rd32(fin + 0x10)) && !career_has_completed(FINAL_EVENT)) {
            pile_add_event(p, fin);
            return;
        }
    }
    if (career_completed_career()) return;
    for (int i = 0; i < career_nchallenges(); i++) {
        const unsigned char *wc = career_challenge(i);
        if (rd16s(wc + 4) == c->stage) pile_maybe_challenge(p, &c->st[c->stage], wc);
    }
    for (int i = 0; i < 64; i++) {                       /* keep what is still open */
        const unsigned char *ev = old.event[i];
        if (ev && (signed char)ev[0x37] == c->stage && (ev[0xd] != 1 || career_driving_suv())
            && !career_has_completed(rd32(ev + 8)))
            pile_add_event(p, ev);
    }
    for (int t = 0; t < 11; t++) {
        int have = pile_count_type(p, t), max = pile_max(t);
        const unsigned char *ev;
        while ((max == -1 || have < max) && (ev = pile_random(p, t)) != NULL) {
            if (!pile_add_event(p, ev)) break;  /* full: the same pick would come back forever */
            have = pile_count_type(p, t);
        }
    }
    int maxh = c->st[c->stage].stage ? (signed char)c->st[c->stage].stage[0x41] : -1;
    for (int i = 0; i < career_nevents(); i++) {
        const unsigned char *ev = career_event(i);
        if (maxh != -1 && maxh <= pile_hidden(p)) break;
        if ((signed char)ev[0x37] == c->stage && ev[0x7f] && !career_has_completed(rd32(ev + 8)) && !pile_has(p, ev))
            pile_add_event(p, ev);
    }
    for (int i = 0; i < career_nevents(); i++) {
        const unsigned char *ev = career_event(i);
        if ((signed char)ev[0x37] == c->stage && ev[0x34] == 2 && !career_has_completed(rd32(ev + 8))
            && !pile_has(p, ev) && cstage_can_do_url(&c->st[c->stage], ev))
            pile_add_event(p, ev);
    }
}
int career_event_in_pile(const unsigned char *ev) { return ev && pile_has(&g_career.pile, ev); }
int career_event_on_radar(const unsigned char *ev)                      /* IsEventVisibleOnRadar */
{
    if (!ev || ev[0x7f] || !career_event_in_pile(ev)) return 0;
    return ev[0xd] != 1 || career_owns_suv();         /* DoesPlayerOwnSUV 8012420c (owned, not driven) */
}
const unsigned char *career_random_hidden_event(void)                   /* GetRandomHiddenEvent 80125948 */
{
    const CareerPile *p = &g_career.pile;
    int n = pile_hidden(p);
    if (!n) return NULL;
    /* GC walks k = bRandom(n) + 1 entries, skipping empty slots, hidden or not */
    int k = airng_int(&g_rng, n) + 1, idx = -1;
    while (k--) {
        idx++;
        while (idx < 64 && !p->event[idx]) idx++;
    }
    return idx < 64 ? p->event[idx] : NULL;
}

/* --------------------------------------------------------------- results */

/* GetReputationForMarginOfVictory 0x512b50: table 0x7f7ac8 per category
   (GetReputationCategoryForEvent 0x512ae0): below t1 nothing, then
   100 / 200 / 300 from t1 / t2 / t3 (drift: points margins 10/20/30) */
static int rep_category(const unsigned char *ev)
{
    if (ev[0x34] == 2) return 6;
    if ((ev[0x34] == 3 && ev[0xd] != 1) || ev[0x34] == 1 || ev[0xd] == 1)
        switch ((signed char)ev[0xf]) {
        case 0: return 1; case 4: return 2; case 1: return 3; case 5: return 4; case 2: return 5;
        }
    return 0;
}
float career_rep_for_margin(float m, const unsigned char *ev)
{
    static const float k_t[7][6] = {
        { 1, 2, 5, 100, 200, 300 }, { 1, 2, 5, 100, 200, 300 }, { 1, 2, 5, 100, 200, 300 },
        { 10, 20, 30, 100, 200, 300 }, { 1, 2, 5, 100, 200, 300 }, { 1, 2, 5, 100, 200, 300 },
        { 1, 2, 5, 100, 200, 300 } };
    int cat = rep_category(ev);
    if (cat < 1 || cat > 7) return 0.0f;
    const float *t = k_t[cat - 1];
    if (m < t[0]) return 0.0f;
    if (m < t[1]) return t[3];
    if (m < t[2]) return t[4];
    return t[5];
}

int career_event_purse(const unsigned char *ev)                         /* 0x500c30 */
{
    const CareerStage *s = &g_career.st[g_career.stage];
    if (ev[0x34] == 1 && s->chosen[0]) return rd16(s->chosen[0] + 2);
    return (int)rd32(ev + 0x30);
}
int career_url_purse(int stage)                                         /* GetURLPurse 80125f2c */
{
    for (int i = 0; i < career_nevents(); i++) {
        const unsigned char *ev = career_event(i);
        if ((signed char)ev[0x37] == stage && ev[0x34] == 2) return (int)rd32(ev + 0x30);
    }
    return 0;
}
int career_random_encounter_purse(int stage)
{
    const unsigned char *sd = stage >= 0 && stage < 8 ? g_career.st[stage].stage : NULL;
    return sd ? rd16s(sd + 2) : 0;
}
int career_stage_race_purse(int type)
{
    /* +0x34 3: an ordinary race (1 sponsor, 2 URL); a finished career (stage 6) has
       none -- the last stage that does */
    for (int st = g_career.stage; st >= 0; st--)
        for (int any = 0; any < 2; any++) {
            long sum = 0; int n = 0;
            for (int i = 0; i < career_nevents(); i++) {
                const unsigned char *ev = career_event(i);
                if ((signed char)ev[0x37] != st || ev[0x34] != 3 || (int)rd32(ev + 0x30) <= 0) continue;
                if (!any && ev[0xf] != type) continue;
                sum += (long)rd32(ev + 0x30); n++;
            }
            if (n) return (int)((sum / n + 50) / 100 * 100);   /* to the $100 */
        }
    return career_random_encounter_purse(g_career.stage);
}
int career_star_rating_purse(int stage)
{
    const unsigned char *sd = stage >= 0 && stage < 8 ? g_career.st[stage].stage : NULL;
    return sd ? rd16s(sd + 4) : 0;
}

/* CommitWinEvent 0x541420: the record marked won; with the reward still
   claimable the purse is paid (the current career event: GetLastPurse
   0x532e50 and its sponsor points; otherwise the event's +0x30); then the
   stage counts it and checks for a clear. Showcase (C9), SMS (C8), speech
   (C10) and world challenge (C3) notifications come with those phases. */
void career_commit_win(uint32_t h, int reward, float margin)
{
    const unsigned char *ev = career_event_by_handle(h);
    if (!ev) return;
    CareerRecord *r = career_add_record(h);       /* the PC writes through a record NotifyEnterEvent made */
    if (r) r->flag[CREC_DONE] = CDONE_WON;
    CareerStage *s = &g_career.st[(signed char)ev[0x37] & 7];
    if (reward) {
        /* +0x7e1c, the race NotifyEnterEvent engaged (not +0x7e24, the shop
           event): GetLastPurse + NotifyCommitSponsorPoints (GC 80127050) */
        if (g_career.engaged_event) {
            g_career.cash += career_event_purse(ev);
            cstage_sponsor_points(s, margin, ev);
        } else
            g_career.cash += (int)rd32(ev + 0x30);
    }
    /* GC 80127050's order: SMS NotifyCompleteEvent, NotifyCommitEvent 80124790
       (NotifyWonEvent + CheckIfClearedStage -- the stage may advance here),
       HandleCareerEventEnd, then WorldChallengeState::NotifyCareerEventCompleted
       8012ceec on the (possibly new) current stage */
    career_sms_complete_event(ev);               /* SMSMessageQueue::NotifyCompleteEvent 8012c6d4 */
    cstage_won(s, ev);
    cstage_check_cleared(s);
    cs_event_end(ev, 1);
    for (int i = 0; i < career_nchallenges(); i++) {   /* the stage's world-race challenges reached exactly now -> their SMS */
        const unsigned char *wc = career_challenge(i);
        if (wc[6] == 0 && rd16s(wc + 4) == g_career.stage &&
            cstage_world_done(&g_career.st[g_career.stage & 7]) == (signed char)wc[7]) career_sms_add_wc(wc);
    }
    printf("career: won %s ($%d)\n", career_string(rd16(ev)), g_career.cash);
    trophy_event_won(ev);
}
void career_commit_lose(uint32_t h)                                     /* CommitLoseEvent 80127178 */
{
    CareerRecord *r = career_add_record(h);
    if (r && r->flag[CREC_DONE] != CDONE_WON) r->flag[CREC_DONE] = CDONE_LOST;
    const unsigned char *ev = career_event_by_handle(h);
    if (ev) cs_event_end(ev, 0);
}
/* NotifyChooseSponsor 80125e80: once per stage; the career car slot, the
   sponsor's signing cash (+0xc) */
void career_choose_sponsor(const unsigned char *sp)
{
    CareerStage *s = &g_career.st[g_career.stage];
    if (s->chosen[0] || !sp) return;
    career_unlock_car_slot();                    /* CommitUnlockCareerCarSlot 801265b4 */
    g_career.cash += rd16s(sp + 0xc);
    s->chosen[0] = sp;
}

/* ------------------------------------------------------- the world layer */

static struct {
    CareerWorldZone *z; int n;                  /* the region's zones */
    CareerZone slot[0x40];                      /* UnlockedZoneManager (+0) */
    CareerZone cur;                             /* +0x7dec.. the zone the car is in (NotifyEnterTriggerZone) */
    CareerZone engaged;                         /* +0x7df0.. (NotifyEngageMechanicRequested) */
    CareerZone left;                            /* the zone the last CZONE_EXIT left (FEngHud::ExitTriggerZone's) */
    uint32_t in_zone;                           /* Player +0x9a4 */
    int engaging;                               /* +0x6820 IsEngagingDialog / SetEngagingDialog */
} g_cw;

void career_set_world_zones(const CareerWorldZone *z, int n)
{
    free(g_cw.z);
    g_cw.z = n > 0 ? malloc(sizeof *z * (size_t)n) : NULL;
    g_cw.n = g_cw.z ? n : 0;
    if (g_cw.z) memcpy(g_cw.z, z, sizeof *z * (size_t)n);
    memset(&g_cw.cur, 0, sizeof g_cw.cur); g_cw.in_zone = 0;
    memset(&g_cw.left, 0, sizeof g_cw.left); memset(&g_cw.engaged, 0, sizeof g_cw.engaged);
    g_cw.engaging = 0;
}
static CareerZone *cw_free_slot(void)                                    /* FindFreeSlot 80128cfc */
{
    for (int i = 0; i < 0x40; i++) if (!g_cw.slot[i].zone) return &g_cw.slot[i];
    return NULL;
}
static CareerZone *cw_add(const CareerZone *z)                           /* AddUnlockedZone 80128d2c.. */
{
    CareerZone *s = cw_free_slot();
    if (s) { *s = *z; s->pile = 0; }
    return s;
}
const CareerZone *career_unlocked_zone(int i)
{
    return i >= 0 && i < 0x40 && g_cw.slot[i].zone ? &g_cw.slot[i] : NULL;
}
static const unsigned char *shop_by_zone(uint32_t zone)                  /* FindShopDataDescByTriggerZone 80121f54 */
{
    for (long k = 0; k + CAR_SHOP <= TN[1]; k += CAR_SHOP) if (rd32(T[1] + k + 0x3c) == zone) return T[1] + k;
    return NULL;
}
static const unsigned char *showcase_by_zone(uint32_t zone)              /* 80122514 */
{
    for (int i = 0; i < career_nshowcases(); i++) if (rd32(career_showcase(i) + 0x2c) == zone) return career_showcase(i);
    return NULL;
}
static const unsigned char *wc_by_zone(uint32_t zone)                    /* FindWorldChallengeByTriggerZone 80122e1c */
{
    for (int i = 0; i < career_nchallenges(); i++)
        if (bstringhash(career_string(rd16(career_challenge(i) + 2))) == zone) return career_challenge(i);
    return NULL;
}
int career_shop_unlocked(const unsigned char *shop)                      /* 80122044 */
{
    const CareerStage *s = &g_career.st[(signed char)shop[0x9d] & 7];
    int need = (int)rd32(shop + 0x74);
    switch (shop[0x9c]) {
    case 0: return 1;
    case 1: return career_has_completed(rd32(shop + 0x74));
    case 2: return need <= cstage_url_done(s);
    case 3: return need <= cstage_world_done(s);
    case 4: return need <= cstage_sponsor_done(s);
    }
    return 0;
}
int career_been_to_shop(const unsigned char *shop)                       /* 80124440 */
{
    CareerRecord *r = career_record(rd32(shop + 0x38));
    return r && r->flag[CREC_DONE] == 5;
}
int career_shop_enterable(const unsigned char *shop)                     /* 801243b8 (r13-0x7134 cheat: off) */
{
    if (!shop || g_career.stage < (signed char)shop[0x9d]) return 0;
    if (shop[0x51]) {
        CareerRecord *r = career_record(rd32(shop + 0x38));
        if (!r || r->flag[CREC_ENTERED] != 1) return 0;
    }
    return career_shop_unlocked(shop);
}
int career_shop_on_radar(const unsigned char *shop)                      /* 80124484 */
{
    if (g_career.stage == 0 && !g_career.b873d && shop[0x50] != 4) return 0;
    return career_shop_enterable(shop);
}
/* ShowcaseState::IsEnterable 8012b124 (IsShowcaseVisibleOnRadar alike): the
   record (+0x20) unlocked (+4 == 2) and not done (+6 != 1) */
static int showcase_enterable(const unsigned char *sc)
{
    CareerRecord *r = sc ? career_record(rd32(sc + 0x20)) : NULL;
    return r && r->flag[CREC_ENTERED] == 2 && r->flag[CREC_DONE] != 1;
}
int career_showcase_enterable(const unsigned char *sc) { return showcase_enterable(sc); }
/* EventPile::IsWorldChallengeInPile: C5 */
static int wc_enterable(const unsigned char *wc)
{
    for (int i = 0; i < 32; i++) if (g_career.pile.challenge[i] == wc) return 1;
    return 0;
}
int career_zone_on_radar(const CareerZone *z)                            /* 80128ad4 */
{
    if (!z || !z->zone) return 0;
    if (z->shop) return career_shop_on_radar(z->shop);
    if (z->event) return career_event_on_radar(z->event);
    if (z->showcase) return showcase_enterable(z->showcase);
    if (z->wc) return wc_enterable(z->wc);
    return 0;
}
int career_zone_enterable(const CareerZone *z)                           /* 80123bd0 */
{
    if (!z || !z->zone) return 0;
    if (g_career.wcs.wc) {                                               /* only the goal */
        if (z->shop && career_wc_goal_shop(z->shop)) return 1;
        return z->showcase && rd32(z->showcase + 0x20) == rd32(g_career.wcs.wc + 0xc);
    }
    if (z->event) return career_event_in_pile(z->event);
    if (z->shop) return career_shop_enterable(z->shop);
    if (z->showcase) return showcase_enterable(z->showcase);
    if (z->wc) return wc_enterable(z->wc);
    return 0;
}
int career_zone_data(uint32_t zone, CareerZone *out)                     /* 801240bc */
{
    memset(out, 0, sizeof *out);
    /* no world-challenge case here: during one the pile is empty (no events), a
       shop zone still reaches NotifyShopDiscovered, and IsEnterable 80123bd0
       lets only the goal in */
    for (int i = 0; i < career_nevents(); i++)                           /* GetFirstUnlockedEvent 80124040 */
        if (rd32(career_event(i) + 0x28) == zone && career_event_in_pile(career_event(i))) { out->event = career_event(i); break; }
    if (!out->event && !(out->shop = shop_by_zone(zone)) && !(out->showcase = showcase_by_zone(zone)) &&
        !(out->wc = wc_by_zone(zone)))
        return 0;
    out->zone = zone;
    return 1;
}
void career_recalc_zones(void)                                           /* 8012924c */
{
    career_build_events();
    memset(g_cw.slot, 0, sizeof g_cw.slot);                              /* Reset 80129090 */
    if (g_career.wcs.wc) {                                               /* only the goal's zone (GetGoalZone 8012d1b4) */
        uint32_t gh = rd32(g_career.wcs.wc + 0xc);
        const unsigned char *sh = career_shop_by_handle(gh);
        CareerZone z = { 0 };
        if (sh) { z.zone = rd32(sh + 0x3c); z.shop = sh; }
        for (int i = 0; !sh && i < career_nshowcases(); i++)
            if (rd32(career_showcase(i) + 0x20) == gh) { z.zone = rd32(career_showcase(i) + 0x2c); z.showcase = career_showcase(i); }
        int found = 0;
        for (int k = 0; z.zone && k < g_cw.n && !found; k++) found = g_cw.z[k].hash == z.zone;
        if (found) cw_add(&z);
        return;
    }
    /* RecalcHelpTriggers: C8 */
    for (int i = 0; i < g_cw.n; i++) {
        if (g_cw.z[i].type < 13 || g_cw.z[i].type > 15) continue;
        CareerZone z = { g_cw.z[i].hash }, *added = NULL;
        const unsigned char *sh = shop_by_zone(z.zone), *sc = showcase_by_zone(z.zone);
        if (sh && career_shop_enterable(sh)) { z.shop = sh; added = cw_add(&z); }
        if (sc && showcase_enterable(sc)) { z.shop = NULL; z.showcase = sc; added = cw_add(&z); }
        (void)added;                                                     /* MinimapResyncBlink: C7 */
    }
    for (int i = 0; i < 64; i++) {
        const unsigned char *ev = g_career.pile.event[i];
        if (!ev) continue;
        uint32_t zh = rd32(ev + 0x28);
        int found = 0;
        for (int k = 0; k < g_cw.n && !found; k++) found = g_cw.z[k].hash == zh && g_cw.z[k].type >= 13 && g_cw.z[k].type <= 15;
        if (!found) continue;
        CareerZone z = { zh, ev };
        CareerZone *s = cw_add(&z);
        if (s) s->pile = 1;
    }
    for (int i = 0; i < 32; i++) {
        const unsigned char *wc = g_career.pile.challenge[i];
        if (!wc) continue;
        CareerZone z = { bstringhash(career_string(rd16(wc + 2))) };
        z.wc = wc;
        int found = 0;
        for (int k = 0; k < g_cw.n && !found; k++) found = g_cw.z[k].hash == z.zone && g_cw.z[k].type >= 13 && g_cw.z[k].type <= 15;
        if (found) cw_add(&z);
    }
}
/* NotifyEventDiscovered 80123aac / NotifyShopDiscovered 80123b38: the
   record's +4 seen; a hidden event / a must-enter shop seen the first time
   joins the radar (AddTriggerZoneToMinimap) */
static void cw_event_discovered(uint32_t zone, const unsigned char *ev)
{
    CareerRecord *r = career_add_record(rd32(ev + 8));
    if (!r) return;
    if (!r->flag[CREC_ENTERED] && ev[0x7f]) { CareerZone z = { zone, ev }; cw_add(&z); }
    r->flag[CREC_ENTERED] = 1;
}
static void cw_shop_discovered(uint32_t zone, const unsigned char *shop)
{
    if ((signed char)shop[0x9d] > g_career.stage) return;
    CareerRecord *r = career_add_record(rd32(shop + 0x38));
    if (!r) return;
    if (!r->flag[CREC_ENTERED] && shop[0x51]) { CareerZone z = { zone }; z.shop = shop; cw_add(&z); }
    r->flag[CREC_ENTERED] = 1;
}
const CareerZone *career_current_zone(void) { return g_cw.cur.zone ? &g_cw.cur : NULL; }
int career_player_zones(const uint32_t *zones, int n)                    /* 80218c44 */
{
    CareerZone z = { 0 };
    uint32_t at = 0;
    for (int i = 0; i < n && !at; i++) if (career_zone_data(zones[i], &z)) at = zones[i];
    if (at == g_cw.in_zone) return 0;   /* 80218c44: Exit / Enter only when the zone (+0x9a4) changes */
    int r = 0;
    if (g_cw.in_zone) {                                                  /* ExitEventTriggerZone 80218ab4 */
        if (g_cw.cur.zone) {
            g_cw.left = g_cw.cur;                                             /* FEngHud::ExitTriggerZone's zone */
            memset(&g_cw.cur, 0, sizeof g_cw.cur);                       /* NotifyExitTriggerZone 80123f74 */
            r |= CZONE_EXIT;                                             /* showcase / challenge exits: C9 / C5 */
        }
    }
    if (at) {                                                            /* EnterEventTriggerZone 80218b70 */
        if (z.shop) cw_shop_discovered(at, z.shop);
        if (career_zone_enterable(&z)) {
            g_cw.cur = z;                                                /* NotifyEnterTriggerZone 80123dc0 */
            if (z.event) cw_event_discovered(at, z.event);
            /* NotifyEnterShopTriggerArea 8012d04c: the goal shop reached */
            else if (z.shop && career_shop_enterable(z.shop) && career_wc_goal_shop(z.shop)) {
                g_career.wcs.state = CWC_AT_SHOP;
                g_career.wcs.failed = 0;
                g_cw.engaging = 1; g_cw.engaged = g_cw.cur;
                r |= CZONE_AUTO;
            } else if (z.showcase) {
                if (g_career.wcs.wc && rd32(z.showcase + 0x20) == rd32(g_career.wcs.wc + 0xc))
                    g_career.wcs.state = CWC_AT_SHOWCASE;                /* NotifyEnterShowcase 8012d14c */
                /* ShowcaseState::NotifyEnteredZone 8012b1a4: an enterable one's
                   engage dialog pops by itself the first time (+5 bit0); the
                   snapshot time limit (+0x30) is 0 in all the data */
                CareerRecord *sr = career_record(rd32(z.showcase + 0x20));
                if (showcase_enterable(z.showcase) && !g_cw.engaging && sr && !(sr->flag[1] & 1)) {
                    sr->flag[1] |= 1;
                    g_cw.engaging = 1; g_cw.engaged = g_cw.cur;
                    r |= CZONE_AUTO;
                }
            }
            /* SMS NotifyEnterTriggerZone (C8), challenge notices (C5) */
            r |= CZONE_ENTER;
        }
    }
    g_cw.in_zone = at;
    return r;
}
int career_can_engage(void)                                              /* 801244d4 */
{
    const CareerZone *z = &g_cw.cur;
    if (g_cw.engaging) return 0;                                         /* IsEngagingDialog */
    if (g_career.wcs.wc) return career_zone_enterable(z);                /* the goal only */
    return (z->shop && career_shop_enterable(z->shop)) || (z->event && career_event_in_pile(z->event)) ||
           (z->showcase && showcase_enterable(z->showcase)) || (z->wc && wc_enterable(z->wc));
}
void career_engage_requested(void) { g_cw.engaged = g_cw.cur; }
void career_reset_engaged(void) { memset(&g_cw.engaged, 0, sizeof g_cw.engaged); }   /* ResetEngagedTriggerZone 80126a90 (+0x7df0..+0x7dfc) */
int  career_engaging_dialog(void) { return g_cw.engaging; }
void career_set_engaging_dialog(int on) { g_cw.engaging = on != 0; }
const CareerZone *career_engaged(void) { return g_cw.engaged.zone ? &g_cw.engaged : NULL; }
const CareerZone *career_left_zone(void) { return g_cw.left.zone ? &g_cw.left : NULL; }
/* _get_career_event_indicator_type 80111c1c: by race type (+0xf), URL
   (+0x34 == 2) 7, SUV (+0xd == 1) 8 */
static int ei_event_type(const unsigned char *ev)
{
    int t;
    switch ((signed char)ev[0xf]) {
    case 0: case 3: t = 1; break;
    case 1: t = 4; break;
    case 4: t = 2; break;
    case 5: t = 3; break;
    default: t = 5; break;
    }
    if (ev[0x34] == 2) return 7;
    return ev[0xd] == 1 ? 8 : t;
}
static int ei_shop_type(const unsigned char *shop)                       /* 80111ca4 */
{
    switch ((signed char)shop[0x50]) {
    case 0: return 0xc;
    case 1: return 0xf;
    case 2: return 0xd;
    case 3: return 0xe;
    case 4: return 0xb;
    case 5: return 0x10;
    default: return 1;
    }
}
int career_zone_indicator(const CareerZone *z)                           /* FEngHud::Enter/ExitTriggerZone 801136d4 */
{
    if (!z || !z->zone) return 0;
    if (z->shop) return ei_shop_type(z->shop);
    if (z->event) return ei_event_type(z->event);
    if (z->showcase) return 0x11;                                        /* 80111d18 */
    if (z->wc) return 0x12;
    return 0;
}

/* RaceEngageDialog / EngageShopDialog text (CAREER.md "C4e: engaging") */
uint32_t career_engage_body(const unsigned char *ev, int (*rnd)(int n))  /* GetEngageDialogBody 80121b88 */
{
    char b[96];
    if (!ev) return 0;
    if (!ev[0x37]) {                                                     /* stage 0: its own message */
        snprintf(b, sizeof b, "ENGAGE_%s", career_string(rd16(ev)));
        return bstringhash(b);
    }
    const char *f = NULL; int n = 0;
    signed char rt = (signed char)ev[0xf];
    if (ev[0xd] == 1) { f = "ENGAGE_SUV_RANDOM_%d"; n = 9; }
    else if (ev[0x34] == 1) {
        if (rt == 0) { f = "ENGAGE_SPON_CIRCUIT_RANDOM_%d"; n = 11; }
        else if (rt == 1) { f = "ENGAGE_SPON_SPRINT_RANDOM_%d"; n = 10; }
        else if (rt == 2) { f = "ENGAGE_SPON_STREET_RANDOM_%d"; n = 10; }
        else if (rt == 4) { f = "ENGAGE_SPON_DRAG_RANDOM_%d"; n = 8; }
        else if (rt == 5) { f = "ENGAGE_SPON_DRIFT_RANDOM_%d"; n = 11; }
    } else if (ev[0x34] == 2) { f = "ENGAGE_URL_RANDOM_%d"; n = 12; }
    else {
        if (rt == 0) { f = "ENGAGE_CIRCUIT_RANDOM_%d"; n = 18; }
        else if (rt == 1) { f = "ENGAGE_SPRINT_RANDOM_%d"; n = 15; }
        else if (rt == 2) { f = "ENGAGE_STREET_RANDOM_%d"; n = 14; }
        else if (rt == 3) n = 10;                                        /* no format (the original hashes NULL) */
        else if (rt == 4) { f = "ENGAGE_DRAG_RANDOM_%d"; n = 12; }
        else if (rt == 5) { f = "ENGAGE_DRIFT_RANDOM_%d"; n = 13; }
    }
    int k = (rnd && n > 0 ? rnd(n) : 0) + 1;                             /* bRandom(n) + 1 */
    if (!f) return 0;
    snprintf(b, sizeof b, f, k);
    return bstringhash(b);
}
/* GetEngageDialogFrom 80121d54: opponent 0's name, or (NULL returned) the
   label at +0x84 in *label */
const char *career_engage_from(const unsigned char *ev, uint32_t *label)
{
    *label = 0;
    if (!ev) return "";
    if (rd32(ev + 0x84)) { *label = rd32(ev + 0x84); return NULL; }
    return career_string(rd16(ev + 0x40));
}
/* RaceEngageDialog::Setup (PC 0x4b53c0): Subject's type name and Race
   Type's label, by event class (+0x34), SUV (+0xd) and race type (+0xf) */
void career_engage_race_type(const unsigned char *ev, uint32_t *subject, uint32_t *type)
{
    uint32_t s = 0x9b39c122u, t = 0x268d063cu;                           /* tutorial / default */
    signed char rt = (signed char)ev[0xf];
    if (ev[0xd] == 1 || ev[0x34] == 3) {                                 /* world, every SUV */
        if (rt == 1) { s = 0x9ec5f82fu; t = 0xba613729u; }
        else if (rt == 2) { s = 0x57f833adu; t = 0x0fda6758u; }
        else if (rt == 4) { s = 0xe45a7387u; t = 0x40c91ba7u; }
        else if (rt == 5) { s = 0x3a10a7c2u; t = 0x59ecb2c2u; }
    } else if (ev[0x34] == 2) { s = 0x80b8267cu; t = 0xb46377bcu; }     /* URL */
    else if (ev[0x34] == 1) {                                            /* sponsor */
        if (rt == 0) { s = 0x0f037a7bu; t = 0x268d063cu; }
        else if (rt == 1) { s = 0x07216a68u; t = 0xba613729u; }
        else if (rt == 2) { s = 0xc5f7902cu; t = 0x0fda6758u; }
        else if (rt == 4) { s = 0x3d6a70e6u; t = 0x40c91ba7u; }
        else if (rt == 5) { s = 0xb5205101u; t = 0x59ecb2c2u; }
    }
    *subject = s; *type = t;
}
uint32_t career_shop_header(const unsigned char *shop)                   /* ShopDataDesc::GetEngageDialogHeader 80122158 */
{
    if (rd32(shop + 0x38) == 0x7fc8d0d0u) return 0x6aa654c5u;            /* CC_MEGALOW */
    switch ((signed char)shop[0x50]) {
    case 0: return 0x7ce21662u;
    case 1: return 0xa5f04bd7u;
    case 2: return 0x0b9253e9u;
    case 3: return 0xaf258ee8u;
    case 4: return 0x4dc0a646u;
    case 5: return 0xa4e5ee6cu;
    default: return 0;
    }
}
/* ShopDataDesc::GetEngageDialogBody 80122214: a crib 0x987eecda; a shop not
   entered yet its "<name>_BODY" when the language has it, else 0x15d6b438 */
uint32_t career_shop_body(const unsigned char *shop, int (*has_label)(uint32_t))
{
    if (!shop[0x50]) return 0x987eecdau;
    if (!career_been_to_shop(shop)) {                                    /* HasBeenEntered: record +6 == 5 */
        char b[0x40 + 8];
        snprintf(b, sizeof b, "%.56s_BODY", (const char *)shop);
        uint32_t h = bstringhash(b);
        if (has_label && has_label(h)) return h;
    }
    return 0x15d6b438u;
}
uint32_t career_shop_accept(const unsigned char *shop) { return shop[0x50] ? 0x054b17dcu : 0x67f55a89u; }

/* NotifyEnterEvent 80126318: the engaged event (+0x7e1c, its preset ride),
   the race counter (+0x7e18), the record in progress (2) unless won, the
   speech's event begin; the spot it was entered from kept without
   has_spawn (+0x7e3c = 0). EventInProgressState::NotifyInitialize (C4) and
   GPSManager::Stop: not ported. */
void career_enter_event(const unsigned char *ev, const float pos[3], uint16_t heading)
{
    CareerState *c = &g_career;
    if (!ev) return;
    c->engaged_event = ev;
    c->engaged_preset = rd32(ev + 0x2c);
    c->u872c++;
    CareerRecord *r = career_add_record(rd32(ev + 8));
    if (r && r->flag[CREC_DONE] != CDONE_WON) r->flag[CREC_DONE] = 2;
    cs_event_begin(ev);
    if (pos) {
        c->has_spawn = 0;
        c->spawn[0] = pos[0]; c->spawn[1] = pos[1]; c->spawn[2] = pos[2];
        c->spawn_heading = heading;
    }
}
/* NotifyEnterFreeRoamFromPostRace 80126918, after MarkCurrentEventComplete
   (the caller's commit): CalcShowPostRaceMovie 801267f0 -- the event's own
   post movie (+4) once it is completed; else Scene07 when the win moved the
   career into stage 2 (the event's stage, +0x37, not the current one after
   the commit); else the event 0x4cce3e1's Scene16 / Scene15. None for a
   race started from the crib World Map (EventInProgressState +0xc,
   StartCareerEventFromWorldMap 8011d380) -- then the
   engaged event is done with (+0x7e1c = 0, NotifyUserIsDoneWithEvent) and
   the next career start uses the spawn (+0x7e3c = 1). 1 = a movie in out. */
int career_post_race_free_roam(const unsigned char *ev, int from_wmap, char *out, int n)
{
    CareerState *c = &g_career;
    int movie = 0;
    if (ev && out && n > 0 && !from_wmap) {
        const char *pm = career_string(rd16(ev + 4));
        const char *name = NULL;
        if (pm && pm[0] && career_has_completed(rd32(ev + 8))) name = pm;
        else if ((signed char)ev[0x37] != c->stage && c->stage == 2) name = "Scene07";
        else if (rd32(ev + 8) == 0x4cce3e1u) name = career_has_completed(0x4cce3e1u) ? "Scene15" : "Scene16";
        if (name) { snprintf(out, (size_t)n, "%s", name); movie = 1; }
    }
    c->engaged_preset = 0;
    c->engaged_event = NULL;
    c->has_spawn = 1;
    return movie;
}
void career_decline_event(const unsigned char *ev)                       /* NotifyDeclineEvent 801265e0 */
{
    CareerRecord *r = ev ? career_add_record(rd32(ev + 8)) : NULL;
    if (r) r->flag[CREC_DONE] = 3;
}
void career_decline_shop(const unsigned char *shop) { (void)shop; }      /* NotifyDeclineEnterShopFromFreeRoam 80126f48: empty */
static const unsigned char *bank_by_zone(uint32_t zone)                  /* FindCareerBankTrigger 801233bc */
{
    for (long k = 0; k + CAR_BANK <= TN[13]; k += CAR_BANK) if (rd32(T[13] + k + 8) == zone) return T[13] + k;
    return NULL;
}
static int bank_claimable(const unsigned char *b)                        /* CanClaimBankReward 80126164 */
{
    if (!b) return 0;
    if (b[2] == 1 && g_career.stage <= (int)rd32(b + 4)) return 0;
    if (b[2] == 2 && !career_has_completed(rd32(b + 4))) return 0;
    return !g_career.a8768[b[3]];                                        /* +0x8768 the claimed banks */
}
int career_bank_claimable(uint32_t zone) { return bank_claimable(bank_by_zone(zone)); }
int career_bank_zone(uint32_t zone)                                      /* 801262b8 -> ClaimBankReward 80126250 */
{
    const unsigned char *b = bank_by_zone(zone);
    if (!zone || !bank_claimable(b)) return 0;
    g_career.cash += rd16(b);
    g_career.a8768[b[3]] = 1;
    return rd16(b);
}

/* ---------------------------------------------------------- CareerSpeech */

/* the PC layout over g_career.speech (Default 0x531800): +1 u8 DDAY_A done,
   +4 s32 the stage-0 timeout chain, +8 u8 the first stage-race line said,
   +0xa5 u8 the stage, +0xa8 8 timer events {s32 ticks, s32 event, u32 param}.
   Ticks are the game's Timer (SetTime: seconds * 4000 + 0.5). */
#define CS_TICKS(sec) ((int32_t)((sec) * 4000.0f + 0.5f))
void (*career_speech_hook)(int kind, uint32_t a, uint32_t b);
static int32_t cs_get(int off) { int32_t v; memcpy(&v, g_career.speech + off, 4); return v; }
static void cs_put(int off, int32_t v) { memcpy(g_career.speech + off, &v, 4); }
static void cs_say(int kind, uint32_t a, uint32_t b) { if (career_speech_hook) career_speech_hook(kind, a, b); }
int career_speech_queued(int ev)                                         /* GC 801895a0 */
{
    for (int i = 0; i < 8; i++) if (cs_get(0xac + 12 * i) == ev) return 1;
    return 0;
}
static void cs_add(int32_t ticks, int ev, uint32_t param)                /* AddTimerEvent 0x5111f0 */
{
    for (int i = 0; i < 8; i++)
        if (!cs_get(0xac + 12 * i)) {
            cs_put(0xa8 + 12 * i, ticks); cs_put(0xac + 12 * i, ev); cs_put(0xb0 + 12 * i, (int32_t)param);
            return;
        }
}
static void cs_add_once(int32_t ticks, int ev) { if (!career_speech_queued(ev)) cs_add(ticks, ev, 0); }
/* NotifyCSStage0Timeout / NotifyCSCanSeeEvents (8012ae1c / 8012ae4c): the
   flag, then RecalcUnlockedZones + NotifyMinimapNewRace (the world layer
   reads the flag, C3/C7) */
static void cs_stage0_timeout(void) { g_career.b873e = 1; }
static void cs_can_see_events(void) { g_career.b873f = 1; }
/* GetCsisMagName / GetCsisDvdName: the showcase name hash -> its Csis
   Mag_Name / Dvd_Name bit (PC tables 0x7fa3f0, 20 pairs; 0x7fa490, 10 pairs);
   not found = the first entry (AutoMaxx 0x10 / Bayview's Best 1) */
static const uint32_t k_cs_mag[20][2] = {
    { 0x9e101f88u, 0x10 }, { 0x601410dcu, 0x80 }, { 0x601417c4u, 0x4 }, { 0x9e102056u, 0x40000 },
    { 0x9e10211bu, 0x40 }, { 0x9e102177u, 0x1000000 }, { 0x9e1021d0u, 0x8000 }, { 0x72eca090u, 0x8000 },
    { 0x601462b5u, 0x1 }, { 0x601417c3u, 0x20 }, { 0x6014211au, 0x8 }, { 0x9e102097u, 0x1000 },
    { 0x9e1021ddu, 0x200000 }, { 0x60145e23u, 0x2000 }, { 0x60145e47u, 0x400000 }, { 0x9e1021e0u, 0x800 },
    { 0x60142b3bu, 0x100000 }, { 0xb5cbefb2u, 0x800000 }, { 0x9e1021e1u, 0x2000000 }, { 0x60145eb8u, 0x4000 } };
static const uint32_t k_cs_dvd[10][2] = {
    { 0x9e101f9eu, 0x1 }, { 0x9e101fb3u, 0x8 }, { 0x60145c24u, 0x100000 }, { 0x629fedbau, 0x20 },
    { 0x601466bcu, 0x4 }, { 0x62968b8eu, 0x40 }, { 0x6298f309u, 0x2 }, { 0x629cdefcu, 0x200 },
    { 0x60145be1u, 0x10 }, { 0x601459f4u, 0x80 } };
static uint32_t cs_mag_name(uint32_t h)
{
    for (int i = 0; i < 20; i++) if (k_cs_mag[i][0] == h) return k_cs_mag[i][1];
    return k_cs_mag[0][1];
}
static uint32_t cs_dvd_name(uint32_t h)
{
    for (int i = 0; i < 10; i++) if (k_cs_dvd[i][0] == h) return k_cs_dvd[i][1];
    return k_cs_dvd[0][1];
}
/* HandleSpeechEvent 0x53e0b0 (GC 801895d0) */
static void cs_event(int ev, uint32_t param)
{
    unsigned char *sp = g_career.speech;
    (void)param;
    switch (ev) {
    case 1: {                                   /* the stage-0 race chain: Type_TimeProgress 1, 4, 8 */
        int32_t st = cs_get(4), say = 1;
        if (st == 3) return;
        if (st == 0) cs_put(4, 1);
        else if (st == 1) { say = 4; cs_put(4, 2); }
        else if (st == 2) { cs_put(4, 3); say = 8; cs_stage0_timeout(); }
        cs_say(CSPK_TIMED, (uint32_t)say, 1);
        return;
    }
    case 2: cs_say(CSPK_IE, 0x400000, 8); cs_can_see_events(); return;
    case 3: cs_say(CSPK_IE, 0x8000, 1); return;
    case 4: cs_say(CSPK_CAREER, 0x2000, 0x20); cs_say(CSPK_SMS, 0x51b30cec, 0); career_sms_add_external(0x51b30cec); return;
    case 5: if (sp[8]) return; cs_say(CSPK_CAREER, 1, 0x20); sp[8] = 1; return;
    case 6: cs_say(CSPK_CAREER, 0x1000000, 0x20); cs_say(CSPK_SMS, 0x83914c51, 0); career_sms_add_external(0x83914c51); return;
    case 7: cs_say(CSPK_CAREER, 0x40, 0x20); return;
    case 8: cs_say(CSPK_CAREER, 0x80, 0x20); return;
    case 9: cs_say(CSPK_CAREER, 0x100, 0x20); return;
    case 10: cs_say(CSPK_CAREER, 0x800000, 0x10); return;
    case 11: cs_say(CSPK_CAREER, 0x20000, 0x20); return;
    case 12: {                                  /* a new stage: Type_Locations 1, 2, 4, 4 for stages 2..5 */
        static const uint32_t loc[4] = { 1, 2, 4, 4 };
        if (sp[0xa5] >= 2 && sp[0xa5] <= 5) cs_say(CSPK_IA, loc[sp[0xa5] - 2], 0x20);
        sp[0xa5] = 0;
        return;
    }
    case 13: cs_say(CSPK_DVD, cs_dvd_name(param), 1); sp[0x18] = 0; return;   /* GetCsisDvdName; DVD pending off */
    case 14: cs_say(CSPK_MAG, cs_mag_name(param), 1); return;                 /* GetCsisMagName */
    case 15: if (param >= 1 && param <= 10) cs_say(CSPK_DVD, 0x400u << (param - 1), 4); return;   /* stars 0x7fa4e0 */
    default: return;
    }
}
void career_speech_tick(uint32_t dt_ms, int speech_done)                 /* UpdateTimerEvents 0x54bd.. */
{
    int32_t dt = (int32_t)(dt_ms * 4), best = 0;
    for (int i = 0; i < 8; i++)
        if (cs_get(0xac + 12 * i)) {
            int32_t t = cs_get(0xa8 + 12 * i) - dt;
            cs_put(0xa8 + 12 * i, t);
            if (t < cs_get(0xa8 + 12 * best)) best = i;
        }
    cs_put(0x114, cs_get(0x114) + dt);                                   /* the WorldTimer ticks */
    int ev = cs_get(0xac + 12 * best);
    if (ev && cs_get(0xa8 + 12 * best) <= 0 && speech_done) {
        uint32_t param = (uint32_t)cs_get(0xb0 + 12 * best);
        cs_put(0xa8 + 12 * best, 0); cs_put(0xac + 12 * best, 0); cs_put(0xb0 + 12 * best, 0);
        cs_event(ev, param);
    }
    /* HandleTick 0x551070, after the timers: the DVD-star line on the FE
       event 2 pulse (+0x28) -- sponsor seen (+0xc), no DVD line pending
       (+0x18), a star requirement (+0x19) not said yet (+0xd + stars) */
    unsigned char *sp = g_career.speech;
    if (sp[0x28]) {
        int st = sp[0x19];
        if (sp[0xc] && !sp[0x18] && st && st <= 10 && !sp[0xd + st]) {
            cs_add(CS_TICKS(1.0f), 15, (uint32_t)st);
            sp[0xd + st] = 1; sp[0x19] = 0;
        }
        sp[0x28] = 0;
    }
}
void career_speech_real(uint32_t ms)                                     /* HandleTick: the RealTimer +0x11c */
{
    cs_put(0x11c, cs_get(0x11c) + (int32_t)(ms * 4));
}

/* HandleNeighbourhood 8018a54c (from HandleTick, career stage > 1): the
   player's neighbourhood (Player +0x9b0, a NEIGHBOURHOOD_%s hash) changed,
   the speech is done and 300 s of world time passed since the last intro
   (+0x54 / +0x58; kept here, not in the saved block). The caller then
   draws GetRandomHiddenEventFromPile and gives its zone's neighbourhood
   to career_speech_hood_say. */
static uint32_t cs_hood_cur;
static int32_t cs_hood_time;
int career_speech_hood_due(uint32_t hood, int speech_done)
{
    if (!hood || g_career.stage <= 1) return 0;
    if (!cs_hood_cur) { cs_hood_cur = hood; cs_hood_time = cs_get(0x114); }
    if (hood == cs_hood_cur) return 0;
    cs_hood_cur = hood;
    return speech_done && cs_get(0x114) - cs_hood_time >= CS_TICKS(300.0f);
}
void career_speech_hood_say(uint32_t hood)                               /* IE(loc, 8, 0x10) */
{
    static const uint32_t tab[][2] = {
        { 0x5a5d44a3u, 0x10 }, { 0x30372bbfu, 0x800000 }, { 0x101a3187u, 1 }, { 0x1024215du, 1 },
        { 0x4d451881u, 0x40000 }, { 0x31ab5422u, 0x80 }, { 0x5755251fu, 0x200000 },
        { 0xb8fce39au, 0x1000000 }, { 0xab3ababau, 0x100000 }, { 0xafa49decu, 0x40 },
        { 0xbb12679eu, 0x10000000 }, { 0xbb0877c8u, 0x8000000 }, { 0xbc4c1f6eu, 0x8000 } };
    if (!hood || hood == cs_hood_cur) return;
    for (int i = 0; i < (int)(sizeof tab / sizeof tab[0]); i++)
        if (tab[i][0] == hood) {
            cs_say(CSPK_IE, tab[i][1], 8 | (uint32_t)0x10 << 16);   /* CharA 8, CharB 0x10 */
            cs_hood_time = cs_get(0x114);
            return;
        }
}
/* GetNeighbourhoodNameInfo 801f3f1c: the table 8036fbc0 by bStringHash of the
   name (a neighbourhood zone's +0x30) */
const char *career_neighbourhood_name(uint32_t zone_hash)
{
    static const char *const names[13] = {
        "JACKSON_HEIGHTS", "PIGEON_PARK", "BEACON_HILL_EAST", "BEACON_HILL_WEST", "HOTELS", "FINANCE",
        "FORT_UNION", "EL_NORTE", "SOUTH_MARKET", "STADIUM", "AIRPORT", "COAL_HARBOUR_EAST", "COAL_HARBOUR_WEST" };
    for (int i = 0; i < 13; i++) if (bstringhash(names[i]) == zone_hash) return names[i];
    return NULL;
}

/* the purging SpeechCmd calls: PurgeSpeech, then the line */
static void cs_say_purge(int kind, uint32_t a, uint32_t b) { cs_say(CSPK_PURGE, 0, 0); cs_say(kind, a, b); }
/* HandleFESpeechEvent 0x5243d0 (GC 80189940): career only; in the crib
   (the engaged shop's +0x50 == 0, or event 0x11) events 1, 2, 0x12 become
   0x11 (and event 1 at shop 0x7fc8d0d0 becomes 0); 4 and 0x11 say their
   line once (+9 / +0xa) and queue timer events; the rest repeat no sooner
   than 3600 s by the RealTimer (+0x120 + 4 ev = the last time) */
int career_speech_fe(int ev)
{
    static int in_crib;                                                  /* 0x8641f8 */
    unsigned char *sp = g_career.speech;
    if (ev == 0x11) in_crib = 1;
    if (g_career.shop) {
        in_crib = g_career.shop[0x50] == 0;
        if (rd32(g_career.shop + 0x38) == 0x7fc8d0d0u && ev == 1) ev = 0;
    }
    if (in_crib && (ev == 1 || ev == 2 || ev == 0x12)) ev = 0x11;
    if (ev == 4) {
        if (sp[9]) goto cooldown;
        sp[9] = 1;
        cs_say(CSPK_FES, 0x20, 0x20);
        cs_add(CS_TICKS(1.0f), 4, 0);
        return 1;
    }
    if (ev == 0x11) {
        if (sp[0xa]) goto cooldown;
        sp[0xa] = 1;
        cs_say(CSPK_FES, 0x100000, 0x10);
        cs_add(CS_TICKS(1.0f), 6, 0);
        cs_add(120000, 10, 0);                                           /* 30 s */
        return 1;
    }
    if (ev == 2) sp[0x28] = 1;
cooldown:
    if (ev < 0 || ev > 0x1b) return 0;
    int32_t last = cs_get(0x120 + 4 * ev);
    int played = last != 0 && last != 0x7fffffff;
    if (played && cs_get(0x11c) - last <= CS_TICKS(3600.0f)) return 0;
    static const uint32_t screen[0x13] = { 0x400000, 0x800000, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x400,
        0x800, 0x2000, 0x4000, 0x8000, 0x10000, 0x20000, 0x40000, 0x80000, 0, 0x2000000 };
    if (ev >= 0x13) cs_say_purge(CSPK_FESDYNO, 1u << (ev - 0x13), 0x10);
    else if (ev == 0x11) return 0;
    else cs_say_purge(CSPK_FES, ev <= 1 && played ? 1 : screen[ev], 0x20);
    cs_put(0x120 + 4 * ev, cs_get(0x11c) ? cs_get(0x11c) : 1);
    return 1;
}
/* HandleEngageRequest 0x5251a0 (GC 8018a05c): the three stage-0 races' one
   line each (+0x24 STAGE_0_CIRCUIT_0, +0x25 _1, +0x26 SPRINT_0; +0x27 = a
   line of this dialog), or an engaged showcase's magazine / DVD line */
void career_speech_engage(const unsigned char *ev, const unsigned char *sc)
{
    unsigned char *sp = g_career.speech;
    sp[0x27] = 0;
    if (ev) {
        uint32_t h = rd32(ev + 8); int f = -1; uint32_t cr = 0, ch = 0;
        if (h == 0xca207583u) { f = 0x24; cr = 0x80000; ch = 1; }
        else if (h == 0xca207584u) { f = 0x25; cr = 0x100000; ch = 8; }
        else if (h == 0x1bcb7ab0u) { f = 0x26; cr = 0x200000; ch = 0x200; }
        if (f >= 0 && !sp[f]) { cs_say_purge(CSPK_CAREER, cr, ch); sp[f] = 1; sp[0x27] = 1; }
    } else if (sc) {
        if (sc[0x24] == 1 || sc[0x24] == 3) cs_say_purge(CSPK_MAG, cs_mag_name(rd32(sc + 0x28)), 2);
        else if (sc[0x24] == 2 || sc[0x24] == 4) cs_say_purge(CSPK_DVD, cs_dvd_name(rd32(sc + 0x28)), 2);
    }
}
void career_speech_decline(void)
{
    if (g_career.speech[0x27]) { g_career.speech[0x27] = 0; cs_say(CSPK_PURGE, 0, 0); }
}
/* HandleOutrunStart 0x511000: the first outrun (+1 clear) its own line;
   later a taunt (+0x1a = bRandom(5), table 8032d4b8 {1, 2, 4, 8, 0x200}) */
static const uint32_t k_cs_taunt[5] = { 1, 2, 4, 8, 0x200 };
void career_speech_outrun_start(void)
{
    unsigned char *sp = g_career.speech;
    if (!sp[1]) { cs_say_purge(CSPK_OUTRUN, 0x20, 0x200); return; }
    sp[0x1a] = (unsigned char)airng_int(&g_rng, 5);
    cs_say_purge(CSPK_OUTRUN, 1, k_cs_taunt[sp[0x1a] % 5]);
}
/* HandleOutrunEnd 0x5110c0: the first one lose 0x80 / win 0x40, then +1;
   later lose 4 or 2, win 8 or 0x10 (bRandom(2)) by the saved taunt */
void career_speech_outrun_end(int won)
{
    unsigned char *sp = g_career.speech;
    if (!sp[1]) { cs_say_purge(CSPK_OUTRUN, won ? 0x40 : 0x80, 0x200); sp[1] = 1; return; }
    int r = airng_int(&g_rng, 2);
    cs_say_purge(CSPK_OUTRUN, won ? (r ? 0x10 : 8) : (r ? 2 : 4), k_cs_taunt[sp[0x1a] % 5]);
}
/* HandleSponsorSelect 0x525510: latched (+0xb) until Done; the first time
   ever (+0xc) Career 0x8000000 + event 11, else Career 0x10 */
void career_speech_sponsor(void)
{
    unsigned char *sp = g_career.speech;
    if (sp[0xb]) return;
    sp[0xb] = 1;
    if (!sp[0xc]) { sp[0xc] = 1; cs_say(CSPK_CAREER, 0x8000000, 0x20); cs_add(CS_TICKS(1.0f), 11, 0); }
    else cs_say(CSPK_CAREER, 0x10, 0x20);
}
void career_speech_sponsor_done(void) { g_career.speech[0xb] = 0; }
/* UniquesBrowser::NotificationMessage (GC 8016cc14) on 0xd05fc3a3: +0x5c == 0
   (a visual part: InstallPart + BuyCarPart) -> HandleUniqueVisualChosen
   8018a4ac = event 8; else InstallJunkmanPart -> HandleUniquePerformanceChosen
   8018a4fc = event 9 (PC 0x5255d0 / 0x525610, called at 0x561866 / 0x561830).
   CONFIRMED G5 11-01 (was a guess from the PC order) */
void career_speech_uniques(int visual) { cs_add(CS_TICKS(1.0f), visual ? 8 : 9, 0); }
/* HandleShowcaseUnlock 0x5253f0: a magazine (type 1/3) not in the list
   (+0x2c, count +0xa4) goes in it, event 14; a DVD (2/4) not in it, with
   the FE event 2 pulse (+0x28) on, goes in it, event 13, DVD pending (+0x18) */
void career_speech_showcase_unlock(const unsigned char *sc)
{
    unsigned char *sp = g_career.speech;
    uint32_t h = rd32(sc + 0x28);
    int n = sp[0xa4];
    for (int i = 0; i < n; i++) if ((uint32_t)cs_get(0x2c + 4 * i) == h) return;   /* FUN_005112f0 */
    if (n >= 30) return;
    if (sc[0x24] == 1 || sc[0x24] == 3) {
        cs_put(0x2c + 4 * n, (int32_t)h); sp[0xa4] = (unsigned char)(n + 1);
        cs_add(CS_TICKS(1.0f), 14, h);
    } else if ((sc[0x24] == 2 || sc[0x24] == 4) && sp[0x28]) {
        cs_put(0x2c + 4 * n, (int32_t)h); sp[0xa4] = (unsigned char)(n + 1);
        cs_add(CS_TICKS(1.0f), 13, h);
        sp[0x18] = 1;
        int st = (int)rdf(sc + 0x3c);                                    /* said[stars] */
        if (st >= 1 && st <= 10) sp[0xd + st] = 1;
    }
}
void career_speech_dvd_stars(int stars) { g_career.speech[0x19] = (unsigned char)stars; }
/* HandleShowcaseMode 0x525850 / GC 8018a85c, from the photo screen's 0x35f8620b
   (ShowcaseMagazineDialog::NotificationMessage 0x4cf170): Career(0x10000, 0x20)
   once; the flag is the saved byte +0xa6 (GC +0xec), cleared by Default 0x531800 */
void career_speech_showcase_mode(void)
{
    if (g_career.speech[0xa6]) return;
    cs_say(CSPK_CAREER, 0x10000, 0x20);
    g_career.speech[0xa6] = 1;
}
static void cs_event_begin(const unsigned char *ev)                      /* HandleCareerEventBegin 0x524240 */
{
    if (rd32(ev + 8) != CAREER_DDAY_A) return;
    cs_add_once(CS_TICKS(15.0f), 2);
    cs_add_once(CS_TICKS(40.0f), 3);
}
static void cs_event_end(const unsigned char *ev, int won)               /* HandleCareerEventEnd 0x5242c0 */
{
    uint32_t h = rd32(ev + 8);
    if (h == 0xca207583u || h == 0xca207584u || h == 0x1bcb7ab0u)        /* STAGE_0_CIRCUIT_0/1, STAGE_0_SPRINT_0 */
        cs_add_once(CS_TICKS(1.0f), 1);
    else if (h == CAREER_DDAY_A) { cs_put(4, 3); g_career.speech[1] = 1; }
    if (ev[0x34] && won) {
        if (!g_career.speech[8] && ev[0x37] == 1) cs_add(CS_TICKS(1.0f), 5, 0);
        const unsigned char *sd = g_career.st[(signed char)ev[0x37] & 7].stage;
        if (sd && rd32(sd + 0x28) == h && ev[0x37] != 5) cs_add(CS_TICKS(1.0f), 7, 0);
    }
}
static void cs_stage_started(int stage)                                  /* HandleStageStarted, in 0x528320 */
{
    if (!g_career.speech[0xa5]) cs_add(CS_TICKS(15.0f), 12, 0);
    g_career.speech[0xa5] = (unsigned char)stage;
}

/* --------------------------------------------------- starting a career */

const unsigned char *career_shop_by_handle(uint32_t h)
{
    for (long k = 0; k + CAR_SHOP <= TN[1]; k += CAR_SHOP)
        if (rd32(T[1] + k + 0x38) == h) return T[1] + k;
    return NULL;
}
const unsigned char *career_shop_of_type(int type)
{
    for (long k = 0; k + CAR_SHOP <= TN[1]; k += CAR_SHOP)
        if ((signed char)T[1][k + 0x50] == type) return T[1] + k;
    return NULL;
}
#ifdef OPENUG2_SIM_PRE_H
/* test aid (simulator): every must-enter shop of the stage so far as if
   driven into (NotifyShopDiscovered's +4 = 1) */
void career_sim_find_shops(void)
{
    for (long k = 0; k + CAR_SHOP <= TN[1]; k += CAR_SHOP) {
        const unsigned char *sh = T[1] + k;
        if (!sh[0x51] || (signed char)sh[0x9d] > g_career.stage) continue;
        CareerRecord *r = career_add_record(rd32(sh + 0x38));
        if (r && !r->flag[CREC_ENTERED]) { r->flag[CREC_ENTERED] = 1; printf("sim: shop %.16s found\n", (const char *)sh + 0x40); }
    }
}
#endif
static const unsigned char *shop_event_now(void)               /* A, or B once A is won */
{
    return career_event_by_handle(career_has_completed(CAREER_DDAY_A) ? CAREER_DDAY_B : CAREER_DDAY_A);
}
/* StartCareerHelper 800d1b30 (UI_StartCareer accept): with DDAY_EVENT_B
   won the career goes to the crib (UI_CareerCrib); before the first movie
   it plays scene01 (LaunchFirstCareerMovie); else free roam resumes at the
   saved spawn with A or B current (0x803bbee0) */
int career_free_roam_start(void);
int career_start(void)
{
    if (career_has_completed(CAREER_DDAY_B)) return CSTART_CRIB;
    if (!g_career.started) return CSTART_FIRST_MOVIE;
    career_enter_shop_event(shop_event_now());
    g_career.has_spawn = 1;                       /* 0x803bbee0 = 1: free roam resumes at the saved spot */
    return CSTART_FREE_ROAM;
}
/* StartCareerFreeRoam 8011cbac (after career_start / the first movie): before
   the first movie is done (0x803bbecc) a first-time career wants the career
   start NIS (CAnimManager::NotifyCareerStartNISNeeded: NIS/Scene_CareerNis1);
   a resumed one has its tutorial done, with the shop exit NIS when it left a
   shop. The ride is career_ride_preset / the career car, the start
   career_respawn (with has_spawn); 2 random encounter cars, traffic. */
int career_free_roam_start(void)
{
    if (!g_career.started) return career_first_time();
    career_tutorial_completed();
    return 0;
}
int career_first_movie_done(void)                                        /* DoWhatNext 80183e80, case 6 */
{
    career_enter_shop_event(shop_event_now());
    int nis = career_free_roam_start();          /* before 0x803bbecc = 1: the start NIS */
    g_career.started = 1;
    return nis;
}
/* NotifyEnterShopEvent 80125ff4: the event becomes current, its record in
   progress (2); EventInProgressState::NotifyInitialize (C4) and
   CareerSpeech::HandleCareerEventBegin */
void career_enter_shop_event(const unsigned char *ev)
{
    if (!ev) return;
    g_career.current_event = rd32(ev + 8);
    CareerRecord *r = career_add_record(rd32(ev + 8));
    if (r) r->flag[CREC_DONE] = 2;
    cs_event_begin(ev);
}
/* IsPlayerRequiredToEnterShop 80121b30: +0x35 requirements of 8 bytes at
   +0x38, kind 4 = enter the shop whose handle is at +4 */
int career_event_requires_shop(const unsigned char *ev, uint32_t shop_handle)
{
    for (int i = 0; ev && i < (signed char)ev[0x35]; i++)
        if (ev[0x38 + 8 * i] == 4 && rd32(ev + 0x3c + 8 * i) == shop_handle) return 1;
    return 0;
}
/* NotifyEnterShopFromFreeRoam 80126db0: the shop's record (5 = entered),
   the GPS stops; a shop the current event requires wins it (NotifyWonEvent +
   CommitWinEvent); B ends the shop events, A hands over to B. The spawn is
   the player's spot at the shop (+0x7e3c..+0x7e50). ShowcaseState / SMS /
   EventInProgressState / WorldChallengeState notifications: C4, C8, C9. */
void career_enter_shop(const unsigned char *shop, const float pos[3], uint16_t heading)
{
    CareerState *c = &g_career;
    if (!shop) return;
    CareerRecord *r = career_add_record(rd32(shop + 0x38));
    if (r) r->flag[CREC_DONE] = 5;
    const unsigned char *ev = c->current_event ? career_event_by_handle(c->current_event) : NULL;
    if (ev) {
        if (shop[0x50] == 0) c->crib_in_event = 1;
        if (career_event_requires_shop(ev, rd32(shop + 0x38))) {
            career_commit_win(rd32(ev + 8), 1, 0.0f);
            if (rd32(ev + 8) == CAREER_DDAY_B) c->current_event = 0;
        }
        if (c->current_event == CAREER_DDAY_A) career_enter_shop_event(career_event_by_handle(CAREER_DDAY_B));
    }
    c->shop = shop;                              /* +0x7e20 */
    if (pos) {
        c->has_spawn = 1;
        c->spawn[0] = pos[0]; c->spawn[1] = pos[1]; c->spawn[2] = pos[2];
        c->spawn_heading = heading;
    }
    career_sms_enter_shop(shop);                 /* SMSMessageQueue::NotifyEnterShop 8012c654 */
    c->engaged_preset = 0;                       /* +0x7e1c = 0: the engaged event is done with */
    c->engaged_event = NULL;
    if (c->wcs.wc && career_wc_goal_shop(shop)) c->wcs.state = CWC_IN_SHOP;   /* NotifyEngageShopTrigger 8012d0d0 */
    trophy_check();
}

/* ---- WorldChallengeState (GC 0x803b4ca4; CAREER.md "C5w") ---------------- */
int career_wc_active(void) { return g_career.wcs.wc != NULL; }
int career_wc_countdown(void) { return g_career.wcs.wc && g_career.wcs.state == CWC_RUNNING; }
int career_wc_goal_shop(const unsigned char *shop)
{
    return g_career.wcs.wc && shop && rd32(shop + 0x38) == rd32(g_career.wcs.wc + 0xc);
}
uint32_t career_wc_start_zone(const unsigned char *wc) { return wc ? bstringhash(career_string(rd16(wc + 2))) : 0; }
static void wc_state_record(const unsigned char *wc, int done)             /* UpdateStateRecord(name, done) */
{
    CareerRecord *r = career_add_record(bstringhash(career_string(rd16(wc))));
    if (r) r->flag[CREC_DONE] = (uint8_t)done;
}
static const unsigned char *showcase_by_handle(uint32_t h);
void career_wc_enter(const unsigned char *wc, uint16_t heading)
{
    CareerState *c = &g_career;
    if (!wc) return;
    c->wcs.failed = 0;
    c->wcs.wc = wc;
    c->wcs.heading = heading;
    c->wcs.state = CWC_RUNNING;
    wc_state_record(wc, 2);
    c->wcs.timer = (float)(int)rd32(wc + 0x10);
    const unsigned char *sc = showcase_by_handle(rd32(wc + 0xc));   /* a showcase goal: NotifyUnlockedShowcase */
    if (sc) career_showcase_unlock(sc);
    career_recalc_zones();
    printf("career: world challenge %s started -- %ds to %08x\n", career_string(rd16(wc)), (int)rd32(wc + 0x10),
           (unsigned)rd32(wc + 0xc));
}
int career_wc_tick(float dt)
{
    CareerState *c = &g_career;
    if (!c->wcs.wc || c->wcs.state != CWC_RUNNING) return 0;
    c->wcs.timer -= dt;
    if (c->wcs.timer > 0.0f) return 0;
    c->wcs.failed = 1;                           /* NotifyFailedMission 8012d394 */
    c->wcs.state = CWC_FAILED;
    g_cw.engaging = 1;
    printf("career: world challenge %s failed (time up)\n", career_string(rd16(c->wcs.wc)));
    return 1;
}
uint32_t career_wc_body(const unsigned char *wc, int (*rnd)(int n), int *seconds)
{
    const char *f = "";
    int n = 1;
    switch ((signed char)wc[0x14]) {
    case 1: n = 5; f = "ENGAGE_VISUAL_GENERIC_%d_BODY"; break;
    case 2: n = 3; f = "ENGAGE_PERF_GENERIC_%d_BODY"; break;
    case 3: n = 0; break;
    case 4: n = 5; f = "ENGAGE_SHOWCASE_GENERIC_%d_BODY"; break;
    }
    char b[64];
    snprintf(b, sizeof b, f, rnd(n) + 1);        /* FEngHashString */
    *seconds = (int)rd32(wc + 0x10);
    return f[0] ? bstringhash(b) : 0;
}
uint32_t career_wc_subject(const unsigned char *wc)
{
    switch ((signed char)wc[0x14]) {
    case 1: return 0x48157d57u;
    case 2: return 0xa517fd7au;
    case 3: return 0xbf178f39u;
    case 4: return 0xe5b2c887u;
    }
    return 0;
}
static void wc_succeed(void)                                               /* NotifySucceedMission 8012d590 */
{
    CareerState *c = &g_career;
    if (c->wcs.wc) wc_state_record(c->wcs.wc, 1);
    printf("career: world challenge %s done\n", c->wcs.wc ? career_string(rd16(c->wcs.wc)) : "?");
    c->wcs.wc = NULL;
    c->wcs.state = CWC_NONE;
    career_recalc_zones();
    trophy_check();
}
void career_wc_unique_chosen(void) { g_career.wcs.state = CWC_DONE; wc_succeed(); }
void career_wc_failure_accepted(void)
{
    CareerState *c = &g_career;
    c->wcs.state = CWC_FAIL_ACCEPTED;
    CareerRecord *r = c->wcs.wc ? career_record(rd32(c->wcs.wc + 0xc)) : NULL;
    if (r) r->flag[CREC_ENTERED] = 0;
}
uint32_t career_wc_failure_rejected(void)
{
    g_career.wcs.state = CWC_RETRY;
    return career_wc_start_zone(g_career.wcs.wc);
}
void career_wc_reset_post_failure(void)
{
    CareerState *c = &g_career;
    if (!c->wcs.wc) return;
    if (c->wcs.state == CWC_FAIL_ACCEPTED) {
        printf("career: world challenge %s given up\n", career_string(rd16(c->wcs.wc)));
        c->wcs.wc = NULL;
        career_recalc_zones();
    } else if (c->wcs.state == CWC_RETRY) career_wc_enter(c->wcs.wc, c->wcs.heading);
}
void career_wc_exit_to_frontend(void)
{
    CareerState *c = &g_career;
    if (!c->wcs.wc) return;
    CareerRecord *r = career_record(rd32(c->wcs.wc + 0xc));
    if (r) r->flag[CREC_ENTERED] = 0;
    c->wcs.state = CWC_NONE;
    c->wcs.wc = NULL;
}
const unsigned char *career_uniques_shop(void)
{
    static const unsigned char *u;
    if (!u) u = career_shop_by_handle(bstringhash("THE_UNIQUES_SHOP"));
    return u;
}
const unsigned char *career_shop_now(void)
{
    if (g_career.wcs.wc && g_career.wcs.state == CWC_IN_SHOP) return career_uniques_shop();
    return g_career.shop;
}
/* MaybeTargetGPSForShopEvent 80126bb4: the current shop event's shop zone
   (FindTrackPathZoneByDataHandle of its +0x3c); NotifyFreeRoamBegun /
   NotifyExitShopNISFinished target B, the stage-0 intro A */
uint32_t career_gps_shop(void)
{
    const unsigned char *ev = g_career.current_event ? career_event_by_handle(g_career.current_event) : NULL;
    const unsigned char *sh = ev && (signed char)ev[0x35] > 0 ? career_shop_by_handle(rd32(ev + 0x3c)) : NULL;
    return sh ? rd32(sh + 0x3c) : 0;
}
uint32_t career_gps_shop_event(const char *name)                        /* 80126bb4's handle test */
{
    return g_career.current_event && g_career.current_event == bstringhash(name) ? career_gps_shop() : 0;
}
uint32_t career_ride_preset(void)                                        /* 8012731c */
{
    if (g_career.engaged_preset) return g_career.engaged_preset;
    const unsigned char *ev = g_career.current_event ? career_event_by_handle(g_career.current_event) : NULL;
    return ev ? rd32(ev + 0x2c) : 0;
}
int career_respawn(float pos[3], uint16_t *heading)                      /* 8012765c */
{
    pos[0] = g_career.spawn[0]; pos[1] = g_career.spawn[1]; pos[2] = g_career.spawn[2];
    *heading = g_career.spawn_heading;
    return g_career.has_spawn;
}

/* Stage0StateManager (GC +0x6824 {state, t0, read, dismissed}). The timer
   is the game's 4000 Hz tick (0x803296c0 = 1/4000); waits are 2 s
   (0x803296c4). The caller does what the returned CS0_* bits ask:
   CS0_INTRO FEngHud::TriggerStage0Intro(2); CS0_SMS CareerSpeech::Stage0SMS,
   the SMS_INSTRUCTION message + HUD notice + autopopup; CS0_MINIMAP
   Stage0Minimap; CS0_CARLOT RecalcUnlockedZones, GPS to A's shop,
   Stage0Carlot; CS0_RELINQUISH input mode 2, RecalcUnlockedZones,
   SMS_INSTRUCTION + notice, GPS to A's shop. */
#define S0_WAIT_MS 2000u
int career_first_time(void) { return g_career.s0.state == 0; }
void career_tutorial_completed(void) { g_career.s0.state = 9; }
void career_stage0_begin(int shop_nis)                                   /* 80130388 */
{
    if (!shop_nis && g_career.s0.state == 0) g_career.s0.state = 1;
}
int career_stage0_has_control(void) { return g_career.s0.state != 0 && g_career.s0.state != 9; }
int career_stage0_show(int what)
{
    int st = g_career.s0.state;
    if (!career_stage0_has_control()) return 0;
    switch (what) {
    case 0: return st == 2;                                              /* ShowTelephone */
    case 1: return !(st == 5 && g_career.s0.read_msg) && st > 2;         /* ShowEngageEventThingy */
    case 2: return st > 5;                                               /* ShowMinimap */
    case 3: return st > 6;                                               /* ShowCarLot */
    default: return st > 8;                                              /* ShowGauges */
    }
}
static int s0_relinquish(void)                                           /* 801303f4 */
{
    g_career.s0.state = 9;
    career_sms_add_external(bstringhash("SMS_INSTRUCTION"));
    return CS0_RELINQUISH;
}
int career_stage0_skip(void) { return career_stage0_has_control() ? s0_relinquish() : 0; }   /* 80130484 */
void career_stage0_read_message(void) { g_career.s0.read_msg = 1; }
void career_stage0_sms_dismissed(void) { g_career.s0.sms_dismissed = 1; }
void career_speech_stage0_intro(void) { cs_say(CSPK_CAREER, 0x8000, 0x20); }   /* Stage0Intro 0x5258b0, with CareerNis1's audio */
int career_stage0_update(uint32_t now, int speech_done)                 /* 80130680 */
{
    CareerState *c = &g_career;
    uint32_t dt = now - c->s0.t0;
    switch (c->s0.state) {
    case 1: c->s0.t0 = now; c->s0.state = 2; return CS0_INTRO;
    case 2: if (dt > S0_WAIT_MS) c->s0.state = 3; return 0;
    case 3:
        if (!speech_done) return 0;
        c->s0.state = 5; c->s0.t0 = now;
        career_sms_add_external(bstringhash("SMS_INSTRUCTION"));   /* + NotifySMSMessage, the forced DoAutopopup (main) */
        cs_say(CSPK_CAREER, 0x800, 0x20);                        /* CareerSpeech::Stage0SMS */
        return CS0_SMS;
    case 4: if (dt > S0_WAIT_MS && speech_done) c->s0.state = 5; return 0;
    case 5:
        if (!(dt > S0_WAIT_MS && speech_done && c->s0.read_msg && c->s0.sms_dismissed)) return 0;
        c->s0.state = 6; c->s0.t0 = now;
        cs_say(CSPK_CAREER, 2, 0x20);                            /* CareerSpeech::Stage0Minimap */
        return CS0_MINIMAP;
    case 6:
        if (!(dt > S0_WAIT_MS && speech_done)) return 0;
        c->s0.state = 7; c->s0.t0 = now;
        cs_say(CSPK_CAREER, 0x10000000, 0x20);                   /* CareerSpeech::Stage0Carlot */
        return CS0_CARLOT;
    case 7: c->s0.state = 8; c->s0.t0 = now; return 0;   /* the GC asks Stage0IsSpeechDone and moves on either way */
    case 8: return s0_relinquish();
    }
    return 0;
}

/* ------------------------------------------------------- the SMS queue (C8) */

/* SMSMessageQueue (GC 8012bc38..8012c9a8, PC 0x5295f0..0x529c40; RE in
   re-tools/out/career/c8/SMS.md). SMSMessageData (T[6], 0x14): +0 name,
   +2 trigger kind, +3 mailbox tab, +4 autopopup, +5 envelope category,
   +8 / +0xc p0 / p1, +0x10 the From name's language hash. A slot: b[0] read,
   b[1] autopopup seen, b[2] deleted, b[3] sortable; u[0] body, u[1] arg,
   u[2] subject (magazine / world challenge text). */
int career_nsms_data(void) { return (int)(TN[6] / CAR_SMS); }
const unsigned char *career_sms_data(int i) { return i >= 0 && i < career_nsms_data() ? T[6] + i * CAR_SMS : NULL; }
static int sms_queued(const unsigned char *d)                           /* IsMessageQueued 8012c150 */
{
    CareerSmsQueue *q = &g_career.sms;
    for (uint32_t i = 0; i < q->u1400 && i < 0x100; i++) if (q->slot[i].msg == d) return 1;   /* deleted ones too */
    return 0;
}
CareerSmsSlot *career_sms_add(const unsigned char *d)                   /* AddMessage 8012c000 */
{
    CareerSmsQueue *q = &g_career.sms;
    if (!d || sms_queued(d) || q->u1400 >= 0x100) return NULL;          /* AllocateMessage 8012bfd8 */
    CareerSmsSlot *m = &q->slot[q->u1400++];
    memset(m, 0, sizeof *m);                                            /* BuildFrom 8012b9c8 */
    m->msg = d;
    q->b140c = 1;
    q->u1404++;
    printf("career: SMS %s queued (%u unread)\n", career_string(rd16(d)), (unsigned)q->u1404);
    return m;
}
CareerSmsSlot *career_sms_add_external(uint32_t name_hash)              /* AddExternalMessage 8012bd30 */
{
    return career_sms_add(career_sms_by_hash(name_hash));
}
/* AddMagazineMessage 8012c098 + MakeMagazineMessage 8012b9f4 */
static void sms_add_magazine(const unsigned char *d, uint32_t body, uint32_t subject, uint32_t arg)
{
    CareerSmsSlot *m = career_sms_add(d);
    if (m) { m->u[1] = arg; m->u[0] = body; m->u[2] = subject; }
}
static uint32_t hash_cont(uint32_t h, const char *s) { while (*s) h = h * 33 + (unsigned char)*s++; return h; }
static uint32_t fmt_hash(const char *f, int v) { char b[64]; snprintf(b, sizeof b, f, v); return bstringhash(b); }
static int g_sms_mag_n, g_sms_wc_n;                                     /* r13-0x7118 / -0x7114 */
static const unsigned char *showcase_by_handle(uint32_t h)              /* FindShowcaseDataByHandle 801224d4 */
{
    for (int i = 0; i < career_nshowcases(); i++) if (rd32(career_showcase(i) + 0x20) == h) return career_showcase(i);
    return NULL;
}
void career_sms_add_wc(const unsigned char *wc)                         /* AddWorldChallengeMessage 8012bd88 */
{
    const unsigned char *sc = showcase_by_handle(rd32(wc + 0xc));
    /* both callers (NotifyCareerEventCompleted 8012ceec, NotifyOutrunRaceFinished
       8012ce38) follow AddWorldChallengeMessage with CareerSpeech::
       HandleWorldChallengeUnlock 8018a374: the challenge's kind (+0x14) 3 or 4,
       whether or not it has an SMS record */
    if (sc && (wc[0x14] == 3 || wc[0x14] == 4)) career_speech_showcase_unlock(sc);
    const unsigned char *d = career_sms_by_hash(rd32(wc + 8));
    if (!d) return;
    uint32_t body = fmt_hash("SMS_MAG_GENERIC_BODY_%d", g_sms_mag_n % 2 + 1);
    uint32_t subj = fmt_hash("SMS_MAG_GENERIC_SUBJ_%d", g_sms_mag_n % 2 + 1);
    if (sc) {                                   /* bStringHash("_TITLE", showcase +0x28) */
        g_sms_mag_n++;
        sms_add_magazine(d, body, subj, hash_cont(rd32(sc + 0x28), "_TITLE"));
        return;
    }
    body = subj = 0;
    uint32_t arg = 0;
    if (wc[0x14] == 2) {
        int k = g_sms_wc_n % 3 + 1;
        body = fmt_hash("SMS_PERF_GENERIC_%d_BODY", k); subj = fmt_hash("SMS_PERF_GENERIC_%d_SUBJECT", k);
        g_sms_wc_n++;
    } else if (wc[0x14] == 1) {
        int k = g_sms_wc_n % 5 + 1;
        body = fmt_hash("SMS_VISUAL_GENERIC_%d_BODY", k); subj = fmt_hash("SMS_VISUAL_GENERIC_%d_SUBJECT", k);
        switch ((signed char)wc[0x15]) {        /* the part category's name */
        case 11: arg = 0x028f7092u; break;
        case 12: arg = 0xbb034ea6u; break;
        case 13: arg = 0x0294d2a3u; break;
        case 22: arg = 0x945e9bc5u; break;
        case 62: arg = 0x78deea1cu; break;
        }
        g_sms_wc_n++;
    }
    sms_add_magazine(d, body, subj, arg);
}
int career_sms_count(void) { return (int)(g_career.sms.u1400 < 0x100 ? g_career.sms.u1400 : 0x100); }
CareerSmsSlot *career_sms_slot(int i) { return i >= 0 && i < career_sms_count() ? &g_career.sms.slot[i] : NULL; }
int career_sms_unread(void) { return (int)g_career.sms.u1404; }        /* GetNumMessagesUnread(fast) */
CareerSmsSlot *career_sms_the_one_unread(void)                          /* GetTheOneUnreadMessage */
{
    for (int i = 0; i < career_sms_count(); i++)
        if (!g_career.sms.slot[i].b[0] && g_career.sms.slot[i].b[2] != 1) return &g_career.sms.slot[i];
    return NULL;
}
void career_sms_read(CareerSmsSlot *m)                                  /* NotifyReadMessage */
{
    if (!m) return;
    if (!m->b[0] && g_career.sms.u1404) g_career.sms.u1404--;
    m->b[0] = 1;
    if (career_stage0_has_control()) career_stage0_read_message();
}
void career_sms_delete(CareerSmsSlot *m)                                /* NotifyDeleteMessage */
{
    if (!m) return;
    if (!m->b[0] && g_career.sms.u1404) g_career.sms.u1404--;
    m->b[2] = 1; m->b[0] = 1;
}
void career_sms_sortable_all(void)                                      /* MarkAllReadMessagesSortable */
{
    for (int i = 0; i < career_sms_count(); i++) if (g_career.sms.slot[i].b[0]) g_career.sms.slot[i].b[3] = 1;
}
void career_sms_summoned(void) { g_career.sms.b140c = 0; }             /* SummonMessageViewer's clear */
/* GetSubject 8012ba10 / GetBody 8012bb04: the language hash of the text
   (a printf format with *arg when arg is set) */
uint32_t career_sms_subject(const CareerSmsSlot *m, uint32_t *arg)
{
    *arg = 0;
    if (m->u[2]) { *arg = m->u[1] ? m->u[1] : 0xffffffffu; return m->u[2]; }
    const char *n = career_string(rd16(m->msg));
    char b[96];
    if (!strncmp(n, "SMS_CAR_UNLOCK_", 14)) return bstringhash("SMS_CAR_UNLOCK_1_SUBJECT");
    snprintf(b, sizeof b, "%s_SUBJECT", n);
    return fehashupper(b);
}
uint32_t career_sms_body(const CareerSmsSlot *m, uint32_t *arg)
{
    *arg = 0;
    if (m->u[0]) { *arg = m->u[1] ? m->u[1] : 0xffffffffu; return m->u[0]; }
    const char *n = career_string(rd16(m->msg));
    char b[96];
    if (!strncmp(n, "SMS_CAR_UNLOCK_", 14)) return bstringhash("SMS_CAR_UNLOCK_1_BODY");
    if (!strncmp(n, "ENGAGE_TIPS_AUTO", 15)) return bstringhash("ENGAGE_TIPS_AUTO_BODY");
    snprintf(b, sizeof b, "%s_BODY", n);
    return fehashupper(b);
}
/* the trigger kinds over the whole table, not yet queued */
static void sms_scan(int (*hit)(const unsigned char *d, const void *a), const void *a)
{
    for (int i = 0; i < career_nsms_data(); i++) {
        const unsigned char *d = career_sms_data(i);
        if (hit(d, a) && !sms_queued(d)) career_sms_add(d);
    }
}
static int sms_hit_event(const unsigned char *d, const void *a)         /* IsTriggeredByCompletingEvent 8012b7fc */
{
    const unsigned char *ev = a;
    uint32_t p0 = rd32(d + 8);
    int k = d[2];
    if (k == 1 && (p0 == rd32(ev + 8) || rd32(d + 0xc) == rd32(ev + 8))) return 1;
    if (k != 9 && k != 10 && k != 12) return 0;
    const CareerStage *s = &g_career.st[(p0 >> 16) & 7];
    int n = k == 9 ? cstage_sponsor_done(s) : k == 10 ? cstage_url_done(s) : cstage_world_done(s);
    return (int)(p0 & 0xffff) <= n;
}
static int sms_hit_shop(const unsigned char *d, const void *a)          /* IsTriggeredByShop 8012b940 */
{
    uint32_t h = rd32((const unsigned char *)a + 0x38);
    return d[2] == 8 || (d[2] == 2 && (rd32(d + 8) == h || rd32(d + 0xc) == h));
}
static int sms_hit_zone(const unsigned char *d, const void *a)          /* IsTriggeredByEnterZone 8012b98c */
{
    uint32_t h = *(const uint32_t *)a;
    return d[2] == 7 && (rd32(d + 8) == h || rd32(d + 0xc) == h);
}
static int sms_hit_kind(const unsigned char *d, const void *a) { return d[2] == *(const int *)a; }
static int sms_hit_dvd(const unsigned char *d, const void *a)           /* IsTriggeredByHavingDVDCover 8012b91c */
{
    return d[2] == 14 && rd32(d + 8) == *(const uint32_t *)a;
}
void career_sms_complete_event(const unsigned char *ev) { if (ev) sms_scan(sms_hit_event, ev); }   /* 8012c6d4 */
void career_sms_enter_shop(const unsigned char *shop) { if (shop) sms_scan(sms_hit_shop, shop); }  /* 8012c654 */
/* NotifyEnterTriggerZone 8012c7d4: kind 7, only for an enterable
   showcase's zone (no data uses it) */
void career_sms_enter_zone(uint32_t zone)
{
    CareerZone z;
    if (!showcase_by_zone(zone) || !career_zone_data(zone, &z) || !career_zone_enterable(&z)) return;
    sms_scan(sms_hit_zone, &zone);
}
void career_sms_dvd_cover(uint32_t n) { sms_scan(sms_hit_dvd, &n); }   /* NotifyDVDCoverElegible 8012c754 */

/* ------------------------------------------------------------ showcases */

const unsigned char *career_showcase_by_handle(uint32_t h) { return showcase_by_handle(h); }
void career_showcase_unlock(const unsigned char *sc)                   /* NotifyUnlockedShowcase 8012b0d8 */
{
    CareerRecord *r = sc ? career_add_record(rd32(sc + 0x20)) : NULL;
    if (r) r->flag[CREC_ENTERED] = 2;
    career_recalc_zones();
}
/* GetCurrentPlayerStarRating 0x528410: the career car's GetNumberOfStars
   (0x5234c0, shop.c's rating, kept with the car when a shop commits it);
   0 with no car */
float career_star_rating(void)
{
    const CareerCar *c = career_current_car();
    return c ? c->stars : 0.0f;
}
/* RecalcDVDUnlocks 0x5331c0 (NotifyFreeRoamBegun): the stage's DVDs (kind 2 /
   4) with their star requirement (+0x3c) reached are unlocked -- the first
   time NotifyDVDCoverElegible(n) -- the others locked again; the speech's
   +0x8895 = the highest locked requirement (0 none); CareerSpeech
   HandleShowcaseUnlock 0x5253f0 on each unlocked one. */
void career_recalc_dvd_unlocks(void)
{
    float stars = career_star_rating();
    const unsigned char *best = NULL;
    for (int i = 0; i < career_nshowcases(); i++) {
        const unsigned char *sc = career_showcase(i);
        if ((sc[0x24] != 2 && sc[0x24] != 4) || sc[0x25] != (unsigned)g_career.stage) continue;
        float need = rdf(sc + 0x3c);
        CareerRecord *r = career_record(rd32(sc + 0x20));
        if (need <= stars) {
            if (!r) {
                career_sms_dvd_cover((uint32_t)(int)need);
                r = career_add_record(rd32(sc + 0x20));
                printf("career: DVD %d unlocked (%.2f stars)\n", (int)need, stars);
            }
            if (r) r->flag[CREC_ENTERED] = 2;
            career_speech_showcase_unlock(sc);                            /* CareerSpeech 0x5253f0 */
        } else {
            if (r) r->flag[CREC_ENTERED] = 0;
            if (!best || rdf(best + 0x3c) < need) best = sc;
        }
    }
    g_career.speech[0x19] = best ? (uint8_t)(int)rdf(best + 0x3c) : 0;
}
/* PlayerCareerState::NotifyShowcaseCompleted 0x541210 (ChooseMagazineForShowcase
   8012b05c): the cash, base (+0x26) + ftol(stars x StarRatingPurse + 0.005);
   RuntimeCareerStageInfo::NotifyShowcaseCompleted 80124730: the handle into
   the stage's list (+0xc4, 6), CheckIfClearedStage. *star = the star part */
int career_showcase_reward(const unsigned char *sc, int *star)
{
    if (!sc) return 0;
    int base = rd16s(sc + 0x26);
    int s = (int)(career_star_rating() * (float)career_star_rating_purse(g_career.stage) + 0.005f);
    g_career.cash += base + s;
    CareerStage *st = &g_career.st[g_career.stage & 7];
    uint32_t h = rd32(sc + 0x20);
    for (int i = 0; i < 6; i++) {
        uint32_t v; memcpy(&v, st->xc4 + 4 * i, 4);
        if (!v || v == h) { memcpy(st->xc4 + 4 * i, &h, 4); break; }
    }
    cstage_check_cleared(st);
    if (star) *star = s;
    printf("career: showcase %s reward $%d + $%d ($%d)\n", (const char *)sc, base, s, g_career.cash);
    return base + s;
}
/* ShowcaseState::NotifyShowcaseCompleted 8012afac: the record done (+6 = 1,
   +4 = 2), the zones (the HUD's ExitTriggerZone and the minimap are the
   caller's); WorldChallengeState::NotifyShowcasePictureTaken 8012d114 first:
   at the showcase (3) -> 5, NotifySucceedMission */
/* NotifyEnterFEMagazineFromFreeRoam 801263dc: the player car's spot and
   heading (+0x7e40..+0x7e50), +0x7e3c = 1 -- free roam starts there again */
void career_enter_fe_magazine(const float pos[3], uint16_t heading)
{
    CareerState *c = &g_career;
    if (!pos) return;
    c->has_spawn = 1;
    c->spawn[0] = pos[0]; c->spawn[1] = pos[1]; c->spawn[2] = pos[2];
    c->spawn_heading = heading;
}
/* CalculateIfUnlocked (0x515360, filter 4) on the GCareer 0x34a1c rows (T[11],
   0x28 each: +0 entity, three levels of 12 from +4: +4 type, +8 arg) -- the
   shop's calc_unlocked without a Shop. Entities outside 1..0x56 count as
   unlocked; entity 0x44's extra 0x579d60 question is left out (guess: no) */
static int stat_level_unlocked(uint32_t ent, int lvl)
{
    if (lvl == 4) return 0;
    if (lvl <= 0 || lvl > 3 || ent < 1 || ent > 0x56) return 1;
    for (long k = 0; T[11] && k + 0x28 <= TN[11]; k += 0x28) {
        const unsigned char *r = T[11] + k;
        if (rd32(r) != ent) continue;
        const unsigned char *l = r + 4 + 12 * (lvl - 1);
        uint32_t a = rd32(l + 8);
        int st = (int)((int32_t)a >> 16), need = (int)(a & 0xffffu);
        const CareerStage *cs = st >= 0 && st < 8 ? &g_career.st[st] : NULL;
        switch (l[4]) {
        case 1: { const unsigned char *sh = career_shop_by_handle(a); return sh && career_shop_enterable(sh); }
        case 2: return cs && need <= cstage_world_done(cs);
        case 3: return cs && need <= cstage_url_done(cs);
        case 4: return cs && need <= cstage_sponsor_done(cs);
        default: return 1;
        }
    }
    return 1;
}
void career_fill_statistics(CareerStats *s)
{
    /* 0x7f7ba8: each stage's required {world, sponsor, URL} wins (x0c unused here) */
    static const int k_req[6][3] = { {0,0,0}, {5,0,0}, {10,3,3}, {20,3,5}, {30,3,7}, {35,3,9} };
    const CareerState *c = &g_career;
    memset(s, 0, sizeof *s);
    s->cash = c->cash;
    s->races = (int)c->u872c;
    int evwon = 0;
    for (int i = 0; i < c->nrec && i < 0x180; i++) {
        const CareerRecord *r = &c->rec[i];
        if (r->flag[CREC_DONE] != 1) continue;
        const unsigned char *ev = career_event_by_handle(r->handle);
        if (!ev) continue;
        evwon++;
        int cls = ev[0x34], type = ev[0xf];
        if (cls == 2) s->url++;
        else if (cls == 1 || cls == 3) {
            if (type == 0) s->circuit++;
            else if (type == 1) s->sprint++;
            else if (type == 4) s->drag++;
            else if (type == 5) s->drift++;
            else if (type == 2) s->streetx++;
        }
    }
    s->wins = s->circuit + s->sprint + s->drag + s->drift + s->streetx + s->url;
    s->win_pct = s->races ? (float)s->wins / (float)s->races : 0.0f;
    s->completion = career_nevents() ? (float)evwon / (float)career_nevents() : 0.0f;
    for (long k = 0; k + CAR_SHOP <= TN[1]; k += CAR_SHOP) {   /* the hidden shops */
        const unsigned char *sh = T[1] + k;
        int st = sh[0x9d];
        if (!sh[0x51] || st < 0 || st >= 8) continue;
        s->shops[st][1]++;
        const CareerRecord *r = career_record(rd32(sh + 0x38));
        if (r && r->flag[CREC_ENTERED] == 1) s->shops[st][0]++;
    }
    for (int i = 0; i < 8; i++) if (c->st[i].chosen[0]) s->sponsors++;
    s->stage = c->stage;
    int cur = c->stage >= 0 && c->stage < 8 ? c->stage : 0;
    for (int i = 0; i < career_nshowcases(); i++) {
        const unsigned char *sc = career_showcase(i);
        int kind = sc[0x24], st = sc[0x25];
        const CareerRecord *r = career_record(rd32(sc + 0x20));
        int won = r && r->flag[CREC_DONE] == 1;
        if (kind == 1 || kind == 3) {
            s->mags_total++; s->mags_won += won;
            if (st >= 0 && st < 8) s->st[st].nmag++;
        } else if (kind == 2 || kind == 4) {
            s->dvds_total++; s->dvds_won += won;
            if (st >= 0 && st < 8 && s->st[st].ndvd < 4) {
                int j = s->st[st].ndvd++;
                s->st[st].stars[j] = rdf(sc + 0x3c); s->st[st].won[j] = (uint8_t)won;
            }
            if (st == cur) { s->sdvd_total++; s->sdvd_won += won; }
        }
    }
    const CareerStage *cs = &c->st[cur];
    s->world_won = cstage_world_done(cs); s->sponsor_won = cstage_sponsor_done(cs); s->url_won = cstage_url_done(cs);
    if (cur < 6) { s->world_req = k_req[cur][0]; s->sponsor_req = k_req[cur][1]; s->url_req = k_req[cur][2]; }
    int num = s->world_won + s->sponsor_won + s->url_won + s->sdvd_won;
    int den = s->world_req + s->sponsor_req + s->url_req + s->sdvd_total;
    s->stage_completion = den ? (float)num / (float)den : 0.0f;
    int ptot = 0, pun = 0;                                         /* 0x5017f0 / 0x515470 */
    for (long k = 0; T[11] && k + 0x28 <= TN[11]; k += 0x28) {
        const unsigned char *r = T[11] + k;
        for (int l = 0; l < 3; l++) {
            int type = r[8 + 12 * l];
            if (type < 1 || type > 4) continue;
            ptot++;
            pun += stat_level_unlocked(rd32(r), (signed char)r[9 + 12 * l]);
        }
    }
    s->parts = ptot ? (float)pun / (float)ptot : 0.0f;
    for (int i = 0; i < 8; i++) {
        s->rep[i] = c->st[i].points;
        s->avg[i] = c->st[i].npoints ? c->st[i].points / (float)c->st[i].npoints : 0.0f;
    }
    s->rep[1] += s->rep[0];
    {   int n = c->st[0].npoints + c->st[1].npoints;
        s->avg[1] = n ? (c->st[0].points + c->st[1].points) / (float)n : 0.0f; }
    trophy_check();                              /* the Status screen (RA's "open the Stats page") */
}
void career_showcase_completed(const unsigned char *sc)
{
    if (!sc) return;
    if (g_career.wcs.wc && g_career.wcs.state == CWC_AT_SHOWCASE) { g_career.wcs.state = CWC_DONE; wc_succeed(); }
    CareerRecord *r = career_add_record(rd32(sc + 0x20));
    if (r) { r->flag[CREC_DONE] = 1; r->flag[CREC_ENTERED] = 2; }
    memset(&g_cw.cur, 0, sizeof g_cw.cur);       /* ExitTriggerZone */
    career_recalc_zones();
    printf("career: showcase %s completed\n", (const char *)sc);
    trophy_check();
}
/* PlayerCareerState::NotifyBeginGetAway 80126f4c: the SMS (kind 4); the
   speech's HandleOutrunStart is C10's */
void career_begin_getaway(void) { int k = 4; sms_scan(sms_hit_kind, &k); career_speech_outrun_start(); }   /* + HandleOutrunStart */
/* NotifyRandomEncounterFinished 80126f84 (career mode): the purse paid /
   spent, the stage's Outrun wins (+0xdc), the SMS (5 / 6), the stage's
   Outrun world challenges (NotifyOutrunRaceFinished 8012ce38), the zones */
void career_encounter_finished(int won)
{
    CareerState *c = &g_career;
    int purse = career_random_encounter_purse(c->stage);
    if (won) c->cash += purse; else career_spend(purse);                /* Deposit / Spend (floors at 0) */
    career_speech_outrun_end(won);                                      /* NotifyRandomEncounterFinished */
    CareerStage *s = &c->st[c->stage & 7];
    if (won) s->dc++;
    int k = won ? 5 : 6; sms_scan(sms_hit_kind, &k);
    if (won)
        for (int i = 0; i < career_nchallenges(); i++) {
            const unsigned char *wc = career_challenge(i);
            if (wc[6] == 2 && rd16s(wc + 4) == c->stage && s->dc == (signed char)wc[7]) career_sms_add_wc(wc);
        }
    career_recalc_zones();
    printf("career: Outrun %s ($%d, %d won this stage)\n", won ? "won" : "lost", c->cash, s->dc);
    trophy_check();
}
/* PlayerCareerState::NotifyInSMSAutoTriggerZone 801260bc (queue 8012c9a8):
   a help zone (type 0x12) holding the car -- its kind 15 tip once, then
   the tip's cash (p1) and RecalcHelpTriggers. 1 = a tip was queued */
int career_sms_help_zone(uint32_t zone)
{
    if (!zone) return 0;                        /* +0x1410 is never written */
    for (int i = 0; i < career_nsms_data(); i++) {
        const unsigned char *d = career_sms_data(i);
        if (d[2] != 15 || rd32(d + 8) != zone) continue;
        if (sms_queued(d) || !career_sms_add(d)) return 0;
        g_career.cash += (int32_t)rd32(d + 0xc);
        printf("career: help zone tip %s, +$%d\n", career_string(rd16(d)), (int)rd32(d + 0xc));
        return 1;
    }
    return 0;
}
/* CanHelpTriggerZoneBeActivated 8012ca54: RecalcHelpTriggers 80128f3c fills UnlockedZoneManager
   +0x600 (<= 128 type-0x12 zones passing this test), rebuilt only from NotifyInSMSAutoTriggerZone
   and RecalcUnlockedZones; its one reader is UpdateTriggerEffects 8022d584 (map mode 1 -> +0x600,
   mode 2 -> banks +0x800; main.c trig_update), so this live test is equivalent */
int career_help_zone_active(uint32_t zone)
{
    for (int i = 0; i < career_nsms_data(); i++) {
        const unsigned char *d = career_sms_data(i);
        if (d[2] == 15 && rd32(d + 8) == zone) return !sms_queued(d);
    }
    return 0;
}
/* UpdateTick 80127688's SMS part, once a frame: the career time (+0x7e54)
   runs in free roam (RC state 4) while Stage0 doesn't have control; Tick
   8012c488 / 0x529750 queues the timed tips (kind 3) and, with an unseen
   autopopup and ConditionsRightToSeeAutopopup (cond: game flow 6, career
   free roam, no shop engaged), marks them all seen and returns 1 =
   DoAutopopup (the viewer + FE sound 0xb/0x10) */
int career_sms_tick(float dt, int roam, int cond)
{
    CareerState *c = &g_career;
    if (roam && !career_stage0_has_control()) c->f8764 += dt;   /* +0x7e54 (PC +0x8764): saved, -1 by Default */
    for (int i = 0; i < career_nsms_data(); i++) {
        const unsigned char *d = career_sms_data(i);
        if (d[2] == 3 && (float)rd32(d + 8) < c->f8764 && !sms_queued(d)) career_sms_add(d);
    }
    int any = 0;
    for (int i = 0; i < career_sms_count() && !any; i++)
        any = c->sms.slot[i].msg[4] == 1 && !c->sms.slot[i].b[1];       /* AnyUnseenAutoPopups 8012bc38 */
    if (!any || !roam || !cond) return 0;
    for (int i = 0; i < career_sms_count(); i++)                        /* MarkAllAutoPopupsSeen 8012bcc4 */
        if (c->sms.slot[i].msg[4] == 1) c->sms.slot[i].b[1] = 1;
    return 1;
}

/* ------------------------------------------------------- the career cars */

/* FEPlayerCarDB's career half (GC 0x803bc6c4 +0x5954, 5 x 0x7fc; PC 0x83ad90,
   stride 0x7f8). The FECarRecord filter is 4 (career) for all of them. */
static CareerCar g_ccar[CAREER_NSLOTS];
static int g_nccar;                                                     /* +0x8140 */
int (*career_car_on_disc)(int type);

uint32_t career_slot_handle(int slot)
{
    char b[32];
    snprintf(b, sizeof b, "CAREER_SLOT_%d", slot);                      /* FEngHashString: sprintf + bStringHash */
    return bstringhash(b);
}
void career_cars_default(void)                                          /* DefaultCareerCars 801340a4 */
{
    memset(g_ccar, 0, sizeof g_ccar);
    g_nccar = 0;
    for (int i = 0; i < CAREER_NSLOTS; i++) {
        g_ccar[i].handle = career_slot_handle(i);                       /* Initialize(handle, 4); in use */
        g_ccar[i].locked = 1;
        g_ccar[i].has = 0;
        g_ccar[i].type = -1;
        memset(g_ccar[i].tune, 100, sizeof g_ccar[i].tune);   /* DefaultCustomTuningSpecs 80135004 */
        g_nccar++;
    }
}
void career_unlock_car_slot(void)                                       /* UnlockACareerCarSlot 80134048 */
{
    for (int i = 0; i < g_nccar; i++)
        if (g_ccar[i].locked) { g_ccar[i].locked = 0; return; }       /* UnlockSlot */
}
CareerCar *career_car_slot(int i) { return i >= 0 && i < g_nccar ? &g_ccar[i] : NULL; }
CareerCar *career_car_by_handle(uint32_t h)
{
    for (int i = 0; i < g_nccar; i++) if (g_ccar[i].handle == h) return &g_ccar[i];
    return NULL;
}
/* GetCurrentCareerCar: the slot named by +0x8740 (Default's "CAREER_0" names
   none until CommitChooseCar); only a slot with a car in it is a ride */
const CareerCar *career_current_car(void)
{
    const CareerCar *c = career_car_by_handle(g_career.career_hash);
    return c && c->has && c->type >= 0 ? c : NULL;
}
void career_switch_car(uint32_t handle) { g_career.career_hash = handle; }
static const char *type_name(int t)                                     /* CarTypeInfo +0 */
{
    const unsigned char *r = airace_type_rec(t);
    return r ? (const char *)r : "";
}
const char *career_car_name(const CareerCar *c) { return c && c->has ? airace_type_name(c->type) : ""; }
int career_slot_counts(int *unlocked, int *empty)                      /* UICareerCarLot InitUnlockedAndEmptySlots */
{
    int u = 0, e = 0;
    for (int i = 0; i < g_nccar; i++)
        if (!g_ccar[i].locked) { u++; if (!g_ccar[i].has) e++; }
    if (unlocked) *unlocked = u;
    if (empty) *empty = e;
    return u;
}
int career_first_empty_slot(void)                                       /* FindFirstEmptySlot */
{
    for (int i = 0; i < g_nccar; i++) if (!g_ccar[i].locked && !g_ccar[i].has) return i;
    return -1;
}

/* DefaultStockCars (PC 0x516510): car types 0..45 with geometry (+0x40), the
   +0xda bit 2 and class 0 (+0x844; 2 too with the cheat 0x838470, off); the
   handle FEngHashString("STOCK_%s", CarTypeInfo +0) */
static int stock_type_ok(int t)
{
    const unsigned char *r = airace_type_rec(t);
    return t >= 0 && t < 0x2e && r && r[0x40] && (r[0xda] & 2) && rd32(r + 0x844) == 0;
}
uint32_t career_stock_handle(int t)
{
    char b[64];
    if (!stock_type_ok(t)) return 0;
    snprintf(b, sizeof b, "STOCK_%s", type_name(t));
    return bstringhash(b);
}
int career_stock_type(uint32_t stock)
{
    for (int t = 0; t < airace_ntypes() && t < 0x2e; t++)
        if (stock && career_stock_handle(t) == stock) return t;
    return -1;
}
/* 8012eff8 with the US build region (0, PC 0x864f24): the region's excluded
   car types (PC table 0x7f7c48: region 0 {7, 6} = CORSA, PEUGOT106; region 1
   {5, 18}) */
int career_car_in_region(int t) { return t != 7 && t != 6; }
/* BuildUnlockedCareerCarList 801273d8: every type but the ones a car unlock
   record (GLOBALB 0x34a1f, {type hash, region 0 event, region 1 event}) ties to
   an event not completed yet */
int career_car_unlocked(int t)
{
    for (long k = 0; k + CAR_CARUNLOCK <= TN[14]; k += CAR_CARUNLOCK) {
        const unsigned char *u = T[14] + k;
        const unsigned char *ev = career_event_by_handle(rd32(u + 4));  /* GetBuildRegion 0 */
        if (!ev || career_has_completed(rd32(ev + 8))) continue;
        for (int i = 0; i < airace_ntypes(); i++) {                     /* GetCarTypeInfoFromHash -> +0x840 */
            const unsigned char *r = airace_type_rec(i);
            if (r && rd32(r + 0xd0) == rd32(u)) { if ((int)rd32(r + 0x840) == t) return 0; break; }
        }
    }
    return t >= 0 && t < 0x2e;
}
/* UnlockSystem::IsCarUnlocked(1, ...) (PC 0x534180): CareerUnlocker 0x529f70
   (the car unlock records above; the easter eggs are never on) or
   check_quick_race_unlock 0x529cc0 -- UnlockedAtBootQuickRaceNorthAmerica
   (PC 0x7f7c08, build region 0) */
int career_qr_car_unlocked(int t)
{
    static const int boot[8] = { 0, 1, 2, 5, 10, 25, 28, 22 };
    for (int i = 0; i < 8; i++) if (boot[i] == t) return 1;
    return career_car_unlocked(t);
}
/* IsTrackUnlockedForQuickRace 80129844 (PC 0x5143c0): each event owning the
   race (GetNextEventThatOwnsTrack 80121694: one of its +0x7e races at +0x18,
   4 bytes each) -- the race's IsUnlockedByDefault (+2 bit 1) or the event
   completed; no event owns it = locked */
int career_qr_track_unlocked(int race)
{
    if (beyond_event_type(race) >= 0) return 1;   /* the World Expansion's events: always open */
    for (int i = 0; i < career_nevents(); i++) {
        const unsigned char *ev = career_event(i);
        int n = (signed char)ev[0x7e], k = 0;
        while (k < n && rd16s(ev + 0x18 + 4 * k) != race) k++;
        if (k == n) continue;
        if ((ev[0x18 + 4 * k + 2] & 2) || career_has_completed(rd32(ev + 8))) return 1;   /* check_event_for_track_unlocked 801297b4 */
    }
    return 0;
}
/* UICareerCarLot::BuildCarList(1): the FEStockCar list (car type order; the
   SortCarList comparator 8011d8c4 is constant) filtered by +0xda bit 2,
   IsCarAvailableInCurrentBuildRegion, HasArtworkOnDisc and IsCarAvailableInShop
   (the unlocked list); CanCarBeShownInShop 801274c8 is always 1 */
int career_lot_cars(int *types, int max)
{
    int n = 0;
    for (int t = 0; t < airace_ntypes() && t < 0x2e; t++) {
        if (!stock_type_ok(t) || !career_car_in_region(t)) continue;
        if (career_car_on_disc && !career_car_on_disc(t)) continue;
        if (!career_car_unlocked(t)) continue;
        if (n < max) types[n] = t;
        n++;
    }
    return n < max ? n : max;
}
static void inv_default(CareerCar *c);
/* FECareerCar::ChooseCarForSlot 801348d8: FillFromStockCar -- the stock car,
   no customization; has = 1, filter 4 */
static void choose_for_slot(CareerCar *c, uint32_t stock)
{
    int t = career_stock_type(stock);
    if (!c || t < 0) return;
    c->stock = stock; c->type = t; c->cfg[0] = 0;
    inv_default(c);                                                     /* the parts inventory defaults */
    /* FillFromStockCar 80135388 calls DefaultCustomTuningSpecs 80135004: the
       5 slots at +0x348 (0x29 bytes each) -> CustomPhysicsTunings::Reset(-1)
       801ca218 = bMemSet 100 over 0x17 + 9 + 9 bytes (CONFIRMED G5 11-01) */
    memset(c->tune, 100, sizeof c->tune);
    c->has = 1;
}
void career_choose_car(int slot, uint32_t stock)                        /* CommitChooseCar 801272b8 */
{
    g_career.b873d = 1;                                                 /* +0x873d (GC +0x7e2c) */
    g_career.career_hash = career_slot_handle(slot);                    /* +0x8740 (GC +0x7e38) */
    choose_for_slot(career_car_by_handle(career_slot_handle(slot)), stock);   /* CareerCarChosen 80133f90 */
}
/* CareerCarTrade 80133ff4 (UICareerCarLot CommitTrade): the slot's parts
   inventory defaults and the stock car replaces it; the current car stays */
void career_car_trade(uint32_t old_handle, uint32_t stock)
{
    choose_for_slot(career_car_by_handle(old_handle), stock);
}
static void inv_default(CareerCar *c)                                   /* OwnedPartInventory::Default 80135a28 */
{
    memset(c->own_ent, 0, sizeof c->own_ent); memset(c->own_legacy, 0, sizeof c->own_legacy);
    memset(c->own_perf, 0, sizeof c->own_perf);
    c->nown = 0; c->spent = 0;
}
void career_spend(int cost)                                             /* Spend 8012a010 */
{
    g_career.cash -= cost;
    if (g_career.cash < 0) g_career.cash = 0;
}
static CareerCar *cur_car(void) { return career_car_by_handle(g_career.career_hash); }
/* OwnsCarPart 801358bc: the part's own bit (word & 1 << idx), never for
   the never-owned slots (key 0); an old save's whole entities stay owned */
int career_owns_part(int ent, uint32_t key)
{
    const CareerCar *c = cur_car();
    if (!c || !key) return 0;
    for (int i = 0; i < c->nown; i++) if (c->own[i] == key) return 1;
    return ent > 0 && ent < 96 && (c->own_legacy[ent >> 3] >> (ent & 7) & 1);
}
/* AnyOwned 80135778: the entity's word != 0 (any part of it bought) */
int career_any_owned(int ent)
{
    const CareerCar *c = cur_car();
    if (!c || ent <= 0 || ent >= 96) return 0;
    return ((c->own_ent[ent >> 3] | c->own_legacy[ent >> 3]) >> (ent & 7) & 1);
}
/* BuyCarPart 80129480: Spend, OwnedPartInventory::BuyCarPart 80135858
   (word |= bit; a no-op for the never-owned slots), car +0x7f0 += cost */
void career_buy_part(int ent, uint32_t key, int cost)
{
    CareerCar *c = cur_car();
    career_spend(cost);
    if (!c) return;
    if (key) {
        int have = 0;
        for (int i = 0; i < c->nown && !have; i++) have = c->own[i] == key;
        if (!have && c->nown < CAREER_OWN_MAX) c->own[c->nown++] = key;
        if (ent > 0 && ent < 96) c->own_ent[ent >> 3] |= (uint8_t)(1u << (ent & 7));
    }
    c->spent += cost;
}
int career_owns_perf(int part)
{
    const CareerCar *c = cur_car();
    return c && part >= 0 && part < 128 && (c->own_perf[part >> 3] >> (part & 7) & 1);
}
void career_buy_perf(int part, int cost, int charge, uint32_t brand)
{
    CareerCar *c = cur_car();
    if (charge) career_spend(cost);
    if (!c || part < 0 || part >= 128) return;
    c->own_perf[part >> 3] |= (uint8_t)(1u << (part & 7));
    c->spent += cost;
    for (int i = 0; i < 0x44; i++)          /* OwnedPartInventory::BuyPerfPart 80135930: the first free slot */
        if (!c->brand[i]) { c->brand[i] = brand; break; }
}
int career_decal_brand_unlocked(uint32_t brand)
{
    if (!brand || !perf_brand_in_package(brand)) return 1;   /* is_brand_in_any_perf_package 8012f894 */
    for (int k = 0; k < g_nccar; k++) {                       /* GetCarFiltered(4): the career's cars */
        const CareerCar *c = &g_ccar[k];
        if (!c->has) continue;
        for (int i = 0; i < 0x44 && c->brand[i]; i++)        /* OwnsBrand 801359e8 */
            if (c->brand[i] == brand) return 1;
    }
    return 0;
}
void career_car_set_cfg(uint32_t h, const char *cfg)
{
    CareerCar *c = career_car_by_handle(h);
    if (!c || !c->has) return;
    snprintf(c->cfg, sizeof c->cfg, "%s", cfg ? cfg : "");
}
void career_car_set_stars(uint32_t h, float stars)
{
    CareerCar *c = career_car_by_handle(h);
    if (c && c->has) c->stars = stars;
}

/* the port's profile block (CCAR): version 5, the slot count, then per slot
   handle, locked, has, stock handle, the customization line (u16 length +
   bytes), the inventory (entity bits, perf bits, spent, u16 count + part
   hashes), the stars (f32), the legacy entity bits (12); version 1 had no
   inventory, 2 no stars, 2 and 3 owned most slots per entity (their entity
   bits load as own_legacy: what was bought stays owned); version 5 adds the
   tunings (5 x 0x29 bytes, PresetCarSlot +0x348), older saves load them stock;
   version 6 the perf brands (0x44 x u32, OwnedPartInventory +0x2ac) */
long career_cars_save(unsigned char *out, long cap)
{
    long n = 0;
#define CC_PUT(v, k) do { if (out && n + (k) <= cap) memcpy(out + n, (v), (size_t)(k)); n += (k); } while (0)
    uint32_t ver = 6, cnt = (uint32_t)g_nccar;
    CC_PUT(&ver, 4); CC_PUT(&cnt, 4);
    for (int i = 0; i < g_nccar; i++) {
        const CareerCar *c = &g_ccar[i];
        uint16_t len = (uint16_t)strlen(c->cfg);
        CC_PUT(&c->handle, 4); CC_PUT(&c->locked, 1); CC_PUT(&c->has, 1); CC_PUT(&c->stock, 4);
        CC_PUT(&len, 2); CC_PUT(c->cfg, len);
        CC_PUT(c->own_ent, 12); CC_PUT(c->own_perf, 16); CC_PUT(&c->spent, 4);
        CC_PUT(&c->nown, 2); CC_PUT(c->own, 4L * c->nown);
        CC_PUT(&c->stars, 4);
        CC_PUT(c->own_legacy, 12);
        CC_PUT(c->tune, sizeof c->tune);
        CC_PUT(c->brand, sizeof c->brand);
    }
#undef CC_PUT
    return n;
}
int career_cars_load(const unsigned char *p, long n)
{
    static CareerCar tmp[CAREER_NSLOTS];
    long i = 8;
    uint32_t ver = n >= 8 ? rd32(p) : 0;
    if (n < 8 || ver < 1 || ver > 6 || rd32(p + 4) != CAREER_NSLOTS) return 0;
    memset(tmp, 0, sizeof tmp);
    for (int k = 0; k < CAREER_NSLOTS; k++) {
        CareerCar *c = &tmp[k];
        if (i + 12 > n) return 0;
        c->handle = rd32(p + i); c->locked = p[i + 4]; c->has = p[i + 5]; c->stock = rd32(p + i + 6);
        uint16_t len = rd16(p + i + 10); i += 12;
        if (i + len > n || len >= CAREER_CFG_MAX) return 0;
        memcpy(c->cfg, p + i, len); c->cfg[len] = 0; i += len;
        if (ver >= 2) {
            if (i + 34 > n) return 0;
            memcpy(c->own_ent, p + i, 12); memcpy(c->own_perf, p + i + 12, 16);
            c->spent = (int32_t)rd32(p + i + 28); c->nown = rd16(p + i + 32); i += 34;
            if (c->nown > CAREER_OWN_MAX || i + 4L * c->nown > n) return 0;
            for (int k = 0; k < c->nown; k++) c->own[k] = rd32(p + i + 4L * k);
            i += 4L * c->nown;
        }
        if (ver >= 3) {
            if (i + 4 > n) return 0;
            c->stars = rdf(p + i); i += 4;
        }
        if (ver >= 4) {
            if (i + 12 > n) return 0;
            memcpy(c->own_legacy, p + i, 12); i += 12;
        } else memcpy(c->own_legacy, c->own_ent, 12);   /* per-entity ownership of the old port */
        if (ver >= 5) {
            if (i + (long)sizeof c->tune > n) return 0;
            memcpy(c->tune, p + i, sizeof c->tune); i += (long)sizeof c->tune;
            for (int s = 0; s < 5; s++) for (int t = 0; t < 0x29; t++) if (c->tune[s][t] > 200) c->tune[s][t] = 100;
        } else memset(c->tune, 100, sizeof c->tune);    /* DefaultCustomTuningSpecs: stock */
        if (ver >= 6) {
            if (i + (long)sizeof c->brand > n) return 0;
            for (int b = 0; b < 0x44; b++) c->brand[b] = rd32(p + i + 4 * b);
            i += (long)sizeof c->brand;
        }
        c->type = c->has ? career_stock_type(c->stock) : -1;
        if (c->has && c->type < 0) {
            printf("career: slot %d car %08x is not a stock car\n", k, (unsigned)c->stock);
            c->has = 0; c->cfg[0] = 0;
        }
    }
    if (i != n) return 0;
    memcpy(g_ccar, tmp, sizeof tmp);
    g_nccar = CAREER_NSLOTS;
    return 1;
}

/* ------------------------------------------------------------------ save */

typedef struct { unsigned char *p; long n, cap; } Out;
static void put(Out *o, const void *v, long n)
{
    if (o->p && o->n + n <= o->cap) memcpy(o->p + o->n, v, (size_t)n);
    o->n += n;
}
static void put32(Out *o, uint32_t v) { put(o, &v, 4); }
static void put8(Out *o, uint8_t v) { put(o, &v, 1); }

/* PC 0x514530 (stage infos 0x500fa0, 0x101 bytes each; SMS queue 0x5014f0),
   10981 bytes, byte-identical to SPEED2.EXE's (sim/test/t_career_oracle.c) */
long career_save(unsigned char *out, long cap)
{
    const CareerState *c = &g_career;
    Out o = { out, 0, cap };
    for (int i = 0; i < 8; i++) {
        const CareerStage *s = &c->st[i];
        put(&o, s->world, 0x8c); put(&o, s->sponsor, 0x10); put(&o, s->url, 0x28); put(&o, s->xc4, 0x18);
        put32(&o, (uint32_t)s->dc); put(&o, &s->points, 4); put32(&o, (uint32_t)s->npoints);
        for (int k = 0; k < 5; k++) put32(&o, s->chosen[k] ? bstringhash(career_string(rd16(s->chosen[k]))) : 0);
        put8(&o, s->cleared);
        put32(&o, s->stage ? (uint32_t)(int32_t)(signed char)s->stage[0] : 0);
    }
    put32(&o, (uint32_t)c->cash); put8(&o, c->crib_in_event); put32(&o, c->u872c); put8(&o, c->b873e); put8(&o, c->b873f);
    put32(&o, (uint32_t)c->stage); put32(&o, (uint32_t)c->nrec); put(&o, c->rec, sizeof c->rec);
    put8(&o, c->started); put8(&o, c->b873d); put32(&o, c->career_hash); put8(&o, c->has_spawn);
    put(&o, c->spawn, 0x10); put(&o, &c->spawn_heading, 2); put(&o, &c->f8764, 4); put(&o, c->a8768, 0x100);
    for (int i = 0; i < 0x100; i++) {
        const CareerSmsSlot *m = &c->sms.slot[i];
        put32(&o, m->msg ? bstringhash(career_string(rd16(m->msg))) : 0);
        put(&o, m->b, 4); for (int k = 0; k < 3; k++) put32(&o, m->u[k]);
    }
    put32(&o, c->sms.u1400); put32(&o, c->sms.u1404); put8(&o, c->sms.b140c);
    put(&o, c->a8869, 0x10); put(&o, c->speech, 0x190); put32(&o, c->current_event);
    return o.n;
}

typedef struct { const unsigned char *p; long n, i; int bad; } In;
static void get(In *in, void *v, long n)
{
    if (in->i + n > in->n) { in->bad = 1; memset(v, 0, (size_t)n); return; }
    memcpy(v, in->p + in->i, (size_t)n); in->i += n;
}
static uint32_t get32(In *in) { uint32_t v; get(in, &v, 4); return v; }
static uint8_t get8(In *in) { uint8_t v; get(in, &v, 1); return v; }

/* PC 0x529230 (stage infos 0x514400) */
int career_load(const unsigned char *p, long n)
{
    if (n != career_save(NULL, 0)) { printf("career: save is %ld bytes, want %ld\n", n, career_save(NULL, 0)); return 0; }
    CareerState *c = &g_career;
    career_default();
    In in = { p, n, 0, 0 };
    for (int i = 0; i < 8; i++) {
        CareerStage *s = &c->st[i];
        get(&in, s->world, 0x8c); get(&in, s->sponsor, 0x10); get(&in, s->url, 0x28); get(&in, s->xc4, 0x18);
        s->dc = (int32_t)get32(&in); get(&in, &s->points, 4); s->npoints = (int32_t)get32(&in);
        for (int k = 0; k < 5; k++) {
            uint32_t h = get32(&in);
            s->chosen[k] = h ? career_sponsor_by_hash(h) : NULL;
        }
        s->cleared = get8(&in);
        int32_t st = (int32_t)get32(&in);
        s->stage = career_stage_data(st);
    }
    c->cash = (int32_t)get32(&in); c->crib_in_event = get8(&in); c->u872c = get32(&in); c->b873e = get8(&in); c->b873f = get8(&in);
    c->stage = (int32_t)get32(&in); c->nrec = (int32_t)get32(&in); get(&in, c->rec, sizeof c->rec);
    c->started = get8(&in); c->b873d = get8(&in); c->career_hash = get32(&in); c->has_spawn = get8(&in);
    get(&in, c->spawn, 0x10); get(&in, &c->spawn_heading, 2); get(&in, &c->f8764, 4); get(&in, c->a8768, 0x100);
    for (int i = 0; i < 0x100; i++) {
        CareerSmsSlot *m = &c->sms.slot[i];
        m->msg = career_sms_by_hash(get32(&in));
        get(&in, m->b, 4); for (int k = 0; k < 3; k++) m->u[k] = get32(&in);
    }
    c->sms.u1400 = get32(&in); c->sms.u1404 = get32(&in); c->sms.b140c = get8(&in);
    {   /* 0x514e80: the slots whose message is not in the table dropped (compacted), the count and unread recounted */
        int k = 0, unread = 0;
        for (int i = 0; i < 0x100; i++)
            if (c->sms.slot[i].msg) {
                c->sms.slot[k] = c->sms.slot[i];
                if (!c->sms.slot[k].b[0] && c->sms.slot[k].b[2] != 1) unread++;
                k++;
            }
        memset(&c->sms.slot[k], 0, sizeof c->sms.slot[0] * (size_t)(0x100 - k));
        c->sms.u1400 = (uint32_t)k; c->sms.u1404 = (uint32_t)unread;
    }
    get(&in, c->a8869, 0x10); get(&in, c->speech, 0x190); c->current_event = get32(&in);
    if (in.bad || c->stage < 0 || c->stage > 7 || c->nrec < 0 || c->nrec > 0x180) { career_default(); return 0; }
    return 1;
}

/* the sim autopilot's stall report: what CheckIfClearedStage still waits for */
void career_debug_stage(void)
{
    CareerStage *s = &g_career.st[g_career.stage & 7];
    if (!s->stage) return;
    uint32_t fin = rd32(s->stage + 0x28);
    printf("career stage %d: cleared %d, final event %08x done %d, world %d sponsor %d url %d, sponsor chosen %d\n",
           g_career.stage, s->cleared, fin, fin ? career_has_completed(fin) : -1,
           cstage_world_done(s), cstage_sponsor_done(s), cstage_url_done(s), s->chosen[0] != NULL);
    for (int i = 0; i < career_nshowcases(); i++) {
        const unsigned char *sc = career_showcase(i);
        if ((signed char)sc[0x25] == (signed char)s->stage[0] && sc[0x35])
            printf("  required showcase %s kind %d done %d unlocked %d\n", (const char *)sc, sc[0x24],
                   career_has_completed(rd32(sc + 0x20)), career_record(rd32(sc + 0x20)) != NULL);
    }
    for (int i = 0; i < career_nevents(); i++) {
        const unsigned char *ev = career_event(i);
        if (career_event_in_pile(ev) && !career_has_completed(rd32(ev + 8)))
            printf("  pile: %s hidden %d\n", career_string((uint16_t)(ev[0] | ev[1] << 8)), ev[0x7f]);
    }
}
