/* aitraffic.c -- the retail NFSU2 race traffic, ported from SPEED2.EXE (v1.2).
 *
 * #included at the end of airacer.c: a traffic car's controller is the
 * TrafficController (0x426580, vtable 0x785428, owner kind 3) on the same
 * base as the AI racer's (0x420500) -- the FuturePoint path, the racers
 * table, the plan and the race driver are shared; what differs is here:
 *   - the tick 0x429d90 (think in its slot of a 2n-frame cycle),
 *   - the goal "Be traffic car" (0x40caf0, vtable 0x7848a0) and its chooser
 *     0x426c20 with the actions DriveTrafficLane (0x426ec0),
 *     AvoidAvoidableAsTraffic (0x424b40) and ObeyTrafficIntersections
 *     (0x425100), the goal's speed 0x41c380,
 *   - the intersections (route chunk 0x34123, 0x114 bytes, kept in the
 *     loaded route file: 16 slots for the traffic ids 0x32..0x41),
 *   - the start placement 0x42e2c0 / 0x42c670 and the TrafficTeleporter
 *     0x42f910 that every second moves a traffic car out of the views'
 *     reach to a place a few hundred metres from a human (0x42e9f0).
 * The car itself runs on the TrafficMover (N2_MOVER_TRAFFIC), the
 * Mellovator once anything touches it or it starts avoiding.
 * Decode notes: E:/re-tools/out/TRAFFIC.md. */

#define TR_VMAX 24.5865f                                                /* 0x7845a4: 55 mph */

/* 0x5ea4e0: an int in [0, n) */
static int ai_irand(AiWorld *w, int n)
{
    if (n <= 0) return 0;
    int k = (int)ai_rand(w, (float)n);
    return k >= n ? n - 1 : k;
}
static void wr32le(unsigned char *p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static void wrfle(unsigned char *p, float f) { uint32_t u; memcpy(&u, &f, 4); wr32le(p, u); }

/* ---- intersections (chunk 0x34123): +0 type (2: stop on its second road,
   3: all-way stop), +4 s16 section of road 0 / 1, +8 s8 the first traffic
   car per road (id, -1 none), +0xa s16 their points, +0x10 the road with
   the right of way (-1 none), +0x14 + k * 0x10 the slot of traffic id
   0x32 + k: +0 on, +4 road, +8 distance to the stop line, +0xc time -- */
static unsigned char *isec_rec(const AiNet *n, int i)
{
    if (i < 0 || i >= n->nsectors) return NULL;
    return (unsigned char *)(uintptr_t)(n->sectors + i * 0x114);      /* the loaded file, writable */
}
static unsigned char *isec_slot(unsigned char *I, int id)
{
    int k = id - 0x32;
    return (k < 0 || k > 15) ? NULL : I + 0x14 + k * 0x10;
}
/* 0x401f20: the intersection of a path point */
static int fp_isec(const AiNet *n, const FPoint *f)
{
    Pt *p = f ? pt_at(n, f->sec, f->pt) : NULL;
    if (!p) return -1;
    unsigned s = pt_flags(p) >> 19 & 0x3f;
    return (s == 0x3f || (int)s >= n->nsectors) ? -1 : (int)s;
}
/* FList +0xc4: the first path point at an intersection */
static const FPoint *tr_sector_fp(const AiRacer *r)
{
    const FList *L = &r->L;
    return (L->sector_at != -1 && L->sector_at < L->n) ? L->p[L->sector_at] : NULL;
}
static int isec_road(const unsigned char *I, int secid)
{
    for (int k = 0; k < 2; k++) if (rds16(I + 4 + 2 * k) == secid) return k;
    return -1;
}
static float isec_first_time(const unsigned char *I, int road)          /* 0x408f80 */
{
    int f = (signed char)I[8 + road];
    if (f == -1) return 99999.0f;
    const unsigned char *s = isec_slot((unsigned char *)(uintptr_t)I, f);
    return s ? rdf(s + 0xc) : 99999.0f;
}
static void isec_first(unsigned char *I, int road)                     /* 0x408fb0 */
{
    float best = 999999.0f;                                             /* 0x7845bc */
    I[8 + road] = 0xff;
    for (int k = 0; k < 16; k++) {
        const unsigned char *s = I + 0x14 + k * 0x10;
        if (s[0] && (int)rd32(s + 4) == road && rdf(s + 0xc) < best) { I[8 + road] = (unsigned char)(0x32 + k); best = rdf(s + 0xc); }
    }
}
/* 0x409110: the right of way lapses when no car of that road is within 12 m */
static void isec_green_check(unsigned char *I)
{
    int g = (int)rd32(I + 0x10);
    if (g == -1) return;
    char near[2] = { 0, 0 };
    for (int k = 0; k < 16; k++) {
        const unsigned char *s = I + 0x14 + k * 0x10;
        int road = (int)rd32(s + 4);
        if (s[0] && rdf(s + 8) < 12.0f && (road == 0 || road == 1)) near[road] = 1;   /* 0x783b94 */
    }
    if ((g == 0 || g == 1) && !near[g]) wr32le(I + 0x10, 0xffffffffu);
}
/* 0x410b50 (traffic only): a car's distance and time to the stop line */
static void isec_register(unsigned char *I, int id, int secid, float dist, float time)
{
    unsigned char *s = isec_slot(I, id);
    if (!s) return;
    int road = isec_road(I, secid);
    wrfle(s + 0xc, time);
    s[0] = 1;
    wrfle(s + 8, dist);
    wr32le(s + 4, (uint32_t)road);
    if (road == -1) { s[0] = 0; return; }
    if (time < isec_first_time(I, road)) I[8 + road] = (unsigned char)id;
}
/* 0x4109a0: may a car of `road` go? Types 1 and 2 only; the road with the
   right of way may, the other not; with none, road 0 may, road 1 when road
   0's first car is 3 s or more away */
static int isec_may_go(const unsigned char *I, int road)
{
    int type = (int)rd32(I);
    if (type != 1 && type != 2) return 0;
    int g = (int)rd32(I + 0x10);
    if (g == road) return 1;
    if (g == (road == 0)) return 0;
    if (road != 0) return 3.0f <= isec_first_time(I, 0);               /* 0x783b98 */
    return 1;
}
static void isec_take(unsigned char *I, int secid)                      /* 0x410a10 */
{
    int k = isec_road(I, secid);
    if (k != -1 && (int)rd32(I + 0x10) != (k == 0)) wr32le(I + 0x10, (uint32_t)k);
}
/* 0x410a50: two traffic cars on different roads of the intersection, both
   short of the stop line */
static int isec_crossing(unsigned char *I, const AiRacer *a, const AiRacer *b)
{
    if (!a || !b || !a->traffic || !b->traffic) return 1;
    const unsigned char *sa = isec_slot(I, a->id), *sb = isec_slot(I, b->id);
    if (!sa || !sb) return 0;
    return sa[0] && sb[0] && 0 <= rdf(sa + 8) && 0 <= rdf(sb + 8) && rd32(sa + 4) != rd32(sb + 4);
}
/* 0x4111c0 (every frame): the intersections whose both roads are traffic
   roads (section +0x70 bit 6) */
static void isec_frame(const AiNet *n)
{
    for (int i = 0; i < n->nsectors; i++) {
        unsigned char *I = isec_rec(n, i);
        int ok = 1;
        for (int k = 0; k < 2 && ok; k++) {
            int id = rds16(I + 4 + 2 * k);
            if (id < 0 || id >= SEC_MAX || !n->sec[id] || !(rd32(n->sec[id] + 0x70) >> 6 & 1)) ok = 0;
        }
        if (ok) isec_green_check(I);
    }
}
/* 0x413800: the car's marks off every intersection */
static void isec_clear(AiRacer *r)
{
    const AiNet *n = r->w->net;
    for (int i = 0; i < n->nsectors; i++) {
        unsigned char *I = isec_rec(n, i), *s = isec_slot(I, r->id);
        if (s) s[0] = 0;
        for (int k = 0; k < 2; k++) if ((signed char)I[8 + k] == r->id) { isec_first(I, k); break; }
    }
}
/* 0x410800: how far the cross road's near edge lies before the car's
   intersection point along its way (0 when the section is not one of it) */
static float isec_edge(const AiNet *n, const unsigned char *I, int secid, int back)
{
    int k = isec_road(I, secid);
    if (k == -1) return 0;
    Pt *p = pt_at(n, secid, rds16(I + 0xa + 2 * k));
    Pt *q = pt_at(n, rds16(I + 4 + 2 * (k == 0)), rds16(I + 0xa + 2 * (k == 0)));
    if (!p || !q) return 0;
    float e1[3], e2[3], d[2] = { pt_dx(p), pt_dy(p) };
    pt_pos3(n, q, 0, 0, -pt_wl(q), e1);                                 /* 0x5dd2f0 */
    pt_pos3(n, q, 0, 0, pt_wr(q), e2);
    if (back) pt_backdir(n, p, d);
    float a = (e1[0] - pt_x(p)) * d[0] + (e1[1] - pt_y(p)) * d[1];
    float b = (e2[0] - pt_x(p)) * d[0] + (e2[1] - pt_y(p)) * d[1];
    return -(a < b ? a : b);
}
/* 0x412170: an entry's distance to the stop line, 12 m short of the cross road */
static float tr_stopdist(const AiRacer *r, const RacerEnt *e)
{
    const AiNet *n = r->w->net;
    const FPoint *f = tr_sector_fp(r);
    if (!f || !e) return 99999.0f;
    const unsigned char *I = isec_rec(n, fp_isec(n, f));
    float edge = I ? isec_edge(n, I, f->sec, f->back) : 0;
    return f->dist - edge - 12.0f - e->pathpos - e->ext[1];             /* 0x783b94 */
}
/* 0x417890: register at the intersection ahead */
static void tr_isec_register(AiRacer *r)
{
    const AiNet *n = r->w->net;
    const FPoint *f = tr_sector_fp(r);
    if (!r->L.n || !r->nent || !f || r->own < 0) return;
    unsigned char *I = isec_rec(n, fp_isec(n, f));
    if (!I) return;
    float d = tr_stopdist(r, &r->ent[r->own]), t;
    float v = r->car.speed;                                             /* car +0x42c */
    if (d <= 0) t = 0;
    else if (v <= 0.001f) t = 99999.0f;
    else { t = d / v; if (t < 0) t = 0; }                               /* 0x401050 */
    isec_register(I, r->id, f->sec, d, t);
}
/* 0x417750: the path ends in a dead end (a kind-3 link against its way)
   less than a second ahead */
static int tr_deadend(const AiRacer *r)
{
    const AiNet *n = r->w->net;
    const FList *L = &r->L;
    if (L->n < 1) return 0;
    const FPoint *f = L->p[L->n - 1];
    Pt *p = pt_at(n, f->sec, f->pt);
    if (!p) return 0;
    int dead = 0, nl = pt_nlinks(n, p);
    for (int k = 0; k < nl && !dead; k++) {
        uint32_t lf = lk_flags(pt_link(n, p, k));
        if ((int)(lf >> 18 & 1) != !f->back && (lf & 0xf) == 3) dead = 1;
    }
    if (!dead) return 0;
    const RacerEnt *o = own_ent(r);
    float v = r->car.absv <= 0.1f ? 0.1f : r->car.absv;
    return (f->dist - (o ? o->pathpos : 0)) / v < 1.0f;                 /* 0x783b88 */
}

/* ---- the cars ahead in a lane ------------------------------------------- */
/* 0x402cc0: the gap to another car straight ahead (its box against the
   car's, their headings blended) and the time to close it along the car's
   velocity; 99999 none */
static void tr_closing(const AiRacer *r, const AiSeen *o, float *d, float *t)
{
    *d = 99999.0f; *t = 99999.0f;
    if (!o || !o->v) return;
    const AiCarView *c = &r->car;
    float wo = r->halfwidth, lo = r->half_len, w2 = o->half[1], l2 = o->half[0];
    float ang = atan2f(o->v->fwd[1], o->v->fwd[0]) - (float)c->heading * (6.2831853f / 65536.0f);
    float k = cosf(ang), k2 = k * k;
    float rel[3] = { o->v->pos[0] - c->pos[0], o->v->pos[1] - c->pos[1], o->v->pos[2] - c->pos[2] };
    float fw = rel[0] * c->fwd[0] + rel[1] * c->fwd[1] + rel[2] * c->fwd[2];   /* 0x591fc0 */
    float lt = rel[0] * c->left[0] + rel[1] * c->left[1] + rel[2] * c->left[2];
    float g = 99999.0f;
    if (0 <= fw && fabsf(lt) <= k2 * w2 + (1.0f - k2) * l2 + wo) {
        g = fw - l2 - lo;
        if (g <= 0) g = 0;
        *d = g;
    }
    if (0.1f < c->absv) {
        float vx = c->vel[0], vy = c->vel[1];
        float q = ((o->v->vel[0] - vx) * vx + vy * (o->v->vel[1] - vy)) / (vx * vx + vy * vy);
        float px = vx * q, py = vy * q, pl = sqrtf(px * px + py * py);
        if (px * vx + vy * py < 0 && 0.001f < pl) *t = g / pl;
    }
}
/* 0x402ed0: the nearest car ahead in a lane (lateral `lat` off its line):
   time 0 within the horizon (or 20 m when standing), else when it closes;
   returns its car, -1 none (time / gap 99999) */
static int tr_ahead(AiRacer *r, int lane, float lat, float horizon, float *t, float *d)
{
    *t = 99999.0f; *d = 99999.0f;
    const RacerEnt *own = own_ent(r);
    if (!own) return -1;
    for (int i = r->own - 1; i >= 0; i--) {
        const RacerEnt *e = &r->ent[i];
        if (e->pathpos < own->pathpos) continue;
        if (e->ext[0] + 0.5f + own->ext[0] < fabsf(ent_lat(e, lane) - lat)) continue;
        float gap = e->pathpos - e->ext[1] - own->pathpos - own->ext[1];
        if (gap <= 0) gap = 0;
        float tt = e->rv[1] < -0.01f ? gap / fabsf(e->rv[1]) : 99999.0f;
        float d2, t2;
        tr_closing(r, seen_car(r->w, e->car), &d2, &t2);
        float tm = tt < t2 ? tt : t2, dm = gap < d2 ? gap : d2;
        float acc = -1.0f;
        if (gap <= horizon || d2 <= horizon || ((gap <= 20.0f || d2 <= 20.0f) && r->car.absv < 0.1f)) acc = 0;
        else if (tm < 99999.0f) acc = tm;
        if (acc < 0) continue;
        *t = acc; *d = dm;
        return e->car;
    }
    return -1;
}
/* 0x4140e0: a car counts unless it is a traffic car crossing the car's
   intersection on the other road */
static int tr_counts(AiRacer *r, int car)
{
    const AiSeen *o = seen_car(r->w, car);
    const FPoint *f = tr_sector_fp(r);
    if (!seen_traffic(o) || !f) return 1;
    unsigned char *I = isec_rec(r->w->net, fp_isec(r->w->net, f));
    if (!I) return 1;
    return isec_crossing(I, o->racer, r) ? 0 : 1;
}
static float tr_vmax(const AiRacer *r) { return r->vcap < TR_VMAX ? r->vcap : TR_VMAX; }   /* vt+0x7c 0x41e7a0 */
/* 0x40f4a0 at a route point: the traffic lane nearest the car */
static int tr_nearest_lane_at(const AiRacer *r, Pt *p)
{
    const AiNet *n = r->w->net;
    if (!p) return -1;
    const unsigned char *s = sec_of(n, p);
    if (!s) return -1;
    int back = !r->rdir, first = back ? 6 : 2, best = -1;
    float lat = pt_latq(n, p, r->car.pos, back), bd = 8.999999e9f;
    for (int m = first; m < first + 4; m++) {
        if (!s[0x82 + m]) continue;
        float dd = fabsf(pt_laneoff(p, m) - lat);
        if (dd < bd) { bd = dd; best = m; }
    }
    if (best != -1 && back) best -= 4;
    return best;
}
/* 0x4053b0: a lane with its offset and rate kept */
static void lane_set3(AiLane *ls, int lane, float off, float rate, float now)
{
    ls->lane = lane; ls->off = off; ls->rate = rate;
    if (fabsf(rate - 999.0f) > 0.001f) ls->stamp = 0;
    else if (ls->stamp == 0) ls->stamp = now;
}

/* ---- the goal "Be traffic car" -------------------------------------------- */
/* 0x40b0b0: the outermost traffic lane the point's section has */
static int fp_outer_lane(const AiNet *n, const FPoint *f)
{
    if (!f || f->sec < 0 || f->pt == -1 || !n->sec[f->sec]) return -1;
    const unsigned char *s = n->sec[f->sec];
    for (int l = 5; l >= 2; l--) if (s[0x82 + fp_lanemap(f, l)]) return l;
    return -1;
}
/* 0x413bb0: by the link's second kind nibble: 2 always, 4 a coin (none when
   it crosses (bit 26) to a lane the target section lacks or away from the
   outermost lane), else never */
static int traffic_take_link(AiRacer *r, Lk *l)
{
    const AiNet *n = r->w->net;
    uint32_t f = lk_flags(l);
    int k1 = f >> 4 & 0xf;
    if (k1 == 2) return 1;                                              /* 0x5dcbe0(1) */
    if (k1 != 4) return 0;
    float p = 0.5f;
    const FPoint *fp = (r->L.cur != -1 && r->L.cur < r->L.n) ? r->L.p[(int)r->L.cur] : NULL;   /* ctrl vt+0x24 -> +0xad */
    int lane = r->ls.lane;                                              /* ctrl vt+0x1c */
    const unsigned char *ts = lk_sec(n, l);
    int li = lane + ((f >> 23 & 1) != (f >> 18 & 1) ? 4 : 0);
    if ((f >> 26 & 1) && (!fp || !ts || li < 0 || li > 9 || !ts[0x82 + li] || fp_outer_lane(n, fp) != lane)) p = 0;
    return ai_rand(r->w, 1.0f) < p;
}
/* 0x4233e0: onto a traffic lane */
static void traffic_lanes(AiRacer *r)
{
    if (r->L.n != 0 && r->nent != 0) {
        AiPlan P;
        memset(&P, 0, sizeof P);
        traffic_lane_plan(r, &r->ls, &P);
    } else {
        int lane = nearest_traffic_lane(r);
        if (lane != -1) lane_set(&r->ls, lane);
    }
}
/* 0x428720's lane check: a traffic lane (2..9) the path point lacks or that
   ends on the path -> the nearest lane either side (a coin for which side
   first) the car may step into, else (human or traffic) the nearest at its
   route point, lane 2 failing that */
static void traffic_lane_gone(AiRacer *r)
{
    AiWorld *w = r->w;
    FList *L = &r->L;
    const FPoint *f = L->p[(int)L->cur];
    int lane = r->ls.lane;
    if (!(1 < lane && lane < 10)) return;
    if (fp_haslane(w->net, f, lane) && (r->car.ctrl_type == 2 || fl_lane_ok(L, lane))) return;
    float off = r->ls.off, rate = r->ls.rate;
    int up = ai_rand(w, 1.0f) < 0.5f;
    for (int k = 1; k < 4; k++)
        for (int j = 0; j < 2; j++) {
            int c = up == j ? lane + k : lane - k;
            if (1 < c && c < 6 && lane_step_ok(r, f, lane, c)) { lane_set3(&r->ls, c, off, rate, w->time); return; }
        }
    if (r->owner_kind == 1 || r->owner_kind == 3) {
        int l = nearest_traffic_lane(r);
        lane_set3(&r->ls, l == -1 ? 2 : l, off, rate, w->time);
    }
}
/* 0x411fa0: the steepest drop in grade between path points from `from`
   while the point lies within `look`; *at the distance of its first point */
static float fl_min_grade(const AiNet *n, const FList *L, int from, float look, float *at)
{
    float best = 99999.0f;
    *at = 99999.0f;
    if (from < 0) from = 0;
    for (int i = from; i < L->n - 1; i++) {
        const FPoint *A = L->p[i], *B = L->p[i + 1];
        if (look < A->dist) break;
        Pt *pb = pt_at(n, B->sec, B->pt), *pa = pt_at(n, A->sec, A->pt);
        float gb = pb ? fp_grade(n, B, pb) : 0, ga = pa ? fp_grade(n, A, pa) : 0;
        if (gb - ga < best) { best = gb - ga; *at = A->dist; }
    }
    return best;
}
/* 0x41c380: the goal's speed: the cap (slower before a crest: 0x7e9bc0
   {(10, 1), (40, 0)} by its distance), the lane's speed here and a little
   ahead, 0 at the path's end; the action's speed below it unless it takes
   the cap; at most 55 mph (vt+0x4c 0x40cb60) */
static void traffic_goal_speed(AiRacer *r)
{
    AiWorld *w = r->w;
    const AiNet *n = w->net;
    AiAction *a = r->act;
    float vmax = tr_vmax(r);
    float look = 1.3f * r->car.absv;                                    /* 0x783564 */
    if (look <= 20.0f) look = 20.0f;                                    /* 0x783568 */
    int idx = -1;
    float pp = 0, lat;
    if (r->nent == 0) { if (r->L.n) idx = fl_locate(n, &r->L, r->car.pos, &pp, &lat, r->ls.lane); }
    else if (r->own >= 0) { idx = r->ent[r->own].idx; pp = r->ent[r->own].pathpos; }
    float at;
    if (fl_min_grade(n, &r->L, idx, look, &at) < -0.1f) {               /* 0x78356c */
        float k = at <= 10.0f ? 1.0f : 40.0f <= at ? 0.0f : 1.0f - (at - 10.0f) / 30.0f;
        vmax = (1.0f - k) * (vmax - 10.0f) + 10.0f;                     /* 0x783570 */
    }
    float ahead = 1.3f * r->car.absv;                                   /* 0x78355c */
    if (ahead < 8.0f) ahead = 8.0f;                                     /* 0x783560 */
    float s1 = fl_speed_at(&r->L, r->ls.lane, pp), s2 = fl_speed_at(&r->L, r->ls.lane, pp + ahead);
    float t = vmax < s1 ? vmax : s1;
    if (!(t < s2)) t = s2;
    if (r->at_end) t = 0;
    float spd = t, x = 0, y = 0;
    if (a && !a->use_cap) { spd = a->speed; x = a->x1f4; y = a->x1f8; }
    if (!(spd < t)) spd = t;
    if (TR_VMAX < spd) spd = TR_VMAX;
    driver_setspeed(r->drv, spd, x, y, a ? a->go : 1);
}

/* ---- the actions ----------------------------------------------------------- */
/* 0x4146c0: a lane (2..5) the car may take while avoiding: a step it may
   make, no car ahead in it within the action's horizon closing in 5 s or
   less -- unless later than the one it avoids and not that car */
static int tr_lane_free(AiRacer *r, AiAction *a, int lane, int cur)
{
    if (lane < 2 || 5 < lane) return 0;
    const FList *L = &r->L;
    const FPoint *f = (0 <= L->cur && L->cur < L->n) ? L->p[(int)L->cur] : NULL;
    if (lane != cur && (!f || !lane_step_ok(r, f, cur, lane))) return 0;
    float t, d;
    int car = tr_ahead(r, lane, 0, a->t220, &t, &d);
    if (car >= 0 && tr_counts(r, car) && t <= 5.0f)                    /* 0x783740 */
        return a->t21c < t && car != a->target;
    return 1;
}
/* DriveTrafficLane exec 0x4242d0 (a traffic car's and a drag racer's): the
   speed is the controller's vt+0x7c -- traffic its cap, a racer 1000
   (0x5a9f50); an AI racer in a drag race asks for nitrous (0x40e390) */
static void tr_drive_exec(AiRacer *r, AiAction *a)
{
    if (lane_class(r->ls.lane) == goal_laneclass(r)) plan_build(r, &r->ls, &a->plan);   /* 0x420d40 */
    else traffic_lane_plan(r, &r->ls, &a->plan);                        /* 0x420ed0 */
    a->use_cap = 0;
    a->speed = r->traffic ? tr_vmax(r) : 1000.0f;
    if ((r->w->drag || r->w->x890108) && r->owner_kind == 2) act_nitrous(r, a);
    a->x1f8 = 2.0f;                                                     /* 0x783724 */
    a->go = 1; a->catchup = 1;
}
/* AvoidAvoidableAsTraffic exec 0x424cb0: round a human ahead (another lane
   if one is free), else slow down behind the car ahead */
static void tr_avoid_exec(AiRacer *r, AiAction *a)
{
    AiWorld *w = r->w;
    const RacerEnt *own = own_ent(r), *e = ent_of(r, a->target);
    float gap = 0;
    int blocked = 0;
    if (e && a->target >= 0 && seen_human(seen_car(w, a->target)) && -40.0f < e->rv[1]) {   /* 0x783750 */
        float g = ent_ahead(r, e) - e->ext[1] - (own ? own->ext[1] : 0);
        gap = g <= 0 ? 0 : g;
        int cur = r->ls.lane, L5[5];
        if (e->rv[0] <= 0) { L5[0] = cur; L5[1] = cur + 1; L5[2] = cur - 1; L5[3] = cur + 2; L5[4] = cur - 2; }
        else { L5[0] = cur; L5[1] = cur - 1; L5[2] = cur + 1; L5[3] = cur - 2; L5[4] = cur + 2; }
        int i = 0;
        for (; i < 5; i++) if (tr_lane_free(r, a, L5[i], cur)) break;
        if (i < 5 && L5[i] != 0xff) lane_set(&r->ls, L5[i]);
        else blocked = 1;
    }
    int lane = r->ls.lane, brake;
    float t, d;
    int car = tr_ahead(r, lane, own_lat(r, lane), a->t220, &t, &d);
    if (car < 0) brake = blocked;
    else {
        if (car != a->target) { a->target = car; a->t220 = 30.0f; }    /* 0x41bc60, 0x783748 */
        a->t21c = t;
        if (r->car.absv < 0.5f) {                                       /* 0x784260 */
            float h = a->t220 <= 30.0f ? 30.0f : a->t220;
            if (h < d) h = d;
            a->t220 = h;
            Pt *p = NULL;
            if (own && own->idx >= 0 && own->idx < r->L.n) p = pt_at(w->net, r->L.p[own->idx]->sec, r->L.p[own->idx]->pt);   /* 0x401ed0 */
            int l = tr_nearest_lane_at(r, p);
            if (l != -1) lane_set(&r->ls, l);
        }
        brake = t < 1.5f || gap <= a->t220;                             /* 0x783744 */
    }
    if (brake) {
        if (d <= 15.0f) { a->speed = 0; a->x1f8 = 0; }                  /* 0x78374c */
        else { a->speed = 6.0f; a->x1f8 = 2.0f; }
    } else { a->speed = tr_vmax(r); a->x1f8 = 2.0f; }
    a->x1f4 = 0;
    plan_build(r, &r->ls, &a->plan);
    a->use_cap = 0; a->go = 1; a->catchup = 1;
}
/* 0x41cd10: done once the target is behind or pulling away and nothing
   that counts closes within 5 s ahead; a new car ahead becomes the target */
static int tr_avoid_done(AiRacer *r, AiAction *a)
{
    const RacerEnt *e = ent_of(r, a->target);
    if (a->target < 0 || !e) return 1;
    if (!(ent_ahead(r, e) < 0) && e->rv[1] <= 5.0f) return 0;          /* 0x7843e8 */
    int lane = r->ls.lane;
    float t, d;
    int car = tr_ahead(r, lane, own_lat(r, lane), a->t220, &t, &d);
    if (car >= 0 && t <= 5.0f) {
        int done = !tr_counts(r, car);
        if (!done && car != a->target) { a->target = car; a->t220 = 30.0f; a->t21c = t; }
        return done;
    }
    return 1;
}
/* ObeyTrafficIntersections exec 0x425150: slow behind a car ahead; stop at
   an all-way stop (type 3) or at a type-2 stop on its second road, else
   go when 0x4109a0 gives the right of way (taking it) */
static void tr_obey_exec(AiRacer *r, AiAction *a)
{
    const AiNet *n = r->w->net;
    const FPoint *f = tr_sector_fp(r);
    unsigned char *I = f ? isec_rec(n, fp_isec(n, f)) : NULL;
    float vmax = tr_vmax(r), t, d;
    a->speed = vmax;
    int lane = r->ls.lane;
    int car = tr_ahead(r, lane, own_lat(r, lane), 30.0f, &t, &d);     /* 0x783758 */
    if (car >= 0 && tr_counts(r, car) && (t < 1.5f || d < 30.0f)) {
        if (d <= 15.0f) { if (!(a->speed < 0)) a->speed = 0; a->x1f8 = 0; }
        else { if (!(a->speed < 6.0f)) a->speed = 6.0f; a->x1f8 = 2.0f; }   /* 0x7859d0 */
    }
    if (I && f) {                                                       /* (always: Obey is chosen at an intersection) */
        int must = -1;
        if (!a->stopped) {
            int type = (int)rd32(I);
            if ((type == 2 && f->sec == rds16(I + 6)) || type == 3) must = 1;
        }
        if (must == -1) {
            int k = isec_road(I, f->sec);
            if (k != -1 && isec_may_go(I, k)) { isec_take(I, f->sec); must = 0; }
            else must = 1;
        }
        float d2 = tr_stopdist(r, own_ent(r));
        if (must) {
            if (d2 <= 30.0f) { if (!(a->speed < 6.0f)) a->speed = 6.0f; }   /* 0x783754 */
            else if (vmax < a->speed) a->speed = vmax;
            if (d2 < 0) {
                if (!(a->speed < 0)) a->speed = 0;
                if (r->car.absv < 0.5f) a->stopped = 1;
            }
        } else if (vmax < a->speed) a->speed = vmax;
    }
    plan_build(r, &r->ls, &a->plan);
    a->x1f4 = 0; a->x1f8 = 1.0f;
    a->use_cap = 0; a->go = 1; a->catchup = 0;
}
/* 0x41ce30: done when no intersection lies ahead; a new one starts over */
static int tr_obey_done(AiRacer *r, AiAction *a)
{
    const FPoint *f = tr_sector_fp(r);
    if (!f) return 1;
    int is = fp_isec(r->w->net, f);
    if (is != a->isec) { a->isec = is; a->stopped = 0; }               /* +0x218 = 0x414930 (always 1) */
    return 0;
}
static void tr_exec(AiRacer *r, AiAction *a)
{
    if (a->type == TR_AVOID) tr_avoid_exec(r, a);
    else if (a->type == TR_OBEY) tr_obey_exec(r, a);
    else tr_drive_exec(r, a);
}
static int tr_done(AiRacer *r, AiAction *a)
{
    return a->type == TR_AVOID ? tr_avoid_done(r, a) : a->type == TR_OBEY ? tr_obey_done(r, a) : 0;
}
/* 0x4145b0: the first car ahead in the lane (any distance) that closes
   within 5 s and counts */
static int tr_avoid_prio(AiRacer *r, int *car, float *t, float *d)
{
    int lane = r->ls.lane;
    int c = tr_ahead(r, lane, own_lat(r, lane), 99999.0f, t, d);
    /* (goal vt+0x68 -- the ambient goal only -- would let race cars faster
       than 20 m/s pass) */
    if (c >= 0 && *t <= 5.0f && tr_counts(r, c)) { *car = c; return 300; }
    return -1;
}
/* the chooser 0x426c20: ObeyTrafficIntersections (600) at an intersection
   ahead, AvoidAvoidableAsTraffic (300, horn first) behind a car, else
   DriveTrafficLane */
static AiAction *tr_choose(AiRacer *r, int cur, int prio)
{
    const FPoint *f = tr_sector_fp(r);
    if (cur != TR_OBEY && prio <= 600 && f) {                           /* 0x4149e0 */
        AiAction *a = act_new(TR_OBEY, 600, -1.0f);                     /* 0x425100 (0x783648) */
        if (a) { a->isec = fp_isec(r->w->net, f); a->stopped = 0; }
        return a;
    }
    int car;
    float t, d;
    if (cur != TR_AVOID && prio <= 300 && tr_avoid_prio(r, &car, &t, &d) >= 0) {
        racer_react(r, car);                                            /* 0x40d5c0 */
        AiAction *a = act_new(TR_AVOID, 300, -1.0f);                    /* 0x424b40 (0x783634) */
        if (a) { a->target = car; a->t21c = t; a->t220 = 30.0f; }
        return a;
    }
    return act_new(TR_DRIVE, 0, -1.0f);                                 /* goal vt+0x3c 0x426ec0 */
}

/* ---- the controller -------------------------------------------------------- */
/* 0x422150: the action runs, the goal's speed, the plan to the driver */
static void traffic_apply(AiRacer *r)
{
    AiAction *a = r->act;
    tr_exec(r, a);
    traffic_goal_speed(r);
    driver_setplan(r, &a->plan);
    plan_keep(&a->plan, NULL, r->w->time, 0);                           /* 0x41d160 */
}
/* 0x4020d0: off the road net: no mover, out of the world */
static void traffic_off(AiRacer *r)
{
    if (r->dead) return;
    r->tmover = 0;
    r->dead = 1;
}
/* vt+0x34 0x42a050: start over -- path, decisions and lane plan afresh,
   back in the world if it was off, DriveTrafficLane */
static void traffic_restart(AiRacer *r)
{
    AiWorld *w = r->w;
    r->finished = 0; r->stop_why = 0;
    for (int i = 0; i < 16; i++) r->linkmem[i] &= ~3u;
    fl_reset(&r->L);                                                    /* 0x411be0(+0x6e8) */
    plan_keep(&r->dplan, NULL, w->time, 0);                             /* 0x41d160(driver +0x10, 0, 0): the plan goes */
    r->drv->gas = 0; r->drv->brake = 0; r->drv->hand = 0;             /* 0x5eb910 / 0x5eb8f0 / 0x5eb900 */
    if (r->dead) { r->dead = 0; r->tmover = N2_MOVER_TRAFFIC; }       /* 0x402110 (0x5f4400) */
    r->at_end = 0;
    if (r->act) act_free(r->act);
    r->act = act_new(TR_DRIVE, 0, -1.0f);
    if (r->act) r->act->start = w->time;
    if (!racer_updatepath(r)) { racer_stop(r); r->stop_why = 5; }
    else if (r->act) traffic_apply(r);
    r->nent = 0; r->own = -1;                                           /* 0x41bf80 */
    r->force_replan = 0;
}
const char *(*airacer_poly_name)(int id);
static int pt_onrace(const AiWorld *w, Pt *p);
/* DIAG: one line when a traffic car's stop latch (+0x710) is first set */
static void tr_latch(AiRacer *r, int why)
{
    if (r->finished) return;
    racer_stop(r);
    r->stop_why = (char)why;
    printf("traffic latch: car %d t %.1f at (%.1f %.1f) v %.1f why %d act %d path %d", r->tidx, r->w->time, r->car.pos[0], r->car.pos[1],
           r->car.absv, why, r->act ? r->act->type : -1, r->L.n);
    for (int c = 0; c < r->ncontact; c++)
        printf(" c(%d %s j %.1f %.1fs @%.1f %.1f %s)", r->contact[c].other, r->contact[c].car ? "car" : r->contact[c].wall ? "wall" : "poly",
               r->contact[c].j, r->w->time - r->contact[c].start, r->contact[c].pt[0], r->contact[c].pt[1],
               r->contact[c].other <= -2 && airacer_poly_name ? airacer_poly_name(-2 - r->contact[c].other) : "");
    if (r->L.n > 0) {
        const FPoint *f0 = r->L.p[0], *f1 = r->L.p[r->L.n - 1];
        Pt *p0 = pt_at(r->w->net, f0->sec, f0->pt), *p1 = pt_at(r->w->net, f1->sec, f1->pt);
        printf(" path sec %d pt %d on %d -> sec %d pt %d on %d back %d", f0->sec, f0->pt, pt_onrace(r->w, p0), f1->sec, f1->pt, pt_onrace(r->w, p1), f1->back);
        if (p1) {
            int nl = pt_nlinks(r->w->net, p1);
            for (int k = 0; k < nl; k++) {
                Lk *l = pt_link(r->w->net, p1, k);
                uint32_t lf = lk_flags(l);
                const unsigned char *ts = lk_sec(r->w->net, l);
                printf(" link(kind %u k1 %u b18 %u -> sec %d)", lf & 0xf, lf >> 4 & 0xf, lf >> 18 & 1, ts ? (int)rdu16(ts + 10) : -1);
            }
        }
    }
    printf("%c", 10);
}
/* the tick (vt slot 3, 0x429d90) */
static void traffic_tick(AiRacer *r, float dt)
{
    AiWorld *w = r->w;
    if (r->dead) return;
    /* 0x408de0: a contact of over 30 s, or standing in a contact while the
       driver wants to go: stop */
    if (r->ncontact > 0) {
        for (int i = 0; i < r->ncontact; i++)
            if (30.0f < r->contact[i].j) { tr_latch(r, 1); racer_stop(r); break; }   /* pair +0x60 sum |J| > 0x783b8c */
        if (r->car.absv < 1.0f && 1.0f < r->drv->vtarget) { tr_latch(r, 2); racer_stop(r); }   /* 0x784250 */
    }
    r->think_left -= dt;
    /* owner kind 3: think on the odd frames of a 2n-frame cycle, in its
       place of the AI manager's traffic list */
    int n = w->ntr;
    if (n < 1) return;
    unsigned k = w->frame % (unsigned)(2 * n);
    if (!(k & 1) || (int)(k >> 1) >= n || w->tr[k >> 1] != r) return;
    r->think_left += 0.2f;                                              /* 0x783b7c */
    isec_clear(r);
    if (r->finished) goto stop;
    linkmem_tidy(r);                                                    /* 0x41d0e0 */
    if (!racer_updatepath(r)) { tr_latch(r, 3); goto stop; }
    if (tr_deadend(r)) { tr_latch(r, 4); goto stop; }
    tr_isec_register(r);
    {
        float own = r->own >= 0 ? r->ent[r->own].pathpos : 0;
        if (fl_atend(w->net, &r->L, own)) r->at_end = 1;               /* 0x40abb0 (0x410620: no traffic sectors) */
    }
    if (r->at_end) { traffic_off(r); goto stop; }
    if (!r->goal) r->goal = GOAL_TRAFFIC;                               /* vt+0x88 0x42c660 */
    {
        AiAction *na;
        int p = -1;
        if (!r->act) na = act_new(TR_DRIVE, 0, -1.0f);
        else {
            int t = ACT_CHOOSE;
            if (!act_status(r, r->act)) { t = r->act->type; p = r->act->prio; }   /* 0x403120 / 0x401990 */
            na = tr_choose(r, t, p);
            if (na && na->prio <= p) { act_free(na); na = NULL; }
        }
        if (na) {
            if (r->act) { act_end(r, r->act); act_free(r->act); }
            r->act = na;
            na->start = w->time;
            if ((na->type == 8 || na->type == 1 || na->type == TR_AVOID) && r->tmover == N2_MOVER_TRAFFIC)
                r->tmover = N2_MOVER_MELLO;                             /* car +0x5cc set: 0x5f46a0(3) */
        }
    }
    if (r->act) traffic_apply(r);
    r->nent = 0; r->own = -1;
    r->force_replan = 0;
    return;
stop:
    racer_stop(r);
    r->nent = 0; r->own = -1;
    r->force_replan = 0;
}

/* ---- placing a traffic car ------------------------------------------------- */
typedef struct { const unsigned char *sec; Pt *pt; int dir; } TrPos;
/* 0x5dcc80: a point of the race route */
static int pt_onrace(const AiWorld *w, Pt *p)
{
    if (!world_race_route(w) || !p) return 0;
    const unsigned char *s = sec_of(w->net, p);
    return s && !(rd32(s + 0x70) >> 9 & 1) && (pt_flags(p) >> 29 & 1);
}
/* 0x5d4480: the section has a traffic lane for the way */
static int sec_haslanes(const unsigned char *s, int dir)
{
    for (int k = 0; k < 4; k++) if (s[0x84 + (dir == 0) * 4 + k]) return 1;
    return 0;
}
/* 0x401cc0: the next point along the section in the way, NULL at its end */
static Pt *sec_step(const unsigned char *s, Pt *p, int dir)
{
    if (!s || !p) return NULL;
    int i = (int)((p - s) - 0x8c) / 0x38;
    if (!dir) return i ? sec_pt(s, i - 1) : NULL;
    return i + 1 < sec_npts(s) ? sec_pt(s, i + 1) : NULL;
}
/* 0x5d8c40 / 0x5d8bf0: traffic may drive the point that way */
static int pt_trafficway(Pt *p, int dir)
{
    uint32_t f = pt_flags(p);
    if (!dir) return (f >> 18 & 1) || (f >> 26 & 1) || (f >> 28 & 1);
    return (f >> 11 & 1) || (f >> 25 & 1) || (f >> 27 & 1);
}
/* 0x5e2da0: a lane's offset right of the forward line (a backward lane's
   own offset is measured along the way back) */
static float lane_lat_fwd(const AiNet *n, Pt *p, int lane)
{
    if ((1 < lane && lane < 6) || lane == 0) return pt_laneoff(p, lane);
    float o[3];
    pt_pos3(n, p, 1, 0, pt_laneoff(p, lane), o);                        /* 0x5dd430 */
    return (o[0] - pt_x(p)) * pt_dy(p) - (o[1] - pt_y(p)) * pt_dx(p);
}
/* 0x410ca0: a random traffic lane of the point's section for the way (ten
   tries), else the first it has; -1 none (lanes 2..5 forward, 6..9 back) */
static int tr_lanepick(AiWorld *w, Pt *p, int dir)
{
    const unsigned char *s = sec_of(w->net, p);
    if (!s) return -1;
    int base = (dir == 0) * 4, first = base + 2;
    for (int i = 0; i < 10; i++) {
        int k = ai_irand(w, 400000) / 100000;
        if (s[0x84 + base + k]) return first + k;
    }
    for (int k = 0; k < 4; k++) if (s[0x84 + base + k]) return first + k;
    return -1;
}
/* the cars the placement rules look at (the AI manager's list): not the
   car itself, not one off the road net or at its path's end */
static int tr_listed(const AiSeen *o, const AiRacer *self)
{
    if (!o->v || (self && o->racer == self)) return 0;
    if (o->racer && o->racer->at_end) return 0;                         /* ctrl +0x77c */
    return o->active;                                                   /* (0x5ec060) */
}
/* 0x409840 / 0x409890 (race state 4): a view within 250 m (eye) or 250 m
   of progress. The views' progress (AI world +0xe0 / +0xe4) is taken from
   the human players' cars. */
static int tr_view_near_pos(const AiWorld *w, const float *q)
{
    if (w->race_state != 4) return 0;
    float m = 99999.0f;
    for (int i = 0; i < w->ncam; i++) {                                 /* 0x5f0fb0 */
        float dx = w->cam[i][0] - q[0], dy = w->cam[i][1] - q[1], dd = sqrtf(dx * dx + dy * dy);
        if (dd < m) m = dd;
    }
    return m < 250.0f;                                                  /* 0x78429c */
}
static int tr_view_near_prog(const AiWorld *w, float prog)
{
    if (w->race_state != 4) return 0;
    float m = 99999.0f;
    for (int i = 0; i < w->nseen; i++) {                                /* 0x5f12a0 */
        const AiSeen *o = &w->seen[i];
        if (!o->v || o->kind != 1) continue;
        float dd = fabsf(progress_diff(w, o->progress, prog));
        if (dd < m) m = dd;
    }
    return m < 250.0f;                                                  /* 0x7e9720 */
}
/* 0x418110: the point is clear for a traffic car: before the start not
   within the record's distance past the start line; on the race route no
   view within 250 m and no car within `rad` of progress (a traffic car
   either way, another ahead of it); off it no eye within 250 m and no car
   within `rad` */
static int tr_clear(AiWorld *w, Pt *p, float rad, const AiRacer *self)
{
    const unsigned char *rec = w->trec;
    int rev = w->reversed ? 1 : 0;
    float prog = rdf(p + 8);
    if (!rec[0xf4 + rev] && world_race_route(w) && (w->race_start < 0 || w->time < w->race_start) &&
        progress_diff(w, prog, w->start_prog) < rdf(rec + 0xf8 + rev * 4)) return 0;
    if (pt_onrace(w, p)) {
        if (tr_view_near_prog(w, prog)) return 0;
        for (int i = 0; i < w->nseen; i++) {                            /* 0x411060 */
            const AiSeen *o = &w->seen[i];
            if (!tr_listed(o, self)) continue;
            float dp = progress_diff(w, prog, o->progress);
            if (o->kind == 3 ? fabsf(dp) < rad : (dp < rad && 0 < dp)) return 0;
        }
    } else {
        float q[2] = { pt_x(p), pt_y(p) };
        if (tr_view_near_pos(w, q)) return 0;
        for (int i = 0; i < w->nseen; i++) {                            /* 0x410fc0 */
            const AiSeen *o = &w->seen[i];
            if (!tr_listed(o, self)) continue;
            float dx = q[0] - o->v->pos[0], dy = q[1] - o->v->pos[1];
            if (sqrtf(dx * dx + dy * dy) < rad) return 0;
        }
    }
    return 1;
}
/* the car stands at a new pose (0x604260 / 0x5f52c0): what the AI sees of
   it until the caller's next frame */
static void tr_pose(AiRacer *r, const float *pos, float head, float speed, float prog)
{
    AiCarView *c = &r->car;
    float cs = cosf(head), sn = sinf(head);
    memcpy(c->pos, pos, 12);
    c->fwd[0] = cs; c->fwd[1] = sn; c->fwd[2] = 0;
    c->left[0] = -sn; c->left[1] = cs; c->left[2] = 0;
    c->up[0] = 0; c->up[1] = 0; c->up[2] = 1;
    c->vel[0] = cs * speed; c->vel[1] = sn * speed; c->vel[2] = 0;
    c->speed = speed; c->absv = fabsf(speed);
    c->heading = (unsigned short)(int)floorf(head * (32768.0f / 3.14159265f) + 0.5f);
    r->progress = prog;
    r->placed = 1;
    memcpy(r->place_pos, pos, 12);
    r->place_head = head;
    r->place_speed = speed;
    for (int i = 0; i < r->w->nseen; i++) {
        AiSeen *o = &r->w->seen[i];
        if (o->racer != r) continue;
        o->progress = prog;
        o->active = 1;
    }
}
/* 0x42c670: a traffic car on a route point's lane, moving at the lane's
   speed: controller on the point and lane, its way, the skill by 0x89e7f4,
   the goal "Be traffic car", the goal's lanes, the TrafficMover */
static void tr_setup(AiRacer *r, Pt *p, int lane, float speed)
{
    AiWorld *w = r->w;
    const AiNet *n = w->net;
    int fwd = 1 < lane && lane < 6;
    float lat = lane_lat_fwd(n, p, lane);
    if (!fwd) lane -= 4;
    r->skill = w->tdiff == 0 ? 9 : w->tdiff == 1 ? 4 : w->tdiff == 2 ? 0 : -1;
    float d[2] = { pt_dx(p), pt_dy(p) };
    if (!fwd) pt_backdir(n, p, d);
    float pos[3];
    pt_pos3(n, p, 0, 0, lat, pos);                                      /* 0x5dd360 */
    r->dead = 0; r->at_end = 0; r->finished = 0;
    r->rpt = NULL;
    ctrl_setpoint(r, p);                                                /* 0x4106c0 */
    lane_set(&r->ls, lane);
    r->rdir = fwd;
    if (r->act) { act_free(r->act); r->act = NULL; }                    /* 0x402070 */
    r->goal = GOAL_TRAFFIC;                                             /* 0x40caf0 */
    tr_pose(r, pos, atan2f(d[1], d[0]), speed <= 1e-5f ? 0 : speed, rdf(p + 8));
    goal_lanes(r);                                                      /* 0x428490 */
    r->tmover = N2_MOVER_TRAFFIC;                                       /* 0x5f46a0(2); on at once (0x5f6cd0) */
}
/* 0x411110: a random point over the sections with traffic lanes for the
   way, weighted by their points (the count is 0x409900's, forward lanes) */
static int tr_randpoint(AiWorld *w, int dir, const unsigned char **s, Pt **p)
{
    const AiNet *n = w->net;
    int k = ai_irand(w, w->tele_points), cum = 0;
    for (int i = 0; i < n->norder; i++) {
        const unsigned char *sec = n->order[i];
        int d = dir;
        if (world_race_route(w) && (rd32(sec + 0x70) & 2)) d = !d;
        if (sec && sec_haslanes(sec, d) && k < (cum += sec_npts(sec))) {
            int idx = cum - k;
            if (idx >= sec_npts(sec)) { *s = sec; *p = NULL; return 1; }   /* (the original reads past the section) */
            *s = sec; *p = sec_pt(sec, idx);
            return 1;
        }
    }
    return 0;
}
/* 0x42e2c0: a random clear point with the link flags 25 / 26 (100 tries) */
static int tr_place_random(AiRacer *r)
{
    AiWorld *w = r->w;
    const unsigned char *rec = w->trec, *s = NULL;
    Pt *p = NULL;
    int lvl = w->traffic_level, rev = w->reversed ? 1 : 0;
    if (!w->tele_points) return 0;
    for (int tries = 0; tries < 100; tries++) {
        int dir = ai_rand(w, 1.0f) < 1.0f - rdf(rec + 0x108 + lvl * 4);
        tr_randpoint(w, dir, &s, &p);
        if (!s || !p) continue;
        uint32_t f = pt_flags(p);
        int idx = (int)((p - s) - 0x8c) / 0x38;
        if (rdu16(s + 10) != (f & 0x7ff) || !(f & 0x2000000) || !(f & 0x4000000) || idx > sec_npts(s) - 2) continue;
        if (!tr_clear(w, p, rdf(rec + 0x100 + rev * 4), NULL)) continue;
        if (world_race_route(w) && (rd32(s + 0x70) & 2)) dir = !dir;
        int lane = tr_lanepick(w, p, dir);                              /* 0x417c30 */
        if (lane == -1) lane = tr_lanepick(w, p, !dir);
        if (lane == -1) continue;
        tr_setup(r, p, lane, (float)p[0x2c + lane]);
        return 1;
    }
    return 0;
}

/* ---- the TrafficTeleporter -------------------------------------------------- */
/* 0x415130: walk `dist` metres from a point along its way, branching at
   links into a 20-entry queue (a kind-2 link followed, kind 7 not taken,
   a dead end stops); the first `max` places found */
typedef struct { Pt *p; float rem; int dir; } TrWalk;
static int tr_walk(AiWorld *w, TrPos *out, int max, Pt *p, float dist, int dir, int f6)
{
    const AiNet *n = w->net;
    TrWalk q[20];
    int head = 0, tail = 0, full = 0, found = 0, pops = 0, dir0 = dir;
    if (!p) return 0;
    float rem = dist;
    if (!dir) rem = pt_seglen(p) + dist;
#define TRQ_FRONT(E) do { if (!full) { head = head == 0 ? 19 : head - 1; full = tail == head; q[head] = (E); } } while (0)
#define TRQ_BACK(E)  do { if (!full) { q[tail] = (E); if (++tail == 20) tail = 0; full = tail == head; } } while (0)
    for (int it = 1; it <= n->norder * 0x400; it++) {
        rem -= pt_seglen(p);
        if (!(rem < 0)) {
            int straight = 1, branched = 0, nl = pt_nlinks(n, p);
            for (int k = 0; k < nl; k++) {
                Lk *l = pt_link(n, p, k);
                uint32_t f = lk_flags(l);
                if ((int)(f >> 18 & 1) == dir) continue;
                int kind = f & 0xf, sel = -1;
                straight = 0;
                if (kind == 3) { rem = -1.0f; break; }
                if (!f6) {
                    if (pt_flags(p) >> 25 & 1) {
                        if ((f >> 4 & 0xf) == 7) { straight = 1; continue; }
                        sel = f >> 4 & 0xf;
                    }
                } else if (pt_flags(p) >> 11 & 1) {
                    if (kind == 7) { straight = 1; continue; }
                    sel = kind;
                }
                Pt *t = lk_pt(n, l);
                if (!t) continue;
                float along = (pt_x(t) - pt_x(p)) * pt_dx(p) + (pt_y(t) - pt_y(p)) * pt_dy(p);
                if (sel == 2) {                                         /* followed */
                    dir ^= (int)(f >> 23 & 1);
                    rem = pt_seglen(p) + rem - along;
                    p = t;
                    goto check;
                }
                TrWalk a = { t, pt_seglen(p) + rem - along, (int)(f >> 23 & 1) ^ dir };
                TrWalk b = { sec_step(sec_of(n, p), p, dir), rem, dir };
                if (b.p) TRQ_FRONT(b);
                if (!world_race_route(w)) { if (!(ai_rand(w, 1.0f) < 0.5f)) TRQ_BACK(a); else TRQ_FRONT(a); }
                else if (kind == 0) TRQ_FRONT(a);
                else TRQ_BACK(a);
                branched = 1; straight = 0;
            }
            if (branched) p = NULL;
            if (straight) {
                if (!p) goto pop;
                p = sec_step(sec_of(n, p), p, dir);
            }
        check:
            if (!p) goto pop;
            if (!pt_trafficway(p, dir0)) goto pop;
            continue;
        }
        if (found < max) {
            out[found].sec = sec_of(n, p); out[found].pt = p; out[found].dir = dir;
            found++;
            if (found == max) return found;
        }
    pop:
        if (!full && head == tail) return found;
        p = q[head].p; rem = q[head].rem; dir = q[head].dir;
        if (++head == 20) head = 0;
        full = 0;
        if (n->nlinks < ++pops) return 0;
    }
#undef TRQ_FRONT
#undef TRQ_BACK
    return 0;
}
/* 0x43d410: q inside the box, widened by m */
static int box_in(const float *lo, const float *hi, const float *q, float m)
{
    return lo[0] - m <= q[0] && q[0] <= hi[0] + m && lo[1] - m <= q[1] && q[1] <= hi[1] + m;
}
/* 0x417d80: up to `max` starting places: the human's, then points of other
   sections running alongside (|cos| > 0.85, 2..30 m beside the road) */
static int tr_parallel(const AiWorld *w, TrPos *e, int max)
{
    const AiNet *n = w->net;
    const unsigned char *own = e[0].sec;
    Pt *p = e[0].pt;
    int cnt = 1;
    float L = pt_seglen(p);
    if (L < 20.0f) L = 20.0f;                                           /* 0x783bdc */
    float wl = pt_wl(p), wr = pt_wr(p);
    float b1lo[2] = { -L, -wl - 30.0f }, b1hi[2] = { L, -wl - 2.0f };   /* 0x783be4 / 0x783be0 */
    float b2lo[2] = { -L, wr + 2.0f }, b2hi[2] = { L, wr + 30.0f };
    float m = (wl < wr ? wr : wl) + 30.0f;
    float px[2] = { pt_x(p), pt_y(p) }, dx = pt_dx(p), dy = pt_dy(p);
    if (max < 2) return cnt;
    for (int s = 0; s < n->norder && cnt < max; s++) {
        const unsigned char *sec = n->order[s];
        if (sec == own) continue;
        float lo[2] = { rdf(sec + 0x60), rdf(sec + 0x64) }, hi[2] = { rdf(sec + 0x68), rdf(sec + 0x6c) };
        if (!box_in(lo, hi, px, m)) continue;
        for (int i = 0; i < sec_npts(sec); ) {
            Pt *c = sec_pt(sec, i);
            float dot = pt_dx(c) * dx + pt_dy(c) * dy;
            int b = e[0].dir ^ (dot < 0);
            float rx = pt_x(c) - px[0], ry = pt_y(c) - px[1];
            if (0.85f < fabsf(dot) && (pt_flags(c) >> (b ? 25 : 26) & 1)) {   /* 0x783be8 */
                float lq[2] = { rx * dx + ry * dy, rx * dy - ry * dx };
                if (box_in(b1lo, b1hi, lq, 0) || box_in(b2lo, b2hi, lq, 0)) {
                    e[cnt].sec = sec; e[cnt].pt = c; e[cnt].dir = b;
                    cnt++;
                    break;
                }
            }
            int st = (int)(sqrtf(rx * rx + ry * ry) * 0.02f);          /* 0x7851c0 */
            i += st < 1 ? 1 : st;
        }
    }
    return cnt;
}
/* 0x42c950: the car put down on a point's lane (0x5fbed0), restarted, the
   goal "Be traffic car", the TrafficMover */
static void tr_replace(AiRacer *r, Pt *p, int lane, float cap, float speed)
{
    AiWorld *w = r->w;
    const AiNet *n = w->net;
    int fwd = 1 < lane && lane < 6;
    r->skill = w->tdiff == 0 ? 9 : w->tdiff == 1 ? 4 : w->tdiff == 2 ? 0 : -1;
    float lat = lane_lat_fwd(n, p, lane);
    if (!fwd) lane -= 4;
    ctrl_setpoint(r, p);                                                /* vt+0x18 */
    lane_set(&r->ls, lane);                                             /* vt+0x20 */
    r->rdir = fwd;
    r->vcap = cap;
    r->tele_time = w->time;
    if (r->act) { act_end(r, r->act); act_free(r->act); r->act = NULL; }   /* 0x42aa20(7) */
    r->goal = GOAL_TRAFFIC;
    goal_lanes(r);
    float d[2] = { pt_dx(p), pt_dy(p) }, pos[3];
    if (!fwd) pt_backdir(n, p, d);
    pt_pos3(n, p, 0, 0, lat, pos);                                      /* 0x5dd360 */
    tr_pose(r, pos, atan2f(d[1], d[0]), speed + 0.1f, rdf(p + 8));       /* 0x5f52c0 */
    ctrl_setpoint(r, p);
    traffic_restart(r);                                                 /* vt+0x34 */
    r->tmover = N2_MOVER_TRAFFIC;
}
/* 0x42e9f0: a place `dist` metres or more from a human, walking its road
   (or one alongside, in turn) ahead of it -- or behind it half the time
   above 55 mph, or when it drives the wrong way -- in steps, 500 m at most;
   on the race route no further than the car already is on that side */
static int tr_tele_near(AiWorld *w, AiRacer *r, const AiSeen *h, float dist)
{
    const AiNet *n = w->net;
    const unsigned char *rec = w->trec;
    int same = ai_rand(w, 1.0f) < 1.0f - rdf(rec + 0x108 + w->traffic_level * 4);
    float fd = progress_diff(w, r->progress, h->progress);
    const AiRacer *hr = h->racer;
    Pt *hp = hr ? hr->rpt : NULL;                                       /* 0x5ec0b0 */
    const unsigned char *hs = hp ? sec_of(n, hp) : NULL;                /* 0x5ec0a0 */
    if (!hs || !hp) return 0;
    int ahead = 1;
    if (!(h->v->absv < TR_VMAX)) { if (ai_rand(w, 1.0f) < 0.5f) ahead = 0; }
    else if (h->wrong_way) ahead = 0;
    int dir = ahead;
    if (world_race_route(w) && (rd32(hs + 0x70) & 2)) dir = !ahead;
    TrPos e[3] = { { hs, hp, dir } };
    int cnt = tr_parallel(w, e, 3);
    w->tele_rot = (w->tele_rot + 1) % cnt;
    if (w->tele_oneway) {   /* port: the next start place running the way `same` wants */
        int k = w->tele_rot;
        for (int t = 0; t < cnt; t++, k = (k + 1) % cnt)
            if ((e[k].dir == e[0].dir) == same) { w->tele_rot = k; break; }
    }
    TrPos cur = e[w->tele_rot];
    int ok = tr_walk(w, &cur, 1, cur.pt, w->tele_first, cur.dir, 0);   /* 0x7e9720 (250) */
    if (ok != 1) return 0;
    Pt *prev = NULL, *p = cur.pt;
    for (int it = 0; ok == 1; it++) {
        if (prev == p || 99 < it) return 0;
        float dd;
        if (pt_onrace(w, p)) dd = progress_diff(w, rdf(p + 8), h->progress);
        else { float dx = pt_x(p) - h->v->pos[0], dy = pt_y(p) - h->v->pos[1]; dd = sqrtf(dx * dx + dy * dy); }
        if (500.0f < dd) return 0;                                      /* 0x783ba8 */
        int b = (cur.dir == 0) != same;
        int lane = tr_lanepick(w, p, b);
        if (lane == -1) lane = tr_lanepick(w, p, !b);
        if (lane != -1 && (pt_flags(p) >> (same ? 25 : 26) & 1) && tr_clear(w, p, dist, r)) {
            if (!r->at_end && world_race_route(w)) {
                if (!ahead) { if (fd < 0 && dd < fd) return 0; }
                else if (0 < fd && fd < dd) return 0;
            }
            tr_replace(r, p, lane, TR_VMAX, (float)p[0x2c + lane]);
            return 1;
        }
        float step = pt_seglen(p);
        if (step < dist) step = dist;
        Pt *orig = p, *qp = p;
        while ((qp == orig || ok == 0) && prev != qp) {
            prev = qp;
            ok = tr_walk(w, &cur, 1, qp, step, cur.dir, 0);
            step += step;
            qp = cur.pt;
        }
        p = qp;
    }
    return 0;
}
/* ---- drag races (0x42f7a0: drag -> 0x42e4a0 first; the sector placement
   0x42e0b0 finds nothing; no random placement in drag) ------------------ */
/* 0x5d8c90 on one section: its first point `dist` or more past `base`
   (with `pos`: 0x5d8a80, the larger of the 2-D distance from pos and the
   progress distance less 50 m) and ahead of base; a point far past it
   skips the next ones (ftol(d * -0.02) steps) */
static Pt *sec_pt_past(AiWorld *w, const unsigned char *s, const float *pos, float base, float dist)
{
    Pt *best = NULL;
    float bd = 999999.0f;
    int n = sec_npts(s);
    for (int i = 0; i < n; i++) {
        Pt *p = sec_pt(s, i);
        float e = progress_diff(w, rdf(p + 8), base), d;
        if (pos) {
            float dx = pt_x(p) - pos[0], dy = pt_y(p) - pos[1], dd = sqrtf(dx * dx + dy * dy);
            d = fabsf(e) - 50.0f;                                       /* 0x785b80 */
            if (d < dd) d = dd;
        } else d = e;
        d -= dist;
        if (0 <= d && d < bd && 0 < e) { bd = d; best = p; }
        int k = (int)(d * -0.02f);                                      /* 0x7852ac, _ftol */
        if (0 < -2 - k) i += -2 - k;
    }
    return best;
}
/* 0x417c60: over the sections with traffic lanes for the way, the point
   `dist` past `base` with the way's link flag (25 forward, 26 back) and a
   next point that way (0x409520), the nearest ahead of base */
static Pt *tr_pt_past(AiWorld *w, const float *pos, float base, float dist, int dir)
{
    const AiNet *n = w->net;
    Pt *best = NULL;
    float bd = 999999.0f;
    for (int i = 0; i < n->norder; i++) {
        const unsigned char *s = n->order[i];
        if (!s) continue;
        int d = dir;
        if (world_race_route(w) && (rd32(s + 0x70) & 2)) d = !d;
        if (!sec_haslanes(s, d)) continue;
        Pt *p = sec_pt_past(w, s, pos, base, dist);
        if (!p || !(pt_flags(p) >> (dir ? 25 : 26) & 1)) continue;
        int idx = (int)((p - s) - 0x8c) / 0x38;
        if (d ? !(idx < sec_npts(s) - 1) : !(0 < idx)) continue;
        float e = progress_diff(w, rdf(p + 8), base);
        if (0 < e && e < bd) { bd = e; best = p; }
    }
    return best;
}
/* 0x42e4a0: traffic car k (racer id 0x32 + k) at progress (k + i) x the
   record's spacing (+0x100 by way), i = 0..99 until a clear point */
static int tr_place_drag(AiRacer *r)
{
    AiWorld *w = r->w;
    if (!world_race_route(w)) return 0;
    const unsigned char *rec = w->trec;
    float sp = rdf(rec + 0x100 + (w->reversed ? 4 : 0));
    for (int i = 0; i < 100; i++) {
        float pr = 1.0f - rdf(rec + 0x108 + w->traffic_level * 4);
        int dir = ai_rand(w, 1.0f) < pr;
        Pt *p = tr_pt_past(w, NULL, 0, (float)(r->id - 0x32 + i) * sp, dir);
        if (!p) continue;
        const unsigned char *s = sec_of(w->net, p);
        if (rd32(s + 0x70) & 2) dir = !dir;
        int lane = tr_lanepick(w, p, dir);
        if (lane == -1) lane = tr_lanepick(w, p, !dir);
        if (lane == -1 || !tr_clear(w, p, sp, NULL)) continue;
        tr_setup(r, p, lane, (float)p[0x2c + lane]);
        return 1;
    }
    return 0;
}
/* 0x42e620: the point 250 m ahead of the human (by position and progress)
   for a random way; clear of cars within `dist` */
static int tr_tele_drag(AiWorld *w, AiRacer *r, const AiSeen *h, float dist)
{
    if (!world_race_route(w)) return 0;
    float pr = 1.0f - rdf(w->trec + 0x108 + w->traffic_level * 4);
    int dir = ai_rand(w, 1.0f) < pr;
    Pt *p = tr_pt_past(w, h->v->pos, h->progress, 250.0f, dir);        /* 0x7e9720 */
    if (!p) return 0;
    if (rd32(sec_of(w->net, p) + 0x70) & 2) dir = !dir;
    int lane = tr_lanepick(w, p, dir);
    if (lane == -1) lane = tr_lanepick(w, p, !dir);
    if (lane == -1 || !tr_clear(w, p, dist, r)) return 0;
    tr_replace(r, p, lane, TR_VMAX, (float)p[0x2c + lane]);
    return 1;
}
/* 0x4112a0: the human is slow (< 30 m/s) with a standing traffic car
   within 250 m of progress. In free roam every car's progress (+0x8fc) is
   0: FinishedLoadingPart1 0x57a650 -> 0x5d9a60 zeroes the route count
   (+0x2c), so UpdatePosition's 0x5d57a0(pos, 1) finds no node and 0x5d9e30
   writes 0 -- one standing traffic car stops the teleports whenever the
   player is under 30 m/s, as in the original */
static int tr_jam(const AiWorld *w, const AiSeen *h)
{
    if (!(h->v->absv < 30.0f)) return 0;                                /* 0x783bf4 */
    for (int i = 0; i < w->ntr; i++) {
        const AiRacer *t = w->tr[i];
        if (!(t->car.absv < 1.0f) || t->at_end) continue;               /* 0x783bf0 */
        if (fabsf(progress_diff(w, h->progress, t->progress)) < 250.0f) return 1;
    }
    return 0;
}
/* 0x418240: the human's teleport distance from its Paths zone (type 16) at
   the traffic level: blob [min, max] by the zone's index; 99999 none */
static float tr_tele_dist(AiWorld *w, const AiSeen *h)
{
    const unsigned char *z = w->zone ? w->zone(w->zone_user, h->v->pos) : NULL;
    if (!z || tr_jam(w, h)) return 99999.0f;
    int idx = (int)rd32(z + 0x2c + w->traffic_level * 4);
    float lo = *blob_f(w, 0xa8c + idx * 8), hi = *blob_f(w, 0xa90 + idx * 8);
    return ai_rand(w, hi - lo) + lo;
}
/* 0x42f800: a random human; (the sector placement 0x42e790 has no traffic
   sectors in the retail data) its distance -> 0x42e9f0 (0x42e620 in drag) */
static int tr_teleport(AiWorld *w, AiRacer *r, const float *dist, const AiSeen **hum, int nh)
{
    if (nh < 1) return 0;
    const AiSeen *h = hum[ai_irand(w, nh)];
    float pr = h->v->speed < 20.0f ? 0.35f : 1.0f;                      /* 0x783bd4 / 0x783bd0 */
    if (ai_rand(w, 1.0f) < pr) { /* 0x42e790: no sectors */ }
    float d = dist[0];                                                  /* one human: player 0 */
    if (!(fabsf(d - 99999.0f) > 0.001f)) return 0;
    if (w->drag || w->x890108) return tr_tele_drag(w, r, h, d);         /* 0x42e620 */
    return tr_tele_near(w, r, h, d);
}
/* 0x417bb0 (goal not ambient): moved when at its path's end or out of
   every view's reach (250 m: progress on the race route, else eye) */
static int tr_needs_move(const AiWorld *w, const AiRacer *r)
{
    if (r->at_end) return 1;
    if (w->race_state != 4) return 1;                                   /* 0x410de0 */
    if (world_race_route(w)) return !tr_view_near_prog(w, r->progress);
    return !tr_view_near_pos(w, r->car.pos);
}

/* ---- the interface ------------------------------------------------------------ */
void aitraffic_setup(AiWorld *w, const AiTrafficSetup *s)
{
    const AiNet *n = w->net;
    w->trec = s->track_rec;
    w->start_prog = s->start_prog;
    w->zone = s->zone; w->zone_user = s->zone_user;
    w->tdiff = s->tdiff;
    /* the teleporter's ctor 0x4179f0 */
    w->tele_period = (w->drag || w->x890108) ? 0.3f : 1.0f;            /* 0x783ba4 / 0x783ba0 */
    w->tele_rr = 0; w->tele_on = 1; w->tele_rot = 0;
    w->tele_first = s->tele_first > 0 ? s->tele_first : 250.0f;
    w->tele_oneway = s->tele_oneway;
    w->tele_points = 0;
    for (int i = 0; i < n->norder; i++)                                 /* 0x409900(.., 1) */
        if (sec_haslanes(n->order[i], 1)) w->tele_points += sec_npts(n->order[i]);
}
int aitraffic_count(const AiWorld *w)
{
    if (!w->trec || w->traffic_level < 1) return 0;
    int k = (signed char)w->trec[0xec + w->traffic_level * 2 + (w->reversed ? 1 : 0)];
    return k < 0 ? 0 : k > 16 ? 16 : k;
}
AiRacer *aitraffic_spawn(AiWorld *w, int slot, int k, const N2Sim *sim, float half_len, float half_wid)
{
    if (w->ntr >= 16 || !w->trec) return NULL;
    AiRacer *r = airacer_new(w, slot, -1, sim, 0, half_wid);
    if (!r) return NULL;
    w->nai--;                                                           /* not a racer in the think list */
    r->slot = -1;
    r->owner_kind = 3; r->traffic = 1; r->in_race = 0;
    r->id = 0x32 + k;                                                   /* 0x6373e0(0x32 + i, kind 3) */
    r->half_len = half_len;
    r->vcap = TR_VMAX;                                                  /* +0x79c */
    r->think_left = (float)r->id * 0.2f * -0.0454545f;                  /* +0x6f0 */
    r->car.ctrl_type = 4;
    driver_init(r->drv, r);
    r->at_end = 1; r->dead = 1;                                         /* not in the world until placed */
    if (slot >= 0 && slot < 24) w->seen[slot].kind = 3;
    if (!((w->drag || w->x890108) ? tr_place_drag(r) : tr_place_random(r))) {   /* 0x42f7a0 */
        airacer_free(r);
        return NULL;
    }
    r->tidx = w->ntr;
    w->tr[w->ntr++] = r;
    return r;
}
int aitraffic_pose(AiRacer *r, float pos[3], float *heading, float *speed)
{
    if (!r || !r->placed) return 0;
    r->placed = 0;
    memcpy(pos, r->place_pos, 12);
    *heading = r->place_head;
    *speed = r->place_speed;
    return 1;
}
int aitraffic_dead(const AiRacer *r) { return r ? r->dead : 1; }
/* diagnostics: the traffic cars standing still (log line every 5 s) */
void aitraffic_report(const AiWorld *w)
{
    int n = 0;
    for (int i = 0; i < w->ntr; i++) {
        const AiRacer *r = w->tr[i];
        if (!r || r->dead || r->car.absv >= 1.0f) continue;
        if (!n++) {
            printf("traffic standing:");
            for (int h = 0; h < w->nseen; h++)
                if (w->seen[h].v && w->seen[h].kind == 1) printf(" (player at (%.0f %.0f) v %.1f)", w->seen[h].v->pos[0], w->seen[h].v->pos[1], w->seen[h].v->absv);
        }
        printf(" [%d at (%.0f %.0f) act %d end %d dead-end %d path %d contacts %d want %.1f prog %.0f stop %d/%d",
               i, r->car.pos[0], r->car.pos[1], r->act ? r->act->type : -1, r->at_end, tr_deadend(r),
               r->L.n, r->ncontact, r->drv ? r->drv->vtarget : -1.0f, r->progress, r->finished, r->stop_why);
        for (int c = 0; c < r->ncontact; c++)
            printf(" c(%d %s j %.1f %.1fs)", r->contact[c].other, r->contact[c].car ? "car" : r->contact[c].wall ? "wall" : "poly",
                   r->contact[c].j, w->time - r->contact[c].start);
        for (int c = 0; c < r->ncontact; c++) printf(" @(%.1f %.1f)", r->contact[c].pt[0], r->contact[c].pt[1]);
        if (r->L.n > 0 && r->L.cur >= 0 && r->L.cur < r->L.n) {
            const FPoint *fc = r->L.p[(int)r->L.cur];
            Pt *pc = pt_at(w->net, fc->sec, fc->pt);
            if (pc) printf(" hd %.0f lane %d laneoff %.1f lat %.1f wl %.1f wr %.1f back %d", r->car.heading * (360.0f / 65536.0f), r->ls.lane,
                           pt_laneoff(pc, r->ls.lane), pt_latq(w->net, pc, r->car.pos, fc->back), pt_wl(pc), pt_wr(pc), fc->back);
        }
        if (r->act && r->act->type == TR_AVOID) {
            const AiSeen *o = seen_car(w, r->act->target);
            printf(" avoid car %d t %.1f h %.0f", r->act->target, r->act->t21c, r->act->t220);
            if (o && o->v) printf(" at (%.0f %.0f) v %.1f kind %d", o->v->pos[0], o->v->pos[1], o->v->absv, o->kind);
        }
        printf("]");
        if (r->L.n > 0) {   /* DIAG */
            const FPoint *f0 = r->L.p[0], *f1 = r->L.p[r->L.n - 1];
            Pt *p0 = pt_at(w->net, f0->sec, f0->pt), *p1 = pt_at(w->net, f1->sec, f1->pt);
            printf("{sec %d x70 %x on %d lanes %d%d -> sec %d pt %d x70 %x on %d}", f0->sec,
                   w->net->sec[f0->sec] ? rd32(w->net->sec[f0->sec] + 0x70) : 0, pt_onrace(w, p0),
                   sec_haslanes(w->net->sec[f0->sec], 0), sec_haslanes(w->net->sec[f0->sec], 1),
                   f1->sec, f1->pt, w->net->sec[f1->sec] ? rd32(w->net->sec[f1->sec] + 0x70) : 0, pt_onrace(w, p1));
        }
    }
    if (n) printf("\n");
}
/* the teleporter's tick 0x42f910: the intersections, then every second (one
   car, 16 in drag) the next traffic car out of reach goes near a human; a
   car with no place found is switched off */
void aitraffic_frame(AiWorld *w, float dt)
{
    isec_frame(w->net);                                                 /* 0x4111c0 */
    const AiSeen *hum[4];
    int nh = 0;
    for (int i = 0; i < w->nseen && nh < 4; i++)
        if (w->seen[i].v && w->seen[i].kind == 1 && w->seen[i].active) hum[nh++] = &w->seen[i];
    if (!w->traffic_level || w->ntr < 1 || w->race_state != 4 || nh < 1 || !w->tele_on) return;
    /* (every 5 s 0x412950 streams the traffic models: all are loaded here) */
    w->tele_period -= dt;
    if (0 < w->tele_period) return;
    w->tele_period = (w->drag || w->x890108) ? 0.3f : 1.0f;            /* 0x4098d0 */
    float dist[3] = { 999999.0f, 999999.0f, 999999.0f };
    int ntry = (w->drag || w->x890108) ? 16 : 1;                        /* 0x4018f0 */
    dist[0] = tr_tele_dist(w, hum[0]);                                  /* (0x5f0e30(-1): no such car) */
    int idx = w->tele_rr, done = 0;
    do {
        if (w->ntr <= ++idx) idx = 0;
        AiRacer *r = w->tr[idx];
        if (tr_needs_move(w, r)) {
            if (!tr_teleport(w, r, dist, hum, nh)) { traffic_off(r); r->at_end = 1; }
            done++;
        }
    } while (done < ntry && idx != w->tele_rr);
    w->tele_rr = idx;
}
