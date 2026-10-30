/* tuning.h -- the crib's Test Track: Performance Tuning / Dyno (TEST_TRACK.md).
 *
 * Physics side (CustomPhysicsSpec, PC 0x59ae40 .. 0x5b4780): the per-car
 * CustomPhysicsTunings (0x29 bytes: types 0-22, ECU 9 points, turbo 9
 * points; byte 100 = stock) applied to a built physics record after the
 * parts rebuild (RebuildPhysicsInfo 0x5b4dc0 -> RebuildCustomTunings
 * 0x5b4780), with the availability table GLOBALB 0x34a1a.
 *
 * Front-end side: the nine FNGs of GLOBAL/InGamePerformanceTuning.bun
 * driven as in-game pause screens (UI_PerformanceTuning_Setting / _Master /
 * _Sliders / _Drivetrain / _Graph / _NOS, UI_PerformanceDyno_MAIN / _Chart /
 * _Results). main.c owns the track switching and the car; tuning.c owns the
 * screens, the session's tunings and the dyno numbers. */
#ifndef OPENUG2_TUNING_H
#define OPENUG2_TUNING_H

#include <stdint.h>
#include "perfparts.h"
#include "render.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TUNE_BYTES 0x29
#define TUNE_NSET  5           /* eTuningSetting: Circuit, Drag, Drift, Street X, URL */

/* GLOBALB 0x34a1a (62 x 0x18). Returns the records found. */
int  tune_init(const unsigned char *globalb, long len);
/* RebuildCustomTunings (PC 0x5b4780) on `built` (N2_REC_SIZE, already the
   parts rebuild of `raw`), with the car's fitted parts and its tunings
   (NULL = stock, all 100) */
void tune_apply(unsigned char *built, const unsigned char *raw, const PerfParts *pp, const uint8_t *tune);
/* eTuningSetting a race type uses (SetupPlayerCarsAndStuff's argument;
   qr: the QR_* type) */
int  tune_setting_for_race(int qr_type);

/* ---- the session (CarTuneManager) ---- */
enum { TT_ACT_NONE, TT_ACT_REBUILD, TT_ACT_TEST_RUN, TT_ACT_RETURN_DYNO, TT_ACT_RESTART,
       TT_ACT_RESUME, TT_ACT_OPTIONS, TT_ACT_SAVE, TT_ACT_QUIT };
/* keys: main.c's pause keys (PK_*) */
enum { TT_K_NONE, TT_K_OK, TT_K_BACK, TT_K_LEFT, TT_K_RIGHT, TT_K_START, TT_K_UP, TT_K_DOWN, TT_K_RESET };

/* BeginCarTuning(entry): the car's record (raw), its parts, its stored
   tunings (copied; TakeControl) */
void tun_begin(int entry, const unsigned char *raw, const PerfParts *pp, const uint8_t (*car_tune)[TUNE_BYTES]);
int  tun_active(void);
int  tun_setting(void);                 /* -1 = none chosen yet */
const uint8_t *tun_temp(void);          /* the tunings the car drives now */
/* the quit's "save your profile now?" answered Yes */
int  tun_quit_save(void);
/* the car is on the dyno (1) or on a test run (0) */
int  tun_on_dyno(void);
void tun_set_on_dyno(int on);
/* the dyno's FEng screen: Setting when no setting yet, else MAIN */
void tun_open_dyno(const char *dataroot);
/* the test run's pause (UI_PerformanceDyno_MAIN, SetupTuningPause) */
void tun_open_pause(const char *dataroot);
int  tun_screen_up(void);
int  tun_key(int key);                  /* returns TT_ACT_* for main.c */
void tun_tick(float dt);
void tun_draw(const RProg *rp, GpuMesh *quad, int W, int H, int dms);
void tun_close_screens(void);
/* HaveAnySettingsChangedFromDatabase */
int  tun_changed(void);
/* CommitCustomTuningsToFEDatabase: the stored set back into the car */
void tun_commit(uint8_t (*car_tune)[TUNE_BYTES]);
void tun_end(void);
/* the test track of the current setting (StartCarTuningRace): event id,
   QR_* type, laps */
void tun_test_event(int *event, int *qr_type, int *laps);

/* ---- the dyno run (DynoManager, PC 0x56d1a0..): main.c runs it every
   physics tick while tun_dyno_live(); the car is held on the rollers by
   n2sim_set_dyno (ResolveDynoForces) between TT_DY_START and TT_DY_END ---- */
typedef struct { int on; float throttle, brake; int gear_req; } TunDynoCtl;
enum { TT_DY_START = 1, TT_DY_END = 2 };
int  tun_dyno_live(void);
/* dt: the tick; rpm / gear (0 R, 1 N, 2 1st..) / forward speed m/s / theoretical
   top speed m/s of the car; o: the driver's controls while o->on */
int  tun_dyno_frame(float dt, float rpm, int gear, float speed, float vtop, TunDynoCtl *o);

#ifdef __cplusplus
}
#endif
#endif
