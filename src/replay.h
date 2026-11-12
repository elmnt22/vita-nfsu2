/* replay.h -- the retail NFSU2 instant replay (step 8b), ported from
 * SPEED2.EXE (v1.2) with the GameCube Speed.elf symbols.
 *
 * Retail: ReplayManager (pReplayManager 0x8650b8) records from
 * MarkTheBeginningOfTime (0x57aad0, at GO) to MarkTheEndOfTime (+0x80, the
 * race's POSTRACE state) and plays back by restoring a snapshot and
 * re-simulating its input log. Its snapshots are the replay's markers:
 * INSTANT_REPLAY (every 300 ticks, the last 2 kept), LAP_SNAPSHOT (every
 * +0xc4 = 15 s of race clock, at most 5, the interval doubling and every
 * second one dropped when full) and THE_PRESENT (taken when the replay
 * opens, restored when it closes). The screen is ReplayMenu
 * (UI_ReplayControl.fng, ctor 0x4c0aa0, notifications 0x4d7bd0); the
 * cameras are Player::CycleReplayCameraType 0x5fafd0 (types 0..5, POVs
 * 0x802130) and UpdateIceCameras 0x5fb050 (type 0's cuts).
 *
 * GUESS: the port's simulation is not deterministic and has no snapshots, so
 * the recording is kinematic: every car's pose, wheels and engine state (and
 * the smokeables) at 30 Hz, interpolated at the 60 Hz playback ticks. The
 * markers, controls, UI and cameras follow the retail rules on top of it.
 */
#ifndef OPENUG2_REPLAY_H
#define OPENUG2_REPLAY_H
#include <stdint.h>
#include "render.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RP_SLOTS   26          /* 0 the player, 1..8 opponents, 9..24 traffic */
#define RP_OPP0    1
#define RP_TR0     9
#define RP_PROPS   32          /* SmokeableSlotPool */

typedef struct {
    unsigned char on;          /* in the world (recorded this frame) */
    signed char vis;           /* traffic: its visual (g_rt.tvi), else -1 */
    float pos[3], rot[9], vel[3];
    float speed, rpm, rpm_tach, throttle;
    int   gear, nos_on, engaged, wheels_down;
    float wheel_w[4], wheel_steer[2], wheel_comp[4], wheel_slip[4];
    int   wheel_contact[4], wheel_surface[4];
    /* what retail's re-simulation would give the effects again: the lamps
       (RPL_*), the tyre marks (0x59dbb0's strength / width), the sliding
       patches' speeds (the tyre smoke, +0x100) and the bump-stop sum (the
       bottom-out sparks) -- full form only, except the lamps */
    unsigned char lights;
    float wheel_skid[4], wheel_skid_w[4], wheel_slipv[4][2], bottom;
    /* what its re-simulation gives the car's sounds again: the skid
       controller's channels (SFXCTL_Skid, 0..1) and RPS_* bits */
    float skid3[3];
    unsigned char snd;
} RpCar;
#define RPS_SHIFTING 1             /* a gear change under way (n2sim_shifting) */
#define RPL_BRAKE  1               /* brake lamps: car +0x944 (brake > 0.05), the player's tail rule */
#define RPL_HEAD   2               /* headlights on (the player's beam keys / night) */
#define RPL_HIGH   4               /* high beam held */
#define RPL_FLASH  8               /* flashing (the racer invitation) */
#define RPL_TAIL   16              /* the player's tail lamp meshes lit by braking */

/* a car contact's effect (0x5a9620 -> acid_contact): kept with its tick and
   fired again when the playback passes it */
typedef struct {
    int   surface, has_vb;
    float pt[3], imp[3], slide, va[3], vb[3];
} RpContact;
/* recording: one contact effect in the tick being recorded */
void rp_contact(const RpContact *c);
/* playback: called for each recorded contact as its tick plays */
extern void (*rp_contact_fx)(const RpContact *c);
/* the collision sounds' calls (opaque to the replay: the caller's own
   bytes, at most RP_SND_LEN): kept with their tick while recording, given
   back to rp_sound_fx as the playback passes that tick */
#define RP_SND_LEN 512
void rp_sound(const void *ev, int len);
extern void (*rp_sound_fx)(const void *ev, int len);
/* a snapshot restore (a marker, or THE_PRESENT at the exit): the world's
   transient state (particles, sound pairs, skid pens) goes, as retail's
   RestoreSnapshot rebuilds it */
extern void (*rp_restore_fx)(void);

typedef struct {
    float t;                   /* the race clock, s (0x7fb718) */
    int   lap;                 /* the HUD car's laps done (racer +9) */
    float laptime;             /* its current lap's time, s */
    RpCar car[RP_SLOTS];
} RpFrame;

/* a new race / the race gone: everything recorded is dropped */
void rp_reset(void);
/* MarkTheBeginningOfTime (0x57aad0): recording starts at race clock t.
   0 = no memory for it (the replay stays off). */
int  rp_begin(float t);
/* MarkTheEndOfTime (+0x80 = 1): RCSTATE_POSTRACE (TheRaceHasFinished) */
void rp_end_of_time(void);
int  rp_recording(void);
/* BeginFrame/EndFrame while recording: one call per world tick (60 Hz)
   that the race world stepped; f = the world after the step. Takes the
   markers (MaybeTakeInstantReplaySnapshot / MaybeTakeLapSnapshot) and the
   smokeables' state itself. */
void rp_record(const RpFrame *f);
/* the snapshot counts: +0xbc (the post-race Replay option) and +0xb8
   (IsInstantReplayAvailable, the pause menu's Replay) */
int  rp_lap_markers(void);
int  rp_instant_markers(void);

/* ---- the replay screen (ReplayMenu) --------------------------------- */
enum { RP_FROM_POSTRACE = 1, RP_FROM_PAUSE = 2 };
enum { RP_K_ACCEPT, RP_K_BACK, RP_K_START, RP_K_LTRIG, RP_K_RTRIG, RP_K_BUTTON0, RP_K_BUTTON1 };
/* opens UI_ReplayControl.fng; the caller keeps the live world (THE_PRESENT)
   until rp_closed() reports the exit. 0 = not opened. */
int  rp_open(const char *dataroot, int from, int drag_or_drift, int laps);
int  rp_up(void);
/* a pad event while the screen is up (0x4d7bd0) */
void rp_key(int k);
/* once per world tick while up: BeginFrame/EndFrame of playback. 1 = the
   playback tick moved (apply rp_sample). */
int  rp_tick(void);
/* the world at the playback tick (interpolated); also sets the smokeables'
   replay state. 0 = nothing to show. */
int  rp_sample(RpFrame *out);
/* once per drawn frame: the loading wait (0.5 s), the type 0 camera's cuts
   (UpdateIceCameras), the scroll bar / lap / time widgets. cur_setting =
   the camera's setting now, speed = the HUD car's speed m/s. */
void rp_frame(float real_dt, int cur_setting, float speed);
/* the camera the replay wants: bit 0 = set chasecam setting *setting (0..4,
   -1 = the player's own: RestoreCameraMover), bit 1 = the camera jumps
   (DefaultDriveCamera(1) after a snapshot restore) */
int  rp_camera(int *setting);
/* the playback rate for the camera's time step: 0 paused / loading, 0.25
   slow motion, 1 */
float rp_rate(void);
/* the screen closed (Exit 0x4a50e0): returns its RP_FROM_* once, 0 else */
int  rp_closed(void);
void rp_draw(const RProg *rp, GpuMesh *quad, int width, int height, int dt_ms);
/* the replay is the world on screen (open, past the first load) */
int  rp_playing(void);

#ifdef __cplusplus
}
#endif
#endif
