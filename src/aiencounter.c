/* aiencounter.c -- free roam's random encounter racers and the Outrun
 * (included at the end of airacer.c; RE: E:/re-tools/out/ENCOUNTER.md).
 *
 * RaceStarter::AddRandomEncounterCars (0x53f000) puts three AI racers in
 * free roam's field (ids 100..102), Deactivated at once. The
 * RandomEncounterManager (Race +0xc40, Update 0x42edf0, mode 0 or 7)
 * places one of them 250..500 m ahead of the player every 5 s or more,
 * has it cruise 10 m ahead of the player on the player's lane (goal 9
 * OffsetPursuit), dodges 5 m aside when the player closes in, offers the
 * challenge when the player sits right behind it (0x40c030), and takes the
 * cars away again (30 s old and 250 m off, or poofed). The challenge itself
 * (mode 7) is the caller's: aienc_begin / aienc_end around it. */

/* the RandomEncounterState (0x85ef70): +0 state (0 idle, 1 can trigger,
   2 a car can be engaged, 3 none), the timer. Outside career its
   conditions (0x513d30) always pass and Reset (0x500ca0) has no cooldown */
static void res_tick(AiWorld *w)                                        /* 0x513db0 */
{
    if (w->mode != 0) return;
    if (w->rem.res == 0) w->rem.res = 1;                                /* 0x513d30: type 7 */
}
static float enc_now(const AiWorld *w) { return w->time; }
/* 0x5f6d50 / 0x5f6e20: in the world or out of it (z -123456) */
static int enc_active(const AiRacer *r) { return r && !r->off; }
static void enc_deactivate(AiRacer *r)
{
    if (!r || r->off) return;
    r->off = 1;
    r->hilite = 0;
    r->poof = 0;
    if (r->act) { act_end(r, r->act); act_free(r->act); r->act = NULL; }
    for (int i = 0; i < r->w->nseen; i++)
        if (r->w->seen[i].racer == r) r->w->seen[i].active = 0;
}
/* the human players' cars */
static int enc_humans(const AiWorld *w, const AiSeen **out, int max)
{
    int n = 0;
    for (int i = 0; i < w->nseen && n < max; i++)
        if (w->seen[i].v && w->seen[i].kind == 1 && w->seen[i].active) out[n++] = &w->seen[i];
    return n;
}
/* 0x5f0e30: the 3-D distance to the closest human player (99999 none) */
static float enc_closest_human(const AiWorld *w, const float *q)
{
    float m = 99999.0f;
    for (int i = 0; i < w->nseen; i++) {
        const AiSeen *o = &w->seen[i];
        if (!o->v || o->kind != 1 || !o->active) continue;
        float dx = o->v->pos[0] - q[0], dy = o->v->pos[1] - q[1], dz = o->v->pos[2] - q[2];
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < m) m = d;
    }
    return m;
}
/* 0x410fc0 TooCloseToAnotherCar(pos, rad, rad_ai, self): any car of the
   AI manager's list but self, not poofed, not switched off (ctrl +0x77c),
   within rad_ai (an AI racer, racer info +4 == 2) or rad (2-D). An
   encounter car out of the world is not counted (its x/y there unknown) */
static int enc_tooclose(const AiWorld *w, const float *q, float rad, float rad_ai, const AiRacer *self)
{
    for (int i = 0; i < w->nseen; i++) {
        const AiSeen *o = &w->seen[i];
        if (!o->v || (self && o->racer == self)) continue;
        if (o->racer && (o->racer->at_end || o->racer->poof || o->racer->off)) continue;
        if (!o->active && o->kind != 1) continue;
        float r = o->kind == 2 ? rad_ai : rad;
        float dx = q[0] - o->v->pos[0], dy = q[1] - o->v->pos[1];
        if (sqrtf(dx * dx + dy * dy) < r) return 1;
    }
    return 0;
}
/* 0x428630 SetOffsetPursuit(car, target, long, lat, time): goal 9 with its
   target, offsets and timer (a new goal unless it has one), the action's
   copy of them kept current, then the lanes (0x428490) */
/* 0x402070 DeleteCurrentGoal: the old goal's CleanUp (ChaseCar 0x40db50:
   its sticky pathfinding decisions go), its action ended; the new goal */
static void enc_goal(AiRacer *r, int goal)
{
    if (r->goal == GOAL_CHASE)
        for (int i = 0; i < 16; i++) if (r->linkmem[i] & 4) r->linkmem[i] &= ~7u;   /* 0x404970 */
    if (r->act) { act_end(r, r->act); act_free(r->act); r->act = NULL; }
    r->goal = goal;
    r->dam_mode = 0;
}
static void enc_pursue(AiRacer *r, int target, float lon, float lat, float t)
{
    if (r->goal != GOAL_PURSUIT) enc_goal(r, GOAL_PURSUIT);            /* 0x42aa20(9) */
    else if (r->act && r->act->type == ACT_DRIVEOFFSET) {
        r->act->target = target; r->act->t214 = lon; r->act->t218 = lat;
    }
    r->op_target = target; r->op_long = lon; r->op_lat = lat;
    r->op_until = enc_now(r->w) + t;
    goal_lanes(r);                                                      /* 0x428490 */
}
/* 0x4285a0 ChaseCar / 0x428510 GetAwayFromCar: the same goal keeps its
   action, only the target changes */
static void enc_chase(AiRacer *r, int target)
{
    if (r->goal != GOAL_CHASE) enc_goal(r, GOAL_CHASE);
    r->ch_target = target;
    goal_lanes(r);
}
static void enc_getaway(AiRacer *r, int target)
{
    if (r->goal != GOAL_GETAWAY) enc_goal(r, GOAL_GETAWAY);
    r->ch_target = target;
    goal_lanes(r);
}
/* 0x5fbed0 ResetToRoutePoint + controller restart (0x40d550): on the
   point's line facing `dir`, at the point's speed for that way (+0x2c /
   +0x2d) */
static void enc_place(AiRacer *r, Pt *p, int dir)
{
    const AiNet *n = r->w->net;
    float d[2] = { pt_dx(p), pt_dy(p) }, pos[3];
    if (!dir) pt_backdir(n, p, d);
    pt_pos3(n, p, 0, 0, 0, pos);
    r->off = 0; r->poof = 0; r->hilite = 0;
    r->rpt = NULL;
    ctrl_setpoint(r, p);
    r->rdir = dir;                                                      /* ctrl +0x10 */
    r->think_left = (float)r->index * 0.1f * -0.0454545f;
    for (int i = 0; i < 16; i++) r->linkmem[i] &= ~3u;
    r->finished = 0; r->at_end = 0;
    r->drv->cmd = 1;
    fl_reset(&r->L);
    r->reset_time = r->w->time;
    r->want_reset = 0;
    tr_pose(r, pos, atan2f(d[1], d[0]), (float)p[0x2c + (dir == 0)], rdf(p + 8));
}
/* 0x42a5e0 TryToPlaceInactiveCars: at most one car every 5 s, the first
   one out of the world, 250..500 m on along the player's road (50 m
   steps), 250 m from every human, on a point flagged 0x800, clear of cars
   (30 m, AI racers 300 m); then it pursues the player 10 m ahead */
static void enc_try_place(AiWorld *w)
{
    AiRem *m = &w->rem;
    float newest = 0;
    for (int k = 0; k < m->n; k++) if (newest < m->s[k].time) newest = m->s[k].time;
    if (!(5.0f < enc_now(w) - newest)) return;                          /* 0x783d38 */
    const AiSeen *hum[4];
    if (enc_humans(w, hum, 4) < 1) return;
    const AiRacer *pr = hum[0]->racer;
    for (int k = 0; k < m->n; k++) {
        AiRacer *car = m->s[k].car;
        if (!car || enc_active(car)) continue;
        Pt *hp = pr ? pr->rpt : NULL;                                   /* 0x5ec0a0 / 0x5ec0b0 */
        if (!hp) continue;
        int dir = pr->rdir != 0;
        TrPos info;
        int res = tr_walk(w, &info, 1, hp, 250.0f, dir, 1);             /* 0x415130 */
        float dist = 250.0f;
        Pt *prev = NULL;
        while (res == 1 && info.pt != prev && dist < 500.0f) {          /* 0x783d10 */
            float q[3] = { pt_x(info.pt), pt_y(info.pt), pt_z(info.pt) };
            if (250.0f <= enc_closest_human(w, q) && (pt_flags(info.pt) & 0x800) &&
                !enc_tooclose(w, q, 30.0f, 300.0f, car)) {              /* 0x783d14 / 0x783d18 */
                enc_place(car, info.pt, info.dir);
                enc_pursue(car, hum[0]->index, 10.0f, 0.0f, 7.0f);     /* 0x783d28 / 0x783d30 */
                m->s[k].time = enc_now(w); m->s[k].state = 1; m->s[k].sel = 0;
                printf("encounter: car %d placed %.0f m on at (%.0f %.0f), %.1f m/s%c", car->id, dist,
                       q[0], q[1], car->place_speed, 10);
                return;
            }
            prev = info.pt;
            res = tr_walk(w, &info, 1, info.pt, 50.0f, info.dir, 1);    /* 0x783d0c */
            dist += 50.0f;
        }
    }
}
/* 0x41c100: where `me` stands from `him` -- a along his way (D), b across
   it (L). The port has no nearby-avoidables list: both from his road
   point's line (0x40cbc0 for D) */
static void enc_rel(const AiWorld *w, const AiRacer *me, const AiSeen *him, float *a, float *b)
{
    const float *hp = him->v->pos, *mp = me->car.pos;
    float f[2] = { him->v->fwd[0], him->v->fwd[1] };
    const AiRacer *hr = him->racer;
    if (hr && hr->rpt) {
        f[0] = pt_dx(hr->rpt); f[1] = pt_dy(hr->rpt);
        if (!hr->rdir) pt_backdir(w->net, hr->rpt, f);
    }
    float dx = mp[0] - hp[0], dy = mp[1] - hp[1];
    *a = dx * f[0] + dy * f[1];
    *b = -(dx * f[1] - dy * f[0]);                                      /* left of him positive */
}
/* 0x42a860 MakeOpponentsDriveNearPlayer: 10 m ahead of the player; within
   30 m along the road 5 m off to the side he is not on (2 m hysteresis),
   back on his line from 20 m */
static void enc_drive_near(AiWorld *w)
{
    AiRem *m = &w->rem;
    const AiSeen *hum[4];
    if (enc_humans(w, hum, 4) < 1) return;
    for (int k = 0; k < m->n; k++) {
        AiRacer *car = m->s[k].car;
        if (!enc_active(car)) continue;
        float a, b, off = 0;
        enc_rel(w, car, hum[0], &a, &b);
        switch (m->s[k].state) {
        case 1:
            if (30.0f <= fabsf(a)) off = 0;                             /* 0x783d20 + 0x783d1c */
            else if (0 <= b) { m->s[k].state = 3; off = 5.0f; }         /* 0x783d2c */
            else { m->s[k].state = 2; off = -5.0f; }
            break;
        case 2:
            if (20.0f <= fabsf(a)) { m->s[k].state = 1; off = 0; }      /* 0x783d1c */
            else if (2.0f < b) { m->s[k].state = 3; off = 5.0f; }       /* 0x783d24 */
            else off = -5.0f;
            break;
        case 3:
            if (20.0f <= fabsf(a)) { m->s[k].state = 1; off = 0; }
            else if (b < -2.0f) { m->s[k].state = 2; off = -5.0f; }
            else off = 5.0f;
            break;
        }
        enc_pursue(car, hum[0]->index, 10.0f, off, 7.0f - (enc_now(w) - m->s[k].time));
    }
}
/* 0x40bee0 TryAndDeactivateOldOpponents: over 30 s old and 250 m from
   every human */
static void enc_drop_old(AiWorld *w)
{
    AiRem *m = &w->rem;
    for (int k = 0; k < m->n; k++) {
        AiRacer *car = m->s[k].car;
        if (!enc_active(car)) continue;
        if (30.0f < enc_now(w) - m->s[k].time && 250.0f <= enc_closest_human(w, car->car.pos)) {   /* 0x783d34 */
            m->s[k].sel = 0;
            enc_deactivate(car);
            printf("encounter: car %d taken away (old, %.0f m off)%c", car->id, enc_closest_human(w, car->car.pos), 10);
        }
    }
}
/* 0x40bf60 TryAndDeactivateLost: poofed and its blink over */
static void enc_drop_lost(AiWorld *w)
{
    AiRem *m = &w->rem;
    for (int k = 0; k < m->n; k++) {
        AiRacer *car = m->s[k].car;
        if (enc_active(car) && car->poof && car->poof_t + car->poof_dur <= enc_now(w)) enc_deactivate(car);
    }
}
/* 0x40bfb0 DeactivateAllActive: every AI racer not poofed and 250 m from
   every human goes; the count of those left */
static int enc_drop_all(AiWorld *w)
{
    AiRem *m = &w->rem;
    int left = 0;
    for (int k = 0; k < m->n; k++) {
        AiRacer *car = m->s[k].car;
        if (!enc_active(car)) continue;
        if (!car->poof && 250.0f <= enc_closest_human(w, car->car.pos)) enc_deactivate(car);
        else left++;
    }
    return left;
}
/* 0x40c030 GetCarThatCanBeEngaged: an encounter car the player sits right
   behind -- the player behind it by its heading, within 30 m (2-D), 5 m of
   height, the car at 30 mph or more, the speeds within 10 mph, the
   headings within 10 degrees (the wider "selected" set is never used: the
   triple pass clears the flag first) */
static AiRacer *enc_candidate(AiWorld *w, const AiSeen *h)
{
    AiRem *m = &w->rem;
    const AiCarView *pv = h->v;
    float pd[3];
    if (1.0f <= pv->absv) { for (int i = 0; i < 3; i++) pd[i] = pv->vel[i] / pv->absv; }   /* 0x784250 */
    else memcpy(pd, pv->fwd, sizeof pd);
    AiRacer *cand = NULL;
    for (int k = 0; k < m->n; k++) {
        AiRacer *car = m->s[k].car;
        m->s[k].sel = 0;
        if (car) car->hilite = 0;                                       /* SetSpecialFX(car, 0) */
        if (!enc_active(car) || car->poof || cand) continue;
        const AiCarView *c = &car->car;
        float r[3] = { c->pos[0] - pv->pos[0], c->pos[1] - pv->pos[1], c->pos[2] - pv->pos[2] };
        if (!(0 < r[0] * c->fwd[0] + r[1] * c->fwd[1] + r[2] * c->fwd[2])) continue;
        float d = sqrtf(r[0] * r[0] + r[1] * r[1]);
        if (30.0f < d || 5.0f < fabsf(r[2])) continue;
        float mph = c->absv * 2.237f, rel = (c->absv - pv->absv) * 2.237f;   /* 0x78426c */
        if (mph < 30.0f || 10.0f < fabsf(rel)) continue;
        float dot = pd[0] * c->fwd[0] + pd[1] * c->fwd[1] + pd[2] * c->fwd[2];
        if (dot > 1.0f) dot = 1.0f; else if (dot < -1.0f) dot = -1.0f;
        if (!(acosf(dot) * (180.0f / 3.14159265f) < 10.0f)) continue;
        cand = car;
        m->s[k].sel = 1;
    }
    return cand;
}
/* 0x413030 TryToEngagePlayer: the candidate highlighted (car FX +0x208c)
   and the HUD's event 9 (0x500d20: RES state 2 / 3) */
static void enc_try_engage(AiWorld *w)
{
    AiRem *m = &w->rem;
    const AiSeen *hum[4];
    if (enc_humans(w, hum, 4) < 1) { m->cand = NULL; m->res = m->res == 2 ? 3 : m->res; return; }
    AiRacer *c = enc_candidate(w, hum[0]);
    {   /* test only (sim scripts): OPENUG2_TEST_ENGAGE takes the nearest placed
           car as the challenger, the autodrive never sits behind one */
        static int test = -1;
        if (test < 0) test = getenv("OPENUG2_TEST_ENGAGE") ? 1 : 0;
        float best = 250.0f;
        for (int k = 0; test && !c && k < m->n; k++) {
            AiRacer *car = m->s[k].car;
            if (!enc_active(car) || car->poof) continue;
            float d = enc_closest_human(w, car->car.pos);
            if (d < best) { best = d; c = car; }
        }
    }
    if (c && m->res != 0) {                                             /* 0x513e00: no WorldChallenge */
        c->hilite = 1;
        m->res = 2;
    } else if (m->res == 2 || m->res == 1) m->res = 3;
    if (c != m->cand && (c || m->res == 2 || m->res == 3)) {
        if (c) printf("encounter: car %d can be challenged (%.0f m/s, the player %.0f m/s)%c", c->id, c->car.absv, hum[0]->v->absv, 10);
        else printf("encounter: no challenger%c", 10);
    }
    m->cand = c;                                                        /* player +0x250 */
    if (!c && m->res == 3) m->res = 1;
}
/* 0x42a9e0 / 0x4285a0: in the Outrun, the leader gets away from the
   second, the others chase the leader */
static void enc_outrun_goals(AiWorld *w)
{
    AiRem *m = &w->rem;
    if (m->norder < 2) return;
    const AiSeen *ls = seen_car(w, m->order[0]);
    AiRacer *lead = ls ? ls->racer : NULL;
    if (lead && enc_active(lead) && !lead->poof) enc_getaway(lead, m->order[1]);
    for (int i = 1; i < m->norder; i++) {
        const AiSeen *s = seen_car(w, m->order[i]);
        AiRacer *c = s ? s->racer : NULL;
        if (c && enc_active(c) && !c->poof) enc_chase(c, m->order[0]);
    }
}
static void outrun_start(AiWorld *w);
static void outrun_tick(AiWorld *w);
static void outrun_check_end(AiWorld *w);
/* 0x42edf0 RandomEncounterManager::Update (Race::Update, mode 0 or 7) */
/* every 5 s: where the placed cars are and what they do */
static void enc_log(AiWorld *w)
{
    static float next;
    float now = enc_now(w);
    if (now < next && now + 10.0f > next) return;
    next = now + 5.0f;
    AiRem *m = &w->rem;
    for (int k = 0; k < m->n; k++) {
        AiRacer *r = m->s[k].car;
        if (!r || r->off) continue;
        printf("encounter: car %d %.0f m from the player, %.1f m/s, goal %d act %d mode %d, lane state %d%s%c", r->id,
               enc_closest_human(w, r->car.pos), r->car.absv, r->goal, r->act ? r->act->type : -1,
               r->act ? (int)r->act->t21c : -1, m->s[k].state, r->hilite ? ", highlighted" : "", 10);
    }
}
void aienc_frame(AiWorld *w)
{
    AiRem *m = &w->rem;
    if (m->n < 1 || (w->mode != 0 && w->mode != 7)) return;
    if (m->request) { m->request = 0; outrun_start(w); }               /* Race::Update: +0xc30 -> 0x607180 */
    if (w->mode == 7) outrun_tick(w);
    res_tick(w);
    switch (m->state) {
    case 0:                                                             /* 0x412f90 Init */
        for (int k = 0; k < m->n; k++) { m->s[k].time = 0; m->s[k].state = 0; m->s[k].sel = 0; }
        m->res = 0;
        m->state = 1;
        break;
    case 1:                                                             /* 0x41ace0 */
        if (m->res == 1) m->state = 3;
        break;
    case 3:
        enc_try_place(w);
        enc_try_engage(w);
        enc_drive_near(w);
        enc_drop_old(w);
        enc_log(w);
        break;
    case 4:
        if (w->mode == 7) enc_outrun_goals(w);
        enc_drop_lost(w);
        break;
    case 5:
        enc_drop_lost(w);
        if (enc_drop_all(w) == 0) { m->res = 0; m->state = 0; }        /* 0x500ca0 Reset */
        break;
    }
    if (w->mode == 7) outrun_check_end(w);
}
void aienc_add(AiWorld *w, AiRacer *r)
{
    AiRem *m = &w->rem;
    if (!r || m->n >= 3) return;
    m->s[m->n].car = r;
    m->s[m->n].time = 0; m->s[m->n].state = 0; m->s[m->n].sel = 0;
    m->n++;
    r->off = 0;
    enc_deactivate(r);                                                  /* mode 0: out of the world at once */
}
int airacer_off(const AiRacer *r) { return !r || r->off; }
int airacer_hilite(const AiRacer *r) { return r && r->hilite; }
int airacer_poofed(const AiRacer *r) { return r && r->poof; }
int airacer_blinking(const AiRacer *r) { return r && r->poof && r->w->time - r->poof_t < r->poof_dur; }   /* 0x5ebff0 */
/* event 9 is on the HUD from TryToEngagePlayer's AddEvent until a press
   (0x4db890(0)) or no candidate */
AiRacer *aienc_candidate(const AiWorld *w) { return w->mode == 0 && w->rem.state == 3 && w->rem.res == 2 ? w->rem.cand : NULL; }

/* ---- the Outrun (race mode 7) ---------------------------------------------- */
/* the racing cars (Race +0x30, RacingCar 0x1f0) by their car index */
static AiSeen *rc_seen(AiWorld *w, int i) { return seen_car(w, w->rem.rc[i].car); }
/* 0x606cb0 FinishCar(i, place, isAI, reason) in mode 7: the place and the
   reason (6 won, 7 lost); a human only finished (SetFinishedRacing: the
   player keeps driving); a type 3 controller Parks (0x42b330, goal 11); an
   AI car poofs, blinking 2 s (+0x974 / +0x978 0x7a55c0, +0x998) */
static void outrun_finish(AiWorld *w, int i, int place, int reason)
{
    AiRem *m = &w->rem;
    if (m->rc[i].place) return;
    m->rc[i].place = place; m->rc[i].reason = reason;
    /* reasons 4 / 6 / 7: +0x198 = now - the race's start, snapped to
       1/100 s (0x577400(0.01): 40 x round(t / 40) in 1/4000 s) */
    int t = (int)((enc_now(w) - m->rc[i].t0) * 4000.0f + 0.5f);
    m->rc[i].ftime = (t + 20) / 40 * 40;
    m->nfin++;                                                          /* Race +0x28 */
    AiSeen *s = rc_seen(w, i);
    AiRacer *r = s ? s->racer : NULL;
    if (m->rc[i].human) {
        m->fin_t = enc_now(w);
        printf("outrun: player %s (place %d, %.1f s)%c", reason == 6 ? "won" : "lost", place, enc_now(w) - m->rc[i].t0, 10);
        return;
    }
    if (r) {
        if (!r->off) printf("outrun: car %d %s (place %d)%c", r->id, reason == 6 ? "won" : "lost", place, 10);
        airacer_park(r);
        r->poof = 1; r->poof_t = enc_now(w); r->poof_dur = 2.0f;
    }
}
/* 0x5fc510 UpdateGetAway: the straight line to the leader, or along the
   leader's road (0x40cbc0 of both on its point, its way) when on that
   road within 30 m and going its way; the leader slow (10 m/s or less):
   behind its point is a lead (minus the distance) */
static void outrun_gap(AiWorld *w, int i, int lead)
{
    AiRem *m = &w->rem;
    if (m->rc[i].place) return;
    if (i == lead) { m->rc[i].gap = 0; return; }
    const AiSeen *me = rc_seen(w, i), *ld = rc_seen(w, lead);
    if (!me || !ld) return;
    const AiCarView *a = me->v, *b = ld->v;
    float gap = d3(a->pos, b->pos);                                     /* 0x43ce10 */
    Pt *P = ld->racer ? ld->racer->rpt : NULL;                          /* ctrl +0xc */
    if ((10.0f < a->absv || b->absv <= 10.0f) && P) {                   /* 0x7a56a8 */
        float pp[3] = { pt_x(P), pt_y(P), pt_z(P) };                    /* 0x401a00 */
        if (d3(a->pos, pp) < 30.0f) {                                   /* 0x7a56b0 */
            if (10.0f < b->absv) {
                int back = !ld->racer->rdir;
                float v[2] = { pt_dx(P), pt_dy(P) };                    /* 0x401b20 */
                if (back) pt_backdir(w->net, P, v);
                if (0 < v[0] * a->vel[0] + v[1] * a->vel[1])
                    gap = pt_along(w->net, P, b->pos, back) - pt_along(w->net, P, a->pos, back);
            } else {
                float d[3] = { pp[0] - a->pos[0], pp[1] - a->pos[1], pp[2] - a->pos[2] };   /* 0x401250 */
                if (d[0] * a->vel[0] + d[1] * a->vel[1] + d[2] * a->vel[2] < 0) gap = -d3(a->pos, pp);
            }
        }
    }
    m->rc[i].gap = gap;
}
/* 0x5eeea0 GetGetAwayDistance: 300 m, 304.9 with the units flag 0x83aa1a != 1 */
static float outrun_limit(const AiWorld *w) { return 300.0f + (w->rem.kmh ? 0.0f : 4.9f); }   /* 0x7a56ac / 0x7a5f48 */
/* Race::Update in mode 7: each car's gap, SortRacingCarsByLeadership
   (0x5eec80: the unfinished by gap, then the finished by place), and the
   single player rule -- the gaps less the leader's, over the limit is a
   loss (reason 7) at the last place left; one car left wins (6) */
static void outrun_tick(AiWorld *w)
{
    AiRem *m = &w->rem;
    if (m->nrc < 1) return;
    int lead = m->srt[0];
    for (int i = 0; i < m->nrc; i++) outrun_gap(w, i, lead);
    for (int a = 1; a < m->nrc; a++) {
        int x = m->srt[a], b = a;
        for (; 0 < b; b--) {
            int y = m->srt[b - 1];
            const AiRc *X = &m->rc[x], *Y = &m->rc[y];
            int before = !X->place ? (Y->place || X->gap < Y->gap) : (Y->place && X->place < Y->place);
            if (!before) break;
            m->srt[b] = y;
        }
        m->srt[b] = x;
    }
    int left = m->nrc - m->nfin;
    float lg = 0;
    int l0 = m->srt[0];
    if (!m->rc[l0].place) { lg = m->rc[l0].gap; m->rc[l0].gap = 0; }
    float lim = outrun_limit(w);
    for (int q = 1; q < m->nrc; q++) {
        int i = m->srt[q];
        if (m->rc[i].place) continue;
        m->rc[i].gap -= lg;
        if (lim < m->rc[i].gap) { outrun_finish(w, i, left, 7); left--; }
    }
    if (left < 2) outrun_finish(w, l0, 1, 6);
    m->norder = m->nrc;
    for (int q = 0; q < m->nrc; q++) m->order[q] = m->rc[m->srt[q]].car;
}
/* 0x607180 StartGetAway: mode 7, the racing cars the AI racers then the
   humans (RacingCar::Init: +0x30 the start, the gap 0), the REM's
   BeginRace (0x40be80: state 4); every AI racer but the challenged one
   finished at once (reason 7, places from humans + 2), Parked and poofed */
static void outrun_start(AiWorld *w)
{
    AiRem *m = &w->rem;
    const AiSeen *hum[4];
    int nh = enc_humans(w, hum, 4);
    AiRacer *cand = m->cand;
    if (nh < 1 || !cand) return;
    w->mode = 7;                                                        /* 0x89e7b0; laps 0 */
    m->nrc = 0; m->nfin = 0; m->fin_t = -1;
    for (int k = 0; k < m->n && m->nrc < 4; k++) {
        int si = m->s[k].car ? racer_own_seen(m->s[k].car) : -1;
        if (si < 0) continue;
        AiRc *c = &m->rc[m->nrc++];
        memset(c, 0, sizeof *c);
        c->car = w->seen[si].index; c->t0 = enc_now(w);
    }
    for (int h = 0; h < nh && m->nrc < 4; h++) {
        AiRc *c = &m->rc[m->nrc++];
        memset(c, 0, sizeof *c);
        c->car = hum[h]->index; c->human = 1; c->t0 = enc_now(w);
    }
    for (int i = 0; i < m->nrc; i++) m->srt[i] = i;
    m->state = 4;                                                       /* 0x40be80 */
    int place = nh + 2;
    for (int i = 0; i < m->nrc; i++) {
        AiSeen *s = rc_seen(w, i);
        if (m->rc[i].human || (s && s->racer == cand)) continue;
        outrun_finish(w, i, place++, 7);
    }
    m->norder = m->nrc;
    for (int q = 0; q < m->nrc; q++) m->order[q] = m->rc[m->srt[q]].car;
    printf("outrun: started against car %d (%.0f m/s), limit %.1f m%c", cand->id, cand->car.absv, outrun_limit(w), 10);
}
/* 0x5f8490 EndGetAway (single player, from 0x60aaa0 once every human is
   finished): mode 0; the result for the caller (career: the purse,
   0x541320); the racing cars back to the humans, player +0x250 cleared;
   the REM's EndRace (0x42efb0): every active AI racer not on goal 1 gets
   it (0x42cd60), state 5 */
static void outrun_end(AiWorld *w)
{
    AiRem *m = &w->rem;
    w->mode = 0;
    for (int i = 0; i < m->nrc; i++)
        if (m->rc[i].human) { m->result = m->rc[i].reason; m->nresult++; }
    m->nrc = 0; m->nfin = 0; m->norder = 0;
    m->cand = NULL;
    for (int k = 0; k < m->n; k++) {
        AiRacer *r = m->s[k].car;
        if (!enc_active(r) || r->goal == GOAL_ROAD_RACE) continue;
        enc_goal(r, GOAL_ROAD_RACE);
        r->entry_done = 0;
        goal_lanes(r);
    }
    m->state = 5;
    printf("outrun: over, the player %s%c", m->result == 6 ? "won" : "lost", 10);
}
/* 0x60b3a0 RCStateRaceInProgress: every human finished and 3 s on -> 0x60aaa0 */
static void outrun_check_end(AiWorld *w)
{
    AiRem *m = &w->rem;
    int racing = 0;
    for (int i = 0; i < m->nrc; i++) if (m->rc[i].human && !m->rc[i].place) racing = 1;
    if (m->nrc && !racing && 0 <= m->fin_t && 3.0f <= enc_now(w) - m->fin_t) outrun_end(w);
}
/* the race-entry view of a car in the Outrun: +0x24 the gap (race_dist in
   mode 7, for the catch-up 0x4084a0), +0xa its standing */
static void enc_seen_race(AiWorld *w, AiSeen *s)
{
    AiRem *m = &w->rem;
    for (int q = 0; q < m->nrc; q++)
        if (m->rc[m->srt[q]].car == s->index) { s->race_dist = m->rc[m->srt[q]].gap; s->race_pos = q + 1; return; }
}
int aienc_engage(AiWorld *w)                                            /* joy event 0xbf (0x602bd0) */
{
    AiRem *m = &w->rem;
    if (w->mode != 0 || m->state != 3 || !m->cand || m->res != 2) return 0;   /* 0x500d80 */
    m->request = 1;                                                     /* Race +0xc30 */
    return 1;
}
void aienc_units(AiWorld *w, int kmh) { w->rem.kmh = kmh != 0; }
/* GetDistanceFromLeader (0x4c9450) for the human: from the leaderboard's
   gaps to the others (positive: that car ahead), the largest one ahead,
   else the nearest one behind (negative), else minus the limit */
int aienc_outrun(const AiWorld *w, float *dist, float *limit, int *rank)
{
    const AiRem *m = &w->rem;
    if (w->mode != 7 || m->nrc < 1) return 0;
    int me = -1;
    for (int i = 0; i < m->nrc; i++) if (m->rc[i].human) { me = i; break; }
    if (me < 0) return 0;
    float lim = outrun_limit(w), d = -lim;
    for (int i = 0; i < m->nrc; i++) {
        if (i == me) continue;
        float g = m->rc[me].gap - m->rc[i].gap;
        if ((0 < g && d < g) || (g < 0 && d < 0 && d < g)) d = g;
    }
    if (dist) *dist = d;
    if (limit) *limit = lim;
    if (rank) { *rank = 0; for (int q = 0; q < m->nrc; q++) if (m->srt[q] == me) *rank = q + 1; }
    return 1;
}
int aienc_result(const AiWorld *w, int *seq) { if (seq) *seq = w->rem.nresult; return w->rem.result; }
/* Player::RenderGetAwayArrow 0x5f3ae0's gate: mode 7, the player not
   finished (+0x50), a human controller, the player not the leader
   (Race +0xbd0[0]) and more than 60 m behind it (its +0x24 gap,
   0x7a5560); target = the leader car's position (+0x60) */
int aienc_arrow(AiWorld *w, float target[3])
{
    AiRem *m = &w->rem;
    if (w->mode != 7 || m->nrc < 1) return 0;
    int me = -1;
    for (int i = 0; i < m->nrc; i++) if (m->rc[i].human) { me = i; break; }
    int lead = m->srt[0];
    if (me < 0 || m->rc[me].place || lead == me || !(60.0f < m->rc[me].gap)) return 0;
    const AiSeen *ld = rc_seen(w, lead);
    if (!ld) return 0;
    for (int a = 0; a < 3; a++) target[a] = ld->v->pos[a];
    return 1;
}
/* the player finished in the Outrun (entry +0x10 = 6 / 7, still mode 7
   until EndGetAway 3 s on): its reason and +0x198 for the race over
   widget (0x5fff50 single player: shown while racing, stage 4) */
int aienc_over(const AiWorld *w, int *reason, int *ftime)
{
    const AiRem *m = &w->rem;
    if (w->mode != 7) return 0;
    for (int i = 0; i < m->nrc; i++) {
        if (!m->rc[i].human || !m->rc[i].place) continue;
        if (reason) *reason = m->rc[i].reason;
        if (ftime) *ftime = m->rc[i].ftime;
        return 1;
    }
    return 0;
}

/* ---- the goals' driving ---------------------------------------------------- */
static int racer_rank(const AiRacer *r)                                 /* RacingCar +0xa */
{
    int i = racer_own_seen(r);
    return i >= 0 ? r->w->seen[i].race_pos : 0;
}
/* 0x40dc00 OffsetPursuit::GetSpeedLimit: an AI racer 60 mph within 100 m
   of the target (else 40 mph), anyone else 30; with no race route the
   target's speed times the graph 0x81e378 by the distance, if more (the
   race route branch, the +0x24 timer, has no use: goal 9 runs in free
   roam only) */
static float offset_limit(const AiRacer *r)
{
    static const float GX[5] = { 0, 20, 50, 100, 150 }, GY[5] = { 1.1f, 1.0f, 0.8f, 0.5f, 0.1f };
    const AiSeen *t = seen_car(r->w, r->op_target);
    float base = 30.0f;                                                 /* 0x7835bc */
    if (!t || !t->v) return base;
    float dx = t->v->pos[0] - r->car.pos[0], dy = t->v->pos[1] - r->car.pos[1], d = sqrtf(dx * dx + dy * dy);
    if (r->owner_kind == 2) base = d <= 100.0f ? 26.821636f : 17.88109f;   /* 0x784258 / 0x7835c4 / 0x7835c8 */
    float g = GY[4];
    if (d <= GX[0]) g = GY[0];
    else for (int i = 1; i < 5; i++)
        if (d <= GX[i]) { g = GY[i - 1] + (GY[i] - GY[i - 1]) * (d - GX[i - 1]) / (GX[i] - GX[i - 1]); break; }
    float v = g * t->v->absv;
    return base < v ? v : base;
}
/* 0x42bc50 DriveAtOffset::CalculatePointsToDrive (every frame): near the
   target (|relD| under 30 m, out again past 50) the racing line at the
   target's lateral offset plus lat, at the target's speed +-2 m/s to close
   the long gap (an AI racer 60 mph at least); else the best line */
static void driveoffset_begin(AiRacer *r, AiAction *a)
{
    AiWorld *w = r->w;
    const RacerEnt *av = ent_of(r, a->target);                          /* 0x60c0f0 */
    if (av) {
        float relD = -(ent_ahead(r, av) + a->t214);
        if (a->t21c == 0) { if (fabsf(relD) < 30.0f) a->t21c = 1; }    /* 0x7835b4 */
        else if (a->t21c == 1 && 50.0f < fabsf(relD)) a->t21c = 0;      /* 0x7835b8 */
    } else a->t21c = 0;
    if (a->t21c == 1) {
        lane_set3(&r->ls, 0, ent_lat(av, 0) + a->t218, 0.15f, w->time); /* 0x4053b0 (0x783760) */
        float x = 0;                                                    /* 0x419c60 */
        const AiPlan *dp = &r->dplan;
        int c = dp->cur;
        if (0 <= c && c < dp->n) {
            int i = fl_find(&r->L, dp->sec[c], dp->pt[c]);
            if (i != -1 && r->L.p[i]) x = fp_latlane(w->net, r->L.p[i], dp->p[c], 0);
        }
        if (1.5f <= fabsf(x - own_lat(r, 0))) plan_build(r, &r->ls, &a->plan);   /* 0x7e8d94 */
        else {
            float d = r->car.absv * 0.75f;                              /* 0x783764 */
            if (d <= 30.0f) d = 30.0f;                                  /* 0x783768 */
            a->plan = r->dplan;
            plan_keep(&a->plan, &r->car, w->time, d);
            plan_fill(r, &a->plan, 1.0f, r->ls.lane, r->ls.off, r->ls.rate);
        }
        float relD = -(ent_ahead(r, av) + a->t214);
        const AiSeen *t = seen_car(w, a->target);
        float vt = t && t->v ? t->v->absv : 0, s;
        if (fabsf(relD) < 2.0f) s = vt;                                 /* 0x78376c */
        else if (relD < 0) s = vt + 2.0f;                               /* 0x783770 */
        else s = vt - 2.0f;
        if (!(0 < s)) s = 0;
        if (r->owner_kind == 2 && !(26.821636f < s)) s = 26.821636f;
        a->speed = s; a->use_cap = 0;
    } else {
        best_line(r, a);                                                /* 0x42b340 */
        a->speed = 1000.0f; a->use_cap = 1;
    }
    a->x1f4 = 0; a->x1f8 = 2.0f; a->go = 1; a->catchup = 1;
}
/* 0x413f50 DecideSignpostAsGetAwayCar: must-take links taken, closed and
   special ones not; else 0.75 while the second car is within 150 m (2-D),
   0.1 beyond (kind 4 with bit 26: 0.333 / 0.5) */
static int getaway_take_link(AiRacer *r, Lk *l)
{
    AiWorld *w = r->w;
    uint32_t f = lk_flags(l);
    int kind = f & 0xf;
    const unsigned char *ts = lk_sec(w->net, l);
    if (kind == 2) return 1;
    if (kind == 5 || kind == 10) return 0;
    if (ts && (rd32(ts + 0x70) >> 10 & 1)) return 0;
    if (kind == 7 || (f >> 20 & 1)) return 0;
    float p = 0.1f;                                                     /* 0x783550 */
    const AiSeen *s = w->rem.norder > 1 ? seen_car(w, w->rem.order[1]) : NULL;   /* Race bd0[1] */
    if (s && s->v) {
        float dx = s->v->pos[0] - r->car.pos[0], dy = s->v->pos[1] - r->car.pos[1];
        if (sqrtf(dx * dx + dy * dy) < 150.0f) p = 0.75f;               /* 0x783554 / 0x78354c */
    }
    if ((f >> 26 & 1) && kind == 4) p = (f >> 27 & 1) ? 0.333f : 0.5f;
    return ai_rand(w, 1.0f) < p;
}

/* ---- ChaseCar's pathfinding (the Outrun, 0x41e540) ------------------------- */
typedef struct {                /* the search state (0x38 bytes) */
    Pt *start; int sdir;
    unsigned short dec[15];     /* bits 0-12 the link, bit 13 taken */
    int count;
    Pt *pt; float rem; int dir;
} PfState;
/* 0x41b230 PathfindDepthFirst: from `start` along its way to a point whose
   road holds the target (4 m of height), within maxd; branches kept on a
   100-entry ring (straight on explored first), 15 decisions at most */
static int pf_depth(AiWorld *w, PfState *res, Pt *start, const float *tgt, float maxd, int fwd, int racer)
{
    const AiNet *n = w->net;
    PfState q[100], cur;
    int head = 0, tail = 0, full = 0, pops = 0;
    if (!start) return 0;
    memset(&cur, 0, sizeof cur);
    cur.start = start; cur.sdir = fwd; cur.pt = start; cur.dir = fwd;
    cur.rem = maxd + (fwd ? 0 : pt_seglen(start));
#define PF_FRONT(E) do { if (!full) { head = head ? head - 1 : 99; q[head] = (E); full = tail == head; } } while (0)   /* 0x40c7e0 */
#define PF_BACK(E)  do { if (!full) { q[tail] = (E); if (++tail == 100) tail = 0; full = tail == head; } } while (0)   /* 0x40c780 */
    for (int it = 1; it <= n->norder * 0x400; it++) {
        cur.rem -= pt_seglen(cur.pt);
        float lat, al;
        pt_outside2(n, cur.pt, tgt, 0, &lat, &al);                      /* 0x40cd00 / 0x40ce10 */
        if (lat < 0 && al < 0 && fabsf(pt_z(cur.pt) - tgt[2]) < 4.0f) { /* 0x783d44 */
            *res = cur; res->start = start; res->sdir = fwd;
            return 1;
        }
        if (cur.rem < 0) goto pop;
        int pushed = 0, none = 1, nl = pt_nlinks(n, cur.pt);
        for (int k = 0; k < nl; k++) {
            Lk *l = pt_link(n, cur.pt, k);
            uint32_t f = lk_flags(l);
            if ((int)(f >> 18 & 1) == cur.dir) continue;               /* the wrong way */
            none = 0;
            int kind = f & 0xf, must = 0;
            if (kind == 3) { cur.rem = -1.0f; break; }                  /* closed */
            if (racer) {
                if (pt_flags(cur.pt) & 0x800) {
                    if (kind == 7 || kind == 5 || kind == 10) { none = 1; continue; }
                    must = kind == 2;
                }
            } else if (pt_flags(cur.pt) >> 25 & 1) {
                int k1 = f >> 4 & 0xf;
                if (k1 == 7 || k1 == 5 || k1 == 10) { none = 1; continue; }
                must = k1 == 2;
            }
            Pt *t = lk_pt(n, l);
            float along = t ? (pt_x(t) - pt_x(cur.pt)) * pt_dx(cur.pt) + (pt_y(t) - pt_y(cur.pt)) * pt_dy(cur.pt) : 0;
            if (must) {                                                 /* followed, no decision kept */
                cur.dir ^= (int)(f >> 23 & 1);
                cur.rem += pt_seglen(cur.pt) - along;
                cur.pt = t;
                break;
            }
            int li = lk_index(n, l) & 0x1fff;
            PfState br = cur;
            br.pt = t; br.dir = cur.dir ^ (int)(f >> 23 & 1);
            br.rem = cur.rem + pt_seglen(cur.pt) - along;
            if (br.count < 15) br.dec[br.count++] = (unsigned short)(li | 0x2000);
            if (cur.count < 15) cur.dec[cur.count++] = (unsigned short)li;
            if (t) { if (world_race_route(w) && kind != 0) PF_BACK(br); else PF_FRONT(br); }
            if (k == nl - 1) {                                          /* only after the last link */
                PfState nx = cur;
                nx.pt = sec_step(sec_of(n, cur.pt), cur.pt, cur.dir);   /* 0x401cc0 */
                if (nx.pt) PF_FRONT(nx);
            }
            pushed = 1;
        }
        if (pushed) cur.pt = NULL;
        if (none && cur.pt) cur.pt = sec_step(sec_of(n, cur.pt), cur.pt, cur.dir);   /* straight on */
        if (!cur.pt) goto pop;
        if (!pt_trafficway(cur.pt, fwd)) goto pop;                      /* 0x5d8bf0 / 0x5d8c40, by the start's way */
        continue;
    pop:
        if (!full && head == tail) return 0;
        cur = q[head];
        if (++head == 100) head = 0;
        full = 0;
        if (n->nlinks < ++pops) return 0;
    }
#undef PF_FRONT
#undef PF_BACK
    return 0;
}
/* 0x41b820 FindPathToTarget (depth-first): in each section whose box holds
   the car, its first point with the car on its road searches (the way by
   dir2); the shortest of up to six found */
static int pf_find(AiWorld *w, PfState *out, const float *pos, const float *dir2, const float *tgt, float maxd)
{
    const AiNet *n = w->net;
    PfState res[6];
    float cost[6];
    int nr = 0;
    for (int s = 0; s < n->norder && nr < 6; s++) {
        const unsigned char *sec = n->order[s];
        if (!sec) continue;
        float lo[2] = { rdf(sec + 0x60), rdf(sec + 0x64) }, hi[2] = { rdf(sec + 0x68), rdf(sec + 0x6c) };
        if (!box_in(lo, hi, pos, 0)) continue;                          /* 0x43d410 */
        int tried = 0;
        for (int i = 0; !tried && i < sec_npts(sec); ) {
            Pt *P = sec_pt(sec, i);
            float fw = pt_dx(P) * dir2[0] + pt_dy(P) * dir2[1], lat, al;
            pt_outside2(n, P, pos, 0, &lat, &al);
            if (fabsf(pos[2] - pt_z(P)) < 4.0f && lat <= 0 && al <= 0) {
                PfState r1;
                tried = 1;
                if (pf_depth(w, &r1, P, tgt, maxd, 0 < fw, 1)) { res[nr] = r1; cost[nr] = maxd - r1.rem; nr++; }
            }
            float dx = pt_x(P) - pos[0], dy = pt_y(P) - pos[1];
            int st = (int)(sqrtf(dx * dx + dy * dy) * 0.02f);           /* 0x7851c0 */
            i += st < 1 ? 1 : st;
        }
    }
    if (nr < 1) return 0;
    int best = 0;
    for (int i = 1; i < nr; i++) if (cost[i] < cost[best]) best = i;
    *out = res[best];
    return 1;
}
/* 0x41e540 DoPathfinding: a path to the target's road point (400 m, racer
   roads; tried the other way round too); the controller set on its start
   and every decision on it written sticky into the link memory */
static void enc_pathfind(AiRacer *r, int target)
{
    AiWorld *w = r->w;
    const AiSeen *t = seen_car(w, target);
    if (!t || !t->v) return;
    float dir[2] = { r->car.vel[0], r->car.vel[1] };
    if (r->car.absv < 1.0f) { dir[0] = r->car.fwd[0]; dir[1] = r->car.fwd[1]; }   /* 0x784250 */
    Pt *P = t->racer ? t->racer->rpt : NULL;                            /* 0x5ec0b0 */
    float tg[3];
    if (P) { tg[0] = pt_x(P); tg[1] = pt_y(P); tg[2] = pt_z(P); }
    else memcpy(tg, t->v->pos, sizeof tg);
    PfState node;
    if (!pf_find(w, &node, r->car.pos, dir, tg, 400.0f)) {
        dir[0] = -dir[0]; dir[1] = -dir[1];
        if (!pf_find(w, &node, r->car.pos, dir, tg, 400.0f)) return;
    }
    r->rpt = node.start;                                                /* ctrl +0xc */
    r->rdir = node.sdir != 0;                                           /* ctrl +0x10 */
    for (int i = 0; i < 16; i++) r->linkmem[i] &= ~3u;
    fl_reset(&r->L);
    for (int k = 0; k < node.count; k++) {
        int slot = -1;
        for (int i = 0; i < 16; i++) if (!(r->linkmem[i] & 1)) { slot = i; break; }
        if (slot < 0) break;
        unsigned d = node.dec[k], link = d & 0x1fff, take = d >> 13 & 1;
        r->linkmem[slot] = ((link << 1 | take) << 3) | (r->linkmem[slot] & 0xfffe0007u) | 7u;
    }
}
/* the goal's Think (vt+0x44): ChaseCar's 0x420910 in the Outrun, an AI racer */
static void goal_think(AiRacer *r)
{
    if (r->goal == GOAL_CHASE && r->w->mode == 7 && r->owner_kind == 2) enc_pathfind(r, r->ch_target);
}
