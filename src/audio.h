/* audio.h — OpenUG2 Audio module: a procedural engine/road/skid/thud synth
 * (no audio assets). The main thread writes the g_* controls, the SDL audio
 * thread reads them — benign float races. */
#ifndef OPENUG2_AUDIO_H
#define OPENUG2_AUDIO_H

#include <SDL.h>
#include <stdint.h>

/* --- engine synth: 3 RPM bands (idle/powerband/scream) crossfaded by RPM,
 * timbre + volume shaped by load. All bands pitch-track the same engine
 * fundamental (base_freq * rpm/center); what crossfades is the harmonic mix. */
#define ENG_IDLE_RPM      1000.0f
#define ENG_REDLINE_RPM   8000.0f
#define ENG_UPSHIFT_RPM   7000.0f
#define ENG_DOWNSHIFT_RPM 3200.0f
#define ENG_SHIFT_S       0.15f     /* clutch/shift-cut window, seconds */
#define ENG_GEARS         6

typedef struct {
    float base_freq;      /* fundamental at this band's center RPM */
    float phase;          /* phase accumulator (audio thread) */
    float volume;         /* rpm-weight * load mix (audio thread) */
} AudioBand;

/* Ginsu-style sample band: a decoded PCM loop from the game's own .abk
 * engine bank, pitch-shifted by a fractional read cursor. */
typedef struct {
    int16_t *samples;       /* decoded 16-bit mono PCM loop */
    uint32_t sample_count;
    float playback_index;   /* fractional read cursor (audio thread) */
    float base_freq;        /* estimated fundamental of the recording, Hz */
    float srate;            /* recording sample rate, Hz */
    float volume;           /* rpm-weight * accel/decel mix (audio thread) */
} AudioSampleBand;

/* A decoded Gnsu20 RPM sweep (SOUND/ENGINE/GIN_*.gin): one continuous
 * recording of the engine revving rpm_min -> rpm_max, with the file's own
 * rpm->sample-position curve and cycle-aligned grain (loop point) table. */
typedef struct {
    int16_t *samples;
    uint32_t total_samples;
    float rpm_min, rpm_max;
    uint32_t sample_rate;
    uint32_t *rpm_pos;      /* n_rpm+1 sample positions at evenly spaced RPMs */
    int n_rpm;
    uint32_t *grain;        /* n_grain+1 ascending cycle-aligned loop points */
    int n_grain;
} GinsuSweep;

typedef struct {
    GinsuSweep accel_sweep, decel_sweep;
    float play_cursor[2];   /* per-sweep looping cursor (accel, decel) */
    int   grain_idx[2];     /* per-sweep current grain window */
    float grain_rpm[2];     /* recorded rpm at the current grain (cached) */
    /* sub-grain crossfade: a secondary cursor sprouts at the rpm-target
       grain and fades in over ~12ms, then becomes the primary */
    int   xf_on[2], xf_grain[2];
    float xf_cursor[2], xf_rpm[2], xf_t[2];
    int   decel_is_alias;   /* decel_sweep shares accel's buffers (no _DCL file) */
    int   has_gin_data;
} Ginsu20State;

typedef struct {
    float current_rpm;              /* audio thread: smoothed toward target */
    volatile float target_rpm;      /* main thread writes */
    volatile float load;            /* main thread writes: 0 coast .. 1 full throttle */
    float current_load;             /* audio thread: smoothed load */
    AudioBand bands[3];             /* low / mid / high (procedural fallback) */
    AudioSampleBand accel_bands[3]; /* .abk Ginsu loops under load */
    AudioSampleBand decel_bands[3]; /* .abk Ginsu loops on coast */
    AudioSampleBand idle_band;      /* stationary loop, blended out by ~2200rpm */
    Ginsu20State gin;               /* native Gnsu20 sweeps (preferred) */
    int sample_mode;                /* 1 = .abk loops loaded, 0 = synth fallback */
    volatile float master_volume;   /* main thread writes */
} EngineSynthState;

extern EngineSynthState g_engine;
extern volatile float g_road_vol;   /* tyre/wind noise, scales with speed */
extern volatile float g_hit;        /* collision thud amplitude (main sets, cb decays) */
extern volatile float g_skid;       /* tyre-screech volume while drifting */
/* the retail skid controller's channels (wheelspin, cornering, slide/brake),
   0..1, played by FX_SKID's pairs; used instead of g_skid while g_skid3_on */
extern volatile float g_skid3[3];
extern volatile int g_skid3_on;
extern volatile int g_turbo_fitted; /* 0 mutes turbo whine + blow-off (main sets) */
extern volatile float g_turbo_boost; /* turbo pressure 0..1 (main sets; drives the whine) */
extern volatile float g_speed_frac;  /* |speed| / top speed 0..1: road + wind loops */
extern volatile int g_nitro;         /* 1 while nitrous is burning */
extern volatile int g_purge;         /* 1 while nitrous is purged (held at a standstill, 0x5ecdb0) */

/* RPM crossfade weights for the 3 bands (pure function, used by the synth
 * and the selftest). */
void eng_band_weights(float rpm, float w[3]);

/* 6-speed virtual gearbox: per-frame step. speed_frac = |speed|/top (0..1),
 * dt in seconds. Advances *gear / *shift_t and writes g_engine.target_rpm +
 * .load; during a shift the load is cut and revs sag toward idle. */
void eng_gearbox_step(float speed_frac, float throttle, float dt,
                      int *gear, float *shift_t);
/* the retail sim (n2sim) shifted: +1 up, -1 down, for the shift sound */
void eng_shift_event(int dir);
/* Manual gearbox: the same ratios, but the driver shifts. Gears run
 * R (-1) <-> N (0) <-> 1..6; shift_req is +1/-1 for one step. Refused: past
 * 6th, a downshift that would put the revs over redline + 300, and N -> R
 * above ~7 km/h. In N the engine free-revs on the throttle; R uses 1st's
 * ratio. At the limiter the revs bounce. */
void eng_gearbox_manual(float speed_frac, float throttle, float dt,
                        int *gear, float *shift_t, int shift_req);
/* rpm the gearbox would show in `gear` at speed_frac */
float eng_gear_rpm(float speed_frac, int gear);
/* Manual drivetrain: multiplier on throttle for the physics. Weak low in
 * the revs, full from 4000-7500, fading to the limiter; 0 at the limiter
 * (gears 1-5) and during the 0.15 s shift cut. 1st slips the clutch off the
 * line (treated as >= 4000 rpm). */
float eng_manual_thrust(float speed_frac, int gear, float shift_t);

/* Pick an engine bank (0..40) for a car folder name. Deterministic name
 * hash — every car gets a stable, distinct voice. NOT the game's authentic
 * assignment: no car->bank table exists in the data; the real per-car
 * recordings are the named Gnsu20 files (SOUND/ENGINE/GIN_*.gin). */
int audio_bank_for_car(const char *carname);

/* Load the game's engine sound bank (SOUND/ENGINE/CAR_%02d_ENG_MB_SPU.abk)
 * into g_engine's idle/accel/decel bands, freeing any previous bank. Call
 * BEFORE audio_init (no locking). Returns loops decoded (7), 0 = synth. */
int audio_load_engine_bank(const char *dataroot, int carbank);

/* Load the car's native Gnsu20 sweeps: scans SOUND/ENGINE for GIN_*.gin
 * whose filename contains the car folder name (accel) and its _DCL/Decel
 * variant (decel; falls back to the accel sweep if absent). Call BEFORE
 * audio_init. Returns sweeps loaded (1-2), 0 = none (use the .abk bank). */
int audio_load_ginsu_sweeps(const char *dataroot, const char *carname);

/* Game sample banks for effects (SOUND/SHIFTING, SOUND/SKIDS): shift
 * one-shots follow the car (reload on car switch, inside audio_hold), the
 * pavement skid loop is global. Return 1 when loaded. */
int audio_load_shift_bank(const char *dataroot, const char *carname);
int audio_load_skid_bank(const char *dataroot);
/* IG_GLOBAL/MB_Stich_Collision.abk: a few heavy crunches and light knocks
 * (picked by measured length/brightness; the bank carries no labels). */
int audio_load_collision_bank(const char *dataroot);
/* SOUND/TURBO bank for the car: the same per-car audio row SPEED2.EXE uses
 * (car type -> row, row -> turbo bank at stock level), clip 0 = whine loop,
 * 1-3 = blow-off; carsfx.c plays it (set AFX_PTURBO). Reload on car switch
 * (inside audio_hold). Returns clips. */
int audio_load_turbo_bank(const char *dataroot, const char *carname);
/* the loaded bank's 0x7f3338 record index (carsfx.c k_turbo_rec), -1 none */
int audio_player_turbo_rec(void);
/* The clips the game's FX_ROADNOISE, FX_WIND and FX_NITROUS events play. Global; load once before audio_init. Returns clips loaded. */
int audio_load_env_banks(const char *dataroot);
/* The other cars' engines (carsfx.c sets them once a frame): voices
 * 0..7 are the AI racers (CARSFX_AIEngine: an SPU engine bank's idle +
 * accel/decel loops, band-mixed like the player's .abk path), 8..13 the
 * traffic (FX_TRAFFIC's loop). gain is relative to the player's engine. */
#define AUDIO_CARVOICES 14
typedef struct {
    volatile int   on;
    volatile int   bank;     /* AI: loaded bank 0..2; -1 = the traffic loop */
    volatile float rpm;      /* ENG_IDLE_RPM..ENG_REDLINE_RPM band-mixer scale */
    volatile float load;     /* 0 coast .. 1 full throttle */
    volatile float gain;     /* linear, relative to the player's engine */
    volatile float pitch;    /* playback ratio (Doppler, detune) */
    volatile float cutoff;   /* low-pass, Hz */
} AudioCarVoice;
extern AudioCarVoice g_carvoice[AUDIO_CARVOICES];
/* the car's audio row (the same table the player's turbo/shift use) */
int audio_car_row(const char *carname);
/* SOUND/ENGINE/CAR_xx_ENG_MB_SPU.abk for the AI engine slots slot[0..n)
   into banks 0..n-1 (call inside audio_hold). Returns banks loaded. */
int audio_load_ai_banks(const char *dataroot, const int *slot, int n);
/* IG_GLOBAL/TRAFFIC_MB.abk clip 0 (FX_TRAFFIC). Before audio_init. */
int audio_load_traffic_clip(const char *dataroot);
/* The other cars' sample effects (carsfx.c sets them once a frame): AI
 * turbo / nitrous / skids / stereo, traffic woosh / horn / stereo /
 * bottom-out. A voice plays clip `clip` of bank set `set`, looped or once;
 * bumping `trig` (re)starts it from the top after `delay` seconds; a
 * one-shot that ran out sets `done` (cleared by the next trig). gain is
 * linear, relative to the player's engine, like g_carvoice. */
enum { AFX_ENV = 1,     /* IG_GLOBAL/ENV_COMMON_MB clips 0..7 */
       AFX_WOOSH,       /* IG_GLOBAL/MB_Stitch_Whoosh clips 0..15 */
       AFX_COLL,        /* IG_GLOBAL/MB_Stich_Collision clips 0..93 (the ones asked for) */
       AFX_NOS,         /* SOUND/NOS/Nitrous_00 clips 0..2 */
       AFX_SKID,        /* SOUND/SKIDS/SKID_PAV_MB clips 0..4 */
       AFX_SCRAPE,      /* IG_GLOBAL/FX_Misc_MB clip 1 (FX_Scrape), clip 0 here */
       AFX_PTURBO,      /* the player's SOUND/TURBO bank (audio_load_turbo_bank), clips 0..3 */
       AFX_TURBO };     /* + n: AI turbo bank n (audio_load_ai_turbo_banks), clips 0..3 */
/* 0..199 the AI and traffic effects, 200.. the collisions (5 hits x 12
   stitch samples, 16 scrapes), 276/277 the player's turbo whine/blow-off */
#define AUDIO_FXVOICES 278
typedef struct {
    volatile int on;
    volatile int set, clip, loop;
    volatile unsigned trig;
    volatile float delay;
    volatile float gain, pitch;
    volatile int done;
} AudioFxVoice;
extern AudioFxVoice g_fxvoice[AUDIO_FXVOICES];
/* the global car-effect banks: ENV_COMMON, the woosh stitch bank,
   collision clips coll[0..ncoll) (the collision and bottom-out stitches')
   and the scrape loop. Before audio_init. Returns clips loaded. */
int audio_load_carfx_banks(const char *dataroot, const int *coll, int ncoll);
/* the AI racers' SOUND/TURBO banks (0x7f3338 index each, at most
   CARSFX_AI_MAX distinct) into sets AFX_TURBO + 0..n-1 (inside audio_hold) */
int audio_load_ai_turbo_banks(const char *dataroot, const int *tbank, int n);
/* the turbo bank index (0x7f3338) of an audio row at an upgrade level
   (0x7f2238 record +8; the AI read level 0) */
int audio_row_turbo(int row);
/* One impact, strength 0..1 (from the car's sudden velocity change). */
void audio_impact(float strength);

/* Pause the mixer around a sound reload: audio_hold(1) returns once the
 * audio thread is outputting silence and touching no engine/clip data;
 * audio_hold(0) resumes. No-op before audio_init. */
void audio_hold(int on);
/* front-end sounds: SoundMgr 0x458520 ids (< 0x17 FE_COMMON_MB, else FE_MB id - 0x17) */
int  audio_load_fe_banks(const char *dataroot);
void audio_fe_play(int id);
/* The Audio options (0x83aa28.., applied by SoundMgr FUN_00469050), 0..1:
   master on everything; engine on the cars' engines; tire on the skids;
   sfx on the other effects; music on the songs (the caller passes the
   front-end or in-game music level). stereo 0 = Mono (0x83aa50). How
   0x469050 maps them onto its mixer channels is not decoded: this split
   is the port's. */
void audio_set_levels(float master, float sfx, float engine, float tire, float music, int stereo);
/* the generic control's sounds (the 0x837fe0 table, type 0 = GENERIC; filled by 0x50cf40) */
enum { FE_SND_UP = 0, FE_SND_DOWN = 1, FE_SND_LEFT = 2, FE_SND_RIGHT = 3, FE_SND_SELECT = 4,
       FE_SND_BACK = 5, FE_SND_START = 6, FE_SND_WRONG = 7, FE_SND_DIALOG = 10,
       FE_SND_SLIDER_LEFT = 8, FE_SND_SLIDER_RIGHT = 9 };
/* the in-game pause: only the front-end sounds and the music sound */
void audio_pause_game(int on);

/* Open + start the synth device. Returns 0 if unavailable (engine runs silent). */
SDL_AudioDeviceID audio_init(void);
/* Share of one CPU core the mixer used since the last call (percent);
 * resets the counters. Vita only (0 elsewhere). */
float audio_cost_pct(void);
void audio_selftest(void);   /* asserts the band-crossfade math */

#endif
