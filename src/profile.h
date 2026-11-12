/* profile.h -- the player profile (SPEED2.EXE UserProfile, 0x502f40..) and
 * its save files, as the PC game keeps them (realmc on the hard disk).
 *
 * A profile is a name (up to 7 characters, no spaces: MC_List lists only
 * those) and the game state that goes with it: the options (with the save
 * prompt / autosave flags 0x844ad8 / 0x844ad9), the garage (every car's
 * customization) and the high-score table (HighScoresDatabase 0x844adc).
 * The working copies of these are the game's own files (options.bin,
 * garage.txt, scores.bin); a profile save packs them into
 * ux0:data/NFSU2/profiles/<NAME>/<NAME> (the PC's
 * %LOCALAPPDATA%\NFS Underground 2\<NAME>\<NAME>) with the realmc
 * container: "20CM" header, sizes, MSB-first CRC-32s (0x6fcd1c).
 *
 * Unnamed ("No Profile", 0xcf4f80c2) the working files still persist,
 * autosave stays off (0x49b170 offers it only when named).
 */
#ifndef OPENUG2_PROFILE_H
#define OPENUG2_PROFILE_H

#ifdef __vita__
#define PROFILE_GARAGE_FILE "ux0:data/NFSU2/garage.txt"
#define PROFILE_DIR_PATH "ux0:data/NFSU2/profiles"   /* = profile.c's PROFILE_DIR */
#else
#define PROFILE_GARAGE_FILE "garage.txt"
#define PROFILE_DIR_PATH "profiles"
#endif

#define PROFILE_NAME_MAX 7          /* FUN_00555f80(.., 7, ..); MC_List: < 8 chars */
#define PROFILE_LIST_MAX 64

void profile_init(void);            /* the save dir (0x4a9c40) and the scores */
int  profile_named(void);           /* IsProfileNamed 0x502fe0 */
const char *profile_name(void);     /* "" while unnamed */
int  profile_file(const char *file, char *out, int cap);   /* a file beside the profile (0 while unnamed) */
void profile_set_name(const char *name);   /* SetProfileName 0x502f40 (empty: unnamed) */

/* the profiles on disk, sorted; names[i] has PROFILE_NAME_MAX + 1 chars */
int  profile_list(char names[][PROFILE_NAME_MAX + 1], int cap);
int  profile_exists(const char *name);
/* write the working set as the current (named) profile; 1 ok */
int  profile_save(void);
/* read a profile into the working files (options.bin, garage.txt,
   scores.bin) and g_opt, and name it; 1 ok */
int  profile_load(const char *name);
/* remove a profile; deleting the current one unnames and defaults (0x4c6330) */
int  profile_delete(const char *name);
/* 0x5346f0: the profile's game state to its defaults (the garage, the
   scores), unnamed; the options stay */
void profile_default(void);

/* AllocBackupDB 0x52a460 / IsDirty 0x52a4c0 / RestoreFromBackupDB 0x5347c0 */
void profile_snapshot(void);
int  profile_dirty(void);           /* the working set differs from the snapshot */
void profile_restore(void);         /* back to the snapshot (cancel) */
void profile_drop_snapshot(void);   /* DeallocBackup 0x5032c0 */

/* High scores (HighScoresDatabase, 320 entries {short track, char, char
   direction, int value}): the track is the track record's number (+0x8a),
   times in 1/4000 s. */
enum { HS_LAP, HS_DRIFT };
/* FindBestLapTime 0x502aa0: the best time on record, else the track's
   default (record +0x9c forward / +0xa0 reverse, ms) */
int  hs_best_time(int track, int reverse, const unsigned char *track_rec);
int  hs_best_drift(int track, int reverse);   /* FindBestDriftScore 0x502c50 */
/* MaybeAddTrackHighScore 0x801314fc: a finished race's time (or drift
   score); 1 a new record */
int  hs_commit(int kind, int track, int reverse, int value);

#endif
