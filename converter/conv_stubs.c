/* conv_stubs.c -- symbols the linked game objects reference only from their
 * drawing paths (car materials, the GL uniform shims). The conversion never
 * reaches them; should it ever, it stops with the name instead of drawing
 * through a GL that is not there. No headers on purpose: only the names
 * matter to the linker. */
void conv_fatal(const char *what);
#define STUB(name) void name(void) { conv_fatal(#name); }
STUB(carfx_default) STUB(carfx_loaded) STUB(carfx_material) STUB(carfx_paint) STUB(carfx_use_paint)
STUB(vita_add_variant) STUB(vita_alias_program) STUB(vita_set_dof)
STUB(vu_ColorMask) STUB(vu_Disable) STUB(vu_Enable) STUB(vu_GetBooleanv) STUB(vu_GetUniformfv)
STUB(vu_IsEnabled) STUB(vu_LinkProgram) STUB(vu_Uniform) STUB(vu_UseProgram) STUB(vu_select)

/* shop.c links in for its screens' cache (shop_build_cache); the rest of
 * the shop reaches career, profile, performance, music and HUD code the
 * converter does not carry. Data the shop reads: zeroed, never touched. */
STUB(airace_type_name) STUB(audio_fe_play) STUB(career_buy_part) STUB(career_buy_perf) 
STUB(career_car_by_handle) STUB(career_car_in_region) STUB(career_car_on_disc) STUB(career_car_slot) 
STUB(career_car_trade) STUB(career_car_unlocked) STUB(career_qr_car_unlocked) STUB(career_qr_track_unlocked) STUB(airace_type_of) STUB(career_choose_car) STUB(career_current_car) 
STUB(career_default) STUB(career_event) STUB(career_first_empty_slot) STUB(career_first_movie_done) 
STUB(career_free_roam_start) STUB(career_has_completed) STUB(career_lot_cars) STUB(career_nevents) 
STUB(career_nshowcases) STUB(career_num_sponsors_chosen) STUB(career_owns_part) STUB(career_owns_perf) 
STUB(career_record) STUB(career_shop_by_handle) STUB(career_shop_enterable) STUB(career_showcase) 
STUB(career_slot_counts) STUB(career_slot_handle) STUB(career_spend) STUB(career_stage_cleared) 
STUB(career_start) STUB(career_stock_handle) STUB(career_stock_type) STUB(career_string) 
STUB(career_switch_car) STUB(career_url_purse) STUB(career_wc_unique_chosen) 
STUB(cstage_sponsor_choice) STUB(cstage_sponsor_done) STUB(cstage_url_done) STUB(cstage_world_done) 
STUB(hs_best_drift) STUB(hs_best_time) STUB(hud_num) 
STUB(hud_pad_glyph) STUB(hud_time_str) STUB(mplay_close) STUB(mplay_open) STUB(mplay_tex) STUB(mplay_update) 
STUB(music_album) STUB(music_artist) STUB(music_default_playlist) STUB(music_play_song) 
STUB(music_set_playlist) STUB(music_title) 
STUB(profile_default) STUB(profile_delete) STUB(profile_dirty) 
STUB(profile_drop_snapshot) STUB(profile_exists) STUB(profile_file) STUB(profile_list) STUB(profile_load) 
STUB(profile_name) STUB(profile_named) STUB(profile_restore) STUB(profile_save) STUB(profile_set_name) 
STUB(profile_snapshot) STUB(smk_is_prop) STUB(profile_xf_city) STUB(profile_xf_enter) STUB(profile_xf_leave) STUB(profile_xf_save)
STUB(airace_ntypes) STUB(career_any_owned) STUB(career_completed_career) STUB(career_decal_brand_unlocked)
STUB(career_sim_find_shops) STUB(career_speech_fe) STUB(career_speech_sponsor) STUB(career_speech_sponsor_done)
STUB(career_speech_uniques) STUB(garage_tt_request)
/* perfparts.c's car stats (the Performance screens) */
STUB(n2sim_accel_curve) STUB(n2sim_accel_rating) STUB(n2sim_create) STUB(n2sim_destroy) STUB(n2sim_param)

long long g_career[4096];   /* options.c, perfparts.c, vinyl.c and pcsave.c link in for the PC save import */

/* world.c bumps the car lighting's light-list generation when a region's
 * lights load (carfx.c); no cars here, so nothing to tell (11-09: the old
 * aborting stub stopped the conversion) */
void carfx_lights_changed(void) { }

/* 11-12: carfx.c's light-list generation (world.c stamps a region's lights
 * with it); no car lighting here, a counter is enough */
#include <stdint.h>
#include <string.h>
uint32_t carfx_new_light_revision(void) { static uint32_t r; return ++r; }

/* 11-12: nfsu2.h n2_lang() asks the Vita's system language (built __vita__
 * like the simulator); -1 = unknown -> English, OPENUG2_LANG still overrides */
int vita_system_lang(void) { return -1; }

/* 11-13: render.c's simulator probe tag (test aid, built with the sim prefix) */
uint32_t g_sim_probe_tag;

/* 11-13: the widescreen PC_Loading art (hud.c); no loading screens here */
unsigned int hud_wide_loading_tex(void) { return 0; }
int hud_is_wide_loading_res(const char *name) { (void)name; return 0; }
float hud_wide_loading_pin(float asp) { (void)asp; return 0.0f; }

/* 11-15: added since the last Prep Tool build (save note, skyline, the
   Vita copy thread); none of them run during a conversion */
struct N2Tex;
void savenote_cancel(void) {}
void skyline_texavg_note(uint32_t key, const struct N2Tex *t) { (void)key; (void)t; }
void vita_copy_push(void *dst, const void *src, unsigned n) { memcpy(dst, src, n); }
int vita_copy_idle(void) { return 1; }
void vita_copy_wait(void) {}
void vita_copy_stats(unsigned long long *us, unsigned long long *bytes) { *us = 0; *bytes = 0; }
void vita_step(const char *label) { (void)label; }   /* the hang recorder (sdl_vita.c) */

/* the expansion cities (xpcity.c) are game-only: the PC tools see Bayview alone */
int g_xpcity_pick;
int xpcity_is_city(const char *track) { (void)track; return 0; }
int xpcity_installed(int pick) { return pick == 0; }
