/* airace.h — who races and where they start, ported from SPEED2.EXE (v1.2).
 *
 * The opponents of a race outside the career are made by the quick race
 * setup (0x53f150 -> 0x53ee90): names drawn from the game's 53, each car a
 * random one of the racer list that is no faster than the player's (stock
 * level within 0.06, the tolerance doubling each pass, 0x4feaf0), then
 * fitted with performance parts to the player's rating and handling and
 * the player's nitrous level (0x532010 -> 0x5b7570), skill drawn from the
 * difficulty's distribution (0x4fed00). The grid (0x5eb5d0) is the event's
 * Forward/BackwardStartingPosition markers by index; who gets which index
 * is the race's start order (0x5264a0: quick race circuit/sprint puts the
 * humans first, the AI after, then swaps the player with a random place). */
#ifndef OPENUG2_AIRACE_H
#define OPENUG2_AIRACE_H

#include "n2sim.h"
#include "perfparts.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the car types: GLOBALB chunk 0x34600, one N2_REC_SIZE record each, in
   car type order (0x8a1ccc) */
int  airace_types_init(const unsigned char *globalb, long len);
int  airace_ntypes(void);
int  airace_type_of(const char *carname);
const unsigned char *airace_type_rec(int type);
const char *airace_type_name(int type);
int  airace_type_suv(int type);               /* record +0x88a */

/* the race table, GLOBALB chunk 0x34a11 (136-byte records: +0xf race type,
   +0x18 u16 event, +0x34 2 = URL); returns the record count */
int airace_races_init(const unsigned char *globalb, long len);
enum { AIRACE_CIRCUIT = 0, AIRACE_SPRINT = 1, AIRACE_STREETX = 2, AIRACE_DRAG = 4,
       AIRACE_DRIFT = 5, AIRACE_URL = 6 };
/* the event's race type, -1 when no record names it */
int airace_event_type(int event);

#define AIRACE_MAX 8
typedef struct {
    char  name[16];               /* racer name (0x7f6cd0) */
    int   id;                     /* racer id 100.. */
    int   type;                   /* car type */
    char  car[32];                /* CARS\<car> */
    int   skin;                   /* DUMMY_SKIN<n> (0x61c280(id - 0x62)) */
    int   skill;                  /* 0..9 */
    int   block;                  /* block level */
    int   start;                  /* start position, 1-based (racer info +3) */
    PerfParts parts;
    unsigned char rec[N2_REC_SIZE];   /* the built physics record */
    float rating, handling;
    int   klass;                  /* PerfConfig +4 */
} AiOpponent;

/* the game's random (0x43c1c0 int in [0, n), 0x43c1f0 float in [0, x)) */
typedef struct { unsigned s; } AiRng;
int   airng_int(AiRng *r, int n);
float airng_float(AiRng *r, float x);

/* 0x412be0: the car type of each of n traffic racers for the region (the
   Paths zone id at the start); out[k] -1 when no type had quota left.
   Returns n (0 without traffic types). */
int airace_traffic_types(int region, int n, AiRng *rng, int *out);

/* 0x4fe970: n distinct racer names (n <= 53) */
void airace_names(AiRng *rng, const char **out, int n);

/* 0x53ee90 + 0x532010: n quick race opponents for the player's car type
   and built record (its parts in pp), difficulty 0..2, the AI tuning blob
   (GLOBALB 0x34b00). Returns the count made. */
int airace_quick(int n, int player_type, const unsigned char *player_built,
                 const PerfParts *player_parts, int difficulty,
                 const unsigned char *blob, AiRng *rng, AiOpponent *out);

/* RaceStarter::AddCareerEventAIOpponents 0x5260b0 (GC 8011a22c): a career
   event's opponents from its entries (CareerEventData +0x40, 12 B each):
   the name, perf mode (0 fixed level, 1 match the player's car, 2 as it
   comes, 3 the stage's GetAIPerformanceMatchingParameter), level, car
   (type -1 = "RANDOM", else the preset's car type), skill by the career
   difficulty and block level. suv_event = event +0xd. */
typedef struct {
    const char *name;
    int mode, level, type, skill, block;
} AiCareerSlot;
int airace_career(int n, const AiCareerSlot *slot, int suv_event, float career_level,
                  const unsigned char *player_built, const PerfParts *player_parts, int drag,
                  AiRng *rng, AiOpponent *out);

/* 0x5264a0: start positions. mode 1 humans first, 2 AI first, 3 humans
   first then the (single) human swapped with a random racer. pos[0..nh)
   are the humans, pos[nh..nh+na) the AI; filled 1-based. */
void airace_start_order(int nh, int na, int mode, AiRng *rng, int *pos);

/* 0x5eb5d0: the event's start slots from ROUTES<x>/TrackPosMarkersAll.bin
   (chunk 0x34146, 48-byte markers: +8 type hash, +0xc index, +0x10 pos,
   +0x20 u16 heading, +0x24 event). Slot k = the marker with index k.
   Returns the number of slots filled (0..max); heading in radians. */
int airace_grid(const char *trackpos_path, int event, int backward,
                float (*pos)[3], float *heading, int max);

/* 0x5d8660(type, 0): the event's first marker of a type (e.g.
   "ForwardStartLine") -- its position and raw heading (u16 units); 0 when
   there is none */
int airace_marker(const char *trackpos_path, int event, const char *type,
                  float pos[3], unsigned *heading16);

unsigned airace_hash(const char *s);          /* 0x43db50 */

#ifdef __cplusplus
}
#endif
#endif
