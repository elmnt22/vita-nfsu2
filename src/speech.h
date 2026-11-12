/* speech.h — the game's speech (Rachel / career lines): EA SPCH event picker +
 * MicroTalk decoder, ported from SPEED2.EXE / GC Speed.elf.
 * Data: SDATA/sdat.viv speech/UG2_Speech.{idx,evt,big} (+ nisaudio for NIS).
 * RE notes: E:/re-tools/out/speech/{SPEECH_DATA,SPCH_PICK,SPEECH_GAME}.md.
 *
 * Threads: speech_cmd/update/pause/stop/done run on the main thread; the
 * decoder runs inside the audio callback (audio_set_speech pull). The main
 * thread reads each clip's compressed bytes from the file; the audio thread
 * never does file I/O. */
#ifndef OPENUG2_SPEECH_H
#define OPENUG2_SPEECH_H

#include <stdint.h>

/* open <dataroot>/SDATA/sdat.viv and load the UG2_Speech (+ nisaudio) index
   tables (~8 KB). Registers the voice with the mixer. 0 = no speech data
   (every call below is then a harmless no-op and speech_done() is 1). */
int  speech_init(const char *dataroot);
/* a SpeechCmd* (career.h CSPK_*):
     CSPK_CAREER  a = Type_CR_Event, b = CharId        -> Career {DX 1, CR, Char}
     CSPK_IE      a = Locations,     b = CharId (or CharA | CharB << 16:
                  the game's SpeechCmdIE(loc, A, B) picks one at random)
                                                        -> Intro_Events {DX 0x10, Loc, Char}
     CSPK_IA      a = Locations,     b = CharId        -> Intro_Area {DX 0x20, Loc, Char}
     CSPK_TIMED   a = TimeProgress                     -> Time {DX 4, TP}
     CSPK_DVD     a = Dvd_Name,      b = Status        -> Career_Dvd {DX 1, CR 0x400, Name, Status}
     CSPK_MAG     a = Mag_Name,      b = Status        -> Career_Magazine {DX 1, CR 0x200, Name, Status}
     CSPK_OUTRUN  a = OR_Parms,      b = CharId        -> Outrun {DX 8, OR, Char, sub-bank}
     CSPK_FES     a = FE_Screen,     b = CharId        -> FE_Screen {DX 2, FE_Mode 1, Screen, Char}
     CSPK_FESDYNO a = Dynoshop,      b = CharId        -> FE_Dynoshop {DX 2, FE_Mode 0x20, Dyno, Char}
     CSPK_PURGE   UG2_SpeechParser::PurgeSpeech: stop the clip, clear the queues
     CSPK_SMS     ignored (not speech)
   All values are the game's Csis bit masks (1 << enum). */
void speech_cmd(int kind, uint32_t a, uint32_t b);
/* IsSpeechDone: nothing playing and nothing queued (1 without speech data) */
int  speech_done(void);
/* SpeechModule::IsStrmQueued: a stream is queued and not yet playing */
int  speech_stream_queued(void);
/* CARSFX_Rain::QueueWeatherStream: the thunder (nisaudio SoundFX_Select(4,
   variant 1 / 2)) on the speech stream; 1 = queued, 0 = speech busy / no data */
int  speech_weather_stream(int variant);
/* the crash's collision stream (SoundFX_Select(2, 8)); 1 if queued */
int  speech_collision_stream(void);
/* once per frame on the main thread: GameSpeech::Update (pick + file reads) */
void speech_update(void);
/* PauseSpeech / ResumeSpeech (the in-game pause menu) */
void speech_pause(int on);
/* purge (movies starting, quit/restart) */
void speech_stop(void);
/* seed the SPCH random generator (gSpeechSeed; speech_init seeds from the
   clock like EAXSound::Random). For tests. */
void speech_seed(int32_t seed);
/* the last pick, for logs/tests: bank id, sub, sample index (-1 none) */
void speech_last_pick(int *bank_id, int *sub, int *sample);

/* the mixer's pull: n mono s16 samples at the clip rate (22050). Returns the
   samples written (the rest is zero-filled). Audio thread only. */
int  speech_pull(void *u, int16_t *mono, int n);

/* ---- NIS audio (nisaudio.big, EA-XA R3, 32 kHz, 1-2 ch). Not wired yet.
   speech_nis_open reads clip `sample` of nisaudio bank index `bank` (0..3,
   the .idx order: 16421, 16420, 16419, 16398) into memory (main thread);
   speech_nis_pull decodes interleaved s16 frames (any thread, one user).
   Returns channels (1/2), 0 on failure; *rate gets the sample rate. */
int  speech_nis_open(int bank, int sample, int *rate);
int  speech_nis_pull(int16_t *out, int nframes);
void speech_nis_close(void);

/* ---- MicroTalk decoder, exposed for the tests. One SCHl clip in memory. */
typedef struct SpeechUTK SpeechUTK;
SpeechUTK *speech_utk_new(void);
void speech_utk_free(SpeechUTK *u);
/* bind a whole SCHl..SCEl clip (bytes stay owned by the caller) */
void speech_utk_start(SpeechUTK *u, const uint8_t *clip, uint32_t size);
/* decode up to n mono samples; returns the count (0 = end of clip) */
int  speech_utk_read(SpeechUTK *u, int16_t *out, int n);

#endif
