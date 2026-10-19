/* style.c — retail NFSU2 style points / drift scoring (see style.h). */
#include "style.h"
#include <string.h>
#include <math.h>

static StyleRec g_rec[STYLE_NMOM];
static int g_nrec;
static int g_now;                       /* 0x86518c */

static float rdf(const unsigned char *p) { float f; memcpy(&f, p, 4); return f; }
static uint32_t rdu(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* walk the bundle's chunks for id (nested containers have the top bit) */
static const unsigned char *find_chunk(const unsigned char *d, long len, uint32_t id, long *sz)
{
    long o = 0;
    while (o + 8 <= len) {
        uint32_t cid = rdu(d + o), csz = rdu(d + o + 4);
        if (o + 8 + (long)csz > len) break;
        if (cid == id) { *sz = csz; return d + o + 8; }
        if (cid & 0x80000000u) {
            const unsigned char *r = find_chunk(d + o + 8, csz, id, sz);
            if (r) return r;
        }
        o += 8 + csz;
    }
    return NULL;
}

int style_init(const unsigned char *g, long len)
{
    long sz = 0;
    const unsigned char *c = find_chunk(g, len, 0x34a07, &sz);
    g_nrec = 0;
    if (!c) return 0;
    /* 0x56c4c0: the rows start 16-aligned (chunk header + 0x17 & ~0xf) */
    long pad = (long)(((uintptr_t)(c - g) + 15) & ~(uintptr_t)15) - (long)(c - g);
    const unsigned char *r = c + pad;
    int n = (int)((sz - pad) / 0x30);
    if (n > STYLE_NMOM) n = STYLE_NMOM;
    for (int i = 0; i < n; i++, r += 0x30) {
        StyleRec *s = &g_rec[i];
        s->hash = rdu(r); s->idx = (int8_t)r[4]; s->b5 = r[5]; s->cont = r[6]; s->mask = r[7];
        s->arm = rdf(r + 8); s->timeout = rdf(r + 0xc); s->thresh = rdf(r + 0x10);
        s->cap = rdf(r + 0x14); s->maxmult = rdf(r + 0x18); s->multstep = rdf(r + 0x1c);
        s->f20 = rdf(r + 0x20); s->f24 = rdf(r + 0x24); s->base = rdf(r + 0x28);
        s->maxchain = (int8_t)r[0x2c];
    }
    return g_nrec = n;
}
const StyleRec *style_rec(int i) { return i >= 0 && i < g_nrec ? &g_rec[i] : NULL; }

void style_set_clock(int ticks) { g_now = ticks; }
int  style_clock(void) { return g_now; }

static float since(int t) { return (float)(g_now - t) * 0.00025f; }   /* x 0x784268 */
static int unset(int t) { return t == 0 || t == 0x7fffffff; }

/* ---- StyleMoment ---------------------------------------------------- */
static void m_state(StyleMoment *m, int s)                              /* 0x56d500 */
{
    if (m->state == s) return;
    if (m->state == 3) {
        m->chain++;
        if (m->mult < m->rec->maxmult) m->mult += m->rec->multstep;
    }
    if (s == 3) {
        float span = (float)(m->p4c - m->p48);
        float t = since(m->t_arm);
        if (m->best_time < t) m->best_time = t;
        if (m->best_pts < m->cur) m->best_pts = m->cur;
        if (m->best_span < span) m->best_span = span;
    }
    m->state = s;
}
static void m_fail(StyleMoment *m, int hold)                            /* 0x56d5c0 */
{
    if (m->state == 1) {
        m->cur = 0; m->raw = 0; m->hold = 0; m->state = 2;
        return;
    }
    m->hold = (uint8_t)hold;
    if (m->state != 4) {
        if (m->state == 3) {
            m->chain++;
            if (m->mult < m->rec->maxmult) m->mult += m->rec->multstep;
        }
        m->state = 4;
    }
}
static void m_bank(StyleMoment *m, int hold)                            /* 0x56d620 */
{
    m->banked += (float)(int)m->cur;       /* _ftol truncates */
    m->hold = (uint8_t)hold;
    m_state(m, 3);
}
static void m_reset(StyleMoment *m)                                     /* 0x56d6c0 */
{
    if (m->hold) { m->hold = 0; return; }
    m->cur = 0; m->raw = 0; m->t_arm = 0;
    if (m->state != 2) {
        if (m->state == 3) {
            m->chain++;
            if (m->mult < m->rec->maxmult) m->mult += m->rec->multstep;
        }
        m->state = 2;
    }
}
static void m_init(StyleMoment *m)                                      /* 0x56d650 */
{
    m->chain = 0; m->cur = 0; m->raw = 0; m->banked = 0;
    m->best_time = 0; m->best_pts = 0; m->hold = 0;
    m->p48 = m->p4c = 0; m->best_span = 0; m->best_val = 0;
    m->t_arm = 0; m->t_add = 0;
    m->mult = 1.0f; m->val = 1.0f;
    if (m->state != 2) {
        if (m->state == 3) {
            m->chain = 1;
            if (1.0f < m->rec->maxmult) m->mult = m->rec->multstep + 1.0f;
        }
        m->state = 2;
    }
}
/* 0x56c740: the running points after dt seconds more of the last value */
static float m_points(StyleMoment *m, float dt)
{
    const StyleRec *r = m->rec;
    float rate = r->base * m->val;
    float a = 1.0f * m->cur + rate * m->mult * dt;       /* 0x79da44 = 1 */
    if (r->cap > 0) { if (r->cap < a) a = r->cap; }
    else if (r->cap < 0) { if (a < r->cap) a = r->cap; }
    float b = rate * dt;
    m->raw += a < b ? a : b;
    return a;
}
static int past_thresh(const StyleRec *r, float v)
{
    return (r->base < 0 && v < r->thresh) || (r->base > 0 && r->thresh < v);
}
void style_moment_add(StyleMoment *m, float v, int p2)                  /* 0x56eab0 */
{
    const StyleRec *r = m->rec;
    if (!(r->maxchain < 0 || m->chain < r->maxchain)) return;
    if (m->state == 3 || m->state == 4) return;
    m->val = v;
    m->t_add = g_now;
    m->p4c = p2;
    if (m->best_val < m->val) m->best_val = m->val;
    if (!r->cont) {
        float p = m_points(m, 1.0f);
        if (past_thresh(r, p)) {
            m->cur = p;
            m_state(m, 0);
            m_bank(m, 0);
        }
    } else if (m->state == 2) {
        m->p48 = p2;
        m->t_arm = g_now;
        m_state(m, 1);
    }
}
static void m_tick(StyleMoment *m, float dt)                            /* 0x56e920 */
{
    const StyleRec *r = m->rec;
    float t = since(m->t_add);
    if (r->timeout < t) {
        if (m->state == 0) {
            m->banked += (float)(int)m->cur;
            m->hold = 0;
            m_state(m, 3);
        } else if (m->state == 1) {
            m_fail(m, 0);
            m_reset(m);
        }
    }
    if ((m->state == 0 || m->state == 1) && t < dt + dt)
        m->cur = m_points(m, dt);
    if (m->state == 1 && r->arm < since(m->t_arm) && past_thresh(r, m->raw)) {
        m->chain++;
        if (m->state != 0) {
            if (m->state == 3) {
                m->chain++;
                if (m->mult < r->maxmult) m->mult += r->multstep;
            }
            m->state = 0;
        }
    }
}

void style_accum_init(StyleAccum *a, int mask)                          /* 0x56ebe0 */
{
    memset(a, 0, sizeof *a);
    for (int i = 0; i < g_nrec; i++) {
        if (!(g_rec[i].mask & mask)) continue;
        StyleMoment *m = &a->store[i];
        memset(m, 0, sizeof *m);
        m->counts = 1;
        m->index = i;
        m->rec = &g_rec[i];
        m->state = 0;                     /* pool memory: m_init moves it to idle */
        m_init(m);
        m->active = 1;
        a->m[i] = m;
    }
    a->total = 0;
}

int style_accum_tick(StyleAccum *a, float dt, StyleMoment **shown, int max)   /* 0x56fd10 */
{
    int n = 0;
    a->total = 0;
    for (int i = 0; i < STYLE_NMOM; i++) {
        StyleMoment *m = a->m[i];
        if (!m || !m->active) continue;
        if ((m->state == 0 || (2 < m->state && m->state < 5)) && !m->hold && n < max && shown)
            shown[n++] = m;
        if (m->counts) a->total += (float)(int)m->banked;
    }
    for (int i = 0; i < STYLE_NMOM; i++) {
        StyleMoment *m = a->m[i];
        if (!m || !m->active) continue;
        if (m->state == 3 || m->state == 4) {
            if (!m->hold) {
                m->cur = 0; m->raw = 0; m->t_arm = 0;
                if (m->state == 3) {
                    m->chain++;
                    if (m->mult < m->rec->maxmult) m->mult += m->rec->multstep;
                }
                m->state = 2;
            } else m->hold = 0;
        }
        m_tick(m, dt);
    }
    return n;
}

void style_accum_finish(StyleAccum *a)                                  /* 0x56d710 */
{
    for (int i = 0; i < STYLE_NMOM; i++) {
        StyleMoment *m = a->m[i];
        if (!m || !m->active) continue;
        if (m->state == 0) {
            int p = (int)m->cur;
            m->hold = 1;
            m->banked += (float)p;
            m_state(m, 3);
        } else if (m->state == 1) m_fail(m, 1);
    }
}

/* ---- MiscStyleManager (drift part) ---------------------------------- */
void style_misc_init(StyleMisc *s)                                      /* 0x56cdf0 */
{
    memset(s, 0, sizeof *s);
    s->clean[0] = s->clean[1] = s->clean[2] = 1;
    s->coll_kind = -1;
}
void style_misc_collision(StyleMisc *s, StyleAccum *a, float imp, int t_made,
                          int wall, int other_traffic, int racing, float up_z)   /* 0x56fb20 */
{
    if (!(3.0f <= imp)) return;                                         /* 0x79db2c */
    if (!((float)(g_now - t_made) * 0.00025f <= 0.1f)) return;          /* 0x79db20 */
    /* (0x5a0160 || 0x592c60 || car +0x424 < 1 || car +0xb8 <= 0.9): a wall
       or a car is always one of the first two */
    (void)racing; (void)up_z;
    if (10.0f < imp) s->clean[1] = s->clean[2] = s->clean[0] = 0;      /* 0x79db1c */
    s->coll_traffic = 0;
    if (wall) s->coll_kind = 3;
    else {
        if (other_traffic) {
            float x = imp / 40.0f;                                      /* 0x79db28 */
            if (1.0f < x) x = 1.0f;
            if (a && a->m[0x10] && a->m[0x10]->active) style_moment_add(a->m[0x10], x, 0);
            s->coll_traffic = 1;
        }
        if (a && a->m[0x15] && a->m[0x15]->active) a->m[0x15]->mult = 1.0f;
        s->coll_kind = 0;
    }
    s->t_coll = g_now;
    s->coll_imp = imp;
}

/* ---- the drift scorer ----------------------------------------------- */
static const float SPD_RUN[4]  = { 13.888889f, 12.5f, 11.111111f, 8.333333f };    /* 0x79da50 */
static const float SPD_IDLE[4] = { 11.111111f, 9.722222f, 8.333333f, 6.944445f }; /* 0x79da60 */
static const float LVL_SPD[6]  = { 1.388889f, 0, 8.333333f, 11.111111f, 13.888889f, 16.666667f }; /* 0x79da70 */
static const int   LVL_PTS[17] = { 0x41855556 /* 0x79da84 = 16.67f, never read as int */,
                                   350, 1400, 4200, 11200, 300, 1200, 3600, 9600,
                                   250, 1000, 3000, 8000, 200, 800, 2400, 6400 };   /* 0x79da84 */
static const int   TITLE[7]    = { 8, 9, 10, 11, 12, 13, 14 };                      /* 0x79dac8 */
static const float DIFF[3]     = { 1.0f, 1.25f, 1.5f };                             /* 0x79dae8 */

static int lvl_pts(int lvl, int klass) { return (int)((float)LVL_PTS[lvl + klass * 4] * 1.0f + 0.5f); }

void drift_scorer_init(DriftScorer *d, StyleAccum *a, StyleMisc *m,
                       int klass, int ai, int difficulty, int human)
{
    memset(d, 0, sizeof *d);                                            /* 0x56c8f0 */
    d->level = 1; d->wheelf = 1.0f; d->rev_left = -1.0f; d->prox = 1.0f;
    d->acc = a; d->misc = m;                                            /* 0x56d7d0 */
    d->klass = klass < 0 ? 0 : klass > 3 ? 3 : klass;
    d->target = 0;
    d->next = lvl_pts(1, d->klass);
    d->diffmult = ai ? DIFF[difficulty < 0 ? 0 : difficulty > 2 ? 2 : difficulty] : 1.0f;
    d->human = human;
}

/* 0x56c940: points for the slide this frame */
static float angle_points(DriftScorer *d, const StyleCar *c)
{
    float pts = 0;
    if (0 < c->wall_contacts) return 0;
    if (d->maxdist <= c->race_dist) {
        float slip = fabsf(c->slip), v = c->speed;
        int against = 0.1f < fabsf(c->yaw_rate) && c->slip * c->yaw_rate < 0;   /* 0x79daf8 */
        StyleMoment *m0 = d->acc->m[0];
        float thr = (m0->state != 0 || against) ? SPD_RUN[d->klass] : SPD_IDLE[d->klass];
        if (unset(d->t_rev) && 5.0f * 0.0174533f < slip && thr < v) {  /* 0x79daf4 */
            float x = v / 8.333333f;                                    /* 0x79da5c */
            pts = x * x * slip;
        }
    }
    if (d->maxdist < c->race_dist) d->maxdist = c->race_dist;
    return pts;
}
/* 0x56ca80: off the plain road (two wheels or more not on ROAD) */
static float wheel_factor(const StyleCar *c)
{
    int n = 0;
    for (int i = 0; i < 4; i++) if (c->surface[i] != 1) n++;
    if (n <= 1) return 1.0f;
    float f = c->speed / (c->top_speed * 0.25f);                        /* 0x79db0c */
    if (f <= 0) return (float)n * 0.0f;
    if (1.0f < f) f = 1.0f;
    return (float)n * f;
}
/* 0x56cb50: the multiplier level */
static int level_tick(DriftScorer *d, const StyleCar *c)
{
    int lvl = d->level;
    StyleMoment *m0 = d->acc->m[0];
    d->lvl_drop = 0; d->lvl_hold = 0;
    if (1 < lvl && (!unset(d->t_rev) || c->speed < LVL_SPD[0] + LVL_SPD[lvl])) {
        d->lvl_drop = 1;
        if (unset(d->t_lvl) || 1.5f < since(d->t_lvl)) {               /* 0x79db08 */
            lvl--;
            d->t_lvl = g_now;
        }
    } else if (!unset(d->t_lvl)) {
        d->lvl_hold = 1;
        if (1.5f < since(d->t_lvl)) d->t_lvl = 0;
    } else if (lvl < 4 && c->speed < LVL_SPD[lvl + 1] - LVL_SPD[0]) {
        d->lvl_hold = 1;
        d->t_lvl = 0;
    } else {
        d->t_lvl = 0;
        if (m0->state == 0 && lvl < 5 && (float)d->next < m0->cur) {
            d->next = lvl_pts(lvl + 1, d->klass);
            lvl++;
        }
    }
    if (lvl < 5) { d->next = lvl_pts(lvl, d->klass); return lvl; }
    d->next = 0;
    return lvl;
}
/* 0x56ccd0: drift titles while running; the widest slip of this direction */
static void titles_tick(DriftScorer *d, const StyleCar *c, uint32_t *title)
{
    StyleAccum *a = d->acc;
    StyleMoment *m0 = a->m[0];
    if (m0->state == 0) {
        int k = d->titles;
        if (k < 7) {
            StyleMoment *t = a->m[TITLE[k]];
            if (t && g_rec[t->index].arm < m0->cur) {
                d->titles = k + 1;
                if (d->human && title) *title = g_rec[t->index].hash;   /* 0x4ac120 */
            }
        }
        float s = c->slip;
        if (d->maxslip == 0 ||
            (0 < s * d->maxslip && ((s < 0 && s < d->maxslip) || (0 < s && d->maxslip < s))))
            d->maxslip = s;
    } else {
        d->maxslip = 0;
        d->titles = 0;
        if (a->m[1] && a->m[1]->active) a->m[1]->mult = 1.0f;
    }
}
/* 0x56dee0: driving backwards (or the human controller's +0x248 flag)
   stops the points; 4 s later it may start again */
static void reverse_tick(DriftScorer *d, const StyleCar *c)
{
    int on = (c->human && c->ctrl_flag) || c->fwd_speed < 0;
    d->rev_left = -1.0f;
    if (!on) { d->t_rev = 0; return; }
    if (unset(d->t_rev)) d->t_rev = g_now;
    float lim = 3.0f + 1.0f;                                            /* 0x79db14 + 0x79db10 */
    float t = since(d->t_rev);
    if (lim < t) d->t_rev = 0;
    if (1.0f < t) d->rev_left = lim - t;
}
/* 0x56dca0: a hard hit in the last half second */
static float wall_tick(DriftScorer *d)
{
    StyleMisc *s = d->misc;
    float t = unset(s->t_coll) ? 99999.9f : since(s->t_coll);          /* 0x79dc18 */
    if (!(t < 0.5f)) { d->wall_latch = 0; return 0; }                   /* 0x79db04 */
    if (d->wall_latch) return 0;
    d->wall_latch = (s->coll_kind == 3 || s->coll_traffic) ? 1 : 0;
    return s->coll_imp;
}
/* 0x56e030: the tandem car (never set) */
static float tandem(const DriftScorer *d) { (void)d; return 1.0f; }
/* 0x56e0a0: drifting past traffic */
static float proximity(DriftScorer *d, const StyleCar *c, const float pos[3],
                       const StyleOther *o, int n)
{
    float f = 1.0f;
    if (d->acc->m[0]->state != 0) return f;
    for (int i = 0; i < n; i++) {
        float dx = o[i].pos[0] - pos[0], dy = o[i].pos[1] - pos[1], dz = o[i].pos[2] - pos[2];
        float r = sqrtf(dx * dx + dy * dy + dz * dz);                   /* 0x43ce10 */
        if (r < 1.0f) r = 1.0f;                                         /* 0x7fa70c */
        if (20.0f < r) r = 20.0f;                                       /* 0x7fa710 */
        float near = 1.0f - (r - 1.0f) / (20.0f - 1.0f);
        float v = c->speed;
        if (v < 2.777778f) v = 2.777778f;                               /* 0x7fa714 */
        if (44.444443f < v) v = 44.444443f;                             /* 0x7fa718 */
        float fast = (v - 2.777778f) / (44.444443f - 2.777778f);
        f += near * (fast * (1.0f - 0.0f) + 0.0f);                      /* 0x7fa71c/0x7fa720 */
    }
    return f;
}

float drift_scorer_tick(DriftScorer *d, const StyleCar *c, const float pos[3],
                        const StyleOther *traffic, int ntraffic, float dt,
                        uint32_t *title)                                /* 0x56ed40 */
{
    StyleMoment *m0 = d->acc->m[0];
    (void)dt;                     /* the parts take it but none reads it */
    if (title) *title = 0;
    if (!m0) return 0;
    titles_tick(d, c, title);
    reverse_tick(d, c);
    float ang = angle_points(d, c);
    int lvl = level_tick(d, c);
    d->wheelf = wheel_factor(c);
    float hit = wall_tick(d);
    float tan = tandem(d);
    float prox = proximity(d, c, pos, traffic, ntraffic);
    d->prox = prox;
    d->angle_pts = ang;
    float pts = d->diffmult * d->wheelf * prox * 1.0f * tan * ang;      /* 0x79dae4 */
    if (0.2f < hit && d->wall_latch) {                                  /* 0x79dafc */
        if (m0->active && (m0->state == 0 || m0->state == 1)) m_fail(m0, 1);
        if (5.0f < hit) d->misc->clean[1] = d->misc->clean[2] = d->misc->clean[0] = 0;   /* 0x79db00 */
        d->level = 1;
        return 0;
    }
    if (0 < pts) {
        m0->mult = (float)lvl;
        style_moment_add(m0, pts, 0);
        if (m0->state == 0) {
            float s = c->slip;
            if (s * d->maxslip < 0) {
                d->level = lvl;
                if (0.4f * 0.6666667f < fabsf(s)) d->maxslip = s;       /* 0x79db18 x 0x78d898 */
                return pts;
            }
        }
    }
    d->level = lvl;
    return 0 < pts ? pts : 0;
}

/* ---- collision pairs ------------------------------------------------- */
StylePair *style_pairs_hit(StylePairs *s, int other, float j)
{
    StylePair *p = NULL;
    for (int i = 0; i < s->n; i++) if (s->p[i].other == other) { p = &s->p[i]; break; }
    if (!p) {
        if (s->n >= 8) return NULL;
        p = &s->p[s->n++];
        p->other = other; p->t_made = g_now; p->imp = 0;
    }
    p->t_last = g_now;
    p->imp += j;
    return p;
}
void style_pairs_tick(StylePairs *s)
{
    int n = 0;
    for (int i = 0; i < s->n; i++)
        if ((float)(g_now - s->p[i].t_last) * 0.00025f < 0.3f) s->p[n++] = s->p[i];   /* 0x7fbee0 */
    s->n = n;
}
int style_pairs_world(const StylePairs *s)
{
    int n = 0;
    for (int i = 0; i < s->n; i++) if (s->p[i].other < 0) n++;
    return n;
}

/* ---- the race entry side -------------------------------------------- */
float drift_score(const StyleAccum *a) { return a->m[0] ? a->m[0]->banked : 0; }

void drift_laps_tick(DriftLaps *l, float score, int maxlap, int laps, int lap_done)   /* 0x5f7a00 */
{
    int i = maxlap;
    if (!lap_done) { i = maxlap + 1; if (laps <= i) i = laps; }
    if (i < 21 && l->lap[i] < score - l->at_lap) l->lap[i] = score - l->at_lap;
    if (lap_done) l->at_lap = score;
}

/* 0x7fa5c0: event, then [class 0..3][difficulty 0..2] */
static const struct { int ev; float s[12]; } DOWNHILL[6] = {
    { 4174, { 40000, 60000, 80000, 34000, 50000, 66000, 27000, 40000, 53000, 20000, 30000, 40000 } },
    { 4175, { 60000, 150000, 250000, 53000, 130000, 220000, 46000, 110000, 185000, 40000, 90000, 150000 } },
    { 4176, { 75000, 112000, 150000, 65000, 97000, 130000, 55000, 82000, 110000, 45000, 67000, 90000 } },
    { 4177, { 100000, 150000, 200000, 86000, 130000, 170000, 73000, 110000, 145000, 60000, 90000, 120000 } },
    { 4178, { 120000, 260000, 400000, 106000, 225000, 350000, 93000, 195000, 300000, 80000, 160000, 250000 } },
    { 4179, { 100000, 200000, 300000, 91000, 175000, 260000, 83000, 150000, 220000, 75000, 125000, 180000 } },
};
int drift_downhill_event(int ev) { return 0x104e <= ev && ev <= 0x1053; }
float drift_downhill_score(int ev, int klass, int diff, int racer_id)  /* 0x56d850 */
{
    float s = 0;
    for (int i = 0; i < 6; i++)
        if (DOWNHILL[i].ev == ev) { s = DOWNHILL[i].s[klass * 3 + diff]; break; }
    if (racer_id == 101) s *= 0.95f;                                    /* 0x79da48 */
    else if (racer_id == 102) s *= 0.9f;                                /* 0x79da4c */
    return s;
}
