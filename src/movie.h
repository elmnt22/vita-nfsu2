/* movie.h - EA movie player core (the .vp6 files in MOVIES): VP6 video + EA-XA R3 audio.
 *
 * The game's FMVs are EA chunk streams (MVhd/SCHl/MV0K/MV0F/SCDl/...),
 * video is On2 VP6.0 (sub-version 7, simple profile, stored bottom-up; the
 * planes returned here are already top-down), audio is
 * EA-XA "R3" ADPCM at 32 kHz stereo. Decoding is integer-only and allocation
 * free after movie_open(). Not thread-safe except for the audio ring:
 * movie_audio() may be called from the mixer thread while another thread
 * calls movie_next_frame() (single producer / single consumer). */
#ifndef MOVIE_H
#define MOVIE_H

#include <stdint.h>

typedef struct Movie Movie;

/* Opens a movie and parses its headers. The file is streamed chunk by
 * chunk; nothing big is read up front. NULL if the file is missing or not
 * an EA VP6 movie. */
Movie *movie_open(const char *path);

/* Display size, frame rate, frame count (from MVhd) and audio format
 * (rate/channels are 0 when the movie has no audio, e.g. blank.vp6).
 * Any out pointer may be NULL. Returns 1. */
int movie_info(Movie *m, int *w, int *h, float *fps, int *frames,
               int *rate, int *channels);

/* Decodes the next video frame. Any audio chunks met on the way are
 * decoded into the audio ring. Planes are YUV 4:2:0, top row first
 * (ystride >= w, uvstride >= w/2, chroma planes are w/2 x h/2).
 * The pointers stay valid until the next call. Returns 1 for a new
 * frame, 0 at the end of the movie. A frame that fails to decode is
 * reported as a repeat of the previous picture (see movie_errors).
 * Playback is over when this returned 0 AND movie_audio_buffered() is 0:
 * the tutorial movies carry ~3 s of audio after their last frame (keep
 * showing the last picture meanwhile). */
int movie_next_frame(Movie *m, const unsigned char **y, const unsigned char **u,
                     const unsigned char **v, int *ystride, int *uvstride);

/* Pulls up to max_frames interleaved s16 PCM frames (channels samples
 * each) decoded so far. Returns the number of frames written. Audio is
 * decoded as a side effect of movie_next_frame(); it arrives roughly one
 * video frame's worth (1280-1400 samples) per frame, slightly behind the
 * video, so the mixer should keep ~3 frames of latency or use the played
 * audio position as the clock for when to fetch the next picture. The
 * ring holds 4 s; audio not pulled in time is dropped. */
int movie_audio(Movie *m, int16_t *out, int max_frames);

/* PCM frames currently waiting in the audio ring. */
int movie_audio_buffered(Movie *m);

/* Frames decoded so far / decode errors seen so far (0 on a clean file). */
int movie_frame_index(Movie *m);
int movie_errors(Movie *m);

void movie_close(Movie *m);

#endif
