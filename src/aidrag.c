/* aidrag.c -- the drag race's AI, ported from SPEED2.EXE (v1.2). Included
 * by airacer.c (after aitraffic.c); notes in E:/re-tools/out/DRAG.md.
 *
 * In a drag race (0x89e7d8) every racer drives the traffic lanes 2..5 of the
 * race's road net. The AI racers take the goal "Win drag race" (2, vtable
 * 0x7847d0): the road race's goal on the lanes, with its own chooser
 * (0x42d470). The human's car gets a controller too -- the DragController
 * (type 2, 0x4299c0) with the goal "Steer player in drag race" (3, vtable
 * 0x784830): the AI steers the car along the lane the player picks with the
 * lane-change buttons, the pad keeps the pedals, the gears and the nitrous
 * (DragDriver 0x4220a0). */

/* the lanes left behind by a lane change (0x40f4a0 with an excluded range):
   the traffic lane at route point p nearest q (backward along the road:
   lanes 6..9, as 2..5), lo..hi skipped unless either is 0xff; -1 none */
static int lane_near_ex(const AiRacer *r, Pt *p, const float *q, int lo, int hi)
{
    const AiNet *n = r->w->net;
    if (!p) return -1;
    const unsigned char *s = sec_of(n, p);
    if (!s) return -1;
    int back = !r->rdir, first = back ? 6 : 2, best = -1;
    if (back) { if (lo != 0xff) lo += 4; if (hi != 0xff) hi += 4; }
    int all = lo == 0xff || hi == 0xff;
    float lat = pt_latq(n, p, q, back), bd = 8.999999e9f;
    for (int m = first; m < first + 4; m++) {
        if (!s[0x82 + m]) continue;
        if (!all && lo <= m && m <= hi) continue;
        float d = fabsf(pt_laneoff(p, m) - lat);
        if (d < bd) { bd = d; best = m; }
    }
    if (best != -1 && back) best -= 4;
    return best;
}
/* an entry's list point (L +8 [entry +0x28]) and its route point */
static const FPoint *ent_fp(const AiRacer *r, const RacerEnt *e)
{
    if (!e || e->idx < 0 || e->idx >= r->L.n) return NULL;
    return r->L.p[e->idx];
}
static Pt *ent_pt(const AiRacer *r, const RacerEnt *e)
{
    const FPoint *f = ent_fp(r, e);
    if (!f || f->sec == -1 || f->pt == -1) return NULL;
    return pt_at(r->w->net, f->sec, f->pt);
}

/* ---- the goals' rules ------------------------------------------------------ */
/* 0x414070 (goal 3's vt+0x28): a branch the human's car takes -- kinds 2
   and 8 always (0x5dcbe0(2)), 0 yes, 5 / 10 / 7 no; kind 4 when the target
   section has the car's lane (0x40da10: lanes outside 2..9 as 2) */
static int drag_take_link(AiRacer *r, Lk *l)
{
    uint32_t f = lk_flags(l);
    int k = f & 0xf;
    if (k == 2 || k == 8) return 1;
    if (k == 5 || k == 10 || k == 7) return 0;
    if (k == 0) return 1;
    if (k != 4) return 0;
    const unsigned char *ts = lk_sec(r->w->net, l);
    if (!ts) return 0;
    int lane = r->ls.lane;                                              /* ctrl vt+0x1c (0x5ec0d0) */
    if (lane < 2 || 9 < lane) return ts[0x82 + 2] != 0;
    return ts[0x82 + lane + ((f >> 23 & 1) != (f >> 18 & 1) ? 4 : 0)] != 0;
}

/* 0x40b110: the lane changes a drag track forbids over stretches of its
   race route (0x7e9728 / 0x7e98a8 / 0x7e9a58 for events 4211..4213): from,
   to, the progress from / to, AI racers 80 m earlier (0x783ccc), the race's
   direction (0x89e7a4) */
typedef struct { int from, to; float lo, hi; int ai_early, dir; } DragZone;
static const DragZone DZ_4211[16] = {
    {2,3,847,874,1,0}, {4,3,847,874,1,0}, {4,5,847,874,1,0}, {5,4,847,874,1,0},
    {2,3,988,1012,1,0}, {3,2,988,1012,1,0}, {3,4,988,1012,1,0}, {5,4,988,1012,1,0},
    {2,3,1270,1295,1,1}, {4,3,1270,1295,1,1}, {4,5,1270,1295,1,1}, {5,4,1270,1295,1,1},
    {2,3,1409,1434,1,1}, {3,2,1409,1434,1,1}, {3,4,1409,1434,1,1}, {5,4,1409,1434,1,1} };
static const DragZone DZ_4212[18] = {
    {2,3,849,873,1,0}, {3,2,849,873,1,0}, {3,4,849,873,1,0}, {5,4,849,873,1,0},
    {2,3,988,1012,1,0}, {3,2,988,1012,1,0}, {3,4,988,1012,1,0}, {4,3,988,1012,1,0}, {4,5,988,1012,1,0},
    {3,2,1270,1295,1,1}, {3,4,1270,1295,1,1}, {4,3,1270,1295,1,1}, {4,5,1270,1295,1,1}, {5,4,1270,1295,1,1},
    {2,3,1409,1434,1,1}, {4,3,1409,1434,1,1}, {4,5,1409,1434,1,1}, {5,4,1409,1434,1,1} };
static const DragZone DZ_4213[12] = {
    {3,2,847,874,1,0}, {3,4,847,874,1,0}, {5,4,847,874,1,0},
    {2,3,988,1012,1,0}, {4,3,988,1012,1,0}, {4,5,988,1012,1,0},
    {3,2,1270,1295,1,1}, {3,4,1270,1295,1,1}, {5,4,1270,1295,1,1},
    {2,3,1409,1434,1,1}, {4,3,1409,1434,1,1}, {4,5,1409,1434,1,1} };
static int drag_zone_ok(int from, int to, float prog, int dir, int event, int ai)
{
    const DragZone *z = NULL;
    int n = 0;
    if (event == 4211) { z = DZ_4211; n = 16; }
    else if (event == 4212) { z = DZ_4212; n = 18; }
    else if (event == 4213) { z = DZ_4213; n = 12; }
    for (int i = 0; i < n; i++, z++) {
        float lo = z->lo;
        if (z->ai_early && ai) lo -= 80.0f;
        if (z->dir == dir && z->from == from && z->to == to) {
            float v = lo < prog ? prog : lo;
            if (z->hi < v) v = z->hi;
            if (prog == v) return 0;
        }
    }
    return 1;
}
/* 0x412500's drag race branch: between two traffic lanes, from a lane the
   point has, every lane on the way must be there, run on no shorter than
   the one before (0x40af30 with -0.1, 0x7e8e64's negative) and be open at
   the car's progress (0x40b110) */
/* one lane of a drag lane change (0x412500's loop): the lane there, run
   on no shorter than the one before (0x40af30, -0.1), and open (0x40b110) */
static int drag_step_ok(const AiRacer *r, const FPoint *f, int c, int k, float prog, int ai);
/* diagnostics: why the player's last lane step was refused */
static char g_drag_why[160];
static void drag_why(const AiRacer *r, const char *fmt, ...)
{
    if (!r || !r->dctl) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_drag_why, sizeof g_drag_why, fmt, ap);
    va_end(ap);
}
static int drag_lane_steps(const AiRacer *r, const FPoint *f, int from, int to)
{
    const AiWorld *w = r->w;
    const AiNet *n = w->net;
    if (!(w->drag || w->x890108)) return 1;
    if (from < 2 || 5 < from || to < 2 || 5 < to || !fp_haslane(n, f, from)) return 1;
    int ai = r->owner_kind == 2;
    float prog = r->progress;                                           /* car +0x8fc */
    if (to < from) {
        for (int c = from; to + 1 <= c; c--) {
            int k = c - 1;
            if (!drag_step_ok(r, f, c, k, prog, ai)) return 0;
        }
    } else {
        for (int c = from; c <= to - 1; c++) {
            int k = c + 1;
            if (!drag_step_ok(r, f, c, k, prog, ai)) return 0;
        }
    }
    return 1;
}

static int drag_step_ok(const AiRacer *r, const FPoint *f, int c, int k, float prog, int ai)
{
    const AiWorld *w = r->w;
    if (!fp_haslane(w->net, f, k)) { drag_why(r, "step %d->%d: lane %d absent at sec %d pt %d", c, k, k, f->sec, f->pt); return 0; }
    if (!fl_lane_gap(f->list, k, c, -0.1f)) {
        drag_why(r, "step %d->%d: lane %d runs %.1f m, lane %d %.1f m (0x40af30)", c, k, k,
                 f->list->lanemin[k - 1], c, f->list->lanemin[c - 1]);
        return 0;
    }
    if (!drag_zone_ok(c, k, prog, w->reversed, w->event, ai)) {
        drag_why(r, "step %d->%d: no-change zone at %.0f m (0x40b110, dir %d)", c, k, prog, w->reversed);
        return 0;
    }
    return 1;
}

/* ---- the actions --------------------------------------------------------- */
/* 0x5a0160 on the car's contacts: touching a wall */
static int wall_contact(const AiRacer *r)
{
    for (int i = 0; i < r->ncontact; i++) if (r->contact[i].wall) return 1;
    return 0;
}
/* the actions' first step on a DragController's car (car +0x24 == 2): the
   lanes left behind are forgotten */
static void drag_forget(AiRacer *r)
{
    if (r->car.ctrl_type != 2) return;
    r->dl_lo = r->dl_hi = 0xff;
    r->dl_moved = 0;
}
/* HitWallDragRace exec 0x4249c0: back into the lane nearest the car at its
   list point (rate 0.151, 0x7837d8), flat out (vt+0x7c) */
static void hitwall_begin(AiRacer *r, AiAction *a)
{
    drag_forget(r);
    int lane = lane_near_ex(r, ent_pt(r, own_ent(r)), r->car.pos, 0xff, 0xff);
    if (lane != -1) lane_set3(&r->ls, lane, 0, 0.151f, r->w->time);
    plan_build(r, &r->ls, &a->plan);                                    /* 0x420d40 */
    a->speed = 1000.0f;                                                 /* ctrl vt+0x7c (0x5a9f50) */
    a->x1f4 = 0; a->x1f8 = 0;
    a->use_cap = 0; a->go = 1; a->catchup = 1;
}
/* IJustGotHitDragRace exec 0x4246b0: hit by a car in its own lane, move a
   lane away from it -- unless the lane towards it is open and the car is
   ahead (both inside their lane), or the hitter is no further across (or a
   human); flat out */
static void hitdrag_begin(AiRacer *r, AiAction *a)
{
    AiWorld *w = r->w;
    if (a->target >= 0) {
        drag_forget(r);
        const RacerEnt *o = own_ent(r), *e = ent_of(r, a->target);      /* 0x60c120 */
        const AiSeen *s = seen_car(w, a->target);
        int mine = lane_near_ex(r, ent_pt(r, o), r->car.pos, 0xff, 0xff);
        int his = e && s ? lane_near_ex(r, ent_pt(r, e), s->v->pos, 0xff, 0xff) : -1;
        if (mine != -1 && his != -1) {
            int lane = mine;
            float rel = ent_lat(e, mine) - own_lat(r, mine);            /* 0x60c010 */
            if (mine == his) {
                float oa = fabsf(own_lat(r, mine)), ha = fabsf(ent_lat(e, mine));   /* 0x60bfa0 */
                int keep;
                if (own_ext(r, 0) <= oa || !(ha < e->ext[0])) keep = !(oa < ha) || seen_human(s);   /* 0x614880 */
                else keep = !(ent_ahead(r, e) < 0);                     /* 0x60bff0 */
                const FPoint *f = ent_fp(r, o);
                int toward = rel < 0 ? mine - 1 : mine + 1;
                if (!(f && lane_step_ok(r, f, mine, toward)) || keep) {
                    int away = rel < 0 ? mine + 1 : mine - 1;
                    if (f && lane_step_ok(r, f, mine, away)) lane = away;
                }
            }
            lane_set(&r->ls, lane);                                     /* 0x405410 */
        }
    }
    plan_build(r, &r->ls, &a->plan);                                    /* 0x420d40 */
    a->speed = 1000.0f;                                                 /* ctrl vt+0x7c */
    a->x1f4 = 0; a->x1f8 = 0;
    a->use_cap = 0; a->go = 1; a->catchup = 1;
}

/* the goals' choosers: "Win drag race" 0x42d470 -- GetUnstuck as the road
   race's, then (ctrl +0x780 is never set) HitWallDragRace (300) touching a
   wall, IJustGotHitDragRace (400) after a hit of 8.33 (0x78373c), then
   AvoidAvoidable (300, the skill's blob +0x534 with it, no approval), else
   DriveTrafficLane; "Steer player in drag race" 0x426a70 -- HitWallDragRace,
   IJustGotHitDragRace, DriveTrafficLane */
static AiAction *drag_choose(AiRacer *r, int cur, int prio)
{
    AiWorld *w = r->w;
    const AiCarView *c = &r->car;
    if (r->goal == GOAL_DRAG_WIN && cur != ACT_UNSTUCK && prio <= 500 && 6.0f < w->time - r->unstuck_time &&
        unstuck_prio(r) >= 0) {                                         /* vt+0x94 / 0x40e7c0 */
        float hold = 1.3f;                                              /* vt+0x98 0x4083e0 */
        if (r->rpt) {
            int back = !r->rdir;
            float out = pt_outside(r->rpt, pt_latq(w->net, r->rpt, c->pos, back), back);
            if (0 < out && r->prog5.count == r->prog5.n && r->prog5.avg < 0.2f) hold = 1.6f;
        }
        r->unstuck_time = w->time;
        return unstuck_new(500, hold);
    }
    if (wall_contact(r)) return act_new(ACT_HITWALL, 300, -1.0f);      /* 0x403450 */
    int car = hit_car_thr(r, 8.3333f, 1);                               /* 0x403410 / 0x402ae0 */
    if (car >= 0) {
        AiAction *a = act_new(ACT_HITDRAG, 400, -1.0f);                 /* 0x424560 */
        if (a) a->target = car;
        return a;
    }
    if (r->goal == GOAL_DRAG_WIN && cur != ACT_AVOID && prio <= 300) {
        float t = 99999.0f;
        if (avoid_prio(r, &car, &t) >= 0) {                             /* 0x42ba20 */
            int skill = r->skill < 1 ? 0 : r->skill;
            racer_react(r, car);                                        /* 0x40d5c0 */
            return avoid_new(r, 300, car, *blob_f(w, skill * 0x68 + 0x534), t);
        }
    }
    return act_new(TR_DRIVE, 0, -1.0f);                                 /* goal vt+0x3c 0x426ec0 */
}

/* ---- the start ------------------------------------------------------------ */
/* 0x4023e0: the driver aims 1 m straight ahead (driver vt+0x40) */
static void drag_aim(AiRacer *r)
{
    float h = (float)r->car.heading * U16RAD_F;
    r->drv->tgt[0] = r->car.pos[0] + cosf(h);
    r->drv->tgt[1] = r->car.pos[1] + sinf(h);
}
/* 0x42c0c0's drag race head: the start delay (+0x75c), once a random in
   the skill's [blob +0x548, +0x54c) + 0.001 (every retail skill's is
   negative). From 0 up: held still until the race clock reaches it; below
   0: in the last |delay| seconds before GO the car only revs and points
   straight on (setspeed(100, 0, 0, 1)). 1: the tick ends here. */
static int drag_start(AiRacer *r)
{
    AiWorld *w = r->w;
    if (r->start_delay == 0) {
        int s = r->skill < 1 ? 0 : r->skill;                            /* vt+0x78 */
        float lo = *blob_f(w, s * 0x68 + 0x548), hi = *blob_f(w, s * 0x68 + 0x54c);
        r->start_delay = ai_rand(w, hi - lo) + lo + 0.001f;             /* 0x78435c */
    }
    if (!(r->start_delay < 0)) {
        float clock = w->race_start < 0 ? -1.0f : w->time - w->race_start;
        if (clock < r->start_delay) { driver_setspeed(r->drv, 0, 0, 0, 0); drag_aim(r); return 1; }
    } else if (w->go_in < -r->start_delay && w->race_state != 4) {      /* 0x5ef2e0 */
        driver_setspeed(r->drv, 100.0f, 0, 0, 1);
        drag_aim(r);
        return 1;
    }
    return 0;
}

/* ---- the DragController (type 2) ------------------------------------------ */
/* 0x576790 on 0x81e370: the lane change's rate by speed (mph): 0.75 at 0,
   0.25 from 100 */
static float drag_rate(const AiRacer *r)
{
    float mph = r->car.absv * 2.237f;                                   /* car +0x430, 0x78426c */
    if (mph <= 0) return 0.75f;
    if (100.0f <= mph) return 0.25f;
    return (0.25f - 0.75f) * (mph / 100.0f) + 0.75f;
}
/* 0x41d720: from the lane nearest the car (2 at the least) move the lane
   plan up to |n| lanes (- left, + right) while 0x412500 allows each step at
   the car's list point; 1 when the lane changed */
static int drag_lane_move(AiRacer *r, int n)
{
    const AiNet *net = r->w->net;
    if (!r->rpt || r->L.n == 0) return 0;
    int near = lane_near_ex(r, r->rpt, r->car.pos, 0xff, 0xff);
    lane_set3(&r->ls, near < 3 ? 2 : near, 0, drag_rate(r), r->w->time);
    int i = fl_closest(net, &r->L, r->car.pos, 1, near, 0, 0);         /* 0x4198c0 */
    const FPoint *f = i >= 0 && i < r->L.n ? r->L.p[i] : NULL;
    int cur = near;
    if (r->dctl) {
        g_drag_why[0] = 0;
        if (!f) snprintf(g_drag_why, sizeof g_drag_why, "no list point (L %d)", r->L.n);
    }
    if (n < 0) { while (n < 0 && f && lane_step_ok(r, f, cur, cur - 1)) { cur--; n++; } }
    else { while (0 < n && f && lane_step_ok(r, f, cur, cur + 1)) { cur++; n--; } }
    lane_set3(&r->ls, cur, 0, drag_rate(r), r->w->time);
    if (r->dctl) {
        r->dl_near = near; r->dl_got = cur;
        r->dl_fsec = f ? f->sec : -1; r->dl_fpt = f ? f->pt : -1;
    }
    return cur != near;
}
/* 0x40fde0: back to the lane nearest the car outside the ones left behind
   (all of them again when that finds none) */
static void drag_lane_back(AiRacer *r)
{
    if (!r->rpt) return;
    int lane = lane_near_ex(r, r->rpt, r->car.pos, r->dl_lo, r->dl_hi);
    if (lane == -1) {
        r->dl_lo = r->dl_hi = 0xff;
        r->dl_moved = 0;
        lane = lane_near_ex(r, r->rpt, r->car.pos, 0xff, 0xff);
    }
    lane_set3(&r->ls, lane, 0, drag_rate(r), r->w->time);
}
/* the DragController's tick 0x42df10 (before the racer tick 0x42c0c0): the
   lanes left behind are forgotten once the car is within 0.5 m of a lane
   outside them; a held button keeps moving the lane plan as far as it may
   go (the lanes it leaves are remembered on the first move), with none the
   plan goes back to the nearest lane not left behind */
static void drag_ctl_tick(AiRacer *r)
{
    const AiNet *n = r->w->net;
    if ((r->dl_lo != 0xff || r->dl_hi != 0xff) && r->rpt && r->L.n != 0) {
        int cur = lane_near_ex(r, r->rpt, r->car.pos, 0xff, 0xff);
        if (cur < r->dl_lo || r->dl_hi < cur) {
            float pp = 0, lat = 0;
            fl_locate(n, &r->L, r->car.pos, &pp, &lat, cur);             /* 0x419bc0 */
            if (fabsf(lat) < 0.5f) { r->dl_lo = r->dl_hi = 0xff; r->dl_moved = 0; }   /* 0x783824 */
        }
    }
    if (r->dl_cmd == 0 || r->dl_cmd == 1) {
        int moved = drag_lane_move(r, r->dl_cmd == 0 ? -4 : 4);
        if (r->dl_moved || !moved) return;
        int cur = lane_near_ex(r, r->rpt, r->car.pos, 0xff, 0xff);
        if (r->dl_cmd == 0) { r->dl_lo = cur; r->dl_hi = 5; }
        else { r->dl_lo = 2; r->dl_hi = cur; }
        r->dl_x7c4 = 1;
        r->dl_moved = 1;
        r->dl_prog = r->progress;                                       /* car +0x8fc */
    } else if (r->dl_cmd == 2) drag_lane_back(r);
}

/* ---- the public side ------------------------------------------------------ */
/* the human's car in a drag race: a DragController (0x4299c0) on it, owner
   kind 1, skill -2 (racer info +0x901), no button held (+0x7b4 = 2) */
AiRacer *airacer_new_drag_player(AiWorld *w, int slot, const N2Sim *sim, int suv, float half_wid)
{
    if (slot >= 0 && slot < 24 && w->seen[slot].racer) {
        w->seen[slot].racer = NULL; w->seen[slot].v = &w->view[slot];
    }
    AiRacer *r = airacer_new(w, slot, -2, sim, suv, half_wid);
    if (!r) return NULL;
    w->nai--;                                                           /* not in the AI manager's think list */
    r->slot = -1;
    r->owner_kind = 1;
    r->car_player = 1;
    r->carno = 0;
    r->id = 0;
    r->dctl = 1;
    r->car.ctrl_type = 2;
    r->dl_cmd = 2;
    r->dl_lo = r->dl_hi = 0xff;
    ring_init(&r->pace, 10);
    return r;
}
/* the lane-change buttons (0x4102b0: JOY_EVENT_DRAG_RACE_CHANGE_LANE_LEFT /
   _RIGHT 0xb8 / 0xb9, their analog twins 0xba / 0xbb): each side acts on a
   change only; a press holds that way (+0x7b4 0 / 1) and forgets the lanes
   left behind, a release asks for a new plan (+0x774) and lets go (2) */
void airacer_drag_lane(AiRacer *r, int right, int pressed)
{
    if (!r || !r->dctl) return;
    char *edge = &r->dl_edge[right ? 0 : 1];                            /* +0x790 / +0x791 */
    char p = pressed != 0;
    if (*edge == p) return;
    *edge = p;
    if (p) {
        int cmd = right ? 1 : 0;
        if (r->dl_cmd == cmd) return;
        r->dl_cmd = cmd;
        r->dl_lo = r->dl_hi = 0xff;
        r->dl_moved = 0;
    } else {
        r->force_replan = 1;
        r->dl_cmd = 2;
        r->dl_moved = 0;
    }
}
int airacer_drag_lane_now(const AiRacer *r) { return r ? r->ls.lane : -1; }
void airacer_drag_why(const AiRacer *r, char *buf, int cap)
{
    if (!r) { snprintf(buf, cap, "-"); return; }
    snprintf(buf, cap, "near lane %d -> plan %d at list sec %d pt %d, prog %.0f, cmd %d excl %d..%d: %s",
             r->dl_near, r->dl_got, r->dl_fsec, r->dl_fpt, r->progress, r->dl_cmd,
             r->dl_lo, r->dl_hi, g_drag_why[0] ? g_drag_why : "ok");
}
void airacer_track_str(const AiRacer *r, char *buf, int cap)
{
    if (!r) { snprintf(buf, cap, "-"); return; }
    const AiNet *n = r->w->net;
    Pt *p = r->rpt;
    int sec = p && sec_of(n, p) ? (int)(pt_flags(p) & 0x7ff) : -1;
    snprintf(buf, cap, "rpt %d/%d (%.0f %.0f) rdir %d L %d [%.0f..%.0f] cur %d plan %d off %.2f",
             sec, p ? pt_index(n, p) : -1, p ? pt_x(p) : 0.0f, p ? pt_y(p) : 0.0f, r->rdir,
             r->L.n, r->L.n ? r->L.p[0]->dist : 0.0f, r->L.n ? r->L.p[r->L.n - 1]->dist : 0.0f,
             (int)r->L.cur, r->dplan.n, r->ls.off);
}
void airacer_contacts(const AiRacer *r, int *walls, int *cars, int *car_id)
{
    *walls = *cars = 0; *car_id = -1;
    if (!r) return;
    for (int i = 0; i < r->ncontact; i++) {
        if (r->contact[i].wall) ++*walls;
        if (r->contact[i].car) { if (*car_id < 0) *car_id = r->contact[i].other; ++*cars; }
    }
}
void aiworld_set_go_in(AiWorld *w, float secs) { w->go_in = secs; }
