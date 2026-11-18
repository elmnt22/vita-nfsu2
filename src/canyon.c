/* canyon.c -- Carbon's canyon duel scoring (NFSC.exe 0x62bd50 and helpers; see canyon.h) */
#include "canyon.h"
#include <string.h>

/* zone 0 ahead (ZoneDistanceAhead / ZoneMultiplierAhead), 1..6 ZoneDistanceN / ZoneMultiplierN,
   7 behind (ZoneDistanceBehind / ZoneMultiplierBehind): gpcore class canyon */
static const float k_dist[CANYON_NZONE] = { 0.0f, 16.0f, 30.0f, 45.0f, 80.0f, 100.0f, 110.0f, 120.0f };
static const float k_mult[CANYON_NZONE] = { 3.0f, 2.0f, 1.5f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f };
#define CANYON_RATE 1000.0f     /* tuning +0x4c (0x616750: 1000) */
#define CANYON_TICK 0.1f        /* 0x9c27b0 */

float canyon_zone_dist(int z) { return z < 0 ? 0.0f : z >= CANYON_NZONE ? k_dist[CANYON_NZONE - 1] : k_dist[z]; }
float canyon_zone_mult(int z) { return z < 0 || z >= CANYON_NZONE ? 0.0f : k_mult[z]; }
int canyon_chaser(const Canyon *c) { return c->leg == 2 ? 1 : 0; }

void canyon_begin(Canyon *c, int leg)
{
    float keep[2] = { c->score[0], c->score[1] }, k1[2] = { c->leg1[0], c->leg1[1] };
    memset(c, 0, sizeof *c);                         /* CanyonRaceClear 0x6202c0 */
    c->leg = leg == 2 ? 2 : 1;
    c->zone = 1;                                     /* +0x28 = 1 */
    c->maxdist = -1e9f;
    if (c->leg == 2) {                               /* CanyonRaceSetup(.., firstPass 0): the points stay */
        c->score[0] = keep[0]; c->score[1] = keep[1];
        c->leg1[0] = k1[0]; c->leg1[1] = k1[1];
    }
}

void canyon_overboard(Canyon *c, int who)
{
    if (c->state != CANYON_RACING) return;
    c->state = CANYON_OVERBOARD;
    c->overboard_who = who;
}

/* 0x6201d0: the speed factor, mph (x 2.237) */
static float speedf(float ms)
{
    float mph = ms * 2.237f;
    if (mph < 30.0f) return 0.0f;
    if (mph < 70.0f) return (mph - 30.0f) * 0.025f;
    return (mph - 70.0f) * 0.0125f + 1.0f;
}

int canyon_update(Canyon *c, float dt, float lead_m, float chaser_dist, float chaser_speed)
{
    if (c->state != CANYON_RACING) return c->state;
    int ch = canyon_chaser(c), ld = 1 - ch;
    int forward = chaser_dist >= c->maxdist;         /* 0x6200e0: at its furthest so far */
    if (forward) c->maxdist = chaser_dist;
    /* 0x6166a0: the first sample fills the window, then the mean of the last four */
    if (!c->primed) { for (int i = 0; i < 4; i++) c->gs[i] = lead_m; c->primed = 1; c->gi = 0; }
    else { c->gs[c->gi] = lead_m; c->gi = (c->gi + 1) & 3; }
    c->gap = (c->gs[0] + c->gs[1] + c->gs[2] + c->gs[3]) * 0.25f;
    int z = CANYON_NZONE - 1;                        /* 0x6167e0: the first zone the gap is inside */
    for (int i = 0; i < CANYON_NZONE; i++) if (c->gap < k_dist[i]) { z = i; break; }
    c->zone = z;
    c->zone_frac = z == 0 || z == CANYON_NZONE - 1 ? 1.0f
                 : (c->gap - k_dist[z - 1]) / (k_dist[z] - k_dist[z - 1]);
    if (z != 0) c->ztime[0] = 0.0f;
    if (z != CANYON_NZONE - 1) c->ztime[CANYON_NZONE - 1] = 0.0f;
    if (c->leg == 2 && c->score[ld] <= c->score[ch]) {   /* the opponent caught the player's points */
        c->state = CANYON_CAUGHT;
        return c->state;
    }
    c->tick += dt;
    if (c->tick < CANYON_TICK) return c->state;
    c->tick -= CANYON_TICK;                          /* one tick a frame at most, as 0x62bd50 */
    c->last_pts = 0.0f;
    if (forward) {
        float p = speedf(chaser_speed) * k_mult[z] * CANYON_RATE * CANYON_TICK;
        c->score[ch] += p;                           /* 0x616850: never below 0 */
        if (c->score[ch] < 0.0f) c->score[ch] = 0.0f;
        c->last_pts = p;
    }
    c->ztime[z] += CANYON_TICK;
    if (c->ztime[0] > CANYON_PASS_TIME) c->state = CANYON_PASSED;
    else if (c->ztime[CANYON_NZONE - 1] > CANYON_LOSE_TIME) c->state = CANYON_LOST;
    return c->state;
}
