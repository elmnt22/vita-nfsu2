/* music.h -- EA Trax: the licensed songs streamed from SDATA/sdat.viv
 * (pfdata/MusicSFx.mus, EALayer3, 32 kHz stereo) through the PathFinder map
 * in pfdata/MusicSFx.mpf. As SPEED2.EXE plays them:
 *   - the song table at 0x7ecd80: 27 x {title, artist, album, "FE"/"IG"/"AL"};
 *   - song i is MPF event i (FUN_0047af90/0047b080 -> FUN_007399e0(h, i));
 *     the event's node chain ends at a sample node whose index - 1 is the
 *     stream in the .mus;
 *   - the playlist (profile +0x2d8, 0x83acb8): 27 x {song, mode} with mode
 *     0 off, 1 front end, 2 in game, 3 all; the default (FUN_00515aa0) is
 *     song i in slot i with the mode from its tag;
 *   - FUN_0046b6f0 picks the next song: Sequential walks the slots after the
 *     last one played for a song still in the remaining mask, Random picks
 *     the n-th remaining; an empty mask refills from the FE / IG mask
 *     (FUN_0046baa0). The first front-end song after boot is song 0
 *     (FUN_0047ac50 state 1, param -2: mask = 1).
 *   - the in-game pause pauses the song (FUN_00481210 -> state 5,
 *     FUN_00739030(h, 1)), resuming continues it.
 * The decoder runs on its own thread into a ring; audio.c mixes it. */
#ifndef OPENUG2_MUSIC_H
#define OPENUG2_MUSIC_H
#include <stdint.h>

#define MUSIC_SONGS 27

enum { MUSIC_OFF = 0, MUSIC_FE = 1, MUSIC_IG = 2 };

/* opens sdat.viv and the MPF map, starts the decoder thread; 0 on failure
   (the game then runs without music) */
int  music_init(const char *dataroot);
/* where the game is: MUSIC_FE (front end), MUSIC_IG (driving), MUSIC_OFF
   (stops). Switching playlist starts that playlist's next song. */
void music_set_mode(int mode);
/* the in-game pause: holds the song where it is */
void music_pause(int on);
/* main-thread tick: starts the next song when one ends */
void music_update(void);
/* the song now playing (-1 none) and its strings */
int  music_current(void);
const char *music_title(int song);
const char *music_artist(int song);
const char *music_album(int song);
/* bumped each time a song starts (the chyron shows it) */
int  music_song_serial(void);
/* the playlist: per slot a song and its mode (0..3), and shuffle (0
   Sequential, 1 Random); FUN_00479220 */
void music_set_playlist(const unsigned char song[MUSIC_SONGS], const unsigned char mode[MUSIC_SONGS], int shuffle);
/* the default playlist (FUN_00515aa0): slot i = song i, mode from its tag */
void music_default_playlist(unsigned char song[MUSIC_SONGS], unsigned char mode[MUSIC_SONGS]);
/* the jukebox's preview (FUN_0047af90): plays the song now; when it ends
   the playlist carries on */
void music_play_song(int song);
/* level 0..1 (the AOFEMusicVol / AOIGMusicVol slider) */
void music_set_volume(float fe, float ig);

/* audio thread: adds n frames of the song, resampled to `rate`, into the
   interleaved stereo float mix */
void music_mix(float *lr, int n, int rate);

#endif
