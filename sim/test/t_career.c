/* C1 career core: the GCareer tables, the event pile per stage, wins,
   sponsors and stage clears through a whole career, and the save round trip.
   C6: the career car slots (FEPlayerCarDB): unlock per sponsor, the Car Lot
   list, choose, trade and their save.
   usage: t_career [seed] [verbose] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "career.h"
#include "airace.h"

static unsigned char *rd(const char *p, long *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b;
}
static unsigned u16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned bhash(const char *s) { unsigned h = 0xffffffffu; while (*s) h = h * 33 + (unsigned char)*s++; return h; }

static void show_pile(void)
{
    int n = 0, hid = 0, wc = 0;
    for (int i = 0; i < 64; i++) if (g_career.pile.event[i]) { n++; hid += g_career.pile.event[i][0x7f] != 0; }
    for (int i = 0; i < 32; i++) wc += g_career.pile.challenge[i] != NULL;
    printf("  pile: %d events (%d hidden), %d challenges:", n, hid, wc);
    for (int i = 0; i < 64; i++) if (g_career.pile.event[i]) printf(" %s", career_string(u16(g_career.pile.event[i])));
    for (int i = 0; i < 32; i++) if (g_career.pile.challenge[i]) printf(" [%s]", career_string(u16(g_career.pile.challenge[i])));
    printf("\n");
}

static void say(int kind, uint32_t a, uint32_t b) { printf("  speech: kind %d %08x %x\n", kind, a, b); }

/* PathsFreeRoam.bin chunk 0x3414a: {+0 type, +0x30 name hash, +0x42 record size} */
static int world_zones(CareerWorldZone *out, int cap)
{
    long n; unsigned char *d = rd("E:/NFS Underground 2 Vita/data/nfsug2/TRACKS/ROUTESL4RA/PathsFreeRoam.bin", &n);
    int k = 0;
    if (!d) return 0;
    for (long o = 0; o + 8 <= n;) {
        unsigned id = u32(d + o), sz = u32(d + o + 4);
        if (id & 0x80000000u) { o += 8; continue; }
        if (id == 0x3414a)
            for (long r = o + 8; r + 0x44 <= o + 8 + (long)sz && k < cap; ) {
                unsigned rs = u16(d + r + 0x42);
                out[k].type = (int)u32(d + r); out[k].hash = u32(d + r + 0x30); k++;
                if (!rs) break;
                r += rs;
            }
        o += 8 + sz;
    }
    free(d);
    return k;
}
/* LANGUAGES/English.bin chunk 0x39000 (0x4ffa80): IsLabelInDatabase */
static unsigned *g_lab; static int g_nlab;
static void lang_load(void)
{
    long n; unsigned char *d = rd("E:/NFS Underground 2 Vita/data/nfsug2/LANGUAGES/English.bin", &n);
    for (long o = 0; d && o + 8 <= n; ) {
        unsigned id = u32(d + o), sz = u32(d + o + 4);
        if (id == 0x39000u && sz >= 16) {
            unsigned k = u32(d + o + 12), t = u32(d + o + 16);
            g_lab = malloc(sizeof *g_lab * k);
            for (unsigned i = 0; i < k; i++) g_lab[i] = u32(d + o + 8 + t + 8 * i);
            g_nlab = (int)k;
            return;
        }
        o += 8 + sz;
    }
}
static int has_label(uint32_t h) { for (int i = 0; i < g_nlab; i++) if (g_lab[i] == h) return 1; return 0; }
static int g_rmax;
static int rnd_max(int n) { g_rmax = n; return n - 1; }
static int rnd_0(int n) { (void)n; return 0; }
static int zone_type(const CareerWorldZone *z, int n, unsigned h) { for (int i = 0; i < n; i++) if (z[i].hash == h) return z[i].type; return -1; }

static int lot_has(const char *name)
{
    int t[64], n = career_lot_cars(t, 64);
    for (int i = 0; i < n; i++) if (!strcmp(airace_type_name(t[i]), name)) return 1;
    return 0;
}
static void show_lot(const char *when)
{
    int t[64], n = career_lot_cars(t, 64), u, e;
    career_slot_counts(&u, &e);
    printf("  car lot (%s): %d cars, slots unlocked %d empty %d, first empty %d:", when, n, u, e, career_first_empty_slot());
    for (int i = 0; i < n; i++) printf(" %s", airace_type_name(t[i]));
    printf("\n");
}

int main(int argc, char **argv)
{
    int verbose = argc > 2 ? atoi(argv[2]) : 0;
    long gl; unsigned char *g = rd("E:/NFS Underground 2 Vita/data/nfsug2/GLOBAL/GLOBALB.BUN", &gl);
    if (!g || !career_init(g, gl)) { printf("no career data\n"); return 1; }
    lang_load();
    if (g_nlab < 1000) { printf("no English.bin labels\n"); return 1; }
    int bad = 0;
    if (airace_types_init(g, gl) < 31) { printf("no car types\n"); return 1; }
    career_default();
    /* GetCarTypeInfoFromHash: +0xd0 = bStringHash(+0 name), +0x840 = the index */
    for (int t = 0; t < airace_ntypes(); t++) {
        const unsigned char *r = airace_type_rec(t);
        if (u32(r + 0xd0) != bhash((const char *)r) || (int)u32(r + 0x840) != t || strcmp((const char *)r, airace_type_name(t))) {
            printf("car type %d %s: hash/index mismatch\n", t, (const char *)r); bad++;
        }
    }
    /* C6: DefaultCareerCars + the first slot unlocked; no current car yet ("CAREER_0") */
    {
        int u, e; career_slot_counts(&u, &e);
        printf("career cars: slot handles %08x..%08x, unlocked %d empty %d, current %p\n", career_slot_handle(0),
               career_slot_handle(4), u, e, (void *)career_current_car());
        if (u != 1 || e != 1 || career_first_empty_slot() != 0 || career_current_car() || career_car_slot(0)->locked ||
            !career_car_slot(1)->locked || career_slot_handle(0) != bhash("CAREER_SLOT_0")) bad++;
    }
    /* the save stores chosen sponsors by bStringHash(name): it must be the record's +8 */
    for (int i = 0; i < 20; i++) {
        const unsigned char *sp = NULL;
        for (int s = 0; s < 8 && !sp; s++) sp = cstage_sponsor_choice(&g_career.st[s], 0);
        (void)sp;
    }
    for (int s = 2; s < 6; s++)
        for (int k = 0; k < 5; k++) {
            const unsigned char *sp = cstage_sponsor_choice(&g_career.st[s], k);
            if (!sp) { printf("stage %d sponsor %d missing\n", s, k); bad++; continue; }
            if (bhash(career_string(u16(sp))) != u32(sp + 8)) { printf("sponsor %s hash mismatch\n", career_string(u16(sp))); bad++; }
        }
    printf("sponsor hash check: %s\n", bad ? "FAIL" : "ok");

    career_build_events();
    show_pile();
    printf("stage 0 before the tutorial flag: pile should be empty\n");

    /* C2: a new career (StartCareerHelper -> scene01 -> free roam in DDAY_PLAYER_CAR) */
    if (career_start() != CSTART_FIRST_MOVIE) { printf("new career: no first movie\n"); bad++; }
    if (!career_first_movie_done()) { printf("new career: no start NIS\n"); bad++; }
    const unsigned char *lot = NULL, *crib = NULL;
    for (int i = 0; i < 24; i++) {
        const unsigned char *sh = career_shop_by_handle(0);
        (void)sh;
    }
    {
        const unsigned char *a = career_event_by_handle(CAREER_DDAY_A), *b = career_event_by_handle(CAREER_DDAY_B);
        lot = career_shop_by_handle(u32(a + 0x3c)); crib = career_shop_by_handle(u32(b + 0x3c));
        printf("current %08x preset %08x (DDAY_PLAYER_CAR f7e19102) gps %08x lot %s crib %s\n", g_career.current_event,
               career_ride_preset(), career_gps_shop(), lot ? (const char *)lot : "-", crib ? (const char *)crib : "-");
        if (g_career.current_event != CAREER_DDAY_A || career_ride_preset() != 0xf7e19102u || !lot || !crib ||
            career_gps_shop() != u32(lot + 0x3c)) bad++;
    }
    /* the stage-0 intro: speech always done, the message read and dismissed at 10 s */
    career_stage0_begin(0);
    int s0 = 0;
    for (unsigned t = 0; t < 60000 && career_stage0_has_control(); t += 100) {
        if (t == 10000) { career_stage0_read_message(); career_stage0_sms_dismissed(); }
        int act = career_stage0_update(t, 1);
        if (act) printf("  stage0 t=%5u state %d actions %x minimap %d gauges %d\n", t, g_career.s0.state, act,
                        career_stage0_show(2), career_stage0_show(4));
        s0 |= act;
    }
    if (s0 != (CS0_INTRO | CS0_SMS | CS0_MINIMAP | CS0_CARLOT | CS0_RELINQUISH) || career_first_time()) {
        printf("stage0 intro incomplete (%x)\n", s0); bad++;
    }
    /* CareerSpeech: CanSeeEvents 15 s after A began (the speech done), not before */
    career_speech_hook = say;
    career_speech_tick(14000, 1);
    if (g_career.b873f) { printf("CanSeeEvents before 15 s\n"); bad++; }
    career_speech_tick(1000, 0);
    if (g_career.b873f) { printf("CanSeeEvents while a line plays\n"); bad++; }
    career_speech_tick(16, 1);
    career_build_events();
    printf("after 15 s: CanSeeEvents %d, line 3 queued %d\n", g_career.b873f, career_speech_queued(3));
    show_pile();
    if (!g_career.b873f || !career_speech_queued(3)) bad++;
    for (int i = 0; i < 30; i++) career_speech_tick(1000, 1);
    if (career_speech_queued(3)) bad++;

    /* C3: the world layer on L4RA's zones while A is current */
    static CareerWorldZone wz[1024];
    int nwz = world_zones(wz, 1024);
    career_set_world_zones(wz, nwz);
    career_recalc_zones();
    int nun = 0, nradar = 0;
    for (int i = 0; i < 64; i++) {
        const CareerZone *z = career_unlocked_zone(i);
        if (!z) continue;
        nun++; nradar += career_zone_on_radar(z);
        printf("  unlocked %08x (type %d) %s%s radar %d\n", z->zone, zone_type(wz, nwz, z->zone),
               z->event ? career_string(u16(z->event)) : z->shop ? (const char *)z->shop : "?", z->pile ? " [pile]" : "",
               career_zone_on_radar(z));
    }
    printf("world: %d zones, %d unlocked, %d on the radar\n", nwz, nun, nradar);
    if (!nwz || nun < 3) bad++;
    {   /* drive into STAGE_0_CIRCUIT_1's zone, then out */
        const unsigned char *ev = career_event_by_handle(0xca207584u);
        unsigned zh = u32(ev + 0x28);
        int r1 = career_player_zones(&zh, 1), eng = career_can_engage();
        const CareerZone *cz = career_current_zone();
        int r2 = career_player_zones(NULL, 0);
        printf("zone %08x: enter %x engage %d current %s, leave %x\n", zh, r1, eng,
               cz && cz->event ? career_string(u16(cz->event)) : "-", r2);
        if (r1 != CZONE_ENTER || !eng || r2 != CZONE_EXIT || career_current_zone()) bad++;
        /* the HUD's EventIndicator type (circuit 1) for the zone entered and the one left */
        CareerZone ez; career_zone_data(zh, &ez);
        if (career_zone_indicator(&ez) != 1 || !career_left_zone() || career_left_zone()->zone != zh ||
            career_zone_indicator(career_left_zone()) != 1) { printf("indicator: circuit zone not type 1\n"); bad++; }
    }
    {   /* C4e: engage the zone -> RaceEngageDialog; decline, then accept (NotifyEnterEvent) */
        CareerState keep = g_career;
        const unsigned char *ev = career_event_by_handle(0xca207584u);
        unsigned zh = u32(ev + 0x28);
        career_player_zones(&zh, 1);
        career_engage_requested();
        career_set_engaging_dialog(1);                   /* RacingJoyHandler: SetEngagingDialog(1) */
        int blocked = !career_can_engage();
        career_set_engaging_dialog(0);                   /* RCStatePauseCallback: the dialog is up */
        const CareerZone *ez = career_engaged();
        uint32_t lab, sub, typ;
        const char *from = career_engage_from(ev, &lab);
        career_engage_race_type(ev, &sub, &typ);
        uint32_t body = career_engage_body(ev, rnd_0);
        printf("engage %s: blocked while engaging %d, engaged %s, body %08x (label %d), from %s/%08x, subject %08x type %08x\n",
               career_string(u16(ev)), blocked, ez && ez->event == ev ? "it" : "-", body, has_label(body),
               from ? from : "-", lab, sub, typ);
        if (!blocked || !ez || ez->event != ev || body != bhash("ENGAGE_STAGE_0_CIRCUIT_1") || !has_label(body) ||
            !has_label(sub) || !has_label(typ) || !has_label(0x17ea64f8u) || !has_label(0x9f29d70du)) bad++;
        career_decline_event(ev);
        CareerRecord *r = career_record(u32(ev + 8));
        int declined = r ? r->flag[CREC_DONE] : -1;
        float at[3] = { 10, 20, 30 };
        unsigned n0 = g_career.u872c;
        g_career.has_spawn = 1;
        career_enter_event(ev, at, 0x4321);
        r = career_record(u32(ev + 8));
        printf("  declined %d; entered: record %d, counter +%u, preset %08x, spawn %d (%.0f %.0f %.0f / %04x)\n", declined,
               r ? r->flag[CREC_DONE] : -1, g_career.u872c - n0, g_career.engaged_preset, g_career.has_spawn,
               g_career.spawn[0], g_career.spawn[1], g_career.spawn[2], g_career.spawn_heading);
        if (declined != 3 || !r || r->flag[CREC_DONE] != 2 || g_career.u872c != n0 + 1 || g_career.engaged_event != ev ||
            g_career.engaged_preset != u32(ev + 0x2c) || (u32(ev + 0x2c) && career_ride_preset() != u32(ev + 0x2c)) || g_career.has_spawn ||
            g_career.spawn[1] != 20 || g_career.spawn_heading != 0x4321) bad++;
        r->flag[CREC_DONE] = CDONE_WON;                  /* a won event stays won */
        career_enter_event(ev, NULL, 0);
        if (r->flag[CREC_DONE] != CDONE_WON) { printf("enter event: a won record reset\n"); bad++; }
        career_player_zones(NULL, 0);
        g_career = keep;
        /* every event's message and type labels are in English.bin (the stage 1+ random ones at both ends) */
        int miss = 0;
        for (int i = 0; i < career_nevents(); i++) {
            const unsigned char *e = career_event(i);
            if (e[0xf] == 3) continue;
            uint32_t b0 = career_engage_body(e, rnd_0), b1 = career_engage_body(e, rnd_max), l2;
            career_engage_race_type(e, &sub, &typ);
            const char *f = career_engage_from(e, &l2);
            if (!has_label(b0) || !has_label(b1) || !has_label(sub) || !has_label(typ) || (!f && !has_label(l2))) {
                if (miss++ < 8) printf("  %s: body %08x/%08x sub %08x type %08x from %08x missing\n", career_string(u16(e)),
                                       b0, b1, sub, typ, l2);
            }
        }
        printf("engage labels: %d events, %d with a label missing\n", career_nevents(), miss);
        if (miss) bad++;
    }
    {   /* banks: one pickup at stage 0, then never again */
        int got = 0, any = 0;
        for (int i = 0; i < nwz && !got; i++) if (wz[i].type == 19 && career_bank_claimable(wz[i].hash)) {
            any++; int c0 = g_career.cash; got = career_bank_zone(wz[i].hash);
            printf("bank %08x: +$%d (cash %d -> %d), again %d\n", wz[i].hash, got, c0, g_career.cash, career_bank_zone(wz[i].hash));
        }
        if (!got) { printf("no bank claimable at stage 0 (%d)\n", any); }
    }
    {   /* EngageShopDialog's text for the car lot / crib; the shops' indicator types */
        CareerZone zl = { 1 }, zc = { 2 };
        zl.shop = lot; zc.shop = crib;
        uint32_t lb = career_shop_body(lot, has_label);
        printf("shop dialog: lot header %08x body %08x accept %08x (type %x), crib header %08x body %08x accept %08x (type %x)\n",
               career_shop_header(lot), lb, career_shop_accept(lot), career_zone_indicator(&zl), career_shop_header(crib),
               career_shop_body(crib, has_label), career_shop_accept(crib), career_zone_indicator(&zc));
        if (career_shop_header(lot) != 0x4dc0a646u || !has_label(lb) || career_shop_accept(lot) != 0x054b17dcu ||
            career_zone_indicator(&zl) != 0xb || career_shop_header(crib) != 0x7ce21662u ||
            career_shop_body(crib, has_label) != 0x987eecdau || career_shop_accept(crib) != 0x67f55a89u ||
            career_zone_indicator(&zc) != 0xc || !has_label(0x4dc0a646u) || !has_label(0x987eecdau) ||
            !has_label(0x054b17dcu) || !has_label(0x67f55a89u)) bad++;
    }
    float at[3] = { 1, 2, 3 };
    career_enter_shop(lot, at, 0x1234);
    printf("car lot: A won %d, current %08x, gps %08x (crib zone %08x), spawn %d\n", career_has_completed(CAREER_DDAY_A),
           g_career.current_event, career_gps_shop(), u32(crib + 0x3c), g_career.has_spawn);
    if (!career_has_completed(CAREER_DDAY_A) || g_career.current_event != CAREER_DDAY_B || career_gps_shop() != u32(crib + 0x3c)) bad++;
    if (career_start() != CSTART_FREE_ROAM || g_career.current_event != CAREER_DDAY_B || career_free_roam_start() ||
        !g_career.has_spawn || career_stage0_has_control()) { printf("resume: not free roam with B\n"); bad++; }
    if (g_career.shop != lot || g_career.engaged_preset) { printf("enter shop: shop / engaged preset\n"); bad++; }
    /* C6: the Car Lot at stage 0 -- one empty slot (state 2): buy a stock car */
    show_lot("stage 0");
    if (lot_has("CORSA") || lot_has("PEUGOT106") || lot_has("350Z") || lot_has("SKYLINE") || !lot_has("PEUGOT") ||
        !lot_has("MIATA") || !lot_has("FOCUS")) { printf("stage 0 car lot list wrong\n"); bad++; }
    {
        int t[64]; career_lot_cars(t, 64);
        int slot = career_first_empty_slot();
        uint32_t stock = career_stock_handle(t[0]);
        career_choose_car(slot, stock);
        const CareerCar *cc = career_current_car();
        printf("bought %s (stock %08x) into slot %d: b873d %d career_hash %08x current %s\n", airace_type_name(t[0]),
               stock, slot, g_career.b873d, g_career.career_hash, cc ? career_car_name(cc) : "-");
        char nm[64]; snprintf(nm, sizeof nm, "STOCK_%s", airace_type_name(t[0]));
        if (slot != 0 || stock != bhash(nm) || !g_career.b873d || g_career.career_hash != career_slot_handle(0) || !cc ||
            cc->type != t[0] || cc->cfg[0] || career_first_empty_slot() != -1) bad++;
    }
    g_career.has_car = 1;

    int races = 0;
    for (int guard = 0; guard < 400 && !career_completed_career(); guard++) {
        int st = g_career.stage;
        if (st >= 3) g_career.have_suv_car = 1;
        career_build_events();
        CareerStage *s = &g_career.st[st];
        if (cstage_need_sponsor(s)) {
            int nsp = cstage_num_sponsors_to_choose(s, cstage_avg_points(&g_career.st[st - 1]));
            printf("stage %d: %d sponsors to choose from (avg points %.0f)\n", st, nsp, cstage_avg_points(&g_career.st[st - 1]));
            if (nsp < 1) { printf("no sponsor offered\n"); bad++; break; }
            const unsigned char *sp = cstage_sponsor_choice(s, guard % nsp);
            int u0 = career_slot_counts(NULL, NULL);
            career_choose_sponsor(sp);
            printf("stage %d: sponsor %s, cash %d, car slots unlocked %d -> %d\n", st, career_string(u16(sp)), g_career.cash,
                   u0, career_slot_counts(NULL, NULL));
            if (career_slot_counts(NULL, NULL) != (u0 < CAREER_NSLOTS ? u0 + 1 : u0)) bad++;
            if (verbose) show_lot("after sponsor");
            continue;
        }
        if (verbose) show_pile();
        const unsigned char *ev = NULL;
        for (int i = 0; i < 64 && !ev; i++) if (g_career.pile.event[i] && !g_career.pile.event[i][0x7f]) ev = g_career.pile.event[i];
        for (int i = 0; i < 64 && !ev; i++) if (g_career.pile.event[i]) ev = g_career.pile.event[i];
        if (!ev) {
            /* nothing to race: the stage's showcases (C9) gate the final event */
            int any = 0;
            for (int i = 0; i < career_nshowcases(); i++) {
                const unsigned char *sc = career_showcase(i);
                if ((signed char)sc[0x25] == st && !career_has_completed(u32(sc + 0x20))) {
                    career_add_record(u32(sc + 0x20))->flag[CREC_DONE] = CDONE_WON; any++;
                }
            }
            printf("stage %d: pile empty, %d showcases marked done (world %d url %d sponsor %d)\n", st, any,
                   cstage_world_done(s), cstage_url_done(s), cstage_sponsor_done(s));
            const unsigned char *sd = s->stage;
            const unsigned char *fin = sd ? career_event_by_handle(u32(sd + 0x28)) : NULL;
            if (!any && fin && u32(fin + 8) == CAREER_DDAY_B && g_career.current_event == CAREER_DDAY_B) {
                career_enter_shop(crib, at, 0);          /* DDAY_EVENT_B: home to the crib */
                printf("stage %d: entered %s, B won %d, current %08x, start -> %d (crib %d)\n", st, (const char *)crib,
                       career_has_completed(CAREER_DDAY_B), g_career.current_event, career_start(), CSTART_CRIB);
                if (career_start() != CSTART_CRIB) bad++;
                continue;
            }
            if (!any) {
                printf("stage %d stuck: final %s done %d\n", st, fin ? career_string(u16(fin)) : "-",
                       fin ? career_has_completed(u32(fin + 8)) : -1);
                bad++;
                break;
            }
            continue;
        }
        g_career.current_event = u32(ev + 8);
        career_commit_win(u32(ev + 8), 1, 3.0f);
        g_career.current_event = 0;
        races++;
    }
    printf("career %s after %d races: stage %d, cash %d, records %d\n",
           career_completed_career() ? "COMPLETED" : "not completed", races, g_career.stage, g_career.cash, g_career.nrec);
    for (int s = 0; s < 6; s++)
        printf(" stage %d: cleared %d world %d url %d sponsor %d points %.0f/%d\n", s, g_career.st[s].cleared,
               cstage_world_done(&g_career.st[s]), cstage_url_done(&g_career.st[s]),
               cstage_sponsor_done(&g_career.st[s]), g_career.st[s].points, g_career.st[s].npoints);

    /* C6 at the end: every car unlocked; slots, a second car, a trade */
    show_lot("career done");
    if (!lot_has("350Z") || !lot_has("SKYLINE") || lot_has("CORSA")) { printf("career done: car lot list wrong\n"); bad++; }
    {
        int u, e, t[64], nl = career_lot_cars(t, 64);
        career_slot_counts(&u, &e);
        int slot = career_first_empty_slot();
        uint32_t cur = g_career.career_hash;
        career_car_set_cfg(career_slot_handle(0), "PEUGOT 0 1 1 1 0 0 0 0 0 0 0 0 PAINT 1");
        career_choose_car(slot, career_stock_handle(t[nl - 1]));
        printf("slots: unlocked %d empty %d, second car %s in slot %d, current %08x\n", u, e,
               career_car_name(career_car_slot(slot)), slot, g_career.career_hash);
        if (slot != 1 || g_career.career_hash != career_slot_handle(1)) bad++;
        g_career.career_hash = cur;                                /* back on slot 0 (the crib's car select) */
        career_car_trade(career_slot_handle(0), career_stock_handle(t[1]));
        const CareerCar *cc = career_current_car();
        printf("trade slot 0 for %s: current %s cfg '%s'\n", airace_type_name(t[1]), cc ? career_car_name(cc) : "-", cc ? cc->cfg : "");
        if (!cc || cc->type != t[1] || cc->cfg[0] || g_career.career_hash != cur) bad++;
        career_car_set_cfg(career_slot_handle(1), "SKYLINE 1 2 2 2 PAINT 3");
    }
    {   /* the car block round trip, and over a career load (which defaults the cars first) */
        long cn = career_cars_save(NULL, 0);
        unsigned char *ca = malloc(cn), *cb = malloc(cn);
        career_cars_save(ca, cn);
        long n2 = career_save(NULL, 0); unsigned char *cs = malloc(n2); career_save(cs, n2);
        int ok1 = career_load(cs, n2);
        int u1 = career_slot_counts(NULL, NULL);
        int ok2 = career_cars_load(ca, cn);
        career_cars_save(cb, cn);
        printf("car save: %ld bytes, career load %d (slots after it %d), cars load %d, round trip %s, current %s '%s'\n", cn,
               ok1, u1, ok2, memcmp(ca, cb, cn) ? "DIFFERS" : "identical", career_car_name(career_current_car()),
               career_car_slot(1)->cfg);
        if (!ok1 || u1 != 1 || !ok2 || memcmp(ca, cb, cn) || !career_current_car() || strcmp(career_car_slot(1)->cfg, "SKYLINE 1 2 2 2 PAINT 3")) bad++;
        if (career_cars_load(ca, cn - 1)) { printf("short car block accepted\n"); bad++; }
        free(ca); free(cb); free(cs);
    }

    long n = career_save(NULL, 0);
    unsigned char *a = malloc(n), *b = malloc(n);
    career_save(a, n);
    int ok = career_load(a, n);
    career_save(b, n);
    printf("save: %ld bytes, load %d, round trip %s\n", n, ok, memcmp(a, b, n) ? "DIFFERS" : "identical");
    if (!ok || memcmp(a, b, n)) bad++;
    printf("%s\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}
