/* rain.h -- the original game's rain (SPEED2.EXE Rain / OnScreenRain /
 * DCORain, GC Speed.elf names): when it rains (the track's "Rain" table and
 * IsRainingAt's clock wave), the 400-drop streak system in the space of the
 * viewing car (Rain::Init 0x613050, Update 0x61e700, Wind 0x6136b0, Render
 * 0x613a70, UpdateAndRender 0x61f120), the drops on the screen (OnScreenRain
 * Update 0x6140f0, render 0x5c7ab0 with rain_drop.fx), the "Night Raining
 * Fog" blend (FogQuery::CalculateFogInfo 0x614300) and the rain's screen tint
 * (ScreenEffectDB slot 2, drawn by 0x5c7860). */
#ifndef OPENUG2_RAIN_H
#define OPENUG2_RAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* GL context needed. glob: GLOBALB.BUN's bytes (RAINDROP, RAINDROPPC), not
   kept. Returns 1 when the drop texture loaded. */
int  rain_init(const unsigned char *glob, long globlen);
void rain_free(void);

/* A track's parameter tables from <troot>/<stem>.BUN (chunk 0x8003B600:
   "Rain", "Wind", "Night Raining Fog", "Fog Blend Distance"), and the new
   World's state: ResetWorldTime (0x576cf0, clock 1 s) and SetRainBase
   0x61e0f0 (Init(type 5): every drop idle). Returns 1 with a Rain table. */
int  rain_load_track(const char *troot, const char *track);
void rain_world_reset(void);   /* a new World: clock 1 s, rain object re-initialised */

/* One drawn frame of view 1 (UpdateAndRender 0x61f120). */
typedef struct {
    int   in_game;        /* game state 6 with the viewing car (else nothing runs) */
    int   paused;         /* GameFlowManager::IsPaused */
    float dt;             /* the frame's time (0x865154) */
    int   steps;          /* 1/60 s frames this drawn frame stands for (the drops step once a frame) */
    float car_pos[3];     /* the viewing car (+0x50) */
    float car_R[9];       /* its rows: forward, left, up (the body matrix) */
    float eye[3];         /* the camera (+0x40) */
    float fwd[3];         /* its direction (+0x50) */
    int   test_track;     /* 0x89e7e6: a Test Track run rains nowhere */
    int   cubic_cam;      /* car +0x18 (Player) -> +0x238 (its CameraMover) -> +0xc (CameraMoverTypes)
                             == 1: the CubicCameraMover drives the view (0x61e8a1) */
    /* TrackPathManager::FindZone types 0x20 / 0x40 holding p (x, y) with a
       height (+0x14) above p[2]; NULL = none */
    int (*cover)(const float p[3]);
} RainFrame;
void  rain_update(const RainFrame *f);

/* Rain::Render (after the particles): V view, PV projection x view
   (column-major GL). Depth test on, writes off, blending by the texture.
   Returns the quads drawn. */
/* the world pass's current fog (colour, density, k, floor) for the streaks */
void  rain_set_world_fog(const float col[3], float density, float k, float floor_);
int   rain_draw(const float PV[16]);
/* OnScreenRain's render (0x5c7ab0, before the screen flash): copies the
   bound framebuffer and draws the drops through rain_drop.fx. Returns the
   drops drawn. */
int   rain_draw_screen(void);

float rain_intensity(void);              /* Rain +0x1f0, 0..1 */
/* 1 while rain_draw / rain_draw_screen have anything to draw (perf: the
   caller skips the pass setup when dry) */
int   rain_has_draws(void);
/* +0x509c: eases to 1 while it rains (0.05 a frame) and back to 0 when dry
   (0.0015 a frame); 1 after a world load and with rain off in the options.
   Only the flare reflection stretch reads it (0x490957: 1.2 + 8.8 x this). */
float rain_wetness(void);
/* GetDesiredRainIntensity 0x61e380 (Car::GetRainIntensity 0x5ed5d0) */
float rain_desired_at(float x, float y);
/* GetAmbientZone 0x46b330 in game: the track's "Ambience" and "Volumes"
   parameter maps at (x, y); *zone / *vol_cdb (hundredths of a dB) are left
   as they were when a map is missing. Returns 1 when the Ambience map is there. */
int   rain_ambience_at(float x, float y, int *zone, int *vol_cdb);
/* the "Night Raining Fog" values (8) at the camera (x, y), blended by
   distance (Fog Blend Distance, 50 m) -- 0 without the table */
int   rain_fog_values(float x, float y, float out[8]);
/* the rain-mixed fog colour bytes (FUN_00613020 from CalculateFogInfo) */
void  rain_set_fog_colour(int c0, int c1, int c2);
/* ScreenEffectDB slot 2: the multiply the flash pass applies (0x5c7860)
   while the rain's effect is live (intensity >= 0.01). Returns 0 when off. */
int   rain_tint(float mul[3]);

#ifdef __cplusplus
}
#endif
#endif
