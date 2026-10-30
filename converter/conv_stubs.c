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
STUB(career_switch_car) STUB(career_url_purse) STUB(career_wc_unique_chosen) STUB(carfx_lights_changed) 
STUB(cstage_sponsor_choice) STUB(cstage_sponsor_done) STUB(cstage_url_done) STUB(cstage_world_done) 
STUB(decal_at) STUB(decal_by_name) STUB(decal_count) STUB(hs_best_drift) STUB(hs_best_time) STUB(hud_num) 
STUB(hud_pad_glyph) STUB(hud_time_str) STUB(mplay_close) STUB(mplay_open) STUB(mplay_tex) STUB(mplay_update) 
STUB(music_album) STUB(music_artist) STUB(music_default_playlist) STUB(music_play_song) 
STUB(music_set_playlist) STUB(music_title) STUB(options_ctl_preset) STUB(options_port_str) STUB(options_save) 
STUB(perf_build) STUB(perf_car_stats) STUB(perf_cat_level) STUB(perf_clear) STUB(perf_fit_level) 
STUB(perf_fit_part) STUB(perf_fit_pkg) STUB(perf_init) STUB(perf_part_fitted) STUB(perf_part_worse) 
STUB(perf_pkg_fitted) STUB(perf_pkg_parts) STUB(perf_pkg_worse) STUB(perf_power) STUB(perf_ready) 
STUB(perf_set_part) STUB(perf_unfit_cat) STUB(profile_default) STUB(profile_delete) STUB(profile_dirty) 
STUB(profile_drop_snapshot) STUB(profile_exists) STUB(profile_file) STUB(profile_list) STUB(profile_load) 
STUB(profile_name) STUB(profile_named) STUB(profile_restore) STUB(profile_save) STUB(profile_set_name) 
STUB(profile_snapshot) STUB(smk_is_prop) STUB(vinyl_at) STUB(vinyl_count) STUB(vinyl_db_load) 
STUB(vinyl_find) 
long long g_career[4096], g_opt[512];
const char *const g_vb_name[64];
