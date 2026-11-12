/* garage_line.c -- one garage.txt car line (see garage_line.h; moved out
 * of main.c so the PC converter's save import writes the same lines). */
#include "garage_line.h"
#include <stdio.h>

void garage_format_line(char *b, size_t cap, const char *car, const N2CarConfig *cfg, const char *rim, int rim_style) {
    size_t n = 0;
#define GF(...) do { if (n < cap) { int k_ = snprintf(b + n, cap - n, __VA_ARGS__); if (k_ > 0) n += (size_t)k_; } } while (0)
    if (cap) b[0] = 0;
    GF("%s %d", car, cfg->body_kit);
    for (int i = 0; i < N2_PART_COUNT; i++) GF(" %d", cfg->parts[i]);
    if (rim && rim[0]) GF(" RIM %s %d", rim, rim_style);
    GF(" PAINT");
    for (int i = 0; i < N2_PAINT_COUNT; i++) GF(" %x", cfg->paint[i]);
    GF(" PERF ");
    for (int i = 0; i < N2_PERF_PARTS; i += 4)
        GF("%x", (cfg->perf[i] ? 1 : 0) | (cfg->perf[i + 1] ? 2 : 0) | (cfg->perf[i + 2] ? 4 : 0) | (cfg->perf[i + 3] ? 8 : 0));
    GF(" VINYL");
    for (int i = 0; i < 4; i++) GF(" %x", cfg->vinyl[i]);
    for (int i = 0; i < 16; i++) GF(" %x", cfg->vinyl_col[i / 4][i % 4]);
    GF(" DECAL");
    for (int i = 0; i < 7; i++) GF(" %x", cfg->decal_layout[i]);
    for (int i = 0; i < 56; i++) GF(" %x", cfg->decal[i / 8][i % 8]);
    GF(" TINT %x", cfg->tint);
    GF(" BULB %x", cfg->bulb);
    GF(" CARBON %x", cfg->carbon);
    GF(" NEON");
    for (int i = 0; i < 8; i++) GF(" %x", cfg->neon[i / 2][i % 2]);
    GF(" HUD %x %x %x %x", cfg->hud, cfg->hud_col[0], cfg->hud_col[1], cfg->hud_col[2]);
    GF(" SPEC %x %x %x", cfg->purge, cfg->hydraulics, cfg->doors);
    GF(" ICE");
    for (int i = 0; i < 12; i++) GF(" %x", cfg->audio_comp[i]);
    {   unsigned jm = 0;
        for (int i = 0; i < 10; i++) jm |= cfg->junk[i] ? 1u << i : 0u;
        GF(" JUNK %x", jm); }
#undef GF
    if (n >= cap && cap) printf("garage: %s's line truncated at %u bytes\n", car, (unsigned)cap);
}
