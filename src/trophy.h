/* trophy.h -- trophies modelled on the RetroAchievements PS2 set for NFSU2
 * (game 5613, 76 achievements by BigWeedSmokerMan). RA conditions read PS2
 * memory, so each one is re-implemented here from the career state; the
 * mapping is in re-tools/out/TROPHIES.md.
 *
 * Nearly every condition is read straight from the career save (event /
 * shop / showcase / world challenge records, the stage infos), so
 * trophy_check() re-evaluates all of them at each hook and an imported or
 * older save catches up by itself. The few that are moments, not state (a
 * Hard finish, a drift, neon), unlock at their hook. A missable one is
 * evaluated only while its stage is still the current one.
 *
 * Unlocked trophies are kept as bits in the profile (block TRPH). With
 * trophies off (the default: no trophies.txt, set in the LiveArea
 * Settings) progress is still kept, but nothing is reported to the system; switching them on
 * reports what was earned at the next profile load.
 *
 * Optional: none of this changes the game. */
#ifndef OPENUG2_TROPHY_H
#define OPENUG2_TROPHY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TROPHY_N 76

#ifdef __vita__
#define TROPHY_ON_FILE "ux0:data/NFSU2/trophies.txt"   /* opt-in: off without it */
#else
#define TROPHY_ON_FILE "trophies.txt"
#endif

typedef struct { uint32_t ra_id; const char *title; } TrophyInfo;
const TrophyInfo *trophy_info(int i);          /* 0..TROPHY_N-1 in RA display order */

void trophy_init(void);                         /* reads the switch file; at boot */
int  trophy_enabled(void);
int  trophy_unlocked(int i);
int  trophy_count(void);
/* the system side (step 4: sceNpTrophy). NULL = log only. Called once per
   unlock, and again for every saved bit by trophy_report_all. */
extern void (*trophy_backend)(int i);
void trophy_report_all(void);                   /* after a profile load */

/* the profile block */
long trophy_save(unsigned char *out, long cap);
int  trophy_load(const unsigned char *p, long n);
void trophy_default(void);

/* hooks (career.c / main.c) */
void trophy_check(void);                        /* re-evaluate the career state */
void trophy_stage_cleared(int stage);           /* NotifyStageCleared, before the stage advances */
void trophy_event_won(const unsigned char *ev); /* CommitWinEvent */
void trophy_race_start(void);
void trophy_drift_bank(float points);           /* one drift banked by the player */
/* the post-race: won = 1st (a drift race by score); neon = the player's car
   has underglow; ev = the career event raced, NULL outside the career */
void trophy_race_end(int won, int neon, const unsigned char *ev);

#ifdef __cplusplus
}
#endif
#endif
