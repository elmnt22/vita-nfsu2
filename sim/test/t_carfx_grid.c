/* Light-cache regression check. Includes the implementation to compare
   indexed queries with its existing full-scan fallback. */
#define SDL_MAIN_HANDLED
#include "../../src/carfx.c"
#include <assert.h>

static void take_builds(unsigned expected) {
    unsigned builds;
    uint64_t ticks;
    carfx_light_grid_stats(&builds, &ticks);
    assert(builds == expected);
}

static void compare_full_scan(CarFxLight *lights, int count, uint32_t revision) {
    const float forward[3] = { 1.0f, 0.0f, 0.0f };
    const float positions[][3] = {
        { 0, 0, 2 }, { 30, 10, 2 }, { -80, 40, 2 }, { 1000, 1000, 2 }
    };
    for (unsigned i = 0; i < sizeof positions / sizeof positions[0]; i++) {
        float indexed[10][4], scanned[10][4];
        int ni = carfx_sh_revision(indexed, positions[i], forward,
                                  lights, count, sizeof *lights, revision);
        assert(g_lg.ok);
        g_lg.ok = 0; /* existing fallback scans every light in source order */
        int ns = carfx_sh_revision(scanned, positions[i], forward,
                                  lights, count, sizeof *lights, revision);
        g_lg.ok = 1;
        assert(ni == ns);
        assert(!memcmp(indexed, scanned, sizeof indexed));
    }
}

int main(void) {
    CarFxLight active[24];
    for (int i = 0; i < 24; i++) {
        active[i] = (CarFxLight){
            { (float)(i % 6) * 12.0f - 30.0f, (float)(i / 6) * 15.0f - 20.0f, 5.0f },
            5.0f, 120.0f, 0xff204080u + (unsigned)i
        };
    }
    uint32_t revision = carfx_new_light_revision();
    compare_full_scan(active, 24, revision);
    take_builds(1);

    /* Background lists and legacy notifications leave this resident cached. */
    for (int i = 0; i < 20; i++) {
        assert(carfx_new_light_revision() != revision);
        carfx_lights_changed();
        compare_full_scan(active, 24, revision);
    }
    take_builds(0);

    /* Same address and length, different contents. */
    active[0].pos[0] = 300.0f;
    active[0].r_out = 8.0f;
    revision = carfx_new_light_revision();
    compare_full_scan(active, 24, revision);
    take_builds(1);

    /* Switching to a different list and back rebuilds each selection. */
    CarFxLight other[1] = { { { 10, 20, 4 }, 2, 60, 0xffaabbccu } };
    compare_full_scan(other, 1, carfx_new_light_revision());
    compare_full_scan(active, 24, revision);
    take_builds(2);

    /* Legacy callers still receive their requested invalidation. */
    const float position[3] = { 0, 0, 2 }, forward[3] = { 1, 0, 0 };
    float sh[10][4];
    carfx_sh(sh, position, forward, active, 24, sizeof *active);
    take_builds(1);
    carfx_lights_changed();
    carfx_sh(sh, position, forward, active, 24, sizeof *active);
    take_builds(1);

    compare_full_scan(other, 0, carfx_new_light_revision());
    take_builds(1);
    puts("carfx grid: full-scan lighting matches; resident revisions and legacy invalidation pass");
    return 0;
}
